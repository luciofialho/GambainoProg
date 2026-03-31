#include <Arduino.h>
#include <PovotoCommon.h>
#include <PovotoData.h>
#include <IOTK.h>
#include <IOTK_Dallas.h>
#include <IOTK_NTP.h>
#include "GambainoCommon.h"
#include "PressureControl.h"



// sensor readigs
float dallasTemperature = 0;
float environmentTemp = 25;
bool  debugTemperatureOverride = false;  // when true, ControlData.temperature is not overwritten by Dallas

// FMT control variables
byte  ChillHeatMode = FMTIDLE;
float lastTarget = -999;
unsigned long int lastTargetChange = 0;
unsigned long int lastModeChange = 0;
unsigned long int nextModeChange = 0;
byte mode = FMTIDLE;
bool interruptedCooling = false;
bool interruptedHeating = false;

bool chillControl = false;
bool heatControl = false;

unsigned long int holdPressureDueToTemperatureRelays() {
  if (millis()-lastModeChange > 1000L)
    return 0;
  else 
    return (1000L - (millis() - lastModeChange));
}

void resetChillHeatCycle() {
  mode = FMTIDLE;
  ChillHeatMode = FMTIDLE;
  lastModeChange = 0;
  nextModeChange = 0;
  interruptedCooling = false;
  interruptedHeating = false;
}


long int fermenterOnOffCycle(float temperature,bool mode) {
  long timeUnit = debugging ? 1000L : 60000L; // mudar para debugar

  if (SetPointData.mode==2) // brewing/transfering mode
    return 5*timeUnit;
  else if (SetPointData.mode==1) { // fermenting mode
    long int onTime, offTime;


    long int minOnTime = FMTData.FMTMinActiveTime * timeUnit;
    long int maxOnTime = FMTData.FMTMaxActiveTime * timeUnit;
    long int minOffTime = FMTData.FMTMinOffTime * timeUnit;

    onTime = long((12.5*log(temperature)-9.04) * timeUnit); // Todo3: simplificar modelo de tempo

    if (onTime>maxOnTime)
      onTime = maxOnTime;
    else if (onTime<minOnTime)
      onTime = minOnTime;

    if (onTime > 10*timeUnit)
      offTime = 10*timeUnit;
    else
      offTime = minOffTime;

    if (mode)
      return onTime;
    else
      return offTime;
  }
  else
    return mode ? 0 : 24*60*60*1000L;
}

