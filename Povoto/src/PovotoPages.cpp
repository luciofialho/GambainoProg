#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <IOTK_NTP.h>
#include <IOTK.h>
#include <math.h>
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "PressureControl.h"
#include "VentModel.h"
#include "TemperatureControl.h"
#include "GambainoCommon.h"
#include "IOTK_GLog.h"
#include "PovotoTasks.h"
#include "PovotoWifi.h"
#include "PovotoSettingsBackup.h"
#include "AutoSetpoints.h"
#include "datalog.h"
#include <stdarg.h>
#include <memory>
#include <esp_heap_caps.h>
#include "CloudLog.h"
#include "CloudSync.h"
#include "GraphHistory.h"
#include "Povoto_UI.h"
#include "DallasSensors.h"
#include "SensorZero.h"
#include "CalibrationEvents.h"
#include "VentingTest.h"
#include "ValveCleaning.h"
#include "KegVolume.h"
#include "NucleationRecord.h"
#include "AtmosphericPressure.h"

// ========== MAIN MENU ==========

void handleMainMenu(AsyncWebServerRequest *request) {
  if (povotoWiFiConfigurationActive()) {
    request->redirect("/wifi");
    return;
  }
  char dateTimeBuf[20];
  char volumeProgressBuf[48];
  NTPFormatedDateTime(dateTimeBuf);
  String uptimeStr = formatedUptime();

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Povoto " + String(FMTData.PovotoNum) + "</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; }";
  html += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 30px; border-radius: 15px; box-shadow: 0 10px 30px rgba(0,0,0,0.3); }";
  html += "h1 { color: #333; text-align: center; margin-bottom: 30px; font-size: 28px; }";
  html += "h1 a { color: inherit; text-decoration: none; }";
  html += ".back-row { text-align: right; margin-top: 20px; }";
  html += ".back-button { display: inline-block; padding: 10px 28px; background: #eee; color: #333; text-decoration: none; border-radius: 8px; font-size: 16px; }";
  html += ".brand{display:flex;align-items:center;justify-content:space-between;gap:14px}.brand-name{display:flex;align-items:center;gap:14px}.brand img{width:78px;height:78px;object-fit:contain}";
  html += ".signature{text-align:center;margin-top:26px}.signature img{display:block;width:220px;max-width:65%;height:auto;margin:0 auto}";
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
  // Logo and name on the left, the fermenter on the right.
  html += "<h1 class='brand'><a class='brand-name' href='/dashboard'><img src='/assets/povoto.svg' alt='Povoto logo'>Povoto</a>"
          "<a href='/dashboard'>FMT " + String(FMTData.PovotoNum) + "</a></h1>";
  html += "<div class='status-box'>";
  html += "<div class='status-item'><strong>Temperature:</strong> " + String(ControlData.temperature, 1) + " °C</div>";
  html += "<div class='status-item'><strong>Pressure:</strong> " + String(ControlData.pressure, 2) + " bar</div>";
  html += "<div class='status-item'><strong>Volume:</strong> " + String(beerVolume, 1) + " L</div>";
  // During a pressure/temperature transition the last stable rate is shown;
  // an info icon gives its time and the preliminary reading on hover.
  String co2RateHtml = String(getBeerCO2EvolutionGramsPerLiterPerDay(), 2);
  if (co2RateInTransition()) {
    uint32_t stableAt = 0;
    const float stableRate = getCO2TransitionStableRate(&stableAt);
    String tip;
    if (isfinite(stableRate)) {
      const time_t at = stableAt, today = NTPEpoch();
      struct tm atTm, todayTm;
      gmtime_r(&at, &atTm);
      gmtime_r(&today, &todayTm);
      char when[16];
      strftime(when, sizeof(when),
               atTm.tm_yday == todayTm.tm_yday && atTm.tm_year == todayTm.tm_year ? "%H:%M" : "%d/%m %H:%M", &atTm);
      tip = "Value is last stable reading at " + String(when) + ". Preliminary unstable reading now is " + co2RateHtml;
      co2RateHtml = String(stableRate, 2);
    } else {
      tip = "Transition: net CO2 release (production &minus; absorption)";
    }
    co2RateHtml += " <span title='" + tip + "' style='cursor:help'>&#9432;</span>";
  }
  html += "<div class='status-item'><strong>SG:</strong> " + String(beerSG, 3) + " (gCO2/L/d: " + co2RateHtml + ")</div>";
  html += "<div class='status-item'><strong>Uptime:</strong> " + uptimeStr + "</div>";
  html += "<div class='status-item'><strong>Date/Time:</strong> " + String(dateTimeBuf) + "</div>";
  html += "</div>";
  html += "<div class='menu-grid'>";
  html += "<a href='/tasks' class='menu-button'><span class='icon'>&#9881;&#65039;</span>Tasks</a>";
  html += "<a href='/setpoint' class='menu-button'><span class='icon'>&#127777;</span>Set Points</a>";
  html += "<a href='/batch' class='menu-button'><span class='icon'>&#128218;</span>Batch Data</a>";
  html += "<a href='/control' class='menu-button'><span class='icon'>&#128736;</span>Control</a>";
  html += "<a href='/calibration' class='menu-button'><span class='icon'>&#128200;</span>Calibration</a>";
  html += "<a href='/graphs' class='menu-button'><span class='icon'>&#128202;</span>Graphs</a>";
  html += "<a href='/fmtdata' class='menu-button'><span class='icon'>&#9881;</span>Settings</a>";
  html += "<a href='/userConfig' class='menu-button'><span class='icon'>&#127899;&#65039;</span>User Configuration</a>";
  if (debugging) {
    html += "<a href='/debugparams' class='menu-button'><span class='icon'>&#128295;</span>Debug Params</a>";
  }
  html += "</div>";
  html += "<div class='back-row'><a href='/dashboard' class='back-button'>Back</a></div>";
  html += "<div class='footer-link'><a href='/getstatus'>full status</a></div>";
  html += "<div class='signature'><img src='/assets/brewtal.svg' alt='Brewtal'></div>";
  html += "</div>";
  html += "</body></html>";
  
  request->send(200, "text/html", html);
}

// ========== GRAPH HISTORY ==========

// Page files from the web LittleFS. Browsers revalidate them on every load,
// so a new filesystem image takes effect without a stale cached script.
void sendWebPageFile(AsyncWebServerRequest *request, const char *path, const char *contentType) {
  AsyncWebServerResponse *response = request->beginResponse(LittleFS, path, contentType);
  response->addHeader("Cache-Control", "no-cache");
  request->send(response);
}

void handleGraphsPage(AsyncWebServerRequest *request) {
  if (!LittleFS.exists("/www/graphs.html")) {
    request->send(503, "text/plain", "Graphs page missing from LittleFS; upload data/www.");
    return;
  }
  sendWebPageFile(request, "/www/graphs.html", "text/html; charset=utf-8");
}

void handleGraphsMeta(AsyncWebServerRequest *request) {
  char json[48];
  if (isfinite(BatchData.batchOG) && BatchData.batchOG > 0.0f)
    snprintf(json, sizeof(json), "{\"og\":%.5f}", BatchData.batchOG);
  else
    snprintf(json, sizeof(json), "{\"og\":null}");
  AsyncWebServerResponse *response = request->beginResponse(
      200, "application/json; charset=utf-8", json);
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
}

struct GraphCsvCursor {
  GraphHistoryPoint *snapshot = nullptr;
  uint16_t total;
  uint16_t next;
  bool headerSent;
  char line[256];
  size_t used;
  size_t position;
  ~GraphCsvCursor() {
    if (snapshot) heap_caps_free(snapshot);
  }
};

static void graphCsvFloat(char *out, size_t capacity, float value, int decimals) {
  if (!isfinite(value)) {
    out[0] = '\0';
    return;
  }
  snprintf(out, capacity, "%.*f", decimals, value);
  for (char *digit = out; *digit; ++digit) {
    if (*digit == '.') *digit = ',';
  }
}

