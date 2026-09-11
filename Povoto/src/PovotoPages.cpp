#include <ESPAsyncWebServer.h>
#include <IOTK_NTP.h>
#include <IOTK.h>
#include <math.h>
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "PressureControl.h"
#include "TemperatureControl.h"
#include "GambainoCommon.h"
#include "IOTK_GLog.h"
#include "PovotoTasks.h"

// ========== MAIN MENU ==========

void handleMainMenu(AsyncWebServerRequest *request) {
  char dateTimeBuf[20];
  char volumeProgressBuf[48];
  NTPFormatedDateTime(dateTimeBuf);
  String uptimeStr = formatedUptime();

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Povoto</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; }";
  html += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 30px; border-radius: 15px; box-shadow: 0 10px 30px rgba(0,0,0,0.3); }";
  html += "h1 { color: #333; text-align: center; margin-bottom: 30px; font-size: 28px; }";
  html += ".status-box { background: #f7f7ff; border: 1px solid #e0e0ff; padding: 14px 18px; border-radius: 10px; margin-bottom: 20px; display: grid; grid-template-columns: repeat(2, 1fr); gap: 10px; font-size: 18px; }";
  html += ".status-item { color: #333; }";
  html += ".footer-link { margin-top: 16px; text-align: center; font-size: 12px; }";
  html += ".footer-link a { color: #666; text-decoration: none; }";
  html += ".footer-link a:hover { text-decoration: underline; }";
  html += ".status-item { color: #333; }";
  html += ".volume-progress { background: #fff8e1; border: 1px solid #ffd54f; color: #6d4c00; padding: 12px; border-radius: 10px; margin-bottom: 16px; font-size: 15px; line-height: 1.5; }";
  html += ".volume-progress strong { display: block; margin-bottom: 4px; font-size: 16px; }";
  html += ".menu-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px; margin-top: 20px; }";
  html += ".menu-button { display: block; padding: 30px 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; text-decoration: none; text-align: center; border-radius: 10px; font-size: 18px; font-weight: bold; transition: transform 0.2s, box-shadow 0.2s; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }";
  html += ".menu-button:hover { transform: translateY(-2px); box-shadow: 0 6px 12px rgba(0,0,0,0.2); }";
  html += ".menu-button:active { transform: translateY(0); }";
  html += ".icon { font-size: 32px; display: block; margin-bottom: 10px; }";
  html += "@media (max-width: 500px) { .menu-grid { grid-template-columns: 1fr; } }";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h1>&#127867; Povoto Configuration</h1>";
  if (isVolumeDeterminationActive()) {
    const uint16_t iter = getVolumeDeterminationIteration();
    const float partialVolume = getVolumeDeterminationCalculatedSoFar();
    html += "<div class='volume-progress'>";
    html += "<strong>Fermenter volume determination in progress</strong>";
    html += "Iteraction: " + String((unsigned int)iter) + "<br>";
    if (isnan(partialVolume)) {
      html += "Volume calculated so far: N/A";
    } else {
      snprintf(volumeProgressBuf, sizeof(volumeProgressBuf), "%.3f L", partialVolume);
      html += "Volume calculated so far: ";
      html += volumeProgressBuf;
    }
    html += "</div>";
  }
  html += "<div class='status-box'>";
  html += "<div class='status-item'><strong>Temperature:</strong> " + String(ControlData.temperature, 1) + " °C</div>";
  html += "<div class='status-item'><strong>Pressure:</strong> " + String(ControlData.pressure, 2) + " bar</div>";
  html += "<div class='status-item'><strong>Volume:</strong> " + String(beerVolume, 1) + " L</div>";
  html += "<div class='status-item'><strong>SG:</strong> " + String(beerSG, 3) + " (gCO2/L/d: " + String(getBeerCO2EvolutionGramsPerLiterPerDay(), 2) + ")</div>";
  html += "<div class='status-item'><strong>Uptime:</strong> " + uptimeStr + "</div>";
  html += "<div class='status-item'><strong>Date/Time:</strong> " + String(dateTimeBuf) + "</div>";
  html += "</div>";
  html += "<div class='menu-grid'>";
  html += "<a href='/tasks' class='menu-button'><span class='icon'>&#9881;&#65039;</span>Tasks</a>";
  html += "<a href='/setpoint' class='menu-button'><span class='icon'>&#127777;</span>Set Points</a>";
  html += "<a href='/batch' class='menu-button'><span class='icon'>&#128218;</span>Batch Data</a>";
  html += "<a href='/control' class='menu-button'><span class='icon'>&#128736;</span>Control</a>";
  html += "<a href='/calibration' class='menu-button'><span class='icon'>&#128200;</span>Calibration</a>";
  html += "<a href='/fmtdata' class='menu-button'><span class='icon'>&#9881;</span>Settings</a>";
  html += "<a href='/userConfig' class='menu-button'><span class='icon'>&#127899;&#65039;</span>User Configuration</a>";
  if (debugging) {
    html += "<a href='/debugparams' class='menu-button'><span class='icon'>&#128295;</span>Debug Params</a>";
  }
  html += "</div>";
  html += "<div class='footer-link'><a href='/getstatus'>full status</a></div>";
  html += "</div>";
  html += "</body></html>";
  
  request->send(200, "text/html", html);
}

// ========== DEBUG PARAMS HANDLERS ==========

void handleDebugParamsPage(AsyncWebServerRequest *request) {
  if (!debugging) {
    request->send(403, "text/plain", "Debug mode only");
    return;
  }

  const size_t BUFFER_SIZE = 3000;
  char* html = (char*)malloc(BUFFER_SIZE);
  if (!html) {
    request->send(500, "text/plain", "Out of memory");
    return;
  }

  char buffer[200];
  size_t remaining;
  strcpy(html, "<!DOCTYPE html><html><head>"
                "<meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                "<title>Debug Params</title>"
                "<style>"
                "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; color: #333; }"
                "h1 { color: #333; }"
                ".container { background-color: white; padding: 20px; border-radius: 8px; max-width: 600px; margin: 0 auto; }"
                ".form-group { margin-bottom: 15px; }"
                "label { display: block; margin-bottom: 5px; font-weight: bold; color: #333; }"
                "input[type='number'] { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }"
                "button { background-color: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin-top: 10px; }"
                "button:hover { background-color: #45a049; }"
                ".btn-secondary { background-color: #888; }"
                ".btn-secondary:hover { background-color: #666; }"
                "</style>"
                "</head><body>"
                "<div class='container'>"
                "<h1>Debug Params</h1>"
                "<form action='/debugparams/update' method='POST'>");

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='pressure'>Pressure (bar):</label>", remaining);
  sprintf(buffer, "<input type='number' id='pressure' name='pressure' value='%.3f' step='0.001'>", ControlData.pressure);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='temperature'>Temperature (°C):</label>", remaining);
  sprintf(buffer, "<input type='number' id='temperature' name='temperature' value='%.2f' step='0.01'>", ControlData.temperature);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='sgPointTime'>0.1 SG generation time:</label>", remaining);
  sprintf(buffer, "<input type='number' id='sgPointTime' name='sgPointTime' value='%.2f' step='0.01'>", sgPointGenerationTime);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<button type='submit'>Save</button> "
               "<button type='button' class='btn-secondary' onclick='window.location=\"/\"'>Cancel</button>"
               "</form>"
               "</div>"
               "</body></html>", remaining);

  request->send(200, "text/html", html);
  free(html);
}

