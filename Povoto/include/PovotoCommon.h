#include <TFT_eSPI.h>
#include <ProcVar.h>

extern TFT_eSPI tft;

#define PINSDA 22
#define PINSCL 17
#define PINDALLAS 33

#define PINCHILLER 13
#define PINHEATER 16
#define PINTRANSFERVALVE 25
#define PINRELIEFVALVE 26

#define PINBUZZER 14
#define PINLED    19

#define PINBTN    34


#define FMTCHILL 0
#define FMTIDLE 1
#define FMTHEAT 2

#define FMTOFFSET 0.2
#define FMTSLOWINCREMENTTIME (36*60L*1000L) // time to change 0.1 ºC ex. 36 minutes = 4ºC per day

extern float dallasTemperature;
extern bool  debugTemperatureOverride;
extern float environmentTemp;
extern byte DisplayMode;

  