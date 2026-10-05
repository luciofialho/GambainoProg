#include "GambainoWiFi.h"

#include <atomic>
#include <DNSServer.h>
#include <Preferences.h>
#include <WiFi.h>

#include "IOTK_ESPAsyncServer.h"
#include "GambainoCommon.h"

namespace {
constexpr char NVS_NAMESPACE[] = "gmb_wifi";
constexpr char KEY_SSID[]      = "ssid";
constexpr char KEY_PASSWORD[]  = "pass";
constexpr char KEY_CHANNEL[]   = "channel";
constexpr unsigned long SCAN_MAX_AGE_MS = 30000UL;
constexpr unsigned long SCAN_RETRY_MS   = 2000UL;
constexpr int MAX_SCANNED_NETWORKS = 20;

enum class State : uint8_t { Connecting, Connected, AccessPoint };

DNSServer dnsServer;
State state = State::Connecting;
char apSsid[33] = "";
char apPassword[65] = "";
const char *uiPath = nullptr;

// configured network (NVS); empty ssid: legacy SSIDs[] list is used
char ssid[33] = "";
char password[65] = "";
uint8_t lastChannel = 0;   // channel of the last connection, hint for attempts and for the AP
int legacyIndex = -1;      // SSIDs[] candidate of the current attempt, -1 when using NVS
char attemptSsid[33] = "";

bool staWasUp = false;
bool attemptActive = false;
unsigned long attemptStart = 0;
unsigned long disconnectedSince = 0;
unsigned long reconnectedAt = 0;   // AP mode: station connected again, AP waiting to close
bool apForced = false;
unsigned long apForcedUntil = 0;

volatile uint8_t lastFailReason = 0;
volatile unsigned long lastFailAt = 0;

// requests from web handlers / other tasks, executed by gambainoWiFiProcess()
std::atomic<bool> reqReconnect{false};
std::atomic<bool> reqAccessPoint{false};
std::atomic<bool> reqCredentials{false};
std::atomic<bool> reqScan{false};
char pendingSsid[33];
char pendingPassword[65];

// scan cache, filled by the loop and read by web handlers (fixed buffers: no reallocation races)
char scannedSsids[MAX_SCANNED_NETWORKS][33];
int8_t scannedRssi[MAX_SCANNED_NETWORKS];
volatile int scannedCount = 0;
volatile bool scanRunning = false;
unsigned long scanAt = 0;
bool scanDone = false;

bool timeReached(unsigned long now, unsigned long start, unsigned long interval) {
  return now - start >= interval;
}

void loadSettings() {
  Preferences store;
  if (!store.begin(NVS_NAMESPACE, true)) return;  // namespace absent on first boot
  store.getString(KEY_SSID, ssid, sizeof(ssid));
  store.getString(KEY_PASSWORD, password, sizeof(password));
  lastChannel = store.getUChar(KEY_CHANNEL, 0);
  store.end();
}

bool saveCredentials() {
  Preferences store;
  if (!store.begin(NVS_NAMESPACE, false)) return false;
  const bool saved = store.putString(KEY_SSID, ssid) == strlen(ssid) &&
                     store.putString(KEY_PASSWORD, password) == strlen(password);
  store.end();
  return saved;
}

void saveChannel(uint8_t channel) {
  if (channel == lastChannel || channel == 0) return;
  lastChannel = channel;
  Preferences store;
  if (!store.begin(NVS_NAMESPACE, false)) return;
  store.putUChar(KEY_CHANNEL, channel);
  store.end();
}

const char *failReasonText(uint8_t reason) {
  switch (reason) {
    case 0:                                return "";
    case WIFI_REASON_NO_AP_FOUND:          return "network not found";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:    return "authentication failed (wrong password?)";
    default:                               return WiFi.disconnectReasonName((wifi_err_reason_t)reason);
  }
}

// last failure arrived after the current attempt started: the station is idle again
bool attemptFailed() {
  return lastFailAt != 0 && (long)(lastFailAt - attemptStart) >= 0;
}

void startAttempt(unsigned long now) {
  const char *pwd;
  if (ssid[0]) {
    legacyIndex = -1;
    strlcpy(attemptSsid, ssid, sizeof(attemptSsid));
    pwd = password;
  }
  else {
    legacyIndex = (legacyIndex + 1) % NUMSSID;
    strlcpy(attemptSsid, SSIDs[legacyIndex], sizeof(attemptSsid));
    pwd = pwds[legacyIndex];
  }
  // disconnect(false,...) keeps the radio on: ESP-NOW and the AP survive the retry
  WiFi.disconnect(false, false);
  WiFi.begin(attemptSsid, pwd, lastChannel);
  attemptActive = true;
  attemptStart = now;
  Serial.printf("WiFi: trying '%s'%s\n", attemptSsid, legacyIndex >= 0 ? " (built-in list)" : "");
}

void startAccessPoint(unsigned long now, bool forced) {
  if (forced) {
    apForced = true;
    apForcedUntil = now + GAMBAINOWIFI_AP_FORCED_MS;
  }
  if (state == State::AccessPoint) return;

  // AP on the channel of the last connection, so ESP-NOW peers that stayed on the
  // router keep hearing us; with the station connected the driver forces its channel.
  uint8_t channel = staWasUp ? WiFi.channel() : lastChannel;
  if (channel < 1 || channel > 13) channel = 1;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsid, apPassword, channel);
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());
  state = State::AccessPoint;
  reconnectedAt = staWasUp ? now : 0;
  attemptStart = now;   // next station attempt after GAMBAINOWIFI_AP_RETRY_MS
  attemptActive = true;
  reqScan = true;
  Serial.printf("WiFi: setup AP '%s' active at %s (channel %u)%s\n", apSsid,
                WiFi.softAPIP().toString().c_str(), channel, forced ? " on request" : "");
}

