#include <Arduino.h>
//#include <IRRemoteESP8266.h>
//#include <IRac.h>
#include <WiFi.h>
#include "GambainoCommon.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "Sidekick-log.h"
#include "IOTK.h"
#include <RCSwitch.h>
#include <esp_now.h>

// Task handle para envio de logs
TaskHandle_t logSendTaskHandle = NULL;

//HTTPClient http;
//bool httpInitialized = false;


// ================== DISPLAY ==================
// #define SCREEN_WIDTH 128
// #define SCREEN_HEIGHT 64
// #define OLED_RESET    -1
// #define SCREEN_ADDRESS 0x3C
// 
// #define I2C_SDA 27
// #define I2C_SCL 14
// 
// Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


#define RESETBREWCOREPIN 16

// ================== IR Remote =======

//#define IR_RECEIVE_PIN 04
//IRrecv irrecv(IR_RECEIVE_PIN, kCaptureBufferSize, kTimeout, true);
//const uint8_t  kTimeout            = 255;
//const uint16_t kCaptureBufferSize  = 1024;
//decode_results results;
//const uint8_t  kTolerancePercentage = 40;

#define PINRF 22
RCSwitch rf = RCSwitch();

// ================== SERIAL2 (GPIO 4/5) ========
#define SERIAL2_RX 18
#define SERIAL2_TX 19

#define MAX_INSTABILITY_MSGS 5
char exceptionMsgs[MAX_INSTABILITY_MSGS][100] = {"","","","",""};
byte nextInstabilityMsgIndex = 0;
char sentinelMsg[100] = "";

// ================== RF COMMAND LOG ==================
#define MAX_RF_LOG 8
struct RFLogEntry {
  unsigned long millis;
  unsigned long rfCode;
  char          command[4];
};
static RFLogEntry rfLog[MAX_RF_LOG];
static byte       rfLogHead = 0;
static byte       rfLogCount = 0;

static void logRFCommand(unsigned long code, const char *cmd) {
  rfLog[rfLogHead].millis  = millis();
  rfLog[rfLogHead].rfCode  = code;
  strncpy(rfLog[rfLogHead].command, cmd, sizeof(rfLog[0].command) - 1);
  rfLog[rfLogHead].command[sizeof(rfLog[0].command) - 1] = '\0';
  rfLogHead = (rfLogHead + 1) % MAX_RF_LOG;
  if (rfLogCount < MAX_RF_LOG) rfLogCount++;
}


char serial2Buffer[MAXPACKETSIZE+1];  // Buffer global para recepção Serial2

// ===== ESP-NOW receive queue (callback → loop) =====
// The ESP-NOW callback runs on Core 0; loop() runs on Core 1.
// We use a simple lock-free ring buffer so the callback only copies bytes
// and the main loop does all processing safely.
#define ESPNOW_QUEUE_SLOTS  8
#define ESPNOW_MAX_FRAME   250
struct EspNowFrame {
  uint8_t data[ESPNOW_MAX_FRAME];
  int     len;
  uint8_t senderMac[6];
};
static EspNowFrame        espnowQueue[ESPNOW_QUEUE_SLOTS];
static volatile int       espnowQHead = 0;  // written by callback (Core 0)
static volatile int       espnowQTail = 0;  // read    by loop()  (Core 1)

static void handlePacket(char type, const char *payload) {
  switch (type) {
    case LOGPACKET: {
      char last = payload[strlen(payload) - 1];
      if (last == '}' || last == ',') {
        cashLogRequest((char*)payload);
      } else {
        Serial.println("Invalid log packet received - missing closing brace");
      }
    } break;

    case SENTINELPACKET:
      Serial.print("Sentinel from Brewcore: ");
      Serial.println(payload);
      strncpy(sentinelMsg, payload, sizeof(sentinelMsg) - 1);
      sentinelMsg[sizeof(sentinelMsg) - 1] = '\0';
      break;

    case INSTABILITYMSGPACKET:
      Serial.print("Instability message from Brewcore: ");
      Serial.println(payload);
      strncpy(exceptionMsgs[nextInstabilityMsgIndex], payload, sizeof(exceptionMsgs[0]) - 1);
      exceptionMsgs[nextInstabilityMsgIndex][sizeof(exceptionMsgs[0]) - 1] = '\0';
      nextInstabilityMsgIndex = (nextInstabilityMsgIndex + 1) % MAX_INSTABILITY_MSGS;
      break;
  }
}

// Wrapper called from loop with senderMac context
static void handlePacketWithMac(char type, const char *payload, const uint8_t *senderMac) {
  Serial.printf("[ESP-NOW] type='%c'(0x%02X) len=%d from ...%02X:%02X\n",
    (type >= 32 ? type : '?'), (uint8_t)type, (int)strlen(payload),
    senderMac[4], senderMac[5]);
  if (type == PEERBROADCASTPACKET || type == PEERREPLYPACKET) {
    handlePeerEspNow(type, payload, senderMac);
  } else {
    handlePacket(type, payload);
  }
}

