#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <GambainoCommon.h>

#define LOGCACHESIZE 20 // max number of entries in datalog cache
char *datalogBuffer[LOGCACHESIZE]; // circular buffer for log entries
long int logLastEntry=-1;   // index of last entry added
long int logRxQueueStart=0; // index of first entry to lock for sending
long int logRxLockedStart=0; // index of first entry being sent
bool hasNewLog = false; // flag to indicate new log entry added
bool brewfatherHasPendingLog = false;
bool brewfatherSending = false;
char brewfatherLatestPayload[MAXPACKETSIZE+1];
unsigned long numCacheOverflow=0; // number of times cache overflowed
unsigned long numBrewfatherReplacedBeforeSend=0;
unsigned long lastCashLogMs      = 0; // millis of last log received from BrewCore
unsigned long lastCashBrewfatherLogMs = 0;
unsigned long lastSendAttemptMs  = 0; // millis of last send attempt to Google Sheets
unsigned long lastSendConnectedMs= 0; // millis of last successful TCP connect
unsigned long numSendAttempts    = 0; // total send attempts
unsigned long numSendConnected   = 0; // total successful connects
unsigned long lastBrewfatherSendAttemptMs  = 0;
unsigned long lastBrewfatherSendConnectedMs= 0;
unsigned long numBrewfatherSendAttempts    = 0;
unsigned long numBrewfatherSendConnected   = 0;
// mutext for log cache
SemaphoreHandle_t mutexLogCache;
static bool logBuffersReady = false;

static bool ensureLogCache() {
  if (mutexLogCache == NULL) mutexLogCache = xSemaphoreCreateMutex();
  if (mutexLogCache == NULL) {
    Serial.println("[LOG] Could not create cache mutex");
    return false;
  }
  if (logBuffersReady) return true;
  if (xSemaphoreTake(mutexLogCache, pdMS_TO_TICKS(10)) != pdTRUE) return false;

  if (!logBuffersReady) {
    bool allocated = true;
    for (int i = 0; i < LOGCACHESIZE; i++) {
      datalogBuffer[i] = (char *)malloc(MAXPACKETSIZE + 1);
      if (!datalogBuffer[i]) {
        Serial.printf("[LOG] Failed to allocate cache entry %d\n", i);
        allocated = false;
        break;
      }
      datalogBuffer[i][0] = '\0';
    }
    if (!allocated) {
      for (int i = 0; i < LOGCACHESIZE; i++) {
        free(datalogBuffer[i]);
        datalogBuffer[i] = NULL;
      }
    } else {
      logBuffersReady = true;
    }
  }
  xSemaphoreGive(mutexLogCache);
  return logBuffersReady;
}

const char* dataLogScriptURL = "https://script.google.com/macros/s/AKfycbyBmwFQoiJUpesd4LlS1Bf908ZcU5m0HmAG3s7Ushouiz10uHpkXKjV8ZOOkGI2nQuyyQ/exec";

#ifndef BREWFATHER_STREAM_URL
#define BREWFATHER_STREAM_URL "http://log.brewfather.net/stream?id=JQ3NcxkNWbcDdD"
#endif

const char* brewfatherStreamURL = BREWFATHER_STREAM_URL;

static void logBrewfatherPayloadPreview(const char *tag, const char *payload) {
  if (!payload) {
    Serial.printf("[BREWFATHER] %s payload=NULL\n", tag);
    return;
  }
  const int len = (int)strlen(payload);
  const int previewLen = len < 180 ? len : 180;
  char preview[181];
  memcpy(preview, payload, previewLen);
  preview[previewLen] = '\0';
  Serial.printf("[BREWFATHER] %s len=%d preview=%s%s\n",
                tag,
                len,
                preview,
                (len > previewLen ? "..." : ""));
}

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

void sendLogToBrewfather();

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

void cashLogRequest(char *logEntry) {
  if (!logEntry || !ensureLogCache()) return;
  lastCashLogMs = millis();
  if (strnlen(logEntry, MAXPACKETSIZE + 1) > MAXPACKETSIZE) return;

  if (xSemaphoreTake(mutexLogCache, pdMS_TO_TICKS(10)) == pdTRUE) {
    // This protects both the batch currently being sent and the queued batch.
    if ((logLastEntry + 1) - logRxLockedStart >= LOGCACHESIZE) {
      numCacheOverflow++;
    }
    else {
      logLastEntry++;
      strncpy(datalogBuffer[logLastEntry % LOGCACHESIZE], logEntry, MAXPACKETSIZE);
      datalogBuffer[logLastEntry % LOGCACHESIZE][MAXPACKETSIZE] = '\0';
      hasNewLog = true;
    }
    xSemaphoreGive(mutexLogCache);
  }
} 

