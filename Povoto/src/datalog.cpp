#include <Arduino.h>
#include <IOTK_GLog.h>
#include <IOTK.h>
#include "GambainoCommon.h"
#include "PovotoCommon.h"
#include "PovotoData.h"
#include "TemperatureControl.h"
#include "datalog.h"
#include "PressureControl.h"
#include "PovotoTasks.h"
#include <math.h>

#define BREWFATHER_SEND_INTERVAL_MS 600000UL  // 10 minutos
#define BREWFATHER_RETRY_INTERVAL_MS 30000UL  // 15 segundos entre tentativas

static bool isValidTemp(float value) {
  return (value != NOTaTEMP && value != 85);
}

static bool isZeroMac(const uint8_t *mac) {
  if (!mac) return true;
  for (int i = 0; i < 6; i++) {
    if (mac[i] != 0) return false;
  }
  return true;
}

static void escapeJsonString(const char *src, char *dst, size_t dstSize) {
  if (!dst || dstSize == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }

  size_t w = 0;
  for (size_t r = 0; src[r] != '\0' && w + 1 < dstSize; r++) {
    char c = src[r];
    if ((c == '"' || c == '\\') && w + 2 < dstSize) {
      dst[w++] = '\\';
      dst[w++] = c;
    } else if ((unsigned char)c < 32) {
      // Skip control chars to keep payload JSON-safe.
    } else {
      dst[w++] = c;
    }
  }
  dst[w] = '\0';
}

static bool buildBrewfatherPayload(char *out, size_t outSize) {
  if (!out || outSize == 0) {
    return false;
  }

  char batchNameEsc[96];
  escapeJsonString(BatchData.batchName, batchNameEsc, sizeof(batchNameEsc));

  const float temp = isValidTemp(ControlData.temperature) ? ControlData.temperature : NAN;
  const float targetTemp = isValidTemp(SetPointData.setPointTemp) ? SetPointData.setPointTemp : NAN;
  const float gravity = (beerSG >= 0.0f && beerSG <= 1.1f) ? beerSG : NAN;
  const float pressure = (ControlData.pressure >= 0.0f && ControlData.pressure <= 3.0f) ? ControlData.pressure : NAN;
  const float bpm = getBeerCO2EvolutionGramsPerLiterPerDay();

  char tempField[24];
  char extTempField[24];
  char gravityField[24];
  char pressureField[24];
  char bpmField[24];

  if (isnan(temp)) snprintf(tempField, sizeof(tempField), "null");
  else snprintf(tempField, sizeof(tempField), "%.2f", temp);

  if (isnan(targetTemp)) snprintf(extTempField, sizeof(extTempField), "null");
  else snprintf(extTempField, sizeof(extTempField), "%.2f", targetTemp);

  if (isnan(gravity)) snprintf(gravityField, sizeof(gravityField), "null");
  else snprintf(gravityField, sizeof(gravityField), "%.5f", gravity);

  if (isnan(pressure)) snprintf(pressureField, sizeof(pressureField), "null");
  else snprintf(pressureField, sizeof(pressureField), "%.3f", pressure);

  if (isnan(bpm)) snprintf(bpmField, sizeof(bpmField), "null");
  else snprintf(bpmField, sizeof(bpmField), "%.2f", bpm);

  int written = snprintf(
    out,
    outSize,
    "{\"name\":\"%s%d\",\"temp_unit\":\"C\",\"pressure_unit\":\"BAR\",\"temp\":%s,\"ext_temp\":%s,\"gravity\":%s,\"gravity_unit\":\"G\",\"pressure\":%s,\"bpm\":%s,\"beer\":\"%s\"}",
    debugging ? "Debfmt" : "Fmt",
    (int)FMTData.PovotoNum,
    tempField,
    extTempField,
    gravityField,
    pressureField,
    bpmField,
    batchNameEsc
  );

  return (written > 0 && (size_t)written < outSize);
}

static const char *taskWindowTypeToText(byte type) {
  switch (type) {
    case 1: return "Dump";
    case 2: return "Gas";
    case 3: return "Liquid";
    case 4: return "Dry Hopping";
    case 5: return "Dynamic Hopping";
    default: return "";
  }
}

