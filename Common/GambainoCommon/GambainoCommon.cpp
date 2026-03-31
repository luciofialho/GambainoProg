#include "GambainoCommon.h"

#include <Arduino.h>
#include <stdarg.h>
#include <string.h>
//#include <FreeRTOS.h>
//#include <semphr.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_now.h>

#include <IOTK_NTP.h>
#include "IOTK_ESPAsyncServer.h"

bool debugging = true;

const char *SSIDs[NUMSSID] = {"Gambaino", "secretoca",  "goiaba"};
const char *pwds [NUMSSID] = {"87654321", "Goiaba5090", "heptA2019"};

unsigned long numCycles=0;
unsigned long longestCycle      =0;
unsigned long averageCycle      =0;

bool safeMode = false;


char datalogFolderNameInUse[20];

// =================== PEER ADDRESSING ====================

GambainoPeer peerBrewCore  = {{0,0,0,0,0,0}, {0,0,0,0}};
GambainoPeer peerSideKick  = {{0,0,0,0,0,0}, {0,0,0,0}};
GambainoPeer peerPovotos[MAXFMTS];

// own peer type / index, set by registerOwnPeer()
static char  ownPeerType  = 0;
static int   ownPeerIndex = 0;   // only meaningful for PEERTYPE_POVOTO

static Preferences peerPrefs;
#define PEERS_NS "Peers"

static PeersSavedCallback peersSavedCallback_ = nullptr;
void setPeersSavedCallback(PeersSavedCallback cb) { peersSavedCallback_ = cb; }

static void peerToBytes(const GambainoPeer &p, uint8_t *out) {
  memcpy(out,     p.mac, 6);
  memcpy(out + 6, p.ip,  4);
}
static void bytesToPeer(const uint8_t *in, GambainoPeer &p) {
  memcpy(p.mac, in,     6);
  memcpy(p.ip,  in + 6, 4);
}

void loadPeers() {
  peerPrefs.begin(PEERS_NS, true);
  uint8_t buf[10];
  if (peerPrefs.getBytesLength("BC") == 10) {
    peerPrefs.getBytes("BC", buf, 10);
    bytesToPeer(buf, peerBrewCore);
  }
  if (peerPrefs.getBytesLength("SK") == 10) {
    peerPrefs.getBytes("SK", buf, 10);
    bytesToPeer(buf, peerSideKick);
  }
  for (int i = 0; i < MAXFMTS; i++) {
    char key[6]; snprintf(key, sizeof(key), "P%d", i);
    if (peerPrefs.getBytesLength(key) == 10) {
      peerPrefs.getBytes(key, buf, 10);
      bytesToPeer(buf, peerPovotos[i]);
    }
  }
  peerPrefs.end();
}

void savePeers() {
  peerPrefs.begin(PEERS_NS, false);
  uint8_t buf[10];
  peerToBytes(peerBrewCore, buf);  peerPrefs.putBytes("BC", buf, 10);
  peerToBytes(peerSideKick, buf);  peerPrefs.putBytes("SK", buf, 10);
  for (int i = 0; i < MAXFMTS; i++) {
    char key[6]; snprintf(key, sizeof(key), "P%d", i);
    peerToBytes(peerPovotos[i], buf); peerPrefs.putBytes(key, buf, 10);
  }
  peerPrefs.end();
  if (peersSavedCallback_) peersSavedCallback_();
}

void registerOwnPeer(char peerType, int povotoIndex) {
  ownPeerType  = peerType;
  ownPeerIndex = povotoIndex;

  IPAddress ip = WiFi.localIP();
  uint8_t mac[6];
  WiFi.macAddress(mac);

  GambainoPeer self;
  memcpy(self.mac, mac, 6);
  self.ip[0] = ip[0]; self.ip[1] = ip[1]; self.ip[2] = ip[2]; self.ip[3] = ip[3];

  switch (peerType) {
    case PEERTYPE_BREWCORE: peerBrewCore = self; break;
    case PEERTYPE_SIDEKICK: peerSideKick = self; break;
    case PEERTYPE_POVOTO:
      if (povotoIndex >= 1 && povotoIndex <= MAXFMTS)
        peerPovotos[povotoIndex - 1] = self;
      break;
  }
  savePeers();
}