void handleDebugParamsUpdate(AsyncWebServerRequest *request) {
  if (!debugging) {
    request->send(403, "text/plain", "Debug mode only");
    return;
  }

  if (request->hasParam("pressure", true)) {
    ControlData.pressure = request->getParam("pressure", true)->value().toFloat();
  }
  if (request->hasParam("temperature", true)) {
    ControlData.temperature = request->getParam("temperature", true)->value().toFloat();
    debugTemperatureOverride = true;
  }
  if (request->hasParam("sgPointTime", true)) {
    sgPointGenerationTime = request->getParam("sgPointTime", true)->value().toFloat();
  }

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='2;url=/'>";
  html += "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>Debug Params Saved!</h1>";
  html += "<p>Redirecting back to menu...</p>";
  html += "</body></html>";

  request->send(200, "text/html", html);
}

// ========== FMT DATA HANDLERS ==========

void handleFMTDataPage(AsyncWebServerRequest *request) {
  const size_t BUFFER_SIZE = 6000;
  char* html = (char*)malloc(BUFFER_SIZE);
  if (!html) {
    request->send(500, "text/plain", "Out of memory");
    return;
  }
  
  char buffer[200];
  size_t remaining;
  
  strcpy(html, "<!DOCTYPE html><html><head>"
                "<meta charset='UTF-8'>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                "<title>SettingsConfiguration</title>"
                "<style>"
                "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; color: #333; }"
                "h1 { color: #333; }"
                ".container { background-color: white; padding: 20px; border-radius: 8px; max-width: 600px; margin: 0 auto; }"
                ".form-group { margin-bottom: 15px; }"
                "label { display: block; margin-bottom: 5px; font-weight: bold; color: #333; }"
                ".heater-time label { text-align: center; }"
                "input[type='number'], input[type='text'] { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }"
                "button { background-color: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin-top: 10px; }"
                "button:hover { background-color: #45a049; }"
                ".btn-secondary { background-color: #888; }"
                ".btn-secondary:hover { background-color: #666; }"
                "</style>"
                "</head><body>"
                "<div class='container'>"
                "<h1>Settings</h1>"
                "<form action='/fmtdata/update' method='POST'>");
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='PovotoNum'>Povoto Number:</label>", remaining);
  sprintf(buffer, "<input type='number' id='PovotoNum' name='PovotoNum' value='%d' min='0' max='255'>", FMTData.PovotoNum);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='FMTVolume'>FMT Total Volume (L):</label>", remaining);
  sprintf(buffer, "<input type='number' id='FMTVolume' name='FMTVolume' value='%.2f' step='0.01'>", FMTData.FMTVolume);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='FMTReliefVolume'>Relief Volume (L):</label>", remaining);
  sprintf(buffer, "<input type='number' id='FMTReliefVolume' name='FMTReliefVolume' value='%.2f' step='0.01'>", FMTData.FMTReliefVolume);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<h2>Cooling cycle</h2><table style='width:100%;table-layout:fixed'>"
    "<thead><tr><th>Temperature (&deg;C)</th><th>On time (min)</th><th>Rest time (min)</th></tr></thead><tbody>", remaining);
  const char* temperatures[] = {"&gt;20", "10", "&lt;0.5"};
  for (int i = 0; i < 3; ++i) {
    snprintf(buffer, sizeof(buffer), "<tr><th scope='row'>%s</th><td>", temperatures[i]);
    strncat(html, buffer, BUFFER_SIZE - strlen(html) - 1);
    snprintf(buffer, sizeof(buffer),
      "<input type='number' aria-label='On time at %s C' name='coolingOn%d' value='%.2f' min='0.01' max='1440' step='0.01' required></td><td>",
      temperatures[i], i, FMTData.coolingCycle[i].onMinutes);
    strncat(html, buffer, BUFFER_SIZE - strlen(html) - 1);
    snprintf(buffer, sizeof(buffer),
      "<input type='number' aria-label='Rest time at %s C' name='coolingOff%d' value='%.2f' min='0.01' max='1440' step='0.01' required></td></tr>",
      temperatures[i], i, FMTData.coolingCycle[i].offMinutes);
    strncat(html, buffer, BUFFER_SIZE - strlen(html) - 1);
  }
  strncat(html, "</tbody></table>", BUFFER_SIZE - strlen(html) - 1);
  strncat(html, "<h2>Heating</h2><input type='hidden' name='heaterConfig' value='1'>"
    "<table style='width:100%;table-layout:fixed'><tbody><tr><td style='text-align:center'>"
    "<label><input type='checkbox' name='enableHeater' value='1'",
    BUFFER_SIZE - strlen(html) - 1);
  if (FMTData.heater.enabled) strncat(html, " checked", BUFFER_SIZE - strlen(html) - 1);
  strncat(html, "> Enable heater</label></td><td class='heater-time'><label for='FMTHeaterOnTime'>On time (min)</label>",
    BUFFER_SIZE - strlen(html) - 1);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='FMTHeaterOnTime' name='FMTHeaterOnTime' value='%.2f' min='0.01' max='1440' step='0.01' required>",
    FMTData.heater.onMinutes);
  strncat(html, buffer, BUFFER_SIZE - strlen(html) - 1);
  strncat(html, "</td><td class='heater-time'><label for='FMTHeaterOffTime'>Rest time (min)</label>", BUFFER_SIZE - strlen(html) - 1);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='FMTHeaterOffTime' name='FMTHeaterOffTime' value='%.2f' min='0.01' max='1440' step='0.01' required>",
    FMTData.heater.offMinutes);
  strncat(html, buffer, BUFFER_SIZE - strlen(html) - 1);
  strncat(html, "</td></tr></tbody></table><script>document.querySelector(\"input[name='enableHeater']\").onchange=function(){document.querySelectorAll('.heater-time').forEach(function(e){e.hidden=!this.checked;e.querySelector('input').disabled=!this.checked;},this);};document.querySelector(\"input[name='enableHeater']\").dispatchEvent(new Event('change'));</script>", BUFFER_SIZE - strlen(html) - 1);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='FMTAltitude'>Altitude (m):</label>", remaining);
  sprintf(buffer, "<input type='number' id='FMTAltitude' name='FMTAltitude' value='%.1f' step='0.1' min='0'>", FMTData.FMTAltitude);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='FMTEffectiveVentingExponent' title='Effective polytropic exponent used to correct the temporary pressure drop caused by gas cooling during venting. Use 1.00 for isothermal venting and approximately 1.29 for adiabatic CO2 venting.'>Effective Venting Exponent:</label>", remaining);
  sprintf(buffer, "<input type='number' id='FMTEffectiveVentingExponent' name='FMTEffectiveVentingExponent' value='%.3f' step='0.001'>", FMTData.FMTEffectiveVentingExponent);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
    strncat(html, "<button type='submit'>Save</button> "
                 "<button type='button' class='btn-secondary' onclick='window.location=\"/\"'>Cancel</button>"
                 "</form>"
                 "</div>"
                 "</body></html>", remaining);
  
  request->send(200, "text/html", html);
  free(html);
}

