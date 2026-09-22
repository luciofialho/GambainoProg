#include "PovotoSettingsBackup.h"

#include <math.h>
#include <string.h>

#include "PovotoData.h"
#include "GasFlowModel.h"

static void appendNumber(String &json, const char *name, double value,
                         uint8_t decimals, bool isLast = false) {
  json += "  \"";
  json += name;
  json += "\": ";
  json += String(value, static_cast<unsigned int>(decimals));
  if (!isLast) json += ',';
  json += '\n';
}

static void appendBool(String &json, const char *name, bool value) {
  json += "  \"";
  json += name;
  json += value ? "\": true,\n" : "\": false,\n";
}

String savePovotoSettingsBackup() {
  String json;
  json.reserve(1800);
  json = "{\n  \"format\": \"povoto-settings\",\n  \"version\": 1,\n";
  appendNumber(json, "PovotoNum", FMTData.PovotoNum, 0);
  appendNumber(json, "FMTVolume", FMTData.FMTVolume, 6);
  appendNumber(json, "expansionTimeCoefficientA", FMTData.expansionTimeCoefficientA, 6);
  appendNumber(json, "expansionTimeCoefficientB", FMTData.expansionTimeCoefficientB, 6);
  appendNumber(json, "targetResidualAfterReliefPercent", FMTData.targetResidualAfterReliefPercent, 6);
  appendNumber(json, "liquidMassInGasVentingPercent", FMTData.liquidMassInGasVentingPercent, 6);
  appendNumber(json, "ventingResidualCoefficientA", FMTData.ventingResidualCoefficientA, 6);
  appendNumber(json, "ventingResidualCoefficientB", FMTData.ventingResidualCoefficientB, 6);
  appendNumber(json, "ventingResidualCoefficientC", FMTData.ventingResidualCoefficientC, 6);
  appendNumber(json, "FMTReliefVolume", FMTData.FMTReliefVolume, 6);
  appendNumber(json, "FMTAltitude", FMTData.FMTAltitude, 6);
  appendNumber(json, "dataLogIntervalSeconds", FMTData.dataLogIntervalSeconds, 0);
  appendNumber(json, "FMTEffectiveVentingExponent", FMTData.FMTEffectiveVentingExponent, 6);
  appendNumber(json, "pressure0Current", FMTData.pressure0Current, 6);
  appendNumber(json, "pressure1Bar", FMTData.pressure1Bar, 6);
  appendNumber(json, "pressure1Current", FMTData.pressure1Current, 6);
  appendNumber(json, "pressure2Bar", FMTData.pressure2Bar, 6);
  appendNumber(json, "pressure2Current", FMTData.pressure2Current, 6);
  appendNumber(json, "maximumPressure", FMTData.maximumPressure, 6);
  appendNumber(json, "co2TransferTime", FMTData.co2TransferTime, 0);
  appendNumber(json, "nucleationWindow", FMTData.nucleationWindow, 0);
  appendBool(json, "heaterEnabled", FMTData.heater.enabled);
  appendNumber(json, "heaterOnMinutes", FMTData.heater.onMinutes, 6);
  appendNumber(json, "heaterOffMinutes", FMTData.heater.offMinutes, 6);
  for (uint8_t i = 0; i < 3; ++i) {
    char key[20];
    snprintf(key, sizeof(key), "coolingOn%u", i);
    appendNumber(json, key, FMTData.coolingCycle[i].onMinutes, 6);
    snprintf(key, sizeof(key), "coolingOff%u", i);
    appendNumber(json, key, FMTData.coolingCycle[i].offMinutes, 6);
  }
  appendNumber(json, "screensaverTime", UserConfigurationData.screensaverTime, 0);
  appendNumber(json, "keypadPin", UserConfigurationData.keypadPin, 0);
  appendNumber(json, "displayBrightness", UserConfigurationData.displayBrightness, 0, true);
  json += "}\n";
  return json;
}

static bool readNumber(const char *json, const char *name, double &value) {
  char key[64];
  snprintf(key, sizeof(key), "\"%s\"", name);
  const char *field = strstr(json, key);
  if (!field) return false;
  const char *colon = strchr(field + strlen(key), ':');
  if (!colon) return false;
  char *end = nullptr;
  value = strtod(colon + 1, &end);
  return end != colon + 1 && isfinite(value);
}

