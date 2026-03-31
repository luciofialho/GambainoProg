#include <Variables.h>
#include <Equipment.h>
#include "GambainoCommon.h"
#include <BrewCoreCommon.h>
#include <Level.h>
#include <Status.h>
#include <Todo.h>
#include <StatusEntrySetup.h>
#include <WaterHeatControl.h>
#include <UI.h>
#include "IOTK_NTP.h"
#include "IPC.h"
#include "IOTK_SimpleMail.h"
#include "SPIFFS.h"
#include <stdarg.h>
#include "EspNowPackets.h"

// extern variables
bool restoringState = false;
char SubStatusLabel[80] = "";


//---

double StatusEntryTime;
bool kettleSourProgram;

float caramelizationBoilStart = 0;

int programToStart = 99;
int diagParameters = 0;

// private Variables
float stopTimer;

void startProgram(int pgm) {
  if (pgm > 100) {
    ProgramParams = pgm % 100;
    pgm = pgm/100;
  }
  else {
    ProgramParams = 0;
  }
  OnGoingProgram = pgm;
  Status    = pgmStatusRange[pgm][0]; 
  EndStatus = pgmStatusRange[pgm][1]; 
  IPCStatus = IPCNOTSTARTED;
  TimeInStatus = 0;
  Todo::resetTodos();

  if (pgm > PGMMANUALCONTROL) {
    say("Starting program %d.",pgm);
    HLTVolume = -HLTDEADVOLUME;
  }
  else {
    ProcVar::releaseAllOverrides();
  }

  if (pgm == PGMDIAGMANUAL || pgm == PGMDIAGAUTO) {
    char time[20];
    char buf[80];

    sprintf(buf,"Diagnostic %s",NTPFormatedDateTime(time));
    SPIFFS.remove("/out/diagnostics.html");

    diagEntry(0,buf);
  }

  TransferVolume = 0;    
}


bool skipStatus(int s) {
  bool skip = false;
  int  st   = int(Status);
  
  if (s!=0)
    st=s;
  
  if (RcpWhirlpoolHoppingTime==0)                           // no whirlpool hopping
    if (st==WHIRLPOOLCHILLTOHOP || st==WHIRLPOOLHOPPING)
        skip = true;

  if (!kettleSourProgram && st >= KS_WAITFIRSTBOIL && st <= KS_SOURING)
      skip = true;
    
  if (RcpRampGrainWeight==0 || RcpRampTime==0)
    if (st==RAMPINFUSION || st==RAMPREST)
      skip = true;
    
  if (OnGoingProgram == PGMKEGCLEAN) {
    byte type = int(ProgramParams) / 10 % 10;
    bool skipSetup = (int(ProgramParams) % 10) == 2;

    if (skipSetup && st==KEGCLEANERRINSE)
      skip = true;

    if (type==2 && st>=KEGPREPDETERG && st<=KEGCLEANERFINALRINSE && st != KEGRINSE && st != KEGASKANOTHERKEG) // rinse only
      skip = true;
  }



  if (OnGoingProgram == PGMDIAGAUTO || OnGoingProgram == PGMDIAGMANUAL)
    if (st>=DIAGMOTORVALVESCOMMAND && st<DIAGRESULT) {
      int bit = 1 << (st-DIAGMOTORVALVESCOMMAND);
      if (diagParameters & bit)
        skip = false;
      else
        skip = true;  
    }


  return skip;
}

#define FMTCIPMAXSTEPS 40
byte FMTCIPnSteps = 0;
byte FMTCIPSteps[FMTCIPMAXSTEPS];
byte FMTCIPCurrentStep = 0;
byte pureRinseSteps;

void FMTCIPStartCycle() {
  FMTCIPnSteps = 0;
  byte nDeterg = OnGoingProgram==PGMFMTAUTO2PHASE ? 2 : 1;
  
  for (byte i=0;i<FMTCIPNUMPURERINSESW;i++)
    FMTCIPSteps[FMTCIPnSteps++] = CIPFMTSERVICEWATERRINSE;
  FMTCIPSteps[FMTCIPnSteps++] = CIPFMTCHECKRINSE;
  pureRinseSteps = FMTCIPnSteps;
  
  for (byte j=0; j<nDeterg; j++) {
    FMTCIPSteps[FMTCIPnSteps++] = CIPFMTDETERGENTCIRC;
    for (byte i=0;i<FMTCIPNUMDETERGRINSESW;i++)
      FMTCIPSteps[FMTCIPnSteps++] = CIPFMTSERVICEWATERRINSE;
    FMTCIPSteps[FMTCIPnSteps++] = CIPFMTCHECKRINSE;
  }
  FMTCIPCurrentStep = 0;
}

byte FMTCIPNextStatus(byte redoStatus=0) {
  static bool isRedo = false;
  byte res = 0;

  if (isRedo) {
    isRedo = false;
    res = CIPFMTCHECKRINSE;
  }
  else if (!redoStatus) {
    if (FMTCIPCurrentStep<FMTCIPnSteps)
      res = FMTCIPSteps[FMTCIPCurrentStep++];
    else
      res = CIPFMTCOMPLETE;
  }
  else {
    isRedo = true;
    res = redoStatus;
  }

  if (isRedo)
    say("FMT CIP: Repeating rinse: %s",statusNames[res]);
  else 
    say("FMT CIP: Entering step %d/%d - %s",FMTCIPCurrentStep,FMTCIPnSteps,statusNames[res]);

  return res;

}

int dataLogInterval() {

    
  if (Status==STANDBY || OnGoingProgram==PGMDIAGMANUAL || OnGoingProgram==PGMDIAGAUTO || (Status>=DIAGI2C && Status<=DIAGRESULT))
    return 0;

  if (debugging)
    return 5 * SECONDSms;

  else if (Status < BREWWAIT)
    return 30 * SECONDSms;

  else if (Status == BREWWAIT)
    return 5 * MINUTESms;

  else if (Status < MASHMASHING)
    return 30 * SECONDSms;

  else if (Status == MASHMASHING)
    return 30 * SECONDSms;

  else if (Status < TRANSFER) {
    if (Status>=KS_SOURINGSETUP && Status<=KS_SOURING)
      return 5 * MINUTESms;
    else
      return 20 * SECONDSms;
  }

  else if (Status==TRANSFER)
    return 10 * SECONDSms;

  else if (Status<=CIPWRINSE4)
    return 20 * SECONDSms;

  return 0;
}

void BKSecondaryControl() { // caramelization boil and circulation in whirlpool line
  if (Status == MASHMASHOUT || Status == PREPSPARGE)
    switch (int(SubStatus)) {
      case 1: 
        if (BKTemp>=CARAMELIZATIONBOILSTARTTEMP) {
          caramelizationBoilStart = TimeInStatus;
          setSubStatus(2,"Caramelization boil");
        }
        break;
      case 2: 
        if (TimeInStatus >= caramelizationBoilStart + (float) RcpCaramelizationBoil * MINUTES) {
          BKTargetTemp = PREBOILTEMP;
          setSubStatus(3,"Caramelization boil finished");
        }
        else
          setCountDownMessage("Caramelization boil ends in %s:%s", (caramelizationBoilStart + (float) RcpCaramelizationBoil * MINUTES) - TimeInStatus);
      break;

    }

  if (Status == KS_WAITFIRSTBOIL || Status == WAITBOIL)
    if (!BKPump.asBoolean() && (BKTemp >= BKTargetTemp-3 || BKTemp >= WORTBOILTEMP - 3))
      BKPump = ON;

  if (Status == BOIL || Status == KS_FIRSTBOIL) {
    float elapsedTime;
    if (Status==KS_FIRSTBOIL)
      elapsedTime = TimeInStatus + (float)RcpKettleSourFirstBoilTime*MINUTES;
    else
      elapsedTime = TimeInStatus + (float)RcpBoilTime*MINUTES;

    bool turnPumpOn = (elapsedTime <= 15*MINUTES) || 
         (TimeInStatus > -30*MINUTES && int(elapsedTime) % (2*60) < 3); // constantes
    if (BKPump.asBoolean() && !turnPumpOn)
      BKPump = OFF;
    else if (!BKPump.asBoolean() && turnPumpOn)
      BKPump = ON;
  }
}

float diagFlowDeviation(float x, float y) {
  float d = (y>0.01) ? (((x-y)/y) *100.) : 100.;
  return d;
}

byte diagFlowDevStatus(float x, float y) {
  if (abs(diagFlowDeviation(x,y)) >= 3) return 2;
  if (abs(diagFlowDeviation(x,y)) >= 1.5) return 1;
  return 0;
}  

