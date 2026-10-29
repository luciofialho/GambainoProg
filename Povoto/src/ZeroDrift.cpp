#include <Arduino.h>
#include <math.h>
#include <esp_heap_caps.h>
#include <memory>
#include "ZeroDrift.h"
#include "PressureControl.h"
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "GambainoCommon.h"
#include "PovotoTasks.h"
#include "TemperatureControl.h"
#include "CalibrationEvents.h"
#include "BuildId.h"
#include "datalog.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

unsigned long int NTPEpoch(); // IOTK_NTP (not #included: see AtmosphericPressure.cpp)

// docs/zero-drift.md
static constexpr uint16_t DRIFT_MAX_SAMPLES = 10080;      // 7 days at one sample per minute
static constexpr uint16_t DRIFT_MIN_FIT_SAMPLES = 10;
static constexpr float DRIFT_MIN_SPAN = 0.5f;             // C of temperature span for a fit
static unsigned long sampleMs() { return debugging ? 5000UL : 60000UL; }

struct DriftSample {
  uint32_t seconds;  // since the start
  float current, currentMin, currentMax; // mA, mean, minimum and maximum of the readings of the interval
  float ta, tg;      // C: ambient, fermenter gas (NAN when missing)
};

static bool running = false;
static DriftSample *samples = nullptr; // PSRAM
static uint16_t sampleCount = 0;
static unsigned long startMillis = 0, lastReading = 0, intervalIndex = 0;
static uint32_t runEpoch = 0;
static double accI = 0, accTa = 0, accTg = 0;
static float minI = NAN, maxI = NAN;
static uint16_t accN = 0, accA = 0, accG = 0;
static float lineP0 = NAN, lineB = NAN, lineBBar = NAN; // the pressure line when the test started
static portMUX_TYPE driftLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool stopRequested = false;
static char statusText[120] = "Not run";

bool zeroDriftActive() { return running; }
bool zeroDriftHasData() { return sampleCount > 0; }

static float environmentCelsius() {
  return isfinite(environmentTemp) && environmentTemp != NOTaTEMP && environmentTemp > -20.0f && environmentTemp < 60.0f ? environmentTemp : NAN;
}

static float gasCelsius() {
  const float t = gasTemperature();
  return isfinite(t) && t > -20.0f && t < 60.0f ? t : NAN;
}

// The stored line without the drift compensation: the test measures the drift itself.
static float readingMbar(float current) { return 1000.0f * convertCurrentToPressureAt(current, NAN); }

// Least squares of the reading (mbar) against the ambient temperature (C), over the samples that have it.
static ZeroDriftFit fit(uint16_t count) {
  ZeroDriftFit r = {};
  double n = 0, sx = 0, sy = 0;
  float lo = NAN, hi = NAN;
  for (uint16_t i = 0; i < count; ++i) {
    const float t = samples[i].ta;
    if (!isfinite(t) || !(samples[i].current > 0.0f)) continue;
    n += 1; sx += t; sy += readingMbar(samples[i].current);
    lo = isfinite(lo) ? min(lo, t) : t;
    hi = isfinite(hi) ? max(hi, t) : t;
  }
  r.samples = (uint16_t)n;
  r.tMin = lo;
  r.tMax = hi;
  if (n < DRIFT_MIN_FIT_SAMPLES || !(hi - lo >= DRIFT_MIN_SPAN)) return r;
  const double mx = sx / n, my = sy / n;
  double sxx = 0, sxy = 0, syy = 0;
  for (uint16_t i = 0; i < count; ++i) {
    const float t = samples[i].ta;
    if (!isfinite(t) || !(samples[i].current > 0.0f)) continue;
    const double dx = t - mx, dy = readingMbar(samples[i].current) - my;
    sxx += dx * dx; sxy += dx * dy; syy += dy * dy;
  }
  if (!(sxx > 0.0)) return r;
  const double b = sxy / sxx, a = my - b * mx;
  const double rss = max(0.0, syy - b * sxy);
  const double sd = sqrt(rss / (n - 2.0));
  r.valid = true;
  r.slope = float(b);
  r.slopeError = float(sd / sqrt(sxx));
  r.zeroTemp = fabs(b) > 1e-6 ? float(-a / b) : NAN;
  r.r2 = syy > 0.0 ? float(sxy * sxy / (sxx * syy)) : 0.0f;
  r.residual = float(sd);
  r.spread = float(b * (hi - lo));
  return r;
}

