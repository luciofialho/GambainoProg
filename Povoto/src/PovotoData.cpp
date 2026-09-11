#include <arduino.h>
#include <math.h>
#include <Preferences.h>
#include "PovotoCommon.h"
#include "PovotoData.h"
#include "PressureControl.h"

// FMT data

FMTData_t FMTData = {
  .nvsSchemaVersion = 0, // Missing version must trigger the first rewrite.
  .PovotoNum = 0,
  .FMTVolume = 120.0,
  .FMTReliefVolume = 2.0,
  .FMTOnTimeDuringBrew = 5.0,
  .FMTOFFTimeDuringBrew = 5.0,
  .FMTAltitude = 0.0,
  .FMTEffectiveVentingExponent = 1.2900f,
  .pressure0Current = 4.5,
  .pressure1Bar = 0.5,
  .pressure1Current = 8.5,
  .pressure2Bar = 0.0,
  .pressure2Current = 0.0,
  .maximumPressure = 2.3f,
  .co2TransferTime = 34,
  .nucleationWindow = 5,
  .heater = {true, 0.5f, 2.0f},
  .coolingCycle = {{30.0f, 15.0f}, {20.0f, 10.0f}, {3.0f, 3.0f}},
  .checksum = 0
};

UserConfigurationData_t UserConfigurationData = {120, 6350, 10, 0};

float Patm = 1.0f;


// batch data

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

CountersData_t CountersData = {
  .totalReliefCount = 0,
  .totalMolsEjected = 0.0,
  .CO2InSolution = 0.0,
  .headSpaceVolume = 0.0f,
  .correctionPlato = 0.0f,
  .SGAttenuation = 0.0f,
  .totalChillTime = 0,
  .totalHeatTime = 0,
  .checksum = 0
};

// =============


void updatePatmFromFMTAltitude() {
  float altitude = FMTData.FMTAltitude;
  if (altitude < 0.0f) {
    altitude = 0.0f;
  }

  Patm = 1.01325f * powf(1.0f - (2.25577e-5f * altitude), 5.25588f);
}

// Direct per-field NVS access. Schema changes trigger a startup rewrite.
static bool validCycleMinutes(float value) {
  return isfinite(value) && value >= 0.01f && value <= 1440.0f;
}