void maybeSendBrewfatherLog() {
  static unsigned long lastSuccessfulSend = 0;
  static unsigned long lastAttemptTime = 0;

  if (SetPointData.mode == MODE_OFF) {
    return;
  }
  if (BatchData.batchNumber == 0) {
    return;
  }
  if (isZeroMac(peerSideKick.mac)) {
    return;
  }

  unsigned long now = millis();
  
  // Se teve sucesso, aguarda 10 minutos para próximo envio
  if (lastSuccessfulSend != 0 && (now - lastSuccessfulSend) < BREWFATHER_SEND_INTERVAL_MS) {
    return;
  }
  
  // Se falhou ou está em retry, aguarda 15 segundos entre tentativas
  if (lastAttemptTime != 0 && (now - lastAttemptTime) < BREWFATHER_RETRY_INTERVAL_MS) {
    return;
  }

  char payload[512];
  if (!buildBrewfatherPayload(payload, sizeof(payload))) {
    return;
  }

  lastAttemptTime = now;  // Registra tentativa antes de enviar

  esp_err_t err = sendEspNow(peerSideKick.mac, 0, false, (uint8_t)BREWFATHERLOGPACKET, payload);
  if (err != ESP_OK) {
    Serial.printf("[BREWFATHER] ESP-NOW send failed: %d\n", (int)err);
    return;
  }
  
  // Atualiza lastSuccessfulSend apenas após envio bem-sucedido
  lastSuccessfulSend = now;
}

