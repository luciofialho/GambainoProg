#ifndef GambainoCommon_h
#define GambainoCommon_h

#include <stdarg.h>
#include <string.h>
#include <Arduino.h>
#include <esp_now.h>
#include "IOTK_ESPAsyncServer.h"




#define MAXPACKETSIZE 2048
#define NOPACKET             '\0'
#define SENTINELPACKET       'S'
#define LOGPACKET            'L'
#define RCCOMMANDPACKET      'C'
#define INSTABILITYMSGPACKET 'M'
#define PEERBROADCASTPACKET  'P'   // broadcast: auto-detect request
#define PEERREPLYPACKET      'R'   // unicast reply with own peer info
#define TRANSFERSTARTPACKET  'T'   // BrewCore→Povoto: "batchNum,inoculationTemp"
#define TRANSFERENDPACKET    'E'   // BrewCore→Povoto: transfer complete
#define FERMERTEMPPACKET     'F'   // Povoto→BrewCore: "povotoIndex,temperature"
#define ENVTEMPPACKET        'V'   // BrewCore→broadcast: "environmentTemp"

#define SERIAL2_SPEED 1000000
#define SERIAL2TIMEOUT 100

#define NUMSSID 3
#define DEFAULTSISID 1

#define STRBUFFERSIZE 180

extern bool debugging;

// =================== PEER ADDRESSING ====================
#define MAXFMTS 10

// Peer type identifiers (sent in PEERREPLYPACKET payload)
#define PEERTYPE_BREWCORE  'B'
#define PEERTYPE_SIDEKICK  'K'
#define PEERTYPE_POVOTO    'P'

struct GambainoPeer {
  uint8_t mac[6];
  uint8_t ip[4];   // IPv4: 0.0.0.0 means unknown/empty
};

extern GambainoPeer peerBrewCore;
extern GambainoPeer peerSideKick;
extern GambainoPeer peerPovotos[MAXFMTS];

// Call once after WiFi is up to register own MAC+IP in the correct peer slot.
// peerType: PEERTYPE_BREWCORE / PEERTYPE_SIDEKICK / PEERTYPE_POVOTO
// povotoIndex: only used when peerType == PEERTYPE_POVOTO (0-based)
void registerOwnPeer(char peerType, int povotoIndex = 0);
void peerUpdateOwnAddress();   // call after WiFi gets IP (done automatically via checkDebugMode)

// Load/save all peer data from/to NVS ("Peers" namespace)
void loadPeers();
void savePeers();

// Append peer addressing HTML to st (call from getStatus functions)
void getPeerStatus(char *st, size_t maxLen);

// Register /peersetup route on the AsyncWebServer (call from UI_SETUP / setup)
void registerPeerSetupRoute();

// Call once after ESP-NOW is ready: esp_now_init (BrewCore) or GLogEspNowInit (Povoto).
// NOT needed for SideKick — it has its own recv callback.
void initPeerEspNowReceive();

// Call from each project's ESP-NOW receive handler.
// Handles PEERBROADCASTPACKET (auto-detect request) and
// PEERREPLYPACKET (auto-detect response from a remote node).
// senderMac: 6-byte MAC of the sender.
void handlePeerEspNow(char packetType, const char *payload, const uint8_t *senderMac);

// Initiate an auto-detect broadcast (called from the web page button)
void peerAutoDetect();

// Callback invocado após savePeers() — permite re-aplicar dependentes (ex: GLogEspNowSetPeer).
typedef void (*PeersSavedCallback)();
void setPeersSavedCallback(PeersSavedCallback cb);

extern const char *SSIDs[NUMSSID];
extern const char *pwds [NUMSSID];

extern unsigned long longestCycle,
                     averageCycle,
                     numCycles;

extern bool safeMode;

//#define BIGBUFFERSIZE 4000
//extern char bigBuffer[BIGBUFFERSIZE+1];


extern char datalogFolderNameInUse[20];


void setupWiFi();
void checkDebugMode();
void verifyWiFiConnection();

bool parseMacAddress(const String &value, uint8_t mac[6]);
void formatMacAddress(const uint8_t mac[6], char *out, size_t outSize);

// ESP-NOW chunked protocol (same packet concept as Serial2, but wireless)
typedef void (*EspNowPacketHandler)(char packetType, const char *payload);
// Register a project-specific handler for non-peer ESP-NOW packets.
// Called with (packetType, payload) for any packet type that is not
// PEERBROADCASTPACKET or PEERREPLYPACKET.
void setExtraEspNowHandler(EspNowPacketHandler handler);
esp_err_t sendEspNow(const uint8_t *mac, uint8_t channel, bool encrypt, uint8_t packetType, const char *payload);
bool     processEspNowData(const uint8_t *data, int len, EspNowPacketHandler handler);
void     getEspNowStats(uint32_t *chunks, uint32_t *drops, uint32_t *resets);

void sendSerial2(const char packetType, const char *fmt, ...); 

char readSerial2(char *buf, int bufsize);

static inline int analogReadStable(int pin) {
  analogRead(pin);
  int a = analogRead(pin);
  int b = analogRead(pin);
  return (a + b) >> 1;
}

#endif