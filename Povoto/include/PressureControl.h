#ifndef PRESSURECONTROL_H
#define PRESSURECONTROL_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

void pressureRelief(bool fromVolumeDetermination);
void pressureControl();
char *getPressureControlStatus(char *st);
void handlePressureHistoryCSV(AsyncWebServerRequest *request);
void handlePressureDumpCSV(AsyncWebServerRequest *request);
bool startVolumeDetermination(char *reason, size_t reasonSize);
bool isVolumeDeterminationActive();
float CO2Mass(float mols=-1);
float CO2DissolvedMols(float pressureBar, float sg, float temperatureC, float volumeL);
boolean inPressureNoiseWindow();

extern float beerVolume;
extern float beerSG;
extern float beerABV;
extern float beerPlato;
extern float sgPointGenerationTime;
extern bool pressureSensorUnstable;
extern float currentReading;
extern float headSpaceCO2Mols;

#endif // PRESSURECONTROL_H