#include "Level.h"
#include <Variables.h>
#include "GambainoCommon.h"
#include <BrewCoreCommon.h>
#include <Equipment.h>
#include <WaterHeatControl.h>
#include <Status.h>
#include <UI.h>
#include "__NumFilters.h"

unsigned long int lastMillis = 0; // used to any kind of time control in this file

void HLTTempVolControl()
{
  bool PotHeat;
  static unsigned long int closedAt = 0;

  PotHeat = HLTHeater.asBoolean();
  
  if (HLTTargetVolume > HLTMAXVOLUME + 1) {   /**** Constante: margem de erro nas contas ****/
    alert("FIRMWARE ERROR: HLT volume target out of range (%.2f)", float(HLTTargetVolume)); 
    HLTTargetVolume = HLTMAXVOLUME;
  }

  if (HLTTargetVolume > 0) {
    if (HLTWaterIn == OPEN) {
      if (HLTVolume >= HLTTargetVolume || 
         (HLTPriority == 'T' && HLTTemp < HLTTargetTemp && HLTTemp < WATERBOILTEMP && HLTVolume >= HLTMINVOLUMEFORHEATER)) {
        waterInStop();
        closedAt = millis();
      }
    }
    else {
      if (closedAt==0)
        closedAt = millis(); // closed by level control
      if (HLTVolume < HLTTargetVolume)
        if (HLTPriority == 'V') {
          waterInStart(HLTTargetVolume - HLTVolume, WATERTARGET_HLT, HLTTargetTemp>35);
          closedAt = 0;
        }
        else { // priority T
          if (HLTVolume < HLTMINVOLUMEFORHEATER) {
            waterInStart(HLTMINVOLUMEFORHEATER - HLTVolume, WATERTARGET_HLT, HLTTargetTemp>35);          
            closedAt = 0;
          }
          else if (    (HLTTemp >= HLTTargetTemp+ALLOWEDTEMPOFFSET*2  || HLTTemp >= WATERBOILTEMP) 
                    && (MILLISDIFF(closedAt,30*SECONDSms))) {
            float addition = HLTVolume * 0.02; // only 2% of volume at once
            if (addition < 1) addition = 1; // but at least 1 liter
            if (HLTVolume + addition > float(HLTTargetVolume))
              addition = HLTTargetVolume - HLTVolume;
            waterInStart(addition, WATERTARGET_HLT, false); 
            closedAt = 0;
          }
        }
    }
  }

  if (HLTTargetTemp == NOHEAT) 
    PotHeat = OFF;
  else if (HLTTargetTemp == CONSTANTHEAT)
    PotHeat = ON;
  else {
    float delta = 0;
    if (HLTPriority=='T' && HLTVolume < HLTTargetVolume) // hysteresis change side in this case
      delta = ALLOWEDTEMPOFFSET;

    float consideredTemp;
    if (HLTVolume >= HLTTargetVolume)
      consideredTemp = HLTTemp;
    else {
      // needs to project temperature to avoid turnning heater off prematurelly
      //float waterInTemp = WaterInTemp <= HLTTemp ? WaterInTemp : HLTTemp; // maybe the thermometer is reading high temp because of still water, so assume it is never hotter than HLT
      float waterInTemp = EnvironmentTemp; 
      float HLTQq = HLTWEIGHT*HLT_SPECIFICHEAT;
      consideredTemp = (HLTTemp*(HLTQq + HLTVolume * WATER_SPECIFICHEAT) +     // projected temp adding remaing water volume but no heat
                        waterInTemp * (HLTTargetVolume-HLTVolume) * WATER_SPECIFICHEAT)
                        / (HLTTargetVolume * WATER_SPECIFICHEAT + HLTQq);
    }
    
    if (PotHeat == OFF) {
      if (consideredTemp < HLTTargetTemp - ALLOWEDTEMPOFFSET + delta) 
        PotHeat = ON;
    }
    else {
      if (consideredTemp >= HLTTargetTemp + delta)
        PotHeat= OFF;
    }
  }

      
  bool tempStdb = (   (HLTTemp >= HLTTargetTemp-ALLOWEDTEMPOFFSET) 
                   || (HLTTargetTemp == CONSTANTHEAT && HLTTemp >= WATERBOILTEMP) 
                   || (HLTTargetTemp == NOHEAT));
  bool volStdb  = (   (HLTVolume >= HLTTargetVolume)  
                   || (HLTTargetVolume == NOWATERIN));

                  
  // safety check in case looses thermometer reading
  if (HLTTemp == NOTaTEMP) {
    if (HLTTargetTemp != NOHEAT)
      tempStdb = false;
    if (PotHeat && HLTTargetTemp != CONSTANTHEAT)
      PotHeat = OFF; 
  }
                   
  HLTStandby = tempStdb && volStdb;
  
  if (PotHeat && HLTVolume < HLTMINVOLUMEFORHEATER) 
    PotHeat = OFF;
  
  HLTHeater  = PotHeat;
}

