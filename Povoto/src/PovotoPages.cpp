#include <ESPAsyncWebServer.h>
#include <IOTK_NTP.h>
#include <IOTK.h>
#include <math.h>
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "PressureControl.h"
#include "GasFlowModel.h"
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
#include "GraphHistory.h"

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
  html += ".brand{display:flex;align-items:center;justify-content:center;gap:14px}.brand img{width:78px;height:78px;object-fit:contain}";
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
  html += "<h1 class='brand'><img src='/assets/povoto.svg' alt='Povoto logo'>Povoto " +
          String(FMTData.PovotoNum) + "</h1>";
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
  // During a pressure/temperature transition an info icon explains the rate on hover.
  html += "<div class='status-item'><strong>SG:</strong> " + String(beerSG, 3) + " (gCO2/L/d: " + String(getBeerCO2EvolutionGramsPerLiterPerDay(), 2) +
          (co2RateInTransition()
               ? " <span title='Transition: net CO2 release (production &minus; absorption)' style='cursor:help'>&#9432;</span>"
               : "") + ")</div>";
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
  html += "<div class='footer-link'><a href='/getstatus'>full status</a></div>";
  html += "<div class='signature'><img src='/assets/brewtal.svg' alt='Brewtal'></div>";
  html += "</div>";
  html += "</body></html>";
  
  request->send(200, "text/html", html);
}

// ========== GRAPH HISTORY (CSV VALIDATION, NO CHART YET) ==========

void handleGraphsPage(AsyncWebServerRequest *request) {
  String html;
  html.reserve(1900);
  html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>Povoto Graphs</title><style>"
         "body{font-family:Arial,sans-serif;background:#f0f0f0;margin:20px;color:#333}"
         ".box{max-width:650px;margin:auto;background:white;padding:24px;border-radius:9px}"
         "a.button{display:inline-block;background:#5168bb;color:white;padding:12px 18px;"
         "margin:8px 8px 8px 0;border-radius:5px;text-decoration:none}"
         "small{color:#555}</style></head><body><div class='box'>"
         "<h1>Graphs</h1><p>Historical observations every 15 minutes."
         " Charts will be added in a later phase; download CSV to validate the data.</p>";
  html += "<p>Observations: " + String(graphHistoryCount()) + "</p>";
  html += "<a class='button' href='/graphs/data.csv'>Download CSV</a>";
  html += "<p><small>Each row is a 15-minute snapshot, not an average. Empty cells mean "
          "unavailable measurements. Flags describe provenance only: 1 = CO2 rate held after "
          "reboot; 2 = transition (CO2 rate omitted); 4 = synthetic point. Values may be "
          "combined, for example 6 = synthetic transition.</small></p>";
  html += "<p><a href='/'>Back to menu</a></p></div></body></html>";
  request->send(200, "text/html; charset=utf-8", html);
}

// [DIAG] Timing of the CSV download callback (temporary): how often the
// server asks for data, how much, and how much TCP send space it has.
struct GraphCsvDiag {
  AsyncClient *client = nullptr;
  unsigned long startMillis = 0, lastMillis = 0, segmentStartMillis = 0;
  uint32_t calls = 0, segmentCalls = 0;
  size_t bytes = 0, segmentStartBytes = 0;
  size_t minAsk = SIZE_MAX, maxAsk = 0, segmentAsk = 0, segmentSpace = 0;
  unsigned long maxGap = 0;
  size_t maxGapAt = 0;
  uint32_t gaps[5] = {}; // <50, 50-150, 150-300, 300-700, >=700 ms
  bool reported = false;
  size_t segMinWritten = SIZE_MAX, segMaxWritten = 0;
  uint32_t segExitNoRows = 0, segExitFull = 0, segSkipped = 0;
  uint16_t *next = nullptr, *total = nullptr;

