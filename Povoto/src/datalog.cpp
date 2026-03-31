#include <Arduino.h>
#include <IOTK_GLog.h>
#include "GambainoCommon.h"
#include "PovotoCommon.h"
#include "PovotoData.h"
#include "TemperatureControl.h"
#include "datalog.h"
#include "PressureControl.h"

static const char *getModeLabel() {
  if (ChillHeatMode == FMTCHILL && interruptedCooling) {
    return "chill (paused)";
  }
  switch (ChillHeatMode) {
    case FMTCHILL:
      return "chill";
    case FMTHEAT:
      return "heat";
    case FMTIDLE:
    default:
      return "idle";
  }
}

void doDataLog() {
  if (!datalogFolderNameInUse[0]) {
    return;
  }

  static bool headerWritten = false;
  static int lastBatchNum = -1;

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
    GLogAddData("Volume");
    GLogAddData("SG");
    GLogAddData("Plato");
    GLogAddData("ABV");
    GLogAddData("ReliefCount");
    GLogAddData("CO2MolsEjected");
    GLogAddData("CO2MolsProduced");
    GLogAddData("ChillTime");
    GLogAddData("HeatTime");
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
    GLogAddData(beerVolume, 0);
    GLogAddData(beerSG,5);
    GLogAddData(beerPlato,3);
    GLogAddData(beerABV,2);
    GLogAddData(CountersData.totalReliefCount,0);
    GLogAddData(CountersData.totalMolsEjected,3);
    GLogAddData(CountersData.totalCO2MolsProduced,3);
    GLogAddData(CountersData.totalChillTime/3600.,2);
    GLogAddData(CountersData.totalHeatTime /3600.,2);
    GLogSend();
  }
}
