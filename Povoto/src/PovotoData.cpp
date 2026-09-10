#include <arduino.h>
#include <math.h>
#include <stddef.h>
#include <Preferences.h>
#include "PovotoCommon.h"
#include "PovotoData.h"
#include "PressureControl.h"

Preferences preferences;

// FMT data
#define FMTDATAKEY "FMTData"  



FMTData_t FMTData = {
  .PovotoNum = 0,
  .FMTVolume = 120.0,
  .FMTReliefVolume = 2.1,
  .FMTMinActiveTime = 10,
  .FMTMaxActiveTime = 25,
  .FMTMinOffTime = 8,
  .FMTalpha = 12.5,
  .FMTbeta = 9.04,
  .FMTHeaterOnTime = 0.5,
  .FMTHeaterOffTime = 2,
  .FMTOnTimeDuringBrew = 5.0,
  .FMTOFFTimeDuringBrew = 5.0,
  .FMTAltitude = 0.0,
  .FMTEffectiveVentingExponent = 1.100f,
  .checksum = 0
};

// Separate NVS key (maximum 15 characters).
#define USERCONFIGDATAKEY "UserConfigData"
UserConfigurationData_t UserConfigurationData = {120, 6350, 10, 0};

// Original packed layout, retained only to migrate existing installations.
struct LegacyFMTData_t {
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
  float FMTEffectiveVentingExponent;
  uint32_t checksum;
} __attribute__((packed));

float Patm = 1.0f;

// Calibration data
#define CALIBRATIONDATAKEY "CalibrationData" 

CalibrationData_t CalibrationData = {
  .pressure0Current = 4.5,
  .pressure1Bar = 0.5,
  .pressure1Current = 8.5,
  .pressure2Bar = 1.0,
  .pressure2Current = 12.5,
  .maximumPressure = 2.3f,
  .co2TransferTime = 180,
  .nucleationWindow = 5,
  .checksum = 0
};

// batch data
#define BATCHDATAKEY "BatchData"

BatchData_t BatchData = {
  .batchName = {'n','a','m','e','l','e','s','s',0},
  .batchNumber = 0,
  .batchDate = {'0','0','/','0','0','/','0','0','0','0',0},
  .batchOG = 1.050f,
  .addedPlato = 0.0f,
  .startPressure = 0.0f,
  .startTemperature = 0.0f,
  .checksum = 0
};

// set points
#define SETPOINTDATAKEY "SetPointData" 

SetPointData_t SetPointData = {
  .mode = 0,
  .setPointTemp = 18.0,
  .setPointSlowTemp = NOTaTEMP,
  .setPointPressure = 0.2,
  .setPointTempSetEpoch = 0,
  .setPointPressureSetEpoch = 0,
  .checksum = 0
};

// control data
#define CONTROLDATAKEY "ControlData"

ControlData_t ControlData = {
  .temperature = 0.0,
  .pressure = 0.0,
  .chillerSwitch = false,
  .heaterSwitch = false,
  .transferValve = false,
  .chillerOverride = 0,
  .heaterOverride = 0,
  .transferOverride = 0,
  .reliefOverride = 0,
  .checksum = 0
};

// counters data
#define COUNTERSDATAKEY "CountersData"

CountersData_t CountersData = {
  .totalReliefCount = 0,
  .totalMolsEjected = 0.0f,
  .CO2InSolution = 0.0f,
  .headSpaceVolume = 0.0f,
  .correctionPlato = 0.0f,
  .SGAttenuation = 0.0f,
  .totalChillTime = 0,
  .totalHeatTime = 0,
  .checksum = 0
};



// =============


bool readNIVData(const char *key, void * start, void *end) {
  // read NIV data from IOTK NVS, returns true if successful and ckecksum ok

  preferences.begin("povoto", true);
  size_t dataSize = (char*)end - (char*)start + 1;
  
  uint8_t* tempBuffer = (uint8_t*)malloc(dataSize);
  if (!tempBuffer) {
    preferences.end();
    return false;
  }
  
  size_t bytesRead = preferences.getBytes(key, tempBuffer, dataSize);
  preferences.end();
    
  if (bytesRead != dataSize) {
    free(tempBuffer);
    return false;
  }
    
  // Calculate checksum of the data (excluding the last 4 bytes which should be the stored checksum)
  uint32_t calculatedChecksum = 0;
  for (size_t i = 0; i < dataSize - sizeof(uint32_t); i++) {
    calculatedChecksum += tempBuffer[i] * (i + 1);
  }
    
  // Get stored checksum from the end of the buffer
  uint32_t storedChecksum = *((uint32_t*)(&tempBuffer[dataSize - sizeof(uint32_t)]));
    
  if (calculatedChecksum == storedChecksum) {
    memcpy(start, tempBuffer, dataSize);
    free(tempBuffer);
    return true;
  }
  
  free(tempBuffer);
  return false;
}

