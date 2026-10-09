#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "GambainoCommon.h"
#include "PovotoTasks.h"
#include "PovotoData.h"
#include "PressureControl.h"
#include "PovotoCommon.h"
#include "datalog.h"
#include "IOTK.h"
#include <IOTK_NTP.h>

byte taskWindowType = 0;
unsigned long taskWindowEndTime = 0;
unsigned long lastTaskMillis = 0;
static float dumpStartPressureBar = 0.0f;
static float dumpStartHeadspaceL = 0.0f;
static float dumpStartBeerVolumeL = 0.0f;
static bool taskRestWindowActive = false;

bool tasksAllowed() {
  return SetPointData.mode != MODE_CONDITIONING;
}

// ===== Task log (one row per task, datalog.h) =====
// Opened at the start, closed at the end (finished, timeout or cancelled) and
// sent after the pressure samples that follow it; a new task sends it earlier,
// with the samples still missing left empty.
static const unsigned long TASK_LOG_SAMPLE_SECONDS[TASK_LOG_PRESSURE_SAMPLES] = {60, 180, 300, 600};
static TaskLogData taskLog;
static bool taskLogOpen = false;    // task running
static bool taskLogPending = false; // task ended, collecting the samples
static unsigned long taskLogStartMillis = 0;
static unsigned long taskLogEndMillis = 0;
static uint8_t taskLogNextSample = 0;
static const char *taskEndOutcome = "finished"; // "timeout" while checkTaskExpiration ends it

static void sendTaskLog() {
  if (!taskLogPending) return;
  taskLogPending = false;
  doTaskDataLog(taskLog);
}

static void closeTaskLog(const char *outcome, const DumpRecalcResult *dump) {
  if (!taskLogOpen) return; // finishing again during nucleation
  taskLogOpen = false;
  const unsigned long now = millis();
  const DissolvedCO2LogData co2 = getDissolvedCO2LogData();
  taskLog.outcome = outcome;
  taskLog.endEpoch = NTPEpoch();
  taskLog.durationSeconds = (now - taskLogStartMillis) / 1000.0f;
  taskLog.temperature = ControlData.temperature;
  taskLog.environmentTemperature = environmentTemp;
  taskLog.atmosphericPressure = Patm;
  taskLog.co2Mode = co2.mode;
  taskLog.gasRate = co2.gasRate;
  taskLog.pressureEnd = ControlData.pressure;
  if (dump) {
    taskLog.pressureEndIso = dump->pressureAfterIsoBar;
    taskLog.deltaH = dump->deltaH;
  }
  taskLog.headSpaceAfter = CountersData.headSpaceVolume;
  taskLog.beerVolume = beerVolume;
  taskLog.dumpedVolume = CountersData.dumpedVolume;
  taskLogEndMillis = now;
  taskLogNextSample = 0;
  taskLogPending = true;
}

static void openTaskLog(byte type) {
  closeTaskLog("cancelled", nullptr); // a task replaced before being finished
  sendTaskLog();
  taskLog = {};
  taskLog.task = taskWindowTypeToText(type);
  taskLog.outcome = "";
  taskLog.co2Mode = "";
  taskLog.startEpoch = NTPEpoch();
  taskLog.pressureStart = ControlData.pressure;
  taskLog.headSpaceBefore = CountersData.headSpaceVolume;
  taskLog.pressureEndIso = NAN;
  taskLog.deltaH = NAN;
  for (uint8_t i = 0; i < TASK_LOG_PRESSURE_SAMPLES; ++i) taskLog.pressureAfter[i] = NAN;
  taskLogStartMillis = millis();
  taskLogOpen = true;
}

void collectTaskLogSamples() {
  if (!taskLogPending) return;
  const unsigned long elapsed = millis() - taskLogEndMillis;
  while (taskLogNextSample < TASK_LOG_PRESSURE_SAMPLES &&
         elapsed >= TASK_LOG_SAMPLE_SECONDS[taskLogNextSample] * 1000UL) {
    taskLog.pressureAfter[taskLogNextSample++] = ControlData.pressure;
  }
  if (taskLogNextSample >= TASK_LOG_PRESSURE_SAMPLES) sendTaskLog();
}

static void startTask(byte type) {
  if (!tasksAllowed()) return;
  openTaskLog(type);
  taskRestWindowActive = false;
  lastTaskMillis = millis();
  taskWindowType = type;
  taskWindowEndTime = lastTaskMillis + (unsigned long)TASK_TIMEOUT_MIN * 60000UL;
}

static void endTask(unsigned long restWindowMinutes) {
  // Finishing again during nucleation must not extend its deadline.
  if (restWindowMinutes && taskRestWindowActive) return;
  taskRestWindowActive = restWindowMinutes != 0;
  lastTaskMillis = millis();
  if (restWindowMinutes)
    taskWindowEndTime = lastTaskMillis + restWindowMinutes * 60000UL;
  else {
    taskWindowType = 0;
    taskWindowEndTime = 0;
  }
}


