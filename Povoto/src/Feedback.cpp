#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include "Feedback.h"
#include "FeedbackMath.h"
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "PressureControl.h"
#include "PovotoTasks.h"
#include "GambainoCommon.h"
#include "CalibrationEvents.h"
#include "TemperatureControl.h"
#include "datalog.h"

unsigned long int NTPEpoch(); // IOTK_NTP (not #included: see AtmosphericPressure.cpp)

// docs/feedback.md
using namespace FeedbackMath;

static constexpr uint8_t FEEDBACK_STORE_VERSION = 1;
static constexpr uint8_t LEAK_NO_DATA = 0, LEAK_OK = 1, LEAK_POSSIBLE = 2;
static constexpr unsigned long SAVE_INTERVAL_MS = 3600000UL;
static unsigned long leakSampleMs() { return debugging ? 5000UL : 30000UL; }
static unsigned long leakWindowMs() { return debugging ? 300000UL : 3600000UL; }
static unsigned long ventSettleMs() { return debugging ? 60000UL : 180000UL; }
// After a task (its nucleation rest included), a set point change, a mode change or a boot.
static unsigned long eventSettleMs() {
  if (debugging) return 120000UL;
  return (unsigned long)max(FMTData.nucleationWindow, 30) * 60000UL;
}
// Temperature stable for at least this long; in Conditioning the dissolution of the CO2 in the cold beer
// also lowers the pressure, for hours after the temperature settles.
static uint32_t temperatureStableSeconds() {
  if (debugging) return 300UL;
  return SetPointData.mode == MODE_CONDITIONING ? 6UL * 3600UL : 2UL * 3600UL;
}

struct FeedbackStore_t {
  uint8_t version;
  double keptProduct;          // product of the moles kept by each vent of the batch
  float exponents[RING];       // measured exponents that passed the filters, newest at exponentNext - 1
  uint8_t exponentCount, exponentNext;
  float ratios[RING];          // fermenter side / flow of the vents where the check applied
  uint8_t ratioCount, ratioNext;
  float lastExponent, lastRatio, lastApparent, lastAirFraction;
  uint8_t leakState;           // LEAK_*
  uint8_t leakRun;             // consecutive windows that looked like a gas loss
  float leakSlope, leakSigma;  // mbar/min, last completed window
  uint32_t leakWindowEpoch;    // local NTP epoch at its end (0 = none)
  uint16_t leakWindows;        // completed windows in the batch
} __attribute__((packed));

static FeedbackStore_t defaultStore() {
  FeedbackStore_t s = {};
  s.version = FEEDBACK_STORE_VERSION;
  s.keptProduct = 1.0;
  s.lastExponent = s.lastRatio = s.lastApparent = s.lastAirFraction = NAN;
  s.leakSlope = s.leakSigma = NAN;
  return s;
}

static FeedbackStore_t store = defaultStore();
static bool dirty = false;
static unsigned long lastSaveMillis = 0;
// Relief whose exponent may enter the ring when the next one opens (0 = none).
static uint32_t eligibleRelief = 0;
// Leak window (RAM only: a boot starts a new one).
static LeakFit leakFit;
static unsigned long leakWindowStart = 0; // 0 = no window
static unsigned long lastLeakSample = 0;
static bool eventSeen = false;
static unsigned long lastEventMillis = 0, lastVentMillis = 0;
static const char *leakWaiting = "boot";

bool readFeedbackFromNIV() {
  Preferences nvs;
  if (!nvs.begin("pvt_feedback", true)) return false;
  FeedbackStore_t loaded;
  const size_t read = nvs.getBytes("store", &loaded, sizeof(loaded));
  nvs.end();
  if (read != sizeof(loaded) || loaded.version != FEEDBACK_STORE_VERSION) return false;
  if (loaded.exponentCount > RING || loaded.exponentNext >= RING || loaded.ratioCount > RING || loaded.ratioNext >= RING)
    return false;
  store = loaded;
  return true;
}

bool writeFeedbackToNIV() {
  Preferences nvs;
  if (!nvs.begin("pvt_feedback", false)) {
    Serial.println("NVS: cannot open pvt_feedback");
    return false;
  }
  const bool saved = nvs.putBytes("store", &store, sizeof(store)) == sizeof(store);
  nvs.end();
  if (!saved) Serial.println("NVS: feedback save incomplete");
  dirty = !saved;
  lastSaveMillis = millis();
  return saved;
}

