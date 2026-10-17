#include <Arduino.h>
#include <math.h>
#include "KegVolume.h"
#include "PressureWait.h"
#include "PressureControl.h"
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "GambainoCommon.h"
#include "PovotoTasks.h"
#include "VentModel.h"
#include "CalibrationEvents.h"
#include "datalog.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

unsigned long int NTPEpoch(); // IOTK_NTP (not #included: see AtmosphericPressure.cpp)

// docs/keg-volume.md
static constexpr uint8_t KEG_MAX_RELIEFS = 12;
static constexpr float KEG_END_PRESSURE = 0.5f; // the plan goes down to this gauge pressure
static constexpr float KEG_MIN_START_PRESSURE = 0.3f;
static constexpr float KEG_STABLE_SLOPE_BAR_PER_MIN = 0.0001f; // 0.1 mbar/min
static constexpr unsigned long KEG_SLOPE_WINDOW_MS = 120000UL;
static constexpr unsigned long KEG_AVERAGE_MS = 10000UL;
static unsigned long kegWaitMinMs() { return debugging ? 10000UL : 120000UL; }
static unsigned long kegWaitMaxMs() { return debugging ? 30000UL : 900000UL; }
static unsigned long kegVentMinMs() { return debugging ? 10000UL : 180000UL; }
static constexpr unsigned long KEG_VENT_MAX_MS = 900000UL;

struct KegRelief {
  uint32_t epoch;
  float p1, p2;                  // gauge, settled
  float fermenterK1, fermenterK2;
  float kegK1, kegK2;            // environment (the keg follows it); NAN -> fermenter
  float patm1, patm2;
  float waitP1, waitP2, ventSeconds;
  bool settled1, settled2;       // false: the wait hit its maximum
  float volume;                  // full mole balance
  float simpleVolume;            // V_E (T_F/T_E) P2 / (P1 - P2)
};

enum KegStage : uint8_t { KEG_IDLE, KEG_WAIT_P1, KEG_OPEN, KEG_VENTING };
static KegStage stage = KEG_IDLE;
static PressureWait *wait = nullptr;
static KegRelief reliefs[KEG_MAX_RELIEFS];
static uint8_t reliefCount = 0, reliefTarget = 0;
static float kegVolume = 0.0f;
static uint32_t runEpoch = 0;   // start of the run (CSV name)
// The page (web task) copies the finished reliefs while the loop works: the
// count and the start of a run change under this lock. Nothing is written to
// flash for the copy.
static portMUX_TYPE reliefLock = portMUX_INITIALIZER_UNLOCKED;
struct KegSnapshot {
  KegRelief r[KEG_MAX_RELIEFS];
  uint8_t count, target;
  float tank;
  uint32_t epoch;
};
static void takeSnapshot(KegSnapshot &out) {
  portENTER_CRITICAL(&reliefLock);
  out.count = reliefCount;
  out.target = reliefTarget;
  out.tank = kegVolume;
  out.epoch = runEpoch;
  memcpy(out.r, reliefs, sizeof(KegRelief) * reliefCount);
  portEXIT_CRITICAL(&reliefLock);
}
static bool reliefStable(const KegRelief &k) { return k.settled1 && k.settled2 && isfinite(k.volume); }
static unsigned long stageMillis = 0, ventWaitMs = 0;
static float simulated = NAN;
static char statusText[96] = "Not run";
// Set by the web handler; the loop aborts (the wait buffer is freed in the loop only).
static volatile bool abortRequested = false;

bool kegVolumeActive() { return stage != KEG_IDLE; }

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
  if (stage != KEG_IDLE) {
    snprintf(line1, sizeof(line1), "Fermenter volume: step %u/%u", reliefCount + 1, reliefTarget);
    snprintf(line2, sizeof(line2), "%s %lus",
             stage == KEG_WAIT_P1 ? "Settling P1" : stage == KEG_OPEN ? "Valve open, equalizing" : "Venting the tank",
             (now - stageMillis) / 1000UL);
    snprintf(line3, sizeof(line3), "P: %.4f bar", ControlData.pressure);
  } else {
    snprintf(line1, sizeof(line1), "Fermenter volume: END");
    snprintf(line2, sizeof(line2), "%s", statusText);
    snprintf(line3, sizeof(line3), "Result on the Calibration page");
  }
  showCalibrationPanel(line1, line2, line3);
}

