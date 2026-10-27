#include <Arduino.h>
#include <math.h>
#include <esp_heap_caps.h>
#include <memory>
#include "HoldTest.h"
#include "PressureWait.h"
#include "PressureControl.h"
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "GambainoCommon.h"
#include "PovotoTasks.h"
#include "TemperatureControl.h"
#include "VentModel.h"
#include "CalibrationEvents.h"
#include "BuildId.h"
#include "datalog.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

unsigned long int NTPEpoch(); // IOTK_NTP (not #included: see AtmosphericPressure.cpp)

// docs/hold-test.md
static constexpr float HOLD_MIN_START_PRESSURE = 0.3f;
static constexpr float HOLD_MIN_STEP_PRESSURE = 0.3f;     // the last step may not be below this
static constexpr float HOLD_HALVING = 0.5f;               // pressure factor between steps, unless the last step would fall below the minimum
static constexpr float HOLD_MAX_FACTOR = 0.9f;            // a smaller drop per step is not worth a step
// Short tests are allowed for checking the routine on the machine (1 min, 1 min per step): their rates are not reliable.
static constexpr float HOLD_MIN_MINUTES = 1.0f, HOLD_MAX_MINUTES = 4320.0f; // 72 h
static constexpr float HOLD_MIN_STEP_MINUTES = 1.0f;       // with more than one step
static constexpr float HOLD_RELIABLE_STEP_MINUTES = 20.0f; // below this the settling is shortened to a quarter of the step
static constexpr uint16_t HOLD_MAX_SAMPLES = 2900;         // 72 h at 90 s = 2880
static constexpr uint16_t HOLD_MIN_FIT_SAMPLES = 4;
static constexpr float HOLD_VENT_MIN_SECONDS = 0.2f, HOLD_VENT_MAX_SECONDS = 600.0f; // halving 130 L takes about a minute or two (2-6 min with a flow restriction at the valve outlet)
// Skipped at the start of a step for its rate: the first step has no vent, the next ones recover from it
// (time constant of 65-90 s, and the walls of the fermenter take longer to return to the ambient temperature).
static unsigned long stepMs = 0; // the length of a step (declared here: the settling depends on it)
static unsigned long settleSeconds(uint8_t step) {
  const unsigned long base = debugging ? 10UL : step == 0 ? 120UL : 600UL;
  if (stepMs >= (unsigned long)(HOLD_RELIABLE_STEP_MINUTES * 60000.0f)) return base;
  const unsigned long quarter = stepMs / 4000UL; // a short test keeps something to analyse
  return quarter < base ? quarter : base;
}

struct HoldSample {
  uint32_t seconds;     // since the start
  uint8_t step;
  float p, tg, te, patm; // gauge bar, K (gas thermometer), K (NAN without ambient), bar
};

enum HoldStage : uint8_t { HOLD_IDLE, HOLD_HOLDING, HOLD_VENTING };
static HoldStage stage = HOLD_IDLE;
static PressureWait *wait = nullptr;
static HoldSample *samples = nullptr;   // PSRAM
static uint16_t sampleCount = 0;
static unsigned long startMillis = 0, durationMs = 0, intervalMs = 10000UL, ventEndMillis = 0;
static uint32_t intervalIndex = 0;
static uint8_t stepCount = 1, currentStep = 0;
static float stepFactor = HOLD_HALVING; // pressure after a vent / pressure before it
static float planned = 0.0f, gasVolume = 0.0f;
static char labelText[41] = "";
static uint32_t runEpoch = 0;
static uint32_t lastSeen = 0;
static double accP = 0, accTg = 0, accTe = 0;
static uint16_t accN = 0, accEnv = 0;
static portMUX_TYPE holdLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool abortRequested = false;
static char statusText[120] = "Not run";
static float simulated = NAN, simulatedStart = NAN;
static unsigned long simulatedBase = 0;

