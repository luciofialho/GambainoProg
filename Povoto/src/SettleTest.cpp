#include <Arduino.h>
#include <math.h>
#include <new>
#include "SettleTest.h"
#include "VentingTest.h"
#include "VentModel.h"
#include "PressureWait.h"
#include "PressureControl.h"
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "GambainoCommon.h"
#include "PovotoTasks.h"
#include "PovotoFilesystem.h"
#include "CalibrationEvents.h"
#include "BuildId.h"

unsigned long int NTPEpoch(); // IOTK_NTP (not #included: see AtmosphericPressure.cpp)

// Settle validation test: see SettleTest.h.
static constexpr const char *ST_FILE = "/settle.csv";
static constexpr float ST_MIN_START_PRESSURE = 1.85f;     // bar gauge
static constexpr float ST_END_PRESSURE = 0.1f;            // bar gauge: below it the test ends
static constexpr unsigned long ST_WAIT_MS = 20UL * 60UL * 1000UL;
static constexpr unsigned long ST_INITIAL_MIN_MS = 45000UL, ST_INITIAL_MAX_MS = 300000UL;
static constexpr unsigned long ST_STABLE_WINDOW_MS = 30000UL;
static constexpr float ST_INITIAL_DELTA_BAR = 0.00015f;   // strict, in 30 s
static constexpr unsigned long ST_AVERAGE_MS = 10000UL;   // P1 = mean of the last 10 s of the wait
static constexpr unsigned long ST_CLOSE_READING_MS = 500UL;
static constexpr unsigned long ST_FLUSH_MS = 20000UL;
static constexpr float ST_STEP_MIN_SECONDS = 3.0f, ST_STEP_MAX_SECONDS = 90.0f;
static constexpr float ST_STEP_EXPONENT = 1.22f;          // polytropic exponent of a step (measured 1.2-1.3)

// value: seconds of the opening (level) or the settled pressure to go to (step), bar gauge.
struct SettleStep { bool step; float value; };
static const SettleStep SEQUENCE[] = {{false, 8.0f}, {false, 2.0f}, {false, 0.5f}, {true, 1.1f},
                                      {false, 8.0f}, {false, 2.0f}, {true, 0.55f}, {false, 8.0f}};
static constexpr uint8_t ST_COUNT = sizeof(SEQUENCE) / sizeof(SEQUENCE[0]);

enum SettleStage : uint8_t { ST_IDLE, ST_INITIAL, ST_OPEN, ST_SETTLE };
static SettleStage stage = ST_IDLE;
static PressureWait *wait = nullptr;
static uint8_t releaseIndex = 0;                 // 0-based
static float p1 = NAN, pClose = NAN, requestedSeconds = NAN, realSeconds = NAN;
static unsigned long stageMillis = 0, openPlannedMs = 0, lastFlushMillis = 0, lastPanelMillis = 0;
static int64_t openMicros = 0;
static uint32_t lastLoggedElapsed = 0, rows = 0, startEpoch = 0;
static bool firstRowOfWait = true;
static String buffer;
static volatile bool abortRequested = false;
static char statusText[160] = "Idle";

bool settleTestActive() {
  return stage != ST_IDLE;
}

static float ambientNow() {
  return isfinite(environmentTemp) && environmentTemp != NOTaTEMP && environmentTemp > -20.0f && environmentTemp < 60.0f
      ? environmentTemp : NAN;
}

static void appendNumber(String &line, float value, uint8_t decimals) {
  if (isfinite(value)) {
    String text(value, (unsigned int)decimals);
    text.replace(".", ",");
    line += text;
  }
  line += ';';
}

static void flushBuffer() {
  if (!buffer.length()) return;
  File f = povotoDataFS().open(ST_FILE, "a");
  if (f) {
    f.print(buffer);
    f.close();
  }
  buffer = "";
  lastFlushMillis = millis();
}