void MainStateMachine() {
  static byte LastStatus = -1;
  bool EnteringStatus = (Status != LastStatus);
  bool GoToNextStatus = false;
  bool NextStatus = false;
  byte forceNextStatus = 0;
  
  float lastTimeInStatus;
  static bool keepSubStatus = false;

  if (programToStart != 99 && (programToStart != int(OnGoingProgram) || Status==MANUALCONTROL)) {
    startProgram(programToStart);
    EnteringStatus = true;
    programToStart = 99;
  }

  bool isRestoring = restoringState;
  if (restoringState) {
    Todo::resetTodos();
    ProcVar::readFromEEPROM(PROCESSPERSISTENCE);
    ProcVar::readFromEEPROM(OVERRIDEPERSISTENCE);  
    calculateVolumesAndTemperatures(false);
    StatusEntryTime = millis()/1000. - TimeInStatus;
    restoringState = false;
    EnteringStatus = true;
    Serial.print("Restoring state: ");Serial.println(Status.getProgValue());
    Serial.print("PGM: "); Serial.println(OnGoingProgram.getProgValue());
  }


  if (Status.isOverrided()) {                   
    if (Status.getProgValue() == MANUALCONTROL) {              // if in manualcontrol, seting a new status just run its setup 
      statusEntrySetup();
      Status.override();
      return;
    }
    else {
      EnteringStatus = true;
      Status.assumeOverrideAsProg();
      statusEntrySetup();
    }
  }

  NextStatus = SkipToNextStatus.asBoolean();

  if (SkipToNextStatus.isOverrided())
      SkipToNextStatus.override();    

  if (EnteringStatus) {
    if (isRestoring) {
      StatusEntryTime = millis()/1000. - TimeInStatus;
    }
    else
      StatusEntryTime = millis()/1000.;

    say("#### Entering status %i: %s",int(Status), statusNames[int(Status)]); 
    kettleSourProgram = RcpKettleSourSouringTemp != 0;

    if (!keepSubStatus)
      setSubStatus(0,"");
    else
      keepSubStatus =false;

    ProcVar::setDatalogInterval(dataLogInterval()); 
    
  }

  if (TimeInStatus.isOverrided()) {
      IPCStatusEntryTime = IPCStatusEntryTime - (TimeInStatus - TimeInStatus.getProgValue())*1000L;
      TimeInStatus.assumeOverrideAsProg();
      StatusEntryTime = millis()/1000. - TimeInStatus;
  }

  TimeInStatus = millis()/1000. - StatusEntryTime;;
  //Serial.println("TimeInStatus: " + String(TimeInStatus.getProgValue()));
  lastTimeInStatus = TimeInStatus;
  
  if (EnteringStatus) 
      statusEntrySetup();

  switch (int(Status)) {
    case STANDBY:
      if (!EnteringStatus)
        if (TimeInStatus > 5*MINUTES) {
          float timeToAlarm = 1*MINUTES;
          static float possibleLeakStart = 0;
          if (WaterInFlow > 0.1 ) {
            if (possibleLeakStart != 0) {
              if (TimeInStatus > possibleLeakStart + timeToAlarm) { // after leaking for 1 minute
                sound_Attention();
                sendSimpleMail("[GAMBAINO] ALERT: Cold water leak", "Cold water inlet flow detected: " + String(float(WaterInFlow)) + " L/min");
                sound_Attention();            
                TimeInStatus = 0; // turn on power and check again after the time
                timeToAlarm = 15*MINUTES;
                possibleLeakStart = 0;
              }
            }
            else
              possibleLeakStart = TimeInStatus;
          }
          else
            possibleLeakStart = 0;
        }
      break;

    case PREBREWSETUP:
      if (EnteringStatus) {
        if (debugging) 
          BKLevel = 0;
      }
      else
        GoToNextStatus = setupLineIsReady();
      break;

    case PREBREWHOTWATERPREP:
      if (EnteringStatus)
        setSubStatus(0,"Loading hot water");
      else {
        if (SubStatus==0) {
          if (debugging && WaterInTarget  < (CIPHOTWATERVOLUME-20)) BKLevel = 2;
          if (BKLevel>=2 && BKTargetTemp != CONSTANTHEAT)
            BKTargetTemp = CONSTANTHEAT;
          if (waterInIsDone() && BKLevel>=2 && BKTargetTemp == CONSTANTHEAT) {
            setSubStatus(1,"Heating BK");
            WhirlpoolValve = PORT_B;
            BKWaterIn = CLOSED;
            if (IPCStatus==IPCNOTSTARTED)
              IPCStart();
          }
        }
        else if (SubStatus==1) {
          if (BKTemp > CIPHOTWATERCIRCULATIONSTART) {
            BKPump = ON;
            HLTPump = ON;
            MLTPump = ON;
            setSubStatus(2,"Pre heating line");
          }
        }
        else 
          GoToNextStatus =  BKTemp >= CIPHOTWATERSTARTTEMP;
      }
      break;        

    case PREBREWHOTWATERCIRC1:
      if (EnteringStatus) {
        if (debugging)
          BKLevel = 3;
        TimeInStatus = -CIPHOTWATERTIME_1;
      }
      else
        if (TimeInStatus >= 0) {
          if (BKPump.asBoolean())
            BKPump = OFF;
          GoToNextStatus = true;
        }
      break;

    case PREBREWHOTWATERCIRC2:    
      if (EnteringStatus)
        TimeInStatus = -CIPHOTWATERTIME_2;
      else
        GoToNextStatus = TimeInStatus>=0;
      break;

    case BREWWAIT:
      if (EnteringStatus) {
        long int targetMinute = (int) (RcpStartTime * 60.0);
        long int waitFor = targetMinute-(NTPHour()*60+NTPMinute());
        if (waitFor <= 0) waitFor += 60*24;
        TimeInStatus = -waitFor * 60;
        calculateVolumesAndTemperatures(true);
      }
      else {
        if (TimeInStatus > -4*HOURS)
          if (ColdBankTargetTemp.getProgValue() != coldBankTargetTempCurve[1])
            ColdBankTargetTemp = coldBankTargetTempCurve[1];
        GoToNextStatus = TimeInStatus > 0;
      }
      break; 

    case HLTLOADANDDEAERATE:
      if (EnteringStatus) {
        setSubStatus(0,"Loading HLT water");
         if (debugging)
           if (IPCStatus < IPCWAITBREWSTART)
              IPCStatus = IPCWAITBREWSTART;
      }
      else {
        if (SubStatus==0 && HLTVolume >= HLTTargetVolume) {
          setSubStatus(1,"Heating HLT water");
          calculateVolumesAndTemperatures(false);
        }
        if (SubStatus==1) {
          //if (HLTTemp >= LODOWATERTEMPERATURE-4) 
           // HLTPump = ON;
          if (HLTTemp >= LODOWATERTEMPERATURE) {
            setSubStatus(2,"Dearating HLT water");
            stopTimer = TimeInStatus+TIMETODEAREATE;
          }
        }
        if (SubStatus==2) {
          if (TimeInStatus >= stopTimer) {
            //HLTPump = OFF;
            GoToNextStatus = true;
          }
          else
            setCountDownMessage("Deareting HLT water. Remaining: %s:%s", stopTimer-TimeInStatus);
        }
      }
      break;

    case PREPWATERFORINFUSION:
      if (EnteringStatus) {
        setSubStatus(0,"Using BK water to chill HLT");
        stopTimer = 0;
        if (HLTInfusionTemp==0) { // in the case status was manually set
          calculateVolumesAndTemperatures(false);
          HLTTargetTemp = (RcpRampGrainWeight > 0 && RcpRampTime > 0) ? HLTRampTemp : HLTInfusionTemp;          
        }
        if (debugging)
          BKLevel = 3;
      }
      else {
        switch (int(SubStatus)) {
          case 0: 
            BKValveB = OPEN;
            if (TimeInStatus>WAITFORVALVE) {
              if (!HLTPump.asBoolean())
                HLTPump = ON;
              if (BKLevel>0) {
                if (!BKPump.asBoolean())
                  BKPump = ON;
              }
              else {
                if (BKPump.asBoolean())
                  BKPump = OFF;
              }
            }
            if (HLTTemp <= HLTTargetTemp + ALLOWEDTEMPOFFSET) 
              setSubStatus(3,"Reached HLT target temperature");
            else 
              if (BKLevel<=0)  {
                stopTimer = TimeInStatus + 1*MINUTES;
                setSubStatus(1,"Waiting for BK to drain by gravity");
              }
            break;

          case 1:
            BKValveB = OPEN;
            WhirlpoolValve = PORT_A;                  
            BKPump = OFF;
            if (TimeInStatus >= stopTimer) {
              setSubStatus(2,"Using service cold water to chill HLT");
            }
            break;

          case 2:
              BKValveB = CLOSED;
              ColdWaterIn = OPEN;
              BKWaterIn   = OPEN;
              WhirlpoolValve = PORT_B;
              BKPump = OFF;
              if (HLTTemp <= HLTTargetTemp + ALLOWEDTEMPOFFSET*2) {
                setSubStatus(3,"Reached HLT target temperature");
                BKPump = OFF;
              }
            break;

          case 3:
            if (HLTTemp <= HLTTargetTemp + ALLOWEDTEMPOFFSET) {
              ColdWaterIn = CLOSED;
              BKWaterIn   = CLOSED;
              HLTPump = OFF;
              BKValveB = CLOSED;
              MLTValveA = CLOSED;
              MLTDrain = CLOSED;
              WhirlpoolValve = PORT_B;
              BKPump = OFF;
              setSubStatus(4,"HLT ready for infusion");
            }
            break;

          case 4:
            GoToNextStatus = setupLineIsReady() 
                             && Todo_StartBrew.neededTodoIsReady();
            if (GoToNextStatus)
              ColdBankTargetTemp = coldBankTargetTempCurve[2];
            break;
        }
      }
      break;

    case RAMPINFUSION:
      if (!EnteringStatus)
        GoToNextStatus = CirculationMode == CIRCNONE;
      break;

    case RAMPREST:
      if (EnteringStatus) 
        TimeInStatus = - (float) RcpRampTime * MINUTES;
      else
        GoToNextStatus = HLTStandby && TimeInStatus >= 0;
      break;

    case INFUSION:
      if (!EnteringStatus) 
        GoToNextStatus = CirculationMode == CIRCNONE;
      break;

    case MASHGRAINREST:
      if (EnteringStatus) {
        Todo_MashRest.start();
      }
      else
        if (HLTStandby)
          GoToNextStatus = Todo_MashRest.neededTodoIsReady();
        else  
          GoToNextStatus = Todo_MashRest.TodoIsReady();
      break;
        
    case MASHMASHING:
      if (EnteringStatus) {
        if (TimeInStatus>=0)
          TimeInStatus = - (float) RcpMashTime * MINUTES;
        MLTTargetTemp = RcpMashTemp;
      }
      else {
        if (TimeInStatus >= - 10 * MINUTES) // in last 10 minutes, raise MLT temperature to save time in mashout
          if (MLTTargetTemp != FINALMASHTEMPERATURE)
            MLTTargetTemp = FINALMASHTEMPERATURE;
        
        if (TimeInStatus > 0)
          if (IPCStatus==IPCFINISHED)
            GoToNextStatus = true;
          else 
            setConsoleMessage("Waiting IPC to finish");
      }
      break;

    case MASHFIRSTRUN:
      if (EnteringStatus) {
        if (RcpFirstWortHopping.asBoolean()) 
          Todo_FirstWortHopping.start();          
        MLTTargetTemp = NOHEAT;
        stopTimer = 0;
        ColdBankTargetTemp = coldBankTargetTempCurve[3];   
      }
      else {
        if (stopTimer==0) {
          if (BKLevel >= 2)
            stopTimer = TimeInStatus * (1 + RcpCaramelizationBoil * CARAMERILIZBOILEVAPORATION/100.) + 30* SECONDS; // add transfer time to compensate for evaporation boil and 30 seconds to ensure level sensor stability
        }
        else {
          GoToNextStatus = TimeInStatus >= stopTimer;
          if (!GoToNextStatus)
            setCountDownMessage("Transfer time remaining: %s:%s", stopTimer-TimeInStatus);
        }
      }
      break;

    case MASHMASHOUT: {
      static float restStart = 0;
      float midTemp = (MASHOUTTARGETTEMP + FINALMASHTEMPERATURE)/2;
      if (EnteringStatus) {
        restStart = 0;
        caramelizationBoilStart = 0;
        if (RcpCaramelizationBoil == 0) {
          setSubStatus(0,"No caramelization boil");
          BKTargetTemp = PREBOILTEMP;
          MLTTargetTemp = MASHOUTTARGETTEMP;           
        }
        else {
          setSubStatus(1,"Heating for caramelization boil");
          BKTargetTemp = CONSTANTHEAT;
          if (RcpCaramelizationBoil > 10 * MINUTES) // if caramelization boil is longer than 10 minutes, will heat to a middle temperature and raise slowly to final temperature
            MLTTargetTemp = midTemp;
          else
            MLTTargetTemp = MASHOUTTARGETTEMP;
        }
      }
      else {
        if (SubStatus==2 && MLTTargetTemp<MASHOUTTARGETTEMP) 
          MLTTargetTemp = midTemp + (MASHOUTTARGETTEMP - midTemp) * (TimeInStatus-caramelizationBoilStart) / (RcpCaramelizationBoil - 10*MINUTES);

        if (restStart == 0 && MLTTemp >= MASHOUTTARGETTEMP) {
          restStart = TimeInStatus;
          say("Reached mashout temp - will wait for 5 minutes");
        }
        
        BKSecondaryControl();

        if (restStart != 0) {
          if (TimeInStatus >= restStart+5*MINUTES)  { /***** Constante ****/
            GoToNextStatus = true;
            if (caramelizationBoilStart!=0)
              caramelizationBoilStart = -(TimeInStatus-caramelizationBoilStart);
            keepSubStatus = true;
          }
          else
            setCountDownMessage("Mashout ends in %s:%s", restStart+5*MINUTES - (int)TimeInStatus);
        }
      }
    }
    break;

    case PREPSPARGE: {
      static float startVolume = 0;
      static float startTemp = 0;
      if (EnteringStatus) {
        MLTTargetTemp = NOHEAT;
        startVolume = HLTVolume;
        startTemp   = HLTTemp;
      }
      else {
         BKSecondaryControl();
      
        if (SubStatus==0 && TimeInStatus > 1*MINUTES && HLTVolume < HLTTargetVolume) { 
          float HLTQq    = HLTWEIGHT*HLT_SPECIFICHEAT;
          float addVolume = ( (startTemp - SPARGETEMP) * (HLTQq + (HLTDEADVOLUME + startVolume) * WATER_SPECIFICHEAT)) /
                      (WATER_SPECIFICHEAT * (SPARGETEMP - EnvironmentTemp)); 
          float newVolume = startVolume + addVolume;
          say();
          say("Updated HLT target volume to reach sparge temp = %.1f",newVolume);
          if (newVolume > HLTMAXVOLUME) {
            SubStatus = 1;
            say("Firmware warning: Adjusting HLT target volume to maximum pot volume");
          }
          else if (HLTVolume > newVolume) {
            HLTTargetVolume = HLTVolume;
            say("Firmware warning: Cannot adjust HLT target to a lower value than is already in pot. Check temperature.");
          }
          else 
            HLTTargetVolume = newVolume;
        }

        // if reached target temperature, checks if it is possible to stop loading water
        if (!HLTStandby && HLTTemp<HLTTargetTemp+1 && HLTVolume<HLTTargetVolume) { 
          float minVol = 60; /***BKMAXVOLUME - (float)RcpGrainWeight * ((float)RcpWaterGrainRatio-2) - MLTVOLTOFALSEBOTTOM; *** Todo3: trocar essa constante ****/
          if (HLTVolume >= minVol) {
            HLTTargetVolume = HLTVolume;
          }
        }
        
        if (HLTStandby) {
          if (!RcpFirstWortHopping.asBoolean() || Todo_FirstWortHopping.neededTodoIsReady()) 
            GoToNextStatus = SubStatus == 0 || SubStatus == 3;
        }
      }
    }
    break;

    case SPARGE:
      if (EnteringStatus) {
        Todo_RunOffComplete.start();
        HLTPump.setLatency(30 * SECONDS); /**** Constante *****/
        Todo_PrepareTopUpPot.start();
        SubStatus = 0;
      }
      else {
        if (BKLevel==3 && SubStatus==0) {  // make sure pump runs at least once before whirlpool intake is under wort to avoid bubbles
          BKPump = ON;
          BKPump.setWithDelay(OFF, 7 * SECONDS); 
          SubStatus = 1;
        }
        if (BKLevel==4) { // sound alert only
          sound_Attention();
          BKPump = ON;
        }
        GoToNextStatus = Todo_RunOffComplete.TodoIsReady();
      }
      break;
  
    case KS_WAITFIRSTBOIL:
    case WAITBOIL:
      if (!EnteringStatus)  {
        GoToNextStatus = TimeInStatus>30*SECONDS && BKStandby;
        BKSecondaryControl();
      }
      break;
    
    case BOIL:
    case KS_FIRSTBOIL: {
      if (EnteringStatus) {
        if (Status==KS_FIRSTBOIL) {
          TimeInStatus = - (float)RcpKettleSourFirstBoilTime*MINUTES;
        }
        else {
          TimeInStatus = - (float)RcpBoilTime*MINUTES;
          if (RcpAddition1 >= 0) Todo_Addition1.start((RcpBoilTime-RcpAddition1)*MINUTES);
          if (RcpAddition2 >= 0) Todo_Addition2.start((RcpBoilTime-RcpAddition2)*MINUTES);
          if (RcpAddition3 >= 0) Todo_Addition3.start((RcpBoilTime-RcpAddition3)*MINUTES);
          if (RcpAddition4 >= 0) Todo_Addition4.start((RcpBoilTime-RcpAddition4)*MINUTES);
          if (RcpAddition5 >= 0) Todo_Addition5.start((RcpBoilTime-RcpAddition5)*MINUTES);
          /**** No caso de retorno de uma reinicialização, seria bom não colocar os que já passaram ou ter persistência de todo ***/
          ColdBankTargetTemp = coldBankTargetTempCurve[4];
          if (Status == BOIL) {
            setSubStatus(0,"");
          }
        }
        limitBKHeater = true;
      }
      else  {
        BKSecondaryControl();
        if (TimeInStatus>-15) { // turn off in the last 15 seconds  *** CONSTANT
          if (BKTargetTemp != NOHEAT) 
            BKTemp = NOHEAT;
        } 
        else {
          if (RcpBoilTemp < 100) { //  using sub boil
            float tt;
            float elapsedTime;

            if (Status==KS_FIRSTBOIL)
              elapsedTime = TimeInStatus + (float)RcpKettleSourFirstBoilTime*MINUTES;
            else
              elapsedTime = TimeInStatus + (float)RcpBoilTime*MINUTES;
            
            if (elapsedTime < SUBBOILRAMPTIME) { 
              tt = RcpBoilTemp - SUBBOILRAMPDELTA * (SUBBOILRAMPTIME - elapsedTime) / SUBBOILRAMPTIME;
            }
            else
              tt = RcpBoilTemp;
            tt = round(tt*10.)/10.;
            if (BKTargetTemp != tt)
              BKTargetTemp = tt;
          }
        }

        // top up water control
        if (Status == BOIL)
          switch (int(SubStatus)) {
            case 0: 
              if (Todo_PrepareTopUpPot.neededTodoIsReady()) {
                setSubStatus(1,"Heating top up water");
                TopUpHeater = ON;
              }
              break;
            case 1:
              if (!TopUpHeater.asBoolean())
                TopUpHeater = ON;

              if (TopUpWaterTemp >= WATERBOILTEMP-3) {
                stopTimer = TimeInStatus+15*MINUTES;
                setSubStatus(2,"Boiling top up water");
              }
              break;
            case 2:
              if (TimeInStatus > stopTimer) {
                setSubStatus(3,"");
                TopUpHeater = OFF;
              }
              else
                setCountDownMessage("Top up water boiling ends in %s:%s", stopTimer-TimeInStatus);
              break;
          }

        if ((GoToNextStatus = (TimeInStatus >= 0))) {
          BKTargetTemp.override();
          limitBKHeater = false;
        }
      }
    }
    break;
   
       
    case WHIRLPOOLCHILLTOHOP:
      if (EnteringStatus) {
        BKTargetTemp = RcpWhirlpoolHoppingTemp;
      }
      else
        GoToNextStatus = BKTemp <= BKTargetTemp+WHIRLPOOLDELTATEMP || RcpWhirlpoolHoppingTemp==0;
      break;
    
    case WHIRLPOOLHOPPING: {
      static bool added = false;
      if (EnteringStatus) {
        BKTargetTemp = NOHEAT;
        Todo_WhirlpoolHopAddition.start();
        added = false;
      }
      else {
        if (Todo_WhirlpoolHopAddition.neededTodoIsReady()) {
          if (!added) {
            added = true;
            TimeInStatus = - (float)RcpWhirlpoolHoppingTime * MINUTES;
          }
          else 
            GoToNextStatus = TimeInStatus>=0;
        }
      }
      break;
    }
        
    case WHIRLPOOL: 
    case KS_WHIRLPOOL: {
      bool ks = (Status==KS_WHIRLPOOL);
      if (EnteringStatus) {
        if (ks)
          BKTargetTemp = RcpKettleSourSouringTemp;
        else 
          BKTargetTemp = RcpWhirlpoolTerminalTemp;

        if (BKTargetTemp==0)
          BKTargetTemp = NOHEAT;
      }
      else {
        if (BKTargetTemp > 0 && BKTemp != NOTaTEMP && BKTemp <= (BKTargetTemp + WHIRLPOOLDELTATEMP)) { 
          BKTargetTemp = NOHEAT;
          Chiller1 = CLOSED;
        }

        if (BKTargetTemp <= 0) {
          GoToNextStatus = ks || (TimeInStatus> (RcpWhirlpoolMinimumTime-RcpWhirlpoolHoppingTime) * MINUTES);
          if (!GoToNextStatus)
            setCountDownMessage("Whirlpool ends in %s:%s", ((RcpWhirlpoolMinimumTime-RcpWhirlpoolHoppingTime) * MINUTES) - TimeInStatus); 
        }
      }
    }
    break;
      
    case KS_SOURINGSETUP:
      if (EnteringStatus)
        ;//setupLine(LINECLEANKS);
      else
        GoToNextStatus = setupLineIsReady();
    break;

    case KS_CLEAN1:
      if (EnteringStatus) {
        GoToNextStatus = false;
      }
      else {
        GoToNextStatus = false && waterInIsDone(); // para forçar depuração
      }
      break;

    case KS_CLEAN2:
      if (EnteringStatus) 
        GoToNextStatus = false; //******* refazer limpeza intermediária de KS
      else 
        GoToNextStatus = false;
      break;
      
    case KS_SOURING:
      if (EnteringStatus) {
        TimeInStatus = -4*24*HOURS;
        Todo_InoculateInKettle.start();
        Todo_InoculateInKettle.neededTodoIsReady();
        Todo_PurgeLine.start();
        Todo_PurgeLine.neededTodoIsReady();
        limitBKHeater = true;
      }
      else {
        if (int(TimeInStatus/60) % 60 == 30) {
          if (!BKPump.asBoolean())
            BKPump = ON;
        }
        else
          if (BKPump.asBoolean())
            BKPump = OFF;
          
        if ((GoToNextStatus = (TimeInStatus>0)))
          limitBKHeater = false;
      }
      break;

    case WHIRLPOOLREST:
      if (EnteringStatus) {
        TimeInStatus = -(float)RcpWhirlpoolRestTime*MINUTES;
        sendTransferStartPacket();
      }
      else {
        checkFermTempTimeout();
        if (TimeInStatus > 0)
          GoToNextStatus = Todo_AllowTransferStart.neededTodoIsReady();
      }
      break;

    case TRANSFER: {
        static unsigned long int totalTime     = 0;
        static unsigned long int lastTime      = 0;
        static float lastVolumeInBK = 0;
        static float totalTempVol = 0;

        if (EnteringStatus) {
          TimeInStatus = -15*SECONDS; /**** Constante ****/      
          if (debugging) BKLevel = 4;

          setSubStatus(1,"Count down");
          TransferVolume = 0;
          lastVolumeInBK = 0;
          totalTempVol = 0;

          if (ColdBankTargetTemp.isOverrided())
            ColdBankTargetTemp.override();
          ColdBankTargetTemp = NOTaTEMP;            

          totalTime     = 0;
          lastTime      = 0;


        }
        else {
          switch (int(SubStatus)) {
            case 1: // give time to chiller 1 release air and cool from CIP heat
              if (TimeInStatus >= 0) {   
                BKValveA = OPEN;
                Todo_TurnTransferPumpOn.start();
                sound_Attention();
                setSubStatus(2,"Wait to turn pump on");
              }
              break;

            case 2: // start transfer, turning pump on
              if (Todo_TurnTransferPumpOn.neededTodoIsReady()) {
                BKPump = ON;
                Todo_SwitchBKValves.start();
                setSubStatus(3,"Transfering via BK A");
              }
              break;

            case 3: // transfer via BK A
              if (float(BKLevel)==2) {
                Todo_SwitchBKValves.dismiss();
              }
              if (Todo_SwitchBKValves.TodoIsReady()) {
                BKValveA = CLOSED;
                BKValveB = OPEN;
                setSubStatus(4,"Transfering via BK B");
              }
              break;

            case 4:  // wait for available BK A 
              if (BKLevel==1) {
                Todo_ReportTransferEnd.start();
                if (RcpTopupWater.asBoolean())
                  setupLine(LINEBREWTOPUP);
                setSubStatus(5,"Transfering via BK B - final part");
              }
              break;

            case 5: // transfer via BK B
              if (BKLevel==0)
                sound_Attention();
              if (Todo_ReportTransferEnd.TodoIsReady()) {
                if (debugging) BKLevel = 0;
                BKValveB = CLOSED;
                BKPump.setWithDelay(OFF,WAITFORVALVE); // delay is to avoid return
                if (RcpTopupWater.asBoolean()) 
                  setSubStatus(6,"Top-up water setup");
                else 
                  setSubStatus(8,"Closing valves an rinsing whirlpool line");                
              }
              break;

            case 6: // wait for connection with top-up water
              if (setupLineIsReady()) {
                Todo_FinishTopUpWater.start();
                BKValveA = OPEN;
                BKPump = ON;
                setSubStatus(7,"Top-up water transfer");
              }
              break;

            case 7: // transfer of topup water
              if (Todo_FinishTopUpWater.neededTodoIsReady())
                setSubStatus(8,"Closing valves an rinsing whirlpool line");
              break;

            case 8:
              BKPump.setWithDelay(OFF,WAITFORVALVE); // delay is to avoid return
              BKValveA = CLOSED;
              BKValveB = CLOSED;
              waterInStart(5,WATERTARGET_BK,true);
              Chiller1 = CLOSED;
              setSubStatus(9,"Closing valves and rinsing whirlpool line");
              break;

            case 9:
              if (waterInIsDone()) {
                sendTransferEndPacket();
                RcpBatchNum = 0;
                GoToNextStatus = true;
              }
              break;
          }

          if (BKPump.asBoolean()) {
            if (MILLISDIFF(lastTime,1000L)) {
              lastTime = millis();
              float vb = TransferVolume;
              float deltaVol = vb - lastVolumeInBK;
              lastVolumeInBK = vb;
              totalTempVol = totalTempVol + deltaVol * float(Chiller2Temp);

              TransferAvgTemp = totalTempVol / vb;

              float consideredTemp = BKLevel>=2 ? TransferAvgTemp : FermenterTemp / 2;

              float tempAdjust = consideredTemp - RcpInoculationTemp; // how much needs to cool to reach inoculation temp
              if (tempAdjust > INOCULATIONTEMPALLOWEDOFFSET)
                ColdBankPump = ON;
              else if (tempAdjust < -INOCULATIONTEMPALLOWEDOFFSET || Chiller2Temp<2)
                if (ColdBankPump.asBoolean()) {
                  if (Chiller2Temp < RcpInoculationTemp+2)
                    ColdBankPump = OFF;
                }
                else
                  if (Chiller2Temp > RcpInoculationTemp+3) // if chiller is too hot, start cooling, even if average temperature is low
                    ColdBankPump = ON;
            }
          }
          else
            ColdBankPump = OFF;

          checkFermTempTimeout();
        }
      }
      break;
  
    case CIPPBSETUP:
      if (EnteringStatus)
        setupLine(LINEDETERG);
      else
        GoToNextStatus = setupLineIsReady();
      break;
      
    case CIPDSETUP:
      if (!EnteringStatus) 
        GoToNextStatus = (OnGoingProgram==PGMBREW || setupLineIsReady());
      break;
      
    case CIPDLOADHEAT: {
      if (EnteringStatus) {
        sprintf(litersMsg,"%d liters)",(int)CIPDVOLUME);
        if (CIPLineMode() != 0)
          Todo_AddDetergent.start(litersMsg);
      }
      else {
        if (CIPLineMode()==0) {
          if (BKTemp >= WATERBOILTEMP - 2 && !BKPump.asBoolean()) { //when near boil, start pump to heat line
            BKPump = ON;
          }
          GoToNextStatus = waterInIsDone() && (BKTemp >= WATERBOILTEMP || BKTemp >= BKTargetTemp - ALLOWEDTEMPOFFSET);
        }
        else
          GoToNextStatus = waterInIsDone() && Todo_AddDetergent.neededTodoIsReady() && BKTemp >= BKTargetTemp - ALLOWEDTEMPOFFSET;
      }
      break;
    }
        

    case CIPDCIRCULATION1:
    case CIPDCIRCULATION2:
    case CIPDCIRCULATION3:
      if (EnteringStatus) 
        TimeInStatus = -CIPLineCirculationTime();
      else {
        if (TimeInStatus >= 0) 
          GoToNextStatus = true;
      }
    break;
  
    case CIPPBRINSE1:
    case CIPPBRINSE2:
    case CIPPBRINSE3:
    case CIPPBRINSE4:
    case CIPDRINSE1:
    case CIPDRINSE2:
    case CIPDRINSE3: {
        if (EnteringStatus) {
          setSubStatus(1,"Rinsing");
          if (CIPLineWithSoak()) {
            if (Status==CIPDRINSE1) {
              BKTargetTemp = CIPLinePotSoakTemperatures[CIPLineMode()];
              BKDrain = CLOSED;
            }
          }
          else {
            BKDrain = OPEN;
            BKTargetTemp = NOHEAT;
          }
        }
        else {
          static float stopVolume = 0;
          switch (int(SubStatus)) {
            case 1:
              if (waterInIsDone()) {
                GoToNextStatus = true;
              }
              else {
                if (BKLevel==4) {
                  stopVolume = WaterInTarget - 6; /* constante: quanto entra de água depois de sensibilizar nivel 4 */
                  setSubStatus(2,"BK preoverflow");
                }
              }
              break;

            case 2:
              if (WaterInTarget <= stopVolume) {
                HotWaterIn = CLOSED;
                HLTWaterIn = CLOSED;
                MLTPump = OFF;
                setSubStatus(3,"BK overflow");
                if (CIPLineWithSoak()) {
                  TimeInStatus  = -CIPLinePotSoakTime();
                }
              }
              break;

            case 3: // BK is full
              if (BKLevel <= 2) {
                HotWaterIn = OPEN;
                HLTWaterIn = OPEN;
                clearConsoleMessage();
                setSubStatus(1,"Rinsing");
                BKTargetTemp = NOHEAT;
              }
              else {
                if (TimeInStatus<0)
                  setConsoleMessage("Soaking");
                else {
                  setConsoleMessage("Drain BK");
                  BKDrain = OPEN;
                }
              }
              break; 
          }
        }
      }
      break;

    case CIPDCHECKRINSE:
      if (EnteringStatus) {
        Todo_ReportRinseOK.start();
        Todo_RedoRinse.start();
      }
      else {
        if (Todo_RedoRinse.TodoIsReady()) {
          forceNextStatus = CIPDRINSE1;
          Todo_RedoRinse.dismiss();
        }
        else if (Todo_ReportRinseOK.TodoIsReady()) {
          GoToNextStatus = true;
          Todo_ReportRinseOK.dismiss();
        }
      }
      break;

    case CIPWSTART:
      if (OnGoingProgram != PGMCIPAUTORINSE) {
        if (!EnteringStatus) {
          GoToNextStatus = setupLineIsReady();
        }
      }
      else 
        if (EnteringStatus) {
          if (!setupLineIsReady())
            Todo_SetupLine.dismiss();
        }
        else 
          GoToNextStatus = waitForValve(TimeInStatus);
      break;



      
    case CIPWRINSE1:
    case CIPWRINSE2:
    case CIPWRINSE3:
    case CIPWRINSE4:
      if (!EnteringStatus) 
        GoToNextStatus = waterInIsDone();
      break;

    case CIPWAFTERRINSE:
      if(!EnteringStatus)
        GoToNextStatus = TimeInStatus > 5*MINUTES;
      break;


      //-------------------------------------

      case MANUALCONTROL:
        if (EnteringStatus) {
          if (ProgramParams > 0)
            setupLine(ProgramParams);
        }
        else
          if (TimeInStatus > 60*MINUTES)
            GoToNextStatus = true;
        break;

      case MANUALCIPLINE:
        break;


      //------ CIPFMT
      case MANUALCIPFMT:
        if (!EnteringStatus) {
          if (HotWaterIn.isOverrided()) {
            HotWaterIn.assumeOverrideAsProg();
            if ( HotWaterIn.asBoolean()) 
              waterInStart(FMTCIPVOL, WATERTARGET_FMT, true);
            else
              waterInStop();
          }
          if (ColdWaterIn.isOverrided()) {
            ColdWaterIn.assumeOverrideAsProg();
            if (ColdWaterIn.asBoolean()) 
              waterInStart(FMTCIPVOL, WATERTARGET_FMT, false);
            else
              waterInStop();
          }

          if (!waterInIsDone()) {
            setConsoleMessage("Service water: %.1f liters remaning",float(WaterInTarget));
          }
        }
        break;
  
      case CIPFMTSTART:
        if (EnteringStatus) {
          FMTCIPStartCycle();
          FMTCycle = CLOSED;
          FMTWaterIn = OPEN;
          FMTDrain = OPEN;
        }
        else 
          GoToNextStatus = setupLineIsReady(true);
        break;
  
      case CIPFMTFIRSTOPENRINSE:
        if (EnteringStatus) {
          FMTDrain = CLOSED;
          waterInStart(2*FMTCIPVOL,WATERTARGET_FMT,true); // open rinse with hot water
        }
        else {
          if (!FMTDrain.asBoolean() &&  WaterInTarget < FMTCIPVOL*1.2)  // reaching half volume - pressure and volume should be enough to open drain
            FMTDrain = OPEN;
          if (waterInIsDone) {
            Todo_ManualFMTClean.start();
            GoToNextStatus = true;
          }
        }
        break;

      case CIPFMTMANUALCLEAN:
        if (Todo_ManualFMTClean.neededTodoIsReady())
          forceNextStatus = FMTCIPNextStatus();
        break; 

      case CIPFMTSERVICEWATERRINSE:
        if (EnteringStatus) {
          FMTCycle = CLOSED;
          FMTDrain = OPEN;
          FMTPump = OFF;
          waterInStart(VOLUMETOHEATSERVICEWATER, WATERTARGET_FMT, true);
          setSubStatus(1,"Warming water");
          stopTimer = 0;
        }
        else {
          switch (int(SubStatus)) {
            case 1:
              if (waterInIsDone()) {
                if (stopTimer==0) 
                  stopTimer = TimeInStatus;
                else
                  if ((TimeInStatus - stopTimer) > FMTDRAINTIMETOWARMWATER) {
                    stopTimer = TimeInStatus;
                    FMTDrain = CLOSED;
                    setSubStatus(2,"Waiting valve after warming");
                  }
              }
              break;

            case 2:
              if (waitForValve(TimeInStatus- stopTimer) && setupLineIsReady()) {
                waterInStart(FMTCIPVOL, WATERTARGET_FMT, true);
                FMTCycle = OPEN;
                setSubStatus(3,"Loading water");
              }
              break;

            case 3:
              setConsoleMessage("Remaining %.1f liters",float(WaterInTarget));
              if (waterInIsDone()) {
                FMTPump = ON;
                stopTimer = TimeInStatus;
                setSubStatus(4,"Spraying water");
              }
              break;

            case 4: {
                int remain = ((FMTCIPCurrentStep < pureRinseSteps) ? FMTCIPPURERINSECIRCULATIONTIME : FMTCIPRINSECIRCULATIONTIME) - (TimeInStatus - stopTimer);
                if (remain<=0) {
                  FMTPump = OFF;
                  FMTDrain = OPEN;
                  FMTWaterIn = OPEN;
                  stopTimer = TimeInStatus;
                  setSubStatus(5,"Drain after spray");
                }
                else
                  setCountDownMessage("Remaining %s:%s", remain);
              }
              break;

            case 5:
              if (!waterInIsDone())
                stopTimer = TimeInStatus;
              else { 
                if ((TimeInStatus - stopTimer) > FMTDRAINTIME + WAITFORVALVE) // time to drain
                  forceNextStatus = FMTCIPNextStatus();
                setCountDownMessage("Remaining %s:%s",(FMTDRAINTIME + WAITFORVALVE) - (TimeInStatus - stopTimer));
              }
              break;
          }
        }
        break;
            
      case CIPFMTDETERGENTCIRC:
        if (EnteringStatus) {
          FMTDrain = OPEN;
          FMTCycle = CLOSED;
          FMTPump = OFF;
          waterInStart(VOLUMETOHEATSERVICEWATER, WATERTARGET_FMT, true);
          setSubStatus(1,"Warming water");
          stopTimer = 0;
        }
        else {
          switch (int(SubStatus)) {
            case 1:
              if (waterInIsDone()) {
                if (stopTimer==0) 
                  stopTimer = TimeInStatus;
                else
                  if ((TimeInStatus - stopTimer) > FMTDRAINTIMETOWARMWATER) {
                    stopTimer = TimeInStatus;
                    FMTDrain = CLOSED;
                    setSubStatus(2,"Waiting valve after warming");
                  }
              }
              break;

            case 2:
              if (waitForValve(TimeInStatus-stopTimer) && setupLineIsReady()) {
                waterInStart(FMTCIPVOL, WATERTARGET_FMT, true);
                FMTCycle = OPEN;
                sprintf(litersMsg,"%d liters)",(int)FMTCIPVOL);
                Todo_AddDetergent.start(litersMsg);
                setSubStatus(3,"Loading water");
              }
              break;

            case 3:
              if (!waterInIsDone()) {
                setConsoleMessage("remaining %.1f liters",float(WaterInTarget));
              }
              else
                if (Todo_AddDetergent.neededTodoIsReady()) {
                  FMTPump = ON;
                  stopTimer = TimeInStatus;
                  setSubStatus(4,"Spraying detergent");
                }
              break;

            case 4:
              if (TimeInStatus - stopTimer >= FMTCIPSPRAYTIME) {
                FMTPump = OFF;
                FMTDrain = OPEN;
                stopTimer = TimeInStatus;
                setSubStatus(5,"Drain after spray");
              }
              else
                setCountDownMessage("Remaining %s:%s",FMTCIPSPRAYTIME - (TimeInStatus - stopTimer));
              break;

            case 5:
              if (!waterInIsDone())
                stopTimer = TimeInStatus;
              else {
                if (TimeInStatus - stopTimer > FMTDRAINTIME + WAITFORVALVE) { // time to drain
                  FMTCycle.setWithDelay(CLOSED,FMTDRAINTIME);
                  forceNextStatus = FMTCIPNextStatus();
                }
                else
                  setCountDownMessage("Remaining %s:%s",FMTDRAINTIME+WAITFORVALVE - (TimeInStatus - stopTimer));
              }
              break;
          }
        }
        break;
        
      case CIPFMTCHECKRINSE:
        if (EnteringStatus) {
          Todo_ReportRinseOK.start();
          Todo_RedoRinse.start();
        }
        else {
          if (Todo_ReportRinseOK.TodoIsReady())
            forceNextStatus = FMTCIPNextStatus();
          else if (Todo_RedoRinse.TodoIsReady())
            forceNextStatus = FMTCIPNextStatus(CIPFMTSERVICEWATERRINSE);    
          if (forceNextStatus) {
            Todo_ReportRinseOK.dismiss();
            Todo_RedoRinse.dismiss();
          }
        }
        break;

      case CIPFMTCOMPLETE:
        if (EnteringStatus) {
          FMTWaterIn = CLOSED;
          FMTCycle = OPEN;
          FMTDrain = OPEN;
        }
        else
          GoToNextStatus = setupLineIsReady();
        break;

      case KEGCLEANERSETUP:
        if (EnteringStatus) 
          Todo_PositionKeg.start();
        else 
          GoToNextStatus = Todo_PositionKeg.neededTodoIsReady();
        break;


      case KEGCLEANERRINSE:
      case KEGCLEANERFINALRINSE:
        if (EnteringStatus) { // tira água da linha
          KegCycle = CLOSED;
          KegDrain = CLOSED;           
          stopTimer = TimeInStatus;
          waterInStart(KEGCLEANVOLUME,WATERTARGET_KEG,true);
          setSubStatus(1,"Cleaner water in");
        }
        else {
          switch (int (SubStatus)) { 
            case 1:                        // fills keg cleaner with water
              if (waterInIsDone()) { 
                stopTimer = TimeInStatus;
                if (OnGoingProgram == PGMKEGCLEAN && (int(ProgramParams) / 10 % 10 < 2)) {
                  setSubStatus(2,"Transfer to detergent tank");
                  KegDetergentValve = OPEN;
                  KegPump = ON;
                }
                else {
                  setSubStatus(4,"Drain cleaner");
                  KegDrain = OPEN;
                  KegPump = ON;
                }
              }
              break;

            case 2: {                        // transfer water from cleaner to detergent tank
                float ellapsed = TimeInStatus - stopTimer;
                if (KegLiquidSensor != KEGLIQUIDINTANK || ellapsed > KEGCLEANERTRANSFEROUTMAXTIMECLEAN) { 
                  setSubStatus(3,"Return to cleaner");
                  if (ellapsed > KEGCLEANERTRANSFEROUTMAXTIMECLEAN) {
                    say("Keg cleaner transfer to detergent tank timed out after %.1f seconds",ellapsed);
                    sound_Attention();
                  }
                  stopTimer = TimeInStatus;
                  KegDetergentValve = CLOSED;
                  KegPump = OFF;
                  KegDetergentPump = ON;
                }
              }
              break;

            case 3: {                      // transfer detergent back to cleaner
                float ellapsed = TimeInStatus - stopTimer;
                if (ellapsed > KEGTRANSFERTOCLEANERMINTIME && (KegLiquidSensor != KEGLIQUIDINRETURN || ellapsed>KEGTRANSFERTOCLEANERMAXTIME)) { 
                  if (ellapsed>KEGTRANSFERTOCLEANERMAXTIME) {
                    say("Keg cleaner transfer back timed out after %.1f seconds",ellapsed);
                    sound_Attention();
                  }
                  setSubStatus(4,"Drain cleaner");
                  stopTimer = TimeInStatus;
                  KegDetergentPump = OFF;
                  KegDrain = OPEN;
                  KegPump = ON;
                }
              }
              break;

            case 4: {                     // drain cleaner
                float ellapsed = TimeInStatus - stopTimer;
                 if (KegLiquidSensor != KEGLIQUIDINTANK || ellapsed > KEGCLEANERTRANSFEROUTMAXTIMECLEAN) {
                  if (ellapsed > KEGCLEANERTRANSFEROUTMAXTIMECLEAN) {
                    say("Keg cleaner drain timed out after %.1f seconds",ellapsed);
                    sound_Attention();
                  }
                  KegDrain = CLOSED;
                  KegPump = OFF;
                  GoToNextStatus = true;
                }
              }
              break;
          }
        }
        break;

      case KEGPREPDETERG:
        if (EnteringStatus) {
          sprintf(litersMsg,"%d liters)",(int)KEGCLEANVOLUME);
          Todo_AddDetergent.start(litersMsg);
          KegDrain = CLOSED;
          KegCycle = OPEN;
          waterInStart(KEGCLEANVOLUME,WATERTARGET_KEG,false);
        }
        else {
          if (waterInIsDone()) {
            if (Todo_AddDetergent.neededTodoIsReady()) 
              GoToNextStatus = true;
          }
        }
        break;

      case KEGDETERGSPRAY:
        if (EnteringStatus) {
          TimeInStatus = -KEGCLEANTIME;
          KegPump = ON;
        }
        else
          if (TimeInStatus >= 0) {
            GoToNextStatus = true;
          }
          else
            setCountDownMessage("Spraying: remaining %s:%s",0-TimeInStatus);
        break;

      case KEGSAVEDETERGENT:
        if (EnteringStatus) {
          KegDetergentValve = OPEN;
          KegCycle = CLOSED;
          setSubStatus(1,"Waiting Keg Cycle valve");
        }
        else {
          switch (int(SubStatus)) {
            case 1:
              if (waitForValve(TimeInStatus)) {
                setSubStatus(2,"Transfering to detergent tank");
                stopTimer = TimeInStatus;
              }
              break;
            case 2:
              kegSensorStabilityTime = 4000;
              if (KegLiquidSensor != KEGLIQUIDINTANK || (TimeInStatus - stopTimer) > KEGCLEANERTRANSFEROUTMAXTIMECLEAN) {
                if ((TimeInStatus - stopTimer) > KEGCLEANERTRANSFEROUTMAXTIMECLEAN) {
                  say("Keg detergent transfer to tank timed out after %.1f seconds",TimeInStatus - stopTimer);
                  sound_Attention();
                }
                KegDetergentValve = CLOSED;
                KegPump = OFF;
                GoToNextStatus = true;
                kegSensorStabilityTime = 1000;
              }
              break;
          }
        }
        break;

        case KEGRINSE: {
          static byte kegRinseCycle = 0;
          if (EnteringStatus) {
            kegRinseCycle = 1;
            if (OnGoingProgram==PGMKEGCLEAN)
              setSubStatus(1,"Rinse %d/%d: start",kegRinseCycle,KEGRINSECYCLES);
            else
              setSubStatus(1,"Rinse start");
          }
          else
            switch(int(SubStatus)) {
              case 1:
                waterInStart(KEGRINSEVOLUME,WATERTARGET_KEG,true);
                KegCycle = OPEN;
                if (OnGoingProgram==PGMKEGCLEAN)
                  setSubStatus(2,"Rinse %d/%d: Water in",kegRinseCycle,KEGRINSECYCLES);
                else 
                  setSubStatus(2,"Water in");
                break;

              case 2:
                if (waterInIsDone()) {
                  TimeInStatus = OnGoingProgram==PGMKEGCLEAN ? -KEGRINSETIMEDURINGCLEANING : -KEGRINSEONLYTIME;
                  KegPump = ON;
                  if (OnGoingProgram==PGMKEGCLEAN)
                    setSubStatus(3,"Rinse %d/%d: Spray",kegRinseCycle,KEGRINSECYCLES);
                  else
                    setSubStatus(3,"Spray");
                }
                break;

              case 3:
                if (TimeInStatus >= 0) {
                  KegCycle = CLOSED;
                  KegDrain = OPEN;
                  if (OnGoingProgram==PGMKEGCLEAN)
                    setSubStatus(4,"Rinse %d/%d: Wait for valve before drain",kegRinseCycle,KEGRINSECYCLES);
                  else
                    setSubStatus(4,"Wait for valve before drain");
                  stopTimer = TimeInStatus;
                }
                else
                  setCountDownMessage("Spraying: remaining %s:%s",0-TimeInStatus);
                break;

              case 4:
                if (waitForValve(TimeInStatus - stopTimer)) {
                  stopTimer = TimeInStatus;
                  if (OnGoingProgram==PGMKEGCLEAN)
                    setSubStatus(5,"Rinse %d/%d: Drain",kegRinseCycle,KEGRINSECYCLES);
                  else
                    setSubStatus(5,"Drain");
                }
                break;

              case 5:
                if (KegLiquidSensor != KEGLIQUIDINTANK || (TimeInStatus - stopTimer) > KEGCLEANERTRANSFEROUTMAXTIMECLEAN) {
                  if ((TimeInStatus - stopTimer) > KEGCLEANERTRANSFEROUTMAXTIMECLEAN) {
                    say("Keg detergent drain timed out after %.1f seconds",TimeInStatus - stopTimer);
                    sound_Attention();
                  }
                  KegDrain = CLOSED;
                  KegPump = OFF;  

                  if (kegRinseCycle < KEGRINSECYCLES && OnGoingProgram==PGMKEGCLEAN) {
                    kegRinseCycle++;
                    if (OnGoingProgram==PGMKEGCLEAN)
                      setSubStatus(1,"Rinse %d/%d: start",kegRinseCycle,KEGRINSECYCLES);
                    else
                      setSubStatus(1,"Rinse start");
                  }
                  else {
                    GoToNextStatus = true;
                  }
                }
                break;
            }
          }
          break;

        case KEGRETURNDETERGENT:
          if (EnteringStatus) {
            KegDetergentPump = ON;
            kegSensorStabilityTime = 3000;
          }
          else {
            if (TimeInStatus > KEGTRANSFERTOCLEANERMINTIME && (KegLiquidSensor != KEGLIQUIDINRETURN || TimeInStatus>KEGTRANSFERTOCLEANERMAXTIME)) { 
              if (TimeInStatus>KEGTRANSFERTOCLEANERMAXTIME) {
                say("Keg detergent return timed out after %.1f seconds",TimeInStatus);
                sound_Attention();
              }
              KegDetergentPump = OFF;
              GoToNextStatus = true;
              kegSensorStabilityTime = 1000;
            } 
          }
          break;

        case KEGASKANOTHERKEG:
          if (EnteringStatus) {
            Todo_PositionKeg.start();
            Todo_NoMoreKegs.start();
            KegCycle = OPEN;
            Buzzer = ON;
          }
          else {        
            if (int(TimeInStatus)%60 > 5*SECONDS)
              Buzzer = OFF;
            else
              Buzzer = ON;   
            if (Todo_PositionKeg.TodoIsReady()) {
              forceNextStatus = OnGoingProgram==PGMKEGCLEAN ? KEGDETERGSPRAY : KEGRINSE;
              Todo_NoMoreKegs.dismiss();
            }
            else if (Todo_NoMoreKegs.TodoIsReady()) {
              Todo_PositionKeg.dismiss();
              GoToNextStatus = true;
            }
          }
          break;

        case KEGDETERGENTDRAIN:
          if (EnteringStatus) {
            KegDrain = OPEN;
            KegCycle = CLOSED;
            KegPump = ON;
            setSubStatus(1,"Drain start");
            stopTimer = TimeInStatus;
          }
          else {
            switch (int(SubStatus)) {
              case 1:
                if (waitForValve(TimeInStatus - stopTimer)) {
                  setSubStatus(2,"Draining");
                  stopTimer = TimeInStatus;
                }
                break;

              case 2:
                if (KegLiquidSensor != KEGLIQUIDINTANK || (TimeInStatus - stopTimer) > KEGCLEANERTRANSFEROUTMAXTIMECLEAN) {
                  if ((TimeInStatus - stopTimer) > KEGCLEANERTRANSFEROUTMAXTIMECLEAN) {
                    say("Keg detergent drain timed out after %.1f seconds",TimeInStatus - stopTimer);
                    sound_Attention();
                  }
                  KegDrain = CLOSED;
                  KegPump = OFF;
                  GoToNextStatus = true;
                }
                break;
            }
          }
          break;

//--------------------- Diagnostic program
       case DIAGI2C:
          if (EnteringStatus) {
            setSubStatus(99,"wainting to start program");
          }
          else {
            if (SubStatus==99) {
              if (TimeInStatus >= 40) {
                setSubStatus(0,"I2C scan");
                diagEntry(1,"I2C devices");
                if (valveCurrentMonitorIsOn)
                  diagEntry(2,"Valve current monitor INA219","%.1fmA (threshold: %.1fmA)",0,actualCurrent,valveThresholdCurrent);
                else
                  diagEntry(2,"Valve current monitor INA219","Connection FAILED",2);
                SubStatus = 0;
              }
            }
            else {
              char buf1[20],buf2[40];
              bool res = ProcVar::checkI2CCluster(SubStatus,buf1,buf2);
              if (buf1[0]) {
                diagEntry(2,buf1,buf2,res ? 0 : 2);
                SubStatus = SubStatus+1;
              }
              else
                GoToNextStatus = true;
            }
          }
          break;

        case DIAGTHERMOMETERS:
          if (EnteringStatus) {
            diagEntry(1,"Thermometers");
            for (int i=0; i<ProcVar::numVars(); i++) {
              ProcVar *v = ProcVar::ProcVarByIndex(i);
              if (v->modeIsDallas()) {
                if (v->getProgValue() == NOTaTEMP)
                  diagEntry(2,v->tag(),"",2);
                else
                  diagEntry(2,v->tag(),"%.1fºC",0,v->getProgValue());
              }
            }
            GoToNextStatus = true;
          }
          break;

        case DIAGMOTORVALVESCOMMAND:
          if (EnteringStatus) {
            if (valveCurrentMonitorIsOn) {
              diagEntry(1,"Motor Valves Command (current and time)");
              setSubStatus(4,"Waiting for valve stabilization");
              stopTimer = 0;
            }
            else {
              diagEntry(1,"Motor Valves Command (current and time)");
              diagEntry(2,"Valve current monitor INA219","Connection FAILED",2);
              GoToNextStatus = true;
            }
          }
          else {
            static float currentSum = 0;
            static int currentCount = 0;
            static byte idx=0;

            switch (int(SubStatus)) {
              case 0:
                if (TimeInStatus >= WAITFORVALVE) {
                  setSubStatus(4,"Select first valve");
                  stopTimer = TimeInStatus;
                }
                break;
                
              case 1:
              case 3:
                if (TimeInStatus >= stopTimer + 4) {
                  setSubStatus(SubStatus+1,"Moving valves");
                  ProcVar *pv = ProcVar::ProcVarByIndex(idx);
                  *pv = (!pv->asBoolean());
                  currentSum = 0;
                  currentCount = 0;
                  stopTimer = TimeInStatus;
                }
                break;


              case 2:
              case 4: 
                {
                  bool nextVar = false;
                  if (idx != 0) { 
                    ProcVar *pv = ProcVar::ProcVarByIndex(idx);                
                    if (waitForValve((TimeInStatus-stopTimer)/2))  { // make it wait for the double of max time
                      char buf[80];
                      snprintf(buf,80,"%s - %s",pv->tag(),pv->asBoolean() ? "Open" : "Close");
                      diagEntry(2, buf, "%.1fmA<br>Time: %.1f seg", ((currentSum/currentCount > valveThresholdCurrent) && TimeInStatus-stopTimer<WAITFORVALVE+1) ? 0 : 2,
                                float(currentSum/currentCount), TimeInStatus-stopTimer); 
                      nextVar = true;
                    }
                    else {
                      currentSum += ina219.getCurrent_mA()/ina219.getBusVoltage_V()*12;
                      currentCount++;
                      delay(20);
                    }
                  }
                  if (nextVar || idx==0)
                    if (SubStatus == 2) {
                      setSubStatus(3,"Valve rest");
                      stopTimer = TimeInStatus;
                    }
                    else {
                      idx++;
                      while (idx < ProcVar::numVars() && ProcVar::ProcVarByIndex(idx)->I2CCluster()!=VALVESJ_ADDR
                                                      && ProcVar::ProcVarByIndex(idx)->I2CCluster()!=VALVESK_ADDR
                                                      && ProcVar::ProcVarByIndex(idx)->I2CCluster()!=VALVESL_ADDR
                                                      && ProcVar::ProcVarByIndex(idx)->I2CCluster()!=VALVESM_ADDR
                                                      && ProcVar::ProcVarByIndex(idx)->I2CCluster()!=VALVESN_ADDR) 
                        idx++;
                      if (idx < ProcVar::numVars()) {
                        stopTimer = TimeInStatus;
                        setSubStatus(1,"Valve rest");
                      }
                      else
                        GoToNextStatus = true;
                    }
                }
                break;
                
            }
          }
          break;
                

        case DIAGVALVES1:
          if (EnteringStatus) {
            diagEntry(1,"Valves - circuit 1");
            setSubStatus(0,"Circuit 1 initial pass thru");
            stopTimer = TimeInStatus;
          }
          else {
            switch (int(SubStatus)) {
              case 0: 
                if (TimeInStatus >= stopTimer + WAITFORVALVE + 2*FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"Circuit 1 initial pass thru", "%.1f L/min", WaterInFlow > POSITIVEFLOWREADING ? 0 : 2,WaterInFlow);
                  MLTDrain = CLOSED;
                  setSubStatus(1,"MLT drain valve");
                  stopTimer = TimeInStatus;
                }
                break;

              case 1:
                if (TimeInStatus >= stopTimer + WAITFORVALVE + FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"MLT Drain Valve", "%.1f L/min", WaterInFlow <= NOFLOWREADING ? 0 : 2,WaterInFlow);
                  MLTDrain = OPEN;
                  MLTValveB = CLOSED;
                  setSubStatus(2,"MLT Valve B");
                  stopTimer = TimeInStatus;                  
                }
                break;

              case 2:
                if (TimeInStatus >= stopTimer + WAITFORVALVE + FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"MLT Valve B", "%.1f L/min", WaterInFlow <= NOFLOWREADING ? 0 : 2,WaterInFlow);
                  MLTValveB = OPEN;
                  BKValveA = CLOSED;
                  setSubStatus(3,"BK Valve A");
                  stopTimer = TimeInStatus;                  
                }
                break;

              case 3:
                if (TimeInStatus >= stopTimer + WAITFORVALVE + FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"BK  Valve A", "%.1f L/min", WaterInFlow <= NOFLOWREADING ? 0 : 2,WaterInFlow);
                  BKValveA = OPEN;
                  WhirlpoolValve = PORT_B;
                  setSubStatus(4,"Whirlpool Valve (B side)");
                  stopTimer = TimeInStatus;                  
                }
                break;

              case 4:
                if (TimeInStatus >= stopTimer + WAITFORVALVE + 2*FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"Whirlpool Valve (B side)", "%.1f L/min", WaterInFlow <= NOFLOWREADING ? 0 : 2,WaterInFlow);
                  WhirlpoolValve = PORT_A;
                  BKWaterIn = CLOSED;
                  setSubStatus(5,"BK Water In Valve");
                  stopTimer = TimeInStatus;                  
                }
                break;

              case 5:
                if (TimeInStatus >= stopTimer + WAITFORVALVE + FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"BK Water in Valve", "%.1f L/min", WaterInFlow <= NOFLOWREADING ? 0 : 2,WaterInFlow);
                  BKWaterIn = OPEN;
                  ColdWaterIn        = CLOSED;
                  setSubStatus(6,"Cold Water In Valve");
                  stopTimer = TimeInStatus;                  
                }
                break;

              case 6:
                if (TimeInStatus >= stopTimer + WAITFORVALVE + FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"Cold Water In Valve", "%.1f L/min", WaterInFlow <= NOFLOWREADING ? 0 : 2,WaterInFlow);
                  ColdWaterIn        = OPEN;
                  setSubStatus(7,"Circuit 1 final pass thru");
                  stopTimer = TimeInStatus;                  
                }
                break;

              case 7: 
                if (TimeInStatus >= stopTimer + WAITFORVALVE + 2*FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"Circuit 1 final pass thru", "%.1f L/min", WaterInFlow > POSITIVEFLOWREADING ? 0 : 2,WaterInFlow);
                  ColdWaterIn        = CLOSED;
                  GoToNextStatus = true;
                }
                break;
            }
          }
          break;

        case DIAGVALVES2:
          if (EnteringStatus) {
            diagEntry(1,"Valves - circuit 2");
            setSubStatus(0,"Circuit 2 initial pass thru");
            stopTimer = TimeInStatus;
          }
          else {
            switch (int(SubStatus)) {
              case 0: 
                if (TimeInStatus >= stopTimer + WAITFORVALVE + 2*FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"Circuit 2 initial pass thru", "%.1f L/min", WaterInFlow > POSITIVEFLOWREADING ? 0 : 2,WaterInFlow);
                  MLTValveA = CLOSED;
                  setSubStatus(1,"MLT Valve A");
                  stopTimer = TimeInStatus;                  
                }
                break;

              case 1:
                if (TimeInStatus >= stopTimer + WAITFORVALVE + FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"MLT Valve A", "%.1f L/min", WaterInFlow <= NOFLOWREADING ? 0 : 2,WaterInFlow);
                  setSubStatus(2,"HLT Valve A passing");
                  HLTValve = PORT_A;
                  stopTimer = TimeInStatus;
                }
                break;

              case 2:
                if (TimeInStatus >= stopTimer + WAITFORVALVE + FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"HLT Valve A passing", "%.1f L/min", WaterInFlow > POSITIVEFLOWREADING ? 0 : 2,WaterInFlow);
                  HLTValve = PORT_B;
                  MLTValveA = OPEN;
                  HotWaterIn = CLOSED;
                  setSubStatus(3,"Hot Water In Valve");
                  stopTimer = TimeInStatus;                  
                }
                break;

              case 3:
                if (TimeInStatus >= stopTimer + WAITFORVALVE + FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"Hot Water In Valve", "%.1f L/min", WaterInFlow <= NOFLOWREADING ? 0 : 2,WaterInFlow);
                  HotWaterIn = OPEN;
                  setSubStatus(4,"Circuit 2 final pass thru");
                  stopTimer = TimeInStatus;                  
                }
                break;

              case 4: 
                if (TimeInStatus >= stopTimer + WAITFORVALVE + 2*FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"Circuit 2 final pass thru", "%.1f L/min", WaterInFlow > POSITIVEFLOWREADING ? 0 : 2,WaterInFlow);
                  GoToNextStatus = true;
                }
            }
          }
          break;  

        case DIAGVALVES3:
          if (EnteringStatus) {
            diagEntry(1,"Valves - circuit 3");
            setSubStatus(0,"Circuit 3 initial pass thru");
            stopTimer = TimeInStatus;
          }
          else {
            switch (int(SubStatus)) {
              case 0: 
                if (TimeInStatus >= stopTimer + WAITFORVALVE + 2*FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"Circuit 3 initial pass thru", "%.1f L/min", WaterInFlow > POSITIVEFLOWREADING ? 0 : 2,WaterInFlow);
                  HLTValve = PORT_B;
                  setSubStatus(1,"HLT Valve (B side closed)");
                  stopTimer = TimeInStatus;                  
                }
                break;

              case 1:
                if (TimeInStatus >= stopTimer + WAITFORVALVE*2 + 2*FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"HLT Valve (B side closed)", "%.1f L/min", WaterInFlow <= NOFLOWREADING ? 0 : 2,WaterInFlow);
                  setSubStatus(2,"HLT Valve (B side open)");
                  HLTValve = PORT_B;
                  MLTValveA = OPEN;
                  stopTimer = TimeInStatus;                    
                }
                break; 

              case 2: 
                if (TimeInStatus >= stopTimer + WAITFORVALVE + 2*FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"HLT Valve (B side open)", "%.1f L/min", WaterInFlow > POSITIVEFLOWREADING ? 0 : 2,WaterInFlow);
                  HLTValve = PORT_A;
                  MLTValveA = CLOSED;
                  WhirlpoolValve = PORT_A;
                  setSubStatus(3,"Whilrpool Valve (A side)");
                  stopTimer = TimeInStatus;                  
                }
                break;

              case 3:
                if (TimeInStatus >= stopTimer + WAITFORVALVE + 2*FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"Whilrpool Valve (A side)", "%.1f L/min", WaterInFlow <= NOFLOWREADING ? 0 : 2,WaterInFlow);
                  setSubStatus(4,"BK Valve B");
                  WhirlpoolValve = PORT_B;
                  BKValveB = CLOSED;
                  stopTimer = TimeInStatus;                    
                }
                break; 

              case 4:
                if (TimeInStatus >= stopTimer + WAITFORVALVE + FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"BK Valve B", "%.1f L/min", WaterInFlow <= NOFLOWREADING ? 0 : 2,WaterInFlow);
                  setSubStatus(5,"Circuit 3 final pass thru");
                  BKValveB = OPEN;
                  stopTimer = TimeInStatus;                    
                }
                break; 

              case 5: 
                if (TimeInStatus >= stopTimer + WAITFORVALVE + 2*FLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"Circuit 3 final pass thru", "%.1f L/min", WaterInFlow > POSITIVEFLOWREADING ? 0 : 2,WaterInFlow);
                  GoToNextStatus = true;
                }
                break;
            }
          }
          break;  

      case DIAGVALVESFMT:
        if (EnteringStatus) {
          diagEntry(1,"Valves - FMT Circuit");
          setSubStatus(0,"FMT circuit initial pass thru");
          stopTimer = TimeInStatus;
        }
        else {
          static float freeFlow = 0;
          switch (int(SubStatus)) {
            case 0: 
              if (TimeInStatus >= stopTimer + WAITFORVALVE + 2*FLOWREADINGSTABILIZATIONTIME) {
                diagEntry(2,"FMT circuit initial pass thru", "%.1f L/min", WaterInFlow > POSITIVEFLOWREADING ? 0 : 2,WaterInFlow);
                FMTDrain = CLOSED;
                setSubStatus(1,"FMT Drain Valve");
                stopTimer = TimeInStatus;                  
                freeFlow = WaterInFlow;
              }
              break;

            case 1:
              if (TimeInStatus >= stopTimer + WAITFORVALVE*2 + 2*FLOWREADINGSTABILIZATIONTIME) {
                diagEntry(2,"Cold Drain valve", "%.1f L/min", WaterInFlow <= NOFLOWREADING ? 0 : 2,WaterInFlow);
                setSubStatus(2,"FMT Cycle Valve");
                FMTDrain = OPEN;
                FMTCycle = CLOSED;
                stopTimer = TimeInStatus;                    
              }
              break; 

            case 2: 
              if (TimeInStatus >= stopTimer + WAITFORVALVE + 2*FLOWREADINGSTABILIZATIONTIME) {
                diagEntry(2,"FMT Cycle Valve", "%.1f L/min", WaterInFlow < freeFlow*0.7 ? 0 : 2,WaterInFlow);
                FMTCycle = OPEN;
                FMTWaterIn = CLOSED;
                setSubStatus(3,"FMT Water In");
                stopTimer = TimeInStatus;                  
              }
              break;

            case 3:
              if (TimeInStatus >= stopTimer + WAITFORVALVE + 2*FLOWREADINGSTABILIZATIONTIME) {
                diagEntry(2,"FMT Water In", "%.1f L/min", WaterInFlow <= NOFLOWREADING ? 0 : 2,WaterInFlow);
                setSubStatus(5,"FMT circuit final pass thru");
                FMTWaterIn = OPEN;
                stopTimer = TimeInStatus;                    
              }
              break; 

            case 5: 
              if (TimeInStatus >= stopTimer + WAITFORVALVE + 2*FLOWREADINGSTABILIZATIONTIME) {
                diagEntry(2,"Circuit 3 final pass thru", "%.1f L/min", WaterInFlow > POSITIVEFLOWREADING ? 0 : 2,WaterInFlow);
                GoToNextStatus = true;
              }
              break;
          }
        }
        break;  

        case DIAGPUMPSFLOWMETERS:
          if (EnteringStatus) {
            setSubStatus(0,"Meters - cold water - no pumps");
            diagEntry(1,SubStatusLabel);             
            stopTimer = TimeInStatus;
          }
          else {
            static float diagRefFlow = 0;
            static float diagRefTransferFlow = 0;

 

            switch (int(SubStatus)) {
              case 0:
                if (TimeInStatus > stopTimer + WAITFORVALVE + LONGFLOWREADINGSTABILIZATIONTIME) {
                  diagRefFlow = WaterInFlow;
                  diagRefTransferFlow = TransferFlow;
                  diagEntry(2,"Inlet metered flow", "%.1f L/min", WaterInFlow > POSITIVEFLOWREADING ? 0 : 2,WaterInFlow);
                  diagEntry(2,"HLT out metered flow", "%.1f L/min", HLT2MLTFlow > POSITIVEFLOWREADING ? 0 : 2,HLT2MLTFlow);
                  diagEntry(2,"FMT in  metered flow", "%.1f L/min", float(TransferFlow) > POSITIVEFLOWREADING ? 0 : 2,float(TransferFlow));
                  setSubStatus(1,"Meters - cold water - BK pump");
                  diagEntry(1,SubStatusLabel); 
                  BKPump = ON;
                  stopTimer = TimeInStatus;
                }
                break;

              case 1:
                if (TimeInStatus > stopTimer + WAITFORVALVE + LONGFLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"Inlet metered flow",   "%.4f L/min", WaterInFlow > POSITIVEFLOWREADING ? 0 : 2,WaterInFlow);
                  diagEntry(2,"HLT out metered flow", "%.4f L/min (dev: %.1f)", diagFlowDevStatus(HLT2MLTFlow,WaterInFlow), HLT2MLTFlow, diagFlowDeviation(HLT2MLTFlow,WaterInFlow));
                  diagEntry(2,"FMT in  metered flow", "%.4f L/min (dev: %.1f)", diagFlowDevStatus(float(TransferFlow),WaterInFlow), float(TransferFlow), diagFlowDeviation(float(TransferFlow),WaterInFlow));
                  diagEntry(2,"BK Pump effect", "%.4f L/min", WaterInFlow > diagRefFlow + 1.5 ? 0 : 2, WaterInFlow-diagRefFlow); /**** constante */
                  setSubStatus(2,"Meters - cold water - HLT pump");
                  diagEntry(1,SubStatusLabel); 
                  BKPump = OFF;
                  HLTPump = ON;
                  stopTimer = TimeInStatus;
                }
                break;                

              case 2:
                if (TimeInStatus > stopTimer + WAITFORVALVE + LONGFLOWREADINGSTABILIZATIONTIME*2) {
                  diagEntry(2,"Inlet metered flow", "%.1f L/min", WaterInFlow > POSITIVEFLOWREADING ? 0 : 2,WaterInFlow);
                  diagEntry(2,"HLT out metered flow", "%.1f L/min (dev: %.1f)", diagFlowDevStatus(HLT2MLTFlow,WaterInFlow), HLT2MLTFlow, diagFlowDeviation(HLT2MLTFlow,WaterInFlow));
                  diagEntry(2,"FMT in  metered flow", "%.1f L/min (dev: %.1f)", diagFlowDevStatus(float(TransferFlow),WaterInFlow), float(TransferFlow), diagFlowDeviation(float(TransferFlow),WaterInFlow));
                  diagEntry(2,"HLT Pump effect", "%.1f L/min", WaterInFlow > diagRefFlow + 1.5 ? 0 : 2, WaterInFlow-diagRefFlow); /**** constante */
                  setSubStatus(3,"Meters - cold water - MLT pump");
                  diagEntry(1,SubStatusLabel);
                  HLTPump = OFF;
                  MLTPump = ON;
                  stopTimer = TimeInStatus;
                }
                break;                

              case 3:
                if (TimeInStatus > stopTimer + WAITFORVALVE + LONGFLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"Inlet metered flow", "%.1f L/min", WaterInFlow > POSITIVEFLOWREADING ? 0 : 2,WaterInFlow);
                  diagEntry(2,"HLT out metered flow", "%.1f L/min (dev: %.1f)", diagFlowDevStatus(HLT2MLTFlow,WaterInFlow), HLT2MLTFlow, diagFlowDeviation(HLT2MLTFlow,WaterInFlow));
                  diagEntry(2,"FMT in  metered flow", "%.1f L/min (dev: %.1f)", diagFlowDevStatus(float(TransferFlow),WaterInFlow), float(TransferFlow), diagFlowDeviation(float(TransferFlow),WaterInFlow));
                  diagEntry(2,"MLT Pump effect", "%.1f L/min", WaterInFlow > diagRefFlow + 0.7 ? 0 : 2, WaterInFlow-diagRefFlow); /**** constante */
                  setSubStatus(4," Meters - hot water - no pumps");
                  diagEntry(1,SubStatusLabel);
                  MLTPump = OFF;
                  ColdWaterIn        = CLOSED;
                  HotWaterIn = OPEN;
                  stopTimer = TimeInStatus;
                }
                break;                

              case 4:
                if (TimeInStatus > stopTimer + WAITFORVALVE + LONGFLOWREADINGSTABILIZATIONTIME) {
                  diagEntry(2,"Inlet metered flow", "%.1f L/min", WaterInFlow > POSITIVEFLOWREADING ? 0 : 2,WaterInFlow);
                  float normalized = WaterInFlow/TransferFlow * diagRefTransferFlow;

                  diagEntry(2,"Inlet comparable to cold normalized by FMT flow","%.1f L/min (dev: %.1f%)", diagFlowDevStatus(normalized,diagRefFlow), normalized, diagFlowDeviation(normalized,diagRefFlow));
//                    devLabel(normalized,diagRefFlow).c_str(), diagFlowDevStatus(normalized,diagRefFlow));

                  diagEntry(2,"HLT out metered flow", "%.1f L/min (dev: %.1f)", diagFlowDevStatus(HLT2MLTFlow,WaterInFlow), HLT2MLTFlow, diagFlowDeviation(HLT2MLTFlow,WaterInFlow));
                  diagEntry(2,"FMT in  metered flow", "%.1f L/min (dev: %.1f)", diagFlowDevStatus(float(TransferFlow),WaterInFlow), float(TransferFlow), diagFlowDeviation(float(TransferFlow),WaterInFlow));
                  GoToNextStatus = true; // others pumps were already tested and are weaker than BK, so no need to repeat hot water with them
                }
                break;
            }
          }
          break;  

        case DIAGMLTLEVEL:
          if (EnteringStatus)
            diagEntry(1,"MLT level sensors");
          else
            GoToNextStatus = true;
          break;  

        case DIAGBKLEVEL:
          if (EnteringStatus)
            diagEntry(1,"BK level sensors");
          else
            GoToNextStatus = true;
          break;  

        case DIAGRESULT:
          setConsoleMessage("<a href='/diagresult' target='_blank'>Diagnostic results are ready</a>");
          if (TimeInStatus < 10*SECONDS && !debugging)
            sound_Attention();
          break;


        //--------------------- Flow meters calibration

        case CALIBRATEFLOWSTART:
          if (EnteringStatus) {
            setupLine(LINECALIBRATION);
          }
          else {
            GoToNextStatus = setupLineIsReady() && waitForValve(TimeInStatus);
          }
          break;

        case CALIBRATEFLOWSTABILIZATION: 
          if (EnteringStatus) {
            if (BKLevel >= 2) {
              setConsoleMessage("Please drain BK tank to proceed with flow calibration");
              sound_Attention();
              forceNextStatus = CALIBRATEFLOWSTABILIZATION;
            }
            else {
              if (OnGoingProgram == PGMCALIBRFLOWCOLD1 || OnGoingProgram == PGMCALIBRFLOWCOLD2 || OnGoingProgram == PGMCALIBRFLOWPUMPS)
                ColdWaterIn        = OPEN;
              else 
                HotWaterIn = OPEN;
              BKWaterIn = OPEN;
              if (OnGoingProgram == PGMCALIBRFLOWPUMPS) {
                BKPump = ON;
                HLTPump = ON;
              }
            }
          }
          else
            GoToNextStatus = BKLevel == 2;
          break;

        case CALIBRATEFLOWMEASUREMENT:
          if (EnteringStatus) {
            startCalibration(1+OnGoingProgram - PGMCALIBRFLOWCOLD1);
          }
          else 
            if (BKLevel==3) {
              stopCalibration();
              BKWaterIn = CLOSED;
              ColdWaterIn        = CLOSED;
              HotWaterIn = CLOSED;
              BKPump = OFF;
              HLTPump = OFF;
              GoToNextStatus = true;
            }
          break;

        case CALIBRATEFLOWEND:
          if (EnteringStatus) {
            MLTValveB = OPEN;
            MLTDrain = OPEN;
          }
          else {
            setConsoleMessage("<a href='/calibrresult' target='_blank'>Calibration results are ready</a>");
            GoToNextStatus = TimeInStatus>5*MINUTES && BKLevel<2;
          }
          break;


    }    // end of status switch

  LastStatus = Status;
  
  if (TimeInStatus != lastTimeInStatus) {
    StatusEntryTime = StatusEntryTime + lastTimeInStatus - TimeInStatus;
  }

  if (Status >= PREBREWHOTWATERPREP && Status <= MASHMASHING)
    IPCStatusMachine();  

  if (GoToNextStatus || NextStatus || forceNextStatus) {
    sound_stateChange();
    if (forceNextStatus) {
      Status = forceNextStatus;
      LastStatus = -1;
    }
    else {
      do {
        Status = Status+1;
      } while (Status<=EndStatus && skipStatus());

      if (Status > EndStatus)  {
        Status    = STANDBY;
        OnGoingProgram = PGMSTANDBY;
        Todo::resetTodos();
        say("...");
        say("################################################");
        say("#####   P R O G R A M    F I N I S H E D   #####");
        say("################################################");
        say("...");
        say("Entering standby mode...");
        say();
      }
    }
  }
}

void setSubStatus(byte subStatus, const char *subStatusLabel, ...) {
  SubStatus = subStatus;
  
  va_list args;
  va_start(args, subStatusLabel);
  vsnprintf(SubStatusLabel, sizeof(SubStatusLabel), subStatusLabel, args);
  va_end(args);
}

   
  