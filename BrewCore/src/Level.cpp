#include "Level.h"
#include "Variables.h"
#include "Equipment.h"
#include "__NumFilters.h"
#include "GambainoCommon.h"
#include <BrewCoreCommon.h>
#include <Status.h>
#include <UI.h>
#include <IPC.h>
#include "driver/pcnt.h"

#define AVGSAMPLES 30

long int avgTotal;
byte avgCount = 0;

// extern variables
Todo *autoDryTodo[3];
float totalHLTWaterIntake = 0;

#define waterInPCNTUnit PCNT_UNIT_0
#define HLT2MLTPCNTUnit PCNT_UNIT_1
#define transferPCNTUnit PCNT_UNIT_2
//#define kegDetergPCNTUnit PCNT_UNIT_3

byte calibrMode = 0;
unsigned long int calibrStartTime = 0;
float calibrFrequency[4][3] ={{0,0,0},{0,0,0},{0,0,0},{0,0,0}}; 
unsigned long int calibrPulses[4][3] = {{0,0,0},{0,0,0},{0,0,0},{0,0,0}};
float calibrVolume[4][3] = {{0,0,0},{0,0,0},{0,0,0},{0,0,0}};
float calibrTotalTime[4] = {0,0,0,0};

void configPCNT(byte pin, pcnt_unit_t unit, uint16_t filterValue = 100) {
  pcnt_config_t pcnt_config = {
    .pulse_gpio_num = pin,
    .ctrl_gpio_num  = PCNT_PIN_NOT_USED,
    .lctrl_mode     = PCNT_MODE_KEEP,
    .hctrl_mode     = PCNT_MODE_KEEP,
    .pos_mode       = PCNT_COUNT_INC,
    .neg_mode       = PCNT_COUNT_DIS,
    .counter_h_lim  = 30000,
    .counter_l_lim  = 0,
    .unit           = unit,
    .channel        = PCNT_CHANNEL_0
  };
  
  // Configure PCNT unit
  esp_err_t err = pcnt_unit_config(&pcnt_config);
  if (err != ESP_OK) {
    Serial.printf("PCNT config error for unit %d: %d\n", unit, err);
    return;
  }
  
  // Set filter value (max 1023 = ~12.8µs @ 80MHz APB)
  pcnt_set_filter_value(unit, filterValue);
  pcnt_filter_enable(unit);
  
  // Clear counter and start
  pcnt_counter_pause(unit);
  pcnt_counter_clear(unit);
  pcnt_counter_resume(unit);
  
  Serial.printf("PCNT unit %d configured on pin %d with filter=%d\n", unit, pin, filterValue);
  
  // Teste específico para KegDetergentFlow (unit 3, pin 19)
  if (unit == PCNT_UNIT_3) {
    Serial.printf("KegDetergentFlow PCNT configured on pin %d (unit %d)\n", pin, unit);
  }
}

void configPCNTs() {
  // Filtro máximo (1023 = ~12.8µs @ 80MHz APB) para todos: pulsos legítimos estão na casa dos ms, sem risco de corte
  configPCNT(PINWATERINMETER,       waterInPCNTUnit,  512);
  configPCNT(PINHLTTOMLTMETER,      HLT2MLTPCNTUnit,  512);
  configPCNT(PINTRANSFERFLOWMETER,  transferPCNTUnit, 512);
  //configPCNT(PINKEGDETERGENTFLOW, kegDetergPCNTUnit);
}

int16_t PCNTReadAndClear(pcnt_unit_t unit) {
  int16_t v = 0;
  esp_err_t err = pcnt_get_counter_value(unit, &v);
  if (err != ESP_OK) {
    Serial.printf("PCNT read error for unit %d: %d\n", unit, err);
    return 0;
  }
  pcnt_counter_clear(unit);
  return v;
}

