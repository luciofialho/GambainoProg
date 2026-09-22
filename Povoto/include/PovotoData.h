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

// Counters data
struct CountersData_t {
  uint32_t totalReliefCount;
  double totalMolsEjected;
  double CO2InSolution;
  // Integral of net produced CO2, normalized by the beer volume at each event.
  double CO2MolsProducedPerLiter;
  float headSpaceVolume;
  float correctionPlato;
  long int totalChillTime; // seconds
  long int totalHeatTime;  // seconds
} __attribute__((packed));

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
bool readCountersDataFromEEPROM();
bool writeCountersDataToNIV();
void maybePersistCountersData();
void updateCountersTimes(bool chillOn, bool heatOn);


#endif // POVOTODATA_H
