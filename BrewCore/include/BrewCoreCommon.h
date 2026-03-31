#ifndef GAMBAINOCOMMON_H
#define GAMBAINOCOMMON_H
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_INA219.h>
#include <Pins.h>

#include <IOTK.h>
#include "ProcVar.h"
#include <SPIFFS.h>



#define OPEN                    1
#define CLOSED                  0
#define ON                      1
#define	OFF                     0
#define PORT_A                  0
#define PORT_B                  1
#define POWERON                 1
#define POWEROFF                0
#define NOWATERIN               -5
#define NOHEAT                  -6


#define BRAZILIANFORMAT    


extern bool forceWebOutput;

extern TaskHandle_t datalogTask;

extern volatile unsigned long int waterInMeterCount;
extern volatile unsigned long int HLTMeterCount;
extern volatile unsigned long int transferMeterCount;


extern double    HLTTempRate;
extern double    BKTempRate;
extern bool     limitBKHeater;

extern char  HLTPriority;          // 'T'emp or 'V'olume
extern bool  HLTStandby;           // HLT has reached both targets
extern bool  BKStandby;            // BK has reached both targets
 

extern OneWire oneWire1;
extern OneWire oneWire2;
extern OneWire oneWire3;
extern DallasTemperature dallas1;
extern DallasTemperature dallas2;
extern DallasTemperature dallas3;

extern Adafruit_INA219 ina219;
extern bool valveCurrentMonitorIsOn;
extern float actualCurrent;
extern float valveStandbyCurrent;
extern float valveThresholdCurrent;

extern char datalogBuffer[MAXPACKETSIZE+2];

extern char litersMsg[15];


void journal(const char *s);
void say(const char *format, ...);
void alert(const char *format, ...);
void say(char *s);
void say();
const char *getRecentAlertsHtml(char *buf, size_t bufSize);


void setDataLogSpreadsheetName(int batchNum, int fmt1BatchNum, int fmt2BatchNum);

extern unsigned long int perfSentinel;
void startPerfSentinel();
void checkPerfSentinel(const char *a, int time=1000);

bool safetyCheck(bool condition, const char *exceptionMsg);

bool waitForValve(float time);



#endif
