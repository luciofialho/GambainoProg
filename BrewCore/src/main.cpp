//#define COLDSIDEONLY
//#define COLDSIDEONLY


#include "GambainoCommon.h"
#include <BrewCoreCommon.h>
#include <Time.h>
#include "ProcVar.h"
//#include "__Timer.h"
#include "__NumFilters.h"
#include "Equipment.h"
#include "Level.h"
#include "Variables.h"
#include "Todo.h"
#include "WaterHeatControl.h"
#include "Status.h"
#include "StatusEntrySetup.h"
#include "ColdSide.h"
#include "UI.h"
#include "IPC.h"
#include "IOTK.h"
#include "IOTK_NTP.h"
#include "IOTK_ESPAsyncServer.h"
#include "CloudLog.h"
#include "EspNowPackets.h"
#include <EEPROM.h>
#include <SPIFFS.h>
#include "IOTK_GLog.h"
#include "esp_task_wdt.h"
#include "PotCleaner.h"
#include "coreDumpDownload.h"


#define SAVETOEEPROMINTERVAL 5 * MINUTES

void controlPower() {
  bool powerOn = false;
  if (valveCurrentMonitorIsOn)
    actualCurrent = ina219.getCurrent_mA() / ina219.getBusVoltage_V() * 12;


  if (millis() < 3*WAITFORVALVE*SECONDSms) {  // after boot, keep power on to move valves and then collect idle current consumption to set threshold for valve duty current
    powerOn = true;
      valveStandbyCurrent = 5;
      valveThresholdCurrent = 11;
    //  }
  }
  else 
    switch (int(Status)) {
      case STANDBY: {
          float start = millis()>60000L  ? 0 : 10;
          powerOn = (TimeInStatus<WAITFORVALVE*3+start) && (TimeInStatus>=start);
        }
        break;
      case BREWWAIT:
        powerOn = (IPCStatus != IPCNOTSTARTED && IPCStatus != IPCWAITBREWSTART) || ColdBankControl.asBoolean();
        break;
      default:
        powerOn = true; 
        break;
    }

  if (powerOn) {
    if (!Power12VOn.asBoolean()) {
      Power12VOn = ON;
      Power12VBlock = OFF;
    }
  }
  else {
    if (Power12VOn.asBoolean()) {
      Power12VOn = OFF;
      Power12VBlock = OFF;
    }
  }
} 

/*
void IRAM_ATTR ISR_ColdWaterInMeter() {
  static volatile unsigned long last = 0;
  if (millis()>last+1) {
    coldWaterInMeterCount++;
    last = millis(); 
  }
}

void IRAM_ATTR ISR_HLT2MLTMeter() {
  static volatile unsigned long last = 0;
  if (millis()>last+1) {
    HLT2MLTMeterCount++;
    last = millis();
  }
}

void IRAM_ATTR ISR_TransferMeter() {
  static volatile unsigned long last = 0;
  if (millis()>last+1) {
    transferMeterCount++;
    last = millis(); 
  }
}

*/

void setup() {
  strcpy(ESP_AppName, "Gambaino - BrewCore");
  dateFormat=DDMMYYYY;
  Serial.begin(115200);
  // Serial2 – comminication with sidekick ESP32
  Serial2.begin(SERIAL2_SPEED, SERIAL_8N1, SERIAL2_RX, SERIAL2_TX);

  pinMode(PINBUZZER,OUTPUT);
  digitalWrite(PINBUZZER,LOW);

  Serial.print("Free heap at beggining: "); Serial.print(ESP.getFreeHeap()); Serial.print(" / "); Serial.println(ESP.getMaxAllocHeap());
  Wire.begin(PINI2CSDA,PINI2CSCL); 
  quickI2CSetZeros();
  //Wire.setClock(50000);
  
  EEPROM.begin(2000);
  UI_SETUP();
  SPIFFS.begin(true,"/a");

  while (!Serial) { ; }      // waits for serial to start
  while (Serial.available()) // flush serial
      Serial.read();
  say();
  say(" ______ _______ _______ ______  _______ _____ __   _  _____ ");
  say("|  ____ |_____| |  |  | |_____] |_____|   |   | \\  | |     |");
  say("|_____| |     | |  |  | |_____] |     | __|__ |  \\_| |_____|");
  say();

  ProcVar::setSayFunction(say);
  ProcVar::setAlertFunction(alert);  
  ProcVar::initProcVarLib();
  delay(20);

  //analogReference(INTERNAL2V56); // DEFAULT or INTERNAL2V56 or INTERNAL1V1 , depending on level sensors calibration (if max < 524 for DEFAULT, turn to INETERNAL2V56 and recalibrate
  
  configVariables();
  ProcVar::writeProcVars();        
  

  ProcVar::readFromEEPROM(CONFIGPERSISTENCE); 
  
  ProcVar::writeProcVars();        
  dallas1.begin();
  dallas2.begin();
  dallas3.begin();
  
  valveCurrentMonitorIsOn = ina219.begin();
  if (!valveCurrentMonitorIsOn)
    say("Couldn't find INA219 chip");
  else
    say("INA219 chip found");

  ProcVar::setDallasLib(&dallas1,&oneWire1);
  ProcVar::setDallasLib(&dallas2,&oneWire2);
  ProcVar::setDallasLib(&dallas3,&oneWire3);
  
  dallas1.setResolution(12); 
  dallas2.setResolution(12); 
  dallas3.setResolution(12);


  sound_Beep();

  pinMode(PINMLTTOPLEVEL,INPUT);
  pinMode(PINBKLEVEL,INPUT);
  pinMode(PINKEGLIQUIDSENSORS,INPUT);

  analogReadResolution(12);                 // garante 0..4095
  analogSetAttenuation(ADC_11db);           // faixa maior (boa para até ~3.3V)
  analogSetPinAttenuation(PINMLTTOPLEVEL, ADC_11db);
  analogSetPinAttenuation(PINBKLEVEL, ADC_11db);
  analogSetPinAttenuation(PINKEGLIQUIDSENSORS, ADC_11db); 
  
  pinMode(PINWATERINMETER,INPUT_PULLUP);
  pinMode(PINHLTTOMLTMETER,INPUT_PULLUP);
  pinMode(PINTRANSFERFLOWMETER,INPUT_PULLUP);  
  configPCNTs();


  // datalog configuration
  GLogSetBuffer(datalogBuffer, sizeof(datalogBuffer));
  datalogTask = NULL;

  Serial.println("GLogSetBuffer done, creating datalog task");
  
  
  xTaskCreatePinnedToCore(
    ProcVar::dataLogTask,  // task function
    "dataLog",             // task name
    16384,                 // stack size
    NULL,              
    1,                     // task priority - will change to 1 after first run
    &datalogTask,          // task handler
    0                      // core 0
  );

  Serial.println("Datalog task created. Setting up coredump download...");
  
  coredumpDownloadSetup(); 

  Serial.println("Coredump download setup done. exiting setup().");
}


