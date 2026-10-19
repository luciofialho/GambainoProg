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
#include "DallasSensors.h"
#include "PovotoAlarms.h"
#include "IOTK_GLog.h"

#include "PovotoCommon.h"

#include "TemperatureControl.h"
#include "PressureControl.h"
#include "datalog.h"
#include "IOTK_ESPAsyncServer.h"
#include <ElegantOTA.h>
#include "PovotoTasks.h"
#include "AutoSetpoints.h"
#include "GraphHistory.h"
#include "CloudLog.h"
#include "CloudSync.h"
#include "PovotoFilesystem.h"
#include "PovotoGraphScreen.h"
#include "AtmosphericPressure.h"
#include "CalibrationEvents.h"
#include "VentingTest.h"
#include "ExpansionTank.h"
#include <esp_system.h>
#include <esp_heap_caps.h>

//#include "esp_heap_caps.h"


TFT_eSPI tft = TFT_eSPI();  // Usa as definições do User_Setup.h
TFT_eSPI_Button touchBtn; 

//#define MAXSTATUSLEN 4096

char buf[PRESSURE_STATUS_SIZE]; // The pressure status is the largest section written here.

static volatile bool resetDisplayRequested = false;
static char datalogBuffer[MAXPACKETSIZE+2];
static char webStatusBuffer[MAXSTATUSLEN + 1];
bool soundAlarm = false;
volatile uint8_t buzzerBeepRequest = 0;

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

const char *resetReasonToString(esp_reset_reason_t reason); // IOTK_ESPAsyncServer

// Own handler (static buffer, not the library's): it must carry the header
// lines of the library's status too (uptime, reset reason).
void handlePovotoStatus(AsyncWebServerRequest *request) {
  char uptime[16];
  snprintf(webStatusBuffer, sizeof(webStatusBuffer),
    "Povoto<br>Network: %s<br>Signal strength: %d (uptime: %s)<br>Reset reason: %s<br>",
    WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "not connected",
    WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0, formatedUptime(uptime),
    resetReasonToString(esp_reset_reason()));
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

  const size_t webTotal = povotoWebFS().totalBytes();
  const size_t webUsed = povotoWebFS().usedBytes();
  const size_t dataTotal = povotoDataFS().totalBytes();
  const size_t dataUsed = povotoDataFS().usedBytes();
  snprintf(buf, sizeof(buf),
           "<b>LittleFS web:</b> %u / %u bytes used (%u free)<br>"
           "<b>LittleFS data:</b> %u / %u bytes used (%u free)%s<br>"
           "<b>PSRAM:</b> %u / %u bytes used (%u free; largest free block %u)<br>"
           "<b>Graph history:</b> %u points<br>",
           unsigned(webUsed), unsigned(webTotal), unsigned(webTotal >= webUsed ? webTotal - webUsed : 0),
           unsigned(dataUsed), unsigned(dataTotal), unsigned(dataTotal >= dataUsed ? dataTotal - dataUsed : 0),
           povotoHasSeparateDataFS() ? "" : " (shared with web)",
           unsigned(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) -
                    heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
           unsigned(heap_caps_get_total_size(MALLOC_CAP_SPIRAM)),
           unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
           unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
           unsigned(graphHistoryCount()));
  strnncat(st, buf, MAXSTATUSLEN);

  snprintf(buf, 199, "<br>Temp: %.2f C (considered %.2f C); pressure %.3f bar<br>",
           dallasTemperature, ControlData.temperature, ControlData.pressure);
  strnncat(st, buf, MAXSTATUSLEN);
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
      snprintf(buf, 199, "<b>Window end time:</b> %lu ms (in %ld s)<br>", taskWindowEndTime, secsLeft);
    } else {
      snprintf(buf, 199, "<b>Window end time:</b> %lu ms (expired)<br>", taskWindowEndTime);
    }
    strnncat(st, buf, MAXSTATUSLEN);
  }

  // Estado atual dos pinos digitais
  snprintf(buf, 199,
    "<br><b>Pins:</b> Chiller=%d LedChiller=%d Heater=%d LedHeater=%d Valve=%d VentLed=%d Buzzer=%d Led=%d<br>",
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
  else if (type == CLOUDCMDPACKET) {
    cloudSyncReceive(payload, espNowSenderMac()); // applied in loop(), SideKick only
  }
  else if (type == ENVTEMPPACKET) {
    // The SideKick's own thermometer is preferred over the BrewCore's.
    const uint8_t *sender = espNowSenderMac();
    bool sidekickMac = false;
    for (uint8_t i = 0; i < 6; ++i) sidekickMac = sidekickMac || peerSideKick.mac[i] != 0;
    sidekickMac = sidekickMac && sender && memcmp(sender, peerSideKick.mac, 6) == 0;
    setEnvironmentTemperatureFromPacket(payload, sidekickMac);
  }
  else if (type == ATMPRESSUREPACKET) {
    setAtmosphericPressureFromPacket(payload);
  }
}