void stopAccessPoint() {
  dnsServer.stop();
  WiFi.softAPdisconnect(true);  // AP only: back to WIFI_STA, the station stays connected
  apForced = false;
  state = State::Connected;
  Serial.println("WiFi: setup AP closed");
}

void onStationUp(unsigned long now) {
  attemptActive = false;
  lastFailReason = 0;
  if (!ssid[0] && legacyIndex >= 0) {
    strlcpy(ssid, SSIDs[legacyIndex], sizeof(ssid));
    strlcpy(password, pwds[legacyIndex], sizeof(password));
    legacyIndex = -1;
    Serial.printf("WiFi: '%s' migrated to NVS: %s\n", ssid, saveCredentials() ? "ok" : "FAILED");
  }
  saveChannel(WiFi.channel());
  Serial.printf("WiFi: connected to '%s', IP %s, channel %d\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(), WiFi.channel());
  if (state == State::AccessPoint) reconnectedAt = now;
  else state = State::Connected;
  updateDebugModeFromWiFi();
}

void onStationDown(unsigned long now) {
  Serial.printf("WiFi: connection lost%s%s\n", lastFailReason ? ": " : "", failReasonText(lastFailReason));
  disconnectedSince = now;
  reconnectedAt = 0;
  if (state == State::Connected) state = State::Connecting;
  if (state == State::Connecting && !attemptActive) startAttempt(now);
}

void processScan(unsigned long now) {
  if (scanRunning) {
    const int16_t result = WiFi.scanComplete();
    if (result == WIFI_SCAN_RUNNING) return;
    int count = 0;
    for (int i = 0; i < result && count < MAX_SCANNED_NETWORKS; ++i) {
      const String name = WiFi.SSID(i);
      if (name.isEmpty()) continue;
      bool duplicate = false;
      for (int j = 0; j < count && !duplicate; ++j) duplicate = name == scannedSsids[j];
      if (duplicate) continue;
      strlcpy(scannedSsids[count], name.c_str(), sizeof(scannedSsids[count]));
      scannedRssi[count] = WiFi.RSSI(i);
      ++count;
    }
    WiFi.scanDelete();
    if (result >= 0) {
      scannedCount = count;
      scanAt = now;
      scanDone = true;
    }
    else reqScan = true;   // failed: try again
    scanRunning = false;
    return;
  }

  if (!reqScan) return;
  // a station connection attempt in progress makes the scan fail
  if (attemptActive && !attemptFailed() && !timeReached(now, attemptStart, GAMBAINOWIFI_ATTEMPT_MS)) return;
  static unsigned long lastScanTry = 0;
  if (lastScanTry && !timeReached(now, lastScanTry, SCAN_RETRY_MS)) return;
  lastScanTry = now;
  if (WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING) {
    scanRunning = true;
    reqScan = false;
  }
}

void processRequests(unsigned long now) {
  if (reqCredentials.exchange(false)) {
    char newSsid[33], newPassword[65];
    strlcpy(newSsid, pendingSsid, sizeof(newSsid));
    strlcpy(newPassword, pendingPassword, sizeof(newPassword));
    // empty password for the network already saved keeps the saved password
    if (newPassword[0] || strcmp(newSsid, ssid) != 0)
      strlcpy(password, newPassword, sizeof(password));
    strlcpy(ssid, newSsid, sizeof(ssid));
    Serial.printf("WiFi: credentials for '%s' saved: %s\n", ssid, saveCredentials() ? "ok" : "FAILED");
    startAttempt(now);
  }
  if (reqAccessPoint.exchange(false)) startAccessPoint(now, true);
  if (reqReconnect.exchange(false)) startAttempt(now);
}

String htmlEscape(const char *value) {
  String escaped;
  for (const char *c = value; *c; ++c) {
    switch (*c) {
      case '&':  escaped += "&amp;";  break;
      case '<':  escaped += "&lt;";   break;
      case '>':  escaped += "&gt;";   break;
      case '"':  escaped += "&quot;"; break;
      case '\'': escaped += "&#39;";  break;
      default:   escaped += *c;       break;
    }
  }
  return escaped;
}

String jsonEscape(const char *value) {
  String escaped;
  for (const char *c = value; *c; ++c) {
    if (*c == '"' || *c == '\\') { escaped += '\\'; escaped += *c; }
    else if ((uint8_t)*c < 0x20) escaped += ' ';
    else escaped += *c;
  }
  return escaped;
}

String statusText() {
  char buf[200];
  String text;
  if (wifiReallyConnected()) {
    snprintf(buf, sizeof(buf), "Connected to '%s' - IP %s, channel %d, signal %d dBm",
             WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.channel(), WiFi.RSSI());
    text = htmlEscape(buf);
  }
  else {
    snprintf(buf, sizeof(buf), "Not connected - trying '%s' (offline for %lus)",
             attemptSsid, (millis() - disconnectedSince) / 1000UL);
    text = htmlEscape(buf);
    if (lastFailReason) {
      text += "<br>Last failure: ";
      text += failReasonText(lastFailReason);
    }
  }
  if (state == State::AccessPoint) {
    snprintf(buf, sizeof(buf), "<br>Setup access point '%s' active at %s (%d client(s))",
             apSsid, WiFi.softAPIP().toString().c_str(), WiFi.softAPgetStationNum());
    text += buf;
    text += reconnectedAt ? " - closing once clients leave" : " - network retried every 30s";
  }
  if (!ssid[0]) text += "<br>No network saved: trying the built-in list";
  return text;
}

void handleWiFiPage(AsyncWebServerRequest *request) {
  String html;
  html.reserve(4096);
  html += "<!DOCTYPE html><html><head><meta charset='utf-8'>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'><title>WiFi - ";
  html += htmlEscape(ESP_AppName);
  html += "</title><style>body{font-family:Arial,sans-serif;margin:16px;background:#eee;color:#222}"
          ".box{max-width:520px;margin:auto;background:#fff;padding:16px;border-radius:8px}"
          "label{display:block;font-weight:bold;margin:14px 0 4px}"
          "input[type=text],input[type=password]{width:100%;box-sizing:border-box;padding:9px;font-size:16px}"
          "button,.btn{display:inline-block;margin-top:14px;padding:10px 16px;border:0;border-radius:4px;"
          "background:#2e7d32;color:#fff;font-size:16px;text-decoration:none}"
          ".st{background:#f4f4f4;padding:8px;border-radius:4px}#nets a{display:block;padding:4px 0}"
          "small{color:#555}</style></head><body><div class='box'><h2>";
  html += htmlEscape(ESP_AppName);
  html += " - WiFi</h2><p class='st'>";
  html += statusText();
  html += "</p>";
  if (uiPath) {
    html += "<p><a class='btn' href='";
    html += uiPath;
    html += "'>Open equipment interface</a>";
    if (state == State::AccessPoint) {
      html += "<br><small>Through the setup access point use http://";
      html += WiFi.softAPIP().toString();
      html += "/ in a regular browser. Charts need internet and may not load.</small>";
    }
    html += "</p>";
  }
  html += "<form method='POST' action='/wifi/save'><label for='ssid'>Network</label>"
          "<input type='text' id='ssid' name='ssid' maxlength='32' required value='";
  html += htmlEscape(ssid);
  html += "'><div id='nets'><small>Scanning...</small></div>"
          "<label for='pw'>Password</label><input type='password' id='pw' name='password' maxlength='63'>"
          "<small><input type='checkbox' onclick=\"pw.type=this.checked?'text':'password'\"> show"
          " &nbsp;(blank keeps the saved password of this network)</small><br>"
          "<button type='submit'>Save and connect</button></form>"
          "<p><a href='#' onclick='poll(1);return false'>Rescan</a> &middot; "
          "<a href='/wifi/reconnect'>Retry connection now</a> &middot; "
          "<a href='/wifi/ap'>Open setup access point</a></p></div>"
          "<script>function poll(r){fetch('/wifi/networks'+(r?'?rescan=1':'')).then(x=>x.json()).then(j=>{"
          "var d=document.getElementById('nets');d.innerHTML=j.scanning?'<small>Scanning...</small>':'';"
          "j.nets.forEach(n=>{var a=document.createElement('a');a.href='#';a.textContent=n.s+' ('+n.r+' dBm)';"
          "a.onclick=function(){document.getElementById('ssid').value=n.s;document.getElementById('pw').focus();return false};"
          "d.appendChild(a)});if(j.scanning)setTimeout(poll,2000)}).catch(()=>setTimeout(poll,3000))}poll(0)</script>"
          "</body></html>";
  request->send(200, "text/html; charset=utf-8", html);
}

void handleNetworks(AsyncWebServerRequest *request) {
  if (request->hasParam("rescan") || !scanDone || timeReached(millis(), scanAt, SCAN_MAX_AGE_MS))
    if (!scanRunning) reqScan = true;
  String json = "{\"scanning\":";
  json += (reqScan || scanRunning) ? "true" : "false";
  json += ",\"nets\":[";
  const int count = scannedCount;
  for (int i = 0; i < count; ++i) {
    if (i) json += ',';
    json += "{\"s\":\"";
    json += jsonEscape(scannedSsids[i]);
    json += "\",\"r\":";
    json += (int)scannedRssi[i];
    json += '}';
  }
  json += "]}";
  request->send(200, "application/json", json);
}

void handleSave(AsyncWebServerRequest *request) {
  const String newSsid = request->hasParam("ssid", true) ? request->getParam("ssid", true)->value() : String();
  const String newPassword = request->hasParam("password", true) ? request->getParam("password", true)->value() : String();
  if (newSsid.isEmpty() || newSsid.length() > 32 || newPassword.length() > 63 ||
      (newPassword.length() > 0 && newPassword.length() < 8)) {
    request->send(400, "text/plain", "Invalid network name or password (WPA passwords have 8 to 63 characters).");
    return;
  }
  if (reqCredentials) {
    request->send(409, "text/plain", "Previous request still being applied, try again.");
    return;
  }
  strlcpy(pendingSsid, newSsid.c_str(), sizeof(pendingSsid));
  strlcpy(pendingPassword, newPassword.c_str(), sizeof(pendingPassword));
  reqCredentials = true;
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<meta http-equiv='refresh' content='8;url=/wifi'></head><body style='font-family:Arial,sans-serif'>"
                "<h3>Saved. Connecting to '";
  html += htmlEscape(newSsid.c_str());
  html += "'...</h3><p>The setup access point stays active until the connection is confirmed.</p></body></html>";
  request->send(200, "text/html; charset=utf-8", html);
}

void handleCaptiveProbe(AsyncWebServerRequest *request) {
  if (state == State::AccessPoint)
    request->redirect(String("http://") + WiFi.softAPIP().toString() + "/wifi");
  else
    request->send(204);
}

void registerRoutes() {
  // sub-routes before "/wifi": AsyncWebServer matches "/wifi" as a prefix of "/wifi/..."
  server.on("/wifi/networks",  HTTP_GET,  handleNetworks);
  server.on("/wifi/save",      HTTP_POST, handleSave);
  server.on("/wifi/reconnect", HTTP_GET, [](AsyncWebServerRequest *request) {
    gambainoWiFiReconnect();
    request->redirect("/wifi");
  });
  server.on("/wifi/ap",        HTTP_GET, [](AsyncWebServerRequest *request) {
    gambainoWiFiStartAP();
    request->redirect("/wifi");
  });
  server.on("/wifi",           HTTP_GET,  handleWiFiPage);

  // captive portal probes (Android, Apple, Windows, Firefox)
  for (const char *probe : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                            "/connecttest.txt", "/ncsi.txt", "/canonical.html", "/success.txt"})
    server.on(probe, HTTP_GET, handleCaptiveProbe);
}
}

