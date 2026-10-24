#include <Arduino.h>
#include <math.h>
#include "ExpansionTank.h"
#include "PressureWait.h"
#include "PressureControl.h"
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "GambainoCommon.h"
#include "PovotoTasks.h"
#include "VentModel.h"
#include "CalibrationEvents.h"
#include "BuildId.h"
#include "datalog.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

unsigned long int NTPEpoch(); // IOTK_NTP (not #included: see AtmosphericPressure.cpp)

// docs/expansion-tank-volume.md
static constexpr uint8_t TANK_MAX_RELIEFS = 12;
static constexpr float TANK_END_PRESSURE = 0.5f; // the plan goes down to this gauge pressure
static constexpr float TANK_MIN_START_PRESSURE = 0.3f;
static constexpr float TANK_STABLE_SLOPE_BAR_PER_MIN = 0.0003f; // 0.3 mbar/min
static constexpr unsigned long TANK_SLOPE_WINDOW_MS = 120000UL;
static constexpr unsigned long TANK_AVERAGE_MS = 10000UL;
// P2 is logged at these times after the valve opens (pressure, fermenter and ambient temperature)
static constexpr uint8_t TANK_CHECKPOINTS = 5;
static constexpr unsigned long TANK_CHECKPOINT_S[TANK_CHECKPOINTS] = {30, 60, 120, 180, 300};
// P1 is logged at these times after the valve closed (the wait of the fermenter starts then, during the vent of the tank):
// only logged, the calculation uses the P1 at the end of the wait (docs/expansion-tank-volume.md)
static constexpr uint8_t TANK_P1_CHECKPOINTS = 6;
static constexpr unsigned long TANK_P1_CHECKPOINT_S[TANK_P1_CHECKPOINTS] = {60, 120, 180, 240, 300, 420};
static unsigned long tankWaitMinMs() { return debugging ? 10000UL : 120000UL; }
// P2 is read at a fixed time after the valve opens; longer waits only accumulate drift. The thermal
// recovery after the opening has a time constant of 60-90 s, so at 300 s a tail of 0.3-1 mbar is left: P2 is the
// asymptote of an exponential fitted to the checkpoints (docs/expansion-tank-volume.md). The relief is not stable if
// that tail is above TANK_OPEN_TAIL_BAR or if the pressure fell by more than TANK_OPEN_DRIFT_BAR between 180 s and 300 s.
static constexpr unsigned long TANK_OPEN_WAIT_MS = 300000UL;
static constexpr float TANK_OPEN_TAIL_BAR = 0.001f;
static constexpr float TANK_OPEN_DRIFT_BAR = 0.001f;
static constexpr float TANK_OPEN_MAX_TAIL_BAR = 0.003f; // a larger tail is not believed: the fit is not used
static unsigned long tankWaitMaxMs() { return debugging ? 30000UL : 900000UL; }
static unsigned long tankVentMinMs() { return debugging ? 10000UL : 180000UL; }
static constexpr unsigned long TANK_VENT_MAX_MS = 900000UL;

struct TankRelief {
  uint32_t epoch;
  float p1, p2;                  // gauge, settled
  float fermenterK1, fermenterK2;
  float tankK1, tankK2;            // environment (the tank follows it); NAN -> fermenter
  float patm1, patm2;
  float waitP1, waitP2, ventSeconds;
  bool settled1, settled2;       // false: the wait hit its maximum
  float volume;                  // full mole balance
  float simpleVolume;            // V_E (T_F/T_E) P2 / (P1 - P2)
  float cpP[TANK_CHECKPOINTS];   // checkpoints of the P2 wait: 10-s mean pressure (gauge), NAN if not reached
  float cpTf[TANK_CHECKPOINTS], cpTe[TANK_CHECKPOINTS]; // fermenter and ambient temperature, K
  // Lowest reading while the valve is open (each new acquisition, median-filtered, after the
  // solenoid noise window), when it happened (s after opening) and how many were seen.
  float pMin, tMin;
  uint16_t openSamples;
  float p2Fit, tail, tau; // asymptote of the checkpoints (bar), its distance from the 300 s value (bar), time constant (s); NAN without a fit
  float cp1P[TANK_P1_CHECKPOINTS], cp1Tf[TANK_P1_CHECKPOINTS]; // checkpoints of the P1 wait: 10-s mean pressure (gauge) and fermenter temperature (K)
};
static void clearCheckpoints(TankRelief &k) {
  for (uint8_t i = 0; i < TANK_CHECKPOINTS; ++i) k.cpP[i] = k.cpTf[i] = k.cpTe[i] = NAN;
  for (uint8_t i = 0; i < TANK_P1_CHECKPOINTS; ++i) k.cp1P[i] = k.cp1Tf[i] = NAN;
}
// The P1 checkpoints of the wait in progress (that of the next relief): copied to its record when the wait ends.
static float waitCpP[TANK_P1_CHECKPOINTS], waitCpTf[TANK_P1_CHECKPOINTS];
static void clearWaitCheckpoints() {
  for (uint8_t i = 0; i < TANK_P1_CHECKPOINTS; ++i) waitCpP[i] = waitCpTf[i] = NAN;
}