static const FMTData_t defaultFMTData = FMTData;
bool readFMTDataFromEEPROM() {
  FMTData = defaultFMTData;
  Preferences store;
  if (!store.begin("pvt_settings", true)) return false;
  FMTData.pressure0Current = store.getFloat("p0Current", defaultFMTData.pressure0Current);
  if (!isfinite(FMTData.pressure0Current)) FMTData.pressure0Current = defaultFMTData.pressure0Current;
  FMTData.pressure1Bar = store.getFloat("p1Bar", defaultFMTData.pressure1Bar);
  if (!isfinite(FMTData.pressure1Bar)) FMTData.pressure1Bar = defaultFMTData.pressure1Bar;
  FMTData.pressure1Current = store.getFloat("p1Current", defaultFMTData.pressure1Current);
  if (!isfinite(FMTData.pressure1Current)) FMTData.pressure1Current = defaultFMTData.pressure1Current;
  FMTData.pressure2Bar = store.getFloat("p2Bar", defaultFMTData.pressure2Bar);
  if (!isfinite(FMTData.pressure2Bar)) FMTData.pressure2Bar = defaultFMTData.pressure2Bar;
  FMTData.pressure2Current = store.getFloat("p2Current", defaultFMTData.pressure2Current);
  if (!isfinite(FMTData.pressure2Current)) FMTData.pressure2Current = defaultFMTData.pressure2Current;
  FMTData.maximumPressure = store.getFloat("maxPressure", defaultFMTData.maximumPressure);
  if (!isfinite(FMTData.maximumPressure)) FMTData.maximumPressure = defaultFMTData.maximumPressure;
  FMTData.co2TransferTime = store.getInt("co2TransferTime", defaultFMTData.co2TransferTime);
  FMTData.nucleationWindow = store.getInt("nucleation", defaultFMTData.nucleationWindow);
  FMTData.nvsSchemaVersion = store.getUInt("schemaVersion", 0);
  FMTData.PovotoNum = store.getUChar("number", defaultFMTData.PovotoNum);
  FMTData.FMTVolume = store.getFloat("volume", defaultFMTData.FMTVolume);
  if (!isfinite(FMTData.FMTVolume)) FMTData.FMTVolume = defaultFMTData.FMTVolume;
  FMTData.FMTReliefVolume = store.getFloat("reliefVolume", defaultFMTData.FMTReliefVolume);
  if (!isfinite(FMTData.FMTReliefVolume)) FMTData.FMTReliefVolume = defaultFMTData.FMTReliefVolume;
  FMTData.FMTOnTimeDuringBrew = store.getFloat("brewOn", defaultFMTData.FMTOnTimeDuringBrew);
  if (!isfinite(FMTData.FMTOnTimeDuringBrew)) FMTData.FMTOnTimeDuringBrew = defaultFMTData.FMTOnTimeDuringBrew;
  FMTData.FMTOFFTimeDuringBrew = store.getFloat("brewOff", defaultFMTData.FMTOFFTimeDuringBrew);
  if (!isfinite(FMTData.FMTOFFTimeDuringBrew)) FMTData.FMTOFFTimeDuringBrew = defaultFMTData.FMTOFFTimeDuringBrew;
  FMTData.FMTAltitude = store.getFloat("altitude", defaultFMTData.FMTAltitude);
  if (!isfinite(FMTData.FMTAltitude)) FMTData.FMTAltitude = defaultFMTData.FMTAltitude;
  FMTData.FMTEffectiveVentingExponent = store.getFloat("ventExponent", defaultFMTData.FMTEffectiveVentingExponent);
  if (!isfinite(FMTData.FMTEffectiveVentingExponent)) FMTData.FMTEffectiveVentingExponent = defaultFMTData.FMTEffectiveVentingExponent;
  if (store.getBytesLength("cooling") == sizeof(FMTData.coolingCycle))
    store.getBytes("cooling", &FMTData.coolingCycle, sizeof(FMTData.coolingCycle));
  for (const auto &point : FMTData.coolingCycle) {
    if (!validCycleMinutes(point.onMinutes) || !validCycleMinutes(point.offMinutes)) {
      memcpy(FMTData.coolingCycle, defaultFMTData.coolingCycle, sizeof(FMTData.coolingCycle));
      break;
    }
  }
  uint8_t heaterBytes[sizeof(FMTData.heater)];
  if (store.getBytesLength("heater") == sizeof(heaterBytes) &&
      store.getBytes("heater", heaterBytes, sizeof(heaterBytes)) == sizeof(heaterBytes) &&
      heaterBytes[0] <= 1) {
    memcpy(&FMTData.heater, heaterBytes, sizeof(heaterBytes));
    if (!validCycleMinutes(FMTData.heater.onMinutes) ||
        !validCycleMinutes(FMTData.heater.offMinutes))
      FMTData.heater = defaultFMTData.heater;
  }
  store.end();
  return true;
}