void setup() {
  Serial.begin(115200);
  delay(50);
  NTPBegin(-3);   // antes de WiFi/servidor: nenhum sync NTP pode ocorrer com fuso 0


  povotoDataInit();
  debugModeProvider = []() { return FMTData.debugMode; };
  datalogFolderProvider = []() -> const char * { return debugging ? "GambainoDebug" : FMTData.googleLogFolder; };
  applyDebugMode(FMTData.debugMode);
  cloudLogBegin();
  cloudSyncBegin();
  ElegantOTA.onStart([]() {
    writeCountersDataToNIV();
    saveCO2Buffers(); // the CO2 windows continue after the update (docs/gco2-rate.md)
  });
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

  // The browser opens on the dashboard; its menu icon leads to /menu.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect(povotoWiFiConfigurationActive() ? "/wifi" : "/dashboard/");
  });
  server.on("/menu", HTTP_GET, handleMainMenu);
  // Old bookmarks and the previous dashboard point to /config.
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/menu"); });
  // Logos, with the web pages in LittleFS (data/www/assets); the cloud copies
  // the same files.
  server.on("/assets/povoto.svg", HTTP_GET, [](AsyncWebServerRequest *request) {
    sendWebPageFile(request, "/www/assets/povoto.svg", "image/svg+xml");
  });
  server.on("/assets/brewtal.svg", HTTP_GET, [](AsyncWebServerRequest *request) {
    sendWebPageFile(request, "/www/assets/brewtal.svg", "image/svg+xml");
  });
  
  server.on("/fmtsettings/save", HTTP_GET, handleFMTDataSave);
  server.on("/fmtsettings/load", HTTP_POST, handleFMTDataLoad);
  server.on("/fmtsettings/update", HTTP_POST, handleFMTDataUpdate);
  server.on("/fmtsettings/dallasscan", HTTP_GET, handleDallasScan);
  server.on("/fmtsettings", HTTP_GET, handleFMTDataPage);
  // Old name of the page (bookmarks).
  server.on("/fmtdata", HTTP_GET, [](AsyncWebServerRequest *request) { request->redirect("/fmtsettings"); });
  server.on("/userConfig", HTTP_GET, handleUserConfigPage);
  server.on("/userConfig/update", HTTP_POST, handleUserConfigUpdate);
  
  server.on("/calibration/zero/start", HTTP_POST, handleSensorZeroStart);
  server.on("/calibration/zero/abort", HTTP_POST, handleSensorZeroAbort);
  server.on("/calibration/zero/save", HTTP_POST, handleSensorZeroSave);
  server.on("/calibration/events.txt", HTTP_GET, handleCalibrationEventsText);
  server.on("/calibration/venting/start", HTTP_POST, handleVentingTestStart);
  server.on("/calibration/venting/abort", HTTP_POST, handleVentingTestAbort);
  server.on("/calibration/venting/save", HTTP_POST, handleVentingTestSave);
  server.on("/calibration/venting.csv", HTTP_GET, handleVentingTestCSV);
  server.on("/calibration/cleaning/start", HTTP_POST, handleValveCleaningStart);
  server.on("/calibration/cleaning/abort", HTTP_POST, handleValveCleaningAbort);
  server.on("/calibration/expansion/start", HTTP_POST, handleExpansionTankStart);
  server.on("/calibration/expansion/abort", HTTP_POST, handleExpansionTankAbort);
  server.on("/calibration/expansion/save", HTTP_POST, handleExpansionTankSave);
  server.on("/calibration/expansion.csv", HTTP_GET, handleExpansionTankCSV);
  server.on("/calibration", HTTP_GET, handleCalibrationDataPage);
  server.on("/calibration/refresh-current", HTTP_GET, handleCalibrationCurrentRefresh);
  server.on("/calibration/update", HTTP_POST, handleCalibrationDataUpdate);
  
  server.on("/batch", HTTP_GET, handleBatchDataPage);
  server.on("/batch/update", HTTP_POST, handleBatchDataUpdate);

  server.on("/counters", HTTP_GET, handleCountersDataPage);
  server.on("/counters/update", HTTP_POST, handleCountersDataUpdate);
  
  // Sub-routes must be registered BEFORE the parent /setpoint route (ESPAsyncWebServer prefix matching)
  server.on("/setpoint/update", HTTP_POST, handleSetPointDataUpdate);
  server.on("/setpoint/auto/update", HTTP_POST, handleAutoSetpointUpdate);
  server.on("/setpoint/auto/reset", HTTP_POST, handleAutoSetpointReset);
  server.on("/setpoint/auto/trigger", HTTP_POST, handleAutoSetpointTrigger);
  server.on("/setpoint/auto/export", HTTP_GET, handleAutoSetpointExport);
  server.on("/setpoint/auto/import", HTTP_POST, handleAutoSetpointImport);
  server.on("/setpoint", HTTP_GET, handleSetPointDataPage);
  
  // Sub-routes must be registered BEFORE the parent /control route,
  // because ESPAsyncWebServer uses prefix matching (a /control handler
  // also matches /control/auto, /control/relief, etc.)
  server.on("/control/update", HTTP_POST, handleControlDataUpdate);
  server.on("/control/auto", HTTP_GET, handleControlAuto);
  server.on("/control/relief", HTTP_POST, handleControlReliefOnce);
  server.on("/control", HTTP_GET, handleControlDataPage);

  server.on("/debugparams/graphs/demo", HTTP_POST, handleGraphsGenerateDemo);
  server.on("/debugparams/update", HTTP_POST, handleDebugParamsUpdate);
  server.on("/debugparams/cloud", HTTP_POST, handleDebugCloudUpdate);
  server.on("/debugparams", HTTP_GET, handleDebugParamsPage);
  // Register child paths before /graphs (prefix-matching async server).
  server.on("/graphs/meta.json", HTTP_GET, handleGraphsMeta);
  server.on("/graphs/data.csv", HTTP_GET, handleGraphsCSV);
  server.on("/graphs/uPlot.iife.min.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/www/uPlot.iife.min.js", "application/javascript");
  });
  server.on("/graphs/uPlot.min.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/www/uPlot.min.css", "text/css");
  });
  server.on("/graphs/graphs.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    sendWebPageFile(request, "/www/graphs.js", "application/javascript");
  });
  server.on("/graphs/graphs.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    sendWebPageFile(request, "/www/graphs.css", "text/css");
  });
  server.on("/graphs/", HTTP_GET, handleGraphsPage);
  server.on("/graphs", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/graphs/");
  });

  // Register child paths before /dashboard (prefix-matching async server).
  server.on("/dashboard/status.json", HTTP_GET, handleDashboardStatus);
  server.on("/dashboard/dashboard.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    sendWebPageFile(request, "/www/dashboard.js", "application/javascript");
  });
  server.on("/dashboard/lcars.ttf", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/www/lcars.ttf", "font/ttf");
    response->addHeader("Cache-Control", "max-age=86400");
    request->send(response);
  });
  // Same background bitmap as the TFT main screen; browsers render BMP natively.
  server.on("/dashboard/LCars.bmp", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/LCars.bmp", "image/bmp");
    response->addHeader("Cache-Control", "max-age=86400");
    request->send(response);
  });
  server.on("/dashboard/", HTTP_GET, handleDashboardPage);
  server.on("/dashboard", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/dashboard/");
  });

  // Sub-routes must be registered BEFORE the parent /tasks route (ESPAsyncWebServer prefix matching)
  server.on("/tasks/start", HTTP_GET, handleTaskStart);
  server.on("/tasks/active", HTTP_GET, handleTaskActive);
  server.on("/tasks/finish", HTTP_GET, handleTaskFinish);
  server.on("/tasks/cancel", HTTP_GET, handleTaskCancel);
  server.on("/tasks", HTTP_GET, handleTasksPage);

  
  server.on("/resetdisplay", HTTP_GET, handleResetDisplay);
  server.on("/net", HTTP_GET, handleReconnectNetwork);
  server.on("/factoryreset", HTTP_POST, handleFactoryReset);
  server.on("/getstatus", HTTP_GET, handlePovotoStatus);
  server.on("/ventstatus", HTTP_GET, handleVentStatus);
  server.on("/restart", HTTP_GET, ESPRestart);
  povotoWiFiRegisterRoutes();

  setStatusSource(getPovotoStatus);
  registerPeerSetupRoute();
  ElegantOTA.begin(&server);

  // Legacy devices mount the original shared partition; migrated devices
  // mount independent web and persistent-data partitions. Never autoformat.
  if (!povotoFilesystemBegin()) {
    Serial.println("Erro ao montar o LittleFS (web ou dados)");
    return;
  }
  Serial.printf("LittleFS montado: %s\n",
                povotoHasSeparateDataFS() ? "web + persist" : "legado compartilhado");
  graphHistoryBegin();
  server.begin();

  initTFT();
  
  
  Wire.begin(PINSDA, PINSCL); 
  atmosphericPressureBegin(); // the pressure comes from the SideKick (docs/atmospheric-pressure.md)

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
  statusLedWrite(0, 0, 0);
  povotoAlarmsSetup();

  mainScreen();

  DallasSetup(PINDALLAS);
  dallasSensorsSetup(); // one slot per role (docs/thermometers.md)
  if (!dallasRoleAssigned(DALLAS_ROLE_FERMENTER))
    Serial.println("[DALLAS] Fermenter thermometer NOT assigned: temperature control idle");
  else if (!dallasOnBus(FMTData.dallasAddress[DALLAS_ROLE_FERMENTER]))
    Serial.println("[DALLAS] Fermenter thermometer NOT found on the bus");

  GLogSetBuffer(datalogBuffer, sizeof(datalogBuffer));
}