enum TankStage : uint8_t { TANK_IDLE, TANK_WAIT_P1, TANK_OPEN, TANK_VENTING };
static TankStage stage = TANK_IDLE;
static PressureWait *wait = nullptr;
static TankRelief reliefs[TANK_MAX_RELIEFS];
static uint8_t reliefCount = 0, reliefTarget = 0; // reliefCount: completed (vented); the next one is being filled
static uint8_t reliefShown = 0;  // reliefs with P1, P2 and volume: one more than reliefCount while the tank vents
static float expansionTankVolume = 0.0f;
static uint32_t runEpoch = 0;   // start of the run (CSV name)
// The page (web task) copies the measured reliefs (reliefShown) while the loop works: the
// counts, the start of a run and the vent time of the last relief change under this lock.
// Nothing is written to flash for the copy.
static portMUX_TYPE reliefLock = portMUX_INITIALIZER_UNLOCKED;
struct TankSnapshot {
  TankRelief r[TANK_MAX_RELIEFS];
  uint8_t count, target;
  float tank;
  uint32_t epoch;
};
static void takeSnapshot(TankSnapshot &out) {
  portENTER_CRITICAL(&reliefLock);
  out.count = reliefShown;
  out.target = reliefTarget;
  out.tank = expansionTankVolume;
  out.epoch = runEpoch;
  memcpy(out.r, reliefs, sizeof(TankRelief) * reliefShown);
  portEXIT_CRITICAL(&reliefLock);
}
static bool reliefStable(const TankRelief &k) { return k.settled1 && k.settled2 && isfinite(k.volume); }
static unsigned long stageMillis = 0, ventWaitMs = 0;
static float simulated = NAN;
static char statusText[96] = "Not run";
// Set by the web handler; the loop aborts (the wait buffer is freed in the loop only).
static volatile bool abortRequested = false;
static volatile float waitSlope = NAN; // bar/min of the last 2 min of the wait, NAN until 5 samples

bool expansionTankActive() { return stage != TANK_IDLE; }

static float environmentKelvin() {
  return isfinite(environmentTemp) && environmentTemp != NOTaTEMP && environmentTemp > -20.0f && environmentTemp < 60.0f
      ? environmentTemp + 273.15f : NAN;
}

static void showProgress(bool force) {
  static unsigned long lastShown = 0;
  const unsigned long now = millis();
  if (!force && now - lastShown < 1000UL) return;
  lastShown = now;
  char line1[48], line2[64], line3[64];
  if (stage != TANK_IDLE) {
    snprintf(line1, sizeof(line1), "Fermenter volume: step %u/%u", reliefCount + 1, reliefTarget);
    snprintf(line2, sizeof(line2), "%s %lus",
             stage == TANK_WAIT_P1 ? "Settling P1" : stage == TANK_OPEN ? "Valve open, equalizing" : "Venting the tank",
             (now - stageMillis) / 1000UL);
    snprintf(line3, sizeof(line3), "P: %.4f bar", ControlData.pressure);
  } else {
    snprintf(line1, sizeof(line1), "Fermenter volume: END");
    snprintf(line2, sizeof(line2), "%s", statusText);
    snprintf(line3, sizeof(line3), "Result on the Calibration page");
  }
  showCalibrationPanel(line1, line2, line3);
}