void handleGraphsCSV(AsyncWebServerRequest *request) {
  auto cursor = std::make_shared<GraphCsvCursor>();
  const uint16_t available = graphHistoryCount();
  if (available) {
    cursor->snapshot = static_cast<GraphHistoryPoint *>(heap_caps_malloc(
        size_t(available) * sizeof(GraphHistoryPoint), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!cursor->snapshot) {
      request->send(503, "text/plain", "Not enough PSRAM to export graph history.");
      return;
    }
  }
  cursor->total = graphHistoryCopyPoints(cursor->snapshot, available);
  cursor->next = 0;
  cursor->headerSent = false;
  cursor->used = cursor->position = 0;

  AsyncWebServerResponse *response = request->beginChunkedResponse(
      "text/csv; charset=utf-8",
      [cursor](uint8_t *buffer, size_t maxLen, size_t) -> size_t {
        size_t written = 0;
        while (written < maxLen) {
          if (cursor->position == cursor->used) {
            cursor->position = 0;
            if (!cursor->headerSent) {
              const char *header = "epoch_local;datetime_local;SG;TempC;TempSetpointC;"
                                   "PressureBar;PressureSetpointBar;gCO2_L_d;ABV_percent;flags\r\n";
              cursor->used = strlen(header);
              memcpy(cursor->line, header, cursor->used + 1);
              cursor->headerSent = true;
            } else {
              if (cursor->next >= cursor->total) {
                // End: nothing pending, so the next call returns 0 and closes the
                // response (position was reset above; without this the last row
                // was sent again on every call, forever).
                cursor->used = 0;
                break;
              }
              const GraphHistoryPoint &point = cursor->snapshot[cursor->next++];
              char date[24], sg[20], temp[20], tempSp[20], press[20], pressSp[20], rate[20], abv[20];
              formatLocalEpochISO(point.epoch, date, sizeof(date));
              graphCsvFloat(sg, sizeof(sg), point.sg, 5);
              graphCsvFloat(temp, sizeof(temp), point.temperature, 2);
              graphCsvFloat(tempSp, sizeof(tempSp), point.temperatureSetpoint, 2);
              graphCsvFloat(press, sizeof(press), point.pressure, 3);
              graphCsvFloat(pressSp, sizeof(pressSp), point.pressureSetpoint, 3);
              graphCsvFloat(rate, sizeof(rate), point.co2Rate, 3);
              graphCsvFloat(abv, sizeof(abv), point.abv, 2);
              const int length = snprintf(cursor->line, sizeof(cursor->line),
                  "%lu;%s;%s;%s;%s;%s;%s;%s;%s;%lu\r\n",
                  static_cast<unsigned long>(point.epoch), date, sg, temp, tempSp,
                  press, pressSp, rate, abv, static_cast<unsigned long>(point.flags));
              cursor->used = length > 0 && static_cast<size_t>(length) < sizeof(cursor->line)
                  ? static_cast<size_t>(length) : 0;
              if (!cursor->used) continue;
            }
          }
          const size_t pending = cursor->used - cursor->position;
          const size_t space = maxLen - written;
          const size_t amount = pending < space ? pending : space;
          memcpy(buffer + written, cursor->line + cursor->position, amount);
          cursor->position += amount;
          written += amount;
        }
        return written;
      });
  response->addHeader("Content-Disposition", "attachment; filename=\"povoto-graphs.csv\"");
  response->addHeader("Cache-Control", "no-store");
  // Read-only graph data may also be previewed from a local file:// page.
  response->addHeader("Access-Control-Allow-Origin", "*");
  request->send(response);
}

void handleGraphsGenerateDemo(AsyncWebServerRequest *request) {
  if (!debugging) {
    request->send(403, "text/plain", "Debug mode only");
    return;
  }
  if (!request->hasParam("confirm", true) ||
      request->getParam("confirm", true)->value() != "replace") {
    request->send(400, "text/plain", "Explicit graph history replacement confirmation required.");
    return;
  }
  if (!graphHistoryGenerateDemo14Days()) {
    request->send(503, "text/plain", "Cannot replace history: NTP, PSRAM or LittleFS unavailable.");
    return;
  }
  request->redirect("/graphs");
}

// ========== WEB DASHBOARD ==========

void handleDashboardPage(AsyncWebServerRequest *request) {
  if (!LittleFS.exists("/www/dashboard.html")) {
    request->send(503, "text/plain", "Dashboard page missing from LittleFS; upload data/www.");
    return;
  }
  sendWebPageFile(request, "/www/dashboard.html", "text/html; charset=utf-8");
}

static void dashboardJsonText(String &json, const char *value) {
  json += '"';
  for (const char *c = value; *c; ++c) {
    const uint8_t ch = uint8_t(*c);
    if (ch == '"' || ch == '\\') {
      json += '\\';
      json += char(ch);
    } else if (ch < 0x20) {
      char escaped[8];
      snprintf(escaped, sizeof(escaped), "\\u%04x", ch);
      json += escaped;
    } else {
      json += char(ch);
    }
  }
  json += '"';
}

static void dashboardJsonString(String &json, const char *key, const char *value) {
  json += '"';
  json += key;
  json += "\":";
  dashboardJsonText(json, value);
  json += ',';
}

// hh:mm:ss, hours without limit.
static void dashboardDuration(char *out, size_t size, long seconds) {
  if (seconds < 0) seconds = 0;
  snprintf(out, size, "%ld:%02ld:%02ld", seconds / 3600, (seconds / 60) % 60, seconds % 60);
}

// "key":["mol","g"], (CO2, 44.01 g/mol).
static void dashboardMolsAndGrams(String &json, const char *key, double mols) {
  char mol[24], grams[24];
  snprintf(mol, sizeof(mol), "%.3f", mols);
  snprintf(grams, sizeof(grams), "%.1f", mols * 44.01);
  json += '"';
  json += key;
  json += "\":[";
  dashboardJsonText(json, mol);
  json += ',';
  dashboardJsonText(json, grams);
  json += "],";
}

// Texts are formatted exactly as screenData() draws them on the TFT, so the
// page shows the same digits; the page only positions them.
void handleDashboardStatus(AsyncWebServerRequest *request) {
  char text[96];
  String json;
  json.reserve(1024);
  json += '{';

  snprintf(text, sizeof(text), "%04d", BatchData.batchNumber);
  dashboardJsonString(json, "batchNumber", text);
  formatBatchDateShort(BatchData.batchDate, text, sizeof(text));
  dashboardJsonString(json, "batchDate", text);
  snprintf(text, sizeof(text), "%d", FMTData.PovotoNum);
  dashboardJsonString(json, "povotoNum", text);
  dashboardJsonString(json, "batchName", BatchData.batchName);
  snprintf(text, sizeof(text), "SG   [ OG=%.3f ]", BatchData.batchOG);
  dashboardJsonString(json, "ogLabel", text);

  if (ControlData.temperature != NOTaTEMP)
    snprintf(text, sizeof(text), "%.1f", ControlData.temperature);
  else
    snprintf(text, sizeof(text), "ERR");
  dashboardJsonString(json, "temperature", text);
  if (SetPointData.setPointTemp != NOTaTEMP)
    snprintf(text, sizeof(text), "%.1f", SetPointData.setPointTemp);
  else
    snprintf(text, sizeof(text), "n/a");
  dashboardJsonString(json, "tempTarget", text);
  if (SetPointData.setPointSlowTemp != NOTaTEMP)
    snprintf(text, sizeof(text), "%.1f", SetPointData.setPointSlowTemp);
  else
    snprintf(text, sizeof(text), "n/a");
  dashboardJsonString(json, "slowTarget", text);
  snprintf(text, sizeof(text), "%.1f", ControlData.pressure);
  dashboardJsonString(json, "pressure", text);
  if (SetPointData.setPointPressure != 0)
    snprintf(text, sizeof(text), "%.2f", SetPointData.setPointPressure);
  else
    snprintf(text, sizeof(text), "n/a");
  dashboardJsonString(json, "pressureTarget", text);

  getCO2RateLabel(text, sizeof(text));
  dashboardJsonString(json, "co2Label", text);
  snprintf(text, sizeof(text), "%.0f", CO2Mass());
  dashboardJsonString(json, "co2Mass", text);
  snprintf(text, sizeof(text), "%.0f", beerVolume);
  dashboardJsonString(json, "volume", text);
  snprintf(text, sizeof(text), "%.3f", beerSG);
  dashboardJsonString(json, "sg", text);
  snprintf(text, sizeof(text), "%.2f", beerABV);
  dashboardJsonString(json, "abv", text);

  // Statistics view of the dashboard (statistics icon): texts as shown.
  json += "\"stats\":{";
  dashboardJsonString(json, "tempMode",
      ChillHeatMode == FMTCHILL ? "Chill" : ChillHeatMode == FMTHEAT ? "Heat" : "Idle");
  dashboardDuration(text, sizeof(text), CountersData.totalChillTime);
  dashboardJsonString(json, "chillTime", text);
  dashboardDuration(text, sizeof(text), CountersData.totalHeatTime);
  dashboardJsonString(json, "heatTime", text);
  snprintf(text, sizeof(text), "%.2f", CountersData.headSpaceVolume);
  dashboardJsonString(json, "headspaceVolume", text);
  snprintf(text, sizeof(text), "%.2f", CountersData.dumpedVolume);
  dashboardJsonString(json, "dumpedVolume", text);
  snprintf(text, sizeof(text), "%lu", (unsigned long)CountersData.totalReliefCount);
  dashboardJsonString(json, "reliefCount", text);
  dashboardMolsAndGrams(json, "co2Headspace", headSpaceCO2Mols);
  dashboardMolsAndGrams(json, "co2Solution", CountersData.CO2InSolution);
  dashboardMolsAndGrams(json, "co2Vented", CountersData.totalMolsEjected);
  json.remove(json.length() - 1);  // last comma
  json += "},";

  const int wifiBars = povotoWiFiStatusIndicator(text, sizeof(text));
  dashboardJsonString(json, "wifiText", text);
  json += "\"wifiBars\":";
  json += wifiBars;
  json += ",\"calibration\":[";
  for (uint8_t i = 0; i < 3; ++i) {
    if (i) json += ',';
    dashboardJsonText(json, getCalibrationDisplayLine(i));
  }
  json += "]}";

  AsyncWebServerResponse *response = request->beginResponse(
      200, "application/json; charset=utf-8", json);
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

// ========== DEBUG PARAMS HANDLERS ==========

void handleDebugParamsPage(AsyncWebServerRequest *request) {
  if (!debugging) {
    request->send(403, "text/plain", "Debug mode only");
    return;
  }

  const size_t BUFFER_SIZE = 6000;
  char* html = (char*)malloc(BUFFER_SIZE);
  if (!html) {
    request->send(500, "text/plain", "Out of memory");
    return;
  }

  char buffer[512];
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
               "<label for='pressure'>Pressure (bar) - empty reads the INA:</label>", remaining);
  // Empty unless overridden, so saving other fields does not freeze the pressure.
  if (debugPressureOverride)
    sprintf(buffer, "<input type='number' id='pressure' name='pressure' value='%.3f' step='0.001'>", ControlData.pressure);
  else
    sprintf(buffer, "<input type='number' id='pressure' name='pressure' value='' step='0.001' placeholder='%.3f (INA)'>", ControlData.pressure);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='temperature'>Temperature (°C) - empty reads the Dallas:</label>", remaining);
  // Empty unless overridden, so saving other fields does not freeze the temperature.
  if (debugTemperatureOverride)
    sprintf(buffer, "<input type='number' id='temperature' name='temperature' value='%.2f' step='0.01'>", ControlData.temperature);
  else
    sprintf(buffer, "<input type='number' id='temperature' name='temperature' value='' step='0.01' placeholder='%.2f (Dallas)'>", ControlData.temperature);
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

  {
    char tempSince[20], pressSince[20];
    formatLocalEpochISO(CountersData.tempStableSince, tempSince, sizeof(tempSince));
    formatLocalEpochISO(CountersData.pressStableSince, pressSince, sizeof(pressSince));
    snprintf(buffer, sizeof(buffer),
             "<div class='form-group'><label for='stabilityShift'>Add to stability time (h):</label>"
             "<input type='number' id='stabilityShift' name='stabilityShift' value='' step='any' min='0'>"
             "<small>Moves 'stable since' back, for STABLE states only. "
             "Temperature: %s %s. Pressure: %s %s.</small></div>",
             getTempStateLabel(), tempSince, getPressStateLabel(), pressSince);
  }
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<button type='submit'>Save</button> "
               "<button type='button' class='btn-secondary' onclick='window.location=\"/menu\"'>Cancel</button>"
               "</form><hr><h2>Graph history preview</h2>"
               "<p>Replace the current graph history with 14 days of synthetic data. "
               "The replacement is persisted in LittleFS; new Fermenting samples follow it.</p>"
               "<form action='/debugparams/graphs/demo' method='POST' "
               "onsubmit='return confirm(\"Replace the current graph history in PSRAM and LittleFS with 14 days of synthetic data?\")'>"
               "<input type='hidden' name='confirm' value='replace'>"
               "<button type='submit'>Replace graph history with 14-day demo</button></form>"
               "<p><a href='/graphs'>Open Graphs / CSV downloads</a></p>", remaining);

  {
    char startText[20], lastText[20], cloud[800];
    formatLocalEpochISO(cloudSyntheticLogStart(), startText, sizeof(startText));
    formatLocalEpochISO(cloudLogLastSentEpoch(), lastText, sizeof(lastText));
    snprintf(cloud, sizeof(cloud),
             "<hr><h2>Cloud log</h2>"
             "<p>One record every %lu s to the SideKick. Sent: %lu, errors: %lu, last: %s.</p>"
             "<form action='/debugparams/cloud' method='POST'>"
             "<label><input type='checkbox' name='synthetic' value='1'%s> Synthetic log</label>"
             "<small>The cloud records carry the 14-day synthetic profile (restarting every 14 days) "
             "instead of the measurements; kept across reboots. %s%s</small><br>"
             "<button type='submit'>Save cloud log</button></form>",
             (unsigned long)cloudLogSlotSeconds(), cloudLogSentCount(), cloudLogSendErrors(),
             lastText[0] ? lastText : "never",
             cloudSyntheticLogStored() ? " checked" : "",
             cloudSyntheticLogStored() ? "Day 0: " : "",
             cloudSyntheticLogStored() ? startText : "");
    remaining = BUFFER_SIZE - strlen(html) - 1;
    strncat(html, cloud, remaining);
  }
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div></body></html>", remaining);

  request->send(200, "text/html", html);
  free(html);
}

void handleDebugCloudUpdate(AsyncWebServerRequest *request) {
  if (!debugging) {
    request->send(403, "text/plain", "Debug mode only");
    return;
  }
  const bool synthetic = request->hasParam("synthetic", true);
  if (!setCloudSyntheticLog(synthetic)) {
    request->send(503, "text/plain", "Could not save: NTP or NVS unavailable.");
    return;
  }
  request->redirect("/debugparams");
}

void handleDebugParamsUpdate(AsyncWebServerRequest *request) {
  if (!debugging) {
    request->send(403, "text/plain", "Debug mode only");
    return;
  }

  if (request->hasParam("pressure", true)) {
    String value = request->getParam("pressure", true)->value();
    value.trim();
    // A value replaces the INA reading; an empty field returns to the INA.
    debugPressureOverride = value.length() > 0;
    if (debugPressureOverride)
      ControlData.pressure = value.toFloat();
  }
  if (request->hasParam("temperature", true)) {
    String value = request->getParam("temperature", true)->value();
    value.trim();
    // A value replaces the Dallas reading; an empty field returns to the Dallas.
    debugTemperatureOverride = value.length() > 0;
    if (debugTemperatureOverride)
      ControlData.temperature = value.toFloat();
  }
  if (request->hasParam("sgPointTime", true)) {
    sgPointGenerationTime = request->getParam("sgPointTime", true)->value().toFloat();
  }
  if (request->hasParam("stabilityShift", true)) {
    // Pretends that more time has passed since the temperature/pressure became stable.
    const float hours = request->getParam("stabilityShift", true)->value().toFloat();
    if (isfinite(hours) && hours > 0.0f) {
      const uint32_t shift = (uint32_t)(hours * 3600.0f);
      if (CountersData.tempState == TEMP_STATE_STABLE && CountersData.tempStableSince != 0) {
        CountersData.tempStableSince = CountersData.tempStableSince > shift
            ? CountersData.tempStableSince - shift : 1;
        writeTempStabilityToNIV();
      }
      if (CountersData.pressState == TEMP_STATE_STABLE && CountersData.pressStableSince != 0) {
        CountersData.pressStableSince = CountersData.pressStableSince > shift
            ? CountersData.pressStableSince - shift : 1;
        writePressureStabilityToNIV();
      }
    }
  }

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='2;url=/menu'>";
  html += "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>Debug Params Saved!</h1>";
  html += "<p>Redirecting back to menu...</p>";
  html += "</body></html>";

  request->send(200, "text/html", html);
}

// ========== FMT DATA HANDLERS ==========

// Thermometers on the Dallas bus and their roles (docs/thermometers.md). The
// list is the last scan, made in the main loop (boot or "Rescan bus").
static const char *const DALLAS_ROLE_FIELDS[DALLAS_ROLE_COUNT] = {
  "dallasFermenter", "dallasEnvironment", "dallasHeadspace"};
static const char *const DALLAS_ROLE_LABELS[DALLAS_ROLE_COUNT] = {
  "Fermenter", "Environment", "Headspace"};

static void dallasTemperatureText(float t, char *out, size_t size) {
  if (isfinite(t) && t != NOTaTEMP && t != 85.0f) snprintf(out, size, "%.2f &deg;C", t);
  else snprintf(out, size, "n/a");
}

static void appendDallasRolesSection(char *html, size_t capacity) {
  char buffer[320];
  char id[DALLAS_ADDRESS_TEXT_SIZE];
  char reading[24];
  DallasScanEntry scan[DALLAS_SCAN_MAX];
  const uint8_t count = getDallasScan(scan, DALLAS_SCAN_MAX);
  auto append = [&](const char *text) { strncat(html, text, capacity - strlen(html) - 1); };
  auto onBus = [&](const uint8_t *address) {
    for (uint8_t i = 0; i < count; ++i)
      if (memcmp(scan[i].address, address, 8) == 0) return true;
    return false;
  };

  append("<h2>Thermometers (Dallas)</h2><input type='hidden' name='dallasConfig' value='1'>"
         "<table style='width:100%'><thead><tr><th>ID</th><th>Reading</th><th>Role</th></tr></thead><tbody>");
  for (uint8_t i = 0; i < count; ++i) {
    const char *role = "-";
    for (uint8_t r = 0; r < DALLAS_ROLE_COUNT; ++r)
      if (memcmp(FMTData.dallasAddress[r], scan[i].address, 8) == 0) role = DALLAS_ROLE_LABELS[r];
    formatDallasAddress(scan[i].address, id, sizeof(id));
    dallasTemperatureText(scan[i].temperature, reading, sizeof(reading));
    snprintf(buffer, sizeof(buffer), "<tr><td><code>%s</code></td><td>%s</td><td>%s</td></tr>", id, reading, role);
    append(buffer);
  }
  for (uint8_t r = 0; r < DALLAS_ROLE_COUNT; ++r) {
    if (!dallasRoleAssigned(r) || onBus(FMTData.dallasAddress[r])) continue;
    formatDallasAddress(FMTData.dallasAddress[r], id, sizeof(id));
    snprintf(buffer, sizeof(buffer), "<tr><td><code>%s</code></td><td><b>not found</b></td><td>%s</td></tr>",
             id, DALLAS_ROLE_LABELS[r]);
    append(buffer);
  }
  if (count == 0) append("<tr><td colspan='3'>No thermometer found on the bus.</td></tr>");
  append("</tbody></table>");

  const char *emptyOption[DALLAS_ROLE_COUNT] = {
    "Not assigned (no temperature control)",
    "Broadcast by ESP-NOW (SideKick, else BrewCore)",
    "Not assigned (gas temperature = fermenter)"};
  const float current[DALLAS_ROLE_COUNT] = {dallasTemperature, environmentTemp, gasTemperature()};
  const char *source[DALLAS_ROLE_COUNT] = {"Dallas", environmentTempSource(), gasTemperatureSource()};
  for (uint8_t r = 0; r < DALLAS_ROLE_COUNT; ++r) {
    dallasTemperatureText(current[r], reading, sizeof(reading));
    snprintf(buffer, sizeof(buffer),
             "<div class='form-group'%s><label for='%s'>%s thermometer (now %s, %s):</label><select id='%s' name='%s'>"
             "<option value=''%s>%s</option>",
             r == 0 ? " style='margin-top:20px'" : "",
             DALLAS_ROLE_FIELDS[r], DALLAS_ROLE_LABELS[r], reading, source[r], DALLAS_ROLE_FIELDS[r],
             DALLAS_ROLE_FIELDS[r], dallasRoleAssigned(r) ? "" : " selected", emptyOption[r]);
    append(buffer);
    // The sensors on the bus, plus the assigned one when it is missing, so
    // saving the page never drops it.
    for (uint8_t i = 0; i <= count; ++i) {
      const uint8_t *address = i < count ? scan[i].address : FMTData.dallasAddress[r];
      if (i == count && (!dallasRoleAssigned(r) || onBus(address))) break;
      formatDallasAddress(address, id, sizeof(id));
      if (i < count) dallasTemperatureText(scan[i].temperature, reading, sizeof(reading));
      else snprintf(reading, sizeof(reading), "not found");
      snprintf(buffer, sizeof(buffer), "<option value='%s'%s>%s (%s)</option>", id,
               memcmp(FMTData.dallasAddress[r], address, 8) == 0 ? " selected" : "", id, reading);
      append(buffer);
    }
    append("</select>");
    if (r == DALLAS_ROLE_FERMENTER && !dallasRoleAssigned(r))
      append("<small><b>No fermenter thermometer: temperature control is idle.</b></small>");
    append("</div>");
  }
  append("<div style='margin-bottom:28px'><button type='button' class='btn-secondary' "
         "onclick='window.location=\"/fmtdata/dallasscan\"'>Rescan bus</button></div>");
}