void loop() {
  ElegantOTA.loop(); // required to actually reboot after a successful update
  povotoWiFiProcess();
  checkTaskExpiration();
  collectTaskLogSamples();
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
  atmosphericPressureProcess();
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
  graphHistorySampleIfDue();
  updateGraphScreenIfNeeded();
  evaluateAutoSetpoints();
  maybeSendBrewfatherLog();
  maybeSendCloudLog();
  cloudSyncProcess();
  maybePersistCountersData();

  static unsigned long lastDataLog = 0;
  if (MILLISDIFF(lastDataLog, (unsigned long)FMTData.dataLogIntervalSeconds * 1000UL)) {
    lastDataLog = millis();
    doDataLog();
  }

  if (resetDisplayRequested) {
    resetDisplayRequested = false;
    resetDisplayHardware();
  }

  processPovotoAlarms(); // sensor alarms: beeps, error screen, LEDs (docs/alarms.md)

  { // buzzer do alarme: liga por 1s a cada acionamento de soundAlarm;
    // buzzerBeepRequest toca N bipes curtos (250 ms ligado, 250 ms desligado)
    static unsigned long lastAlarm = 0;
    static unsigned long beepStart = 0;
    static uint8_t beepCount = 0;
    if (buzzerBeepRequest) {
      beepCount = buzzerBeepRequest;
      buzzerBeepRequest = 0;
      beepStart = millis();
    }
    if (soundAlarm) {
      lastAlarm = millis();
      soundAlarm = false;
      digitalWrite(PINBUZZER, millis() % 1000 < 250 ? HIGH : LOW);
    }
    else if (beepCount) {
      const unsigned long phase = (millis() - beepStart) / 250UL;
      if (phase >= 2UL * beepCount) {
        beepCount = 0;
        digitalWrite(PINBUZZER, LOW);
      }
      else
        digitalWrite(PINBUZZER, phase % 2 == 0 ? HIGH : LOW);
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
 * 6. SideKick/src/Sidekick-log.cpp: erro HTTP permanente no Brewfather retém
 *    o mesmo payload indefinidamente, bloqueando os seguintes e enchendo a
 *    fila. Distinguir erros permanentes de falhas que justificam nova tentativa.
 * 7. SideKick/src/Sidekick-log.cpp: o envio ao Google libera o lote apos escrever
 *    o POST mesmo com resposta HTTP 429/500. Distinguir rejeicao HTTP explicita
 *    de resposta ausente e registrar o codigo real no diagnostico.
 * 
 * 
 * a atualizacao dos campos na tela (temperatura, por exemplo), está deixando sujeira quando os caracteres ocupam menos espaço que a atualização anterior.
 * nao calcular as coisas (abv, volume, etc) quando estiver em off
 * Futuramente, tratar casos de fermentações muito rapidas
 * 
 */