String kegVolumeStatusText() {
  if (stage == KEG_IDLE) return String(statusText);
  char text[160];
  snprintf(text, sizeof(text), "Running: step %u of %u (planned), %s for %lu s, pressure %.4f bar", reliefCount + 1, reliefTarget,
           stage == KEG_WAIT_P1 ? "waiting for a stable P1" : stage == KEG_OPEN ? "valve open, waiting for a stable P2"
                                                                               : "venting the tank",
           (millis() - stageMillis) / 1000UL, ControlData.pressure);
  return String(text);
}

static void finish(const char *outcome) {
  routineValve(false);
  stage = KEG_IDLE;
  delete wait;
  wait = nullptr;
  const KegVolumeResult r = getKegVolumeResult();
  if (r.available) {
    snprintf(statusText, sizeof(statusText), "%s: %.2f L (sd %.2f L, %u of %u reliefs used)", outcome, r.mean, r.sd, r.reliefs,
             r.total);
    char details[160];
    snprintf(details, sizeof(details), "tank %.3f L, %u of %u reliefs used: %.3f L (sd %.3f; simple formula %.3f)%s", kegVolume,
             r.reliefs, r.total, r.mean, r.sd, r.simpleMean, strcmp(outcome, "Completed") ? " (aborted)" : "");
    calibrationEventAppend("fermenter-volume", details);
  } else {
    snprintf(statusText, sizeof(statusText), "%s without a usable relief", outcome);
  }
  showProgress(true);
}

// Web task: only requests it; processKegVolume() aborts in the main loop.
void abortKegVolume() {
  if (stage != KEG_IDLE) abortRequested = true;
}

// f = V_F / (V_F + V_E) is the pressure ratio of each relief (the absolute ratio
// is approximated by the gauge one): n = ceil(ln(0.5 / P) / ln f), from 1 to 12.
uint8_t kegVolumePlannedReliefs(float startPressure, float tankVolume) {
  if (!(startPressure > KEG_END_PRESSURE) || !(tankVolume > 0.0f) || !(FMTData.FMTVolume > 0.0f)) return 1;
  const double f = FMTData.FMTVolume / (FMTData.FMTVolume + (double)tankVolume);
  const double n = ceil(log(KEG_END_PRESSURE / startPressure) / log(f));
  return (uint8_t)constrain((int)n, 1, (int)KEG_MAX_RELIEFS);
}

bool startKegVolume(float volume, char *reason, size_t reasonSize) {
  const char *blocked = nullptr;
  if (SetPointData.mode != MODE_OFF) blocked = "Mode must be OFF";
  else if (taskWindowType != 0 || calibrationRoutineActive() || inTheMiddleOfRelief()) blocked = "Process in progress";
  else if (!isfinite(ControlData.pressure) || ControlData.pressure < KEG_MIN_START_PRESSURE)
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
  kegVolume = volume;
  reliefTarget = kegVolumePlannedReliefs(ControlData.pressure, volume);
  reliefCount = 0;
  runEpoch = NTPEpoch();
  portEXIT_CRITICAL(&reliefLock);
  if (FMTData.lastExpTankVolume != volume) { // the next run starts with it
    FMTData.lastExpTankVolume = volume;
    writeFMTDataToNIV();
  }
  routineValve(false); // de-energized: the keg vents through R
  simulated = ControlData.pressure;
  wait->reset(millis());
  stageMillis = millis();
  abortRequested = false;
  stage = KEG_WAIT_P1;
  snprintf(statusText, sizeof(statusText), "Running");
  showProgress(true);
  return true;
}

// Stable: at least the minimum wait and |slope of the last 2 min| < 0.1 mbar/min;
// at the maximum the wait ends anyway (settled = false).
static bool waitDone(bool &settled) {
  const uint32_t last = wait->lastSampleElapsed();
  settled = false;
  if (wait->count == 0 || last < kegWaitMinMs()) return false;
  const float slope = wait->slopePerMinute(KEG_SLOPE_WINDOW_MS);
  settled = isfinite(slope) && fabsf(slope) < KEG_STABLE_SLOPE_BAR_PER_MIN;
  // Bench boards: the short wait cannot meet the 120-s slope criterion; the simulated
  // pressure has settled by its end (tau 3 s), so it counts as stable.
  if (debugging && last >= kegWaitMaxMs()) settled = true;
  return settled || last >= kegWaitMaxMs();
}

