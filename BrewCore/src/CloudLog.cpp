#include "GambainoCommon.h"
#include <BrewCoreCommon.h>
#include "Variables.h"
#include "Status.h"
#include "IOTK_ESPAsyncServer.h"


float mode(bool chill, bool heat) {
  return chill ? 0 : (heat ? 2 : 1);
}

bool isValidTemp(float f) {
  return (f  != 85 && f!=NOTaTEMP);
}

void returnCloudLog(AsyncWebServerRequest * request) {
  char response[2000];  // Tamanho suficiente para cabeçalhos + JSON
  char line[100];
  bool toSend = false;

  Serial.println("passou");
  if (request->args()>=1 && request->argName(0)=="log") {
    int zero = 0;
    if (request->arg(zero)=="Brewfather_FMT1" || request->arg(zero)=="Brewfather_FMT2") {
      byte fmt = request->arg(zero)=="Brewfather_FMT1" ? 1 : 2;

      
      if (!debugging)
        if (fmt==1)
          strcpy(response,"{\"name\":\"Fmt1\",\"temp_unit\":\"C\",\"pressure_unit\":\"BAR\"");
        else
          strcpy(response,"{\"name\":\"Fmt2\",\"temp_unit\":\"C\",\"pressure_unit\":\"BAR\"");
      else
        if (fmt==1)
          strcpy(response,"{\"name\":\"Debfmt1\",\"temp_unit\":\"C\",\"pressure_unit\":\"BAR\"");
        else
          strcpy(response,"{\"name\":\"Debfmt2\",\"temp_unit\":\"C\",\"pressure_unit\":\"BAR\"");

      toSend = false;
      /*
      if (isValidTemp(Fermenter1Temp)) {
        sprintf(line,",\"temp\":%.1f",float(fmt==1 ? Fermenter1Temp : Fermenter2Temp)+ (!debugging ? 0 : millis()%10));
        strcat(response,line);
        toSend = true;        
      }
      if (isValidTemp(EnvironmentTemp)) {
        sprintf(line,",\"ext_temp\":%.1f",float(EnvironmentTemp) + (!debugging ? 0 : millis()%10));
        strcat(response,line);
      }
      if (Fermenter1Pressure>=0) {
        sprintf(line,",\"pressure\":%.1f",float(fmt==1 ? Fermenter1Pressure : Fermenter2Pressure));
        strcat(response,line);
        toSend = true;
      }
      if (int(FMT1BatchNum) > 0) {
        sprintf(line,",\"beer\":%d",fmt==1 ? int(FMT1BatchNum) : int(FMT2BatchNum));
        strcat(response,line);
      }
      strcat(response,"}");

      if (toSend) 
        request->send(200, "text/plain", response);
      else
        request->send(200, "text/plain", "");
    } // brewfather

    else if (request->arg(zero)=="Thingspeak") {
      response[0] = 0;
      
      toSend = false;
      /* 1 - Fermenter 1 Temp
      if (isValidTemp(Fermenter1Temp)) {
        sprintf(line,"field1=%.1f",float(Fermenter1Temp));
        strcat(response,line);
        toSend = true;        
      }

      // 2 - Fermenter 1 Target Temp
      if (isValidTemp(Fermenter1TargetTemp)) {
        if (toSend) strcat(response,"&");
        sprintf(line,"field2=%.1f",float(Fermenter1TargetTemp));
        strcat(response,line);
      }

      // 3 - Fermenter 1 Control
      if (toSend) 
        strcat(response,"&");
      sprintf(line,"field3=%d",mode(Fermenter1Control.asBoolean(), Fermenter1Heater.asBoolean()));
      strcat(response,line);

      // 4 - Fermenter 1 Pressure
      if (Fermenter1Pressure>=0) {
        sprintf(line,"&field4=%.1f",float(Fermenter1Pressure));
        strcat(response,line);
      }

      // 5 - Fermenter 2 Temp
      if (isValidTemp(Fermenter2Temp)) {
        sprintf(line,"&field5=%.1f",float(Fermenter2Temp));
        strcat(response,line);
      }

      // 6 - Fermenter 2 Target Temp
      if (isValidTemp(Fermenter2TargetTemp)) {
        sprintf(line,"&field6=%.1f",float(Fermenter2TargetTemp));
        strcat(response,line);
      }

      // 7 - Fermenter 2 Control
      sprintf(line,"&field7=%d",mode(Fermenter2Control.asBoolean(), Fermenter2Heater.asBoolean()));
      strcat(response,line);

      // 8 - Fermenter 2 Pressure
      if (Fermenter2Pressure>=0) {
        sprintf(line,"&field8=%.1f",float(Fermenter2Pressure));
        strcat(response,line);
      }
        */

      // API Key
      if (!debugging)
        strcat(response,"&api_key=B94D0TSVKGUZKVG6");
      else
        strcat(response,"&api_key=DBE6U66JY62EIPOB");

      request->send(200, "text/plain", response);
    } // thingspeak
    else
      request->send(200, "text/plain", "");
  }
}
