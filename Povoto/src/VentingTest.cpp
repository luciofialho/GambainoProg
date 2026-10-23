#include <Arduino.h>
#include <math.h>
#include <algorithm>
#include <memory>
#include "VentingTest.h"
#include "PressureWait.h"
#include "PressureControl.h"
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "GambainoCommon.h"
#include "PovotoTasks.h"
#include "PovotoFilesystem.h"
#include "AtmosphericPressure.h"
#include "CalibrationEvents.h"
#include "BuildId.h"
#include "SettleTest.h"
#include "datalog.h"
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

unsigned long int NTPEpoch(); // IOTK_NTP (not #included: see AtmosphericPressure.cpp)

// docs/venting-test.md
// Gauge bar. Start above the highest fermentation pressure + ~3% (~2.0-2.05 bar
// for 1.9); end below the lowest one (0.25 bar for fermentations at 0.3 bar:
// below ~0.4 bar, subsonic, the end value of the curve overestimates the flow).
static const float VT_LEVELS[] = {1.9f, 1.5f, 1.1f, 0.8f, 0.5f, 0.25f};
static constexpr uint8_t VT_LEVEL_COUNT = sizeof(VT_LEVELS) / sizeof(VT_LEVELS[0]);
// Alternating durations at one level; the 2 s at both ends give the drift.
static const float VT_DURATIONS[] = {2.0f, 0.5f, 8.0f, 1.0f, 4.0f, 2.0f};
static constexpr uint8_t VT_DURATION_COUNT = sizeof(VT_DURATIONS) / sizeof(VT_DURATIONS[0]);
// Start + 6 levels: 7 x 6 openings + 6 steps.
static constexpr uint8_t VT_MAX_RELEASES = 48;
static constexpr uint8_t VT_MAX_LEVELS = VT_LEVEL_COUNT + 1;
static constexpr float VT_END_PRESSURE = 0.1f;            // below it the test ends
// Reduced chi-square: noise floor of a settled reading and timing noise.
static constexpr float VT_NOISE_FLOOR_BAR = 0.00005f;
static constexpr float VT_TIMING_NOISE_S = 0.002f;
static constexpr float VT_MIN_START_PRESSURE = 0.8f;
static constexpr float VT_LEVEL_MARGIN = 0.15f;     // a level must be this far below the start
static constexpr float VT_STEP_MIN_SECONDS = 3.0f, VT_STEP_MAX_SECONDS = 90.0f;
// Polytropic exponent used to plan the first step of a test (1.2-1.3 measured in the steps of 20-21/10/2026).
static constexpr float VT_STEP_EXPONENT = 1.22f;
// A wait ends when the slope of the pressure, a least-squares line over the last VT_SLOPE_WINDOW_MS, falls below the
// limit. (Before, the difference of two single readings < 0.3 mbar in 30 s: with ~0.2 mbar of noise it stopped the waits
// while the pressure was still rising 1-2 mbar/min.) Around the releases of 1 s or less (the one that just ended or
// the next one) the limit is half: a settled-pressure error of ~1 mbar is 10% of the drop of a 0.5 s release.
static constexpr float VT_STABLE_SLOPE_MBAR_MIN = 0.6f;         // the same as 0.3 mbar in 30 s
static constexpr float VT_STABLE_SLOPE_STRICT_MBAR_MIN = 0.3f;
static constexpr float VT_SHORT_RELEASE_SECONDS = 1.0f;
static constexpr unsigned long VT_SLOPE_WINDOW_MS = 40000UL;
static constexpr unsigned long VT_AVERAGE_MS = 10000UL;
static constexpr unsigned long VT_CLOSE_READING_MS = 500UL;
static unsigned long vtWaitMinMs() { return debugging ? 5000UL : 45000UL; }
static unsigned long vtWaitMaxMs() { return debugging ? 10000UL : 300000UL; }
static constexpr uint8_t VT_KIND_LEVEL = 0, VT_KIND_STEP = 1;
// Bit of VentReleaseRaw::kind (the record size stays): the wait ended at the maximum, not stable.
static constexpr uint8_t VT_KIND_UNSETTLED = 0x80;

static const char *const LABELS[VENTING_LABEL_COUNT] = {
  "as-is", "muffler-cleaned", "valve-cleaned", "new-valve", "after-cleaning-cycle"};

const char *ventingTestLabelName(uint8_t label) {
  return label < VENTING_LABEL_COUNT ? LABELS[label] : "?";
}

// ----- History file -----
struct VentReleaseRaw {
  float requested, seconds;   // real opening time
  float p1, pClose, p2;       // gauge bar
  float tempK;                // fermenter at P1
  float ambientC;             // ambient temperature when the release opened (NAN = none received)
  float waitSeconds;          // settling after the release
  float pInf, tauSeconds, ampBar; // exponential recovery of the wait (PressureWait::fitRecovery): settled pressure, tau, amplitude
  float endSlope, tempEndK;       // least-squares slope of the last 60 s of the wait (bar/min) and the fermenter temperature at its end
  float p1Noise, p2Noise;     // standard error of the P1 / P2 means (bar)
  uint8_t level, kind;
} __attribute__((packed));

