#include "PressureControl.h"
#include "GasFlowModel.h"
#include "ExpansionResidualFit.h"
#include "TemperatureControl.h"
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "GambainoCommon.h"
#include "PovotoTasks.h"
#include "datalog.h"
#include <INA226.h>
#include <Arduino.h>
#include <IOTK.h>
#include <IOTK_NTP.h>
#include "__NumFilters.h"
#include <LittleFS.h>
#include "PovotoFilesystem.h"
#include <math.h>
#include <algorithm>


#define DEBUGACCELERATION (debugging ? 20L : 1L)
#define TRANSFERTIME (8000 / DEBUGACCELERATION) 
#define RELIEFTIME (8000 / DEBUGACCELERATION)

#define PRESSURE_MEDIAN_WINDOW 9
#define CURRENT_MEDIAN_MIN_SAMPLE_MS 20 // defined for 16 samples at 1.1ms in the INA226 (~17.6ms)
#define INSTABILITYTHRESHOLDMA 4.0f

#define INA226_I2C_ADDRESS 0x44
#define INA226_SHUNT_OHMS 3.0f

#define SOLENOID_NOISE_MS 400

#define TRANSFER_CLOSE_PRESSURE_BLOCK_MS (2*CURRENT_MEDIAN_MIN_SAMPLE_MS*PRESSURE_MEDIAN_WINDOW) // gives time to fill and renovate the whole vector, avoiding transient pneumatics effects

#define PRESSURE_SAMPLE_MIN_MS 250
#define PRESSURE_SAMPLES_MAX 3000
#define PRESSURE_RELIEF_HISTORY_MAX 500
#define VOLUME_DETERMINATION_RECORD_START_CYCLE 6
#define VOLUME_DETERMINATION_RECORD_END_CYCLE 35
#define VOLUME_DETERMINATION_OPEN_MS (3L*60000UL / DEBUGACCELERATION)
#define VOLUME_DETERMINATION_WAIT_MS (4L*60000UL / DEBUGACCELERATION)
#define VOLUME_DETERMINATION_FAST_WAIT_MS (2L*60000UL / DEBUGACCELERATION)
static constexpr unsigned VOLUME_MIN_VALID_RELIEFS = 10;
static constexpr unsigned VOLUME_CONVERGENCE_WINDOW = 5;
static constexpr float VOLUME_MAX_FIT_SPREAD = 0.005f;
static constexpr float VOLUME_MAX_METHOD_DIFFERENCE_PERCENT = 1.0f;
static constexpr unsigned VOLUME_RECENT_FIT_WINDOW = 10;
static constexpr float VOLUME_MAX_TREND_PERCENT_PER_CYCLE = 0.05f;
static constexpr float VOLUME_MAX_RECENT_DIFFERENCE_PERCENT = 0.5f;
#define RELIEFS_WINDOW_SIZE 2
#define RELIEF_OVERDUE_FACTOR 1.20f
#define RELIEF_PER_HOUR_MIN_DISPLAY 0.20f

#define CONST_R 0.0831446 // constante dos gases em bar*L/(mol*K) 
#define CO2MOLAR_MASS 44.01


INA226 ina226(INA226_I2C_ADDRESS);
bool pressureSensorConnected = false;
bool debugPressureOverride = false; // debugging only: pressure comes from the debug page, not the INA
float currentReading = 0.0; // Corrente em mA


averageFloatVector lnPressureDropAvg(15);
static float headspaceFiltered = NAN;
static float headspaceFilterAlpha = 0.05f;
float beerVolume = 0.0f;
float beerSG = 0.0;
float beerABV = 0.0;
float headSpaceCO2Mols = 0.0f;
float beerCO2EvolutionGramsPerLiterPerDay = 0.0f;
float sgPointGenerationTime = 0.0f;

static constexpr unsigned long CO2_EVOLUTION_SAMPLE_MS = 60000UL;
static constexpr uint16_t CO2_EVOLUTION_HISTORY_SIZE = 71;
// Window (samples, ~1/min) from which a rate is trusted enough to be saved, and
// until which the saved rate is reported after a reboot.
static constexpr uint16_t CO2_EVOLUTION_MATURE_SAMPLES = 15;
static constexpr uint16_t CO2_RULE_MIN_SAMPLES = 31; // 30 min between the first and last samples
static constexpr uint32_t CO2_RATE_HELD_MAX_AGE_S = 2UL * 3600UL;
// Dissolved-CO2 state (docs/dissolved-co2.md), persisted in
// CountersData.co2DissolvedMode; values 0 and 1 keep their previous meaning.
enum CO2DissolvedState : uint8_t {
  CO2_STATE_HALF_LIFE = 0,       // no generation: dissolved moves only with the gas phase
  CO2_STATE_EQUILIBRIUM = 1,     // generation confirmed: Henry at the relief threshold
  CO2_STATE_INITIAL = 2,         // batch start, before reliefs: Henry at the current pressure
  CO2_STATE_HALF_LIFE_ARMED = 3  // half-life after fermentables were added: easier return
};
static CO2DissolvedState co2DissolvedState = CO2_STATE_INITIAL;
// State seen by the last dissolved-CO2 update; detects model steps.
static CO2DissolvedState co2PreviousDissolvedState = CO2_STATE_INITIAL;
static bool co2StateIsHalfLife(CO2DissolvedState s) {
  return s == CO2_STATE_HALF_LIFE || s == CO2_STATE_HALF_LIFE_ARMED;
}
// Transitions, calibrated on batches 159 and 160 with the gas-phase CO2 rate
// (ejected + headspace + expansion tank, independent of the dissolved model).
// The armed values have no data yet (no fermentables addition logged).
static constexpr uint32_t CO2_INITIAL_CONFIRM_RELIEFS = 3;
static constexpr float CO2_GAS_RATE_EXIT = 0.3f;         // g/L/d: below it, no generation
static constexpr float CO2_GAS_RATE_RETURN = 0.5f;       // g/L/d: above it, generation again
static constexpr float CO2_GAS_RATE_RETURN_ARMED = 0.3f; // g/L/d, after fermentables
static constexpr unsigned long CO2_EXIT_HOLD_MS = 180UL * MINUTESms;         // uninterrupted
static constexpr unsigned long CO2_RETURN_HOLD_MS = 360UL * MINUTESms;       // uninterrupted
static constexpr unsigned long CO2_RETURN_ARMED_HOLD_MS = 60UL * MINUTESms;  // uninterrupted
static constexpr uint32_t CO2_ARMED_MAX_AGE_S = 7UL * 24UL * 3600UL;
// Half-life: gas changes smaller than this keep the baseline. Without it the
// oscillation of the gas phase (cooling cycles, sensor noise) ratchets: every
// fall is absorbed and every rise counts as production (+1.8 mol in the cold
// crash of batch 159; ~0 with 0.02-0.05 mol).
static constexpr double CO2_HALF_LIFE_GAS_DEADBAND_MOLS = 0.05;
static constexpr uint16_t CO2_GAS_RATE_MIN_SAMPLES = 60;               // ~1 h window
static constexpr unsigned long CO2_TASK_QUIET_MS = 80UL * MINUTESms;   // window + 10 min
// Gas-phase step between two samples, without a relief, too fast for a
// fermentation (gas added outside a task): the rate window gives no decision.
static constexpr float CO2_EXTERNAL_STEP_GPLD = 50.0f; // g/L/d over one sample
static float co2GasRate = NAN;                        // g/L/d, NAN = no decision
// 30-min means for the dissolved CO2 (block "Henry mean" below).
static float henryMeanPressure = NAN; // bar
static float henryMeanMols = NAN;     // mol, mean of Henry at each sample's pressure and temperature
static const char *co2StateDecision = "no-decision";
static unsigned long co2StateConditionSinceMillis = 0; // 0 = no condition running
static unsigned long co2StateHoldMs = 0;
// Half-life: the dissolved CO2 follows the measured gas phase (docs/dissolved-co2.md).
static bool co2GasBaselineValid = false;
static double co2GasBaselineMols = 0.0;
static uint32_t co2EvolutionLastReliefCount = 0; // relief count at the last CO2 sample
struct CO2EvolutionSample {
  unsigned long millisStamp;
  float pressure;
  double gasMols;       // ejected + headspace + expansion tank
  bool externalStep;    // pressure jump without relief (e.g. gas added): no decision
};
static CO2EvolutionSample co2EvolutionHistory[CO2_EVOLUTION_HISTORY_SIZE];
static uint16_t co2EvolutionStart = 0;
static uint16_t co2EvolutionCount = 0;

// One gCO2/L/d value per minute, from windows of at least CO2_RULE_MIN_SAMPLES,
// for the trend that ends a transition (docs/gco2-rate.md). RAM only: after a
// reboot the trend needs a new hour.
static constexpr uint16_t CO2_TREND_SAMPLES = 61;
struct CO2TrendSample {
  unsigned long millisStamp;
  float rate;
};
static CO2TrendSample co2Trend[CO2_TREND_SAMPLES];
static uint16_t co2TrendStart = 0;
static uint16_t co2TrendCount = 0;
static void updateCO2Transition();
static void resetCO2Transition(bool startAtNextSample);

static unsigned long int timeToStartExpansion     = 0;
static unsigned long int timeToFinishExpansion    = 0;
static unsigned long int timeToRegisterPressure = 0; // after a relief event
static unsigned long int noPressureReadUntil = 0;
static unsigned long reliefValveOpenedMillis = 0;
// A relief opened during a task (or its nucleation window) does not measure the
// headspace: gas and volume are changing by unknown amounts.
static bool reliefOpenedDuringTask = false;
static unsigned long reliefValveClosedMillis = 0;
static constexpr unsigned long POLYTROPIC_SETTLE_MS = 3UL * MINUTESms;
static constexpr unsigned long POLYTROPIC_SAMPLE_INTERVAL_MS = 30UL * 1000UL;
// Allow up to one second of loop/sensor delay beyond the sampling interval.
static constexpr unsigned long POLYTROPIC_MAX_SAMPLE_GAP_MS = POLYTROPIC_SAMPLE_INTERVAL_MS + 1000UL;
static constexpr uint8_t POLYTROPIC_SAMPLE_COUNT = 16;
static constexpr uint8_t POLYTROPIC_MIN_SAMPLES = 8;
struct PolytropicPressureSample { unsigned long millisStamp; float pressure; };
static PolytropicPressureSample polytropicSamples[POLYTROPIC_SAMPLE_COUNT];
static uint8_t polytropicSampleCount = 0, polytropicSampleNext = 0;
static unsigned long polytropicLastSampleMillis = 0;
static unsigned long polytropicReferenceCloseMillis = 0;
static unsigned long polytropicReferenceReliefNumber = 0;
static float polytropicReferencePressureBefore = NAN, polytropicReferencePressureAfter = NAN;
// The fitted pressure must be evaluated at the same instant as the measured
// post-relief pressure used to determine the exponent.
static unsigned long polytropicReferencePressureAfterMillis = 0;
static unsigned long polytropicSourceReliefNumber = 0;
static uint8_t polytropicResultSampleCount = 0;
static float polytropicBackExtrapolatedPressure = NAN;
static float polytropicFitSlopeBarPerMinute = NAN, polytropicEstimatedExponent = NAN;
static float polytropicFitRMSEBar = NAN;
static float currentOnReliefMeasured = 0.0f;

// ===== [DIAG] recuperação pós-relief + headspace sombra (somente log) =====
static constexpr float SHADOW_EXPONENT_SLOPE_PER_C = 0.0078f;  // empírico (fermentação 160)
static constexpr float SHADOW_EXPONENT_HINGE_C     = 21.0f;    // abaixo disso, sem correção
static float shadowExponent = NAN;
static float shadowHeadspaceInstant = NAN;
static float shadowHeadspaceFiltered = NAN;

static const unsigned long RECOVERY_OFFSETS_MS[] = {
  360, 500, 750, 1000, 1500, 2000, 3000, 5000, 7500,
  10000, 15000, 20000, 30000, 45000, 60000
};
static constexpr uint8_t RECOVERY_POINTS =
  sizeof(RECOVERY_OFFSETS_MS) / sizeof(RECOVERY_OFFSETS_MS[0]);
static float recoveryPressure[RECOVERY_POINTS];
static unsigned long recoveryActualMs[RECOVERY_POINTS];
static uint8_t recoveryNext = 0;
static bool recoveryActive = false;
static unsigned long recoveryCloseMillis = 0;
static unsigned long recoveryReliefNumber = 0;
static float recoveryP1 = NAN, recoveryP1Extrap = NAN;
static float recoveryEnvTemp = NAN, recoveryBeerTemp = NAN;
static float recoveryOpenSeconds = NAN;
// ===== [DIAG] fim =====

float  adjustedPressureAfterRelief;
static float adjustedEquilibriumPressure = NAN;
static float tankPressureAtClose = NAN; // expansion tank at valve close, gauge bar

float pressureOnReliefMeas = 0.0f;
static unsigned long pressureOnReliefMeasuredMillis = 0;
float pressureOnReliefExtrap = 0;
float pressureAfterRelief = 0;
unsigned long pressureAfterReliefMillis = 0;
// Acquisition time of the latest filtered pressure, not its processing time.
static unsigned long pressureAcquiredMillis = 0;
static bool pressureAcquisitionValid = false;
float pressureReachedTarget = 0;
unsigned long int pressureReachedTargetMillis = 0;

static float lastPressureTarget = NAN;
static float lastSlowPressureTarget = NOTaTEMP;
static unsigned long lastSlowPressureTargetChange = 0;

static unsigned long slowPressureIncrementIntervalMs(float speedPerDay) {
  if (!isfinite(speedPerDay) || speedPerDay <= 0.0f) return ULONG_MAX;
  const double interval = 86400000.0 * 0.01 / speedPerDay;
  return static_cast<unsigned long>(fmax(1000.0, fmin(interval, double(ULONG_MAX))));
}

static void processSlowPressureTarget() {
  bool changed = false;

  if (SetPointData.setPointSlowPressure != lastSlowPressureTarget) {
    // A new slow target may be configured with a new current target.
    lastPressureTarget = SetPointData.setPointPressure;
    lastSlowPressureTargetChange = 0;
  }
  else if (isfinite(lastPressureTarget) &&
           fabsf(SetPointData.setPointPressure - lastPressureTarget) > 0.00001f &&
           SetPointData.setPointSlowPressure != NOTaTEMP) {
    // A direct pressure target change takes precedence over the slow transition.
    SetPointData.setPointSlowPressure = NOTaTEMP;
    lastSlowPressureTargetChange = 0;
    changed = true;
  }

  if (SetPointData.setPointSlowPressure != NOTaTEMP) {
    if (lastSlowPressureTargetChange == 0) {
      lastSlowPressureTargetChange = millis();
    }
    else if (MILLISDIFF(lastSlowPressureTargetChange,
                         slowPressureIncrementIntervalMs(SetPointData.setPointSlowPressureSpeed))) {
      const float destination = SetPointData.setPointSlowPressure;
      const float current = SetPointData.setPointPressure;
      if (destination < current - 0.01f) {
        SetPointData.setPointPressure = current - 0.01f;
      }
      else if (destination > current + 0.01f) {
        SetPointData.setPointPressure = current + 0.01f;
      }
      else {
        SetPointData.setPointPressure = destination;
        SetPointData.setPointSlowPressure = NOTaTEMP;
      }
      lastSlowPressureTargetChange = millis();
      changed = true;
    }
  }
  else {
    lastSlowPressureTargetChange = 0;
  }

  lastPressureTarget = SetPointData.setPointPressure;
  lastSlowPressureTarget = SetPointData.setPointSlowPressure;
  if (changed) writeSetPointDataToNIV();
}

struct PressureReliefRecord {
  char timestamp[32];
  float temperature;
  float pressureBefore;
  float pressureAfter;
  float currentBefore;
  float currentAfter;
  float tiK;
  float tfK;
  float pi;
  float pfAdjusted;
  float adjustedEquilibriumPressure;
  uint16_t nReliefs;
  float factorMedio;
  float fermenterVolume;
  float fittedVolume;
  float volumeDifferencePercent;
  float recentFittedVolume;
  float recentDifferencePercent;
  float trendPercentPerCycle;
  bool converged;
  bool volumeMetricsValid;
  bool pressureSettled;
  float environmentTemperature; // NAN when the sensor is not valid
  float tRefK;                  // tank reference temperature (fast mode)
  float kRelief;                // k of this relief that returns FMTVolume (fast mode)
  float kCumulative;            // same from the cumulative factor
};

static PressureReliefRecord *pressureReliefHistory = nullptr;
static uint16_t pressureReliefIndex = 0;
static uint16_t pressureReliefCount = 0;
static int16_t pendingReliefIndex = -1;
static unsigned long lastSolenoidToggleMillis = 0;
float pressureDropFactor = 0.99f;

static bool volumeDeterminationActive = false;
static bool volumeDeterminationFast = false;
static bool speedCalibrationActive = false;
static bool speedCalibrationVenting = false;
static const uint8_t speedDurations[] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 30};
static constexpr uint8_t SPEED_VENTING_DURATION_COUNT = 1;
static constexpr uint8_t SPEED_VENTING_CYCLES = 10;
static constexpr uint8_t SPEED_VENTING_OPEN_SECONDS = 20;
static constexpr uint8_t SPEED_EXPANSION_DURATION_COUNT = sizeof(speedDurations) / sizeof(speedDurations[0]);
static constexpr uint8_t SPEED_CYCLES_PER_DURATION = 5;
static constexpr uint8_t SPEED_MAX_RECORDS = SPEED_EXPANSION_DURATION_COUNT * SPEED_CYCLES_PER_DURATION;
static constexpr float SPEED_VENTING_NOISE_PRESSURE_BAR = 0.050f;
struct SpeedRecord { float p1, p2, pl, r, openSeconds; bool valid; };
static SpeedRecord speedRecords[2][SPEED_MAX_RECORDS];
static uint8_t speedRecordCount[2] = {0, 0};
static uint8_t speedStage = 0; // 0: open, 1: close, 2: settle/read
static unsigned long speedStageMillis = 0;
static float speedVolumeFactor = 0.0f;
static unsigned long speedSettlingIntervalMs() {
  return debugging ? 10000UL : 180000UL;
}
static const char *speedStatus = "Idle";

static uint8_t speedDurationCount(bool venting) {
  return venting ? SPEED_VENTING_DURATION_COUNT : SPEED_EXPANSION_DURATION_COUNT;
}

static uint8_t speedRecordTarget(bool venting) {
  return venting ? SPEED_VENTING_CYCLES : speedDurationCount(false) * SPEED_CYCLES_PER_DURATION;
}

static uint8_t speedRequestedSeconds(uint8_t recordIndex, bool venting) {
  return venting ? SPEED_VENTING_OPEN_SECONDS : speedDurations[recordIndex % speedDurationCount(false)];
}

static unsigned long speedOpenDurationMs(uint8_t recordIndex, bool venting) {
  return (unsigned long)speedRequestedSeconds(recordIndex, venting) * 1000UL;
}
static float volumeStartPressure = 0.0f;
static float volumeStartTemperatureK = 0.0f;
static uint16_t volumeStartReliefIteration = 0;
static float volumeTargetPressure = 0.0f;
static unsigned long volumeLastReliefMillis = 0;
static uint16_t volumeIteration = 0;
static bool volumeAwaitingRecord = false;
static int16_t volumeRecordIndex = -1;
static bool volumeSummaryAvailable = false;
static float volumeSummaryTiK = 0.0f;
static float volumeSummaryTfK = 0.0f;
static float volumeSummaryPi = 0.0f;
static float volumeSummaryPfAdjusted = 0.0f;
static uint16_t volumeSummaryNReliefs = 0;
static float volumeSummaryFactor = 0.0f;
static float volumeSummaryFermenterVolume = 0.0f;
static VolumeKCalibration volumeCalibration = {};
static float volumeCalculatedSoFar = 0.0f;
static bool volumeCalculatedSoFarValid = false;
static float volumeFittedSoFar = NAN;
static bool volumeConverged = false;
static bool volumePressureSettled = false;
static unsigned long volumeAdjustedEquilibriumCaptureMillis = 0;
static float volumeAdjustedEquilibriumSnapshot = NAN;

static bool volumeHasConverged() {
  if (!pressureReliefHistory || pressureReliefCount < VOLUME_CONVERGENCE_WINDOW) return false;
  unsigned validCount = 0;
  float minVolume = INFINITY;
  float maxVolume = 0.0f;
  double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
  bool windowSettled = true;
  for (uint16_t i = 0; i < pressureReliefCount; ++i) {
    const uint16_t idx = (pressureReliefIndex + PRESSURE_RELIEF_HISTORY_MAX - pressureReliefCount + i)
                         % PRESSURE_RELIEF_HISTORY_MAX;
    const PressureReliefRecord &r = pressureReliefHistory[idx];
    const bool valid = r.volumeMetricsValid && isfinite(r.fittedVolume) && r.fittedVolume > 0.0f;
    if (valid) ++validCount;
    if (i >= pressureReliefCount - VOLUME_CONVERGENCE_WINDOW) {
      if (!valid) return false;
      windowSettled = windowSettled && r.pressureSettled;
      const double x = r.nReliefs;
      const double y = r.fittedVolume;
      sx += x; sy += y; sxx += x * x; sxy += x * y;
      minVolume = fminf(minVolume, r.fittedVolume);
      maxVolume = fmaxf(maxVolume, r.fittedVolume);
    }
  }
  PressureReliefRecord &last = pressureReliefHistory[
      (pressureReliefIndex + PRESSURE_RELIEF_HISTORY_MAX - 1) % PRESSURE_RELIEF_HISTORY_MAX];
  const double denominator = VOLUME_CONVERGENCE_WINDOW * sxx - sx * sx;
  if (denominator <= 0.0) return false;
  const double slope = (VOLUME_CONVERGENCE_WINDOW * sxy - sx * sy) / denominator;
  last.trendPercentPerCycle = 100.0 * slope / (sy / VOLUME_CONVERGENCE_WINDOW);
  return windowSettled && validCount >= VOLUME_MIN_VALID_RELIEFS &&
         (maxVolume - minVolume) / minVolume < VOLUME_MAX_FIT_SPREAD &&
         isfinite(last.trendPercentPerCycle) &&
         fabsf(last.trendPercentPerCycle) < VOLUME_MAX_TREND_PERCENT_PER_CYCLE &&
         isfinite(last.recentDifferencePercent) &&
         fabsf(last.recentDifferencePercent) < VOLUME_MAX_RECENT_DIFFERENCE_PERCENT &&
         isfinite(last.volumeDifferencePercent) &&
         fabsf(last.volumeDifferencePercent) < VOLUME_MAX_METHOD_DIFFERENCE_PERCENT;
}

struct PressureSampleRecord {
  char timestamp[6];
  unsigned long millisStamp;
  float pressure;
};

static PressureSampleRecord *pressureSamples = nullptr;
static uint16_t pressureSamplesIndex = 0;
static uint16_t pressureSamplesCount = 0;
static unsigned long pressureLastSampleMillis = 0;
static bool pressureDumpInProgress = false;
static PressureSampleRecord *pressureDumpSamples = nullptr;
static uint16_t pressureDumpCount = 0;
static uint16_t pressureDumpIndex = 0;
static bool pressureDumpHeaderSent = false;
static bool pressureDumpDone = false;

static bool pressureHistoryExportInProgress = false;
static uint16_t pressureHistoryExportAvailable = 0;
static uint16_t pressureHistoryExportStartIndex = 0;
static uint16_t pressureHistoryExportIndex = 0;
static bool pressureHistoryHeaderSent = false;
static bool pressureHistoryExportDone = false;

static float currentWindow[PRESSURE_MEDIAN_WINDOW];
static uint8_t currentWindowSortedIdx[PRESSURE_MEDIAN_WINDOW];
static uint8_t currentWindowCount = 0;
static uint8_t currentWindowIndex = 0;
static unsigned long lastCurrentMedianSampleMillis = 0;
static bool derivedStateRestorePending = true;

static unsigned long reliefMillisWindow[RELIEFS_WINDOW_SIZE];
static uint8_t reliefMillisCount = 0;
static uint8_t reliefMillisIndex = 0;
static bool reliefsPerHourAvailable = false;
static float reliefsPerHourValue = 0.0f;

float kelvin(float x) {
  return x + 273.15f;
}

// ===== Expansion-tank accounting (docs/expansion-tank-k.md) =====
// In a short expansion the gas pushed into the tank heats up (flow work) and
// the tank wall pulls it toward the ambient temperature. k = gamma/(1 + q) is
// the filling factor (q: fraction of the compression heat lost to the wall);
// the same exchange leaves phi = 1 - (gamma/k - 1)/(gamma - 1) of any
// temperature difference, so the tank is at Tref = Tamb + phi*(Tferm - Tamb).
// Relative to the fermenter gas counted at Tferm, the tank behaves as an
// isothermal volume Vr/k * Tferm/Tref. Valve timing and the gas-flow model keep
// the physical FMTReliefVolume.
static constexpr float GAMMA_CO2 = 1.29f;
static constexpr float GAMMA_AIR = 1.40f;

static bool ENV_TEMP_VALID(float t);

static float expansionTankRetainedFraction(float k, float gamma) {
  if (!(k > 0.0f) || !(gamma > 1.0f)) return 0.0f;
  return fminf(1.0f, fmaxf(0.0f, 1.0f - (gamma / k - 1.0f) / (gamma - 1.0f)));
}

// Tank reference temperature (K); Tferm without a valid ambient reading.
static float expansionTankReferenceKelvin(float k, float gamma, float fermenterC, float ambientC) {
  if (!ENV_TEMP_VALID(ambientC)) return kelvin(fermenterC);
  return kelvin(ambientC + expansionTankRetainedFraction(k, gamma) * (fermenterC - ambientC));
}

// Effective tank volume for any k > 0 (also used by the calibration).
static float expansionTankVolumeForK(float k, float gamma, float fermenterC, float ambientC) {
  if (!(k > 0.0f) || !isfinite(fermenterC)) return NAN;
  const float tref = expansionTankReferenceKelvin(k, gamma, fermenterC, ambientC);
  return tref > 0.0f ? FMTData.FMTReliefVolume / k * kelvin(fermenterC) / tref : NAN;
}

static float accountingReliefVolume(float k, float gamma, float fermenterC, float ambientC) {
  const float volume = expansionTankVolumeForK(isValidExpansionTankK(k) ? k : 1.0f,
                                               gamma, fermenterC, ambientC);
  return isfinite(volume) && volume > 0.0f ? volume : FMTData.FMTReliefVolume;
}

// k that makes the effective tank volume equal requiredVolume. The volume falls
// with k (1/k dominates the Tref term), so a bisection over 0.8-1.6 is enough;
// NAN outside it.
static float solveExpansionTankK(float requiredVolume, float gamma, float fermenterC, float ambientC) {
  if (!(requiredVolume > 0.0f)) return NAN;
  float lo = 0.8f, hi = 1.6f;
  const float volumeLo = expansionTankVolumeForK(lo, gamma, fermenterC, ambientC);
  const float volumeHi = expansionTankVolumeForK(hi, gamma, fermenterC, ambientC);
  if (!isfinite(volumeLo) || !isfinite(volumeHi) ||
      requiredVolume > volumeLo || requiredVolume < volumeHi) return NAN;
  for (int i = 0; i < 40; ++i) {
    const float mid = 0.5f * (lo + hi);
    if (expansionTankVolumeForK(mid, gamma, fermenterC, ambientC) > requiredVolume) lo = mid;
    else hi = mid;
  }
  return 0.5f * (lo + hi);
}

