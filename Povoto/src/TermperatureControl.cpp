#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include <PovotoCommon.h>
#include <PovotoData.h>
#include <IOTK.h>
#include <IOTK_Dallas.h>
#include <IOTK_NTP.h>
#include "GambainoCommon.h"
#include "PressureControl.h"
#include "DallasSensors.h"



// sensor readigs
float dallasTemperature = NOTaTEMP; // invalid until the first reading (never 0 C)
// Environment: its own Dallas when assigned, else the temperature broadcast
// by ESP-NOW: the SideKick's (its own Dallas) before the BrewCore's
// (docs/thermometers.md).
float environmentTemp = NOTaTEMP;
static constexpr unsigned long ENVIRONMENT_TEMP_TIMEOUT_MS = 10UL * 60UL * 1000UL;
// Broadcast environment temperature, one slot per sender kind.
struct ReceivedEnvironmentTemp {
  float value;
  unsigned long millisStamp;
  bool valid;
};
static ReceivedEnvironmentTemp receivedEnvironment[2] = {{NOTaTEMP, 0, false}, {NOTaTEMP, 0, false}};
static constexpr uint8_t ENV_FROM_SIDEKICK = 0;
static constexpr uint8_t ENV_FROM_OTHER = 1; // the BrewCore
static uint8_t environmentReceivedFrom = ENV_FROM_SIDEKICK;
bool  debugTemperatureOverride = false;  // when true, ControlData.temperature is not overwritten by Dallas

static void updateEnvironmentTemperatures() {
  environmentReceivedFrom = receivedEnvironment[ENV_FROM_SIDEKICK].valid ? ENV_FROM_SIDEKICK : ENV_FROM_OTHER;
  const float received = receivedEnvironment[environmentReceivedFrom].valid
      ? receivedEnvironment[environmentReceivedFrom].value : NOTaTEMP;
  environmentTemp = dallasRoleAssigned(DALLAS_ROLE_ENVIRONMENT)
      ? dallasRoleTemperature(DALLAS_ROLE_ENVIRONMENT) : received;
}

const char *environmentTempSource() {
  if (dallasRoleAssigned(DALLAS_ROLE_ENVIRONMENT)) return "Dallas";
  if (!receivedEnvironment[environmentReceivedFrom].valid) return "none received";
  return environmentReceivedFrom == ENV_FROM_SIDEKICK ? "SideKick" : "BrewCore";
}

// Headspace thermometer when assigned and with a valid reading (held 60 s),
// else the fermenter's. No configuration key: the assignment decides. It never
// controls the temperature (docs/thermometers.md).
static bool headspaceTemperatureValid() {
  if (!dallasRoleAssigned(DALLAS_ROLE_HEADSPACE)) return false;
  const float t = dallasRoleTemperature(DALLAS_ROLE_HEADSPACE);
  return isfinite(t) && t != NOTaTEMP && t != 85.0f;
}

float gasTemperature() {
  return headspaceTemperatureValid() ? dallasRoleTemperature(DALLAS_ROLE_HEADSPACE) : ControlData.temperature;
}

const char *gasTemperatureSource() {
  return headspaceTemperatureValid() ? "headspace" : "liquid";
}

float headspaceTemperature() {
  return headspaceTemperatureValid() ? dallasRoleTemperature(DALLAS_ROLE_HEADSPACE) : NAN;
}

// ESP-NOW callback: stored here, applied by temperatureControl() in the loop.
void setEnvironmentTemperatureFromPacket(const char *payload, bool fromSideKick) {
  ReceivedEnvironmentTemp &slot = receivedEnvironment[fromSideKick ? ENV_FROM_SIDEKICK : ENV_FROM_OTHER];
  char *end = nullptr;
  const float received = payload ? strtof(payload, &end) : NAN;
  if (!payload || end == payload || *end != '\0' ||
      !isfinite(received) || received == NOTaTEMP || received == 85.0f) {
    slot.value = NOTaTEMP;
    slot.valid = false;
    return;
  }
  slot.value = received;
  slot.millisStamp = millis();
  slot.valid = true;
}

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

