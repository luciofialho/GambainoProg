#include "PressureControl.h"
#include "TemperatureControl.h"
#include "PovotoData.h"
#include "PovotoCommon.h"
#include "GambainoCommon.h"
#include "PovotoTasks.h"
#include <Adafruit_INA219.h>
#include <Arduino.h>
#include <IOTK.h>
#include <IOTK_NTP.h>
#include "__NumFilters.h"
#include <math.h>


#define DEBUGACCELERATION (debugging ? 20L : 1L)
#define TRANSFERTIME (15000 / DEBUGACCELERATION) 
#define RELIEFTIME (10000 / DEBUGACCELERATION)
#define SOLENOIDCLOSINGTIME (1000 / DEBUGACCELERATION)

#define PRESSURE_MEDIAN_WINDOW 36
#define CURRENT_MEDIAN_MIN_SAMPLE_MS 25

#define INA219_SHUNT_OHMS 10.0f

// Detecção de instabilidade: range máximo tolerado dentro da janela do filtro
#define PRESSURE_INSTABILITY_RANGE_THRESHOLD 0.5f  // bar
// Ciclos consecutivos ruins para declarar instabilidade / bons para recuperar
#define PRESSURE_INSTABILITY_BAD_COUNT  3
#define PRESSURE_INSTABILITY_GOOD_COUNT 8

#define SOLENOID_NOISE_MS 400

#define TRANSFER_CLOSE_PRESSURE_BLOCK_MS 5000

#define PRESSURE_SAMPLE_MIN_MS 250
#define PRESSURE_SAMPLES_MAX 3000
#define PRESSURE_RELIEF_HISTORY_MAX 500
#define RELIEFS_WINDOW_SIZE 2
#define RELIEF_OVERDUE_FACTOR 1.20f
#define RELIEF_PER_HOUR_MIN_DISPLAY 0.20f

#define CONST_R 0.0831446 // constante dos gases em bar*L/(mol*K) 
#define CO2MOLAR_MASS 44.01


Adafruit_INA219 ina219;
bool pressureSensorConnected = false;
float currentReading = 0.0; // Corrente em mA


averageFloatVector lnPressureDropAvg(15);
float headSpaceVolume = 0.0f;
float beerVolume = 0.0f;
float beerSG = 0.0;
float beerABV = 0.0;
float beerPlato = 0.0f;
float dissolvedCO2Mols = 0.0f;
float headSpaceCO2Mols = 0.0f;
float sgPointGenerationTime = 0.0f;

static unsigned long int timeToStartExpansion     = 0;
static unsigned long int timeToFinishExpansion    = 0;
static unsigned long int timeToStartVenting       = 0;
static unsigned long int timeToFinishVenting      = 0;
static unsigned long int timeToFinishRelief     = 0;
static unsigned long int timeToRegisterPressure = 0; // after a relief event
static unsigned long int noPressureReadUntil = 0;
static float lastPressure = 0;

static float blindHighPressure = 0;
static unsigned long int blindHighPressureMillis = 0;
static float blindLowPressure = 0;
static unsigned long int blindLowPressureMillis = 0;

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
static float pressureDropFactor = 0.99f;

static bool volumeDeterminationActive = false;
static float volumeStartPressure = 0.0f;
static float volumeStartTemperatureK = 0.0f;
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
static uint8_t currentWindowCount = 0;
static uint8_t currentWindowIndex = 0;
static unsigned long lastCurrentMedianSampleMillis = 0;

static float pressureWindow[PRESSURE_MEDIAN_WINDOW];
static uint8_t pressureWindowCount = 0;
static uint8_t pressureWindowIndex = 0;

bool pressureSensorUnstable = false;
static uint8_t pressureBadCount  = 0;
static uint8_t pressureGoodCount = 0;
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
  return (FMTData.FMTReliefVolume * dropFactor) / (1 - dropFactor);
}

static void updateBeerVolumeFromHeadspace() {
  beerVolume = FMTData.FMTVolume - headSpaceVolume;
  if (beerVolume < 0.0f) {
    beerVolume = 0.0f;
  }
}