String expansionTankStatusText() {
  if (stage == TANK_IDLE) return String(statusText);
  char text[200];
  const unsigned long elapsed = (millis() - stageMillis) / 1000UL;
  char duration[40]; // the vent has a planned duration; the waits end when the pressure is stable
  if (stage == TANK_VENTING) snprintf(duration, sizeof(duration), "for %lu s (elapsed: %lu s)", ventWaitMs / 1000UL, elapsed);
  else if (stage == TANK_OPEN && !debugging)
    snprintf(duration, sizeof(duration), "for %lu s (P2 read at %lu s)", elapsed, TANK_OPEN_WAIT_MS / 1000UL);
  else snprintf(duration, sizeof(duration), "for %lu s", elapsed);
  int n = snprintf(text, sizeof(text), "Running: step %u of %u (planned), %s %s, pressure %.4f bar", reliefCount + 1,
                   reliefTarget,
                   stage == TANK_WAIT_P1 ? "waiting for a stable P1" : stage == TANK_OPEN ? "valve open, waiting for P2"
                                                                                       : "venting the tank",
                   duration, ControlData.pressure);
  if (stage != TANK_VENTING && (stage != TANK_OPEN || debugging) && n > 0 && n < (int)sizeof(text)) {
    const float slope = waitSlope; // bar/min, the last 2 min
    if (isfinite(slope))
      snprintf(text + n, sizeof(text) - n, ", slope %+.2f mbar/min (limit %.2f)", slope * 1000.0f,
               TANK_STABLE_SLOPE_BAR_PER_MIN * 1000.0f);
  }
  return String(text);
}

static void finish(const char *outcome) {
  routineValve(false);
  stage = TANK_IDLE;
  delete wait;
  wait = nullptr;
  const ExpansionTankResult r = getExpansionTankResult();
  if (r.available) {
    snprintf(statusText, sizeof(statusText), "%s: %.2f L (sd %.2f L, %u of %u reliefs used)", outcome, r.mean, r.sd, r.reliefs,
             r.total);
    char details[160];
    snprintf(details, sizeof(details), "tank %.3f L, %u of %u reliefs used: %.3f L (sd %.3f; simple formula %.3f)%s", expansionTankVolume,
             r.reliefs, r.total, r.mean, r.sd, r.simpleMean, strcmp(outcome, "Completed") ? " (aborted)" : "");
    calibrationEventAppend("fermenter-volume", details);
  } else {
    snprintf(statusText, sizeof(statusText), "%s without a usable relief", outcome);
  }
  showProgress(true);
}

// Web task: only requests it; processExpansionTank() aborts in the main loop.
void abortExpansionTank() {
  if (stage != TANK_IDLE) abortRequested = true;
}

// f = V_F / (V_F + V_E) is the pressure ratio of each relief (the absolute ratio
// is approximated by the gauge one): n = ceil(ln(0.5 / P) / ln f), from 1 to 12.
uint8_t expansionTankPlannedReliefs(float startPressure, float tankVolume) {
  if (!(startPressure > TANK_END_PRESSURE) || !(tankVolume > 0.0f) || !(FMTData.FMTVolume > 0.0f)) return 1;
  const double f = FMTData.FMTVolume / (FMTData.FMTVolume + (double)tankVolume);
  const double n = ceil(log(TANK_END_PRESSURE / startPressure) / log(f));
  return (uint8_t)constrain((int)n, 1, (int)TANK_MAX_RELIEFS);
}