static bool hasKey(const char *json, const char *name) {
  char key[64];
  snprintf(key, sizeof(key), "\"%s\"", name);
  return strstr(json, key) != nullptr;
}

static bool readFloat(const char *json, const char *name, float &value) {
  double parsed = 0.0;
  if (!readNumber(json, name, parsed) || parsed < -3.402823e38 || parsed > 3.402823e38) return false;
  value = static_cast<float>(parsed);
  return isfinite(value);
}

static bool readInt(const char *json, const char *name, int &value) {
  double parsed = 0.0;
  if (!readNumber(json, name, parsed) || parsed < -2147483648.0 ||
      parsed > 2147483647.0 || floor(parsed) != parsed) return false;
  value = static_cast<int>(parsed);
  return true;
}

static bool readBool(const char *json, const char *name, bool &value) {
  char key[64];
  snprintf(key, sizeof(key), "\"%s\"", name);
  const char *field = strstr(json, key);
  if (!field) return false;
  const char *colon = strchr(field + strlen(key), ':');
  if (!colon) return false;
  while (*++colon == ' ' || *colon == '\t' || *colon == '\r' || *colon == '\n') {}
  if (strncmp(colon, "true", 4) == 0) {
    value = true;
    return true;
  }
  if (strncmp(colon, "false", 5) == 0) {
    value = false;
    return true;
  }
  return false;
}