static bool readCoolingMinutes(AsyncWebServerRequest *request, const char *name, float &value) {
  if (!request->hasParam(name, true)) return true;
  const String text = request->getParam(name, true)->value();
  char *end = nullptr;
  const float parsed = strtof(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0' || !isfinite(parsed) ||
      parsed < 0.01f || parsed > 1440.0f) return false;
  value = parsed;
  return true;
}

void handleFMTDataUpdate(AsyncWebServerRequest *request) {
  CoolingCyclePoint_t cooling[3];
  for (int i = 0; i < 3; ++i) {
    char name[20];
    float onMinutes = FMTData.coolingCycle[i].onMinutes;
    float offMinutes = FMTData.coolingCycle[i].offMinutes;
    snprintf(name, sizeof(name), "coolingOn%d", i);
    bool valid = readCoolingMinutes(request, name, onMinutes);
    snprintf(name, sizeof(name), "coolingOff%d", i);
    valid = readCoolingMinutes(request, name, offMinutes) && valid;
    if (!valid) {
      request->send(400, "text/plain", "Cooling times must be between 0.01 and 1440 minutes.");
      return;
    }
    cooling[i] = {onMinutes, offMinutes};
  }
  if (!(cooling[0].onMinutes > cooling[1].onMinutes + 1.0f && cooling[1].onMinutes > cooling[2].onMinutes + 1.0f) ||
      !(cooling[0].offMinutes > cooling[1].offMinutes + 1.0f && cooling[1].offMinutes > cooling[2].offMinutes + 1.0f)) {
    request->send(400, "text/plain", "Cooling times must decrease by more than 1 minute at each temperature step.");
    return;
  }
  float heaterOn = FMTData.heater.onMinutes;
  float heaterOff = FMTData.heater.offMinutes;
  if (!readCoolingMinutes(request, "FMTHeaterOnTime", heaterOn) ||
      !readCoolingMinutes(request, "FMTHeaterOffTime", heaterOff)) {
    request->send(400, "text/plain", "Heater times must be between 0.01 and 1440 minutes.");
    return;
  }
  if (request->hasParam("heaterConfig", true))
    FMTData.heater.enabled = request->hasParam("enableHeater", true) &&
      request->getParam("enableHeater", true)->value() == "1";
  FMTData.heater.onMinutes = heaterOn;
  FMTData.heater.offMinutes = heaterOff;
  memcpy(FMTData.coolingCycle, cooling, sizeof(cooling));
  if (request->hasParam("PovotoNum", true)) {
    FMTData.PovotoNum = request->getParam("PovotoNum", true)->value().toInt();
  }
  if (request->hasParam("FMTVolume", true)) {
    FMTData.FMTVolume = request->getParam("FMTVolume", true)->value().toFloat();
  }
  if (request->hasParam("FMTReliefVolume", true)) {
    const float relief = request->getParam("FMTReliefVolume", true)->value().toFloat();
    const float volume = request->hasParam("FMTVolume", true) ? request->getParam("FMTVolume", true)->value().toFloat() : FMTData.FMTVolume;
    if (!(relief > 0.0f && relief < 0.1f * volume)) {
      request->send(400, "text/plain", "Relief volume must be greater than zero and less than 10% of FMT total volume.");
      return;
    }
    FMTData.FMTReliefVolume = relief;
  }
  if (request->hasParam("FMTOnTimeDuringBrew", true)) {
    FMTData.FMTOnTimeDuringBrew = request->getParam("FMTOnTimeDuringBrew", true)->value().toFloat();
  }
  if (request->hasParam("FMTOFFTimeDuringBrew", true)) {
    FMTData.FMTOFFTimeDuringBrew = request->getParam("FMTOFFTimeDuringBrew", true)->value().toFloat();
  }
  if (request->hasParam("FMTAltitude", true)) {
    FMTData.FMTAltitude = request->getParam("FMTAltitude", true)->value().toFloat();
  }
  if (request->hasParam("FMTEffectiveVentingExponent", true)) {
    const float exponent = request->getParam("FMTEffectiveVentingExponent", true)->value().toFloat();
    if (exponent < 1.0f || exponent > 1.30f) {
      request->send(400, "text/plain", "Effective venting exponent must be between 1.00 and 1.30.");
      return;
    }
    FMTData.FMTEffectiveVentingExponent = exponent;
  }
  writeFMTDataToNIV();
  updatePatmFromFMTAltitude();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='2;url=/'>";
  html += "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>FMT Data Saved!</h1>";
  html += "<p>Redirecting back to menu...</p>";
  html += "</body></html>";
  
  request->send(200, "text/html", html);
}

// ========== CALIBRATION DATA HANDLERS ==========

static float measureCurrentAverage(uint32_t durationMs, uint32_t sampleIntervalMs) {
  if (sampleIntervalMs == 0) {
    sampleIntervalMs = 100;
  }

  const unsigned long start = millis();
  double sum = 0.0;
  uint32_t count = 0;

  while ((millis() - start) < durationMs) {
    sum += currentReading;
    count++;
    delay(sampleIntervalMs);
  }

  if (count == 0) {
    return currentReading;
  }

  return (float)(sum / (double)count);
}

void handleCalibrationCurrentRefresh(AsyncWebServerRequest *request) {
  const float avgCurrent = measureCurrentAverage(30000UL, 100UL);
  String target = "/calibration?avgCurrent=" + String(avgCurrent, 2);
  request->redirect(target);
}