static inline uint8_t releaseKind(const VentReleaseRaw &x) { return x.kind & ~VT_KIND_UNSETTLED; }

struct VentTestRecord {
  uint32_t epoch;
  uint16_t batch;
  uint8_t label, co2, patmSource, count, fitted, reserved;
  float volume, patm, ambientC, fermenterC;
  char firmware[24];
  VentModel::Curve curve;
  float shapeRmsPercent, driftPercent, driftSePercent;
  float chiSquare;            // reduced chi-square of the fit
  float vsReference[3], vsSaved[3];
  VentReleaseRaw rel[VT_MAX_RELEASES];
} __attribute__((packed));

struct VentHistoryHeader {
  uint32_t magic;
  uint16_t slotSize;
  uint8_t count, next;
} __attribute__((packed));

// New file and magic with the (k, beta) curve: the cubic history "/venttests.bin" (magic "VTH3") is left alone for the fallback firmware.
static constexpr const char *VT_FILE = "/venttest6.bin";
static constexpr uint32_t VT_MAGIC = 0x38485456UL; // "VTH8" (48 releases, noise, chi-square; Patm source 0 SideKick / 1 altitude; curve k, beta without t0; ambient per release; P_inf, end slope and end temperature of each wait)
static constexpr uint8_t VT_HISTORY = 50;

static bool readHeader(File &f, VentHistoryHeader &h) {
  return f && f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && h.magic == VT_MAGIC &&
         h.slotSize == sizeof(VentTestRecord) && h.count <= VT_HISTORY && h.next < VT_HISTORY &&
         f.size() >= sizeof(h) + (size_t)h.count * sizeof(VentTestRecord);
}

uint8_t ventingTestHistoryCount() {
  File f = povotoDataFS().open(VT_FILE, "r");
  VentHistoryHeader h;
  const uint8_t count = readHeader(f, h) ? h.count : 0;
  if (f) f.close();
  return count;
}

static bool readRecord(uint8_t index, VentTestRecord &r) {
  File f = povotoDataFS().open(VT_FILE, "r");
  VentHistoryHeader h;
  bool ok = readHeader(f, h) && index < h.count;
  if (ok) {
    const uint8_t slot = (h.next + VT_HISTORY - 1 - index) % VT_HISTORY;
    ok = f.seek(sizeof(h) + (size_t)slot * sizeof(VentTestRecord)) &&
         f.read((uint8_t *)&r, sizeof(r)) == sizeof(r);
  }
  if (f) f.close();
  return ok;
}

static bool appendRecord(const VentTestRecord &r) {
  VentHistoryHeader h = {VT_MAGIC, sizeof(VentTestRecord), 0, 0};
  {
    File f = povotoDataFS().open(VT_FILE, "r");
    VentHistoryHeader old;
    if (readHeader(f, old)) h = old;
    if (f) f.close();
  }
  const uint8_t slot = h.next;
  File f = povotoDataFS().exists(VT_FILE) && h.count > 0 ? povotoDataFS().open(VT_FILE, "r+")
                                                         : povotoDataFS().open(VT_FILE, "w");
  if (!f) return false;
  h.next = (h.next + 1) % VT_HISTORY;
  if (h.count < VT_HISTORY) ++h.count;
  bool ok = f.seek(0) && f.write((const uint8_t *)&h, sizeof(h)) == sizeof(h) &&
            f.seek(sizeof(h) + (size_t)slot * sizeof(VentTestRecord)) &&
            f.write((const uint8_t *)&r, sizeof(r)) == sizeof(r);
  f.close();
  return ok;
}

// ----- Fit -----
static float tauRatioMinusOne(const VentModel::Curve &other, const VentModel::Curve &test, float p) {
  if (!VentModel::validCurve(other)) return NAN;
  return float(VentModel::tauRef(other, p, Patm) / VentModel::tauRef(test, p, Patm) - 1.0); // test flow / other flow - 1
}

static unsigned long lastFitMillis = 0; // duration of the last fit (it takes seconds on the board)

static void fitYield() { delay(1); } // let the other tasks run between the evaluations of the fit