  void call(size_t maxLen) {
    const unsigned long now = millis();
    if (!calls) startMillis = lastMillis = segmentStartMillis = now;
    const unsigned long gap = now - lastMillis;
    if (calls) {
      gaps[gap < 50 ? 0 : gap < 150 ? 1 : gap < 300 ? 2 : gap < 700 ? 3 : 4]++;
      if (gap > maxGap) { maxGap = gap; maxGapAt = bytes; }
    }
    lastMillis = now;
    ++calls; ++segmentCalls;
    minAsk = maxLen < minAsk ? maxLen : minAsk;
    maxAsk = maxLen > maxAsk ? maxLen : maxAsk;
    segmentAsk += maxLen;
    segmentSpace += client ? client->space() : 0;
  }
  void sent(size_t written, size_t maxLen) {
    bytes += written;
    segMinWritten = written < segMinWritten ? written : segMinWritten;
    segMaxWritten = written > segMaxWritten ? written : segMaxWritten;
    if (written >= maxLen) ++segExitFull; else ++segExitNoRows;
    if (bytes - segmentStartBytes >= 16384) {
      const unsigned long ms = millis() - segmentStartMillis;
      Serial.printf("[DIAG CSV] %6u..%6u B: %5lu ms (%.1f KB/s), %u calls, ask avg %u, space avg %u, "
                    "written %u..%u, exit full %u / short %u, skipped rows %u, row %u/%u\n",
                    unsigned(segmentStartBytes), unsigned(bytes), ms,
                    ms ? (bytes - segmentStartBytes) / 1.024 / ms : 0.0, unsigned(segmentCalls),
                    unsigned(segmentAsk / segmentCalls), unsigned(segmentSpace / segmentCalls),
                    unsigned(segMinWritten), unsigned(segMaxWritten), unsigned(segExitFull),
                    unsigned(segExitNoRows), unsigned(segSkipped),
                    unsigned(next ? *next : 0), unsigned(total ? *total : 0));
      segmentStartBytes = bytes;
      segmentStartMillis = millis();
      segmentCalls = 0; segmentAsk = 0; segmentSpace = 0;
      segMinWritten = SIZE_MAX; segMaxWritten = 0; segExitFull = segExitNoRows = segSkipped = 0;
    }
  }
  void report(const char *how) {
    if (reported) return;
    reported = true;
    const unsigned long ms = millis() - startMillis;
    Serial.printf("[DIAG CSV] %s: %u B in %lu ms (%.1f KB/s), %u calls, ask %u..%u, "
                  "max gap %lu ms at %u B, gaps <50:%u 50-150:%u 150-300:%u 300-700:%u >=700:%u\n",
                  how, unsigned(bytes), ms, ms ? bytes / 1.024 / ms : 0.0, unsigned(calls),
                  unsigned(minAsk == SIZE_MAX ? 0 : minAsk), unsigned(maxAsk), maxGap, unsigned(maxGapAt),
                  unsigned(gaps[0]), unsigned(gaps[1]), unsigned(gaps[2]), unsigned(gaps[3]), unsigned(gaps[4]));
  }
};

struct GraphCsvCursor {
  GraphHistoryPoint *snapshot = nullptr;
  uint16_t total;
  uint16_t next;
  bool headerSent;
  char line[256];
  size_t used;
  size_t position;
  GraphCsvDiag diag; // [DIAG]
  ~GraphCsvCursor() {
    diag.report("closed"); // [DIAG] also when the browser aborts
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
  cursor->diag.client = request->client(); // [DIAG]
  cursor->diag.next = &cursor->next;
  cursor->diag.total = &cursor->total;

  AsyncWebServerResponse *response = request->beginChunkedResponse(
      "text/csv; charset=utf-8",
      [cursor](uint8_t *buffer, size_t maxLen, size_t) -> size_t {
        cursor->diag.call(maxLen); // [DIAG]
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
              if (!cursor->used) {
                ++cursor->diag.segSkipped; // [DIAG] row longer than the line buffer
                continue;
              }
            }
          }
          const size_t pending = cursor->used - cursor->position;
          const size_t space = maxLen - written;
          const size_t amount = pending < space ? pending : space;
          memcpy(buffer + written, cursor->line + cursor->position, amount);
          cursor->position += amount;
          written += amount;
        }
        cursor->diag.sent(written, maxLen); // [DIAG]
        if (!written) cursor->diag.report("done");
        return written;
      });
  response->addHeader("Content-Disposition", "attachment; filename=\"povoto-graphs.csv\"");
  response->addHeader("Cache-Control", "no-store");
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