void handleCalibrationDataPage(AsyncWebServerRequest *request) {
  float pageCurrentReading = currentReading;
  bool usingAverageCurrent = false;
  if (request->hasParam("avgCurrent")) {
    pageCurrentReading = request->getParam("avgCurrent")->value().toFloat();
    usingAverageCurrent = true;
  }

  const size_t BUFFER_SIZE = 7000;
  char* html = (char*)malloc(BUFFER_SIZE);
  if (!html) {
    request->send(500, "text/plain", "Out of memory");
    return;
  }

  size_t remaining;
  strcpy(html, "<!DOCTYPE html><html><head>");
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<meta charset='UTF-8'>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<meta name='viewport' content='width=device-width, initial-scale=1'>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<title>Calibration Data</title>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<style>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "h1 { color: #333; text-align: center; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".form-group { margin-bottom: 15px; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "label { display: block; margin-bottom: 5px; color: #666; font-weight: bold; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".input-row { display: flex; gap: 8px; align-items: center; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".point-row { display: grid; grid-template-columns: 80px 1fr 1.35fr; gap: 12px; align-items: end; } .point-row > .form-group { margin-bottom: 15px; } .point-caption { font-weight: bold; padding-bottom: 24px; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".input-row input { flex: 1; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "input[type='number']:not(.input-row input) { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "button { padding: 8px 14px; margin: 2px; border: none; border-radius: 4px; cursor: pointer; font-size: 14px; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "button[type='submit'] { background: #4CAF50; color: white; font-size: 16px; padding: 10px 20px; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".btn-secondary { background: #999; color: white; font-size: 16px; padding: 10px 20px; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".btn-use { background: #2196F3; color: white; white-space: nowrap; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".btn-refresh { background: #ff9800; color: white; white-space: nowrap; text-decoration: none; display: inline-block; padding: 8px 14px; border-radius: 4px; font-size: 14px; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".current-badge { background: #e8f5e9; border: 1px solid #a5d6a7; border-radius: 6px; padding: 8px 14px; margin-bottom: 18px; font-size: 1.1em; color: #2e7d32; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".current-row { display: flex; gap: 10px; align-items: center; margin-bottom: 18px; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".current-row .current-badge { flex: 1; margin-bottom: 0; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</style></head><body>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='container'>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<h1>Calibration Data</h1>", remaining);

  // Corrente medida atual / media 30s
  char buffer[300];
  snprintf(buffer, sizeof(buffer),
    "<div class='current-row'><div class='current-badge'>&#128268; Actual current reading%s: <b>%.2f mA</b></div><a class='btn-refresh' href='/calibration/refresh-current'>Update</a></div>",
    usingAverageCurrent ? " (30s avg)" : "",
    pageCurrentReading);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<form action='/calibration/update' method='POST'>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<h2>Pressure sensor</h2>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='point-row'><div class='point-caption'>Point A</div><div class='form-group'><label>Pressure (bar):</label><input type='number' value='0.00' disabled></div>", remaining);

  // Pressure 0 Current
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='pressure0Current'>Current (mA):</label>"
               "<div class='input-row'>", remaining);
  snprintf(buffer, sizeof(buffer),
    "<input type='number' id='pressure0Current' name='pressure0Current' value='%.2f' step='0.01'>"
    "<button type='button' class='btn-use' onclick=\"document.getElementById('pressure0Current').value='%.2f'\">Use actual</button>",
    FMTData.pressure0Current, pageCurrentReading);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div></div></div>", remaining);

  // Pressure 1 (point B)
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='point-row'><div class='point-caption'>Point B</div><div class='form-group'>"
               "<label for='pressure1Bar'>Pressure (bar):</label>", remaining);
  snprintf(buffer, sizeof(buffer),
    "<input type='number' id='pressure1Bar' name='pressure1Bar' value='%.2f' step='0.01'>",
    FMTData.pressure1Bar);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  // Pressure 1 Current
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='pressure1Current'>Current (mA):</label>"
               "<div class='input-row'>", remaining);
  snprintf(buffer, sizeof(buffer),
    "<input type='number' id='pressure1Current' name='pressure1Current' value='%.2f' step='0.01'>"
    "<button type='button' class='btn-use' onclick=\"document.getElementById('pressure1Current').value='%.2f'\">Use actual</button>",
    FMTData.pressure1Current, pageCurrentReading);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div></div></div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<label><input type='checkbox' name='threePoint' value='1' onclick=\"document.getElementById('thirdPointFields').hidden=!this.checked; if(!this.checked){document.getElementById('pressure2Bar').value='0';document.getElementById('pressure2Current').value='0';}\"", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, FMTData.pressure2Bar != 0.0f ? " checked" : "", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "> Three point quadratic curve</label><div id='thirdPointFields'", remaining);
  if (FMTData.pressure2Bar == 0.0f) {
    remaining = BUFFER_SIZE - strlen(html) - 1;
    strncat(html, " hidden", remaining);
  }
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ">", remaining);

  // Pressure 2 Bar
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='point-row'><div class='point-caption'>Point C</div><div class='form-group'>"
               "<label for='pressure2Bar'>Pressure (bar):</label>", remaining);
  snprintf(buffer, sizeof(buffer),
    "<input type='number' id='pressure2Bar' name='pressure2Bar' value='%.2f' step='0.01'>",
    FMTData.pressure2Bar);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  // Pressure 2 Current
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='pressure2Current'>Current (mA):</label>"
               "<div class='input-row'>", remaining);
  snprintf(buffer, sizeof(buffer),
    "<input type='number' id='pressure2Current' name='pressure2Current' value='%.2f' step='0.01'>"
    "<button type='button' class='btn-use' onclick=\"document.getElementById('pressure2Current').value='%.2f'\">Use actual</button>",
    FMTData.pressure2Current, pageCurrentReading);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div></div></div>", remaining);

  // End of fields controlled by the three-point checkbox.
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<h2>Dissolved CO2 dynamics</h2>", remaining);

  // Maximum security pressure
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='maximumPressure'>Maximum security pressure (bar):</label>", remaining);
  snprintf(buffer, sizeof(buffer),
    "<input type='number' id='maximumPressure' name='maximumPressure' value='%.2f' step='0.01' min='0'>",
    FMTData.maximumPressure);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  // CO2 transfer time
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='co2TransferTime'>CO2 equilibrim half-time (hours):</label>", remaining);
  snprintf(buffer, sizeof(buffer),
    "<input type='number' id='co2TransferTime' name='co2TransferTime' value='%d' step='1' min='0'>",
    FMTData.co2TransferTime);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  // Nucleation window
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='nucleationWindow'>Nucleation window (min):</label>", remaining);
  snprintf(buffer, sizeof(buffer),
    "<input type='number' id='nucleationWindow' name='nucleationWindow' value='%d' step='1' min='0'>",
    FMTData.nucleationWindow);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<button type='submit'>Save</button> "
               "<button type='button' class='btn-secondary' onclick='window.location=\"/\"'>Cancel</button>"
               "</form>"
               "</div>"
               "</body></html>", remaining);

  request->send(200, "text/html", html);
  free(html);
}

void handleCalibrationDataUpdate(AsyncWebServerRequest *request) {
  const float p1 = request->hasParam("pressure1Bar", true) ? request->getParam("pressure1Bar", true)->value().toFloat() : FMTData.pressure1Bar;
  const float p2 = request->hasParam("pressure2Bar", true) ? request->getParam("pressure2Bar", true)->value().toFloat() : FMTData.pressure2Bar;
  const float c0 = request->hasParam("pressure0Current", true) ? request->getParam("pressure0Current", true)->value().toFloat() : FMTData.pressure0Current;
  const float c1 = request->hasParam("pressure1Current", true) ? request->getParam("pressure1Current", true)->value().toFloat() : FMTData.pressure1Current;
  const float c2 = request->hasParam("pressure2Current", true) ? request->getParam("pressure2Current", true)->value().toFloat() : FMTData.pressure2Current;
  const float maxP = request->hasParam("maximumPressure", true) ? request->getParam("maximumPressure", true)->value().toFloat() : FMTData.maximumPressure;
  const bool threePoint = request->hasParam("threePoint", true);
  if (!(p1 > 0.5f && p1 < maxP) || (threePoint && p2 != 0.0f && !(p2 > p1 + 0.5f && p2 < maxP)) || !(c1 > c0) || (threePoint && p2 != 0.0f && !(c2 > c1)) || !(maxP > 2.0f && maxP < 3.0f)) {
    request->send(400, "text/plain", "Invalid calibration values. Check pressure, current and maximum pressure limits.");
    return;
  }
  if (request->hasParam("pressure0Current", true)) {
    FMTData.pressure0Current = request->getParam("pressure0Current", true)->value().toFloat();
  }
  if (request->hasParam("pressure1Bar", true)) {
    FMTData.pressure1Bar = request->getParam("pressure1Bar", true)->value().toFloat();
  }
  if (request->hasParam("pressure1Current", true)) {
    FMTData.pressure1Current = request->getParam("pressure1Current", true)->value().toFloat();
  }
  if (request->hasParam("pressure2Bar", true)) {
    FMTData.pressure2Bar = request->getParam("pressure2Bar", true)->value().toFloat();
  }
  if (request->hasParam("pressure2Current", true)) {
    FMTData.pressure2Current = request->getParam("pressure2Current", true)->value().toFloat();
  }
  if (!request->hasParam("threePoint", true)) {
    FMTData.pressure2Bar = 0.0f;
    FMTData.pressure2Current = 0.0f;
  }
  if (request->hasParam("maximumPressure", true)) {
    FMTData.maximumPressure = request->getParam("maximumPressure", true)->value().toFloat();
  }
  if (request->hasParam("co2TransferTime", true)) {
    FMTData.co2TransferTime = request->getParam("co2TransferTime", true)->value().toInt();
  }
  if (request->hasParam("nucleationWindow", true)) {
    FMTData.nucleationWindow = request->getParam("nucleationWindow", true)->value().toInt();
  }
  
  writeFMTDataToNIV();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='2;url=/'>";
  html += "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>Calibration Data Saved!</h1>";
  html += "<p>Redirecting back to menu...</p>";
  html += "</body></html>";
  
  request->send(200, "text/html", html);
}