static float fermentationReliefVolume() {
  return accountingReliefVolume(FMTData.expansionTankKCO2, GAMMA_CO2,
                                ControlData.temperature, environmentTemp);
}

// Volume determination. Fast mode opens the valve for the fermentation's
// expansion time (1% residual): k and Tref of the test gas (air by default,
// CO2 with the fermenter purged) and the residual compensated as in a relief.
// Slow mode keeps it open 3 min, so the tank cools back and equalizes:
// physical volume, no residual.
static bool volumeDeterminationCO2 = false;

static float volumeRoutineK() {
  return volumeDeterminationCO2 ? FMTData.expansionTankKCO2 : FMTData.expansionTankKAir;
}

static float volumeRoutineGamma() {
  return volumeDeterminationCO2 ? GAMMA_CO2 : GAMMA_AIR;
}

static float volumeRoutineReliefVolume(float fermenterC, float ambientC) {
  return volumeDeterminationFast
      ? accountingReliefVolume(volumeRoutineK(), volumeRoutineGamma(), fermenterC, ambientC)
      : FMTData.FMTReliefVolume;
}

static float volumeRoutineReliefVolume() {
  return volumeRoutineReliefVolume(ControlData.temperature, environmentTemp);
}

// Pressure ratio after/before a relief extended to full equalization.
static float volumeRoutineEquilibriumFactor(float factor) {
  if (!volumeDeterminationFast || !isfinite(factor)) return factor;
  const float residual = FMTData.targetResidualAfterReliefPercent / 100.0f;
  return 1.0f - (1.0f - factor) / (1.0f - residual);
}

// Gas volume of a vessel whose pressure falls by dropFactor (after/before, at
// full equalization) when expanded into reliefVolume.
float volumeEstimationFromPressureDrop(float dropFactor, float reliefVolume) {
  if (dropFactor <= 0.0f || dropFactor >= 1.0f) {
    return NAN;
  }
  return (reliefVolume * dropFactor) / (1.0f -dropFactor);
}

float volumeEstimationFromPressureDrop(float dropFactor) {
  return volumeEstimationFromPressureDrop(dropFactor, fermentationReliefVolume());
}

static float volumeRoutineVolume(float factor, float fermenterC, float ambientC) {
  return volumeEstimationFromPressureDrop(volumeRoutineEquilibriumFactor(factor),
                                          volumeRoutineReliefVolume(fermenterC, ambientC));
}

static float volumeRoutineVolume(float factor) {
  return volumeRoutineVolume(factor, ControlData.temperature, environmentTemp);
}

// k of the test gas that makes the fast test return FMTVolume (the empty
// fermenter); NAN in slow mode.
static float volumeRoutineCalibratedK(float factor, float fermenterC, float ambientC) {
  if (!volumeDeterminationFast || !(FMTData.FMTVolume > 0.0f)) return NAN;
  const float equalized = volumeRoutineEquilibriumFactor(factor);
  if (!(equalized > 0.0f && equalized < 1.0f)) return NAN;
  return solveExpansionTankK(FMTData.FMTVolume * (1.0f - equalized) / equalized,
                             volumeRoutineGamma(), fermenterC, ambientC);
}

// kCO2 from the known beer volume (Relief log, diagnostics): the k that makes
// this relief's equalized drop factor match the headspace FMTVolume - beer.
static float kCO2FromBeerVolume(float dropFactor) {
  if (!(dropFactor > 0.0f && dropFactor < 1.0f) ||
      !isfinite(BatchData.initialBeerVolume) || !(BatchData.initialBeerVolume > 0.0f)) return NAN;
  const float headspace = FMTData.FMTVolume - (BatchData.initialBeerVolume - CountersData.dumpedVolume);
  if (!(headspace > 0.0f)) return NAN;
  return solveExpansionTankK(headspace * (1.0f - dropFactor) / dropFactor, GAMMA_CO2,
                             ControlData.temperature, environmentTemp);
}

static void updateBeerVolumeFromHeadspace() {
  // The pressure-drop estimate needs three completed reliefs. Until then, use
  // the batch's measured fill volume for every CO2 calculation.
  if (CountersData.totalReliefCount < 3 && !isfinite(headspaceFiltered) &&
      isfinite(BatchData.initialBeerVolume) &&
      BatchData.initialBeerVolume > 0.0f &&
      BatchData.initialBeerVolume <= FMTData.FMTVolume) {
    beerVolume = BatchData.initialBeerVolume;
    CountersData.headSpaceVolume = FMTData.FMTVolume - beerVolume;
    if (isfinite(FMTData.FMTReliefVolume) && FMTData.FMTReliefVolume > 0.0f) {
      pressureDropFactor = CountersData.headSpaceVolume /
        (CountersData.headSpaceVolume + fermentationReliefVolume());
    }
    return;
  }
  beerVolume = FMTData.FMTVolume - CountersData.headSpaceVolume;
  if (beerVolume < 0.0f) {
    beerVolume = 0.0f;
  }
}



float CO2DissolvedMols(float pressureBar, float sg, float temperatureC, float volumeL) {
  if (pressureBar <= 0.0f || volumeL <= 0.0f) {
    return 0.0f;
  }

  const float tempK = temperatureC + 273.15f;
  const float kH_298 = 0.0334f; // mol/(L*atm) at 25C for CO2 in water fonte: Sander, R. (2015). Compilation of Henry's law constants (version 4.0) for water as solvent. Atmospheric Chemistry and Physics, 15(8), 4399-4981. https://doi.org/10.5194/acp-15-4399-2015
  const float pressureAtm = (pressureBar+Patm) * 0.986923f; // constant is bar --> atm conversion

  float kH = kH_298 * expf(2400.0f * (1.0f / tempK - 1.0f / 298.15f));

  float sgConsidered;
  if (std::isfinite(sg)) 
    sgConsidered = sg;
  else
    sgConsidered = BatchData.batchOG;

  float sgPoints = (sgConsidered - 1.0f) * 1000.0f;
  float sgCorrection = 1.0f - (sgPoints * 0.0015f); // Correção linear: cada ponto de SG reduz a solubilidade em 0.15%. Ex: SG 1.050 tem correção de 7.5%, SG 1.100 tem correção de 15%. Fonte: https://www.brewersfriend.com/2012/11/19/co2-solubility-in-beer/
  if (sgCorrection < 0.5f) sgCorrection = 0.5f;
  if (sgCorrection > 1.0f) sgCorrection = 1.0f;
  if (!isfinite(sgCorrection)) {
    sgCorrection = 1.0f;
  }

  float molPerL = kH * pressureAtm * sgCorrection;
  return molPerL * volumeL;
}

static float expansionPressureThreshold() {
  if (SetPointData.setPointPressure <= 0.0f ||
      !isfinite(pressureDropFactor) || pressureDropFactor <= 0.0f) {
    return ControlData.pressure;
  }
  return SetPointData.setPointPressure / sqrtf(pressureDropFactor);
}

static bool gasFlowCycle = false;
static bool gasTankHasHistory = false;
static unsigned long gasClosedMillis = 0;
static unsigned long gasMinimumVentingMilliseconds = 0;
static double gasLoggedVentingSeconds = NAN, gasLoggedResidual = 0;
static double gasLoggedOpenSeconds = 0;
static double gasLoggedExpansionOptimalSeconds = NAN;
static double gasPlannedExpansionSeconds = NAN;
// Measured inventory at transfer-valve close. This is the CO2-balance source
// of truth; it is derived from EjectedPressure after the post-relief reading.
static double gasTankMolesAtClose = 0;
static double gasTankPressureAtClose = 0;
static bool gasVentingActive = false;
static double gasVentingFactorAtClose = NAN;
static double gasVentingFermenterVolume = 0, gasVentingExpansionVolume = 0;
static double gasVentedMolesAccounted = 0;
static double gasVentedMolesCredited = 0;
static bool gasPreviousTankValid = false;
static double gasPreviousTankPressureAtClose = NAN;
static double gasPreviousTankPressureAtOpen = NAN;
static double gasPreviousTankRemainingMoles = NAN;
static double gasPreviousTankCreditedMoles = NAN;
static double gasPreviousTankVentingSeconds = NAN;
static double gasPreviousTankResidualFraction = NAN;
static double gasLoggedPreviousVentingResidual = NAN;
static double gasInitialMoles = 0;
static double gasCycleA = 1, gasCycleB = 1.5;
static double gasHeadspace = 0;
static double gasCalculatedPressureCompensation = NAN;
static double gasAppliedPressureCompensation = NAN;
static bool gasPressureCompensationValid = false;
static const char *gasHeadspaceUpdateStatus = "not_evaluated";

static bool gasFlowMode() {
  return !volumeDeterminationActive && SetPointData.mode == MODE_FERMENTING;
}

static double calculateExpansionTankRemainingMoles(unsigned long now) {
  if (!gasTankHasHistory) return 0;
  if (!gasVentingActive) return gasInitialMoles;
  return gasTankMolesAtClose * GasFlow::ventingResidual((now - gasClosedMillis) / 1000.0,
    gasVentingFermenterVolume, gasVentingExpansionVolume, gasVentingFactorAtClose);
}

// Track gas actually vented to atmosphere, once per increment. The tank
// inventory is pressure-measured; apply the liquid correction exactly here.
static void accountExpansionTankVenting(unsigned long now) {
  if (!gasVentingActive) return;
  const double remaining = calculateExpansionTankRemainingMoles(now);
  const double cumulativeVentedNow = gasTankMolesAtClose - remaining;
  if (!isfinite(cumulativeVentedNow) ||
      cumulativeVentedNow <= gasVentedMolesAccounted) return;
  const double deltaVented = cumulativeVentedNow - gasVentedMolesAccounted;
  gasVentedMolesAccounted = cumulativeVentedNow;
  const double credited = deltaVented *
    (1.0 - FMTData.liquidMassInGasVentingPercent / 100.0);
  gasVentedMolesCredited += credited;
  CountersData.totalMolsEjected += credited;
}

// The previous tank vents only until the next transfer valve opens. Capture
// its completed cycle before changing the state for the new relief.
static void capturePreviousTankVenting(unsigned long now) {
  accountExpansionTankVenting(now);
  gasPreviousTankValid = gasTankHasHistory;
  if (gasPreviousTankValid) {
    gasPreviousTankPressureAtClose = gasTankPressureAtClose;
    gasPreviousTankVentingSeconds = (now - gasClosedMillis) / 1000.0;
    gasPreviousTankResidualFraction = GasFlow::ventingResidual(
      gasPreviousTankVentingSeconds, gasVentingFermenterVolume,
      gasVentingExpansionVolume, gasVentingFactorAtClose);
    gasPreviousTankRemainingMoles = calculateExpansionTankRemainingMoles(now);
    gasPreviousTankPressureAtOpen = gasTankPressureAtClose * gasPreviousTankResidualFraction;
    gasPreviousTankCreditedMoles = gasVentedMolesCredited;
  } else {
    gasPreviousTankPressureAtClose = NAN;
    gasPreviousTankVentingSeconds = NAN;
    gasPreviousTankResidualFraction = NAN;
    gasPreviousTankRemainingMoles = NAN;
    gasPreviousTankPressureAtOpen = NAN;
    gasPreviousTankCreditedMoles = NAN;
  }
  gasInitialMoles = gasPreviousTankValid ? gasPreviousTankRemainingMoles : 0;
  gasVentingActive = false;
}

static double expansionTankInventoryMoles() {
  if (!gasTankHasHistory) return 0;
  return gasVentingActive ? gasTankMolesAtClose - gasVentedMolesAccounted : gasInitialMoles;
}

// Keep a persistent progress integral in mol/L. The snapshot is deliberately
// kept in RAM: after a reboot the first read establishes a new baseline, so a
// restored persistent integral is never charged with its past total again.
// The debt (CountersData.co2CorrectionDebt) is persisted: a reboot must not
// forgive a fall of the total that later rises would have paid.
static constexpr unsigned long CO2_PRODUCED_PER_L_SAMPLE_MS = 30000UL;
static bool co2ProducedTotalInitialized = false;
static double lastCO2ProducedTotalMols = 0.0;
static unsigned long lastCO2ProducedPerLiterUpdateMillis = 0;
static double co2ProducedDiagnosticTotalMols = NAN;
static double co2ProducedDiagnosticRawDeltaMols = NAN;
static double co2ProducedDiagnosticCreditedDeltaMols = NAN;

static void updateCO2MolsProducedPerLiter(float beerVolumeBeforeEvent) {
  const unsigned long now = millis();
  if (lastCO2ProducedPerLiterUpdateMillis != 0 &&
      now - lastCO2ProducedPerLiterUpdateMillis < CO2_PRODUCED_PER_L_SAMPLE_MS) {
    return;
  }
  lastCO2ProducedPerLiterUpdateMillis = now;

  const double totalMols = CountersData.totalMolsEjected +
    CountersData.CO2InSolution + double(headSpaceCO2Mols) + expansionTankInventoryMoles();
  if (!isfinite(totalMols)) return;

  co2ProducedDiagnosticTotalMols = totalMols;
  co2ProducedDiagnosticCreditedDeltaMols = 0.0;

  if (!co2ProducedTotalInitialized) {
    lastCO2ProducedTotalMols = totalMols;
    co2ProducedTotalInitialized = true;
    co2ProducedDiagnosticRawDeltaMols = 0.0;
    return;
  }

  const double rawDeltaMols = totalMols - lastCO2ProducedTotalMols;
  co2ProducedDiagnosticRawDeltaMols = rawDeltaMols;
  double deltaProducedCO2Mols = 0.0;
  if (rawDeltaMols < 0.0) {
    CountersData.co2CorrectionDebt += -rawDeltaMols;
  } else if (rawDeltaMols > 0.0) {
    const double debtPaymentMols = fmin(rawDeltaMols, CountersData.co2CorrectionDebt);
    CountersData.co2CorrectionDebt -= debtPaymentMols;
    deltaProducedCO2Mols = rawDeltaMols - debtPaymentMols;
  }

  if (deltaProducedCO2Mols > 0.0 && isfinite(beerVolumeBeforeEvent) &&
      beerVolumeBeforeEvent > 0.0f) {
    CountersData.CO2MolsProducedPerLiter += deltaProducedCO2Mols / beerVolumeBeforeEvent;
  }
  co2ProducedDiagnosticCreditedDeltaMols = deltaProducedCO2Mols;
  lastCO2ProducedTotalMols = totalMols;
}

// New baseline for the integral; the debt is kept (reboot, counters restore).
static void restartCO2ProducedBaseline() {
  co2ProducedTotalInitialized = false;
  lastCO2ProducedTotalMols = 0.0;
  lastCO2ProducedPerLiterUpdateMillis = 0;
  co2ProducedDiagnosticTotalMols = NAN;
  co2ProducedDiagnosticRawDeltaMols = NAN;
  co2ProducedDiagnosticCreditedDeltaMols = NAN;
}

// The integral was set explicitly (new batch, Counters page): no debt.
void resetCO2MolsProducedPerLiterTracking() {
  restartCO2ProducedBaseline();
  CountersData.co2CorrectionDebt = 0.0;
}

static bool shouldStartGasExpansion(unsigned long now) {
  if (SetPointData.setPointPressure <= 0) return false;
  if (gasVentingActive && now - gasClosedMillis < gasMinimumVentingMilliseconds) return false;
  return ControlData.pressure >= expansionPressureThreshold();
}

static double expansionTime(float pressure) {
  return GasFlow::expansionTime(pressure,
    FMTData.targetResidualAfterReliefPercent / 100.0,
    FMTData.expansionTimeCoefficientA, FMTData.expansionTimeCoefficientB);
}

static unsigned long expansionTimeMilliseconds(float pressure) {
  const double seconds = expansionTime(pressure);
  gasPlannedExpansionSeconds = seconds;
  return isfinite(seconds) ? (unsigned long)fmax(1.0, floor(seconds * 1000.0)) : 1UL;
}

static void beginGasExpansion() {
  gasLoggedPreviousVentingResidual = gasPreviousTankResidualFraction;
  gasLoggedVentingSeconds = gasPreviousTankVentingSeconds;
  // Snapshot the optimal times at opening, before pressure/configuration changes.
  gasLoggedExpansionOptimalSeconds = expansionTime(ControlData.pressure);
  gasHeadspace = CountersData.headSpaceVolume;
  if (!(gasHeadspace > 0))
    gasHeadspace = volumeEstimationFromPressureDrop(pressureDropFactor);
  gasHeadspace = fmin(gasHeadspace, FMTData.FMTVolume);
}

static void finishGasExpansion(unsigned long now) {
  const double seconds = (now - reliefValveOpenedMillis) / 1000.0;
  const double residual = FMTData.targetResidualAfterReliefPercent / 100.0;
  gasLoggedResidual = residual;
  gasLoggedOpenSeconds = seconds;
  gasMinimumVentingMilliseconds = (unsigned long)fmax(0.0, seconds * 1000.0);
  gasClosedMillis = now;
  // The venting factor is set in processPressure() from the measured tank
  // pressure at close; the tank is not vented before that.
  gasVentingFermenterVolume = FMTData.FMTVolume;
  gasVentingExpansionVolume = FMTData.FMTReliefVolume;
  Serial.printf("[GAS FLOW] open=%.3fs expansionResidual=%.6f initial=%.6fmol\n",
    seconds, residual, gasInitialMoles);
}

static const char *co2DissolvedStateLabel(CO2DissolvedState state) {
  switch (state) {
    case CO2_STATE_EQUILIBRIUM:     return "immediate"; // label kept for log continuity
    case CO2_STATE_INITIAL:         return "initial";
    case CO2_STATE_HALF_LIFE_ARMED: return "half-life-armed";
    default:                        return "half-life";
  }
}

static const char *co2DissolvedEstimationModeLabel() {
  return co2DissolvedStateLabel(co2DissolvedState);
}

// Measured CO2 outside the liquid: vented, in the headspace and in the expansion tank.
static double gasPhaseCO2Mols() {
  return CountersData.totalMolsEjected + double(headSpaceCO2Mols) + expansionTankInventoryMoles();
}

// State changes are persisted at once, so a reboot keeps the state.
static void setCO2DissolvedState(CO2DissolvedState next, const char *reason) {
  if (next == co2DissolvedState) return;
  Serial.printf("[CO2 STATE] %s -> %s (%s), dissolved %.3f mol, gas rate %.2f g/L/d\n",
                co2DissolvedStateLabel(co2DissolvedState), co2DissolvedStateLabel(next),
                reason, CountersData.CO2InSolution, co2GasRate);
  co2DissolvedState = next;
  CountersData.co2DissolvedMode = (uint8_t)next;
  if (next == CO2_STATE_HALF_LIFE_ARMED) CountersData.co2ArmedAt = NTPEpoch(); // 0 = no NTP yet
  else CountersData.co2ArmedAt = 0;
  co2StateConditionSinceMillis = 0;
  co2StateHoldMs = 0;
  co2GasBaselineValid = false; // half-life starts from the gas phase at entry
  writeCountersDataToNIV();
}

uint8_t getCO2DissolvedState() {
  return (uint8_t)co2DissolvedState;
}

const char *getCO2DissolvedStateLabel(uint8_t state) {
  return co2DissolvedStateLabel((CO2DissolvedState)state);
}

// Manual intervention from the Counters page: same path as the automatic
// transitions (persistence, armed time, baseline, rebase of the gCO2 window).
bool setCO2DissolvedStateManually(uint8_t state) {
  if (state > CO2_STATE_HALF_LIFE_ARMED) return false;
  setCO2DissolvedState((CO2DissolvedState)state, "manual");
  return true;
}

// CountersData was reset for a new batch (co2DissolvedMode = initial).
static void resetBeerCO2Evolution();
static void invalidateCO2BufferFile();

void resetCO2DissolvedStateForNewBatch() {
  co2DissolvedState = (CO2DissolvedState)CountersData.co2DissolvedMode;
  co2PreviousDissolvedState = co2DissolvedState;
  co2StateConditionSinceMillis = 0;
  co2StateHoldMs = 0;
  co2GasBaselineValid = false;
  co2GasRate = NAN;
  co2StateDecision = "no-decision";
  // Samples of the previous batch must not enter the windows of the new one.
  resetBeerCO2Evolution();
  invalidateCO2BufferFile();
  resetCO2Transition(false);
}

// Back from Conditioning: nothing is generating (the batch was conditioning),
// so equilibrium becomes half-life; the rate returns it to equilibrium if a
// fermentation restarts. The restore restarts the gCO2 window, the produced-
// CO2 reference and the gas baseline, so the frozen interval is not counted.
void resumeCO2AccountingAfterConditioning() {
  if (co2DissolvedState == CO2_STATE_EQUILIBRIUM)
    setCO2DissolvedState(CO2_STATE_HALF_LIFE, "resumed from conditioning");
  resetCO2Transition(true); // the rate before Conditioning is not a stable reading
  requestDerivedStateRestoreFromCounters();
}

// Fermentables were added (BatchData.addedPlato increased): a refermentation
// is expected, so the half-life returns to equilibrium with a lower threshold.
void notifyFermentablesAdded() {
  if (co2DissolvedState == CO2_STATE_HALF_LIFE) {
    setCO2DissolvedState(CO2_STATE_HALF_LIFE_ARMED, "fermentables added");
  } else if (co2DissolvedState == CO2_STATE_HALF_LIFE_ARMED) {
    CountersData.co2ArmedAt = NTPEpoch(); // a new addition restarts the 7 days
    Serial.println("[CO2 STATE] half-life-armed renewed (fermentables added)");
    writeCountersDataToNIV();
  }
}

// Initial -> equilibrium once reliefs show CO2 leaving the liquid; the armed
// half-life expires after CO2_ARMED_MAX_AGE_S (by NTP; without NTP it waits).
static void updateCO2DissolvedStateFromEvents() {
  if (SetPointData.mode == MODE_CONDITIONING) return; // frozen
  if (co2DissolvedState == CO2_STATE_INITIAL &&
      SetPointData.mode == MODE_FERMENTING &&
      CountersData.totalReliefCount >= CO2_INITIAL_CONFIRM_RELIEFS)
    setCO2DissolvedState(CO2_STATE_EQUILIBRIUM, "reliefs started");
  if (co2DissolvedState == CO2_STATE_HALF_LIFE_ARMED) {
    const unsigned long epoch = NTPEpoch();
    if (epoch != 0 && CountersData.co2ArmedAt == 0) {
      CountersData.co2ArmedAt = epoch; // armed before NTP: count from now
    } else if (epoch != 0 && epoch >= CountersData.co2ArmedAt &&
               epoch - CountersData.co2ArmedAt > CO2_ARMED_MAX_AGE_S) {
      setCO2DissolvedState(CO2_STATE_HALF_LIFE, "armed expired");
    }
  }
}

// Called once per CO2 sample (~1/min) with the gas-phase rate (NAN = no decision).
// A condition must hold without interruption; any other result restarts it.
static void updateCO2DissolvedStateFromGasRate(unsigned long now, float rate) {
  co2GasRate = rate;
  const bool armed = co2DissolvedState == CO2_STATE_HALF_LIFE_ARMED;
  const float returnRate = armed ? CO2_GAS_RATE_RETURN_ARMED : CO2_GAS_RATE_RETURN;
  const bool decide = isfinite(rate) && SetPointData.mode == MODE_FERMENTING;
  if (!decide) co2StateDecision = "no-decision";
  else if (rate > returnRate) co2StateDecision = "generating";
  else if (rate < CO2_GAS_RATE_EXIT) co2StateDecision = "idle";
  else co2StateDecision = "between";

  unsigned long hold = 0;
  CO2DissolvedState next = co2DissolvedState;
  if (decide && co2DissolvedState == CO2_STATE_EQUILIBRIUM && rate < CO2_GAS_RATE_EXIT) {
    hold = CO2_EXIT_HOLD_MS;
    next = CO2_STATE_HALF_LIFE;
  } else if (decide && co2StateIsHalfLife(co2DissolvedState) && rate > returnRate) {
    hold = armed ? CO2_RETURN_ARMED_HOLD_MS : CO2_RETURN_HOLD_MS;
    next = CO2_STATE_EQUILIBRIUM;
  }
  if (next == co2DissolvedState) {
    co2StateConditionSinceMillis = 0;
    co2StateHoldMs = 0;
    return;
  }
  if (!co2StateConditionSinceMillis) co2StateConditionSinceMillis = now;
  co2StateHoldMs = hold;
  if (now - co2StateConditionSinceMillis >= hold)
    setCO2DissolvedState(next, next == CO2_STATE_HALF_LIFE ? "gas rate below exit" : "gas rate above return");
}

static unsigned long co2DissolvedCriteriaElapsedMillis(unsigned long now) {
  return co2StateConditionSinceMillis ? now - co2StateConditionSinceMillis : 0;
}

// Equilibrium and initial: Henry at the 30-min mean pressure (constant in the
// relief cycle, follows rises and falls; a set point change is not a jump).
// Half-life limits and before the first sample: the current pressure.
static float dissolvedCO2CalculationPressure() {
  if (co2StateIsHalfLife(co2DissolvedState) || !isfinite(henryMeanPressure)) return ControlData.pressure;
  return henryMeanPressure;
}

DissolvedCO2LogData getDissolvedCO2LogData() {
  DissolvedCO2LogData data = {};
  data.mode = co2DissolvedEstimationModeLabel();
  data.criteriaState = co2StateDecision;
  // The relief-cadence and 10-minute pressure criteria were replaced by the
  // gas-phase rate; their columns stay empty until the log is revised.
  data.withReliefsState = "";
  data.withoutReliefsState = "";
  data.calculationPressure = dissolvedCO2CalculationPressure();
  data.equilibriumMols = (!co2StateIsHalfLife(co2DissolvedState) && isfinite(henryMeanMols))
      ? henryMeanMols
      : CO2DissolvedMols(data.calculationPressure, beerSG, ControlData.temperature, beerVolume);
  data.previousPressure = NAN;
  data.gasRate = co2GasRate;
  return data;
}

// ===== Henry mean (docs/dissolved-co2.md) =====
// In equilibrium and initial the dissolved CO2 is the 30-min mean of Henry at
// each sample's pressure and temperature: constant in the relief cycle (the
// saw-tooth averages out), following rises, falls and cooling. The transfer
// between the beer and the gas is not modelled: gCO2/L/d is the net CO2 released
// the beer (docs/gco2-rate.md), and the SG error of a transition undoes itself
// when the beer is back in equilibrium.
static constexpr uint16_t HENRY_MEAN_SAMPLES = 30; // 60-s samples: 30 min
struct HenrySample {
  unsigned long millisStamp;
  float henryMols; // Henry at the sample's pressure, temperature, SG and volume
  float pressure;
};
static HenrySample henryBuffer[HENRY_MEAN_SAMPLES];
static uint16_t henryStart = 0, henryCount = 0;

static const HenrySample &henryAt(uint16_t i) {
  return henryBuffer[(henryStart + i) % HENRY_MEAN_SAMPLES];
}