static void push(float *ring, uint8_t &count, uint8_t &next, float value) {
  ring[next] = value;
  next = (next + 1) % RING;
  if (count < RING) ++count;
}

static void resetLeakWindow() {
  leakWindowStart = 0;
  leakFit.clear();
}

void feedbackFactoryReset() {
  store = defaultStore();
  dirty = false;
  eligibleRelief = 0;
  resetLeakWindow();
}

void feedbackResetForNewBatch() {
  feedbackFactoryReset();
  writeFeedbackToNIV();
}

// Gas at the start of the batch: air only, at the start pressure and temperature of the batch.
static double startAbs() {
  const float p = BatchData.startPressure;
  return (isfinite(p) && p >= 0.0f && p < 4.0f ? p : 0.0f) + Patm;
}

static double startKelvin() {
  const float t = BatchData.startTemperature;
  return (isfinite(t) && t > -5.0f && t < 60.0f ? t : 20.0f) + 273.15;
}

void feedbackVent(const FeedbackVent &vent) {
  store.keptProduct *= ventKeptFraction(vent.pOnGauge, vent.pAfterGauge, Patm, vent.gasKelvin);
  const float now = isfinite(vent.pAfterGauge) ? vent.pAfterGauge : vent.pOnGauge;
  const double air = airFraction(store.keptProduct, startAbs(), startKelvin(), now + Patm, vent.gasKelvin);
  store.lastAirFraction = float(air);
  eligibleRelief = vent.counted && air < AIR_FRACTION_LIMIT && taskWindowType == 0 ? vent.reliefNumber : 0;
  if (vent.ratioEvaluated && isfinite(vent.ratio)) {
    push(store.ratios, store.ratioCount, store.ratioNext, vent.ratio);
    store.lastRatio = vent.ratio;
  }
  if (isfinite(vent.apparentHeadspace)) store.lastApparent = vent.apparentHeadspace;
  dirty = true;
}

void feedbackExponent(float exponent, float rmseBar, uint32_t sourceRelief) {
  const bool valid = sourceRelief != 0 && sourceRelief == eligibleRelief && taskWindowType == 0 &&
      isfinite(exponent) && isfinite(rmseBar) && rmseBar <= EXPONENT_RMSE_LIMIT;
  eligibleRelief = 0;
  if (!valid) return;
  push(store.exponents, store.exponentCount, store.exponentNext, exponent);
  store.lastExponent = exponent;
  dirty = true;
}

static float gasKelvinOrNan() {
  const float t = gasTemperature();
  return isfinite(t) && t != NOTaTEMP && t > -20.0f && t < 60.0f ? t + 273.15f : NAN;
}

// nullptr when a leak window may run now; otherwise why not. Disturbances restart the settling.
static const char *leakBlocked(unsigned long now) {
  const uint8_t mode = SetPointData.mode;
  const char *event = nullptr;
  if (mode != MODE_FERMENTING && mode != MODE_CONDITIONING) event = "mode";
  else if (calibrationRoutineActive()) event = "calibration routine";
  else if (!pressureReadingValid() || pressureSensorAlarmActive()) event = "pressure sensor";
  else if (taskWindowType != 0) event = "task";
  else if (mode == MODE_FERMENTING && (CountersData.pressState == TEMP_STATE_CHANGING_DIRECT ||
                                       CountersData.pressState == TEMP_STATE_CHANGING_SLOW))
    event = "pressure set point changing";
  if (event) {
    eventSeen = true;
    lastEventMillis = now;
    return event;
  }
  if (inTheMiddleOfRelief() || ControlData.transferValve || ControlData.transferOverride == VALVE_OVERRIDE_OPEN) {
    lastVentMillis = now;
    return "vent";
  }
  if (now - lastEventMillis < eventSettleMs()) return "settling after a task, set point, mode or boot";
  if (lastVentMillis && now - lastVentMillis < ventSettleMs()) return "settling after a vent";
  if (!isfinite(gasKelvinOrNan())) return "gas temperature";
  if (CountersData.tempState != TEMP_STATE_STABLE || CountersData.tempStableSince == 0) return "temperature not stable";
  const uint32_t epoch = NTPEpoch();
  if (epoch == 0) return "no NTP time";
  if (epoch < CountersData.tempStableSince + temperatureStableSeconds()) return "temperature stable for too short";
  return nullptr;
}

