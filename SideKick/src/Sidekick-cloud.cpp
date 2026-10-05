#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <IOTK_ESPAsyncServer.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Sidekick-cloud.h"
#include "HttpsRootCAs.h"

namespace {
constexpr char SPOOL_DIR[] = "/cloud";
constexpr char SPOOL_FILE[] = "/cloud/spool.txt";
constexpr char OFFSET_FILE[] = "/cloud/sent";
constexpr char NVS_NAMESPACE[] = "sk_cloud";
// ~1,300 records: more than a day of 4 Povotos every 5 min.
constexpr size_t SPOOL_MAX_BYTES = 600UL * 1024UL;
constexpr size_t RECORD_MAX_BYTES = 1024;
// Small posts: TLS needs two ~16.7 KB contiguous buffers and the heap is
// fragmented; 8 records still drain a day of backlog in ~40 min.
constexpr size_t POST_MAX_RECORDS = 8;
constexpr size_t POST_MAX_BYTES = 3072;

SemaphoreHandle_t spoolMutex = nullptr;
bool spoolReady = false;

// Written by the web task, read by LogSend: copies under the mutex.
String cloudUrl;
String cloudToken;

std::atomic<unsigned long> recordsSpooled{0};
std::atomic<unsigned long> recordsDropped{0};
std::atomic<unsigned long> recordsPosted{0};
std::atomic<unsigned long> postsFailed{0};
std::atomic<int> lastHttpCode{0};
std::atomic<unsigned long> lastPostOkMs{0};
std::atomic<unsigned long> lastPostAttemptMs{0};
std::atomic<size_t> pendingBytes{0};

// Callers hold spoolMutex.
size_t readOffset() {
  File file = LittleFS.open(OFFSET_FILE, "r");
  if (!file) return 0;
  const size_t offset = (size_t)file.parseInt();
  file.close();
  return offset;
}

bool writeOffset(size_t offset) {
  File file = LittleFS.open(OFFSET_FILE, "w");
  if (!file) return false;
  const bool ok = file.print((unsigned long)offset) > 0;
  file.close();
  return ok;
}

size_t spoolSize() {
  File file = LittleFS.open(SPOOL_FILE, "r");
  if (!file) return 0;
  const size_t size = file.size();
  file.close();
  return size;
}

void clearSpool() {
  LittleFS.remove(SPOOL_FILE);
  LittleFS.remove(OFFSET_FILE);
  pendingBytes = 0;
}

void updatePending() {
  const size_t size = spoolSize();
  const size_t offset = readOffset();
  pendingBytes = size > offset ? size - offset : 0;
}

void loadConfig() {
  Preferences store;
  if (!store.begin(NVS_NAMESPACE, true)) return;
  cloudUrl = store.getString("url", "");
  cloudToken = store.getString("token", "");
  store.end();
}

void handleCloudPage(AsyncWebServerRequest *request) {
  String url;
  bool hasToken;
  if (spoolMutex && xSemaphoreTake(spoolMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    url = cloudUrl;
    hasToken = cloudToken.length() > 0;
    xSemaphoreGive(spoolMutex);
  } else {
    request->send(503, "text/plain", "Busy, try again");
    return;
  }
  String html =
      "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<title>Cloud log</title>"
      "<style>body{font-family:Arial,sans-serif;margin:20px;max-width:640px}"
      "label{display:block;margin-top:12px;font-weight:bold}"
      "input{width:100%;padding:8px;box-sizing:border-box}"
      "button{margin-top:14px;padding:10px 18px}</style></head><body>"
      "<h1>Cloud log</h1><form method='POST' action='/cloud/update'>"
      "<label>Ingest URL</label><input name='url' value='";
  url.replace("'", "&#39;");
  html += url;
  html += "' placeholder='https://&lt;app&gt;/api/ingest'>"
          "<label>SideKick token</label><input name='token' type='password' placeholder='";
  html += hasToken ? "(kept if empty)" : "(not set)";
  html += "'><button type='submit'>Save</button></form><p>";
  char status[900] = "";
  appendCloudLogStatus(status, sizeof(status));
  html += status;
  html += "</p><p><a href='/getstatus'>Status</a></p></body></html>";
  request->send(200, "text/html", html);
}

void handleCloudUpdate(AsyncWebServerRequest *request) {
  if (!spoolMutex) {
    request->send(503, "text/plain", "Cloud log unavailable");
    return;
  }
  String url = request->hasParam("url", true) ? request->getParam("url", true)->value() : "";
  String token = request->hasParam("token", true) ? request->getParam("token", true)->value() : "";
  url.trim();
  token.trim();
  if (url.length() && !url.startsWith("https://")) {
    request->send(400, "text/plain", "The URL must start with https://");
    return;
  }
  Preferences store;
  bool saved = store.begin(NVS_NAMESPACE, false);
  if (saved) {
    saved = store.putString("url", url) == url.length();
    if (saved && token.length()) saved = store.putString("token", token) == token.length();
    store.end();
  }
  if (!saved) {
    request->send(500, "text/plain", "Could not save to NVS");
    return;
  }
  if (xSemaphoreTake(spoolMutex, portMAX_DELAY) == pdTRUE) {
    cloudUrl = url;
    if (token.length()) cloudToken = token;
    xSemaphoreGive(spoolMutex);
  }
  responseConfirmation(request, "Cloud log settings saved", "/cloud");
}
} // namespace

bool initCloudLog() {
  spoolMutex = xSemaphoreCreateMutex();
  if (!spoolMutex) return false;
  loadConfig();
  // The partition holds only this spool; a blank or old SPIFFS image is formatted.
  if (!LittleFS.begin(true)) {
    Serial.println("[CLOUD] LittleFS unavailable; cloud log disabled");
    return false;
  }
  if (!LittleFS.exists(SPOOL_DIR)) LittleFS.mkdir(SPOOL_DIR);
  if (readOffset() > spoolSize()) clearSpool();
  updatePending();
  spoolReady = true;
  return true;
}

void cashCloudLogRecord(const char *record) {
  if (!spoolReady || !record) return;
  const size_t length = strnlen(record, RECORD_MAX_BYTES + 1);
  if (length < 2 || length > RECORD_MAX_BYTES || record[0] != '{' ||
      record[length - 1] != '}' || memchr(record, '\n', length)) {
    Serial.println("[CLOUD] Invalid record ignored");
    ++recordsDropped;
    return;
  }
  // The loop must not wait long behind an HTTP read of the spool.
  if (xSemaphoreTake(spoolMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
    ++recordsDropped;
    return;
  }
  bool ok = false;
  if (spoolSize() + length + 1 <= SPOOL_MAX_BYTES) {
    File file = LittleFS.open(SPOOL_FILE, FILE_APPEND);
    if (file) {
      ok = file.write((const uint8_t *)record, length) == length && file.write('\n') == 1;
      file.close();
    }
  }
  if (ok) {
    ++recordsSpooled;
    pendingBytes += length + 1;
  } else {
    ++recordsDropped;
  }
  xSemaphoreGive(spoolMutex);
}

void sendCloudLog() {
  if (!spoolReady || WiFi.status() != WL_CONNECTED) return;

  // Copy the settings and the next records under the lock; post without it.
  String url, token;
  String body;
  size_t count = 0;
  size_t nextOffset = 0;
  if (xSemaphoreTake(spoolMutex, portMAX_DELAY) != pdTRUE) return;
  url = cloudUrl;
  token = cloudToken;
  if (url.length() && token.length()) {
    File file = LittleFS.open(SPOOL_FILE, "r");
    const size_t offset = readOffset();
    if (file && file.seek(offset)) {
      body.reserve(POST_MAX_BYTES + RECORD_MAX_BYTES + 32);
      nextOffset = offset;
      while (count < POST_MAX_RECORDS && body.length() < POST_MAX_BYTES && file.available()) {
        String line = file.readStringUntil('\n');
        nextOffset += line.length() + 1;
        line.trim();
        if (!line.length()) continue;
        // One JSON record per line: a damaged line costs only itself.
        body += line;
        body += '\n';
        ++count;
      }
    }
    if (file) file.close();
  }
  xSemaphoreGive(spoolMutex);
  if (!count) return;

  WiFiClientSecure client;
  client.setCACert(httpsRootCAs);
  client.setTimeout(10);          // seconds
  client.setHandshakeTimeout(10); // seconds
  HTTPClient http;
  lastPostAttemptMs = millis();
  if (!http.begin(client, url)) {
    ++postsFailed;
    lastHttpCode = -1000;
    return;
  }
  http.setConnectTimeout(10000);
  http.setTimeout(10000);
  http.addHeader("Content-Type", "application/x-ndjson");
  http.addHeader("Authorization", "Bearer " + token);
  const int code = http.POST(body);
  http.end();
  lastHttpCode = code;

  // 2xx: stored (the cloud ignores records it already has and counts bad
  // lines as rejected). 400: the request itself was refused and would block
  // the spool forever, so it is skipped. Anything else (network, 401, 5xx)
  // is retried.
  const bool advance = (code >= 200 && code < 300) || code == 400;
  if (!advance) {
    ++postsFailed;
    Serial.printf("[CLOUD] POST failed: %d (heap %u, largest block %u)\n", code,
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    return;
  }
  if (code == 400) Serial.println("[CLOUD] Batch rejected (400); skipped");
  else {
    recordsPosted += count;
    lastPostOkMs = millis();
  }

  if (xSemaphoreTake(spoolMutex, portMAX_DELAY) != pdTRUE) return;
  if (nextOffset >= spoolSize()) clearSpool();
  else if (writeOffset(nextOffset)) updatePending();
  xSemaphoreGive(spoolMutex);
}

static void appendAgo(char *st, size_t size, const char *label, unsigned long ms) {
  char line[96];
  if (!ms) snprintf(line, sizeof(line), "%s never<br>", label);
  else snprintf(line, sizeof(line), "%s %lu s ago<br>", label, (millis() - ms) / 1000UL);
  strncat(st, line, size - strlen(st) - 1);
}

void appendCloudLogStatus(char *st, size_t size) {
  char line[200];
  snprintf(line, sizeof(line),
           "<br>Cloud log: %s<br>Spooled: %lu, posted: %lu, dropped: %lu, failed posts: %lu<br>"
           "Pending: %u bytes, last HTTP code: %d<br>",
           !spoolReady ? "LittleFS unavailable" : cloudUrl.length() ? "configured" : "not configured (/cloud)",
           recordsSpooled.load(), recordsPosted.load(), recordsDropped.load(), postsFailed.load(),
           (unsigned)pendingBytes.load(), lastHttpCode.load());
  strncat(st, line, size - strlen(st) - 1);
  // TLS needs ~40 KB, partly contiguous: watch the largest block.
  snprintf(line, sizeof(line), "Heap free: %u, largest block: %u, minimum ever: %u<br>",
           (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getMinFreeHeap());
  strncat(st, line, size - strlen(st) - 1);
  appendAgo(st, size, "Last cloud post attempt:", lastPostAttemptMs.load());
  appendAgo(st, size, "Last cloud post OK:", lastPostOkMs.load());
}

void registerCloudLogRoutes() {
  // Child path first: the async server matches by prefix.
  server.on("/cloud/update", HTTP_POST, handleCloudUpdate);
  server.on("/cloud", HTTP_GET, handleCloudPage);
}
