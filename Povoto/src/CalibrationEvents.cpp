#include <Arduino.h>
#include "CalibrationEvents.h"
#include "PovotoFilesystem.h"
#include "datalog.h"
#include "PovotoData.h"

unsigned long int NTPEpoch(); // IOTK_NTP (not #included: see AtmosphericPressure.cpp)

static constexpr const char *EVENTS_FILE = "/calevents.txt";
static constexpr const char *EVENTS_TMP = "/calevents.tmp";
static constexpr size_t EVENTS_MAX_BYTES = 16384;

static String readEvents() {
  File f = povotoDataFS().open(EVENTS_FILE, "r");
  if (!f) return String();
  String text = f.readString();
  f.close();
  return text;
}

// Keeps the newest half (whole lines) when the file grows past the limit.
static void trimEvents() {
  File f = povotoDataFS().open(EVENTS_FILE, "r");
  if (!f) return;
  const size_t size = f.size();
  f.close();
  if (size <= EVENTS_MAX_BYTES) return;
  const String text = readEvents();
  const int cut = text.indexOf('\n', text.length() / 2);
  if (cut < 0) return;
  File out = povotoDataFS().open(EVENTS_TMP, "w");
  if (!out) return;
  const bool ok = out.print(text.substring(cut + 1)) == text.length() - (cut + 1);
  out.close();
  if (!ok) {
    povotoDataFS().remove(EVENTS_TMP);
    return;
  }
  povotoDataFS().remove(EVENTS_FILE);
  povotoDataFS().rename(EVENTS_TMP, EVENTS_FILE);
}

void calibrationEventAppend(const char *type, const char *details) {
  char when[20];
  formatLocalEpochISO(NTPEpoch(), when, sizeof(when));
  File f = povotoDataFS().open(EVENTS_FILE, "a");
  if (!f) return;
  f.printf("%s;%s;%s\n", when[0] ? when : "no-time", type, details ? details : "");
  f.close();
  Serial.printf("[CALIBRATION EVENT] %s %s\n", type, details ? details : "");
  trimEvents();
}

static void htmlEscapeInto(String &out, const String &text) {
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '&') out += "&amp;";
    else out += c;
  }
}

String calibrationEventsHtmlRows(uint8_t count) {
  const String text = readEvents();
  String rows;
  int end = text.length();
  while (count > 0 && end > 0) {
    if (text[end - 1] == '\n') --end;
    const int start = text.lastIndexOf('\n', end - 1) + 1;
    const String line = text.substring(start, end);
    end = start;
    const int a = line.indexOf(';');
    const int b = a < 0 ? -1 : line.indexOf(';', a + 1);
    if (b < 0) continue;
    rows += "<tr><td>";
    htmlEscapeInto(rows, line.substring(0, a));
    rows += "</td><td>";
    htmlEscapeInto(rows, line.substring(a + 1, b));
    rows += "</td><td>";
    htmlEscapeInto(rows, line.substring(b + 1));
    rows += "</td></tr>";
    --count;
  }
  return rows;
}

String lastCalibrationEventType() {
  const String text = readEvents();
  int end = text.length();
  if (end > 0 && text[end - 1] == '\n') --end;
  if (end <= 0) return String();
  const int start = text.lastIndexOf('\n', end - 1) + 1;
  const String line = text.substring(start, end);
  const int a = line.indexOf(';');
  const int b = a < 0 ? -1 : line.indexOf(';', a + 1);
  return b < 0 ? String() : line.substring(a + 1, b);
}

bool lastCalibrationEventOfType(const char *type, String &when, String &details) {
  const String text = readEvents();
  int end = text.length();
  while (end > 0) {
    if (text[end - 1] == '\n') --end;
    const int start = text.lastIndexOf('\n', end - 1) + 1;
    const String line = text.substring(start, end);
    end = start;
    const int a = line.indexOf(';');
    const int b = a < 0 ? -1 : line.indexOf(';', a + 1);
    if (b < 0 || line.substring(a + 1, b) != type) continue;
    when = line.substring(0, a);
    details = line.substring(b + 1);
    return true;
  }
  return false;
}

String calibrationFileName(const char *type, uint32_t epoch, bool incomplete, const char *extension) {
  char iso[24];
  formatLocalEpochISO(epoch >= 1600000000UL ? epoch : 0, iso, sizeof(iso)); // YYYY-MM-DDTHH:MM:SS
  String stamp = "nodate";
  if (iso[0]) stamp = String(iso).substring(0, 4) + String(iso).substring(5, 7) + String(iso).substring(8, 10) +
                      String(iso).substring(11, 13) + String(iso).substring(14, 16);
  return String(incomplete ? "Incomplete-" : "") + "PovotoCalibration-" + type + "-FMT" + String(FMTData.PovotoNum) + "-" +
         stamp + "." + extension;
}

void handleCalibrationEventsText(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain; charset=utf-8", readEvents());
  response->addHeader("Content-Disposition",
                      "attachment; filename=\"" + calibrationFileName("Events", NTPEpoch(), false, "txt") + "\"");
  request->send(response);
}
