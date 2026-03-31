#include "ColdSide.h"
//#include <__Timer.h>
#include "variables.h"
#include <Status.h>
#include "IOTK_NTP.h"
#include <UI.h>
#include "GambainoCommon.h"
#include <BrewCoreCommon.h>

#define COLDSIDEOFFSET 0.5                       // offset allowed in coldbank and cellar

void coldSideControl() {
    if (ColdBankTemp != NOTaTEMP && ColdBankTargetTemp != NOTaTEMP && MILLISDIFF(0,120000L) && !ColdBankValve.asBoolean() && Status != STANDBY) {
      ColdBankControl.setBooleanFromOffset(ColdBankTargetTemp,ColdBankTemp,COLDSIDEOFFSET,'-');
      ColdBankPump = ColdBankControl;
    }
    else {
      ColdBankControl = OFF;
      if (!ColdBankValve.asBoolean())
        ColdBankPump = OFF;
    }
    
    if (NTPDay()%3==1 && NTPHour()==12 && NTPMinute()<02) { //****** Colocar no programa principal
      static byte lastDay=99;
      if (lastDay!=NTPDay()) {
        if (OnGoingProgram == PGMSTANDBY) {
          startProgram(PGMCIPAUTORINSE);
        }
        lastDay = NTPDay();
      }
    }
        
}

