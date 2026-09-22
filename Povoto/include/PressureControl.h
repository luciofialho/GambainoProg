#ifndef PRESSURECONTROL_H
#define PRESSURECONTROL_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

void pressureRelief(bool fromVolumeDetermination);
void pressureControl();
struct DissolvedCO2LogData {
  const char *mode;
  const char *criteriaState;
  const char *withReliefsState;
  const char *withoutReliefsState;
  unsigned long criteriaElapsedMillis;
  unsigned long confirmationMillis;
  unsigned long withReliefsElapsedMillis;
  unsigned long withoutReliefsElapsedMillis;
  float calculationPressure;
  float equilibriumMols;
  float previousPressure;
  float reliefIntervalSeconds;
  float sinceLastReliefSeconds;
};
DissolvedCO2LogData getDissolvedCO2LogData();
char *getPressureControlStatus(char *st);
void handlePressureHistoryCSV(AsyncWebServerRequest *request);
void handlePressureDumpCSV(AsyncWebServerRequest *request);
bool startSpeedCalibration(bool venting, char *reason, size_t reasonSize);
String getSpeedCalibrationStatus();
bool isSpeedCalibrationActive();
void drawCalibrationStatus();
void handleSpeedCalibrationCSV(AsyncWebServerRequest *request);
void handleExpansionResidualFit(AsyncWebServerRequest *request);
bool startVolumeDetermination(bool fast, char *reason, size_t reasonSize);
bool isVolumeDeterminationActive();
uint16_t getVolumeDeterminationIteration();
float getVolumeDeterminationCalculatedSoFar();
void applyDumpWindowHeadspaceRecalc(float headspaceBeforeL, float pressureBeforeBar, float pressureAfterBar);
void requestDerivedStateRestoreFromCounters();
void resetCO2MolsProducedPerLiterTracking();
float CO2Mass(float mols=-1);
// g CO2/L/day; presentation clamps negative evolution to zero.
float getBeerCO2EvolutionGramsPerLiterPerDay();
float CO2DissolvedMols(float pressureBar, float sg, float temperatureC, float volumeL);
boolean inPressureNoiseWindow();
void getReliefsPerHourText(char *out, size_t outSize);
void getReliefsPerHourCompactText(char *out, size_t outSize);
float getReliefsPerHourValue();
float SGToApparentPlato(float sg);
float SGToRealPlato(float sg);

extern float beerVolume;
extern float beerSG;
extern float beerABV;
extern float sgPointGenerationTime;
extern bool pressureSensorUnstable;
extern float currentReading;
extern float headSpaceCO2Mols;
// Signed rate over up to 71 samples; averages each end from nine samples onward.
// Zero until five samples are collected; sampling starts after two minutes uptime.
extern float beerCO2EvolutionGramsPerLiterPerDay;
extern float adjustedPressureAfterRelief;
extern float pressureOnReliefExtrap; // extrapolates for relief time window
extern float pressureAfterRelief;
extern unsigned long pressureAfterReliefMillis;
extern float pressureReachedTarget;
extern unsigned long int pressureReachedTargetMillis;
extern float pressureDropFactor;

#endif // PRESSURECONTROL_H