// Time the opening must last to take the settled pressure from p1v to target (the model of the fit:
// t = k S, same integral, polytropic exponent of the step).
static float stepSeconds(float p1v, float target) {
  if (!(p1v > target + 0.02f)) return ST_STEP_MIN_SECONDS;
  const double tK = 273.15 + (isfinite(ControlData.temperature) ? ControlData.temperature : 20.0f);
  const double s = VentModel::releaseIntegral(FMTData.FMTVolume, tK, p1v, target, Patm, ST_STEP_EXPONENT, false,
                                              (double)FMTData.ventCurve.beta);
  const double seconds = (double)FMTData.ventCurve.k * s;
  return isfinite(seconds) ? fminf(ST_STEP_MAX_SECONDS, fmaxf(ST_STEP_MIN_SECONDS, (float)seconds)) : ST_STEP_MIN_SECONDS;
}

static const char *kindText() {
  if (stage == ST_INITIAL) return "initial";
  return SEQUENCE[releaseIndex].step ? "step" : "level";
}

// One row per new reading of a wait; the columns of the release only on its first row.
static void logSample(uint32_t elapsedMs, float pressure) {
  String line;
  line += String(stage == ST_INITIAL ? 0 : releaseIndex + 1) + ';';
  if (firstRowOfWait) {
    line += String(kindText()) + ';';
    appendNumber(line, requestedSeconds, 2);
    appendNumber(line, realSeconds, 4);
    appendNumber(line, p1, 6);
    appendNumber(line, pClose, 6);
    firstRowOfWait = false;
  } else {
    line += ";;;;;";
  }
  appendNumber(line, elapsedMs / 1000.0f, 1);
  appendNumber(line, pressure, 6);
  appendNumber(line, ControlData.temperature + 273.15f, 2);
  const float ambient = ambientNow();
  if (isfinite(ambient)) {
    String text(ambient, 2U);
    text.replace(".", ",");
    line += text;
  }
  line += '\n';
  buffer += line;
  ++rows;
  if (buffer.length() > 1500 || millis() - lastFlushMillis >= ST_FLUSH_MS) flushBuffer();
}

static void finish(const char *outcome) {
  routineValve(false);
  flushBuffer();
  stage = ST_IDLE;
  delete wait;
  wait = nullptr;
  char details[200];
  snprintf(details, sizeof(details), "%s after %u of %u releases, %lu rows in %s [%s]", outcome, (unsigned)releaseIndex, (unsigned)ST_COUNT,
           (unsigned long)rows, ST_FILE, BUILD_ID);
  calibrationEventAppend("settle-test", details);
  snprintf(statusText, sizeof(statusText), "%s: %u of %u releases, %lu rows; download /settle/file", outcome, (unsigned)releaseIndex,
           (unsigned)ST_COUNT, (unsigned long)rows);
  showCalibrationPanel("Settle test: END", outcome, "File: /settle/file");
}

static void openRelease(unsigned long now) {
  const SettleStep &s = SEQUENCE[releaseIndex];
  requestedSeconds = s.step ? stepSeconds(p1, s.value) : s.value;
  realSeconds = NAN;
  pClose = NAN;
  openPlannedMs = (unsigned long)lroundf(requestedSeconds * 1000.0f);
  openMicros = routineValve(true);
  stageMillis = now;
  stage = ST_OPEN;
}