bool startExpansionTank(float volume, char *reason, size_t reasonSize) {
  const char *blocked = nullptr;
  if (SetPointData.mode != MODE_OFF) blocked = "Mode must be OFF";
  else if (taskWindowType != 0 || calibrationRoutineActive() || inTheMiddleOfRelief()) blocked = "Process in progress";
  else if (!isfinite(ControlData.pressure) || ControlData.pressure < TANK_MIN_START_PRESSURE)
    blocked = "Insufficient pressure (min 0.3 bar)";
  else if (!(volume > 0.5f && volume < 500.0f)) blocked = "Invalid expansion tank volume";
  if (!blocked && !wait) {
    wait = new (std::nothrow) PressureWait;
    if (!wait) blocked = "Out of memory";
  }
  if (blocked) {
    snprintf(reason, reasonSize, "%s", blocked);
    return false;
  }
  portENTER_CRITICAL(&reliefLock);
  expansionTankVolume = volume;
  reliefTarget = expansionTankPlannedReliefs(ControlData.pressure, volume);
  reliefCount = 0;
  reliefShown = 0;
  runEpoch = NTPEpoch();
  portEXIT_CRITICAL(&reliefLock);
  if (FMTData.lastExpTankVolume != volume) { // the next run starts with it
    FMTData.lastExpTankVolume = volume;
    writeFMTDataToNIV();
  }
  routineValve(false); // de-energized: the tank vents through R
  waitSlope = NAN;
  simulated = ControlData.pressure;
  wait->reset(millis());
  clearWaitCheckpoints();
  stageMillis = millis();
  abortRequested = false;
  stage = TANK_WAIT_P1;
  snprintf(statusText, sizeof(statusText), "Running");
  showProgress(true);
  return true;
}

// P1: stable when at least the minimum wait (2 min) has passed and |slope of the last 2 min| is below the
// limit (0.3 mbar/min); at the maximum the wait ends anyway (settled = false).
static bool waitDone(bool &settled) {
  const uint32_t last = wait->lastSampleElapsed();
  settled = false;
  if (wait->count == 0 || last < tankWaitMinMs()) return false;
  const float slope = wait->slopePerMinute(TANK_SLOPE_WINDOW_MS);
  settled = isfinite(slope) && fabsf(slope) < TANK_STABLE_SLOPE_BAR_PER_MIN;
  // Bench boards: the short wait cannot meet the 120-s slope criterion; the simulated
  // pressure has settled by its end (tau 3 s), so it counts as stable.
  if (debugging && last >= tankWaitMaxMs()) settled = true;
  return settled || last >= tankWaitMaxMs();
}

// P(t) = P_inf - A exp(-t / tau) fitted to the checkpoints (least squares for P_inf and A at each tau of a grid,
// the best tau kept). False when a checkpoint is missing or the fit is not a recovery (A <= 0, or a tail above the limit).
static bool fitOpenAsymptote(TankRelief &k) {
  k.p2Fit = k.tail = k.tau = NAN;
  for (uint8_t i = 0; i < TANK_CHECKPOINTS; ++i)
    if (!isfinite(k.cpP[i])) return false;
  const double ref = k.cpP[TANK_CHECKPOINTS - 1];
  double bestSse = INFINITY, bestA = NAN, bestTau = NAN;
  for (double tau = 20.0; tau <= 300.0; tau += 5.0) {
    double sx = 0, sxx = 0, sy = 0, sxy = 0;
    for (uint8_t i = 0; i < TANK_CHECKPOINTS; ++i) {
      const double x = exp(-(double)TANK_CHECKPOINT_S[i] / tau), y = (k.cpP[i] - ref) * 1000.0; // mbar around the 300 s value
      sx += x; sxx += x * x; sy += y; sxy += x * y;
    }
    const double n = TANK_CHECKPOINTS, den = n * sxx - sx * sx;
    if (!(den > 0.0)) continue;
    const double c = (n * sxy - sx * sy) / den, a = (sy - c * sx) / n; // y = a + c x, c = -A
    if (!(c < 0.0)) continue;
    double sse = 0;
    for (uint8_t i = 0; i < TANK_CHECKPOINTS; ++i) {
      const double r = (k.cpP[i] - ref) * 1000.0 - (a + c * exp(-(double)TANK_CHECKPOINT_S[i] / tau));
      sse += r * r;
    }
    if (sse < bestSse) { bestSse = sse; bestA = a; bestTau = tau; }
  }
  if (!isfinite(bestSse)) return false;
  const float tail = float(bestA / 1000.0); // asymptote minus the 300 s value
  if (!(tail >= 0.0f && tail <= TANK_OPEN_MAX_TAIL_BAR)) return false;
  k.tail = tail;
  k.tau = (float)bestTau;
  k.p2Fit = (float)ref + tail;
  return true;
}