void cancelActiveTask() {
  closeTaskLog("cancelled", nullptr);
  if (taskWindowType != 0) endTask(0);
}

void startDumpTask() {
  if (!tasksAllowed()) return;
  dumpStartPressureBar = ControlData.pressure;
  dumpStartHeadspaceL = CountersData.headSpaceVolume;
  dumpStartBeerVolumeL = beerVolume;
  startTask(1);
}
void startGasTask()            { startTask(2); }
void startLiquidTask()         { startTask(3); }
void startDryHoppingTask()     { startTask(4); }
void startDynamicHoppingTask() { startTask(5); }

void endDumpTask() {
  const DumpRecalcResult dump =
    applyDumpWindowHeadspaceRecalc(dumpStartHeadspaceL, dumpStartPressureBar, ControlData.pressure);
  // The headspace recalculation treats the pressure loss during a dump as
  // liquid removal. Accumulate only a validated, positive inferred loss.
  const float dumpedThisTask = dumpStartBeerVolumeL - beerVolume;
  scaleDissolvedCO2ForBeerVolume(dumpStartBeerVolumeL, beerVolume);
  if (isfinite(dumpedThisTask) && dumpedThisTask > 0.0f) {
    CountersData.dumpedVolume += dumpedThisTask;
    writeCountersDataToNIV();
  }
  closeTaskLog(taskEndOutcome, &dump);
  endTask(0);
}

void endGasTask() {
  closeTaskLog(taskEndOutcome, nullptr);
  endTask(FMTData.nucleationWindow);
}

// [DAILY-HS] These change the volume by an unknown amount: the stored hours
// no longer apply, and the average restarts after 18 new hours.
void endLiquidTask() {
  clearDailyHeadspace("liquid");
  closeTaskLog(taskEndOutcome, nullptr);
  endTask(FMTData.nucleationWindow);
}

void endDryHoppingTask() {
  clearDailyHeadspace("dryhop");
  closeTaskLog(taskEndOutcome, nullptr);
  endTask(FMTData.nucleationWindow);
}

void endDynamicHoppingTask() {
  clearDailyHeadspace("dynhop");
  closeTaskLog(taskEndOutcome, nullptr);
  endTask(FMTData.nucleationWindow);
}

void checkTaskExpiration() {
  if (taskWindowType != 0 && taskWindowEndTime != 0 && MILLISDIFF(taskWindowEndTime,0)) {
    if (taskRestWindowActive) {
      endTask(0);
      return;
    }
    taskEndOutcome = "timeout";
    switch (taskWindowType) {
      case 1: endDumpTask(); break;
      case 2: endGasTask(); break;
      case 3: endLiquidTask(); break;
      case 4: endDryHoppingTask(); break;
      case 5: endDynamicHoppingTask(); break;
      default: closeTaskLog(taskEndOutcome, nullptr); endTask(0); break;
    }
    taskEndOutcome = "finished";
  }
}

// ========== TASKS HANDLERS ==========

static const char* taskName(byte type) {
  switch (type) {
    case 1: return "Dump";
    case 2: return "Gas venting/injection";
    case 3: return "Liquid addition";
    case 4: return "Dry hopping";
    case 5: return "Dynamic hopping";
    default: return "Unknown";
  }
}

void handleTasksPage(AsyncWebServerRequest *request) {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Tasks</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; }";
  html += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 30px; border-radius: 15px; box-shadow: 0 10px 30px rgba(0,0,0,0.3); }";
  html += "h1 { color: #333; text-align: center; margin-bottom: 30px; }";
  html += ".menu-grid { display: grid; grid-template-columns: 1fr; gap: 15px; margin-top: 20px; }";
  html += ".menu-button { display: block; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; text-decoration: none; text-align: center; border-radius: 10px; font-size: 18px; font-weight: bold; transition: transform 0.2s; }";
  html += ".menu-button:hover { transform: translateY(-2px); }";
  html += ".back-link { text-align: center; margin-top: 20px; }";
  html += ".back-link a { color: #666; font-size: 14px; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>&#9881;&#65039; Tasks</h1>";
  if (!tasksAllowed()) {
    html += "<p style='text-align:center;color:#555;'>Tasks are disabled in Conditioning: the fermenter works as a plain refrigerator.</p>";
    html += "<div class='back-link'><a href='/'>&#8592; Back to menu</a></div>";
    html += "</div></body></html>";
    request->send(200, "text/html", html);
    return;
  }
  if (taskWindowType != 0) {
    html += "<div style='background:#fff3cd;border:1px solid #ffc107;border-radius:8px;padding:12px;margin-bottom:20px;text-align:center;font-weight:bold;color:#856404;'>";
    html += "Active task: ";
    html += taskName(taskWindowType);
    html += " &mdash; <a href='/tasks/active'>Go to task</a></div>";
  }
  html += "<div class='menu-grid'>";
  html += "<a href='/tasks/start?type=1' class='menu-button'>Dump</a>";
  html += "<a href='/tasks/start?type=2' class='menu-button'>Gas venting/injection</a>";
  html += "<a href='/tasks/start?type=3' class='menu-button'>Liquid addition</a>";
  html += "<a href='/tasks/start?type=4' class='menu-button'>Dry hopping</a>";
  html += "<a href='/tasks/start?type=5' class='menu-button'>Dynamic hopping</a>";
  html += "</div>";
  html += "<div class='back-link'><a href='/'>&#8592; Back to menu</a></div>";
  html += "</div></body></html>";
  request->send(200, "text/html", html);
}

