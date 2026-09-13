#include "PressureControl.h"
#include "TemperatureControl.h"
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "GambainoCommon.h"
#include "PovotoTasks.h"
#include "datalog.h"
#include <Adafruit_INA219.h>
#include <Arduino.h>
#include <IOTK.h>
#include <IOTK_NTP.h>
#include "__NumFilters.h"
#include <math.h>


#define DEBUGACCELERATION (debugging ? 20L : 1L)
#define TRANSFERTIME (8000 / DEBUGACCELERATION) 
#define RELIEFTIME (8000 / DEBUGACCELERATION)

#define PRESSURE_MEDIAN_WINDOW 9
#define CURRENT_MEDIAN_MIN_SAMPLE_MS 20 // defined for 32 samples in INA 219 (17ms)
#define INSTABILITYTHRESHOLDMA 4.0f

#define INA219_SHUNT_OHMS 10.0f

#define SOLENOID_NOISE_MS 400

#define TRANSFER_CLOSE_PRESSURE_BLOCK_MS (2*CURRENT_MEDIAN_MIN_SAMPLE_MS*PRESSURE_MEDIAN_WINDOW) // gives time to fill and renovate the whole vector, avoiding transient pneumatics effects

#define PRESSURE_SAMPLE_MIN_MS 250
#define PRESSURE_SAMPLES_MAX 3000
#define PRESSURE_RELIEF_HISTORY_MAX 500
#define VOLUME_DETERMINATION_RECORD_START_CYCLE 6
#define VOLUME_DETERMINATION_RECORD_END_CYCLE 35
#define VOLUME_DETERMINATION_CYCLE_INTERVAL_MS (5L*60000UL / DEBUGACCELERATION)
#define RELIEFS_WINDOW_SIZE 2
#define RELIEF_OVERDUE_FACTOR 1.20f
#define RELIEF_PER_HOUR_MIN_DISPLAY 0.20f

#define CONST_R 0.0831446 // constante dos gases em bar*L/(mol*K) 
#define CO2MOLAR_MASS 44.01


Adafruit_INA219 ina219;
bool pressureSensorConnected = false;
float currentReading = 0.0; // Corrente em mA


averageFloatVector lnPressureDropAvg(15);
float beerVolume = 0.0f;
float beerSG = 0.0;
float beerABV = 0.0;
float headSpaceCO2Mols = 0.0f;
float beerCO2EvolutionGramsPerLiterPerDay = 0.0f;
float sgPointGenerationTime = 0.0f;

static constexpr unsigned long CO2_EVOLUTION_SAMPLE_MS = 60000UL;
static constexpr uint16_t CO2_EVOLUTION_HISTORY_SIZE = 71;
static constexpr unsigned long CO2_IMMEDIATE_PERCEPTION_DELAY_MS = 10UL * MINUTESms;
enum CO2DissolvedEstimationMode : uint8_t {
  CO2_DISSOLVED_HALF_LIFE,
  CO2_DISSOLVED_IMMEDIATE
};
static CO2DissolvedEstimationMode co2DissolvedEstimationMode = CO2_DISSOLVED_HALF_LIFE;
static unsigned long co2ActiveFermentationCriteriaSinceMillis = 0;
static unsigned long co2FermentationConfirmationMs = CO2_IMMEDIATE_PERCEPTION_DELAY_MS;
struct CO2EvolutionSample {
  unsigned long millisStamp;
  double totalMols;
  float pressure;
};
static CO2EvolutionSample co2EvolutionHistory[CO2_EVOLUTION_HISTORY_SIZE];
static uint16_t co2EvolutionStart = 0;
static uint16_t co2EvolutionCount = 0;

static unsigned long int timeToStartExpansion     = 0;
static unsigned long int timeToFinishExpansion    = 0;
static unsigned long int timeToRegisterPressure = 0; // after a relief event
static unsigned long int noPressureReadUntil = 0;
static unsigned long reliefValveOpenedMillis = 0;
static float currentOnReliefMeasured = 0.0f;

float  adjustedPressureAfterRelief;

float pressureOnReliefMeas = 0.0f;
float pressureOnReliefExtrap = 0;
float pressureAfterRelief = 0;
unsigned long pressureAfterReliefMillis = 0;
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
  uint16_t nReliefs;
  float factorMedio;
  float fermenterVolume;
  bool volumeMetricsValid;
};

static PressureReliefRecord *pressureReliefHistory = nullptr;
static uint16_t pressureReliefIndex = 0;
static uint16_t pressureReliefCount = 0;
static int16_t pendingReliefIndex = -1;
static unsigned long lastSolenoidToggleMillis = 0;
float pressureDropFactor = 0.99f;

static bool volumeDeterminationActive = false;
static bool speedCalibrationActive = false;
static bool speedCalibrationVenting = false;
static const uint8_t speedDurations[] = {1, 2, 4, 6, 8, 10, 12, 14, 16};
struct SpeedRecord { float p1, p2, pl, r; };
static SpeedRecord speedRecords[2][27];
static uint8_t speedRecordCount[2] = {0, 0};
static uint8_t speedStage = 0; // 0: open, 1: close, 2: settle/read
static unsigned long speedStageMillis = 0;
static float speedVolumeFactor = 0.0f;
static unsigned long speedSettlingIntervalMs() {
  return debugging ? 10000UL : 180000UL;
}
static const char *speedStatus = "Idle";
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
static float volumeCalculatedSoFar = 0.0f;
static bool volumeCalculatedSoFarValid = false;

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

float volumeEstimationFromPressureDrop(float dropFactor) {
  if (dropFactor <= 0.0f || dropFactor >= 1.0f) {
    return NAN;
  }
  return (FMTData.FMTReliefVolume * dropFactor) / (1.0f -dropFactor);
}

