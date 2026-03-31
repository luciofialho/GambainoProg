#ifndef Level_h
#include "Todo.h"

#define Level_h

#define MLT 0
#define HLT 1
#define BK  2


#define TOPLEVELBETWEENBOTH   0
#define TOPLEVELBOTHDRY       1
#define TOPLEVELBOTHSUBMERSED 2

#define KEGLIQUIDNOLIQUID 0
#define KEGLIQUIDINTANK 1  // liquid present in tank, but not in the return line
#define KEGLIQUIDINRETURN 2 // liquid present in return line (may have liquid in tank also)


#define WATERTARGET_HLT 1
#define WATERTARGET_BK 2
#define WATERTARGET_COLDBANK 3
#define WATERTARGET_KEG 4
#define WATERTARGET_FMT 5

const char* const WaterTargetLabels[] = {
  "HLT",
  "BK",
  "Cold Bank",
  "Keg",
  "FMT"
};


extern Todo *autoDryTodo[3];
extern float totalHLTWaterIntake;

void configPCNTs();

void manageLevel();

void waterInStart(float liters, byte target, bool isHot);

void waterInStop();

bool waterInIsDone();

void startCalibration(byte mode);

void stopCalibration();

char *getCalibration(char *buf);

#endif