void waterInStart(float liters, byte target, bool isHot) {
  WaterInTarget = liters;    

  if (liters>0) {
    char buf[100];
    if (isHot) 
      HotWaterIn = OPEN;
    else
      ColdWaterIn        = OPEN;

    say("Starting water in: %.1f L to target %s (%s)", liters, 
           WaterTargetLabels[target-1], 
           isHot ? "hot" : "cold");

    HLTWaterIn = target == WATERTARGET_HLT ? OPEN : CLOSED;
    BKWaterIn  = target == WATERTARGET_BK ? OPEN : CLOSED;
    ColdBankWaterIn = target == WATERTARGET_COLDBANK ? OPEN : CLOSED;
    KegWaterIn = target == WATERTARGET_KEG ? OPEN : CLOSED;
    FMTWaterIn = target == WATERTARGET_FMT ? OPEN : CLOSED;
  }
  else 
    waterInStop();

}

void waterInStop() {
  if (WaterInTarget!=0) {
    WaterInTarget = 0;
    say("Stopping water in");
  }

  bool keepColdOpen = ColdWaterIn.asBoolean() &&
                      HLTWaterIn.asBoolean()   &&
                      (HLTPriority == 'T')     &&
                      (float(HLTVolume) < float(HLTTargetVolume));

  if (ColdWaterIn.isOverrided())
    ColdWaterIn.assumeOverrideAsProg();
  if (HotWaterIn.isOverrided())
    HotWaterIn.assumeOverrideAsProg();
  if (HLTWaterIn.isOverrided())
    HLTWaterIn.assumeOverrideAsProg();
  if (BKWaterIn.isOverrided())
    BKWaterIn.assumeOverrideAsProg();
  if (ColdBankWaterIn.isOverrided())
    ColdBankWaterIn.assumeOverrideAsProg();
  if (KegWaterIn.isOverrided())
    KegWaterIn.assumeOverrideAsProg();
  if (FMTWaterIn.isOverrided())
    FMTWaterIn.assumeOverrideAsProg();

  if (HotWaterIn.asBoolean())
    HotWaterIn = CLOSED;

  if (ColdWaterIn.asBoolean())
    if (!keepColdOpen) 
      ColdWaterIn = CLOSED;

  if (HLTWaterIn.asBoolean())
    HLTWaterIn = CLOSED;

  if (BKWaterIn.asBoolean())
    BKWaterIn = CLOSED;
  if (ColdBankWaterIn.asBoolean())
    ColdBankWaterIn = CLOSED;
  if (KegWaterIn.asBoolean())
    KegWaterIn = CLOSED;
  if (FMTWaterIn.asBoolean())
    FMTWaterIn = CLOSED;

}

bool waterInIsDone() {
  return (WaterInTarget==0);
}