static void fitRecord(VentTestRecord &r) {
  const unsigned long fitStart = millis();
  VentModel::Release rel[VT_MAX_RELEASES];
  uint8_t index[VT_MAX_RELEASES];
  float exponents[VT_MAX_RELEASES], residuals[VT_MAX_RELEASES], measured[VT_MAX_RELEASES];
  float noise1[VT_MAX_RELEASES], noise2[VT_MAX_RELEASES];
  uint8_t n = 0, nm = 0;
  for (uint8_t i = 0; i < r.count; ++i) {
    const VentReleaseRaw &x = r.rel[i];
    // The first release is left out: its P1 is the pressure of the start, not the settled P2 of a release, and it was the
    // outlier of every test (residual of -130 to -150 ms). It stays in the CSV.
    if (i == 0) continue;
    if (!(x.seconds > 0.2f) || !(x.p2 > 0.05f) || !(x.p1 > x.p2 + 0.001f) || !(x.tempK > 200.0f)) continue;
    rel[n] = {x.seconds, x.p1, x.pClose, x.p2, x.tempK};
    noise1[n] = x.p1Noise;
    noise2[n] = x.p2Noise;
    index[n++] = i;
    if (isfinite(x.pClose)) {
      const double m = VentModel::releaseExponent(x.p1, x.pClose, x.p2, r.patm, NAN);
      if (isfinite(m)) measured[nm++] = (float)m;
    }
  }
  r.fitted = 0;
  float fallback = 1.0f;
  if (nm > 0) {
    std::sort(measured, measured + nm);
    fallback = (nm % 2) ? measured[nm / 2] : 0.5f * (measured[nm / 2 - 1] + measured[nm / 2]);
  }
  VentModel::Curve curve = {};
  const bool fitOk = VentModel::fitVentingTest(rel, n, r.volume, r.patm, r.co2, fallback, curve, residuals,
                                               exponents, fitYield);
  lastFitMillis = millis() - fitStart;
  if (!fitOk) return;
  float pMin = INFINITY, pMax = 0.0f;
  for (uint8_t i = 0; i < n; ++i) { pMin = fminf(pMin, rel[i].p2); pMax = fmaxf(pMax, rel[i].p1); }
  curve.pMin = pMin;
  curve.pMax = pMax;
  curve.epoch = r.epoch;
  r.curve = curve;
  r.fitted = VentModel::validCurve(curve) ? 1 : 0;
  double shape = 0.0, shortSum = 0.0, shortSquares = 0.0, drift = 0.0;
  uint8_t shapeN = 0, shortN = 0, driftN = 0;
  for (uint8_t i = 0; i < n; ++i) {
    if (rel[i].seconds >= 4.0f) {
      const double relative = residuals[i] / rel[i].seconds;
      shape += relative * relative;
      ++shapeN;
    }
    if (rel[i].seconds <= 2.2f) {
      shortSum += residuals[i];
      shortSquares += (double)residuals[i] * residuals[i];
      ++shortN;
    }
  }
  for (uint8_t level = 0; level < VT_MAX_LEVELS; ++level) {
    int first = -1, last = -1;
    for (uint8_t i = 0; i < n; ++i) {
      const VentReleaseRaw &x = r.rel[index[i]];
      if (x.level != level || releaseKind(x) != VT_KIND_LEVEL || fabsf(x.requested - 2.0f) > 0.01f) continue;
      if (first < 0) first = i;
      last = i;
    }
    if (first >= 0 && last > first) {
      drift += (residuals[last] - residuals[first]) / 2.0;
      ++driftN;
    }
  }
  // The short releases are dominated by the reading noise (~15-45 ms): the drift is judged against it.
  const double shortSd = shortN > 1 ? sqrt(fmax(0.0, (shortSquares - shortSum * shortSum / shortN) / (shortN - 1))) : NAN;
  r.shapeRmsPercent = shapeN ? float(100.0 * sqrt(shape / shapeN)) : NAN;
  r.driftPercent = driftN ? float(100.0 * drift / driftN) : NAN;
  r.driftSePercent = driftN ? float(100.0 * shortSd * sqrt(2.0) / 2.0 / sqrt((double)driftN)) : NAN;
  // Residuals against the noise of each reading: ~1 when the model follows the
  // data; well above 1 (limit 3), the shape does not follow.
  r.chiSquare = float(VentModel::reducedChiSquare(curve, rel, noise1, noise2, residuals, exponents, n, r.volume,
                                                  r.patm, r.co2, VT_NOISE_FLOOR_BAR, VT_TIMING_NOISE_S));
  const float pressures[3] = {1.8f, 1.2f, 0.8f};
  for (uint8_t i = 0; i < 3; ++i) {
    r.vsReference[i] = tauRatioMinusOne(FMTData.ventReferenceCurve, curve, pressures[i]);
    r.vsSaved[i] = tauRatioMinusOne(FMTData.ventCurve, curve, pressures[i]);
  }
}

static void summaryFrom(const VentTestRecord &r, VentingTestSummary &s) {
  s.available = true;
  s.epoch = r.epoch;
  s.batch = r.batch;
  s.label = r.label;
  s.co2 = r.co2;
  s.volume = r.volume;
  s.patm = r.patm;
  s.patmSource = r.patmSource;
  s.ambientC = r.ambientC;
  s.fermenterC = r.fermenterC;
  s.releases = r.count;
  s.fitted = r.fitted;
  s.curve = r.curve;
  s.shapeRmsPercent = r.shapeRmsPercent;
  s.chiSquare = r.chiSquare;
  s.driftPercent = r.driftPercent;
  s.driftSePercent = r.driftSePercent;
  for (uint8_t i = 0; i < 3; ++i) {
    s.vsReference[i] = r.vsReference[i];
    s.vsSaved[i] = r.vsSaved[i];
  }
}

