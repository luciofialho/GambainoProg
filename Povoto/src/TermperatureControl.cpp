#include <Arduino.h>
#include <math.h>
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
unsigned long int lastTargetChange = 1; // 1 is to avoid imediate target change at startup due to invalid initial value of lastTarget
unsigned long int lastModeChange = 1;
unsigned long int nextModeChange = 1;
bool millisOverflowWindow = false;

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
  lastModeChange = 1; // 1 is to avoid immediate mode change at startup due to invalid initial value
  nextModeChange = 1; // 1 is to avoid immediate mode change at startup due to invalid initial value
  interruptedCooling = false;
  interruptedHeating = false;
}

char *getTemperatureModeLabel() {
  static char modeIdle[] = "IDLE";
  static char modeChill[] = "CHILL";
  static char modeChillPaused[] = "CHILL (paused)";
  static char modeHeat[] = "HEAT";
  static char modeHeatPaused[] = "HEAT (paused)";

  if (mode == FMTCHILL)
    return modeChill;
  if (mode == FMTHEAT)
    return modeHeat;
  if (interruptedCooling)
    return modeChillPaused;
  if (interruptedHeating)
    return modeHeatPaused;
  return modeIdle;
}


// Quadratic interpolation in log(T), through 20, 10 and 0.5 C.
// The displayed 0 C row is anchored at 0.5 C for a continuous lower clamp.
static float coolingCycleMinutes(float temperature, float at20, float at10, float at0) {
  if (!isfinite(temperature) || temperature <= 0.5f) return at0;
  if (temperature >= 20.0f) return at20;
  const double x = log(double(temperature));
  const double x0 = log(0.5);
  const double x10 = log(10.0);
  const double x20 = log(20.0);
  const double minutes =
    at0 * (x - x10) * (x - x20) / ((x0 - x10) * (x0 - x20)) +
    at10 * (x - x0) * (x - x20) / ((x10 - x0) * (x10 - x20)) +
    at20 * (x - x0) * (x - x10) / ((x20 - x0) * (x20 - x10));
  // Arbitrary table values can overshoot between anchors. Keep durations
  // positive and within the same range accepted by the settings page.
  return float(fmax(0.01, fmin(1440.0, minutes)));
}

long int fermenterOnOffCycle(float temperature, byte targetMode) {
  long timeUnit = debugging ? 1000L : 60000L; 

  if (SetPointData.mode==MODE_BREWING_TRANSFERING) // brewing/transfering mode
    return 5*timeUnit;
  else if (SetPointData.mode==MODE_FERMENTING || SetPointData.mode==MODE_CONDITIONING) { // fermenting mode
    const long onTime = lround(coolingCycleMinutes(temperature,
      FMTData.coolingCycle[0].onMinutes, FMTData.coolingCycle[1].onMinutes,
      FMTData.coolingCycle[2].onMinutes) * timeUnit);
    const long offTime = lround(coolingCycleMinutes(temperature,
      FMTData.coolingCycle[0].offMinutes, FMTData.coolingCycle[1].offMinutes,
      FMTData.coolingCycle[2].offMinutes) * timeUnit);
    if (targetMode == FMTCHILL)
      return onTime;

    if (targetMode == FMTHEAT)
      return long(FMTData.heater.onMinutes * timeUnit);

    if (mode == FMTHEAT)
      return long(FMTData.heater.offMinutes * timeUnit);

    return offTime;
  }
  else
    return targetMode == FMTCHILL ? 0 : 24*60*60*1000L;
}