void BKTempControl()
{
  byte PotHeat;
  float boilTemp = (Status == MASHMASHOUT || Status == PREPSPARGE) ? WORTBOILTEMP : WATERBOILTEMP;
  
    PotHeat = BKHeater;

 
  if (BKTargetTemp == NOHEAT) 
    PotHeat = 0;
  else if (BKTargetTemp == CONSTANTHEAT) {
    if (BKTemp > boilTemp)
      PotHeat = 2;
    else  
      PotHeat = 3;
  }
  else {
    float L0 = BKTargetTemp - ALLOWEDTEMPOFFSET / 2.0;
    float L1 = BKTargetTemp;
    float L2 = BKTargetTemp + ALLOWEDTEMPOFFSET / 2.0;
    float t;
    
    byte  PotHeatA, PotHeatB;
    float delta = 0.1;
    
    t = BKTemp+delta; if (t < L0) PotHeatA = 3; else if (t < L1) PotHeatA = 2; else if (t < L2) PotHeatA = 1; else PotHeatA = 0;
    t = BKTemp-delta; if (t < L0) PotHeatB = 3; else if (t < L1) PotHeatB = 2; else if (t < L2) PotHeatB = 1; else PotHeatB = 0;

    if (PotHeat != PotHeatA && PotHeat != PotHeatB) 
      t = BKTemp;     if (t < L0) PotHeat  = 3; else if (t < L1) PotHeat  = 2; else if (t < L2) PotHeat  = 1; else PotHeat  = 0;
  }

  bool tempStdb = (   (BKTemp >= BKTargetTemp-ALLOWEDTEMPOFFSET) 
                   || (BKTargetTemp == CONSTANTHEAT && BKTemp >= boilTemp) 
                   || (BKTargetTemp == NOHEAT));
                  
  // safety check in case looses thermometer reading
  if (BKTemp == NOTaTEMP) {
    if (BKTargetTemp != NOHEAT)
      tempStdb = false;
    if (PotHeat && BKTargetTemp != CONSTANTHEAT)
      PotHeat = 0; 
  }
                   
  BKStandby = tempStdb;
  
  if (PotHeat && BKLevel <= 1)
    PotHeat = 0;
    
 
  if (PotHeat==3 && limitBKHeater && BKTemp >= WORTTEMPTOAVOIDBOILDOVER) { // limit BKHeater to avoid boil over 
    long pointInCycle = millis() % 10000; // 10 seconds cycle
    long onTime = (BKTargetTemp - BKTemp) / (BKTargetTemp - WORTBOILTEMP) * 4000+2000; // 3 to 7 seconds is the maximum on time
    if (onTime < 3000) 
      onTime = 3000;
    else if (onTime > 8000)
      onTime = 8000;
    if (pointInCycle > onTime) 
      PotHeat = 2;   // limit BKHeater to avoid boil over during whirlpool line circulation in boil
  }
  
  BKHeater  = PotHeat;
  
  PotHeat = BKHeater; // necessary, in case of override
  BKHeaterL = (PotHeat == 1 || PotHeat == 3);
  BKHeaterH = (PotHeat == 2 || PotHeat == 3);
  
  if (BKHeaterL.isOverrided()) BKHeaterL.override(); // for security, heater direct control does not accept override
  if (BKHeaterH.isOverrided()) BKHeaterH.override();
}