void handleDallasScan(AsyncWebServerRequest *request) {
  requestDallasScan();
  request->send(200, "text/html",
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='3;url=/fmtdata'>"
    "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style></head>"
    "<body><h1>Scanning the Dallas bus...</h1><p>Back to Settings in 3 seconds.</p></body></html>");
}

void handleFMTDataPage(AsyncWebServerRequest *request) {
  // Four thermometer roles with up to DALLAS_SCAN_MAX options each.
  const size_t BUFFER_SIZE = 18000;
  char* html = (char*)malloc(BUFFER_SIZE);
  if (!html) {
    request->send(500, "text/plain", "Out of memory");
    return;
  }
  
  char buffer[320];
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
                "input[type='number'], input[type='text'], select { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }"
                "button { background-color: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; margin-top: 10px; }"
                "button:hover { background-color: #45a049; }"
                ".btn-secondary { background-color: #888; }"
                ".btn-secondary:hover { background-color: #666; }"
                ".settings-actions { display:flex; flex-wrap:wrap; gap:8px; margin-top:24px; }"
                "</style>"
                "</head><body>"
                "<div class='container'>"
                "<h1>Settings</h1>"
                "<form id='settingsForm' action='/fmtdata/update' method='POST'>");
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='PovotoNum'>Povoto Number:</label>", remaining);
  sprintf(buffer, "<input type='number' id='PovotoNum' name='PovotoNum' value='%d' min='0' max='255'>", FMTData.PovotoNum);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'><label for='dataLogIntervalSeconds'>Log interval:</label>"
    "<select id='dataLogIntervalSeconds' name='dataLogIntervalSeconds'>", remaining);
  const int logIntervals[] = {30, 60, 120, 300, 600};
  const char *logIntervalLabels[] = {"30 seconds", "1 minute", "2 minutes", "5 minutes", "10 minutes"};
  for (uint8_t i = 0; i < sizeof(logIntervals) / sizeof(logIntervals[0]); ++i) {
    snprintf(buffer, sizeof(buffer), "<option value='%d'%s>%s</option>", logIntervals[i],
      FMTData.dataLogIntervalSeconds == logIntervals[i] ? " selected" : "", logIntervalLabels[i]);
    strncat(html, buffer, BUFFER_SIZE - strlen(html) - 1);
  }
  strncat(html, "</select></div>", BUFFER_SIZE - strlen(html) - 1);
  // The hidden field tells the update that the checkbox was on the form
  // (an unchecked checkbox is not posted).
  snprintf(buffer, sizeof(buffer),
           "<div class='form-group'><input type='hidden' name='cloudEditsField' value='1'>"
           "<label><input type='checkbox' name='cloudEdits' value='1'%s> Accept new setpoints and triggered actions from cloud</label>"
           "<small>Set points and triggered action rules changed on the cloud site are applied by this Povoto.</small></div>",
           cloudEditsAccepted() ? " checked" : "");
  strncat(html, buffer, BUFFER_SIZE - strlen(html) - 1);
  
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='form-group'>"
               "<label for='FMTVolume'>FMT Total Volume (L):</label>", remaining);
  sprintf(buffer, "<input type='number' id='FMTVolume' name='FMTVolume' value='%.2f' step='0.01'>", FMTData.FMTVolume);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  // Pressure limits, the vent drop and the altitude, in this order.
  snprintf(buffer, sizeof(buffer),
           "<div class='form-group'><label for='maximumPressure'>Maximum security pressure (bar):</label>"
           "<input type='number' id='maximumPressure' name='maximumPressure' value='%.2f' step='0.01' min='2.01' max='2.99'>"
           "<small>2.0 to 3.0 bar. The valve opens above it.</small></div>", FMTData.maximumPressure);
  strncat(html, buffer, BUFFER_SIZE - strlen(html) - 1);
  snprintf(buffer, sizeof(buffer),
           "<div class='form-group'><label for='maxSetPointPressure'>Maximum pressure setpoint (bar):</label>"
           "<input type='number' id='maxSetPointPressure' name='maxSetPointPressure' value='%.2f' step='0.01' min='0.1' max='2.98'>"
           "<small>Highest pressure set point accepted (page, triggered actions, cloud); below the security pressure.</small></div>",
           FMTData.maxSetPointPressure);
  strncat(html, buffer, BUFFER_SIZE - strlen(html) - 1);
  snprintf(buffer, sizeof(buffer),
           "<div class='form-group'><label for='ventTargetDropPercent'>Target drop of the gauge pressure per vent (%%):</label>"
           "<input type='number' id='ventTargetDropPercent' name='ventTargetDropPercent' value='%.2f' step='any' min='1' max='15' required>"
           "</div>", FMTData.ventTargetDropPercent);
  strncat(html, buffer, BUFFER_SIZE - strlen(html) - 1);
  snprintf(buffer, sizeof(buffer),
           "<div class='form-group'><label for='FMTAltitude'>Altitude (m):</label>"
           "<input type='number' id='FMTAltitude' name='FMTAltitude' value='%.1f' step='0.1' min='0'></div>", FMTData.FMTAltitude);
  strncat(html, buffer, BUFFER_SIZE - strlen(html) - 1);

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

  appendDallasRolesSection(html, BUFFER_SIZE);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</form>"
                "<div class='settings-actions'>"
                "<button type='button' class='btn-secondary' onclick='window.location=\"/wifi/reconfigure\"'>Reconfigure WiFi</button>"
                "<button type='button' onclick='window.location=\"/fmtdata/save\"'>Backup settings</button>"
                "<button type='button' class='btn-secondary' onclick='document.getElementById(\"settingsFile\").click()'>Restore settings from backups</button>"
                "<input id='settingsFile' type='file' accept='application/json,.json' hidden>"
                "</div>"
                "<div class='settings-actions'>"
                "<button type='submit' form='settingsForm'>Save</button>"
                "<button type='button' class='btn-secondary' onclick='window.location=\"/menu\"'>Cancel</button>"
                "</div>"
                "<script>document.getElementById('settingsFile').addEventListener('change',async function(){const file=this.files[0];if(!file)return;try{const response=await fetch('/fmtdata/load',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'settings='+encodeURIComponent(await file.text())});const message=await response.text();if(!response.ok){alert(message);return;}alert(message);window.location='/fmtdata';}catch(error){alert('Could not restore the settings file.');}finally{this.value='';}});</script>"
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
  const auto postedFloat = [request](const char *name, float current) {
    return request->hasParam(name, true) ? request->getParam(name, true)->value().toFloat() : current;
  };
  const float maximumPressure = postedFloat("maximumPressure", FMTData.maximumPressure);
  const float maxSetPointPressure = postedFloat("maxSetPointPressure", FMTData.maxSetPointPressure);
  const float ventDrop = postedFloat("ventTargetDropPercent", FMTData.ventTargetDropPercent);
  if (!isValidMaximumPressure(maximumPressure) || !(FMTData.pressure1Bar < maximumPressure)) {
    request->send(400, "text/plain", "Maximum security pressure must be between 2.0 and 3.0 bar and above calibration point B.");
    return;
  }
  if (!isValidMaxSetPointPressure(maxSetPointPressure, maximumPressure)) {
    request->send(400, "text/plain", "Maximum pressure setpoint must be above 0 and below the maximum security pressure.");
    return;
  }
  if (!isValidVentTargetDrop(ventDrop)) {
    request->send(400, "text/plain", "Target drop of the gauge pressure per vent must be between 1 and 15%.");
    return;
  }
  float heaterOn = FMTData.heater.onMinutes;
  float heaterOff = FMTData.heater.offMinutes;
  if (!readCoolingMinutes(request, "FMTHeaterOnTime", heaterOn) ||
      !readCoolingMinutes(request, "FMTHeaterOffTime", heaterOff)) {
    request->send(400, "text/plain", "Heater times must be between 0.01 and 1440 minutes.");
    return;
  }
  const bool dallasConfig = request->hasParam("dallasConfig", true);
  uint8_t dallasAddress[DALLAS_ROLE_COUNT][8];
  memcpy(dallasAddress, FMTData.dallasAddress, sizeof(dallasAddress));
  if (dallasConfig) {
    for (uint8_t r = 0; r < DALLAS_ROLE_COUNT; ++r) {
      const String text = request->hasParam(DALLAS_ROLE_FIELDS[r], true)
          ? request->getParam(DALLAS_ROLE_FIELDS[r], true)->value() : String();
      if (!parseDallasAddress(text.c_str(), dallasAddress[r])) {
        request->send(400, "text/plain", "Invalid thermometer ID.");
        return;
      }
    }
    if (!dallasRolesDistinct(dallasAddress)) {
      request->send(400, "text/plain", "The same thermometer cannot have two roles.");
      return;
    }
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
  if (request->hasParam("FMTAltitude", true)) {
    FMTData.FMTAltitude = request->getParam("FMTAltitude", true)->value().toFloat();
  }
  FMTData.maximumPressure = maximumPressure;
  FMTData.maxSetPointPressure = maxSetPointPressure;
  FMTData.ventTargetDropPercent = ventDrop;
  if (request->hasParam("dataLogIntervalSeconds", true)) {
    const int interval = request->getParam("dataLogIntervalSeconds", true)->value().toInt();
    if (!isValidDataLogIntervalSeconds(interval)) {
      request->send(400, "text/plain", "Log interval must be 30 seconds, 1, 2, 5, or 10 minutes.");
      return;
    }
    FMTData.dataLogIntervalSeconds = interval;
  }
  if (request->hasParam("cloudEditsField", true) &&
      !setCloudEditsAccepted(request->hasParam("cloudEdits", true))) {
    request->send(500, "text/plain", "Could not save Accept cloud edits.");
    return;
  }
  if (dallasConfig) memcpy(FMTData.dallasAddress, dallasAddress, sizeof(dallasAddress));
  writeFMTDataToNIV();
  updatePatmFromFMTAltitude();
  if (dallasConfig) applyDallasRoles();

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='2;url=/menu'>";
  html += "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>FMT Data Saved!</h1>";
  html += "<p>Redirecting back to menu...</p>";
  html += "</body></html>";
  
  request->send(200, "text/html", html);
}

void handleFMTDataSave(AsyncWebServerRequest *request) {
  const String settings = savePovotoSettingsBackup();
  AsyncWebServerResponse *response = request->beginResponse(
    200, "application/json; charset=utf-8", settings);
  response->addHeader("Content-Disposition", "attachment; filename=\"povoto-settings.json\"");
  request->send(response);
}

