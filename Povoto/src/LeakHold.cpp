#include <Arduino.h>
#include <math.h>
#include "LeakHold.h"
#include "PressureWait.h"
#include "PressureControl.h"
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "GambainoCommon.h"
#include "PovotoTasks.h"
#include "CalibrationEvents.h"
#include "BuildId.h"
#include "datalog.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

unsigned long int NTPEpoch(); // IOTK_NTP (not #included: see AtmosphericPressure.cpp)

// docs/leak-hold-test.md
static constexpr float LEAK_MIN_START_PRESSURE = 0.5f;
static constexpr unsigned long LEAK_SAMPLE_MS = 10000UL;
static unsigned long closedMs() { return debugging ? 40000UL : 600000UL; }     // fermenter alone
static unsigned long openMs() { return debugging ? 60000UL : 900000UL; }       // valve to the tank open
// Skipped at the start of each phase for the fit: the thermal recovery has a time constant of ~45 s.
static unsigned long closedSkipMs() { return debugging ? 10000UL : 120000UL; }
static unsigned long openSkipMs() { return debugging ? 20000UL : 300000UL; }
static constexpr uint8_t LEAK_MAX_SAMPLES = 160;   // 60 + 90 plus a margin

enum LeakStage : uint8_t { LEAK_IDLE, LEAK_CLOSED, LEAK_OPEN };
struct LeakSample {
  uint8_t phase;     // 0 closed, 1 open
  uint16_t seconds;  // since the start of the phase
  float p, tf, te, patm; // gauge bar, K, K (NAN without ambient), bar
};

static LeakStage stage = LEAK_IDLE;
static PressureWait *wait = nullptr;
static LeakSample samples[LEAK_MAX_SAMPLES];
static uint8_t sampleCount = 0;
static unsigned long phaseMillis = 0;
static uint8_t phaseSamples = 0;     // taken in the current phase
static float tankVolume = 0.0f;
static uint32_t runEpoch = 0;
static portMUX_TYPE leakLock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool abortRequested = false;
static char statusText[120] = "Not run";
static float simulated = NAN, simulatedStart = NAN;

bool leakHoldActive() { return stage != LEAK_IDLE; }
bool leakHoldHasData() { return sampleCount > 0; }

static float environmentKelvin() {
  return isfinite(environmentTemp) && environmentTemp != NOTaTEMP && environmentTemp > -20.0f && environmentTemp < 60.0f
      ? environmentTemp + 273.15f : NAN;
}

// Least squares of y = (P + Patm) / T and of P against minutes, over the samples of a phase after its settling time.
static LeakHoldPhase summarize(uint8_t phase, unsigned long skipMs, float volume) {
  LeakHoldPhase r = {};
  uint8_t count;
  portENTER_CRITICAL(&leakLock);
  count = sampleCount;
  portEXIT_CRITICAL(&leakLock);
  double n = 0, sx = 0, sxx = 0, sy = 0, sxy = 0, sp = 0, spx = 0, st = 0, stx = 0, sPressure = 0, sAbs = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const LeakSample &s = samples[i];
    if (s.phase != phase || s.seconds * 1000UL < skipMs) continue;
    const double x = s.seconds / 60.0, y = (s.p + s.patm) / s.tf;
    n += 1; sx += x; sxx += x * x; sy += y; sxy += x * y;
    sp += s.p; spx += x * s.p; st += s.tf; stx += x * s.tf; sPressure += s.p; sAbs += s.p + s.patm;
  }
  const double den = n * sxx - sx * sx;
  if (n < 6 || !(den > 0.0)) return r;
  const double slopeY = (n * sxy - sx * sy) / den, meanY = sy / n;
  r.valid = true;
  r.samples = (uint8_t)n;
  r.pressure = float(sPressure / n);
  r.slope = float(1000.0 * (n * spx - sx * sp) / den);
  r.tempSlope = float((n * stx - sx * st) / den);
  const double loss = -slopeY / meanY; // fraction per minute
  r.lossPercent = float(100.0 * loss);
  const double patm = (sAbs - sPressure) / n;
  r.flow = float(1000.0 * loss * volume * (sAbs / n) / patm);
  return r;
}