void tempVolControl() 
{
  HLTTempVolControl();
  BKTempControl();    


  static unsigned long int lastTempAvg = 0;
  #define TEMPAVGINTERVAL (2*DALLASREQUESTINTERVAL)
  static averageFloatVector HLTavg(8);
  static averageFloatVector BKavg (8);
  if (MILLISDIFF(lastTempAvg,TEMPAVGINTERVAL)) {
    static float lastHLTTemp;
    static float lastBKTemp;
    
    int factor = int((100*MINUTESms)/(millis()-lastTempAvg));
    lastTempAvg = millis();
    HLTavg.add((HLTTemp - lastHLTTemp)*factor);
    BKavg .add((BKTemp  - lastBKTemp )*factor);
    lastHLTTemp = HLTTemp;
    lastBKTemp  = BKTemp;
  } 
}

float HLTTargetTempForMLTHeating(bool evaluateCoilEfficiency) {
  if (Status != MASHMASHING && Status != MASHMASHOUT) {
    say("HLTTargetTempForMLTHeating shoud only be called in MASHMASHING and MASHMASHOUT states");
    return NOHEAT;
  }

  int error = 0;
  float targHLT;

  float tempTop = MLTTopTemp;
  float tempMid = MLTTemp;
  float tempBottom = MLTOutTemp;
  float tempRet = HLTOutTemp;
  float tempHLT = HLTTemp;
  float targMLT = MLTTargetTemp;

  if (tempTop == NOTaTEMP) {
    error = 1;
    tempTop = tempMid;
  }

  if (tempMid == NOTaTEMP) {
    error = 1;
    tempMid = tempTop;
  }

  if (tempBottom == NOTaTEMP) {
    error = 1;
   tempBottom = tempMid;
  }

  if (tempHLT == NOTaTEMP) error = 2;

  if (targMLT == NOHEAT) error = 3;

  if (tempRet == NOTaTEMP) {
    error = 1;
    tempRet = tempHLT * HeatCoilEfficiency.defaultValue();
  }

  if (error==0 || error==1) {
    float tempMLT = (5*tempTop + tempMid) / 6;
    float deltaMLT = targMLT - tempMLT;

    if (evaluateCoilEfficiency) {
      float efficiency;
      if (abs(HLTTemp - tempBottom) < 0.5)
        efficiency = 1;

      else {
        efficiency = (HLTOutTemp - tempBottom) / (HLTTemp - tempBottom);
        if (efficiency<0.5) 
          efficiency = 0.5; //****** Constante
        else if (efficiency>1) 
          efficiency = 1;
      }
      HeatCoilEfficiency = efficiency;
    }
    else
      HeatCoilEfficiency = HeatCoilEfficiency.defaultValue();


    float returnTemperatureLimit = (Status==MASHMASHING) ? MAXWORTTEMPINCOILFORMASHING : MAXWORTTEMPINCOILFORMASHOUT;
    float targHLTLimit = tempBottom + (returnTemperatureLimit - tempBottom) / HeatCoilEfficiency;
    if (targHLTLimit > WATERBOILTEMP-1) 
      targHLTLimit = WATERBOILTEMP-1;

    targHLT = tempBottom + (targMLT - tempBottom + HeatDampeningFactor * deltaMLT + HeatAdditiveCorrection) / HeatCoilEfficiency;
    if (targHLT > targHLTLimit) 
      targHLT = targHLTLimit;

  }  

  if (error==2) {
    setAlertMessage("HLT control disabled due to thermometer failure");
    return NOHEAT;
  }
  else if (error==3) {
    setAlertMessage("HLT control disabled due to missing MLT target temperature");
    return NOHEAT;
  }
  else if (error==1) 
    setAlertMessage("HLT control inacurate due to missing HLT return temperature");    

  if (targHLT<0)
    targHLT = NOHEAT;
  return targHLT;
}