static void updateHenryMeans() {
  if (henryCount == 0) {
    henryMeanMols = henryMeanPressure = NAN;
    return;
  }
  double h = 0.0, p = 0.0;
  for (uint16_t i = 0; i < henryCount; ++i) {
    h += henryAt(i).henryMols;
    p += henryAt(i).pressure;
  }
  henryMeanMols = float(h / henryCount);
  henryMeanPressure = float(p / henryCount);
}

static void resetHenryBuffer() {
  henryStart = henryCount = 0;
  updateHenryMeans();
}

// One call per CO2 sample (60 s).
static void addHenrySample(unsigned long now) {
  if (henryCount == HENRY_MEAN_SAMPLES) {
    henryStart = (henryStart + 1) % HENRY_MEAN_SAMPLES;
    --henryCount;
  }
  HenrySample &s = henryBuffer[(henryStart + henryCount) % HENRY_MEAN_SAMPLES];
  s.millisStamp = now;
  s.henryMols = CO2DissolvedMols(ControlData.pressure, beerSG, ControlData.temperature, beerVolume);
  s.pressure = ControlData.pressure;
  ++henryCount;
  updateHenryMeans();
}

// A dump removes beer with its dissolved CO2; in the half-life the amount is
// not recomputed from Henry, so it follows the volume.
void scaleDissolvedCO2ForBeerVolume(float volumeBefore, float volumeAfter) {
  if (!co2StateIsHalfLife(co2DissolvedState)) return;
  if (!isfinite(volumeBefore) || !isfinite(volumeAfter) || volumeBefore <= 0.0f ||
      volumeAfter <= 0.0f || volumeAfter >= volumeBefore) return;
  const double removed = CountersData.CO2InSolution * (1.0 - double(volumeAfter) / volumeBefore);
  CountersData.CO2InSolution -= removed;
  co2GasBaselineValid = false;
  Serial.printf("[CO2 DUMP] dissolved -%.3f mol (%.1f -> %.1f L)\n", removed, volumeBefore, volumeAfter);
}

static void recomputeDissolvedCO2MolsFromCurrentState() {
  static unsigned long lastUpdateMillis = 0;
  static bool startGuardActive = false;
  const unsigned long now = millis();

  // Preserva o CO2 já inicializado ou restaurado dos contadores.
  if (!lastUpdateMillis) {
    lastUpdateMillis = now;
    return;
  }
  lastUpdateMillis = now;

  // Batch start: no dissolved CO2 until the pressure rises above the start
  // pressure; the equilibrium amount then counts as produced (it is the air in
  // the headspace that makes it an overestimate, until the reliefs purge it).
  if (CountersData.totalReliefCount == 0 &&
      ControlData.pressure <= BatchData.startPressure) {
    CountersData.CO2InSolution = 0.0;
    startGuardActive = true;
    co2GasBaselineValid = false;
    co2PreviousDissolvedState = co2DissolvedState;
    return;
  }
  const bool leavingStartGuard = startGuardActive;
  startGuardActive = false;

  if (!co2StateIsHalfLife(co2DissolvedState)) {
    // 30-min mean of Henry (current pressure until the first sample).
    CountersData.CO2InSolution = isfinite(henryMeanMols) ? double(henryMeanMols)
        : CO2DissolvedMols(ControlData.pressure, beerSG, ControlData.temperature, beerVolume);
    (void)leavingStartGuard;
    co2PreviousDissolvedState = co2DissolvedState;
    co2GasBaselineValid = false;
    return;
  }
  co2PreviousDissolvedState = co2DissolvedState;

  // Half-life (hybrid, by measurement; docs/dissolved-co2.md): the dissolved
  // CO2 moves only with the measured gas phase G, bounded by Henry H at the
  // current pressure.
  //   G rises:  first from the CO2 above H (release), the rest is production.
  //   G falls:  into the liquid up to H (absorption), the rest is debt.
  // During dry/dynamic hopping (and their nucleation window) any gas that
  // appears comes from the liquid, down to zero. Other tasks move gas or
  // volume by unknown amounts: the dissolved CO2 is kept. Changes are applied
  // only beyond CO2_HALF_LIFE_GAS_DEADBAND_MOLS from the baseline.
  // Wait for pressure and headspace to settle after a boot.
  if (now < 120000UL) {
    co2GasBaselineValid = false;
    return;
  }
  const double gas = gasPhaseCO2Mols();
  const double equilibrium = CO2DissolvedMols(ControlData.pressure, beerSG, ControlData.temperature, beerVolume);
  if (!isfinite(gas) || !isfinite(equilibrium)) {
    co2GasBaselineValid = false;
    return;
  }
  const bool hopTask = taskWindowType == 4 || taskWindowType == 5;
  const bool otherTask = taskWindowType != 0 && !hopTask;
  if (co2GasBaselineValid && !otherTask) {
    const double deltaGas = gas - co2GasBaselineMols;
    if (fabs(deltaGas) < CO2_HALF_LIFE_GAS_DEADBAND_MOLS) return; // keep the baseline
    double dissolved = CountersData.CO2InSolution;
    if (deltaGas > 0.0) {
      const double releasable = hopTask ? dissolved : fmax(0.0, dissolved - equilibrium);
      dissolved -= fmin(deltaGas, releasable);
    } else if (deltaGas < 0.0) {
      dissolved += fmin(-deltaGas, fmax(0.0, equilibrium - dissolved));
    }
    CountersData.CO2InSolution = fmax(0.0, dissolved);
  }
  co2GasBaselineMols = gas;
  co2GasBaselineValid = true;
}

static void recomputeHeadspaceCO2MolsFromCurrentState() {
  const float currentTemperatureK = kelvin(ControlData.temperature);
  const float initialTemperatureK = kelvin(BatchData.startTemperature);
  if (!isfinite(CountersData.headSpaceVolume) || CountersData.headSpaceVolume <= 0.0f ||
      !isfinite(currentTemperatureK) || currentTemperatureK <= 0.0f ||
      !isfinite(initialTemperatureK) || initialTemperatureK <= 0.0f ||
      !isfinite(ControlData.pressure) || !isfinite(BatchData.startPressure)) {
    headSpaceCO2Mols = 0.0f;
    return;
  }

  headSpaceCO2Mols = ControlData.pressure    * CountersData.headSpaceVolume / (CONST_R * currentTemperatureK)
                   - BatchData.startPressure * CountersData.headSpaceVolume / (CONST_R * initialTemperatureK);
  if (!isfinite(headSpaceCO2Mols) || headSpaceCO2Mols < 0.0f) {
    headSpaceCO2Mols = 0.0f;
  }
}

// [DAILY-HS] Applies a headspace value without touching the EMA state.
static bool applyHeadspaceValue(float headspace) {
  if (!isfinite(headspace) || headspace <= 0.0f ||
      !isfinite(FMTData.FMTVolume) || headspace >= FMTData.FMTVolume) {
    return false;
  }

  // The headspace CO2 is recomputed with the new volume: not a gas movement.
  if (headspace != CountersData.headSpaceVolume) co2GasBaselineValid = false;
  CountersData.headSpaceVolume = headspace;
  beerVolume = FMTData.FMTVolume - headspace;
  if (isfinite(FMTData.FMTReliefVolume) && FMTData.FMTReliefVolume > 0.0f) {
    pressureDropFactor = headspace /
      (headspace + fermentationReliefVolume());
  }
  return true;
}

static bool applyFilteredHeadspace(float headspace) {
  if (!isfinite(headspace) || headspace <= 0.0f ||
      !isfinite(FMTData.FMTVolume) || headspace >= FMTData.FMTVolume) {
    return false;
  }
  headspaceFiltered = headspace;
  return applyHeadspaceValue(headspace);
}

// ===== [DAILY-HS] 24-hour headspace average =====
// The per-relief headspace follows the daily cycle of the room temperature
// (hysteresis), which cancels over 24 h. Each hour weighs the same (mean of
// the hourly means). Bins live in CountersData.dailyHs.
enum DailyHsState : uint8_t { DAILY_HS_EMA, DAILY_HS_HOLD, DAILY_HS_VALID };
static DailyHsState dailyHsState = DAILY_HS_EMA;
static float dailyHsValue = NAN;  // mean of the hourly means (NAN = no hours)
static uint8_t dailyHsHours = 0;  // hours of the last 24 with samples

static const char *dailyHsStateLabel() {
  switch (dailyHsState) {
    case DAILY_HS_VALID: return "valid";
    case DAILY_HS_HOLD:  return "hold";
    default:             return "ema";
  }
}

// Recomputes from the bins. Without a valid NTP time the last result is kept.
static void dailyHsEvaluate() {
  const unsigned long epoch = NTPEpoch();
  if (epoch == 0) return;
  const uint32_t hourNow = epoch / 3600UL;
  DailyHeadspace_t &daily = CountersData.dailyHs;
  double total = 0.0;
  uint8_t hours = 0;
  for (int i = 0; i < DAILY_HS_BINS; i++) {
    const DailyHeadspaceBin_t &bin = daily.bins[i];
    // Only the last 24 hours; hours ahead of now (clock jump) are ignored.
    if (bin.count == 0 || bin.hourId > hourNow || bin.hourId + (DAILY_HS_BINS - 1) < hourNow) continue;
    total += bin.sum / bin.count;
    hours++;
  }
  dailyHsHours = hours;
  dailyHsValue = hours ? float(total / hours) : NAN;
  if (hours >= DAILY_HS_MIN_HOURS) {
    daily.heldValue = dailyHsValue;
    dailyHsState = DAILY_HS_VALID;
  } else {
    dailyHsState = isfinite(daily.heldValue) ? DAILY_HS_HOLD : DAILY_HS_EMA;
  }
}

// Without a valid NTP time the sample is not accumulated.
static void dailyHsAccumulate(float headspace) {
  const unsigned long epoch = NTPEpoch();
  if (epoch == 0) return;
  const uint32_t hourId = epoch / 3600UL;
  DailyHeadspaceBin_t &bin = CountersData.dailyHs.bins[hourId % DAILY_HS_BINS];
  const bool newHour = bin.hourId != hourId;
  if (newHour) {
    bin.hourId = hourId;
    bin.count = 0;
    bin.sum = 0.0f;
  }
  if (bin.count < UINT16_MAX) {
    bin.sum += headspace;
    bin.count++;
  }
  dailyHsEvaluate();
  if (newHour) writeDailyHeadspaceToNIV(); // once per hour, not per relief
}

// Applied headspace: the daily average when valid, else the held value,
// else the EMA. The EMA itself keeps running (fallback and log).
static bool applySelectedHeadspace(bool *fromDaily = nullptr) {
  const bool daily = dailyHsState == DAILY_HS_VALID || dailyHsState == DAILY_HS_HOLD;
  const float value = dailyHsState == DAILY_HS_VALID ? dailyHsValue
      : dailyHsState == DAILY_HS_HOLD ? CountersData.dailyHs.heldValue
      : headspaceFiltered;
  const bool applied = applyHeadspaceValue(value);
  if (fromDaily) *fromDaily = applied && daily;
  return applied;
}

// After boot or a counters reload: the held value covers the time until the
// bins can be evaluated with a valid NTP time.
static void dailyHsRestore() {
  dailyHsValue = NAN;
  dailyHsHours = 0;
  dailyHsState = isfinite(CountersData.dailyHs.heldValue) ? DAILY_HS_HOLD : DAILY_HS_EMA;
  dailyHsEvaluate();
}

void rebaseDailyHeadspace(float deltaL, const char *reason) {
  if (!isfinite(deltaL)) return;
  DailyHeadspace_t &daily = CountersData.dailyHs;
  const float before = dailyHsState == DAILY_HS_VALID ? dailyHsValue : daily.heldValue;
  for (int i = 0; i < DAILY_HS_BINS; i++) {
    if (daily.bins[i].count > 0)
      daily.bins[i].sum += deltaL * daily.bins[i].count;
  }
  if (isfinite(daily.heldValue)) daily.heldValue += deltaL;
  // Shift the cached result too, in case the time is not valid to re-evaluate.
  if (isfinite(dailyHsValue)) dailyHsValue += deltaL;
  dailyHsEvaluate();
  writeDailyHeadspaceToNIV();
  const float after = dailyHsState == DAILY_HS_VALID ? dailyHsValue : daily.heldValue;
  Serial.printf("[DAILY-HS] rebase %s dH=%+.3f L %.3f->%.3f L (%s, %u h)\n",
                reason, deltaL, before, after, dailyHsStateLabel(), (unsigned)dailyHsHours);
}

// Empties the bins and the cached result (new batch). Not persisted here.
void resetDailyHeadspaceTracking() {
  resetDailyHeadspace();
  dailyHsValue = NAN;
  dailyHsHours = 0;
  dailyHsState = DAILY_HS_EMA;
}

void clearDailyHeadspace(const char *reason) {
  const float before = dailyHsState == DAILY_HS_VALID ? dailyHsValue : CountersData.dailyHs.heldValue;
  resetDailyHeadspaceTracking();
  // The volume changed by an unknown amount: let the EMA reconverge quickly
  // (it decays back to 0.05 by itself).
  headspaceFilterAlpha = 0.5f;
  writeDailyHeadspaceToNIV();
  Serial.printf("[DAILY-HS] clear %s %.3f->nan L (ema)\n", reason, before);
}

DailyHeadspaceLogData getDailyHeadspaceLogData() {
  dailyHsEvaluate();
  return {headspaceFiltered, dailyHsValue, dailyHsHours, dailyHsStateLabel()};
}

void resetHeadspaceFilterTracking() {
  lnPressureDropAvg.clear();
  headspaceFiltered = NAN;
  headspaceFilterAlpha = 0.05f;
}

static void resetBeerCO2Evolution() {
  co2EvolutionStart = 0;
  co2EvolutionCount = 0;
  co2TrendStart = 0;
  co2TrendCount = 0;
  beerCO2EvolutionGramsPerLiterPerDay = 0.0f;
  resetHenryBuffer();
}

// Gas-phase CO2 rate (g/L/d) over the gCO2 window, with the same end
// averaging; it does not depend on the dissolved model. NAN (no decision)
// with a short window, during a task and CO2_TASK_QUIET_MS after it, or with
// an external gas step in the window.
static float gasPhaseCO2Rate(unsigned long now) {
  if (co2EvolutionCount < CO2_GAS_RATE_MIN_SAMPLES || taskWindowType != 0 ||
      (lastTaskMillis != 0 && now - lastTaskMillis < CO2_TASK_QUIET_MS) ||
      !isfinite(beerVolume) || beerVolume <= 0.0f)
    return NAN;
  for (uint16_t i = 0; i < co2EvolutionCount; ++i) {
    const CO2EvolutionSample &s = co2EvolutionHistory[(co2EvolutionStart + i) % CO2_EVOLUTION_HISTORY_SIZE];
    if (s.externalStep || !isfinite(s.gasMols)) return NAN;
  }
  const uint16_t avgWindow = co2EvolutionCount / 3 < 10 ? co2EvolutionCount / 3 : 10;
  const unsigned long firstMillis = co2EvolutionHistory[co2EvolutionStart].millisStamp;
  double firstMols = 0.0, lastMols = 0.0, firstTime = 0.0, lastTime = 0.0;
  for (uint16_t i = 0; i < avgWindow; ++i) {
    const CO2EvolutionSample &first = co2EvolutionHistory[(co2EvolutionStart + i) % CO2_EVOLUTION_HISTORY_SIZE];
    const CO2EvolutionSample &last = co2EvolutionHistory[(co2EvolutionStart + co2EvolutionCount - avgWindow + i) % CO2_EVOLUTION_HISTORY_SIZE];
    firstMols += first.gasMols;
    lastMols += last.gasMols;
    firstTime += first.millisStamp - firstMillis;
    lastTime += last.millisStamp - firstMillis;
  }
  const double elapsedMs = (lastTime - firstTime) / avgWindow;
  if (elapsedMs <= 0.0) return NAN;
  return (lastMols - firstMols) / avgWindow * CO2MOLAR_MASS * 86400000.0
      / (double(beerVolume) * elapsedMs);
}

// ===== CO2 buffers across a reboot (docs/gco2-rate.md) =====
// The gCO2 window (gas phase) and the 30-min Henry samples are saved to the
// data filesystem (povotoDataFS()) every CO2_BUFFER_SAVE_MS and at an OTA start. At boot, once NTP is
// valid, they are restored if the file is at most CO2_BUFFER_MAX_GAP_S old and
// of the same batch, in Fermenting. The gas series is stitched to the current
// gas phase at its last rate, so CO2 lost by the reboot (ejected not yet saved
// in the counters) does not look like a change of the rate.
static constexpr const char *CO2_BUFFER_FILE = "/co2buffers.bin";
static constexpr const char *CO2_BUFFER_TMP = "/co2buffers.tmp";
static constexpr uint32_t CO2_BUFFER_MAGIC = 0x32304243UL; // "CB02"
// Quick boots (OTA ~1 min, power blips, crashes) are well below this; a longer
// gap is a power cut without temperature and pressure control: start over.
static constexpr uint32_t CO2_BUFFER_MAX_GAP_S = 600;
static constexpr unsigned long CO2_BUFFER_SAVE_MS = 10UL * MINUTESms;
static constexpr unsigned long CO2_BUFFER_RESTORE_WAIT_MS = 5UL * MINUTESms; // for NTP
static constexpr uint16_t CO2_BUFFER_STITCH_SAMPLES = 10;
static bool co2BufferRestorePending = true; // boot: sampling waits for the restore decision
static unsigned long co2BufferLastSaveMillis = 0;
static char co2BufferStatus[64] = "pending";

struct CO2BufferHeader {
  uint32_t magic;
  uint16_t batchNumber;
  uint8_t mode;
  uint32_t savedEpoch;
  uint32_t reliefCount; // counters reset since the save (new batch) -> reject
  uint16_t co2Count;
  uint16_t henryCount;
} __attribute__((packed));
struct CO2SampleFile {
  uint32_t ageMs;
  float pressure;
  double gasMols;
  uint8_t externalStep;
} __attribute__((packed));
struct HenrySampleFile {
  uint32_t ageMs;
  float henryMols;
  float pressure;
} __attribute__((packed));

void saveCO2Buffers() {
  const unsigned long epoch = NTPEpoch();
  if (co2BufferRestorePending || epoch == 0 || BatchData.batchNumber == 0 ||
      SetPointData.mode != MODE_FERMENTING || (co2EvolutionCount == 0 && henryCount == 0))
    return;
  File f = povotoDataFS().open(CO2_BUFFER_TMP, "w");
  if (!f) return;
  const unsigned long now = millis();
  const CO2BufferHeader h = {CO2_BUFFER_MAGIC, BatchData.batchNumber, SetPointData.mode, (uint32_t)epoch,
                             CountersData.totalReliefCount, co2EvolutionCount, henryCount};
  bool ok = f.write((const uint8_t *)&h, sizeof(h)) == sizeof(h);
  for (uint16_t i = 0; ok && i < co2EvolutionCount; ++i) {
    const CO2EvolutionSample &s = co2EvolutionHistory[(co2EvolutionStart + i) % CO2_EVOLUTION_HISTORY_SIZE];
    const CO2SampleFile r = {(uint32_t)(now - s.millisStamp), s.pressure, s.gasMols,
                             (uint8_t)(s.externalStep ? 1 : 0)};
    ok = f.write((const uint8_t *)&r, sizeof(r)) == sizeof(r);
  }
  for (uint16_t i = 0; ok && i < henryCount; ++i) {
    const HenrySample &s = henryAt(i);
    const HenrySampleFile r = {(uint32_t)(now - s.millisStamp), s.henryMols, s.pressure};
    ok = f.write((const uint8_t *)&r, sizeof(r)) == sizeof(r);
  }
  f.close();
  if (!ok) {
    povotoDataFS().remove(CO2_BUFFER_TMP);
    return;
  }
  povotoDataFS().remove(CO2_BUFFER_FILE);
  povotoDataFS().rename(CO2_BUFFER_TMP, CO2_BUFFER_FILE);
  co2BufferLastSaveMillis = now;
}

// The saved buffers no longer describe the CO2 accounting (counters edited,
// new batch, back from Conditioning).
static void invalidateCO2BufferFile() {
  if (co2BufferRestorePending) return; // boot: the file is still to be read
  povotoDataFS().remove(CO2_BUFFER_FILE);
}

// Least-squares slope (mol/ms) of the gas over the last samples.
static double co2BufferTailSlope() {
  const uint16_t n = co2EvolutionCount < CO2_BUFFER_STITCH_SAMPLES ? co2EvolutionCount : CO2_BUFFER_STITCH_SAMPLES;
  if (n < 2) return 0.0;
  const uint16_t from = co2EvolutionCount - n;
  const unsigned long t0 = co2EvolutionHistory[from].millisStamp;
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (uint16_t i = from; i < co2EvolutionCount; ++i) {
    const double x = double(co2EvolutionHistory[i].millisStamp - t0);
    const double y = co2EvolutionHistory[i].gasMols;
    sx += x; sy += y; sxx += x * x; sxy += x * y;
  }
  const double d = n * sxx - sx * sx;
  return d > 0.0 ? (n * sxy - sx * sy) / d : 0.0;
}

static void restoreCO2Buffers() {
  co2BufferRestorePending = false;
  File f = povotoDataFS().open(CO2_BUFFER_FILE, "r");
  if (!f) {
    strcpy(co2BufferStatus, "not restored (no file)");
    return;
  }
  CO2BufferHeader h;
  const unsigned long epoch = NTPEpoch();
  const char *reject = nullptr;
  if (f.read((uint8_t *)&h, sizeof(h)) != sizeof(h)) reject = "invalid file";
  else if (h.magic == 0) reject = "zeroed file";
  else if (h.magic != CO2_BUFFER_MAGIC) reject = "invalid file";
  else if (h.co2Count > CO2_EVOLUTION_HISTORY_SIZE || h.henryCount > HENRY_MEAN_SAMPLES ||
           f.size() != sizeof(h) + h.co2Count * sizeof(CO2SampleFile) + h.henryCount * sizeof(HenrySampleFile))
    reject = "invalid file";
  else if (h.batchNumber != BatchData.batchNumber || CountersData.totalReliefCount < h.reliefCount)
    reject = "other batch";
  else if (h.mode != SetPointData.mode || SetPointData.mode != MODE_FERMENTING) reject = "mode changed";
  else if (epoch < h.savedEpoch || epoch - h.savedEpoch > CO2_BUFFER_MAX_GAP_S) reject = "too old";
  if (reject) {
    f.close();
    snprintf(co2BufferStatus, sizeof(co2BufferStatus), "not restored (%s)", reject);
    return;
  }
  const unsigned long now = millis();
  const unsigned long gapMs = (epoch - h.savedEpoch) * 1000UL;
  bool ok = true;
  for (uint16_t i = 0; ok && i < h.co2Count; ++i) {
    CO2SampleFile r;
    ok = f.read((uint8_t *)&r, sizeof(r)) == sizeof(r);
    co2EvolutionHistory[i] = {now - (r.ageMs + gapMs), r.pressure, r.gasMols, r.externalStep != 0};
  }
  for (uint16_t i = 0; ok && i < h.henryCount; ++i) {
    HenrySampleFile r;
    ok = f.read((uint8_t *)&r, sizeof(r)) == sizeof(r);
    henryBuffer[i] = {now - (r.ageMs + gapMs), r.henryMols, r.pressure};
  }
  f.close();
  if (!ok) {
    resetBeerCO2Evolution();
    strcpy(co2BufferStatus, "not restored (read error)");
    return;
  }
  co2EvolutionStart = 0;
  co2EvolutionCount = h.co2Count;
  henryStart = 0;
  henryCount = h.henryCount;
  updateHenryMeans(); // the dissolved CO2 continues the value before the boot

  // Stitch: the gas series continues at its last rate up to now.
  if (co2EvolutionCount > 0) {
    const CO2EvolutionSample &last = co2EvolutionHistory[co2EvolutionCount - 1];
    const double gasShift = gasPhaseCO2Mols() -
        (last.gasMols + co2BufferTailSlope() * double(now - last.millisStamp));
    for (uint16_t i = 0; i < co2EvolutionCount; ++i) co2EvolutionHistory[i].gasMols += gasShift;
    Serial.printf("[CO2 BUFFERS] restored %u+%u samples, gap %lu s, gas shift %+.3f mol\n",
                  (unsigned)co2EvolutionCount, (unsigned)henryCount, gapMs / 1000UL, gasShift);
  }
  co2EvolutionLastReliefCount = CountersData.totalReliefCount;
  snprintf(co2BufferStatus, sizeof(co2BufferStatus), "restored (gap %lu s, %u+%u samples)",
           gapMs / 1000UL, (unsigned)co2EvolutionCount, (unsigned)henryCount);
}

// Called from pressureControl() after the counters restore.
static void handleCO2BufferRestore() {
  if (!co2BufferRestorePending) return;
  if (NTPEpoch() != 0) restoreCO2Buffers();
  else if (millis() > CO2_BUFFER_RESTORE_WAIT_MS) {
    co2BufferRestorePending = false;
    strcpy(co2BufferStatus, "not restored (no NTP)");
  }
}