static bool startSettleTest(char *reason, size_t reasonSize) {
  const char *blocked = nullptr;
  if (stage != ST_IDLE) blocked = "A settle test is already running";
  else if (debugging) blocked = "Not available on debug boards";
  else if (SetPointData.mode != MODE_OFF) blocked = "Mode must be OFF";
  else if (taskWindowType != 0 || calibrationRoutineActive() || inTheMiddleOfRelief()) blocked = "Process in progress";
  else if (!isfinite(ControlData.pressure) || ControlData.pressure < ST_MIN_START_PRESSURE)
    blocked = "Insufficient pressure (min 1.85 bar; start at ~2.0 bar and wait ~15 min)";
  if (!blocked) {
    wait = new (std::nothrow) PressureWait;
    if (!wait) blocked = "Out of memory";
  }
  if (!blocked) {
    File f = povotoDataFS().open(ST_FILE, "w");
    if (!f) {
      blocked = "Could not open the file";
      delete wait;
      wait = nullptr;
    } else {
      f.print("release;kind;t_requested_s;t_real_s;P1_bar;P_close_bar;seconds_since_close;pressure_bar;temp_ferm_K;temp_amb_C\n");
      f.close();
    }
  }
  if (blocked) {
    snprintf(reason, reasonSize, "%s", blocked);
    return false;
  }
  buffer = "";
  rows = 0;
  releaseIndex = 0;
  p1 = pClose = requestedSeconds = realSeconds = NAN;
  startEpoch = NTPEpoch();
  lastLoggedElapsed = 0;
  firstRowOfWait = true;
  abortRequested = false;
  lastFlushMillis = lastPanelMillis = stageMillis = millis();
  wait->reset(millis());
  stage = ST_INITIAL;
  snprintf(statusText, sizeof(statusText), "Running");
  return true;
}

void processSettleTest() {
  if (stage == ST_IDLE) return;
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
    else snprintf(reason, sizeof(reason), "Aborted (%.2f bar is above the %.2f bar security pressure)", ControlData.pressure,
                  FMTData.maximumPressure);
    finish(reason);
    return;
  }
  const unsigned long now = millis();
  const unsigned long acquisition = pressureAcquisitionMillis();
  if (stage == ST_INITIAL || stage == ST_SETTLE) {
    wait->sample(now, ControlData.pressure, acquisition, stage == ST_SETTLE ? ST_CLOSE_READING_MS : 0);
    if (wait->count > 0 && wait->lastSampleElapsed() != lastLoggedElapsed) {
      lastLoggedElapsed = wait->lastSampleElapsed();
      if (stage == ST_SETTLE && !isfinite(pClose)) pClose = wait->first;
      logSample(lastLoggedElapsed, wait->newest(0));
    }
  }
  if (stage == ST_INITIAL) {
    const uint32_t last = wait->lastSampleElapsed();
    bool done = wait->count > 0 && last >= ST_INITIAL_MAX_MS;
    if (!done && wait->count > 0 && last >= ST_INITIAL_MIN_MS && last >= ST_STABLE_WINDOW_MS) {
      const float change = wait->change(ST_STABLE_WINDOW_MS);
      done = isfinite(change) && fabsf(change) < ST_INITIAL_DELTA_BAR;
    }
    if (done) {
      p1 = wait->mean(ST_AVERAGE_MS);
      flushBuffer();
      openRelease(now);
    }
  } else if (stage == ST_OPEN && now - stageMillis >= openPlannedMs) {
    const int64_t closeMicros = routineValve(false);
    realSeconds = float((closeMicros - openMicros) / 1e6);
    wait->reset(now);
    lastLoggedElapsed = 0;
    firstRowOfWait = true;
    stageMillis = now;
    stage = ST_SETTLE;
  } else if (stage == ST_SETTLE && now - stageMillis >= ST_WAIT_MS) {
    p1 = wait->mean(ST_AVERAGE_MS);
    flushBuffer();
    ++releaseIndex;
    if (releaseIndex >= ST_COUNT) {
      finish("Completed");
    } else if (!isfinite(p1) || p1 < ST_END_PRESSURE) {
      finish("Completed (pressure below the limit)");
    } else {
      lastLoggedElapsed = 0;
      firstRowOfWait = true;
      openRelease(now);
    }
  }
  if (stage != ST_IDLE && now - lastPanelMillis >= 1000UL) {
    lastPanelMillis = now;
    char line2[48], line3[48];
    snprintf(line2, sizeof(line2), "%s %u/%u | %lu min", kindText(), (unsigned)(stage == ST_INITIAL ? 0 : releaseIndex + 1),
             (unsigned)ST_COUNT, (now - stageMillis) / 60000UL);
    snprintf(line3, sizeof(line3), "P: %.3f bar", ControlData.pressure);
    showCalibrationPanel("Settle test: RUN", line2, line3);
  }
}