// Full mole balance (ideal air): the fermenter loses what the keg gains.
static void computeRelief(KegRelief &k) {
  const float fermenterK2 = k.fermenterK2;
  const float kegK1 = isfinite(k.kegK1) ? k.kegK1 : k.fermenterK1;
  const float kegK2 = isfinite(k.kegK2) ? k.kegK2 : fermenterK2;
  const double fermenterLoss = (k.p1 + k.patm1) / k.fermenterK1 - (k.p2 + k.patm2) / fermenterK2;
  const double kegGain = (k.p2 + k.patm2) / kegK2 - k.patm1 / kegK1;
  k.volume = fermenterLoss > 0.0 ? float(kegVolume * kegGain / fermenterLoss) : NAN;
  k.simpleVolume = k.p1 > k.p2 ? kegVolume * (fermenterK2 / kegK2) * k.p2 / (k.p1 - k.p2) : NAN;
}

void processKegVolume() {
  if (stage == KEG_IDLE) return;
  if (abortRequested) {
    abortRequested = false;
    finish("Aborted");
    return;
  }
  if (SetPointData.mode != MODE_OFF || taskWindowType != 0 || !isfinite(ControlData.pressure) ||
      ControlData.pressure > FMTData.maximumPressure) {
    finish("Aborted (mode, task or pressure)");
    return;
  }
  const unsigned long now = millis();
  KegRelief &k = reliefs[reliefCount];
  if (debugging) { // bench boards: equalization with the FMT volume, tau 3 s
    if (stage == KEG_OPEN) {
      const float equal = (k.p1 + Patm) * FMTData.FMTVolume / (FMTData.FMTVolume + kegVolume) +
                          Patm * kegVolume / (FMTData.FMTVolume + kegVolume) - Patm;
      simulated = equal + (k.p1 - equal) * expf(-(now - stageMillis) / 3000.0f);
    }
    ControlData.pressure = simulated + random(-2, 3) * 0.00005f;
  }
  wait->sample(now, ControlData.pressure, debugging ? 0 : pressureAcquisitionMillis(), 1000);
  bool settled = false;
  if (stage == KEG_WAIT_P1 && waitDone(settled)) {
    k = {};
    k.epoch = NTPEpoch();
    k.p1 = wait->mean(KEG_AVERAGE_MS);
    k.fermenterK1 = ControlData.temperature + 273.15f;
    k.kegK1 = environmentKelvin();
    k.patm1 = Patm;
    k.settled1 = settled;
    k.waitP1 = wait->lastSampleElapsed() / 1000.0f;
    routineValve(true); // fermenter -> keg
    wait->reset(now);
    stageMillis = now;
    stage = KEG_OPEN;
    showProgress(true);
  } else if (stage == KEG_OPEN && waitDone(settled)) {
    k.p2 = wait->mean(KEG_AVERAGE_MS);
    k.fermenterK2 = ControlData.temperature + 273.15f;
    k.kegK2 = environmentKelvin();
    k.patm2 = Patm;
    k.settled2 = settled;
    k.waitP2 = wait->lastSampleElapsed() / 1000.0f;
    computeRelief(k);
    Serial.printf("[KEG] relief %u: P1 %.5f P2 %.5f bar, V_F %.3f L (simple %.3f)\n", reliefCount + 1, k.p1, k.p2,
                  k.volume, k.simpleVolume);
    routineValve(false); // the keg vents through R
    // A complete vent: about six time constants of the keg through the valve
    // (current curve with air), 3-15 min.
    const double tau = VentModel::tauRef(FMTData.ventCurve, k.p2) * kegVolume / VentModel::V_REF;
    ventWaitMs = (unsigned long)fmin((double)KEG_VENT_MAX_MS, fmax((double)kegVentMinMs(), 6000.0 * tau));
    if (debugging) ventWaitMs = kegVentMinMs();
    wait->reset(now);
    stageMillis = now;
    stage = KEG_VENTING;
    showProgress(true);
  } else if (stage == KEG_VENTING && now - stageMillis >= ventWaitMs) {
    k.ventSeconds = (now - stageMillis) / 1000.0f;
    portENTER_CRITICAL(&reliefLock);
    ++reliefCount;
    portEXIT_CRITICAL(&reliefLock);
    if (reliefCount >= reliefTarget || ControlData.pressure < KEG_MIN_START_PRESSURE * 0.5f) {
      finish("Completed");
      return;
    }
    // The fermenter was closed during the keg vent: its wait continues.
    stageMillis = now;
    stage = KEG_WAIT_P1;
    showProgress(true);
  }
  if (stage != KEG_IDLE) showProgress(false);
}