static void recomputeBeerCO2EvolutionFromCurrentState() {
  const unsigned long now = millis();
  if (now < 120000UL || co2BufferRestorePending) {
    return;
  }

  // gCO2/L/d is the net CO2 released by the beer: ejected + headspace + expansion
  // tank (docs/gco2-rate.md). The dissolved CO2 is not in the rate.
  const double gasMols = gasPhaseCO2Mols();
  if (!isfinite(beerVolume) || beerVolume <= 0.0f || !isfinite(gasMols)) {
    beerCO2EvolutionGramsPerLiterPerDay = 0.0f;
    return;
  }

  if (co2EvolutionCount > 0) {
    const uint16_t last = (co2EvolutionStart + co2EvolutionCount - 1) % CO2_EVOLUTION_HISTORY_SIZE;
    if (now - co2EvolutionHistory[last].millisStamp < CO2_EVOLUTION_SAMPLE_MS) {
      return;
    }
  }

  // When full, replace the oldest sample.
  if (co2EvolutionCount == CO2_EVOLUTION_HISTORY_SIZE) {
    co2EvolutionStart = (co2EvolutionStart + 1) % CO2_EVOLUTION_HISTORY_SIZE;
    --co2EvolutionCount;
  }

  bool externalStep = false;
  if (co2EvolutionCount > 0 && CountersData.totalReliefCount == co2EvolutionLastReliefCount) {
    const CO2EvolutionSample &previous =
        co2EvolutionHistory[(co2EvolutionStart + co2EvolutionCount - 1) % CO2_EVOLUTION_HISTORY_SIZE];
    const double stepMs = double(now - previous.millisStamp);
    externalStep = stepMs > 0.0 && (gasMols - previous.gasMols) * CO2MOLAR_MASS * 86400000.0
        / (double(beerVolume) * stepMs) > CO2_EXTERNAL_STEP_GPLD;
  }
  co2EvolutionLastReliefCount = CountersData.totalReliefCount;

  const uint16_t index = (co2EvolutionStart + co2EvolutionCount) % CO2_EVOLUTION_HISTORY_SIZE;
  co2EvolutionHistory[index] = {now, ControlData.pressure, gasMols, externalStep};
  ++co2EvolutionCount;
  addHenrySample(now);
  if (co2BufferLastSaveMillis == 0) co2BufferLastSaveMillis = now;
  else if (now - co2BufferLastSaveMillis >= CO2_BUFFER_SAVE_MS) saveCO2Buffers();

  updateCO2DissolvedStateFromGasRate(now, gasPhaseCO2Rate(now));

  if (co2EvolutionCount < 5) {
    beerCO2EvolutionGramsPerLiterPerDay = 0.0f;
    updateCO2Transition();
    return;
  }

  const uint16_t avgWindow = co2EvolutionCount < 9
      ? 1 : (co2EvolutionCount / 3 < 10 ? co2EvolutionCount / 3 : 10);
  const unsigned long firstMillis = co2EvolutionHistory[co2EvolutionStart].millisStamp;
  double firstMols = 0.0, lastMols = 0.0;
  double firstTime = 0.0, lastTime = 0.0;
  for (uint16_t i = 0; i < avgWindow; ++i) {
    const CO2EvolutionSample &first = co2EvolutionHistory[(co2EvolutionStart + i) % CO2_EVOLUTION_HISTORY_SIZE];
    const CO2EvolutionSample &last = co2EvolutionHistory[(co2EvolutionStart + co2EvolutionCount - avgWindow + i) % CO2_EVOLUTION_HISTORY_SIZE];
    firstMols += first.gasMols;
    lastMols += last.gasMols;
    // Relative unsigned timestamps preserve elapsed time across millis() rollover.
    firstTime += first.millisStamp - firstMillis;
    lastTime += last.millisStamp - firstMillis;
  }
  const double deltaMols = (lastMols - firstMols) / avgWindow;
  const double elapsedMs = (lastTime - firstTime) / avgWindow;
  if (elapsedMs <= 0.0) {
    beerCO2EvolutionGramsPerLiterPerDay = 0.0f;
    updateCO2Transition();
    return;
  }
  // Preserve the signed net change; clamp only when presenting the result.
  beerCO2EvolutionGramsPerLiterPerDay = deltaMols
      * CO2MOLAR_MASS * 86400000.0
      / (double(beerVolume) * elapsedMs);

  // Keep the last rate from a mature window, with its time, so that after a
  // reboot it can be reported while the new window is still short. Persisted
  // with the other counters (writeCountersDataToNIV), no extra flash writes.
  if (co2EvolutionCount >= CO2_EVOLUTION_MATURE_SAMPLES) {
    const unsigned long epoch = NTPEpoch();
    if (epoch != 0) {
      CountersData.co2RateHeld = beerCO2EvolutionGramsPerLiterPerDay;
      CountersData.co2RateHeldAt = epoch;
    }
  }

  if (co2EvolutionCount >= CO2_RULE_MIN_SAMPLES) {
    if (co2TrendCount == CO2_TREND_SAMPLES) {
      co2TrendStart = (co2TrendStart + 1) % CO2_TREND_SAMPLES;
      --co2TrendCount;
    }
    co2Trend[(co2TrendStart + co2TrendCount) % CO2_TREND_SAMPLES] = {now, beerCO2EvolutionGramsPerLiterPerDay};
    ++co2TrendCount;
  }
  updateCO2Transition();
}

// The rate saved before a reboot replaces the calculated one only while the
// new window has fewer than CO2_EVOLUTION_MATURE_SAMPLES and the saved value
// is at most CO2_RATE_HELD_MAX_AGE_S old (by NTP; without NTP it is not used).
static bool co2RateHeldApplies() {
  if (co2EvolutionCount >= CO2_EVOLUTION_MATURE_SAMPLES) return false;
  if (!isfinite(CountersData.co2RateHeld) || CountersData.co2RateHeldAt == 0) return false;
  const unsigned long now = NTPEpoch();
  return now != 0 && now >= CountersData.co2RateHeldAt &&
         now - CountersData.co2RateHeldAt <= CO2_RATE_HELD_MAX_AGE_S;
}

// Signed rate for display and logs; automatic rules use only the calculated
// beerCO2EvolutionGramsPerLiterPerDay.
float getReportedCO2EvolutionGramsPerLiterPerDay() {
  if (SetPointData.mode == MODE_CONDITIONING) return 0.0f; // balance frozen
  return co2RateHeldApplies() ? CountersData.co2RateHeld : beerCO2EvolutionGramsPerLiterPerDay;
}

const char *getCO2EvolutionSource() {
  if (co2RateInTransition()) return "transition";
  return co2RateHeldApplies() ? "held" : "calculated";
}

// Transition (docs/gco2-rate.md): after a pressure or temperature change the
// net CO2 released by the beer differs from the production (the beer absorbs
// after a rise or cooling, releases after a fall or warming). It starts when
// temperature or pressure are not STABLE, or STABLE for less than
// CO2_TRANSITION_MIN_SETTLED_S. It ends when all of these hold:
//  - temperature STABLE for CO2_TRANSITION_MIN_SETTLED_S;
//  - with a pressure target, pressure STABLE and its first relief since then
//    CO2_TRANSITION_MIN_SETTLED_S ago (batch 160: STABLE at 17:45, first
//    relief at 1.9 bar at 18:41);
//  - the trend of the last hour within the limit of the direction for
//    CO2_TRANSITION_TREND_PERSIST_S (batch 160: below +5%/h for 32 min from
//    20:59 while the rate rose again afterwards).
// Or CO2_TRANSITION_MAX_S after the last change, with both STABLE. Without NTP
// the settling cannot be timed: transition.
static constexpr uint32_t CO2_TRANSITION_MIN_SETTLED_S = 3600UL;
static constexpr uint32_t CO2_TRANSITION_TREND_PERSIST_S = 3600UL;
static constexpr uint32_t CO2_TRANSITION_MAX_S = 8UL * 3600UL;
static constexpr float CO2_TRANSITION_RISE_MAX = 0.05f;      // 1/h: absorbing, the rise stopped
static constexpr float CO2_TRANSITION_FALL_MAX = 0.10f;      // 1/h: releasing, the fall slowed
static constexpr float CO2_TRANSITION_TREND_FLOOR = 0.5f;    // g/L/d, denominator floor of the trend
static constexpr float CO2_TRANSITION_HENRY_DEADBAND = 0.01f; // relative change of the Henry target
static unsigned long co2TrendOkSince = 0; // NTP epoch since the trend is within the limit (0 = not)
static uint32_t co2TransReliefSeen = 0;   // relief count at the previous update
static bool co2TransReliefSeenValid = false;

static bool co2StableFor(uint8_t state, uint32_t since, unsigned long now, uint32_t seconds) {
  return state == TEMP_STATE_STABLE && since != 0 && now >= since && now - since >= seconds;
}

// Temperature or pressure changing, or stable for less than the minimum.
static bool co2TransitionCondition(unsigned long now) {
  const bool temp = co2StableFor(CountersData.tempState, CountersData.tempStableSince, now,
                                 CO2_TRANSITION_MIN_SETTLED_S);
  const bool press = SetPointData.setPointPressure <= 0.0f ||
      co2StableFor(CountersData.pressState, CountersData.pressStableSince, now, CO2_TRANSITION_MIN_SETTLED_S);
  return !(temp && press);
}

// Henry equilibrium (mol) at the targets: where the dissolved CO2 is heading.
static float co2TransitionTargetHenry() {
  const float pressure = SetPointData.setPointPressure > 0.0f ? SetPointData.setPointPressure : ControlData.pressure;
  const float temperature = (SetPointData.setPointTemp > -50.0f && SetPointData.setPointTemp < 100.0f)
      ? SetPointData.setPointTemp : ControlData.temperature;
  return CO2DissolvedMols(pressure, beerSG, temperature, beerVolume);
}

// Relative trend (1/h) of gCO2/L/d: least-squares slope over the last hour
// divided by the fitted current value (at least the floor). NAN before an hour.
static float co2RateTrend() {
  if (co2TrendCount < CO2_TREND_SAMPLES) return NAN;
  const CO2TrendSample &oldest = co2Trend[co2TrendStart];
  const CO2TrendSample &newest = co2Trend[(co2TrendStart + co2TrendCount - 1) % CO2_TREND_SAMPLES];
  if (newest.millisStamp - oldest.millisStamp < 59UL * 60000UL) return NAN;
  double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
  for (uint16_t i = 0; i < co2TrendCount; ++i) {
    const CO2TrendSample &s = co2Trend[(co2TrendStart + i) % CO2_TREND_SAMPLES];
    const double x = -double(newest.millisStamp - s.millisStamp) / 3600000.0; // h, newest at 0
    sx += x;
    sy += s.rate;
    sxx += x * x;
    sxy += x * s.rate;
  }
  const double n = co2TrendCount;
  const double den = n * sxx - sx * sx;
  if (den <= 0.0) return NAN;
  const double slope = (n * sxy - sx * sy) / den;
  const double current = (sy - slope * sx) / n;
  return float(slope / fmax(current, double(CO2_TRANSITION_TREND_FLOOR)));
}

static void startCO2Transition(unsigned long now, bool rateIsStable) {
  CountersData.co2TransStart = now;
  CountersData.co2TransDir = CO2_TRANS_UNKNOWN;
  CountersData.co2TransHenryRef = isfinite(henryMeanMols)
      ? henryMeanMols : CO2DissolvedMols(ControlData.pressure, beerSG, ControlData.temperature, beerVolume);
  const float rate = getReportedCO2EvolutionGramsPerLiterPerDay();
  if (rateIsStable && co2EvolutionCount >= CO2_RULE_MIN_SAMPLES && isfinite(rate)) {
    CountersData.co2TransRate = fmaxf(0.0f, rate);
    CountersData.co2TransRateAt = now;
  } else {
    CountersData.co2TransRate = NAN;
    CountersData.co2TransRateAt = 0;
  }
  CountersData.co2TransReliefAt = 0;
  co2TrendOkSince = 0;
  Serial.printf("[CO2 TRANSITION] start, stable rate %.2f g/L/d, Henry %.3f mol\n",
                CountersData.co2TransRate, CountersData.co2TransHenryRef);
  writeCountersDataToNIV();
}

static void endCO2Transition(unsigned long now, const char *reason) {
  Serial.printf("[CO2 TRANSITION] end (%s) after %lu min\n", reason,
                now >= CountersData.co2TransStart ? (now - CountersData.co2TransStart) / 60UL : 0UL);
  CountersData.co2TransStart = 0;
  CountersData.co2TransDir = CO2_TRANS_UNKNOWN;
  CountersData.co2TransHenryRef = NAN;
  CountersData.co2TransRate = NAN;
  CountersData.co2TransRateAt = 0;
  CountersData.co2TransReliefAt = 0;
  co2TrendOkSince = 0;
  writeCountersDataToNIV();
}

// The next sample starts a transition with no stable rate (back from
// Conditioning). A new batch clears it.
static void resetCO2Transition(bool startAtNextSample) {
  CountersData.co2TransStart = startAtNextSample ? 1 : 0;
  CountersData.co2TransDir = CO2_TRANS_UNKNOWN;
  CountersData.co2TransHenryRef = NAN;
  CountersData.co2TransRate = NAN;
  CountersData.co2TransRateAt = 0;
  CountersData.co2TransReliefAt = 0;
  co2TrendOkSince = 0;
}

// Once per gCO2 sample (1 min), in Fermenting with NTP.
static void updateCO2Transition() {
  if (SetPointData.mode != MODE_FERMENTING) return;
  const unsigned long now = NTPEpoch();
  if (now == 0) return;
  const bool newRelief = co2TransReliefSeenValid && CountersData.totalReliefCount != co2TransReliefSeen;
  co2TransReliefSeen = CountersData.totalReliefCount;
  co2TransReliefSeenValid = true;

  if (CountersData.co2TransStart == 0) {
    if (!co2TransitionCondition(now)) return;
    startCO2Transition(now, true);
  } else if (CountersData.co2TransStart == 1 || CountersData.co2TransStart > now) {
    startCO2Transition(now, false);
  }
  bool changed = false;

  // Direction: the Henry target against the equilibrium before the start.
  uint8_t direction = CO2_TRANS_UNKNOWN;
  const float ref = CountersData.co2TransHenryRef;
  const float target = co2TransitionTargetHenry();
  if (isfinite(ref) && ref > 0.0f && isfinite(target)) {
    const float relative = (target - ref) / ref;
    if (relative > CO2_TRANSITION_HENRY_DEADBAND) direction = CO2_TRANS_ABSORBING;
    else if (relative < -CO2_TRANSITION_HENRY_DEADBAND) direction = CO2_TRANS_RELEASING;
  }
  uint8_t merged = CountersData.co2TransDir;
  if (merged == CO2_TRANS_UNKNOWN) merged = direction;
  else if (merged != CO2_TRANS_MIXED && direction != merged) merged = CO2_TRANS_MIXED;
  if (merged != CountersData.co2TransDir) {
    CountersData.co2TransDir = merged;
    changed = true;
  }

  // First relief with the pressure stable; leaving STABLE waits for another.
  const bool pressureTarget = SetPointData.setPointPressure > 0.0f;
  if (pressureTarget && CountersData.pressState != TEMP_STATE_STABLE) {
    if (CountersData.co2TransReliefAt != 0) {
      CountersData.co2TransReliefAt = 0;
      changed = true;
    }
  } else if (pressureTarget && CountersData.co2TransReliefAt == 0 && newRelief) {
    CountersData.co2TransReliefAt = now;
    changed = true;
  }

  const float trend = co2RateTrend();
  const uint8_t dir = CountersData.co2TransDir;
  const bool trendOk = isfinite(trend) &&
      (dir == CO2_TRANS_RELEASING || trend <= CO2_TRANSITION_RISE_MAX) &&
      (dir == CO2_TRANS_ABSORBING || trend >= -CO2_TRANSITION_FALL_MAX);
  if (!trendOk) co2TrendOkSince = 0;
  else if (co2TrendOkSince == 0) co2TrendOkSince = now;

  const bool tempOk = co2StableFor(CountersData.tempState, CountersData.tempStableSince, now,
                                   CO2_TRANSITION_MIN_SETTLED_S);
  const bool pressOk = !pressureTarget ||
      (CountersData.pressState == TEMP_STATE_STABLE && CountersData.co2TransReliefAt != 0 &&
       now >= CountersData.co2TransReliefAt && now - CountersData.co2TransReliefAt >= CO2_TRANSITION_MIN_SETTLED_S);
  const bool trendSettled = co2TrendOkSince != 0 && now - co2TrendOkSince >= CO2_TRANSITION_TREND_PERSIST_S;
  if (tempOk && pressOk && trendSettled) {
    endCO2Transition(now, "settled");
    return;
  }
  const bool bothStable = CountersData.tempState == TEMP_STATE_STABLE &&
      (!pressureTarget || CountersData.pressState == TEMP_STATE_STABLE);
  uint32_t lastChange = CountersData.co2TransStart;
  if (CountersData.tempStableSince > lastChange) lastChange = CountersData.tempStableSince;
  if (pressureTarget && CountersData.pressStableSince > lastChange) lastChange = CountersData.pressStableSince;
  if (bothStable && now >= lastChange && now - lastChange >= CO2_TRANSITION_MAX_S) {
    endCO2Transition(now, "8 h limit");
    return;
  }
  if (changed) writeCountersDataToNIV();
}

bool co2RateInTransition() {
  if (SetPointData.mode != MODE_FERMENTING) return false;
  const unsigned long now = NTPEpoch();
  if (now == 0) return true;
  return CountersData.co2TransStart != 0 || co2TransitionCondition(now);
}

float getCO2TransitionStableRate(uint32_t *epoch) {
  const bool available = co2RateInTransition() && isfinite(CountersData.co2TransRate) &&
      CountersData.co2TransRateAt != 0;
  if (epoch) *epoch = available ? CountersData.co2TransRateAt : 0;
  return available ? CountersData.co2TransRate : NAN;
}

// For the automatic rules: only a calculated positive rate over a window of
// at least CO2_RULE_MIN_SAMPLES (30 min); shorter windows cover few relief
// cycles (batch 160: 7.19 after 4 min with 8.8 real fired a "< 8" rule).
// In a transition the net release only bounds the production: while the beer
// releases CO2 it is an upper bound, so "gCO2 < x" also holds for the
// production; while it absorbs (or the direction is unknown or mixed) there is
// no usable rate.
float getRuleCO2EvolutionGramsPerLiterPerDay() {
  if (co2EvolutionCount < CO2_RULE_MIN_SAMPLES) return NAN;
  if (co2RateInTransition() &&
      !(CountersData.co2TransStart > 1 && CountersData.co2TransDir == CO2_TRANS_RELEASING))
    return NAN;
  return (isfinite(beerCO2EvolutionGramsPerLiterPerDay) && beerCO2EvolutionGramsPerLiterPerDay > 0.0f)
      ? beerCO2EvolutionGramsPerLiterPerDay : NAN;
}

float getBeerCO2EvolutionGramsPerLiterPerDay() {
  return fmaxf(0.0f, getReportedCO2EvolutionGramsPerLiterPerDay());
}

static void releasePressureReliefHistory() {
  if (pressureReliefHistory) {
    delete[] pressureReliefHistory;
    pressureReliefHistory = nullptr;
  }
  pressureReliefIndex = 0;
  pressureReliefCount = 0;
  pendingReliefIndex = -1;
  volumeRecordIndex = -1;
  volumeAwaitingRecord = false;
  volumeStartPressure = 0.0f;
  volumeStartTemperatureK = 0.0f;
  volumeStartReliefIteration = 0;
}

static void resetReliefsPerHourState() {
  reliefMillisCount = 0;
  reliefMillisIndex = 0;
  reliefsPerHourAvailable = false;
  reliefsPerHourValue = 0.0f;
}

static void updateReliefsPerHour(bool registerReliefEvent) {
  if (SetPointData.mode != MODE_FERMENTING) {
    resetReliefsPerHourState();
    return;
  }

  const unsigned long now = millis();
  if (registerReliefEvent) {
    reliefMillisWindow[reliefMillisIndex] = now;
    reliefMillisIndex = (reliefMillisIndex + 1) % RELIEFS_WINDOW_SIZE;
    if (reliefMillisCount < RELIEFS_WINDOW_SIZE) {
      reliefMillisCount++;
    }
  }

  if (reliefMillisCount < RELIEFS_WINDOW_SIZE) {
    reliefsPerHourAvailable = false;
    return;
  }

  const uint8_t oldestIndex = reliefMillisIndex;
  const uint8_t newestIndex = (oldestIndex + RELIEFS_WINDOW_SIZE - 1) % RELIEFS_WINDOW_SIZE;
  const unsigned long firstMillis = reliefMillisWindow[oldestIndex];
  const unsigned long lastMillis = reliefMillisWindow[newestIndex];

  unsigned long consideredLastMillis = lastMillis;
  const unsigned long historicalSpan = lastMillis - firstMillis;
  if (historicalSpan == 0) {
    reliefsPerHourAvailable = false;
    return;
  }

  const float historicalAverage = historicalSpan / float(RELIEFS_WINDOW_SIZE - 1);
  const unsigned long sinceLastRelief = now - lastMillis;
  if (sinceLastRelief > (unsigned long)(historicalAverage * RELIEF_OVERDUE_FACTOR)) {
    consideredLastMillis = now;
  }

  const unsigned long consideredSpan = consideredLastMillis - firstMillis;
  if (consideredSpan == 0) {
    reliefsPerHourAvailable = false;
    return;
  }

  const float avgMillisPerRelief = consideredSpan / float(RELIEFS_WINDOW_SIZE - 1);
  if (avgMillisPerRelief <= 0.0f) {
    reliefsPerHourAvailable = false;
    return;
  }

  reliefsPerHourValue = 3600000.0f / avgMillisPerRelief;
  reliefsPerHourAvailable = true;
}

void getReliefsPerHourText(char *out, size_t outSize) {
  if (!out || outSize == 0) {
    return;
  }

  out[0] = '\0';
  if (SetPointData.mode != MODE_FERMENTING) {
    return;
  }

  if (!reliefsPerHourAvailable || reliefsPerHourValue < RELIEF_PER_HOUR_MIN_DISPLAY) {
    snprintf(out, outSize, " (Reliefs/hour: N/A)");
    return;
  }

  snprintf(out, outSize, " (Reliefs/hour: %.1f)", reliefsPerHourValue);
}

void getReliefsPerHourCompactText(char *out, size_t outSize) {
  if (!out || outSize == 0) {
    return;
  }

  out[0] = '\0';
  if (SetPointData.mode != MODE_FERMENTING) {
    return;
  }

  if (!reliefsPerHourAvailable || reliefsPerHourValue < RELIEF_PER_HOUR_MIN_DISPLAY) {
    snprintf(out, outSize, " RPH:N/A");
    return;
  }

  snprintf(out, outSize, " RPH:%.1f", reliefsPerHourValue);
}

float getReliefsPerHourValue() {
  if (SetPointData.mode != MODE_FERMENTING) {
    return NAN;
  }
  if (!reliefsPerHourAvailable || reliefsPerHourValue < RELIEF_PER_HOUR_MIN_DISPLAY) {
    return NAN;
  }
  return reliefsPerHourValue;
}

float getTotalCO2Mols() {
  return CountersData.totalMolsEjected + CountersData.CO2InSolution +
    headSpaceCO2Mols + expansionTankInventoryMoles();
}

float CO2Mass(float mols) {
  if (mols == -1)
    return getTotalCO2Mols() * CO2MOLAR_MASS;
  else
    return mols * CO2MOLAR_MASS;
}

float SGToApparentPlato(float sg) {
  return ((135.997f * sg - 630.272f) * sg
          + 1111.14f) * sg
          - 616.868f;
}

float SGToRealPlato(float sg) {
  const float oe = SGToApparentPlato(BatchData.batchOG);
  const float ae = SGToApparentPlato(sg);

  return 0.1808f * oe + 0.8192f * ae;
}

float ApparentPlatoToSG(float apparentPlato) {
    // Boa estimativa inicial
    float sg = 1.0f + apparentPlato /
        (258.6f - (apparentPlato / 258.2f) * 227.1f);

    // Inverte SGToApparentPlato()
    for (int i = 0; i < 4; i++) {
        float calculatedPlato = SGToApparentPlato(sg);

        // Derivada do polinômio Plato(SG)
        float derivative =
            1111.14f
            - 1260.544f * sg
            + 407.991f * sg * sg;

        sg -= (calculatedPlato - apparentPlato) / derivative;
    }

    return sg;
}

float RealPlatoToSG(float realPlato) {
    const float originalPlato =
        SGToApparentPlato(BatchData.batchOG);

    const float apparentPlato =
        (realPlato - 0.1808f * originalPlato) / 0.8192f;

    return ApparentPlatoToSG(apparentPlato);
}


static void restoreDerivedStateFromCounters() {
  dailyHsRestore(); // [DAILY-HS]
  // Restore the dissolved-CO2 mode saved before the reboot.
  // (values validated by PovotoData: 0..3).
  co2DissolvedState = (CO2DissolvedState)CountersData.co2DissolvedMode;
  co2PreviousDissolvedState = co2DissolvedState;
  co2GasBaselineValid = false;

  if (CountersData.headSpaceVolume > 0.0f) {
    updateBeerVolumeFromHeadspace();
    if (CountersData.totalReliefCount >= 3) {
      if (applyFilteredHeadspace(CountersData.headSpaceVolume)) {
        headspaceFilterAlpha = 0.05f;
      } else {
        resetHeadspaceFilterTracking();
      }
    } else {
      resetHeadspaceFilterTracking();
    }
    recomputeHeadspaceCO2MolsFromCurrentState();
  }
  else {
    resetHeadspaceFilterTracking();
    CountersData.headSpaceVolume = 0.0f;
    beerVolume = 0.0f;
    headSpaceCO2Mols = 0.0f;
  }
}

void requestDerivedStateRestoreFromCounters() {
  resetBeerCO2Evolution();
  invalidateCO2BufferFile(); // at boot the file is kept for the restore
  restartCO2ProducedBaseline();
  derivedStateRestorePending = true;
}

// [DAILY-HS] Last dump, reported once in the next Cold log row.
static DumpLogData lastDumpLog = {false, NAN, NAN, 0, 0, NAN};

bool takeDumpLogData(DumpLogData &data) {
  if (!lastDumpLog.pending) return false;
  data = lastDumpLog;
  lastDumpLog.pending = false;
  return true;
}

void applyDumpWindowHeadspaceRecalc(float headspaceBeforeL, float pressureBeforeBar, float pressureAfterBar,
                                    unsigned long startMillis, unsigned long endMillis) {
  // [DAILY-HS] Logged even when the recalculation is not applied (dH = NAN).
  lastDumpLog = {true, pressureBeforeBar, pressureAfterBar, startMillis, endMillis, NAN};
  // Ideal gas with constant moles/temperature during the dump window:
  // P1_abs * H_before = P2_abs * H_after  =>  H_after = H_before * P1_abs / P2_abs.
  if (headspaceBeforeL <= 0.0f) {
    return;
  }

  const float p1Abs = pressureBeforeBar + Patm;
  const float p2Abs = pressureAfterBar + Patm;
  if (p1Abs <= 0.0f || p2Abs <= 0.0f || p1Abs <= p2Abs) {
    return;
  }

  float headAfter = headspaceBeforeL * (p1Abs / p2Abs);
  if (headAfter < 0.0f) {
    return;
  }

  if (headAfter > headspaceBeforeL) {
    if (applyFilteredHeadspace(headAfter)) {
      headspaceFilterAlpha = 0.5f;
      lnPressureDropAvg.clear();
      // [DAILY-HS] Shift the stored hours by the same dH, then apply the
      // daily/held value (or headAfter, which is now the EMA).
      const float deltaL = headAfter - headspaceBeforeL;
      rebaseDailyHeadspace(deltaL, "dump");
      applySelectedHeadspace();
      lastDumpLog.deltaH = deltaL;
    }
  }


  recomputeHeadspaceCO2MolsFromCurrentState();

  const float impliedHeadspaceDeltaL = CountersData.headSpaceVolume - headspaceBeforeL;
  Serial.printf("[DUMP] Headspace recalculated: H_before=%.3f L, P1=%.3f bar, P2=%.3f bar, H_after=%.3f L, dH=%.3f L, Beer=%.3f L\n",
                headspaceBeforeL,
                pressureBeforeBar,
                pressureAfterBar,
                CountersData.headSpaceVolume,
                impliedHeadspaceDeltaL,
                beerVolume);
  Serial.printf("[DAILY-HS] dump start=%lu ms end=%lu ms, rebase dH=%.3f L\n",
                startMillis, endMillis, lastDumpLog.deltaH);
}

static void markSolenoidToggle() {
  lastSolenoidToggleMillis = millis();
}

static float readCurrentFromINA226mA() {
  return ina226.getShuntVoltage_mV() / INA226_SHUNT_OHMS;
}

// Current is obtained straight from the shunt ADC, averaging 16 conversions of
// 1.1ms each there (~17.6ms) so each software sample represents a full shunt cycle.
static void configureINA226CurrentAveraging() {
  ina226.setAverage(INA226_16_SAMPLES);
  ina226.setBusVoltageConversionTime(INA226_1100_us);
  ina226.setShuntVoltageConversionTime(INA226_1100_us);
}


boolean inPressureNoiseWindow() {
  return !(MILLISDIFF(lastSolenoidToggleMillis,SOLENOID_NOISE_MS));
}