// ========== DEBUG PARAMS HANDLERS ==========

void handleDebugParamsPage(AsyncWebServerRequest *request) {
  if (!debugging) {
    request->send(403, "text/plain", "Debug mode only");
    return;
  }

  const size_t BUFFER_SIZE = 4500;
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
               "<button type='button' class='btn-secondary' onclick='window.location=\"/\"'>Cancel</button>"
               "</form><hr><h2>Graph history preview</h2>"
               "<p>Replace the current graph history with 14 days of synthetic data. "
               "The replacement is persisted in LittleFS; new Fermenting samples follow it.</p>"
               "<form action='/debugparams/graphs/demo' method='POST' "
               "onsubmit='return confirm(\"Replace the current graph history in PSRAM and LittleFS with 14 days of synthetic data?\")'>"
               "<input type='hidden' name='confirm' value='replace'>"
               "<button type='submit'>Replace graph history with 14-day demo</button></form>"
               "<p><a href='/graphs'>Open Graphs / CSV downloads</a></p>"
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
  const size_t BUFFER_SIZE = 7500;
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
    strncat(html, "<button type='submit'>Apply page changes</button> "
                 "<button type='button' class='btn-secondary' onclick='window.location=\"/\"'>Cancel</button>"
                 "</form>"
                 "<div class='settings-actions'>"
                 "<button type='button' class='btn-secondary' onclick='window.location=\"/wifi/reconfigure\"'>Reconfigure WiFi network</button>"
                 "<button type='button' onclick='window.location=\"/fmtdata/save\"'>Save settings</button>"
                 "<button type='button' class='btn-secondary' onclick='document.getElementById(\"settingsFile\").click()'>Load settings</button>"
                 "<input id='settingsFile' type='file' accept='application/json,.json' hidden>"
                 "</div>"
                 "<script>document.getElementById('settingsFile').addEventListener('change',async function(){const file=this.files[0];if(!file)return;try{const response=await fetch('/fmtdata/load',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'settings='+encodeURIComponent(await file.text())});const message=await response.text();if(!response.ok){alert(message);return;}alert(message);window.location='/fmtdata';}catch(error){alert('Could not load settings file.');}finally{this.value='';}});</script>"
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
  if (request->hasParam("FMTAltitude", true)) {
    FMTData.FMTAltitude = request->getParam("FMTAltitude", true)->value().toFloat();
  }
  if (request->hasParam("dataLogIntervalSeconds", true)) {
    const int interval = request->getParam("dataLogIntervalSeconds", true)->value().toInt();
    if (!isValidDataLogIntervalSeconds(interval)) {
      request->send(400, "text/plain", "Log interval must be 30 seconds, 1, 2, 5, or 10 minutes.");
      return;
    }
    FMTData.dataLogIntervalSeconds = interval;
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

void handleCalibrationDataPage(AsyncWebServerRequest *request) {
  float pageCurrentReading = currentReading;
  bool usingAverageCurrent = false;
  if (request->hasParam("avgCurrent")) {
    pageCurrentReading = request->getParam("avgCurrent")->value().toFloat();
    usingAverageCurrent = true;
  }

  const size_t BUFFER_SIZE = 8000;
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

  String page(html);
  String volumeStatus = "Not running";
  String gasFields = "<h2>Gas flow transfer speed</h2>"
    "<p>Time to reach R% residual pressure at pressure P: t(R,P) = &minus;ln(R)/(a &minus; b &middot; P), with P in gauge bar and R as a fraction.</p>"
    "<div class='form-group'><label for='targetResidualAfterReliefPercent'>Target residual pressure after relief (%)</label><input type='number' id='targetResidualAfterReliefPercent' name='targetResidualAfterReliefPercent' step='0.01' min='0.01' max='99.99' required value='" + String(FMTData.targetResidualAfterReliefPercent, 3) + "'></div>"
    "<div style='display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px'>"
    "<div class='form-group'><label for='expansionTimeCoefficientA'>Coefficient a</label><input type='number' id='expansionTimeCoefficientA' name='expansionTimeCoefficientA' step='any' required value='" + String(FMTData.expansionTimeCoefficientA, 6) + "'></div>"
    "<div class='form-group'><label for='expansionTimeCoefficientB'>Coefficient b</label><input type='number' id='expansionTimeCoefficientB' name='expansionTimeCoefficientB' step='any' required value='" + String(FMTData.expansionTimeCoefficientB, 6) + "'></div></div>"
    "<p>Expansion time for R = " + String(FMTData.targetResidualAfterReliefPercent, 3) + "% at 1 bar: " +
    String(GasFlow::expansionTime(1.0, FMTData.targetResidualAfterReliefPercent / 100.0,
      FMTData.expansionTimeCoefficientA, FMTData.expansionTimeCoefficientB), 2) + " s.</p>";
  const double ventingFactorAtOneBar = GasFlow::ventingResidualFactorAtPressure(1.0,
    FMTData.ventingResidualCoefficientA, FMTData.ventingResidualCoefficientB,
    FMTData.ventingResidualCoefficientC);
  const double ventingTimeAtOneBar = GasFlow::ventingSecondsForResidual(
    FMTData.targetResidualAfterReliefPercent / 100.0,
    FMTData.FMTVolume, FMTData.FMTReliefVolume, ventingFactorAtOneBar);
  gasFields += "<p>Venting residual curve after 20 s: F(P) = c &middot; P<sup>2</sup> + d &middot; P + e (P in gauge bar)</p>"
    "<div style='display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px'>"
    "<div class='form-group'><label for='ventingResidualCoefficientA'>c</label><input type='number' id='ventingResidualCoefficientA' name='ventingResidualCoefficientA' step='any' required value='" + String(FMTData.ventingResidualCoefficientA, 6) + "'></div>"
    "<div class='form-group'><label for='ventingResidualCoefficientB'>d</label><input type='number' id='ventingResidualCoefficientB' name='ventingResidualCoefficientB' step='any' required value='" + String(FMTData.ventingResidualCoefficientB, 6) + "'></div>"
    "<div class='form-group'><label for='ventingResidualCoefficientC'>e</label><input type='number' id='ventingResidualCoefficientC' name='ventingResidualCoefficientC' step='any' required value='" + String(FMTData.ventingResidualCoefficientC, 6) + "'></div></div>"
    "<p>Venting time for R = " + String(FMTData.targetResidualAfterReliefPercent, 3) + "% at 1 bar: " + String(ventingTimeAtOneBar, 2) + " s.</p>"
    "<div class='form-group'><label for='liquidMassInGasVentingPercent'>Liquid mass in gas venting (%)</label><input type='number' id='liquidMassInGasVentingPercent' name='liquidMassInGasVentingPercent' step='0.01' min='0' max='100' required value='" + String(FMTData.liquidMassInGasVentingPercent, 3) + "'></div>";
  page.replace("<button type='submit'>Save</button>", gasFields + "<button type='submit'>Save</button>");
  if (isVolumeDeterminationActive()) {
    volumeStatus = "Running - relief cycles: " + String(getVolumeDeterminationIteration()) + "/35";
  } else {
    const float calculatedVolume = getVolumeDeterminationCalculatedSoFar();
    if (!isnan(calculatedVolume)) {
      volumeStatus = "Completed - calculated FMT volume: " + String(calculatedVolume, 3) + " L";
    }
  }

  const String actions = String("<div class='container' style='margin-top:20px'><h2>Calibration tests</h2>") +
    "<p>Requirements: Mode OFF; initial pressure at least 1.9 bar.</p><p>No pressure target.</p>" +
    "<hr><h3>FMT volume determination</h3>" +
    "<p>Determines the fermenter total volume from the pressure reduction produced by repeated expansions into the configured relief volume. " +
    "Slow mode keeps the existing three-minute expansion and four-minute settling time. Fast mode uses t(R,P) for each expansion and waits two minutes after closing. " +
    "The test stops when the volume estimates converge, or after 35 relief cycles. It reports both the initial-to-final-pressure and full-series-fit results, compensated for temperature.</p>" +
    "<p>Convergence requires at least 10 valid expansions: the last five fitted estimates must span less than 0.5%, with a trend below 0.05% per cycle and settled pressure. " +
    "The endpoint and full-series results must agree within 1%, and the fit of the last 10 readings must agree with the full-series fit within 0.5%. The CSV includes these diagnostics.</p>" +
    "<form action='/startvolume' method='POST'><button name='mode' value='slow'>Volume determination (slow)</button> <button name='mode' value='fast'>Volume determination (fast)</button></form><p>" + volumeStatus + "</p>" +
    "<p>Download results: <a href='/pressurehistory'>Pressure history</a> | " +
    "<a href='/pressuredump'>Pressure dump</a></p>" +
    "<hr><h3>Gas transfer speed determination</h3>" +
    "<p>Expansion speed: 5 cycles of 1, 2, 4, 6, 8, 10, 12, 14, 16 and 30 seconds. Venting speed: 10 successive releases of 20 seconds, without repressurization. Wait 3 minutes after closing (10 seconds in debug mode) before each pressure reading.</p>" +
    "<p>Venting speed records pressureAfter / pressureBefore for each 20-second release; it does not change the calibration fields. The test stops after ten releases or when the measured pressure falls below 0.4 bar. Venting requires the valve outlet connected to atmosphere.</p>" +
    "<form action='/calibration/speed' method='POST'><button name='type' value='expansion'>Expansion speed</button> " +
    "<button name='type' value='venting'>Venting speed</button></form><p>" + getSpeedCalibrationStatus() + "</p>" +
    "<p>Download results: <a href='/calibration/speed.csv?type=expansion'>Expansion CSV</a> | " +
    "<a href='/calibration/speed.csv?type=venting'>Venting CSV</a></p>" +
    "<p>CSV results are kept in RAM until restart or the next test of the same type.</p></div>";
  page.replace("</body>", actions + "</body>");
  request->send(200, "text/html", page);
  free(html);
}

void handleCalibrationDataUpdate(AsyncWebServerRequest *request) {
  const float curveA = request->hasParam("expansionTimeCoefficientA", true) ? request->getParam("expansionTimeCoefficientA", true)->value().toFloat() : FMTData.expansionTimeCoefficientA;
  const float curveB = request->hasParam("expansionTimeCoefficientB", true) ? request->getParam("expansionTimeCoefficientB", true)->value().toFloat() : FMTData.expansionTimeCoefficientB;
  const float targetResidual = request->hasParam("targetResidualAfterReliefPercent", true) ? request->getParam("targetResidualAfterReliefPercent", true)->value().toFloat() : FMTData.targetResidualAfterReliefPercent;
  const float liquidMass = request->hasParam("liquidMassInGasVentingPercent", true) ? request->getParam("liquidMassInGasVentingPercent", true)->value().toFloat() : FMTData.liquidMassInGasVentingPercent;
  const float ventingResidualCoefficientA = request->hasParam("ventingResidualCoefficientA", true) ? request->getParam("ventingResidualCoefficientA", true)->value().toFloat() : FMTData.ventingResidualCoefficientA;
  const float ventingResidualCoefficientB = request->hasParam("ventingResidualCoefficientB", true) ? request->getParam("ventingResidualCoefficientB", true)->value().toFloat() : FMTData.ventingResidualCoefficientB;
  const float ventingResidualCoefficientC = request->hasParam("ventingResidualCoefficientC", true) ? request->getParam("ventingResidualCoefficientC", true)->value().toFloat() : FMTData.ventingResidualCoefficientC;
  const float p1 = request->hasParam("pressure1Bar", true) ? request->getParam("pressure1Bar", true)->value().toFloat() : FMTData.pressure1Bar;
  const float p2 = request->hasParam("pressure2Bar", true) ? request->getParam("pressure2Bar", true)->value().toFloat() : FMTData.pressure2Bar;
  const float c0 = request->hasParam("pressure0Current", true) ? request->getParam("pressure0Current", true)->value().toFloat() : FMTData.pressure0Current;
  const float c1 = request->hasParam("pressure1Current", true) ? request->getParam("pressure1Current", true)->value().toFloat() : FMTData.pressure1Current;
  const float c2 = request->hasParam("pressure2Current", true) ? request->getParam("pressure2Current", true)->value().toFloat() : FMTData.pressure2Current;
  const float maxP = request->hasParam("maximumPressure", true) ? request->getParam("maximumPressure", true)->value().toFloat() : FMTData.maximumPressure;
  const bool threePoint = request->hasParam("threePoint", true);
  if (!GasFlow::validExpansionParameters(curveA, curveB, maxP) ||
      !(targetResidual > 0.0f && targetResidual < 100.0f) || !(liquidMass >= 0.0f && liquidMass <= 100.0f) ||
      !GasFlow::validVentingResidualCurve(ventingResidualCoefficientA, ventingResidualCoefficientB,
                                           ventingResidualCoefficientC, maxP)) {
    request->send(400, "text/plain", "Invalid gas-flow parameters. a - b*P must remain positive through maximum pressure, and residual/liquid mass must be percentages between 0 and 100.");
    return;
  }
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

  if (request->hasParam("nucleationWindow", true)) {
    FMTData.nucleationWindow = request->getParam("nucleationWindow", true)->value().toInt();
  }
  
  FMTData.expansionTimeCoefficientA = curveA;
  FMTData.expansionTimeCoefficientB = curveB;
  FMTData.targetResidualAfterReliefPercent = targetResidual;
  FMTData.liquidMassInGasVentingPercent = liquidMass;
  FMTData.ventingResidualCoefficientA = ventingResidualCoefficientA;
  FMTData.ventingResidualCoefficientB = ventingResidualCoefficientB;
  FMTData.ventingResidualCoefficientC = ventingResidualCoefficientC;
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
  html += "<meta http-equiv='refresh' content='2;url=/'>";
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
  strncat(html, "<button type='submit'>Save</button> <button type='button' class='btn-secondary' onclick='window.location=\"/\"'>Cancel</button></form></div></body></html>", remaining);

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
  html += "<meta http-equiv='refresh' content='2;url=/counters'>";
  html += "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>Counters Data Saved!</h1>";
  if (notes.length()) html += "<p>" + notes + "</p>";
  html += "<p>Redirecting back to counters page...</p>";
  html += "</body></html>";

  request->send(200, "text/html", html);
}

// ========== AUTOMATIC SET POINTS ==========

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
    "<h2>Automatic set points</h2>"
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
    const String error = validateAutoSetpointRule(rule);
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
    request->send(500, "text/plain", "Automatic set points could not be saved to NVS.");
    return;
  }
  // Save & export posts with noredirect and downloads the XML itself.
  if (request->hasParam("noredirect")) {
    request->send(200, "text/plain", "saved");
    return;
  }
  request->redirect("/setpoint");
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
  const size_t BUFFER_SIZE = 40000; // ~2.3 KB per automatic set point rule
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
                 "<button type='button' class='btn-secondary' onclick='window.location=\"/\"'>Cancel</button>"
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
  html += "<meta http-equiv='refresh' content='2;url=/'>";
  html += "<style>body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }</style>";
  html += "</head><body>";
  html += "<h1>Control Data Saved!</h1>";
  html += "<p>Redirecting back to menu...</p>";
  html += "</body></html>";
  
  request->send(200, "text/html", html);
}

void handleStartSpeedCalibration(AsyncWebServerRequest *request) {
  if (!request->hasParam("type", true)) {
    request->send(400, "text/plain", "Missing test type");
    return;
  }
  const String type = request->getParam("type", true)->value();
  if (type != "expansion" && type != "venting") {
    request->send(400, "text/plain", "Invalid test type");
    return;
  }
  char reason[100];
  if (!startSpeedCalibration(type == "venting", reason, sizeof(reason))) {
    request->send(409, "text/plain", reason);
    return;
  }
  request->redirect("/calibration");
}

void handleStartVolume(AsyncWebServerRequest *request) {
  char reason[80];
  
  const bool fast = request->hasParam("mode", true) && request->getParam("mode", true)->value() == "fast";
  bool started = startVolumeDetermination(fast, reason, sizeof(reason));
  if (started) {
    request->redirect("/calibration?volume=1");
  } else {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='2;url=/calibration'>";
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