void temperatureControl() {
  static unsigned long int lastRun = 0;
  if (!(MILLISDIFF(lastRun,100))) 
    return;

  
  byte newMode = FMTIDLE;  
  bool needsToWriteNIV = false;

  static unsigned long int lastNoTemp = 0;

  if (dallasTemperature == NOTaTEMP || dallasTemperature == 85) {
    if (debugging) {
      if (!debugTemperatureOverride)
        ControlData.temperature = 18;
    }
    else {
      if (lastNoTemp == 0)
        lastNoTemp = millis();
      else if (MILLISDIFF(lastNoTemp,60000L) || millis()<60000L)  // time to ignore invalid temperature
        ControlData.temperature = NOTaTEMP;
    }
  }
  else {
    if (!debugTemperatureOverride)
      ControlData.temperature = dallasTemperature;
    lastNoTemp = 0;
  }

  if (SetPointData.setPointTemp == 85) 
    SetPointData.setPointTemp = NOTaTEMP;
  if (SetPointData.setPointSlowTemp == 85) 
    SetPointData.setPointSlowTemp = NOTaTEMP;

  if (lastTarget != -999) { // avoid entering in the first time it is called, so we keep setting after restart
    if (SetPointData.setPointTemp != lastTarget && SetPointData.setPointSlowTemp != NOTaTEMP) { //if Target is manually set
      SetPointData.setPointSlowTemp = NOTaTEMP; // reset slowTarget 
      lastModeChange = 0;
    }
  }


  // Process slow target changes
  if (SetPointData.setPointSlowTemp != NOTaTEMP) {
    if (lastTargetChange==0)
      lastTargetChange = millis();
    else {
      if (MILLISDIFF(lastTargetChange,FMTSLOWINCREMENTTIME)) {
        if (SetPointData.setPointSlowTemp<SetPointData.setPointTemp) {
          if (SetPointData.setPointSlowTemp < SetPointData.setPointTemp-0.1)
            SetPointData.setPointTemp = SetPointData.setPointTemp-0.1;
          else {
            SetPointData.setPointTemp = SetPointData.setPointSlowTemp;
            SetPointData.setPointSlowTemp = NOTaTEMP;
          }
          lastTargetChange = millis();
        }
        else if (SetPointData.setPointSlowTemp > SetPointData.setPointTemp) {
          if (SetPointData.setPointSlowTemp > SetPointData.setPointTemp+0.1)
            SetPointData.setPointTemp = SetPointData.setPointTemp + 0.1;
          else {
            SetPointData.setPointTemp = SetPointData.setPointSlowTemp;
            SetPointData.setPointSlowTemp = NOTaTEMP;
          }
          lastTargetChange = millis();
        }

        needsToWriteNIV = true;
      }
    }
  }
  else
    lastTargetChange = 0;


  newMode = mode;

  if (ControlData.temperature == NOTaTEMP || SetPointData.setPointTemp == NOTaTEMP) 
    newMode = FMTIDLE;
  else {
    float minTarget = SetPointData.setPointTemp;
    float maxTarget = SetPointData.setPointTemp + FMTOFFSET;
    if ((mode==FMTCHILL) && millis()-lastModeChange > 2*60000L && ControlData.temperature > 8) /*** constantes ****/
      minTarget += 0.1; // reduce histeresis after turning off
    if (ControlData.temperature < 4) {
      minTarget -= FMTOFFSET; 
      //maxTarget += FMTOFFSET;
    }
    else {
      if (SetPointData.setPointSlowTemp != NOTaTEMP && ControlData.temperature < SetPointData.setPointSlowTemp)
        maxTarget += FMTOFFSET; // accept twice the offset to postpone action, due to long term target
      if (SetPointData.setPointSlowTemp != NOTaTEMP && ControlData.temperature > SetPointData.setPointSlowTemp)
        minTarget -= FMTOFFSET;
    }

    switch (mode) {
      case FMTIDLE:
        if (ControlData.temperature > maxTarget  || (ControlData.temperature>minTarget && interruptedCooling)) {
          if (millis() > nextModeChange)
            newMode = FMTCHILL;
        }
        else if (ControlData.temperature < minTarget) {
          if (lastModeChange==0 || millis()>nextModeChange) // anti boucing Constante
            newMode = FMTHEAT; 
        }
      break;

      case FMTCHILL: 
        if (ControlData.temperature <= minTarget) {
          newMode = FMTIDLE;
          interruptedCooling = false;
        }
        else if (millis()>nextModeChange) {
          newMode = FMTIDLE;
          interruptedCooling = true;
        }
        break;

      case FMTHEAT:
        if (ControlData.temperature >= SetPointData.setPointTemp)
          newMode = FMTIDLE;
        else if (millis()>nextModeChange) {
          newMode = FMTIDLE;
          interruptedHeating = true;
        }
        break;
    }
  }
    
  if (newMode != mode) {
    lastModeChange = millis();
    mode = newMode;
    nextModeChange = millis() + fermenterOnOffCycle(ControlData.temperature,newMode==FMTCHILL);
  }

  // interval control
  byte actualMode = newMode;
  long int actualMaxTime;
  long int offsetTime;

  switch (actualMode) {    
    case FMTCHILL:
      chillControl = millis()>30000;
      heatControl = false;
    break;

    case FMTHEAT:
      chillControl = false;
      heatControl = true;
      break;
  
    case FMTIDLE:
    default:
      chillControl = false;
      heatControl = false;
      break;
  }

  if (needsToWriteNIV) {
    writeSetPointDataToNIV();
  }

  if (ControlData.chillerOverride == 1)
    chillControl = true;
  else if (ControlData.chillerOverride == 2)
    chillControl = false;

  if (ControlData.heaterOverride == 1)
    heatControl = true;
  else if (ControlData.heaterOverride == 2)
    heatControl = false;

  updateCountersTimes(chillControl, heatControl);

  if (!inPressureNoiseWindow()) {
    digitalWrite(PINCHILLER, chillControl);
    digitalWrite(PINHEATER, heatControl);
    digitalWrite(PINTRANSFERVALVE, ControlData.transferOverride==1 ? HIGH : (ControlData.transferOverride==2 ? LOW : ControlData.transferValve ? HIGH : LOW));
    digitalWrite(PINRELIEFVALVE, ControlData.reliefOverride==1 ? HIGH : (ControlData.reliefOverride==2 ? LOW : ControlData.reliefValve ? HIGH : LOW));
  }

  digitalWrite(PINLED, (millis() % 2000) > 500 && (millis() % 2000) < 550);

}

char *getTemperatureControlStatus(char *st) {
  char buf[2048];  
    sprintf(buf, "<br>-------<br>Temperature: %.2f C<br>Environment Temp: %.2f C<br>Target: %.2f C<br>Mode: %s<br>Chill: %s<br>Heat: %s<br>Total chill time: %ld s<br>Total heat time: %ld s<br>", 
          ControlData.temperature,
          environmentTemp,
          SetPointData.setPointTemp,
          mode==FMTIDLE ? "IDLE" : (mode==FMTCHILL ? "CHILL" : "HEAT"),
          chillControl ? "ON" : "OFF",
      heatControl ? "ON" : "OFF",
      CountersData.totalChillTime,
      CountersData.totalHeatTime);
  strnncat(st,buf,2048);

  //ionclua todas as variáveis declaradas no // FMT control variables
  sprintf(buf, "Last Target: %.2f C<br>Last Target Change: %lu<br>Last Mode Change: %lu (%ld sec. ago)<br>Next Mode Change: %lu (in %ld sec.)<br>Interrupted Cooling: %s<br>",
          lastTarget,
          lastTargetChange, 
          lastModeChange, (long int) (millis() - lastModeChange)/1000,
          nextModeChange, (long int) (nextModeChange- millis())/1000,
          interruptedCooling ? "YES" : "NO");
  strnncat(st,buf,2048);
  return st;
}