// ========== BATCH DATA HANDLERS ==========

void handleBatchDataPage(AsyncWebServerRequest *request) {
  const size_t BUFFER_SIZE = 5000;
  char* html = (char*)malloc(BUFFER_SIZE);
  if (!html) {
    request->send(500, "text/plain", "Out of memory");
    return;
  }
  
  size_t remaining;
  strcpy(html, "<!DOCTYPE html><html><head>");
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<meta charset='UTF-8'>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<meta name='viewport' content='width=device-width, initial-scale=1'>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<title>Batch Data</title>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<style>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "h1 { color: #333; text-align: center; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".form-group { margin-bottom: 15px; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "label { display: block; margin-bottom: 5px; color: #666; font-weight: bold; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "input { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "button { padding: 10px 20px; margin: 5px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "button[type='submit'] { background: #4CAF50; color: white; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".btn-secondary { background: #999; color: white; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</style></head><body>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='container'>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<h1>Batch Data</h1>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<form action='/batch/update' method='POST'>", remaining);

  char buffer[200];
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='batchName'>Batch Name:</label>", remaining);
  sprintf(buffer, "<input type='text' id='batchName' name='batchName' value='%s' maxlength='31'>", BatchData.batchName);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='batchNumber'>Batch Number:</label>", remaining);
  sprintf(buffer, "<input type='number' id='batchNumber' name='batchNumber' value='%d' min='0' max='9999'>", BatchData.batchNumber);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='batchDate'>Batch Date (DD/MM/YYYY):</label>", remaining);
  sprintf(buffer, "<input type='text' id='batchDate' name='batchDate' value='%s' maxlength='10' placeholder='DD/MM/YYYY'>", BatchData.batchDate);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='batchOG'>Original Gravity (OG):</label>", remaining);
  sprintf(buffer, "<input type='number' id='batchOG' name='batchOG' value='%.3f' step='0.001' min='0' placeholder='1.050'>", BatchData.batchOG);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='addedPlato'>Added &deg;P:</label>", remaining);
  sprintf(buffer, "<input type='number' id='addedPlato' name='addedPlato' value='%.2f' step='0.01' min='0' placeholder='0.00'>", BatchData.addedPlato);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='startPressure'>Start Pressure (bar):</label>", remaining);
  sprintf(buffer, "<input type='number' id='startPressure' name='startPressure' value='%.1f' step='0.1' min='0' placeholder='0.0'>", BatchData.startPressure);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='startTemperature'>Start Temperature (&deg;C):</label>", remaining);
  sprintf(buffer, "<input type='number' id='startTemperature' name='startTemperature' value='%.1f' step='0.1' placeholder='0.0'>", BatchData.startTemperature);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='correctionPlato'>Correction &deg;P:</label>", remaining);
  sprintf(buffer, "<input type='number' id='correctionPlato' name='correctionPlato' value='%.2f' step='0.01'>", CountersData.correctionPlato);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
    strncat(html, "<button type='submit'>Save</button> "
                 "<button type='button' class='btn-secondary' onclick='window.location=\"/\"'>Cancel</button>"
                 "</form>"
                 "</div>"
                 "</body></html>", remaining);
  
  request->send(200, "text/html", html);
  free(html);
}

void handleBatchDataUpdate(AsyncWebServerRequest *request) {
  if (request->hasParam("batchName", true)) {
    String value = request->getParam("batchName", true)->value();
    strncpy(BatchData.batchName, value.c_str(), 31);
    BatchData.batchName[31] = 0;
  }
  if (request->hasParam("batchNumber", true)) {
    BatchData.batchNumber = (uint16_t)request->getParam("batchNumber", true)->value().toInt();
  }
  if (request->hasParam("batchDate", true)) {
    String value = request->getParam("batchDate", true)->value();
    strncpy(BatchData.batchDate, value.c_str(), 10);
    BatchData.batchDate[10] = 0;
  }
  if (request->hasParam("batchOG", true)) {
    BatchData.batchOG = request->getParam("batchOG", true)->value().toFloat();
  }
  if (request->hasParam("addedPlato", true)) {
    BatchData.addedPlato = request->getParam("addedPlato", true)->value().toFloat();
  }
  if (request->hasParam("startPressure", true)) {
    BatchData.startPressure = request->getParam("startPressure", true)->value().toFloat();
  }
  if (request->hasParam("startTemperature", true)) {
    BatchData.startTemperature = request->getParam("startTemperature", true)->value().toFloat();
  }
  if (request->hasParam("correctionPlato", true)) {
    CountersData.correctionPlato = request->getParam("correctionPlato", true)->value().toFloat();
  }
  
  writeBatchDataToNIV();
  writeCountersDataToNIV();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='2;url=/'>";
  html += "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>Batch Data Saved!</h1>";
  html += "<p>Redirecting back to menu...</p>";
  html += "</body></html>";
  
  request->send(200, "text/html", html);
}

// ========== COUNTERS DATA HANDLERS ==========

void handleCountersDataPage(AsyncWebServerRequest *request) {
  const size_t BUFFER_SIZE = 5500;
  char* html = (char*)malloc(BUFFER_SIZE);
  if (!html) {
    request->send(500, "text/plain", "Out of memory");
    return;
  }

  size_t remaining;
  char buffer[220];

  strcpy(html, "<!DOCTYPE html><html><head>");
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<meta charset='UTF-8'>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<meta name='viewport' content='width=device-width, initial-scale=1'>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<title>Counters Data</title>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<style>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".container { max-width: 680px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "h1 { color: #333; text-align: center; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".summary { background: #f7f7ff; border: 1px solid #dde2ff; border-radius: 8px; padding: 12px; margin-bottom: 18px; color: #444; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".readonly-group { margin-bottom: 18px; padding: 14px; border: 1px solid #e3e3e3; border-radius: 8px; background: #fafafa; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".readonly-group h2 { margin: 0 0 12px 0; font-size: 18px; color: #333; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".form-group { margin-bottom: 15px; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "label { display: block; margin-bottom: 5px; color: #666; font-weight: bold; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "input { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "input[readonly] { background: #f3f3f3; color: #555; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "small { color: #777; display: block; margin-top: 4px; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "button { padding: 10px 20px; margin: 5px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "button[type='submit'] { background: #4CAF50; color: white; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".btn-secondary { background: #999; color: white; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</style></head><body>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='container'>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<h1>Counters Data</h1>", remaining);

  snprintf(buffer, sizeof(buffer),
           "<div class='summary'>Derived beer volume: %.2f L<br>Current dissolved CO2 estimate: %.3f mol</div>",
           beerVolume,
           CountersData.CO2InSolution);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='readonly-group'><h2>Derived Fields</h2>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'><label for='derivedBeerVolume'>Beer Volume (L):</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='derivedBeerVolume' value='%.2f' step='0.01' readonly>", beerVolume);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'><label for='derivedHeadSpaceCO2'>Headspace CO2 (mol):</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='derivedHeadSpaceCO2' value='%.3f' step='0.001' readonly>", headSpaceCO2Mols);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'><label for='derivedBeerSG'>Beer SG:</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='derivedBeerSG' value='%.4f' step='0.0001' readonly>", beerSG);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'><label for='derivedBeerABV'>Beer ABV (%):</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='derivedBeerABV' value='%.2f' step='0.01' readonly>", beerABV);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div></div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<form action='/counters/update' method='POST'>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'><label for='totalReliefCount'>Total Relief Count:</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='totalReliefCount' name='totalReliefCount' value='%lu' min='0' step='1'>", (unsigned long)CountersData.totalReliefCount);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'><label for='totalMolsEjected'>Total Mols Ejected:</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='totalMolsEjected' name='totalMolsEjected' value='%.3f' step='0.001'>", CountersData.totalMolsEjected);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'><label for='CO2InSolution'>CO2 In Solution (mol):</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='CO2InSolution' name='CO2InSolution' value='%.3f' step='0.001'>", CountersData.CO2InSolution);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'><label for='headSpaceVolume'>Headspace Volume (L):</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='headSpaceVolume' name='headSpaceVolume' value='%.2f' step='0.01' min='0'>", CountersData.headSpaceVolume);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<small>Changing this recomputes the derived beer volume in RAM after save.</small></div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'><label for='correctionPlato'>Correction Plato:</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='correctionPlato' name='correctionPlato' value='%.2f' step='0.01'>", CountersData.correctionPlato);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'><label for='SGAttenuation'>SG Attenuation:</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='SGAttenuation' name='SGAttenuation' value='%.4f' step='0.0001'>", CountersData.SGAttenuation);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'><label for='totalChillTime'>Total Chill Time (s):</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='totalChillTime' name='totalChillTime' value='%ld' step='1'>", CountersData.totalChillTime);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'><label for='totalHeatTime'>Total Heat Time (s):</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='totalHeatTime' name='totalHeatTime' value='%ld' step='1'>", CountersData.totalHeatTime);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<button type='submit'>Save</button> <button type='button' class='btn-secondary' onclick='window.location=\"/\"'>Cancel</button></form></div></body></html>", remaining);

  request->send(200, "text/html", html);
  free(html);
}