static float medianFilter(float sample) {
  if (currentWindowCount < PRESSURE_MEDIAN_WINDOW) {
    const uint8_t sampleIdx = currentWindowIndex;
    currentWindow[sampleIdx] = sample;
    currentWindowIndex = (currentWindowIndex + 1) % PRESSURE_MEDIAN_WINDOW;

    // Insert index in sorted order by the value in currentWindow.
    int insertPos = currentWindowCount;
    while (insertPos > 0 && currentWindow[currentWindowSortedIdx[insertPos - 1]] > sample) {
      currentWindowSortedIdx[insertPos] = currentWindowSortedIdx[insertPos - 1];
      --insertPos;
    }
    currentWindowSortedIdx[insertPos] = sampleIdx;
    currentWindowCount++;
  }
  else {
    // Sliding window is full: remove overwritten index, then reinsert it by new value.
    const uint8_t sampleIdx = currentWindowIndex;
    currentWindow[sampleIdx] = sample;
    currentWindowIndex = (currentWindowIndex + 1) % PRESSURE_MEDIAN_WINDOW;

    int removePos = -1;
    for (uint8_t i = 0; i < currentWindowCount; ++i) {
      if (currentWindowSortedIdx[i] == sampleIdx) {
        removePos = i;
        break;
      }
    }

    if (removePos < 0) {
      removePos = 0;
    }

    for (uint8_t i = (uint8_t)removePos; i < currentWindowCount - 1; ++i) {
      currentWindowSortedIdx[i] = currentWindowSortedIdx[i + 1];
    }

    // Insert updated index (currentWindowCount-1 valid elements after removal).
    int insertPos = currentWindowCount - 1;
    while (insertPos > 0 && currentWindow[currentWindowSortedIdx[insertPos - 1]] > sample) {
      currentWindowSortedIdx[insertPos] = currentWindowSortedIdx[insertPos - 1];
      --insertPos;
    }
    currentWindowSortedIdx[insertPos] = sampleIdx;
  }

  uint8_t centralCount = currentWindowCount / 3;
  if (centralCount == 0) {
    centralCount = 1;
  }

  const uint8_t start = (currentWindowCount - centralCount) / 2;
  const float centralMin = currentWindow[currentWindowSortedIdx[start]];
  const float centralMax = currentWindow[currentWindowSortedIdx[start + centralCount - 1]];
  if ((centralMax - centralMin) > INSTABILITYTHRESHOLDMA) {
    return 0.0f;
  }

  float sum = 0.0f;
  for (uint8_t i = 0; i < centralCount; ++i) {
    sum += currentWindow[currentWindowSortedIdx[start + i]];
  }

  return sum / centralCount;
}

static void resetCurrentMedianFilter() {
  currentWindowCount = 0;
  currentWindowIndex = 0;
  lastCurrentMedianSampleMillis = 0;
}

float convertCurrentToPressure(float current) {
  float pressure = 0.0f;
  if (current <= FMTData.pressure0Current) {
    pressure = 0.0f;
  }
  else if (FMTData.pressure2Bar != 0.0f && FMTData.pressure2Current != 0.0f) { // quadratic interpolation using Lagrange polynomials
    const float x0 = FMTData.pressure0Current;
    const float y0 = 0.0f;
    const float x1 = FMTData.pressure1Current;
    const float y1 = FMTData.pressure1Bar;
    const float x2 = FMTData.pressure2Current;
    const float y2 = FMTData.pressure2Bar;

    const float d0 = (x0 - x1) * (x0 - x2);
    const float d1 = (x1 - x0) * (x1 - x2);
    const float d2 = (x2 - x0) * (x2 - x1);

    if (fabsf(d0) > 0.000001f && fabsf(d1) > 0.000001f && fabsf(d2) > 0.000001f) {
      const float l0 = ((current - x1) * (current - x2)) / d0;
      const float l1 = ((current - x0) * (current - x2)) / d1;
      const float l2 = ((current - x0) * (current - x1)) / d2;
      pressure = y0 * l0 + y1 * l1 + y2 * l2;
    }
    else {
      const float denom = FMTData.pressure2Current - FMTData.pressure1Current;
      if (fabsf(denom) > 0.000001f) {
        float ratio = (current - FMTData.pressure1Current) / denom;
        pressure = FMTData.pressure1Bar + ratio * (FMTData.pressure2Bar - FMTData.pressure1Bar);
      }
    }
  }
  else { // linear interpolation
    const float denom = FMTData.pressure1Current - FMTData.pressure0Current;
    if (fabsf(denom) > 0.000001f) {
      float ratio = (current - FMTData.pressure0Current) / denom;
      pressure = ratio * FMTData.pressure1Bar;
    }
  }

  if (pressure < 0.0f) {
    pressure = 0.0f;
  }

  return pressure;
}

bool inTheMiddleOfRelief() {
  return (timeToStartExpansion || timeToFinishExpansion || timeToRegisterPressure);
}

void readPressure() {
  // Try to initialize the INA226 if it hasn't been done yet
  static bool initialized = false;
  if (!initialized) {
    pressureSensorConnected = ina226.begin();
    if (pressureSensorConnected) {
      Serial.println("INA226 pressure sensor initialized successfully");
      configureINA226CurrentAveraging();
      Serial.printf("INA226: config register after setup = 0x%04X\n", ina226.getRegister(0x00));
    } else {
      Serial.printf("Could not find INA226 pressure sensor at address 0x%02X\n", INA226_I2C_ADDRESS);
    }
    initialized = true;
  }
  
  if (!inPressureNoiseWindow() && MILLISDIFF(noPressureReadUntil, 0)) {
    // In debugging mode, a pressure set on the debug page replaces the INA.
    const bool simulatedPressure = debugging && debugPressureOverride;
    if (pressureSensorConnected) {
      // Read and filter the INA current even in debugging mode. Debug pressure
      // simulation is only used when this reading is zero, the INA is absent
      // or the pressure was set on the debug page.
      if (currentWindowCount == 0 || MILLISDIFF(lastCurrentMedianSampleMillis, CURRENT_MEDIAN_MIN_SAMPLE_MS)) {
        currentReading = medianFilter(readCurrentFromINA226mA());
        lastCurrentMedianSampleMillis = millis();
      }

      if (!simulatedPressure && !(debugging && currentReading == 0.0f)) {
        ControlData.pressure = convertCurrentToPressure(currentReading);
        pressureAcquiredMillis = lastCurrentMedianSampleMillis;
        pressureAcquisitionValid = isfinite(ControlData.pressure);
        return;
      }
    }

    if (debugging && (simulatedPressure || !pressureSensorConnected || currentReading == 0.0f)) {
      pressureAcquiredMillis = millis();
      pressureAcquisitionValid = true;
      static unsigned long lastPressureIncrease = 0;
      if (sgPointGenerationTime != 0 && !inTheMiddleOfRelief() && beerSG > 1.010f) {
        if (MILLISDIFF(lastPressureIncrease,1000*sgPointGenerationTime))  {
          lastPressureIncrease = millis();
          ControlData.pressure += 0.1f;
        } 
      }
    }
    else if (!pressureSensorConnected) {
    pressureAcquisitionValid = false;
    // Se não tem sensor, zera a pressão - Lucio urgente - precisar alertar
    ControlData.pressure = 0.0;
    currentReading = 0.0;
    }
  }
}  

static char calibrationDisplayLines[3][80] = {};

static void showVolumeStatus(const char *line1, const char *line2, const char *line3) {
  // Publish the text; screenData draws it after the normal screen update.
  snprintf(calibrationDisplayLines[0], sizeof(calibrationDisplayLines[0]), "%s", line1);
  snprintf(calibrationDisplayLines[1], sizeof(calibrationDisplayLines[1]), "%s", line2);
  snprintf(calibrationDisplayLines[2], sizeof(calibrationDisplayLines[2]), "%s", line3);
  Serial.print(line1); Serial.print(" | "); Serial.print(line2); Serial.print(" | "); Serial.println(line3);
}

void drawCalibrationStatus() {
  if (!calibrationDisplayLines[0][0]) return;
  tft.setFreeFont(nullptr);
  tft.setTextFont(2);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  // Keep the temperature and pressure readings below this panel unobstructed.
  tft.fillRect(10, 2, 460, 66, TFT_BLACK);
  for (uint8_t i = 0; i < 3; ++i) {
    tft.drawString(calibrationDisplayLines[i], 16, 5 + i * 20, 2);
  }
}

static void formatFloatCsv(char *out, size_t size, float value, uint8_t decimals) {
  snprintf(out, size, "%.*f", decimals, value);
  for (size_t i = 0; out[i] != '\0'; ++i) {
    if (out[i] == '.') {
      out[i] = ',';
    }
  }
}

// Least-squares fit with a free intercept: ln(Padjusted) = a + b*N.
// Include the initial measurement at N=0 and each settled measurement once.
static float fitVolumeFromHistory(unsigned recentWindow = 0) {
  if (!pressureReliefHistory || !isfinite(volumeStartPressure) || volumeStartPressure <= 0.0f)
    return NAN;
  if (recentWindow && pressureReliefCount < recentWindow) return NAN;
  unsigned count = recentWindow ? 0 : 1;
  double meanX = 0.0;
  double meanY = recentWindow ? 0.0 : log((double)volumeStartPressure);
  double sxx = 0.0;
  double sxy = 0.0;
  for (uint16_t i = recentWindow ? pressureReliefCount - recentWindow : 0; i < pressureReliefCount; ++i) {
    const uint16_t idx = (pressureReliefIndex + PRESSURE_RELIEF_HISTORY_MAX - pressureReliefCount + i)
                         % PRESSURE_RELIEF_HISTORY_MAX;
    const PressureReliefRecord &record = pressureReliefHistory[idx];
    if (record.nReliefs == 0 ||
        !isfinite(record.pfAdjusted) || record.pfAdjusted <= 0.0f) {
      if (recentWindow) return NAN;
      continue;
    }
    const double x = record.nReliefs;
    const double y = log((double)record.pfAdjusted);
    ++count;
    const double dx = x - meanX;
    const double dy = y - meanY;
    meanX += dx / count;
    meanY += dy / count;
    sxx += dx * (x - meanX);
    sxy += dx * (y - meanY);
  }
  if (count < 2 || sxx <= 0.0) return NAN;
  const double slope = sxy / sxx;
  if (!isfinite(slope) || slope >= 0.0 ||
      !isfinite(FMTData.FMTReliefVolume) || FMTData.FMTReliefVolume <= 0.0f) return NAN;
  const float volume = volumeRoutineVolume((float)exp(slope));
  return isfinite(volume) && volume > 0.0f ? volume : NAN;
}

static void formatVolumeComparison(char *extremes, size_t extremesSize,
                                   char *fit, size_t fitSize, float endpointVolume,
                                   float fittedVolume) {
  if (isfinite(endpointVolume) && endpointVolume > 0.0f)
    snprintf(extremes, extremesSize, "Extremos: %.3f L", endpointVolume);
  else
    snprintf(extremes, extremesSize, "Extremos: N/A");
  if (isfinite(fittedVolume) && fittedVolume > 0.0f) {
    if (isfinite(endpointVolume) && endpointVolume > 0.0f)
      snprintf(fit, fitSize, "Ajuste: %.3f L (%+.2f%%)", fittedVolume,
               100.0f * (fittedVolume / endpointVolume - 1.0f));
    else
      snprintf(fit, fitSize, "Ajuste: %.3f L", fittedVolume);
  } else {
    snprintf(fit, fitSize, "Ajuste: N/A");
  }
}

static void finalizeVolumeDeterminationSummary() {
  if (!volumeDeterminationActive) {
    return;
  }

  volumeSummaryAvailable = false;
  volumeSummaryTiK = volumeStartTemperatureK;
  volumeSummaryTfK = kelvin(ControlData.temperature);
  volumeSummaryPi = volumeStartPressure;
  volumeSummaryNReliefs = (volumeIteration >= volumeStartReliefIteration)
                        ? (volumeIteration - volumeStartReliefIteration)
                        : 0;

  if (volumeSummaryTiK <= 0.0f || volumeSummaryTfK <= 0.0f ||
      volumeSummaryPi <= 0.0f || volumeSummaryNReliefs == 0) {
    volumeDeterminationActive = false;
    showVolumeStatus("Volume: END", "Dados insuficientes", "Sem resumo");
    return;
  }

  volumeSummaryPfAdjusted = (ControlData.pressure + Patm) * (volumeSummaryTiK / volumeSummaryTfK) - Patm;
  if (volumeSummaryPfAdjusted <= 0.0f) {
    volumeDeterminationActive = false;
    showVolumeStatus("Volume: END", "Pf ajustada invalida", "Sem resumo");
    return;
  }

  volumeSummaryFactor = powf(volumeSummaryPfAdjusted / volumeSummaryPi, 1.0f / (float)volumeSummaryNReliefs);
  const float denom = 1.0f - volumeSummaryFactor;
  if (fabsf(denom) < 0.000001f) {
    volumeDeterminationActive = false;
    showVolumeStatus("Volume: END", "Fator invalido", "Sem resumo");
    return;
  }

  // Mean temperatures of the test; the calibrated k is the median of the
  // per-relief values (the cumulative-factor k is shown for comparison).
  float fermenterSum = 0.0f, ambientSum = 0.0f;
  uint16_t fermenterCount = 0, ambientCount = 0;
  static constexpr uint16_t K_VALUES_MAX = 64; // the test stops at 35 reliefs
  static float kValues[K_VALUES_MAX];
  uint16_t kCount = 0;
  for (uint16_t i = 0; i < pressureReliefCount && pressureReliefHistory; ++i) {
    const PressureReliefRecord &record = pressureReliefHistory[i];
    if (isfinite(record.temperature)) { fermenterSum += record.temperature; ++fermenterCount; }
    if (isfinite(record.environmentTemperature)) { ambientSum += record.environmentTemperature; ++ambientCount; }
    if (isfinite(record.kRelief) && kCount < K_VALUES_MAX) kValues[kCount++] = record.kRelief;
  }
  const float meanFermenterC = fermenterCount ? fermenterSum / fermenterCount : ControlData.temperature;
  const float meanAmbientC = ambientCount ? ambientSum / ambientCount : NAN;
  volumeSummaryFermenterVolume = volumeRoutineVolume(volumeSummaryFactor, meanFermenterC, meanAmbientC);
  volumeCalibration = {};
  volumeCalibration.co2 = volumeDeterminationCO2;
  volumeCalibration.fermenterC = meanFermenterC;
  volumeCalibration.ambientC = meanAmbientC;
  volumeCalibration.volume = volumeSummaryFermenterVolume;
  volumeCalibration.kOverall = volumeRoutineCalibratedK(volumeSummaryFactor, meanFermenterC, meanAmbientC);
  volumeCalibration.kMedian = NAN;
  volumeCalibration.kSpread = NAN;
  volumeCalibration.count = kCount;
  if (kCount > 0) {
    std::sort(kValues, kValues + kCount);
    volumeCalibration.kMedian = (kCount % 2) ? kValues[kCount / 2]
                                             : 0.5f * (kValues[kCount / 2 - 1] + kValues[kCount / 2]);
    float sum = 0.0f, sumSquares = 0.0f;
    for (uint16_t i = 0; i < kCount; ++i) { sum += kValues[i]; sumSquares += kValues[i] * kValues[i]; }
    const float mean = sum / kCount;
    volumeCalibration.kSpread = kCount > 1 ? sqrtf(fmaxf(0.0f, (sumSquares - kCount * mean * mean) / (kCount - 1))) : NAN;
  }
  volumeCalibration.available = volumeDeterminationFast && isfinite(volumeCalibration.kMedian);
  Serial.printf("[VOLUME] %s (%s): factor %.5f, volume %.2f L (relief volume %.3f L, Tferm %.2f C, Tamb %.2f C); "
                "k for FMTVolume %.1f L: median %.4f (sd %.4f, n %u), cumulative %.4f\n",
                volumeDeterminationFast ? "fast" : "slow", volumeDeterminationCO2 ? "CO2" : "air",
                volumeSummaryFactor, volumeSummaryFermenterVolume,
                volumeRoutineReliefVolume(meanFermenterC, meanAmbientC), meanFermenterC, meanAmbientC,
                FMTData.FMTVolume, volumeCalibration.kMedian, volumeCalibration.kSpread,
                (unsigned)kCount, volumeCalibration.kOverall);
  volumeSummaryAvailable = true;
  volumeCalculatedSoFar = volumeSummaryFermenterVolume;
  volumeCalculatedSoFarValid = true;
  volumeDeterminationActive = false;

  char line1[40];
  char line2[40];
  char line3[40];
  snprintf(line1, sizeof(line1), "END: %s (%u/%u)", volumeConverged ? "convergiu" : "limite",
           (unsigned)volumeSummaryNReliefs, (unsigned)volumeIteration);
  formatVolumeComparison(line2, sizeof(line2), line3, sizeof(line3),
                         volumeSummaryFermenterVolume, volumeFittedSoFar);
  showVolumeStatus(line1, line2, line3);
}

void calculateFermentationState() {
  const float OE =
      SGToApparentPlato(BatchData.batchOG)
      + BatchData.addedPlato;

  const float initialSG =
    ApparentPlatoToSG(OE);

  const float initialDensityKgL =
    initialSG * 0.9982f;  // SG 20/20

  const float initialBeerMassPerLiterG = 1000.0f * initialDensityKgL;

  const float initialExtractMassPerLiterG = initialBeerMassPerLiterG * OE / 100.0f;

  const float producedCO2MassPerLiterG = 44.0095f *
    fmax(0.0, CountersData.CO2MolsProducedPerLiter);

  const float fermentedExtractMassPerLiterG =
    producedCO2MassPerLiterG * 2.0665f / 0.9565f;

  const float producedYeastMassPerLiterG =
    producedCO2MassPerLiterG * 0.11f / 0.9565f;

  const float producedEthanolMassPerLiterG = producedCO2MassPerLiterG / 0.9565f;

  const float remainingExtractMassPerLiterG = initialExtractMassPerLiterG -
    fermentedExtractMassPerLiterG;

  // Cerveja clarificada e degaseificada
  const float currentBeerMassPerLiterG = initialBeerMassPerLiterG -
    producedCO2MassPerLiterG - producedYeastMassPerLiterG;

  const float beerRealPlato =
    100.0f *
    remainingExtractMassPerLiterG /
    currentBeerMassPerLiterG;

  const float beerApparentPlato =
    (beerRealPlato - 0.1808f * OE) /
    0.8192f;

  beerSG =
    ApparentPlatoToSG(beerApparentPlato);

  const float beerABW =
    100.0f *
    producedEthanolMassPerLiterG /
    currentBeerMassPerLiterG;

  const float beerDensityKgL =
    beerSG * 0.9982f;

  beerABV =
    beerABW *
    beerDensityKgL /
    0.78924f;
}

static float adjustedPressureForPostRelief(float postReliefPressure,
                                          unsigned long postReliefMillis) {
  // This correction estimates equilibrium pressure at the acquisition time
  // of postReliefPressure, exactly as the diagnostic regression does. It
  // does not move that pressure back to valve closing.
  if ((int32_t)(postReliefMillis - reliefValveClosedMillis) < 0 ||
      (int32_t)(reliefValveOpenedMillis - pressureOnReliefMeasuredMillis) < 0 ||
      !isfinite(postReliefPressure) || !isfinite(pressureOnReliefMeas) ||
      postReliefPressure + Patm <= 0.0f || pressureOnReliefMeas + Patm <= 0.0f ||
      !isfinite(FMTData.FMTEffectiveVentingExponent) ||
      FMTData.FMTEffectiveVentingExponent <= 0.0f) return NAN;
  return (pressureOnReliefMeas + Patm) * powf(
    (postReliefPressure + Patm) / (pressureOnReliefMeas + Patm),
    1.0f / FMTData.FMTEffectiveVentingExponent) - Patm;
}

static float adjustedEquilibriumPressureForPostRelief(float postReliefPressure,
                                                       unsigned long postReliefMillis,
                                                       bool usePolytropicCorrection = true) {
  const float adjusted = usePolytropicCorrection
    ? adjustedPressureForPostRelief(postReliefPressure, postReliefMillis) : postReliefPressure;
  const float residual = FMTData.targetResidualAfterReliefPercent / 100.0f;
  return pressureOnReliefMeas - (pressureOnReliefMeas - adjusted) / (1.0f - residual);
}

static void clearPolytropicSamples() {
  polytropicSampleCount = polytropicSampleNext = 0;
  polytropicLastSampleMillis = 0;
}

static void collectPolytropicPressureSample() {
  const unsigned long now = pressureAcquiredMillis;
  if (!pressureAcquisitionValid || timeToRegisterPressure ||
      !polytropicReferenceCloseMillis || ControlData.transferValve ||
      !isfinite(ControlData.pressure) || ControlData.pressure <= 0.0f ||
      now - polytropicReferenceCloseMillis < POLYTROPIC_SETTLE_MS ||
      !MILLISDIFF(noPressureReadUntil, 0) || inPressureNoiseWindow()) return;
  if (polytropicLastSampleMillis &&
      now - polytropicLastSampleMillis < POLYTROPIC_SAMPLE_INTERVAL_MS) return;
  if (polytropicLastSampleMillis &&
      now - polytropicLastSampleMillis > POLYTROPIC_MAX_SAMPLE_GAP_MS) clearPolytropicSamples();
  polytropicSamples[polytropicSampleNext] = {now, ControlData.pressure};
  polytropicSampleNext = (polytropicSampleNext + 1) % POLYTROPIC_SAMPLE_COUNT;
  if (polytropicSampleCount < POLYTROPIC_SAMPLE_COUNT) ++polytropicSampleCount;
  polytropicLastSampleMillis = now;
}

static void estimatePreviousReliefPolytropicExponent() {
  polytropicSourceReliefNumber = polytropicReferenceReliefNumber;
  polytropicResultSampleCount = polytropicSampleCount;
  polytropicBackExtrapolatedPressure = NAN;
  polytropicFitSlopeBarPerMinute = NAN;
  polytropicEstimatedExponent = NAN;
  polytropicFitRMSEBar = NAN;
  if (!polytropicReferenceCloseMillis ||
      polytropicSampleCount < POLYTROPIC_MIN_SAMPLES ||
      !isfinite(polytropicReferencePressureBefore) ||
      !isfinite(polytropicReferencePressureAfter) ||
      (int32_t)(polytropicReferencePressureAfterMillis - polytropicReferenceCloseMillis) < 0) return;

  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  unsigned long previousElapsed = 0;
  for (uint8_t i = 0; i < polytropicSampleCount; ++i) {
    const uint8_t idx = (polytropicSampleNext + POLYTROPIC_SAMPLE_COUNT -
                         polytropicSampleCount + i) % POLYTROPIC_SAMPLE_COUNT;
    const PolytropicPressureSample &sample = polytropicSamples[idx];
    const unsigned long elapsedMs = sample.millisStamp - polytropicReferenceCloseMillis;
    if (!isfinite(sample.pressure) || elapsedMs < POLYTROPIC_SETTLE_MS ||
        (int32_t)(sample.millisStamp - polytropicReferencePressureAfterMillis) < 0 ||
        (i && (elapsedMs <= previousElapsed ||
               elapsedMs - previousElapsed < POLYTROPIC_SAMPLE_INTERVAL_MS ||
               elapsedMs - previousElapsed > POLYTROPIC_MAX_SAMPLE_GAP_MS))) return;
    previousElapsed = elapsedMs;
    // Fit against the post-relief measurement timestamp, rather than valve
    // closing.  The extrapolated pressure and pressureAfter are then
    // temporally comparable in the exponent calculation below.
    const double x = (sample.millisStamp - polytropicReferencePressureAfterMillis) / 1000.0;
    const double y = sample.pressure;
    sx += x; sy += y; sxx += x*x; sxy += x*y;
  }

  const double n = polytropicSampleCount;
  const double denominator = n*sxx - sx*sx;
  if (denominator <= 0) return;
  const double slope = (n*sxy - sx*sy) / denominator;
  const double intercept = (sy - slope*sx) / n;
  if (!isfinite(slope) || !isfinite(intercept)) return;
  // Normalize by the actual count so 8- and 16-point windows are comparable.
  // This measures fit residuals inside the window, not extrapolation error.
  double squaredResiduals = 0;
  for (uint8_t i = 0; i < polytropicSampleCount; ++i) {
    const uint8_t idx = (polytropicSampleNext + POLYTROPIC_SAMPLE_COUNT -
                         polytropicSampleCount + i) % POLYTROPIC_SAMPLE_COUNT;
    const PolytropicPressureSample &sample = polytropicSamples[idx];
    const double x = (sample.millisStamp - polytropicReferencePressureAfterMillis) / 1000.0;
    const double residual = sample.pressure - (intercept + slope*x);
    squaredResiduals += residual*residual;
  }
  polytropicFitRMSEBar = sqrt(squaredResiduals / n);
  polytropicBackExtrapolatedPressure = intercept;
  polytropicFitSlopeBarPerMinute = slope * 60.0;

  const double pBeforeAbs = polytropicReferencePressureBefore + Patm;
  const double pAfterAbs = polytropicReferencePressureAfter + Patm;
  const double pAdjustedAbs = intercept + Patm;
  if (!(pAfterAbs > 0 && pAdjustedAbs > pAfterAbs &&
        pBeforeAbs > pAdjustedAbs)) return;
  const double exponent = log(pAfterAbs / pBeforeAbs) /
                          log(pAdjustedAbs / pBeforeAbs);
  if (isfinite(exponent) && exponent >= 0.5 && exponent <= 2.0)
    polytropicEstimatedExponent = exponent;
}

// ===== [DIAG] funções (somente log) =====
// [DIAG] Definida em datalog.cpp (pode ser movida para datalog.h).
void doRecoveryDataLog(unsigned long reliefNumber, float p1, float p1Extrap,
                       float envTemp, float beerTemp, float openSeconds,
                       float shadowExponent, float shadowHsInstant, float shadowHsFiltered,
                       uint8_t points, const unsigned long *ms, const float *pressure);

static bool ENV_TEMP_VALID(float t) {
  // TODO: trocar por "recebido há menos de X min" quando houver timestamp.
  return isfinite(t) && t > -20.0f && t < 60.0f && t != NOTaTEMP;
}

static float shadowExponentNow() {
  float e = FMTData.FMTEffectiveVentingExponent;
  if (ENV_TEMP_VALID(environmentTemp))
    e += SHADOW_EXPONENT_SLOPE_PER_C * fmaxf(0.0f, environmentTemp - SHADOW_EXPONENT_HINGE_C);
  return e;
}

// Mesmo cálculo de adjustedEquilibriumPressureForPostRelief(), com expoente explícito.
static float shadowHeadspaceFromRelief(float exponent) {
  if (!isfinite(exponent) || exponent <= 0.0f ||
      !isfinite(pressureAfterRelief) || !isfinite(pressureOnReliefMeas) ||
      !isfinite(pressureOnReliefExtrap) || pressureOnReliefExtrap <= 0.01f ||
      pressureAfterRelief + Patm <= 0.0f || pressureOnReliefMeas + Patm <= 0.0f) return NAN;
  const float adj = (pressureOnReliefMeas + Patm) *
    powf((pressureAfterRelief + Patm) / (pressureOnReliefMeas + Patm), 1.0f / exponent) - Patm;
  const float residual = FMTData.targetResidualAfterReliefPercent / 100.0f;
  const float peq = pressureOnReliefMeas - (pressureOnReliefMeas - adj) / (1.0f - residual);
  const float f = peq / pressureOnReliefExtrap;
  if (!(f > 0.0f && f < 1.0f)) return NAN;
  return volumeEstimationFromPressureDrop(f);
}