// P2: fixed wait (bench boards keep the short slope wait of the simulation). Stable: the tail left at 300 s is small
// (or, without a fit, the pressure moved little between 180 s and 300 s) and the pressure did not fall.
static bool openWaitDone(TankRelief &k, bool &settled) {
  if (debugging) return waitDone(settled);
  settled = false;
  if (wait->count == 0 || wait->lastSampleElapsed() < TANK_OPEN_WAIT_MS) return false;
  const bool haveCheckpoints = isfinite(k.cpP[3]) && isfinite(k.cpP[4]);
  const float change = haveCheckpoints ? k.cpP[4] - k.cpP[3] : NAN;
  if (fitOpenAsymptote(k)) settled = k.tail <= TANK_OPEN_TAIL_BAR && change > -TANK_OPEN_DRIFT_BAR;
  else settled = haveCheckpoints && fabsf(change) < TANK_OPEN_DRIFT_BAR;
  return true;
}

// Full mole balance (ideal air): the fermenter loses what the tank gains.
static void computeRelief(TankRelief &k) {
  const float fermenterK2 = k.fermenterK2;
  const float tankK1 = isfinite(k.tankK1) ? k.tankK1 : k.fermenterK1;
  const float tankK2 = isfinite(k.tankK2) ? k.tankK2 : fermenterK2;
  const double fermenterLoss = (k.p1 + k.patm1) / k.fermenterK1 - (k.p2 + k.patm2) / fermenterK2;
  const double tankGain = (k.p2 + k.patm2) / tankK2 - k.patm1 / tankK1;
  k.volume = fermenterLoss > 0.0 ? float(expansionTankVolume * tankGain / fermenterLoss) : NAN;
  k.simpleVolume = k.p1 > k.p2 ? expansionTankVolume * (fermenterK2 / tankK2) * k.p2 / (k.p1 - k.p2) : NAN;
}