// Called when WiFi gets an IP (from checkDebugMode) to fill in the real address.
void peerUpdateOwnAddress() {
  if (ownPeerType == 0) return;
  IPAddress ip = WiFi.localIP();
  if (ip == IPAddress(0, 0, 0, 0)) return;
  uint8_t macBytes[6]; WiFi.macAddress(macBytes);
  GambainoPeer self;
  memcpy(self.mac, macBytes, 6);
  self.ip[0] = ip[0]; self.ip[1] = ip[1]; self.ip[2] = ip[2]; self.ip[3] = ip[3];
  switch (ownPeerType) {
    case PEERTYPE_BREWCORE: peerBrewCore = self; break;
    case PEERTYPE_SIDEKICK: peerSideKick = self; break;
    case PEERTYPE_POVOTO:
      if (ownPeerIndex >= 0 && ownPeerIndex < MAXFMTS)
        peerPovotos[ownPeerIndex-1] = self;
      break;
  }
  savePeers();
  Serial.printf("peerUpdateOwnAddress: type=%c idx=%d IP=%d.%d.%d.%d\n",
                ownPeerType, ownPeerIndex, ip[0], ip[1], ip[2], ip[3]);
}

// ---- HTML page helpers ----

static void appendPeerRow(char *st, size_t maxLen, const char *label, const char *keyMac, const char *keyIp, const GambainoPeer &p) {
  char macStr[18] = "";
  char ipStr[16]  = "";
  formatMacAddress(p.mac, macStr, sizeof(macStr));
  snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", p.ip[0], p.ip[1], p.ip[2], p.ip[3]);

  char row[512];
  snprintf(row, sizeof(row),
    "<tr><td><b>%s</b></td>"
    "<td><input name='%s' value='%s' size='17' placeholder='AA:BB:CC:DD:EE:FF'></td>"
    "<td><input name='%s' value='%s' size='15' placeholder='192.168.1.x'></td>"
    "</tr>",
    label, keyMac, macStr, keyIp, ipStr);
  strncat(st, row, maxLen - strlen(st) - 1);
}

void getPeerStatus(char *st, size_t maxLen) {
  char row[256];
  strncat(st, "<br><b>Peer Addresses</b> (<a href='/peersetup'>edit</a>)<br>", maxLen - strlen(st) - 1);

  auto fmt = [&](const char *name, const GambainoPeer &p) {
    char mac[18] = "", ip[16] = "";
    formatMacAddress(p.mac, mac, sizeof(mac));
    snprintf(ip, sizeof(ip), "%d.%d.%d.%d", p.ip[0], p.ip[1], p.ip[2], p.ip[3]);
    snprintf(row, sizeof(row), "&nbsp;&nbsp;%s: MAC=%s IP=%s<br>", name, mac, ip);
    strncat(st, row, maxLen - strlen(st) - 1);
  };

  fmt("BrewCore", peerBrewCore);
  fmt("SideKick", peerSideKick);
  for (int i = 0; i < MAXFMTS; i++) {
    bool hasData = false;
    for (int b = 0; b < 6; b++) if (peerPovotos[i].mac[b]) { hasData = true; break; }
    if (!hasData) continue;
    char label[16]; snprintf(label, sizeof(label), "Povoto[%d]", i+1);
    fmt(label, peerPovotos[i]);
  }
}

// ---- /peersetup page ----

static void handlePeerSetupGet(AsyncWebServerRequest *request) {
  String html;
  html.reserve(4096);
  html += "<!DOCTYPE html><html><head><meta charset='utf-8'>"
          "<title>Peer Setup</title>"
          "<style>body{font-family:sans-serif;padding:10px}"
          "table{border-collapse:collapse}td{padding:4px 8px}"
          "input{font-size:1em}button{margin:4px;padding:6px 14px;font-size:1em}</style>"
          "</head><body>";
  html += "<h2>Peer Addressing</h2>";
  html += "<form method='POST' action='/peersetup/update'>";
  html += "<table border='1'><tr><th>Node</th><th>MAC</th><th>IP</th></tr>";

  auto row = [&](const char *label, const char *keyMac, const char *keyIp, const GambainoPeer &p) {
    char mac[18] = "", ip[16] = "";
    formatMacAddress(p.mac, mac, sizeof(mac));
    snprintf(ip, sizeof(ip), "%d.%d.%d.%d", p.ip[0], p.ip[1], p.ip[2], p.ip[3]);
    html += "<tr><td><b>"; html += label; html += "</b></td>";
    html += "<td><input name='"; html += keyMac; html += "' value='"; html += mac;
    html += "' size='17' placeholder='AA:BB:CC:DD:EE:FF'></td>";
    html += "<td><input name='"; html += keyIp;  html += "' value='"; html += ip;
    html += "' size='15' placeholder='192.168.1.x'></td></tr>";
  };

  row("BrewCore", "bc_mac", "bc_ip", peerBrewCore);
  row("SideKick", "sk_mac", "sk_ip", peerSideKick);
  for (int i = 0; i < MAXFMTS; i++) {
    char label[16], km[12], ki[12];
    snprintf(label, sizeof(label), "Povoto[%d]", i+1);
    snprintf(km,    sizeof(km),    "p%d_mac", i);
    snprintf(ki,    sizeof(ki),    "p%d_ip",  i);
    row(label, km, ki, peerPovotos[i]);
  }

  html += "</table><br>";
  html += "<input type='submit' value='Save'>&nbsp;";
  html += "<button type='button' onclick='autoDetect()'>Auto Detect</button>";
  html += "<br><div id='msg'></div>";
  html += "<script>"
          "function autoDetect(){"
          "  document.getElementById('msg').innerHTML='Auto-detecting...';"
          "  fetch('/peersetup/autodetect').then(r=>r.text()).then(t=>{"
          "    document.getElementById('msg').innerHTML=t;"
          "    setTimeout(()=>location.reload(),3000);"
          "  });"
          "}"
          "</script>";
  html += "</form></body></html>";
  request->send(200, "text/html; charset=utf-8", html);
}