bool writeFMTDataToNIV() {
  Preferences store;
  if (!store.begin("pvt_settings", false)) {
    Serial.println("NVS: cannot open pvt_settings");
    return false;
  }
  bool saved = true;
  saved = (store.putUChar("number", FMTData.PovotoNum) == sizeof(FMTData.PovotoNum)) && saved;
  saved = (store.putFloat("volume", FMTData.FMTVolume) == sizeof(FMTData.FMTVolume)) && saved;
  saved = (store.putFloat("reliefVolume", FMTData.FMTReliefVolume) == sizeof(FMTData.FMTReliefVolume)) && saved;
  saved = (store.putFloat("brewOn", FMTData.FMTOnTimeDuringBrew) == sizeof(FMTData.FMTOnTimeDuringBrew)) && saved;
  saved = (store.putFloat("brewOff", FMTData.FMTOFFTimeDuringBrew) == sizeof(FMTData.FMTOFFTimeDuringBrew)) && saved;
  saved = (store.putFloat("altitude", FMTData.FMTAltitude) == sizeof(FMTData.FMTAltitude)) && saved;
  saved = (store.putFloat("ventExponent", FMTData.FMTEffectiveVentingExponent) == sizeof(FMTData.FMTEffectiveVentingExponent)) && saved;
  saved = (store.putBytes("cooling", &FMTData.coolingCycle, sizeof(FMTData.coolingCycle)) == sizeof(FMTData.coolingCycle)) && saved;
  saved = (store.putBytes("heater", &FMTData.heater, sizeof(FMTData.heater)) == sizeof(FMTData.heater)) && saved;
  saved = (store.putFloat("p0Current", FMTData.pressure0Current) == sizeof(FMTData.pressure0Current)) && saved;
  saved = (store.putFloat("p1Bar", FMTData.pressure1Bar) == sizeof(FMTData.pressure1Bar)) && saved;
  saved = (store.putFloat("p1Current", FMTData.pressure1Current) == sizeof(FMTData.pressure1Current)) && saved;
  saved = (store.putFloat("p2Bar", FMTData.pressure2Bar) == sizeof(FMTData.pressure2Bar)) && saved;
  saved = (store.putFloat("p2Current", FMTData.pressure2Current) == sizeof(FMTData.pressure2Current)) && saved;
  saved = (store.putFloat("maxPressure", FMTData.maximumPressure) == sizeof(FMTData.maximumPressure)) && saved;
  saved = (store.putInt("co2TransferTime", FMTData.co2TransferTime) == sizeof(FMTData.co2TransferTime)) && saved;
  saved = (store.putInt("nucleation", FMTData.nucleationWindow) == sizeof(FMTData.nucleationWindow)) && saved;
  // Write the completion marker only after all settings were saved.
  if (saved)
    saved = store.putUInt("schemaVersion", FMTData.nvsSchemaVersion) == sizeof(FMTData.nvsSchemaVersion);
  store.end();
  if (!saved) Serial.println("NVS: FMTData save incomplete");
  return saved;
}

static const UserConfigurationData_t defaultUserConfigurationData = UserConfigurationData;
bool readUserConfigurationDataFromEEPROM() {
  UserConfigurationData = defaultUserConfigurationData;
  Preferences store;
  if (!store.begin("pvt_user", true)) return false;
  UserConfigurationData.screensaverTime = store.getInt("screensaver", defaultUserConfigurationData.screensaverTime);
  UserConfigurationData.keypadPin = store.getInt("keypadPin", defaultUserConfigurationData.keypadPin);
  UserConfigurationData.displayBrightness = store.getInt("brightness", defaultUserConfigurationData.displayBrightness);
  if (UserConfigurationData.screensaverTime < 10) UserConfigurationData.screensaverTime = defaultUserConfigurationData.screensaverTime;
  if (UserConfigurationData.keypadPin < 0 || UserConfigurationData.keypadPin > 9999) UserConfigurationData.keypadPin = defaultUserConfigurationData.keypadPin;
  if (UserConfigurationData.displayBrightness < 1 || UserConfigurationData.displayBrightness > 10) UserConfigurationData.displayBrightness = defaultUserConfigurationData.displayBrightness;
  store.end();
  return true;
}

bool writeUserConfigurationDataToNIV() {
  Preferences store;
  if (!store.begin("pvt_user", false)) {
    Serial.println("NVS: cannot open pvt_user");
    return false;
  }
  bool saved = true;
  saved = (store.putInt("screensaver", UserConfigurationData.screensaverTime) == sizeof(UserConfigurationData.screensaverTime)) && saved;
  saved = (store.putInt("keypadPin", UserConfigurationData.keypadPin) == sizeof(UserConfigurationData.keypadPin)) && saved;
  saved = (store.putInt("brightness", UserConfigurationData.displayBrightness) == sizeof(UserConfigurationData.displayBrightness)) && saved;
  store.end();
  if (!saved) Serial.println("NVS: UserConfigurationData save incomplete");
  return saved;
}

