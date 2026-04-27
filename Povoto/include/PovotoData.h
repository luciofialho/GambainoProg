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


// FMT data struct
struct FMTData_t {
  byte PovotoNum;
  float FMTVolume;
  float FMTReliefVolume;
  float FMTMinActiveTime;
  float FMTMaxActiveTime;
  float FMTMinOffTime;
  float FMTalpha;
  float FMTbeta;
  float FMTHeaterOnTime;
  float FMTHeaterOffTime;
  float FMTOnTimeDuringBrew;
  float FMTOFFTimeDuringBrew;
  float FMTAltitude;
  int   FMTScreensaverTime;
  int   FMTKeypadPin;   // PIN de 4 dígitos para desbloquear o teclado no display
  uint32_t checksum;
} __attribute__((packed));


extern FMTData_t FMTData;
extern float Patm;

// Calibration data
struct CalibrationData_t {
  float pressure0Current;
  float pressure1Bar;
  float pressure1Current;
  float pressure2Bar;
  float pressure2Current;
  float maximumPressure;
  int   co2TransferTime;
  int   nucleationWindow;
  uint32_t checksum;
} __attribute__((packed));

extern CalibrationData_t CalibrationData;

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
  bool reliefValve;
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
  float totalMolsEjected;
  float CO2InSolution;
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

bool readFMTDataFromEEPROM();
void writeFMTDataToNIV();

bool readCalibrationDataFromEEPROM();
void writeCalibrationDataToNIV();

bool readBatchDataFromEEPROM();
void writeBatchDataToNIV();

bool readSetPointDataFromEEPROM();
void writeSetPointDataToNIV();

void updatePatmFromFMTAltitude();

void resetCountersForNewBatch();
bool readCountersDataFromEEPROM();
void writeCountersDataToNIV();
void maybePersistCountersData();
void updateCountersTimes(bool chillOn, bool heatOn);


#endif // POVOTODATA_H