static void startRecoveryCapture(unsigned long closeMillis, float openSeconds) {
  recoveryActive = true;
  recoveryNext = 0;
  recoveryCloseMillis = closeMillis;
  recoveryReliefNumber = CountersData.totalReliefCount + 1;  // incrementado em processPressure(true)
  recoveryP1 = pressureOnReliefMeas;
  recoveryP1Extrap = pressureOnReliefExtrap;
  recoveryEnvTemp = environmentTemp;
  recoveryBeerTemp = ControlData.temperature;
  recoveryOpenSeconds = openSeconds;
  for (uint8_t i = 0; i < RECOVERY_POINTS; ++i) {
    recoveryPressure[i] = NAN;
    recoveryActualMs[i] = 0;
  }
}

static void finishRecoveryCapture() {
  if (!recoveryActive) return;
  recoveryActive = false;
  doRecoveryDataLog(recoveryReliefNumber, recoveryP1, recoveryP1Extrap,
                    recoveryEnvTemp, recoveryBeerTemp, recoveryOpenSeconds,
                    shadowExponent, shadowHeadspaceInstant, shadowHeadspaceFiltered,
                    RECOVERY_POINTS, recoveryActualMs, recoveryPressure);
}

// Chamar a cada loop (ver [E]). Um ponto por chamada, com o tempo real registrado.
static void collectRecoverySample() {
  if (!recoveryActive) return;
  if (ControlData.transferValve) {          // novo relief começou antes de 60 s
    finishRecoveryCapture();
    return;
  }
  if (!pressureAcquisitionValid || inPressureNoiseWindow() ||
      !isfinite(ControlData.pressure)) return;
  const long elapsed = (long)(pressureAcquiredMillis - recoveryCloseMillis);
  if (elapsed < 0) return;
  if (recoveryNext < RECOVERY_POINTS &&
      (unsigned long)elapsed >= RECOVERY_OFFSETS_MS[recoveryNext]) {
    recoveryPressure[recoveryNext] = ControlData.pressure;
    recoveryActualMs[recoveryNext] = (unsigned long)elapsed;
    ++recoveryNext;
  }
  if (recoveryNext >= RECOVERY_POINTS) finishRecoveryCapture();
}
// ===== [DIAG] fim =====

void processPressure(bool afterRelief) {
  // Capture the old volume before a relief can update headspace below.
  const float beerVolumeBeforeEvent = beerVolume;
  updateBeerVolumeFromHeadspace();
  const float reliefPressureReachedTarget = pressureReachedTarget;
  const unsigned long reliefPressureReachedTargetMillis = pressureReachedTargetMillis;
  float instantPressureDropFactor = NAN;
  bool headspaceUpdated = false;
  bool headspaceFromDaily = false;        // [DAILY-HS]
  float headspaceMeasuredForLog = NAN;    // [DAILY-HS]
  bool collectingInitialHeadspaceSamples = false;
  float ejectedMols = 0.0f;
  float ventingElapsedAtLogSeconds = NAN;
  float ejectedMolsBeforeLiquidCorrection = 0.0f;

 // Conditioning: plain refrigerator. The CO2 balance stays as it was at the
  // entry (docs/conditioning.md); only the derived SG/ABV are recomputed from
  // the counters (after a reboot they would otherwise be missing).
  if (SetPointData.mode == MODE_CONDITIONING) {
    if (afterRelief) { // safety relief: close the cycle without accounting
      pressureAfterRelief = ControlData.pressure;
      pressureAfterReliefMillis = pressureAcquiredMillis;
      pressureReachedTarget = 0;
      pressureReachedTargetMillis = 0;
      pendingReliefIndex = -1;
    }
    calculateFermentationState();
    return;
  }

  if (afterRelief && debugging) {
    if (volumeDeterminationActive && pressureSamples) 
      ControlData.pressure = ControlData.pressure * 0.984f + random(-5,5) * 0.0005; 
    else
      ControlData.pressure = ControlData.pressure * 0.942f + random(-5,5) * 0.0005;       
  }      

  updateReliefsPerHour(afterRelief);
  
  if  (afterRelief) {
    pressureAfterRelief = ControlData.pressure;
    pressureAfterReliefMillis = pressureAcquiredMillis;
    // These belong to the relief being completed. Preserve them for its log
    // before clearing the live values used to detect the next relief cycle.
    pressureReachedTarget = 0;
    pressureReachedTargetMillis = 0;
    adjustedPressureAfterRelief = volumeDeterminationActive
      ? pressureAfterRelief : adjustedPressureForPostRelief(pressureAfterRelief, pressureAfterReliefMillis);
       // Todo: tentar fazer esse expoente ser determinado dinamicamente ou entao apurar o fator de queda de pressao ao na ejeçao (talvez so sirva para fermentacao  estavel e intensa)

    const float targetResidual = FMTData.targetResidualAfterReliefPercent / 100.0f;
    adjustedEquilibriumPressure = adjustedEquilibriumPressureForPostRelief(
      pressureAfterRelief, pressureAfterReliefMillis, !volumeDeterminationActive);
    if (volumeDeterminationActive && isfinite(volumeAdjustedEquilibriumSnapshot))
      adjustedEquilibriumPressure = volumeAdjustedEquilibriumSnapshot;
    // Expansion-tank pressure at close. Fermenter and tank both stop at
    // (1 - r) of their way to the common pressure, so the gap left between them
    // is r * pressureOnReliefMeas (the tank starts at 0 gauge) and the tank is at
    // (1 - r) * Peq = (1 - r) * Pon - drop. The former Pon - drop / (1 - r)^2
    // left a gap of only ~2r * drop (+0.9 to 1.7% ejected, batch 160).
    tankPressureAtClose = (1.0f - targetResidual) * pressureOnReliefMeas -
      (pressureOnReliefMeas - pressureAfterRelief);

    if (isfinite(pressureOnReliefExtrap) && pressureOnReliefExtrap > 0.01f &&
        isfinite(adjustedEquilibriumPressure)) {
      instantPressureDropFactor = adjustedEquilibriumPressure / pressureOnReliefExtrap;
    }

    // [DIAG] headspace sombra com expoente dependente da T ambiente (somente log)
    if (!volumeDeterminationActive) {
      shadowExponent = shadowExponentNow();
      shadowHeadspaceInstant = shadowHeadspaceFromRelief(shadowExponent);
      if (isfinite(shadowHeadspaceInstant)) {
        shadowHeadspaceFiltered = isfinite(shadowHeadspaceFiltered)
          ? shadowHeadspaceFiltered + 0.05f * (shadowHeadspaceInstant - shadowHeadspaceFiltered)
          : shadowHeadspaceInstant;
      }
    }

    const bool skipTaskWindow = reliefOpenedDuringTask || taskWindowType != 0;
    if (!skipTaskWindow && isfinite(instantPressureDropFactor) && instantPressureDropFactor > 0.0f &&
        instantPressureDropFactor < 1.0f) {
      const float headspaceMeasured =
        volumeEstimationFromPressureDrop(instantPressureDropFactor);
      const bool validHeadspaceMeasured = isfinite(headspaceMeasured) &&
        headspaceMeasured > 0.0f && headspaceMeasured < FMTData.FMTVolume;
      // [DAILY-HS] Every valid measurement enters the 24-hour average,
      // including reliefs 1-3, except those of the volume determination.
      if (validHeadspaceMeasured) {
        headspaceMeasuredForLog = headspaceMeasured;
        if (!volumeDeterminationActive) dailyHsAccumulate(headspaceMeasured);
      }
      if (validHeadspaceMeasured && CountersData.totalReliefCount < 3 &&
          !isfinite(headspaceFiltered)) {
        // Keep the three initial factors only for the geometric initialization.
        lnPressureDropAvg.add(logf(instantPressureDropFactor));
        collectingInitialHeadspaceSamples = true;
      } else if (validHeadspaceMeasured) {
        if (!isfinite(headspaceFiltered)) {
          if (applyFilteredHeadspace(headspaceMeasured)) {
            headspaceFilterAlpha = 0.05f;
            headspaceUpdated = applySelectedHeadspace(&headspaceFromDaily); // [DAILY-HS]
          }
        } else {
          headspaceFiltered += headspaceFilterAlpha *
            (headspaceMeasured - headspaceFiltered);
          headspaceFilterAlpha = fmaxf(0.05f,
            headspaceFilterAlpha / (1.0f + headspaceFilterAlpha));
          // [DAILY-HS] The EMA stays in headspaceFiltered; apply the selected value.
          headspaceUpdated = applySelectedHeadspace(&headspaceFromDaily);
        }
      }
    }

    if (gasFlowCycle && headspaceUpdated && headspaceFromDaily) {
      gasHeadspaceUpdateStatus = "updated_daily_average"; // [DAILY-HS]
    } else if (gasFlowCycle && headspaceUpdated) {
      gasHeadspaceUpdateStatus = "updated_adjusted_equilibrium";
    } else if (gasFlowCycle && collectingInitialHeadspaceSamples) {
      gasHeadspaceUpdateStatus = "collecting_initial_samples";
    } else if (gasFlowCycle && skipTaskWindow) {
      gasHeadspaceUpdateStatus = "skipped_task_window";
    } else if (gasFlowCycle &&
               strcmp(gasHeadspaceUpdateStatus, "missing_pressure_rise_reference") != 0 &&
               strcmp(gasHeadspaceUpdateStatus, "invalid_pressure_compensation") != 0) {
      gasHeadspaceUpdateStatus = "skipped_invalid_pressure_ratio";
    }
    
    if (pendingReliefIndex >= 0) {
      PressureReliefRecord &record = pressureReliefHistory[pendingReliefIndex];
      record.pressureAfter = ControlData.pressure;
      record.currentAfter = currentReading;
      record.adjustedEquilibriumPressure = adjustedEquilibriumPressure;
    }
    pendingReliefIndex = -1;

    // The tank pressure at close is the measured transfer inventory. Install it
    // as the venting curve baseline only now, after the post-relief reading.
    const float expansionTankMoles = fmaxf(0.0f, tankPressureAtClose) * fermentationReliefVolume() /
      (CONST_R * kelvin(ControlData.temperature));
    gasTankMolesAtClose = expansionTankMoles;
    gasTankPressureAtClose = fmaxf(0.0f, tankPressureAtClose);
    gasVentingFactorAtClose = GasFlow::ventingResidualFactorAtPressure(
      tankPressureAtClose, FMTData.ventingResidualCoefficientA,
      FMTData.ventingResidualCoefficientB, FMTData.ventingResidualCoefficientC);
    if (!gasFlowCycle) {
      gasVentingFermenterVolume = FMTData.FMTVolume;
      gasVentingExpansionVolume = FMTData.FMTReliefVolume;
    }
    gasTankHasHistory = true;
    gasVentedMolesAccounted = 0;
    gasVentedMolesCredited = 0;
    gasVentingActive = true;

    const unsigned long ventingLogMillis = millis();
    accountExpansionTankVenting(ventingLogMillis);
    ventingElapsedAtLogSeconds = (ventingLogMillis - gasClosedMillis) / 1000.0f;
    ejectedMolsBeforeLiquidCorrection = fmaxf(0.0f,
      (float)gasVentedMolesAccounted);
    ejectedMols = ejectedMolsBeforeLiquidCorrection *
      (1.0f - FMTData.liquidMassInGasVentingPercent / 100.0f);
    
    CountersData.totalReliefCount += 1;    
    polytropicReferenceCloseMillis = reliefValveClosedMillis;
    polytropicReferenceReliefNumber = CountersData.totalReliefCount;
    polytropicReferencePressureBefore = pressureOnReliefMeas;
    polytropicReferencePressureAfter = pressureAfterRelief;
    polytropicReferencePressureAfterMillis = pressureAfterReliefMillis;

  }

  recomputeDissolvedCO2MolsFromCurrentState();
  recomputeHeadspaceCO2MolsFromCurrentState();
  recomputeBeerCO2EvolutionFromCurrentState();
  updateCO2MolsProducedPerLiter(beerVolumeBeforeEvent);
  
  //float lastTotalCO2MolsProceduced = CountersData.CO2InSolution + headSpaceCO2Mols + CountersData.totalMolsEjected; está sendo usado ou não?

//  float massCO2Produced = CO2Mass(CountersData.totalCO2MolsProduced - lastTotalCO2MolsProceduced);

//  Serial.println(EstimateSGFromProducedCO2Mol(beerSG, beerVolume, CountersData.totalCO2MolsProduced - lastTotalCO2MolsProceduced) *1000.0);
/*
  float Pi = SGToPlato(BatchData.batchOG) + BatchData.addedPlato;
  float SGu = 1 + (Pi / (258.6-(Pi/258.2)*227.1));
  float totalCO2Mols = CountersData.totalMolsEjected + CountersData.CO2InSolution + headSpaceCO2Mols + expansionTankInventoryMoles();
  beerPlato = (100*(10*beerVolume*SGu*Pi - 90.08 * totalCO2Mols)
                / (1000*beerVolume*SGu - 44.01 * totalCO2Mols) 
             - 0.1808*Pi ) 
             / 0.8192;
  beerSG = PlatoToSG(beerPlato);
  beerABV = 100*(105*(BatchData.batchOG - beerSG) / (100 - beerSG) * (beerSG / 0.79));
  */
  calculateFermentationState();
  
  if (afterRelief && volumeDeterminationActive) {
    volumeIteration++;

    if (volumeAwaitingRecord && volumeRecordIndex >= 0) {
      PressureReliefRecord &record = pressureReliefHistory[volumeRecordIndex];

      record.tiK = volumeStartTemperatureK;
      record.tfK = kelvin(ControlData.temperature);
      record.pi = volumeStartPressure;
      record.nReliefs = (volumeIteration >= volumeStartReliefIteration)
              ? (volumeIteration - volumeStartReliefIteration)
              : 0;
      record.volumeMetricsValid = false;

      if (record.tiK > 0.0f && record.tfK > 0.0f && record.pi > 0.0f && record.nReliefs > 0) {
        record.pfAdjusted = (record.pressureAfter+Patm) * (record.tiK / record.tfK) - Patm;
        if (record.pfAdjusted > 0.0f) {
          record.factorMedio = powf(record.pfAdjusted / record.pi, 1.0f / (float)record.nReliefs);

            record.fermenterVolume = volumeRoutineVolume(record.factorMedio, record.temperature,
                                                         record.environmentTemperature);
            record.volumeMetricsValid = isfinite(record.fermenterVolume) && record.fermenterVolume > 0.0f;
            volumeCalculatedSoFar = record.fermenterVolume;
            volumeCalculatedSoFarValid = record.volumeMetricsValid;
            record.kCumulative = volumeRoutineCalibratedK(record.factorMedio, record.temperature,
                                                          record.environmentTemperature);
        }
      }
      if (volumeDeterminationFast) {
        record.tRefK = expansionTankReferenceKelvin(volumeRoutineK(), volumeRoutineGamma(),
                                                    record.temperature, record.environmentTemperature);
        if (record.pressureBefore > 0.0001f)
          record.kRelief = volumeRoutineCalibratedK(record.pressureAfter / record.pressureBefore,
                                                    record.temperature, record.environmentTemperature);
      }

      volumeFittedSoFar = fitVolumeFromHistory();
      record.fittedVolume = volumeFittedSoFar;
      record.pressureSettled = volumePressureSettled;
      record.volumeDifferencePercent = record.volumeMetricsValid && isfinite(record.fittedVolume)
          ? 100.0f * (record.fittedVolume / record.fermenterVolume - 1.0f) : NAN;
      record.recentFittedVolume = fitVolumeFromHistory(VOLUME_RECENT_FIT_WINDOW);
      record.recentDifferencePercent = isfinite(record.recentFittedVolume) &&
          isfinite(record.fittedVolume) && record.fittedVolume > 0.0f
          ? 100.0f * (record.recentFittedVolume / record.fittedVolume - 1.0f) : NAN;
      volumeConverged = volumeHasConverged();
      record.converged = volumeConverged;

      char line1[40];
      char line2[40];
      char line3[40];
      snprintf(line1, sizeof(line1), "Iteracao: %u", volumeIteration);
      formatVolumeComparison(line2, sizeof(line2), line3, sizeof(line3),
                             record.volumeMetricsValid ? record.fermenterVolume : NAN,
                             record.fittedVolume);
      showVolumeStatus(line1, line2, line3);
      volumeAwaitingRecord = false;
      volumeRecordIndex = -1;
    }
  }

  if (afterRelief) {
    const double totalCO2Mols = CountersData.totalMolsEjected +
                                CountersData.CO2InSolution + headSpaceCO2Mols + expansionTankInventoryMoles();
    ReliefLogData reliefLog = {};
    reliefLog.povotoNumber = (int)FMTData.PovotoNum;
    reliefLog.reliefNumber = CountersData.totalReliefCount;
    reliefLog.valveOpenedMillis = reliefValveOpenedMillis;
    reliefLog.pressureReachedTargetMillis = reliefPressureReachedTargetMillis;
    reliefLog.pressureAfterReliefMillis = pressureAfterReliefMillis;
    reliefLog.temperature = ControlData.temperature;
    reliefLog.targetPressure = SetPointData.setPointPressure;
    reliefLog.atmosphericPressure = Patm;
    reliefLog.environmentTemperature = environmentTemp;
    reliefLog.tankReferenceTemperature = expansionTankReferenceKelvin(
        FMTData.expansionTankKCO2, GAMMA_CO2, ControlData.temperature, environmentTemp) - 273.15f;
    reliefLog.reliefVolume = FMTData.FMTReliefVolume;
    reliefLog.effectiveVentingExponent = FMTData.FMTEffectiveVentingExponent;
    reliefLog.pressureOnReliefMeasured = pressureOnReliefMeas;
    reliefLog.currentOnReliefMeasured = currentOnReliefMeasured;
    reliefLog.pressureReachedTarget = reliefPressureReachedTarget;
    reliefLog.pressureOnReliefExtrapolated = pressureOnReliefExtrap;
    reliefLog.pressureAfterRelief = pressureAfterRelief;
    reliefLog.currentAfterRelief = currentReading;
    reliefLog.adjustedPressureAfterRelief = adjustedPressureAfterRelief;
    reliefLog.adjustedEquilibriumPressure = adjustedEquilibriumPressure;
    reliefLog.tankPressureAtClose = tankPressureAtClose;
    reliefLog.liquidMassInGasVentingPercent = FMTData.liquidMassInGasVentingPercent;
    reliefLog.expansionTankResidualMoles = gasPreviousTankValid ?
      (float)gasPreviousTankRemainingMoles : NAN;
    reliefLog.ventingElapsedAtLogSeconds = ventingElapsedAtLogSeconds;
    reliefLog.previousTankPressureAtCloseBar = (float)gasPreviousTankPressureAtClose;
    reliefLog.previousTankPressureAtOpenBar = (float)gasPreviousTankPressureAtOpen;
    reliefLog.previousTankEjectedMoles = (float)gasPreviousTankCreditedMoles;
    reliefLog.ejectedMolsBeforeLiquidCorrection = ejectedMolsBeforeLiquidCorrection;
    reliefLog.instantaneousPressureDropFactor = instantPressureDropFactor;
    reliefLog.pressureDropFactor = pressureDropFactor;
    reliefLog.headSpaceVolume = CountersData.headSpaceVolume;
    reliefLog.beerVolume = beerVolume;
    reliefLog.ejectedMols = ejectedMols;
    reliefLog.gasFlowModelActive = gasFlowCycle;
    reliefLog.gasOpeningSeconds = gasFlowCycle ? gasLoggedOpenSeconds : NAN;
    reliefLog.gasPreviousVentingSeconds = gasFlowCycle ? gasLoggedVentingSeconds : NAN;
    reliefLog.gasExpansionResidual = gasFlowCycle ? gasLoggedResidual : NAN;
    reliefLog.gasTankMolesAtClose = gasTankMolesAtClose;
    reliefLog.gasVentingResidualFactor = gasFlowCycle ? gasVentingFactorAtClose : NAN;
    reliefLog.gasExpansionOptimalSeconds = gasFlowCycle ? gasLoggedExpansionOptimalSeconds : NAN;
    reliefLog.gasPlannedExpansionSeconds = gasFlowCycle ? gasPlannedExpansionSeconds : NAN;
    reliefLog.gasCalculatedPressureCompensation = gasFlowCycle ? gasCalculatedPressureCompensation : NAN;
    reliefLog.gasAppliedPressureCompensation = gasFlowCycle ? gasAppliedPressureCompensation : NAN;
    reliefLog.gasHeadspaceUpdateStatus = gasFlowCycle ? gasHeadspaceUpdateStatus : "not_applicable";
    reliefLog.gasVentingResidual = gasFlowCycle ? gasLoggedPreviousVentingResidual : NAN;
    reliefLog.totalMolsEjected = CountersData.totalMolsEjected;
    reliefLog.headSpaceCO2Mols = headSpaceCO2Mols;
    reliefLog.dissolvedCO2Mols = CountersData.CO2InSolution;
    reliefLog.totalCO2Mols = totalCO2Mols;
    reliefLog.beerSG = beerSG;
    reliefLog.polytropicSourceReliefNumber = polytropicSourceReliefNumber;
    reliefLog.polytropicSampleCount = polytropicResultSampleCount;
    reliefLog.polytropicBackExtrapolatedPressure = polytropicBackExtrapolatedPressure;
    reliefLog.polytropicFitSlopeBarPerMinute = polytropicFitSlopeBarPerMinute;
    reliefLog.polytropicEstimatedExponent = polytropicEstimatedExponent;
    reliefLog.polytropicFitRMSEBar = polytropicFitRMSEBar;
    // [DAILY-HS] headSpaceVolume above is the applied value.
    reliefLog.headSpaceMeasured = headspaceMeasuredForLog;
    reliefLog.kCO2FromBeerVolume = volumeDeterminationActive ? NAN : kCO2FromBeerVolume(instantPressureDropFactor);
    reliefLog.headSpaceEMA = headspaceFiltered;
    reliefLog.headSpaceDaily = dailyHsValue;
    reliefLog.dailyHours = dailyHsHours;
    reliefLog.dailyState = dailyHsStateLabel();
    doReliefDataLog(reliefLog);

    // The third relief was logged using the configured initial volume. Its
    // pressure sample completes the geometric initialization for the filter,
    // which is installed only for the next relief and later calculations.
    if (CountersData.totalReliefCount == 3 &&
        isfinite(BatchData.initialBeerVolume) &&
        BatchData.initialBeerVolume > 0.0f &&
        BatchData.initialBeerVolume <= FMTData.FMTVolume &&
        !isfinite(headspaceFiltered)) {
      const float initialFactor = expf(lnPressureDropAvg.value());
      const float estimatedHeadspace = volumeEstimationFromPressureDrop(initialFactor);
      if (applyFilteredHeadspace(estimatedHeadspace)) {
        headspaceFilterAlpha = 0.05f;
        lnPressureDropAvg.clear();
        applySelectedHeadspace(); // [DAILY-HS] daily/held value, if any, wins over the EMA
      }
    }
  }
}

bool processReliefCycle() {
  if (inTheMiddleOfRelief()) {
    static unsigned long ReliefStartPressureTime = 0;
    if (timeToStartExpansion) {
      if (!MILLISDIFF(timeToStartExpansion, 0)) {
        digitalWrite(PINVENTINGLED, HIGH); // just to control led indicating delay to finish last cycle relief
        ;Serial.printf("Cor: vermelha (2) %lu\n", millis() / 1000);
      }
      else {
        //;Serial.printf("[PRESSURE] %lu / %lu: Abrindo transfer valve. Pressure=%.2f bar\n", millis(), timeToStartExpansion, ControlData.pressure);
        // Use an acquisition at/after the scheduled opening for the common
        // pre-relief reference used by both correction and diagnostic fit.
        if (!pressureAcquisitionValid || inPressureNoiseWindow() ||
            (int32_t)(pressureAcquiredMillis - timeToStartExpansion) < 0) return true;
        estimatePreviousReliefPolytropicExponent();
        pressureOnReliefMeas = ControlData.pressure;
        pressureOnReliefMeasuredMillis = pressureAcquiredMillis;
        currentOnReliefMeasured = currentReading;
        ReliefStartPressureTime = millis();
        reliefValveOpenedMillis = ReliefStartPressureTime;
        reliefOpenedDuringTask = taskWindowType != 0;
        capturePreviousTankVenting(ReliefStartPressureTime);
        if (gasFlowCycle) {
          timeToFinishExpansion = expansionTimeMilliseconds(pressureOnReliefMeas);
          beginGasExpansion();
        }
        digitalWrite(PINVENTINGLED, LOW);
        digitalWrite(PINTRANSFERVALVE, HIGH);
        markSolenoidToggle();
        ControlData.transferValve = true;
        timeToFinishExpansion += millis();
        timeToStartExpansion = 0; 
      }
    }
    else if (timeToFinishExpansion) {
      if (MILLISDIFF(timeToFinishExpansion, 0)) {
        //;Serial.printf("[PRESSURE] %lu / %lu: Fechando transfer valve. Pressure=%.2f bar\n", millis(), timeToFinishExpansion, ControlData.pressure);
        float extrapolation =
            (pressureOnReliefMeas - pressureReachedTarget)
            * float(millis() - ReliefStartPressureTime)
            / float(ReliefStartPressureTime - pressureReachedTargetMillis);
        if (gasFlowCycle) {
          gasCalculatedPressureCompensation = extrapolation;
          gasAppliedPressureCompensation = NAN;
          gasPressureCompensationValid = false;
          const unsigned long observationMs = ReliefStartPressureTime - pressureReachedTargetMillis;
          if (!pressureReachedTargetMillis || observationMs == 0 || observationMs >= 0x80000000UL) {
            gasHeadspaceUpdateStatus = "missing_pressure_rise_reference";
          } else if (!isfinite(extrapolation) || extrapolation < 0) {
            gasHeadspaceUpdateStatus = "invalid_pressure_compensation";
          } else {
            gasPressureCompensationValid = true;
            gasAppliedPressureCompensation = fminf(extrapolation, 0.1f);
            gasHeadspaceUpdateStatus = "pending_pressure_reading";
          }
        }
        if (!isfinite(extrapolation) || extrapolation < 0) {
          extrapolation = 0.0f;
        } else {
          extrapolation = fminf(extrapolation, 0.1f);
        }
        pressureOnReliefExtrap = pressureOnReliefMeas + extrapolation;
        if (gasFlowCycle && !gasPressureCompensationValid)
          pressureOnReliefExtrap = NAN;

        digitalWrite(PINTRANSFERVALVE, LOW);
        digitalWrite(PINVENTINGLED, HIGH);
        markSolenoidToggle();
        ControlData.transferValve = false;
        const unsigned long transferClosedMillis = millis();
        reliefValveClosedMillis = transferClosedMillis;
        // [DIAG] inicia a captura da recuperação pós-relief (somente log)
        if (!volumeDeterminationActive && SetPointData.mode != MODE_CONDITIONING)
          startRecoveryCapture(transferClosedMillis,
                               (transferClosedMillis - reliefValveOpenedMillis) / 1000.0f);
        pressureAcquisitionValid = false;
        clearPolytropicSamples();
        // The measured inventory is installed at the post-relief reading, but
        // its venting clock starts when the transfer valve actually closes.
        gasClosedMillis = transferClosedMillis;
        if (gasFlowCycle) finishGasExpansion(transferClosedMillis);
        resetCurrentMedianFilter();
        noPressureReadUntil = millis() + TRANSFER_CLOSE_PRESSURE_BLOCK_MS;
        if (volumeDeterminationActive) {
          // Fast mode uses two minutes of venting; slow mode preserves four.
          volumeLastReliefMillis = millis();
          volumePressureSettled = false;
          volumeAdjustedEquilibriumSnapshot = NAN;
          volumeAdjustedEquilibriumCaptureMillis = noPressureReadUntil;
          timeToRegisterPressure = volumeLastReliefMillis +
              (volumeDeterminationFast ? VOLUME_DETERMINATION_FAST_WAIT_MS : VOLUME_DETERMINATION_WAIT_MS);
        } else {
          timeToRegisterPressure = noPressureReadUntil;
        }
        timeToFinishExpansion = 0;
      }
    }
    /*else {
      digitalWrite(PINTRANSFERVALVE, LOW);
      digitalWrite(PINVENTINGLED, LOW);
      ControlData.transferValve = false;  
        ;Serial.println("Cor: apagada2");

    }*/

    if (volumeAdjustedEquilibriumCaptureMillis &&
        pressureAcquisitionValid &&
        (int32_t)(pressureAcquiredMillis - volumeAdjustedEquilibriumCaptureMillis) >= 0) {
      // Capture the equilibrium pressure in the same post-close window used
      // by a normal relief, independently from the later volume reading.
      volumeAdjustedEquilibriumSnapshot =
        adjustedEquilibriumPressureForPostRelief(ControlData.pressure, pressureAcquiredMillis, false);
      volumeAdjustedEquilibriumCaptureMillis = 0;
    }

    if (timeToRegisterPressure) {
      if (pressureAcquisitionValid &&
          (int32_t)(pressureAcquiredMillis - timeToRegisterPressure) >= 0) {
        //;Serial.printf("[PRESSURE] %lu / %lu: Registrando pressão. Pressure=%.2f bar\n", millis(), timeToRegisterPressure, ControlData.pressure);
        timeToRegisterPressure = 0;
        if (volumeDeterminationActive) volumePressureSettled = true;
        processPressure(true);
        if (gasFlowCycle) {
          gasFlowCycle = false;
        }
        digitalWrite(PINVENTINGLED, LOW);        
        digitalWrite(PINTRANSFERVALVE, LOW);

      }
    }    
    
    return true;
  }
  else
    return false;
}


