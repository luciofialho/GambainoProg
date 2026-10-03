#ifndef POVOTO_WIFI_H
#define POVOTO_WIFI_H

#include <Arduino.h>

class AsyncWebServerRequest;

void povotoWiFiInit();
void povotoWiFiProcess();
void povotoWiFiRegisterRoutes();
void povotoWiFiReconnect();
void povotoWiFiStartConfiguration();
bool povotoWiFiConfigurationActive();
bool povotoWiFiDrawTft();
bool povotoWiFiHandleTouch(uint16_t x, uint16_t y);
bool povotoWiFiHandleStatusTouch(uint16_t x, uint16_t y);
void povotoWiFiNotifyTouchReleased();
void povotoWiFiDrawStatusIndicator();
// Same status as the TFT indicator: signal bars (1-4) when connected,
// otherwise 0 and the status text.
int povotoWiFiStatusIndicator(char *text, size_t size);

void handlePovotoWiFiPage(AsyncWebServerRequest *request);
void handlePovotoWiFiSave(AsyncWebServerRequest *request);
void handlePovotoWiFiReconfigure(AsyncWebServerRequest *request);

#endif