bool loadPovotoSettingsBackup(const String &settings) {
  if (settings.length() == 0 || settings.length() > 4096) return false;
  const char *json = settings.c_str();
  const char *format = strstr(json, "\"format\"");
  if (!format || !strstr(format, "\"povoto-settings\"")) return false;
  double version = 0.0;
  if (hasKey(json, "version") && !readNumber(json, "version", version)) return false;

  FMTData_t loadedFmt = FMTData;
  UserConfigurationData_t loadedUser = UserConfigurationData;
  int povotoNumber = 0;
#define READ_BACKUP_FLOAT(KEY, DESTINATION) do { \
  if (hasKey(json, KEY)) { \
    float parsedValue = 0.0f; \
    if (!readFloat(json, KEY, parsedValue)) return false; \
    DESTINATION = parsedValue; \
  } \
} while (false)
#define READ_BACKUP_INT(KEY, DESTINATION) do { \
  if (hasKey(json, KEY)) { \
    int parsedValue = 0; \
    if (!readInt(json, KEY, parsedValue)) return false; \
    DESTINATION = parsedValue; \
  } \
} while (false)
#define READ_BACKUP_BOOL(KEY, DESTINATION) do { \
  if (hasKey(json, KEY)) { \
    bool parsedValue = false; \
    if (!readBool(json, KEY, parsedValue)) return false; \
    DESTINATION = parsedValue; \
  } \
} while (false)

  povotoNumber = loadedFmt.PovotoNum;
  if (hasKey(json, "PovotoNum") && !readInt(json, "PovotoNum", povotoNumber)) return false;
  READ_BACKUP_FLOAT("FMTVolume", loadedFmt.FMTVolume);
  READ_BACKUP_FLOAT("expansionTimeCoefficientA", loadedFmt.expansionTimeCoefficientA);
  READ_BACKUP_FLOAT("expansionTimeCoefficientB", loadedFmt.expansionTimeCoefficientB);
  READ_BACKUP_FLOAT("targetResidualAfterReliefPercent", loadedFmt.targetResidualAfterReliefPercent);
  READ_BACKUP_FLOAT("liquidMassInGasVentingPercent", loadedFmt.liquidMassInGasVentingPercent);
  if (hasKey(json, "ventingResidualCoefficientA")) {
    READ_BACKUP_FLOAT("ventingResidualCoefficientA", loadedFmt.ventingResidualCoefficientA);
    READ_BACKUP_FLOAT("ventingResidualCoefficientB", loadedFmt.ventingResidualCoefficientB);
    READ_BACKUP_FLOAT("ventingResidualCoefficientC", loadedFmt.ventingResidualCoefficientC);
  } else {
    float factorAt18Bar = loadedFmt.ventingResidualCoefficientC;
    float factorAt05Bar = loadedFmt.ventingResidualCoefficientC;
    if (hasKey(json, "ventingResidualFactorAt18Bar")) {
      READ_BACKUP_FLOAT("ventingResidualFactorAt18Bar", factorAt18Bar);
      READ_BACKUP_FLOAT("ventingResidualFactorAt05Bar", factorAt05Bar);
    } else {
      READ_BACKUP_FLOAT("ventingResidualFactor", factorAt18Bar);
      factorAt05Bar = factorAt18Bar;
    }
    loadedFmt.ventingResidualCoefficientA = 0.0f;
    loadedFmt.ventingResidualCoefficientB = (factorAt18Bar - factorAt05Bar) / 1.3f;
    loadedFmt.ventingResidualCoefficientC = factorAt05Bar - 0.5f * loadedFmt.ventingResidualCoefficientB;
  }
  if (!GasFlow::validExpansionParameters(loadedFmt.expansionTimeCoefficientA, loadedFmt.expansionTimeCoefficientB, loadedFmt.maximumPressure) ||
      !(loadedFmt.targetResidualAfterReliefPercent > 0.0f && loadedFmt.targetResidualAfterReliefPercent < 100.0f) ||
      !(loadedFmt.liquidMassInGasVentingPercent >= 0.0f && loadedFmt.liquidMassInGasVentingPercent <= 100.0f) ||
      !GasFlow::validVentingResidualCoefficients(loadedFmt.ventingResidualCoefficientA,
                                                  loadedFmt.ventingResidualCoefficientB,
                                                  loadedFmt.ventingResidualCoefficientC)) return false;
  READ_BACKUP_FLOAT("FMTReliefVolume", loadedFmt.FMTReliefVolume);
  READ_BACKUP_FLOAT("FMTAltitude", loadedFmt.FMTAltitude);
  READ_BACKUP_INT("dataLogIntervalSeconds", loadedFmt.dataLogIntervalSeconds);
  if (!isValidDataLogIntervalSeconds(loadedFmt.dataLogIntervalSeconds)) return false;
  READ_BACKUP_FLOAT("FMTEffectiveVentingExponent", loadedFmt.FMTEffectiveVentingExponent);
  READ_BACKUP_FLOAT("pressure0Current", loadedFmt.pressure0Current);
  READ_BACKUP_FLOAT("pressure1Bar", loadedFmt.pressure1Bar);
  READ_BACKUP_FLOAT("pressure1Current", loadedFmt.pressure1Current);
  READ_BACKUP_FLOAT("pressure2Bar", loadedFmt.pressure2Bar);
  READ_BACKUP_FLOAT("pressure2Current", loadedFmt.pressure2Current);
  READ_BACKUP_FLOAT("maximumPressure", loadedFmt.maximumPressure);
  READ_BACKUP_INT("co2TransferTime", loadedFmt.co2TransferTime);
  READ_BACKUP_INT("nucleationWindow", loadedFmt.nucleationWindow);
  READ_BACKUP_BOOL("heaterEnabled", loadedFmt.heater.enabled);
  READ_BACKUP_FLOAT("heaterOnMinutes", loadedFmt.heater.onMinutes);
  READ_BACKUP_FLOAT("heaterOffMinutes", loadedFmt.heater.offMinutes);
  READ_BACKUP_INT("screensaverTime", loadedUser.screensaverTime);
  READ_BACKUP_INT("keypadPin", loadedUser.keypadPin);
  READ_BACKUP_INT("displayBrightness", loadedUser.displayBrightness);
  loadedFmt.PovotoNum = static_cast<byte>(povotoNumber);

  for (uint8_t i = 0; i < 3; ++i) {
    char key[20];
    snprintf(key, sizeof(key), "coolingOn%u", i);
    READ_BACKUP_FLOAT(key, loadedFmt.coolingCycle[i].onMinutes);
    snprintf(key, sizeof(key), "coolingOff%u", i);
    READ_BACKUP_FLOAT(key, loadedFmt.coolingCycle[i].offMinutes);
  }

#undef READ_BACKUP_FLOAT
#undef READ_BACKUP_INT
#undef READ_BACKUP_BOOL

  loadedFmt.nvsSchemaVersion = PVT_NVS_SCHEMA_VERSION;
  FMTData = loadedFmt;
  UserConfigurationData = loadedUser;
  updatePatmFromFMTAltitude();
  return writeFMTDataToNIV() && writeUserConfigurationDataToNIV();
}
