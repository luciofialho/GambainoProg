#include <stdio.h>
#include <stdarg.h>
#include <Arduino.h>
#include "GambainoCommon.h"
#include <BrewCoreCommon.h>
#include <UI.h>
#include "IOTK_NTP.h"
#include "Equipment.h"
//#include <EMailSender.h>
#include "ProcVar.h"
#include "Status.h"


//----------------------------------- extern declarations 


bool forceWebOutput = false;

TaskHandle_t datalogTask = NULL;
  
volatile unsigned long int coldWaterInMeterCount;
volatile unsigned long int HLT2MLTMeterCount;
volatile unsigned long int transferMeterCount;

double   HLTTempRate;
double   BKTempRate;
bool     limitBKHeater;

char  HLTPriority = 'V';         // 'T'emp or 'V'olume
bool  HLTStandby = true;           // HLT has reached both targets
bool  BKStandby = true;            // BK has reached both targets

OneWire oneWire1(PINONEWIRE1);
OneWire oneWire2(PINONEWIRE2);
OneWire oneWire3(PINONEWIRE3);
DallasTemperature dallas1(&oneWire1);
DallasTemperature dallas2(&oneWire2);
DallasTemperature dallas3(&oneWire3);

Adafruit_INA219 ina219;
bool valveCurrentMonitorIsOn = false;
float actualCurrent = 0;
float valveThresholdCurrent = 0;
float valveStandbyCurrent = 0;

char datalogBuffer[MAXPACKETSIZE+2] = "";

char litersMsg[15];
char lastSentinelName[40];

static const uint8_t RECENT_ALERTS_SIZE = 5;
static char recentAlerts[RECENT_ALERTS_SIZE][101];
static uint8_t recentAlertPos = 0;
static uint8_t recentAlertCount = 0;



void journal(const char *s) {
  char line[STRBUFFERSIZE+25];

  if (s && s[0]) 
    sprintf(line, "[%6ld.%1i / %2ld / %02d:%02d:%02d] %s", millis()/1000, int((millis()/100) % 10),numCycles%100,
                  NTPHour(), NTPMinute(), NTPSecond(),
                  s);
  else 
    line[0] = 0;

  Serial.println(line);

  //****** Todo3: gravar de outra forma
}

void internal_say_v(bool alert, const char *format, va_list args) {
  char buf[STRBUFFERSIZE+1];
  vsnprintf(buf,STRBUFFERSIZE,format,args);
  journal(buf);
  if (alert) {
    char alertLine[101];
    snprintf(alertLine, sizeof(alertLine), "%02d %.97s", NTPDay(), buf);
    strncpy(recentAlerts[recentAlertPos], alertLine, sizeof(recentAlerts[recentAlertPos]) - 1);
    recentAlerts[recentAlertPos][sizeof(recentAlerts[recentAlertPos]) - 1] = '\0';
    recentAlertPos++;
    if (recentAlertPos >= RECENT_ALERTS_SIZE)
      recentAlertPos = 0;
    if (recentAlertCount < RECENT_ALERTS_SIZE)
      recentAlertCount++;
    Led2 = ON;
    Led2.setWithDelay(OFF,3);
    if (Status != STANDBY) {
      sound_Attention();
      Buzzer = ON;
      Buzzer.setWithDelay(OFF,3);
    }
    sendSerial2(INSTABILITYMSGPACKET,buf);
  }
}

const char *getRecentAlertsHtml(char *buf, size_t bufSize) {
  if (!buf || bufSize == 0)
    return "";

  buf[0] = '\0';
  size_t used = 0;

  auto append = [&](const char *s) {
    if (!s) return;
    size_t len = strlen(s);
    if (used >= bufSize - 1) return;
    size_t avail = bufSize - 1 - used;
    size_t n = (len < avail) ? len : avail;
    memcpy(buf + used, s, n);
    used += n;
    buf[used] = '\0';
  };

  if (recentAlertCount == 0) {
    append("<br>Recent alerts: none<br>");
    return buf;
  }

  append("<br>Recent alerts:<br>");
  uint8_t total = recentAlertCount;
  uint8_t start = (recentAlertPos + RECENT_ALERTS_SIZE - total) % RECENT_ALERTS_SIZE;
  for (uint8_t i = 0; i < total; i++) {
    uint8_t idx = (start + i) % RECENT_ALERTS_SIZE;
    append(recentAlerts[idx]);
    append("<br>");
  }

  return buf;
}

void say(const char *format, ...) {
  va_list args;
  va_start(args, format);
  internal_say_v(false, format, args);
  va_end(args);
}

void say(char *s) {
  journal(s);
}

void say() {
  journal(" ");
}

void alert(const char *format, ...) {
  va_list args;
  va_start(args, format);
  char buf[STRBUFFERSIZE+20];
  NTPFormatedTime(buf);
  strcat(buf, ": ");
  strncat(buf, format, STRBUFFERSIZE - 12);
  internal_say_v(true, buf, args);
  va_end(args);
}


unsigned long int perfSentinel = 0;
void startPerfSentinel() {perfSentinel=millis();}
void checkPerfSentinel(const char *a, int time) {
  strncpy(lastSentinelName,a,sizeof(lastSentinelName)-1); 
  if (MILLISDIFF(perfSentinel,time)) {
    Serial.print("PERFORMANCE ISSUE: "); 
    char buf[100];
    char buf2[30];
    NTPFormatedTime(buf2);
    snprintf(buf,99,"[%s] %s took %i ms", buf2, a, millis()-perfSentinel);
    Serial.print(buf); 
    sendSerial2(SENTINELPACKET, buf);
  }
  perfSentinel = millis();
}

bool safetyCheck(bool condition, const char *exceptionMsg) {
  if (!condition)
    say("Program delayed: %s",exceptionMsg);
  return condition;
}

bool waitForValve(float time) {
  if (time >= WAITFORVALVE)
    return true;
  else if (time>3*SECONDS && valveCurrentMonitorIsOn && abs(ina219.getCurrent_mA()/ina219.getBusVoltage_V()*12 < valveThresholdCurrent))
    return true;

  return false;
}
