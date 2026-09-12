#include "PovotoWifi.h"

#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <qrcode.h>

#include "IOTK_ESPAsyncServer.h"
#include "PovotoCommon.h"
#include "displayUtils.h"

namespace {
constexpr char WIFI_NAMESPACE[] = "pvt_wifi";
constexpr char WIFI_SSID_KEY[] = "ssid";
constexpr char WIFI_PASSWORD_KEY[] = "password";
constexpr char SETUP_AP_SSID[] = "Povoto Setup";
constexpr char SETUP_AP_PASSWORD[] = "povoto123";
constexpr int MAX_SCANNED_NETWORKS = 30;

enum class WiFiState : uint8_t { Unconfigured, Connecting, Configuring, Connected };

DNSServer dnsServer;
WiFiState wifiState = WiFiState::Configuring;
String configuredSsid;
String configuredPassword;
String pendingSsid;
String pendingPassword;
bool connectPending = false;
bool screenDirty = true;
unsigned long lastStatusTapAt = 0;
bool statusTouchHeld = false;
String scannedSsids[MAX_SCANNED_NETWORKS];
int scannedNetworkCount = -1;
bool scanStartRequested = false;

void updateWiFiStatusLed() {
  const bool pulseOn = (millis() % 1000UL) < 200UL;
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;

  if (pulseOn && configuredSsid.isEmpty()) {
    red = 255;                             // No Wi-Fi configured: yellow.
    green = 180;
  }
  else if (pulseOn && WiFi.status() == WL_CONNECTED) {
    green = 255;                           // Connected: green.
  }
  else if (pulseOn) {
    red = 255;                             // Configured but disconnected: red.
  }

  static uint32_t previousColor = 0xFFFFFFFFUL;
  const uint32_t color = (uint32_t(red) << 16) | (uint32_t(green) << 8) | blue;
  if (color == previousColor) return;
  previousColor = color;
  neopixelWrite(PINLED, red, green, blue);
}

String htmlEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '\"': escaped += "&quot;"; break;
      case '\'': escaped += "&#39;"; break;
      default: escaped += value[i]; break;
    }
  }
  return escaped;
}

void loadCredentials() {
  Preferences store;
  configuredSsid = "";
  configuredPassword = "";
  if (store.begin(WIFI_NAMESPACE, false)) {
    configuredSsid = store.getString(WIFI_SSID_KEY, "");
    configuredPassword = store.getString(WIFI_PASSWORD_KEY, "");
    store.end();
  }
}

bool saveCredentials(const String &ssid, const String &password) {
  Preferences store;
  if (!store.begin(WIFI_NAMESPACE, false)) return false;
  const bool saved = store.putString(WIFI_SSID_KEY, ssid) == ssid.length() &&
                     store.putString(WIFI_PASSWORD_KEY, password) == password.length();
  store.end();
  return saved;
}

void beginStationConnection() {
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(configuredSsid.c_str(), configuredPassword.c_str());
  wifiState = WiFiState::Connecting;
  screenDirty = true;
  Serial.printf("WiFi: attempting to connect to '%s'\n", configuredSsid.c_str());
}

void startAccessPoint() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASSWORD);
  dnsServer.start(53, "*", WiFi.softAPIP());
  wifiState = WiFiState::Configuring;
  scannedNetworkCount = -1;
  scanStartRequested = true;
  screenDirty = true;
  Serial.printf("WiFi: configuration AP '%s' at %s\n", SETUP_AP_SSID,
                WiFi.softAPIP().toString().c_str());
}

void processNetworkScan() {
  if (scanStartRequested) {
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
    WiFi.scanDelete();
    WiFi.scanNetworks(true, true);
    scanStartRequested = false;
    return;
  }

  const int result = WiFi.scanComplete();
  if (result < 0) return;
  scannedNetworkCount = min(result, MAX_SCANNED_NETWORKS);
  for (int i = 0; i < scannedNetworkCount; ++i) scannedSsids[i] = WiFi.SSID(i);
  WiFi.scanDelete();
}

void drawQrCode(const char *text, int centerX, int topY) {
  QRCode qr;
  uint8_t data[qrcode_getBufferSize(5)];
  qrcode_initText(&qr, data, 5, ECC_LOW, text);
  const int scale = 4;
  const int qrSize = qr.size * scale;
  const int left = centerX - qrSize / 2;
  tft.fillRect(left - 8, topY - 8, qrSize + 16, qrSize + 16, TFT_WHITE);
  for (uint8_t y = 0; y < qr.size; ++y) {
    for (uint8_t x = 0; x < qr.size; ++x) {
      tft.fillRect(left + x * scale, topY + y * scale, scale, scale,
                   qrcode_getModule(&qr, x, y) ? TFT_BLACK : TFT_WHITE);
    }
  }
}
}