// ----- URLs -----
static String statusPage() {
  String text;
  if (stage == ST_IDLE) {
    text = String(statusText);
  } else {
    char line[200];
    const unsigned long elapsed = (millis() - stageMillis) / 1000UL;
    if (stage == ST_INITIAL)
      snprintf(line, sizeof(line), "Running: initial wait (a stable pressure before the first release), %lu s, pressure %.4f bar", elapsed,
               ControlData.pressure);
    else if (stage == ST_OPEN)
      snprintf(line, sizeof(line), "Running: release %u of %u (%s), valve open for %.2f s, pressure %.4f bar", (unsigned)(releaseIndex + 1),
               (unsigned)ST_COUNT, kindText(), requestedSeconds, ControlData.pressure);
    else
      snprintf(line, sizeof(line), "Running: release %u of %u (%s, %.3f s), wait %lu s of %lu s, pressure %.4f bar", (unsigned)(releaseIndex + 1),
               (unsigned)ST_COUNT, kindText(), realSeconds, elapsed, ST_WAIT_MS / 1000UL, ControlData.pressure);
    text = line;
    text += ", " + String((unsigned long)rows) + " rows logged";
  }
  File f = povotoDataFS().open(ST_FILE, "r");
  const size_t bytes = f ? f.size() : 0;
  if (f) f.close();
  return "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='10'><title>Settle test</title>"
         "<style>body{font-family:Arial,sans-serif;margin:16px}</style></head><body><h2>Settle test</h2><p>" + text +
         "</p><p>File /settle.csv: " + String((unsigned long)bytes) + " bytes</p><p><a href='/settle/file'>Download</a> | "
         "<a href='/settle/abort?confirm=yes'>Abort</a></p></body></html>";
}

void handleSettleStart(AsyncWebServerRequest *request) {
  if (!request->hasParam("confirm") || request->getParam("confirm")->value() != "yes") {
    request->send(200, "text/plain",
                  "Settle test: 8 releases, each followed by a 20 min recovery logged at 1 reading per second (about 3 h). "
                  "Mode OFF, empty fermenter at ~2 bar for ~15 min. To start: /settle/start?confirm=yes");
    return;
  }
  char reason[100];
  if (!startSettleTest(reason, sizeof(reason))) {
    request->send(409, "text/plain", String("Settle test not started: ") + reason);
    return;
  }
  request->redirect("/settle/status");
}

void handleSettleStatus(AsyncWebServerRequest *request) {
  request->send(200, "text/html", statusPage());
}

void handleSettleAbort(AsyncWebServerRequest *request) {
  if (!request->hasParam("confirm") || request->getParam("confirm")->value() != "yes") {
    request->send(200, "text/plain", "To abort: /settle/abort?confirm=yes");
    return;
  }
  if (stage != ST_IDLE) abortRequested = true;
  request->redirect("/settle/status");
}

void handleSettleFile(AsyncWebServerRequest *request) {
  if (!povotoDataFS().exists(ST_FILE)) {
    request->send(404, "text/plain", "No settle test file");
    return;
  }
  // AsyncFileResponse always adds its own Content-Disposition (attachment with download = true, inline otherwise):
  // adding another one makes the browser refuse the response (ERR_RESPONSE_HEADERS_MULTIPLE_CONTENT_DISPOSITION).
  AsyncWebServerResponse *response = request->beginResponse(povotoDataFS(), ST_FILE, "text/csv", true);
  request->send(response);
}
