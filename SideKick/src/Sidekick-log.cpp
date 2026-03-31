#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <GambainoCommon.h>

#define LOGCACHESIZE 20 // max number of entries in datalog cache
char *datalogBuffer[LOGCACHESIZE]; // circular buffer for log entries
long int logLastEntry=-1;   // index of last entry added
long int logRxQueueStart=0; // index of first entry to lock for sending
long int logRxLockedStart=0; // index of first entry being sent
bool hasNewLog = false; // flag to indicate new log entry added
unsigned long numCacheOverflow=0; // number of times cache overflowed
unsigned long lastCashLogMs      = 0; // millis of last log received from BrewCore
unsigned long lastSendAttemptMs  = 0; // millis of last send attempt to Google Sheets
unsigned long lastSendConnectedMs= 0; // millis of last successful TCP connect
unsigned long numSendAttempts    = 0; // total send attempts
unsigned long numSendConnected   = 0; // total successful connects
// mutext for log cache
SemaphoreHandle_t mutexLogCache;

const char* dataLogScriptURL = "https://script.google.com/macros/s/AKfycbyBmwFQoiJUpesd4LlS1Bf908ZcU5m0HmAG3s7Ushouiz10uHpkXKjV8ZOOkGI2nQuyyQ/exec";

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

void initLogBuffers() {
  for (int i=0; i<LOGCACHESIZE; i++) {
    datalogBuffer[i] = (char *)malloc(MAXPACKETSIZE+1); 
    
    if (datalogBuffer[i] == NULL) {
      Serial.print("Failed to allocate memory for datalogBuffer"); Serial.println(i);
    }
    datalogBuffer[i][0] = '\0'; // initialize as empty string
  }

}

void cashLogRequest(char *logEntry) {
  lastCashLogMs = millis();
  //Serial.printf("[LOG] cashLogRequest: will be idx=%ld, len=%d\n", logLastEntry + 1, (int)strlen(logEntry));
  static bool initialized = false;
  if (!initialized) {
    initLogBuffers();
    initialized = true;
  }

  if (mutexLogCache == NULL)
    mutexLogCache = xSemaphoreCreateMutex();

  if (1/*xSemaphoreTake(mutexLogCache,pdMS_TO_TICKS(10)*/) {
    if (logLastEntry > logRxLockedStart + LOGCACHESIZE-1) {
      numCacheOverflow++;
      //Serial.printf("[LOG] OVERFLOW! lastEntry=%ld lockedStart=%ld queueStart=%ld\n", logLastEntry, logRxLockedStart, logRxQueueStart);
    }
    else {
      logLastEntry++;
      strcpy(datalogBuffer[logLastEntry % LOGCACHESIZE],logEntry);
      hasNewLog = true;
      //Serial.printf("[LOG] Cached slot=%ld idx=%ld hasNewLog=true\n", logLastEntry % LOGCACHESIZE, logLastEntry);
    }
    //xSemaphoreGive(mutexLogCache);
  }
} 

void sendLogToGoogleSheets() {
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

  appendAgoStr(buf, sizeof(buf), "Last log received from BrewCore:", lastCashLogMs);
  strncat(st, buf, 200);
  appendAgoStr(buf, sizeof(buf), "Last send attempt:", lastSendAttemptMs);
  strncat(st, buf, 200);
  appendAgoStr(buf, sizeof(buf), "Last successful connect:", lastSendConnectedMs);
  strncat(st, buf, 200);
  snprintf(buf, sizeof(buf), "Send attempts: %lu / connected: %lu <br>", numSendAttempts, numSendConnected);
  strncat(st, buf, 200);

  return st;  
}