static void updateBeerVolumeFromHeadspace() {
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

enum class FermentationCriteria : uint8_t { Inactive, Active, Imprecise };
static FermentationCriteria fermentationCriteria = FermentationCriteria::Inactive;
static DissolvedCO2LogData dissolvedCO2LogData = {};

static const char *fermentationCriteriaLabel(FermentationCriteria state) {
  switch (state) {
    case FermentationCriteria::Active: return "active";
    case FermentationCriteria::Imprecise: return "imprecise";
    default: return "inactive";
  }
}

static FermentationCriteria hasActiveFermentationCriteria() {
  const unsigned long now = millis();
  static constexpr unsigned long observationWindowMs = 10UL * MINUTESms;
  static constexpr unsigned long maximumReliefIntervalMs = 2UL * MINUTESms;
  static constexpr uint16_t observationSamples = observationWindowMs / CO2_EVOLUTION_SAMPLE_MS;
  static constexpr float CO2PressurePerceptionThresholdForWindow = 0.05f; // Example threshold value

  static bool timingCriteria = false;
  static bool timingReliefCriteria = false;
  static unsigned long reliefCriteriaSinceMillis = 0;
  static unsigned long pressureCriteriaSinceMillis = 0;

  bool criteriaWithoutReliefs = false;
  bool criteriaWithReliefs = false;
  FermentationCriteria result = FermentationCriteria::Inactive;
  co2FermentationConfirmationMs = CO2_IMMEDIATE_PERCEPTION_DELAY_MS;

  const float pressure = ControlData.pressure;
  dissolvedCO2LogData.withReliefsState = "not evaluated";
  dissolvedCO2LogData.withoutReliefsState = "not evaluated";
  dissolvedCO2LogData.withReliefsElapsedMillis = 0;
  dissolvedCO2LogData.withoutReliefsElapsedMillis = 0;
  dissolvedCO2LogData.previousPressure = NAN;
  if (SetPointData.mode != MODE_FERMENTING) {
    timingCriteria = false;
    timingReliefCriteria = false;
    co2ActiveFermentationCriteriaSinceMillis = 0;
  }
  else if (taskWindowType != 0 || now - lastTaskMillis < observationWindowMs ||
             !isfinite(pressure) || !isfinite(SetPointData.setPointPressure)) {
    timingCriteria = false;
    timingReliefCriteria = false;
    co2ActiveFermentationCriteriaSinceMillis = 0;
    criteriaWithoutReliefs = false;
  } else {
    const bool reducingPressure = SetPointData.setPointSlowPressure != NOTaTEMP &&
        SetPointData.setPointSlowPressure < SetPointData.setPointPressure;

    // "With reliefs" determination
    if (!reducingPressure && reliefMillisCount >= 2 &&
        isfinite(pressureAfterRelief) && pressureAfterRelief < SetPointData.setPointPressure) {
      const uint8_t lastIndex = (reliefMillisIndex + RELIEFS_WINDOW_SIZE - 1) % RELIEFS_WINDOW_SIZE;
      const uint8_t previousIndex = (lastIndex + RELIEFS_WINDOW_SIZE - 1) % RELIEFS_WINDOW_SIZE;
      const unsigned long lastRelief = reliefMillisWindow[lastIndex];
      const unsigned long interval = lastRelief - reliefMillisWindow[previousIndex];
      // A pair of old, close reliefs must not keep the criterion true indefinitely.
      criteriaWithReliefs = interval > 0 && interval < maximumReliefIntervalMs && now - lastRelief < maximumReliefIntervalMs;

    }

    FermentationCriteria withReliefsResult = FermentationCriteria::Inactive;
    if (criteriaWithReliefs) {
      if (!timingReliefCriteria) {
        reliefCriteriaSinceMillis = now;
        timingReliefCriteria = true;
      }
      withReliefsResult = now - reliefCriteriaSinceMillis >= observationWindowMs
          ? FermentationCriteria::Active : FermentationCriteria::Imprecise;
    } else if (timingReliefCriteria) {
      timingReliefCriteria = false;
    }

    // "Without reliefs" determination
    // Samples are approximately one minute apart.
    const float previousPressure = co2EvolutionCount > observationSamples
        ? co2EvolutionHistory[(co2EvolutionStart + co2EvolutionCount - 1 - observationSamples) % CO2_EVOLUTION_HISTORY_SIZE].pressure
        : NAN;
    criteriaWithoutReliefs = pressure < SetPointData.setPointPressure && isfinite(previousPressure) && pressure > previousPressure+CO2PressurePerceptionThresholdForWindow;
    dissolvedCO2LogData.previousPressure = previousPressure;

    FermentationCriteria withoutReliefsResult = FermentationCriteria::Inactive;
    if (criteriaWithoutReliefs) {
      if (!timingCriteria) {
        pressureCriteriaSinceMillis = now;
        timingCriteria = true;
      }
      withoutReliefsResult = now - pressureCriteriaSinceMillis >= CO2_IMMEDIATE_PERCEPTION_DELAY_MS
          ? FermentationCriteria::Active : FermentationCriteria::Imprecise;
    } else {
      timingCriteria = false;
      if (pressure < SetPointData.setPointPressure && !isfinite(previousPressure)) {
        withoutReliefsResult = FermentationCriteria::Imprecise;
      }
    }

    if (withReliefsResult == FermentationCriteria::Active ||
        withoutReliefsResult == FermentationCriteria::Active) {
      result = FermentationCriteria::Active;
    } else if (withReliefsResult == FermentationCriteria::Imprecise &&
               withoutReliefsResult == FermentationCriteria::Imprecise) {
      result = FermentationCriteria::Imprecise;
    }

    dissolvedCO2LogData.withReliefsState = fermentationCriteriaLabel(withReliefsResult);
    dissolvedCO2LogData.withoutReliefsState = fermentationCriteriaLabel(withoutReliefsResult);
    dissolvedCO2LogData.withReliefsElapsedMillis = timingReliefCriteria ? now - reliefCriteriaSinceMillis : 0;
    dissolvedCO2LogData.withoutReliefsElapsedMillis = timingCriteria ? now - pressureCriteriaSinceMillis : 0;

    // Report the timer of a criterion supporting the selected state.
    if (timingReliefCriteria && withReliefsResult == result) {
      co2ActiveFermentationCriteriaSinceMillis = reliefCriteriaSinceMillis;
      co2FermentationConfirmationMs = observationWindowMs;
    } else if (timingCriteria && withoutReliefsResult == result) {
      co2ActiveFermentationCriteriaSinceMillis = pressureCriteriaSinceMillis;
    } else {
      co2ActiveFermentationCriteriaSinceMillis = 0;
    }
  }
  return result;
}

static float expansionPressureThreshold() {
  if (SetPointData.setPointPressure <= 0.0f ||
      !isfinite(pressureDropFactor) || pressureDropFactor <= 0.0f) {
    return ControlData.pressure;
  }
  return SetPointData.setPointPressure / sqrtf(pressureDropFactor);
}

static void updateCO2DissolvedEstimationMode() {
  fermentationCriteria = hasActiveFermentationCriteria();
  if (fermentationCriteria == FermentationCriteria::Inactive) {
    co2DissolvedEstimationMode = CO2_DISSOLVED_HALF_LIFE;
  } else if (fermentationCriteria == FermentationCriteria::Active) {
    co2DissolvedEstimationMode = CO2_DISSOLVED_IMMEDIATE;
  }
}

static unsigned long co2DissolvedCriteriaElapsedMillis(unsigned long now) {
  if (co2ActiveFermentationCriteriaSinceMillis == 0) {
    return 0;
  }
  return now - co2ActiveFermentationCriteriaSinceMillis;
}

static float dissolvedCO2CalculationPressure() {
  return co2DissolvedEstimationMode == CO2_DISSOLVED_IMMEDIATE
      ? expansionPressureThreshold()
      : ControlData.pressure;
}

static const char *co2DissolvedEstimationModeLabel() {
  return co2DissolvedEstimationMode == CO2_DISSOLVED_IMMEDIATE
      ? "immediate"
      : "half-life";
}

DissolvedCO2LogData getDissolvedCO2LogData() {
  DissolvedCO2LogData data = dissolvedCO2LogData;
  const unsigned long now = millis();
  data.mode = co2DissolvedEstimationModeLabel();
  data.criteriaState = fermentationCriteriaLabel(fermentationCriteria);
  if (!data.withReliefsState) data.withReliefsState = "not evaluated";
  if (!data.withoutReliefsState) data.withoutReliefsState = "not evaluated";
  data.criteriaElapsedMillis = co2DissolvedCriteriaElapsedMillis(now);
  data.confirmationMillis = co2FermentationConfirmationMs;
  data.calculationPressure = dissolvedCO2CalculationPressure();
  data.equilibriumMols = CO2DissolvedMols(data.calculationPressure, beerSG, ControlData.temperature, beerVolume);
  data.reliefIntervalSeconds = NAN;
  data.sinceLastReliefSeconds = NAN;
  if (reliefMillisCount > 0) {
    const uint8_t last = (reliefMillisIndex + RELIEFS_WINDOW_SIZE - 1) % RELIEFS_WINDOW_SIZE;
    data.sinceLastReliefSeconds = (now - reliefMillisWindow[last]) / 1000.0f;
    if (reliefMillisCount >= 2) {
      const uint8_t previous = (last + RELIEFS_WINDOW_SIZE - 1) % RELIEFS_WINDOW_SIZE;
      data.reliefIntervalSeconds = (reliefMillisWindow[last] - reliefMillisWindow[previous]) / 1000.0f;
    }
  }
  return data;
}

static void recomputeDissolvedCO2MolsFromCurrentState() {
    static unsigned long lastUpdateMillis = 0;

    const unsigned long now = millis();

    // Preserva o CO₂ já inicializado ou restaurado dos contadores.
    if (!lastUpdateMillis) {
        lastUpdateMillis = now;
        return;
    }

    if (CountersData.totalReliefCount == 0 &&
        ControlData.pressure <= BatchData.startPressure) {
      CountersData.CO2InSolution = 0.0;
      lastUpdateMillis = now;
      return;
    }

    const float calculationPressure = dissolvedCO2CalculationPressure();
    const double equilibriumMols = CO2DissolvedMols(
        calculationPressure, beerSG, ControlData.temperature, beerVolume);

    if (co2DissolvedEstimationMode == CO2_DISSOLVED_IMMEDIATE) {
      CountersData.CO2InSolution = equilibriumMols;
      lastUpdateMillis = now;
      return;
    }

    if (!MILLISDIFF(lastUpdateMillis, 30000UL)) 
      return;

    double dtSeconds = (millis() - lastUpdateMillis) / 1000.0;

    if (dtSeconds<0.0 || dtSeconds > 60.0) // Ignore if the time difference is negative or greater than 1 minute
      dtSeconds = 30.0;

    lastUpdateMillis = now;

    const double t50Seconds = double(FMTData.co2TransferTime) * 3600.0;

    if (t50Seconds > 0.0) {
        const double alpha =
            -expm1(-0.6931471805599453 * dtSeconds / t50Seconds);

        CountersData.CO2InSolution +=
            alpha * (equilibriumMols - CountersData.CO2InSolution);
    } else {
        CountersData.CO2InSolution = equilibriumMols;
    }
}

static void recomputeHeadspaceCO2MolsFromCurrentState() {
  if (CountersData.totalReliefCount > 1) {
    headSpaceCO2Mols = ControlData.pressure    * CountersData.headSpaceVolume / (CONST_R * kelvin(ControlData.temperature))
                     - BatchData.startPressure * CountersData.headSpaceVolume / (CONST_R * kelvin(BatchData.startTemperature));
    if (headSpaceCO2Mols < 0.0f) {
      headSpaceCO2Mols = 0.0f;
    }
  }
  else {
    headSpaceCO2Mols = 0.0f;
  }
}

static void resetBeerCO2Evolution() {
  co2EvolutionStart = 0;
  co2EvolutionCount = 0;
  beerCO2EvolutionGramsPerLiterPerDay = 0.0f;
}

static void recomputeBeerCO2EvolutionFromCurrentState() {
  const unsigned long now = millis();
  if (now < 120000UL) {
    return;
  }

  const double totalMols = CountersData.totalMolsEjected
      + CountersData.CO2InSolution + double(headSpaceCO2Mols);
  if (!isfinite(beerVolume) || beerVolume <= 0.0f || !isfinite(totalMols)) {
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

  const uint16_t index = (co2EvolutionStart + co2EvolutionCount) % CO2_EVOLUTION_HISTORY_SIZE;
  co2EvolutionHistory[index] = {now, totalMols, ControlData.pressure};
  ++co2EvolutionCount;

  if (co2EvolutionCount < 5) {
    beerCO2EvolutionGramsPerLiterPerDay = 0.0f;
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
    firstMols += first.totalMols;
    lastMols += last.totalMols;
    // Relative unsigned timestamps preserve elapsed time across millis() rollover.
    firstTime += first.millisStamp - firstMillis;
    lastTime += last.millisStamp - firstMillis;
  }
  const double deltaMols = (lastMols - firstMols) / avgWindow;
  const double elapsedMs = (lastTime - firstTime) / avgWindow;
  if (elapsedMs <= 0.0) {
    beerCO2EvolutionGramsPerLiterPerDay = 0.0f;
    return;
  }
  // Preserve the signed net change; clamp only when presenting the result.
  beerCO2EvolutionGramsPerLiterPerDay = deltaMols
      * CO2MOLAR_MASS * 86400000.0
      / (double(beerVolume) * elapsedMs);
}

float getBeerCO2EvolutionGramsPerLiterPerDay() {
  return fmaxf(0.0f, beerCO2EvolutionGramsPerLiterPerDay);
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

float CO2Mass(float mols) {
  if (mols == -1) 
    return (CountersData.totalMolsEjected + CountersData.CO2InSolution + headSpaceCO2Mols) * CO2MOLAR_MASS;
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
  if (CountersData.headSpaceVolume > 0.0f) {
    updateBeerVolumeFromHeadspace();
    recomputeHeadspaceCO2MolsFromCurrentState();
  }
  else {
    CountersData.headSpaceVolume = 0.0f;
    beerVolume = 0.0f;
    headSpaceCO2Mols = 0.0f;
  }
}

void requestDerivedStateRestoreFromCounters() {
  resetBeerCO2Evolution();
  derivedStateRestorePending = true;
}

void applyDumpWindowHeadspaceRecalc(float headspaceBeforeL, float pressureBeforeBar, float pressureAfterBar) {
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

  if (headAfter > FMTData.FMTVolume) {
    headAfter = FMTData.FMTVolume;
  }

  if (headAfter > headspaceBeforeL) {
    CountersData.headSpaceVolume = headAfter;
    updateBeerVolumeFromHeadspace();
    lnPressureDropAvg.clear();
    // Keep the internal factor coherent with the recalculated headspace.
    if ((CountersData.headSpaceVolume + FMTData.FMTReliefVolume) > 0.0f) {
      pressureDropFactor = CountersData.headSpaceVolume / (CountersData.headSpaceVolume + FMTData.FMTReliefVolume); // Lucio: rever
      pressureDropFactor = fmaxf(0.001f, fminf(pressureDropFactor, 0.999f));
      lnPressureDropAvg.add(logf(pressureDropFactor));
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
}

static void markSolenoidToggle() {
  lastSolenoidToggleMillis = millis();
}

static float readCurrentFromINA219mA() {
  #ifdef INA219_SHUNT_OHMS
    return ina219.getShuntVoltage_mV() / INA219_SHUNT_OHMS;
  #else
    return ina219.getCurrent_mA();
  #endif
}

// Current is obtained from the shunt ADC. Average 32 conversions there
// (about 17 ms) so each software sample represents a complete shunt cycle.
static void configureINA219CurrentAveraging() {
  const uint16_t config = INA219_CONFIG_BVOLTAGERANGE_32V |
                          INA219_CONFIG_GAIN_8_320MV |
                          INA219_CONFIG_BADCRES_12BIT |
                          INA219_CONFIG_SADCRES_12BIT_32S_17MS |
                          INA219_CONFIG_MODE_SVOLT_CONTINUOUS;

  Wire.beginTransmission(INA219_ADDRESS);
  Wire.write(INA219_REG_CONFIG);
  Wire.write(uint8_t(config >> 8));
  Wire.write(uint8_t(config));
  if (Wire.endTransmission() != 0) {
    Serial.println("INA219: unable to configure current averaging");
  }
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
  // Try to initialize the INA219 if it hasn't been done yet
  static bool initialized = false;
  if (!initialized) {
    pressureSensorConnected = ina219.begin();
    if (pressureSensorConnected) {
      Serial.println("INA219 pressure sensor initialized successfully");
    } else {
      Serial.println("Could not find INA219 pressure sensor");
    }
    ina219.setCalibration_32V_2A();
    configureINA219CurrentAveraging();
    initialized = true;
  }
  
  if (!inPressureNoiseWindow() && MILLISDIFF(noPressureReadUntil, 0)) {
    if (pressureSensorConnected) {
      // Read and filter the INA current even in debugging mode. Debug pressure
      // simulation is only used when this reading is zero or the INA is absent.
      if (currentWindowCount == 0 || MILLISDIFF(lastCurrentMedianSampleMillis, CURRENT_MEDIAN_MIN_SAMPLE_MS)) {
        currentReading = medianFilter(readCurrentFromINA219mA());
        lastCurrentMedianSampleMillis = millis();
      }

      if (!(debugging && currentReading == 0.0f)) {
        ControlData.pressure = convertCurrentToPressure(currentReading);
        return;
      }
    }

    if (debugging && (!pressureSensorConnected || currentReading == 0.0f)) {
      static unsigned long lastPressureIncrease = 0;
      if (sgPointGenerationTime != 0 && !inTheMiddleOfRelief() && beerSG > 1.010f) {
        if (MILLISDIFF(lastPressureIncrease,1000*sgPointGenerationTime))  {
          lastPressureIncrease = millis();
          ControlData.pressure += 0.1f;
        } 
      }
    }
    else if (!pressureSensorConnected) {
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

  // Formula solicitada pelo usuario.
  volumeSummaryFermenterVolume = volumeEstimationFromPressureDrop(volumeSummaryFactor);
  volumeSummaryAvailable = true;
  volumeCalculatedSoFar = volumeSummaryFermenterVolume;
  volumeCalculatedSoFarValid = true;
  volumeDeterminationActive = false;

  char line1[40];
  char line2[40];
  char line3[40];
  snprintf(line1, sizeof(line1), "Volume: END (%u reliefs)", (unsigned)volumeSummaryNReliefs);
  snprintf(line2, sizeof(line2), "f=%.4f Pi=%.3f Pf=%.3f", volumeSummaryFactor, volumeSummaryPi, volumeSummaryPfAdjusted);
  snprintf(line3, sizeof(line3), "Vf=%.3fL", volumeSummaryFermenterVolume);
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

  const float initialBeerMassG =
    1000.0f *
    beerVolume * 
    initialDensityKgL;

  const float initialExtractMassG =
    initialBeerMassG * OE / 100.0f;

  const double producedCO2Mols =
    CountersData.totalMolsEjected
    + CountersData.CO2InSolution
    + headSpaceCO2Mols;

  const float producedCO2MassG =
    44.0095f * producedCO2Mols;

  const float fermentedExtractMassG =
    producedCO2MassG * 2.0665f / 0.9565f;

  const float producedYeastMassG =
    producedCO2MassG * 0.11f / 0.9565f;

  const float producedEthanolMassG =
    producedCO2MassG / 0.9565f;

  const float remainingExtractMassG =
    initialExtractMassG -
    fermentedExtractMassG;

  // Cerveja clarificada e degaseificada
  const float currentBeerMassG =
    initialBeerMassG -
    producedCO2MassG -
    producedYeastMassG;

  const float beerRealPlato =
    100.0f *
    remainingExtractMassG /
    currentBeerMassG;

  const float beerApparentPlato =
    (beerRealPlato - 0.1808f * OE) /
    0.8192f;

  beerSG =
    ApparentPlatoToSG(beerApparentPlato);

  const float beerABW =
    100.0f *
    producedEthanolMassG /
    currentBeerMassG;

  const float beerDensityKgL =
    beerSG * 0.9982f;

  beerABV =
    beerABW *
    beerDensityKgL /
    0.78924f;
}

void processPressure(bool afterRelief) {
  const float reliefPressureReachedTarget = pressureReachedTarget;
  const unsigned long reliefPressureReachedTargetMillis = pressureReachedTargetMillis;
  float instantPressureDropFactor = NAN;
  float ejectedMols = 0.0f;

  if (afterRelief && debugging) {
    if (volumeDeterminationActive && pressureSamples) 
      ControlData.pressure = ControlData.pressure * 0.984f + random(-5,5) * 0.0005; 
    else
      ControlData.pressure = ControlData.pressure * 0.942f + random(-5,5) * 0.0005;       
  }      

  updateReliefsPerHour(afterRelief);
  
  if  (afterRelief) {
    pressureAfterRelief = ControlData.pressure;
    pressureAfterReliefMillis = millis();
    // These belong to the relief being completed. Preserve them for its log
    // before clearing the live values used to detect the next relief cycle.
    pressureReachedTarget = 0;
    pressureReachedTargetMillis = 0;
    adjustedPressureAfterRelief = (pressureOnReliefMeas+Patm) * powf(((pressureAfterRelief+Patm) / (pressureOnReliefMeas+Patm)), (1.0f / FMTData.FMTEffectiveVentingExponent)) - Patm;
       // Todo: tentar fazer esse expoente ser determinado dinamicamente ou entao apurar o fator de queda de pressao ao na ejeçao (talvez so sirva para fermentacao  estavel e intensa)

    instantPressureDropFactor = 1.0f;
    if (pressureOnReliefExtrap > 0.01f) {
      instantPressureDropFactor = (adjustedPressureAfterRelief / pressureOnReliefExtrap); 
    }

    // Keep factor in a valid range for log() and downstream equations.
    instantPressureDropFactor = fmaxf(0.001f, fminf(instantPressureDropFactor, 0.999f));
    lnPressureDropAvg.add(logf(instantPressureDropFactor));
    pressureDropFactor = expf(lnPressureDropAvg.value());
    pressureDropFactor = fmaxf(0.001f, fminf(pressureDropFactor, 0.999f));

    // The calculated headspace is persisted immediately because all CO2
    // calculations below use this same value to derive beer volume and moles.
    CountersData.headSpaceVolume = volumeEstimationFromPressureDrop(pressureDropFactor);
    updateBeerVolumeFromHeadspace();
    
    if (pendingReliefIndex >= 0) {
      PressureReliefRecord &record = pressureReliefHistory[pendingReliefIndex];
      record.pressureAfter = ControlData.pressure;
      record.currentAfter = currentReading;
    }
    pendingReliefIndex = -1;

    //float ejectedMols = adjustedPressureAfterRelief * FMTData.FMTReliefVolume / (CONST_R * kelvin(ControlData.temperature));
    ejectedMols = (pressureOnReliefExtrap - adjustedPressureAfterRelief) * CountersData.headSpaceVolume / (CONST_R * kelvin(ControlData.temperature)); // removes the apparent pressure drop caused by polytropic cooling.
    CountersData.totalMolsEjected += ejectedMols;
    
    CountersData.totalReliefCount += 1;    

  }

  recomputeDissolvedCO2MolsFromCurrentState();
  recomputeHeadspaceCO2MolsFromCurrentState();
  recomputeBeerCO2EvolutionFromCurrentState();
  
  //float lastTotalCO2MolsProceduced = CountersData.CO2InSolution + headSpaceCO2Mols + CountersData.totalMolsEjected; está sendo usado ou não?

//  float massCO2Produced = CO2Mass(CountersData.totalCO2MolsProduced - lastTotalCO2MolsProceduced);

//  Serial.println(EstimateSGFromProducedCO2Mol(beerSG, beerVolume, CountersData.totalCO2MolsProduced - lastTotalCO2MolsProceduced) *1000.0);
/*
  float Pi = SGToPlato(BatchData.batchOG) + BatchData.addedPlato;
  float SGu = 1 + (Pi / (258.6-(Pi/258.2)*227.1));
  float totalCO2Mols = CountersData.totalMolsEjected + CountersData.CO2InSolution + headSpaceCO2Mols;
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
    volumeLastReliefMillis = millis();

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

            record.fermenterVolume = volumeEstimationFromPressureDrop(record.factorMedio);
            record.volumeMetricsValid = true;
            volumeCalculatedSoFar = record.fermenterVolume;
            volumeCalculatedSoFarValid = true;
        }
      }

      char line1[32];
      char line2[32];
      char line3[32];
      snprintf(line1, sizeof(line1), "Iteracao: %u", volumeIteration);
      snprintf(line2, sizeof(line2), "Pf adj: %.3f", record.pfAdjusted);
      snprintf(line3, sizeof(line3), "Vf: %.3f", record.fermenterVolume);
      showVolumeStatus(line1, line2, line3);
      volumeAwaitingRecord = false;
      volumeRecordIndex = -1;
    }
  }

  if (afterRelief) {
    const double totalCO2Mols = CountersData.totalMolsEjected +
                                CountersData.CO2InSolution + headSpaceCO2Mols;
    ReliefLogData reliefLog = {};
    reliefLog.povotoNumber = (int)FMTData.PovotoNum;
    reliefLog.valveOpenedMillis = reliefValveOpenedMillis;
    reliefLog.pressureReachedTargetMillis = reliefPressureReachedTargetMillis;
    reliefLog.pressureAfterReliefMillis = pressureAfterReliefMillis;
    reliefLog.volumeDeterminationActive = volumeDeterminationActive;
    reliefLog.temperature = ControlData.temperature;
    reliefLog.targetPressure = SetPointData.setPointPressure;
    reliefLog.atmosphericPressure = Patm;
    reliefLog.reliefVolume = FMTData.FMTReliefVolume;
    reliefLog.effectiveVentingExponent = FMTData.FMTEffectiveVentingExponent;
    reliefLog.pressureOnReliefMeasured = pressureOnReliefMeas;
    reliefLog.currentOnReliefMeasured = currentOnReliefMeasured;
    reliefLog.pressureReachedTarget = reliefPressureReachedTarget;
    reliefLog.pressureOnReliefExtrapolated = pressureOnReliefExtrap;
    reliefLog.pressureAfterRelief = pressureAfterRelief;
    reliefLog.currentAfterRelief = currentReading;
    reliefLog.adjustedPressureAfterRelief = adjustedPressureAfterRelief;
    reliefLog.instantaneousPressureDropFactor = instantPressureDropFactor;
    reliefLog.pressureDropFactor = pressureDropFactor;
    reliefLog.headSpaceVolume = CountersData.headSpaceVolume;
    reliefLog.beerVolume = beerVolume;
    reliefLog.ejectedMols = ejectedMols;
    reliefLog.totalMolsEjected = CountersData.totalMolsEjected;
    reliefLog.headSpaceCO2Mols = headSpaceCO2Mols;
    reliefLog.dissolvedCO2Mols = CountersData.CO2InSolution;
    reliefLog.totalCO2Mols = totalCO2Mols;
    reliefLog.beerSG = beerSG;
    reliefLog.beerRealPlato = SGToRealPlato(beerSG);
    reliefLog.beerABV = beerABV;
    reliefLog.totalReliefCount = CountersData.totalReliefCount;
    reliefLog.reliefsPerHour = reliefsPerHourValue;
    reliefLog.beerCO2EvolutionGramsPerLiterPerDay = beerCO2EvolutionGramsPerLiterPerDay;
    doReliefDataLog(reliefLog);
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
        pressureOnReliefMeas = ControlData.pressure;
        currentOnReliefMeasured = currentReading;
        ReliefStartPressureTime = millis();
        reliefValveOpenedMillis = ReliefStartPressureTime;
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
        if (!isfinite(extrapolation) || extrapolation < 0 || extrapolation > 0.02) {
          extrapolation = 0.0f;
        }
        pressureOnReliefExtrap = pressureOnReliefMeas + extrapolation;

        digitalWrite(PINTRANSFERVALVE, LOW);
        digitalWrite(PINVENTINGLED, HIGH);
        markSolenoidToggle();
        ControlData.transferValve = false;
        resetCurrentMedianFilter();
        noPressureReadUntil = millis() + TRANSFER_CLOSE_PRESSURE_BLOCK_MS;
        timeToRegisterPressure = noPressureReadUntil; // registra após o período de bloqueio pós-fechamento da transfer
        timeToFinishExpansion = 0;
      }
    }
    /*else {
      digitalWrite(PINTRANSFERVALVE, LOW);
      digitalWrite(PINVENTINGLED, LOW);
      ControlData.transferValve = false;  
        ;Serial.println("Cor: apagada2");

    }*/

    if (timeToRegisterPressure) {
      if (MILLISDIFF(timeToRegisterPressure, 0)) {
        //;Serial.printf("[PRESSURE] %lu / %lu: Registrando pressão. Pressure=%.2f bar\n", millis(), timeToRegisterPressure, ControlData.pressure);
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


void pressureRelief(bool fromVolumeDetermination) {
  static unsigned long lastReliefEvent = 0;
  if (speedCalibrationActive || inTheMiddleOfRelief()) { // if we're still in the middle of a relief, ignore new relief requests to avoid overlapping and potential hardware issues
    return;
  }
  
  if (fromVolumeDetermination) {
    const uint16_t nextCycle = volumeIteration + 1;
    const bool shouldRecordCycle = (nextCycle >= VOLUME_DETERMINATION_RECORD_START_CYCLE &&
                                    nextCycle <= VOLUME_DETERMINATION_RECORD_END_CYCLE);

    volumeAwaitingRecord = false;
    volumeRecordIndex = -1;
    pendingReliefIndex = -1;

    if (shouldRecordCycle) {
      if (!pressureReliefHistory) {
        return;
      }

      if (volumeStartTemperatureK <= 0.0f || volumeStartPressure <= 0.0f) {
        volumeStartTemperatureK = kelvin(ControlData.temperature);
        volumeStartPressure = ControlData.pressure;
        volumeStartReliefIteration = volumeIteration;
      }

      const uint16_t recordIndex = pressureReliefIndex;
      PressureReliefRecord &record = pressureReliefHistory[recordIndex];
      record.timestamp[0] = '\0';
      NTPFormatedDateTime(record.timestamp);
      record.temperature = ControlData.temperature;
      record.pressureBefore = ControlData.pressure;
      record.pressureAfter = 0.0f;
      record.currentBefore = currentReading;
      record.currentAfter = 0.0f;
      record.tiK = 0.0f;
      record.tfK = 0.0f;
      record.pi = 0.0f;
      record.pfAdjusted = 0.0f;
      record.nReliefs = 0;
      record.factorMedio = 0.0f;
      record.fermenterVolume = 0.0f;
      record.volumeMetricsValid = false;

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

  const unsigned long scaledTransferTime = (unsigned long)TRANSFERTIME / reliefDurationDivisor;
  const unsigned long scaledReliefTime = (unsigned long)RELIEFTIME / reliefDurationDivisor;
  const unsigned long scaledExtraMs = isBrewingTransfer ? 0: extraMs;
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
          const char *header = "data_hora;temperatura;pressao_antes;pressao_depois;corrente_antes_mA;corrente_depois_mA;patm;relief_volume;volume_estimado;Ti_K;Tf_K;Pi;Pf_ajustada;nReliefs;fatorMedio;volume_fermentador\n";
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

          const float denom = record.pressureBefore - record.pressureAfter;
          float volumeEstimated = 0.0f;
          if (fabsf(denom) > 0.0001f) {
            volumeEstimated = (record.pressureAfter * FMTData.FMTReliefVolume) / denom;
          }

          char tempBuf[16];
          char pBeforeBuf[16];
          char pAfterBuf[16];
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
          formatFloatCsv(pBeforeBuf, sizeof(pBeforeBuf), record.pressureBefore, 3);
          formatFloatCsv(pAfterBuf, sizeof(pAfterBuf), record.pressureAfter, 3);
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
            formatFloatCsv(fermenterVolBuf, sizeof(fermenterVolBuf), record.fermenterVolume, 3);
          }

          char line[320];
          int lineLen = snprintf(
              line,
              sizeof(line),
              "%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s;%s\n",
              dateBufSafe,
              tempBuf,
              pBeforeBuf,
              pAfterBuf,
              currentBeforeBuf,
              currentAfterBuf,
              patmBuf,
              reliefVolBuf,
              volumeBuf,
              tiBuf,
              tfBuf,
              piBuf,
              pfAdjBuf,
              nReliefsBuf,
              factorBuf,
              fermenterVolBuf);

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
  else if (!venting && (!isfinite(FMTData.FMTVolume) || FMTData.FMTVolume <= 0.0f ||
                       !isfinite(FMTData.FMTReliefVolume) || FMTData.FMTReliefVolume <= 0.0f))
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
         "/27; venting: " + String(speedRecordCount[1]) + "/27";
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
  char line1[48], line2[64], line3[64];
  snprintf(line1, sizeof(line1), "%s speed: %s",
           speedCalibrationVenting ? "Venting" : "Expansion",
           speedCalibrationActive ? "RUN" : count == 27 ? "END" : "ABORT");
  if (speedCalibrationActive) {
    snprintf(line2, sizeof(line2), "Ciclo %u/3 | %us | %u/27",
             count / 9 + 1, speedDurations[count % 9], count);
    const unsigned long duration = speedStage == 1 ? speedDurations[count % 9] * 1000UL : speedSettlingIntervalMs();
    const unsigned long elapsed = now - speedStageMillis;
    const unsigned long remaining = elapsed >= duration ? 0 : (duration - elapsed + 999UL) / 1000UL;
    snprintf(line3, sizeof(line3), "%s %lus | P: %.3f bar",
             speedStage == 1 ? "Aberta:" : "Espera:", remaining, ControlData.pressure);
  } else {
    snprintf(line2, sizeof(line2), "Medicoes: %u/27", count);
    snprintf(line3, sizeof(line3), "%s", count == 27 ? "CSV na pagina Calibration" : speedStatus);
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
                                speedDurations[count % 9]);

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
    if (record.p1 - record.pl <= 0.0f) {
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
  } else if (speedStage == 1 && now - speedStageMillis >= speedDurations[count % 9] * 1000UL) {
    closeSpeedValve();
    speedStageMillis = now;
    speedStage = 2;
    showSpeedCalibrationProgress(true);
  } else if (speedStage == 2 && now - speedStageMillis >= speedSettlingIntervalMs()) {
    record.p2 = ControlData.pressure;
    record.r = (record.p2 - record.pl) / (record.p1 - record.pl);
    ++count;
    speedStage = 0;
    if (count == 27) {
      speedStatus = "Completed";
      speedCalibrationActive = false;
      showSpeedCalibrationProgress(true);
    }
  }
  if (speedCalibrationActive && speedStage != 0) showSpeedCalibrationProgress();
}

void handleSpeedCalibrationCSV(AsyncWebServerRequest *request) {
  const bool venting = request->hasParam("type") && request->getParam("type")->value() == "venting";
  const uint8_t count = speedRecordCount[venting ? 1 : 0];
  String csv = "ciclo,tempo,P1,P2,PL,R\n";
  csv.reserve(4096);
  for (uint8_t i = 0; i < count; ++i) {
    const SpeedRecord &r = speedRecords[venting ? 1 : 0][i];
    char line[160];
    snprintf(line, sizeof(line), "%u,%u,%.6f,%.6f,%.6f,%.6f\n",
             i / 9 + 1, speedDurations[i % 9], r.p1, r.p2, r.pl, r.r);
    csv += line;
  }
  AsyncWebServerResponse *response = request->beginResponse(200, "text/csv", csv);
  response->addHeader("Content-Disposition", venting ? "attachment; filename=venting_speed.csv" :
                                                      "attachment; filename=expansion_speed.csv");
  request->send(response);
}

bool startVolumeDetermination(char *reason, size_t reasonSize) {
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

void pressureControl() {
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
  updateCO2DissolvedEstimationMode();

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

  if (volumeDeterminationActive) {
    if (volumeIteration >= VOLUME_DETERMINATION_RECORD_END_CYCLE) {
      finalizeVolumeDeterminationSummary();
    } else if (MILLISDIFF(volumeLastReliefMillis, VOLUME_DETERMINATION_CYCLE_INTERVAL_MS)) {
      pressureRelief(true);
    }
  } else if (SetPointData.setPointPressure > 0.0f && 
             ControlData.pressure > (SetPointData.setPointPressure / sqrt(pressureDropFactor)) &&
             (taskWindowType == 0 || MILLISDIFF(taskWindowEndTime, 0))) {
    pressureRelief(false);
  }
  else if (ControlData.pressure > FMTData.maximumPressure) {
    soundAlarm = true;
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
char *getPressureControlStatus(char *st) {

  int16_t rawShuntRegister = 0;
  st[0] = '\0';

  snprintf(tmp, sizeof(tmp), "<br>---------PRESSURE CONTROL:<br>");
    strnncat(st, tmp, 2048);
  if (1 || pressureSensorConnected) {
    const unsigned long now = millis();
    const float co2CalculationPressure = dissolvedCO2CalculationPressure();
    const float equilibriumCO2Mols = CO2DissolvedMols(
      co2CalculationPressure, beerSG, ControlData.temperature, beerVolume);
    const double totalCO2Mols = CountersData.totalMolsEjected
      + CountersData.CO2InSolution + headSpaceCO2Mols;

    snprintf(tmp, sizeof(tmp),
             "Measured pressure: %.3f bar<br>Target pressure: %.3f bar<br>Atmospheric pressure: %.3f bar<br>",
             ControlData.pressure, SetPointData.setPointPressure, Patm);
    strnncat(st, tmp, 2048);
    snprintf(tmp, sizeof(tmp),
             "INA: Filtered current reading: %.2f mA Shunt voltage: %.2f mV Momentary current: %.2f mA<br>",
             currentReading, ina219.getShuntVoltage_mV(), readCurrentFromINA219mA());
    strnncat(st, tmp, 2048);

    const unsigned long criteriaElapsedMs = co2DissolvedCriteriaElapsedMillis(now);
    snprintf(tmp, sizeof(tmp),
             "CO2 dissolved estimation: %s; active-fermentation criteria: %s for %.1f / %.1f s; calculation pressure: %.3f bar<br>",
             co2DissolvedEstimationModeLabel(),
             fermentationCriteria == FermentationCriteria::Active ? "met" :
                 fermentationCriteria == FermentationCriteria::Imprecise ? "imprecise" : "not met",
             criteriaElapsedMs / 1000.0f,
             co2FermentationConfirmationMs / 1000.0f,
             co2CalculationPressure);
    strnncat(st, tmp, 2048);

    strnncat(st, "-------------------------------------------------------------------------------------------<br>", 2048);
    snprintf(tmp, sizeof(tmp),
             "<br>CO2 moles accounting:<br>&nbsp;&nbsp;&nbsp;&nbsp;Headspace: %.3f<br>&nbsp;&nbsp;&nbsp;&nbsp;Dissolved: %.3f (if in equilibrium: %.3f)<br>&nbsp;&nbsp;&nbsp;&nbsp;Ejected: %.3f<br>&nbsp;&nbsp;&nbsp;&nbsp;Total: %.3f (%.2f g)<br>",
             headSpaceCO2Mols, CountersData.CO2InSolution, equilibriumCO2Mols,
             CountersData.totalMolsEjected, totalCO2Mols, CO2Mass());
    strnncat(st, tmp, 2048);

    strnncat(st, "<br>Expansions:<br>", 2048);
    snprintf(tmp, sizeof(tmp), "&nbsp;&nbsp;&nbsp;&nbsp;Relief count: %lu<br>",
             (unsigned long)CountersData.totalReliefCount);
    strnncat(st, tmp, 2048);
    if (!reliefsPerHourAvailable || reliefsPerHourValue < RELIEF_PER_HOUR_MIN_DISPLAY) {
      if (!reliefsPerHourAvailable) {
        snprintf(tmp, sizeof(tmp), "&nbsp;&nbsp;&nbsp;&nbsp;Reliefs/hour: N/A (need %u reliefs, have %u)<br>", (unsigned)RELIEFS_WINDOW_SIZE, reliefMillisCount);
      } else {
        snprintf(tmp, sizeof(tmp), "&nbsp;&nbsp;&nbsp;&nbsp;Reliefs/hour: N/A (< %.2f/h)<br>", RELIEF_PER_HOUR_MIN_DISPLAY);
      }
    } else {
      snprintf(tmp, sizeof(tmp), "&nbsp;&nbsp;&nbsp;&nbsp;Reliefs/hour: %.2f<br>", reliefsPerHourValue);
    }
    strnncat(st, tmp, 2048);
    snprintf(tmp, sizeof(tmp), "&nbsp;&nbsp;&nbsp;&nbsp;gCO2/L/d: %.2f<br>",
             getBeerCO2EvolutionGramsPerLiterPerDay());
    strnncat(st, tmp, 2048);

    snprintf(tmp, sizeof(tmp),
             "<br>Volumes:<br>&nbsp;&nbsp;&nbsp;&nbsp;Headspace volume: %.2f L<br>&nbsp;&nbsp;&nbsp;&nbsp;Beer volume: %.2f L<br>&nbsp;&nbsp;&nbsp;&nbsp;Expansion pressure drop factor (%%): %.3f<br>",
             CountersData.headSpaceVolume, beerVolume, pressureDropFactor * 100);
    strnncat(st, tmp, 2048);

    snprintf(tmp, sizeof(tmp),
             "<br>Gravity:<br>&nbsp;&nbsp;&nbsp;&nbsp;OG: %.4f (extract: %.3fP)<br>&nbsp;&nbsp;&nbsp;&nbsp;SG: %.4f (apparent extract: %.3fP)<br>&nbsp;&nbsp;&nbsp;&nbsp;ABV: %.2f%%<br>",
             BatchData.batchOG, SGToApparentPlato(BatchData.batchOG),
             beerSG, SGToApparentPlato(beerSG), beerABV);
    strnncat(st, tmp, 2048);
  } else {
    snprintf(tmp, sizeof(tmp), "INA219 Pressure Sensor: DISCONNECTED<br>Atmospheric pressure: %.3f bar<br>", Patm);
    strnncat(st, tmp, 2048);
  }

  // --- Diagnóstico de relés ---
  unsigned long now = millis();
  strnncat(st, "<br>--- Relay cycle ---<br>", 2048);
  if (!inTheMiddleOfRelief()) {
    strnncat(st, "Cycle: IDLE<br>", 2048);
  } else {
    strnncat(st, "Cycle: ACTIVE<br>", 2048);
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
    strnncat(st, tmp, 2048);
    snprintf(tmp, sizeof(tmp),
             "timeToStartExpansion=%lu<br>timeToFinishExpansion=%lu<br>timeToRegisterPressure=%lu<br>",
             timeToStartExpansion, timeToFinishExpansion,
             timeToRegisterPressure);
    strnncat(st, tmp, 2048);
  }

  return st;
}


/*Implementar redução por purga
Implementar tasks de adição de volume o de intercenção em gás
implementar conditioning
quando reiniciou perdeu contador de co2 ejetado
ver se coutersdata dá persistência à densidade final e parar de atualizar em conditioning
*/