static const BatchData_t defaultBatchData = BatchData;
bool readBatchDataFromEEPROM() {
  BatchData = defaultBatchData;
  Preferences store;
  if (!store.begin("pvt_batch", true)) return false;
  store.getString("name", String(defaultBatchData.batchName)).toCharArray(BatchData.batchName, sizeof(BatchData.batchName));
  BatchData.batchNumber = store.getUShort("number", defaultBatchData.batchNumber);
  store.getString("date", String(defaultBatchData.batchDate)).toCharArray(BatchData.batchDate, sizeof(BatchData.batchDate));
  BatchData.batchOG = store.getFloat("og", defaultBatchData.batchOG);
  if (!isfinite(BatchData.batchOG)) BatchData.batchOG = defaultBatchData.batchOG;
  BatchData.addedPlato = store.getFloat("addedPlato", defaultBatchData.addedPlato);
  if (!isfinite(BatchData.addedPlato)) BatchData.addedPlato = defaultBatchData.addedPlato;
  BatchData.startPressure = store.getFloat("startPressure", defaultBatchData.startPressure);
  if (!isfinite(BatchData.startPressure)) BatchData.startPressure = defaultBatchData.startPressure;
  BatchData.startTemperature = store.getFloat("startTemp", defaultBatchData.startTemperature);
  if (!isfinite(BatchData.startTemperature)) BatchData.startTemperature = defaultBatchData.startTemperature;
  store.end();
  return true;
}

bool writeBatchDataToNIV() {
  Preferences store;
  if (!store.begin("pvt_batch", false)) {
    Serial.println("NVS: cannot open pvt_batch");
    return false;
  }
  bool saved = true;
  saved = (store.putString("name", BatchData.batchName) == strlen(BatchData.batchName)) && saved;
  saved = (store.putUShort("number", BatchData.batchNumber) == sizeof(BatchData.batchNumber)) && saved;
  saved = (store.putString("date", BatchData.batchDate) == strlen(BatchData.batchDate)) && saved;
  saved = (store.putFloat("og", BatchData.batchOG) == sizeof(BatchData.batchOG)) && saved;
  saved = (store.putFloat("addedPlato", BatchData.addedPlato) == sizeof(BatchData.addedPlato)) && saved;
  saved = (store.putFloat("startPressure", BatchData.startPressure) == sizeof(BatchData.startPressure)) && saved;
  saved = (store.putFloat("startTemp", BatchData.startTemperature) == sizeof(BatchData.startTemperature)) && saved;
  store.end();
  if (!saved) Serial.println("NVS: BatchData save incomplete");
  return saved;
}

static const SetPointData_t defaultSetPointData = SetPointData;
bool readSetPointDataFromEEPROM() {
  SetPointData = defaultSetPointData;
  Preferences store;
  if (!store.begin("pvt_setpoint", true)) return false;
  SetPointData.mode = store.getUChar("mode", defaultSetPointData.mode);
  SetPointData.setPointTemp = store.getFloat("temperature", defaultSetPointData.setPointTemp);
  if (!isfinite(SetPointData.setPointTemp)) SetPointData.setPointTemp = defaultSetPointData.setPointTemp;
  SetPointData.setPointSlowTemp = store.getFloat("slowTemp", defaultSetPointData.setPointSlowTemp);
  if (!isfinite(SetPointData.setPointSlowTemp)) SetPointData.setPointSlowTemp = defaultSetPointData.setPointSlowTemp;
  SetPointData.setPointPressure = store.getFloat("pressure", defaultSetPointData.setPointPressure);
  if (!isfinite(SetPointData.setPointPressure)) SetPointData.setPointPressure = defaultSetPointData.setPointPressure;
  SetPointData.setPointTempSetEpoch = store.getUInt("tempEpoch", defaultSetPointData.setPointTempSetEpoch);
  SetPointData.setPointPressureSetEpoch = store.getUInt("pressureEpoch", defaultSetPointData.setPointPressureSetEpoch);
  if (SetPointData.mode > MODE_CONDITIONING) SetPointData.mode = defaultSetPointData.mode;
  store.end();
  return true;
}

bool writeSetPointDataToNIV() {
  Preferences store;
  if (!store.begin("pvt_setpoint", false)) {
    Serial.println("NVS: cannot open pvt_setpoint");
    return false;
  }
  bool saved = true;
  saved = (store.putUChar("mode", SetPointData.mode) == sizeof(SetPointData.mode)) && saved;
  saved = (store.putFloat("temperature", SetPointData.setPointTemp) == sizeof(SetPointData.setPointTemp)) && saved;
  saved = (store.putFloat("slowTemp", SetPointData.setPointSlowTemp) == sizeof(SetPointData.setPointSlowTemp)) && saved;
  saved = (store.putFloat("pressure", SetPointData.setPointPressure) == sizeof(SetPointData.setPointPressure)) && saved;
  saved = (store.putUInt("tempEpoch", SetPointData.setPointTempSetEpoch) == sizeof(SetPointData.setPointTempSetEpoch)) && saved;
  saved = (store.putUInt("pressureEpoch", SetPointData.setPointPressureSetEpoch) == sizeof(SetPointData.setPointPressureSetEpoch)) && saved;
  store.end();
  if (!saved) Serial.println("NVS: SetPointData save incomplete");
  return saved;
}