void doDataLog() {
  if (!datalogFolderNameInUse[0]) {
    return;
  }

  static bool headerWritten = false;
  static int lastBatchNum = -1;

  if (BatchData.batchNumber == 0) {
    headerWritten = false;
    return;
  }

  if (SetPointData.mode == MODE_OFF) {
    return;
  }

  int batchNum = (int)BatchData.batchNumber;
  if (batchNum != lastBatchNum) {
    lastBatchNum = batchNum;
    headerWritten = false;
  }
  char batchStr[6]; // All uint16_t batch numbers plus the terminator.
  snprintf(batchStr, sizeof(batchStr), "%03d", batchNum);

  if (!headerWritten) {
    GLogBegin(datalogFolderNameInUse, batchStr, "Cold");
    GLogAddTimeStamp();
    GLogAddData("FMT");
    GLogAddData("Temperature");
    GLogAddData("TempTarget");
    GLogAddData("Pressure");
    GLogAddData("PressureTarget");
    GLogAddData("HeadSpaceVolume");
    GLogAddData("BeerVolume");
    GLogAddData("SG");
    GLogAddData("RE (Plato)");
    GLogAddData("ABV");
    GLogAddData("HeadSpaceCO2Mols");
    GLogAddData("CO2MolsSol");
    GLogAddData("CO2MolSolIfEq");
    GLogAddData("CO2MolsEjected");
    GLogAddData("ReliefCount");
    GLogAddData("gCO2/L/d");
    GLogAddData("PressureDropFactor");
    GLogAddData("TemperatureMode");
    GLogAddData("taskWindowType");
    GLogAddData("ChillTime(h)");
    GLogAddData("HeatTime(h)");
    GLogAddData("Millis");
    GLogAddData("pressureOnReliefExtrap");
    GLogAddData("PressureAfterRelief");
    GLogAddData("PressureAfterReliefMillis");
    GLogAddData("AdjustedPressureAfterRelief");
    GLogAddData("PressureReachedTarget");
    GLogAddData("PressureReachedTargetMillis");
    GLogAddData("DissolvedCO2Mode");
    GLogAddData("DissolvedCO2CalculationPressure");
    GLogAddData("DissolvedCO2MolsAtEquilibrium");
    GLogAddData("DissolvedCO2CriteriaState");
    GLogAddData("DissolvedCO2CriteriaElapsedMillis");
    GLogAddData("DissolvedCO2ConfirmationMillis");
    GLogAddData("CO2WithReliefsState");
    GLogAddData("CO2WithReliefsElapsedMillis");
    GLogAddData("CO2WithoutReliefsState");
    GLogAddData("CO2WithoutReliefsElapsedMillis");
    GLogAddData("Pressure10MinAgo");
    GLogAddData("ReliefIntervalSeconds");
    GLogAddData("SinceLastReliefSeconds");
    GLogAddData("LastTaskMillis");
    GLogAddData("OperatingMode");
    GLogAddData("AtmosphericPressure");

    GLogSend();
    headerWritten = true;
  }
  else {
    GLogBegin(datalogFolderNameInUse, batchStr, "Cold");
    GLogAddTimeStamp();
    GLogAddData(FMTData.PovotoNum);
    GLogAddData(ControlData.temperature, 2);
    GLogAddData(SetPointData.setPointTemp, 2);
    GLogAddData(ControlData.pressure, 3);
    GLogAddData(SetPointData.setPointPressure, 3);
    GLogAddData(CountersData.headSpaceVolume, 3);
    GLogAddData(beerVolume, 3);
    GLogAddData(beerSG,5);
    GLogAddData(SGToRealPlato(beerSG),3);
    GLogAddData(beerABV,2);
    GLogAddData(headSpaceCO2Mols,3);
    GLogAddData(CountersData.CO2InSolution,3);
    GLogAddData(CO2DissolvedMols(ControlData.pressure, beerSG, ControlData.temperature, beerVolume),3);
    GLogAddData(CountersData.totalMolsEjected,3);
    GLogAddData(CountersData.totalReliefCount,0);
    GLogAddData(beerCO2EvolutionGramsPerLiterPerDay,3);
    GLogAddData(pressureDropFactor,5);
    GLogAddData(getTemperatureModeLabel());
    GLogAddData(taskWindowTypeToText(taskWindowType));
    GLogAddData(CountersData.totalChillTime/3600.,2);
    GLogAddData(CountersData.totalHeatTime /3600.,2);
    GLogAddData(millis());
    GLogAddData(pressureOnReliefExtrap,3);
    GLogAddData(pressureAfterRelief,3);
    GLogAddData(pressureAfterReliefMillis);
    GLogAddData(adjustedPressureAfterRelief,3);
    GLogAddData(pressureReachedTarget,3);
    GLogAddData(pressureReachedTargetMillis);
    const DissolvedCO2LogData co2 = getDissolvedCO2LogData();
    GLogAddData(co2.mode);
    GLogAddData(co2.calculationPressure, 3);
    GLogAddData(co2.equilibriumMols, 6);
    GLogAddData(co2.criteriaState);
    GLogAddData(co2.criteriaElapsedMillis);
    GLogAddData(co2.confirmationMillis);
    GLogAddData(co2.withReliefsState);
    GLogAddData(co2.withReliefsElapsedMillis);
    GLogAddData(co2.withoutReliefsState);
    GLogAddData(co2.withoutReliefsElapsedMillis);
    GLogAddData(co2.previousPressure, 3);
    GLogAddData(co2.reliefIntervalSeconds, 3);
    GLogAddData(co2.sinceLastReliefSeconds, 3);
    GLogAddData(lastTaskMillis);
    GLogAddData((int)SetPointData.mode);
    GLogAddData(Patm, 3);
    GLogSend();
  }
}