void handleFMTDataLoad(AsyncWebServerRequest *request) {
  if (!request->hasParam("settings", true)) {
    request->send(400, "text/plain; charset=utf-8", "No settings file was received.");
    return;
  }
  if (!loadPovotoSettingsBackup(request->getParam("settings", true)->value())) {
    request->send(400, "text/plain; charset=utf-8", "Invalid Povoto settings file.");
    return;
  }
  request->send(200, "text/plain; charset=utf-8", "Settings loaded and saved to NVS.");
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

// Vent parameters and flow curves (docs/vent.md). The editable fields belong to
// the one form of the page (form='calForm'), saved by the Save button at the end.
static String curveText(const VentModel::Curve &curve) {
  if (!VentModel::validCurve(curve)) return "none";
  char when[20];
  formatLocalEpochISO(curve.epoch, when, sizeof(when));
  return String("tau(P) = ") + String(curve.a[0], 3) + " + " + String(curve.a[1], 3) + " P + " + String(curve.a[2], 3) +
    " P&sup2; + " + String(curve.a[3], 4) + " P&sup3; s (100 L of air, 20 &deg;C), t0 " + String(1000.0f * curve.t0, 1) +
    " ms, " + String(curve.pMin, 2) + "-" + String(curve.pMax, 2) + " bar, " +
    (when[0] ? String("test of ") + when : String("default: venting test of 18/09, t0 not measured"));
}

static String calibrationField(const char *name, const char *label, float value, int decimals, const char *range,
                               const char *title = "") {
  return String("<div class='form-group'><label for='") + name + "' title='" + title + "'>" + label + "</label><input type='number' id='" +
    name + "' name='" + name + "' form='calForm' step='any' " + range + " required value='" + String(value, decimals) + "'></div>";
}

static String ventSettingsHtml() {
  String html = "<hr><h2>Vent</h2><p>The valve vents the fermenter to the atmosphere for a time estimated from the flow curve "
    "and the headspace; the CO2 is counted from the real opening time (docs/vent.md). The target drop per vent is on the "
    "Settings page. The opening is at least " + String(VENT_MIN_SECONDS, 1) + " s and at most the time that gives " +
    String(VENT_SAFETY_DROP_FACTOR, 0) + " times the target drop with the smallest plausible headspace (the minimum wins).</p>";
  html += calibrationField("flowLevelFactor", "Flow level factor (degassed SG, between batches only)", FMTData.flowLevelFactor, 4,
                           "min='0.5' max='1.5'");
  html += calibrationField("FMTEffectiveVentingExponent", "Effective Venting Exponent", FMTData.FMTEffectiveVentingExponent, 3,
                           "min='1' max='1.3'",
                           "Effective polytropic exponent used to correct the temporary pressure drop caused by gas cooling during "
                           "venting. Use 1.00 for isothermal venting and approximately 1.29 for adiabatic CO2 venting.");
  html += "<p><b>Flow curve:</b> " + curveText(FMTData.ventCurve) + "<br><b>Reference curve:</b> " +
    curveText(FMTData.ventReferenceCurve) + "<br>Last vents: <a href='/ventstatus'>/ventstatus</a></p>";
  // Opening times with the current headspace and the gas temperature.
  const float headspace = CountersData.headSpaceVolume > 0.0f ? CountersData.headSpaceVolume
      : FMTData.FMTVolume - BatchData.initialBeerVolume;
  const float t = gasTemperature();
  const double tK = (isfinite(t) && t > -20.0f && t < 60.0f ? t : 20.0f) + 273.15;
  html += "<p>Opening for the target drop with " + String(headspace, 1) + " L:";
  for (const float p : {0.5f, 1.0f, 1.9f}) {
    const double seconds = VentModel::ventSeconds(FMTData.ventCurve, FMTData.flowLevelFactor, headspace, p,
        p * (1.0 - FMTData.ventTargetDropPercent / 100.0), Patm, tK, FMTData.FMTEffectiveVentingExponent);
    html += " " + String(p, 1) + " bar " + (isfinite(seconds) ? String(seconds, 2) + " s" : String("n/a")) + ";";
  }
  html += " CO2/air flow " + String(VentModel::co2AirRatio(1.9, Patm), 3) + " (choked) to " +
    String(VentModel::co2AirRatio(0.5, Patm), 3) + " (0.5 bar).</p>";
  return html;
}

// Pressure sensor: points A and B (a straight line) and the sensor zero
// routine (docs/pressure-sensor-zero.md).
static String pressureSensorSectionHtml(float pageCurrentReading, bool usingAverageCurrent) {
  auto num = [](float v, int d) { return isfinite(v) ? String(v, d) : String("n/a"); };
  char buffer[420];
  snprintf(buffer, sizeof(buffer),
    "<h2>Pressure sensor</h2><div class='current-row'><div class='current-badge'>&#128268; Actual current reading%s: <b>%.2f mA</b>"
    "</div><a class='btn-refresh' href='/calibration/refresh-current'>Update</a></div>",
    usingAverageCurrent ? " (30s avg)" : "", pageCurrentReading);
  String html = buffer;
  html += "<div class='point-row'><div class='point-caption'>Point A</div><div class='form-group'><label>Pressure (bar):</label>"
          "<input type='number' value='0.00' disabled></div>";
  snprintf(buffer, sizeof(buffer),
    "<div class='form-group'><label for='pressure0Current'>Current (mA):</label><div class='input-row'>"
    "<input type='number' id='pressure0Current' name='pressure0Current' form='calForm' value='%.2f' step='0.01'>"
    "<button type='button' class='btn-use' onclick=\"document.getElementById('pressure0Current').value='%.2f'\">Use actual</button>"
    "</div></div></div>", FMTData.pressure0Current, pageCurrentReading);
  html += buffer;
  snprintf(buffer, sizeof(buffer),
    "<div class='point-row'><div class='point-caption'>Point B</div><div class='form-group'>"
    "<label for='pressure1Bar'>Pressure (bar):</label>"
    "<input type='number' id='pressure1Bar' name='pressure1Bar' form='calForm' value='%.2f' step='0.01'></div>",
    FMTData.pressure1Bar);
  html += buffer;
  snprintf(buffer, sizeof(buffer),
    "<div class='form-group'><label for='pressure1Current'>Current (mA):</label><div class='input-row'>"
    "<input type='number' id='pressure1Current' name='pressure1Current' form='calForm' value='%.2f' step='0.01'>"
    "<button type='button' class='btn-use' onclick=\"document.getElementById('pressure1Current').value='%.2f'\">Use actual</button>"
    "</div></div></div>", FMTData.pressure1Current, pageCurrentReading);
  html += buffer;

  const SensorZeroResult r = getSensorZeroResult();
  html += "<h3>Sensor zero</h3>"
    "<p>Open the fermenter to the atmosphere (ideally one opening at the top and one at the bottom), wait a few minutes "
    "and confirm. The routine takes 5 min and opens no valve: it averages the filtered sensor current and compares it with the "
    "stored zero (the pressure shown elsewhere is clipped at zero and hides a zero that is too high). Requirements: mode OFF, "
    "board on for at least 30 min (now " + String(millis() / 60000UL) + " min).</p>";
  if (sensorZeroActive()) {
    html += "<p><b>Running:</b> " + String(sensorZeroProgressMinutes(), 1) + " min; current " + num(currentReading, 3) +
      " mA. <form action='/calibration/zero/abort' method='POST'><button>Abort</button></form></p>";
  } else {
    html += "<form action='/calibration/zero/start' method='POST'><label><input type='checkbox' name='open' value='1' required> "
      "The fermenter is open to the atmosphere</label> <button>Sensor zero</button></form>";
  }
  if (r.available) {
    html += "<p>Mean current " + num(r.meanCurrent, 4) + " mA over " + num(r.minutes, 1) + " min (" + String(r.samples) +
      " samples); sd " + num(r.sdCurrent * r.barPerMilliamp * 1000.0f, 2) + " mbar; range of 10-s means " +
      num(r.blockAmplitude * r.barPerMilliamp * 1000.0f, 2) + " mbar; trend " + num(r.trend * r.barPerMilliamp * 1000.0f, 2) +
      " mbar.<br>Stored zero " + num(r.storedZero, 4) + " mA; difference <b>" + (r.difference >= 0 ? "+" : "") +
      num(r.difference, 4) + " mA = " + (r.difference >= 0 ? "+" : "") + num(r.difference * r.barPerMilliamp * 1000.0f, 2) +
      " mbar</b> (no clipping).</p>";
    if (!r.stable)
      html += "<p><b>Unstable reading: fermenter not open, gas flowing or sensor drifting.</b> Check and repeat.</p>";
    if (!r.warmedUp)
      html += "<p><b>The board was on for less than 30 min at the start:</b> repeat later to save.</p>";
    if (r.saved) html += "<p>Saved.</p>";
    if (r.canSave)
      html += "<form action='/calibration/zero/save' method='POST'><button>Save zero</button> "
        "(shifts the two calibration currents by the difference; the gain is kept)</form>";
  }
  return html;
}

// Venting test (docs/venting-test.md): start form, progress, last result, history.
static String pct(float fraction) {
  return isfinite(fraction) ? (fraction >= 0 ? "+" : "") + String(100.0f * fraction, 2) + "%" : String("n/a");
}

static String ventingTestSectionHtml() {
  auto num = [](float v, int d) { return isfinite(v) ? String(v, d) : String("n/a"); };
  String html = "<hr><h2>Venting test</h2><p>Empty fermenter with air (the configured FMT volume, " + String(FMTData.FMTVolume, 1) +
    " L), the valve outlet in its working assembly (witness bottle). Pressurize above the highest fermentation pressure + ~3% "
    "(2.0-2.05 bar for 1.9) and wait ~10 min. The test alternates openings of 2, 0.5, 8, 1, 4 and 2 s at the start pressure and at "
    "~1.9, 1.5, 1.1, 0.8, 0.5 and 0.25 bar (a long opening steps down between levels), waits for a stable pressure "
    "after each one (0.3 mbar in 30 s, 45-180 s) and measures the real opening time. About 55-65 min. Vents outside "
    "the pressure range of the saved curve are flagged. Do not touch the equipment.</p>";
  if (ventingTestActive()) {
    html += "<p><b>" + ventingTestProgressText() + "</b></p><form action='/calibration/venting/abort' method='POST'>"
      "<button>Abort</button></form><p><a href='/calibration/venting.csv?test=current'>Current test CSV</a> "
      "(the finished releases so far)</p>";
  } else {
    const uint8_t preset = defaultVentingTestLabel();
    html += "<form action='/calibration/venting/start' method='POST'><label>Label <select name='label'>";
    for (uint8_t i = 0; i < VENTING_LABEL_COUNT; ++i)
      html += String("<option value='") + i + "'" + (i == preset ? " selected" : "") + ">" + ventingTestLabelName(i) + "</option>";
    html += "</select></label> <button>Venting test</button></form><p>" + ventingTestProgressText() + "</p>";
  }
  const uint8_t count = ventingTestHistoryCount();
  VentingTestSummary s;
  if (count > 0 && getVentingTestSummary(0, s)) {
    char when[20];
    formatLocalEpochISO(s.epoch, when, sizeof(when));
    html += String("<p><b>Last test</b> (") + (when[0] ? when : "no time") + ", " + ventingTestLabelName(s.label) + ", " +
      (s.co2 ? "CO2" : "air") + ", " + num(s.volume, 1) + " L, Patm " + num(1000.0f * s.patm, 1) + " hPa " +
      atmosphericPressureSourceLabel(s.patmSource) + ", " + String(s.releases) + " releases): ";
    if (s.fitted) {
      html += "tau(P) = " + num(s.curve.a[0], 3) + " + " + num(s.curve.a[1], 3) + " P + " + num(s.curve.a[2], 3) +
        " P&sup2; + " + num(s.curve.a[3], 4) + " P&sup3; s (100 L, 20 &deg;C, " + num(s.curve.pMin, 2) + "-" +
        num(s.curve.pMax, 2) + " bar); <b>t0 " + num(1000.0f * s.curve.t0, 1) + " &plusmn; " + num(s.t0SeMs, 1) +
        " ms</b>; reduced chi-square " + (s.chiSquare > 3.0f ? "<b>" + num(s.chiSquare, 2) + " (above 3: the cubic does not "
        "follow the data)</b>" : num(s.chiSquare, 2)) + " (shape residual " + num(s.shapeRmsPercent, 2) + "% rms); drift " +
        num(s.driftPercent, 2) + " &plusmn; " +
        num(s.driftSePercent, 2) + "%.<br>Flow against the reference at 1.8 / 1.2 / 0.8 bar: " + pct(s.vsReference[0]) + " / " + pct(s.vsReference[1]) +
        " / " + pct(s.vsReference[2]) + "; against the saved curve: " + pct(s.vsSaved[0]) + " / " + pct(s.vsSaved[1]) +
        " / " + pct(s.vsSaved[2]) + ".</p>";
      if (!ventingTestActive())
        html += "<form action='/calibration/venting/save' method='POST'><input type='hidden' name='test' value='0'>"
          "<button name='target' value='curve'>Save curve and t0</button> "
          "<button name='target' value='reference'>Save as reference curve</button> (analyse the CSV first)</form>";
    } else {
      html += "fit failed.</p>";
    }
  }
  if (count > 0) {
    html += "<p><a href='/calibration/venting.csv?test=0'>Last test CSV</a> | <a href='/calibration/venting.csv?test=all'>"
      "All tests CSV</a> (" + String(count) + " in the history; <code>python tests/venting_fit.py file.csv</code> "
      "redoes the fit)</p><table style='width:100%;font-size:0.85em'><tr><th>#</th><th>Time</th><th>Label</th><th>t0 ms</th>"
      "<th>vs ref 1.8/1.2/0.8</th><th>Drift</th><th>chi2</th></tr>";
    for (uint8_t i = 0; i < count && i < 10; ++i) {
      if (!getVentingTestSummary(i, s)) continue;
      char when[20];
      formatLocalEpochISO(s.epoch, when, sizeof(when));
      html += "<tr><td><a href='/calibration/venting.csv?test=" + String(i) + "'>" + String(i) + "</a></td><td>" + when +
        "</td><td>" + ventingTestLabelName(s.label) + "</td><td>" + num(1000.0f * s.curve.t0, 1) + "</td><td>" +
        pct(s.vsReference[0]) + " " + pct(s.vsReference[1]) + " " + pct(s.vsReference[2]) + "</td><td>" +
        num(s.driftPercent, 2) + "%</td><td>" + num(s.chiSquare, 2) + "</td></tr>";
    }
    html += "</table>";
  }
  return html;
}

static String dissolvedCO2SectionHtml() {
  String html = "<hr><h2>Dissolved CO2 dynamics</h2>";
  html += String("<div class='form-group'><label for='nucleationWindow'>Nucleation window (min):</label>"
                 "<input type='number' id='nucleationWindow' name='nucleationWindow' form='calForm' value='") +
          String(FMTData.nucleationWindow) + "' step='1' min='0'></div>";
  // Information only: the firmware estimates the window after each hopping task and never changes the field.
  html += "<p style='font-size:0.9em'><b>Estimate of the window</b> (information only, never applied; one event per "
          "dry / dynamic hopping task, 3 h after it, in 10-min blocks: docs/dissolved-co2.md, section 8). BaselineRate in g/L/d; "
          "mols in mol.</p>";
  const String progress = nucleationEstimateStatusText();
  if (progress.length()) html += "<p style='font-size:0.9em'>" + progress + "</p>";
  String when, details;
  if (lastCalibrationEventOfType("nucleation-estimate", when, details)) {
    html += "<p style='font-size:0.9em'>Last (" + when + "): ";
    for (size_t i = 0; i < details.length(); ++i) {
      const char c = details[i];
      if (c == '<') html += "&lt;";
      else if (c == '>') html += "&gt;";
      else if (c == '&') html += "&amp;";
      else html += c;
    }
    html += "</p>";
  } else {
    html += "<p style='font-size:0.9em'>No estimate yet.</p>";
  }
  return html;
}

// Valve cleaning cycle (docs/valve-cleaning.md).
static String valveCleaningSectionHtml() {
  String html = "<hr><h2>Valve cleaning</h2><p>Mode OFF. Detach port <b>P</b> from the fermenter and take it to the drain; "
    "remove the muffler of port <b>R</b> and take it to the drain. Pressurized liquid (a few tenths of a bar) "
    "<b>only on port A</b>. Each phase alternates the valve energized (A&rarr;P) and de-energized (A&rarr;R), 2 s + 2 s. "
    "Phases: neutral detergent, water, 70% alcohol, air (drying); change the liquid on A before each one. No caustic soda "
    "and no percarbonate (aluminium body); keep the coil dry. The first time, check that the liquid comes out alternately "
    "through P and R.</p><p><b>" + valveCleaningStatusText() + "</b></p>";
  if (valveCleaningRunning()) {
    html += "<form action='/calibration/cleaning/abort' method='POST'><button>Abort</button></form>";
  } else if (valveCleaningNextPhase() < VALVE_CLEANING_PHASES || !valveCleaningActive()) {
    const uint8_t phase = valveCleaningActive() ? valveCleaningNextPhase() : 0;
    html += String("<form action='/calibration/cleaning/start' method='POST'><label><input type='checkbox' name='ready' "
      "value='1' required> ") + valveCleaningPhaseName(phase) + " is connected to port A</label> <label>Minutes "
      "<input type='number' name='minutes' value='4' min='0.5' max='15' step='0.5' style='width:70px'></label> "
      "<button>Valve cleaning: phase " + String(phase + 1) + "</button></form>";
    if (valveCleaningActive())
      html += "<form action='/calibration/cleaning/abort' method='POST'><button>Abort the cycle</button></form>";
  }
  return html;
}

// Fermenter volume with an expansion tank (docs/keg-volume.md).
static String fermenterVolumeSectionHtml() {
  const KegVolumeResult r = getKegVolumeResult();
  String html = "<hr><h2>Fermenter volume</h2><p>Fermenter volume determination &mdash; measures the fermenter volume (FMT volume) "
    "with an expansion tank of known volume.<br>Connect the expansion tank to port A.<br>Pressurize the fermenter to ~2.0 bar, "
    "wait 15 min and press Start.<br>The routine takes about 2&ndash;3 h and repeats by itself down to ~0.5 bar: it waits for a "
    "stable pressure, opens the valve to the tank, waits again, closes and vents the tank.<br>Result: the fermenter volume "
    "(mean of the stable steps) and its spread. If accepted, press Save as new FMT volume.</p><p><b>" + kegVolumeStatusText() +
    "</b></p>";
  if (kegVolumeActive()) {
    html += "<form action='/calibration/keg/abort' method='POST'><button>Abort</button></form>";
  } else {
    html += "<form action='/calibration/keg/start' method='POST'><label>Expansion tank volume (L) <input type='number' name='keg' "
      "id='tankVolume' step='0.001' min='0.5' style='width:90px' required value='" + String(FMTData.lastExpTankVolume, 3) +
      "'></label> <button>Start</button></form><p>Planned at the current pressure (" + String(ControlData.pressure, 2) +
      " bar) with this tank: <b>" + String(kegVolumePlannedReliefs(ControlData.pressure, FMTData.lastExpTankVolume)) +
      "</b> reliefs (computed again at the start).</p>";
  }
  if (r.total > 0) {
    html += "<p><a href='/calibration/keg.csv'>" + String(kegVolumeActive() ? "Current run CSV" : "CSV") + "</a> (every relief, "
      "with its stability marks)</p>";
  }
  if (r.available) {
    html += "<p>Expansion tank " + String(r.tankVolume, 3) + " L: fermenter <b>" + String(r.mean, 2) + " L</b>" +
      (isfinite(r.sd) ? " (sd " + String(r.sd, 2) + " L = " + String(100.0f * r.sd / r.mean, 2) + "%)" : String("")) +
      ", " + String(r.reliefs) + " of " + String(r.total) + " reliefs used; simple formula " + String(r.simpleMean, 2) +
      " L.<br>Configured FMT volume: <b>" + String(r.fmtVolume, 2) + " L</b>.</p>";
    if (r.canSave) {
      html += "<form action='/calibration/keg/save' method='POST' onsubmit=\"return confirm('Save " + String(r.mean, 2) +
        " L as the new FMT volume (now " + String(r.fmtVolume, 2) + " L)?')\"><button>Save as new FMT volume</button></form>";
    } else if (!kegVolumeActive()) {
      html += "<p>At least 2 stable reliefs are needed to save the volume.</p>";
    }
  } else if (r.total > 0) {
    html += "<p>No stable relief yet (" + String(r.total) + " done).<br>Configured FMT volume: <b>" + String(r.fmtVolume, 2) +
      " L</b>.</p>";
  }
  if (r.total > 0) {
    html += "<table style='width:100%;font-size:0.85em'><tr><th>#</th><th>P1 bar</th><th>P2 bar</th><th>T ferm &deg;C</th>"
      "<th>T tank &deg;C</th><th>V L</th><th>simple L</th><th>stable</th></tr>" + kegVolumeRowsHtml() + "</table>";
  }
  return html;
}

static void sendCalibrationMessage(AsyncWebServerRequest *request, const char *title, const char *text) {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='3;url=/calibration'>"
    "<style>body{font-family:Arial;text-align:center;margin-top:50px;}</style></head><body><h1>";
  html += title;
  html += "</h1><p>";
  html += text;
  html += "</p></body></html>";
  request->send(200, "text/html", html);
}

void handleSensorZeroStart(AsyncWebServerRequest *request) {
  if (!request->hasParam("open", true)) {
    sendCalibrationMessage(request, "Sensor zero not started", "Confirm that the fermenter is open to the atmosphere.");
    return;
  }
  char reason[100];
  if (!startSensorZero(reason, sizeof(reason))) {
    sendCalibrationMessage(request, "Sensor zero not started", reason);
    return;
  }
  request->redirect("/calibration");
}

// Always air, in the fermenter with the configured FMT volume.
void handleVentingTestStart(AsyncWebServerRequest *request) {
  const uint8_t label = request->hasParam("label", true) ? request->getParam("label", true)->value().toInt() : 0;
  char reason[100];
  if (!startVentingTest(label, false, FMTData.FMTVolume, reason, sizeof(reason))) {
    sendCalibrationMessage(request, "Venting test not started", reason);
    return;
  }
  request->redirect("/calibration");
}

void handleVentingTestAbort(AsyncWebServerRequest *request) {
  abortVentingTest();
  request->redirect("/calibration");
}

void handleVentingTestSave(AsyncWebServerRequest *request) {
  const uint8_t index = request->hasParam("test", true) ? request->getParam("test", true)->value().toInt() : 0;
  const bool reference = request->hasParam("target", true) && request->getParam("target", true)->value() == "reference";
  char reason[100];
  if (!saveVentingTestCurve(index, reference, reason, sizeof(reason))) {
    sendCalibrationMessage(request, "Curve not saved", reason);
    return;
  }
  request->redirect("/calibration");
}

void handleValveCleaningStart(AsyncWebServerRequest *request) {
  if (!request->hasParam("ready", true)) {
    sendCalibrationMessage(request, "Phase not started", "Confirm the liquid on port A.");
    return;
  }
  const float minutes = request->hasParam("minutes", true) ? request->getParam("minutes", true)->value().toFloat() : 4.0f;
  char reason[100];
  if (!startValveCleaningPhase(minutes, reason, sizeof(reason))) {
    sendCalibrationMessage(request, "Phase not started", reason);
    return;
  }
  request->redirect("/calibration");
}

void handleValveCleaningAbort(AsyncWebServerRequest *request) {
  abortValveCleaning();
  request->redirect("/calibration");
}

void handleKegVolumeStart(AsyncWebServerRequest *request) {
  const float tank = request->hasParam("keg", true) ? request->getParam("keg", true)->value().toFloat() : NAN;
  char reason[100];
  if (!startKegVolume(tank, reason, sizeof(reason))) {
    sendCalibrationMessage(request, "Fermenter volume not started", reason);
    return;
  }
  request->redirect("/calibration");
}

void handleKegVolumeAbort(AsyncWebServerRequest *request) {
  abortKegVolume();
  request->redirect("/calibration");
}

void handleKegVolumeSave(AsyncWebServerRequest *request) {
  char reason[100];
  if (!saveKegVolumeAsFmtVolume(reason, sizeof(reason))) {
    sendCalibrationMessage(request, "FMT volume not saved", reason);
    return;
  }
  request->redirect("/calibration");
}

void handleSensorZeroAbort(AsyncWebServerRequest *request) {
  abortSensorZero();
  request->redirect("/calibration");
}

void handleSensorZeroSave(AsyncWebServerRequest *request) {
  char reason[100];
  if (!saveSensorZero(reason, sizeof(reason))) {
    sendCalibrationMessage(request, "Zero not saved", reason);
    return;
  }
  request->redirect("/calibration");
}

void handleCalibrationDataPage(AsyncWebServerRequest *request) {
  float pageCurrentReading = currentReading;
  bool usingAverageCurrent = false;
  if (request->hasParam("avgCurrent")) {
    pageCurrentReading = request->getParam("avgCurrent")->value().toFloat();
    usingAverageCurrent = true;
  }

  // Sections in order, with a dividing line; the editable fields of all of them
  // belong to the empty form 'calForm', saved by the Save button at the end.
  String page = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'><title>Calibration</title><style>"
    "body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }"
    ".container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }"
    "h1 { color: #333; text-align: center; }"
    "hr { border: 0; border-top: 2px solid #ddd; margin: 26px 0 18px; }"
    ".form-group { margin-bottom: 15px; }"
    "label { display: block; margin-bottom: 5px; color: #666; font-weight: bold; }"
    ".input-row { display: flex; gap: 8px; align-items: center; }"
    ".point-row { display: grid; grid-template-columns: 80px 1fr 1.35fr; gap: 12px; align-items: end; }"
    ".point-row > .form-group { margin-bottom: 15px; } .point-caption { font-weight: bold; padding-bottom: 24px; }"
    ".input-row input { flex: 1; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }"
    "input[type='number']:not(.input-row input) { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }"
    "input[type='checkbox'] { width: auto; } label > input[type='checkbox'] { margin-right: 6px; }"
    "button { padding: 8px 14px; margin: 2px; border: none; border-radius: 4px; cursor: pointer; font-size: 14px; }"
    "button[type='submit'] { background: #4CAF50; color: white; font-size: 16px; padding: 10px 20px; }"
    ".btn-secondary { background: #999; color: white; font-size: 16px; padding: 10px 20px; }"
    ".btn-use { background: #2196F3; color: white; white-space: nowrap; }"
    ".btn-refresh { background: #ff9800; color: white; white-space: nowrap; text-decoration: none; display: inline-block; padding: 8px 14px; border-radius: 4px; font-size: 14px; }"
    ".current-badge { background: #e8f5e9; border: 1px solid #a5d6a7; border-radius: 6px; padding: 8px 14px; font-size: 1.1em; color: #2e7d32; }"
    ".current-row { display: flex; gap: 10px; align-items: center; margin-bottom: 18px; } .current-row .current-badge { flex: 1; }"
    "</style></head><body><div class='container'><h1>Calibration</h1>"
    "<p>Requirements of the routines: mode OFF, no task. Do not press Save before analysing the data "
    "(docs/manual-calibracao-manutencao.md). Vent diagnostics: <a href='/ventstatus'>/ventstatus</a>.</p>"
    "<form id='calForm' action='/calibration/update' method='POST'></form>";
  page += pressureSensorSectionHtml(pageCurrentReading, usingAverageCurrent);
  page += fermenterVolumeSectionHtml();
  page += ventingTestSectionHtml();
  page += ventSettingsHtml();
  page += dissolvedCO2SectionHtml();
  page += valveCleaningSectionHtml();
  page += "<hr><div><button type='submit' form='calForm'>Save</button> "
          "<button type='button' class='btn-secondary' onclick='window.location=\"/menu\"'>Cancel</button></div>";
  const String events = calibrationEventsHtmlRows(10);
  if (events.length())
    page += "<hr><h3>Calibration events</h3><p>Newest first; <a href='/calibration/events.txt'>all</a></p>"
      "<table style='width:100%;font-size:0.9em'><tr><th>Time</th><th>Event</th><th>Details</th></tr>" + events + "</table>";
  page += "</div></body></html>";
  if (sensorZeroActive() || ventingTestActive() || valveCleaningRunning() || kegVolumeActive())
    page.replace("<head>", "<head><meta http-equiv='refresh' content='10'>");
  request->send(200, "text/html", page);
}

void handleCalibrationDataUpdate(AsyncWebServerRequest *request) {
  auto value = [request](const char *name, float current) {
    return request->hasParam(name, true) ? request->getParam(name, true)->value().toFloat() : current;
  };
  const float exponent = value("FMTEffectiveVentingExponent", FMTData.FMTEffectiveVentingExponent);
  const float level = value("flowLevelFactor", FMTData.flowLevelFactor);
  const float p1 = value("pressure1Bar", FMTData.pressure1Bar);
  const float c0 = value("pressure0Current", FMTData.pressure0Current);
  const float c1 = value("pressure1Current", FMTData.pressure1Current);
  if (!isValidFlowLevelFactor(level)) {
    request->send(400, "text/plain", "Invalid flow level factor: it must be from 0.5 to 1.5.");
    return;
  }
  if (!(exponent >= 1.0f && exponent <= 1.30f)) {
    request->send(400, "text/plain", "Effective venting exponent must be between 1.00 and 1.30.");
    return;
  }
  if (!(p1 > 0.5f && p1 < FMTData.maximumPressure) || !isfinite(c0) || !isfinite(c1) || !(c1 > c0)) {
    request->send(400, "text/plain", "Invalid calibration values: point B must be above 0.5 bar and below the maximum security "
                  "pressure, and its current above that of point A.");
    return;
  }
  FMTData.pressure0Current = c0;
  FMTData.pressure1Bar = p1;
  FMTData.pressure1Current = c1;
  FMTData.flowLevelFactor = level;
  FMTData.FMTEffectiveVentingExponent = exponent;
  if (request->hasParam("nucleationWindow", true)) {
    FMTData.nucleationWindow = request->getParam("nucleationWindow", true)->value().toInt();
  }
  writeFMTDataToNIV();

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='2;url=/menu'>";
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
               "<label for='initialBeerVolume'>Initial beer volume (L):</label>", remaining);
  sprintf(buffer, "<input type='number' id='initialBeerVolume' name='initialBeerVolume' value='%.2f' step='0.01' min='0' placeholder='0.00'>", BatchData.initialBeerVolume);
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
                 "<button type='button' class='btn-secondary' onclick='window.location=\"/menu\"'>Cancel</button>"
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
  bool fermentablesAdded = false;
  if (request->hasParam("addedPlato", true)) {
    const float addedPlato = request->getParam("addedPlato", true)->value().toFloat();
    fermentablesAdded = isfinite(addedPlato) && addedPlato > BatchData.addedPlato;
    BatchData.addedPlato = addedPlato;
  }
  if (request->hasParam("initialBeerVolume", true)) {
    const float volume = request->getParam("initialBeerVolume", true)->value().toFloat();
    if (isfinite(volume) && volume >= 0.0f) {
      BatchData.initialBeerVolume = volume;
    }
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
  // A refermentation is expected: the half-life returns to equilibrium sooner.
  if (fermentablesAdded) notifyFermentablesAdded();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='2;url=/menu'>";
  html += "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>Batch Data Saved!</h1>";
  html += "<p>Redirecting back to menu...</p>";
  html += "</body></html>";
  
  request->send(200, "text/html", html);
}

// ========== COUNTERS DATA HANDLERS ==========

// Select with the four stability states (TEMP_STATE_* values).
static void appendStabilityControls(char *html, size_t size, const char *prefix, const char *title,
                                    uint8_t state, uint32_t stableSince) {
  static const char *const labels[] = {"STABLE", "CHANGING_DIRECT", "CHANGING_SLOW", "UNSTABLE"};
  char buffer[400];
  snprintf(buffer, sizeof(buffer),
           "<div class='form-group'><label for='%sState'>%s stability:</label><select id='%sState' name='%sState'>",
           prefix, title, prefix, prefix);
  strncat(html, buffer, size - strlen(html) - 1);
  for (uint8_t i = 0; i < 4; ++i) {
    snprintf(buffer, sizeof(buffer), "<option value='%u'%s>%s</option>", i, i == state ? " selected" : "", labels[i]);
    strncat(html, buffer, size - strlen(html) - 1);
  }
  const unsigned long now = NTPEpoch();
  char current[24] = "";
  if (state == TEMP_STATE_STABLE && stableSince != 0 && now >= stableSince)
    snprintf(current, sizeof(current), "%.2f", (now - stableSince) / 3600.0f);
  snprintf(buffer, sizeof(buffer),
           "</select><input type='number' name='%sStableHours' value='' step='any' min='0' placeholder='stable for (h): %s'>"
           "<small>STABLE: stable for the hours given (empty = keep, or now if it was not stable). "
           "Other states restart the stability time.</small></div>",
           prefix, current[0] ? current : "-");
  strncat(html, buffer, size - strlen(html) - 1);
}

// Applies a manual stability state. Returns false when STABLE needs NTP and there is none.
static bool applyStabilityControls(AsyncWebServerRequest *request, const char *prefix,
                                   uint8_t &state, uint32_t &stableSince, bool &changed) {
  char name[24];
  snprintf(name, sizeof(name), "%sState", prefix);
  changed = false;
  if (!request->hasParam(name, true)) return true;
  const long requested = request->getParam(name, true)->value().toInt();
  if (requested < 0 || requested > TEMP_STATE_UNSTABLE) return true;
  snprintf(name, sizeof(name), "%sStableHours", prefix);
  String hoursText = request->hasParam(name, true) ? request->getParam(name, true)->value() : String();
  hoursText.trim();
  if ((uint8_t)requested == state && hoursText.length() == 0) return true; // unchanged
  if (requested != TEMP_STATE_STABLE) {
    state = (uint8_t)requested;
    stableSince = 0;
    changed = true;
    return true;
  }
  const unsigned long now = NTPEpoch();
  if (now == 0) return false;
  float hours = hoursText.length() ? hoursText.toFloat() : 0.0f;
  if (!isfinite(hours) || hours < 0.0f) hours = 0.0f;
  const uint32_t shift = (uint32_t)(hours * 3600.0f);
  state = TEMP_STATE_STABLE;
  stableSince = now > shift ? now - shift : 1;
  changed = true;
  return true;
}

void handleCountersDataPage(AsyncWebServerRequest *request) {
  const size_t BUFFER_SIZE = 8000;
  char* html = (char*)malloc(BUFFER_SIZE);
  if (!html) {
    request->send(500, "text/plain", "Out of memory");
    return;
  }

  size_t remaining;
  char buffer[220];
  const float totalCO2MolsProduced = getTotalCO2Mols();

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
  strncat(html, "input, select { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; } select { margin-bottom: 6px; }", remaining);
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
           "<div class='summary'>Derived beer volume: %.2f L<br>Current dissolved CO2 estimate: %.3f mol<br>Total CO2 produced: %.3f mol</div>",
           beerVolume,
           CountersData.CO2InSolution,
           totalCO2MolsProduced);
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
  strncat(html, "<div class='form-group'><label for='CO2MolsProducedPerLiter'>CO2 Mols Produced Per Liter (mol/L):</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='CO2MolsProducedPerLiter' name='CO2MolsProducedPerLiter' value='%.6f' step='0.000001' min='0'>", CountersData.CO2MolsProducedPerLiter);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<small>Manual changes become the new accumulated value.</small></div>", remaining);

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
  strncat(html, "<div class='readonly-group'><h2>States</h2>", remaining);
  {
    const uint8_t co2State = getCO2DissolvedState();
    remaining = BUFFER_SIZE - strlen(html) - 1;
    strncat(html, "<div class='form-group'><label for='co2DissolvedState'>Dissolved CO2 state:</label><select id='co2DissolvedState' name='co2DissolvedState'>", remaining);
    static const uint8_t order[] = {2, 1, 0, 3}; // initial, equilibrium, half-life, armed
    for (uint8_t i = 0; i < 4; ++i) {
      snprintf(buffer, sizeof(buffer), "<option value='%u'%s>%s</option>", order[i],
               order[i] == co2State ? " selected" : "", getCO2DissolvedStateLabel(order[i]));
      remaining = BUFFER_SIZE - strlen(html) - 1;
      strncat(html, buffer, remaining);
    }
    remaining = BUFFER_SIZE - strlen(html) - 1;
    strncat(html, "</select><small>immediate = equilibrium at the relief threshold. A manual change is kept until the automatic "
                  "criteria change it again (docs/dissolved-co2.md).</small></div>", remaining);
  }
  appendStabilityControls(html, BUFFER_SIZE, "temp", "Temperature", CountersData.tempState, CountersData.tempStableSince);
  appendStabilityControls(html, BUFFER_SIZE, "press", "Pressure", CountersData.pressState, CountersData.pressStableSince);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<button type='submit'>Save</button> <button type='button' class='btn-secondary' onclick='window.location=\"/menu\"'>Cancel</button></form></div></body></html>", remaining);

  request->send(200, "text/html", html);
  free(html);
}