static const CountersData_t defaultCountersData = CountersData;
bool readCountersDataFromEEPROM() {
  CountersData = defaultCountersData;
  Preferences store;
  if (!store.begin("pvt_counters", true)) return false;
  CountersData.totalReliefCount = store.getUInt("reliefCount", defaultCountersData.totalReliefCount);
  CountersData.totalMolsEjected = store.getDouble("molsEjected", defaultCountersData.totalMolsEjected);
  if (!isfinite(CountersData.totalMolsEjected)) CountersData.totalMolsEjected = defaultCountersData.totalMolsEjected;
  CountersData.CO2InSolution = store.getDouble("co2Solution", defaultCountersData.CO2InSolution);
  if (!isfinite(CountersData.CO2InSolution)) CountersData.CO2InSolution = defaultCountersData.CO2InSolution;
  CountersData.headSpaceVolume = store.getFloat("headSpace", defaultCountersData.headSpaceVolume);
  if (!isfinite(CountersData.headSpaceVolume)) CountersData.headSpaceVolume = defaultCountersData.headSpaceVolume;
  CountersData.correctionPlato = store.getFloat("correctPlato", defaultCountersData.correctionPlato);
  if (!isfinite(CountersData.correctionPlato)) CountersData.correctionPlato = defaultCountersData.correctionPlato;
  CountersData.SGAttenuation = store.getFloat("sgAttenuation", defaultCountersData.SGAttenuation);
  if (!isfinite(CountersData.SGAttenuation)) CountersData.SGAttenuation = defaultCountersData.SGAttenuation;
  CountersData.totalChillTime = store.getInt("chillTime", defaultCountersData.totalChillTime);
  CountersData.totalHeatTime = store.getInt("heatTime", defaultCountersData.totalHeatTime);
  store.end();
  return true;
}

bool writeCountersDataToNIV() {
  Preferences store;
  if (!store.begin("pvt_counters", false)) {
    Serial.println("NVS: cannot open pvt_counters");
    return false;
  }
  bool saved = true;
  saved = (store.putUInt("reliefCount", CountersData.totalReliefCount) == sizeof(CountersData.totalReliefCount)) && saved;
  saved = (store.putDouble("molsEjected", CountersData.totalMolsEjected) == sizeof(CountersData.totalMolsEjected)) && saved;
  saved = (store.putDouble("co2Solution", CountersData.CO2InSolution) == sizeof(CountersData.CO2InSolution)) && saved;
  saved = (store.putFloat("headSpace", CountersData.headSpaceVolume) == sizeof(CountersData.headSpaceVolume)) && saved;
  saved = (store.putFloat("correctPlato", CountersData.correctionPlato) == sizeof(CountersData.correctionPlato)) && saved;
  saved = (store.putFloat("sgAttenuation", CountersData.SGAttenuation) == sizeof(CountersData.SGAttenuation)) && saved;
  saved = (store.putInt("chillTime", CountersData.totalChillTime) == sizeof(CountersData.totalChillTime)) && saved;
  saved = (store.putInt("heatTime", CountersData.totalHeatTime) == sizeof(CountersData.totalHeatTime)) && saved;
  store.end();
  if (!saved) Serial.println("NVS: CountersData save incomplete");
  return saved;
}

static bool clearPovotoNamespace(const char *name) {
  Preferences store;
  if (!store.begin(name, false)) {
    Serial.printf("NVS: cannot open %s for clearing\n", name);
    return false;
  }
  const bool cleared = store.clear();
  store.end();
  if (!cleared) Serial.printf("NVS: failed to clear %s\n", name);
  return cleared;
}

