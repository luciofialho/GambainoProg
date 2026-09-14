#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <GambainoCommon.h>
#include "Sidekick-log.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace {
constexpr size_t LOGCACHESIZE = 20;
constexpr size_t BREWFATHER_QUEUE_SIZE = 4;
struct LogRecord {
  char payload[MAXPACKETSIZE + 1];
};

// Ownership: free queue -> receiver -> ready queue -> LogSend batch -> free queue.
// Only the owner accesses the payload; HTTP never holds a shared lock.
LogRecord googleRecords[LOGCACHESIZE];
QueueHandle_t freeLogQueue = nullptr;
QueueHandle_t readyLogQueue = nullptr;
QueueHandle_t brewfatherQueue = nullptr; // Pending payloads in arrival order.
StaticQueue_t freeLogQueueControl, readyLogQueueControl, brewfatherQueueControl;
LogRecord *freeLogQueueStorage[LOGCACHESIZE];
LogRecord *readyLogQueueStorage[LOGCACHESIZE];
LogRecord brewfatherQueueStorage[BREWFATHER_QUEUE_SIZE];

// Accessed exclusively by the one LogSend task.
LogRecord *googleBatch[LOGCACHESIZE];
size_t googleBatchCount = 0;
LogRecord brewfatherPayload;
bool brewfatherRetryPending = false;

// Diagnostic values read by the web task; these do not control ownership.
std::atomic<unsigned long> retainedGoogleLogs{0};
std::atomic<bool> brewfatherRetained{false};
std::atomic<unsigned long> numCacheOverflow{0};
std::atomic<unsigned long> numLogsAccepted{0};
std::atomic<unsigned long> numBrewfatherReceived{0};
std::atomic<unsigned long> numBrewfatherQueueOverflow{0};
std::atomic<unsigned long> lastCashLogMs{0};
std::atomic<unsigned long> lastCashBrewfatherLogMs{0};
std::atomic<unsigned long> lastSendAttemptMs{0};
std::atomic<unsigned long> lastSendConnectedMs{0};
std::atomic<unsigned long> numSendAttempts{0};
std::atomic<unsigned long> numSendConnected{0};
std::atomic<unsigned long> lastBrewfatherSendAttemptMs{0};
std::atomic<unsigned long> lastBrewfatherSendConnectedMs{0};
std::atomic<unsigned long> numBrewfatherSendAttempts{0};
std::atomic<unsigned long> numBrewfatherSendConnected{0};
}

bool initLogQueues() {
  // Called only in setup, before any receiver or LogSend task can use the queues.
  if (freeLogQueue) return readyLogQueue && brewfatherQueue;
  freeLogQueue = xQueueCreateStatic(LOGCACHESIZE, sizeof(LogRecord *),
      reinterpret_cast<uint8_t *>(freeLogQueueStorage), &freeLogQueueControl);
  readyLogQueue = xQueueCreateStatic(LOGCACHESIZE, sizeof(LogRecord *),
      reinterpret_cast<uint8_t *>(readyLogQueueStorage), &readyLogQueueControl);
  brewfatherQueue = xQueueCreateStatic(BREWFATHER_QUEUE_SIZE, sizeof(LogRecord),
      reinterpret_cast<uint8_t *>(brewfatherQueueStorage), &brewfatherQueueControl);
  if (!freeLogQueue || !readyLogQueue || !brewfatherQueue) return false;
  for (size_t i = 0; i < LOGCACHESIZE; ++i) {
    LogRecord *record = &googleRecords[i];
    xQueueSend(freeLogQueue, &record, 0);
  }
  return true;
}

const char* dataLogScriptURL = "https://script.google.com/macros/s/AKfycbyBmwFQoiJUpesd4LlS1Bf908ZcU5m0HmAG3s7Ushouiz10uHpkXKjV8ZOOkGI2nQuyyQ/exec";

#ifndef BREWFATHER_STREAM_URL
#define BREWFATHER_STREAM_URL "http://log.brewfather.net/stream?id=JQ3NcxkNWbcDdD"
#endif

const char* brewfatherStreamURL = BREWFATHER_STREAM_URL;