// The form shows rounded values; a field counts as edited only when the
// posted value differs from the shown one by at least half its last digit.
// Otherwise saving the page would replace the stored values with rounded ones
// and restart the CO2 accounting.
static bool postedCounterChanged(AsyncWebServerRequest *request, const char *name,
                                 double current, double resolution, double &value) {
  if (!request->hasParam(name, true)) return false;
  value = request->getParam(name, true)->value().toDouble();
  return isfinite(value) && fabs(value - current) >= resolution / 2.0;
}

void handleCountersDataUpdate(AsyncWebServerRequest *request) {
  bool co2StateChanged = false;
  bool co2ProducedPerLiterChanged = false;
  double value = 0.0;
  if (postedCounterChanged(request, "totalReliefCount", CountersData.totalReliefCount, 1.0, value) && value >= 0.0) {
    CountersData.totalReliefCount = (uint32_t)lround(value);
    co2StateChanged = true;
  }
  if (postedCounterChanged(request, "totalMolsEjected", CountersData.totalMolsEjected, 0.001, value)) {
    CountersData.totalMolsEjected = value;
    co2StateChanged = true;
  }
  if (postedCounterChanged(request, "CO2InSolution", CountersData.CO2InSolution, 0.001, value)) {
    CountersData.CO2InSolution = value;
    co2StateChanged = true;
  }
  if (postedCounterChanged(request, "CO2MolsProducedPerLiter", CountersData.CO2MolsProducedPerLiter, 0.000001, value) &&
      value >= 0.0) {
    CountersData.CO2MolsProducedPerLiter = value;
    co2ProducedPerLiterChanged = true;
  }
  if (postedCounterChanged(request, "headSpaceVolume", CountersData.headSpaceVolume, 0.01, value)) {
    CountersData.headSpaceVolume = (float)value;
    co2StateChanged = true;
  }
  if (postedCounterChanged(request, "correctionPlato", CountersData.correctionPlato, 0.01, value)) {
    CountersData.correctionPlato = (float)value;
  }
  if (request->hasParam("totalChillTime", true)) {
    CountersData.totalChillTime = request->getParam("totalChillTime", true)->value().toInt();
  }
  if (request->hasParam("totalHeatTime", true)) {
    CountersData.totalHeatTime = request->getParam("totalHeatTime", true)->value().toInt();
  }

  // Stability states: persisted by their own writers. CountersData is
  // packed, so the fields go through local copies.
  String notes;
  bool stabilityChanged = false;
  uint8_t state = CountersData.tempState;
  uint32_t since = CountersData.tempStableSince;
  if (!applyStabilityControls(request, "temp", state, since, stabilityChanged)) {
    notes += "Temperature not set to STABLE: no NTP time.<br>";
  } else if (stabilityChanged) {
    CountersData.tempState = state;
    CountersData.tempStableSince = since;
    writeTempStabilityToNIV();
  }
  state = CountersData.pressState;
  since = CountersData.pressStableSince;
  if (!applyStabilityControls(request, "press", state, since, stabilityChanged)) {
    notes += "Pressure not set to STABLE: no NTP time.<br>";
  } else if (stabilityChanged) {
    CountersData.pressState = state;
    CountersData.pressStableSince = since;
    writePressureStabilityToNIV();
  }

  // Dissolved-CO2 state: persisted by setCO2DissolvedState(); the restore
  // requested below reads CountersData.co2DissolvedMode, so it keeps it.
  if (request->hasParam("co2DissolvedState", true)) {
    const long state = request->getParam("co2DissolvedState", true)->value().toInt();
    if (state >= 0 && state <= 3 && (uint8_t)state != getCO2DissolvedState())
      setCO2DissolvedStateManually((uint8_t)state);
  }

  writeCountersDataToNIV();
  if (co2ProducedPerLiterChanged) {
    resetCO2MolsProducedPerLiterTracking();
  }
  if (co2StateChanged) {
    requestDerivedStateRestoreFromCounters();
  }

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='2;url=/menu'>";
  html += "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>Counters Data Saved!</h1>";
  if (notes.length()) html += "<p>" + notes + "</p>";
  html += "<p>Redirecting back to counters page...</p>";
  html += "</body></html>";

  request->send(200, "text/html", html);
}