ZeroDriftResult getZeroDriftResult() {
  ZeroDriftResult r = {};
  uint16_t count;
  portENTER_CRITICAL(&driftLock);
  count = sampleCount;
  portEXIT_CRITICAL(&driftLock);
  r.running = running;
  r.available = count > 0;
  r.samples = count;
  r.capacity = DRIFT_MAX_SAMPLES;
  r.intervalSeconds = (uint16_t)(sampleMs() / 1000UL);
  r.ambient = r.ambientMin = r.ambientMax = NAN;
  if (!count) return r;
  for (uint16_t i = 0; i < count; ++i) {
    const DriftSample &s = samples[i];
    if (isfinite(s.ta)) {
      r.ambientMin = isfinite(r.ambientMin) ? min(r.ambientMin, s.ta) : s.ta;
      r.ambientMax = isfinite(r.ambientMax) ? max(r.ambientMax, s.ta) : s.ta;
    }
  }
  const DriftSample &last = samples[count - 1];
  r.elapsedSeconds = last.seconds;
  r.ambient = last.ta;
  r.current = last.current;
  r.pressure = readingMbar(last.current);
  r.byAmbient = fit(count);
  return r;
}

String zeroDriftStatusText() {
  if (!running) return String(statusText);
  const unsigned long elapsed = (millis() - startMillis) / 1000UL;
  char text[120];
  snprintf(text, sizeof(text), "Running: %lu h %02lu min, %u samples, reading %.1f mbar", elapsed / 3600UL, (elapsed / 60UL) % 60UL,
           sampleCount, readingMbar(currentReading));
  return String(text);
}

static void showProgress(bool force) {
  static unsigned long lastShown = 0;
  const unsigned long now = millis();
  if (!force && now - lastShown < 1000UL) return;
  lastShown = now;
  char line1[48], line2[64], line3[64];
  if (running) {
    const unsigned long elapsed = (now - startMillis) / 1000UL;
    snprintf(line1, sizeof(line1), "Zero drift");
    snprintf(line2, sizeof(line2), "%lu h %02lu min", elapsed / 3600UL, (elapsed / 60UL) % 60UL);
    snprintf(line3, sizeof(line3), "%.1f mbar", readingMbar(currentReading));
  } else {
    snprintf(line1, sizeof(line1), "Zero drift: END");
    snprintf(line2, sizeof(line2), "%s", statusText);
    snprintf(line3, sizeof(line3), "Result on the Calibration page");
  }
  showCalibrationPanel(line1, line2, line3);
}

static void finish(const char *outcome) {
  running = false;
  const ZeroDriftResult r = getZeroDriftResult();
  if (r.available) {
    char details[260];
    int n = snprintf(details, sizeof(details), "%lu min, %u samples", (unsigned long)(r.elapsedSeconds / 60UL), r.samples);
    auto add = [&](const char *name, const ZeroDriftFit &f) {
      if (n > 0 && n < (int)sizeof(details) && f.valid)
        n += snprintf(details + n, sizeof(details) - n, "; %s %.1f to %.1f C: %.2f +-%.2f mbar/C, zero at %.1f C, sd %.2f mbar", name, f.tMin,
                      f.tMax, f.slope, f.slopeError, f.zeroTemp, f.residual);
    };
    add("ambient", r.byAmbient);
    if (strcmp(outcome, "Completed") && n > 0 && n < (int)sizeof(details)) snprintf(details + n, sizeof(details) - n, " (%s)", outcome);
    calibrationEventAppend("zero-drift", details);
    snprintf(statusText, sizeof(statusText), "%s", outcome);
  } else {
    snprintf(statusText, sizeof(statusText), "%s without data", outcome);
  }
  showProgress(true);
}

void stopZeroDrift() {
  if (running) stopRequested = true;
}