void manageHydrometers() 
{
    #define WIFACTOR (572.6) // number of pulses per liter: (6*Q-8), Q=L/min
    #define WICORRECTION 20.9 // Qs = (f+8)/360

    #define HLTFACTOR (586) 
    #define HLTCORRECTION 0

    #define TRANSFERFACTOR 648.3 
    #define TRANSFERCORRECTION 2.4

    #define KEGDETERGFACTOR 637.74
    #define KEGDETERGCORRECTION 3.37


    #define WIADJ  (1.000)
    #define HLTADJ (1.000)
    #define TRANSFERADJ (1.000)
    #define KEGDETERGADJ (1.000)
    #define FLOWSENSORINTERVAL 500
    #define AVGCYCLES 10

    static unsigned long int lastFlowSum=0;
    unsigned int WIParcel;
    unsigned int HLTParcel;
    unsigned int transferParcel;
    //unsigned int kegDetergParcel;
    static weightedAverageFloatVector WIAvg(AVGCYCLES);
    static weightedAverageFloatVector HLTAvg(AVGCYCLES);
    static weightedAverageFloatVector transferAvg(AVGCYCLES);
    //static weightedAverageFloatVector kegDetergAvg(AVGCYCLES);


    if (MILLISDIFF(lastFlowSum,FLOWSENSORINTERVAL)) {                                      
      bool waterIn = (ColdWaterIn.asBoolean() || HotWaterIn.asBoolean()) 
                     && (HLTWaterIn.asBoolean() || BKWaterIn.asBoolean() || ColdBankWaterIn.asBoolean() || KegWaterIn.asBoolean() || FMTWaterIn.asBoolean());
      bool HLT2MLT = (HLTValve== PORT_B && HLTPump.asBoolean()) || Status==CIPWRINSE4; 
      bool transfering = WhirlpoolValve==PORT_B && BKPump.asBoolean();

      // acquire time and parcels
      int lastDuration = millis() - lastFlowSum;
      lastFlowSum = millis();

      WIParcel       = PCNTReadAndClear(waterInPCNTUnit);
      HLTParcel      = PCNTReadAndClear(HLT2MLTPCNTUnit);
      transferParcel = PCNTReadAndClear(transferPCNTUnit);
      //kegDetergParcel = PCNTReadAndClear(kegDetergPCNTUnit);
      
      // Filtro de ruído: ignora contagens muito baixas (provavelmente ruído)
      #define MIN_PULSES_THRESHOLD 3  // mínimo de pulsos para considerar válido
      if (WIParcel > 0 && WIParcel < MIN_PULSES_THRESHOLD) WIParcel = 0;
      if (HLTParcel > 0 && HLTParcel < MIN_PULSES_THRESHOLD) HLTParcel = 0;
      if (transferParcel > 0 && transferParcel < MIN_PULSES_THRESHOLD) transferParcel = 0;
      //if (kegDetergParcel > 0 && kegDetergParcel < MIN_PULSES_THRESHOLD) kegDetergParcel = 0;
      
      // Debug: mostrar valores lidos dos PCNTs se houver algum valor diferente de zero
      /*
      if (WIParcel != 0 || HLTParcel != 0 || transferParcel != 0 || KegDetergentPump.asBoolean()) {
        Serial.printf("PCNT values (filtered): WI=%d, HLT=%d, Transfer=%d, Duration=%d, pin 19 read: %d\n", 
          (int)WIParcel, (int)HLTParcel, (int)transferParcel, (int)lastDuration, digitalRead(19));
        Serial.printf("Flow rates: WI=%.2f, HLT=%.2f, Transfer=%.2f  L/min\n",
          WIAvg.value() * 60, HLTAvg.value() * 60, transferAvg.value() * 60);
      }
          */

      if (debugging) {
        if (waterIn) 
          WIParcel = 0.1*WIFACTOR;/// (Status==PREPSPARGE ? 10 : 2);

        if (HLTVolume>0 && HLT2MLT)
          HLTParcel = HLTFACTOR;          

        if (transfering) 
          transferParcel = 0.1*TRANSFERFACTOR;          
      }

      // calibration
      if (calibrMode!=0) {
        calibrPulses[calibrMode-1][0] += WIParcel;
        calibrPulses[calibrMode-1][1] += HLTParcel;
        calibrPulses[calibrMode-1][2] += transferParcel;
      }

      // compute frequencies, adjusted flows and volume additions
      float WIFreq = WIParcel * 1000.0/lastDuration;
      float HLTFreq = HLTParcel * 1000.0/lastDuration;
      float TransferFreq = transferParcel * 1000.0/lastDuration;
      //float KegDetergFreq = kegDetergParcel * 1000.0/lastDuration;

      float adjustedWIFlow = WIFreq==0 ? 0 : (WIFreq + WICORRECTION) / WIFACTOR * WIADJ;
      float adjustedHLTFlow = HLTFreq==0 ? 0 : (HLTFreq + HLTCORRECTION) / HLTFACTOR * HLTADJ;
      float adjustedTransferFlow = TransferFreq==0 ? 0 : (TransferFreq + TRANSFERCORRECTION) / TRANSFERFACTOR * TRANSFERADJ;
      //float adjustedKegDetergFlow = KegDetergFreq==0 ? 0 : (KegDetergFreq + KEGDETERGCORRECTION) / KEGDETERGFACTOR * KEGDETERGADJ;

      float WIVolAddition = adjustedWIFlow * lastDuration / 1000.0;
      float HLTVolAddition = adjustedHLTFlow * lastDuration / 1000.0;
      float TransferVolAddition = adjustedTransferFlow * lastDuration / 1000.0;
      //float KegDetergVolAddition = adjustedKegDetergFlow * lastDuration / 1000.0;

      if (calibrMode!=0) {
        calibrVolume[calibrMode-1][0] += WIVolAddition;
        calibrVolume[calibrMode-1][1] += HLTVolAddition;
        calibrVolume[calibrMode-1][2] += TransferVolAddition;
      }

      // feed averages and compute average flows in l/min
      WIAvg.add(adjustedWIFlow, lastDuration);
      HLTAvg.add(adjustedHLTFlow, lastDuration);
      transferAvg.add(adjustedTransferFlow, lastDuration);
      //kegDetergAvg.add(adjustedKegDetergFlow, lastDuration);

      WaterInFlow = WIAvg.value() * 60;
      HLT2MLTFlow = HLTAvg.value() * 60;
      TransferFlow = transferAvg.value() * 60;
      //KegDetergentFlow = kegDetergAvg.value() * 60;


      // finally update volumes and take actions if needed
      
      //--------------------------------------- HLT water in addition to HLT volume
      if (waterIn) {
        if (HLTWaterIn.asBoolean() &&
          (LineConfiguration==LINEPREBREW || LineConfiguration==LINEINFUSION || LineConfiguration==LINEBREW || LineConfiguration==LINEBREWTOPUP)) {
          HLTVolume = HLTVolume + WIVolAddition;
          totalHLTWaterIntake +=  WIVolAddition;
        }

        /* Lúcio: implementar checagem do hidrometro perdido em outro local */
      }
      else 
        WIAvg.clear();

      //--------------------------------------- HLT to MLT meter
      if (HLT2MLT) {
        HLTVolume = HLTVolume - HLTVolAddition / 1.027; // constant compensates hot water expansion
        if (HLTParcel==0)
          if (HLTAvg.fullVector() && HLTAvg.value()==0 && !HLTPump.isOverrided()) {
            if (HLTVolume > VOLUMETOCHECKHLTEMPTY) {
              if (HLTPump.asBoolean()) {
                HLTPump.override(OFF);
                setAlertMessage("Did not sense transfer from HLT to MLT. Overrided pump. Check and release.");
              }
            }
            else 
              HLTVolume = 0;
          }
      }
      else 
        HLTAvg.clear();

      //--------------------------------------- Water in target control
      static float lastWaterTarget = 0; 
      static byte closingCycleCounter = 0;
      if (lastWaterTarget>0) {
        WaterInTarget = float(WaterInTarget - WIVolAddition);
        if (WaterInTarget<=0)  {
          closingCycleCounter = 2; // number of cycles to wait before stopping water in - avoid unecessary rapid open/closeo state changes
          WaterInTarget = 0;
        }
      }
      else if (closingCycleCounter>0) {
        closingCycleCounter--;
        if (closingCycleCounter==0)
          waterInStop();
      }
      lastWaterTarget = WaterInTarget;
          
      //--------------------------------------- Transfer to FMT meter
      if (transfering)
        TransferVolume = TransferVolume + TransferVolAddition;
    }
}