LeakHoldResult getLeakHoldResult() {
  LeakHoldResult r = {};
  r.running = stage != LEAK_IDLE;
  r.tankVolume = tankVolume;
  r.closed = summarize(0, closedSkipMs(), FMTData.FMTVolume);
  r.open = summarize(1, openSkipMs(), FMTData.FMTVolume + tankVolume);
  r.available = r.closed.valid || r.open.valid;
  if (r.closed.valid && r.open.valid && r.closed.pressure > 0.01f) {
    r.tankSideValid = true;
    r.tankSideFlow = r.open.flow - r.closed.flow * (r.open.pressure / r.closed.pressure);
  }
  return r;
}

static void showProgress(bool force) {
  static unsigned long lastShown = 0;
  const unsigned long now = millis();
  if (!force && now - lastShown < 1000UL) return;
  lastShown = now;
  char line1[48], line2[64], line3[64];
  if (stage != LEAK_IDLE) {
    snprintf(line1, sizeof(line1), "Leak test: %s", stage == LEAK_CLOSED ? "fermenter alone" : "valve open");
    const unsigned long total = stage == LEAK_CLOSED ? closedMs() : openMs();
    snprintf(line2, sizeof(line2), "%lu of %lu s", (now - phaseMillis) / 1000UL, total / 1000UL);
    snprintf(line3, sizeof(line3), "P: %.4f bar", ControlData.pressure);
  } else {
    snprintf(line1, sizeof(line1), "Leak test: END");
    snprintf(line2, sizeof(line2), "%s", statusText);
    snprintf(line3, sizeof(line3), "Result on the Calibration page");
  }
  showCalibrationPanel(line1, line2, line3);
}

String leakHoldStatusText() {
  if (stage == LEAK_IDLE) return String(statusText);
  char text[160];
  const unsigned long total = stage == LEAK_CLOSED ? closedMs() : openMs();
  snprintf(text, sizeof(text), "Running: %s, %lu of %lu s, pressure %.4f bar",
           stage == LEAK_CLOSED ? "fermenter closed (tank vented)" : "valve to the tank open", (millis() - phaseMillis) / 1000UL,
           total / 1000UL, ControlData.pressure);
  return String(text);
}

static void finish(const char *outcome) {
  routineValve(false);
  stage = LEAK_IDLE;
  delete wait;
  wait = nullptr;
  const LeakHoldResult r = getLeakHoldResult();
  if (r.available) {
    char details[200];
    int n = snprintf(details, sizeof(details), "tank %.3f L", tankVolume);
    if (r.closed.valid && n > 0 && n < (int)sizeof(details))
      n += snprintf(details + n, sizeof(details) - n, "; closed %.3f mbar/min (%.4f %%/min, %.1f mL/min)", r.closed.slope,
                    r.closed.lossPercent, r.closed.flow);
    if (r.open.valid && n > 0 && n < (int)sizeof(details))
      n += snprintf(details + n, sizeof(details) - n, "; open %.3f mbar/min (%.4f %%/min, %.1f mL/min)", r.open.slope,
                    r.open.lossPercent, r.open.flow);
    if (r.tankSideValid && n > 0 && n < (int)sizeof(details))
      n += snprintf(details + n, sizeof(details) - n, "; tank side %.1f mL/min", r.tankSideFlow);
    if (strcmp(outcome, "Completed")) snprintf(details + n, sizeof(details) - n, " (aborted)");
    calibrationEventAppend("leak-hold", details);
    snprintf(statusText, sizeof(statusText), "%s", outcome);
  } else {
    snprintf(statusText, sizeof(statusText), "%s without enough data", outcome);
  }
  showProgress(true);
}

void abortLeakHold() {
  if (stage != LEAK_IDLE) abortRequested = true;
}

bool startLeakHold(float volume, char *reason, size_t reasonSize) {
  const char *blocked = nullptr;
  if (SetPointData.mode != MODE_OFF) blocked = "Mode must be OFF";
  else if (taskWindowType != 0 || calibrationRoutineActive() || inTheMiddleOfRelief()) blocked = "Process in progress";
  else if (!isfinite(ControlData.pressure) || ControlData.pressure < LEAK_MIN_START_PRESSURE)
    blocked = "Insufficient pressure (min 0.5 bar)";
  else if (!(volume > 0.5f && volume < 500.0f)) blocked = "Invalid expansion tank volume";
  else if (!(ControlData.temperature > -20.0f && ControlData.temperature < 60.0f)) blocked = "No fermenter temperature";
  if (!blocked && !wait) {
    wait = new (std::nothrow) PressureWait;
    if (!wait) blocked = "Out of memory";
  }
  if (blocked) {
    snprintf(reason, reasonSize, "%s", blocked);
    return false;
  }
  portENTER_CRITICAL(&leakLock);
  tankVolume = volume;
  sampleCount = 0;
  phaseSamples = 0;
  runEpoch = NTPEpoch();
  portEXIT_CRITICAL(&leakLock);
  if (FMTData.lastExpTankVolume != volume) { // shared with the fermenter volume routine
    FMTData.lastExpTankVolume = volume;
    writeFMTDataToNIV();
  }
  routineValve(false); // de-energized: the tank vents through R, the fermenter stays closed
  simulated = simulatedStart = ControlData.pressure;
  wait->reset(millis());
  phaseMillis = millis();
  abortRequested = false;
  stage = LEAK_CLOSED;
  snprintf(statusText, sizeof(statusText), "Running");
  showProgress(true);
  return true;
}

