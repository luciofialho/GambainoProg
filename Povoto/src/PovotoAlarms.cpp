#include <Arduino.h>
#include "PovotoAlarms.h"
#include "PovotoCommon.h"
#include "PovotoTasks.h"
#include "PressureControl.h"
#include "TemperatureControl.h"
#include "Povoto_UI.h"
#include "GambainoCommon.h"
#include "PovotoData.h"
#include "PovotoMail.h"
#include <WiFi.h>

// Beeps: every minute for the first 10 minutes of an alarm, then every 15 minutes
// until the first hour, then every hour. Screen, LEDs and e-mail go on as before.
static unsigned long alarmBeepInterval(unsigned long activeMs) {
  if (activeMs < 600000UL) return 60000UL;
  if (activeMs < 3600000UL) return 900000UL;
  return 3600000UL;
}
static constexpr unsigned long ALARM_MAIL_INTERVAL_MS = 3600000UL;
static constexpr unsigned long ALARM_BLINK_PERIOD_MS = 200UL; // 5 Hz
static constexpr unsigned long TASK_BLINK_PERIOD_MS = 1000UL; // 1 Hz

static const ErrorScreenText THERMOMETER_SCREEN = {
  "NO TEMP", "SENSOR", "FERMENTER THERMOMETER MISSING", "TEMPERATURE CONTROL STOPPED",
  "Assign it on the Settings page"};
static const ErrorScreenText PRESSURE_NOT_RESPONDING_SCREEN = {
  "PRESSURE", "SENSOR", "PRESSURE SENSOR NOT RESPONDING", "PRESSURE READING UNRELIABLE",
  "Check the 4-20 mA sensor and the INA226 wiring"};
static const ErrorScreenText PRESSURE_OUT_OF_RANGE_SCREEN = {
  "PRESSURE", "SENSOR", "PRESSURE SENSOR CURRENT OUT OF RANGE", "PRESSURE READING UNRELIABLE",
  "Check the 4-20 mA sensor and the INA226 wiring"};

static const ErrorScreenText RELIEF_NO_EFFECT_SCREEN = {
  "VENT", "FAILED", "VENTS DO NOT LOWER THE PRESSURE", "VALVE NOT OPENING OR OUTLET BLOCKED?",
  "Check the valve, its outlet and the witness bottle"};
static const ErrorScreenText VALVE_STUCK_OPEN_SCREEN = {
  "VALVE", "OPEN", "PRESSURE FALLS AFTER THE VENT", "VALVE STUCK OPEN?",
  "Close the line by hand and check the valve"};

static PovotoAlarm activeAlarm = ALARM_NONE;

static const char *modeLabel(byte mode) {
  switch (mode) {
    case MODE_OFF: return "Off";
    case MODE_BREWING_TRANSFERING: return "Brewing/transferring";
    case MODE_FERMENTING: return "Fermenting";
    case MODE_CONDITIONING: return "Conditioning";
    case MODE_MANUAL: return "Manual control";
    default: return "?";
  }
}

// E-mail at the start and every hour while it lasts (none when it is solved):
// there may be nobody near the Povoto. The batch number and beer name tell the
// sites apart (the Povoto does not know its cloud site).
static void sendAlarmMail(PovotoAlarm alarm, unsigned long activeMs) {
  char subject[160];
  snprintf(subject, sizeof(subject), "[POVOTO %d] ALARM: %s failure - batch %u %s%s",
           (int)FMTData.PovotoNum, povotoAlarmLabel(alarm), (unsigned)BatchData.batchNumber,
           BatchData.batchName, debugging ? " (debug mode)" : "");

  String body;
  body.reserve(900);
  body += subject;
  body += "\n\n";
  char line[96];
  snprintf(line, sizeof(line), "Batch: %u\nBeer: %s\n", (unsigned)BatchData.batchNumber, BatchData.batchName);
  body += line;
  snprintf(line, sizeof(line), "Active for: %lu h %02lu min\n\n",
           activeMs / 3600000UL, (activeMs / 60000UL) % 60UL);
  body += line;
  switch (alarm) {
    case ALARM_PRESSURE_SENSOR:
      body += describePressureSensor();
      body += "\nThe pressure reading is unreliable: check the 4-20 mA sensor and the INA226 wiring.\n";
      break;
    case ALARM_RELIEF:
      body += describeReliefCheck();
      body += "\nThe vents lower the pressure less than expected (two in a row below 80%): check the valve, its outlet and the witness bottle. "
              "These vents are not counted by the flow model.\n";
      break;
    case ALARM_VALVE_STUCK_OPEN:
      body += describeValveStuckOpen();
      body += "\nThe pressure keeps falling after the vent closed: the valve may be stuck open. "
              "Close the line by hand and check or clean the valve.\n";
      break;
    default:
      body += describeFermenterThermometer();
      body += "\nAssign the fermenter thermometer on the Settings page or check the Dallas wiring.\n";
      break;
  }
  body += "This e-mail repeats every hour while the alarm lasts.\n";
  char info[200];
  snprintf(info, sizeof(info), "\nMode: %s\nIP: %s\n",
           modeLabel(SetPointData.mode), WiFi.localIP().toString().c_str());
  body += info;
  Serial.printf("[ALARM] e-mail: %s\n", subject);
  queuePovotoMail(subject, body);
}