bool getVentingTestSummary(uint8_t index, VentingTestSummary &out) {
  out = {};
  VentTestRecord *r = new (std::nothrow) VentTestRecord;
  if (!r) return false;
  const bool ok = readRecord(index, *r);
  if (ok) summaryFrom(*r, out);
  delete r;
  return ok;
}

uint8_t defaultVentingTestLabel() {
  return lastCalibrationEventType() == "valve-cleaning" ? 4 : 0;
}

bool saveVentingTestCurve(uint8_t index, bool reference, char *reason, size_t reasonSize) {
  VentingTestSummary s;
  if (ventingTestActive() || !getVentingTestSummary(index, s) || !s.fitted || !VentModel::validCurve(s.curve)) {
    snprintf(reason, reasonSize, "No valid fitted curve in this test");
    return false;
  }
  if (SetPointData.mode == MODE_FERMENTING) {
    snprintf(reason, reasonSize, "Curves are saved between batches only (mode is Fermenting)");
    return false;
  }
  if (reference) FMTData.ventReferenceCurve = s.curve;
  else FMTData.ventCurve = s.curve;
  if (!writeFMTDataToNIV()) {
    snprintf(reason, reasonSize, "Could not save to NVS");
    return false;
  }
  char when[20], details[160];
  formatLocalEpochISO(s.epoch, when, sizeof(when));
  snprintf(details, sizeof(details), "test of %s (%s): k %.3f s, beta %.4f, %.2f-%.2f bar", when,
           ventingTestLabelName(s.label), s.curve.k, s.curve.beta, s.curve.pMin, s.curve.pMax);
  calibrationEventAppend(reference ? "reference-curve-saved" : "flow-curve-saved", details);
  return true;
}

// ----- Test -----
enum VtStage : uint8_t { VT_IDLE, VT_WAIT_P1, VT_OPEN, VT_SETTLE };
static VtStage stage = VT_IDLE;
static VentTestRecord *test = nullptr;  // the running test
static PressureWait *wait = nullptr;
static float levelTargets[VT_MAX_LEVELS];
static uint8_t levelCount = 0, currentLevel = 0, durationIndex = 0;
static uint8_t plannedReleases = 0;     // all the levels: 6 releases each and a step between them (at most 48; the test also ends below 0.1 bar)
static bool stepNext = false;           // the next release steps down to the next level
static float currentP1 = NAN;
static float currentP1Noise = NAN; // standard error of the P1 of the next release
static unsigned long stageMillis = 0, openPlannedMs = 0;
static int64_t openMicros = 0;
static volatile float waitSlope = NAN;  // mbar/min over the last 40 s of the wait (the web task never touches `wait`)
static volatile float stableLimit = VT_STABLE_SLOPE_MBAR_MIN; // limit of the current wait (mbar/min)
static float simulatedPressure = NAN;   // debug boards
static char statusText[96] = "Idle";
// Set by the web handler; the loop aborts (buffers are freed in the loop only).
static volatile bool abortRequested = false;

bool ventingTestActive() {
  return stage != VT_IDLE || settleTestActive(); // the settle test (SettleTest.cpp) is a valve routine as well
}

// The page (web task) copies the finished releases of the running test while
// the loop writes it: `test`, its count and its deletion change under this lock.
// Nothing is written to flash for the copy.
static portMUX_TYPE testLock = portMUX_INITIALIZER_UNLOCKED;

static void releaseBuffers() {
  portENTER_CRITICAL(&testLock);
  VentTestRecord *oldTest = test;
  test = nullptr;
  portEXIT_CRITICAL(&testLock);
  delete oldTest;
  delete wait;
  wait = nullptr;
}

// Copy of the running test with its finished releases; false when none runs.
static bool copyRunningTest(VentTestRecord &out) {
  bool ok = false;
  portENTER_CRITICAL(&testLock);
  if (stage != VT_IDLE && test) {
    memcpy(&out, test, sizeof(out));
    ok = true;
  }
  portEXIT_CRITICAL(&testLock);
  return ok;
}

static float ambientNow() {
  return isfinite(environmentTemp) && environmentTemp != NOTaTEMP && environmentTemp > -20.0f && environmentTemp < 60.0f
      ? environmentTemp : NAN;
}

// Polytropic exponent to plan a step: that of the previous step of this test, else the median of the
// releases of 4 s or more of the current level, else VT_STEP_EXPONENT.
static float stepExponent() {
  float last = NAN, longE[VT_DURATION_COUNT];
  uint8_t n = 0;
  for (uint8_t i = 0; i < test->count; ++i) {
    const VentReleaseRaw &x = test->rel[i];
    const float e = (float)VentModel::releaseExponent(x.p1, x.pClose, x.p2, Patm, NAN);
    if (!isfinite(e)) continue;
    if (releaseKind(x) == VT_KIND_STEP) last = e;
    else if (x.level == currentLevel && x.requested >= 4.0f && n < VT_DURATION_COUNT) longE[n++] = e;
  }
  if (isfinite(last)) return last;
  if (n) {
    std::sort(longE, longE + n);
    return (n % 2) ? longE[n / 2] : 0.5f * (longE[n / 2 - 1] + longE[n / 2]);
  }
  return VT_STEP_EXPONENT;
}

