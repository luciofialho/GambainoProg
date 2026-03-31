#include "IPC.h"

#include "GambainoCommon.h"
#include <BrewCoreCommon.h>
#include <Variables.h>
#include <Status.h>
#include <Equipment.h>
#include <WaterHeatControl.h>
#include <Todo.h>
#include <UI.h>
#include <level.h>

long int IPCStatusEntryTime;

void IPCStatusMachine()
{
  static byte LastStatus = -1;  
  bool enteringIPCStatus = (IPCStatus != LastStatus);
  bool GoToNextStatus = false;

  static unsigned long int clock = 0;
  static byte subStatus = 0;
  static float lastColdBankVolume = 0;

  if (IPCStatus.isOverrided()) {                   
    enteringIPCStatus = 1;
    IPCStatus.assumeOverrideAsProg();
  }
  
  if (enteringIPCStatus) {
      say("#### IPC Entering status %i: %s",int(IPCStatus), IPCStatusNames[int(IPCStatus)]);
      
      IPCStatusEntryTime = millis();
  }

  if (IPCTimeInStatus.isOverrided()) {
    IPCStatusEntryTime = millis() - long(float(IPCTimeInStatus)) * 1000L;
    IPCTimeInStatus.assumeOverrideAsProg();
  }
  IPCTimeInStatus = (millis()-IPCStatusEntryTime)/1000.f;

  switch (int(IPCStatus)) {
    case IPCNOTSTARTED: break;

    case IPCPREBREWWATERFLUSH:
        if (enteringIPCStatus) {
          subStatus = 0;
          FMTDrain = OPEN;
          FMTCycle = CLOSED;
          FMTPump = OFF;
        }
        else {
          switch(subStatus) {
            case 0:
              setConsoleMessage("FMT: Waiting for valve");
              waterInStart(FMTCIPVOL, WATERTARGET_FMT, true);
              subStatus = 1;
              break;
            case 1:
              if (waterInIsDone()) {
                GoToNextStatus = true;
              }
              else
                setConsoleMessage("FMT: Flushing water: remaining %.1f liters",float(WaterInTarget));
              break;
          }
        }
        break;

    case IPCPREBREWFMTDETERG:
    case IPCPREBREWFMTRINSE1:
    case IPCPREBREWFMTRINSE2:
    case IPCPREBREWFMTRINSE3: {
      if (enteringIPCStatus) {
        subStatus = 0;
        FMTDrain = CLOSED;
        FMTCycle = OPEN;
        FMTPump = OFF;
        if (debugging && BKTemp>WATERBOILTEMP)
          GoToNextStatus = true; // skip in debug if water is already hot
      }
      else {
        switch(subStatus) {
          case 0:
            setConsoleMessage("FMT: Waiting for valve");
            if (waitForValve(IPCTimeInStatus) && waterInIsDone()) { // avoid conflict in water intake
              if (IPCStatus == IPCPREBREWFMTDETERG) {
                waterInStart(FMTCIPVOL, WATERTARGET_FMT, true);
                sprintf(litersMsg,"%d liters)",(int)FMTCIPVOL);
                Todo_AddDetergent.start(litersMsg);
              }
              else 
                waterInStart(FMTCIPVOL, WATERTARGET_FMT, IPCStatus == IPCPREBREWFMTRINSE1 || IPCStatus == IPCPREBREWFMTRINSE2);
              subStatus = 1;
            }
            break;

          case 1: // wait to load water
            if (waterInIsDone()) {
              if  (IPCStatus !=  IPCPREBREWFMTDETERG || Todo_AddDetergent.neededTodoIsReady()) {
                clock = IPCTimeInStatus;
                subStatus++;
                FMTPump = ON;
              }            
            }
            else
              setConsoleMessage("FMT: Loading water: remaining %.1f liters",float(WaterInTarget));
            break;

          case 2: { // spray
              int remain;
              switch (int(IPCStatus)) {
                case IPCPREBREWFMTDETERG: 
                  remain = FMTCIPSPRAYTIME;
                  break;
                case IPCPREBREWFMTRINSE1:
                case IPCPREBREWFMTRINSE2:
                  remain = FMTCIPPURERINSECIRCULATIONTIME;
                  break;
                default:
                  remain = FMTCIPPURERINSECIRCULATIONTIME/2;
                  break;
              }
              remain = remain - (IPCTimeInStatus-clock);

              if (remain>=0) 
                setCountDownMessage("FMT spray: remaining %s:%s", remain);
              else {
                FMTPump = OFF;
                FMTDrain = OPEN;
                clock = IPCTimeInStatus;
                subStatus++;
              }
            }
            break;

          case 3: // drain
            if ((IPCTimeInStatus - clock) > (FMTDRAINTIME + WAITFORVALVE * (Status==IPCPREBREWFMTRINSE3 ? 2 : 1))) { 
              GoToNextStatus = true;
            }
            else
              setCountDownMessage("Draining FMT: remaining %s:%s",FMTDRAINTIME+WAITFORVALVE * (Status==IPCPREBREWFMTRINSE3 ? 2 : 1) - (IPCTimeInStatus-clock));
            break;
        }
      }
    }
    break; 

    case IPCPREBREWPREPSANIT:
      if (enteringIPCStatus) {
        subStatus = 0;
        FMTDrain = CLOSED;
        FMTPump = OFF;
        FMTCycle = OPEN;
      }
      else {
        switch (subStatus) {
          case 0:
            setConsoleMessage("Waiting for valve");
            if (waitForValve(IPCTimeInStatus)) {
              waterInStart(FMTCIPVOL, WATERTARGET_FMT, false);
              sprintf(litersMsg,"%d liters)",(int)FMTCIPVOL);
              Todo_Sanitizer.start(litersMsg);
              subStatus++;
            }
            break;

          case 1:
            setConsoleMessage("Loading water: remaining %.1f liters",float(WaterInTarget));
            if (waterInIsDone()) {
              clock = IPCTimeInStatus;
              subStatus++;
            }
            break;     

          case 2:
            if (Todo_Sanitizer.neededTodoIsReady()) 
              subStatus++;
            break;

          case 3:
            if (waitForValve(IPCTimeInStatus - clock)) {
              waterInStart(COLDBANKVOLUME, WATERTARGET_COLDBANK, false);
              GoToNextStatus = true;
              FMTPump = ON;
              clock = 0;
              lastColdBankVolume = COLDBANKVOLUME;
            }
            break;
        }   
      }
      break;

    case IPCPREBREWLOADCOLDBANK:
      if (IPCTimeInStatus>5*60 && FMTPump.asBoolean()) // first homogenization of sanitizer
        FMTPump = OFF;
      if ( waterInIsDone()) {
        GoToNextStatus = true;
      }
      else {
        if (IPCTimeInStatus-clock > 30) {
          if (WaterInTarget == lastColdBankVolume) {
            GoToNextStatus = true;
          }
          clock = IPCTimeInStatus;
          lastColdBankVolume = WaterInTarget;
        }
      }
      if (GoToNextStatus) {
        if (FMTPump.asBoolean()) 
          FMTPump = OFF;
        waterInStop();
        ColdBankTargetTemp = coldBankTargetTempCurve[0];
      }
      break;

    case IPCWAITBREWSTART:
      if (enteringIPCStatus) {
        FMTDrain = CLOSED;
        FMTPump = OFF;
        FMTCycle = OPEN;
        FMTWaterIn = CLOSED;
      }
      if (((long int)IPCTimeInStatus) % (60 * 60) < (3 * 60)) { // 3 minutes every hour 
        if (!FMTPump.asBoolean())
          FMTPump =  ON;
      }
      else 
        if (FMTPump.asBoolean())
          FMTPump = OFF;
      GoToNextStatus = Status>BREWWAIT;
      break;

    case IPCPREBREWSPRAYSANIT:
      if (enteringIPCStatus) {
        FMTPump = ON;
        FMTDrain = CLOSED;
        FMTCycle = OPEN;
        FMTWaterIn = CLOSED;
      }        
      else {
        if (GoToNextStatus = IPCTimeInStatus >= PREBREWCIP_FMTSANITIZER) {
          FMTPump = OFF;
          FMTCycle = CLOSED;
        }
        else 
          setCountDownMessage("Spraying sanitizer in FMT: remaining %s:%s",PREBREWCIP_FMTSANITIZER - IPCTimeInStatus);
      }
      break;

    case IPCPAUSEBEFORESANITLINE:
      if (enteringIPCStatus) {
        FMTDrain = CLOSED;
        FMTPump = OFF;
        FMTCycle = CLOSED;
        FMTWaterIn = CLOSED;
      }
      else 
        if (LineConfiguration == LINEBREW && setupLineIsReady) 
          GoToNextStatus = true;
      break;

    case IPCSANITTOLINE: {
      if (enteringIPCStatus) {
        BKValveA = OPEN;
        Todo_TransferSanitizerToLine.start();
        FMTDrain = CLOSED;
        FMTPump = OFF;
        FMTCycle = CLOSED;
        FMTWaterIn = CLOSED;
        MLTValveB = CLOSED;
        BKValveB = CLOSED;
        WhirlpoolValve = PORT_B;
      }
      else {
        GoToNextStatus = Todo_TransferSanitizerToLine.neededTodoIsReady();
      }
    }
    break;

    case IPCSANITINLINE: {
      if (!enteringIPCStatus) {
        GoToNextStatus = float(IPCTimeInStatus) >= 25*MINUTES; /* constante */
        if (!GoToNextStatus)
          setCountDownMessage("Sanitizer in line: remaining %s:%s", 25*MINUTES - float(IPCTimeInStatus));
      }
    }
    break;

    case IPCPURGELINE: 
      if (enteringIPCStatus) {
        MLTValveB = CLOSED;
        BKValveA = OPEN;
        BKValveB = CLOSED;
        WhirlpoolValve = PORT_B;
        Todo_PurgeLine.start();
      }
      else 
        GoToNextStatus = Todo_PurgeLine.neededTodoIsReady();          
      break;

    case IPCCLEANBK:
      if (enteringIPCStatus) {
        Todo_CleanBK.start();
        MLTValveB = CLOSED;
        BKValveA = CLOSED;
        BKValveB = CLOSED;
        WhirlpoolValve = PORT_A;
        FMTDrain = OPEN;
        FMTCycle = OPEN;
      }
      else
        GoToNextStatus = Todo_CleanBK.neededTodoIsReady();
      break;

   case IPCFINISHED: 
      if (debugging && enteringIPCStatus) BKLevel = 0;
      break;
  }

  LastStatus = IPCStatus;
  if (GoToNextStatus) {
    switch (int(IPCStatus)) {
      case IPCPREBREWWATERFLUSH:
        if (RcpFermenterCleaning==2) 
          IPCStatus = IPCPREBREWPREPSANIT;
        else
          IPCStatus = IPCPREBREWFMTRINSE1; 
        break;

      default:
        IPCStatus = IPCStatus + 1;
    }
  }
}


void IPCStart()
{
  if (RcpFermenterCleaning==0)
    IPCStatus = IPCPREBREWFMTDETERG;
  else
    IPCStatus = IPCPREBREWWATERFLUSH;
}