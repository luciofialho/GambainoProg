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
#include "PovotoWifi.h"

#include "IOTK_NTP.h"
#include "IOTK_Dallas.h"
#include "IOTK_GLog.h"

#include "PovotoCommon.h"

#include "TemperatureControl.h"
#include "PressureControl.h"
#include "datalog.h"
#include "IOTK_ESPAsyncServer.h"
#include <ElegantOTA.h>
#include "PovotoTasks.h"
#include <esp_system.h>

//#include "esp_heap_caps.h"


TFT_eSPI tft = TFT_eSPI();  // Usa as definições do User_Setup.h
TFT_eSPI_Button touchBtn; 

//#define MAXSTATUSLEN 4096

char buf[2048];

static volatile bool resetDisplayRequested = false;
static char datalogBuffer[MAXPACKETSIZE+2];
static char webStatusBuffer[MAXSTATUSLEN + 1];
bool soundAlarm = false;

char *getPovotoStatus(char *st);

void handleResetDisplay(AsyncWebServerRequest *request) {
  Serial.println(">>> RESET DISPLAY REQUEST <<<");
  resetDisplayRequested = true;  // defer to main loop — cannot call delay()/SPI from async task
  request->redirect("/control");
}

void handleReconnectNetwork(AsyncWebServerRequest *request) {
  Serial.println(">>> RECONNECTNETWORK REQUEST <<<");
  povotoWiFiReconnect();
  responseConfirmation(request, "Reconnecting to WiFi...", "/control");
}

void handleFactoryReset(AsyncWebServerRequest *request) {
  if (!resetPovotoDataToFactoryDefaults()) {
    request->send(500, "text/plain", "Factory reset could not be saved to NVS.");
    return;
  }
  request->send(200, "text/plain", "Factory defaults restored.");
}