static bool parseIp(const String &s, uint8_t ip[4]) {
  int idx = 0, start = 0;
  for (int i = 0; i <= (int)s.length() && idx < 4; i++) {
    if (i == (int)s.length() || s[i] == '.') {
      ip[idx++] = (uint8_t)s.substring(start, i).toInt();
      start = i + 1;
    }
  }
  return idx == 4;
}

static void handlePeerSetupPost(AsyncWebServerRequest *request) {
  auto getStr = [&](const char *key) -> String {
    return request->hasParam(key, true) ? request->getParam(key, true)->value() : String("");
  };

  auto applyPeer = [&](const char *km, const char *ki, GambainoPeer &p) {
    String sm = getStr(km); String si = getStr(ki);
    if (sm.length() >= 17) parseMacAddress(sm, p.mac);
    if (si.length() >= 7)  parseIp(si, p.ip);
  };

  applyPeer("bc_mac", "bc_ip", peerBrewCore);
  applyPeer("sk_mac", "sk_ip", peerSideKick);
  for (int i = 0; i < MAXFMTS; i++) {
    char km[12], ki[12];
    snprintf(km, sizeof(km), "p%d_mac", i);
    snprintf(ki, sizeof(ki), "p%d_ip",  i);
    applyPeer(km, ki, peerPovotos[i]);
  }
  savePeers();
  request->redirect("/peersetup");
}

// Build the broadcast/reply payload:
// Format: "<type>,<povotoIndex>,<mac>,<ip>,<debug>"
// Example: "B,0,AA:BB:CC:DD:EE:FF,192.168.1.10,1"
static void buildOwnPeerPayload(char *buf, size_t bufLen) {
  uint8_t macBytes[6]; WiFi.macAddress(macBytes);
  char mac[18] = "";
  formatMacAddress(macBytes, mac, sizeof(mac));
  IPAddress ip = WiFi.localIP();
  snprintf(buf, bufLen, "%c,%d,%s,%d.%d.%d.%d,%d",
           ownPeerType, ownPeerIndex,
           mac,
           ip[0], ip[1], ip[2], ip[3],
           debugging ? 1 : 0);
}

static void applyPeerPayload(const char *payload, const uint8_t *senderMac) {
  // payload format: "<type>,<index>,<mac>,<ip>,<debug>"
  char buf[120];
  strncpy(buf, payload, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *tok = strtok(buf, ",");
  if (!tok) return;
  char ptype = tok[0];

  tok = strtok(NULL, ",");
  if (!tok) return;
  int pidx = atoi(tok);

  tok = strtok(NULL, ",");
  if (!tok) return;
  char macStr[18]; strncpy(macStr, tok, sizeof(macStr) - 1);

  tok = strtok(NULL, ",");
  if (!tok) return;
  char ipStr[16]; strncpy(ipStr, tok, sizeof(ipStr) - 1);

  tok = strtok(NULL, ",");
  if (!tok) return;
  bool remoteDebug = (atoi(tok) != 0);

  // Only accept replies matching our own debug mode
  if (remoteDebug != debugging) {
    Serial.printf("applyPeerPayload: REJEITADO (remoteDebug=%d != debugging=%d)\n",
                  (int)remoteDebug, (int)debugging);
    return;
  }

  GambainoPeer p;
  parseMacAddress(String(macStr), p.mac);
  parseIp(String(ipStr), p.ip);

  switch (ptype) {
    case PEERTYPE_BREWCORE: peerBrewCore = p; break;
    case PEERTYPE_SIDEKICK: peerSideKick = p; break;
    case PEERTYPE_POVOTO:
      if (pidx >= 1 && pidx <= MAXFMTS) peerPovotos[pidx - 1] = p;
      break;
  }
  savePeers();
  Serial.printf("Peer auto-detect: applied %c[%d] from payload\n", ptype, pidx);
}

// Broadcast MAC (ESP-NOW)
static const uint8_t ESPNOW_BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

void peerAutoDetect() {
  // Pre-register broadcast peer with correct ifidx so esp_now_add_peer succeeds.
  if (!esp_now_is_peer_exist(ESPNOW_BROADCAST_MAC)) {
    esp_now_peer_info_t bcast = {};
    memcpy(bcast.peer_addr, ESPNOW_BROADCAST_MAC, 6);
    bcast.channel = 0;            // 0 = follow current WiFi channel
    bcast.encrypt = false;
    bcast.ifidx   = WIFI_IF_STA;
    esp_err_t addErr = esp_now_add_peer(&bcast);
    Serial.printf("peerAutoDetect: add broadcast peer -> %d\n", (int)addErr);
  }
  char bcast[4]; snprintf(bcast, sizeof(bcast), "%d", debugging ? 1 : 0);
  // sendEspNow wraps in chunked format so processEspNowData on receivers can parse it.
  esp_err_t err = sendEspNow(ESPNOW_BROADCAST_MAC, 0, false,
                             (uint8_t)PEERBROADCASTPACKET, bcast);
  Serial.printf("peerAutoDetect: sent broadcast, err=%d payload='%s'\n", (int)err, bcast);
}

static void handlePeerAutoDetectRoute(AsyncWebServerRequest *request) {
  peerAutoDetect();
  request->send(200, "text/plain", "Broadcast sent. Waiting for replies...");
}

// Garante que o MAC está registrado como peer ESP-NOW (unicast exige isso antes de enviar).
static void ensureEspNowPeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 0;       // 0 = segue canal WiFi atual
  peer.encrypt = false;
  peer.ifidx   = WIFI_IF_STA;
  esp_err_t err = esp_now_add_peer(&peer);
  Serial.printf("ensureEspNowPeer %02X:%02X:%02X:%02X:%02X:%02X -> %d\n",
                mac[0],mac[1],mac[2],mac[3],mac[4],mac[5], (int)err);
}

