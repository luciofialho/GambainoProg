#include "GambainoCommon.h"
#include <BrewCoreCommon.h>
#include <Equipment.h>
#include <Variables.h>
#include <Status.h>
#include <WaterHeatControl.h>
#include <Todo.h>
#include <level.h>


void statusEntrySetup() {
    switch (int(Status)) {
        case STANDBY:
        case DIAGMOTORVALVESCOMMAND:
        case DIAGRESULT:
          setupLine(LINEFMTCIP);
          CirculationMode    = CIRCNONE;
            HLTValve         = PORT_A;
            HLTPump          = OFF;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_B;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = (millis()>3*MINUTESms) ? OPEN : CLOSED; // after boot, keep closed. After other program like rinse, open
          waterInStop();
          FMTDrain           = MLTDrain;
          FMTCycle           = OPEN;
          FMTPump           = OFF;
          if (debugging) BKLevel = 0;
          TopUpHeater        = OFF;
          ColdWaterIn        = CLOSED;
          HotWaterIn         = CLOSED;
          KegPump = OFF;
          KegDrain = CLOSED;
          KegDetergentValve = CLOSED;

          break;
            
        case MANUALCONTROL:
          // Manual control does not get processed: keeps last configuration
          // but it also does not set circulationMode, so level reading is forced to be constant
          setupLine(LINEBREW);
          HLTPriority        = 'V';
          break;

        case PREBREWSETUP:
          setupLine(LINEPREBREW);
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_A;
            HLTPump          = OFF;
            MLTValveA        = CLOSED;
            MLTValveB        = OPEN;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = OPEN;
          BKValveB           = OPEN;
          WhirlpoolValve     = PORT_A;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStop();
          TopUpHeater        = OFF;
          break;
          
        case PREBREWHOTWATERPREP:
          setupLine(LINEPREBREW);        
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_A;
            HLTPump          = OFF;
            MLTValveA        = CLOSED;
            MLTValveB        = OPEN;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = OPEN;
          BKValveB           = OPEN;
          WhirlpoolValve     = PORT_A;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStart(CIPHOTWATERVOLUME, WATERTARGET_BK, true);
          TopUpHeater        = OFF;
          break;

        case PREBREWHOTWATERCIRC1:
          setupLine(LINEPREBREW);
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_A;
            HLTPump          = ON;
            MLTValveA        = CLOSED;
            MLTValveB        = OPEN;
            MLTPump          = ON;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = CIPHOTWATERTEMP;
          BKValveA           = OPEN;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_B;
          BKPump             = ON;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          // waterin managed by IPC
          TopUpHeater        = OFF;
          break;

        case PREBREWHOTWATERCIRC2:
          setupLine(LINEPREBREW);        
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_A;
            HLTPump          = OFF;
            MLTValveA        = OPEN;
            MLTValveB        = OPEN;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = CIPHOTWATERTEMP;
          BKValveA           = CLOSED;
          BKValveB           = OPEN;
          WhirlpoolValve     = PORT_A;
          BKPump             = OFF; BKPump.setWithDelay(ON,WAITFORVALVE);
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          // waterin managed by IPC
          TopUpHeater        = OFF;
          break;

        case BREWWAIT:
          setupLine(LINEINFUSION);        
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_A;
            HLTPump          = OFF;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = 5; // to check if valves are closed
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_B;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          // waterIn managed by IPC
          TopUpHeater        = OFF;
          break;

        case HLTLOADANDDEAERATE:
          setupLine(LINEINFUSION);        
          CirculationMode    = CIRCNONE;
            HLTValve         = PORT_A;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;
          HLTTargetTemp      = LODOWATERTEMPERATURE+5; 
          HLTTargetVolume    = HLTMAXVOLUME; 
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_B;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          // waterIn managed by IPC
          TopUpHeater        = OFF;
          break;


        case PREPWATERFORINFUSION:
          setupLine(LINEINFUSION);        
          CirculationMode    = CIRCNONE;
            HLTValve         = PORT_A;
            MLTValveA        = OPEN;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;

          HLTTargetTemp      = (RcpRampGrainWeight > 0 && RcpRampTime > 0) ? HLTRampTemp : HLTInfusionTemp;
          HLTTargetVolume    = HLTMAXVOLUME; 
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = OPEN;
          WhirlpoolValve     = PORT_A;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStop();
          MLTDrain           = OPEN;
          // waterIn managed by IPC
          TopUpHeater        = OFF;
          break;
            
        case RAMPINFUSION: 
          setupLine(LINEINFUSION);        
          CirculationMode    = CIRCHLTTOMLT;
          HLTTargetTemp      = HLTInfusionTemp;
          HLTTargetVolume    = HLTMAXVOLUME-rampInfusionVolume;
          HLTPriority        = 'T';
          //BKTargetTemp       
          //BKValveA           
          //BKValveB           
          //WhirlpoolValve     
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          ColdWaterIn        = CLOSED;
          HotWaterIn         = CLOSED;
          // waterIn managed by IPC
          TopUpHeater        = OFF;
          break;
            
        case RAMPREST: 
          setupLine(LINEINFUSION);        
          CirculationMode    = CIRCNONE;
            HLTValve         = PORT_A;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;

          HLTTargetTemp      = HLTInfusionTemp + HLTDeltaTemp;
          HLTTargetVolume    = HLTMAXVOLUME-rampInfusionVolume;
          HLTPriority        = 'T';
          //BKTargetTemp       
          //BKValveA           
          //BKValveB           
          //WhirlpoolValve     
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          // waterIn managed by IPC
          TopUpHeater        = OFF;
          break;

        case INFUSION:
          setupLine(LINEINFUSION);        
          CirculationMode    = CIRCHLTTOMLT;
          HLTTargetTemp      = HLTInfusionTemp + HLTDeltaTemp;
          HLTTargetVolume    = HLTMAXVOLUME - rampInfusionVolume - infusionVolume-0.1;
          HLTPriority        = 'V';
          //BKTargetTemp       
          //BKValveA           
          //BKValveB           
          WhirlpoolValve     = PORT_B;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          // waterIn managed by IPC
          TopUpHeater        = OFF;
          break;
            
        case MASHGRAINREST:
          setupLine(LINEBREW);        
          CirculationMode    = CIRCNONE; 
            HLTValve         = PORT_A;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;

          HLTTargetTemp      = RcpMashTemp + HeatAdditiveCorrection;        
          HLTTargetVolume    = BKMAXVOLUME - (rampInfusionVolume + infusionVolume) + RcpGrainWeight + HLTDEADVOLUME + MLTVOLUMELTOFALSEBOTTOM + 20; // *2 estimates constante  //HLTVolume + (HLTMAXVOLUME-HLTVolume)/1.027; /*** Constante: dilatação da água que vai entrar ***/
                               if (HLTTargetVolume>HLTMAXVOLUME) HLTTargetVolume = HLTMAXVOLUME;
          HLTPriority        = 'T';
          BKTargetTemp       = NOHEAT;
          BKValveA           = OPEN;
          BKValveB           = CLOSED;
          //WhirlpoolValve     = PORT_A;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          // waterIn managed by IPC
          TopUpHeater        = OFF;
          break;

        case MASHMASHING:
          setupLine(LINEBREW);                
          CirculationMode    = CIRCHEAT;
          //HLTTargetTemp   
          HLTTargetVolume    = BKMAXVOLUME - (rampInfusionVolume + infusionVolume) + RcpGrainWeight + HLTDEADVOLUME + MLTVOLUMELTOFALSEBOTTOM + 20; // *2 estimates constante  //HLTVolume + (HLTMAXVOLUME-HLTVolume)/1.027; /*** Constante: dilatação da água que vai entrar ***/
                               if (HLTTargetVolume>HLTMAXVOLUME) HLTTargetVolume = HLTMAXVOLUME;
          HLTPriority        = 'T';
          BKTargetTemp       = NOHEAT;
          BKValveA           = OPEN;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_B;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          // waterIn managed by IPC
          TopUpHeater        = OFF;
          break;
            
        case MASHFIRSTRUN:
          setupLine(LINEBREW);
          CirculationMode    = CIRCRUNOFF;
          HLTTargetTemp      = SPARGETEMP;        
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'T';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = OPEN;
          WhirlpoolValve     = PORT_A;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStop();
          TopUpHeater        = OFF;
          if (debugging) BKLevel = 0;
          break;
            
        case MASHMASHOUT:
          setupLine(LINEBREW);
          CirculationMode    = CIRCHEAT;
          //HLTTargetTemp      
          HLTTargetVolume    = HLTMAXVOLUME;
          HLTPriority        = 'T';
          BKTargetTemp       = NOHEAT; // will change accordingly to caramelization boil in main status machine
          BKValveA           = CLOSED;
          BKValveB           = OPEN;
          WhirlpoolValve     = PORT_A;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStop();
          TopUpHeater        = OFF;
          if (debugging) BKLevel = 2;
          break;                    

        case PREPSPARGE:
          setupLine(LINEBREW);
          CirculationMode    = CIRCNONE;
            HLTValve         = PORT_A;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;          
          HLTTargetTemp      = SPARGETEMP;        
          HLTTargetVolume    = HLTMAXVOLUME;
          HLTPriority        = 'V';
//        BKTargetTemp       // no change
          BKValveA           = CLOSED;
          BKValveB           = OPEN;
          WhirlpoolValve     = PORT_A;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          waterInStop();
          TopUpHeater        = OFF;
          break;                    

            
        case SPARGE:
          setupLine(LINEBREW);
          CirculationMode    = CIRCRUNOFF;
          HLTTargetTemp      = SPARGETEMP;        
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = ((RcpBoilTemp >= 100) ? PREBOILTEMP : float(RcpBoilTemp))-SUBBOILRAMPDELTA;
          BKValveA           = OPEN;
          BKValveB           = OPEN;
          WhirlpoolValve     = PORT_A;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStop();
          TopUpHeater        = OFF;
          break;                    

        case KS_WAITFIRSTBOIL:
        case WAITBOIL:
        case KS_FIRSTBOIL:
        case BOIL:
          setupLine(LINEBREW);
          CirculationMode    = CIRCNONE;
            HLTValve         = PORT_A;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;          
          HLTTargetTemp      = NOHEAT;     
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = (RcpBoilTemp >= 100) ? CONSTANTHEAT : float(RcpBoilTemp)- SUBBOILRAMPDELTA;
          BKValveA           = OPEN;
          BKValveB           = CLOSED;
          BKPump             = Status == KS_FIRSTBOIL || Status == BOIL; // changes in main state machine
          WhirlpoolValve     = PORT_A;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain     .setWithDelay(OPEN, WAITFORVALVE);
          waterInStop();
          TopUpHeater        = OFF;

          if (debugging) BKLevel = 4;
          break;
          
        case WHIRLPOOLCHILLTOHOP:
          setupLine(LINEBREW);
          CirculationMode    = CIRCNONE;
            HLTValve         = PORT_A;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;          
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = OPEN;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_A;
          BKPump             . setWithDelay(ON,  WAITFORVALVE);
          Chiller1           = OPEN;
          ColdBankValve      = PORT_A;
          MLTDrain           = OPEN;
          waterInStop();
          TopUpHeater        = OFF;
          break;

        case WHIRLPOOLHOPPING:
          setupLine(LINEBREW);
          CirculationMode    = CIRCNONE;
            HLTValve         = PORT_A;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;          
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = OPEN;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_A;
          BKPump             = ON;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStop();
          ColdBankWaterIn    = CLOSED;
          TopUpHeater        = OFF;
          break;

        case WHIRLPOOL:
        case KS_WHIRLPOOL:
          setupLine(LINEBREW);
          CirculationMode    = CIRCNONE;
            HLTValve         = PORT_A;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;          
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = OPEN;
          BKValveB           = Status == KS_WHIRLPOOL ? OPEN : CLOSED;
          WhirlpoolValve     = PORT_A;
          BKPump             = ON;
          Chiller1           = OPEN;
          ColdBankValve      = PORT_A;
          MLTDrain           = OPEN;
          waterInStop();
          TopUpHeater        = OFF;
          break;

        case KS_SOURINGSETUP:
        case KS_CLEAN1:
          setupLine(LINEBREW);
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_A;
            HLTPump          = OFF;
            MLTValveA        = OPEN;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = RcpKettleSourSouringTemp;
          BKValveA           = CLOSED;
          BKValveB           = CLOSED;
          BKPump             = OFF;
          WhirlpoolValve     = PORT_A;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStop();
          TopUpHeater        = OFF;
          break;      

        case KS_CLEAN2:
          setupLine(LINEBREW);
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_A;
            HLTPump          = OFF;
            MLTValveA        = OPEN;
            MLTValveB        = OPEN;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = RcpKettleSourSouringTemp;
          BKValveA           = CLOSED;
          BKValveB           = CLOSED;
          BKPump             = OFF;
          WhirlpoolValve     = PORT_B;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStop();
          TopUpHeater        = OFF;
          break;      

        case KS_SOURING:
          setupLine(LINEBREW);
          CirculationMode    = CIRCNONE;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = RcpKettleSourSouringTemp;
          BKValveA           = OPEN;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_B;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStop();
          TopUpHeater        = OFF;
          break;
        
        case WHIRLPOOLREST:        
          setupLine(LINEBREW);
          CirculationMode    = CIRCNONE;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_B;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStop();
          TopUpHeater        = OFF;
          break;

        case TRANSFER:        
          setupLine(LINEBREW);
          CirculationMode    = CIRCNONE;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;    // will be open after some time in main status machine
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_B;
          BKPump             = OFF;       // will be turned on after some time in main status machine
          Chiller1           = OPEN;
          ColdBankValve      = PORT_B;
          MLTDrain           = CLOSED;
          waterInStop();
          TopUpHeater        = OFF;
          break;
            
        case CIPPBSETUP:
        case CIPDSETUP:
          setupLine(LINEDETERG);
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_A;
            HLTPump          = OFF;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_B;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStop();
          TopUpHeater        = OFF;
          BKDrain            = CLOSED;
          break;
          
        case CIPDLOADHEAT:
        setupLine(LINEDETERG);
          CirculationMode    = CIRCCIP;
          HLTValve           = PORT_A;
          HLTPump            = OFF;
          MLTValveA          = OPEN;
          MLTValveB          = OPEN;
          MLTPump            = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = CIPLineTemperature();
          BKValveA           = OPEN;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_B;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStart(CIPDVOLUME, WATERTARGET_BK, CIPLineTemperature() > 35);
          TopUpHeater        = OFF;
          BKDrain            = CLOSED;
          break;
            
        case CIPDCIRCULATION1: 
          setupLine(LINEDETERG);
          CirculationMode    = CIRCCIP;
          HLTValve           = PORT_A;
          HLTPump            = OFF;
          MLTValveA          = OPEN;
          MLTValveB          = OPEN;
          MLTPump            = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = CIPLineTemperature();
          BKValveA           = OPEN;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_A;
          BKPump             .setWithDelay(ON, WAITFORVALVE);
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStop();
          TopUpHeater        = OFF;
          BKDrain            = CLOSED;
          break;

        case CIPDCIRCULATION2: 
          setupLine(LINEDETERG);
          CirculationMode    = CIRCCIP;
          HLTValve           = PORT_A;
          HLTPump            .setWithDelay(ON, WAITFORVALVE);
          MLTValveA          = OPEN; // dont care, no reason to change
          MLTValveB          = OPEN;
          MLTPump            .setWithDelay(ON, WAITFORVALVE);
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = CIPLineTemperature();
          BKValveA           = CLOSED;
          BKValveB           = OPEN;
          WhirlpoolValve     = PORT_B;
          BKPump             = ON;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStop();
          TopUpHeater        = OFF;
          BKDrain            = CLOSED;
          break;

        case CIPDCIRCULATION3: 
          setupLine(LINEDETERG);
          CirculationMode    = CIRCCIP;
          HLTValve           = PORT_B;
          HLTPump            = ON;
          MLTValveA          = OPEN; // dont care, no reason to change
          MLTValveB          = OPEN;
          MLTPump            = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = CIPLineTemperature();
          BKValveA           = CLOSED;
          BKValveB           = OPEN;
          WhirlpoolValve     = PORT_B;
          BKPump             = ON;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStop();
          TopUpHeater        = OFF;
          BKDrain            = CLOSED;
          break;


        case CIPPBRINSE1:
        case CIPDRINSE1: 
          setupLine(LINEDETERG);
          CirculationMode    = CIRCCIP;
          HLTValve           = PORT_A;
          HLTPump            = OFF;
          MLTValveA          = CLOSED;
          MLTValveB          = CLOSED;
          MLTPump            = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          //BKTargetTemp       managed in main state machine
          BKValveA           = CLOSED;
          BKValveB           = OPEN;
          WhirlpoolValve     = PORT_B;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStart(CIPLineRinseVolume(), WATERTARGET_HLT, true);
          TopUpHeater        = OFF;
          //BKDrain           = handled in main state machine
          break;
          
        case CIPPBRINSE2:
        case CIPDRINSE2: 
          setupLine(LINEDETERG);
          CirculationMode    = CIRCCIP;
          HLTValve           = PORT_A;
          HLTPump            = OFF;
          MLTValveA          = OPEN;
          MLTValveB          = CLOSED;
          MLTPump            .setWithDelay(ON, WAITFORVALVE);//urgente confirmar
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          //BKTargetTemp       managed in main state machine
          BKValveA           = OPEN;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_A;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStart(CIPLineRinseVolume(), WATERTARGET_HLT, true);
          TopUpHeater        = OFF;
          //BKDrain           = handled in main state machine
          break;

        case CIPPBRINSE3:
        case CIPDRINSE3: 
          setupLine(LINEDETERG);
          CirculationMode    = CIRCCIP;
          HLTValve           = PORT_B;
          HLTPump            = OFF;
          MLTValveA          = OPEN;
          MLTValveB          = OPEN;
          MLTPump            = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          //BKTargetTemp       managed in main state machine
          BKValveA           = CLOSED;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_A;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStart(CIPLineRinseVolume(), WATERTARGET_HLT, true);
          TopUpHeater        = OFF;
          //BKDrain           = handled in main state machine
          break;

        case CIPPBRINSE4:
          setupLine(LINEDETERG);
          CirculationMode    = CIRCCIP;
          HLTValve           = PORT_A;
          HLTPump            = OFF;
          MLTValveA          = OPEN;
          MLTValveB          = OPEN;
          MLTPump            = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = CIPLineTemperature();
          BKValveA           = OPEN;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_B;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStart(CIPLineRinseVolume(), WATERTARGET_BK, true);
          TopUpHeater        = OFF;
          //BKDrain           = handled in main state machine
          break;

        case CIPDCHECKRINSE: 
          setupLine(LINEDETERG);
          CirculationMode    = CIRCCIP;
          HLTValve           = PORT_A;
          HLTPump            = OFF;
          MLTValveA          = OPEN;
          MLTValveB          = CLOSED;
          MLTPump            = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = OPEN;
          BKValveB           = OPEN;
          WhirlpoolValve     = PORT_A;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          waterInStop();
          TopUpHeater        = OFF;
          break;

          
//---------------------

        case CIPWSTART:
          setupLine(LINERINSE);
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_A; 
            HLTPump          = OFF;
            MLTValveA        = OPEN;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = CLOSED;
          BKPump             = OFF;
          WhirlpoolValve     = PORT_A;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = OPEN;
          waterInStop();
          TopUpHeater        = OFF;
          break;

        case CIPWRINSE1:
          setupLine(LINERINSE);
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_A; 
            HLTPump          = OFF;
            MLTValveA        = OPEN;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = CLOSED;
          BKPump             = OFF;
          WhirlpoolValve     = PORT_A;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = OPEN;
          waterInStart(CIPWVolumes[OnGoingProgram==PGMCIPAUTORINSE][0], WATERTARGET_BK, true);
          TopUpHeater        = OFF;
          break;

        case CIPWRINSE2:
          setupLine(LINERINSE);
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_A; 
            HLTPump          = OFF;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = OPEN;
          BKPump             = OFF;
          WhirlpoolValve     = PORT_B;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = OPEN;
          waterInStart(CIPWVolumes[OnGoingProgram==PGMCIPAUTORINSE][1], WATERTARGET_BK, true);
          TopUpHeater        = OFF;
          break;        

        case CIPWRINSE3:
          setupLine(LINERINSE);
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_B;
            HLTPump          = OFF;
            MLTValveA        = CLOSED;
            MLTValveB        = OPEN;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = OPEN;
          BKValveB           = CLOSED;
          BKPump             = OFF;
          WhirlpoolValve     = PORT_B;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = OPEN;
          waterInStart(CIPWVolumes[OnGoingProgram==PGMCIPAUTORINSE][2], WATERTARGET_HLT, true);
          TopUpHeater        = OFF;
          break;                 

        case CIPWRINSE4:
          setupLine(LINERINSE);
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_B;
            HLTPump          = OFF;
            MLTValveA        = OPEN;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = CLOSED;
          BKPump             = OFF;
          WhirlpoolValve     = PORT_A;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = OPEN;
          waterInStart(CIPWVolumes[OnGoingProgram==PGMCIPAUTORINSE][3], WATERTARGET_HLT, true);
          TopUpHeater        = OFF;
          break;            

        case CIPWAFTERRINSE:
          setupLine(LINERINSE);
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_B;
            HLTPump          = OFF;
            MLTValveA        = OPEN;
            MLTValveB        = OPEN;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = OPEN;
          BKValveB           = OPEN;
          BKPump             = OFF;
          WhirlpoolValve     = PORT_A;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = OPEN;
          TopUpHeater        = OFF;
          break;                      

        case MANUALCIPLINE:
          setupLine(LINERINSE);
          break;

        case CIPFMTSTART ... CIPFMTCOMPLETE:
          setupLine(LINEFMTCIP);
          CirculationMode    = CIRCNONE;
            HLTValve         = PORT_A;
            HLTPump          = OFF;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_A;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = OPEN;
          BKWaterIn        = CLOSED;
          ColdBankWaterIn    = CLOSED;          
          TopUpHeater        = OFF;
          if (debugging) BKLevel = 0;
          break;

        case KEGCLEANERSETUP ... KEGCLEANERFINALRINSE:
          //setupLine(LINEKEGCLEANING); 
          CirculationMode    = CIRCNONE;
            HLTValve         = PORT_A;
            HLTPump          = OFF;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_A;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = OPEN;
          BKWaterIn        = CLOSED;
          ColdBankWaterIn    = CLOSED;          
          TopUpHeater        = OFF;
          if (debugging) BKLevel = 0;
          break;


        case DIAGI2C:
        case DIAGTHERMOMETERS:
        break;

        case DIAGVALVES1:
          setupLine(LINERINSE);
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_A;
            HLTPump          = OFF;
            MLTValveA        = CLOSED;
            MLTValveB        = OPEN;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = OPEN;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_B;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = OPEN;
          HLTWaterIn         = CLOSED;
          BKWaterIn          = OPEN;
          ColdBankWaterIn    = CLOSED;          
          TopUpHeater        = OFF;

          ColdWaterIn        = OPEN;
          HotWaterIn         = CLOSED;
          break;


        case DIAGVALVES2:
          setupLine(LINERINSE);        
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_B;
            HLTPump          = OFF;
            MLTValveA        = OPEN;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_A;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = OPEN;
          HLTWaterIn         = OPEN;
          BKWaterIn          = CLOSED;
          ColdBankWaterIn    = CLOSED;          
          TopUpHeater        = OFF;

          ColdWaterIn        = CLOSED;
          HotWaterIn         = OPEN;
          break;          

        case DIAGVALVES3:
        case DIAGPUMPSFLOWMETERS:        
          setupLine(LINERINSE);        
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_A;
            HLTPump          = OFF;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = OPEN;
          WhirlpoolValve     = PORT_B;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = OPEN;
          BKWaterIn        = OPEN;
          ColdBankWaterIn    = CLOSED;          
          TopUpHeater        = OFF;

          ColdWaterIn        = OPEN;
          HotWaterIn         = CLOSED;

          FMTWaterIn         = CLOSED;
          break;          

        case DIAGVALVESFMT:
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_A;
            HLTPump          = OFF;
            MLTValveA        = CLOSED;
            MLTValveB        = CLOSED;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = CLOSED;
          WhirlpoolValve     = PORT_A;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          BKWaterIn        = CLOSED;
          ColdBankWaterIn    = CLOSED;          
          TopUpHeater        = OFF;

          ColdWaterIn        = OPEN;
          HotWaterIn         = CLOSED;

          FMTWaterIn         = OPEN;
          FMTDrain          = OPEN;
          FMTCycle          = OPEN;
          break;          


    case CALIBRATEFLOWSTART:          
          CirculationMode    = CIRCCIP;
            HLTValve         = PORT_A;
            HLTPump          = OFF;
            MLTValveA        = CLOSED;
            MLTValveB        = OPEN;
            MLTPump          = OFF;
          HLTTargetTemp      = NOHEAT;
          HLTTargetVolume    = NOWATERIN;
          HLTPriority        = 'V';
          BKTargetTemp       = NOHEAT;
          BKValveA           = CLOSED;
          BKValveB           = OPEN;
          WhirlpoolValve     = PORT_B;
          BKPump             = OFF;
          Chiller1           = CLOSED;
          ColdBankValve      = PORT_A;
          MLTDrain           = CLOSED;
          BKWaterIn        = OPEN;
          ColdBankWaterIn    = CLOSED;          
          TopUpHeater        = OFF;

          ColdWaterIn        = CLOSED;
          HotWaterIn         = CLOSED;


          break;
    }
    HLTStandby = BKStandby = 0;



    
}