void handleCountersDataUpdate(AsyncWebServerRequest *request) {
  bool co2StateChanged = false;
  if (request->hasParam("totalReliefCount", true)) {
    const uint32_t value = (uint32_t)request->getParam("totalReliefCount", true)->value().toInt();
    co2StateChanged |= value != CountersData.totalReliefCount;
    CountersData.totalReliefCount = value;
  }
  if (request->hasParam("totalMolsEjected", true)) {
    const float value = request->getParam("totalMolsEjected", true)->value().toFloat();
    co2StateChanged |= value != CountersData.totalMolsEjected;
    CountersData.totalMolsEjected = value;
  }
  if (request->hasParam("CO2InSolution", true)) {
    const float value = request->getParam("CO2InSolution", true)->value().toFloat();
    co2StateChanged |= value != CountersData.CO2InSolution;
    CountersData.CO2InSolution = value;
  }
  if (request->hasParam("headSpaceVolume", true)) {
    const float value = request->getParam("headSpaceVolume", true)->value().toFloat();
    co2StateChanged |= value != CountersData.headSpaceVolume;
    CountersData.headSpaceVolume = value;
  }
  if (request->hasParam("correctionPlato", true)) {
    CountersData.correctionPlato = request->getParam("correctionPlato", true)->value().toFloat();
  }
  if (request->hasParam("SGAttenuation", true)) {
    CountersData.SGAttenuation = request->getParam("SGAttenuation", true)->value().toFloat();
  }
  if (request->hasParam("totalChillTime", true)) {
    CountersData.totalChillTime = request->getParam("totalChillTime", true)->value().toInt();
  }
  if (request->hasParam("totalHeatTime", true)) {
    CountersData.totalHeatTime = request->getParam("totalHeatTime", true)->value().toInt();
  }

  writeCountersDataToNIV();
  if (co2StateChanged) {
    requestDerivedStateRestoreFromCounters();
  }

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='2;url=/counters'>";
  html += "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>Counters Data Saved!</h1>";
  html += "<p>Redirecting back to counters page...</p>";
  html += "</body></html>";

  request->send(200, "text/html", html);
}

// ========== SETPOINT DATA HANDLERS ==========