void writeNIVData(const char *key, void * start, void *end) {
  // write NIV data to IOTK NVS

    
  size_t dataSize = (char*)end - (char*)start + 1;
  uint8_t* dataPtr = (uint8_t*)start;
  
  // Calculate checksum of the data (excluding the last 4 bytes)
  uint32_t calculatedChecksum = 0;
  for (size_t i = 0; i < dataSize - sizeof(uint32_t); i++) {
    calculatedChecksum += dataPtr[i] * (i + 1);
  }
    
  uint32_t *checksumInMemory = (uint32_t*)((uint8_t*)start + dataSize - sizeof(uint32_t));
  
  Serial.printf("writeNIVData: key=%s, start=%p, end=%p, dataSize=%d, checksum=%08X\n", 
                key, start, end, dataSize, calculatedChecksum);
  
  if (*checksumInMemory == calculatedChecksum) {
    Serial.println("Checksum unchanged, skipping write");
    return;
  }
  else {
    *checksumInMemory = calculatedChecksum;
    preferences.begin("povoto", false);
    preferences.putBytes(key, start, dataSize);
    preferences.end();
    Serial.println("Data written to NVS");
  }
} 

bool readFMTDataFromEEPROM() {
  if (readNIVData(FMTDATAKEY, &FMTData, ((byte*)&FMTData) + sizeof(FMTData_t) - 1)) {
    return true;
  }
  LegacyFMTData_t legacy = {};
  if (!readNIVData(FMTDATAKEY, &legacy, ((byte*)&legacy) + sizeof(legacy) - 1)) {
    return false;
  }
  memcpy(&FMTData, &legacy, offsetof(LegacyFMTData_t, FMTScreensaverTime));
  FMTData.FMTEffectiveVentingExponent = legacy.FMTEffectiveVentingExponent;
  FMTData.checksum = 0;
  // Save the extracted preferences before replacing the legacy FMT block.
  if (!readUserConfigurationDataFromEEPROM()) {
    UserConfigurationData.screensaverTime = legacy.FMTScreensaverTime >= 10 ? legacy.FMTScreensaverTime : 120;
    UserConfigurationData.keypadPin = legacy.FMTKeypadPin >= 0 && legacy.FMTKeypadPin <= 9999 ? legacy.FMTKeypadPin : 6350;
    writeUserConfigurationDataToNIV();
  }
  writeFMTDataToNIV();
  return true;
}

void writeFMTDataToNIV() {
  writeNIVData(FMTDATAKEY, &FMTData, ((byte*)&FMTData) + sizeof(FMTData_t) - 1);
} 

bool readUserConfigurationDataFromEEPROM() {
  UserConfigurationData_t saved = {};
  if (!readNIVData(USERCONFIGDATAKEY, &saved, ((byte*)&saved) + sizeof(saved) - 1) ||
      saved.screensaverTime < 10 || saved.keypadPin < 0 || saved.keypadPin > 9999 ||
      saved.displayBrightness < 1 || saved.displayBrightness > 10) {
    return false;
  }
  UserConfigurationData = saved;
  return true;
}

void writeUserConfigurationDataToNIV() {
  writeNIVData(USERCONFIGDATAKEY, &UserConfigurationData,
      ((byte*)&UserConfigurationData) + sizeof(UserConfigurationData) - 1);
}

void updatePatmFromFMTAltitude() {
  float altitude = FMTData.FMTAltitude;
  if (altitude < 0.0f) {
    altitude = 0.0f;
  }

  Patm = 1.01325f * powf(1.0f - (2.25577e-5f * altitude), 5.25588f);
}

bool readBatchDataFromEEPROM() {
  return readNIVData(BATCHDATAKEY, &BatchData, ((byte*)&BatchData) + sizeof(BatchData_t) - 1);
}