void processExpansionTank() {
  if (stage == TANK_IDLE) return;
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
  TankRelief &k = reliefs[reliefCount];
  if (debugging) { // bench boards: equalization with the FMT volume, tau 3 s
    if (stage == TANK_OPEN) {
      const float equal = (k.p1 + Patm) * FMTData.FMTVolume / (FMTData.FMTVolume + expansionTankVolume) +
                          Patm * expansionTankVolume / (FMTData.FMTVolume + expansionTankVolume) - Patm;
      simulated = equal + (k.p1 - equal) * expf(-(float)(now - stageMillis) / 3000.0f);
    }
    ControlData.pressure = simulated + random(-2, 3) * 0.00005f;
  }
  wait->sample(now, ControlData.pressure, debugging ? 0 : pressureAcquisitionMillis(), 1000);
  waitSlope = wait->slopePerMinute(TANK_SLOPE_WINDOW_MS); // for the status text (the web task never touches `wait`)
  if (stage == TANK_VENTING || stage == TANK_WAIT_P1) { // the wait of the fermenter, closed, for the next P1
    const uint32_t waited = wait->lastSampleElapsed();
    for (uint8_t i = 0; i < TANK_P1_CHECKPOINTS; ++i)
      if (!isfinite(waitCpP[i]) && waited >= TANK_P1_CHECKPOINT_S[i] * 1000UL) {
        waitCpP[i] = wait->mean(TANK_AVERAGE_MS);
        waitCpTf[i] = ControlData.temperature + 273.15f;
      }
  }
  if (stage == TANK_OPEN) { // k is not shown to the web task until its P2 is final
    static unsigned long lastOpenAcquisition = 0;
    if (k.openSamples == 0) lastOpenAcquisition = 0;
    const unsigned long acquired = debugging ? now : pressureAcquisitionMillis();
    if (acquired != lastOpenAcquisition && (long)(acquired - stageMillis) >= 0) {
      lastOpenAcquisition = acquired;
      if (k.openSamples < UINT16_MAX) ++k.openSamples;
      if (!isfinite(k.pMin) || ControlData.pressure < k.pMin) {
        k.pMin = ControlData.pressure;
        k.tMin = (acquired - stageMillis) / 1000.0f;
      }
    }
    const uint32_t elapsed = wait->lastSampleElapsed();
    for (uint8_t i = 0; i < TANK_CHECKPOINTS; ++i)
      if (!isfinite(k.cpP[i]) && elapsed >= TANK_CHECKPOINT_S[i] * 1000UL) {
        k.cpP[i] = wait->mean(TANK_AVERAGE_MS);
        k.cpTf[i] = ControlData.temperature + 273.15f;
        k.cpTe[i] = environmentKelvin();
      }
  }
  bool settled = false;
  if (stage == TANK_WAIT_P1 && waitDone(settled)) {
    k = {};
    clearCheckpoints(k);
    for (uint8_t i = 0; i < TANK_P1_CHECKPOINTS; ++i) {
      k.cp1P[i] = waitCpP[i];
      k.cp1Tf[i] = waitCpTf[i];
    }
    k.pMin = k.tMin = NAN;
    k.p2Fit = k.tail = k.tau = NAN;
    k.epoch = NTPEpoch();
    k.p1 = wait->mean(TANK_AVERAGE_MS);
    k.fermenterK1 = ControlData.temperature + 273.15f;
    k.tankK1 = environmentKelvin();
    k.patm1 = Patm;
    k.settled1 = settled;
    k.waitP1 = wait->lastSampleElapsed() / 1000.0f;
    routineValve(true); // fermenter -> tank
    wait->reset(now);
    stageMillis = now;
    stage = TANK_OPEN;
    showProgress(true);
  } else if (stage == TANK_OPEN && openWaitDone(k, settled)) {
    k.p2 = isfinite(k.p2Fit) ? k.p2Fit : wait->mean(TANK_AVERAGE_MS);
    k.fermenterK2 = ControlData.temperature + 273.15f;
    k.tankK2 = environmentKelvin();
    k.patm2 = Patm;
    k.settled2 = settled;
    k.waitP2 = wait->lastSampleElapsed() / 1000.0f;
    computeRelief(k);
    portENTER_CRITICAL(&reliefLock);
    k.ventSeconds = NAN; // filled when the vent ends; the relief is already usable
    reliefShown = reliefCount + 1;
    portEXIT_CRITICAL(&reliefLock);
    Serial.printf("[EXPTANK] relief %u: P1 %.5f P2 %.5f bar, V_F %.3f L (simple %.3f)\n", reliefCount + 1, k.p1, k.p2,
                  k.volume, k.simpleVolume);
    routineValve(false); // the tank vents through R
    if (reliefCount + 1 >= reliefTarget || ControlData.pressure < TANK_MIN_START_PRESSURE * 0.5f) {
      // The last relief: no other follows, so the run ends with the valve de-energized (the tank vents by itself)
      // instead of waiting for the vent; its vent_s stays empty.
      portENTER_CRITICAL(&reliefLock);
      ++reliefCount;
      portEXIT_CRITICAL(&reliefLock);
      finish("Completed");
      return;
    }
    // A complete vent: about six time constants of the tank through the valve
    // (current curve with air), 3-15 min.
    const double tau = VentModel::tauRef(FMTData.ventCurve, k.p2, Patm) * expansionTankVolume / VentModel::V_REF;
    ventWaitMs = (unsigned long)fmin((double)TANK_VENT_MAX_MS, fmax((double)tankVentMinMs(), 6000.0 * tau));
    if (debugging) ventWaitMs = tankVentMinMs();
    wait->reset(now);
    clearWaitCheckpoints();
    stageMillis = now;
    stage = TANK_VENTING;
    showProgress(true);
  } else if (stage == TANK_VENTING && now - stageMillis >= ventWaitMs) {
    portENTER_CRITICAL(&reliefLock);
    k.ventSeconds = (now - stageMillis) / 1000.0f;
    ++reliefCount;
    portEXIT_CRITICAL(&reliefLock);
    if (reliefCount >= reliefTarget || ControlData.pressure < TANK_MIN_START_PRESSURE * 0.5f) {
      finish("Completed");
      return;
    }
    // The fermenter was closed during the tank vent: its wait continues.
    stageMillis = now;
    stage = TANK_WAIT_P1;
    showProgress(true);
  }
  if (stage != TANK_IDLE) showProgress(false);
}

