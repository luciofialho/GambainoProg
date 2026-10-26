#ifndef POVOTODATA_H
#define POVOTODATA_H

#include <arduino.h>
#include <ESPAsyncWebServer.h>
#include "VentModel.h"

// Fermenter operating modes
#define MODE_OFF                 0
#define MODE_BREWING_TRANSFERING 1
#define MODE_FERMENTING          2
#define MODE_CONDITIONING        3
// Manual control: like Off for the control and the logs (outputs only through
// the overrides of the Control page, no batch), shown as "Manual control".
#define MODE_MANUAL              4
inline bool modeIsOffLike(unsigned mode) { return mode == MODE_OFF || mode == MODE_MANUAL; }

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

// FMT data struct. The expansion-tank fields of the te-final tag (keys
// expTimeA, expTimeB, targetResidual, kCO2, kAir, flowCO2Air, ventResidualA..C,
// reliefVolume, dallasExpTank) are no longer read or written: they stay in NVS
// for a fallback to the tag (docs/sem-te-plano.md, section 1).
struct FMTData_t {
  uint32_t nvsSchemaVersion;
  byte PovotoNum;
  bool googleLogEnabled; // Google Sheets datalog on/off (default on)
  char googleLogFolder[20]; // Sheets folder outside debug mode (default Beers)
  bool debugMode; // bench board: simulated sensors, short timings (default true)
  float FMTVolume;
  float FMTAltitude;
  int dataLogIntervalSeconds;
  float FMTEffectiveVentingExponent;
  // Direct vent (docs/vent.md): drop of the gauge pressure aimed at by each
  // vent. The minimum opening (VENT_MIN_SECONDS) and the safety drop
  // (VENT_SAFETY_DROP_FACTOR x target) are fixed; the NVS keys ventMinSec,
  // ventSafeDrop and liquidGasMass of earlier versions stay in NVS, unused.
  float ventTargetDropPercent;
  // Level of the flow curve converted to CO2, adjusted between batches by the
  // degassed SG (manual P7).
  float flowLevelFactor;
  // Valve flow curve (k, beta) saved from a venting test (air), and the fixed
  // reference curve the tests are compared with (all zero = none).
  VentModel::Curve ventCurve;
  VentModel::Curve ventReferenceCurve;
  float pressure0Current;
  float pressure1Bar;
  float pressure1Current;
  float maximumPressure;       // security pressure (valve opens above it)
  float maxSetPointPressure;   // highest pressure set point accepted (NVS maxSetPress)
  float lastExpTankVolume;     // last expansion tank volume typed on the fermenter volume routine (NVS lastExpTankVol)
  int   nucleationWindow;
  HeaterCycle_t heater;
  CoolingCyclePoint_t coolingCycle[3]; // Fixed rows: 20, 10 and 0 degrees C.
  // ROM address of the Dallas thermometer of each role (DALLAS_ROLE_*),
  // all zeros = not assigned; one NVS key per role (docs/thermometers.md).
  uint8_t dallasAddress[3][8];
} __attribute__((packed));

// Thermometer roles on the Dallas bus. Unassigned: the fermenter has no
// reading (never chosen automatically), the environment comes from the
// ESP-NOW broadcast and the gas temperature is the fermenter's (no headspace
// thermometer). The expansion-tank role of the te-final tag is gone; its key
// dallasExpTank stays in NVS, unused.
constexpr uint8_t DALLAS_ROLE_FERMENTER = 0;
constexpr uint8_t DALLAS_ROLE_ENVIRONMENT = 1;
constexpr uint8_t DALLAS_ROLE_HEADSPACE = 2; // gas in the headspace (docs/thermometers.md)
constexpr uint8_t DALLAS_ROLE_COUNT = 3;


extern FMTData_t FMTData;