static unsigned long slowTargetIncrementIntervalMs(float speedPerDay, float increment) {
  if (!isfinite(speedPerDay) || speedPerDay <= 0.0f) return ULONG_MAX;
  const double interval = 86400000.0 * increment / speedPerDay;
  return static_cast<unsigned long>(fmax(1000.0, fmin(interval, double(ULONG_MAX))));
}

// ===== Temperature stability =====
// The final target is the slow target while a ramp is active; ramp steps and
// ramp completion keep it unchanged, so only real set point changes restart
// the cycle, whatever their origin (web page, touch, ESP-NOW, rules).
// STABLE within FMTOFFSET of the target; lost (UNSTABLE) beyond 3*FMTOFFSET.
static bool stabilityTargetKnown = false;
static float stabilityLastTarget = NOTaTEMP;

static float temperatureFinalTarget() {
  return SetPointData.setPointSlowTemp != NOTaTEMP
      ? SetPointData.setPointSlowTemp : SetPointData.setPointTemp;
}

void markTemperatureSetpointChanged(bool slow) {
  CountersData.tempState = slow ? TEMP_STATE_CHANGING_SLOW : TEMP_STATE_CHANGING_DIRECT;
  CountersData.tempStableSince = 0;
  stabilityLastTarget = temperatureFinalTarget();
  stabilityTargetKnown = true;
  writeTempStabilityToNIV();
}

const char *getTempStateLabel() {
  switch (CountersData.tempState) {
    case TEMP_STATE_STABLE:          return "STABLE";
    case TEMP_STATE_CHANGING_DIRECT: return "CHANGING_DIRECT";
    case TEMP_STATE_CHANGING_SLOW:   return "CHANGING_SLOW";
    case TEMP_STATE_UNSTABLE:        return "UNSTABLE";
    default:                         return "";
  }
}