static void evaluateLeakWindow() {
  double slope, sigma;
  if (!leakFit.slope(slope, sigma)) return;
  store.leakSlope = float(slope);
  store.leakSigma = float(sigma);
  store.leakWindowEpoch = NTPEpoch();
  if (store.leakWindows < 0xFFFF) ++store.leakWindows;
  if (leakLike(slope, sigma)) {
    if (store.leakRun < 0xFF) ++store.leakRun;
    if (store.leakRun >= LEAK_WINDOWS) store.leakState = LEAK_POSSIBLE;
  } else {
    store.leakRun = 0;
    store.leakState = LEAK_OK;
  }
  dirty = true;
}

static void processLeakWindow(unsigned long now) {
  const char *blocked = leakBlocked(now);
  if (blocked) {
    leakWaiting = blocked;
    if (leakWindowStart) resetLeakWindow();
    if (store.leakRun) { // only consecutive undisturbed windows count
      store.leakRun = 0;
      dirty = true;
    }
    return;
  }
  leakWaiting = "";
  if (inPressureNoiseWindow() || (lastLeakSample && now - lastLeakSample < leakSampleMs())) return;
  lastLeakSample = now;
  if (!leakWindowStart) {
    leakFit.clear();
    leakWindowStart = now;
  }
  leakFit.add((now - leakWindowStart) / 60000.0, ControlData.pressure + Patm, gasKelvinOrNan());
  if (now - leakWindowStart >= leakWindowMs() && leakFit.n >= 0.8 * leakWindowMs() / leakSampleMs()) {
    evaluateLeakWindow();
    resetLeakWindow();
  }
}

void processFeedback() {
  const unsigned long now = millis();
  if (!eventSeen) { // the boot counts as a disturbance
    eventSeen = true;
    lastEventMillis = now;
  }
  if (taskWindowType != 0) eligibleRelief = 0; // a task between two vents spoils the recovery
  processLeakWindow(now);
  if (dirty && now - lastSaveMillis >= SAVE_INTERVAL_MS) writeFeedbackToNIV();
}

// ----- Page -----

static String num(float v, int d) { return isfinite(v) ? String(v, d) : String("n/a"); }

static float exponentMedian() { return median(store.exponents, store.exponentCount); }

// Last nucleation estimate: false without one with a finite window.
static bool lastNucleationEstimate(String &when, int &minutes, String &quality) {
  String details;
  if (!lastCalibrationEventOfType("nucleation-estimate", when, details)) return false;
  const int w = details.indexOf("EstimatedWindowMin=");
  const int q = details.indexOf("Quality=");
  if (w < 0 || q < 0) return false;
  const int start = w + strlen("EstimatedWindowMin=");
  if (start >= (int)details.length() || !isDigit(details[start])) return false;
  minutes = details.substring(start).toInt();
  int end = q + strlen("Quality=");
  while (end < (int)details.length() && details[end] != ',' && details[end] != '(') ++end;
  quality = details.substring(q + strlen("Quality="), end);
  quality.trim();
  return minutes > 0;
}

static bool nucleationUsable(int minutes, const String &quality) {
  return quality == "ok" && minutes > 0 && minutes != FMTData.nucleationWindow;
}

static String useButton(const char *action, const char *label) {
  return String("<form action='/calibration/feedback/") + action + "' method='POST' style='display:inline'>"
         "<button class='btn-use'>" + label + "</button></form>";
}