// ========== TRIGGERED ACTIONS (AUTOMATIC SET POINTS) ==========

static void appendHtml(char *html, size_t size, const char *format, ...) {
  const size_t used = strlen(html);
  if (used + 1 >= size) return;
  va_list args;
  va_start(args, format);
  vsnprintf(html + used, size - used, format, args);
  va_end(args);
}

// %g keeps the value as typed, so saving the page again does not round it.
static void formatOptional(float value, char *out, size_t outSize) {
  if (isnan(value)) out[0] = '\0';
  else snprintf(out, outSize, "%g", value);
}

// decimals < 0: value as typed (%g) and any step; otherwise that fixed precision.
static void appendRuleInput(char *html, size_t size, int rule, const char *field,
                            const char *label, float value, bool readOnly, int decimals = -1,
                            bool disabled = false) {
  char text[16];
  if (decimals < 0 || isnan(value)) formatOptional(value, text, sizeof(text));
  else snprintf(text, sizeof(text), "%.*f", decimals, value);
  char step[12] = "any";
  if (decimals >= 0) snprintf(step, sizeof(step), "%g", powf(10.0f, -decimals));
  appendHtml(html, size,
    "<div class='form-group'><label for='r%d_%s'>%s</label>"
    "<input type='number' step='%s' id='r%d_%s' name='r%d_%s' value='%s'%s%s></div>",
    rule, field, label, step, rule, field, rule, field, text,
    readOnly ? " readonly" : "", disabled ? " disabled" : "");
}

// Disabled trigger inputs need enabled mirrors so their saved values survive a
// manual-only form submission and can be restored if the checkbox is cleared.
static void appendRuleTriggerMirror(char *html, size_t size, int rule,
                                    const char *field, float value, bool manualOnly, int decimals = -1) {
  char text[16];
  if (decimals < 0 || isnan(value)) formatOptional(value, text, sizeof(text));
  else snprintf(text, sizeof(text), "%.*f", decimals, value);
  appendHtml(html, size,
    "<input type='hidden' id='r%d_%s_copy' name='r%d_%s' value='%s'%s>",
    rule, field, rule, field, text, manualOnly ? "" : " disabled");
}

// For HTML text, HTML attributes and XML attributes.
static String escapeMarkup(const char *text) {
  String out;
  for (const char *c = text; *c; c++) {
    switch (*c) {
      case '&':  out += "&amp;"; break;
      case '<':  out += "&lt;"; break;
      case '>':  out += "&gt;"; break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default:   out += *c;
    }
  }
  return out;
}