void handlePeerEspNow(char packetType, const char *payload, const uint8_t *senderMac) {
  if (packetType == PEERBROADCASTPACKET) {
    // Someone is requesting auto-detect: check debug mode matches
    bool remoteDebug = (atoi(payload) != 0);
    if (remoteDebug != debugging) {
      Serial.printf("Peer auto-detect: IGNORADO (remoteDebug=%d != debugging=%d) meuIP=%s\n",
                    (int)remoteDebug, (int)debugging, WiFi.localIP().toString().c_str());
      return;
    }
    // Registra o MAC do solicitante para poder responder em unicast
    ensureEspNowPeer(senderMac);
    // Reply with our own info
    char replyPayload[120];
    buildOwnPeerPayload(replyPayload, sizeof(replyPayload));
    sendEspNow(senderMac, WiFi.channel(), false,
               (uint8_t)PEERREPLYPACKET, replyPayload);
    Serial.println("Peer auto-detect: sent reply to requester");
  }
  else if (packetType == PEERREPLYPACKET) {
    applyPeerPayload(payload, senderMac);
  }
}

// ---- ESP-NOW receive (BrewCore / Povoto) ----
// Non-capturing: peerRecvSenderMac_ is module-static so the lambda can be a plain function pointer.
static uint8_t peerRecvSenderMac_[6];
static EspNowPacketHandler extraEspNowHandler_ = nullptr;

void setExtraEspNowHandler(EspNowPacketHandler handler) {
  extraEspNowHandler_ = handler;
}
static void gambainoEspNowRecvCb(const uint8_t *mac, const uint8_t *data, int len) {
  if (!data || len < 7) return;
  if (mac) memcpy(peerRecvSenderMac_, mac, 6);
  else     memset(peerRecvSenderMac_, 0, 6);
  processEspNowData(data, len, [](char type, const char *payload) {
    if (type == PEERBROADCASTPACKET || type == PEERREPLYPACKET) {
      Serial.printf("[gambainoEspNowRecvCb] peer pkt type='%c'(0x%02X) from %02X:%02X:%02X:%02X:%02X:%02X\n",
                    (type >= 32 ? type : '?'), (uint8_t)type,
                    peerRecvSenderMac_[0], peerRecvSenderMac_[1], peerRecvSenderMac_[2],
                    peerRecvSenderMac_[3], peerRecvSenderMac_[4], peerRecvSenderMac_[5]);
      handlePeerEspNow(type, payload, peerRecvSenderMac_);
    } else if (extraEspNowHandler_) {
      extraEspNowHandler_(type, payload);
    }
  });
}

// Call once after ESP-NOW is initialized (after esp_now_init / GLogEspNowInit).
// NOT for SideKick — it has its own recv callback that already routes peer packets.
void initPeerEspNowReceive() {
  esp_err_t err = esp_now_register_recv_cb(gambainoEspNowRecvCb);
  Serial.printf("initPeerEspNowReceive: %s\n", err == ESP_OK ? "OK" : "FAILED");
}