void handleTaskStart(AsyncWebServerRequest *request) {
  if (!request->hasParam("type")) {
    request->redirect("/tasks");
    return;
  }
  byte type = (byte)request->getParam("type")->value().toInt();
  if (!tasksAllowed()) {
    request->redirect("/tasks");
    return;
  }
  switch (type) {
    case 1: startDumpTask();          break;
    case 2: startGasTask();           break;
    case 3: startLiquidTask();        break;
    case 4: startDryHoppingTask();    break;
    case 5: startDynamicHoppingTask();break;
    default: request->redirect("/tasks"); return;
  }

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Task: "; html += taskName(type); html += "</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; }";
  html += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 30px; border-radius: 15px; box-shadow: 0 10px 30px rgba(0,0,0,0.3); text-align: center; }";
  html += "h1 { color: #333; margin-bottom: 10px; }";
  html += ".task-label { font-size: 22px; color: #555; margin-bottom: 30px; }";
  html += ".finish-btn { display: inline-block; padding: 24px 48px; background: #e53935; color: white; text-decoration: none; border-radius: 12px; font-size: 24px; font-weight: bold; margin-top: 20px; transition: background 0.2s; }";
  html += ".finish-btn:hover { background: #b71c1c; }";
  html += ".cancel-link { display: block; margin-top: 24px; font-size: 14px; color: #888; }";
  html += ".cancel-link a { color: #888; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>&#9881;&#65039; Active Task</h1>";
  html += "<div class='task-label'>"; html += taskName(type); html += "</div>";
  html += "<a href='/tasks/finish?type="; html += String(type); html += "' class='finish-btn'>&#9989; Finish Task</a>";
  html += "<div class='cancel-link'><a href='/tasks/cancel'>Cancel task</a></div>";
  html += "</div></body></html>";
  request->send(200, "text/html", html);
}

void handleTaskActive(AsyncWebServerRequest *request) {
  if (taskWindowType == 0) {
    request->redirect("/tasks");
    return;
  }
  byte type = taskWindowType;
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Task: "; html += taskName(type); html += "</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; }";
  html += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 30px; border-radius: 15px; box-shadow: 0 10px 30px rgba(0,0,0,0.3); text-align: center; }";
  html += "h1 { color: #333; margin-bottom: 10px; }";
  html += ".task-label { font-size: 22px; color: #555; margin-bottom: 30px; }";
  html += ".finish-btn { display: inline-block; padding: 24px 48px; background: #e53935; color: white; text-decoration: none; border-radius: 12px; font-size: 24px; font-weight: bold; margin-top: 20px; transition: background 0.2s; }";
  html += ".finish-btn:hover { background: #b71c1c; }";
  html += ".cancel-link { display: block; margin-top: 24px; font-size: 14px; color: #888; }";
  html += ".cancel-link a { color: #888; }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>&#9881;&#65039; Active Task</h1>";
  html += "<div class='task-label'>"; html += taskName(type); html += "</div>";
  html += "<a href='/tasks/finish?type="; html += String(type); html += "' class='finish-btn'>&#9989; Finish Task</a>";
  html += "<div class='cancel-link'><a href='/tasks/cancel'>Cancel task</a></div>";
  html += "</div></body></html>";
  request->send(200, "text/html", html);
}

void handleTaskFinish(AsyncWebServerRequest *request) {
  byte type = taskWindowType;
  if (request->hasParam("type")) {
    type = (byte)request->getParam("type")->value().toInt();
  }
  switch (type) {
    case 1: endDumpTask();          break;
    case 2: endGasTask();           break;
    case 3: endLiquidTask();        break;
    case 4: endDryHoppingTask();    break;
    case 5: endDynamicHoppingTask();break;
    default: closeTaskLog(taskEndOutcome, nullptr); endTask(0); break;
  }
  request->redirect("/");
}

void handleTaskCancel(AsyncWebServerRequest *request) {
  closeTaskLog("cancelled", nullptr);
  endTask(0);
  request->redirect("/");
}