void pressureRelief(bool fromVolumeDetermination) {
  static unsigned long lastReliefEvent = 0;
  if (speedCalibrationActive || inTheMiddleOfRelief()) { // if we're still in the middle of a relief, ignore new relief requests to avoid overlapping and potential hardware issues
    return;
  }
  const bool useGasFlow = !fromVolumeDetermination && gasFlowMode();
  gasFlowCycle = useGasFlow;
  if (gasFlowCycle) {
    gasCycleA = FMTData.expansionTimeCoefficientA;
    gasCycleB = FMTData.expansionTimeCoefficientB;
  }
  
  if (fromVolumeDetermination) {
    const uint16_t nextCycle = volumeIteration + 1;
    // Keep a complete diagnostic history. The first five cycles remain
    // stabilization cycles and are excluded from the volume calculation by
    // delaying the calculation baseline until cycle six.
    const bool shouldRecordCycle = nextCycle <= VOLUME_DETERMINATION_RECORD_END_CYCLE;

    volumeAwaitingRecord = false;
    volumeRecordIndex = -1;
    pendingReliefIndex = -1;

    if (shouldRecordCycle) {
      if (!pressureReliefHistory) {
        return;
      }

      if (nextCycle == VOLUME_DETERMINATION_RECORD_START_CYCLE &&
          (volumeStartTemperatureK <= 0.0f || volumeStartPressure <= 0.0f)) {
        volumeStartTemperatureK = kelvin(ControlData.temperature);
        volumeStartPressure = ControlData.pressure;
        volumeStartReliefIteration = volumeIteration;
      }

      const uint16_t recordIndex = pressureReliefIndex;
      PressureReliefRecord &record = pressureReliefHistory[recordIndex];
      record.timestamp[0] = '\0';
      NTPFormatedDateTime(record.timestamp);
      record.temperature = ControlData.temperature;
      record.environmentTemperature = ENV_TEMP_VALID(environmentTemp) ? environmentTemp : NAN;
      record.tRefK = NAN;
      record.kRelief = NAN;
      record.kCumulative = NAN;
      record.pressureBefore = ControlData.pressure;
      record.pressureAfter = 0.0f;
      record.currentBefore = currentReading;
      record.currentAfter = 0.0f;
      record.tiK = 0.0f;
      record.tfK = 0.0f;
      record.pi = 0.0f;
      record.pfAdjusted = 0.0f;
      record.adjustedEquilibriumPressure = NAN;
      record.nReliefs = 0;
      record.factorMedio = 0.0f;
      record.fermenterVolume = 0.0f;
      record.fittedVolume = NAN;
      record.volumeDifferencePercent = NAN;
      record.recentFittedVolume = NAN;
      record.recentDifferencePercent = NAN;
      record.trendPercentPerCycle = NAN;
      record.converged = false;
      record.volumeMetricsValid = false;
      record.pressureSettled = false;

      pendingReliefIndex = recordIndex;
      volumeAwaitingRecord = true;
      volumeRecordIndex = recordIndex;

      pressureReliefIndex = (pressureReliefIndex + 1) % PRESSURE_RELIEF_HISTORY_MAX;
      if (pressureReliefCount < PRESSURE_RELIEF_HISTORY_MAX) {
        pressureReliefCount++;
      }
    }
  }

  const float pressureSeconds = (ControlData.pressure > 0.0f) ? ControlData.pressure : 0.0f;
  const unsigned long extraMs = (unsigned long)(pressureSeconds * 1000.0L) / (debugging ? 10 : 1);
  const bool isBrewingTransfer = (SetPointData.mode == MODE_BREWING_TRANSFERING);
  const unsigned long reliefDurationDivisor = isBrewingTransfer ? 2UL : 1UL;
  const unsigned long scaledTransferTime = volumeDeterminationActive
      ? (volumeDeterminationFast ? expansionTimeMilliseconds(ControlData.pressure) : VOLUME_DETERMINATION_OPEN_MS)
      : (unsigned long)TRANSFERTIME / reliefDurationDivisor;
  const unsigned long scaledReliefTime = (unsigned long)RELIEFTIME / reliefDurationDivisor;
  const unsigned long scaledExtraMs = (volumeDeterminationActive || isBrewingTransfer) ? 0 : extraMs;
  const unsigned long scaledFinishMs = isBrewingTransfer ? 0 : 1000UL;
  
  
  if (MILLISDIFF(lastReliefEvent,TRANSFERTIME + RELIEFTIME)) {
    timeToStartExpansion     = millis() + holdPressureDueToTemperatureRelays();
  }
  else {
    timeToStartExpansion     = lastReliefEvent + TRANSFERTIME + RELIEFTIME;
    if (timeToStartExpansion < millis() + holdPressureDueToTemperatureRelays()) {
      timeToStartExpansion = millis() + holdPressureDueToTemperatureRelays();
    }
  }
  timeToFinishExpansion    = scaledTransferTime + scaledExtraMs;
  if (gasFlowCycle) {
    timeToStartExpansion = millis() + holdPressureDueToTemperatureRelays();
    if (!timeToStartExpansion) timeToStartExpansion = 1;
    timeToFinishExpansion = expansionTimeMilliseconds(ControlData.pressure);
  }
  timeToRegisterPressure = 0; // garante que estado anterior não vaza para novo ciclo


  lastReliefEvent = millis()


  ;Serial.printf("[PRESSURE] Relief requested: pressure=%.2f bar, extraMs=%lu, transferOpen=%lu, transferClose=%lu\n", 
                ControlData.pressure, extraMs, timeToStartExpansion, timeToFinishExpansion );

  // NÃO chamar processReliefCycle() aqui:
  // pressureRelief() é chamado do async web task (core 0) e processReliefCycle()
  // também é chamado do main loop (core 1) via pressureControl().
  // Chamar aqui cria uma race condition onde ambos executam stage 1 simultaneamente
  // e o += millis() de timeToFinishExpansion é aplicado duas vezes,
  // resultando em timeToFinishExpansion ≈ 2*millis()+5000 → transfer fica aberto por ~16min.

}

void handlePressureHistoryCSV(AsyncWebServerRequest *request) {
  if (pressureHistoryExportInProgress) {
    request->send(409, "text/plain", "History export in progress");
    return;
  }

  pressureHistoryExportInProgress = true;
  pressureHistoryExportAvailable = (pressureReliefHistory ? pressureReliefCount : 0);
  if (pressureHistoryExportAvailable > 100) {
    pressureHistoryExportAvailable = 100;
  }
  pressureHistoryExportStartIndex = (pressureReliefIndex + PRESSURE_RELIEF_HISTORY_MAX - pressureHistoryExportAvailable) % PRESSURE_RELIEF_HISTORY_MAX;
  pressureHistoryExportIndex = 0;
  pressureHistoryHeaderSent = false;
  pressureHistoryExportDone = false;

  AsyncWebServerResponse *response = request->beginChunkedResponse(
      "text/csv",
      [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        (void)index;

        if (!pressureHistoryExportInProgress) {
          return 0;
        }

        if (pressureHistoryExportDone) {
          pressureHistoryExportDone = false;
          pressureHistoryExportInProgress = false;
          pressureHistoryExportAvailable = 0;
          pressureHistoryExportStartIndex = 0;
          pressureHistoryExportIndex = 0;
          pressureHistoryHeaderSent = false;
          return 0;
        }

        size_t len = 0;

        if (!pressureHistoryHeaderSent) {
          const char *header = "data_hora;temperatura;temperatura_ambiente;pressao_antes;pressao_depois;adjustedEquilibriumPressure;corrente_antes_mA;corrente_depois_mA;patm;relief_volume;modo;gas;k;T_ref_K;relief_volume_efetivo;volume_estimado;Ti_K;Tf_K;Pi;Pf_ajustada;nReliefs;fatorMedio;fator_equalizado;volume_fermentador;k_relief;k_para_FMTVolume;volume_ajuste;diferenca_ajuste_percentual;volume_ajuste_ultimas10;diferenca_recente_percentual;tendencia_percentual_por_ciclo;pressao_estabilizada;convergiu\n";
          size_t headerLen = strlen(header);
          if (headerLen > maxLen) {
            headerLen = maxLen;
          }
          memcpy(buffer, header, headerLen);
          len += headerLen;
          pressureHistoryHeaderSent = (headerLen == strlen(header));
          if (!pressureHistoryHeaderSent) {
            return len;
          }
        }

        while (len < maxLen && pressureHistoryExportIndex < pressureHistoryExportAvailable && pressureReliefHistory) {
          const uint16_t idx = (pressureHistoryExportStartIndex + pressureHistoryExportIndex) % PRESSURE_RELIEF_HISTORY_MAX;
          const PressureReliefRecord &record = pressureReliefHistory[idx];

          // Same equalization, residual and k as the routine's result.
          float volumeEstimated = 0.0f;
          if (record.pressureBefore > 0.0001f) {
            const float single = volumeRoutineVolume(record.pressureAfter / record.pressureBefore,
                                                     record.temperature, record.environmentTemperature);
            if (isfinite(single)) volumeEstimated = single;
          }

          char tempBuf[16];
          char envTempBuf[16] = "";
          char modeBuf[8];
          char gasBuf[8];
          char kBuf[16];
          char tRefBuf[16] = "";
          char kReliefBuf[16] = "";
          char effectiveReliefBuf[16];
          char equalizedFactorBuf[16] = "";
          char kAirBuf[16] = "";
          char pBeforeBuf[16];
          char pAfterBuf[16];
          char equilibriumBuf[16];
          char currentBeforeBuf[16];
          char currentAfterBuf[16];
          char patmBuf[16];
          char reliefVolBuf[16];
          char volumeBuf[16];
          char tiBuf[16] = "";
          char tfBuf[16] = "";
          char piBuf[16] = "";
          char pfAdjBuf[16] = "";
          char nReliefsBuf[12] = "";
          char factorBuf[16] = "";
          char fermenterVolBuf[16] = "";
          char fittedVolBuf[16] = "";
          char differenceBuf[16] = "";
          char recentBuf[16] = "";
          char recentDifferenceBuf[16] = "";
          char trendBuf[16] = "";
          char dateBufSafe[32];

          if (record.timestamp[0]) {
            strncpy(dateBufSafe, record.timestamp, sizeof(dateBufSafe) - 1);
            dateBufSafe[sizeof(dateBufSafe) - 1] = '\0';
          } else {
            strncpy(dateBufSafe, "0", sizeof(dateBufSafe));
            dateBufSafe[sizeof(dateBufSafe) - 1] = '\0';
          }
          for (size_t i = 0; dateBufSafe[i] != '\0'; ++i) {
            if ((unsigned char)dateBufSafe[i] < 32 || dateBufSafe[i] == ';' || dateBufSafe[i] == '\n' || dateBufSafe[i] == '\r') {
              dateBufSafe[i] = '_';
            }
          }

          formatFloatCsv(tempBuf, sizeof(tempBuf), record.temperature, 2);
          if (isfinite(record.environmentTemperature))
            formatFloatCsv(envTempBuf, sizeof(envTempBuf), record.environmentTemperature, 2);
          snprintf(modeBuf, sizeof(modeBuf), "%s", volumeDeterminationFast ? "rapido" : "lento");
          snprintf(gasBuf, sizeof(gasBuf), "%s", volumeDeterminationCO2 ? "CO2" : "ar");
          formatFloatCsv(kBuf, sizeof(kBuf), volumeDeterminationFast ? volumeRoutineK() : 1.0f, 3);
          if (isfinite(record.tRefK)) formatFloatCsv(tRefBuf, sizeof(tRefBuf), record.tRefK, 2);
          if (isfinite(record.kRelief)) formatFloatCsv(kReliefBuf, sizeof(kReliefBuf), record.kRelief, 4);
          formatFloatCsv(effectiveReliefBuf, sizeof(effectiveReliefBuf),
                         volumeRoutineReliefVolume(record.temperature, record.environmentTemperature), 3);
          formatFloatCsv(pBeforeBuf, sizeof(pBeforeBuf), record.pressureBefore, 3);
          formatFloatCsv(pAfterBuf, sizeof(pAfterBuf), record.pressureAfter, 3);
          formatFloatCsv(equilibriumBuf, sizeof(equilibriumBuf), record.adjustedEquilibriumPressure, 3);
          formatFloatCsv(currentBeforeBuf, sizeof(currentBeforeBuf), record.currentBefore, 4);
          formatFloatCsv(currentAfterBuf, sizeof(currentAfterBuf), record.currentAfter, 4);
          formatFloatCsv(patmBuf, sizeof(patmBuf), Patm, 3);
          formatFloatCsv(reliefVolBuf, sizeof(reliefVolBuf), FMTData.FMTReliefVolume, 2);
          formatFloatCsv(volumeBuf, sizeof(volumeBuf), volumeEstimated, 2);

          if (record.volumeMetricsValid) {
            formatFloatCsv(tiBuf, sizeof(tiBuf), record.tiK, 2);
            formatFloatCsv(tfBuf, sizeof(tfBuf), record.tfK, 2);
            formatFloatCsv(piBuf, sizeof(piBuf), record.pi, 3);
            formatFloatCsv(pfAdjBuf, sizeof(pfAdjBuf), record.pfAdjusted, 3);
            snprintf(nReliefsBuf, sizeof(nReliefsBuf), "%u", (unsigned)record.nReliefs);
            formatFloatCsv(factorBuf, sizeof(factorBuf), record.factorMedio, 5);
            formatFloatCsv(equalizedFactorBuf, sizeof(equalizedFactorBuf),
                           volumeRoutineEquilibriumFactor(record.factorMedio), 5);
            formatFloatCsv(fermenterVolBuf, sizeof(fermenterVolBuf), record.fermenterVolume, 3);
            if (isfinite(record.kCumulative))
              formatFloatCsv(kAirBuf, sizeof(kAirBuf), record.kCumulative, 4);
          }

          if (isfinite(record.fittedVolume))
            formatFloatCsv(fittedVolBuf, sizeof(fittedVolBuf), record.fittedVolume, 3);
          if (isfinite(record.volumeDifferencePercent))
            formatFloatCsv(differenceBuf, sizeof(differenceBuf), record.volumeDifferencePercent, 3);

          if (isfinite(record.recentFittedVolume))
            formatFloatCsv(recentBuf, sizeof(recentBuf), record.recentFittedVolume, 3);
          if (isfinite(record.recentDifferencePercent))
            formatFloatCsv(recentDifferenceBuf, sizeof(recentDifferenceBuf), record.recentDifferencePercent, 3);
          if (isfinite(record.trendPercentPerCycle))
            formatFloatCsv(trendBuf, sizeof(trendBuf), record.trendPercentPerCycle, 4);

          char line[512];
          int lineLen = snprintf(
              line,
              sizeof(line),
              "%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%u;%u\n",
              dateBufSafe,
              tempBuf,
              envTempBuf,
              pBeforeBuf,
              pAfterBuf,
              equilibriumBuf,
              currentBeforeBuf,
              currentAfterBuf,
              patmBuf,
              reliefVolBuf,
              modeBuf,
              gasBuf,
              kBuf,
              tRefBuf,
              effectiveReliefBuf,
              volumeBuf,
              tiBuf,
              tfBuf,
              piBuf,
              pfAdjBuf,
              nReliefsBuf,
              factorBuf,
              equalizedFactorBuf,
              fermenterVolBuf,
              kReliefBuf,
              kAirBuf,
              fittedVolBuf,
              differenceBuf,
              recentBuf, recentDifferenceBuf, trendBuf,
              (unsigned)record.pressureSettled, (unsigned)record.converged);

          if (lineLen <= 0) {
            pressureHistoryExportIndex++;
            continue;
          }

          if (len + (size_t)lineLen > maxLen) {
            break;
          }

          memcpy(buffer + len, line, (size_t)lineLen);
          len += (size_t)lineLen;
          pressureHistoryExportIndex++;
        }

        if (pressureHistoryExportIndex >= pressureHistoryExportAvailable) {
          pressureHistoryExportDone = true;
        }

        return len;
      });

  response->addHeader("Content-Disposition", "attachment; filename=pressure_history.csv");
  response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  response->addHeader("Pragma", "no-cache");
  response->addHeader("Expires", "0");
  request->send(response);
}

void handlePressureDumpCSV(AsyncWebServerRequest *request) {
  if (pressureDumpInProgress) {
    request->send(409, "text/plain", "Dump in progress");
    return;
  }

  pressureDumpInProgress = true;
  pressureDumpSamples = pressureSamples;
  pressureDumpCount = (pressureDumpSamples ? pressureSamplesCount : 0);
  pressureDumpIndex = 0;
  pressureDumpHeaderSent = false;
  pressureDumpDone = false;

  AsyncWebServerResponse *response = request->beginChunkedResponse(
      "text/csv",
      [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        (void)index;

        if (!pressureDumpInProgress) {
          return 0;
        }

        if (pressureDumpDone) {
          pressureDumpDone = false;
          pressureDumpInProgress = false;
          if (pressureSamples) {
            delete[] pressureSamples;
            pressureSamples = nullptr;
          }
          pressureSamplesIndex = 0;
          pressureSamplesCount = 0;
          pressureLastSampleMillis = 0;
          pressureDumpSamples = nullptr;
          pressureDumpCount = 0;
          pressureDumpIndex = 0;
          pressureDumpHeaderSent = false;
          return 0;
        }

        size_t len = 0;
        if (!pressureDumpHeaderSent) {
          const char *header = "min_sec;millis;pressao\n";
          size_t headerLen = strlen(header);
          if (headerLen > maxLen) {
            headerLen = maxLen;
          }
          memcpy(buffer, header, headerLen);
          len += headerLen;
          pressureDumpHeaderSent = (headerLen == strlen(header));
          if (!pressureDumpHeaderSent) {
            return len;
          }
        }

        while (len < maxLen && pressureDumpIndex < pressureDumpCount && pressureDumpSamples) {
          const PressureSampleRecord &record = pressureDumpSamples[pressureDumpIndex];
          const char *dateBuf = record.timestamp[0] ? record.timestamp : "00:00";
          char pressBuf[16];
          formatFloatCsv(pressBuf, sizeof(pressBuf), record.pressure, 3);
          char line[64];
          int lineLen = snprintf(line, sizeof(line), "%s;%lu;%s\n", dateBuf, record.millisStamp, pressBuf);
          if (lineLen <= 0) {
            pressureDumpIndex++;
            continue;
          }
          if (len + (size_t)lineLen > maxLen) {
            break;
          }
          memcpy(buffer + len, line, (size_t)lineLen);
          len += (size_t)lineLen;
          pressureDumpIndex++;
        }

        if (pressureDumpIndex >= pressureDumpCount) {
          pressureDumpDone = true;
        }

        return len;
      });

  response->addHeader("Content-Disposition", "attachment; filename=pressure_dump.csv");
  request->send(response);
}


// Valve writes happen only in the main loop, just like the relief state machine.
bool startSpeedCalibration(bool venting, char *reason, size_t reasonSize) {
  const char *blocked = nullptr;
  if (SetPointData.mode != MODE_OFF) blocked = "Mode must be OFF";
  else if (speedCalibrationActive || volumeDeterminationActive || inTheMiddleOfRelief() || taskWindowType != 0)
    blocked = "Process in progress";
  else if (!isfinite(ControlData.pressure) || ControlData.pressure < 1.9f)
    blocked = "Insufficient pressure (min 1.9 bar)";
  else if (!isfinite(FMTData.FMTVolume) || FMTData.FMTVolume <= 0.0f ||
           !isfinite(FMTData.FMTReliefVolume) || FMTData.FMTReliefVolume <= 0.0f)
    blocked = "Invalid fermenter or expansion volume";
  if (blocked) {
    snprintf(reason, reasonSize, "%s", blocked);
    return false;
  }
  speedCalibrationVenting = venting;
  speedVolumeFactor = venting ? 0.0f : FMTData.FMTVolume / (FMTData.FMTVolume + FMTData.FMTReliefVolume);
  speedRecordCount[venting ? 1 : 0] = 0;
  speedStage = 0;
  speedStatus = "Running";
  speedCalibrationActive = true;
  return true;
}

String getSpeedCalibrationStatus() {
  return String(speedStatus) + " - expansion: " + String(speedRecordCount[0]) +
         "/" + String(speedRecordTarget(false)) + "; venting: " + String(speedRecordCount[1]) +
         "/" + String(speedRecordTarget(true));
}

static void closeSpeedValve() {
  digitalWrite(PINTRANSFERVALVE, LOW);
  ControlData.transferValve = false;
  markSolenoidToggle();
  resetCurrentMedianFilter();
  noPressureReadUntil = millis() + TRANSFER_CLOSE_PRESSURE_BLOCK_MS;
}

bool isSpeedCalibrationActive() {
  return speedCalibrationActive;
}

static void showSpeedCalibrationProgress(bool force = false) {
  static unsigned long lastUpdate = 0;
  const unsigned long now = millis();
  if (!force && now - lastUpdate < 1000UL) return;
  lastUpdate = now;
  const uint8_t count = speedRecordCount[speedCalibrationVenting ? 1 : 0];
  const uint8_t target = speedRecordTarget(speedCalibrationVenting);
  char line1[48], line2[64], line3[64];
  snprintf(line1, sizeof(line1), "%s speed: %s",
           speedCalibrationVenting ? "Venting" : "Expansion",
           speedCalibrationActive ? "RUN" : (count >= target ||
             (speedCalibrationVenting && count > 0 && speedRecords[1][count - 1].p2 < 0.4f)) ? "END" : "ABORT");
  if (speedCalibrationActive) {
    snprintf(line2, sizeof(line2), "Ciclo %u/%u | %us | %u/%u",
             speedCalibrationVenting ? count + 1 : count / speedDurationCount(false) + 1,
             speedCalibrationVenting ? SPEED_VENTING_CYCLES : SPEED_CYCLES_PER_DURATION,
             speedRequestedSeconds(count, speedCalibrationVenting), count, target);
    const unsigned long duration = speedStage == 1 ? speedOpenDurationMs(count, speedCalibrationVenting) : speedSettlingIntervalMs();
    const unsigned long elapsed = now - speedStageMillis;
    const unsigned long remaining = elapsed >= duration ? 0 : (duration - elapsed + 999UL) / 1000UL;
    snprintf(line3, sizeof(line3), "%s %lus | P: %.3f bar",
             speedStage == 1 ? "Aberta:" : "Espera:", remaining, ControlData.pressure);
  } else {
    snprintf(line2, sizeof(line2), "Medicoes: %u/%u", count, target);
    snprintf(line3, sizeof(line3), "%s", count == target ? "CSV na pagina Calibration" : speedStatus);
  }
  showVolumeStatus(line1, line2, line3);
}

static void processSpeedCalibration() {
  if (SetPointData.mode != MODE_OFF || taskWindowType != 0 ||
      !isfinite(ControlData.pressure) || ControlData.pressure > FMTData.maximumPressure) {
    closeSpeedValve();
    speedStatus = "Aborted: mode, task or pressure changed";
    speedCalibrationActive = false;
    showSpeedCalibrationProgress(true);
    return;
  }
  const unsigned long now = millis();
  uint8_t &count = speedRecordCount[speedCalibrationVenting ? 1 : 0];
  SpeedRecord &record = speedRecords[speedCalibrationVenting ? 1 : 0][count];
  if (debugging && speedStage == 1) {
    // Seconds of valve opening, capped at the requested duration even if a
    // loop iteration runs late. Keep this pressure throughout settling.
    const float seconds = fminf((now - speedStageMillis) / 1000.0f,
                                speedOpenDurationMs(count, speedCalibrationVenting) / 1000.0f);

    float f;
    
    if (!speedCalibrationVenting)
      f = 1-powf(100,-seconds/10);
    else
      f = 1-powf(100,-seconds/100);

    ControlData.pressure = record.p1 * (1.-f) + (record.pl) * f;
  }
  if (speedStage == 0) {
    record.p1 = ControlData.pressure;
    record.pl = record.p1 * speedVolumeFactor;
    if (!speedCalibrationVenting && record.p1 - record.pl <= 0.0f) {
      speedStatus = "Aborted: pressure too low to calculate R";
      speedCalibrationActive = false;
      showSpeedCalibrationProgress(true);
      return;
    }
    digitalWrite(PINTRANSFERVALVE, HIGH);
    ControlData.transferValve = true;
    markSolenoidToggle();
    speedStageMillis = now;
    speedStage = 1;
    showSpeedCalibrationProgress(true);
  } else if (speedStage == 1 && now - speedStageMillis >= speedOpenDurationMs(count, speedCalibrationVenting)) {
    closeSpeedValve();
    record.openSeconds = (millis() - speedStageMillis) / 1000.0f;
    speedStageMillis = now;
    speedStage = 2;
    showSpeedCalibrationProgress(true);
  } else if (speedStage == 2 && now - speedStageMillis >= speedSettlingIntervalMs()) {
    record.p2 = ControlData.pressure;
    if (speedCalibrationVenting) {
      record.r = record.p2 / record.p1;
      record.valid = isfinite(record.r) && record.p1 > SPEED_VENTING_NOISE_PRESSURE_BAR &&
                     record.p2 > SPEED_VENTING_NOISE_PRESSURE_BAR && record.r > 0.0f && record.r < 1.0f;
    } else {
      record.r = (record.p2 - record.pl) / (record.p1 - record.pl);
      record.valid = isfinite(record.r);
    }
    ++count;
    speedStage = 0;
    const bool ventingReachedStopPressure = speedCalibrationVenting && record.p2 < 0.4f;
    if (count == speedRecordTarget(speedCalibrationVenting) || ventingReachedStopPressure) {
      if (speedCalibrationVenting) {
        uint8_t validSampleCount = 0;
        for (uint8_t i = 0; i < count; ++i) {
          if (speedRecords[1][i].valid) ++validSampleCount;
        }
        static char ventingResult[80];
        snprintf(ventingResult, sizeof(ventingResult), "Completed: %u/%u valid%s",
                 validSampleCount, count, ventingReachedStopPressure ? "; below 0.4 bar" : "");
        speedStatus = ventingResult;
      } else speedStatus = "Completed";
      speedCalibrationActive = false;
      showSpeedCalibrationProgress(true);
    }
  }
  if (speedCalibrationActive && speedStage != 0) showSpeedCalibrationProgress();
}