static void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
  int next = (espnowQHead + 1) % ESPNOW_QUEUE_SLOTS;
  if (next == espnowQTail) return;  // queue full — drop frame
  if (len > ESPNOW_MAX_FRAME) len = ESPNOW_MAX_FRAME;
  memcpy(espnowQueue[espnowQHead].data, data, len);
  espnowQueue[espnowQHead].len = len;
  if (mac) memcpy(espnowQueue[espnowQHead].senderMac, mac, 6);
  else     memset(espnowQueue[espnowQHead].senderMac, 0, 6);
  espnowQHead = next;  // publish slot (32-bit write is atomic on Xtensa)
}

// ================== TASK DE ENVIO DE LOGS ====
void logSendTask(void *pvParameters) {
  const TickType_t xDelay = pdMS_TO_TICKS(15000); // 15 segundos
  
  for(;;) {
    sendLogToGoogleSheets();
    
    vTaskDelay(xDelay); // Suspende a task por 15 segundos
  }
}

char *getSideKickStatus(char *st) {
  char buf1[2048];
  strcat(st, getLogStatus(buf1));

  strcat(st, "<br>Performance issue Message:");
  strcat(st, sentinelMsg);


  strcat(st, "<br><br>Instability Messages:<br>");
  for (int i = 0; i < MAX_INSTABILITY_MSGS; i++) {
    byte idx = (nextInstabilityMsgIndex - i + 2*MAX_INSTABILITY_MSGS - 1) % MAX_INSTABILITY_MSGS;
    strcat(st, exceptionMsgs[idx]);
    strcat(st, "<br>"); 
  }

  strcat(st, "<br>Últimos comandos RF enviados ao BrewCore:<br>");
  if (rfLogCount == 0) {
    strcat(st, "(nenhum)<br>");
  } else {
    char rfBuf[80];
    for (byte i = 0; i < rfLogCount; i++) {
      byte idx = (rfLogHead + MAX_RF_LOG - rfLogCount + i) % MAX_RF_LOG;
      unsigned long age = (millis() - rfLog[idx].millis) / 1000UL;
      snprintf(rfBuf, sizeof(rfBuf), "&nbsp;&nbsp;[%c] código %lu &nbsp;(há %lus)<br>",
               rfLog[idx].command[0], rfLog[idx].rfCode, age);
      strcat(st, rfBuf);
    }
  }

  uint32_t espChunks, espDrops, espResets;
  getEspNowStats(&espChunks, &espDrops, &espResets);
  char buf2[128];
  snprintf(buf2, sizeof(buf2), "<br>ESP-NOW chunks: %lu<br>ESP-NOW drops: %lu<br>ESP-NOW resets: %lu<br>",
           (unsigned long)espChunks,
           (unsigned long)espDrops,
           (unsigned long)espResets);
  strcat(st, buf2);

  getPeerStatus(st, MAXSTATUSLEN);

  return st;
}

// ================== SETUP =====================
void setup() {
  strcpy(ESP_AppName, "Gambaino - SideKick");  
  Serial.begin(115200);
  delay(500);

  // WiFi Setup
  setupWiFi();
  loadPeers();
  registerOwnPeer(PEERTYPE_SIDEKICK);

  setStatusSource(getSideKickStatus);
  registerPeerSetupRoute();

  pinMode(RESETBREWCOREPIN, OUTPUT);
  digitalWrite(RESETBREWCOREPIN, LOW); 


  xTaskCreatePinnedToCore(
    logSendTask,           // Função da task
    "LogSend",             // Nome da task (para debug)
    8192,                  // Stack size (16KB)
    NULL,                  // Parâmetros (não usado)
    1,                     // Prioridade (1 = baixa)
    &logSendTaskHandle,    // Handle da task
    1                      // Core 1 – TLS handshake é CPU-intensivo; Core 0 fica livre para IDLE/WiFi stack
  );
  Serial.println("Log send task created on core 0");

  // I2C (comentado - display não está sendo usado)
  // Wire.begin(I2C_SDA, I2C_SCL);

  /*
  pinMode(IR_RECEIVE_PIN, INPUT_PULLUP);

  Serial.println("\nIniciando receptor IR...");
  irrecv.setTolerance(kTolerancePercentage);
  irrecv.enableIRIn();  // Start the receiver
  Serial.println("Receptor IR ATIVO - aguardando sinais...");
  Serial.print("Buffer size: ");
  Serial.println(kCaptureBufferSize);
  Serial.print("Timeout: ");
  Serial.print(kTimeout);
  Serial.println("us");
 */

  pinMode(PINRF, INPUT);
  rf.enableReceive(digitalPinToInterrupt(PINRF));

 
  // Serial2 – comunicação com outro ESP32 (definições em GambainoCommon.h)
  Serial2.setRxBufferSize(MAXPACKETSIZE*2+32); // aumenta buffer RX para evitar overflow
  Serial2.setTimeout(5);
  Serial2.begin(SERIAL2_SPEED, SERIAL_8N1, SERIAL2_RX, SERIAL2_TX);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onEspNowRecv);
    Serial.println("ESP-NOW receiver initialized");
  } else {
    Serial.println("ESP-NOW init failed");
  }
}