void temperatureControl() {
  static unsigned long int lastRun = 0;
  if (!(MILLISDIFF(lastRun,100))) 
    return;
  lastRun = millis();

  
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

  if (ControlData.temperature == NOTaTEMP || SetPointData.setPointTemp == NOTaTEMP) {
    newMode = FMTIDLE;
    interruptedCooling = false;
    interruptedHeating = false;
  }
  else {
    float minTarget = SetPointData.setPointTemp > 6 ? SetPointData.setPointTemp - FMTOFFSET : SetPointData.setPointTemp;
    float maxTarget = SetPointData.setPointTemp + FMTOFFSET;
    if ((mode==FMTCHILL) && millis()-lastModeChange > 5*60000L && ControlData.temperature > 8) /*** constantes ****/
      minTarget += 0.1; // reduce histeresis 


    // in slow temperature target transictions, accept twice the offset to postpone action
    if (SetPointData.setPointSlowTemp != NOTaTEMP && ControlData.temperature < SetPointData.setPointSlowTemp)
      maxTarget += FMTOFFSET; 
    if (SetPointData.setPointSlowTemp != NOTaTEMP && ControlData.temperature > SetPointData.setPointSlowTemp)
      minTarget -= FMTOFFSET;
    
    switch (mode) {
      case FMTIDLE:
        if (ControlData.temperature > maxTarget  || (ControlData.temperature>minTarget && interruptedCooling)) {
          if (!millisOverflowWindow && MILLISPAST(nextModeChange)) {
            newMode = FMTCHILL;
          }
        }
        else if (FMTData.heater.enabled && ControlData.temperature < minTarget - 2*FMTOFFSET) { // Lucio: melhorar isso
          if (lastModeChange==0 || (!millisOverflowWindow && MILLISPAST(nextModeChange))) // anti boucing Constante
            newMode = FMTHEAT; 
        }
      break;

      case FMTCHILL: 
        if (ControlData.temperature <= minTarget) {
          newMode = FMTIDLE;
          interruptedCooling = false;
        }
        else if ((!millisOverflowWindow) && (MILLISPAST(nextModeChange))) {
          newMode = FMTIDLE;
          interruptedCooling = true;
        }
        break;

      case FMTHEAT:
        if (ControlData.temperature >= SetPointData.setPointTemp) {
          newMode = FMTIDLE;
          interruptedHeating = false;
        }
        else if (!millisOverflowWindow && MILLISPAST(nextModeChange)) {
          newMode = FMTIDLE;
          interruptedHeating = true;
        }
        break;
    }
  }
    
  if (!FMTData.heater.enabled) {
    if (newMode == FMTHEAT) newMode = FMTIDLE;
    interruptedHeating = false;
  }

  if (newMode != mode) {
    ;Serial.print(millis()); Serial.print(": Mode change from "); Serial.print(mode==FMTIDLE ? "IDLE" : (mode==FMTCHILL ? "CHILL" : "HEAT")); Serial.print(" to "); Serial.println(newMode==FMTIDLE ? "IDLE" : (newMode==FMTCHILL ? "CHILL" : "HEAT"));
    if (newMode == FMTCHILL || newMode == FMTHEAT) {
      interruptedCooling = false;
      interruptedHeating = false;
    }

    lastModeChange = millis();
    mode = newMode;
    nextModeChange = millis() + fermenterOnOffCycle(ControlData.temperature,newMode);
    if (nextModeChange < lastModeChange)  // overflow in millis()
      millisOverflowWindow = true;
    else
      millisOverflowWindow = false;
  }

  if (mode == FMTIDLE) {
    if (interruptedCooling)
      ChillHeatMode = FMTCHILL;
    else if (interruptedHeating)
      ChillHeatMode = FMTHEAT;
    else
      ChillHeatMode = FMTIDLE;
  }
  else {
    ChillHeatMode = mode;
  }

  // interval control
  byte actualMode = newMode;
  long int actualMaxTime;
  long int offsetTime;

  switch (actualMode) {    
    case FMTCHILL:
      chillControl = millis()>30000; // only if it's been more than 30s since povoto is on, to avoid stressing chiller during maintenance
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
  else if (SetPointData.mode == MODE_OFF)
    chillControl = false; 
  if (ControlData.heaterOverride == 1)
    heatControl = true;
  else if (ControlData.heaterOverride == 2)
    heatControl = false;
  else if (SetPointData.mode == MODE_OFF)
    heatControl = false; 

  if (!FMTData.heater.enabled) {
    heatControl = false;
    // Disable immediately, even during the pressure-noise window.
    digitalWrite(PINHEATER, LOW);
    digitalWrite(PINLEDHEATER, LOW);
  }

  updateCountersTimes(chillControl, heatControl);

  
  if (!inPressureNoiseWindow()) {
    digitalWrite(PINCHILLER, chillControl);
    digitalWrite(PINHEATER, heatControl);

    if (chillControl)
      digitalWrite(PINLEDCHILLER, HIGH);
    else if (interruptedCooling)
      digitalWrite(PINLEDCHILLER, (millis() % 500) < 50);
    else
      digitalWrite(PINLEDCHILLER, LOW);

    if (heatControl)
      digitalWrite(PINLEDHEATER, HIGH);
    else if (interruptedHeating)
      digitalWrite(PINLEDHEATER, (millis() % 500) < 50);
    else
      digitalWrite(PINLEDHEATER, LOW);
  }
}

char *getTemperatureControlStatus(char *st) {
  const float chillerOnMinutes = coolingCycleMinutes(
    ControlData.temperature,
    FMTData.coolingCycle[0].onMinutes, FMTData.coolingCycle[1].onMinutes,
    FMTData.coolingCycle[2].onMinutes);
  const float chillerOffMinutes = coolingCycleMinutes(
    ControlData.temperature,
    FMTData.coolingCycle[0].offMinutes, FMTData.coolingCycle[1].offMinutes,
    FMTData.coolingCycle[2].offMinutes);

  char buf[2048];  
    sprintf(buf, "<br>-------TEMPERATURE CONTROL<br>Dallas sensor: %s<br>Temperature: %.2f C<br>Environment Temp: %.2f C<br>Target temp: %.2f C<br>Mode: %s<br>Chill: %s<br>Interrupted cooling: %s<br>Heat: %s<br>Interrupted heating: %s<br>Total chill time: %ld s<br>Total heat time: %ld s<br>",
          (dallasTemperature == NOTaTEMP || dallasTemperature == 85) ? "NOT DETECTED" : "OK",
          ControlData.temperature,
          environmentTemp,
          SetPointData.setPointTemp,
          getTemperatureModeLabel(),
          chillControl ? "ON" : "OFF",
          interruptedCooling ? "YES" : "NO",
          heatControl ? "ON" : "OFF",
          interruptedHeating ? "YES" : "NO",
          CountersData.totalChillTime,
          CountersData.totalHeatTime);
  strnncat(st,buf,2048);

  snprintf(buf, sizeof(buf),
           "Calculated cycle times at %.2f C: Chiller on: %.2f min; Chiller off: %.2f min; Heater on: %.2f min; Heater off: %.2f min<br>",
           ControlData.temperature, chillerOnMinutes, chillerOffMinutes,
           FMTData.heater.onMinutes, FMTData.heater.offMinutes);
  strnncat(st, buf, 2048);

  //ionclua todas as variáveis declaradas no // FMT control variables
  sprintf(buf, "Last Target: %.2f C<br>Last Target Change: %lu<br>Last Mode Change: %lu (%ld sec. ago)<br>Next Mode Change: %lu (in %ld sec.)<br>",
          lastTarget,
          lastTargetChange, 
          lastModeChange, (long int) (millis() - lastModeChange)/1000,
          nextModeChange, (long int) (nextModeChange- millis())/1000);
  strnncat(st,buf,2048);
  return st;
}