bool holdTestActive() { return stage != HOLD_IDLE; }
bool holdTestHasData() { return sampleCount > 0; }

static float environmentKelvin() {
  return isfinite(environmentTemp) && environmentTemp != NOTaTEMP && environmentTemp > -20.0f && environmentTemp < 60.0f
      ? environmentTemp + 273.15f : NAN;
}

// Least squares of y = (P + Patm) / T and of P and T against minutes, over the samples of [from, to] seconds.
static HoldTestWindow fitRange(uint16_t count, uint32_t from, uint32_t to) {
  HoldTestWindow r = {};
  double n = 0, sx = 0, sy = 0, sp = 0, st = 0, sAbs = 0;
  for (uint16_t i = 0; i < count; ++i) {
    const HoldSample &s = samples[i];
    if (s.seconds < from || s.seconds > to) continue;
    n += 1; sx += s.seconds / 60.0; sy += (s.p + s.patm) / s.tg; sp += s.p; st += s.tg; sAbs += s.p + s.patm;
  }
  if (n < HOLD_MIN_FIT_SAMPLES) return r;
  const double mx = sx / n, my = sy / n, mp = sp / n, mt = st / n;
  double sxx = 0, sxy = 0, sxp = 0, sxt = 0;
  for (uint16_t i = 0; i < count; ++i) {
    const HoldSample &s = samples[i];
    if (s.seconds < from || s.seconds > to) continue;
    const double dx = s.seconds / 60.0 - mx;
    sxx += dx * dx; sxy += dx * ((s.p + s.patm) / s.tg - my); sxp += dx * (s.p - mp); sxt += dx * (s.tg - mt);
  }
  if (!(sxx > 0.0)) return r;
  r.valid = true;
  r.samples = (uint16_t)n;
  r.pressure = float(mp);
  r.slope = float(1000.0 * sxp / sxx);
  r.tempSlope = float(sxt / sxx);
  const double loss = -(sxy / sxx) / my; // fraction per minute
  r.lossPercent = float(100.0 * loss);
  const double patm = sAbs / n - mp;
  r.flow = float(1000.0 * loss * gasVolume * (sAbs / n) / patm);
  return r;
}

HoldTestResult getHoldTestResult() {
  HoldTestResult r = {};
  uint16_t count;
  portENTER_CRITICAL(&holdLock);
  count = sampleCount;
  portEXIT_CRITICAL(&holdLock);
  r.running = stage != HOLD_IDLE;
  r.available = count > 0;
  r.minutes = planned;
  r.volume = gasVolume;
  r.steps = stepCount;
  r.stepMinutes = planned / stepCount;
  r.intervalSeconds = (uint16_t)(intervalMs / 1000UL);
  snprintf(r.label, sizeof(r.label), "%s", labelText);
  if (!count) return r;
  const uint32_t last = samples[count - 1].seconds;
  r.elapsedSeconds = last;
  r.currentStep = samples[count - 1].step;
  const uint32_t stepS = (uint32_t)(stepMs / 1000UL);
  for (uint8_t k = 0; k < stepCount; ++k) {
    const uint32_t from = k * stepS + settleSeconds(k), end = (k + 1) * stepS, to = min(end, last);
    if (from >= to) continue;
    r.step[k].whole = fitRange(count, from, to);
    const uint32_t mid = (from + to) / 2;
    r.step[k].first = fitRange(count, from, mid);
    r.step[k].second = fitRange(count, mid + 1, to);
  }
  const uint32_t curFrom = r.currentStep * stepS + settleSeconds(r.currentStep);
  const uint32_t from10 = last > 600 ? last - 600 : 0, from30 = last > 1800 ? last - 1800 : 0;
  r.last10 = fitRange(count, from10 > curFrom ? from10 : curFrom, last);
  r.last30 = fitRange(count, from30 > curFrom ? from30 : curFrom, last);
  // Exponent of the loss against the gauge pressure: least squares of ln(flow) on ln(P), steps that lose.
  double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (uint8_t k = 0; k < stepCount; ++k) {
    const HoldTestWindow &w = r.step[k].whole;
    if (!w.valid || !(w.lossPercent > 0.01f) || !(w.flow > 0.0f) || !(w.pressure > 0.0f)) continue;
    const double x = log((double)w.pressure), y = log((double)w.flow);
    n += 1; sx += x; sy += y; sxx += x * x; sxy += x * y;
  }
  const double den = n * sxx - sx * sx;
  if (n >= 3 && den > 1e-9) {
    r.exponentValid = true;
    r.exponent = float((n * sxy - sx * sy) / den);
  }
  return r;
}

