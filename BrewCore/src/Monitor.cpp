#include <IOTK.h>
#include <IOTK_ESPAsyncServer.h>
#include "GambainoCommon.h"
#include <BrewCoreCommon.h>
#include <Variables.h>

extern const int BIGBUFFERSIZE;
extern char bigBuffer[];

void speed(AsyncWebServerRequest *request) {
  char buf[40];
  strcpy(bigBuffer,"Water inlet: ");
  if (WaterInFlow>0) {
    sprintf(buf,"%.2f L/min",WaterInFlow);
    strcat(bigBuffer,buf);
    if (HLTWaterIn.asBoolean() && HLTTargetVolume > HLTVolume){
      sprintf(buf," --- time remaining: %.2f min", (HLTTargetVolume-HLTVolume)/WaterInFlow);
      strcat(bigBuffer,buf);
    }
    else if (WaterInTarget > 0) {
      sprintf(buf," --- time remaining: %.2f min", WaterInTarget/WaterInFlow);
      strcat(bigBuffer,buf);
    }
    strcat(bigBuffer,"<BR>");
  }
  else
    strcat(bigBuffer,"   no flow <BR>");

  strcat(bigBuffer,"<BR>HLT out flow: ");
  if (HLT2MLTFlow>0) {
    sprintf(buf,"%.2f L/min", HLT2MLTFlow);
    strcat(bigBuffer,buf);
    if (HLTTargetVolume < HLTVolume && HLTValve.asBoolean() && HLTPump.asBoolean()) {
      sprintf(buf," --- time remaining: %.2f min", (HLTVolume-HLTTargetVolume)/HLT2MLTFlow);
      strcat(bigBuffer,buf);
    }
    strcat(bigBuffer,"<BR>");
  }
  else
    strcat(bigBuffer,"   no flow<BR>");


  strcat(bigBuffer,"<BR>FMT lineflow: ");
  if (TransferFlow>0) {
    sprintf(buf,"%.2f L/min <BR>", TransferFlow);
    strcat(bigBuffer,buf);
  }
  else
    strcat(bigBuffer,"   no flow <BR>");

  request->send(200,"text/html",bigBuffer);
}