static bool startsWithIgnoreCase(const char *value, const char *prefix) {
  if (!value || !prefix) return false;
  size_t i = 0;
  for (; prefix[i] != '\0'; i++) {
    char a = value[i];
    char b = prefix[i];
    if (a == '\0') return false;
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

// GTS Root R1 - Root CA usada pelos serviços Google (incluindo script.google.com)
const char rootCACertificate[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFzANBgkqhkiG9w0BAQwFADBHMQsw
CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU
MBIGA1UEAxMLR1RTIFJvb3QgUjEwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw
MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp
Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjEwggIiMA0GCSqGSIb3DQEBAQUA
A4ICDwAwggIKAoICAQC2EQKLHuOhd5s73L+UPreVp0A8of2C+X0yBoJx9vaMf/vo
27xqLpeXo4xL+Sv2sfnOhB2x+cWX3u+58qPpvBKJXqeqUqv4IyfLpLGcY9vXmX7w
Cl7raKb0xlpHDU0QM+NOsROjyBhsS+z8CZDfnWQpJSMHobTSPS5g4M/SCYe7zUjw
TcLCeoiKu7rPWRnWr4+wB7CeMfGCwcDfLqZtbBkOtdh+JhpFAz2weaSUKK0Pfybl
qAj+lug8aJRT7oM6iCsVlgmy4HqMLnXWnOunVmSPlk9orj2XwoSPwLxAwAtcvfaH
szVsrBhQf4TgTM2S0yDpM7xSma8ytSmzJSq0SPly4cpk9+aCEI3oncKKiPo4Zor8
Y/kB+Xj9e1x3+naH+uzfsQ55lVe0vSbv1gHR6xYKu44LtcXFilWr06zqkUspzBmk
MiVOKvFlRNACzqrOSbTqn3yDsEB750Orp2yjj32JgfpMpf/VjsPOS+C12LOORc92
wO1AK/1TD7Cn1TsNsYqiA94xrcx36m97PtbfkSIS5r762DL8EGMUUXLeXdYWk70p
aDPvOmbsB4om3xPXV2V4J95eSRQAogB/mqghtqmxlbCluQ0WEdrHbEg8QOB+DVrN
VjzRlwW5y0vtOUucxD/SVRNuJLDWcfr0wbrM7Rv1/oFB2ACYPTrIrnqYNxgFlQID
AQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E
FgQU5K8rJnEaK0gnhS9SZizv8IkTcT4wDQYJKoZIhvcNAQEMBQADggIBAJ+qQibb
C5u+/x6Wki4+omVKapi6Ist9wTrYggoGxval3sBOh2Z5ofmmWJyq+bXmYOfg6LEe
QkEzCzc9zolwFcq1JKjPa7XSQCGYzyI0zzvFIoTgxQ6KfF2I5DUkzps+GlQebtuy
h6f88/qBVRRiClmpIgUxPoLW7ttXNLwzldMXG+gnoot7TiYaelpkttGsN/H9oPM4
7HLwEXWdyzRSjeZ2axfG34arJ45JK3VmgRAhpuo+9K4l/3wV3s6MJT/KYnAK9y8J
ZgfIPxz88NtFMN9iiMG1D53Dn0reWVlHxYciNuaCp+0KueIHoI17eko8cdLiA6Ef
MgfdG+RCzgwARWGAtQsgWSl4vflVy2PFPEz0tv/bal8xa5meLMFrUKTX5hgUvYU/
Z6tGn6D/Qqc6f1zLXbBwHSs09dR2CQzreExZBfMzQsNhFRAbd03OIozUhfJFfbdT
6u9AWpQKXCBfTkBdYiJ23//OYb2MI3jSNwLgjt7RETeJ9r/tSQdirpLsQBqvFAnZ
0E6yove+7u7Y/9waLd64NnHi/Hm3lCXRSHNboTXns5lndcEZOitHTtNCjv0xyBZm
2tIMPNuzjsmhDYAPexZ3FL//2wmUspO8IFgV6dtxQ/PeEMMA3KgqlbbC1j+Qa3bb
bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c
-----END CERTIFICATE-----
)EOF";

void cashLogRequest(const char *logEntry) {
  if (!logEntry || !freeLogQueue || !readyLogQueue) return;
  lastCashLogMs = millis();
  const size_t length = strnlen(logEntry, MAXPACKETSIZE + 1);
  if (length == 0 || length > MAXPACKETSIZE) return;

  LogRecord *record;
  if (xQueueReceive(freeLogQueue, &record, 0) != pdTRUE) {
    ++numCacheOverflow;
    return;
  }
  memcpy(record->payload, logEntry, length + 1);
  if (xQueueSend(readyLogQueue, &record, 0) != pdTRUE) {
    // A checked-out pool record always has room in the ready queue.
    xQueueSend(freeLogQueue, &record, 0);
    ++numCacheOverflow;
    return;
  }
  ++numLogsAccepted;
}

void cashBrewfatherLogRequest(const char *logEntry) {
  if (!logEntry || !brewfatherQueue) return;
  const size_t length = strnlen(logEntry, MAXPACKETSIZE + 1);
  if (length == 0 || length > MAXPACKETSIZE) return;
  LogRecord record = {};
  memcpy(record.payload, logEntry, length + 1);
  lastCashBrewfatherLogMs = millis();
  ++numBrewfatherReceived;
  // Never wait in the receive path or overwrite an already queued payload.
  if (xQueueSend(brewfatherQueue, &record, 0) != pdTRUE) {
    ++numBrewfatherQueueOverflow;
  }
}

static bool writeGoogleString(WiFiClientSecure &client, const char *text) {
  const size_t length = strlen(text);
  return client.write((const uint8_t *)text, length) == length;
}

static bool readGoogleSuccess(WiFiClientSecure &client) {
  const unsigned long deadline = millis() + 10000UL;
  while (!client.available()) {
    if (!client.connected() || (long)(millis() - deadline) >= 0) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  char statusLine[96];
  const size_t length = client.readBytesUntil('\n', statusLine, sizeof(statusLine) - 1);
  statusLine[length] = '\0';
  int statusCode = 0;
  return sscanf(statusLine, "HTTP/%*u.%*u %d", &statusCode) == 1 &&
         statusCode >= 200 && statusCode < 400;
}

void sendLogToGoogleSheets() {
  if (!readyLogQueue || WiFi.status() != WL_CONNECTED) return;
  // Retain a failed batch locally; incoming logs only use the other pool slots.
  if (googleBatchCount == 0) {
    while (googleBatchCount < LOGCACHESIZE &&
           xQueueReceive(readyLogQueue, &googleBatch[googleBatchCount], 0) == pdTRUE) {
      ++googleBatchCount;
    }
    retainedGoogleLogs = googleBatchCount;
  }
  if (googleBatchCount == 0) return;

  WiFiClientSecure client;
  client.setCACert(rootCACertificate);
  client.setTimeout(10);          // WiFiClientSecure expects seconds.
  client.setHandshakeTimeout(10); // seconds.
  lastSendAttemptMs = millis();
  numSendAttempts++;

  bool sent = false;
  if (client.connect("script.google.com", 443)) {
    lastSendConnectedMs = millis();
    numSendConnected++;
    int totalLen = 0;
    for (size_t idx = 0; idx < googleBatchCount; ++idx) {
      totalLen += (int)strlen(googleBatch[idx]->payload);
      if (idx + 1 < googleBatchCount) totalLen++;
    }
    const char wrapper1[] = "{\"requests\": [";
    const char wrapper2[] = "]}";
    totalLen += strlen(wrapper1) + strlen(wrapper2);

    const char *path = strchr(dataLogScriptURL + 8, '/');
    char header[512];
    snprintf(header, sizeof(header),
             "POST %s HTTP/1.1\r\nHost: script.google.com\r\n"
             "Content-Type: application/json\r\nContent-Length: %d\r\n"
             "Connection: close\r\n\r\n",
             path ? path : dataLogScriptURL, totalLen);
    bool writeOk = writeGoogleString(client, header) && writeGoogleString(client, wrapper1);
    for (size_t idx = 0; writeOk && idx < googleBatchCount; ++idx) {
      writeOk = writeGoogleString(client, googleBatch[idx]->payload);
      if (writeOk && idx + 1 < googleBatchCount) writeOk = writeGoogleString(client, ",");
    }
    if (writeOk) writeOk = writeGoogleString(client, wrapper2);

    // Apps Script can execute the request and close the connection before
    // the ESP32 gets its status line. Retrying that fully written POST adds
    // the same rows again. Once every byte has been accepted by the socket,
    // use at-most-once delivery for Google Sheets; a missing reply is kept
    // as a diagnostic only.
    if (writeOk) {
      sent = true;
      if (!readGoogleSuccess(client)) {
        Serial.println("Google Sheets reply unavailable after POST; batch released to avoid duplicate rows");
      }
    }
  }
  client.stop();
  if (sent) {
    for (size_t i = 0; i < googleBatchCount; ++i) {
      xQueueSend(freeLogQueue, &googleBatch[i], 0);
    }
    googleBatchCount = 0;
    retainedGoogleLogs = 0;
  }
}

void sendLogToBrewfather() {
  if (!brewfatherStreamURL[0] || !brewfatherQueue || WiFi.status() != WL_CONNECTED) return;
  // LogSend alone owns the retry payload. Keep it until a successful POST,
  // then take the next queued payload on the following send attempt.
  if (!brewfatherRetryPending &&
      xQueueReceive(brewfatherQueue, &brewfatherPayload, 0) == pdTRUE) {
    brewfatherRetryPending = true;
  }
  if (!brewfatherRetryPending) return;
  brewfatherRetained = true;
  const char *payload = brewfatherPayload.payload;

  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  bool beginOk;
  if (startsWithIgnoreCase(brewfatherStreamURL, "https://")) {
    // A custom HTTPS endpoint has no configured CA in this project yet.
    secureClient.setInsecure();
    secureClient.setTimeout(10);
    secureClient.setHandshakeTimeout(10);
    beginOk = http.begin(secureClient, brewfatherStreamURL);
  } else {
    beginOk = http.begin(plainClient, brewfatherStreamURL);
  }

  lastBrewfatherSendAttemptMs = millis();
  numBrewfatherSendAttempts++;
  bool sent = false;
  if (beginOk) {
    http.setConnectTimeout(10000);
    http.setTimeout(10000);
    http.addHeader("Content-Type", "application/json");
    const int httpCode = http.POST((uint8_t *)payload, strlen(payload));
    sent = httpCode >= 200 && httpCode < 300;
    if (sent) {
      lastBrewfatherSendConnectedMs = millis();
      numBrewfatherSendConnected++;
    } else {
      Serial.printf("[BREWFATHER] HTTP POST failed: %d\n", httpCode);
    }
    http.end();
  } else {
    Serial.println("[BREWFATHER] http.begin failed");
  }

  brewfatherRetryPending = !sent;
  brewfatherRetained = brewfatherRetryPending;
}

static void appendAgoStr(char *buf, size_t sz, const char *label, unsigned long ts) {
  if (ts == 0) {
    snprintf(buf, sz, "%s never <br>", label);
  } else {
    unsigned long ago = (millis() - ts) / 1000;
    if (ago < 60)
      snprintf(buf, sz, "%s %lu sec ago <br>", label, ago);
    else
      snprintf(buf, sz, "%s %lu min ago <br>", label, ago / 60);
  }
}

char * getLogStatus(char * st) {
  char buf[200];

  snprintf(buf, sizeof(buf), "SideKick free heap: %u bytes <br><br>", ESP.getFreeHeap());
  strncpy(st, buf, 200);
  snprintf(buf, sizeof(buf), "Log queue ready: %u / %u <br>Retained for send/retry: %lu <br>",
           readyLogQueue ? (unsigned)uxQueueMessagesWaiting(readyLogQueue) : 0,
           (unsigned)LOGCACHESIZE, retainedGoogleLogs.load());
  strncat(st, buf, 200);
  snprintf(buf, sizeof(buf), "Logs accepted: %lu <br>Log cache overflows: %lu <br>",
           numLogsAccepted.load(), numCacheOverflow.load());
  strncat(st, buf, 200);
  snprintf(buf, sizeof(buf), "Brewfather received: %lu <br>Brewfather queue: %u / %u <br>",
           numBrewfatherReceived.load(),
           brewfatherQueue ? (unsigned)uxQueueMessagesWaiting(brewfatherQueue) : 0,
           (unsigned)BREWFATHER_QUEUE_SIZE);
  strncat(st, buf, 200);
  snprintf(buf, sizeof(buf), "Brewfather retained for send/retry: %s <br>Brewfather queue overflows: %lu <br>",
           brewfatherRetained.load() ? "YES" : "NO", numBrewfatherQueueOverflow.load());
  strncat(st, buf, 200);

  appendAgoStr(buf, sizeof(buf), "Last log received:", lastCashLogMs.load());
  strncat(st, buf, 200);
  appendAgoStr(buf, sizeof(buf), "Last Brewfather payload received:", lastCashBrewfatherLogMs.load());
  strncat(st, buf, 200);
  appendAgoStr(buf, sizeof(buf), "Last send attempt:", lastSendAttemptMs.load());
  strncat(st, buf, 200);
  appendAgoStr(buf, sizeof(buf), "Last successful connect:", lastSendConnectedMs.load());
  strncat(st, buf, 200);
  snprintf(buf, sizeof(buf), "Send attempts: %lu / connected: %lu <br>", numSendAttempts.load(), numSendConnected.load());
  strncat(st, buf, 200);
  appendAgoStr(buf, sizeof(buf), "Last Brewfather send attempt:", lastBrewfatherSendAttemptMs.load());
  strncat(st, buf, 200);
  appendAgoStr(buf, sizeof(buf), "Last Brewfather successful connect:", lastBrewfatherSendConnectedMs.load());
  strncat(st, buf, 200);
  snprintf(buf, sizeof(buf), "Brewfather attempts: %lu / connected: %lu <br>", numBrewfatherSendAttempts.load(), numBrewfatherSendConnected.load());
  strncat(st, buf, 200);

  return st;  
}