static void showProgress(bool force) {
  static unsigned long lastShown = 0;
  const unsigned long now = millis();
  if (!force && now - lastShown < 1000UL) return;
  lastShown = now;
  char line1[48], line2[64], line3[64];
  if (stage != HOLD_IDLE) {
    snprintf(line1, sizeof(line1), "Leak hold: step %u of %u", currentStep + 1, stepCount);
    snprintf(line2, sizeof(line2), "%lu of %lu min", (now - startMillis) / 60000UL, durationMs / 60000UL);
    snprintf(line3, sizeof(line3), "P: %.4f bar", ControlData.pressure);
  } else {
    snprintf(line1, sizeof(line1), "Leak hold: END");
    snprintf(line2, sizeof(line2), "%s", statusText);
    snprintf(line3, sizeof(line3), "Result on the Calibration page");
  }
  showCalibrationPanel(line1, line2, line3);
}

String holdTestStatusText() {
  if (stage == HOLD_IDLE) return String(statusText);
  char text[160];
  const unsigned long elapsed = (millis() - startMillis) / 1000UL;
  const unsigned long total = (unsigned long)(planned * 60.0f + 0.5f);
  snprintf(text, sizeof(text), "Running%s: step %u of %u, %lu h %02lu min of %lu h %02lu min, pressure %.4f bar",
           stage == HOLD_VENTING ? " (venting)" : "", currentStep + 1, stepCount, elapsed / 3600UL, (elapsed / 60UL) % 60UL,
           total / 3600UL, (total / 60UL) % 60UL, ControlData.pressure);
  return String(text);
}

static void finish(const char *outcome) {
  routineValve(false);
  stage = HOLD_IDLE;
  delete wait;
  wait = nullptr;
  const HoldTestResult r = getHoldTestResult();
  if (r.available) {
    char details[260];
    int n = snprintf(details, sizeof(details), "%s; %.0f min, %u step%s", r.label[0] ? r.label : "no label", planned, stepCount,
                     stepCount > 1 ? "s" : "");
    for (uint8_t k = 0; k < stepCount && n > 0 && n < (int)sizeof(details); ++k) {
      const HoldTestWindow &w = r.step[k].whole;
      if (w.valid)
        n += snprintf(details + n, sizeof(details) - n, "; %.2f bar %.3f mbar/min (%.4f %%/min, %.1f mL/min)", w.pressure, w.slope,
                      w.lossPercent, w.flow);
    }
    if (strcmp(outcome, "Completed") && n > 0 && n < (int)sizeof(details)) snprintf(details + n, sizeof(details) - n, " (aborted)");
    calibrationEventAppend("hold-test", details);
    snprintf(statusText, sizeof(statusText), "%s", outcome);
  } else {
    snprintf(statusText, sizeof(statusText), "%s without enough data", outcome);
  }
  showProgress(true);
}

void abortHoldTest() {
  if (stage != HOLD_IDLE) abortRequested = true;
}

// Pressure factor between steps: halving, or the milder factor that lands the last step on the minimum pressure.
static float stepFactorFor(float startPressure, uint8_t steps) {
  if (steps < 2) return 1.0f;
  const float floorFactor = powf(HOLD_MIN_STEP_PRESSURE / startPressure, 1.0f / (steps - 1));
  return floorFactor > HOLD_HALVING ? floorFactor : HOLD_HALVING;
}

