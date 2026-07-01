#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "GambainoCommon.h"
#include "PovotoTasks.h"
#include "PovotoData.h"
#include "PressureControl.h"

byte taskWindowType = 0;
unsigned long taskWindowEndTime = 0;
static float dumpStartPressureBar = 0.0f;
static float dumpStartHeadspaceL = 0.0f;

static void startTask(byte type) {
  taskWindowType = type;
  taskWindowEndTime = millis() + (unsigned long)TASK_TIMEOUT_MIN * 60000UL;
}

static void endTask() {
  taskWindowEndTime = millis() + (unsigned long)CalibrationData.nucleationWindow * 60000UL;
}

void startDumpTask() {
  dumpStartPressureBar = ControlData.pressure;
  dumpStartHeadspaceL = CountersData.headSpaceVolume;
  startTask(1);
}
void startGasTask()            { startTask(2); }
void startLiquidTask()         { startTask(3); }
void startDryHoppingTask()     { startTask(4); }
void startDynamicHoppingTask() { startTask(5); }

void endDumpTask() {
  applyDumpWindowHeadspaceRecalc(dumpStartHeadspaceL, dumpStartPressureBar, ControlData.pressure);
  taskWindowType = 0;
  taskWindowEndTime = 0;
}

void endGasTask() {
  endTask();
}

void endLiquidTask() {
  endTask();
}

void endDryHoppingTask() {
  endTask();
}

void endDynamicHoppingTask() {
  endTask();
}

void checkTaskExpiration() {
  if (taskWindowType != 0 && taskWindowEndTime != 0 && millis() > taskWindowEndTime) {
    if (taskWindowType == 1) {
      endDumpTask();
    }
    else {
      taskWindowType = 0;
      taskWindowEndTime = 0;
    }
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
    default: taskWindowType = 0; taskWindowEndTime = 0; break;
  }
  request->redirect("/");
}

void handleTaskCancel(AsyncWebServerRequest *request) {
  taskWindowType = 0;
  taskWindowEndTime = 0;
  request->redirect("/");
}