static ExpansionTankResult resultFrom(const TankSnapshot &snap) {
  ExpansionTankResult r = {};
  double sum = 0.0, squares = 0.0, simple = 0.0;
  float low = INFINITY, high = -INFINITY;
  uint8_t n = 0;
  for (uint8_t i = 0; i < snap.count; ++i) {
    if (!reliefStable(snap.r[i])) continue; // unstable steps stay out of the mean and the deviation
    low = fminf(low, snap.r[i].volume);
    high = fmaxf(high, snap.r[i].volume);
    sum += snap.r[i].volume;
    squares += (double)snap.r[i].volume * snap.r[i].volume;
    simple += snap.r[i].simpleVolume;
    ++n;
  }
  r.available = n > 0;
  r.tankVolume = snap.tank;
  r.reliefs = n;
  r.total = snap.count;
  r.planned = snap.target;
  r.mean = n ? float(sum / n) : NAN;
  r.sd = n > 1 ? float(sqrt(fmax(0.0, (squares - sum * sum / n) / (n - 1)))) : NAN;
  r.simpleMean = n ? float(simple / n) : NAN;
  r.minVolume = n ? low : NAN;
  r.maxVolume = n ? high : NAN;
  r.fmtVolume = FMTData.FMTVolume;
  r.canSave = stage == TANK_IDLE && n >= 2;
  return r;
}

ExpansionTankResult getExpansionTankResult() {
  TankSnapshot snap;
  takeSnapshot(snap);
  return resultFrom(snap);
}

bool saveExpansionTankAsFmtVolume(char *reason, size_t reasonSize) {
  const ExpansionTankResult r = getExpansionTankResult();
  if (stage != TANK_IDLE) {
    snprintf(reason, reasonSize, "The routine is still running");
    return false;
  }
  if (r.reliefs < 2 || !isfinite(r.mean) || !(r.mean > 1.0f)) {
    snprintf(reason, reasonSize, "At least 2 stable reliefs are needed");
    return false;
  }
  const float previous = FMTData.FMTVolume;
  FMTData.FMTVolume = r.mean;
  if (!writeFMTDataToNIV()) {
    FMTData.FMTVolume = previous;
    snprintf(reason, reasonSize, "Could not save to NVS");
    return false;
  }
  char details[160];
  snprintf(details, sizeof(details), "FMT volume %.2f -> %.2f L (sd %.2f L, %u of %u reliefs used, tank %.3f L)", previous,
           r.mean, r.sd, r.reliefs, r.total, r.tankVolume);
  calibrationEventAppend("fmt-volume-saved", details);
  return true;
}

String expansionTankRowsHtml() {
  TankSnapshot snap;
  takeSnapshot(snap);
  String rows;
  for (uint8_t i = 0; i < snap.count; ++i) {
    const TankRelief &k = snap.r[i];
    rows += "<tr><td>" + String(i + 1) + "</td><td>" + String(k.p1, 4) + "</td><td>" + String(k.p2, 4) + "</td><td>" +
      String(k.fermenterK1 - 273.15f, 2) + " / " + String(k.fermenterK2 - 273.15f, 2) + "</td><td>" +
      (isfinite(k.tankK2) ? String(k.tankK2 - 273.15f, 2) : String("n/a")) + "</td><td>" + String(k.volume, 3) + "</td><td>" +
      String(k.simpleVolume, 3) + "</td><td>" + (reliefStable(k) ? "yes" : "<b>no</b> (excluded)") + "</td></tr>";
  }
  return rows;
}