// Opening that takes the settled pressure from p1 to the next level: the model of the fit itself,
// t = k S with the release integral (docs/venting-test.md), the curve in use and the polytropic
// exponent of the steps. The old formula (a constant rebound and no cooling) landed 18-47 mbar low.
static float stepSeconds(float p1, float target) {
  if (!(p1 > target + 0.02f)) return VT_STEP_MIN_SECONDS;
  const double tK = 273.15 + (isfinite(ControlData.temperature) ? ControlData.temperature : 20.0f);
  const double s = VentModel::releaseIntegral(test->volume, tK, p1, target, Patm, stepExponent(), test->co2,
                                              (double)FMTData.ventCurve.beta);
  const double seconds = (double)FMTData.ventCurve.k * s;
  return isfinite(seconds) ? fminf(VT_STEP_MAX_SECONDS, fmaxf(VT_STEP_MIN_SECONDS, (float)seconds)) : VT_STEP_MIN_SECONDS;
}

static void showProgress(bool force) {
  static unsigned long lastShown = 0;
  const unsigned long now = millis();
  if (!force && now - lastShown < 1000UL) return;
  lastShown = now;
  char line1[48], line2[64], line3[64];
  if (stage != VT_IDLE) {
    snprintf(line1, sizeof(line1), "Venting test: RUN level %u/%u", currentLevel + 1, levelCount);
    snprintf(line2, sizeof(line2), "Release %u/%u | %s %lus", test ? test->count + 1 : 0, plannedReleases,
             stage == VT_OPEN ? "open" : "settling", (now - stageMillis) / 1000UL);
    snprintf(line3, sizeof(line3), "P: %.3f bar (%s)", ControlData.pressure, ventingTestLabelName(test ? test->label : 0));
  } else {
    snprintf(line1, sizeof(line1), "Venting test: END");
    snprintf(line2, sizeof(line2), "%s", statusText);
    snprintf(line3, sizeof(line3), "Result on the Calibration page");
  }
  showCalibrationPanel(line1, line2, line3);
}

String ventingTestProgressText() {
  if (stage == VT_IDLE) return String(statusText);
  char text[220];
  int n = snprintf(text, sizeof(text), "Running: level %u of %u, release %u/%u, %s for %lu s, pressure %.3f bar",
                   currentLevel + 1, levelCount, test->count + 1, plannedReleases,
                   stage == VT_OPEN ? "valve open" : "waiting for a stable pressure", (millis() - stageMillis) / 1000UL,
                   ControlData.pressure);
  if (stage != VT_OPEN && n > 0 && n < (int)sizeof(text)) {
    const float slope = waitSlope; // mbar/min over the last 40 s
    if (isfinite(slope))
      snprintf(text + n, sizeof(text) - n, ", slope %+.2f mbar/min (limit %.2f)", slope, (float)stableLimit);
  }
  return String(text);
}

static void finishTest(const char *outcome) {
  routineValve(false);
  stage = VT_IDLE;
  if (test && test->count > 0) {
    fitRecord(*test);
    const bool saved = appendRecord(*test);
    char details[240];
    snprintf(details, sizeof(details), "%s, %s, %u releases, %s; chi2 %.2f, drift %+.2f%%, "
             "vs reference %+.2f/%+.2f/%+.2f%%, fit %lu ms%s", ventingTestLabelName(test->label), test->co2 ? "CO2" : "air",
             test->count, outcome, test->chiSquare, test->driftPercent,
             100.0f * test->vsReference[0], 100.0f * test->vsReference[1], 100.0f * test->vsReference[2],
             lastFitMillis, saved ? "" : " (history NOT saved)");
    calibrationEventAppend("venting-test", details);
    snprintf(statusText, sizeof(statusText), "%s: %u releases%s%s", outcome, test->count,
             test->fitted ? ", fitted" : ", fit failed", saved ? "" : ", history not saved");
  } else {
    snprintf(statusText, sizeof(statusText), "%s before the first release", outcome);
  }
  releaseBuffers();
  showProgress(true);
}

// Web task: only requests it; processVentingTest() aborts in the main loop.
void abortVentingTest() {
  if (stage != VT_IDLE) abortRequested = true;
}