void CircControl()
{
    static unsigned char lastCircStatus = 0;
    static unsigned long CircStatusEntryTime=0;
    
    bool MLT_p, lastMLT_p;
    bool HLT_p, lastHLT_p;
    bool BK_p,  lastBK_p;

    MLT_p   = lastMLT_p   = MLTPump.asBoolean();    
    HLT_p   = lastHLT_p   = HLTPump.asBoolean();
    BK_p    = lastBK_p    = BKPump.asBoolean();
    
    bool newStatus = false;
    
    int c = int(CirculationMode);
    
    if (c == CIRCCIP) // During CIP, all control is done by main state machine
        return;

    if (c != lastCircStatus) {
      lastCircStatus = c;
      CircStatusEntryTime = millis();
      say("Circulation mode set to %d",c);
      newStatus = true;
    }
    else
      if (CircStatusEntryTime==0)
        CircStatusEntryTime = millis(); // just to be safe in case of reset/resume
        
    switch(c) {
        case CIRCNONE:
          if (HLTValve==PORT_A && HLTVolume>HLTMINVOLUMEFORTEMP && HLTTargetTemp != NOHEAT) {
            float realTarget = HLTTargetTemp!=CONSTANTHEAT ? HLTTargetTemp : WATERBOILTEMP;
            if (HLTTemp<realTarget - ALLOWEDTEMPOFFSET) 
              HLT_p = (HLTTemp>=realTarget-10 && HLTHeater.asBoolean()) ? ON : OFF ;
            else
              if (HLTTemp>realTarget + ALLOWEDTEMPOFFSET) {
                if (Status != PREPWATERFORINFUSION)
                  HLT_p = (int(TimeInStatus) % 240 < 60L) ? ON : OFF; // 1 minute on, 3 minutes off          
              }
            else
              HLT_p = OFF;
          }
          else
            HLT_p = OFF;
          break;

        case CIRCHEAT:
          MLTValveA = OPEN;
          MLTValveB = CLOSED;
          MLTDrain = CLOSED;
          HLTValve = PORT_A;
          MLT_p = ON;
          HLTTargetTemp = HLTTargetTempForMLTHeating(MILLISDIFF(CircStatusEntryTime,30000L));
          HLT_p = ON;
          break;
            
        case CIRCRUNOFF:
          {
            bool HLTTempOk = (Status==MASHFIRSTRUN) ? true : (HLTTemp >= SPARGETEMP-ALLOWEDSPARGETEMPDELTA && HLTTemp <= SPARGETEMP+ALLOWEDSPARGETEMPDELTA);
            bool hasWater = HLTVolume>2;

            HLTValve = PORT_B;
            MLTValveA = CLOSED;
            MLTValveB = OPEN;
            MLTDrain = CLOSED;
            
            if (HLTVolume < HLTMINVOLUMEFORTEMP) {
              HLTTempOk = true;
              if (HLTTargetTemp != NOHEAT)
                HLTTargetTemp = NOHEAT;
            }
                
            if (!hasWater) 
              HLT_p = OFF;
            else {
              static bool alertMessage = false;

              if (HLT_p == OFF && MLTTopLevel != TOPLEVELBOTHSUBMERSED && HLTTempOk) //***** Constante
                HLT_p   = ON;
              else if (MLTTopLevel == TOPLEVELBOTHSUBMERSED || !HLTTempOk) 
                HLT_p   = OFF;
              
              if (MLTTopLevel != TOPLEVELBOTHDRY) {
                lastMillis = 0;
                if (alertMessage) {
                  setAlertMessage("");
                  alertMessage = false;
                }
                MLT_p = ON;
              }
              else if (MLTTopLevel == TOPLEVELBOTHDRY)
                if (lastMillis == 0) {
                  lastMillis = millis();
                  MLT_p = ON;
                }
                else if (MILLISDIFF(lastMillis,1*MINUTESms)) { // more than 1 minute, gives a warning and continues
                  setAlertMessage("MLT dry for too long. Check sparge water");
                  alertMessage = true;
                  if (MILLISDIFF(lastMillis,5*MINUTESms)) // more than 5 minutes, stops MLT pump
                    MLT_p = OFF;
                  else  
                    MLT_p = ON;
                }
            }
          }
          break;
            
        case CIRCHLTTOMLT: {

          MLTValveA     = OPEN;
          MLTValveB     = CLOSED;
          MLT_p         = OFF;
          HLTValve      = PORT_B;
          HLT_p         = ON;
            
          if (HLTVolume <= HLTTargetVolume || HLTVolume<=0) {
            HLTValve        = PORT_A;
            HLT_p           = OFF;
            CirculationMode = CIRCNONE;
          }
        }
        break;
    }


    if (c != CIRCCIP) {
      if (!(waitForValve((millis()-CircStatusEntryTime)/1000))) { // during valve stabilization, keep pumps off
        MLT_p = OFF;
        HLT_p = OFF;
      }
       
      if (HLTVolume <= 0)
        HLT_p = OFF;

      if (MLT_p != lastMLT_p || newStatus) MLTPump   = MLT_p;
      if (HLT_p != lastHLT_p || newStatus) HLTPump = HLT_p;
      if (BK_p != lastBK_p   || newStatus) BKPump = BK_p;
    }
}