void gambainoWiFiBegin(const char *apName, const char *apPasswordArg, const char *uiPathArg) {
  // called again after server.reset() (BrewCore REINIT): only the routes are restored,
  // the connection and the AP are left untouched
  static bool started = false;
  if (started) {
    ESPSetupServerOnly();
    registerRoutes();
    return;
  }
  started = true;

  uiPath = uiPathArg;
  strlcpy(apPassword, apPasswordArg, sizeof(apPassword));
  loadSettings();

  WiFi.persistent(false);       // credentials live in our NVS namespace; avoids flash writes per attempt
  WiFi.setAutoReconnect(false); // retries are paced here: the core would retry nonstop, hopping channels
  WiFi.mode(WIFI_STA);          // also required before esp_now_init()
  WiFi.setSleep(false);
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
    const uint8_t reason = info.wifi_sta_disconnected.reason;
    if (reason == WIFI_REASON_ASSOC_LEAVE) return;   // our own disconnect()
    lastFailReason = reason;
    lastFailAt = millis();
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(apSsid, sizeof(apSsid), "%s-%02X%02X", apName, mac[4], mac[5]);

  ESPSetupServerOnly();
  registerRoutes();

  const unsigned long now = millis();
  disconnectedSince = now;
  startAttempt(now);
}