bool startVentingTest(uint8_t label, bool co2, float volume, char *reason, size_t reasonSize) {
  const char *blocked = nullptr;
  if (label >= VENTING_LABEL_COUNT) blocked = "Invalid label";
  else if (SetPointData.mode != MODE_OFF) blocked = "Mode must be OFF";
  else if (taskWindowType != 0 || calibrationRoutineActive() || inTheMiddleOfRelief()) blocked = "Process in progress";
  else if (!isfinite(ControlData.pressure) || ControlData.pressure < VT_MIN_START_PRESSURE)
    blocked = "Insufficient pressure (min 0.8 bar; start at ~1.9 bar)";
  else if (!(volume > 1.0f && volume <= 1000.0f)) blocked = "Invalid test volume";
  if (!blocked) {
    test = new (std::nothrow) VentTestRecord;
    wait = new (std::nothrow) PressureWait;
    if (!test || !wait) {
      releaseBuffers();
      blocked = "Out of memory";
    }
  }
  if (blocked) {
    snprintf(reason, reasonSize, "%s", blocked);
    return false;
  }
  memset(test, 0, sizeof(*test));
  test->epoch = NTPEpoch();
  test->batch = BatchData.batchNumber;
  test->label = label;
  test->co2 = co2;
  test->volume = volume;
  test->patm = Patm;
  test->patmSource = atmosphericPressureSource();
  test->ambientC = ambientNow();
  test->fermenterC = ControlData.temperature;
  snprintf(test->firmware, sizeof(test->firmware), "%s", BUILD_ID); // 20 characters, fits the 24 of the stored record
  // Level 1 is the start pressure; then each table level well below it.
  levelCount = 0;
  levelTargets[levelCount++] = ControlData.pressure;
  for (uint8_t i = 0; i < VT_LEVEL_COUNT && levelCount < VT_MAX_LEVELS; ++i)
    if (VT_LEVELS[i] < ControlData.pressure - VT_LEVEL_MARGIN) levelTargets[levelCount++] = VT_LEVELS[i];
  plannedReleases = (uint8_t)fminf(VT_MAX_RELEASES, levelCount * VT_DURATION_COUNT + levelCount - 1);
  currentLevel = 0;
  durationIndex = 0;
  stepNext = false;
  simulatedPressure = ControlData.pressure;
  wait->reset(millis());
  waitSlope = NAN;
  stableLimit = VT_STABLE_SLOPE_MBAR_MIN;
  stageMillis = millis();
  abortRequested = false;
  stage = VT_WAIT_P1;
  snprintf(statusText, sizeof(statusText), "Running");
  showProgress(true);
  return true;
}

static bool waitDone() {
  const uint32_t last = wait->lastSampleElapsed();
  if (wait->count == 0) return false;
  const float slope = last >= VT_SLOPE_WINDOW_MS ? wait->slopePerMinute(VT_SLOPE_WINDOW_MS) : NAN; // bar/min
  waitSlope = isfinite(slope) ? 1000.0f * slope : NAN; // also for the status text
  if (last >= vtWaitMaxMs()) return true;
  if (last < vtWaitMinMs() || last < VT_SLOPE_WINDOW_MS) return false;
  return isfinite(waitSlope) && fabsf(waitSlope) < stableLimit;
}

static void openRelease(float p1, unsigned long now) {
  VentReleaseRaw &x = test->rel[test->count];
  memset(&x, 0, sizeof(x));
  x.p1 = p1;
  x.p1Noise = currentP1Noise;
  x.pClose = x.p2 = x.p2Noise = NAN;
  x.pInf = x.tauSeconds = x.ampBar = x.endSlope = x.tempEndK = NAN;
  x.tempK = 273.15f + ControlData.temperature;
  x.ambientC = ambientNow(); // SideKick packet every 60 s: it can be missing for a while after a reboot
  x.level = currentLevel;
  if (stepNext) {
    x.kind = VT_KIND_STEP;
    x.requested = stepSeconds(p1, levelTargets[currentLevel + 1]);
  } else {
    x.kind = VT_KIND_LEVEL;
    x.requested = VT_DURATIONS[durationIndex];
  }
  openPlannedMs = (unsigned long)lroundf(x.requested * 1000.0f);
  openMicros = routineValve(true);
  stageMillis = now;
  stage = VT_OPEN;
}

