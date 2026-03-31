#define USER_SETUP_INFO
#include <FS.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>  // Biblioteca TFT_eSPI
#include <SPI.h>
#include <Wire.h>      // Biblioteca I2C
#include "IOTK.h"

#include "displayUtils.h"
#include "Povoto_UI.h"
#include "GambainoCommon.h"
#include <user_setup.h>
#include "PovotoData.h"
#include "PovotoPages.h"

#include "IOTK_NTP.h"
#include "IOTK_Dallas.h"
#include "IOTK_GLog.h"

#include "PovotoCommon.h"

#include "TemperatureControl.h"
#include "PressureControl.h"
#include "datalog.h"
#include "IOTK_ESPAsyncServer.h"
#include <esp_system.h>

//#include "esp_heap_caps.h"


TFT_eSPI tft = TFT_eSPI();  // Usa as definições do User_Setup.h
TFT_eSPI_Button touchBtn; 

//#define MAXSTATUSLEN 4096

char buf[2048];

static volatile bool resetDisplayRequested = false;
static char datalogBuffer[MAXPACKETSIZE+2];

void handleResetDisplay(AsyncWebServerRequest *request) {
  Serial.println(">>> RESET DISPLAY REQUEST <<<");
  resetDisplayRequested = true;  // defer to main loop — cannot call delay()/SPI from async task
  request->redirect("/control");
}

char * getPovotoStatus(char *st) {
  char smallBuf[100] = "Implementar";
  NTPFormatedDateTime(smallBuf);
  
  snprintf(buf,199,"<BR>WiFi SSID: %s<BR>System time: %s<BR>",WiFi.SSID().c_str(),smallBuf);
  strnncat(st,buf,MAXSTATUSLEN);

  snprintf(buf,199,"<BR>Debugging mode: %s<br>",debugging ? "ON" : "OFF");
  strnncat(st,buf,MAXSTATUSLEN);

  // incluir informações da tamperatura atual, setpoint e estado dos relés
  snprintf(buf,199,"<br>Current Temp: %.2f C<br>Considered temperature: %.2f C<br>",dallasTemperature, ControlData.temperature);

  strnncat(st,buf,MAXSTATUSLEN);      
  snprintf(buf,199,"Current Pressure: %.3f bar<br>",ControlData.pressure);
  strnncat(st,buf,MAXSTATUSLEN);
  buf[0] = '\0';
  getTemperatureControlStatus(buf);
  strnncat(st,buf,MAXSTATUSLEN);
  buf[0] = '\0';
  getPressureControlStatus(buf);
  strnncat(st,buf,MAXSTATUSLEN);
  buf[0] = '\0';
  GLogGetEspNowStatus(buf, sizeof(buf));
  strnncat(st,buf,MAXSTATUSLEN);

  getPeerStatus(st, MAXSTATUSLEN);

  return st;
}