// ================== LOOP ======================
void loop() {
  static unsigned long lastSerial2Read = 0;


  verifyWiFiConnection();
  checkDebugMode();
  handle_IOTK();

  // reads Serial2 
  if (MILLISDIFF(lastSerial2Read, 10)) {
    lastSerial2Read = millis();
    if (char type = readSerial2(serial2Buffer, sizeof(serial2Buffer))) {
      Serial.printf("[Serial2] type='%c'(0x%02X) len=%d\n",
        (type >= 32 ? type : '?'), (uint8_t)type, (int)strlen(serial2Buffer));
      handlePacket(type, serial2Buffer);
    }
  }

  // reads espnow
  while (espnowQTail != espnowQHead) {
    EspNowFrame &frame = espnowQueue[espnowQTail];
    // Store sender MAC in a static so the non-capturing lambda can access it
    static uint8_t espnowCurrentSenderMac_[6];
    memcpy(espnowCurrentSenderMac_, frame.senderMac, 6);
    processEspNowData(frame.data, frame.len, [](char type, const char *payload) {
      handlePacketWithMac(type, payload, espnowCurrentSenderMac_);
    });
    espnowQTail = (espnowQTail + 1) % ESPNOW_QUEUE_SLOTS;
  }


  if (rf.available()) {
    // Obtém o valor recebido
    unsigned long value = rf.getReceivedValue();
    
    if (value == 0 || rf.getReceivedBitlength() != 24 ) {
      //Serial.println("Código desconhecido recebido");
    } else {
      Serial.println("=============================");
      Serial.print("Código recebido: ");
      Serial.println(value);
      Serial.print("Bits: ");
      Serial.println(rf.getReceivedBitlength());
      Serial.print("Protocolo: ");
      Serial.println(rf.getReceivedProtocol());
      Serial.print("Delay: ");
      Serial.println(rf.getReceivedDelay());
      Serial.println("=============================");
    }
    
    // Reset para receber o próximo sinal
    rf.resetAvailable();

    // Anti-bouncing: ignora o mesmo código repetido dentro de 1 segundo
    static unsigned long lastRFMillis = 0;
    static unsigned long lastRFValue  = 0;
    if (value != 0 && (value != lastRFValue || millis() - lastRFMillis >= 1000)) {
      lastRFValue  = value;
      lastRFMillis = millis();
      switch (value) {
        case 753828:
          sendSerial2(RCCOMMANDPACKET, "A");
          logRFCommand(value, "A");
          break;
        case 753832:
          sendSerial2(RCCOMMANDPACKET, "B");
          logRFCommand(value, "B");
          break;
        case 753825:
          sendSerial2(RCCOMMANDPACKET, "C");
          logRFCommand(value, "C");
          break;
        case 753826:
          sendSerial2(RCCOMMANDPACKET, "D");
          logRFCommand(value, "D");
          break;
        default:
          logRFCommand(value, "?");
          break;
      }
    }

  }



/*
  static unsigned long lastIrOk = 0;
  // Verifica receptor IR
  if (irrecv.decode(&results)) {
    uint32_t now = millis();
    lastIrOk = now;
    irrecv.resume();

    Serial.printf(D_STR_TIMESTAMP " : %06u.%03u\n", now / 1000, now % 1000);
    if (results.overflow)
      Serial.printf(D_WARN_BUFFERFULL "\n", kCaptureBufferSize);

    Serial.println(D_STR_LIBRARY "   : v" _IRREMOTEESP8266_VERSION_STR "\n");
    if (kTolerancePercentage != kTolerance)
      Serial.printf(D_STR_TOLERANCE " : %d%%\n", kTolerancePercentage);

    Serial.print(resultToHumanReadableBasic(&results));

    String description = IRAcUtils::resultAcToString(&results);
    if (description.length()) Serial.println(D_STR_MESGDESC ": " + description);
    yield();



    Serial.println(resultToSourceCode(&results));
    Serial.println();
    yield();


    // Mapeia comandos IR para Serial2
    switch (results.command) {
      case 0xDE:      // seta para cima controle creative
      case 0x17: // Verde controle antigo
        sendSerial2(RCCOMMANDPACKET, "W");
        break;

      case 0xB1:      // seta para baixo controle creative
      case 0x18:   // amarelo controle antigo
        sendSerial2(RCCOMMANDPACKET, "D");
        break;

      case 0x81:      // select controle creative
      case 0x16:    // vermelho controle antigo
        sendSerial2(RCCOMMANDPACKET, "S");
        break;
    }

    Serial.print("COMMAND: ");
    Serial.print(results.command, HEX);
    Serial.print("  ");
    Serial.println(results.command, BIN);
  }
  if (millis() - lastIrOk > 60000) {
    irrecv.disableIRIn();
    delay(5);
    irrecv.enableIRIn();
    lastIrOk = millis();
    //Serial.println("IR restarted (watchdog)");
  }  
    */

}