void processVentingTest() {
  processSettleTest();
  if (stage == VT_IDLE) return;
  if (abortRequested) {
    abortRequested = false;
    finishTest("Aborted");
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
    finishTest(reason);
    return;
  }
  const unsigned long now = millis();
  VentReleaseRaw &x = test->rel[test->count];
  if (debugging) { // bench boards: exponential release with the current curve
    if (stage == VT_OPEN) {
      const double tau = VentModel::tauRef(FMTData.ventCurve, x.p1, Patm) * test->volume / VentModel::V_REF;
      const double t = (now - stageMillis) / 1000.0;
      simulatedPressure = float((x.p1 + Patm) * exp(-t / tau) - Patm);
    }
    ControlData.pressure = simulatedPressure + random(-3, 4) * 0.0001f;
  }
  const unsigned long acquisition = debugging ? 0 : pressureAcquisitionMillis();
  if (stage == VT_WAIT_P1) {
    wait->sample(now, ControlData.pressure, acquisition, 0);
    if (waitDone()) {
      currentP1 = wait->mean(VT_AVERAGE_MS);
      currentP1Noise = wait->meanStandardError(VT_AVERAGE_MS);
      openRelease(currentP1, now);
      showProgress(true);
    }
  } else if (stage == VT_OPEN && now - stageMillis >= openPlannedMs) {
    const int64_t closeMicros = routineValve(false);
    x.seconds = float((closeMicros - openMicros) / 1e6);
    // Stability limit of the wait that follows: strict next to a short release (this one or the next).
    const bool levelRelease = releaseKind(x) == VT_KIND_LEVEL;
    const bool shortNow = levelRelease && x.requested <= VT_SHORT_RELEASE_SECONDS;
    const bool shortNext = levelRelease && durationIndex + 1 < VT_DURATION_COUNT &&
                           VT_DURATIONS[durationIndex + 1] <= VT_SHORT_RELEASE_SECONDS;
    stableLimit = (shortNow || shortNext) ? VT_STABLE_SLOPE_STRICT_MBAR_MIN : VT_STABLE_SLOPE_MBAR_MIN;
    wait->reset(now);
    waitSlope = NAN;
    stageMillis = now;
    stage = VT_SETTLE;
    showProgress(true);
  } else if (stage == VT_SETTLE) {
    wait->sample(now, ControlData.pressure, acquisition, VT_CLOSE_READING_MS);
    if (waitDone()) {
      x.pClose = wait->first;
      x.p2 = wait->mean(VT_AVERAGE_MS);
      x.p2Noise = currentP1Noise = wait->meanStandardError(VT_AVERAGE_MS); // the next P1
      x.waitSeconds = wait->lastSampleElapsed() / 1000.0f;
      // Settled pressure extrapolated from the recovery of the wait: logged next to the P2 read (it is not used by the fit).
      float pInf = NAN, tauSeconds = NAN, ampBar = NAN; // (x is packed: no references to its fields)
      if (!wait->fitRecovery(pInf, tauSeconds, ampBar)) pInf = tauSeconds = ampBar = NAN;
      x.pInf = pInf;
      x.tauSeconds = tauSeconds;
      x.ampBar = ampBar;
      // The pressure still falling at the end of the wait is a leak or the gas still cooling (10 mbar per K at 2 bar): both are logged.
      x.endSlope = wait->slopePerMinute(60000UL);
      x.tempEndK = 273.15f + ControlData.temperature;
      const float slope = waitSlope; // set by waitDone() in this same pass
      if (!(isfinite(slope) && fabsf(slope) < stableLimit)) x.kind |= VT_KIND_UNSETTLED;
      portENTER_CRITICAL(&testLock);
      ++test->count;
      portEXIT_CRITICAL(&testLock);
      // Next release: the next duration, the step down, or the end.
      if (releaseKind(x) == VT_KIND_STEP) {
        ++currentLevel;
        durationIndex = 0;
        stepNext = false;
      } else if (++durationIndex >= VT_DURATION_COUNT) {
        stepNext = currentLevel + 1 < levelCount;
        if (!stepNext) {
          finishTest("Completed");
          return;
        }
      }
      if (test->count >= VT_MAX_RELEASES || x.p2 < VT_END_PRESSURE) {
        finishTest("Completed");
        return;
      }
      // millis() again, not `now`: the extrapolation above takes ~0.1-0.2 s, and the opening time is counted from this instant.
      openRelease(x.p2, millis());
      showProgress(true);
    }
  }
  if (stage != VT_IDLE) showProgress(false);
}

// ----- CSV -----
static void csvNumber(String &line, float value, uint8_t decimals) {
  if (isfinite(value)) {
    String text(value, (unsigned int)decimals);
    text.replace(".", ",");
    line += text;
  }
  line += ';';
}

struct VentCsvExport {
  int16_t testIndex;     // next test (history index)
  int16_t lastTest;      // last test to export
  uint8_t releaseIndex;  // next release of the current test
  bool headerSent;
  bool loaded;
  bool live;             // the running test (a copy of its finished releases)
  VentTestRecord record;
};