void handleExpansionTankCSV(AsyncWebServerRequest *request) {
  // The finished reliefs of a run in progress are exported as they are, from a
  // copy taken under the lock (no flash writes); every relief, with its stability marks.
  const bool running = expansionTankActive();
  TankSnapshot snap;
  takeSnapshot(snap);
  String csv = "relief;date_time;tank_L;P1_bar;P2_bar;T_ferm1_C;T_ferm2_C;T_tank1_C;T_tank2_C;patm1_bar;patm2_bar;"
               "wait_P1_s;wait_P2_s;vent_s;settled_P1;settled_P2;V_fermenter_L;V_simple_L";
  // P2 wait checkpoints: pressure, fermenter and ambient temperature at 30, 60, 120, 180 and 300 s after opening
  for (uint8_t c = 0; c < TANK_CHECKPOINTS; ++c) csv += ";P2_" + String(TANK_CHECKPOINT_S[c]) + "s_bar";
  for (uint8_t c = 0; c < TANK_CHECKPOINTS; ++c) csv += ";Tferm_" + String(TANK_CHECKPOINT_S[c]) + "s_C";
  for (uint8_t c = 0; c < TANK_CHECKPOINTS; ++c) csv += ";Tenv_" + String(TANK_CHECKPOINT_S[c]) + "s_C";
  csv += ";Pmin_open_bar;t_Pmin_s;open_samples;P2_tail_mbar;P2_tau_s";
  // P1 wait checkpoints: pressure and fermenter temperature at 60, 120, 180, 240, 300 and 420 s after the valve closed
  for (uint8_t c = 0; c < TANK_P1_CHECKPOINTS; ++c) csv += ";P1_" + String(TANK_P1_CHECKPOINT_S[c]) + "s_bar";
  for (uint8_t c = 0; c < TANK_P1_CHECKPOINTS; ++c) csv += ";Tferm_P1_" + String(TANK_P1_CHECKPOINT_S[c]) + "s_C";
  csv += ";build\n";
  auto add = [&csv](float v, int d) {
    if (isfinite(v)) {
      String t(v, (unsigned int)d);
      t.replace(".", ",");
      csv += t;
    }
    csv += ';';
  };
  for (uint8_t i = 0; i < snap.count; ++i) {
    const TankRelief &k = snap.r[i];
    char when[20];
    formatLocalEpochISO(k.epoch, when, sizeof(when));
    csv += String(i + 1) + ';' + when + ';';
    add(snap.tank, 3); add(k.p1, 6); add(k.p2, 6);
    add(k.fermenterK1 - 273.15f, 2); add(k.fermenterK2 - 273.15f, 2);
    add(k.tankK1 - 273.15f, 2); add(k.tankK2 - 273.15f, 2);
    add(k.patm1, 5); add(k.patm2, 5);
    add(k.waitP1, 0); add(k.waitP2, 0); add(k.ventSeconds, 0);
    csv += String(k.settled1 ? "yes" : "no") + ';' + (k.settled2 ? "yes" : "no") + ';';
    add(k.volume, 4);
    add(k.simpleVolume, 4);
    for (uint8_t c = 0; c < TANK_CHECKPOINTS; ++c) add(k.cpP[c], 6);
    for (uint8_t c = 0; c < TANK_CHECKPOINTS; ++c) add(k.cpTf[c] - 273.15f, 2);
    for (uint8_t c = 0; c < TANK_CHECKPOINTS; ++c) add(k.cpTe[c] - 273.15f, 2);
    add(k.pMin, 6); add(k.tMin, 2); add((float)k.openSamples, 0);
    add(k.tail * 1000.0f, 2); add(k.tau, 0);
    for (uint8_t c = 0; c < TANK_P1_CHECKPOINTS; ++c) add(k.cp1P[c], 6);
    for (uint8_t c = 0; c < TANK_P1_CHECKPOINTS; ++c) add(k.cp1Tf[c] - 273.15f, 2);
    csv += BUILD_ID;
    csv += '\n';
  }
  AsyncWebServerResponse *response = request->beginResponse(200, "text/csv", csv);
  response->addHeader("Content-Disposition",
                      "attachment; filename=\"" + calibrationFileName("FermenterVolume", snap.epoch, running) + "\"");
  request->send(response);
}
