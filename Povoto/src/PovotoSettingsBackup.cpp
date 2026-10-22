#include "PovotoSettingsBackup.h"

#include <math.h>
#include <string.h>

#include "PovotoData.h"
#include "DallasSensors.h"

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

// Dallas role addresses: XX-XX-XX-XX-XX-XX-XX-XX, "" = not assigned.
static const char *const DALLAS_BACKUP_KEYS[DALLAS_ROLE_COUNT] = {
  "dallasFermenter", "dallasEnvironment", "dallasHeadspace"};

// Flow curve (docs/vent.md): k, beta, pressure range and test time. The backups of the cubic
// curve (keys ...A0..A3) have no ...K: the curve is then left as it is. The ...T0 key of the (k, beta, t0)
// backups is ignored.
static void appendCurve(String &json, const char *prefix, const VentModel::Curve &curve) {
  char key[32];
  snprintf(key, sizeof(key), "%sK", prefix);
  appendNumber(json, key, curve.k, 6);
  snprintf(key, sizeof(key), "%sBeta", prefix);
  appendNumber(json, key, curve.beta, 6);
  snprintf(key, sizeof(key), "%sPMin", prefix);
  appendNumber(json, key, curve.pMin, 6);
  snprintf(key, sizeof(key), "%sPMax", prefix);
  appendNumber(json, key, curve.pMax, 6);
  snprintf(key, sizeof(key), "%sEpoch", prefix);
  appendNumber(json, key, curve.epoch, 0);
}

static void appendDallasAddress(String &json, const char *name, const uint8_t *address) {
  char text[DALLAS_ADDRESS_TEXT_SIZE];
  formatDallasAddress(address, text, sizeof(text));
  json += "  \"";
  json += name;
  json += "\": \"";
  json += text;
  json += "\",\n";
}

