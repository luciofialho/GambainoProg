#ifndef POVOTOPAGES_H
#define POVOTOPAGES_H

// Web interface handlers
void handleMainMenu(AsyncWebServerRequest *request);

void handleFMTDataPage(AsyncWebServerRequest *request);
void handleFMTDataUpdate(AsyncWebServerRequest *request);

void handleCalibrationDataPage(AsyncWebServerRequest *request);
void handleCalibrationDataUpdate(AsyncWebServerRequest *request);

void handleBatchDataPage(AsyncWebServerRequest *request);
void handleBatchDataUpdate(AsyncWebServerRequest *request);

void handleSetPointDataPage(AsyncWebServerRequest *request);
void handleSetPointDataUpdate(AsyncWebServerRequest *request);

void handleControlDataPage(AsyncWebServerRequest *request);
void handleControlDataUpdate(AsyncWebServerRequest *request);
void handleStartVolume(AsyncWebServerRequest *request);
void handleControlAuto(AsyncWebServerRequest *request);
void handleControlReliefOnce(AsyncWebServerRequest *request);
void handleDebugParamsPage(AsyncWebServerRequest *request);
void handleDebugParamsUpdate(AsyncWebServerRequest *request);

#endif