static bool appendCsvRow(VentCsvExport &e, String &line) {
  const VentTestRecord &r = e.record;
  const VentReleaseRaw &x = r.rel[e.releaseIndex];
  char date[24];
  formatLocalEpochISO(r.epoch, date, sizeof(date));
  line = "";
  line += (e.live ? String("current") : String(e.testIndex)) + ';' + date + ';' + String(r.epoch) + ';' + String(r.batch) + ';' +
          ventingTestLabelName(r.label) + ';' + (r.co2 ? "CO2" : "air") + ';';
  csvNumber(line, r.volume, 3);
  csvNumber(line, r.patm, 5);
  line += String(atmosphericPressureSourceLabel(r.patmSource)) + ';';
  line += String(r.firmware) + ';' + String(e.releaseIndex + 1) + ';' + String(x.level + 1) + ';' +
          (releaseKind(x) == VT_KIND_STEP ? "step" : "level") + ';';
  csvNumber(line, x.requested, 2);
  csvNumber(line, x.seconds, 4);
  csvNumber(line, x.p1, 6);
  csvNumber(line, x.pClose, 6);
  csvNumber(line, x.p2, 6);
  csvNumber(line, x.pInf, 6);
  csvNumber(line, x.tauSeconds, 1);
  csvNumber(line, 1000.0f * x.ampBar, 2);
  csvNumber(line, 1000.0f * x.endSlope, 3);
  csvNumber(line, x.tempEndK, 2);
  csvNumber(line, x.tempK, 2);
  csvNumber(line, x.ambientC, 2);
  csvNumber(line, x.waitSeconds, 1);
  line += (x.kind & VT_KIND_UNSETTLED ? "no" : "yes");
  line += ';';
  csvNumber(line, 1000.0f * x.p1Noise, 4);
  csvNumber(line, 1000.0f * x.p2Noise, 4);
  csvNumber(line, r.fitted ? r.curve.k : NAN, 4);
  csvNumber(line, r.fitted ? r.curve.beta : NAN, 5);
  csvNumber(line, r.shapeRmsPercent, 3);
  csvNumber(line, r.chiSquare, 3);
  csvNumber(line, r.driftPercent, 3);
  csvNumber(line, r.driftSePercent, 3);
  for (uint8_t i = 0; i < 3; ++i) csvNumber(line, 100.0f * r.vsReference[i], 3);
  line.remove(line.length() - 1);
  line += '\n';
  return true;
}

void handleVentingTestCSV(AsyncWebServerRequest *request) {
  const String which = request->hasParam("test") ? request->getParam("test")->value() : "0";
  const uint8_t count = ventingTestHistoryCount();
  // Freed with the response, also when the client disconnects.
  std::shared_ptr<VentCsvExport> e(new (std::nothrow) VentCsvExport);
  if (!e) {
    request->send(500, "text/plain", "Out of memory");
    return;
  }
  e->live = false;
  String fileName;
  if (which == "current") {
    if (!copyRunningTest(e->record)) {
      request->send(404, "text/plain", "No venting test is running");
      return;
    }
    // Only finished releases; the fit columns do not exist yet.
    e->live = true;
    e->record.fitted = 0;
    e->record.shapeRmsPercent = e->record.chiSquare = NAN;
    e->record.driftPercent = e->record.driftSePercent = NAN;
    for (uint8_t i = 0; i < 3; ++i) e->record.vsReference[i] = e->record.vsSaved[i] = NAN;
    fileName = calibrationFileName("Venting", e->record.epoch, true);
  } else if (count == 0) {
    request->send(404, "text/plain", "No venting test in the history");
    return;
  }
  if (e->live) {
    e->testIndex = e->lastTest = 0;
  } else {
    e->testIndex = which == "all" ? count - 1 : (int16_t)constrain(which.toInt(), 0, count - 1);
    e->lastTest = which == "all" ? 0 : e->testIndex; // oldest first when exporting all
    if (which == "all") {
      fileName = calibrationFileName("VentingHistory", NTPEpoch(), false);
    } else {
      fileName = calibrationFileName("Venting", readRecord((uint8_t)e->testIndex, e->record) ? e->record.epoch : 0, false);
    }
  }
  e->releaseIndex = 0;
  e->headerSent = false;
  e->loaded = e->live;
  AsyncWebServerResponse *response = request->beginChunkedResponse("text/csv",
      [e](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        (void)index;
        size_t len = 0;
        String line;
        if (!e->headerSent) {
          line = "test;date_time;epoch;batch;label;gas;volume_L;patm_bar;patm_source;"
                 "firmware;release;level;kind;t_requested_s;t_real_s;P1_bar;P_close_bar;P2_bar;Pinf_bar;Pinf_tau_s;Pinf_amp_mbar;end_slope_mbar_min;temp_end_K;temp_ferm_K;temp_amb_C;wait_s;settled;"
                 "P1_noise_mbar;P2_noise_mbar;"
                 "fit_K_s;fit_beta;shape_rms_pct;chi2_reduced;drift_pct;drift_se_pct;"
                 "vs_ref_1_8_pct;vs_ref_1_2_pct;vs_ref_0_8_pct\n";
          if (line.length() > maxLen) return 0;
          memcpy(buffer, line.c_str(), line.length());
          len = line.length();
          e->headerSent = true;
        }
        while (e->testIndex >= e->lastTest) {
          if (!e->loaded) {
            if (!readRecord((uint8_t)e->testIndex, e->record)) e->record.count = 0;
            e->loaded = true;
            e->releaseIndex = 0;
          }
          if (e->releaseIndex >= e->record.count || e->releaseIndex >= VT_MAX_RELEASES) {
            --e->testIndex;
            e->loaded = false;
            continue;
          }
          appendCsvRow(*e, line);
          if (len + line.length() > maxLen) return len;
          memcpy(buffer + len, line.c_str(), line.length());
          len += line.length();
          ++e->releaseIndex;
        }
        return len;
      });
  response->addHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
  request->send(response);
}