void manageLevel() {
  static unsigned long int lastHydrometerManage=0;
  if (MILLISDIFF(lastHydrometerManage,30)) {
    lastHydrometerManage = millis();

    manageHydrometers();
    int a;
    if (debugging) 
      MLTTopLevel = TOPLEVELBETWEENBOTH;
    else {
      a = analogReadStable(PINMLTTOPLEVEL);
      if (a>3072) 
        MLTTopLevel = TOPLEVELBOTHSUBMERSED;   
      else if (a > 1024) 
        MLTTopLevel = TOPLEVELBETWEENBOTH;
      else                                 
        MLTTopLevel = TOPLEVELBOTHDRY;  
    }

    static unsigned long int lastChangeHLT=0, lastChangeBK=0;
    static int lastBKLevel=-1;
    static int newBKLevel; // these are statics because of debugging mode

    if (!debugging) {
      a = analogReadStable(PINBKLEVEL);
      if (a<440)
        newBKLevel = 0;
      else if (a<1400)
        newBKLevel = 1;
      else if (a<2450)
        newBKLevel = 2;
      else if (a<3500)
        newBKLevel = 3;
      else
        newBKLevel = 4;
    }
    else { // debugging
      if (BKLevel.isOverrided()) 
        BKLevel.assumeOverrideAsProg();
        
      newBKLevel = BKLevel;
    }

    if (newBKLevel != lastBKLevel) {
      lastChangeBK = millis();
      lastBKLevel = newBKLevel;
    }
    else if (BKLevel != newBKLevel)
      if (MILLISDIFF(lastChangeBK,2000)) { // the sensor has to stay stable for two seconds before accept it change
        BKLevel = newBKLevel;
      }

    if (!debugging) {
      static unsigned long int lastChangeKegLiquidSensor = 0;
      int liqRead = analogReadStable(PINKEGLIQUIDSENSORS);
      byte newKegLiquidSensor  = liqRead < 1024 ? KEGLIQUIDINRETURN : (liqRead<3072 ? KEGLIQUIDINTANK : KEGLIQUIDNOLIQUID); // 0 = no liquid, 1 = liquid present, 2 = error
      if (newKegLiquidSensor == KegLiquidSensor) {
        lastChangeKegLiquidSensor = 0;
      }
      else {
        if (lastChangeKegLiquidSensor == 0)
          lastChangeKegLiquidSensor = millis();
        else
          if (MILLISDIFF(lastChangeKegLiquidSensor,kegSensorStabilityTime)) { // the sensor has to stay stable for a time before accept it change
            KegLiquidSensor = newKegLiquidSensor;
            lastChangeKegLiquidSensor = 0;
          }
      }
    }
    else { // debugging
      if (KegPump.asBoolean())
        KegLiquidSensor = KEGLIQUIDINTANK;
      else if (KegDetergentPump.asBoolean())
        KegLiquidSensor = KEGLIQUIDINRETURN;
      else
        KegLiquidSensor = KEGLIQUIDNOLIQUID;
    }
  }
}

