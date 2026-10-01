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
#include <IOTK_NTP.h>
#include <WiFi.h>

#define BREWFATHER_SEND_INTERVAL_MS 600000UL  // 10 minutos
#define BREWFATHER_RETRY_INTERVAL_MS 30000UL  // 15 segundos entre tentativas

static bool isValidTemp(float value) {
  return isfinite(value) && value != NOTaTEMP && value != 85;
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
  const float extTemp = isValidTemp(environmentTemp) ? environmentTemp : NAN;
  const float targetTemp = isValidTemp(SetPointData.setPointTemp) ? SetPointData.setPointTemp : NAN;
  const float gravity = (beerSG >= 0.0f && beerSG <= 1.1f) ? beerSG : NAN;
  const float pressure = (ControlData.pressure >= 0.0f && ControlData.pressure <= 3.0f) ? ControlData.pressure : NAN;
  const float bpm = getBeerCO2EvolutionGramsPerLiterPerDay();

  char tempField[40] = "";
  char extTempField[40] = "";
  char targetTempField[40] = "";
  char gravityField[24];
  char pressureField[24];
  char bpmField[40] = "";

  if (isfinite(temp)) snprintf(tempField, sizeof(tempField), ",\"temp\":%.2f", temp);
  if (isfinite(extTemp)) snprintf(extTempField, sizeof(extTempField), ",\"ext_temp\":%.2f", extTemp);
  if (isfinite(targetTemp))
    snprintf(targetTempField, sizeof(targetTempField), ",\"temp_target\":%.2f", targetTemp);

  if (isnan(gravity)) snprintf(gravityField, sizeof(gravityField), "null");
  else snprintf(gravityField, sizeof(gravityField), "%.5f", gravity);

  if (isnan(pressure)) snprintf(pressureField, sizeof(pressureField), "null");
  else snprintf(pressureField, sizeof(pressureField), "%.3f", pressure);

  // No rate during a pressure/temperature transition: the net CO2 released by the beer
  // would draw a false slowdown (docs/gco2-rate.md).
  if (isfinite(bpm) && bpm > 0.0f && !co2RateInTransition())
    snprintf(bpmField, sizeof(bpmField), ",\"bpm\":%.2f", bpm);

  int written = snprintf(
    out,
    outSize,
    "{\"name\":\"%s%d\",\"temp_unit\":\"C\",\"pressure_unit\":\"BAR\"%s%s%s,\"gravity\":%s,\"gravity_unit\":\"G\",\"pressure\":%s%s,\"beer\":\"%s\"}",
    debugging ? "Debfmt" : "Fmt",
    (int)FMTData.PovotoNum,
    tempField,
    extTempField,
    targetTempField,
    gravityField,
    pressureField,
    bpmField,
    batchNameEsc
  );

  return (written > 0 && (size_t)written < outSize);
}

