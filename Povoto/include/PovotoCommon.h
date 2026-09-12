#include <TFT_eSPI.h>
#include <ProcVar.h>

extern TFT_eSPI tft;

#define PINSDA 8
#define PINSCL 9
#define PINDALLAS 21

#define PINCHILLER 39
#define PINLEDCHILLER 1
#define PINHEATER 40
#define PINLEDHEATER 2
#define PINTRANSFERVALVE 41
#define PINVENTINGLED 42

#define PINBUZZER 47
#define PINLED    48  // Addressable RGB LED built into the ESP32-S3 DevKitC.

#define PINBTN    14


#define FMTCHILL 0
#define FMTIDLE 1
#define FMTHEAT 2

#define FMTOFFSET 0.3
extern float dallasTemperature;
extern bool  debugTemperatureOverride;
extern float environmentTemp;
extern byte DisplayMode;
extern bool  soundAlarm;


