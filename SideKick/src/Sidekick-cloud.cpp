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
#include <sys/stat.h>
#include "Sidekick-cloud.h"
#include "GambainoCommon.h"
#include "GambainoWiFi.h"
#include "HttpsRootCAs.h"

namespace {
constexpr char LITTLEFS_MOUNT[] = "/littlefs";
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

// Phase 2: with nothing to send, the SideKick still posts every POLL_MS, and
// the cloud answers each post with the pending requests of this site (one
// TLS connection per minute at most). LogSend queues them; loop() sends them
// to the Povotos (the ESP-NOW sends stay in one task).
constexpr unsigned long POLL_MS = 60000;
constexpr size_t COMMAND_MAX_BYTES = 1100;
struct CloudCommand {
  char json[COMMAND_MAX_BYTES];
};
CloudCommand commandStorage[3];
StaticQueue_t commandQueueControl;
QueueHandle_t commandQueue = nullptr;
std::atomic<unsigned long> commandsReceived{0};
std::atomic<unsigned long> commandsForwarded{0};
std::atomic<unsigned long> commandsUndeliverable{0};

// Batch state: only the latest version of each Povoto, in RAM (not spooled);
// posted with the history while newer than the last one the cloud accepted.
// A reboot loses it, but the Povoto sends a new one every slot.
constexpr size_t STATE_MAX_BYTES = 400;
constexpr int STATE_SLOTS = 10;
struct CloudState {
  uint8_t num;            // PovotoNum, 0 = free slot
  uint32_t version;       // bumped on every state received
  uint32_t sentVersion;   // version the cloud accepted
  char json[STATE_MAX_BYTES];
};
CloudState cloudStates[STATE_SLOTS];  // guarded by spoolMutex

// Written by the web task, read by LogSend: copies under the mutex.
String cloudUrl;
String cloudToken;
std::atomic<bool> cloudConfigured{false};
// Bumped on every settings change: a whoami answer for older settings is dropped.
std::atomic<uint32_t> cloudConfigGeneration{0};

// Site of the token, asked to the cloud (GET /api/whoami): >0 site, 0 unknown, -1 invalid token.
constexpr unsigned long WHOAMI_RETRY_MS = 5UL * 60UL * 1000UL;
std::atomic<int> cloudSite{0};
std::atomic<unsigned long> lastWhoamiMs{0};
// Name of the site (cloud table sidekicks), written by LogSend, read by the web task.
char cloudSiteName[48] = "";
portMUX_TYPE cloudSiteNameMux = portMUX_INITIALIZER_UNLOCKED;

// Reads the JSON string value of key ("name") from the whoami answer.
String jsonStringField(const String &body, const char *key) {
  const String pattern = String("\"") + key + "\":\"";
  int pos = body.indexOf(pattern);
  if (pos < 0) return String();
  String value;
  for (pos += pattern.length(); pos < (int)body.length() && body[pos] != '"'; ++pos) {
    if (body[pos] == '\\' && pos + 1 < (int)body.length()) {
      const char escaped = body[++pos];
      if (escaped == 'u') { pos += 4; value += '?'; }   // non-ASCII: not needed here
      else value += escaped == 'n' || escaped == 't' ? ' ' : escaped;
    }
    else value += body[pos];
  }
  return value;
}

std::atomic<unsigned long> recordsSpooled{0};
std::atomic<unsigned long> recordsDropped{0};
std::atomic<unsigned long> recordsPosted{0};
std::atomic<unsigned long> statesPosted{0};
std::atomic<unsigned long> postsFailed{0};
std::atomic<int> lastHttpCode{0};
std::atomic<unsigned long> lastPostOkMs{0};
std::atomic<unsigned long> lastPostAttemptMs{0};
std::atomic<size_t> pendingBytes{0};

// stat() on the VFS path: unlike LittleFS.open()/exists(), a missing file is
// not logged as an error. The spool files are absent whenever everything was
// sent, and LogSend looks at them every 15 s.
bool fileSize(const char *path, size_t &size) {
  char full[48];
  snprintf(full, sizeof(full), "%s%s", LITTLEFS_MOUNT, path);
  struct stat info;
  if (stat(full, &info) != 0) return false;
  size = (size_t)info.st_size;
  return true;
}

// Callers hold spoolMutex.
size_t readOffset() {
  size_t size;
  if (!fileSize(OFFSET_FILE, size)) return 0;
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
  size_t size;
  return fileSize(SPOOL_FILE, size) ? size : 0;
}

void clearSpool() {
  size_t size;
  if (fileSize(SPOOL_FILE, size)) LittleFS.remove(SPOOL_FILE);
  if (fileSize(OFFSET_FILE, size)) LittleFS.remove(OFFSET_FILE);
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
  cloudConfigured = cloudUrl.length() && cloudToken.length();
}

// Connection settings fields (web task). key: "url" or "token".
bool saveSetting(const char *key, const char *value, String &target) {
  String text(value);
  text.trim();
  if (!strcmp(key, "url") && text.length() && !text.startsWith("https://")) return false;
  if (!spoolMutex || xSemaphoreTake(spoolMutex, pdMS_TO_TICKS(500)) != pdTRUE) return false;
  bool saved = true;
  if (text != target) {
    Preferences store;
    saved = store.begin(NVS_NAMESPACE, false) && store.putString(key, text) == text.length();
    store.end();
    if (saved) {
      target = text;
      cloudConfigured = cloudUrl.length() && cloudToken.length();
      ++cloudConfigGeneration;
      cloudSite = 0;
      lastWhoamiMs = 0;
    }
  }
  xSemaphoreGive(spoolMutex);
  return saved;
}

void getUrlSetting(char *buf, size_t size) {
  if (!spoolMutex || xSemaphoreTake(spoolMutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
  strlcpy(buf, cloudUrl.c_str(), size);
  xSemaphoreGive(spoolMutex);
}

// The token itself never leaves the SideKick: the page only learns whether it is set.
void getTokenSetting(char *buf, size_t size) {
  if (!spoolMutex || xSemaphoreTake(spoolMutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
  strlcpy(buf, cloudToken.length() ? "*" : "", size);
  xSemaphoreGive(spoolMutex);
}

bool setUrlSetting(const char *value)   { return saveSetting("url", value, cloudUrl); }
bool setTokenSetting(const char *value) { return saveSetting("token", value, cloudToken); }

// LogSend task: asks the cloud which site the token belongs to, while unknown.
void refreshCloudSite() {
  if (cloudSite != 0) return;
  const unsigned long last = lastWhoamiMs;
  if (last && millis() - last < WHOAMI_RETRY_MS) return;
  String url, token;
  if (xSemaphoreTake(spoolMutex, portMAX_DELAY) != pdTRUE) return;
  url = cloudUrl;
  token = cloudToken;
  const uint32_t generation = cloudConfigGeneration;
  xSemaphoreGive(spoolMutex);
  // the whoami route sits next to the ingest one
  if (!token.length() || !url.endsWith("/api/ingest")) return;
  lastWhoamiMs = millis() | 1UL;
  url = url.substring(0, url.length() - strlen("ingest")) + "whoami";

  WiFiClientSecure client;
  client.setCACert(httpsRootCAs);
  client.setTimeout(10);          // seconds
  client.setHandshakeTimeout(10); // seconds
  HTTPClient http;
  if (!http.begin(client, url)) return;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);
  http.addHeader("Authorization", "Bearer " + token);
  const int code = http.GET();
  const String body = code > 0 ? http.getString() : String();
  http.end();
  if (generation != cloudConfigGeneration) return;

  const int sitePos = body.indexOf("\"site\":");
  if (code == 200 && sitePos >= 0 && body.substring(sitePos + 7).toInt() > 0) {
    const String name = jsonStringField(body, "name");
    portENTER_CRITICAL(&cloudSiteNameMux);
    strlcpy(cloudSiteName, name.c_str(), sizeof(cloudSiteName));
    portEXIT_CRITICAL(&cloudSiteNameMux);
    cloudSite = body.substring(sitePos + 7).toInt();
  }
  // A Worker without /api/whoami answers 401 too, but with the Access page.
  else if (code == 401 && body.indexOf("invalid token") >= 0)
    cloudSite = -1;
  else
    Serial.printf("[CLOUD] whoami failed: %d\n", code);
}

} // namespace

bool initCloudLog() {
  spoolMutex = xSemaphoreCreateMutex();
  if (!spoolMutex) return false;
  commandQueue = xQueueCreateStatic(3, sizeof(CloudCommand), reinterpret_cast<uint8_t *>(commandStorage),
                                    &commandQueueControl);
  loadConfig();
  // The partition holds only this spool; a blank or old SPIFFS image is formatted.
  if (!LittleFS.begin(true, LITTLEFS_MOUNT)) {
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

void cashCloudState(const char *state) {
  if (!spoolReady || !state) return;
  const size_t length = strnlen(state, STATE_MAX_BYTES);
  const char *numKey = strstr(state, "\"p\":");
  const int num = numKey ? atoi(numKey + 4) : 0;
  if (length < 2 || length >= STATE_MAX_BYTES || state[0] != '{' || state[length - 1] != '}' ||
      memchr(state, '\n', length) || num < 1 || num > 99) {
    Serial.println("[CLOUD] Invalid state ignored");
    return;
  }
  if (xSemaphoreTake(spoolMutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
  CloudState *slot = nullptr;
  for (CloudState &candidate : cloudStates) {
    if (candidate.num == num) { slot = &candidate; break; }
    if (!slot && candidate.num == 0) slot = &candidate;
  }
  if (slot) {
    slot->num = num;
    memcpy(slot->json, state, length + 1);
    ++slot->version;
  }
  else Serial.println("[CLOUD] No room for another Povoto state");
  xSemaphoreGive(spoolMutex);
}

void sendCloudLog() {
  if (!spoolReady || WiFi.status() != WL_CONNECTED) return;
  refreshCloudSite();

  // Copy the settings and the next records under the lock; post without it.
  String url, token;
  String body;
  size_t count = 0;
  size_t nextOffset = 0;
  if (xSemaphoreTake(spoolMutex, portMAX_DELAY) != pdTRUE) return;
  url = cloudUrl;
  token = cloudToken;
  size_t queued = 0;
  const unsigned long lastAttempt = lastPostAttemptMs;
  const bool pollDue = !lastAttempt || millis() - lastAttempt >= POLL_MS;
  if (url.length() && token.length() && fileSize(SPOOL_FILE, queued) && queued) {
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
  // Batch states newer than the cloud has ride in the same post (one TLS
  // connection), each with the version that is acknowledged after a 2xx.
  uint32_t stateVersions[STATE_SLOTS] = {};
  size_t stateCount = 0;
  if (url.length() && token.length()) {
    for (int i = 0; i < STATE_SLOTS; ++i) {
      const CloudState &state = cloudStates[i];
      if (!state.num || state.version == state.sentVersion) continue;
      body += state.json;
      body += '\n';
      stateVersions[i] = state.version;
      ++stateCount;
    }
  }
  xSemaphoreGive(spoolMutex);
  if (!count && !stateCount && !(pollDue && url.length() && token.length())) return;

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
  // NDJSON answer: a summary line, then one line per request for a Povoto.
  const String answer = code >= 200 && code < 300 ? http.getString() : String();
  http.end();
  lastHttpCode = code;
  for (int start = 0; start < (int)answer.length();) {
    int end = answer.indexOf('\n', start);
    if (end < 0) end = answer.length();
    if (answer.indexOf("\"k\":\"cmd\"", start) == -1) break;
    const String line = answer.substring(start, end);
    start = end + 1;
    if (line.indexOf("\"k\":\"cmd\"") < 0) continue;
    // Static: 1.1 KB more on the LogSend stack, which TLS already fills.
    static CloudCommand command;
    if (line.length() >= sizeof(command.json) || !commandQueue) continue;
    strlcpy(command.json, line.c_str(), sizeof(command.json));
    ++commandsReceived;
    if (xQueueSend(commandQueue, &command, 0) != pdTRUE) ++commandsUndeliverable;
  }

  // 2xx: stored (the cloud ignores records it already has and counts bad
  // lines as rejected). 400: the request itself was refused and would block
  // the spool forever, so it is skipped. Anything else (network, 401, 5xx)
  // is retried.
  const bool advance = (code >= 200 && code < 300) || code == 400;
  // the ingest route answers 401 only for a token unknown to the cloud
  if (code == 401) cloudSite = -1;
  else if (advance && cloudSite == -1) cloudSite = 0;   // token accepted again: ask the site
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
  if (count) {
    if (nextOffset >= spoolSize()) clearSpool();
    else if (writeOffset(nextOffset)) updatePending();
  }
  // A state received during the post keeps its newer version pending.
  for (int i = 0; i < STATE_SLOTS; ++i) {
    if (stateVersions[i]) cloudStates[i].sentVersion = stateVersions[i];
  }
  if (code != 400) statesPosted += stateCount;
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
           "<br>Cloud log: %s<br>Spooled: %lu, posted: %lu, dropped: %lu, failed posts: %lu, states posted: %lu<br>"
           "Pending: %u bytes, last HTTP code: %d<br>",
           !spoolReady ? "LittleFS unavailable" : cloudConfigured ? "configured" : "not configured (Connection settings)",
           recordsSpooled.load(), recordsPosted.load(), recordsDropped.load(), postsFailed.load(), statesPosted.load(),
           (unsigned)pendingBytes.load(), lastHttpCode.load());
  strncat(st, line, size - strlen(st) - 1);
  // TLS needs ~40 KB, partly contiguous: watch the largest block.
  snprintf(line, sizeof(line), "Heap free: %u, largest block: %u, minimum ever: %u<br>",
           (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getMinFreeHeap());
  strncat(st, line, size - strlen(st) - 1);
  snprintf(line, sizeof(line), "Cloud requests: received %lu, forwarded %lu, not delivered %lu<br>",
           commandsReceived.load(), commandsForwarded.load(), commandsUndeliverable.load());
  strncat(st, line, size - strlen(st) - 1);
  appendAgo(st, size, "Last cloud post attempt:", lastPostAttemptMs.load());
  appendAgo(st, size, "Last cloud post OK:", lastPostOkMs.load());
}

void appendCloudSiteStatus(char *st, size_t size) {
  const int site = cloudSite;
  char line[160];
  if (!cloudConfigured) snprintf(line, sizeof(line), "&nbsp;&nbsp;Site: cloud not configured<br>");
  else if (site == 0)   snprintf(line, sizeof(line), "&nbsp;&nbsp;Site: unknown (asking the cloud)<br>");
  else if (site < 0)    snprintf(line, sizeof(line), "&nbsp;&nbsp;Site: invalid cloud token<br>");
  else {
    char name[sizeof(cloudSiteName)];
    portENTER_CRITICAL(&cloudSiteNameMux);
    strlcpy(name, cloudSiteName, sizeof(name));
    portEXIT_CRITICAL(&cloudSiteNameMux);
    String escaped;
    for (const char *c = name; *c; ++c) {
      if (*c == '<') escaped += "&lt;";
      else if (*c == '>') escaped += "&gt;";
      else if (*c == '&') escaped += "&amp;";
      else escaped += *c;
    }
    if (escaped.length()) snprintf(line, sizeof(line), "&nbsp;&nbsp;Site: %d (%s)<br>", site, escaped.c_str());
    else                  snprintf(line, sizeof(line), "&nbsp;&nbsp;Site: %d<br>", site);
  }
  strlcat(st, line, size);
}

// loop(): each request goes to its Povoto ("p", the first key after "k").
void forwardCloudCommands() {
  if (!commandQueue) return;
  static CloudCommand command; // loop task only
  if (xQueueReceive(commandQueue, &command, 0) != pdTRUE) return;
  const char *numKey = strstr(command.json, "\"p\":");
  const int num = numKey ? atoi(numKey + 4) : 0;
  const uint8_t *mac = num >= 1 && num <= MAXFMTS ? peerPovotos[num - 1].mac : nullptr;
  bool known = false;
  for (int i = 0; mac && i < 6; ++i) known = known || mac[i];
  if (!known || sendEspNow(mac, 0, false, (uint8_t)CLOUDCMDPACKET, command.json) != ESP_OK) {
    ++commandsUndeliverable;
    Serial.printf("[CLOUD] request for Povoto %d not delivered\n", num);
    return;
  }
  ++commandsForwarded;
}

void registerCloudLogSettings() {
  gambainoWiFiAddSettingsField({"cloudurl", "Cloud ingest URL", 200, getUrlSetting, setUrlSetting});
  gambainoWiFiAddSettingsField({"cloudtoken", "Cloud token (SideKick site)", 100, getTokenSetting, setTokenSetting, true});
}