bool startHoldTest(float minutes, uint8_t steps, const char *label, char *reason, size_t reasonSize) {
  const char *blocked = nullptr;
  char text[100];
  if (SetPointData.mode != MODE_OFF) blocked = "Mode must be OFF";
  else if (taskWindowType != 0 || calibrationRoutineActive() || inTheMiddleOfRelief()) blocked = "Process in progress";
  else if (!isfinite(ControlData.pressure) || ControlData.pressure < HOLD_MIN_START_PRESSURE)
    blocked = "Insufficient pressure (min 0.3 bar)";
  else if (!(minutes >= HOLD_MIN_MINUTES && minutes <= HOLD_MAX_MINUTES)) blocked = "Duration must be from 1 to 4320 min";
  else if (steps < 1 || steps > HOLD_MAX_STEPS) blocked = "Steps must be from 1 to 4";
  else if (!(FMTData.FMTVolume > 0.01f && FMTData.FMTVolume < 500.0f)) blocked = "Invalid FMT volume";
  else if (!(gasTemperature() > -20.0f && gasTemperature() < 60.0f)) blocked = "No fermenter temperature";
  else if (steps > 1 && !debugging && minutes < HOLD_MIN_STEP_MINUTES * steps) {
    snprintf(text, sizeof(text), "%u steps need at least %.0f min", steps, HOLD_MIN_STEP_MINUTES * steps);
    blocked = text;
  } else if (steps > 1 && stepFactorFor(ControlData.pressure, steps) > HOLD_MAX_FACTOR) {
    snprintf(text, sizeof(text), "%.2f bar is too low for %u steps (down to %.1f bar)", ControlData.pressure, steps,
             HOLD_MIN_STEP_PRESSURE);
    blocked = text;
  } else if (steps > 1 && !VentModel::validCurve(FMTData.ventCurve)) blocked = "No valid vent flow curve";
  if (!blocked && !samples) {
    samples = static_cast<HoldSample *>(heap_caps_malloc(sizeof(HoldSample) * HOLD_MAX_SAMPLES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!samples) blocked = "Not enough PSRAM";
  }
  if (!blocked && !wait) {
    wait = new (std::nothrow) PressureWait;
    if (!wait) blocked = "Out of memory";
  }
  if (blocked) {
    snprintf(reason, reasonSize, "%s", blocked);
    return false;
  }
  portENTER_CRITICAL(&holdLock);
  sampleCount = 0;
  runEpoch = NTPEpoch();
  portEXIT_CRITICAL(&holdLock);
  planned = minutes;
  gasVolume = FMTData.FMTVolume; // only converts the rate to mL/min; with a smaller closed volume look at the pressure
  stepCount = steps;
  stepFactor = stepFactorFor(ControlData.pressure, steps);
  currentStep = 0;
  durationMs = (unsigned long)(minutes * 60000.0f);
  stepMs = durationMs / steps;
  // Interval of the samples: 5 s up to 10 min, 10 s up to 6 h, 30 s up to 24 h, 60 s up to 48 h, 90 s up to 72 h; each
  // sample is the mean of the readings of its interval.
  const unsigned long seconds = durationMs / 1000UL;
  intervalMs = (seconds <= 600UL ? 5UL : seconds <= 21600UL ? 10UL : seconds <= 86400UL ? 30UL : seconds <= 172800UL ? 60UL : 90UL) * 1000UL;
  if (debugging) { durationMs = min(durationMs, 120000UL); stepMs = durationMs / steps; intervalMs = 5000UL; }
  intervalIndex = 0;
  accP = accTg = accTe = 0;
  accN = accEnv = 0;
  lastSeen = 0;
  size_t i = 0;
  for (const char *c = label ? label : ""; *c && i < sizeof(labelText) - 1; ++c)
    if (*c != ';' && *c != '\n' && *c != '\r' && *c != '"') labelText[i++] = *c;
  labelText[i] = '\0';
  routineValve(false); // de-energized: the fermenter is closed
  simulated = simulatedStart = ControlData.pressure;
  simulatedBase = 0;
  wait->reset(millis());
  startMillis = millis();
  abortRequested = false;
  stage = HOLD_HOLDING;
  snprintf(statusText, sizeof(statusText), "Running");
  showProgress(true);
  return true;
}

// The vent between two steps: the opening time that takes the pressure to half of the current one (the model of
// the venting test: t = k S, air, the FMT volume, the effective exponent). Returns false when it cannot be done.
static bool startStepVent(unsigned long now) {
  const float p1 = wait->count > 0 ? wait->mean(10000UL) : ControlData.pressure;
  const float target = p1 * stepFactor;
  const double tK = gasTemperature() + 273.15;
  const VentModel::Curve &c = FMTData.ventCurve;
  const double s = VentModel::releaseIntegral(FMTData.FMTVolume, tK, p1, target, Patm, FMTData.FMTEffectiveVentingExponent, false, (double)c.beta);
  const double seconds = (double)c.k * s;
  char details[120];
  if (!(target >= 0.2f) || !isfinite(seconds) || seconds < HOLD_VENT_MIN_SECONDS || seconds > HOLD_VENT_MAX_SECONDS) {
    snprintf(details, sizeof(details), "step %u of %u: no vent (%.3f to %.3f bar, %.2f s), same pressure", currentStep + 2, stepCount, p1,
             target, isfinite(seconds) ? seconds : -1.0);
    calibrationEventAppend("hold-test-vent", details);
    return false;
  }
  snprintf(details, sizeof(details), "step %u of %u: %.3f to %.3f bar, open %.2f s", currentStep + 2, stepCount, p1, target, seconds);
  calibrationEventAppend("hold-test-vent", details);
  ventEndMillis = now + (unsigned long)(seconds * 1000.0);
  routineValve(true); // A to P: the fermenter vents through the port A
  if (debugging) {
    simulatedStart = target;
    simulatedBase = now - startMillis;
  }
  return true;
}

void processHoldTest() {
  if (stage == HOLD_IDLE) return;
  if (abortRequested) {
    abortRequested = false;
    finish("Aborted");
    return;
  }
  if (SetPointData.mode != MODE_OFF || taskWindowType != 0 || !isfinite(ControlData.pressure) ||
      ControlData.pressure > FMTData.maximumPressure) {
    char reason[72];
    if (SetPointData.mode != MODE_OFF) snprintf(reason, sizeof(reason), "Aborted (mode is not OFF)");
    else if (taskWindowType != 0) snprintf(reason, sizeof(reason), "Aborted (a task started)");
    else if (!isfinite(ControlData.pressure)) snprintf(reason, sizeof(reason), "Aborted (invalid pressure)");
    else snprintf(reason, sizeof(reason), "Aborted (%.2f bar is above the %.2f bar security pressure)",
                  ControlData.pressure, FMTData.maximumPressure);
    finish(reason);
    return;
  }
  const unsigned long now = millis();
  const unsigned long elapsed = now - startMillis;
  if (debugging) { // bench boards: a slow loss of 0.03 % per minute
    simulated = simulatedStart * (1.0f - 0.0003f * (elapsed - simulatedBase) / 60000.0f);
    ControlData.pressure = simulated + random(-2, 3) * 0.00005f;
  }
  if (stage == HOLD_VENTING && (long)(now - ventEndMillis) >= 0) {
    routineValve(false);
    stage = HOLD_HOLDING;
  }
  wait->sample(now, ControlData.pressure, debugging ? 0 : pressureAcquisitionMillis(), 1000);
  const uint32_t seen = wait->lastSampleElapsed();
  if (wait->count > 0 && seen != lastSeen) { // a new one-second reading: it goes into the mean of the interval
    lastSeen = seen;
    const float tg = gasTemperature(), te = environmentKelvin();
    if (isfinite(tg)) {
      accP += wait->newest(0);
      accTg += tg + 273.15f;
      ++accN;
      if (isfinite(te)) { accTe += te; ++accEnv; }
    }
  }
  if (elapsed >= (unsigned long)(intervalIndex + 1) * intervalMs) {
    ++intervalIndex;
    if (accN > 0 && sampleCount < HOLD_MAX_SAMPLES) {
      HoldSample s;
      s.seconds = (uint32_t)((unsigned long)intervalIndex * intervalMs / 1000UL);
      s.step = currentStep;
      s.p = float(accP / accN);
      s.tg = float(accTg / accN);
      s.te = accEnv ? float(accTe / accEnv) : NAN;
      s.patm = Patm;
      portENTER_CRITICAL(&holdLock);
      samples[sampleCount] = s;
      ++sampleCount;
      portEXIT_CRITICAL(&holdLock);
    }
    accP = accTg = accTe = 0;
    accN = accEnv = 0;
  }
  if (elapsed >= durationMs) {
    finish("Completed");
    return;
  }
  // The end of a step: the controlled vent to half of the pressure, and the next step begins.
  if (stage == HOLD_HOLDING && currentStep + 1 < stepCount && elapsed >= (unsigned long)(currentStep + 1) * stepMs) {
    if (startStepVent(now)) stage = HOLD_VENTING;
    ++currentStep;
  }
  showProgress(false);
}

// Chunked CSV: the 2900 samples of a 48 h test do not fit in a String.
struct HoldCsvCursor {
  uint16_t count = 0, next = 0;
  bool headerSent = false;
  char line[120];
  size_t used = 0, position = 0;
  char label[41];
};

void handleHoldTestCSV(AsyncWebServerRequest *request) {
  auto cursor = std::make_shared<HoldCsvCursor>();
  uint32_t epoch;
  portENTER_CRITICAL(&holdLock);
  cursor->count = sampleCount;
  epoch = runEpoch;
  portEXIT_CRITICAL(&holdLock);
  snprintf(cursor->label, sizeof(cursor->label), "%s", labelText);
  const bool running = stage != HOLD_IDLE;
  AsyncWebServerResponse *response = request->beginChunkedResponse(
      "text/csv; charset=utf-8",
      [cursor](uint8_t *buffer, size_t maxLen, size_t) -> size_t {
        size_t written = 0;
        while (written < maxLen) {
          if (cursor->position == cursor->used) {
            cursor->position = 0;
            cursor->used = 0;
            if (!cursor->headerSent) {
              cursor->used = snprintf(cursor->line, sizeof(cursor->line), "label;step;t_s;t_step_s;P_bar;T_gas_C;T_env_C;patm_bar;build\n");
              cursor->headerSent = true;
            } else if (cursor->next < cursor->count) {
              const HoldSample &s = samples[cursor->next++];
              const uint32_t stepStart = (uint32_t)(s.step * (stepMs / 1000UL));
              char te[12] = "";
              if (isfinite(s.te)) snprintf(te, sizeof(te), "%.2f", s.te - 273.15f);
              cursor->used = snprintf(cursor->line, sizeof(cursor->line), "%s;%u;%lu;%lu;%.6f;%.2f;%s;%.5f;%s\n", cursor->label,
                                      s.step + 1, (unsigned long)s.seconds, (unsigned long)(s.seconds - stepStart), s.p, s.tg - 273.15f, te,
                                      s.patm, BUILD_ID);
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
  response->addHeader("Content-Disposition", "attachment; filename=\"" + calibrationFileName("HoldTest", epoch, running) + "\"");
  request->send(response);
}