void doReliefDataLog(const ReliefLogData &data) {
  if (!datalogFolderNameInUse[0] || BatchData.batchNumber == 0) {
    return;
  }

  static bool headerWritten = false;
  static int lastBatchNum = -1;
  // A relief is identified by the instant at which its valve was opened.
  // processPressure(true) is expected to run once, but retaining this small
  // guard prevents a repeated scheduling event from producing a second row.
  static bool lastReliefLogged = false;
  static int lastReliefBatchNum = -1;
  static unsigned long lastReliefValveOpenedMillis = 0;
  const int batchNum = (int)BatchData.batchNumber;

  if (lastReliefLogged &&
      lastReliefBatchNum == batchNum &&
      lastReliefValveOpenedMillis == data.valveOpenedMillis) {
    return;
  }

  lastReliefLogged = true;
  lastReliefBatchNum = batchNum;
  lastReliefValveOpenedMillis = data.valveOpenedMillis;

  if (batchNum != lastBatchNum) {
    lastBatchNum = batchNum;
    headerWritten = false;
  }

  char batchStr[6]; // All uint16_t batch numbers plus the terminator.
  snprintf(batchStr, sizeof(batchStr), "%03d", batchNum);

  if (!headerWritten) {
    GLogBegin(datalogFolderNameInUse, batchStr, "Relief");
    GLogAddTimeStamp();
    GLogAddData("FMT");
    GLogAddData("ReliefNumber");
    GLogAddData("ValveOpenedMillis");
    GLogAddData("VolumeDetermination");
    GLogAddData("Temperature");
    GLogAddData("PressureTarget");
    GLogAddData("AtmosphericPressure");
    GLogAddData("ReliefVolume");
    GLogAddData("EffectiveVentingExponent");
    GLogAddData("PressureOnReliefMeasured");
    GLogAddData("CurrentOnReliefMeasured");
    GLogAddData("PressureReachedTarget");
    GLogAddData("PressureReachedTargetMillis");
    GLogAddData("PressureOnReliefExtrapolated");
    GLogAddData("PressureAfterRelief");
    GLogAddData("PressureAfterReliefMillis");
    GLogAddData("CurrentAfterRelief");
    GLogAddData("AdjustedPressureAfterRelief");
    GLogAddData("AdjustedEquilibriumPressure");
    GLogAddData("EjectedPressure");
    GLogAddData("LiquidMassInGasVentingPercent");
    GLogAddData("ExpansionTankResidualMoles");
    GLogAddData("EjectedMolsBeforeLiquidCorrection");
    GLogAddData("InstantPressureDropFactor");
    GLogAddData("PressureDropFactor");
    GLogAddData("HeadSpaceVolume");
    GLogAddData("BeerVolume");
    GLogAddData("EjectedMols");
    GLogAddData("TotalEjectedMols");
    GLogAddData("HeadSpaceCO2Mols");
    GLogAddData("DissolvedCO2Mols");
    GLogAddData("TotalCO2Mols");
    GLogAddData("BeerSG");
    GLogAddData("BeerRealPlato");
    GLogAddData("BeerABV");
    GLogAddData("ReliefCount");
    GLogAddData("ReliefsPerHour");
    GLogAddData("gCO2/L/d");
    GLogAddData("GasFlowModelActive");
    GLogAddData("GasRiseRateBarPerSecond");
    GLogAddData("GasAvailableSeconds");
    GLogAddData("GasOpeningSeconds");
    GLogAddData("GasPreviousVentingSeconds");
    GLogAddData("GasExpansionResidual");
    GLogAddData("GasInitialResidualMoles");
    GLogAddData("GasTankMolesAtClose");
    GLogAddData("GasModelTankMolesAtClose");
    GLogAddData("GasModelTankMolesDifferencePercent");
    GLogAddData("GasVentingResidualFactor");
    GLogAddData("GasVentingResidual");
    GLogAddData("GasExpansionOptimalSeconds");
    GLogAddData("GasVentingOptimalSeconds");
    GLogAddData("GasVentingFermenterOptimalSeconds");
    GLogAddData("GasVentingVolumeRatio");
    GLogAddData("GasPlannedExpansionSeconds");
    GLogAddData("GasPlannedVentingSeconds");
    GLogAddData("GasProjectedFermenterPressure");
    GLogAddData("GasProjectedExpansionPressure");
    GLogAddData("GasProjectedPressureDifference");
    GLogAddData("GasCalculatedPressureCompensation");
    GLogAddData("GasAppliedPressureCompensation");
    GLogAddData("GasHeadspaceUpdateStatus");
    GLogSend();
    headerWritten = true;
  }

  // Send the data row separately so the first relief is logged as well.
  GLogBegin(datalogFolderNameInUse, batchStr, "Relief");
  GLogAddTimeStamp();
  GLogAddData(data.povotoNumber);
  GLogAddData(data.reliefNumber);
  GLogAddData(data.valveOpenedMillis);
  GLogAddData(data.volumeDeterminationActive ? "yes" : "no");
  GLogAddData(data.temperature, 2);
  GLogAddData(data.targetPressure, 3);
  GLogAddData(data.atmosphericPressure, 3);
  GLogAddData(data.reliefVolume, 3);
  GLogAddData(data.effectiveVentingExponent, 3);
  GLogAddData(data.pressureOnReliefMeasured, 3);
  GLogAddData(data.currentOnReliefMeasured, 3);
  GLogAddData(data.pressureReachedTarget, 3);
  GLogAddData(data.pressureReachedTargetMillis);
  GLogAddData(data.pressureOnReliefExtrapolated, 3);
  GLogAddData(data.pressureAfterRelief, 3);
  GLogAddData(data.pressureAfterReliefMillis);
  GLogAddData(data.currentAfterRelief, 3);
  GLogAddData(data.adjustedPressureAfterRelief, 3);
  GLogAddData(data.adjustedEquilibriumPressure, 3);
  GLogAddData(data.ejectedPressure, 3);
  GLogAddData(data.liquidMassInGasVentingPercent, 3);
  GLogAddData(data.expansionTankResidualMoles, 6);
  GLogAddData(data.ejectedMolsBeforeLiquidCorrection, 6);
  GLogAddData(data.instantaneousPressureDropFactor, 6);
  GLogAddData(data.pressureDropFactor, 6);
  GLogAddData(data.headSpaceVolume, 3);
  GLogAddData(data.beerVolume, 3);
  GLogAddData(data.ejectedMols, 6);
  GLogAddData((float)data.totalMolsEjected, 6);
  GLogAddData(data.headSpaceCO2Mols, 6);
  GLogAddData((float)data.dissolvedCO2Mols, 6);
  GLogAddData((float)data.totalCO2Mols, 6);
  GLogAddData(data.beerSG, 5);
  GLogAddData(data.beerRealPlato, 3);
  GLogAddData(data.beerABV, 3);
  GLogAddData(data.totalReliefCount);
  GLogAddData(data.reliefsPerHour, 3);
  GLogAddData(data.beerCO2EvolutionGramsPerLiterPerDay, 3);
  GLogAddData(data.gasFlowModelActive ? "yes" : "no");
  GLogAddData(data.gasRiseRateBarPerSecond, 8);
  GLogAddData(data.gasAvailableSeconds, 3);
  GLogAddData(data.gasOpeningSeconds, 3);
  GLogAddData(data.gasPreviousVentingSeconds, 3);
  GLogAddData(data.gasExpansionResidual, 6);
  GLogAddData(data.gasInitialResidualMoles, 8);
  GLogAddData(data.gasTankMolesAtClose, 8);
  GLogAddData(data.gasModelTankMolesAtClose, 8);
  GLogAddData(data.gasModelTankMolesDifferencePercent, 4);
  GLogAddData(data.gasVentingResidualFactor, 6);
  GLogAddData(data.gasVentingResidual, 6);
  GLogAddData(data.gasExpansionOptimalSeconds, 3);
  GLogAddData(data.gasVentingOptimalSeconds, 3);
  GLogAddData(data.gasVentingFermenterOptimalSeconds, 3);
  GLogAddData(data.gasVentingVolumeRatio, 6);
  GLogAddData(data.gasPlannedExpansionSeconds, 3);
  GLogAddData(data.gasPlannedVentingSeconds, 3);
  GLogAddData(data.gasProjectedFermenterPressure, 6);
  GLogAddData(data.gasProjectedExpansionPressure, 6);
  GLogAddData(data.gasProjectedPressureDifference, 6);
  GLogAddData(data.gasCalculatedPressureCompensation, 6);
  GLogAddData(data.gasAppliedPressureCompensation, 6);
  GLogAddData(data.gasHeadspaceUpdateStatus);
  GLogSend();
}
