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
#define PINRELIEFVALVE 42

#define PINBUZZER 47
#define PINLED    13

#define PINBTN    14


#define FMTCHILL 0
#define FMTIDLE 1
#define FMTHEAT 2

#define FMTOFFSET 0.2
#define FMTSLOWINCREMENTTIME (36*60L*1000L) // time to change 0.1 ºC ex. 36 minutes = 4ºC per day

extern float dallasTemperature;
extern bool  debugTemperatureOverride;
extern float environmentTemp;
extern byte DisplayMode;

  