static void handlePovotoEspNow(char type, const char *payload) {
  if (type == TRANSFERSTARTPACKET) {
    // payload: "batchNum,inoculationTemp"
    char buf[32];
    strncpy(buf, payload, 31);
    buf[31] = '\0';
    char *comma = strchr(buf, ',');
    if (!comma) return;
    *comma = '\0';
    int   batchNum = atoi(buf);
    float inocTemp = atof(comma + 1);
    BatchData.batchNumber = (uint16_t)batchNum;
    writeBatchDataToNIV();
    SetPointData.setPointTemp = inocTemp;
    SetPointData.mode = 2;
    writeSetPointDataToNIV();
    resetChillHeatCycle();
    Serial.printf("TransferStart: batch=%d temp=%.1f\n", batchNum, inocTemp);
  }
  else if (type == TRANSFERENDPACKET) {
    SetPointData.mode = 1; // fermenting
    BatchData.startPressure = ControlData.pressure;
    BatchData.startTemperature = ControlData.temperature;
    writeSetPointDataToNIV();
    resetChillHeatCycle();
    resetCountersForNewBatch();
    Serial.println("TransferEnd: mode set to 1");
  }
  else if (type == ENVTEMPPACKET) {
    environmentTemp = atof(payload);
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);


  povotoDataInit();
  //esp_register_shutdown_handler(writeCountersDataToNIV);

  setupWiFi();
  loadPeers();
  registerOwnPeer(PEERTYPE_POVOTO, FMTData.PovotoNum);

  GLogEspNowInit();
  GLogEspNowSetPeer(peerSideKick.mac);
  GLogSetOutput(GLOG_OUTPUT_ESPNOW);
  setPeersSavedCallback([]() { GLogEspNowSetPeer(peerSideKick.mac); });
  initPeerEspNowReceive();  // must be after GLogEspNowInit (which calls esp_now_init)
  setExtraEspNowHandler(handlePovotoEspNow);

  server.on("/", HTTP_GET, handleMainMenu);
  server.on("/config", HTTP_GET, handleMainMenu);
  
  server.on("/fmtdata", HTTP_GET, handleFMTDataPage);
  server.on("/fmtdata/update", HTTP_POST, handleFMTDataUpdate);
  
  server.on("/calibration", HTTP_GET, handleCalibrationDataPage);
  server.on("/calibration/update", HTTP_POST, handleCalibrationDataUpdate);
  
  server.on("/batch", HTTP_GET, handleBatchDataPage);
  server.on("/batch/update", HTTP_POST, handleBatchDataUpdate);
  
  server.on("/setpoint", HTTP_GET, handleSetPointDataPage);
  server.on("/setpoint/update", HTTP_POST, handleSetPointDataUpdate);
  
  // Sub-routes must be registered BEFORE the parent /control route,
  // because ESPAsyncWebServer uses prefix matching (a /control handler
  // also matches /control/auto, /control/relief, etc.)
  server.on("/control/update", HTTP_POST, handleControlDataUpdate);
  server.on("/control/auto", HTTP_GET, handleControlAuto);
  server.on("/control/relief", HTTP_GET, handleControlReliefOnce);
  server.on("/control/relief", HTTP_POST, handleControlReliefOnce);
  server.on("/control/startvolume", HTTP_GET, handleStartVolume);
  server.on("/control", HTTP_GET, handleControlDataPage);
  server.on("/startvolume", HTTP_GET, handleStartVolume);

  server.on("/debugparams", HTTP_GET, handleDebugParamsPage);
  server.on("/debugparams/update", HTTP_POST, handleDebugParamsUpdate);

  server.on("/pressurehistory", HTTP_GET, handlePressureHistoryCSV);
  server.on("/pressuredump", HTTP_GET, handlePressureDumpCSV);
  
  server.on("/resetdisplay", HTTP_GET, handleResetDisplay);

  setStatusSource(getPovotoStatus);
  registerPeerSetupRoute();

  initTFT();
  
  
  Wire.begin(PINSDA, PINSCL); 

  uint8_t vend = rd(0xA8); // deve dar 0x11 (FocalTech)
  uint8_t chip = rd(0xA3); // 0x06 ou 0x36

  Serial.printf("VendorID=0x%02X, ChipID=0x%02X\n", vend, chip);

  NTPBegin(-3);
  
  // Teste de inicialização do touch SPI
  //Serial.println("Inicializando touch SPI...");
  //uint16_t calData[5];
  //uint8_t calDataOK = 0;
  
  if (!LittleFS.begin()) {
    Serial.println("Erro ao montar o LittleFS");
    return;
  }
  Serial.println("LittleFS montado com sucesso");

  pinMode(TFT_RST,OUTPUT);
  pinMode(PINCHILLER, OUTPUT);
  pinMode(PINHEATER, OUTPUT);
  pinMode(PINTRANSFERVALVE, OUTPUT);
  pinMode(PINRELIEFVALVE, OUTPUT);
  pinMode(PINBUZZER, OUTPUT);
  pinMode(PINLED,OUTPUT);
  pinMode(PINBTN, INPUT);
  digitalWrite(PINCHILLER, LOW);
  digitalWrite(PINHEATER, LOW); 
  digitalWrite(PINTRANSFERVALVE, LOW);
  digitalWrite(PINRELIEFVALVE, LOW);
  digitalWrite(PINBUZZER, LOW);
  digitalWrite(PINLED, LOW);

  mainScreen();

  DallasSetup(PINDALLAS);
  uniqueDallasThermometer(dallasTemperature);

  GLogSetBuffer(datalogBuffer, sizeof(datalogBuffer));
}

void loop() {
  verifyWiFiConnection();
  checkDebugMode();
  handle_IOTK();
  { // controla sinalização de conexão do wifi
    static bool first = true;
    if (WiFi.status() != WL_CONNECTED)  {
      TFTWaintingWifiConnection();
      first = true;
    }
    else {
      if (first) {
        first = false;
        mainScreen();
      }
    }
  }

  processTouch();

  // a cada 1 segundo: atualiza temperatura e tela
  static unsigned long lastTempUpdate = 0;
  if (millis() - lastTempUpdate > 1000) {
    lastTempUpdate = millis();
    //temperature += 0.1;
    screenData();
  }

  temperatureControl();
  // While in transfer mode (mode==2), report fermenter temp to BrewCore every 30s
  {
    static unsigned long lastFermReport = 0;
    if (SetPointData.mode == 2 && MILLISDIFF(lastFermReport, 30000UL)) {
      lastFermReport = millis();
      char payload[24];
      snprintf(payload, sizeof(payload), "%d,%.2f", (int)FMTData.PovotoNum, ControlData.temperature);
      sendEspNow(peerBrewCore.mac, 0, false, (uint8_t)FERMERTEMPPACKET, payload);
    }
  }
  pressureControl();
  maybePersistCountersData();

  static unsigned long lastDataLog = 0;
  if (millis() - lastDataLog >= 60000UL) {
    lastDataLog = millis();
    doDataLog();
  }

  if (resetDisplayRequested) {
    resetDisplayRequested = false;
    resetDisplayHardware();
  }

  static unsigned long buttonPressStart = 0;
  static bool resetTriggered = false;
  bool buttonPressed = !digitalRead(PINBTN);

  if (buttonPressed) {
    //digitalWrite(PINBUZZER, HIGH);
    if (buttonPressStart == 0) {
      buttonPressStart = millis();
      resetTriggered = false;
    } else if (!resetTriggered && (millis() - buttonPressStart) >= 3000) {
      resetTriggered = true;
      resetDisplayHardware();
    }
  } else {
    //digitalWrite(PINBUZZER, LOW);
    if (buttonPressStart != 0 && !resetTriggered) {
      unsigned long pressDuration = millis() - buttonPressStart;
      if (pressDuration >= 50 && pressDuration < 3000) {

        forceScreenSaver(!isScreenSaverActive());
      }
    }
    buttonPressStart = 0;
    resetTriggered = false;
  }
}


