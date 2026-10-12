#include "PressureControl.h"
#include "VentModel.h"
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
#include "AtmosphericPressure.h"
#include "SensorZero.h"
#include "PovotoAlarms.h"
#include "VentingTest.h"
#include "ValveCleaning.h"
#include "KegVolume.h"
#include <esp_timer.h>
#include <math.h>
#include <algorithm>


#define PRESSURE_MEDIAN_WINDOW 9
#define CURRENT_MEDIAN_MIN_SAMPLE_MS 20 // defined for 16 samples at 1.1ms in the INA226 (~17.6ms)
#define INSTABILITYTHRESHOLDMA 4.0f

#define INA226_I2C_ADDRESS 0x44
#define INA226_SHUNT_OHMS 3.0f

#define SOLENOID_NOISE_MS 400

#define TRANSFER_CLOSE_PRESSURE_BLOCK_MS (2*CURRENT_MEDIAN_MIN_SAMPLE_MS*PRESSURE_MEDIAN_WINDOW) // gives time to fill and renovate the whole vector, avoiding transient pneumatics effects

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


float  adjustedPressureAfterRelief;

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

static unsigned long lastSolenoidToggleMillis = 0;
// Fraction of the gauge pressure kept by a vent, 1 - target drop: a vent fires
// at setPoint/sqrt(f) and settles near setPoint*sqrt(f) (pressure stability
// bands use the same cycle).
float pressureDropFactor = 0.94f;

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

