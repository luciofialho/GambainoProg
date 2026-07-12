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
  const float bpm = getReliefsPerHourValue();

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
  char batchStr[4];
  snprintf(batchStr, sizeof(batchStr), "%03d", batchNum);

  if (!headerWritten) {
    GLogBegin(datalogFolderNameInUse, batchStr, "Cold");
    GLogAddTimeStamp();
    GLogAddData("FMT");
    GLogAddData("Temperature");
    GLogAddData("TempTarget");
    GLogAddData("Pressure");
    GLogAddData("PressureTarget");
    GLogAddData("TemperatureMode");
    GLogAddData("Volume");
    GLogAddData("SG");
    GLogAddData("Plato");
    GLogAddData("ABV");
    GLogAddData("ReliefCount");
    GLogAddData("CO2MolsEjected");
    GLogAddData("CO2InSolution");
    GLogAddData("HeadSpaceVolume");
    GLogAddData("CorrectionPlato");
    GLogAddData("SGAttenuation");
    GLogAddData("HeadSpaceCO2Mols");
    GLogAddData("ChillTime");
    GLogAddData("HeatTime");
    GLogAddData("taskWindowType");
    GLogAddData("Millis");
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
    GLogAddData(getTemperatureModeLabel());
    GLogAddData(beerVolume, 0);
    GLogAddData(beerSG,5);
    GLogAddData(beerPlato,3);
    GLogAddData(beerABV,2);
    GLogAddData(CountersData.totalReliefCount,0);
    GLogAddData(CountersData.totalMolsEjected,3);
    GLogAddData(CountersData.CO2InSolution,3);
    GLogAddData(CountersData.headSpaceVolume,3);
    GLogAddData(CountersData.correctionPlato,3);
    GLogAddData(CountersData.SGAttenuation,5);
    GLogAddData(headSpaceCO2Mols,3);
    GLogAddData(CountersData.totalChillTime/3600.,2);
    GLogAddData(CountersData.totalHeatTime /3600.,2);
    GLogAddData(taskWindowTypeToText(taskWindowType));
    GLogAddData(millis());
    GLogSend();
  }
}