static void recomputeHeadspaceCO2MolsFromCurrentState() {
  if (CountersData.totalReliefCount > 1) {
    headSpaceCO2Mols = ControlData.pressure    * headSpaceVolume / (CONST_R * kelvin(ControlData.temperature))
                     - BatchData.startPressure * headSpaceVolume / (CONST_R * kelvin(BatchData.startTemperature));
    if (headSpaceCO2Mols < 0.0f) {
      headSpaceCO2Mols = 0.0f;
    }
  }
  else {
    headSpaceCO2Mols = 0.0f;
  }
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

float SGToPlato(float sg) {
  if (sg < 1.0f) return 0.0f;
  float sg2 = sg * sg;
  float sg3 = sg2 * sg;
  return -616.868f + 1111.14f * sg - 630.272f * sg2 + 135.997f * sg3;
}

float PlatoToSG(float plato) {
  if (plato <= 0.0f) return 1.0f;
  return 1.0f + plato / (258.6f - (plato / 258.2f) * 227.1f);
}

float CO2DissolvedMols(float pressureBar, float sg, float temperatureC, float volumeL) {
  if (pressureBar <= 0.0f || volumeL <= 0.0f) {
    return 0.0f;
  }

  const float tempK = temperatureC + 273.15f;
  const float kH_298 = 0.0334f; // mol/(L*atm) at 25C for CO2 in water fonte: Sander, R. (2015). Compilation of Henry's law constants (version 4.0) for water as solvent. Atmospheric Chemistry and Physics, 15(8), 4399-4981. https://doi.org/10.5194/acp-15-4399-2015
  const float pressureAtm = (pressureBar+Patm) * 0.986923f; // constant is bar --> atm conversion

  float kH = kH_298 * expf(2400.0f * (1.0f / 298.15f - 1.0f / tempK));

  float sgPoints = (sg - 1.0f) * 1000.0f;
  float sgCorrection = 1.0f - (sgPoints * 0.0015f); // Correção linear: cada ponto de SG reduz a solubilidade em 0.15%. Ex: SG 1.050 tem correção de 7.5%, SG 1.100 tem correção de 15%. Fonte: https://www.brewersfriend.com/2012/11/19/co2-solubility-in-beer/
  if (sgCorrection < 0.5f) sgCorrection = 0.5f;
  if (sgCorrection > 1.0f) sgCorrection = 1.0f;

  float molPerL = kH * pressureAtm * sgCorrection;
  return molPerL * volumeL;
}

static void restoreDerivedStateFromCounters() {
  if (CountersData.headSpaceVolume > 0.0f) {
    headSpaceVolume = CountersData.headSpaceVolume;
    updateBeerVolumeFromHeadspace();
    recomputeHeadspaceCO2MolsFromCurrentState();
  }
  else {
    headSpaceVolume = 0.0f;
    beerVolume = 0.0f;
    headSpaceCO2Mols = 0.0f;
  }
}

void requestDerivedStateRestoreFromCounters() {
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

  if (headAfter<headspaceBeforeL) {
    headSpaceVolume = headAfter; 
    updateBeerVolumeFromHeadspace();
    lnPressureDropAvg.clear();
    // Keep the internal factor coherent with the recalculated headspace.
    if ((headSpaceVolume + FMTData.FMTReliefVolume) > 0.0f) {
      pressureDropFactor = headSpaceVolume / (headSpaceVolume + FMTData.FMTReliefVolume);
      pressureDropFactor = fmaxf(0.001f, fminf(pressureDropFactor, 0.999f));
      lnPressureDropAvg.add(logf(pressureDropFactor));
    }
  }


  recomputeHeadspaceCO2MolsFromCurrentState();

  CountersData.headSpaceVolume = headSpaceVolume;

  const float impliedRemovedL = headSpaceVolume - headspaceBeforeL;
  Serial.printf("[DUMP] Headspace recalculated: H_before=%.3f L, P1=%.3f bar, P2=%.3f bar, H_after=%.3f L, dH=%.3f L, Beer=%.3f L\n",
                headspaceBeforeL,
                pressureBeforeBar,
                pressureAfterBar,
                headSpaceVolume,
                impliedRemovedL,
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


boolean inPressureNoiseWindow() {
  return !(MILLISDIFF(lastSolenoidToggleMillis,SOLENOID_NOISE_MS));
}

static float medianFilter(float sample) {
  currentWindow[currentWindowIndex] = sample;
  currentWindowIndex = (currentWindowIndex + 1) % PRESSURE_MEDIAN_WINDOW;
  if (currentWindowCount < PRESSURE_MEDIAN_WINDOW) {
    currentWindowCount++;
  }

  float temp[PRESSURE_MEDIAN_WINDOW];
  for (uint8_t i = 0; i < currentWindowCount; ++i) {
    temp[i] = currentWindow[i];
  }

  for (uint8_t i = 1; i < currentWindowCount; ++i) {
    float key = temp[i];
    int8_t j = i - 1;
    while (j >= 0 && temp[j] > key) {
      temp[j + 1] = temp[j];
      --j;
    }
    temp[j + 1] = key;
  }

  uint8_t centralCount = currentWindowCount / 3;
  if (centralCount == 0) {
    centralCount = 1;
  }

  const uint8_t start = (currentWindowCount - centralCount) / 2;
  float sum = 0.0f;
  for (uint8_t i = 0; i < centralCount; ++i) {
    sum += temp[start + i];
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
  if (current <= CalibrationData.pressure0Current) {
    pressure = 0.0f;
  }
  else if (CalibrationData.pressure2Current != 0.0f) { // quadratic interpolation using Lagrange polynomials
    const float x0 = CalibrationData.pressure0Current;
    const float y0 = 0.0f;
    const float x1 = CalibrationData.pressure1Current;
    const float y1 = CalibrationData.pressure1Bar;
    const float x2 = CalibrationData.pressure2Current;
    const float y2 = CalibrationData.pressure2Bar;

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
      const float denom = CalibrationData.pressure2Current - CalibrationData.pressure1Current;
      if (fabsf(denom) > 0.000001f) {
        float ratio = (current - CalibrationData.pressure1Current) / denom;
        pressure = CalibrationData.pressure1Bar + ratio * (CalibrationData.pressure2Bar - CalibrationData.pressure1Bar);
      }
    }
  }
  else {
    const float denom = CalibrationData.pressure1Current - CalibrationData.pressure0Current;
    if (fabsf(denom) > 0.000001f) {
      float ratio = (current - CalibrationData.pressure0Current) / denom;
      pressure = ratio * CalibrationData.pressure1Bar;
    }
  }

  if (pressure < 0.0f) {
    pressure = 0.0f;
  }

  return pressure;
}

static float pressureStabilityFilter(float sample) {
  pressureWindow[pressureWindowIndex] = sample;
  pressureWindowIndex = (pressureWindowIndex + 1) % PRESSURE_MEDIAN_WINDOW;
  if (pressureWindowCount < PRESSURE_MEDIAN_WINDOW) {
    pressureWindowCount++;
  }

  float temp[PRESSURE_MEDIAN_WINDOW];
  for (uint8_t i = 0; i < pressureWindowCount; ++i) {
    temp[i] = pressureWindow[i];
  }

  for (uint8_t i = 1; i < pressureWindowCount; ++i) {
    float key = temp[i];
    int8_t j = i - 1;
    while (j >= 0 && temp[j] > key) {
      temp[j + 1] = temp[j];
      --j;
    }
    temp[j + 1] = key;
  }

  // Detecção de instabilidade: array já está ordenado → range = max - min (custo zero)
  if (pressureWindowCount >= PRESSURE_MEDIAN_WINDOW) {
    float range = temp[pressureWindowCount - 1] - temp[0];
    if (range > PRESSURE_INSTABILITY_RANGE_THRESHOLD) {
      pressureGoodCount = 0;
      if (++pressureBadCount >= PRESSURE_INSTABILITY_BAD_COUNT) {
        pressureBadCount = PRESSURE_INSTABILITY_BAD_COUNT; // evita overflow
        if (!pressureSensorUnstable) {
          pressureSensorUnstable = true;
          Serial.printf("[PRESSURE] Instabilidade detectada! Range=%.3f bar\n", range);
        }
      }
    } else {
      pressureBadCount = 0;
      if (++pressureGoodCount >= PRESSURE_INSTABILITY_GOOD_COUNT) {
        pressureGoodCount = PRESSURE_INSTABILITY_GOOD_COUNT; // evita overflow
        if (pressureSensorUnstable) {
          pressureSensorUnstable = false;
          Serial.println("[PRESSURE] Sensor estabilizado.");
        }
      }
    }
  }

  if (pressureBadCount == 0 && !pressureSensorUnstable) {
    uint8_t mid = pressureWindowCount / 2;
    if ((pressureWindowCount % 2) == 1) {
      return temp[mid];
    }
    return (temp[mid - 1] + temp[mid]) * 0.5f;
  }
  return 0.0f;
}


void readPressure() {
  // Try to initialize the INA219 if it hasn't been done yet
  static bool initialized = false;
  if (!initialized) {
    pressureSensorConnected = ina219.begin() || debugging;
    if (pressureSensorConnected) {
      Serial.println("INA219 pressure sensor initialized successfully");
    } else {
      Serial.println("Could not find INA219 pressure sensor");
    }
    ina219.setCalibration_32V_2A();
    initialized = true;
  }
  
  if (pressureSensorConnected && !inPressureNoiseWindow() && MILLISDIFF(noPressureReadUntil, 0)) {
    if (!debugging) {
      // Lê e filtra a corrente do INA219 antes de converter para pressão
      if (currentWindowCount == 0 || MILLISDIFF(lastCurrentMedianSampleMillis, CURRENT_MEDIAN_MIN_SAMPLE_MS)) {
        currentReading = medianFilter(readCurrentFromINA219mA());
        lastCurrentMedianSampleMillis = millis();
      }

      float pressure = convertCurrentToPressure(currentReading);

      pressure = pressureStabilityFilter(pressure);
      ControlData.pressure = pressure;
    }
    else { //is debugging
      static unsigned long lastPressureIncrease = 0;
      if (sgPointGenerationTime != 0 && timeToFinishVenting == 0 && beerSG > 1.010f) {
        if (MILLISDIFF(lastPressureIncrease,1000*sgPointGenerationTime))  {
          lastPressureIncrease = millis();
          ControlData.pressure += 0.1f;
        } 
      }
    }
  }
  else if (!pressureSensorConnected) {
    // Se não tem sensor, zera a pressão - Lucio urgente - precisar alertar
    ControlData.pressure = 0.0;
    currentReading = 0.0;
  }  
}  

static void showVolumeStatus(const char *line1, const char *line2, const char *line3) {
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.fillRect(10, 220, 300, 90, TFT_BLACK);
  tft.drawString(line1, 10, 225, 2);
  tft.drawString(line2, 10, 245, 2);
  tft.drawString(line3, 10, 265, 2);
  Serial.print(line1); Serial.print(" | "); Serial.print(line2); Serial.print(" | "); Serial.println(line3);
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
  volumeSummaryNReliefs = volumeIteration;

  if (volumeSummaryTiK <= 0.0f || volumeSummaryTfK <= 0.0f ||
      volumeSummaryPi <= 0.0f || volumeSummaryNReliefs == 0) {
    volumeDeterminationActive = false;
    showVolumeStatus("Volume: END", "Dados insuficientes", "Sem resumo");
    return;
  }

  volumeSummaryPfAdjusted = ControlData.pressure * (volumeSummaryTiK / volumeSummaryTfK);
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



void processPressure(bool afterRelief) {
  if (afterRelief && debugging) {
    if (volumeDeterminationActive && pressureSamples) 
      ControlData.pressure = ControlData.pressure * 0.984f + random(-5,5) * 0.0005; 
    else
      ControlData.pressure = ControlData.pressure * 0.942f + random(-5,5) * 0.0005;       
  }      

  updateReliefsPerHour(afterRelief);
  
  if  (afterRelief) {
    static float lastPressureDrop = 0;

    float pressureGainOverTime = (blindHighPressure - blindLowPressure) / (blindHighPressureMillis - blindLowPressureMillis);
    if (pressureGainOverTime < 0 || pressureGainOverTime > 1e-4) {
      pressureGainOverTime = 0;
    }
    
    float blindWindowCorrection = pressureGainOverTime * (millis() - blindHighPressureMillis);
    
    //lastPressureDrop = ControlData.pressure - lastPressure + blindWindowCorrection;

    blindLowPressure = ControlData.pressure;
    blindLowPressureMillis = millis();

    float instantPressureDropFactor = 1.0f;
    if (lastPressure > 0.01f) {
      instantPressureDropFactor = (ControlData.pressure - blindWindowCorrection) / lastPressure;
    }

    // Keep factor in a valid range for log() and downstream equations.
    instantPressureDropFactor = fmaxf(0.001f, fminf(instantPressureDropFactor, 0.999f));
    lnPressureDropAvg.add(logf(instantPressureDropFactor));
    pressureDropFactor = expf(lnPressureDropAvg.value());
    pressureDropFactor = fmaxf(0.001f, fminf(pressureDropFactor, 0.999f));
    
    headSpaceVolume = volumeEstimationFromPressureDrop(pressureDropFactor); 
    updateBeerVolumeFromHeadspace();
    
    if (pendingReliefIndex >= 0) {
      PressureReliefRecord &record = pressureReliefHistory[pendingReliefIndex];
      record.pressureAfter = ControlData.pressure;
      record.currentAfter = currentReading;
    }
    pendingReliefIndex = -1;

    float ejectedMols = ControlData.pressure * FMTData.FMTReliefVolume / (CONST_R * kelvin(ControlData.temperature));
    CountersData.totalMolsEjected += ejectedMols;
    
    CountersData.totalReliefCount += 1;    

  }

  if (CountersData.totalReliefCount> 1 || ControlData.pressure > BatchData.startPressure+0.2) 
    dissolvedCO2Mols = CO2DissolvedMols(ControlData.pressure, beerSG, ControlData.temperature, beerVolume);
  else
    dissolvedCO2Mols = 0;

  recomputeHeadspaceCO2MolsFromCurrentState();
  
  //float lastTotalCO2MolsProceduced = CountersData.CO2InSolution + headSpaceCO2Mols + CountersData.totalMolsEjected; está sendo usado ou não?

  CountersData.CO2InSolution = dissolvedCO2Mols;
  CountersData.headSpaceVolume = headSpaceVolume;

//  float massCO2Produced = CO2Mass(CountersData.totalCO2MolsProduced - lastTotalCO2MolsProceduced);
//  CountersData.SGAttenuation += 2.047*massCO2Produced/(beerVolume)/1000;

//  CountersData.SGAttenuation -= EstimateSGFromProducedCO2Mol(beerSG, beerVolume, CountersData.totalCO2MolsProduced - lastTotalCO2MolsProceduced) - beerSG;
//  Serial.println(EstimateSGFromProducedCO2Mol(beerSG, beerVolume, CountersData.totalCO2MolsProduced - lastTotalCO2MolsProceduced) *1000.0);

  float Pi = SGToPlato(BatchData.batchOG) + BatchData.addedPlato;
  float SGu = 1 + (Pi / (258.6-(Pi/258.2)*227.1));
  float totalCO2Mols = CountersData.totalMolsEjected + CountersData.CO2InSolution + headSpaceCO2Mols;
  beerPlato = (100*(10*beerVolume*SGu*Pi - 90.08 * totalCO2Mols)
                / (1000*beerVolume*SGu - 44.01 * totalCO2Mols) 
             - 0.1808*Pi ) 
             / 0.8192;
  beerSG = PlatoToSG(beerPlato);
  beerABV = 100*(105*(BatchData.batchOG - beerSG) / (100 - beerSG) * (beerSG / 0.79));

  //beerSG = BatchData.batchOG - CountersData.SGAttenuation;
  
  if (afterRelief && volumeDeterminationActive) {
    if (volumeAwaitingRecord && volumeRecordIndex >= 0) {
      PressureReliefRecord &record = pressureReliefHistory[volumeRecordIndex];
      volumeIteration++;

      record.tiK = volumeStartTemperatureK;
      record.tfK = kelvin(ControlData.temperature);
      record.pi = volumeStartPressure;
      record.nReliefs = volumeIteration;
      record.volumeMetricsValid = false;

      if (record.tiK > 0.0f && record.tfK > 0.0f && record.pi > 0.0f && record.nReliefs > 0) {
        record.pfAdjusted = record.pressureAfter * (record.tiK / record.tfK);
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

  lastPressure = ControlData.pressure;
}

bool processReliefCycle() {
  if (timeToFinishRelief || timeToRegisterPressure) {
    if (timeToStartExpansion) {
      if (MILLISDIFF(timeToStartExpansion, 0)) {
        //;Serial.printf("[PRESSURE] %lu / %lu: Abrindo transfer valve. Pressure=%.2f bar\n", millis(), timeToStartExpansion, ControlData.pressure);
        lastPressure = ControlData.pressure;
        blindHighPressure = ControlData.pressure;
        blindHighPressureMillis = millis();
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
        digitalWrite(PINTRANSFERVALVE, LOW);
       markSolenoidToggle();
        ControlData.transferValve = false;
        timeToStartVenting    += millis();
        resetCurrentMedianFilter();
        noPressureReadUntil = millis() + TRANSFER_CLOSE_PRESSURE_BLOCK_MS;
        timeToRegisterPressure = noPressureReadUntil + 1000; // registra após o período de bloqueio pós-fechamento da transfer
        timeToFinishExpansion = 0;
      }
    }
    else if (timeToStartVenting) {
      if(MILLISDIFF(timeToStartVenting, 0)) {
        //;Serial.printf("[PRESSURE] %lu / %lu: Abrindo relief valve. Pressure=%.2f bar\n", millis(), timeToStartVenting, ControlData.pressure);
        digitalWrite(PINRELIEFVALVE, HIGH);
        markSolenoidToggle();
        ControlData.reliefValve = true;
        timeToFinishVenting += millis();
        timeToStartVenting = 0;
      }
    }

    else if (timeToFinishVenting) {
      if (MILLISDIFF(timeToFinishVenting, 0)) {
        //;Serial.printf("[PRESSURE] %lu / %lu: Fechando relief valve. Pressure=%.2f bar\n", millis(), timeToFinishVenting, ControlData.pressure);
        digitalWrite(PINRELIEFVALVE, LOW);
        markSolenoidToggle();
        ControlData.reliefValve = false;
        timeToFinishVenting = 0;
        timeToFinishRelief += millis();
      }
    }
    else {
      digitalWrite(PINTRANSFERVALVE, LOW);
      digitalWrite(PINRELIEFVALVE, LOW);
      ControlData.transferValve = false;  
      ControlData.reliefValve = false;
      if (timeToFinishRelief && MILLISDIFF(timeToFinishRelief, 0)) {
        timeToFinishRelief = 0;
      }
    }

    if (timeToRegisterPressure) {
      if (MILLISDIFF(timeToRegisterPressure, 0)) {
        //;Serial.printf("[PRESSURE] %lu / %lu: Registrando pressão. Pressure=%.2f bar\n", millis(), timeToRegisterPressure, ControlData.pressure);
        timeToRegisterPressure = 0;
        processPressure(true);
      }
    }    
    
    return true;
  }
  else
    return false;
}

void pressureRelief(bool fromVolumeDetermination) {
  if (timeToFinishRelief) { // if we're still in the middle of a relief, ignore new relief requests to avoid overlapping and potential hardware issues
    return;
  }

  if (fromVolumeDetermination) {
    if (!pressureReliefHistory) {
      return;
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

  const float pressureSeconds = (ControlData.pressure > 0.0f) ? ControlData.pressure : 0.0f;
  const unsigned long extraMs = (unsigned long)(pressureSeconds * 1000.0L) / (debugging ? 10 : 1);
  const bool isBrewingTransfer = (SetPointData.mode == MODE_BREWING_TRANSFERING);
  const unsigned long reliefDurationDivisor = isBrewingTransfer ? 2UL : 1UL;

  const unsigned long scaledTransferTime = (unsigned long)TRANSFERTIME / reliefDurationDivisor;
  const unsigned long scaledSolenoidClosingTime = isBrewingTransfer ? 0: (unsigned long)SOLENOIDCLOSINGTIME;
  const unsigned long scaledReliefTime = (unsigned long)RELIEFTIME / reliefDurationDivisor;
  const unsigned long scaledExtraMs = isBrewingTransfer ? 0: extraMs;
  const unsigned long scaledFinishMs = isBrewingTransfer ? 0 : 1000UL;

  timeToStartExpansion     = millis() + holdPressureDueToTemperatureRelays();
  timeToFinishExpansion    = scaledTransferTime + scaledExtraMs;
  timeToStartVenting       = scaledSolenoidClosingTime;
  timeToFinishVenting      = scaledReliefTime + scaledExtraMs;
  // processPressureRelays uses timeToFinishRelief as the cycle-active flag.
  // In MODE_OFF (volume determination), scaledFinishMs is 0, so keep a non-zero flag.
  timeToFinishRelief     = (scaledFinishMs > 0) ? scaledFinishMs : 1UL;
  timeToRegisterPressure = 0; // garante que estado anterior não vaza para novo ciclo

  ;Serial.printf("[PRESSURE] Relief requested: pressure=%.2f bar, extraMs=%lu, transferOpen=%lu, transferClose=%lu, reliefOpen=%lu, reliefClose=%lu\n", 
                ControlData.pressure, extraMs, timeToStartExpansion, timeToFinishExpansion, timeToStartVenting, timeToFinishVenting);

  // NÃO chamar processReliefCycle() aqui:
  // pressureRelief() é chamado do async web task (core 0) e processReliefCycle()
  // também é chamado do main loop (core 1) via pressureControl().
  // Chamar aqui cria uma race condition onde ambos executam stage 1 simultaneamente
  // e o += millis() de timeToFinishExpansion é aplicado duas vezes,
  // resultando em timeToFinishExpansion ≈ 2*millis()+5000 → transfer fica aberto por ~16min.
  // O main loop chamará processReliefCycle() dentro de milissegundos.
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

  if (volumeDeterminationActive || timeToFinishRelief) {
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

  if (ControlData.pressure < 1.5f) {
    if (reason && reasonSize > 0) {
      snprintf(reason, reasonSize, "Insufficient pressure (min 1.5 bar)");
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
  volumeStartPressure = ControlData.pressure;
  volumeStartTemperatureK = kelvin(ControlData.temperature);
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
    beerPlato = SGToPlato(beerSG);
  }

  readPressure();

  if (SetPointData.mode != MODE_OFF &&
      !volumeDeterminationActive &&
      !timeToFinishRelief &&
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

  if (!pressureSensorUnstable) {
    if (volumeDeterminationActive) {
      if (ControlData.pressure > volumeTargetPressure) {
        pressureRelief(true);
      } else {
        finalizeVolumeDeterminationSummary();
      }
    }
    else 
      if (SetPointData.setPointPressure > 0.0f && 
         ControlData.pressure > (SetPointData.setPointPressure / sqrt(pressureDropFactor)) &&
        (taskWindowType == 0 || !MILLISDIFF(taskWindowEndTime, 0))) {
          pressureRelief(false);
        }
      if (ControlData.pressure > CalibrationData.maximumPressure) {
        pressureRelief(false);
      }
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

char *getPressureControlStatus(char *st) {
  char tmp[160];

  int16_t rawShuntRegister = 0;
  st[0] = '\0';

  if (pressureSensorConnected) {
    snprintf(tmp, sizeof(tmp), "INA219 Pressure Sensor: OK%s<br>",
             pressureSensorUnstable ? " [INSTÁVEL]" : "");
    strncat(st, tmp, 2000);
    snprintf(tmp, sizeof(tmp), "Instability: bad=%u good=%u<br>", pressureBadCount, pressureGoodCount);
    strncat(st, tmp, 2000);
    snprintf(tmp, sizeof(tmp), "Atmospheric pressure: %.3f bar<br>Filtered current reading: %.2f mA<br>Calculated pressure: %.3f bar<br>",
             Patm, currentReading, ControlData.pressure);
    strncat(st, tmp, 2000);


    snprintf(tmp, sizeof(tmp), "Momentary current: %.2f mA<br>Library current: %.2f mA<br>",
          readCurrentFromINA219mA(), ina219.getCurrent_mA());

    strncat(st, tmp, 2000);
    snprintf(tmp, sizeof(tmp), "Pressure drop factor (%%): %.3f<br>Headspace volume: %.2f L<br>Beer volume: %.2f L<br>",
             pressureDropFactor * 100, headSpaceVolume, beerVolume);
    strncat(st, tmp, 2000);
    snprintf(tmp, sizeof(tmp), "Beer SG: %.4f<br>Beer ABV: %.2f%%<br>", beerSG, beerABV);
    strncat(st, tmp, 2000);
    snprintf(tmp, sizeof(tmp), "Bus voltage: %.2f V<br>Shunt voltage: %.2f mV<br>Power: %.2f mW<br>Calibration: 16V/400mA (shunt range up to 40mV)<br>",
          ina219.getBusVoltage_V(), ina219.getShuntVoltage_mV(), ina219.getPower_mW());
    strncat(st, tmp, 2000);

    
    snprintf(tmp, sizeof(tmp), "Relief count: %lu<br>Ejected CO2 mols: %.3f<br>Dissolved CO2 mols: %.3f<br>",
             (unsigned long)CountersData.totalReliefCount, CountersData.totalMolsEjected, dissolvedCO2Mols);
    strncat(st, tmp, 2000);
    if (!reliefsPerHourAvailable || reliefsPerHourValue < RELIEF_PER_HOUR_MIN_DISPLAY) {
      if (!reliefsPerHourAvailable) {
        snprintf(tmp, sizeof(tmp), "Reliefs/hour: N/A (need %u reliefs, have %u)<br>", (unsigned)RELIEFS_WINDOW_SIZE, reliefMillisCount);
      } else {
        snprintf(tmp, sizeof(tmp), "Reliefs/hour: N/A (< %.2f/h)<br>", RELIEF_PER_HOUR_MIN_DISPLAY);
      }
    } else {
      snprintf(tmp, sizeof(tmp), "Reliefs/hour: %.2f<br>", reliefsPerHourValue);
    }
    strncat(st, tmp, 2000);
    snprintf(tmp, sizeof(tmp), "Headspace CO2 mols: %.3f<br>Total CO2 mols: %.3f<br>Total CO2 mass: %.2f g<br>",
             headSpaceCO2Mols, CountersData.totalMolsEjected + CountersData.CO2InSolution + headSpaceCO2Mols, CO2Mass());
    strncat(st, tmp, 2000);
  } else {
    snprintf(tmp, sizeof(tmp), "INA219 Pressure Sensor: DISCONNECTED<br>Atmospheric pressure: %.3f bar<br>", Patm);
    strncat(st, tmp, 2000);
  }

  // --- Diagnóstico de relés ---
  unsigned long now = millis();
  strncat(st, "<br>--- Relay cycle ---<br>", 2000);
  if (!timeToFinishRelief) {
    strncat(st, "Cycle: IDLE<br>", 2000);
  } else {
    strncat(st, "Cycle: ACTIVE<br>", 2000);
    if (timeToStartExpansion) {
      snprintf(tmp, sizeof(tmp), "Stage: waiting to open transfer (in %ld ms)<br>",
               (long)(timeToStartExpansion - now));
    } else if (timeToFinishExpansion) {
      snprintf(tmp, sizeof(tmp), "Stage: transfer OPEN - closes in %ld ms<br>",
               (long)(timeToFinishExpansion - now));
    } else if (timeToStartVenting) {
      snprintf(tmp, sizeof(tmp), "Stage: waiting to open relief (in %ld ms)<br>",
               (long)(timeToStartVenting - now));
    } else if (timeToRegisterPressure) {
      snprintf(tmp, sizeof(tmp), "Stage: relief OPEN - measuring pressure in %ld ms<br>",
               (long)(timeToRegisterPressure - now));
    } else if (timeToFinishVenting) {
      snprintf(tmp, sizeof(tmp), "Stage: relief OPEN - closes in %ld ms<br>",
               (long)(timeToFinishVenting - now));
    } else {
      snprintf(tmp, sizeof(tmp), "Stage: finishing (cooldown %ld ms)<br>",
               (long)(timeToFinishRelief - now));
    }
    strncat(st, tmp, 2000);
    snprintf(tmp, sizeof(tmp),
             "timeToStartExpansion=%lu timeToFinishExpansion=%lu<br>"
             "timeToStartVenting=%lu timeToFinishVenting=%lu<br>"
             "timeToFinishRelief=%lu timeToRegister=%lu<br>",
             timeToStartExpansion, timeToFinishExpansion,
             timeToStartVenting, timeToFinishVenting,
             timeToFinishRelief, timeToRegisterPressure);
    strncat(st, tmp, 2000);
  }

  return st;
}


/*Implementar redução por purga
Implementar transferência de massa
Rever modelo de controle de resfriamento / aquecimento
Implementar tasks de adição de volume o de intercenção em gás
implementar conditioning
quando reiniciou perdeu contador de co2 ejetado
ver se coutersdata dá persistência à densidade final e parar de atualizar em conditioning
*/


