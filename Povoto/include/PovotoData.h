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
constexpr uint32_t PVT_NVS_SCHEMA_VERSION = 2;

// FMT data struct
struct FMTData_t {
  uint32_t nvsSchemaVersion;
  byte PovotoNum;
  float FMTVolume;
  float FMTReliefVolume;
  float FMTOnTimeDuringBrew;
  float FMTOFFTimeDuringBrew;
  float FMTAltitude;
  float FMTEffectiveVentingExponent;
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
  uint32_t checksum;
} __attribute__((packed));


extern FMTData_t FMTData;

struct UserConfigurationData_t {
  int screensaverTime;
  int keypadPin;
  int displayBrightness; // 1..10
  uint32_t checksum;
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
  float startPressure;
  float startTemperature;
  uint32_t checksum;
} __attribute__((packed));

extern BatchData_t BatchData;

// Set points
struct SetPointData_t {
  byte mode;  // MODE_OFF=0, MODE_BREWING_TRANSFERING=1, MODE_FERMENTING=2, MODE_CONDITIONING=3
  float setPointTemp;
  float setPointSlowTemp;
  float setPointPressure;
  uint32_t setPointTempSetEpoch;
  uint32_t setPointPressureSetEpoch;
  uint32_t checksum;
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
  uint32_t checksum;
} __attribute__((packed));

extern ControlData_t ControlData;

// Counters data
struct CountersData_t {
  uint32_t totalReliefCount;
  double totalMolsEjected;
  double CO2InSolution;
  float headSpaceVolume;
  float correctionPlato;
  float SGAttenuation;
  long int totalChillTime; // seconds
  long int totalHeatTime;  // seconds
  uint32_t checksum;
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