static void updateTemperatureStability() {
  const float target = temperatureFinalTarget();
  if (!stabilityTargetKnown) {
    // After boot, the state restored from counters refers to this target.
    stabilityLastTarget = target;
    stabilityTargetKnown = true;
  }
  else if (target != stabilityLastTarget) {
    markTemperatureSetpointChanged(SetPointData.setPointSlowTemp != NOTaTEMP);
  }

  if (ControlData.temperature == NOTaTEMP || target == NOTaTEMP) return;
  const float deviation = fabsf(ControlData.temperature - target);
  if (CountersData.tempState == TEMP_STATE_STABLE) {
    // Leaving the tolerance band loses stability; it must be reached again.
    if (deviation > 3 * FMTOFFSET) {
      CountersData.tempState = TEMP_STATE_UNSTABLE;
      CountersData.tempStableSince = 0;
      writeTempStabilityToNIV();
    }
    return;
  }
  if (deviation > FMTOFFSET) return;

  // Stays pending until NTP is valid; the time recorded is when it was stamped.
  static unsigned long lastNTPCheck = 0;
  if (!MILLISDIFF(lastNTPCheck, 1000)) return;
  lastNTPCheck = millis();
  const unsigned long now = NTPEpoch();
  if (now == 0) return;
  CountersData.tempState = TEMP_STATE_STABLE;
  CountersData.tempStableSince = now;
  writeTempStabilityToNIV();
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

// Fermenting or conditioning with a temperature target and no fermenter
// thermometer (not assigned, or no valid reading for 60 s): the control is
// idle. Signalled by PovotoAlarms.cpp (docs/alarms.md). In debug mode it
// follows the considered temperature: 85 or -127 (debug page) trigger it.
static bool thermometerAlarmActive = false;

bool fermenterThermometerAlarmActive() {
  return thermometerAlarmActive;
}

// Alarm e-mail (PovotoAlarms.cpp).
String describeFermenterThermometer() {
  char id[DALLAS_ADDRESS_TEXT_SIZE];
  formatDallasAddress(FMTData.dallasAddress[DALLAS_ROLE_FERMENTER], id, sizeof(id));
  char text[256];
  snprintf(text, sizeof(text),
           "Fermenter thermometer: %s%s\nDallas reading: %.2f C\nConsidered temperature: %.2f C (target %.2f C)\n"
           "Temperature control: idle while the thermometer is missing\n",
           id[0] ? id : "NOT ASSIGNED",
           id[0] && !dallasOnBus(FMTData.dallasAddress[DALLAS_ROLE_FERMENTER]) ? " (not found in last scan)" : "",
           dallasTemperature, ControlData.temperature, SetPointData.setPointTemp);
  return String(text);
}

static void fermenterThermometerAlarm() {
  static constexpr unsigned long INVALID_TOLERANCE_MS = 60000UL;
  static unsigned long invalidSince = 0; // 0 = valid reading
  const unsigned long now = millis();
  // Debug mode: only the considered temperature counts (the debug page value
  // or the fake 18 C), so the alarm is tested by setting 85 or -127 there.
  const float t = debugging ? ControlData.temperature : dallasTemperature;
  const bool valid = isfinite(t) && t != NOTaTEMP && t != 85;
  if (valid) invalidSince = 0;
  else if (invalidSince == 0) invalidSince = now | 1;
  const bool missing = (!debugging && !dallasRoleAssigned(DALLAS_ROLE_FERMENTER)) ||
      (invalidSince != 0 && now - invalidSince >= INVALID_TOLERANCE_MS);
  const bool controlling = (SetPointData.mode == MODE_FERMENTING || SetPointData.mode == MODE_CONDITIONING) &&
      SetPointData.setPointTemp != NOTaTEMP;
  thermometerAlarmActive = controlling && missing;
}

void temperatureControl() {
  static unsigned long int lastRun = 0;
  if (!(MILLISDIFF(lastRun,100))) 
    return;
  lastRun = millis();

  for (ReceivedEnvironmentTemp &slot : receivedEnvironment) {
    if (slot.valid && (unsigned long)(lastRun - slot.millisStamp) >= ENVIRONMENT_TEMP_TIMEOUT_MS) {
      slot.value = NOTaTEMP;
      slot.valid = false;
    }
  }
  dallasSensorsProcess();
  updateEnvironmentTemperatures();

  
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

  fermenterThermometerAlarm();

  static float lastSlowTarget = NOTaTEMP;
  if (SetPointData.setPointSlowTemp != lastSlowTarget) {
    // A new slow target may be sent together with a new current target.
    // Treat that pair as a new transition instead of a manual cancellation.
    lastTarget = SetPointData.setPointTemp;
    lastTargetChange = 0;
  }
  else if (lastTarget != -999 && SetPointData.setPointTemp != lastTarget && SetPointData.setPointSlowTemp != NOTaTEMP) {
      // A direct target change cancels the transition in progress.
      SetPointData.setPointSlowTemp = NOTaTEMP; // reset slowTarget 
      lastModeChange = 0;
      needsToWriteNIV = true;
  }


  // Process slow target changes
  if (SetPointData.setPointSlowTemp != NOTaTEMP) {
    if (lastTargetChange==0)
      lastTargetChange = millis();
    else {
      if (MILLISDIFF(lastTargetChange,
                      slowTargetIncrementIntervalMs(SetPointData.setPointSlowTempSpeed, 0.1f))) {
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

  lastTarget = SetPointData.setPointTemp;
  lastSlowTarget = SetPointData.setPointSlowTemp;

  // After slow-target processing, so a direct change that cancels a ramp is
  // seen with its final value.
  updateTemperatureStability();


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
      case FMTIDLE: {
        if (ControlData.temperature > maxTarget  || (ControlData.temperature>minTarget && interruptedCooling)) {
          if (!millisOverflowWindow && MILLISPAST(nextModeChange)) {
            newMode = FMTCHILL;
          }
        }
        // Avoid switching rapidly from cooling to heating: after a short idle
        // period require a 3*OFFSET drop. Relax this linearly to OFFSET over
        // twenty minutes of idle time.
        constexpr unsigned long shortIdleMs = 5UL * 60000UL;
        constexpr unsigned long longIdleMs = 20UL * 60000UL;
        const unsigned long idleElapsedMs = millis() - lastModeChange;
        float heatRestartAlpha = 1.0f;
        if (idleElapsedMs <= shortIdleMs) {
          heatRestartAlpha = 3.0f;
        } else if (idleElapsedMs < longIdleMs) {
          const float progress = float(idleElapsedMs - shortIdleMs) /
              float(longIdleMs - shortIdleMs);
          heatRestartAlpha = 3.0f - 2.0f * progress;
        }
        const float heatRestartThreshold = SetPointData.setPointTemp -
            heatRestartAlpha * FMTOFFSET;
        if (newMode != FMTCHILL && FMTData.heater.enabled &&
            ControlData.temperature < heatRestartThreshold) {
          if (lastModeChange==0 || (!millisOverflowWindow && MILLISPAST(nextModeChange))) // anti boucing Constante
            newMode = FMTHEAT; 
        }
      break;
      }

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
    // Calculate the rest time while mode still identifies the cycle just ended.
    nextModeChange = millis() + fermenterOnOffCycle(ControlData.temperature,newMode);
    mode = newMode;
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

  const char *dallasPowerMode = Dallas.isParasitePowerMode()
      ? "parasitic"
      : "non-parasitic";

  char buf[2048];  
    sprintf(buf, "<br>-------TEMPERATURE CONTROL<br>Dallas sensor: %s<br>Dallas power mode: %s<br>Temperature: %.2f C<br>Environment Temp: %.2f C (%s)<br>Target temp: %.2f C<br>Mode: %s<br>Chill: %s<br>Interrupted cooling: %s<br>Heat: %s<br>Interrupted heating: %s<br>Total chill time: %ld s<br>Total heat time: %ld s<br>",
          (dallasTemperature == NOTaTEMP || dallasTemperature == 85) ? "NOT DETECTED" : "OK",
          dallasPowerMode,
          ControlData.temperature,
          environmentTemp, environmentTempSource(),
          SetPointData.setPointTemp,
          getTemperatureModeLabel(),
          chillControl ? "ON" : "OFF",
          interruptedCooling ? "YES" : "NO",
          heatControl ? "ON" : "OFF",
          interruptedHeating ? "YES" : "NO",
          CountersData.totalChillTime,
          CountersData.totalHeatTime);
  strnncat(st,buf,2048);

  // Each Dallas role, then the sensors of the last scan without a role
  // (docs/thermometers.md).
  static const char *const roleNames[DALLAS_ROLE_COUNT] = {"fermenter", "environment", "headspace"};
  static const char *const unassignedNotes[DALLAS_ROLE_COUNT] = {
    "temperature control idle", "using the ESP-NOW broadcast", "gas = fermenter"};
  char id[DALLAS_ADDRESS_TEXT_SIZE];
  for (uint8_t r = 0; r < DALLAS_ROLE_COUNT; ++r) {
    if (!dallasRoleAssigned(r)) {
      snprintf(buf, sizeof(buf), "Dallas %s: not assigned (%s)<br>", roleNames[r], unassignedNotes[r]);
    } else {
      const float t = dallasRoleTemperature(r);
      const bool valid = isfinite(t) && t != NOTaTEMP && t != 85.0f;
      formatDallasAddress(FMTData.dallasAddress[r], id, sizeof(id));
      snprintf(buf, sizeof(buf), "Dallas %s: %s %s %.2f C%s<br>", roleNames[r], id,
               valid ? "OK" : "NO READING", t,
               dallasOnBus(FMTData.dallasAddress[r]) ? "" : " (not found in last scan)");
    }
    strnncat(st, buf, 2048);
  }
  snprintf(buf, sizeof(buf), "Gas temperature: %.2f C (%s); fermenter thermometer alarm: %s<br>",
           gasTemperature(), gasTemperatureSource(), thermometerAlarmActive ? "ACTIVE" : "off");
  strnncat(st, buf, 2048);
  DallasScanEntry scan[DALLAS_SCAN_MAX];
  const uint8_t scanned = getDallasScan(scan, DALLAS_SCAN_MAX);
  snprintf(buf, sizeof(buf), "Dallas bus: %u sensor(s) in last scan<br>", scanned);
  strnncat(st, buf, 2048);
  for (uint8_t i = 0; i < scanned; ++i) {
    bool hasRole = false;
    for (uint8_t r = 0; r < DALLAS_ROLE_COUNT; ++r)
      hasRole = hasRole || memcmp(FMTData.dallasAddress[r], scan[i].address, 8) == 0;
    if (hasRole) continue;
    formatDallasAddress(scan[i].address, id, sizeof(id));
    snprintf(buf, sizeof(buf), "Dallas no role: %s %.2f C (last scan)<br>", id, scan[i].temperature);
    strnncat(st, buf, 2048);
  }

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