inline bool isValidVentTargetDrop(float percent) { return isfinite(percent) && percent >= 1.0f && percent <= 15.0f; }
// Fixed vent limits: the minimum opening wins over the safety drop (planVent).
constexpr float VENT_MIN_SECONDS = 1.5f;
constexpr float VENT_SAFETY_DROP_FACTOR = 2.0f;
// The pressure sensor (0 to 2 bar) reads reasonably well up to 2.3 bar.
constexpr float MAX_SECURITY_PRESSURE_LIMIT = 2.3f;
constexpr float MAX_SETPOINT_PRESSURE_LIMIT = 2.0f;
inline bool isValidMaximumPressure(float bar) { return isfinite(bar) && bar > 2.0f && bar <= MAX_SECURITY_PRESSURE_LIMIT; }
// The highest pressure set point is below the security pressure and up to 2 bar.
inline bool isValidMaxSetPointPressure(float bar, float securityBar) {
  return isfinite(bar) && bar > 0.0f && bar <= MAX_SETPOINT_PRESSURE_LIMIT && bar < securityBar;
}
// Povoto numbers are 0 (not set yet) to 99 (the cloud ids are site * 100 + number).
inline bool isValidPovotoNumber(int number) { return number >= 0 && number <= 99; }
inline bool isValidFlowLevelFactor(float factor) { return isfinite(factor) && factor >= 0.5f && factor <= 1.5f; }

struct UserConfigurationData_t {
  int screensaverTime;
  int keypadPin;
  int displayBrightness; // 1..10
  int ledBrightness;     // 1..10, the RGB status LED (neopixel)
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
  byte mode;  // MODE_OFF=0, MODE_BREWING_TRANSFERING=1, MODE_FERMENTING=2, MODE_CONDITIONING=3, MODE_MANUAL=4
  float setPointTemp;
  float setPointSlowTemp;
  float setPointSlowTempSpeed;
  float setPointPressure;
  float setPointSlowPressure;
  float setPointSlowPressureSpeed;
} __attribute__((packed));

extern SetPointData_t SetPointData;

// Override of the vent valve (Control page, "Valve"); not saved, a boot returns to Auto.
// Open holds the valve open (at most 10 min); Closed suspends the automatic reliefs, but
// never the one above the maximum security pressure (docs/vent.md).
constexpr byte VALVE_OVERRIDE_AUTO = 0;
constexpr byte VALVE_OVERRIDE_OPEN = 1;
constexpr byte VALVE_OVERRIDE_CLOSED = 2;

// control
struct ControlData_t {
  float temperature;
  float pressure;
  bool chillerSwitch;
  bool heaterSwitch;
  bool transferValve;
  byte chillerOverride;
  byte heaterOverride;
  byte transferOverride; // VALVE_OVERRIDE_*
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
  // CO2 debt of the produced-CO2 integral (mol): falls of the CO2 total, paid
  // by later rises before they count as production. Survives a reboot.
  double co2CorrectionDebt;
  // gCO2/L/d transition (docs/gco2-rate.md): local NTP epoch of its start
  // (0 = none; 1 = start at the next sample, after the first boot of this
  // firmware or back from Conditioning), direction of the dissolved CO2
  // (CO2_TRANS_*), Henry equilibrium before it (mol), the last stable rate
  // and its epoch (NAN/0 = none), and the epoch of the first relief with the
  // pressure stable (0 = none yet).
  uint32_t co2TransStart;
  uint8_t co2TransDir;
  float co2TransHenryRef;
  float co2TransRate;
  uint32_t co2TransRateAt;
  uint32_t co2TransReliefAt;
} __attribute__((packed));

#define TEMP_STATE_STABLE          0
#define TEMP_STATE_CHANGING_DIRECT 1
#define TEMP_STATE_CHANGING_SLOW   2
// Was stable, then left the tolerance band; becomes STABLE again with a new time.
#define TEMP_STATE_UNSTABLE        3

// CountersData.co2TransDir: how the dissolved CO2 moves in a transition.
#define CO2_TRANS_UNKNOWN   0
#define CO2_TRANS_ABSORBING 1 // pressure up or cooling: net release <= production
#define CO2_TRANS_RELEASING 2 // pressure down or warming: net release >= production
#define CO2_TRANS_MIXED     3 // both in the same transition

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

// Re-selects the atmospheric pressure source (altitude changed); AtmosphericPressure.cpp.
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