void writeBatchDataToNIV() {
  writeNIVData(BATCHDATAKEY, &BatchData, ((byte*)&BatchData) + sizeof(BatchData_t) - 1);
}

bool readSetPointDataFromEEPROM() {
  return readNIVData(SETPOINTDATAKEY, &SetPointData, ((byte*)&SetPointData) + sizeof(SetPointData_t) - 1);
}

void writeSetPointDataToNIV() {
  writeNIVData(SETPOINTDATAKEY, &SetPointData, ((byte*)&SetPointData) + sizeof(SetPointData_t) - 1);
}

bool readCalibrationDataFromEEPROM() {
  return readNIVData(CALIBRATIONDATAKEY, &CalibrationData, ((byte*)&CalibrationData) + sizeof(CalibrationData_t) - 1);
}

void writeCalibrationDataToNIV() {
  writeNIVData(CALIBRATIONDATAKEY, &CalibrationData, ((byte*)&CalibrationData) + sizeof(CalibrationData_t) - 1);
}

void povotoDataInit() {
  preferences.begin("povoto", false); 


  if (!readFMTDataFromEEPROM())
    Serial.println("FMT data load failed, using defaults");

  if (!readUserConfigurationDataFromEEPROM())
    Serial.println("User configuration load failed, using defaults");

  if (!readCalibrationDataFromEEPROM())
    Serial.println("Calibration data load failed, using defaults");

  if (!readBatchDataFromEEPROM())
    Serial.println("Batch data load failed, using defaults");

  if (!readSetPointDataFromEEPROM())
    Serial.println("Set point data load failed, using defaults");

  if (!readCountersDataFromEEPROM())
    Serial.println("Counters data load failed, using defaults");
  else
    requestDerivedStateRestoreFromCounters();

  updatePatmFromFMTAltitude();
}

void resetCountersForNewBatch() {
  //zera totalbubblecount 
  CountersData.totalReliefCount = 0;
  CountersData.totalMolsEjected = 0.0f;
  CountersData.CO2InSolution = 0.0f;
  CountersData.headSpaceVolume = 0.0f;
  CountersData.SGAttenuation = 0.0f;
  CountersData.totalChillTime = 0;
  CountersData.totalHeatTime = 0;

  writeCountersDataToNIV();
}

bool readCountersDataFromEEPROM() {
  return readNIVData(COUNTERSDATAKEY, &CountersData, ((byte*)&CountersData) + sizeof(CountersData_t) - 1);
}

void writeCountersDataToNIV() {
  writeNIVData(COUNTERSDATAKEY, &CountersData, ((byte*)&CountersData) + sizeof(CountersData_t) - 1);
}

void maybePersistCountersData() {
  static long int lastSavedTotalTime = -1;
  static uint32_t lastSavedReliefCount = 0;

  if (lastSavedTotalTime < 0) {
    lastSavedTotalTime = CountersData.totalChillTime + CountersData.totalHeatTime;
    lastSavedReliefCount = CountersData.totalReliefCount;
    return;
  }

  long int totalTime = CountersData.totalChillTime + CountersData.totalHeatTime;
  if ((totalTime - lastSavedTotalTime) >= 900 ||
      (CountersData.totalReliefCount - lastSavedReliefCount) >= 5) {
    writeCountersDataToNIV();
    lastSavedTotalTime = totalTime;
    lastSavedReliefCount = CountersData.totalReliefCount;
  }
}

void updateCountersTimes(bool chillOn, bool heatOn) {
  static unsigned long int lastCountersUpdate = 0;
  static unsigned long int chillMsRemainder = 0;
  static unsigned long int heatMsRemainder = 0;

  if (lastCountersUpdate == 0) {
    lastCountersUpdate = millis();
    return;
  }

  unsigned long int now = millis();
  unsigned long int delta = now - lastCountersUpdate;
  lastCountersUpdate = now;

  if (chillOn) {
    chillMsRemainder += delta;
    if (chillMsRemainder >= 1000) {
      CountersData.totalChillTime += (long int)(chillMsRemainder / 1000);
      chillMsRemainder = chillMsRemainder % 1000;
    }
  }

  if (heatOn) {
    heatMsRemainder += delta;
    if (heatMsRemainder >= 1000) {
      CountersData.totalHeatTime += (long int)(heatMsRemainder / 1000);
      heatMsRemainder = heatMsRemainder % 1000;
    }
  }
}