void registerPeerSetupRoute() {
  // Sub-routes MUST be registered before the parent to prevent prefix-match swallowing.
  server.on("/peersetup/autodetect", HTTP_GET,  handlePeerAutoDetectRoute);
  server.on("/peersetup/update",    HTTP_POST, handlePeerSetupPost);
  server.on("/peersetup",           HTTP_GET,  handlePeerSetupGet);
}

// Marcadores (6 bytes cada: '\n' + 5 letras)
static const char kTagSntnl[] = "\nSntnl";
static const char kTagLogPk[] = "\nLogPk";
static const char kTagCmdPk[] = "\nCmdPk";
static const char kTagInstb[] = "\nInstb";
static const uint8_t kTagLen = 6;

void setupWiFi() {
  WiFi.disconnect();

  ESPSetup(NUMSSID,SSIDs,pwds);
}

void checkDebugMode() {
  if (wifiHasJustConnected()) {
    debugging = WiFi.localIP() != IPAddress(192,168,29,178) && 
                WiFi.localIP() != IPAddress(192,168,29,181) &&
                WiFi.localIP() != IPAddress(192,168,29,182);
    if (debugging) {
      Serial.println("Entering development mode");
      strcpy(datalogFolderNameInUse, "GambainoDebug");
    }
    else {
      Serial.println("Entering operational mode");
      strcpy(datalogFolderNameInUse, "Beers");
    }
    peerUpdateOwnAddress();  // update peer slot with real IP now that WiFi is up
  }
}


void verifyWiFiConnection() {
  /*
  static unsigned long int last = 0;
  if (MILLISDIFF(last,60000L)) {
    last = millis();
    if (WiFi.getMode()==WIFI_MODE_AP) {
      Serial.print("connectado: "); Serial.println(WiFi.softAPgetStationNum());
/ *      if (WiFi.softAPgetStationNum()==0) {
        ;Ser ial.println("procurando");
        if (hasAnySSID()) {
          //setupWiFi();
          ;Se rial.println("rodou");
        }
      }
* /
    }
    else if (wifiIsDisconnected() ) {
      if (timeWiFiInStatus()>30000) {
        sound_Beep();
        //setupWiFi();
      }
    }
    else { 
      if (!debugging && WiFi.SSID() != "Gambaino" && hasSSID("Gambaino")) {
        say("trying to setup wifi");
        //setupWiFi();
      }
    }
  }*/
}

static inline uint16_t crc16_ccitt_false_update(uint16_t crc, uint8_t data) {
  crc ^= (uint16_t)data << 8;
  for (uint8_t i = 0; i < 8; i++) {
    crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
  }
  return crc;
}

static inline uint16_t crc16_ccitt_false(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) crc = crc16_ccitt_false_update(crc, data[i]);
  return crc;
}

// Buffer estático para evitar stack overflow
static char sendBuffer[MAXPACKETSIZE + 1];
// Mutex para proteger contra chamadas concorrentes
static SemaphoreHandle_t serial2Mutex = NULL;
// Counter para detectar chamadas muito frequentes
static unsigned long lastSendTime = 0;
static int sendCount = 0;

