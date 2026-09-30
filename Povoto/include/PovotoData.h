#ifndef POVOTODATA_H
#define POVOTODATA_H

#include <arduino.h>
#include <ESPAsyncWebServer.h>

// Fermenter operating modes
#define MODE_OFF                 0
#define MODE_BREWING_TRANSFERING 1
#define MODE_FERMENTING          2
#define MODE_CONDITIONING        3

// Helper macros to stringify defines at compile time
#define XSTR(x) #x
#define TOSTR(x) XSTR(x)

constexpr int DEFAULT_DATA_LOG_INTERVAL_SECONDS = 60;
inline bool isValidDataLogIntervalSeconds(int seconds) {
  return seconds == 30 || seconds == 60 || seconds == 120 || seconds == 300 || seconds == 600;
}


struct CoolingCyclePoint_t {
  float onMinutes;
  float offMinutes;
} __attribute__((packed));

struct HeaterCycle_t {
  bool enabled;
  float onMinutes;
  float offMinutes;
} __attribute__((packed));

// Increment to clear/rewrite the active Povoto namespaces after loading them.
constexpr uint32_t PVT_NVS_SCHEMA_VERSION = 3;

// FMT data struct
struct FMTData_t {
  uint32_t nvsSchemaVersion;
  byte PovotoNum;
  float FMTVolume;
  float FMTReliefVolume;
  float FMTAltitude;
  int dataLogIntervalSeconds;
  float FMTEffectiveVentingExponent;
  // t(R,P) = -ln(R) / (a - b*P), with P in gauge bar and R as a fraction.
  float expansionTimeCoefficientA;
  float expansionTimeCoefficientB;
  float targetResidualAfterReliefPercent;
  float liquidMassInGasVentingPercent;
  // F(P) = c*P^2 + d*P + e: residual fraction after 20 s of venting at
  // gauge pressure P (bar).
  float ventingResidualCoefficientA;
  float ventingResidualCoefficientB;
  float ventingResidualCoefficientC;
  float pressure0Current;
  float pressure1Bar;
  float pressure1Current;
  float pressure2Bar;
  float pressure2Current;
  float maximumPressure;
  int   co2TransferTime;
  int   nucleationWindow;
  HeaterCycle_t heater;
  CoolingCyclePoint_t coolingCycle[3]; // Fixed rows: 20, 10 and 0 degrees C.
} __attribute__((packed));


extern FMTData_t FMTData;

struct UserConfigurationData_t {
  int screensaverTime;
  int keypadPin;
  int displayBrightness; // 1..10
} __attribute__((packed));
extern UserConfigurationData_t UserConfigurationData;
bool readUserConfigurationDataFromEEPROM();
bool writeUserConfigurationDataToNIV();
extern float Patm;


// Batch data
struct BatchData_t {
  char batchName[32]; 
  uint16_t batchNumber;
  char batchDate[11];
  float batchOG;
  float addedPlato;
  float initialBeerVolume;
  float startPressure;
  float startTemperature;
} __attribute__((packed));

extern BatchData_t BatchData;

// Set points
struct SetPointData_t {
  byte mode;  // MODE_OFF=0, MODE_BREWING_TRANSFERING=1, MODE_FERMENTING=2, MODE_CONDITIONING=3
  float setPointTemp;
  float setPointSlowTemp;
  float setPointSlowTempSpeed;
  float setPointPressure;
  float setPointSlowPressure;
  float setPointSlowPressureSpeed;
} __attribute__((packed));

extern SetPointData_t SetPointData;

// control 
struct ControlData_t {
  float temperature;
  float pressure;
  bool chillerSwitch;
  bool heaterSwitch;
  bool transferValve;
  byte chillerOverride;
  byte heaterOverride;
  byte transferOverride;
  byte reliefOverride;
} __attribute__((packed));

extern ControlData_t ControlData;

// [DAILY-HS] 24-hour headspace average: one bin per hour (hourId = local NTP
// epoch / 3600), each hour weighted equally.
constexpr int DAILY_HS_BINS = 24;
constexpr int DAILY_HS_MIN_HOURS = 18;

struct DailyHeadspaceBin_t {
  uint32_t hourId;
  uint16_t count;
  float sum;     // sum of the headspace measurements of that hour (L)
} __attribute__((packed));

struct DailyHeadspace_t {
  DailyHeadspaceBin_t bins[DAILY_HS_BINS];
  float heldValue; // last valid daily value (NAN = none)
} __attribute__((packed));

// Counters data
struct CountersData_t {
  uint32_t totalReliefCount;
  double totalMolsEjected;
  double CO2InSolution;
  // Integral of net produced CO2, normalized by the beer volume at each event.
  double CO2MolsProducedPerLiter;
  float headSpaceVolume;
  // Beer volume removed through completed manual Dump tasks (L).
  float dumpedVolume;
  float correctionPlato;
  long int totalChillTime; // seconds
  long int totalHeatTime;  // seconds
  // Dissolved-CO2 state, restored after reboot (docs/dissolved-co2.md):
  // 0 = half-life, 1 = equilibrium (immediate), 2 = initial, 3 = half-life armed.
  uint8_t co2DissolvedMode;
  // Temperature stability phase (TEMP_STATE_*) and the local NTP epoch
  // (UTC-3) at which it became stable (0 = not yet stable).
  uint8_t tempState;
  uint32_t tempStableSince;
  // Same for pressure (TEMP_STATE_* values). It becomes stable inside the
  // relief cycle of the final target (see PressureControl.cpp).
  uint8_t pressState;
  uint32_t pressStableSince;
  // [DAILY-HS] Hourly bins and held value of the 24-hour headspace average.
  DailyHeadspace_t dailyHs;
  // Last gCO2/L/d from a mature window and its local NTP epoch, reported
  // after a reboot while the new window is short (docs/gco2-rate.md).
  float co2RateHeld;
  uint32_t co2RateHeldAt;
  // Local NTP epoch at which the half-life was armed by added fermentables
  // (0 = not armed, or armed before NTP).
  uint32_t co2ArmedAt;
} __attribute__((packed));

#define TEMP_STATE_STABLE          0
#define TEMP_STATE_CHANGING_DIRECT 1
#define TEMP_STATE_CHANGING_SLOW   2
// Was stable, then left the tolerance band; becomes STABLE again with a new time.
#define TEMP_STATE_UNSTABLE        3

extern CountersData_t CountersData;

// Function prototypes
void povotoDataInit();
bool resetPovotoDataToFactoryDefaults();

bool readFMTDataFromEEPROM();
bool writeFMTDataToNIV();


bool readBatchDataFromEEPROM();
bool writeBatchDataToNIV();

bool readSetPointDataFromEEPROM();
bool writeSetPointDataToNIV();

void updatePatmFromFMTAltitude();

void resetCountersForNewBatch();
// Conditioning: the fermenter becomes a plain refrigerator (docs/conditioning.md).
void enterConditioning();
void resumeFermentingFromConditioning();
bool readCountersDataFromEEPROM();
bool writeCountersDataToNIV();
bool writeTempStabilityToNIV();
bool writePressureStabilityToNIV();
bool writeDailyHeadspaceToNIV(); // [DAILY-HS]
void resetDailyHeadspace();      // [DAILY-HS] all bins empty, no held value
void maybePersistCountersData();
void updateCountersTimes(bool chillOn, bool heatOn);


#endif // POVOTODATA_H