void gambainoWiFiProcess() {
  const unsigned long now = millis();
  processRequests(now);

  const bool up = wifiReallyConnected();
  if (up != staWasUp) {
    staWasUp = up;
    if (up) onStationUp(now);
    else    onStationDown(now);
  }

  switch (state) {
    case State::Connected:
      break;

    case State::Connecting:
      if (timeReached(now, disconnectedSince, GAMBAINOWIFI_AP_FALLBACK_MS))
        startAccessPoint(now, false);
      else if (!scanRunning && timeReached(now, attemptStart, GAMBAINOWIFI_ATTEMPT_MS))
        startAttempt(now);
      break;

    case State::AccessPoint:
      dnsServer.processNextRequest();
      if (up) {
        if (apForced && (long)(now - apForcedUntil) < 0) break;
        if (WiFi.softAPgetStationNum() == 0 || timeReached(now, reconnectedAt, GAMBAINOWIFI_AP_CLOSE_GRACE_MS))
          stopAccessPoint();
      }
      else if (!scanRunning && timeReached(now, attemptStart, GAMBAINOWIFI_AP_RETRY_MS))
        startAttempt(now);
      break;
  }

  processScan(now);
}

void gambainoWiFiReconnect() { reqReconnect = true; }
void gambainoWiFiStartAP()   { reqAccessPoint = true; }

bool gambainoWiFiConnected() { return wifiReallyConnected(); }
bool gambainoWiFiApActive()  { return state == State::AccessPoint; }

void gambainoWiFiStatus(char *st, size_t maxLen) {
  if (!st || maxLen == 0) return;
  String block = "<br><b>WiFi</b> (<a href='/wifi'>configure</a>)<br>&nbsp;&nbsp;";
  block += statusText();
  block += "<br>";
  const size_t used = strnlen(st, maxLen);
  if (used + 1 < maxLen) strncat(st, block.c_str(), maxLen - used - 1);
}