void cashBrewfatherLogRequest(char *logEntry) {
  if (!logEntry || !ensureLogCache()) return;

  lastCashBrewfatherLogMs = millis();

  if (xSemaphoreTake(mutexLogCache,pdMS_TO_TICKS(10)) == pdTRUE) {
    if (brewfatherHasPendingLog && !brewfatherSending) {
      numBrewfatherReplacedBeforeSend++;
    }
    strncpy(brewfatherLatestPayload, logEntry, MAXPACKETSIZE);
    brewfatherLatestPayload[MAXPACKETSIZE] = '\0';
    brewfatherHasPendingLog = true;
    Serial.printf("[BREWFATHER] W received pending=%d sending=%d replacedBeforeSend=%lu\n",
                  (int)brewfatherHasPendingLog,
                  (int)brewfatherSending,
                  numBrewfatherReplacedBeforeSend);
    logBrewfatherPayloadPreview("RX", brewfatherLatestPayload);
    xSemaphoreGive(mutexLogCache);
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

static void finishGoogleSend(bool sent, long int start, long int end) {
  if (xSemaphoreTake(mutexLogCache, portMAX_DELAY) != pdTRUE) return;
  if (sent) {
    logRxLockedStart = end;
    hasNewLog = logRxQueueStart <= logLastEntry;
  } else {
    logRxLockedStart = start;
    logRxQueueStart = start;
    hasNewLog = true;
  }
  xSemaphoreGive(mutexLogCache);
}

static void sendLogToGoogleSheetsImpl() {
  if (!ensureLogCache()) return;

  long int start;
  long int end;
  if (xSemaphoreTake(mutexLogCache, pdMS_TO_TICKS(10)) != pdTRUE) return;
  if (!hasNewLog || logRxQueueStart > logLastEntry) {
    xSemaphoreGive(mutexLogCache);
    return;
  }
  start = logRxQueueStart;
  end = logLastEntry + 1;
  logRxLockedStart = start;
  logRxQueueStart = end;
  hasNewLog = false;
  xSemaphoreGive(mutexLogCache);

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
    for (long int idx = start; idx < end; idx++) {
      totalLen += (int)strlen(datalogBuffer[idx % LOGCACHESIZE]);
      if (idx + 1 < end) totalLen++;
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
    for (long int idx = start; writeOk && idx < end; idx++) {
      writeOk = writeGoogleString(client, datalogBuffer[idx % LOGCACHESIZE]);
      if (writeOk && idx + 1 < end) writeOk = writeGoogleString(client, ",");
    }
    if (writeOk) writeOk = writeGoogleString(client, wrapper2);
    sent = writeOk && readGoogleSuccess(client);
  }
  client.stop();
  finishGoogleSend(sent, start, end);
}

void sendLogToGoogleSheets() {
  sendLogToGoogleSheetsImpl();
  return;
#if 0 // Previous implementation retained below temporarily for source-history context.
  if (mutexLogCache == NULL)
    return;

  if (1/*xSemaphoreTake(mutexLogCache,pdMS_TO_TICKS(10))*/) {
    if (hasNewLog) {
      long int start = logRxQueueStart;
      long int end   = logLastEntry + 1; // index after last to send
      logRxLockedStart = start;
      logRxQueueStart  = end;
      // NOTE: hasNewLog is NOT cleared yet – only cleared after confirmed success

      //Serial.printf("[LOG] Sending entries %ld..%ld (%ld entries) to Google\n", start, end - 1, end - start);

      WiFiClientSecure client;       
      client.setCACert(rootCACertificate);
      client.setTimeout(10000);
      lastSendAttemptMs = millis();
      numSendAttempts++;

      if (!client.connect("script.google.com", 443)) {
        //Serial.println("[LOG] Connection FAILED – restoring queue for retry");
        // Restore queue so entries are retried on next cycle
        logRxLockedStart = start;
        logRxQueueStart  = start;
        return;
      }
      lastSendConnectedMs = millis();
      numSendConnected++;
      //Serial.println("[LOG] Connected OK");

      int totalLen = 0;
      long int idx = start;
      do {
        totalLen += (int)strlen(datalogBuffer[idx % LOGCACHESIZE])+1;
        idx++;
      } while (idx != end);
      totalLen--; // uncount comma for last entry does not have comma after it

      char wrapper1[] = "{\"requests\": [";
      char wrapper2[] = "]}";
      totalLen += strlen(wrapper1) + strlen(wrapper2);

      // Use path-only (origin-form) for direct TLS connection, not full https:// URL
      const char* path = strchr(dataLogScriptURL + 8, '/'); // skip "https://"
      char header[512];
      snprintf(header, sizeof(header),
        "POST %s HTTP/1.1\r\n"
        "Host: script.google.com\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        path ? path : dataLogScriptURL, totalLen);

      //Serial.printf("[LOG] POST path=%s bodyLen=%d\n", path ? path : "?", totalLen);

      client.print(header); ///erial.print(header);
      client.print(wrapper1);//Serial.print(wrapper1);
      idx = start;
      do {
        client.print(datalogBuffer[idx % LOGCACHESIZE]); //Serial.print(datalogBuffer[idx % LOGCACHESIZE]);
        if (idx != end - 1) {
          client.print(","); //Serial.print(",");
        }
        idx++;
      } while (idx != end);
      client.print(wrapper2); //Serial.print(wrapper2);
      client.flush();

      // Read and log the HTTP response (crucial: Google returns 302 redirect)
      String statusLine = client.readStringUntil('\n');
      statusLine.trim();
      //Serial.printf("[LOG] HTTP status: %s\n", statusLine.c_str());
      while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        line.trim();
        // if (line.startsWith("Location:"))   Serial.printf("[LOG]   %s\n", line.c_str());
        if (line.length() == 0) break; // blank line = end of headers
      }

      client.stop();
      hasNewLog = false; // only clear after successful send
      logRxLockedStart = logRxQueueStart;
      //Serial.printf("[LOG] Done. lockedStart=%ld queueStart=%ld lastEntry=%ld\n", logRxLockedStart, logRxQueueStart, logLastEntry);
    }
    else {
      //xSemaphoreGive(mutexLogCache);
    }
  }
#endif
}

static void sendLogToBrewfatherImpl() {
  if (!brewfatherStreamURL[0] || !ensureLogCache()) return;

  char payload[MAXPACKETSIZE + 1];
  if (xSemaphoreTake(mutexLogCache, pdMS_TO_TICKS(10)) != pdTRUE) return;
  if (brewfatherSending || !brewfatherHasPendingLog) {
    xSemaphoreGive(mutexLogCache);
    return;
  }
  brewfatherSending = true;
  strncpy(payload, brewfatherLatestPayload, MAXPACKETSIZE);
  payload[MAXPACKETSIZE] = '\0';
  brewfatherHasPendingLog = false;
  xSemaphoreGive(mutexLogCache);

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

  if (xSemaphoreTake(mutexLogCache, portMAX_DELAY) == pdTRUE) {
    // A newer payload received while this POST ran always wins.
    if (!sent && !brewfatherHasPendingLog) {
      strncpy(brewfatherLatestPayload, payload, MAXPACKETSIZE);
      brewfatherLatestPayload[MAXPACKETSIZE] = '\0';
      brewfatherHasPendingLog = true;
    }
    brewfatherSending = false;
    xSemaphoreGive(mutexLogCache);
  }
}

void sendLogToBrewfather() {
  sendLogToBrewfatherImpl();
  return;
#if 0 // Previous implementation retained below temporarily for source-history context.
  if (!brewfatherStreamURL[0]) {
    Serial.println("[BREWFATHER] Skip send: BREWFATHER_STREAM_URL is empty");
    return;
  }
  if (mutexLogCache == NULL)
    return;

  if (brewfatherSending) {
    Serial.println("[BREWFATHER] Skip send: already sending");
    return;
  }

  if (1/*xSemaphoreTake(mutexLogCache,pdMS_TO_TICKS(10))*/) {
    if (!brewfatherHasPendingLog) {
      return;
    }

    brewfatherSending = true;

    while (brewfatherHasPendingLog) {
      char payload[MAXPACKETSIZE+1];
      strncpy(payload, brewfatherLatestPayload, MAXPACKETSIZE);
      payload[MAXPACKETSIZE] = '\0';
      brewfatherHasPendingLog = false;

      Serial.printf("[BREWFATHER] HTTP begin url=%s\n", brewfatherStreamURL);
      logBrewfatherPayloadPreview("TX", payload);

      HTTPClient http;

      lastBrewfatherSendAttemptMs = millis();
      numBrewfatherSendAttempts++;

      bool beginOk = false;
      if (startsWithIgnoreCase(brewfatherStreamURL, "https://")) {
        WiFiClientSecure secureClient;
        secureClient.setInsecure();
        beginOk = http.begin(secureClient, brewfatherStreamURL);
        Serial.println("[BREWFATHER] Transport: HTTPS (WiFiClientSecure)");
      } else {
        WiFiClient plainClient;
        beginOk = http.begin(plainClient, brewfatherStreamURL);
        Serial.println("[BREWFATHER] Transport: HTTP (WiFiClient)");
      }

      if (!beginOk) {
        Serial.println("[BREWFATHER] http.begin failed");
        brewfatherHasPendingLog = true;
        break;
      }

      http.addHeader("Content-Type", "application/json");
      int httpCode = http.POST((uint8_t*)payload, strlen(payload));
      String responseBody = http.getString();
      http.end();

      const int respLen = responseBody.length();
      String respPreview = responseBody.substring(0, respLen > 220 ? 220 : respLen);
      Serial.printf("[BREWFATHER] HTTP POST code=%d bodyLen=%d bodyPreview=%s%s\n",
                    httpCode,
                    respLen,
                    respPreview.c_str(),
                    (respLen > 220 ? "..." : ""));

      if (httpCode > 0 && httpCode < 400) {
        lastBrewfatherSendConnectedMs = millis();
        numBrewfatherSendConnected++;
        Serial.printf("[BREWFATHER] Send OK attempts=%lu connected=%lu\n",
                      numBrewfatherSendAttempts,
                      numBrewfatherSendConnected);
      } else {
        Serial.printf("[BREWFATHER] HTTP POST failed: %d\n", httpCode);
        // Keep latest payload to retry later.
        strncpy(brewfatherLatestPayload, payload, MAXPACKETSIZE);
        brewfatherLatestPayload[MAXPACKETSIZE] = '\0';
        brewfatherHasPendingLog = true;
        break;
      }
    }

    brewfatherSending = false;
  }
#endif
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
  snprintf(buf,sizeof(buf),"Log last entry index: %ld <br>", logLastEntry);
  strncat(st, buf, 200);
  snprintf(buf,sizeof(buf),"Log queue start index: %ld <br>", logRxQueueStart);
  strncat(st, buf, 200);
  snprintf(buf,sizeof(buf),"Log locked start index: %ld <br>", logRxLockedStart);
  strncat(st, buf, 200);
  snprintf(buf,sizeof(buf),"Log cache overflows: %lu <br>", numCacheOverflow);
  strncat(st, buf, 200);
  snprintf(buf,sizeof(buf),"Brewfather replaced-before-send: %lu <br>", numBrewfatherReplacedBeforeSend);
  strncat(st, buf, 200);
  snprintf(buf,sizeof(buf),"Brewfather pending payload: %s <br>", brewfatherHasPendingLog ? "YES" : "NO");
  strncat(st, buf, 200);

  appendAgoStr(buf, sizeof(buf), "Last log received from BrewCore:", lastCashLogMs);
  strncat(st, buf, 200);
  appendAgoStr(buf, sizeof(buf), "Last Brewfather payload received:", lastCashBrewfatherLogMs);
  strncat(st, buf, 200);
  appendAgoStr(buf, sizeof(buf), "Last send attempt:", lastSendAttemptMs);
  strncat(st, buf, 200);
  appendAgoStr(buf, sizeof(buf), "Last successful connect:", lastSendConnectedMs);
  strncat(st, buf, 200);
  snprintf(buf, sizeof(buf), "Send attempts: %lu / connected: %lu <br>", numSendAttempts, numSendConnected);
  strncat(st, buf, 200);
  appendAgoStr(buf, sizeof(buf), "Last Brewfather send attempt:", lastBrewfatherSendAttemptMs);
  strncat(st, buf, 200);
  appendAgoStr(buf, sizeof(buf), "Last Brewfather successful connect:", lastBrewfatherSendConnectedMs);
  strncat(st, buf, 200);
  snprintf(buf, sizeof(buf), "Brewfather attempts: %lu / connected: %lu <br>", numBrewfatherSendAttempts, numBrewfatherSendConnected);
  strncat(st, buf, 200);

  return st;  
}