String feedbackTabHtml() {
  String html = "<h2>Polytropic exponent</h2><p>Configured " + num(FMTData.FMTEffectiveVentingExponent, 3);
  const float med = exponentMedian();
  html += ". Measured: median " + num(med, 3) + " of " + String(store.exponentCount) + " vents, last " +
          num(store.lastExponent, 3) + ". Air in the headspace " + num(100.0f * store.lastAirFraction, 1) + "%.</p>";
  if (store.exponentCount < EXPONENT_MIN_COUNT)
    html += "<p>Use: after " + String(EXPONENT_MIN_COUNT) + " vents.</p>";
  else if (!(med >= 1.0f && med <= 1.30f))
    html += "<p>Outside 1.00-1.30: not usable.</p>";
  else
    html += "<p>" + useButton("exponent", ("Use " + String(med, 3)).c_str()) + "</p>";

  html += "<hr><h2>Nucleation window</h2><p>Configured " + String(FMTData.nucleationWindow) + " min. ";
  String when, quality;
  int minutes = 0;
  if (lastNucleationEstimate(when, minutes, quality)) {
    html += "Last estimate (" + when + "): " + String(minutes) + " min, " + quality + ".</p>";
    if (nucleationUsable(minutes, quality))
      html += "<p>" + useButton("nucleation", ("Use " + String(minutes) + " min").c_str()) + "</p>";
  } else {
    html += "No estimate.</p>";
  }

  const DailyHeadspaceLogData daily = getDailyHeadspaceLogData();
  html += "<hr><h2>Vent</h2><p>Fermenter side / flow: median " + num(median(store.ratios, store.ratioCount), 3) + " of " +
          String(store.ratioCount) + " vents, last " + num(store.lastRatio, 3) + ".<br>Apparent headspace: last " +
          num(store.lastApparent, 2) + " L, 24 h " + num(daily.daily, 2) + " L (" + daily.state + ", " +
          String(daily.hours) + " h), EMA " + num(daily.ema, 2) + " L.</p>";

  static const char *const states[] = {"no data", "ok", "<b>possible leak</b>"};
  html += "<hr><h2>Leak</h2><p>" + String(states[store.leakState <= LEAK_POSSIBLE ? store.leakState : 0]) + ". ";
  if (store.leakWindows) {
    char when2[20];
    formatLocalEpochISO(store.leakWindowEpoch, when2, sizeof(when2));
    html += "Last window (" + String(when2[0] ? when2 : "no time") + "): " + num(store.leakSlope, 3) + " &plusmn; " +
            num(store.leakSigma, 3) + " mbar/min; " + String(store.leakWindows) + " windows; " + String(store.leakRun) +
            " of " + String(LEAK_WINDOWS) + " in a row.";
  }
  html += "</p><p>Now: ";
  if (leakWindowStart)
    html += "window " + String((millis() - leakWindowStart) / 60000UL) + " of " + String(leakWindowMs() / 60000UL) + " min.";
  else
    html += String("waiting (") + leakWaiting + ").";
  html += "</p>";
  return html;
}

static void backToTab(AsyncWebServerRequest *request) { request->redirect("/calibration?tab=feedback"); }

void handleFeedbackUseExponent(AsyncWebServerRequest *request) {
  const float med = exponentMedian();
  if (store.exponentCount < EXPONENT_MIN_COUNT || !(med >= 1.0f && med <= 1.30f)) {
    request->send(409, "text/plain", "The measured exponent is not usable now.");
    return;
  }
  const float old = FMTData.FMTEffectiveVentingExponent;
  FMTData.FMTEffectiveVentingExponent = roundf(med * 1000.0f) / 1000.0f; // as the Physical tab shows it
  writeFMTDataToNIV();
  char details[96];
  snprintf(details, sizeof(details), "%.3f -> %.3f (median of %u vents)", old, FMTData.FMTEffectiveVentingExponent,
           (unsigned)store.exponentCount);
  calibrationEventAppend("exponent-from-feedback", details);
  backToTab(request);
}

void handleFeedbackUseNucleation(AsyncWebServerRequest *request) {
  String when, quality;
  int minutes = 0;
  if (!lastNucleationEstimate(when, minutes, quality) || !nucleationUsable(minutes, quality)) {
    request->send(409, "text/plain", "The nucleation estimate is not usable now.");
    return;
  }
  const int old = FMTData.nucleationWindow;
  FMTData.nucleationWindow = minutes;
  writeFMTDataToNIV();
  char details[96];
  snprintf(details, sizeof(details), "%d -> %d min (estimate of %s)", old, minutes, when.c_str());
  calibrationEventAppend("nucleation-from-feedback", details);
  backToTab(request);
}