void handleSetPointDataPage(AsyncWebServerRequest *request) {
  const size_t BUFFER_SIZE = 5000;
  char* html = (char*)malloc(BUFFER_SIZE);
  if (!html) {
    request->send(500, "text/plain", "Out of memory");
    return;
  }
  
  size_t remaining;
  strcpy(html, "<!DOCTYPE html><html><head>");
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<meta charset='UTF-8'>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<meta name='viewport' content='width=device-width, initial-scale=1'>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<title>SetPoint Data</title>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<style>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "h1 { color: #333; text-align: center; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".form-group { margin-bottom: 15px; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "label { display: block; margin-bottom: 5px; color: #666; font-weight: bold; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "input, select { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "button { padding: 10px 20px; margin: 5px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "button[type='submit'] { background: #4CAF50; color: white; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".btn-secondary { background: #999; color: white; }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</style></head><body>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='container'>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<h1>SetPoint Data</h1>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<form action='/setpoint/update' method='POST'>", remaining);

  char buffer[512];
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='mode'>Mode:</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<select id='mode' name='mode'>"
                                   "<option value='" TOSTR(MODE_OFF)                 "'%s>Off</option>"
                                   "<option value='" TOSTR(MODE_BREWING_TRANSFERING) "'%s>Brewing/Transfering</option>"
                                   "<option value='" TOSTR(MODE_FERMENTING)          "'%s>Fermenting</option>"
                                   "<option value='" TOSTR(MODE_CONDITIONING)        "'%s>Conditioning</option>"
                                   "</select>",
          SetPointData.mode == MODE_OFF                 ? " selected" : "",
          SetPointData.mode == MODE_BREWING_TRANSFERING ? " selected" : "",
          SetPointData.mode == MODE_FERMENTING          ? " selected" : "",
          SetPointData.mode == MODE_CONDITIONING        ? " selected" : "");
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='setPointTemp'>Set Point Temperature (°C):</label>", remaining);
  {
    char tmp[16];
    if (SetPointData.setPointTemp == NOTaTEMP) snprintf(tmp, sizeof(tmp), "N/A");
    else snprintf(tmp, sizeof(tmp), "%.1f", SetPointData.setPointTemp);
    snprintf(buffer, sizeof(buffer), "<input type='text' id='setPointTemp' name='setPointTemp' value='%s'>", tmp);
  }
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='setPointSlowTemp'>Set Point Slow Temperature (°C):</label>", remaining);
  {
    char tmp[16];
    if (SetPointData.setPointSlowTemp == NOTaTEMP) snprintf(tmp, sizeof(tmp), "N/A");
    else snprintf(tmp, sizeof(tmp), "%.1f", SetPointData.setPointSlowTemp);
    snprintf(buffer, sizeof(buffer), "<input type='text' id='setPointSlowTemp' name='setPointSlowTemp' value='%s'>", tmp);
  }
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='setPointPressure'>Set Point Pressure (bar):</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='setPointPressure' name='setPointPressure' value='%.2f' step='0.01'>", SetPointData.setPointPressure);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
    strncat(html, "<button type='submit'>Save</button> "
                 "<button type='button' class='btn-secondary' onclick='window.location=\"/\"'>Cancel</button>"
                 "</form>"
                 "</div>"
                 "</body></html>", remaining);
  
  request->send(200, "text/html", html);
  free(html);
}

void handleSetPointDataUpdate(AsyncWebServerRequest *request) {
  float oldTemp = SetPointData.setPointTemp;
  float oldPressure = SetPointData.setPointPressure;
  byte oldMode = SetPointData.mode;

  if (request->hasParam("mode", true)) {
    SetPointData.mode = request->getParam("mode", true)->value().toInt();
  }
  if (request->hasParam("setPointTemp", true)) {
    String v = request->getParam("setPointTemp", true)->value();
    v.trim();
    if (v.length() == 0 || v.equalsIgnoreCase("N/A"))
      SetPointData.setPointTemp = NOTaTEMP;
    else
      SetPointData.setPointTemp = v.toFloat();
  }
  if (request->hasParam("setPointSlowTemp", true)) {
    String v = request->getParam("setPointSlowTemp", true)->value();
    v.trim();
    if (v.length() == 0 || v.equalsIgnoreCase("N/A"))
      SetPointData.setPointSlowTemp = NOTaTEMP;
    else
      SetPointData.setPointSlowTemp = v.toFloat();
  }
  if (request->hasParam("setPointPressure", true)) {
    SetPointData.setPointPressure = request->getParam("setPointPressure", true)->value().toFloat();
  }

  if (fabsf(SetPointData.setPointTemp - oldTemp) > 0.0001f) {
    SetPointData.setPointTempSetEpoch = NTPEpoch();
  }
  if (fabsf(SetPointData.setPointPressure - oldPressure) > 0.00001f) {
    SetPointData.setPointPressureSetEpoch = NTPEpoch();
  }
  if (SetPointData.mode != oldMode) {
    resetChillHeatCycle();
    if (SetPointData.mode == MODE_FERMENTING) {
      resetCountersForNewBatch();
    }
  }
  
  writeSetPointDataToNIV();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='2;url=/'>";
  html += "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>SetPoint Data Saved!</h1>";
  html += "<p>Redirecting back to menu...</p>";
  html += "</body></html>";
  
  request->send(200, "text/html", html);
}

// ========== CONTROL DATA HANDLERS ==========



void handleControlDataPage(AsyncWebServerRequest *request) {
  const size_t BUFFER_SIZE = 20000;
  char* html = (char*)malloc(BUFFER_SIZE);
  //char html[BUFFER_SIZE];
  if (!html) {
    request->send(500, "text/plain", "Out of memory");
    return;
  }
  
  char buffer[500];
  size_t remaining;
  
  strcpy(html, "<!DOCTYPE html><html><head>"
                "<meta charset='UTF-8'>"
                "<title>Control</title>"
                "<style>"
                "body{font-family:Arial;margin:20px;background:#f0f0f0;}"
                ".c{background:white;padding:20px;border-radius:8px;max-width:520px;margin:0 auto;}"
                ".r{margin:10px 0;display:flex;align-items:center;gap:10px;}"
                "label{font-weight:bold;width:60px;font-size:12px;}"
                "input,select{padding:4px;border:1px solid #ddd;width:80px;font-size:12px;}"
                "input[readonly]{background:#f9f9f9;}"
                "button{background:#4CAF50;color:white;padding:6px 12px;border:none;margin:5px;}"
                ".action-btn{display:inline-block;background:#4CAF50;color:white;padding:6px 12px;margin:5px;text-decoration:none;border-radius:2px;}"
                ".sub-link{margin-top:8px;text-align:right;font-size:12px;}"
                ".sub-link a{color:#666;text-decoration:none;}"
                ".sub-link a:hover{text-decoration:underline;}"
                ".notice{margin-top:15px;padding:10px;border:1px dashed #999;background:#fafafa;font-size:12px;}"
                "</style>"
                "<script>"
                "function u(s,c){document.getElementById(c).checked=document.getElementById(s).value=='1';}"
                "</script>"
                "</head><body><div class='c'>"
                "<h3>Control</h3>"
                "<form method='POST' action='/control/update'>");


  remaining = BUFFER_SIZE - strlen(html) - 1;
  sprintf(buffer, "<div class='r'><label>Temp:</label><input type='number' step='0.01' value='%.1f' readonly></div>", ControlData.temperature);
  strncat(html, buffer, remaining);
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  sprintf(buffer, "<div class='r'><label>Press:</label><input type='number' step='0.01' value='%.1f' readonly></div>", ControlData.pressure);
  strncat(html, buffer, remaining);
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='r'><label>Chill:</label><select id='a' name='chillerOverride' onchange='u(\"a\",\"b\")'>", remaining);
  sprintf(buffer, "<option value='0'%s>Auto</option><option value='1'%s>ON</option><option value='2'%s>OFF</option></select>",
          ControlData.chillerOverride == 0 ? " selected" : "",
          ControlData.chillerOverride == 1 ? " selected" : "",
          ControlData.chillerOverride == 2 ? " selected" : "");
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  sprintf(buffer, "<input type='checkbox' id='b'%s readonly onclick='return false'></div>", ControlData.chillerSwitch ? " checked" : "");
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='r'><label>Heat:</label><select id='c' name='heaterOverride' onchange='u(\"c\",\"d\")'>", remaining);
  sprintf(buffer, "<option value='0'%s>Auto</option><option value='1'%s>ON</option><option value='2'%s>OFF</option></select>",
          ControlData.heaterOverride == 0 ? " selected" : "",
          ControlData.heaterOverride == 1 ? " selected" : "",
          ControlData.heaterOverride == 2 ? " selected" : "");

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  sprintf(buffer, "<input type='checkbox' id='d'%s readonly onclick='return false'></div>", ControlData.heaterSwitch ? " checked" : "");
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='r'><label>Trans:</label><select id='e' name='transferOverride' onchange='u(\"e\",\"f\")'>", remaining);
  sprintf(buffer, "<option value='0'%s>Auto</option><option value='1'%s>ON</option><option value='2'%s>OFF</option></select>",
          ControlData.transferOverride == 0 ? " selected" : "",
          ControlData.transferOverride == 1 ? " selected" : "",
          ControlData.transferOverride == 2 ? " selected" : "");
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  sprintf(buffer, "<input type='checkbox' id='f'%s readonly onclick='return false'></div>", ControlData.transferValve ? " checked" : "");
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<button type='submit'>Save</button>"
               "<button type='button' onclick='window.location=\"/\"'>Back</button>"
               "</form>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div style='margin-top:10px;'>"
               "<a class='action-btn' href='/control/auto'>All Auto</a>"
               "<a class='action-btn' href='/control/relief'>Relief once</a>"
               "<a class='action-btn' href='/resetdisplay'>Reset display</a>"
               "<a class='action-btn' href='/startvolume?from=control' onclick=\"return confirm('Start volume determination?');\">Start volume</a>"
               "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='sub-link'><a href='/counters'>Counters</a></div>", remaining);

  if (request->hasParam("volume", false)) {
    remaining = BUFFER_SIZE - strlen(html) - 1;
    strncat(html, "<div class='notice'>after volume determination finished, download results links are:<br>"
                 "<a href='/pressurehistory'>/pressurehistory</a><br>"
                 "<a href='/pressuredump'>/pressuredump</a></div>", remaining);
  }

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>"
               "<script>u('a','b');u('c','d');u('e','f');u('g','h');</script>"
               "</body></html>", remaining);
  
  request->send(200, "text/html", html);
  free(html);
}