// Same format as GLogAddTimeStamp (local time); empty when epoch is 0.
void formatLocalEpochISO(uint32_t epoch, char *out, size_t outSize) {
  if (epoch == 0) {
    out[0] = '\0';
    return;
  }
  unsigned long day;
  int8_t dayOfWeek, hours, minutes, seconds, dayOfMonth, month;
  int16_t year;
  convertFromEpoch(epoch, day, dayOfWeek, hours, minutes, seconds, dayOfMonth, month, year);
  snprintf(out, outSize, "%04d-%02d-%02dT%02d:%02d:%02d",
           year, month, dayOfMonth, hours, minutes, seconds);
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

static bool conditioningFinalColdPending = false;       // RAM only
static bool conditioningFinalBrewfatherPending = false;

void requestConditioningFinalRecord() {
  conditioningFinalColdPending = true;
  conditioningFinalBrewfatherPending = true;
}

void maybeSendBrewfatherLog() {
  static unsigned long lastSuccessfulSend = 0;
  static unsigned long lastAttemptTime = 0;

  if (SetPointData.mode == MODE_OFF) {
    return;
  }
  const bool finalPoint = SetPointData.mode == MODE_CONDITIONING;
  if (finalPoint && !conditioningFinalBrewfatherPending) {
    return;
  }
  if (BatchData.batchNumber == 0) {
    return;
  }
  if (isZeroMac(peerSideKick.mac)) {
    return;
  }

  unsigned long now = millis();
  
  // Se teve sucesso, aguarda 10 minutos para próximo envio (o ponto final não espera)
  if (!finalPoint && lastSuccessfulSend != 0 && (now - lastSuccessfulSend) < BREWFATHER_SEND_INTERVAL_MS) {
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
  if (finalPoint) conditioningFinalBrewfatherPending = false;
}

// Logs start only once WiFi and NTP have been up since the boot: before that
// ESP-NOW may be on a channel other than the SideKick's and the time stamp
// would be 1970 (batch 160: the Cold header, sent ~10 s after the boot, was
// lost at every boot from 29/09 on).
static bool logLinkReady() {
  static bool ready = false;
  if (!ready) ready = WiFi.status() == WL_CONNECTED && NTPEpoch() != 0;
  return ready;
}

// Sends a header prepared with GLogBegin/GLogAddData. It counts as written
// once the SideKick acknowledged all its chunks; otherwise it is sent again
// with the next row. After LOG_HEADER_MAX_ATTEMPTS it counts as written, so a
// link that never confirms does not repeat it before every row.
static constexpr unsigned long LOG_HEADER_ACK_TIMEOUT_MS = 300;
static constexpr uint8_t LOG_HEADER_MAX_ATTEMPTS = 5;
static bool sendLogHeader(const char *sheet, uint8_t &attempts) {
  GLogSend();
  const GLogDelivery delivery = GLogWaitDelivery(LOG_HEADER_ACK_TIMEOUT_MS);
  if (delivery == GLOG_DELIVERY_OK) {
    attempts = 0;
    return true;
  }
  ++attempts;
  Serial.printf("[LOG] %s header not acknowledged (%s), attempt %u/%u\n", sheet,
                delivery == GLOG_DELIVERY_FAILED ? "failed" : "timeout",
                (unsigned)attempts, (unsigned)LOG_HEADER_MAX_ATTEMPTS);
  if (attempts < LOG_HEADER_MAX_ATTEMPTS) return false;
  attempts = 0;
  return true;
}

void doDataLog() {
  if (!datalogFolderNameInUse[0] || !logLinkReady()) {
    return;
  }

  static bool headerWritten = false;
  static uint8_t headerAttempts = 0;
  static int lastBatchNum = -1;

  if (BatchData.batchNumber == 0) {
    headerWritten = false;
    return;
  }

  if (SetPointData.mode == MODE_OFF) {
    return;
  }
  if (SetPointData.mode == MODE_CONDITIONING && !conditioningFinalColdPending) {
    return;
  }

  int batchNum = (int)BatchData.batchNumber;
  if (batchNum != lastBatchNum) {
    lastBatchNum = batchNum;
    headerWritten = false;
    headerAttempts = 0;
  }
  char batchStr[6]; // All uint16_t batch numbers plus the terminator.
  snprintf(batchStr, sizeof(batchStr), "%03d", batchNum);

  if (!headerWritten) {
    GLogBegin(datalogFolderNameInUse, batchStr, "Cold");
    GLogAddTimeStamp();
    GLogAddData("FMT");
    GLogAddData("ReliefCount");
    GLogAddData("Temperature");
    GLogAddData("TempTarget");
    GLogAddData("Pressure");
    GLogAddData("PressureTarget");
    GLogAddData("gCO2/L/d");
    GLogAddData("HeadSpaceVolume");
    GLogAddData("BeerVolume");
    GLogAddData("DumpedVolume");
    GLogAddData("SG");
    GLogAddData("RE (Plato)");
    GLogAddData("ABV");
    GLogAddData("HeadSpaceCO2Mols");
    GLogAddData("CO2MolsSol");
    GLogAddData("CO2MolSolIfEq");
    GLogAddData("CO2MolsEjected");
    GLogAddData("PressureDropFactor");
    GLogAddData("TemperatureMode");
    GLogAddData("TempState");
    GLogAddData("TempStableSince");
    GLogAddData("PressState");
    GLogAddData("PressStableSince");
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
    GLogAddData("EnvironmentTemperature");
    // [DAILY-HS] 24-hour headspace average and the last dump (dump columns
    // are filled only in the first row after a dump).
    GLogAddData("HeadSpaceEMA");
    GLogAddData("HeadSpaceDaily");
    GLogAddData("DailyHours");
    GLogAddData("DailyState");
    GLogAddData("DumpP1");
    GLogAddData("DumpP2");
    GLogAddData("DumpStartMillis");
    GLogAddData("DumpEndMillis");
    GLogAddData("DumpDeltaH");
    GLogAddData("gCO2Source"); // "calculated", "held" (after a reboot) or "transition"
    GLogAddData("GasCO2Rate"); // gas-phase g/L/d of the dissolved-CO2 state (empty = no decision)

    headerWritten = sendLogHeader("Cold", headerAttempts);
  }
  // The row follows its header in the same call.
  {
    GLogBegin(datalogFolderNameInUse, batchStr, "Cold");
    GLogAddTimeStamp();
    GLogAddData(FMTData.PovotoNum);
    GLogAddData(CountersData.totalReliefCount,0);
    GLogAddData(ControlData.temperature, 2);
    GLogAddData(SetPointData.setPointTemp, 2);
    GLogAddData(ControlData.pressure, 6);
    GLogAddData(SetPointData.setPointPressure, 6);
    GLogAddData(getReportedCO2EvolutionGramsPerLiterPerDay(), 3); // source in gCO2Source
    GLogAddData(CountersData.headSpaceVolume, 3);
    GLogAddData(beerVolume, 3);
    GLogAddData(CountersData.dumpedVolume, 3);
    GLogAddData(beerSG,5);
    GLogAddData(SGToRealPlato(beerSG),3);
    GLogAddData(beerABV,2);
    GLogAddData(headSpaceCO2Mols,3);
    GLogAddData(CountersData.CO2InSolution,3);
    GLogAddData(CO2DissolvedMols(ControlData.pressure, beerSG, ControlData.temperature, beerVolume),3);
    GLogAddData(CountersData.totalMolsEjected,3);
    GLogAddData(pressureDropFactor,5);
    GLogAddData(getTemperatureModeLabel());
    GLogAddData(getTempStateLabel());
    char tempStableSinceText[20];
    formatLocalEpochISO(CountersData.tempStableSince, tempStableSinceText, sizeof(tempStableSinceText));
    GLogAddData(tempStableSinceText);
    GLogAddData(getPressStateLabel());
    char pressStableSinceText[20];
    formatLocalEpochISO(CountersData.pressStableSince, pressStableSinceText, sizeof(pressStableSinceText));
    GLogAddData(pressStableSinceText);
    GLogAddData(taskWindowTypeToText(taskWindowType));
    GLogAddData(CountersData.totalChillTime/3600.,2);
    GLogAddData(CountersData.totalHeatTime /3600.,2);
    GLogAddData(millis());
    GLogAddData(pressureOnReliefExtrap,6);
    GLogAddData(pressureAfterRelief,6);
    GLogAddData(pressureAfterReliefMillis);
    GLogAddData(adjustedPressureAfterRelief,6);
    GLogAddData(pressureReachedTarget,6);
    GLogAddData(pressureReachedTargetMillis);
    const DissolvedCO2LogData co2 = getDissolvedCO2LogData();
    GLogAddData(co2.mode);
    GLogAddData(co2.calculationPressure, 6);
    GLogAddData(co2.equilibriumMols, 6);
    GLogAddData(co2.criteriaState);
    GLogAddData(co2.criteriaElapsedMillis);
    GLogAddData(co2.confirmationMillis);
    GLogAddData(co2.withReliefsState);
    GLogAddData(co2.withReliefsElapsedMillis);
    GLogAddData(co2.withoutReliefsState);
    GLogAddData(co2.withoutReliefsElapsedMillis);
    GLogAddData(co2.previousPressure, 6);
    GLogAddData(co2.reliefIntervalSeconds, 3);
    GLogAddData(co2.sinceLastReliefSeconds, 3);
    GLogAddData(lastTaskMillis);
    GLogAddData((int)SetPointData.mode);
    GLogAddData(Patm, 6);
    GLogAddData(isValidTemp(environmentTemp) ? environmentTemp : NAN, 2);
    // [DAILY-HS]
    const DailyHeadspaceLogData dailyHs = getDailyHeadspaceLogData();
    GLogAddData(dailyHs.ema, 3);
    GLogAddData(dailyHs.daily, 3);
    GLogAddData((int)dailyHs.hours);
    GLogAddData(dailyHs.state);
    DumpLogData dump;
    if (takeDumpLogData(dump)) {
      GLogAddData(dump.pressureBeforeBar, 6);
      GLogAddData(dump.pressureAfterBar, 6);
      GLogAddData(dump.startMillis);
      GLogAddData(dump.endMillis);
      GLogAddData(dump.deltaH, 3);
    } else {
      for (int i = 0; i < 5; i++) GLogAddData("");
    }
    GLogAddData(getCO2EvolutionSource());
    GLogAddData(co2.gasRate, 3);
    GLogSend();
    if (SetPointData.mode == MODE_CONDITIONING) conditioningFinalColdPending = false;
  }
}

void doReliefDataLog(const ReliefLogData &data) {
  if (!datalogFolderNameInUse[0] || BatchData.batchNumber == 0 ||
      SetPointData.mode == MODE_CONDITIONING || !logLinkReady()) {
    return;
  }

  static bool headerWritten = false;
  static uint8_t headerAttempts = 0;
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
    headerAttempts = 0;
  }

  char batchStr[6]; // All uint16_t batch numbers plus the terminator.
  snprintf(batchStr, sizeof(batchStr), "%03d", batchNum);

  if (!headerWritten) {
    GLogBegin(datalogFolderNameInUse, batchStr, "Relief");
    GLogAddTimeStamp();
    GLogAddData("FMT");
    GLogAddData("ReliefNumber");
    GLogAddData("ValveOpenedMillis");
    GLogAddData("Temperature");
    GLogAddData("PressureTarget");
    GLogAddData("AtmosphericPressure");
    GLogAddData("EnvironmentTemperature");
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
    GLogAddData("ExpansionTankResidualMoles"); // Previous cycle, at this relief's opening.
    GLogAddData("EjectedMolsBeforeLiquidCorrectionAtLog");
    GLogAddData("InstantPressureDropFactor");
    GLogAddData("PressureDropFactor");
    GLogAddData("HeadSpaceVolume");
    GLogAddData("BeerVolume");
    GLogAddData("EjectedMolsAtLog");
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
    GLogAddData("PolytropicEstimatedExponent");
    GLogAddData("PolytropicSampleCount");
    GLogAddData("PolytropicFitRMSEBar");
    GLogAddData("ResidualFromReliefNumber");
    GLogAddData("PreviousTankPressureAtCloseBar");
    GLogAddData("PreviousTankMolesAtClose");
    GLogAddData("PreviousTankPressureAtOpenBar");
    GLogAddData("PreviousTankEjectedMolesBeforeLiquidCorrection");
    GLogAddData("PreviousTankEjectedMoles");
    GLogAddData("VentingElapsedAtLogSeconds");
    // [DAILY-HS] HeadSpaceVolume above is the applied value.
    GLogAddData("HeadSpaceMeasured");
    GLogAddData("HeadSpaceEMA");
    GLogAddData("HeadSpaceDaily");
    GLogAddData("DailyHours");
    GLogAddData("DailyState");
    headerWritten = sendLogHeader("Relief", headerAttempts);
  }

  // Send the data row separately so the first relief is logged as well.
  GLogBegin(datalogFolderNameInUse, batchStr, "Relief");
  GLogAddTimeStamp();
  GLogAddData(data.povotoNumber);
  GLogAddData(data.reliefNumber);
  GLogAddData(data.valveOpenedMillis);
  GLogAddData(data.temperature, 2);
  GLogAddData(data.targetPressure, 6);
  GLogAddData(data.atmosphericPressure, 6);
  GLogAddData(isValidTemp(data.environmentTemperature) ? data.environmentTemperature : NAN, 2);
  GLogAddData(data.reliefVolume, 3);
  GLogAddData(data.effectiveVentingExponent, 3);
  GLogAddData(data.pressureOnReliefMeasured, 6);
  GLogAddData(data.currentOnReliefMeasured, 3);
  GLogAddData(data.pressureReachedTarget, 6);
  GLogAddData(data.pressureReachedTargetMillis);
  GLogAddData(data.pressureOnReliefExtrapolated, 6);
  GLogAddData(data.pressureAfterRelief, 6);
  GLogAddData(data.pressureAfterReliefMillis);
  GLogAddData(data.currentAfterRelief, 3);
  GLogAddData(data.adjustedPressureAfterRelief, 6);
  GLogAddData(data.adjustedEquilibriumPressure, 6);
  GLogAddData(data.ejectedPressure, 6);
  GLogAddData(data.liquidMassInGasVentingPercent, 3);
  GLogAddData(data.expansionTankResidualMoles, 9);
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
  GLogAddData(data.polytropicEstimatedExponent, 6);
  GLogAddData(data.polytropicSampleCount);
  GLogAddData(data.polytropicFitRMSEBar, 6);
  if (data.previousTankMolesAtClose >= 0.0f && isfinite(data.previousTankMolesAtClose))
    GLogAddData(data.previousReliefNumber);
  else
    GLogAddData("");
  GLogAddData(data.previousTankPressureAtCloseBar, 6);
  GLogAddData(data.previousTankMolesAtClose, 8);
  GLogAddData(data.previousTankPressureAtOpenBar, 9);
  GLogAddData(data.previousTankEjectedMolesBeforeLiquidCorrection, 8);
  GLogAddData(data.previousTankEjectedMoles, 8);
  GLogAddData(data.ventingElapsedAtLogSeconds, 3);
  // [DAILY-HS]
  GLogAddData(data.headSpaceMeasured, 3);
  GLogAddData(data.headSpaceEMA, 3);
  GLogAddData(data.headSpaceDaily, 3);
  GLogAddData((int)data.dailyHours);
  GLogAddData(data.dailyState ? data.dailyState : "");
  GLogSend();
}

// ===== [DIAG] log da recuperação pós-relief + headspace sombra =====
void doRecoveryDataLog(unsigned long reliefNumber, float p1, float p1Extrap,
                       float envTemp, float beerTemp, float openSeconds,
                       float shadowExponent, float shadowHsInstant, float shadowHsFiltered,
                       uint8_t points, const unsigned long *ms, const float *pressure) {
  if (!datalogFolderNameInUse[0] || BatchData.batchNumber == 0 ||
      SetPointData.mode == MODE_CONDITIONING || !logLinkReady()) return;

  static bool headerWritten = false;
  static uint8_t headerAttempts = 0;
  static int lastBatchNum = -1;
  const int batchNum = (int)BatchData.batchNumber;
  if (batchNum != lastBatchNum) { lastBatchNum = batchNum; headerWritten = false; headerAttempts = 0; }

  char batchStr[6];
  snprintf(batchStr, sizeof(batchStr), "%03d", batchNum);

  if (!headerWritten) {
    GLogBegin(datalogFolderNameInUse, batchStr, "Recovery");
    GLogAddTimeStamp();
    GLogAddData("FMT");
    GLogAddData("ReliefNumber");
    GLogAddData("PressureOnRelief");
    GLogAddData("PressureOnReliefExtrapolated");
    GLogAddData("EnvironmentTemperature");
    GLogAddData("Temperature");
    GLogAddData("OpenSeconds");
    GLogAddData("ShadowExponent");
    GLogAddData("ShadowHeadspaceInstant");
    GLogAddData("ShadowHeadspaceFiltered");
    char name[16];
    for (uint8_t i = 0; i < points; ++i) {
      snprintf(name, sizeof(name), "t%u_ms", (unsigned)i);  GLogAddData(name);
      snprintf(name, sizeof(name), "p%u", (unsigned)i);     GLogAddData(name);
    }
    headerWritten = sendLogHeader("Recovery", headerAttempts);
  }

  GLogBegin(datalogFolderNameInUse, batchStr, "Recovery");
  GLogAddTimeStamp();
  GLogAddData(FMTData.PovotoNum);
  GLogAddData(reliefNumber);
  GLogAddData(p1, 6);
  GLogAddData(p1Extrap, 6);
  GLogAddData(isValidTemp(envTemp) ? envTemp : NAN, 2);
  GLogAddData(beerTemp, 2);
  GLogAddData(openSeconds, 3);
  GLogAddData(shadowExponent, 4);
  GLogAddData(shadowHsInstant, 3);
  GLogAddData(shadowHsFiltered, 3);
  for (uint8_t i = 0; i < points; ++i) {
    GLogAddData(ms[i]);
    GLogAddData(pressure[i], 6);
  }
  GLogSend();
}