void sendSerial2(const char packetType, const char *fmt, ...) {
  // Inicializa mutex se necessário
  if (serial2Mutex == NULL) {
    serial2Mutex = xSemaphoreCreateMutex();
    if (serial2Mutex == NULL) {
      Serial.println("ERROR: Failed to create Serial2 mutex");
      return;
    }
  }

  // Tenta obter o mutex (timeout de 100ms para evitar travamento)
  if (xSemaphoreTake(serial2Mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    Serial.println("WARNING: Serial2 mutex timeout");
    return;
  }

  // Proteção contra chamadas muito frequentes (anti-flood) - AGORA PROTEGIDO PELO MUTEX
  unsigned long now = millis();
  if (now - lastSendTime < 50) { // Mínimo 50ms entre chamadas
    sendCount++;
    if (sendCount > 10) {
      // Muitas chamadas em sequência, pode estar causando problemas
      Serial.print("WARNING: sendSerial2 flooding detected, skipping: ");
      Serial.println(fmt);
      xSemaphoreGive(serial2Mutex);
      return;
    }
  } else {
    sendCount = 0;
  }
  lastSendTime = now;

  // Verifica se há espaço no buffer de transmissão do Serial2
  if (Serial2.availableForWrite() < 50) {
    // Buffer quase cheio, libera mutex e sai para evitar blocking
    xSemaphoreGive(serial2Mutex);
    Serial.println("WARNING: Serial2 TX buffer full");
    return;
  }

  // Formata string no buffer estático
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(sendBuffer, sizeof(sendBuffer), fmt, args);
  va_end(args);

  // Verificações de segurança
  if (len <= 0 || len >= (int)sizeof(sendBuffer)) {
    xSemaphoreGive(serial2Mutex);
    Serial.println("ERROR: vsnprintf failed or buffer overflow");
    return;
  }

  uint16_t length = (uint16_t)len;
  if (length > MAXPACKETSIZE) {
    xSemaphoreGive(serial2Mutex);
    Serial.println("ERROR: Packet size exceeds MAXPACKETSIZE");
    return;
  }

  // Watchdog feed antes de operações que podem demorar
  esp_task_wdt_reset();

  // Tag
  switch (packetType) {
    case SENTINELPACKET:        Serial2.write(kTagSntnl); break;
    case LOGPACKET:             Serial2.write(kTagLogPk); break;
    case RCCOMMANDPACKET:       Serial2.write(kTagCmdPk); break;
    case INSTABILITYMSGPACKET:  Serial2.write(kTagInstb); break;
    default: 
      xSemaphoreGive(serial2Mutex);
      Serial.println("ERROR: Unknown packet type");
      return;
  }

  // Length (payload only)
  Serial2.write((uint8_t)(length >> 8));
  Serial2.write((uint8_t)(length & 0xFF));

  // Payload em chunks para evitar blocking longo
  const int CHUNK_SIZE = 64;
  for (int i = 0; i < length; i += CHUNK_SIZE) {
    int chunkLen = min(CHUNK_SIZE, length - i);
    Serial2.write((uint8_t *)(sendBuffer + i), chunkLen);
    
    // Yield entre chunks para não travar sistema
    if (i + CHUNK_SIZE < length) {
      yield();
      esp_task_wdt_reset();
    }
  }

  // CRC16 (over payload)
  uint16_t crc = crc16_ccitt_false((uint8_t*)sendBuffer, length);
  Serial2.write((uint8_t)(crc >> 8));      // CRC MSB
  Serial2.write((uint8_t)(crc & 0xFF));    // CRC LSB

  // Força envio imediato se possível
  Serial2.flush();

  // Libera mutex
  xSemaphoreGive(serial2Mutex);

  // Feed watchdog após operação completa
  esp_task_wdt_reset();
}



// returns letter when packet is complete. \0 if no packet available
char readSerial2(char *buf, int bufsize) {
  enum State : uint8_t { SEEK_TAG, READ_LEN1, READ_LEN2, READ_PAYLOAD, READ_CRC1, READ_CRC2 };
  static State st = SEEK_TAG;

  static char win[kTagLen];
  static uint8_t winFill = 0;

  static uint16_t length = 0;
  static uint16_t bytesRead = 0;
  static uint32_t startTime = 0;
  static char outType = NOPACKET;

  static uint16_t crcCalc = 0xFFFF;
  static uint16_t crcRecv = 0;

  auto winMatches = [&](const char *tag, const char code) -> char {
    for (uint8_t i = 0; i < kTagLen; i++) {
      if (win[i] != tag[i]) return NOPACKET; 
    }
    return code;
  };

  // Packet timeout: reset everything
  if (st != SEEK_TAG && (millis() - startTime > SERIAL2TIMEOUT)) {
    st = SEEK_TAG;
    winFill = 0;
    length = 0;
    bytesRead = 0;
    outType = NOPACKET;
    crcCalc = 0xFFFF;
    crcRecv = 0;
    return NOPACKET;
  }

  int budget = 128;

  while (budget-- > 0 && Serial2.available()) {
    uint8_t b = (uint8_t)Serial2.read();

    switch (st) {
      case SEEK_TAG: {
        if (winFill < kTagLen) win[winFill++] = (char)b;
        else {
          for (uint8_t i = 0; i < kTagLen - 1; i++) win[i] = win[i + 1];
          win[kTagLen - 1] = (char)b;
        }

        if (winFill == kTagLen) {
          char code;
          if (!(code = winMatches(kTagSntnl, SENTINELPACKET)))
            if (!(code = winMatches(kTagLogPk, LOGPACKET)))
              if (!(code = winMatches(kTagCmdPk, RCCOMMANDPACKET)))
                code = winMatches(kTagInstb, INSTABILITYMSGPACKET);

          if (code) {
            outType = code;
            st = READ_LEN1;
            startTime = millis();
            winFill = 0;
          }
        }
        break;
      }

      case READ_LEN1:
        length = ((uint16_t)b) << 8;
        st = READ_LEN2;
        break;

      case READ_LEN2:
        length |= b;

        // check length
        if (length == 0 || length > MAXPACKETSIZE) {
          st = SEEK_TAG; winFill = 0; outType = NOPACKET;
          break;
        }

        // If overflows buffer, discard packet
        if (length >= (uint16_t)bufsize) {
          st = SEEK_TAG; winFill = 0; outType = NOPACKET;
          break;
        }

        bytesRead = 0;
        crcCalc = 0xFFFF;
        crcRecv = 0;
        st = READ_PAYLOAD;
        break;

      case READ_PAYLOAD:
        buf[bytesRead++] = (char)b;
        crcCalc = crc16_ccitt_false_update(crcCalc, b);

        if (bytesRead >= length) {
          buf[bytesRead] = '\0';
          st = READ_CRC1;
        }
        break;

      case READ_CRC1:
        crcRecv = ((uint16_t)b) << 8;
        st = READ_CRC2;
        break;

      case READ_CRC2:
        crcRecv |= b;

        // checks CRC
        if (crcRecv == crcCalc) {
          char outTypeCopy = outType;
          outType = NOPACKET;
          st = SEEK_TAG;
          winFill = 0;
          return outTypeCopy;
        } else {
          // invalid CRC
          st = SEEK_TAG;
          winFill = 0;
          outType = NOPACKET;
          buf[0] = '\0';
          return NOPACKET;
        }
    }
  }

  return NOPACKET;
}

bool parseMacAddress(const String &value, uint8_t mac[6]) {
  int index = 0;
  int len = value.length();
  int i = 0;
  while (i < len && index < 6) {
    while (i < len && (value[i] == ':' || value[i] == '-' || value[i] == ' ')) {
      i++;
    }
    if (i + 1 >= len) return false;
    char c1 = value[i++];
    char c2 = value[i++];
    auto hexToNibble = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    int n1 = hexToNibble(c1);
    int n2 = hexToNibble(c2);
    if (n1 < 0 || n2 < 0) return false;
    mac[index++] = (uint8_t)((n1 << 4) | n2);
  }
  return index == 6;
}

void formatMacAddress(const uint8_t mac[6], char *out, size_t outSize) {
  if (!out || outSize < 18) return;
  snprintf(out, outSize, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// =========== ESP-NOW CHUNKED PROTOCOL ===========

#define ESPNOW_MAX_CHUNK 200

static uint32_t espnowChunksRcv_  = 0;
static uint32_t espnowDrops_      = 0;
static uint32_t espnowResetCount_ = 0;
static char     espnowBuf_[MAXPACKETSIZE + 1];
static uint16_t espnowExpTotal_   = 0;
static uint16_t espnowNextSeq_    = 0;
static uint16_t espnowOffset_     = 0;
static uint8_t  espnowPktType_    = (uint8_t)NOPACKET;

static const char *espNowErrToString(esp_err_t err) {
  switch (err) {
    case ESP_OK:                  return "ESP_OK";
    case ESP_ERR_ESPNOW_NOT_INIT: return "ESP_ERR_ESPNOW_NOT_INIT";
    case ESP_ERR_ESPNOW_ARG:      return "ESP_ERR_ESPNOW_ARG";
    case ESP_ERR_ESPNOW_INTERNAL: return "ESP_ERR_ESPNOW_INTERNAL";
    case ESP_ERR_ESPNOW_NO_MEM:   return "ESP_ERR_ESPNOW_NO_MEM";
    case ESP_ERR_ESPNOW_NOT_FOUND:return "ESP_ERR_ESPNOW_NOT_FOUND";
    case ESP_ERR_ESPNOW_IF:       return "ESP_ERR_ESPNOW_IF";
    default:                      return "ESP_ERR_UNKNOWN";
  }
}

void getEspNowStats(uint32_t *chunks, uint32_t *drops, uint32_t *resets) {
  if (chunks) *chunks = espnowChunksRcv_;
  if (drops)  *drops  = espnowDrops_;
  if (resets) *resets = espnowResetCount_;
}

esp_err_t sendEspNow(const uint8_t *mac, uint8_t channel, bool encrypt, uint8_t packetType, const char *payload) {
  if (!mac || !payload) return ESP_ERR_ESPNOW_ARG;

  bool allZeroMac = true;
  for (int i = 0; i < 6; i++) {
    if (mac[i] != 0) {
      allZeroMac = false;
      break;
    }
  }
  if (allZeroMac) return ESP_ERR_ESPNOW_ARG;

  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = channel;
    peerInfo.encrypt = encrypt;
    peerInfo.ifidx   = WIFI_IF_STA;
    esp_err_t addErr = esp_now_add_peer(&peerInfo);
    if (addErr != ESP_OK && addErr != ESP_ERR_ESPNOW_EXIST) {
      Serial.printf("ESP-NOW add peer failed: %d\n", (int)addErr);
      return addErr;
    }
  }

  const size_t   maxData = ESPNOW_MAX_CHUNK;
  const size_t   len     = strlen(payload);
  const uint16_t total   = (uint16_t)((len + maxData - 1) / maxData);
  if (total == 0) return ESP_OK;

  esp_err_t lastErr = ESP_OK;

  for (uint16_t seq = 0; seq < total; ++seq) {
    size_t offset   = seq * maxData;
    size_t chunkLen = len - offset;
    if (chunkLen > maxData) chunkLen = maxData;
    struct __attribute__((packed)) {
      uint8_t  packetType;
      uint16_t seq;
      uint16_t total;
      uint16_t len;
      char     data[ESPNOW_MAX_CHUNK];
    } chunk;
    chunk.packetType = packetType;
    chunk.seq        = seq;
    chunk.total      = total;
    chunk.len        = (uint16_t)chunkLen;
    memcpy(chunk.data, payload + offset, chunkLen);
    esp_err_t sendErr = esp_now_send(mac, (uint8_t*)&chunk,
                                     sizeof(chunk.packetType) + sizeof(chunk.seq) +
                                     sizeof(chunk.total)      + sizeof(chunk.len) + chunkLen);
    if (sendErr != ESP_OK) {
      char macStr[18];
      snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      wl_status_t wifiStatus = WiFi.status();
      Serial.printf("ESP-NOW send failed: err=%d/%s pkt=%u seq=%u/%u chunk=%u payload=%u peer=%s cfgCh=%u wifiCh=%u wifiStatus=%d mode=%d heap=%u maxAlloc=%u peerExists=%d\n",
                    (int)sendErr,
                    espNowErrToString(sendErr),
                    (unsigned)packetType,
                    (unsigned)seq,
                    (unsigned)total,
                    (unsigned)chunkLen,
                    (unsigned)len,
                    macStr,
                    (unsigned)channel,
                    (unsigned)WiFi.channel(),
                    (int)wifiStatus,
                    (int)WiFi.getMode(),
                    (unsigned)ESP.getFreeHeap(),
                    (unsigned)ESP.getMaxAllocHeap(),
                    esp_now_is_peer_exist(mac) ? 1 : 0);

      lastErr = sendErr;
      break;
    }
    if (seq + 1 < total) {
      yield();
    }
  }

  return lastErr;
}

bool processEspNowData(const uint8_t *data, int len, EspNowPacketHandler handler) {
  // header: packetType(1) + seq(2) + total(2) + chunkLen(2) = 7 bytes
  if (len < 7) return false;

  const uint8_t *p = data;
  uint8_t  packetType;
  uint16_t seq, total, chunkLen;
  memcpy(&packetType, p,     sizeof(packetType));
  memcpy(&seq,        p + 1, sizeof(seq));
  memcpy(&total,      p + 3, sizeof(total));
  memcpy(&chunkLen,   p + 5, sizeof(chunkLen));

  if (chunkLen > ESPNOW_MAX_CHUNK || (int)(7 + chunkLen) > len) return false;

  espnowChunksRcv_++;

  if (seq == 0) {
    if (espnowExpTotal_ > 0 && espnowNextSeq_ > 0) {
      espnowDrops_++;
      espnowResetCount_++;
      Serial.printf("ESP-NOW drop: incomplete reset (had %u/%u chunks)\n", espnowNextSeq_, espnowExpTotal_);
    }
    espnowExpTotal_  = total;
    espnowNextSeq_   = 0;
    espnowOffset_    = 0;
    espnowPktType_   = packetType;
  }

  if (total == 0 || packetType != espnowPktType_ || seq != espnowNextSeq_) {
    espnowDrops_++;
    Serial.printf("ESP-NOW drop: type=%u seq=%u expected=%u total=%u\n", packetType, seq, espnowNextSeq_, total);
    espnowExpTotal_  = 0;
    espnowNextSeq_   = 0;
    espnowOffset_    = 0;
    espnowPktType_   = (uint8_t)NOPACKET;
    espnowResetCount_++;
    return false;
  }

  if (espnowOffset_ + chunkLen >= MAXPACKETSIZE) {
    espnowDrops_++;
    Serial.println("ESP-NOW drop: buffer overflow");
    espnowExpTotal_  = 0;
    espnowNextSeq_   = 0;
    espnowOffset_    = 0;
    espnowPktType_   = (uint8_t)NOPACKET;
    espnowResetCount_++;
    return false;
  }

  memcpy(espnowBuf_ + espnowOffset_, p + 7, chunkLen);
  espnowOffset_ += chunkLen;
  espnowNextSeq_++;

  if (espnowNextSeq_ >= espnowExpTotal_) {
    espnowBuf_[espnowOffset_] = '\0';
    if (handler) handler((char)espnowPktType_, espnowBuf_);
    espnowExpTotal_  = 0;
    espnowNextSeq_   = 0;
    espnowOffset_    = 0;
    espnowPktType_   = (uint8_t)NOPACKET;
    return true;
  }

  return false;
}