void handleControlDataUpdate(AsyncWebServerRequest *request) {
  if (request->hasParam("chillerOverride", true)) {
    ControlData.chillerOverride = request->getParam("chillerOverride", true)->value().toInt();
  }
  if (request->hasParam("heaterOverride", true)) {
    ControlData.heaterOverride = request->getParam("heaterOverride", true)->value().toInt();
  }
  if (request->hasParam("transferOverride", true)) {
    ControlData.transferOverride = request->getParam("transferOverride", true)->value().toInt();
  }
  if (request->hasParam("reliefOverride", true)) {
    ControlData.reliefOverride = request->getParam("reliefOverride", true)->value().toInt();
  }
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='2;url=/'>";
  html += "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>Control Data Saved!</h1>";
  html += "<p>Redirecting back to menu...</p>";
  html += "</body></html>";
  
  request->send(200, "text/html", html);
}

void handleStartVolume(AsyncWebServerRequest *request) {
  char reason[80];
  if (debugging) {
    ControlData.pressure = 1.6;
  }
  
  bool started = startVolumeDetermination(reason, sizeof(reason));
  if (started) {
    request->redirect("/control?volume=1");
  } else {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='2;url=/control'>";
    html += "<style>body{font-family:Arial;text-align:center;margin-top:50px;}";
    html += "</head><body>";
    html += "<h1>Volume determination blocked</h1>";
    html += "<p>" + String(reason) + "</p>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  }
}

void handleControlAuto(AsyncWebServerRequest *request) {
  ControlData.chillerOverride = 0;
  ControlData.heaterOverride = 0;
  ControlData.transferOverride = 0;
  ControlData.reliefOverride = 0;
  request->redirect("/control");
}

void handleControlReliefOnce(AsyncWebServerRequest *request) {
  Serial.println("Relief once requested");
  pressureRelief(false);
  request->redirect("/control");
}







void handleUserConfigPage(AsyncWebServerRequest *request) {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>User Configuration</title><style>"
    "body{font-family:Arial,sans-serif;background:#f7f7ff;padding:20px;}"
    ".container{max-width:600px;margin:auto;background:white;padding:24px;border-radius:15px;}"
    "label{display:block;margin-top:20px;}input{box-sizing:border-box;width:100%;padding:10px;}"
    "button{margin-top:24px;padding:12px 24px;background:#667eea;color:white;border:0;border-radius:8px;}"
    "</style></head><body><div class='container'><h1>User Configuration</h1>"
    "<form action='/userConfig/update' method='POST'>"
    "<label for='screensaverTime'>Screensaver Time (seconds):</label>"
    "<input type='number' id='screensaverTime' name='screensaverTime' min='10' max='2147483647' step='1' required value='";
  html += String(UserConfigurationData.screensaverTime);
  html += "'><label for='keypadPin'>Keypad PIN (4 digits):</label>"
    "<input type='number' id='keypadPin' name='keypadPin' min='0' max='9999' step='1' required value='";
  html += String(UserConfigurationData.keypadPin);
  html += "'><label for='displayBrightness'>Display Brightness: <output id='brightnessValue'>";
  html += String(UserConfigurationData.displayBrightness);
  html += "</output> / 10</label><input type='range' id='displayBrightness' name='displayBrightness' "
    "min='1' max='10' step='1' oninput=\"document.getElementById('brightnessValue').value=this.value\" value='";
  html += String(UserConfigurationData.displayBrightness);
  html += "'><button type='submit'>Save</button> <a href='/'>Cancel</a></form></div></body></html>";
  request->send(200, "text/html", html);
}

static bool readUserConfigInteger(AsyncWebServerRequest *request, const char *name,
                                  int minimum, int maximum, int &value) {
  if (!request->hasParam(name, true)) return true;
  const String text = request->getParam(name, true)->value();
  if (text.isEmpty()) return false;
  // Parse digits with a bound check before multiplication, avoiding overflow.
  int parsed = 0;
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    if (c < '0' || c > '9' || parsed > (maximum - (c - '0')) / 10) return false;
    parsed = parsed * 10 + c - '0';
    if (parsed > maximum) return false;
  }
  if (parsed < minimum) return false;
  value = parsed;
  return true;
}

void handleUserConfigUpdate(AsyncWebServerRequest *request) {
  int screensaverTime = UserConfigurationData.screensaverTime;
  int keypadPin = UserConfigurationData.keypadPin;
  int brightness = UserConfigurationData.displayBrightness;
  if (!readUserConfigInteger(request, "screensaverTime", 10, 2147483647, screensaverTime) ||
      !readUserConfigInteger(request, "keypadPin", 0, 9999, keypadPin) ||
      !readUserConfigInteger(request, "displayBrightness", 1, 10, brightness)) {
    request->send(400, "text/plain", "Invalid settings: screensaver >= 10 seconds, PIN 0-9999, brightness 1-10.");
    return;
  }
  UserConfigurationData.screensaverTime = screensaverTime;
  UserConfigurationData.keypadPin = keypadPin;
  UserConfigurationData.displayBrightness = brightness;
  writeUserConfigurationDataToNIV();
  request->redirect("/userConfig");
}