String savePovotoSettingsBackup() {
  String json;
  json.reserve(1800);
  json = "{\n  \"format\": \"povoto-settings\",\n  \"version\": 1,\n";
  appendNumber(json, "PovotoNum", FMTData.PovotoNum, 0);
  appendBool(json, "debugMode", FMTData.debugMode);
  appendBool(json, "googleLogEnabled", FMTData.googleLogEnabled);
  appendNumber(json, "FMTVolume", FMTData.FMTVolume, 6);
  appendNumber(json, "ventTargetDropPercent", FMTData.ventTargetDropPercent, 6);
  appendNumber(json, "flowLevelFactor", FMTData.flowLevelFactor, 6);
  appendCurve(json, "ventCurve", FMTData.ventCurve);
  appendCurve(json, "ventReferenceCurve", FMTData.ventReferenceCurve);
  appendNumber(json, "FMTAltitude", FMTData.FMTAltitude, 6);
  appendNumber(json, "dataLogIntervalSeconds", FMTData.dataLogIntervalSeconds, 0);
  appendNumber(json, "FMTEffectiveVentingExponent", FMTData.FMTEffectiveVentingExponent, 6);
  appendNumber(json, "pressure0Current", FMTData.pressure0Current, 6);
  appendNumber(json, "pressure1Bar", FMTData.pressure1Bar, 6);
  appendNumber(json, "pressure1Current", FMTData.pressure1Current, 6);
  appendNumber(json, "maximumPressure", FMTData.maximumPressure, 6);
  appendNumber(json, "maxSetPointPressure", FMTData.maxSetPointPressure, 6);
  appendNumber(json, "lastExpTankVolume", FMTData.lastExpTankVolume, 6);
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
  for (uint8_t role = 0; role < DALLAS_ROLE_COUNT; ++role)
    appendDallasAddress(json, DALLAS_BACKUP_KEYS[role], FMTData.dallasAddress[role]);
  appendNumber(json, "screensaverTime", UserConfigurationData.screensaverTime, 0);
  appendNumber(json, "keypadPin", UserConfigurationData.keypadPin, 0);
  appendNumber(json, "displayBrightness", UserConfigurationData.displayBrightness, 0);
  appendNumber(json, "ledBrightness", UserConfigurationData.ledBrightness, 0, true);
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

static bool readDallasAddress(const char *json, const char *name, uint8_t *address) {
  char key[64];
  snprintf(key, sizeof(key), "\"%s\"", name);
  const char *field = strstr(json, key);
  if (!field) return false;
  const char *colon = strchr(field + strlen(key), ':');
  if (!colon) return false;
  const char *open = strchr(colon, '"');
  if (!open) return false;
  const char *close = strchr(open + 1, '"');
  if (!close || close - open - 1 >= (int)DALLAS_ADDRESS_TEXT_SIZE) return false;
  char text[DALLAS_ADDRESS_TEXT_SIZE];
  memcpy(text, open + 1, close - open - 1);
  text[close - open - 1] = '\0';
  return parseDallasAddress(text, address);
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

// Optional curve: absent keeps the current one; present must be complete and valid.
static bool readCurve(const char *json, const char *prefix, VentModel::Curve &curve, bool allowEmpty) {
  char key[32];
  snprintf(key, sizeof(key), "%sK", prefix);
  if (!hasKey(json, key)) return true;
  VentModel::Curve loaded = {};
  float value = 0.0f; // the curve is packed: no references to its fields
  if (!readFloat(json, key, value)) return false;
  loaded.k = value;
  snprintf(key, sizeof(key), "%sBeta", prefix);
  if (!readFloat(json, key, value)) return false;
  loaded.beta = value;
  double epoch = 0.0;
  snprintf(key, sizeof(key), "%sPMin", prefix);
  if (!readFloat(json, key, value)) return false;
  loaded.pMin = value;
  snprintf(key, sizeof(key), "%sPMax", prefix);
  if (!readFloat(json, key, value)) return false;
  loaded.pMax = value;
  snprintf(key, sizeof(key), "%sEpoch", prefix);
  if (!readNumber(json, key, epoch) || epoch < 0.0 || epoch > 4294967295.0) return false;
  loaded.epoch = (uint32_t)epoch;
  const bool empty = loaded.pMin == 0.0f && loaded.pMax == 0.0f && loaded.k == 0.0f;
  if (!(allowEmpty && empty) && !VentModel::validCurve(loaded)) return false;
  curve = loaded;
  return true;
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
  READ_BACKUP_BOOL("debugMode", loadedFmt.debugMode);
  READ_BACKUP_BOOL("googleLogEnabled", loadedFmt.googleLogEnabled);
  READ_BACKUP_FLOAT("FMTVolume", loadedFmt.FMTVolume);
  // Expansion-tank fields of older files (te-final tag), and the retired
  // liquidMassInGasVentingPercent, ventMinSeconds and ventSafetyDropPercent
  // fields, are ignored.
  READ_BACKUP_FLOAT("ventTargetDropPercent", loadedFmt.ventTargetDropPercent);
  READ_BACKUP_FLOAT("flowLevelFactor", loadedFmt.flowLevelFactor);
  if (!isValidVentTargetDrop(loadedFmt.ventTargetDropPercent) || !isValidFlowLevelFactor(loadedFmt.flowLevelFactor))
    return false;
  if (!readCurve(json, "ventCurve", loadedFmt.ventCurve, false) ||
      !readCurve(json, "ventReferenceCurve", loadedFmt.ventReferenceCurve, true)) return false;
  READ_BACKUP_FLOAT("FMTAltitude", loadedFmt.FMTAltitude);
  READ_BACKUP_INT("dataLogIntervalSeconds", loadedFmt.dataLogIntervalSeconds);
  if (!isValidDataLogIntervalSeconds(loadedFmt.dataLogIntervalSeconds)) return false;
  READ_BACKUP_FLOAT("FMTEffectiveVentingExponent", loadedFmt.FMTEffectiveVentingExponent);
  READ_BACKUP_FLOAT("pressure0Current", loadedFmt.pressure0Current);
  READ_BACKUP_FLOAT("pressure1Bar", loadedFmt.pressure1Bar);
  READ_BACKUP_FLOAT("pressure1Current", loadedFmt.pressure1Current);
  READ_BACKUP_FLOAT("maximumPressure", loadedFmt.maximumPressure);
  READ_BACKUP_FLOAT("maxSetPointPressure", loadedFmt.maxSetPointPressure);
  READ_BACKUP_FLOAT("lastExpTankVolume", loadedFmt.lastExpTankVolume);
  // The security pressure is above 2.0 and up to 2.3 bar, the highest set point is up to 2.0 bar and below it.
  if (!isValidPovotoNumber(povotoNumber) || !isValidMaximumPressure(loadedFmt.maximumPressure) ||
      !isValidMaxSetPointPressure(loadedFmt.maxSetPointPressure, loadedFmt.maximumPressure) ||
      !(loadedFmt.lastExpTankVolume > 0.0f)) return false;
  READ_BACKUP_INT("nucleationWindow", loadedFmt.nucleationWindow);
  READ_BACKUP_BOOL("heaterEnabled", loadedFmt.heater.enabled);
  READ_BACKUP_FLOAT("heaterOnMinutes", loadedFmt.heater.onMinutes);
  READ_BACKUP_FLOAT("heaterOffMinutes", loadedFmt.heater.offMinutes);
  READ_BACKUP_INT("screensaverTime", loadedUser.screensaverTime);
  READ_BACKUP_INT("keypadPin", loadedUser.keypadPin);
  READ_BACKUP_INT("displayBrightness", loadedUser.displayBrightness);
  READ_BACKUP_INT("ledBrightness", loadedUser.ledBrightness);  // missing in older files: kept
  if (loadedUser.ledBrightness < 1 || loadedUser.ledBrightness > 10) return false;
  loadedFmt.PovotoNum = static_cast<byte>(povotoNumber);

  for (uint8_t i = 0; i < 3; ++i) {
    char key[20];
    snprintf(key, sizeof(key), "coolingOn%u", i);
    READ_BACKUP_FLOAT(key, loadedFmt.coolingCycle[i].onMinutes);
    snprintf(key, sizeof(key), "coolingOff%u", i);
    READ_BACKUP_FLOAT(key, loadedFmt.coolingCycle[i].offMinutes);
  }
  // Optional: older backups keep the current assignments.
  for (uint8_t role = 0; role < DALLAS_ROLE_COUNT; ++role) {
    if (!hasKey(json, DALLAS_BACKUP_KEYS[role])) continue;
    if (!readDallasAddress(json, DALLAS_BACKUP_KEYS[role], loadedFmt.dallasAddress[role])) return false;
  }
  if (!dallasRolesDistinct(loadedFmt.dallasAddress)) return false;

#undef READ_BACKUP_FLOAT
#undef READ_BACKUP_INT
#undef READ_BACKUP_BOOL

  loadedFmt.nvsSchemaVersion = PVT_NVS_SCHEMA_VERSION;
  FMTData = loadedFmt;
  UserConfigurationData = loadedUser;
  updatePatmFromFMTAltitude();
  applyDallasRoles();
  return writeFMTDataToNIV() && writeUserConfigurationDataToNIV();
}