void loop() {
  char cmd[MAXLINE];
  unsigned long CycleStart = millis();
  if (char c=readSerial2(cmd,sizeof(cmd))) {
    Serial.print("Serial2 RX (");
    Serial.print(c);
    Serial.print("): ");
    Serial.println(cmd);
    if (c==RCCOMMANDPACKET) {
      static unsigned long int lastC=0;
      Led1 = ON;
      Led1.setWithDelay(OFF,1);
      switch(cmd[0]) {
        case 'A':
            sound_Beep();
            if (Status!=STANDBY && Power12VOn.asBoolean()) 
            if (!PotCleanerWaterIn.asBoolean()) {
              Led1 = ON;
              Led1.setWithDelay(OFF,1);        
              potCleanerWaterIn();
            }
            else {
              Led2 = ON;
              Led2.setWithDelay(OFF,1);        
              potCleanerStop();
            }
          break;

        case 'B':
          if (Status!=STANDBY && Power12VOn.asBoolean()) {
            sound_Beep();
            Led2 = ON;
            Led2.setWithDelay(OFF,1);        
            potCleanerWaterOut();
          }
          break;

        case 'C':
          lastC = millis();
          break;

        case 'D':
          if (!MILLISDIFF(lastC,2000)) {
            sound_Beep();
            Led1 = ON;
            Led1.setWithDelay(OFF,1);        
            if (Status == STANDBY) {
              startProgram(PGMMANUALCONTROL);
            }
            lastC = 0;
            // else completar com tratamento de todo com controle remoto
          }
          break;  
      }
    }
  }

  startPerfSentinel();  
  //
  verifyWiFiConnection();
  checkDebugMode();
  checkPerfSentinel("VerifyWifi");

  handle_IOTK();
  checkPerfSentinel("Handle");

  checkDebugMode();


  ProcVar::setDatalogData(RcpBatchNum,datalogFolderNameInUse,"Hot",statusNames[int(Status)], SubStatusLabel, IPCStatusNames[int(IPCStatus)]); 



  if (hasAlertMessage()) {
    sound_Attention();
  }
  controlPower();


  if (serialPooling(cmd)) 
      cmdProcess(NULL,cmd); 

  checkPerfSentinel("Main loop - Part A");

  tempVolControl();
  CircControl();
  ProcVar::acquireDallas(); 
  processDerivedVars();

  checkPerfSentinel("Main loop - Part B");

  coldSideControl();
  //dryerControl();

  checkPerfSentinel("ColdSideControl");  

  manageLevel();

  checkPerfSentinel("ManageLevel");  

  clearConsoleMessage();
  MainStateMachine();
  commitConsoleMessage();
  Todo::monitorTodos();
  sendEnvTempPacket();

  checkPerfSentinel("StateMachine and todos");  

  ProcVar::writeProcVars();

  checkPerfSentinel("WriteProcVars");  

  potCleanerHandle();
  checkPerfSentinel("PotCleanerHandle");


//-----

/*
  if (!safeMode) {
    checkPerfSentinel("Datalog");  
    doCloudLog();
    checkPerfSentinel("CloudLog");  
  }
*/

/*
  // Save Non Volatile Data
  static unsigned long int lastEEPROMWrite = 0;
  static byte oldStatus = 0;
  if (!debugging) // : estava dando problema nas interrupções dos hidrometros. perdia precisão //
    if (OnGoingProgram == PGMBREW && Status != STANDBY && Status != BREWWAIT &&
        ((MILLISDIFF(lastEEPROMWrite,SAVETOEEPROMINTERVAL)) || Status != oldStatus)) {
      ProcVar::writeToEEPROM(PROCESSPERSISTENCE);
      ProcVar::writeToEEPROM(OVERRIDEPERSISTENCE);
      lastEEPROMWrite = millis();
      oldStatus = Status;
    }

  checkPerfSentinel("EEPROM");  
*/

  // count cycles and compute cycle time
  static unsigned long maxMillis=0;

  if (millis()<maxMillis)    // detect millis cycle
      numCycles = 1;
  else
      numCycles++;
      
  averageCycle = millis() / numCycles;
  maxMillis = millis();

  int CycleTime = millis() - CycleStart;
  if (millis()>30*SECONDSms) // doesnt consider first 30 seconds, while things still being initialized
      if (CycleTime > longestCycle) longestCycle = CycleTime;

}

/* todo:
- checar hidrometros e emitir alerta
- standby desligar coisas do keg

*/