static void appendAutoSetpointSection(char *html, size_t size) {
  AutoSetpointRule_t rules[AUTO_SETPOINT_RULE_COUNT];
  AutoSetpointStatus_t status;
  getAutoSetpointRules(rules);
  getAutoSetpointStatus(status);

  appendHtml(html, size,
    "<h2>Triggered actions</h2>"
    "<p class='hint'>Evaluated only in Fermenting mode. Each rule fires once, when all filled "
    "triggers are met. Manual-only rules run only through Trigger now. Empty fields are ignored.</p>"
    // autocomplete off: a reload must show the stored values, not the typed ones.
    "<form id='rulesForm' action='/setpoint/auto/update' method='POST' autocomplete='off' onsubmit='return checkRules(event)'>");

  for (int i = 0; i < AUTO_SETPOINT_RULE_COUNT; i++) {
    const AutoSetpointRule_t &rule = rules[i];
    const bool triggered = status.triggeredAt[i] != 0;
    const bool previousHasTrigger = i == 0 || rules[i - 1].manualOnly ||
                                    autoSetpointRuleHasTrigger(rules[i - 1]);
    const bool ownHasContent = rule.name[0] || rule.manualOnly || rule.requiresPrevious ||
                               !autoSetpointRuleIsEmpty(rule) || triggered;
    const bool hidden = i > 0 && !previousHasTrigger && !ownHasContent;
    char triggeredText[24] = "not yet triggered";
    if (triggered) {
      formatLocalEpochISO(status.triggeredAt[i], triggeredText, sizeof(triggeredText));
      char *t = strchr(triggeredText, 'T');
      if (t) *t = ' ';
    }
    const String name = escapeMarkup(rule.name);
    appendHtml(html, size,
      "<fieldset class='rule' id='r%d_rule' data-triggered='%d'%s>"
      "<legend>Rule %d<span id='r%d_title'>%s%s</span></legend>"
      "<div class='rule-status'>Triggered at: <b>%s</b>",
      i, triggered ? 1 : 0, hidden ? " hidden" : "",
      i + 1, i, name.length() ? " - " : "", name.c_str(), triggeredText);
    if (triggered)
      appendHtml(html, size,
        " <button type='submit' class='btn-reset' data-rule-action='1' formaction='/setpoint/auto/reset?rule=%d' formnovalidate>Reset</button>",
        i + 1);
    else if (i == 0 || !rule.requiresPrevious || status.triggeredAt[i - 1] != 0)
      appendHtml(html, size,
        " <button type='button' class='btn-trigger' onclick='triggerRule(%d)'>Trigger now</button>",
        i + 1);
    appendHtml(html, size,
      "</div><div class='form-group'><label for='r%d_name'>Name:</label>"
      "<input type='text' id='r%d_name' name='r%d_name' value='%s' maxlength='%u'"
      " placeholder='What this rule is meant for'%s></div>",
      i, i, i, name.c_str(), (unsigned)(AUTO_SETPOINT_NAME_SIZE - 1), triggered ? " readonly" : "");
    appendHtml(html, size,
      "<label class='req'><input type='checkbox' id='r%d_manual' name='r%d_manual' value='1'%s%s> Manual only</label>",
      i, i, rule.manualOnly ? " checked" : "", triggered ? " disabled" : "");
    appendHtml(html, size, "<div class='rule-title'>Triggers</div><div class='rule-grid'>");
    appendRuleInput(html, size, i, "sh", "Temp. stable for (h):", rule.stableHours, triggered, -1, rule.manualOnly);
    appendRuleInput(html, size, i, "psh", "Press. stable for (h):", rule.pressureStableHours, triggered, -1, rule.manualOnly);
    appendRuleInput(html, size, i, "sg", "SG &lt; x:", rule.sgBelow, triggered, 3, rule.manualOnly);
    appendRuleInput(html, size, i, "co2", "gCO2/L/d &lt; x:", rule.co2RateBelow, triggered, -1, rule.manualOnly);
    appendHtml(html, size, "</div>");
    appendRuleTriggerMirror(html, size, i, "sh", rule.stableHours, rule.manualOnly);
    appendRuleTriggerMirror(html, size, i, "psh", rule.pressureStableHours, rule.manualOnly);
    appendRuleTriggerMirror(html, size, i, "sg", rule.sgBelow, rule.manualOnly, 3);
    appendRuleTriggerMirror(html, size, i, "co2", rule.co2RateBelow, rule.manualOnly);
    if (i > 0)
      appendHtml(html, size,
        "<label class='req'><input type='checkbox' id='r%d_req' name='r%d_req' value='1'%s%s>"
        " Requires rule %d</label>",
        i, i, rule.requiresPrevious ? " checked" : "", triggered ? " disabled" : "", i);
    appendHtml(html, size, "<div class='rule-title'>New set points</div><div class='rule-grid'>");
    appendRuleInput(html, size, i, "t", "Temperature  (&deg;C):", rule.temperature, triggered);
    appendRuleInput(html, size, i, "ts", "Temperature slow (&deg;C):", rule.temperatureSlow, triggered);
    appendRuleInput(html, size, i, "p", "Pressure (bar):", rule.pressure, triggered);
    appendRuleInput(html, size, i, "ps", "Pressure slow (bar):", rule.pressureSlow, triggered);
    appendHtml(html, size, "</div></fieldset>");
  }

  appendHtml(html, size,
    "<button type='submit'>Save rules</button>"
    "</form>");

  appendHtml(html, size,
    "<div class='rule-title'>Export / import</div>"
    "<p class='hint'>The XML holds names, manual-only flags, triggers and actions. Exporting saves the rules first. "
    "Importing replaces all rules and clears their trigger times.</p>"
    "<button type='button' class='btn-secondary' onclick='saveAndExport()'>Save &amp; export XML</button> "
    "<input type='file' id='xmlFile' accept='.xml,application/xml,text/xml' style='width:auto'> "
    "<button type='button' class='btn-secondary' onclick='importRules()'>Import XML</button>"
    "<div id='importMsg' class='import-msg'></div>");

  appendHtml(html, size,
    "<script>"
    "function confirmTrigger(i){return confirm('Trigger the SAVED definition of rule '+i+"
    "' now? Unsaved edits on this page will not be applied.');}"
    "function triggerRule(i){"
    "if(!confirmTrigger(i))return;"
    "fetch('/setpoint/auto/trigger?rule='+i,{method:'POST'})"
    ".then(res=>res.text().then(m=>{if(!res.ok)throw m;window.location='/setpoint';}))"
    ".catch(e=>alert(typeof e==='string'?e:'Trigger failed: '+e));}"
    "function syncManual(i){"
    "const manual=document.getElementById('r'+i+'_manual').checked;"
    "for(const f of ['sh','psh','sg','co2']){"
    "const input=document.getElementById('r'+i+'_'+f),copy=document.getElementById('r'+i+'_'+f+'_copy');"
    "copy.value=input.value;input.disabled=manual;copy.disabled=!manual;}}"
    "function ruleHasTrigger(i){"
    "return document.getElementById('r'+i+'_manual').checked||"
    "['sh','psh','sg','co2'].some(f=>document.getElementById('r'+i+'_'+f).value.trim()!=='');}"
    "function ruleHasContent(i){"
    "const rule=document.getElementById('r'+i+'_rule');"
    "return rule.dataset.triggered==='1'||document.getElementById('r'+i+'_name').value.trim()!==''||"
    "document.getElementById('r'+i+'_manual').checked||"
    "(i>0&&document.getElementById('r'+i+'_req').checked)||"
    "['sh','psh','sg','co2','t','ts','p','ps'].some(f=>"
    "document.getElementById('r'+i+'_'+f).value.trim()!=='');}"
    "function updateRuleVisibility(){"
    "const count=document.querySelectorAll('fieldset.rule').length;"
    "for(let i=1;i<count;i++)document.getElementById('r'+i+'_rule').hidden="
    "!ruleHasTrigger(i-1)&&!ruleHasContent(i);}"
    "function checkRules(e){"
    "if(e&&e.submitter&&e.submitter.dataset.ruleAction)return true;"
    "for(let i=0;i<%d;i++){"
    "const v=f=>document.getElementById('r'+i+'_'+f).value.trim()!=='';"
    "const trig=['sh','psh','sg','co2'].some(v);"
    "const act=['p','ps','t','ts'].some(v);"
    "if(act&&!document.getElementById('r'+i+'_manual').checked&&!trig){"
    "alert('Rule '+(i+1)+': at least one trigger is required.');return false;}"
    "}return true;}"
    "const XT=['stableHours','pressureStableHours','sgBelow','co2RateBelow'],"
    "XA=['pressure','pressureSlow','temperature','temperatureSlow'],"
    "XK=['sh','psh','sg','co2','p','ps','t','ts'];"
    "function importFail(m){const e=document.getElementById('importMsg');e.textContent=m;e.style.display='block';}"
    // Saves with the same checks as Save rules; downloads only if the save succeeded.
    "function saveAndExport(){"
    "if(!checkRules())return;"
    "const b=new URLSearchParams(new FormData(document.getElementById('rulesForm')));"
    "fetch('/setpoint/auto/update?noredirect=1',{method:'POST',body:b})"
    ".then(res=>res.text().then(m=>{if(!res.ok)throw m;"
    "document.getElementById('importMsg').style.display='none';"
    "window.location='/setpoint/auto/export';}))"
    ".catch(e=>importFail(typeof e==='string'?e:'Save failed: '+e));}"
    "function importRules(){"
    "const f=document.getElementById('xmlFile').files[0];"
    "if(!f){importFail('Choose an XML file first.');return;}"
    "f.text().then(t=>{"
    "const d=new DOMParser().parseFromString(t,'application/xml');"
    "if(d.getElementsByTagName('parsererror').length)throw 'The file is not well-formed XML.';"
    "const r=d.documentElement;"
    "if(r.nodeName!=='povotoAutoSetpoints')throw 'Root element must be povotoAutoSetpoints, found '+r.nodeName+'.';"
    "if(r.getAttribute('version')!=='2')throw 'Unsupported version '+r.getAttribute('version')+' (expected 2).';"
    "const b=new URLSearchParams(),seen={};"
    "for(const e of r.children){"
    "if(e.nodeName!=='rule')throw 'Unexpected element '+e.nodeName+'.';"
    "const i=Number(e.getAttribute('index'));"
    "if(!Number.isInteger(i)||i<1||i>%d)throw 'Rule index must be from 1 to %d.';"
    "if(seen[i])throw 'Rule '+i+' appears more than once.';"
    "seen[i]=1;"
    "b.append('r'+(i-1)+'_name',e.getAttribute('name')||'');"
    "const manual=(e.getAttribute('manualOnly')||'false').trim();"
    "if(manual!=='true'&&manual!=='false')throw 'Rule '+i+': manualOnly must be true or false.';"
    "if(manual==='true')b.append('r'+(i-1)+'_manual','1');"
    "const tr=e.getElementsByTagName('trigger')[0],ac=e.getElementsByTagName('action')[0];"
    "const rq=((tr&&tr.getAttribute('requiresPrevious'))||'').trim();"
    "if(rq!==''&&rq!=='true'&&rq!=='false')throw 'Rule '+i+': requiresPrevious must be true or false.';"
    "if(rq==='true'&&i>1)b.append('r'+(i-1)+'_req','1');"
    "const names=XT.concat(XA);"
    "names.forEach((n,k)=>{const el=k<4?tr:ac;const v=((el&&el.getAttribute(n))||'').trim();"
    "if(v!==''&&!isFinite(Number(v)))throw 'Rule '+i+': '+n+' is not a number: '+v;"
    "b.append('r'+(i-1)+'_'+XK[k],v);});"
    "}"
    "if(!confirm('Replace all rules and clear their trigger times?'))return null;"
    "return fetch('/setpoint/auto/import',{method:'POST',body:b});"
    // A new navigation (not reload) so the browser does not restore typed values.
    "}).then(res=>{if(!res)return;return res.text().then(m=>{if(res.ok)window.location.href='/setpoint';else importFail(m);});})"
    ".catch(e=>importFail(typeof e==='string'?e:'Import failed: '+e));}"
    // Keeps the name next to 'Rule N' in step with the name field.
    "for(let i=0;i<%d;i++){"
    "const n=document.getElementById('r'+i+'_name'),t=document.getElementById('r'+i+'_title');"
    "n.addEventListener('input',()=>{const v=n.value.trim();t.textContent=v?' - '+v:'';});"
    "const manual=document.getElementById('r'+i+'_manual');"
    "manual.addEventListener('change',()=>syncManual(i));syncManual(i);}"
    "const rulesForm=document.getElementById('rulesForm');"
    "rulesForm.addEventListener('input',updateRuleVisibility);"
    "rulesForm.addEventListener('change',updateRuleVisibility);"
    "updateRuleVisibility();"
    "</script>",
    AUTO_SETPOINT_RULE_COUNT, AUTO_SETPOINT_RULE_COUNT, AUTO_SETPOINT_RULE_COUNT,
    AUTO_SETPOINT_RULE_COUNT);
}

