#ifndef GambainoWiFi_h
#define GambainoWiFi_h

// Non-blocking Wi-Fi manager for BrewCore and SideKick (replaces setupWiFi()).
//
// - SSID/password live in NVS (namespace "gmb_wifi"), configured at /wifi.
// - Without a station connection for GAMBAINOWIFI_AP_FALLBACK_MS, a setup AP
//   "<apName>-XXXX" (captive portal) is opened; the station keeps retrying the
//   configured network in the background and the AP closes once it connects.
// - The same AsyncWebServer serves the AP, so the device UI stays reachable at
//   http://192.168.4.1/ while the AP is up (contingency operation).
// - While NVS has no credentials, the legacy SSIDs[] list is tried and the first
//   network that connects is migrated to NVS.

#include <Arduino.h>

#define GAMBAINOWIFI_ATTEMPT_MS        15000UL   // time given to each station connection attempt
#define GAMBAINOWIFI_AP_FALLBACK_MS    60000UL   // offline this long -> open the setup AP
#define GAMBAINOWIFI_AP_RETRY_MS       30000UL   // AP mode: interval between station attempts
#define GAMBAINOWIFI_AP_CLOSE_GRACE_MS 120000UL  // reconnected: max wait for AP clients to leave
#define GAMBAINOWIFI_AP_FORCED_MS      600000UL  // AP opened on request stays at least this long

// Starts the Wi-Fi stack, web server/OTA (ESPSetupServerOnly) and the /wifi routes.
// apPassword: at least 8 characters.
// uiPath: when not null, the setup page links to the device UI (BrewCore contingency).
void gambainoWiFiBegin(const char *apName, const char *apPassword, const char *uiPath = nullptr);

// Call from loop(). Never blocks.
void gambainoWiFiProcess();

// Safe from any task (web handlers, command interpreters): executed by gambainoWiFiProcess().
void gambainoWiFiReconnect();
void gambainoWiFiStartAP();

bool gambainoWiFiConnected();
bool gambainoWiFiApActive();

// Appends an HTML status block (for /getstatus).
void gambainoWiFiStatus(char *st, size_t maxLen);

#endif