void handlePovotoStatus(AsyncWebServerRequest *request) {
  snprintf(webStatusBuffer, sizeof(webStatusBuffer),
    "Povoto<br>Network: %s<br>Signal strength: %d<br>",
    WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "not connected",
    WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  getPovotoStatus(webStatusBuffer);
  request->send(200, "text/html; charset=utf-8", webStatusBuffer);
}

char * getPovotoStatus(char *st) {
  char smallBuf[100] = "Implementar";
  NTPFormatedDateTime(smallBuf);
  
  snprintf(buf,199,"<BR>System time: %s<BR>",smallBuf);
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

  // Window type and end time
  if (taskWindowType == 0) {
    strnncat(st, "<br><b>Window:</b> none<br>", MAXSTATUSLEN);
  } else {
    static const char *taskLabels[] = { "", "Dump", "Gas venting/injection", "Liquid addition", "Dry hopping", "Dynamic hopping" };
    const char *label = (taskWindowType <= 5) ? taskLabels[taskWindowType] : "Unknown";
    snprintf(buf, 199, "<br><b>Window type:</b> %d (%s)<br>", taskWindowType, label);
    strnncat(st, buf, MAXSTATUSLEN);
  }
  if (taskWindowEndTime == 0) {
    strnncat(st, "<b>Window end time:</b> 0 (expired)<br>", MAXSTATUSLEN);
  } else {
    unsigned long now = millis();
    if (taskWindowEndTime > now) {
      long secsLeft = (long)((taskWindowEndTime - now) / 1000UL);
      snprintf(buf, 199, "<b>Window end time:</b> %lu ms (daqui a %ld segundos)<br>", taskWindowEndTime, secsLeft);
    } else {
      snprintf(buf, 199, "<b>Window end time:</b> %lu ms (expirada)<br>", taskWindowEndTime);
    }
    strnncat(st, buf, MAXSTATUSLEN);
  }

  // Estado atual dos pinos digitais
  snprintf(buf, 199,
    "<br><b>Pins:</b> Chiller=%d LedChiller=%d Heater=%d LedHeater=%d TransferValve=%d ReliefValve=%d Buzzer=%d Led=%d<br>",
    digitalRead(PINCHILLER), digitalRead(PINLEDCHILLER), digitalRead(PINHEATER), digitalRead(PINLEDHEATER),
    digitalRead(PINTRANSFERVALVE), digitalRead(PINVENTINGLED), digitalRead(PINBUZZER), digitalRead(PINLED));
  strnncat(st, buf, MAXSTATUSLEN);

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
    SetPointData.mode = MODE_BREWING_TRANSFERING;
    writeSetPointDataToNIV();
    resetChillHeatCycle();
    Serial.printf("TransferStart: batch=%d temp=%.1f\n", batchNum, inocTemp);
  }
  else if (type == TRANSFERENDPACKET) {
    SetPointData.mode = MODE_FERMENTING;
    BatchData.startPressure = ControlData.pressure;
    BatchData.startTemperature = ControlData.temperature;
    writeSetPointDataToNIV();
    resetChillHeatCycle();
    resetCountersForNewBatch();
    Serial.println("TransferEnd: mode set to fermenting");
  }
  else if (type == ENVTEMPPACKET) {
    environmentTemp = atof(payload);
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);


  povotoDataInit();
  ElegantOTA.onStart(writeCountersDataToNIV);
  //esp_register_shutdown_handler(writeCountersDataToNIV);

  povotoWiFiInit();
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
  
  server.on("/fmtdata/save", HTTP_GET, handleFMTDataSave);
  server.on("/fmtdata/load", HTTP_POST, handleFMTDataLoad);
  server.on("/fmtdata/update", HTTP_POST, handleFMTDataUpdate);
  server.on("/fmtdata", HTTP_GET, handleFMTDataPage);
  server.on("/userConfig", HTTP_GET, handleUserConfigPage);
  server.on("/userConfig/update", HTTP_POST, handleUserConfigUpdate);
  
  server.on("/calibration/speed.csv", HTTP_GET, handleSpeedCalibrationCSV);
  server.on("/calibration/speed", HTTP_POST, handleStartSpeedCalibration);
  server.on("/calibration", HTTP_GET, handleCalibrationDataPage);
  server.on("/calibration/refresh-current", HTTP_GET, handleCalibrationCurrentRefresh);
  server.on("/calibration/update", HTTP_POST, handleCalibrationDataUpdate);
  
  server.on("/batch", HTTP_GET, handleBatchDataPage);
  server.on("/batch/update", HTTP_POST, handleBatchDataUpdate);

  server.on("/counters", HTTP_GET, handleCountersDataPage);
  server.on("/counters/update", HTTP_POST, handleCountersDataUpdate);
  
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

  // Sub-routes must be registered BEFORE the parent /tasks route (ESPAsyncWebServer prefix matching)
  server.on("/tasks/start", HTTP_GET, handleTaskStart);
  server.on("/tasks/active", HTTP_GET, handleTaskActive);
  server.on("/tasks/finish", HTTP_GET, handleTaskFinish);
  server.on("/tasks/cancel", HTTP_GET, handleTaskCancel);
  server.on("/tasks", HTTP_GET, handleTasksPage);

  server.on("/pressurehistory", HTTP_GET, handlePressureHistoryCSV);
  server.on("/pressuredump", HTTP_GET, handlePressureDumpCSV);
  
  server.on("/resetdisplay", HTTP_GET, handleResetDisplay);
  server.on("/net", HTTP_GET, handleReconnectNetwork);
  server.on("/factoryreset", HTTP_POST, handleFactoryReset);
  server.on("/getstatus", HTTP_GET, handlePovotoStatus);
  server.on("/restart", HTTP_GET, ESPRestart);
  povotoWiFiRegisterRoutes();

  setStatusSource(getPovotoStatus);
  registerPeerSetupRoute();
  ElegantOTA.begin(&server);
  server.begin();

  if (!LittleFS.begin(true)) {
    Serial.println("Erro ao montar o LittleFS");
    return;
  }
  Serial.println("LittleFS montado com sucesso");

  initTFT();
  
  
  Wire.begin(PINSDA, PINSCL); 

  Wire.beginTransmission(FT_ADDR);
  bool touchPresent = (Wire.endTransmission() == 0);
  setTouchControllerReady(touchPresent);
  if (touchPresent) {
    uint8_t vend = rd(0xA8); // deve dar 0x11 (FocalTech)
    uint8_t chip = rd(0xA3); // 0x06 ou 0x36
    Serial.printf("VendorID=0x%02X, ChipID=0x%02X\n", vend, chip);
  } else {
    Serial.printf("Touch FocalTech nao detectado no I2C (addr 0x%02X, SDA=%d SCL=%d)\n", FT_ADDR, PINSDA, PINSCL);
  }

  NTPBegin(-3);
  
  // Teste de inicialização do touch SPI
  //Serial.println("Inicializando touch SPI...");
  //uint16_t calData[5];
  //uint8_t calDataOK = 0;
  
  pinMode(TFT_RST,OUTPUT);
  pinMode(PINCHILLER, OUTPUT);
  pinMode(PINLEDCHILLER, OUTPUT);
  pinMode(PINHEATER, OUTPUT);
  pinMode(PINLEDHEATER, OUTPUT);
  pinMode(PINTRANSFERVALVE, OUTPUT);
  pinMode(PINVENTINGLED, OUTPUT);
  pinMode(PINBUZZER, OUTPUT);
  pinMode(PINBTN, INPUT);
  digitalWrite(PINCHILLER, LOW);
  digitalWrite(PINLEDCHILLER, LOW);
  digitalWrite(PINHEATER, LOW); 
  digitalWrite(PINLEDHEATER, LOW);  
  digitalWrite(PINTRANSFERVALVE, LOW);
  digitalWrite(PINVENTINGLED, LOW);
  digitalWrite(PINBUZZER, LOW);
  neopixelWrite(PINLED, 0, 0, 0);

  mainScreen();

  DallasSetup(PINDALLAS);
  uniqueDallasThermometer(dallasTemperature);
  delay(800); // aguarda 1a conversao do DS18B20 para diagnostico
  if (dallasTemperature == NOTaTEMP || dallasTemperature == 85) {
    Serial.println("[DALLAS] Termometro NAO detectado");
  } else {
    Serial.printf("[DALLAS] Termometro detectado. Temperatura inicial: %.2f C\n", dallasTemperature);
  }

  GLogSetBuffer(datalogBuffer, sizeof(datalogBuffer));
}