const char *povotoAlarmLabel(PovotoAlarm alarm) {
  switch (alarm) {
    case ALARM_PRESSURE_SENSOR: return "pressure sensor";
    case ALARM_RELIEF: return "vent below expected drop";
    case ALARM_VALVE_STUCK_OPEN: return "valve stuck open";
    case ALARM_FERMENTER_THERMOMETER: return "fermenter thermometer";
    default: return "none";
  }
}

PovotoAlarm activePovotoAlarm() {
  return activeAlarm;
}

void povotoAlarmsSetup() {
  pinMode(PINATTENTIONLED, OUTPUT);
  digitalWrite(PINATTENTIONLED, LOW);
}

static const ErrorScreenText *alarmScreen(PovotoAlarm alarm) {
  switch (alarm) {
    case ALARM_PRESSURE_SENSOR:
      return pressureSensorNotResponding() ? &PRESSURE_NOT_RESPONDING_SCREEN : &PRESSURE_OUT_OF_RANGE_SCREEN;
    case ALARM_RELIEF: return &RELIEF_NO_EFFECT_SCREEN;
    case ALARM_VALVE_STUCK_OPEN: return &VALVE_STUCK_OPEN_SCREEN;
    case ALARM_FERMENTER_THERMOMETER: return &THERMOMETER_SCREEN;
    default: return nullptr;
  }
}

void processPovotoAlarms() {
  checkPressureSensor();
  const PovotoAlarm alarm = pressureSensorAlarmActive() ? ALARM_PRESSURE_SENSOR
      : valveStuckOpenAlarmActive() ? ALARM_VALVE_STUCK_OPEN
      : reliefCheckAlarmActive() ? ALARM_RELIEF
      : fermenterThermometerAlarmActive() ? ALARM_FERMENTER_THERMOMETER : ALARM_NONE;
  const unsigned long now = millis();
  static unsigned long lastBeeps = 0; // 0 = not beeping
  static unsigned long alarmSince = 0;
  static unsigned long lastMail = 0;

  if (alarm != activeAlarm) {
    Serial.printf("[ALARM] %s -> %s\n", povotoAlarmLabel(activeAlarm), povotoAlarmLabel(alarm));
    activeAlarm = alarm;
    lastBeeps = 0;
    alarmSince = now;
    lastMail = now;
    if (alarm != ALARM_NONE) sendAlarmMail(alarm, 0);
    if (alarm == ALARM_NONE) {
      digitalWrite(PINVENTINGLED, LOW);
      statusLedWrite(0, 0, 0); // the Wi-Fi indication resumes
    }
  }
  // Also follows a change of the problem (pressure: not responding / out of
  // range).
  showErrorScreen(alarmScreen(alarm));

  if (alarm != ALARM_NONE && now - lastMail >= ALARM_MAIL_INTERVAL_MS) {
    lastMail = now;
    sendAlarmMail(alarm, now - alarmSince);
  }

  // Silent in debug mode (bench boards); screen and LEDs still signal.
  if (alarm != ALARM_NONE && (lastBeeps == 0 || now - lastBeeps >= alarmBeepInterval(now - alarmSince))) {
    lastBeeps = now | 1;
    if (!debugging)
      buzzerBeepRequest = alarm == ALARM_PRESSURE_SENSOR ? 4 : alarm == ALARM_VALVE_STUCK_OPEN ? 6
          : alarm == ALARM_RELIEF ? 5 : 3;
  }

  const bool alarmBlink = (now % ALARM_BLINK_PERIOD_MS) < ALARM_BLINK_PERIOD_MS / 2;
  if (alarm != ALARM_NONE) {
    static bool blinkOn = false;
    if (alarmBlink != blinkOn) {
      blinkOn = alarmBlink;
      digitalWrite(PINVENTINGLED, alarmBlink ? HIGH : LOW);
      statusLedWrite(alarmBlink ? 255 : 0, 0, 0);
    }
  }

  bool attention = false;
  if (alarm != ALARM_NONE) attention = alarmBlink;
  else if (isTaskRunning()) attention = (now % TASK_BLINK_PERIOD_MS) < TASK_BLINK_PERIOD_MS / 2;
  static bool attentionOn = false;
  if (attention != attentionOn) {
    attentionOn = attention;
    digitalWrite(PINATTENTIONLED, attention ? HIGH : LOW);
  }
}