void handleSpeedCalibrationCSV(AsyncWebServerRequest *request) {
  const bool venting = request->hasParam("type") && request->getParam("type")->value() == "venting";
  const uint8_t count = speedRecordCount[venting ? 1 : 0];
  String csv = venting ? "liberacao;tempo_aberto_s;pressureBefore;pressureAfter;residualFactor;valido\n"
                        : "ciclo;tempo;tempo_aberto_s;P1;P2;PL;R\n";
  csv.reserve(4096);
  for (uint8_t i = 0; i < count; ++i) {
    const SpeedRecord &r = speedRecords[venting ? 1 : 0][i];
    char line[160];
    char openTimeBuf[16];
    char timeBuf[24];
    char p1Buf[16];
    char p2Buf[16];
    char plBuf[16];
    char rBuf[16];
    formatFloatCsv(openTimeBuf, sizeof(openTimeBuf), r.openSeconds, 3);
    if (!venting)
      snprintf(timeBuf, sizeof(timeBuf), "%u", speedRequestedSeconds(i, false));
    formatFloatCsv(p1Buf, sizeof(p1Buf), r.p1, 6);
    formatFloatCsv(p2Buf, sizeof(p2Buf), r.p2, 6);
    formatFloatCsv(plBuf, sizeof(plBuf), r.pl, 6);
    formatFloatCsv(rBuf, sizeof(rBuf), r.r, 6);
    if (venting) {
      snprintf(line, sizeof(line), "%u;%s;%s;%s;%s;%s\n", i + 1, openTimeBuf, p1Buf, p2Buf,
               rBuf, r.valid ? "sim" : "nao");
    } else {
      snprintf(line, sizeof(line), "%u;%s;%s;%s;%s;%s;%s\n",
               i / speedDurationCount(false) + 1, timeBuf, openTimeBuf,
               p1Buf, p2Buf, plBuf, rBuf);
    }
    csv += line;
  }
  AsyncWebServerResponse *response = request->beginResponse(200, "text/csv", csv);
  response->addHeader("Content-Disposition", venting ? "attachment; filename=venting_speed.csv" :
                                                      "attachment; filename=expansion_speed.csv");
  request->send(response);
}

void handleExpansionResidualFit(AsyncWebServerRequest *request) {
  if (speedCalibrationActive) {
    request->send(409, "application/json", "{\"ok\":false,\"status\":\"expansion test is still running\"}");
    return;
  }
  const uint8_t count = speedRecordCount[0];
  ExpansionResidualSample samples[SPEED_MAX_RECORDS];
  for (uint8_t i = 0; i < count; ++i) {
    samples[i].seconds = speedRecords[0][i].openSeconds;
    samples[i].residual = speedRecords[0][i].r;
  }
  const ExpansionResidualFitResult fit = fitExpansionResidualCurve(samples, count);
  const bool usable = fit.status == ExpansionResidualFitStatus::Success;
  String json = "{\"ok\":" + String(usable ? "true" : "false") +
      ",\"status\":\"" + expansionResidualFitStatusText(fit.status) + "\"" +
      ",\"used\":" + String((unsigned)fit.usedPoints) +
      ",\"discarded\":" + String((unsigned)fit.discardedPoints) +
      ",\"distinctTimes\":" + String((unsigned)fit.distinctTimes);
  if (isfinite(fit.coefficient)) json += ",\"coefficient\":" + String(fit.coefficient, 9);
  if (isfinite(fit.exponent)) json += ",\"exponent\":" + String(fit.exponent, 9);
  if (isfinite(fit.sse)) json += ",\"sse\":" + String(fit.sse, 9);
  if (isfinite(fit.rmse)) json += ",\"rmse\":" + String(fit.rmse, 9);
  json += "}";
  request->send(200, "application/json", json);
}

bool startVolumeDetermination(bool fast, bool co2, char *reason, size_t reasonSize) {
  if (reason && reasonSize > 0) {
    reason[0] = '\0';
  }

  if (SetPointData.mode != MODE_OFF) {
    if (reason && reasonSize > 0) {
      snprintf(reason, reasonSize, "Mode must be OFF");
    }
    return false;
  }

  if (speedCalibrationActive || volumeDeterminationActive || inTheMiddleOfRelief()) {
    if (reason && reasonSize > 0) {
      snprintf(reason, reasonSize, "Process in progress");
    }
    return false;
  }

  if (FMTData.FMTReliefVolume <= 0.0f) {
    if (reason && reasonSize > 0) {
      snprintf(reason, reasonSize, "Missing FMTReliefVolume");
    }
    return false;
  }

  if (!isfinite(ControlData.pressure) || ControlData.pressure < 1.9f) {
    if (reason && reasonSize > 0) {
      snprintf(reason, reasonSize, "Insufficient pressure (min 1.9 bar)");
    }
    return false;
  }

  if (!pressureReliefHistory) {
    pressureReliefHistory = new(std::nothrow) PressureReliefRecord[PRESSURE_RELIEF_HISTORY_MAX];
    if (!pressureReliefHistory) {
      if (reason && reasonSize > 0) {
        snprintf(reason, reasonSize, "Sem memoria para historico");
      }
      return false;
    }
  }

  // Novo processo de determinacao: limpa historico de relief desta rodada.
  pressureReliefIndex = 0;
  pressureReliefCount = 0;

  volumeDeterminationActive = true;
  volumeDeterminationFast = fast;
  volumeDeterminationCO2 = co2;
  volumeCalibration = {};
  volumeStartPressure = 0.0f;
  volumeStartTemperatureK = 0.0f;
  volumeStartReliefIteration = 0;
  volumeTargetPressure = 0.5f;
  volumeLastReliefMillis = 0;
  volumeIteration = 0;
  volumeAwaitingRecord = false;
  volumeRecordIndex = -1;
  volumeSummaryAvailable = false;
  volumeCalculatedSoFar = 0.0f;
  volumeCalculatedSoFarValid = false;
  volumeFittedSoFar = NAN;
  volumeConverged = false;
  volumePressureSettled = false;
  volumeAdjustedEquilibriumCaptureMillis = 0;
  volumeAdjustedEquilibriumSnapshot = NAN;
  if (!pressureSamples) {
    pressureSamples = new(std::nothrow) PressureSampleRecord[PRESSURE_SAMPLES_MAX];
  }

  pressureSamplesIndex = 0;
  pressureSamplesCount = 0;
  pressureLastSampleMillis = 0;

  if (!pressureSamples) {
    if (reason && reasonSize > 0) {
      snprintf(reason, reasonSize, "Sem memoria para log");
    }
    volumeDeterminationActive = false;
    return false;
  }

  char line1[32];
  char line2[32];
  snprintf(line1, sizeof(line1), "Volume: START");
  snprintf(line2, sizeof(line2), "P ini: %.2f bar", volumeStartPressure);
  showVolumeStatus(line1, line2, "Waiting...");

  if (reason && reasonSize > 0) {
    snprintf(reason, reasonSize, "Process started successfully");
  }
  return true;
}

bool isVolumeDeterminationActive() {
  return volumeDeterminationActive;
}

uint16_t getVolumeDeterminationIteration() {
  return volumeIteration;
}

float getVolumeDeterminationCalculatedSoFar() {
  if (!volumeCalculatedSoFarValid) {
    return NAN;
  }
  return volumeCalculatedSoFar;
}

VolumeKCalibration getVolumeKCalibration() {
  return volumeCalibration;
}

// ===== Pressure stability =====
// Same cycle as the temperature stability. The final target is the slow
// target while a ramp is active; ramp steps and ramp completion keep it
// unchanged. With f = pressureDropFactor, a relief fires at setPoint/sqrt(f)
// and brings the pressure to setPoint*sqrt(f):
// - entry (STABLE, no ramp in progress): inside that relief cycle;
// - exit (UNSTABLE): outside min(setPoint*f, setPoint-0.05) ..
//   max(setPoint/f, setPoint+0.05), so the entry band is always narrower.
static bool pressureStabilityTargetKnown = false;

struct PressureStabilityBands {
  bool entryValid; // false without an estimate of the relief cycle (f)
  float entryLow, entryHigh, exitLow, exitHigh;
};

static PressureStabilityBands pressureStabilityBands(float setPoint) {
  PressureStabilityBands b;
  // f is the fraction of pressure kept by a relief (0 < f < 1).
  b.entryValid = isfinite(pressureDropFactor) && pressureDropFactor > 0.0f && pressureDropFactor < 1.0f;
  const float f = b.entryValid ? pressureDropFactor : 1.0f;
  const float root = sqrtf(f);
  b.entryLow = setPoint * root;
  b.entryHigh = setPoint / root;
  b.exitLow = fminf(setPoint * f, setPoint - 0.05f);
  b.exitHigh = fmaxf(setPoint / f, setPoint + 0.05f);
  return b;
}
static float pressureStabilityLastTarget = NOTaTEMP;

static float pressureFinalTarget() {
  return SetPointData.setPointSlowPressure != NOTaTEMP
      ? SetPointData.setPointSlowPressure : SetPointData.setPointPressure;
}

const char *getPressStateLabel() {
  switch (CountersData.pressState) {
    case TEMP_STATE_STABLE:          return "STABLE";
    case TEMP_STATE_CHANGING_DIRECT: return "CHANGING_DIRECT";
    case TEMP_STATE_CHANGING_SLOW:   return "CHANGING_SLOW";
    case TEMP_STATE_UNSTABLE:        return "UNSTABLE";
    default:                         return "";
  }
}

bool pressureReadingValid() {
  return (pressureSensorConnected || debugging) && isfinite(ControlData.pressure);
}

void markPressureSetpointChanged(bool slow) {
  CountersData.pressState = slow ? TEMP_STATE_CHANGING_SLOW : TEMP_STATE_CHANGING_DIRECT;
  CountersData.pressStableSince = 0;
  pressureStabilityLastTarget = pressureFinalTarget();
  pressureStabilityTargetKnown = true;
  writePressureStabilityToNIV();
}

static void updatePressureStability() {
  const float target = pressureFinalTarget();
  if (!pressureStabilityTargetKnown) {
    // After boot, the state restored from counters refers to this target.
    pressureStabilityLastTarget = target;
    pressureStabilityTargetKnown = true;
  }
  else if (target != pressureStabilityLastTarget) {
    markPressureSetpointChanged(SetPointData.setPointSlowPressure != NOTaTEMP);
  }

  if (SetPointData.setPointPressure <= 0.0f || !pressureReadingValid()) return;
  const float pressure = ControlData.pressure;
  const PressureStabilityBands bands = pressureStabilityBands(SetPointData.setPointPressure);
  if (CountersData.pressState == TEMP_STATE_STABLE) {
    // Leaving the exit band loses stability; it must be reached again.
    if (pressure < bands.exitLow || pressure > bands.exitHigh) {
      CountersData.pressState = TEMP_STATE_UNSTABLE;
      CountersData.pressStableSince = 0;
      writePressureStabilityToNIV();
    }
    return;
  }
  if (SetPointData.setPointSlowPressure != NOTaTEMP) return; // ramp in progress
  if (!bands.entryValid) return; // no estimate of the relief cycle yet
  if (pressure < bands.entryLow || pressure > bands.entryHigh) return;

  // Stays pending until NTP is valid; the time recorded is when it was stamped.
  static unsigned long lastNTPCheck = 0;
  if (!MILLISDIFF(lastNTPCheck, 1000)) return;
  lastNTPCheck = millis();
  const unsigned long now = NTPEpoch();
  if (now == 0) return;
  CountersData.pressState = TEMP_STATE_STABLE;
  CountersData.pressStableSince = now;
  writePressureStabilityToNIV();
}

void pressureControl() {
  accountExpansionTankVenting(millis());
  if (beerSG == 0) {
    beerSG = BatchData.batchOG;
  }

  // During a debug speed test the simulated pressure owns the reading,
  // including the settling interval and the next measurement's P1.
  if (!(debugging && speedCalibrationActive)) {
    readPressure();
  }
  if (speedCalibrationActive) {
    processSpeedCalibration();
    return;
  }
  processSlowPressureTarget();
  updatePressureStability();
  updateCO2DissolvedStateFromEvents();

  if (SetPointData.mode != MODE_OFF &&
      !volumeDeterminationActive &&
      !inTheMiddleOfRelief() &&
      pressureReliefHistory) {
    releasePressureReliefHistory();
  }

  if (derivedStateRestorePending) {
    restoreDerivedStateFromCounters();
    derivedStateRestorePending = false;
  }
  handleCO2BufferRestore();

  if (!processReliefCycle()) {
    static unsigned long lastPressureCheckMillis = 0;
    if (MILLISDIFF(lastPressureCheckMillis, 1000)) {
      lastPressureCheckMillis = millis();
      processPressure(false);
    }
  }

  if (ControlData.pressure >= SetPointData.setPointPressure && pressureReachedTargetMillis==0) {
    pressureReachedTarget = ControlData.pressure;
    pressureReachedTargetMillis = millis();
  }

  collectPolytropicPressureSample();
  collectRecoverySample();  // [DIAG]

  if (volumeDeterminationActive) {
    if (volumeConverged || volumeIteration >= VOLUME_DETERMINATION_RECORD_END_CYCLE) {
      finalizeVolumeDeterminationSummary();
    } else if (!inTheMiddleOfRelief() &&
               MILLISDIFF(volumeLastReliefMillis,
                 volumeDeterminationFast ? VOLUME_DETERMINATION_FAST_WAIT_MS : VOLUME_DETERMINATION_WAIT_MS)) {
      pressureRelief(true);
    }
  } else if (ControlData.pressure > FMTData.maximumPressure) {
    soundAlarm = true;
    pressureRelief(false);
  } else if (gasFlowMode() && !inTheMiddleOfRelief() &&
             (taskWindowType == 0 || MILLISDIFF(taskWindowEndTime, 0)) &&
             shouldStartGasExpansion(millis())) {
    pressureRelief(false);
  } else if (!gasFlowMode() && SetPointData.mode != MODE_CONDITIONING &&
             SetPointData.setPointPressure > 0.0f &&
             ControlData.pressure > (SetPointData.setPointPressure / sqrt(pressureDropFactor)) &&
             (taskWindowType == 0 || MILLISDIFF(taskWindowEndTime, 0))) {
    pressureRelief(false);
  }

  if (!pressureDumpInProgress && volumeDeterminationActive && pressureSamples) {
    const unsigned long now = millis();
    if (pressureLastSampleMillis == 0 || (now - pressureLastSampleMillis) >= PRESSURE_SAMPLE_MIN_MS) {
      if (pressureSamplesCount < PRESSURE_SAMPLES_MAX) {
        pressureLastSampleMillis = now;
        PressureSampleRecord &sample = pressureSamples[pressureSamplesIndex];
        char tempTs[32] = "";
        NTPFormatedDateTime(tempTs);
        const size_t len = strlen(tempTs);
        if (len >= 5) {
          strncpy(sample.timestamp, tempTs + (len - 5), sizeof(sample.timestamp) - 1);
          sample.timestamp[sizeof(sample.timestamp) - 1] = '\0';
        } else {
          strncpy(sample.timestamp, "00:00", sizeof(sample.timestamp));
          sample.timestamp[sizeof(sample.timestamp) - 1] = '\0';
        }
        sample.millisStamp = now;
        sample.pressure = ControlData.pressure;

        pressureSamplesIndex++;
        pressureSamplesCount = pressureSamplesIndex;
      }
    }
  }
}

char tmp[384];
// "Temperature: STABLE since 2026-09-29T14:02:10 (26.4 h)" or just the state.
static void appendStabilityStatus(char *st, const char *name, const char *state, uint32_t since) {
  char when[20];
  formatLocalEpochISO(since, when, sizeof(when));
  const unsigned long now = since ? NTPEpoch() : 0;
  if (since && now >= since)
    snprintf(tmp, sizeof(tmp), "&nbsp;&nbsp;&nbsp;&nbsp;%s: %s since %s (%.1f h)<br>",
             name, state, when, (now - since) / 3600.0f);
  else if (since)
    snprintf(tmp, sizeof(tmp), "&nbsp;&nbsp;&nbsp;&nbsp;%s: %s since %s<br>", name, state, when);
  else
    snprintf(tmp, sizeof(tmp), "&nbsp;&nbsp;&nbsp;&nbsp;%s: %s<br>", name, state);
  strnncat(st, tmp, PRESSURE_STATUS_SIZE);
}

char *getPressureControlStatus(char *st) {

  int16_t rawShuntRegister = 0;
  st[0] = '\0';

  snprintf(tmp, sizeof(tmp), "<br>---------PRESSURE CONTROL:<br>");
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);
  if (1 || pressureSensorConnected) {
    const unsigned long now = millis();
    const float co2CalculationPressure = dissolvedCO2CalculationPressure();
    const float equilibriumCO2Mols = CO2DissolvedMols(
      co2CalculationPressure, beerSG, ControlData.temperature, beerVolume);
    const double totalCO2Mols = CountersData.totalMolsEjected
      + CountersData.CO2InSolution + headSpaceCO2Mols + expansionTankInventoryMoles();

    snprintf(tmp, sizeof(tmp),
             "Measured pressure: %.3f bar<br>Target pressure: %.3f bar<br>Atmospheric pressure: %.3f bar<br>",
             ControlData.pressure, SetPointData.setPointPressure, Patm);
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);
    snprintf(tmp, sizeof(tmp),
             "INA: Filtered current reading: %.2f mA Shunt voltage: %.2f mV Momentary current: %.2f mA<br>",
             currentReading, ina226.getShuntVoltage_mV(), readCurrentFromINA226mA());
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);

    const unsigned long criteriaElapsedMs = co2DissolvedCriteriaElapsedMillis(now);
    snprintf(tmp, sizeof(tmp),
             "CO2 dissolved estimation: %s; gas-phase rate %.2f g/L/d (%s) for %.0f / %.0f min; calculation pressure: %.3f bar<br>",
             co2DissolvedEstimationModeLabel(), co2GasRate, co2StateDecision,
             criteriaElapsedMs / 60000.0f, co2StateHoldMs / 60000.0f,
             co2CalculationPressure);
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);

    strnncat(st, "-------------------------------------------------------------------------------------------<br>", PRESSURE_STATUS_SIZE);
    snprintf(tmp, sizeof(tmp),
             "<br>CO2 moles accounting:<br>&nbsp;&nbsp;&nbsp;&nbsp;Headspace: %.3f<br>&nbsp;&nbsp;&nbsp;&nbsp;Dissolved: %.3f (if in equilibrium: %.3f)<br>&nbsp;&nbsp;&nbsp;&nbsp;Ejected: %.3f<br>&nbsp;&nbsp;&nbsp;&nbsp;Total: %.3f (%.2f g)<br>",
             headSpaceCO2Mols, CountersData.CO2InSolution, equilibriumCO2Mols,
             CountersData.totalMolsEjected, totalCO2Mols, CO2Mass());
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);
    snprintf(tmp, sizeof(tmp),
             "&nbsp;&nbsp;&nbsp;&nbsp;CO2 progress: raw total %.6f; raw delta %+.6f; correction debt %.6f; credited %+.6f mol<br>",
             co2ProducedDiagnosticTotalMols, co2ProducedDiagnosticRawDeltaMols,
             CountersData.co2CorrectionDebt, co2ProducedDiagnosticCreditedDeltaMols);
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);

    strnncat(st, "<br>Expansions:<br>", PRESSURE_STATUS_SIZE);
    snprintf(tmp, sizeof(tmp), "&nbsp;&nbsp;&nbsp;&nbsp;Relief count: %lu<br>",
             (unsigned long)CountersData.totalReliefCount);
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);
    if (!reliefsPerHourAvailable || reliefsPerHourValue < RELIEF_PER_HOUR_MIN_DISPLAY) {
      if (!reliefsPerHourAvailable) {
        snprintf(tmp, sizeof(tmp), "&nbsp;&nbsp;&nbsp;&nbsp;Reliefs/hour: N/A (need %u reliefs, have %u)<br>", (unsigned)RELIEFS_WINDOW_SIZE, reliefMillisCount);
      } else {
        snprintf(tmp, sizeof(tmp), "&nbsp;&nbsp;&nbsp;&nbsp;Reliefs/hour: N/A (< %.2f/h)<br>", RELIEF_PER_HOUR_MIN_DISPLAY);
      }
    } else {
      snprintf(tmp, sizeof(tmp), "&nbsp;&nbsp;&nbsp;&nbsp;Reliefs/hour: %.2f<br>", reliefsPerHourValue);
    }
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);
    snprintf(tmp, sizeof(tmp), "&nbsp;&nbsp;&nbsp;&nbsp;gCO2/L/d: %.2f (%s; %u samples)<br>",
             getBeerCO2EvolutionGramsPerLiterPerDay(), getCO2EvolutionSource(),
             (unsigned)co2EvolutionCount);
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);
    snprintf(tmp, sizeof(tmp), "&nbsp;&nbsp;&nbsp;&nbsp;CO2 buffers at boot: %s<br>", co2BufferStatus);
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);
    {
      // What currently holds the next expansion back (gas flow mode).
      const float threshold = expansionPressureThreshold();
      long waitMs = 0;
      if (gasVentingActive && now - gasClosedMillis < gasMinimumVentingMilliseconds)
        waitMs = (long)(gasMinimumVentingMilliseconds - (now - gasClosedMillis));
      snprintf(tmp, sizeof(tmp),
               "&nbsp;&nbsp;&nbsp;&nbsp;Expansion time: %.2f s now (%.3f bar); %.2f s at relief threshold (%.3f bar)<br>",
               expansionTime(ControlData.pressure), ControlData.pressure,
               expansionTime(threshold), threshold);
      strnncat(st, tmp, PRESSURE_STATUS_SIZE);
    }

    snprintf(tmp, sizeof(tmp),
             "<br>Volumes:<br>&nbsp;&nbsp;&nbsp;&nbsp;Headspace volume: %.2f L<br>&nbsp;&nbsp;&nbsp;&nbsp;Beer volume: %.2f L<br>&nbsp;&nbsp;&nbsp;&nbsp;Dumped volume: %.2f L<br>&nbsp;&nbsp;&nbsp;&nbsp;Expansion pressure drop factor (%%): %.3f<br>",
             CountersData.headSpaceVolume, beerVolume, CountersData.dumpedVolume, pressureDropFactor * 100);
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);
    // [DAILY-HS] Where the applied headspace comes from.
    snprintf(tmp, sizeof(tmp),
             "&nbsp;&nbsp;&nbsp;&nbsp;Headspace sources: 24 h average %.3f L (%u h, %s), held %.3f L, EMA %.3f L<br>",
             dailyHsValue, (unsigned)dailyHsHours, dailyHsStateLabel(),
             CountersData.dailyHs.heldValue, headspaceFiltered);
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);

    snprintf(tmp, sizeof(tmp),
             "<br>Gravity:<br>&nbsp;&nbsp;&nbsp;&nbsp;OG: %.4f (extract: %.3fP)<br>&nbsp;&nbsp;&nbsp;&nbsp;SG: %.4f (apparent extract: %.3fP)<br>&nbsp;&nbsp;&nbsp;&nbsp;ABV: %.2f%%<br>",
             BatchData.batchOG, SGToApparentPlato(BatchData.batchOG),
             beerSG, SGToApparentPlato(beerSG), beerABV);
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);

    strnncat(st, "<br>Stability:<br>", PRESSURE_STATUS_SIZE);
    appendStabilityStatus(st, "Temperature", getTempStateLabel(), CountersData.tempStableSince);
    appendStabilityStatus(st, "Pressure", getPressStateLabel(), CountersData.pressStableSince);
    if (SetPointData.setPointPressure > 0.0f) {
      const PressureStabilityBands bands = pressureStabilityBands(SetPointData.setPointPressure);
      if (bands.entryValid)
        snprintf(tmp, sizeof(tmp),
                 "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;bands: entry %.3f-%.3f bar, exit %.3f-%.3f bar<br>",
                 bands.entryLow, bands.entryHigh, bands.exitLow, bands.exitHigh);
      else
        snprintf(tmp, sizeof(tmp),
                 "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;bands: entry n/a (no drop factor), exit %.3f-%.3f bar<br>",
                 bands.exitLow, bands.exitHigh);
      strnncat(st, tmp, PRESSURE_STATUS_SIZE);
    }
  } else {
    snprintf(tmp, sizeof(tmp), "INA226 Pressure Sensor: DISCONNECTED<br>Atmospheric pressure: %.3f bar<br>", Patm);
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);
  }

  // --- Diagnóstico de relés ---
  unsigned long now = millis();
  strnncat(st, "<br>--- Relay cycle ---<br>", PRESSURE_STATUS_SIZE);
  if (!inTheMiddleOfRelief()) {
    strnncat(st, "Cycle: IDLE<br>", PRESSURE_STATUS_SIZE);
  } else {
    strnncat(st, "Cycle: ACTIVE<br>", PRESSURE_STATUS_SIZE);
    if (timeToStartExpansion) {
      snprintf(tmp, sizeof(tmp), "Stage: waiting to open transfer (in %ld ms)<br>",
               (long)(timeToStartExpansion - now));
    } else if (timeToFinishExpansion) {
      snprintf(tmp, sizeof(tmp), "Stage: transfer OPEN - closes in %ld ms<br>",
               (long)(timeToFinishExpansion - now));
    } else if (timeToRegisterPressure) {
      snprintf(tmp, sizeof(tmp), "Stage: transfer CLOSED - measuring pressure in %ld ms<br>",
               (long)(timeToRegisterPressure - now));
    }
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);
    snprintf(tmp, sizeof(tmp),
             "timeToStartExpansion=%lu<br>timeToFinishExpansion=%lu<br>timeToRegisterPressure=%lu<br>",
             timeToStartExpansion, timeToFinishExpansion,
             timeToRegisterPressure);
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);
  }

  return st;
}


/*Implementar redução por purga
Implementar tasks de adição de volume o de intercenção em gás
implementar conditioning
quando reiniciou perdeu contador de co2 ejetado
ver se coutersdata dá persistência à densidade final e parar de atualizar em conditioning
*/