void loop() {
  ElegantOTA.loop(); // required to actually reboot after a successful update
  povotoWiFiProcess();
  checkTaskExpiration();
  updateTaskUIIfActive();
  handle_IOTK();
  { // controla sinalização de conexão do wifi
    static bool first = true;
    if (povotoWiFiDrawTft()) {
      first = true;
    }
    else {
      if (first) {
        first = false;
        if (!isTempKeyboardActive())
          mainScreen();
      }
    }
  }

  processTouch();

  // a cada 1 segundo: atualiza temperatura e tela (não durante o teclado)
  static unsigned long lastTempUpdate = 0;
  if (!isTempKeyboardActive() && MILLISDIFF(lastTempUpdate, 1000)) {
    lastTempUpdate = millis();
    //temperature += 0.1;
    screenData();
  }

  temperatureControl();
  // While in transfer mode (MODE_BREWING_TRANSFERING), report fermenter temp to BrewCore every 30s
  {
    static unsigned long lastFermReport = 0;
    if (SetPointData.mode == MODE_BREWING_TRANSFERING && MILLISDIFF(lastFermReport, 30000UL)) {
      lastFermReport = millis();
      char payload[24];
      snprintf(payload, sizeof(payload), "%d,%.2f", (int)FMTData.PovotoNum, ControlData.temperature);
      sendEspNow(peerBrewCore.mac, 0, false, (uint8_t)FERMERTEMPPACKET, payload);
    }
  }
  pressureControl();
  maybeSendBrewfatherLog();
  maybePersistCountersData();

  static unsigned long lastDataLog = 0;
  if (MILLISDIFF(lastDataLog, 60000UL)) {
    lastDataLog = millis();
    doDataLog();
  }

  if (resetDisplayRequested) {
    resetDisplayRequested = false;
    resetDisplayHardware();
  }

  { // buzzer do alarme: liga por 1s a cada acionamento de soundAlarm
    static unsigned long lastAlarm = 0;
    if (soundAlarm) {
      lastAlarm = millis();
      soundAlarm = false;
      digitalWrite(PINBUZZER, millis() % 1000 < 250 ? HIGH : LOW);
    } 
    else if (millis() > lastAlarm + 1000) {
      digitalWrite(PINBUZZER, LOW);
    }
  }

  static unsigned long buttonPressStart = 0;
  static bool resetTriggered = false;
  bool buttonPressed = !digitalRead(PINBTN);

  if (buttonPressed) {
    //digitalWrite(PINBUZZER, HIGH);
    if (buttonPressStart == 0) {
      buttonPressStart = millis();
      resetTriggered = false;
    } else if (!resetTriggered && (MILLISDIFF(buttonPressStart, 3000))) {
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


/*
Calculo de vbolume do headspace está sensível a pressão ou a temperatura. volume caiu só de subir pressão/temperatura 
o task de dump está jogando o volume lá para cima

*/

/* Pendencias da revisao de logica e memoria (itens ainda nao corrigidos):
 * 2. PressureControl.cpp: o callback de pressure_history.csv pode ler
 *    pressureReliefHistory enquanto o loop libera esse buffer ao sair de OFF.
 *    Garantir a vida util do historico durante toda a exportacao.
 * 3. PressureControl.cpp: downloads interrompidos podem deixar
 *    pressureDumpInProgress/pressureHistoryExportInProgress ativos, bloqueando
 *    novos downloads e, no dump, a coleta. Limpar o estado na desconexao.
 * 4. PressureControl.cpp: concluir pressure_dump.csv apaga pressureSamples
 *    mesmo durante a determinacao de volume, impedindo novas amostras nessa
 *    rodada. Separar a exportacao da liberacao do buffer ainda em uso.
 * 6. SideKick/src/Sidekick-log.cpp: erro HTTP permanente no Brewfather retém
 *    o mesmo payload indefinidamente, bloqueando os seguintes e enchendo a
 *    fila. Distinguir erros permanentes de falhas que justificam nova tentativa.
 * 7. SideKick/src/Sidekick-log.cpp: o envio ao Google libera o lote apos escrever
 *    o POST mesmo com resposta HTTP 429/500. Distinguir rejeicao HTTP explicita
 *    de resposta ausente e registrar o codigo real no diagnostico.
 */