void startCalibration(byte mode) {
  calibrMode = mode;
  calibrStartTime = millis();
  calibrFrequency[calibrMode-1][0] = 0;
  calibrFrequency[calibrMode-1][1] = 0;
  calibrFrequency[calibrMode-1][2] = 0;
  calibrPulses[calibrMode-1][0] = 0;
  calibrPulses[calibrMode-1][1] = 0;
  calibrPulses[calibrMode-1][2] = 0;
  calibrVolume[calibrMode-1][0] = 0;
  calibrVolume[calibrMode-1][1] = 0;
  calibrVolume[calibrMode-1][2] = 0;
}

void stopCalibration() {
  byte mode = calibrMode-1;
  calibrMode = 0;
  unsigned long int elapsedTime = (millis() - calibrStartTime); // in ms
  calibrFrequency[mode][0] = (calibrPulses[mode][0] * 1000.0) / elapsedTime;
  calibrFrequency[mode][1] = (calibrPulses[mode][1] * 1000.0) / elapsedTime;
  calibrFrequency[mode][2] = (calibrPulses[mode][2] * 1000.0) / elapsedTime;
  calibrTotalTime[mode] = elapsedTime / 1000.0;
}

char *getCalibration(char *buf) {
  char b[200];
  buf[0]=0;
  for (byte mode=0; mode<4; mode++) {
    sprintf(b,"mode %d - total time: %.2f sec\n\n",mode+1, calibrTotalTime[mode]);
    strcat(buf, b);
    sprintf(b, "Mode %d - frequency:\n  WaterIn: %.2f pulses/sec\n  HLT2MLT: %.2f pulses/sec\n  Transfer: %.2f pulses/sec\n\n", 
            mode+1,
            calibrFrequency[mode][0],
            calibrFrequency[mode][1],
            calibrFrequency[mode][2]);
    strcat(buf, b);
    sprintf(b, "Mode %d - pulses:\n  WaterIn: %lu\n  HLT2MLT: %lu\n  Transfer: %lu\n\n", 
            mode+1,
            (unsigned long)calibrPulses[mode][0],
            (unsigned long)calibrPulses[mode][1],
            (unsigned long)calibrPulses[mode][2]);
    strcat(buf, b);

    sprintf(b, "Mode %d - volumes:\n  WaterIn: %.2f\n  HLT2MLT: %.2f\n  Transfer: %.2f\n\n", 
            mode+1,
            calibrVolume[mode][0],
            calibrVolume[mode][1],
            calibrVolume[mode][2]);
    strcat(buf, b);

  }
  return buf;
}



