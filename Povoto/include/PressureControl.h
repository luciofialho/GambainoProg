#ifndef PRESSURECONTROL_H
#define PRESSURECONTROL_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Size of the buffer passed to getPressureControlStatus(). Worst case of the
// section (all branches, widest values) is about 3.1 KB; keep a margin.
constexpr size_t PRESSURE_STATUS_SIZE = 4096;

void pressureRelief(bool fromVolumeDetermination);
void pressureControl();
struct DissolvedCO2LogData {
  const char *mode;
  const char *criteriaState;
  float calculationPressure;
  float equilibriumMols;
  float gasRate; // gas-phase CO2 rate, g/L/d (NAN = no decision)
};
DissolvedCO2LogData getDissolvedCO2LogData();
// Dissolved-CO2 state (docs/dissolved-co2.md).
void notifyFermentablesAdded();
void resetCO2DissolvedStateForNewBatch();
void scaleDissolvedCO2ForBeerVolume(float volumeBefore, float volumeAfter);
uint8_t getCO2DissolvedState(); // CountersData.co2DissolvedMode values
const char *getCO2DissolvedStateLabel(uint8_t state);
bool setCO2DissolvedStateManually(uint8_t state); // Counters page
void resumeCO2AccountingAfterConditioning();
// Saves the gCO2 window and the 30-min Henry samples (every 10 min and at an OTA start).
void saveCO2Buffers();
char *getPressureControlStatus(char *st);
void handlePressureHistoryCSV(AsyncWebServerRequest *request);
void handlePressureDumpCSV(AsyncWebServerRequest *request);
// Gas-transfer speed tests (docs/calibration-speed-evaluation.md).
constexpr uint8_t SPEED_TEST_EXPANSION = 0;
constexpr uint8_t SPEED_TEST_VENTING = 1;
constexpr uint8_t SPEED_TEST_K_FLOOR = 2;
bool startSpeedCalibration(uint8_t test, bool co2, char *reason, size_t reasonSize);
// Results of the last run of each test (air values and CO2 conversions).
struct SpeedCalibrationResults {
  // Expansion: R = exp(-(a - b*P)*t) + H*exp(-t/tau) + floor/P.
  bool expansionAvailable;
  bool expansionGasCO2;
  float floorRP, floorR;        // floor as R*P1 (bar) and as R
  float heatH, heatTau;         // tank heating
  bool flowAvailable;
  float flowA, flowB;           // test gas
  float flowACO2, flowBCO2;     // converted (opening-time coefficients)
  float rmse;
  uint8_t points;
  float openSeconds[3];         // fermentation opening time at 0.8, 1.5, 1.9 bar
  float kHeat[3];               // heating k at those times
  float kAir;                   // at 1.5 bar
  float kCO2Estimated;          // 1 + 1.3*(kAir - 1)
  float headspaceExponent;      // empty fermenter, diagnostics
  // k & floor.
  bool kFloorAvailable;
  bool kFloorGasCO2;
  float kFloorPressure, kFloorRP, kFloorR, kFloorK, kFloorSeconds;
  // Venting: F(P) = c*P^2 + d*P + e.
  bool ventingAvailable;
  bool ventingGasCO2;
  float ventingAir[3];          // c, d, e of the test gas
  float ventingCO2[3];          // converted
  float ventingRmse;
  uint8_t ventingPoints;
};
SpeedCalibrationResults getSpeedCalibrationResults();
String getSpeedCalibrationStatus();
bool isSpeedCalibrationActive();
void drawCalibrationStatus();
// Line 0..2 of the calibration panel; empty while the panel is hidden.
const char *getCalibrationDisplayLine(uint8_t index);
void handleSpeedCalibrationCSV(AsyncWebServerRequest *request);
// fast: the fermentation's expansion time; co2: the empty fermenter was purged
// with CO2 (k and Tref of CO2 instead of air).
bool startVolumeDetermination(bool fast, bool co2, char *reason, size_t reasonSize);
// k calibration of the last fast volume test against FMTVolume (empty
// fermenter); docs/expansion-tank-k.md.
struct VolumeKCalibration {
  bool available;   // fast test finished with per-relief k values
  bool co2;         // test gas
  float kMedian;    // value to save: median of the per-relief k
  float kSpread;    // standard deviation of the per-relief k
  float kOverall;   // from the cumulative factor, for comparison
  uint16_t count;
  float fermenterC; // mean temperatures of the test
  float ambientC;
  float volume;     // volume returned with the current k
};
VolumeKCalibration getVolumeKCalibration();
bool isVolumeDeterminationActive();
uint16_t getVolumeDeterminationIteration();
float getVolumeDeterminationCalculatedSoFar();
// Result of a dump recalculation, for the Task log.
struct DumpRecalcResult {
  float pressureAfterIsoBar; // P2 after the polytropic correction (NAN = not computed)
  float deltaH;              // headspace change applied, L (NAN = not applied)
};
DumpRecalcResult applyDumpWindowHeadspaceRecalc(float headspaceBeforeL, float pressureBeforeBar,
                                                float pressureAfterBar);

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

void requestDerivedStateRestoreFromCounters();
void resetHeadspaceFilterTracking();
void resetCO2MolsProducedPerLiterTracking();
float CO2Mass(float mols=-1);
float getTotalCO2Mols();
// g CO2/L/day; presentation clamps negative evolution to zero.
float getBeerCO2EvolutionGramsPerLiterPerDay();
// Rate for the automatic rules: NAN unless the window has 30 min, and in a
// transition unless the beer releases CO2 (docs/automatic-actions.md).
float getRuleCO2EvolutionGramsPerLiterPerDay();
// After a pressure/temperature change, until the rate settles (docs/gco2-rate.md).
bool co2RateInTransition();
// In a transition: the last stable gCO2/L/d and its local NTP epoch (NAN/0 = none).
float getCO2TransitionStableRate(uint32_t *epoch);
// Signed; after a reboot, the saved rate while the new window is short
// (docs/gco2-rate.md). Automatic rules use beerCO2EvolutionGramsPerLiterPerDay.
float getReportedCO2EvolutionGramsPerLiterPerDay();
const char *getCO2EvolutionSource(); // "calculated", "held" or "transition"
float CO2DissolvedMols(float pressureBar, float sg, float temperatureC, float volumeL);
boolean inPressureNoiseWindow();
void getReliefsPerHourText(char *out, size_t outSize);
// "gCO2 [x.x/(L.d)]" in Fermenting, "g CO2" otherwise (TFT and dashboards).
void getCO2RateLabel(char *out, size_t outSize);
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

#endif // PRESSURECONTROL_H