bool startZeroDrift(char *reason, size_t reasonSize) {
  const char *blocked = nullptr;
  if (SetPointData.mode != MODE_OFF) blocked = "Mode must be OFF";
  else if (taskWindowType != 0 || calibrationRoutineActive() || inTheMiddleOfRelief()) blocked = "Process in progress";
  if (!blocked && !samples) {
    samples = static_cast<DriftSample *>(heap_caps_malloc(sizeof(DriftSample) * DRIFT_MAX_SAMPLES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!samples) blocked = "Not enough PSRAM";
  }
  if (blocked) {
    snprintf(reason, reasonSize, "%s", blocked);
    return false;
  }
  portENTER_CRITICAL(&driftLock);
  sampleCount = 0;
  runEpoch = NTPEpoch();
  portEXIT_CRITICAL(&driftLock);
  lineP0 = FMTData.pressure0Current;
  lineB = FMTData.pressure1Current;
  lineBBar = FMTData.pressure1Bar;
  intervalIndex = 0;
  accI = accTa = accTg = 0;
  accN = accA = accG = 0;
  minI = maxI = NAN;
  lastReading = 0;
  routineValve(false); // de-energized: the fermenter stays closed from the expansion tank
  startMillis = millis();
  stopRequested = false;
  running = true;
  snprintf(statusText, sizeof(statusText), "Running");
  showProgress(true);
  return true;
}

void processZeroDrift() {
  if (!running) return;
  if (stopRequested) {
    stopRequested = false;
    finish("Stopped");
    return;
  }
  if (SetPointData.mode != MODE_OFF || taskWindowType != 0) {
    finish(SetPointData.mode != MODE_OFF ? "Stopped (mode is not OFF)" : "Stopped (a task started)");
    return;
  }
  const unsigned long now = millis();
  if (now - lastReading >= 1000UL) { // a reading of the filtered current each second goes into the mean of the interval
    lastReading = now;
    if (isfinite(currentReading) && currentReading > 0.0f) {
      accI += currentReading;
      minI = isfinite(minI) ? min(minI, currentReading) : currentReading;
      maxI = isfinite(maxI) ? max(maxI, currentReading) : currentReading;
      ++accN;
    }
    const float ta = environmentCelsius(), tg = gasCelsius();
    if (isfinite(ta)) { accTa += ta; ++accA; }
    if (isfinite(tg)) { accTg += tg; ++accG; }
  }
  const unsigned long elapsed = now - startMillis;
  if (elapsed >= (intervalIndex + 1) * sampleMs()) {
    ++intervalIndex;
    if (accN > 0) {
      DriftSample s;
      s.seconds = (uint32_t)(intervalIndex * sampleMs() / 1000UL);
      s.current = float(accI / accN);
      s.currentMin = minI;
      s.currentMax = maxI;
      s.ta = accA ? float(accTa / accA) : NAN;
      s.tg = accG ? float(accTg / accG) : NAN;
      portENTER_CRITICAL(&driftLock);
      samples[sampleCount] = s;
      ++sampleCount;
      portEXIT_CRITICAL(&driftLock);
    }
    accI = accTa = accTg = 0;
    accN = accA = accG = 0;
    minI = maxI = NAN;
    if (sampleCount >= DRIFT_MAX_SAMPLES) {
      finish("Completed");
      return;
    }
  }
  showProgress(false);
}

// Chunked CSV: the 10 080 samples of a full run do not fit in a String.
struct DriftCsvCursor {
  uint16_t count = 0, next = 0;
  bool headerSent = false;
  char line[200];
  size_t used = 0, position = 0;
};

void handleZeroDriftCSV(AsyncWebServerRequest *request) {
  auto cursor = std::make_shared<DriftCsvCursor>();
  uint32_t epoch;
  portENTER_CRITICAL(&driftLock);
  cursor->count = sampleCount;
  epoch = runEpoch;
  portEXIT_CRITICAL(&driftLock);
  const float p0 = lineP0, pb = lineB, pbBar = lineBBar;
  const bool active = running;
  AsyncWebServerResponse *response = request->beginChunkedResponse(
      "text/csv; charset=utf-8",
      [cursor, epoch, p0, pb, pbBar](uint8_t *buffer, size_t maxLen, size_t) -> size_t {
        size_t written = 0;
        while (written < maxLen) {
          if (cursor->position == cursor->used) {
            cursor->position = 0;
            cursor->used = 0;
            if (!cursor->headerSent) {
              cursor->used = snprintf(cursor->line, sizeof(cursor->line),
                                      "date_time;t_s;T_amb_C;T_gas_C;I_mA;I_min_mA;I_max_mA;P_bar;p0_mA;pB_mA;pB_bar;build\n");
              cursor->headerSent = true;
            } else if (cursor->next < cursor->count) {
              const DriftSample &s = samples[cursor->next++];
              char when[24] = "";
              if (epoch >= 1600000000UL) formatLocalEpochISO(epoch + s.seconds, when, sizeof(when));
              char ta[12] = "", tg[12] = "";
              if (isfinite(s.ta)) snprintf(ta, sizeof(ta), "%.2f", s.ta);
              if (isfinite(s.tg)) snprintf(tg, sizeof(tg), "%.2f", s.tg);
              cursor->used = snprintf(cursor->line, sizeof(cursor->line), "%s;%lu;%s;%s;%.4f;%.4f;%.4f;%.6f;%.4f;%.4f;%.4f;%s\n", when,
                                      (unsigned long)s.seconds, ta, tg, s.current, s.currentMin, s.currentMax,
                                      convertCurrentToPressureAt(s.current, NAN), p0, pb, pbBar, BUILD_ID);
              for (size_t i = 0; i < cursor->used; ++i)
                if (cursor->line[i] == '.') cursor->line[i] = ','; // decimal comma, as the other calibration files
            } else {
              break;
            }
          }
          const size_t chunk = min(maxLen - written, cursor->used - cursor->position);
          memcpy(buffer + written, cursor->line + cursor->position, chunk);
          cursor->position += chunk;
          written += chunk;
        }
        return written;
      });
  response->addHeader("Content-Disposition", "attachment; filename=\"" + calibrationFileName("ZeroDrift", epoch, active) + "\"");
  request->send(response);
}
