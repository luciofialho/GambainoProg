#ifndef PRESSURECONTROL_H
#define PRESSURECONTROL_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Size of the buffer passed to getPressureControlStatus().
constexpr size_t PRESSURE_STATUS_SIZE = 3072;

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
void applyDumpWindowHeadspaceRecalc(float headspaceBeforeL, float pressureBeforeBar, float pressureAfterBar,
                                    unsigned long startMillis, unsigned long endMillis);

// [DAILY-HS] 24-hour headspace average (docs/spec_headspace_24h.md).
struct DailyHeadspaceLogData {
  float ema;        // headspaceFiltered
  float daily;      // mean of the hourly means (NAN = no hours)
  uint8_t hours;    // hours of the last 24 with samples
  const char *state; // "valid", "hold" or "ema"
};
DailyHeadspaceLogData getDailyHeadspaceLogData(); // re-evaluates when NTP is valid
// Shifts the stored hours (and the held value) by deltaL; persists.
void rebaseDailyHeadspace(float deltaL, const char *reason);
// Empties the hours and the held value, speeds up the EMA; persists.
void clearDailyHeadspace(const char *reason);
// New batch: empties the hours and the cached result; not persisted.
void resetDailyHeadspaceTracking();

// [DAILY-HS] Last dump, for one Cold log row.
struct DumpLogData {
  bool pending;
  float pressureBeforeBar;  // P1
  float pressureAfterBar;   // P2
  unsigned long startMillis;
  unsigned long endMillis;
  float deltaH;             // rebase applied (NAN = not applied)
};
bool takeDumpLogData(DumpLogData &data);
void requestDerivedStateRestoreFromCounters();
void resetHeadspaceFilterTracking();
void resetCO2MolsProducedPerLiterTracking();
float CO2Mass(float mols=-1);
float getTotalCO2Mols();
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
extern bool pressureSensorConnected;
extern bool debugPressureOverride;
// Connected sensor (or debugging) and a finite reading.
bool pressureReadingValid();
void markPressureSetpointChanged(bool slow);
const char *getPressStateLabel();
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
