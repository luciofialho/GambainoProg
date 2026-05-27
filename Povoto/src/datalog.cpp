#include <Arduino.h>
#include <IOTK_GLog.h>
#include "GambainoCommon.h"
#include "PovotoCommon.h"
#include "PovotoData.h"
#include "TemperatureControl.h"
#include "datalog.h"
#include "PressureControl.h"
#include "PovotoTasks.h"

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
    GLogAddData(taskWindowType);
    GLogAddData(millis());
    GLogSend();
  }
}