static void updateBeerVolumeFromHeadspace() {
  // The headspace measured by the vents needs three completed vents. Until
  // then, use the batch's measured fill volume for every CO2 calculation.
  if (CountersData.totalReliefCount < 3 && !isfinite(headspaceFiltered) &&
      isfinite(BatchData.initialBeerVolume) &&
      BatchData.initialBeerVolume > 0.0f &&
      BatchData.initialBeerVolume <= FMTData.FMTVolume) {
    beerVolume = BatchData.initialBeerVolume;
    CountersData.headSpaceVolume = FMTData.FMTVolume - beerVolume;
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

// ===== Direct vent (docs/vent.md) =====
// The valve vents the fermenter to the atmosphere for a time estimated from
// the flow curve and the headspace. The CO2 that left is counted from the real
// opening time and the pressures at opening and right after closing, without
// the headspace and without reading the sensor during the transient.
static constexpr float VENT_MAX_SECONDS = 30.0f;  // hard cap of one opening
static constexpr float VENT_DEFAULT_GAS_C = 20.0f; // no valid temperature at all
struct VentPlan {
  float headspace;         // L: applied value (24-h average, held, EMA or initial)
  float smallestHeadspace; // L: smallest plausible value, for the safety limit
  float gasTemperatureC;
  const char *gasSource;   // "headspace" or "liquid"
  float estimatedSeconds;  // the target drop with `headspace`
  float safetySeconds;     // the safety drop with `smallestHeadspace`
  float plannedSeconds;
  const char *limit;       // "estimate", "minimum" or "safety"
};
static VentPlan ventPlan = {NAN, NAN, NAN, "liquid", NAN, NAN, NAN, ""};
static int64_t ventOpenMicros = 0;
static float ventRealSeconds = NAN;
static const char *ventHeadspaceStatus = "not_evaluated";
// Last vent, for the Relief log and the status.
struct VentResult {
  float pMeanGauge;        // log mean of P_on and the pressure right after closing
  float cooling;
  float flowConstant;      // CO2, with ratio, Z and level
  float flowMoles;         // flow model, before the liquid-mass correction
  float countedMoles;      // flow model, or fermenter side for a vent without effect
  const char *counted;     // "flow", "fermenter-side" or "none" (not Fermenting)
  float apparentHeadspace; // flow moles / isothermal drop (NAN = not available)
  float dropPercent;       // isothermal drop / P_on extrapolated
};
static VentResult ventResult = {NAN, NAN, NAN, NAN, NAN, "none", NAN, NAN};

static float ventGasKelvin() {
  float t = gasTemperature();
  if (!isfinite(t) || t == NOTaTEMP || t < -20.0f || t > 60.0f) t = VENT_DEFAULT_GAS_C;
  return kelvin(t);
}

// Headspace for the opening time: the applied value; before any, the
// fermenter minus the beer informed for the batch.
static float initialHeadspaceEstimate() {
  const float beer = BatchData.initialBeerVolume - CountersData.dumpedVolume;
  return (isfinite(beer) && beer > 0.0f && beer < FMTData.FMTVolume) ? FMTData.FMTVolume - beer : NAN;
}

static float ventHeadspaceForTime() {
  if (isfinite(CountersData.headSpaceVolume) && CountersData.headSpaceVolume > 0.0f &&
      CountersData.headSpaceVolume < FMTData.FMTVolume) return CountersData.headSpaceVolume;
  const float initial = initialHeadspaceEstimate();
  return isfinite(initial) ? initial : FMTData.FMTVolume;
}

// Smallest of the applied value, the EMA and the informed one (a wrong
// headspace must not open the valve for too long).
static float ventSmallestHeadspace() {
  float smallest = ventHeadspaceForTime();
  if (isfinite(headspaceFiltered) && headspaceFiltered > 0.0f) smallest = fminf(smallest, headspaceFiltered);
  const float initial = initialHeadspaceEstimate();
  if (isfinite(initial)) smallest = fminf(smallest, initial);
  return smallest;
}

// Opening time from the pressure at opening: the target drop with the applied
// headspace, at most the safety drop with the smallest plausible one, and at
// least the minimum opening (which wins over the safety limit).
static void planVent(float pOnGauge) {
  ventPlan.headspace = ventHeadspaceForTime();
  ventPlan.smallestHeadspace = ventSmallestHeadspace();
  ventPlan.gasSource = gasTemperatureSource();
  const double tK = ventGasKelvin();
  ventPlan.gasTemperatureC = tK - 273.15;
  const double exponent = FMTData.FMTEffectiveVentingExponent;
  const VentModel::Curve &curve = FMTData.ventCurve;
  ventPlan.estimatedSeconds = VentModel::ventSeconds(curve, FMTData.flowLevelFactor, ventPlan.headspace, pOnGauge,
      pOnGauge * (1.0 - FMTData.ventTargetDropPercent / 100.0), Patm, tK, exponent);
  ventPlan.safetySeconds = VentModel::ventSeconds(curve, FMTData.flowLevelFactor, ventPlan.smallestHeadspace, pOnGauge,
      pOnGauge * (1.0 - FMTData.ventSafetyDropPercent / 100.0), Patm, tK, exponent);
  float seconds = ventPlan.estimatedSeconds;
  ventPlan.limit = "estimate";
  if (isfinite(ventPlan.safetySeconds) && (!isfinite(seconds) || seconds > ventPlan.safetySeconds)) {
    seconds = ventPlan.safetySeconds;
    ventPlan.limit = "safety";
  }
  if (!isfinite(seconds) || seconds < FMTData.ventMinSeconds) {
    seconds = FMTData.ventMinSeconds;
    ventPlan.limit = "minimum";
  }
  ventPlan.plannedSeconds = fminf(seconds, VENT_MAX_SECONDS);
}

static bool ventCountsCO2() {
  return SetPointData.mode == MODE_FERMENTING;
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
    CountersData.CO2InSolution + double(headSpaceCO2Mols);
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

// Measured CO2 outside the liquid: vented and in the headspace.
static double gasPhaseCO2Mols() {
  return CountersData.totalMolsEjected + double(headSpaceCO2Mols);
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
  data.calculationPressure = dissolvedCO2CalculationPressure();
  data.equilibriumMols = (!co2StateIsHalfLife(co2DissolvedState) && isfinite(henryMeanMols))
      ? henryMeanMols
      : CO2DissolvedMols(data.calculationPressure, beerSG, ControlData.temperature, beerVolume);
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

// CO2 in the headspace above the start: real gas (Z of CO2) at the gas
// temperature now and at the batch start (docs/vent.md). The atmospheric part
// is subtracted at both ends, as with the gauge pressures before.
static double headspaceMolesAboveAtmosphere(double gauge, double tKelvin, double volume) {
  const double pAbs = gauge + Patm;
  return volume / (CONST_R * tKelvin) *
      (pAbs / VentModel::co2Z(pAbs, tKelvin) - Patm / VentModel::co2Z(Patm, tKelvin));
}

static void recomputeHeadspaceCO2MolsFromCurrentState() {
  const float currentTemperatureK = ventGasKelvin();
  const float initialTemperatureK = kelvin(BatchData.startTemperature);
  if (!isfinite(CountersData.headSpaceVolume) || CountersData.headSpaceVolume <= 0.0f ||
      !isfinite(currentTemperatureK) || currentTemperatureK <= 0.0f ||
      !isfinite(initialTemperatureK) || initialTemperatureK <= 0.0f ||
      !isfinite(ControlData.pressure) || !isfinite(BatchData.startPressure)) {
    headSpaceCO2Mols = 0.0f;
    return;
  }

  headSpaceCO2Mols = headspaceMolesAboveAtmosphere(ControlData.pressure, currentTemperatureK, CountersData.headSpaceVolume)
                   - headspaceMolesAboveAtmosphere(BatchData.startPressure, initialTemperatureK, CountersData.headSpaceVolume);
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

void getCO2RateLabel(char *out, size_t outSize) {
  if (!out || outSize == 0) {
    return;
  }

  // gCO2/L/d with the graph's rule: Fermenting only, positive and outside a
  // transition. Same text on the TFT, the web dashboard and the cloud one.
  if (SetPointData.mode != MODE_FERMENTING) {
    snprintf(out, outSize, "g CO2");
    return;
  }
  const float rate = getBeerCO2EvolutionGramsPerLiterPerDay();
  if (isfinite(rate) && rate > 0.0f && !co2RateInTransition())
    snprintf(out, outSize, "gCO2 [%.1f/L/d]", rate);
  else
    snprintf(out, outSize, "gCO2 [---]");
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
    headSpaceCO2Mols;
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

DumpRecalcResult applyDumpWindowHeadspaceRecalc(float headspaceBeforeL, float pressureBeforeBar,
                                                float pressureAfterBar) {
  // Logged in the Task log even when the recalculation is not applied (dH = NAN).
  DumpRecalcResult result = {NAN, NAN};
  // Ideal gas with constant moles/temperature during the dump window:
  // P1_abs * H_before = P2_abs * H_after  =>  H_after = H_before * P1_abs / P2_abs.
  if (headspaceBeforeL <= 0.0f) {
    return result;
  }

  const float p1Abs = pressureBeforeBar + Patm;
  const float p2ReadAbs = pressureAfterBar + Patm;
  if (p1Abs <= 0.0f || p2ReadAbs <= 0.0f || p1Abs <= p2ReadAbs) {
    return result;
  }
  // P2 is read right after the valve closes, with the expanded gas still cold.
  // Same polytropic correction as the reliefs: P2_iso = P1 * (P2/P1)^(1/n).
  // Batch 160 (2 dumps, volumes measured): 1.21 -> 0.93 L (0.95 measured) and
  // 0.68 -> 0.53 L (0.53). Empirical: n also absorbs a ~10 mbar drop that does
  // not recover; a small dump (20-30 mbar) may still read high.
  const float exponent = FMTData.FMTEffectiveVentingExponent;
  const float p2Abs = (isfinite(exponent) && exponent > 0.0f)
      ? p1Abs * powf(p2ReadAbs / p1Abs, 1.0f / exponent) : p2ReadAbs;
  result.pressureAfterIsoBar = p2Abs - Patm;

  float headAfter = headspaceBeforeL * (p1Abs / p2Abs);
  if (headAfter < 0.0f) {
    return result;
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
      result.deltaH = deltaL;
    }
  }


  recomputeHeadspaceCO2MolsFromCurrentState();

  const float impliedHeadspaceDeltaL = CountersData.headSpaceVolume - headspaceBeforeL;
  Serial.printf("[DUMP] Headspace recalculated: H_before=%.3f L, P1=%.3f bar, P2=%.3f bar (iso %.3f), H_after=%.3f L, dH=%.3f L, Beer=%.3f L\n",
                headspaceBeforeL,
                pressureBeforeBar,
                pressureAfterBar,
                result.pressureAfterIsoBar,
                CountersData.headSpaceVolume,
                impliedHeadspaceDeltaL,
                beerVolume);
  Serial.printf("[DAILY-HS] dump rebase dH=%.3f L\n", result.deltaH);
  return result;
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

// Calibration curve without the clipping at zero: below the zero current the
// result is negative (sensor zero check, docs/pressure-sensor-zero.md).
float convertCurrentToPressureUnclipped(float current) {
  float pressure = 0.0f;
  if (FMTData.pressure2Bar != 0.0f && FMTData.pressure2Current != 0.0f) { // quadratic interpolation using Lagrange polynomials
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
  return pressure;
}

float convertCurrentToPressure(float current) {
  if (current <= FMTData.pressure0Current) return 0.0f;
  const float pressure = convertCurrentToPressureUnclipped(current);
  return pressure < 0.0f ? 0.0f : pressure;
}

bool inTheMiddleOfRelief() {
  return (timeToStartExpansion || timeToFinishExpansion || timeToRegisterPressure);
}

unsigned long pressureAcquisitionMillis() {
  return pressureAcquiredMillis;
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

// ===== Pressure sensor check (docs/alarms.md) =====
// Fermenting or conditioning with a pressure target and no valid pressure
// reading for 10 s: the INA226 does not answer, or the loop current is out of
// [I(0 bar) - 1.5 mA, I(maximum pressure) + 1.5 mA] (open loop, short, sensor
// fault). Detection only: the pressure control is not changed. In debug mode
// only a negative considered pressure (debug page) triggers it.
static constexpr float PRESSURE_SENSOR_CURRENT_MARGIN_MA = 1.5f;
static constexpr unsigned long PRESSURE_SENSOR_INVALID_TOLERANCE_MS = 10000UL;
static constexpr unsigned long INA226_PROBE_INTERVAL_MS = 2000UL;
static bool ina226Responding = true;
static bool pressureSensorAlarm = false;
static bool pressureSensorNoResponse = false;
static const char *pressureSensorProblemText = "";

// Calibration current at a gauge pressure: straight line through points 0-1,
// or 1-2 above point 1 when point 2 is calibrated.
static float currentAtPressure(float bar) {
  if (FMTData.pressure2Bar > FMTData.pressure1Bar && bar > FMTData.pressure1Bar)
    return FMTData.pressure1Current + (bar - FMTData.pressure1Bar) *
        (FMTData.pressure2Current - FMTData.pressure1Current) / (FMTData.pressure2Bar - FMTData.pressure1Bar);
  if (!(FMTData.pressure1Bar > 0.0f)) return NAN;
  return FMTData.pressure0Current + bar * (FMTData.pressure1Current - FMTData.pressure0Current) / FMTData.pressure1Bar;
}

static float pressureSensorCurrentLow() {
  return FMTData.pressure0Current - PRESSURE_SENSOR_CURRENT_MARGIN_MA;
}

static float pressureSensorCurrentHigh() {
  return currentAtPressure(FMTData.maximumPressure) + PRESSURE_SENSOR_CURRENT_MARGIN_MA;
}

// Main loop only (I2C probe).
void checkPressureSensor() {
  static unsigned long lastProbe = 0;
  static unsigned long invalidSince = 0; // 0 = valid reading
  const unsigned long now = millis();
  if (pressureSensorConnected && (lastProbe == 0 || now - lastProbe >= INA226_PROBE_INTERVAL_MS)) {
    lastProbe = now | 1;
    ina226Responding = ina226.isConnected();
  }
  // Debug mode: only the considered pressure counts (the debug page value or
  // the simulation), so the alarm is tested by setting a negative pressure.
  const bool responding = debugging || (pressureSensorConnected && ina226Responding);
  const float high = pressureSensorCurrentHigh();
  const bool inRange = debugging
      ? ControlData.pressure >= 0.0f
      : responding && isfinite(currentReading) && isfinite(high) &&
        currentReading >= pressureSensorCurrentLow() && currentReading <= high;
  if (inRange) invalidSince = 0;
  else if (invalidSince == 0) invalidSince = now | 1;
  pressureSensorNoResponse = !responding;
  pressureSensorProblemText = !responding ? "INA226 not responding"
      : inRange ? "" : debugging ? "negative pressure (debug)" : "current out of range";
  const bool controlling = (SetPointData.mode == MODE_FERMENTING || SetPointData.mode == MODE_CONDITIONING) &&
      SetPointData.setPointPressure != NOTaTEMP;
  pressureSensorAlarm = controlling && invalidSince != 0 && now - invalidSince >= PRESSURE_SENSOR_INVALID_TOLERANCE_MS;
}

bool pressureSensorAlarmActive() {
  return pressureSensorAlarm;
}

bool pressureSensorNotResponding() {
  return pressureSensorNoResponse;
}

const char *pressureSensorProblem() {
  return pressureSensorProblemText;
}

// Alarm e-mail (PovotoAlarms.cpp).
String describePressureSensor() {
  char text[256];
  snprintf(text, sizeof(text),
           "Problem: %s\nINA226: %s\nFiltered current: %.2f mA (valid %.2f to %.2f mA)\n"
           "Considered pressure: %.3f bar (target %.3f bar, maximum %.2f bar)\n",
           pressureSensorProblemText[0] ? pressureSensorProblemText : "none",
           pressureSensorConnected && ina226Responding ? "responding" : "NOT responding",
           currentReading, pressureSensorCurrentLow(), pressureSensorCurrentHigh(),
           ControlData.pressure, SetPointData.setPointPressure, FMTData.maximumPressure);
  return String(text);
}

// ===== Vent check (docs/alarms.md) =====
// The isothermal drop of each fermentation vent against the drop expected
// from the CO2 counted by the flow model and the applied headspace (24-hour
// average, frozen reference). Below 0.25x the vent had (almost) no effect:
// valve not opening, outlet blocked, or the expansion tank reconnected by
// mistake. Such a vent is not counted by the flow model (it would count moles
// that did not leave): its CO2 is the fermenter-side loss over the reference
// headspace, and it does not feed the headspace. Two in a row raise the
// alarm; a normal vent clears it.
static constexpr float RELIEF_NO_EFFECT_DROP_RATIO = 0.25f;
static constexpr uint8_t RELIEF_CHECK_CONSECUTIVE = 2;
static constexpr float RELIEF_CHECK_MIN_PRESSURE = 0.1f; // bar
static constexpr uint8_t RELIEF_CHECK_SKIPPED = 0;
static constexpr uint8_t RELIEF_CHECK_OK = 1;
static constexpr uint8_t RELIEF_CHECK_NO_EFFECT = 3; // value kept from the expansion-tank check
static uint8_t reliefCheckLast = RELIEF_CHECK_SKIPPED;  // last evaluated vent
static uint8_t reliefCheckRun = 0;                      // consecutive vents without effect
static bool reliefCheckAlarm = false;
static float reliefCheckDropRatio = NAN;
static float reliefCheckExpectedDrop = NAN;
static float reliefCheckMeasuredDrop = NAN;
static float reliefCheckPressure = NAN;
static float reliefCheckReference = NAN;

static const char *reliefCheckLabel(uint8_t check) {
  switch (check) {
    case RELIEF_CHECK_OK: return "ok";
    case RELIEF_CHECK_NO_EFFECT: return "no-effect";
    default: return "";
  }
}

// flowMoles: CO2 counted by the flow model for this vent; tKelvin: gas.
static uint8_t evaluateVentCheck(double flowMoles, double tKelvin, bool taskWindow) {
  const float reference = CountersData.headSpaceVolume;
  const bool applicable = ventCountsCO2() && !taskWindow && !pressureSensorAlarm &&
      !(debugging && debugPressureOverride) && isfinite(reference) && reference > 0.0f &&
      isfinite(flowMoles) && flowMoles > 0.0 &&
      isfinite(pressureOnReliefExtrap) && pressureOnReliefExtrap >= RELIEF_CHECK_MIN_PRESSURE &&
      isfinite(adjustedPressureAfterRelief);
  reliefCheckDropRatio = NAN;
  uint8_t result = RELIEF_CHECK_SKIPPED;
  if (applicable) {
    // Isothermal drops of P/Z (real gas): measured, and expected from the
    // counted moles in the reference headspace.
    const double pOn = pressureOnReliefExtrap + Patm, pAfter = adjustedPressureAfterRelief + Patm;
    const double measured = pOn / VentModel::co2Z(pOn, tKelvin) - pAfter / VentModel::co2Z(pAfter, tKelvin);
    const double expected = flowMoles * CONST_R * tKelvin / reference;
    reliefCheckDropRatio = float(measured / expected);
    reliefCheckMeasuredDrop = pressureOnReliefExtrap - adjustedPressureAfterRelief;
    reliefCheckExpectedDrop = float(reliefCheckMeasuredDrop / reliefCheckDropRatio);
    reliefCheckPressure = pressureOnReliefExtrap;
    reliefCheckReference = reference;
    result = reliefCheckDropRatio < RELIEF_NO_EFFECT_DROP_RATIO ? RELIEF_CHECK_NO_EFFECT : RELIEF_CHECK_OK;
  }
  if (result == RELIEF_CHECK_NO_EFFECT) {
    reliefCheckRun = reliefCheckLast == RELIEF_CHECK_NO_EFFECT ? reliefCheckRun + 1 : 1;
    if (reliefCheckRun >= RELIEF_CHECK_CONSECUTIVE) reliefCheckAlarm = true;
    Serial.printf("[VENT] without effect: drop %.4f bar, expected %.4f bar (%.2fx), run %u\n",
                  reliefCheckMeasuredDrop, reliefCheckExpectedDrop, reliefCheckDropRatio, reliefCheckRun);
  } else if (result == RELIEF_CHECK_OK) {
    reliefCheckRun = 0;
    reliefCheckAlarm = false;
  }
  if (result != RELIEF_CHECK_SKIPPED) reliefCheckLast = result;
  return result;
}

bool reliefCheckAlarmActive() {
  return reliefCheckAlarm && SetPointData.mode == MODE_FERMENTING && !(debugging && debugPressureOverride);
}

// Alarm e-mail (PovotoAlarms.cpp).
String describeReliefCheck() {
  char text[320];
  snprintf(text, sizeof(text),
           "Vent check: %s\nLast vent drop: %.4f bar, expected %.4f bar from the counted CO2 (%.2fx; alarm below %.2fx)\n"
           "Pressure at the vent: %.3f bar\nReference headspace: %.2f L\n",
           reliefCheckAlarm ? "no pressure drop (valve not opening or outlet blocked?)" : "ok",
           reliefCheckMeasuredDrop, reliefCheckExpectedDrop, reliefCheckDropRatio,
           RELIEF_NO_EFFECT_DROP_RATIO, reliefCheckPressure, reliefCheckReference);
  return String(text);
}

// ===== Valve stuck open (docs/alarms.md) =====
// After a fermentation vent the pressure recovers (the gas warms back and the
// fermentation goes on). If it is still falling STUCK_OPEN_CHECK_MS after the
// reading right after closing, by more than STUCK_OPEN_FALL_BAR, the valve did
// not close: alarm at once (the fermenter is losing its pressure). A later
// vent that recovers normally clears it.
static constexpr unsigned long STUCK_OPEN_CHECK_MS = 30000UL;
static constexpr float STUCK_OPEN_FALL_BAR = 0.005f;
static bool stuckOpenArmed = false;
static float stuckOpenReference = NAN;      // pressure read right after closing
static unsigned long stuckOpenReferenceMillis = 0;
static float stuckOpenLastFall = NAN;       // reference - pressure STUCK_OPEN_CHECK_MS later
static bool stuckOpenAlarm = false;

static void armStuckOpenCheck() {
  stuckOpenArmed = ventCountsCO2() && isfinite(pressureAfterRelief);
  stuckOpenReference = pressureAfterRelief;
  stuckOpenReferenceMillis = pressureAfterReliefMillis;
}

static void checkValveStuckOpen() {
  if (!ventCountsCO2()) {
    stuckOpenArmed = false;
    stuckOpenAlarm = false;
    return;
  }
  if (!stuckOpenArmed || inTheMiddleOfRelief() || !pressureAcquisitionValid ||
      (int32_t)(pressureAcquiredMillis - stuckOpenReferenceMillis) < (int32_t)STUCK_OPEN_CHECK_MS) return;
  stuckOpenArmed = false;
  // A task may lower the pressure on purpose (dump); the debug page owns the pressure.
  if (taskWindowType != 0 || reliefOpenedDuringTask || pressureSensorAlarm ||
      (debugging && debugPressureOverride)) return;
  stuckOpenLastFall = stuckOpenReference - ControlData.pressure;
  const bool stuck = stuckOpenLastFall > STUCK_OPEN_FALL_BAR;
  if (stuck != stuckOpenAlarm)
    Serial.printf("[VENT] valve stuck open: %s (pressure fell %.4f bar in %lu s after closing)\n",
                  stuck ? "YES" : "cleared", stuckOpenLastFall, STUCK_OPEN_CHECK_MS / 1000UL);
  stuckOpenAlarm = stuck;
}

bool valveStuckOpenAlarmActive() {
  return stuckOpenAlarm && SetPointData.mode == MODE_FERMENTING && !(debugging && debugPressureOverride);
}

String describeValveStuckOpen() {
  char text[200];
  snprintf(text, sizeof(text),
           "Pressure after the last vent: %.4f bar right after closing, %.4f bar lower %lu s later "
           "(alarm above %.3f bar)\nPressure now: %.3f bar (target %.3f bar)\n",
           stuckOpenReference, stuckOpenLastFall, STUCK_OPEN_CHECK_MS / 1000UL, STUCK_OPEN_FALL_BAR,
           ControlData.pressure, SetPointData.setPointPressure);
  return String(text);
}

static char calibrationDisplayLines[3][80] = {};

void showCalibrationPanel(const char *line1, const char *line2, const char *line3) {
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

const char *getCalibrationDisplayLine(uint8_t index) {
  return index < 3 ? calibrationDisplayLines[index] : "";
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

void processPressure(bool afterRelief) {
  // Capture the old volume before a vent can update the headspace below.
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
  uint8_t reliefCheckResult = RELIEF_CHECK_SKIPPED;
  float ejectedMolsBeforeLiquidCorrection = 0.0f;
  double ventGasK = NAN;

 // Conditioning: plain refrigerator. The CO2 balance stays as it was at the
  // entry (docs/conditioning.md); only the derived SG/ABV are recomputed from
  // the counters (after a reboot they would otherwise be missing).
  if (SetPointData.mode == MODE_CONDITIONING) {
    if (afterRelief) { // safety vent: close the cycle without accounting
      pressureAfterRelief = ControlData.pressure;
      pressureAfterReliefMillis = pressureAcquiredMillis;
      pressureReachedTarget = 0;
      pressureReachedTargetMillis = 0;
    }
    calculateFermentationState();
    return;
  }

  // Bench boards have no valve: the target drop is simulated.
  if (afterRelief && debugging)
    ControlData.pressure = ControlData.pressure * (1.0f - FMTData.ventTargetDropPercent / 100.0f) +
                           random(-5, 5) * 0.0005;

  updateReliefsPerHour(afterRelief);

  if (afterRelief) {
    pressureAfterRelief = ControlData.pressure;
    pressureAfterReliefMillis = pressureAcquiredMillis;
    // These belong to the vent being completed. Preserve them for its log
    // before clearing the live values used to detect the next cycle.
    pressureReachedTarget = 0;
    pressureReachedTargetMillis = 0;
    // Settled pressure: the reading right after closing (gas still cold)
    // through the polytropic exponent.
    adjustedPressureAfterRelief = adjustedPressureForPostRelief(pressureAfterRelief, pressureAfterReliefMillis);
    if (isfinite(pressureOnReliefExtrap) && pressureOnReliefExtrap > 0.01f &&
        isfinite(adjustedPressureAfterRelief)) {
      instantPressureDropFactor = adjustedPressureAfterRelief / pressureOnReliefExtrap;
    }

    // Count (docs/vent.md): the flow model with the real opening time, the
    // log mean of the pressures at opening and right after closing, and the
    // gas temperature. No headspace and no reading during the transient.
    ventGasK = ventGasKelvin();
    const VentModel::VentCount count = VentModel::ventMoles(FMTData.ventCurve, FMTData.flowLevelFactor,
        ventRealSeconds, pressureOnReliefMeas, pressureAfterRelief, Patm, ventGasK,
        FMTData.FMTEffectiveVentingExponent);
    ventResult.pMeanGauge = count.pMeanGauge;
    ventResult.cooling = count.cooling;
    ventResult.flowConstant = count.flowConstant;
    ventResult.flowMoles = count.moles;
    ventResult.apparentHeadspace = (isfinite(pressureOnReliefExtrap) && isfinite(adjustedPressureAfterRelief))
        ? VentModel::apparentHeadspace(count.moles, pressureOnReliefExtrap, adjustedPressureAfterRelief, Patm, ventGasK)
        : NAN;
    ventResult.dropPercent = isfinite(instantPressureDropFactor) ? 100.0f * (1.0f - instantPressureDropFactor) : NAN;

    const bool skipTaskWindow = reliefOpenedDuringTask || taskWindowType != 0;
    reliefCheckResult = evaluateVentCheck(count.moles, ventGasK, skipTaskWindow);
    const bool noEffect = reliefCheckResult == RELIEF_CHECK_NO_EFFECT;
    double moles = count.moles;
    ventResult.counted = "flow";
    if (noEffect) {
      // Fermenter-side loss over the reference headspace (about nothing when
      // the valve did not open).
      const double pOn = pressureOnReliefExtrap + Patm, pAfter = adjustedPressureAfterRelief + Patm;
      moles = fmax(0.0, (pOn / VentModel::co2Z(pOn, ventGasK) - pAfter / VentModel::co2Z(pAfter, ventGasK)) *
                        CountersData.headSpaceVolume / (CONST_R * ventGasK));
      ventResult.counted = "fermenter-side";
    }
    if (!ventCountsCO2()) {
      moles = 0.0;
      ventResult.counted = "none";
    }
    ventResult.countedMoles = moles;
    ejectedMolsBeforeLiquidCorrection = moles;
    ejectedMols = moles * (1.0f - FMTData.liquidMassInGasVentingPercent / 100.0f);
    CountersData.totalMolsEjected += ejectedMols;

    // Apparent headspace of the vent: the 24-hour average and the EMA, except
    // in task windows and for a vent without effect.
    if (ventCountsCO2() && !skipTaskWindow && !noEffect) {
      const float headspaceMeasured = ventResult.apparentHeadspace;
      const bool validHeadspaceMeasured = isfinite(headspaceMeasured) &&
        headspaceMeasured > 0.0f && headspaceMeasured < FMTData.FMTVolume;
      // [DAILY-HS] Every valid measurement enters the 24-hour average,
      // including vents 1-3.
      if (validHeadspaceMeasured) {
        headspaceMeasuredForLog = headspaceMeasured;
        dailyHsAccumulate(headspaceMeasured);
      }
      if (validHeadspaceMeasured && CountersData.totalReliefCount < 3 &&
          !isfinite(headspaceFiltered)) {
        // Keep the three initial values only for the geometric initialization.
        lnPressureDropAvg.add(logf(headspaceMeasured));
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

    if (!ventCountsCO2()) {
      ventHeadspaceStatus = "not_counted";
    } else if (headspaceUpdated && headspaceFromDaily) {
      ventHeadspaceStatus = "updated_daily_average"; // [DAILY-HS]
    } else if (headspaceUpdated) {
      ventHeadspaceStatus = "updated_ema";
    } else if (collectingInitialHeadspaceSamples) {
      ventHeadspaceStatus = "collecting_initial_samples";
    } else if (noEffect) {
      ventHeadspaceStatus = "skipped_vent_check";
    } else if (skipTaskWindow) {
      ventHeadspaceStatus = "skipped_task_window";
    } else if (!isfinite(pressureOnReliefExtrap)) {
      ventHeadspaceStatus = "missing_pressure_rise_reference";
    } else {
      ventHeadspaceStatus = "skipped_invalid_drop";
    }

    CountersData.totalReliefCount += 1;
    polytropicReferenceCloseMillis = reliefValveClosedMillis;
    polytropicReferenceReliefNumber = CountersData.totalReliefCount;
    polytropicReferencePressureBefore = pressureOnReliefMeas;
    polytropicReferencePressureAfter = pressureAfterRelief;
    polytropicReferencePressureAfterMillis = pressureAfterReliefMillis;
    armStuckOpenCheck();
  }

  recomputeDissolvedCO2MolsFromCurrentState();
  recomputeHeadspaceCO2MolsFromCurrentState();
  recomputeBeerCO2EvolutionFromCurrentState();
  updateCO2MolsProducedPerLiter(beerVolumeBeforeEvent);
  calculateFermentationState();

  if (afterRelief) {
    const double totalCO2Mols = CountersData.totalMolsEjected +
                                CountersData.CO2InSolution + headSpaceCO2Mols;
    ReliefLogData reliefLog = {};
    reliefLog.povotoNumber = (int)FMTData.PovotoNum;
    reliefLog.reliefNumber = CountersData.totalReliefCount;
    reliefLog.valveOpenedMillis = reliefValveOpenedMillis;
    reliefLog.pressureReachedTargetMillis = reliefPressureReachedTargetMillis;
    reliefLog.pressureAfterReliefMillis = pressureAfterReliefMillis;
    reliefLog.temperature = ControlData.temperature;
    reliefLog.targetPressure = SetPointData.setPointPressure;
    reliefLog.atmosphericPressure = Patm;
    reliefLog.atmosphericPressureSource = atmosphericPressureSourceLabel();
    reliefLog.environmentTemperature = environmentTemp;
    reliefLog.gasTemperature = ventGasK - 273.15;
    reliefLog.gasTemperatureSource = ventPlan.gasSource;
    reliefLog.effectiveVentingExponent = FMTData.FMTEffectiveVentingExponent;
    reliefLog.pressureOnReliefMeasured = pressureOnReliefMeas;
    reliefLog.currentOnReliefMeasured = currentOnReliefMeasured;
    reliefLog.pressureReachedTarget = reliefPressureReachedTarget;
    reliefLog.pressureOnReliefExtrapolated = pressureOnReliefExtrap;
    reliefLog.pressureAfterRelief = pressureAfterRelief;
    reliefLog.currentAfterRelief = currentReading;
    reliefLog.adjustedPressureAfterRelief = adjustedPressureAfterRelief;
    reliefLog.ventEstimatedSeconds = ventPlan.estimatedSeconds;
    reliefLog.ventSafetySeconds = ventPlan.safetySeconds;
    reliefLog.ventPlannedSeconds = ventPlan.plannedSeconds;
    reliefLog.ventLimit = ventPlan.limit;
    reliefLog.ventHeadspaceForTime = ventPlan.headspace;
    reliefLog.ventRealSeconds = ventRealSeconds;
    reliefLog.ventT0Seconds = FMTData.ventCurve.t0;
    reliefLog.ventMeanPressure = ventResult.pMeanGauge;
    reliefLog.ventCoolingFactor = ventResult.cooling;
    reliefLog.ventFlowConstant = ventResult.flowConstant;
    reliefLog.flowLevelFactor = FMTData.flowLevelFactor;
    reliefLog.ventFlowMoles = ventResult.flowMoles;
    reliefLog.ventCounted = ventResult.counted;
    reliefLog.ventDropPercent = ventResult.dropPercent;
    reliefLog.ventTargetDropPercent = FMTData.ventTargetDropPercent;
    reliefLog.liquidMassInGasVentingPercent = FMTData.liquidMassInGasVentingPercent;
    reliefLog.ejectedMolsBeforeLiquidCorrection = ejectedMolsBeforeLiquidCorrection;
    reliefLog.instantaneousPressureDropFactor = instantPressureDropFactor;
    reliefLog.headSpaceVolume = CountersData.headSpaceVolume;
    reliefLog.beerVolume = beerVolume;
    reliefLog.ejectedMols = ejectedMols;
    reliefLog.ventHeadspaceStatus = ventHeadspaceStatus;
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
    reliefLog.headSpaceApparent = ventResult.apparentHeadspace;
    reliefLog.headSpaceEMA = headspaceFiltered;
    reliefLog.headSpaceDaily = dailyHsValue;
    reliefLog.dailyHours = dailyHsHours;
    reliefLog.dailyState = dailyHsStateLabel();
    reliefLog.reliefDropRatio = reliefCheckResult != RELIEF_CHECK_SKIPPED ? reliefCheckDropRatio : NAN;
    reliefLog.reliefCheck = reliefCheckLabel(reliefCheckResult);
    doReliefDataLog(reliefLog);

    // The third vent was logged using the configured initial volume. Its
    // value completes the geometric initialization of the filter, which is
    // installed only for the next vent and later calculations.
    if (CountersData.totalReliefCount == 3 &&
        isfinite(BatchData.initialBeerVolume) &&
        BatchData.initialBeerVolume > 0.0f &&
        BatchData.initialBeerVolume <= FMTData.FMTVolume &&
        !isfinite(headspaceFiltered) && lnPressureDropAvg.value() != 0.0f) {
      const float estimatedHeadspace = expf(lnPressureDropAvg.value());
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
        digitalWrite(PINVENTINGLED, HIGH); // LED: a vent is waiting to open
      }
      else {
        // Use an acquisition at/after the scheduled opening for the common
        // pre-vent reference used by both correction and diagnostic fit.
        if (!pressureAcquisitionValid || inPressureNoiseWindow() ||
            (int32_t)(pressureAcquiredMillis - timeToStartExpansion) < 0) return true;
        estimatePreviousReliefPolytropicExponent();
        pressureOnReliefMeas = ControlData.pressure;
        pressureOnReliefMeasuredMillis = pressureAcquiredMillis;
        currentOnReliefMeasured = currentReading;
        planVent(pressureOnReliefMeas);
        digitalWrite(PINVENTINGLED, LOW);
        digitalWrite(PINTRANSFERVALVE, HIGH); ventOpenMicros = esp_timer_get_time();
        ReliefStartPressureTime = millis();
        reliefValveOpenedMillis = ReliefStartPressureTime;
        reliefOpenedDuringTask = taskWindowType != 0;
        markSolenoidToggle();
        ControlData.transferValve = true;
        timeToFinishExpansion = ReliefStartPressureTime + (unsigned long)lroundf(ventPlan.plannedSeconds * 1000.0f);
        if (!timeToFinishExpansion) timeToFinishExpansion = 1;
        timeToStartExpansion = 0;
      }
    }
    else if (timeToFinishExpansion) {
      if (MILLISDIFF(timeToFinishExpansion, 0)) {
        // The closing is timed here, in the loop: a late loop only makes the
        // drop larger; the real opening time is what counts.
        digitalWrite(PINTRANSFERVALVE, LOW); const int64_t ventCloseMicros = esp_timer_get_time();
        ventRealSeconds = float((ventCloseMicros - ventOpenMicros) / 1e6);
        // Rise of the pressure by the fermentation during the opening, from
        // the rate before it (at most 0.1 bar): the reference for the drop.
        float extrapolation =
            (pressureOnReliefMeas - pressureReachedTarget)
            * float(millis() - ReliefStartPressureTime)
            / float(ReliefStartPressureTime - pressureReachedTargetMillis);
        const unsigned long observationMs = ReliefStartPressureTime - pressureReachedTargetMillis;
        const bool riseReference = pressureReachedTargetMillis && observationMs != 0 && observationMs < 0x80000000UL &&
            isfinite(extrapolation) && extrapolation >= 0.0f;
        // Without a reference only the count runs (it does not use it); the
        // headspace and the vent check need it.
        pressureOnReliefExtrap = riseReference || !ventCountsCO2()
            ? pressureOnReliefMeas + (riseReference ? fminf(extrapolation, 0.1f) : 0.0f) : NAN;

        digitalWrite(PINVENTINGLED, HIGH);
        markSolenoidToggle();
        ControlData.transferValve = false;
        reliefValveClosedMillis = millis();
        pressureAcquisitionValid = false;
        clearPolytropicSamples();
        resetCurrentMedianFilter();
        noPressureReadUntil = millis() + TRANSFER_CLOSE_PRESSURE_BLOCK_MS;
        timeToRegisterPressure = noPressureReadUntil;
        if (!timeToRegisterPressure) timeToRegisterPressure = 1;
        timeToFinishExpansion = 0;
      }
    }

    if (timeToRegisterPressure) {
      if (pressureAcquisitionValid &&
          (int32_t)(pressureAcquiredMillis - timeToRegisterPressure) >= 0) {
        timeToRegisterPressure = 0;
        processPressure(true);
        digitalWrite(PINVENTINGLED, LOW);
        digitalWrite(PINTRANSFERVALVE, LOW);
      }
    }

    return true;
  }
  else
    return false;
}


// Called from the main loop and from the web task (Control page): it only
// schedules; processReliefCycle() opens and closes the valve in the loop.
void pressureRelief() {
  if (inTheMiddleOfRelief()) { // never overlap two vents
    return;
  }
  timeToRegisterPressure = 0; // no state of the previous cycle leaks into the new one
  timeToFinishExpansion = 0;
  unsigned long start = millis() + holdPressureDueToTemperatureRelays();
  if (!start) start = 1;
  timeToStartExpansion = start;
  Serial.printf("[PRESSURE] Vent requested: pressure=%.3f bar, opening at %lu\n",
                ControlData.pressure, timeToStartExpansion);
}

bool calibrationRoutineActive() {
  return sensorZeroActive() || ventingTestActive() || valveCleaningActive() || kegVolumeActive();
}

// Routines that drive the valve themselves: no automatic vent meanwhile.
static bool valveRoutineActive() {
  return ventingTestActive() || valveCleaningActive() || kegVolumeActive();
}

int64_t routineValve(bool open) {
  digitalWrite(PINTRANSFERVALVE, open ? HIGH : LOW);
  const int64_t micros = esp_timer_get_time();
  digitalWrite(PINVENTINGLED, open ? HIGH : LOW);
  ControlData.transferValve = open;
  markSolenoidToggle();
  if (!open) {
    resetCurrentMedianFilter();
    noPressureReadUntil = millis() + TRANSFER_CLOSE_PRESSURE_BLOCK_MS;
  }
  return micros;
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
  if (beerSG == 0) {
    beerSG = BatchData.batchOG;
  }
  // A debug board's routine simulates the pressure itself.
  if (!(debugging && valveRoutineActive())) readPressure();
  pressureDropFactor = 1.0f - FMTData.ventTargetDropPercent / 100.0f;
  if (valveRoutineActive()) {
    processVentingTest();
    processValveCleaning();
    processKegVolume();
    return;
  }
  processSensorZero();
  processSlowPressureTarget();
  updatePressureStability();
  updateCO2DissolvedStateFromEvents();

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
  checkValveStuckOpen();

  if (inTheMiddleOfRelief()) return;
  if (ControlData.pressure > FMTData.maximumPressure) {
    soundAlarm = true;
    pressureRelief();
  } else if (SetPointData.mode != MODE_CONDITIONING && SetPointData.setPointPressure > 0.0f &&
             ControlData.pressure >= expansionPressureThreshold() &&
             (taskWindowType == 0 || MILLISDIFF(taskWindowEndTime, 0))) {
    pressureRelief();
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
  {
    const unsigned long now = millis();
    const float co2CalculationPressure = dissolvedCO2CalculationPressure();
    const float equilibriumCO2Mols = CO2DissolvedMols(
      co2CalculationPressure, beerSG, ControlData.temperature, beerVolume);
    const double totalCO2Mols = CountersData.totalMolsEjected
      + CountersData.CO2InSolution + headSpaceCO2Mols;

    snprintf(tmp, sizeof(tmp),
             "Measured pressure: %.3f bar<br>Target pressure: %.3f bar<br>",
             ControlData.pressure, SetPointData.setPointPressure);
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);
    appendAtmosphericPressureStatus(st, PRESSURE_STATUS_SIZE);
    snprintf(tmp, sizeof(tmp),
             "INA: filtered %.3f mA, momentary %.3f mA, shunt %.3f mV; pressure without clipping %.4f bar<br>",
             currentReading, readCurrentFromINA226mA(), ina226.getShuntVoltage_mV(),
             convertCurrentToPressureUnclipped(currentReading));
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);

    const unsigned long criteriaElapsedMs = co2DissolvedCriteriaElapsedMillis(now);
    snprintf(tmp, sizeof(tmp),
             "CO2 dissolved estimation: %s; gas-phase rate %.2f g/L/d (%s) for %.0f / %.0f min; calculation pressure: %.3f bar<br>",
             co2DissolvedEstimationModeLabel(), co2GasRate, co2StateDecision,
             criteriaElapsedMs / 60000.0f, co2StateHoldMs / 60000.0f,
             co2CalculationPressure);
    strnncat(st, tmp, PRESSURE_STATUS_SIZE);

    strnncat(st, "--------------------<br>", PRESSURE_STATUS_SIZE);
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

    strnncat(st, "<br>Vents:<br>", PRESSURE_STATUS_SIZE);
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
      // docs/vent.md: the next opening at the threshold and the last vent (the
      // parameters and the curve are on the Calibration page).
      const VentModel::Curve &curve = FMTData.ventCurve;
      const float threshold = expansionPressureThreshold();
      const double tK = ventGasKelvin();
      const float headspace = ventHeadspaceForTime();
      snprintf(tmp, sizeof(tmp),
               "&nbsp;&nbsp;&nbsp;&nbsp;Next vent at %.3f bar: %.2f s (Vh %.2f L, gas %.2f C %s)%s<br>",
               threshold, VentModel::ventSeconds(curve, FMTData.flowLevelFactor, headspace, threshold,
                   threshold * (1.0 - FMTData.ventTargetDropPercent / 100.0), Patm, tK, FMTData.FMTEffectiveVentingExponent),
               headspace, tK - 273.15, gasTemperatureSource(), curve.epoch ? "" : "; default curve");
      strnncat(st, tmp, PRESSURE_STATUS_SIZE);
      snprintf(tmp, sizeof(tmp),
               "&nbsp;&nbsp;&nbsp;&nbsp;Last: %.2f s (%s), real %.3f s; Pm %.3f bar; %.4f mol %s; Vh %.2f L; drop %.2f%%<br>",
               ventPlan.plannedSeconds, ventPlan.limit, ventRealSeconds, ventResult.pMeanGauge,
               ventResult.countedMoles, ventResult.counted, ventResult.apparentHeadspace, ventResult.dropPercent);
      strnncat(st, tmp, PRESSURE_STATUS_SIZE);
    }

    snprintf(tmp, sizeof(tmp),
             "<br>Volumes:<br>&nbsp;&nbsp;&nbsp;&nbsp;Headspace volume: %.2f L<br>&nbsp;&nbsp;&nbsp;&nbsp;Beer volume: %.2f L<br>&nbsp;&nbsp;&nbsp;&nbsp;Dumped volume: %.2f L<br>&nbsp;&nbsp;&nbsp;&nbsp;Vent pressure factor (%%): %.3f<br>",
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
      // The vent factor is a parameter: the entry band is always defined.
      const PressureStabilityBands bands = pressureStabilityBands(SetPointData.setPointPressure);
      snprintf(tmp, sizeof(tmp),
               "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;bands: entry %.3f-%.3f bar, exit %.3f-%.3f bar<br>",
               bands.entryLow, bands.entryHigh, bands.exitLow, bands.exitHigh);
      strnncat(st, tmp, PRESSURE_STATUS_SIZE);
    }
  }
  snprintf(tmp, sizeof(tmp), "Pressure sensor check: %s; valid current %.2f-%.2f mA; alarm %s<br>",
           pressureSensorProblemText[0] ? pressureSensorProblemText : "OK",
           pressureSensorCurrentLow(), pressureSensorCurrentHigh(),
           pressureSensorAlarm ? "ACTIVE" : "off");
  strnncat(st, tmp, PRESSURE_STATUS_SIZE);
  snprintf(tmp, sizeof(tmp), "Vent check: %s, drop %.4f/%.4f bar (%.2fx), run %u, alarm %s; stuck open %s (fall %.4f bar)<br>",
           reliefCheckLast == RELIEF_CHECK_SKIPPED ? "none" : reliefCheckLabel(reliefCheckLast),
           reliefCheckMeasuredDrop, reliefCheckExpectedDrop, reliefCheckDropRatio, reliefCheckRun,
           reliefCheckAlarmActive() ? "ON" : "off", valveStuckOpenAlarmActive() ? "ALARM" : "no", stuckOpenLastFall);
  strnncat(st, tmp, PRESSURE_STATUS_SIZE);

  // --- Diagnóstico de relés ---
  unsigned long now = millis();
  strnncat(st, "<br>--- Relay cycle ---<br>", PRESSURE_STATUS_SIZE);
  if (!inTheMiddleOfRelief()) {
    strnncat(st, "Cycle: IDLE<br>", PRESSURE_STATUS_SIZE);
  } else {
    strnncat(st, "Cycle: ACTIVE<br>", PRESSURE_STATUS_SIZE);
    if (timeToStartExpansion) {
      snprintf(tmp, sizeof(tmp), "Stage: waiting to open the valve (in %ld ms)<br>",
               (long)(timeToStartExpansion - now));
    } else if (timeToFinishExpansion) {
      snprintf(tmp, sizeof(tmp), "Stage: valve OPEN - closes in %ld ms<br>",
               (long)(timeToFinishExpansion - now));
    } else if (timeToRegisterPressure) {
      snprintf(tmp, sizeof(tmp), "Stage: valve CLOSED - measuring pressure in %ld ms<br>",
               (long)(timeToRegisterPressure - now));
    }
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