bool resetPovotoDataToFactoryDefaults() {
  // Keep all in-memory data and all active Povoto namespaces in sync.
  FMTData = defaultFMTData;
  FMTData.nvsSchemaVersion = PVT_NVS_SCHEMA_VERSION;
  UserConfigurationData = defaultUserConfigurationData;
  BatchData = defaultBatchData;
  SetPointData = defaultSetPointData;
  CountersData = defaultCountersData;

  bool saved = clearPovotoNamespace("pvt_settings");
  saved = clearPovotoNamespace("pvt_user") && saved;
  saved = clearPovotoNamespace("pvt_batch") && saved;
  saved = clearPovotoNamespace("pvt_setpoint") && saved;
  saved = clearPovotoNamespace("pvt_calib") && saved; // Retired namespace.
  saved = clearPovotoNamespace("pvt_counters") && saved;
  saved = clearPovotoNamespace("pvt_wifi") && saved;

  saved = writeFMTDataToNIV() && saved;
  saved = writeUserConfigurationDataToNIV() && saved;
  saved = writeBatchDataToNIV() && saved;
  saved = writeSetPointDataToNIV() && saved;
  saved = writeCountersDataToNIV() && saved;
  updatePatmFromFMTAltitude();
  requestDerivedStateRestoreFromCounters();

  if (saved) Serial.println("NVS: factory defaults restored");
  else Serial.println("NVS: factory reset incomplete");
  return saved;
}

// All data must already be loaded in RAM. Peers, Wi-Fi and other namespaces
// are outside this operation. Interrupted rewrites may lose unsaved values.
static void rewriteNVSIfSchemaChanged() {
  if (FMTData.nvsSchemaVersion == PVT_NVS_SCHEMA_VERSION) return;
  Serial.printf("NVS: rewriting schema %lu -> %lu\n",
    (unsigned long)FMTData.nvsSchemaVersion, (unsigned long)PVT_NVS_SCHEMA_VERSION);
  FMTData.nvsSchemaVersion = 0; // Keep retrying until the entire rewrite succeeds.
  bool saved = clearPovotoNamespace("pvt_settings");
  saved = clearPovotoNamespace("pvt_user") && saved;
  saved = clearPovotoNamespace("pvt_batch") && saved;
  saved = clearPovotoNamespace("pvt_setpoint") && saved;
  saved = clearPovotoNamespace("pvt_calib") && saved; // Retired namespace.
  saved = clearPovotoNamespace("pvt_counters") && saved;

  saved = writeFMTDataToNIV() && saved;
  saved = writeUserConfigurationDataToNIV() && saved;
  saved = writeBatchDataToNIV() && saved;
  saved = writeSetPointDataToNIV() && saved;
  saved = writeCountersDataToNIV() && saved;
  if (saved) {
    FMTData.nvsSchemaVersion = PVT_NVS_SCHEMA_VERSION;
    saved = writeFMTDataToNIV();
  }
  if (!saved) {
    FMTData.nvsSchemaVersion = 0;
    Serial.println("NVS: schema rewrite incomplete; retry on next boot");
  } else {
    Serial.println("NVS: schema rewrite complete");
  }
}

void povotoDataInit() {


  if (!readFMTDataFromEEPROM())
    Serial.println("FMT data load failed, using defaults");

  if (!readUserConfigurationDataFromEEPROM())
    Serial.println("User configuration load failed, using defaults");


  if (!readBatchDataFromEEPROM())
    Serial.println("Batch data load failed, using defaults");

  if (!readSetPointDataFromEEPROM())
    Serial.println("Set point data load failed, using defaults");

  if (!readCountersDataFromEEPROM())
    Serial.println("Counters data load failed, using defaults");
  else
    requestDerivedStateRestoreFromCounters();

  rewriteNVSIfSchemaChanged();
  updatePatmFromFMTAltitude();
}

void resetCountersForNewBatch() {
  //zera totalbubblecount 
  CountersData.totalReliefCount = 0;
  CountersData.totalMolsEjected = 0.0;
  CountersData.CO2InSolution = 0.0;
  CountersData.headSpaceVolume = 0.0f;
  CountersData.SGAttenuation = 0.0f;
  CountersData.totalChillTime = 0;
  CountersData.totalHeatTime = 0;

  writeCountersDataToNIV();
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