// Empty means not used. Returns false for text that is not a number.
static bool parseOptionalFloat(AsyncWebServerRequest *request, const String &name, float &value) {
  value = NAN;
  if (!request->hasParam(name, true)) return true;
  String text = request->getParam(name, true)->value();
  text.trim();
  if (text.length() == 0) return true;
  char *end = nullptr;
  const float parsed = strtof(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0' || !isfinite(parsed)) return false;
  value = parsed;
  return true;
}

// Rules whose skip flag is set keep the value already in rules[].
static String parseAutoSetpointRules(AsyncWebServerRequest *request, AutoSetpointRule_t *rules,
                                     const bool *skip) {
  static const char *fields[] = {"sh", "psh", "sg", "co2", "p", "ps", "t", "ts"};
  for (int i = 0; i < AUTO_SETPOINT_RULE_COUNT; i++) {
    if (skip && skip[i]) continue;
    float values[8];
    for (int f = 0; f < 8; f++) {
      if (!parseOptionalFloat(request, "r" + String(i) + "_" + fields[f], values[f]))
        return "Rule " + String(i + 1) + ": invalid number.";
    }
    // SG is kept with 3 decimals, as shown on the page.
    if (!isnan(values[2])) values[2] = roundf(values[2] * 1000.0f) / 1000.0f;
    AutoSetpointRule_t rule = {"", values[0], values[1], values[2], values[3],
                               values[4], values[5], values[6], values[7], 0, 0};
    // An unchecked box is not posted. Rule 1 has no previous rule.
    rule.requiresPrevious = (i > 0 && request->hasParam("r" + String(i) + "_req", true)) ? 1 : 0;
    rule.manualOnly = request->hasParam("r" + String(i) + "_manual", true) ? 1 : 0;
    const String nameParam = "r" + String(i) + "_name";
    if (request->hasParam(nameParam, true)) {
      String name = request->getParam(nameParam, true)->value();
      name.trim();
      setAutoSetpointRuleName(rule, name);
    }
    const String error = validateAutoSetpointRule(rule, true);
    if (error.length()) return "Rule " + String(i + 1) + ": " + error;
    rules[i] = rule;
  }
  return "";
}

void handleAutoSetpointUpdate(AsyncWebServerRequest *request) {
  AutoSetpointRule_t rules[AUTO_SETPOINT_RULE_COUNT];
  AutoSetpointStatus_t status;
  getAutoSetpointRules(rules);
  getAutoSetpointStatus(status);

  bool triggered[AUTO_SETPOINT_RULE_COUNT];
  for (int i = 0; i < AUTO_SETPOINT_RULE_COUNT; i++)
    triggered[i] = status.triggeredAt[i] != 0; // Read-only until reset.
  const String error = parseAutoSetpointRules(request, rules, triggered);
  if (error.length()) {
    request->send(400, "text/plain", error);
    return;
  }

  if (!saveAutoSetpointRules(rules)) {
    request->send(500, "text/plain", "Triggered actions could not be saved to NVS.");
    return;
  }
  // Save & export posts with noredirect and downloads the XML itself.
  if (request->hasParam("noredirect")) {
    request->send(200, "text/plain", "saved");
    return;
  }
  request->redirect("/menu");
}

static void appendXmlAttribute(String &xml, const char *name, float value, int decimals = -1) {
  char text[16] = "";
  if (!isnan(value)) {
    if (decimals < 0) snprintf(text, sizeof(text), "%g", value);
    else snprintf(text, sizeof(text), "%.*f", decimals, value);
  }
  xml += " ";
  xml += name;
  xml += "=\"";
  xml += text;
  xml += "\"";
}

// Definitions only; trigger times are not exported.
void handleAutoSetpointExport(AsyncWebServerRequest *request) {
  AutoSetpointRule_t rules[AUTO_SETPOINT_RULE_COUNT];
  getAutoSetpointRules(rules);
  String xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<povotoAutoSetpoints version=\"2\">\n";
  for (int i = 0; i < AUTO_SETPOINT_RULE_COUNT; i++) {
    const AutoSetpointRule_t &rule = rules[i];
    xml += "  <rule index=\"" + String(i + 1) + "\" name=\"" + escapeMarkup(rule.name) +
           "\" manualOnly=\"" + (rule.manualOnly ? "true" : "false") + "\">\n    <trigger";
    appendXmlAttribute(xml, "stableHours", rule.stableHours);
    appendXmlAttribute(xml, "pressureStableHours", rule.pressureStableHours);
    appendXmlAttribute(xml, "sgBelow", rule.sgBelow, 3);
    appendXmlAttribute(xml, "co2RateBelow", rule.co2RateBelow);
    if (i > 0) xml += String(" requiresPrevious=\"") + (rule.requiresPrevious ? "true" : "false") + "\"";
    xml += "/>\n    <action";
    appendXmlAttribute(xml, "pressure", rule.pressure);
    appendXmlAttribute(xml, "pressureSlow", rule.pressureSlow);
    appendXmlAttribute(xml, "temperature", rule.temperature);
    appendXmlAttribute(xml, "temperatureSlow", rule.temperatureSlow);
    xml += "/>\n  </rule>\n";
  }
  xml += "</povotoAutoSetpoints>\n";
  AsyncWebServerResponse *response = request->beginResponse(200, "application/xml; charset=utf-8", xml);
  response->addHeader("Content-Disposition", "attachment; filename=\"povoto-autosetpoints.xml\"");
  request->send(response);
}

// The browser parses and checks the XML, then posts the same fields as the
// rules form. Rules missing from the file are imported empty.
void handleAutoSetpointImport(AsyncWebServerRequest *request) {
  AutoSetpointRule_t rules[AUTO_SETPOINT_RULE_COUNT];
  const String error = parseAutoSetpointRules(request, rules, nullptr);
  if (error.length()) {
    request->send(400, "text/plain; charset=utf-8", error);
    return;
  }
  if (!importAutoSetpointRules(rules)) {
    request->send(500, "text/plain; charset=utf-8", "Imported rules could not be saved to NVS.");
    return;
  }
  request->send(200, "text/plain; charset=utf-8", "Rules imported.");
}

void handleAutoSetpointReset(AsyncWebServerRequest *request) {
  const int rule = request->hasParam("rule") ? request->getParam("rule")->value().toInt() : 0;
  if (rule < 1 || rule > AUTO_SETPOINT_RULE_COUNT) {
    request->send(400, "text/plain", "Invalid rule.");
    return;
  }
  if (!resetAutoSetpointTrigger(rule - 1)) {
    request->send(500, "text/plain", "Rule reset could not be saved to NVS.");
    return;
  }
  request->redirect("/setpoint");
}

void handleAutoSetpointTrigger(AsyncWebServerRequest *request) {
  const int rule = request->hasParam("rule") ? request->getParam("rule")->value().toInt() : 0;
  const AutoSetpointManualTriggerResult result = triggerAutoSetpointRuleNow(rule - 1);
  switch (result) {
    case AutoSetpointManualTriggerResult::Triggered:
      request->redirect("/setpoint");
      return;
    case AutoSetpointManualTriggerResult::InvalidRule:
      request->send(400, "text/plain", "Invalid rule.");
      return;
    case AutoSetpointManualTriggerResult::AlreadyTriggered:
      request->send(409, "text/plain", "Rule has already triggered. Reset it before triggering again.");
      return;
    case AutoSetpointManualTriggerResult::PreviousNotTriggered:
      request->send(409, "text/plain", "The previous rule has not triggered yet.");
      return;
    case AutoSetpointManualTriggerResult::NoClock:
      request->send(503, "text/plain", "NTP time is unavailable. The rule was not triggered.");
      return;
    case AutoSetpointManualTriggerResult::StorageError:
      request->send(500, "text/plain", "Trigger time could not be saved to NVS. No action was applied.");
      return;
  }
}

// ========== SETPOINT DATA HANDLERS ==========

void handleSetPointDataPage(AsyncWebServerRequest *request) {
  const size_t BUFFER_SIZE = 40000; // ~2.3 KB per triggered action rule
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
  strncat(html, ".container { max-width: 760px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }", remaining);
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
  strncat(html, "h2 { color: #333; margin: 28px 0 12px; } .setpoint-grid { display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 14px; } .setpoint-grid .form-group { margin-bottom: 0; } @media (max-width: 600px) { .setpoint-grid { grid-template-columns: 1fr; } }", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, ".hint { color: #666; font-size: 14px; } fieldset.rule { border: 1px solid #ddd; border-radius: 6px; margin: 0 0 16px; padding: 10px 14px; } fieldset.rule legend { font-weight: bold; color: #333; } .rule-status { margin-bottom: 8px; } .rule-title { color: #333; font-weight: bold; margin: 10px 0 6px; } .rule-grid { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 10px; } .rule-grid .form-group { margin-bottom: 0; } input[readonly], input:disabled { background: #f3f3f3; color: #777; } label.req { font-weight: normal; margin: 10px 0 0; } label.req input { width: auto; margin-right: 6px; } button.btn-reset, button.btn-trigger { color: white; padding: 4px 12px; font-size: 14px; } button.btn-reset { background: #e67e22; } button.btn-trigger { background: #4CAF50; } .import-msg { display: none; margin-top: 10px; padding: 10px; border-radius: 4px; background: #fdecea; color: #b71c1c; border: 1px solid #f5c6cb; } @media (max-width: 600px) { .rule-grid { grid-template-columns: 1fr 1fr; } }", remaining);
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
  strncat(html, "<h2>Temperature</h2><div class='setpoint-grid'><div class='form-group'><label for='setPointTemp'>Set point (&deg;C):</label>", remaining);
  {
    char tmp[16];
    if (SetPointData.setPointTemp == NOTaTEMP) snprintf(tmp, sizeof(tmp), "N/A");
    else snprintf(tmp, sizeof(tmp), "%.1f", SetPointData.setPointTemp);
    snprintf(buffer, sizeof(buffer), "<input type='text' id='setPointTemp' name='setPointTemp' value='%s'>", tmp);
  }
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div><div class='form-group'><label for='setPointSlowTemp'>Slow set point (&deg;C):</label>", remaining);
  {
    char tmp[16];
    if (SetPointData.setPointSlowTemp == NOTaTEMP) snprintf(tmp, sizeof(tmp), "N/A");
    else snprintf(tmp, sizeof(tmp), "%.1f", SetPointData.setPointSlowTemp);
    snprintf(buffer, sizeof(buffer), "<input type='text' id='setPointSlowTemp' name='setPointSlowTemp' value='%s'>", tmp);
  }
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div><div class='form-group'><label for='setPointSlowTempSpeed'>Slow set point speed (&deg;C/day):</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='setPointSlowTempSpeed' name='setPointSlowTempSpeed' value='%.1f' step='0.1' min='1.0' max='8.0' title='Allowed range: 1.0 to 8.0 degrees/day' oninvalid=\"this.setCustomValidity('Enter a value from 1.0 to 8.0 degrees/day.')\" oninput=\"this.setCustomValidity('')\">", SetPointData.setPointSlowTempSpeed);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div></div><h2>Pressure</h2><div class='setpoint-grid'><div class='form-group'><label for='setPointPressure'>Set point (bar):</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='setPointPressure' name='setPointPressure' value='%.2f' step='0.01'>", SetPointData.setPointPressure);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div><div class='form-group'><label for='setPointSlowPressure'>Slow set point (bar):</label>", remaining);
  {
    char tmp[16];
    if (SetPointData.setPointSlowPressure == NOTaTEMP) snprintf(tmp, sizeof(tmp), "N/A");
    else snprintf(tmp, sizeof(tmp), "%.2f", SetPointData.setPointSlowPressure);
    snprintf(buffer, sizeof(buffer), "<input type='text' id='setPointSlowPressure' name='setPointSlowPressure' value='%s'>", tmp);
  }
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div><div class='form-group'><label for='setPointSlowPressureSpeed'>Slow set point speed (bar/day):</label>", remaining);
  snprintf(buffer, sizeof(buffer), "<input type='number' id='setPointSlowPressureSpeed' name='setPointSlowPressureSpeed' value='%.1f' step='0.1' min='0.1' max='2.0' title='Allowed range: 0.1 to 2.0 bar/day' oninvalid=\"this.setCustomValidity('Enter a value from 0.1 to 2.0 bar/day.')\" oninput=\"this.setCustomValidity('')\">", SetPointData.setPointSlowPressureSpeed);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, buffer, remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "</div></div>", remaining);
  remaining = BUFFER_SIZE - strlen(html) - 1;
    strncat(html, "<button type='submit'>Save</button> "
                 "<button type='button' class='btn-secondary' onclick='window.location=\"/menu\"'>Cancel</button>"
                 "</form>", remaining);
  appendAutoSetpointSection(html, BUFFER_SIZE);
  appendHtml(html, BUFFER_SIZE, "</div></body></html>");
  
  request->send(200, "text/html", html);
  free(html);
}

void handleSetPointDataUpdate(AsyncWebServerRequest *request) {
  byte oldMode = SetPointData.mode;
  float slowTempSpeed = SetPointData.setPointSlowTempSpeed;
  float slowPressureSpeed = SetPointData.setPointSlowPressureSpeed;

  if (request->hasParam("setPointSlowTempSpeed", true)) {
    slowTempSpeed = request->getParam("setPointSlowTempSpeed", true)->value().toFloat();
    if (!isfinite(slowTempSpeed) || slowTempSpeed < 1.0f || slowTempSpeed > 8.0f) {
      request->send(400, "text/plain", "Slow temperature set point speed must be from 1.0 to 8.0 degrees/day.");
      return;
    }
  }
  if (request->hasParam("setPointSlowPressureSpeed", true)) {
    slowPressureSpeed = request->getParam("setPointSlowPressureSpeed", true)->value().toFloat();
    if (!isfinite(slowPressureSpeed) || slowPressureSpeed < 0.1f || slowPressureSpeed > 2.0f) {
      request->send(400, "text/plain", "Slow pressure set point speed must be from 0.1 to 2.0 bar/day.");
      return;
    }
  }

  // Pressure set points are held to the Maximum pressure setpoint (Settings).
  const auto pressureTooHigh = [request](const char *name, bool optional) {
    if (!request->hasParam(name, true)) return false;
    String v = request->getParam(name, true)->value();
    v.trim();
    if (optional && (v.length() == 0 || v.equalsIgnoreCase("N/A"))) return false;
    const float value = v.toFloat();
    return !(value >= 0.0f && value <= FMTData.maxSetPointPressure);
  };
  if (pressureTooHigh("setPointPressure", false) || pressureTooHigh("setPointSlowPressure", true)) {
    request->send(400, "text/plain", "Pressure set points must be from 0 to the Maximum pressure setpoint (" +
                  String(FMTData.maxSetPointPressure, 2) + " bar, Settings page).");
    return;
  }

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
  if (request->hasParam("setPointSlowTempSpeed", true)) {
    SetPointData.setPointSlowTempSpeed = slowTempSpeed;
  }
  if (request->hasParam("setPointPressure", true)) {
    SetPointData.setPointPressure = request->getParam("setPointPressure", true)->value().toFloat();
  }
  if (request->hasParam("setPointSlowPressure", true)) {
    String v = request->getParam("setPointSlowPressure", true)->value();
    v.trim();
    if (v.length() == 0 || v.equalsIgnoreCase("N/A"))
      SetPointData.setPointSlowPressure = NOTaTEMP;
    else
      SetPointData.setPointSlowPressure = v.toFloat();
  }
  if (request->hasParam("setPointSlowPressureSpeed", true)) {
    SetPointData.setPointSlowPressureSpeed = slowPressureSpeed;
  }
  if (SetPointData.mode != oldMode) {
    if (SetPointData.mode == MODE_CONDITIONING) {
      enterConditioning(); // after the form fields: the pressure target becomes 0
    } else {
      resetChillHeatCycle();
      // A new batch starts only from Off or Brewing/Transfering.
      if (SetPointData.mode == MODE_FERMENTING) {
        if (oldMode == MODE_CONDITIONING) resumeFermentingFromConditioning();
        else resetCountersForNewBatch();
      }
    }
  }
  
  writeSetPointDataToNIV();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta http-equiv='refresh' content='2;url=/menu'>";
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
               "<button type='button' onclick='window.location=\"/menu\"'>Back</button>"
               "</form>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div style='margin-top:10px;'>"
               "<a class='action-btn' href='/control/auto'>All Auto</a>"
               "<a class='action-btn' href='/control/relief'>Relief once</a>"
               "<a class='action-btn' href='/resetdisplay'>Reset display</a>"
               "</div>", remaining);

  remaining = BUFFER_SIZE - strlen(html) - 1;
  strncat(html, "<div class='sub-link'><a href='/counters'>Counters</a></div>", remaining);

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
  html += "<meta http-equiv='refresh' content='2;url=/menu'>";
  html += "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>Control Data Saved!</h1>";
  html += "<p>Redirecting back to menu...</p>";
  html += "</body></html>";
  
  request->send(200, "text/html", html);
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
  pressureRelief();
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
  html += "'><button type='submit'>Save</button> <a href='/menu'>Cancel</a></form></div></body></html>";
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
  request->redirect("/menu");
}