void processLeakHold() {
  if (stage == LEAK_IDLE) return;
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
  const unsigned long elapsed = now - phaseMillis;
  if (debugging) { // bench boards: a slow leak, and the equalization of the open phase with tau 3 s
    const float leakPerMin = 0.0003f;
    if (stage == LEAK_CLOSED) {
      simulated = simulatedStart * (1.0f - leakPerMin * elapsed / 60000.0f);
    } else {
      const float equal = (simulatedStart + Patm) * FMTData.FMTVolume / (FMTData.FMTVolume + tankVolume) +
                          Patm * tankVolume / (FMTData.FMTVolume + tankVolume) - Patm;
      simulated = equal * (1.0f - leakPerMin * elapsed / 60000.0f) + (simulatedStart - equal) * expf(-(float)elapsed / 3000.0f);
    }
    ControlData.pressure = simulated + random(-2, 3) * 0.00005f;
  }
  wait->sample(now, ControlData.pressure, debugging ? 0 : pressureAcquisitionMillis(), 1000);
  if (wait->count > 0 && elapsed >= (unsigned long)(phaseSamples + 1) * LEAK_SAMPLE_MS && sampleCount < LEAK_MAX_SAMPLES) {
    LeakSample s;
    s.phase = stage == LEAK_CLOSED ? 0 : 1;
    s.seconds = (uint16_t)((unsigned long)(phaseSamples + 1) * LEAK_SAMPLE_MS / 1000UL);
    s.p = wait->mean(LEAK_SAMPLE_MS);
    s.tf = ControlData.temperature + 273.15f;
    s.te = environmentKelvin();
    s.patm = Patm;
    portENTER_CRITICAL(&leakLock);
    samples[sampleCount] = s;
    ++sampleCount;
    portEXIT_CRITICAL(&leakLock);
    ++phaseSamples;
  }
  if (stage == LEAK_CLOSED && elapsed >= closedMs()) {
    simulatedStart = ControlData.pressure; // debug simulation only
    routineValve(true); // fermenter -> tank
    wait->reset(now);
    phaseMillis = now;
    phaseSamples = 0;
    stage = LEAK_OPEN;
    showProgress(true);
  } else if (stage == LEAK_OPEN && elapsed >= openMs()) {
    finish("Completed");
    return;
  }
  if (stage != LEAK_IDLE) showProgress(false);
}

void handleLeakHoldCSV(AsyncWebServerRequest *request) {
  const bool running = leakHoldActive();
  uint8_t count;
  uint32_t epoch;
  portENTER_CRITICAL(&leakLock);
  count = sampleCount;
  epoch = runEpoch;
  portEXIT_CRITICAL(&leakLock);
  String csv = "phase;t_s;P_bar;T_ferm_C;T_env_C;patm_bar;build\n";
  auto add = [&csv](float v, int d, bool last = false) {
    if (isfinite(v)) {
      String t(v, (unsigned int)d);
      t.replace(".", ",");
      csv += t;
    }
    if (!last) csv += ';';
  };
  for (uint8_t i = 0; i < count; ++i) {
    const LeakSample &s = samples[i];
    csv += String(s.phase == 0 ? "fermenter" : "fermenter+tank") + ';' + String(s.seconds) + ';';
    add(s.p, 6); add(s.tf - 273.15f, 2); add(s.te - 273.15f, 2); add(s.patm, 5);
    csv += BUILD_ID;
    csv += '\n';
  }
  AsyncWebServerResponse *response = request->beginResponse(200, "text/csv", csv);
  response->addHeader("Content-Disposition", "attachment; filename=\"" + calibrationFileName("LeakHold", epoch, running) + "\"");
  request->send(response);
}