static KegVolumeResult resultFrom(const KegSnapshot &snap) {
  KegVolumeResult r = {};
  double sum = 0.0, squares = 0.0, simple = 0.0;
  uint8_t n = 0;
  for (uint8_t i = 0; i < snap.count; ++i) {
    if (!reliefStable(snap.r[i])) continue; // unstable steps stay out of the mean and the deviation
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
  r.fmtVolume = FMTData.FMTVolume;
  r.canSave = stage == KEG_IDLE && n >= 2;
  return r;
}

KegVolumeResult getKegVolumeResult() {
  KegSnapshot snap;
  takeSnapshot(snap);
  return resultFrom(snap);
}

bool saveKegVolumeAsFmtVolume(char *reason, size_t reasonSize) {
  const KegVolumeResult r = getKegVolumeResult();
  if (stage != KEG_IDLE) {
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

String kegVolumeRowsHtml() {
  KegSnapshot snap;
  takeSnapshot(snap);
  String rows;
  for (uint8_t i = 0; i < snap.count; ++i) {
    const KegRelief &k = snap.r[i];
    rows += "<tr><td>" + String(i + 1) + "</td><td>" + String(k.p1, 4) + "</td><td>" + String(k.p2, 4) + "</td><td>" +
      String(k.fermenterK1 - 273.15f, 2) + " / " + String(k.fermenterK2 - 273.15f, 2) + "</td><td>" +
      (isfinite(k.kegK2) ? String(k.kegK2 - 273.15f, 2) : String("n/a")) + "</td><td>" + String(k.volume, 3) + "</td><td>" +
      String(k.simpleVolume, 3) + "</td><td>" + (reliefStable(k) ? "yes" : "<b>no</b> (excluded)") + "</td></tr>";
  }
  return rows;
}

void handleKegVolumeCSV(AsyncWebServerRequest *request) {
  // The finished reliefs of a run in progress are exported as they are, from a
  // copy taken under the lock (no flash writes); every relief, with its stability marks.
  const bool running = kegVolumeActive();
  KegSnapshot snap;
  takeSnapshot(snap);
  String csv = "relief;date_time;tank_L;P1_bar;P2_bar;T_ferm1_C;T_ferm2_C;T_tank1_C;T_tank2_C;patm1_bar;patm2_bar;"
               "wait_P1_s;wait_P2_s;vent_s;settled_P1;settled_P2;V_fermenter_L;V_simple_L\n";
  auto add = [&csv](float v, int d) {
    if (isfinite(v)) {
      String t(v, (unsigned int)d);
      t.replace(".", ",");
      csv += t;
    }
    csv += ';';
  };
  for (uint8_t i = 0; i < snap.count; ++i) {
    const KegRelief &k = snap.r[i];
    char when[20];
    formatLocalEpochISO(k.epoch, when, sizeof(when));
    csv += String(i + 1) + ';' + when + ';';
    add(snap.tank, 3); add(k.p1, 6); add(k.p2, 6);
    add(k.fermenterK1 - 273.15f, 2); add(k.fermenterK2 - 273.15f, 2);
    add(k.kegK1 - 273.15f, 2); add(k.kegK2 - 273.15f, 2);
    add(k.patm1, 5); add(k.patm2, 5);
    add(k.waitP1, 0); add(k.waitP2, 0); add(k.ventSeconds, 0);
    csv += String(k.settled1 ? "yes" : "no") + ';' + (k.settled2 ? "yes" : "no") + ';';
    add(k.volume, 4);
    add(k.simpleVolume, 4);
    csv.remove(csv.length() - 1);
    csv += '\n';
  }
  AsyncWebServerResponse *response = request->beginResponse(200, "text/csv", csv);
  response->addHeader("Content-Disposition",
                      "attachment; filename=\"" + calibrationFileName("FermenterVolume", snap.epoch, running) + "\"");
  request->send(response);
}
