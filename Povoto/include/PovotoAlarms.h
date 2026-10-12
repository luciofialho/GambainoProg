#ifndef POVOTO_ALARMS_H
#define POVOTO_ALARMS_H

#include <Arduino.h>

// Equipment alarms (docs/alarms.md), by precedence: the pressure sensor, the
// valve stuck open, the vents without effect and the fermenter thermometer. The active one beeps every minute, takes
// the screen and blinks the venting LED, the neopixel (red) and the attention
// LED. The attention LED also blinks slowly while a task runs.
enum PovotoAlarm : uint8_t {
  ALARM_NONE = 0,
  ALARM_PRESSURE_SENSOR,
  ALARM_RELIEF,               // vents without effect
  ALARM_VALVE_STUCK_OPEN,
  ALARM_FERMENTER_THERMOMETER,
};

void povotoAlarmsSetup();
void processPovotoAlarms(); // main loop
PovotoAlarm activePovotoAlarm();
const char *povotoAlarmLabel(PovotoAlarm alarm);

#endif