void povotoWiFiInit() {
  loadCredentials();
  if (configuredSsid.isEmpty()) {
    WiFi.mode(WIFI_STA); // Keep the radio ready for ESP-NOW without starting an AP.
    wifiState = WiFiState::Unconfigured;
  }
  else beginStationConnection();
}

void povotoWiFiProcess() {
  if (connectPending) {
    connectPending = false;
    configuredSsid = pendingSsid;
    configuredPassword = pendingPassword;
    beginStationConnection();
  }

  updateWiFiStatusLed();

  if (wifiState == WiFiState::Configuring) {
    dnsServer.processNextRequest();
    processNetworkScan();
    return;
  }

  if (wifiState == WiFiState::Connected && WiFi.status() != WL_CONNECTED) {
    wifiState = WiFiState::Connecting;
    WiFi.reconnect();
  }

  if (wifiState == WiFiState::Connecting && WiFi.status() == WL_CONNECTED) {
    wifiState = WiFiState::Connected;
    screenDirty = true;
    Serial.printf("WiFi: connected to '%s', IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
  }
}

void povotoWiFiReconnect() {
  loadCredentials();
  if (configuredSsid.isEmpty()) {
    wifiState = WiFiState::Unconfigured;
    screenDirty = true;
  }
  else beginStationConnection();
}

void povotoWiFiStartConfiguration() {
  startAccessPoint();
}

bool povotoWiFiConfigurationActive() {
  return wifiState == WiFiState::Configuring;
}

bool povotoWiFiDrawTft() {
  if (wifiState != WiFiState::Configuring) return false;
  if (!screenDirty) return true;

  const int width = tft.width();
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  const String wifiQr = String("WIFI:T:WPA;S:") + SETUP_AP_SSID + ";P:" + SETUP_AP_PASSWORD + ";;";
  tft.drawString("Configure WiFi network", width / 2, 18, 4);
  tft.drawString("Scan to join the setup network", width / 2, 43, 2);
  drawQrCode(wifiQr.c_str(), width / 2, 58);
  tft.drawString(SETUP_AP_SSID, width / 2, 205, 2);
  tft.drawString("Open 192.168.4.1 if the page does not open", width / 2, 230, 2);
  tft.drawString("The network login page should open automatically", width / 2, 258, 2);
  screenDirty = false;
  return true;
}

bool povotoWiFiHandleTouch(uint16_t x, uint16_t y) {
  (void)x;
  (void)y;
  return wifiState == WiFiState::Configuring;
}

bool povotoWiFiHandleStatusTouch(uint16_t x, uint16_t y) {
  if (x < tft.width() - 180 || y < tft.height() - 40) return false;
  if (statusTouchHeld) return true;
  statusTouchHeld = true;
  const unsigned long now = millis();
  const bool doubleTap = lastStatusTapAt != 0 && now - lastStatusTapAt >= 80 && now - lastStatusTapAt <= 500;
  lastStatusTapAt = now;
  if (!doubleTap) return true;

  lastStatusTapAt = 0;
  if (wifiState == WiFiState::Connected) {
    showConfigQRCode();
  } else {
    startAccessPoint();
  }
  return true;
}

void povotoWiFiNotifyTouchReleased() {
  statusTouchHeld = false;
}

void povotoWiFiDrawStatusIndicator() {
  static WiFiState previousState = WiFiState::Unconfigured;
  const int width = tft.width();
  const int height = tft.height();
  const int statusLeft = width - 180;
  const int statusTop = height - 32;
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  if (wifiState == WiFiState::Unconfigured) {
    tft.fillRect(statusLeft, statusTop, width - statusLeft, height - statusTop, TFT_BLACK);
    tft.drawString("No WiFi configured", width - 5, statusTop + 9, 2);
    previousState = wifiState;
    return;
  }
  if (wifiState == WiFiState::Connecting) {
    tft.fillRect(statusLeft, statusTop, width - statusLeft, height - statusTop, TFT_BLACK);
    char status[32];
    snprintf(status, sizeof(status), "Connecting to %.16s", configuredSsid.c_str());
    tft.drawString(status, width - 5, statusTop + 9, 2);
    previousState = wifiState;
    return;
  }
  if (wifiState == WiFiState::Configuring) {
    tft.fillRect(statusLeft, statusTop, width - statusLeft, height - statusTop, TFT_BLACK);
    tft.drawString("WiFi setup", width - 5, statusTop + 9, 2);
    previousState = wifiState;
    return;
  }

  if (previousState != WiFiState::Connected &&
      !restoreMainBackgroundRect(statusLeft, statusTop, width - statusLeft, height - statusTop)) {
    tft.fillRect(statusLeft, statusTop, width - statusLeft, height - statusTop, TFT_BLACK);
  }

  const int rssi = WiFi.RSSI();
  const int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
  const int right = width - 8;
  const int bottom = height - 6;
  for (int i = 0; i < 4; ++i) {
    const int barHeight = 3 + i * 3;
    const uint16_t color = i < bars ? TFT_GREEN : TFT_DARKGREY;
    tft.fillRect(right - (3 - i) * 5, bottom - barHeight, 3, barHeight, color);
  }
  previousState = wifiState;
}

void handlePovotoWiFiPage(AsyncWebServerRequest *request) {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Configure WiFi</title>";
  if (scannedNetworkCount < 0) html += "<meta http-equiv='refresh' content='2'>";
  html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;color:#333}.box{max-width:560px;margin:auto;background:#fff;padding:20px;border-radius:10px}label{display:block;font-weight:bold;margin:16px 0 5px}select,input{width:100%;box-sizing:border-box;padding:10px;border:1px solid #bbb;border-radius:4px}.password{position:relative}.password input{padding-right:48px}.eye{position:absolute;right:2px;top:2px;margin:0;padding:8px 10px;background:transparent;color:#333;font-size:20px}button{margin-top:20px;padding:10px 18px;border:0;border-radius:4px;background:#4caf50;color:white;font-size:16px}</style></head><body><div class='box'><h1>Configure WiFi network</h1><p>Select the network and enter its password.</p><form method='POST' action='/wifi/save'><label for='ssid'>Network</label><select id='ssid' name='ssid' required>";
  for (int i = 0; i < scannedNetworkCount; ++i) {
    const String &ssid = scannedSsids[i];
    if (!ssid.isEmpty()) html += "<option value='" + htmlEscape(ssid) + "'>" + htmlEscape(ssid) + "</option>";
  }
  html += "</select>";
  if (scannedNetworkCount < 0) html += "<p>Scanning nearby networks...</p>";
  html += "<label for='password'>Password</label><div class='password'><input id='password' name='password' type='password' maxlength='63' autocomplete='current-password'><button class='eye' type='button' aria-label='Show password' onclick=\"var p=document.getElementById('password');p.type=p.type==='password'?'text':'password';\">&#128065;</button></div><button type='submit'>Connect</button></form></div></body></html>";
  request->send(200, "text/html", html);
}

void handlePovotoWiFiSave(AsyncWebServerRequest *request) {
  if (!request->hasParam("ssid", true)) {
    request->send(400, "text/plain", "Select a WiFi network.");
    return;
  }
  const String ssid = request->getParam("ssid", true)->value();
  const String password = request->hasParam("password", true) ? request->getParam("password", true)->value() : "";
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63 || !saveCredentials(ssid, password)) {
    request->send(400, "text/plain", "Could not save WiFi credentials.");
    return;
  }
  pendingSsid = ssid;
  pendingPassword = password;
  connectPending = true;
  request->send(200, "text/html", "<!DOCTYPE html><html><head><meta http-equiv='refresh' content='3;url=/'></head><body><h1>Connecting to WiFi...</h1><p>The Povoto is leaving setup mode now.</p></body></html>");
}

void handlePovotoWiFiReconfigure(AsyncWebServerRequest *request) {
  povotoWiFiStartConfiguration();
  request->redirect("/wifi");
}

void povotoWiFiRegisterRoutes() {
  server.on("/wifi", HTTP_GET, handlePovotoWiFiPage);
  server.on("/wifi/save", HTTP_POST, handlePovotoWiFiSave);
  server.on("/wifi/reconfigure", HTTP_GET, handlePovotoWiFiReconfigure);
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/wifi"); });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/wifi"); });
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/wifi"); });
  server.onNotFound([](AsyncWebServerRequest *request) {
    if (povotoWiFiConfigurationActive()) request->redirect("/wifi");
    else request->send(404, "text/plain", "Not found");
  });
}
