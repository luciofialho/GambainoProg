#include "PotCleaner.h"
#include "BrewCoreCommon.h"
#include "Variables.h"

#define POTCLEANER_WATEROUT_TIME 1000
#define POTCLEANER_WATEROUT_TIME_PREWATER 7000


static byte mode =0;
static unsigned long timeToClose =0;
static unsigned long timeStarted =0;

void potCleanerWaterIn() {
  mode = 1;
}

void potCleanerWaterOut() {
  if (mode==0) {
    timeToClose = millis() + POTCLEANER_WATEROUT_TIME_PREWATER;
    mode = 2;
  }
  else {
    mode = 3; 
  }
}
void potCleanerStop() {
  mode = 0;
}

void potCleanerHandle() {
  switch(mode) {
    case 0:                             // idle
      PotCleanerWaterIn = CLOSED;
      PotCleanerDrain = OPEN;
      break;

    case 1:                            // water in
      PotCleanerWaterIn = OPEN;
      PotCleanerDrain = CLOSED;
      break;

    case 2:                            // water out prewater - if it was not filling before
      PotCleanerWaterIn = OPEN;
      PotCleanerDrain = CLOSED;
      if (millis() > timeToClose) {
        PotCleanerWaterIn = CLOSED;
        timeToClose = millis() + POTCLEANER_WATEROUT_TIME_PREWATER;
        mode = 3;
      }
      break;

    case 3:                            // start drain cycle clock for valve
      timeStarted = millis();
      mode = 4;
      break;

    case 4:                            // Wait for drain valve
      PotCleanerWaterIn = OPEN;
      PotCleanerDrain = OPEN;
      if (waitForValve((millis()-timeStarted)/1000)) {
        timeToClose = millis() + POTCLEANER_WATEROUT_TIME;
        mode = 5;
      }
      break;
    
    case 5:                            // Prime line
      PotCleanerWaterIn = OPEN;
      PotCleanerDrain = OPEN;
      if (millis() > timeToClose)
        mode = 0;
      break;
  }
}