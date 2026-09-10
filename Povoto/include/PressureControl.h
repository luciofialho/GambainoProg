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
uint16_t getVolumeDeterminationIteration();
float getVolumeDeterminationCalculatedSoFar();
void applyDumpWindowHeadspaceRecalc(float headspaceBeforeL, float pressureBeforeBar, float pressureAfterBar);
void requestDerivedStateRestoreFromCounters();
float CO2Mass(float mols=-1);
float CO2DissolvedMols(float pressureBar, float sg, float temperatureC, float volumeL);
boolean inPressureNoiseWindow();
void getReliefsPerHourText(char *out, size_t outSize);
void getReliefsPerHourCompactText(char *out, size_t outSize);
float getReliefsPerHourValue();
float SGToApparentPlato(float sg);

extern float beerVolume;
extern float beerSG;
extern float beerABV;
extern float sgPointGenerationTime;
extern bool pressureSensorUnstable;
extern float currentReading;
extern float headSpaceCO2Mols;
extern float adjustedPressureAfterRelief;
extern float pressureOnReliefExtrap; // extrapolates for relief time window
extern float pressureAfterRelief;
extern unsigned long pressureAfterReliefMillis;
extern float pressureReachedTarget;
extern unsigned long int pressureReachedTargetMillis;
extern float pressureDropFactor;

#endif // PRESSURECONTROL_H