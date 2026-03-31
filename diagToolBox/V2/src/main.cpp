#include <IOTK.h>
#include <IOTK_ESP32.h>
#include <IOTK_NTP.h>
//#include <IOTK_WiFi.h>
#include <Adafruit_SH1106.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <IOTK_Dallas.h>
#include <OneWire.h>
#include <Adafruit_INA219.h>



Adafruit_INA219 ina219(0x40);


#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define PINSDA 5
#define PINSCL 4

#define PINBTNL 14
#define PINBTNR 27


#define PINDALLAS 16

Adafruit_SH1106 oled(-1);

OneWire ds(PINDALLAS);
DallasTemperature dt(&ds);


const char* ssid     = "goiaba";
const char* password = "heptA2019";

float current1=0;
float current2=0;

#define MAXDALLAS 40
String DallasScanResult;
float dallasTemp[MAXDALLAS];
char dallasFamily[MAXDALLAS][40];
byte dallasVeredict[MAXDALLAS];


int lastCont = 255;
byte btnL, btnR;
byte opcao;
byte NDallas=0;
long int pulses,holdPulses=0;

uint8_t addr[8];

void displayBtnL(String txt) {
  oled.setTextSize(1);
  int16_t x, y;
  uint16_t w, h;
  oled.getTextBounds(txt, 0, 0, &x, &y, &w, &h);
  int16_t posX = 0;
  int16_t posY = SCREEN_HEIGHT - h;
  oled.setCursor(posX,posY);
  oled.print(txt);
}

void displayBtnR(String txt) {
  oled.setTextSize(1);
  int16_t x, y;
  uint16_t w, h;
  oled.getTextBounds(txt, 0, 0, &x, &y, &w, &h);
  int16_t posX = SCREEN_WIDTH - w;
  int16_t posY = SCREEN_HEIGHT - h;
  oled.setCursor(posX,posY);
  oled.print(txt);
}

void processBtn() {
  static byte lastBtnL, lastBtnR;

  byte btnLSel = !digitalRead(PINBTNL);
  byte btnRSel = !digitalRead(PINBTNR);

  if (!lastBtnL) {
    if (btnLSel) {
      btnL = 1;
      lastBtnL = 1;
    }
    else {
      btnL = 0;
      lastBtnL = 0;
    }
  }
  else {
    btnL = 0;
    if (!btnLSel)
      lastBtnL = 0;
  }


  if (!lastBtnR) {
    if (btnRSel) {
      btnR = 1;
      lastBtnR = 1;
    }
    else {
      btnR = 0;
      lastBtnR = 0;
    }
  }
  else {
    btnR = 0;
    if (!btnRSel)
      lastBtnR = 0;
  }
}

String getStatus() {
  String st;

  st = "<BR><BR>" + DallasScanResult + 
       "<BR><BR>Hydrometer counter: " + String(pulses);
  if (holdPulses) st += "    (held: " + String(holdPulses)+")";
  
  st += "<BR><BR>";
  for (byte ad=1; ad<=127; ad++) {
    Wire.beginTransmission(ad);
    byte error = Wire.endTransmission();
    if (error == 0) {
      char buf[10];
      sprintf(buf,"0x%02X",ad);
      st+= "I2C device at " + String(buf);
      switch (ad) {
        case 0x3f: st += " (oLed)<BR>";
        default: st += "<BR>";
      }
    }
    else if (error == 4)
      st += "Unknown error at address " + String(ad) + "<BR>";
  }

  st += "<BR>";

  return st;
}

void scanDallasOLedMsg(byte n) {
  oled.clearDisplay();
  oled.setCursor(0,0);
  oled.setTextColor(WHITE);
  oled.setTextSize(2);
  oled.println("Scanning");
  oled.display();
}

void doScanDallas() {
  DallasScanResult = "";
  scanDallasOLedMsg(0);
  dt.requestTemperatures();
  delay(1000);
  NDallas = 0;
  while (ds.search(addr)) {
    scanDallasOLedMsg(NDallas+1);
    dallasTemp[NDallas] = dt.getTempC(addr);
    delay(200);
    DallasScanResult += String(NDallas+1) + " --> " + checkupDallas(&ds,addr,dallasFamily[NDallas],&(dallasVeredict[NDallas])) + "<BR>";
    NDallas++;
  }
}

void scanDallasAndShow() {
  doScanDallas();
   if (NDallas==0) {
    oled.clearDisplay();
    oled.setTextSize(2);         
    oled.setCursor(0, 0); 
    oled.println("No Dallas");
    oled.println(" found");
    displayBtnR("<< Voltar");
    oled.display();
    do {
      processBtn();
      delay(200);
    } while (!btnR);
  }
  else {
    byte toShow = 1;
    do {
      oled.clearDisplay();
      oled.setCursor(0,0);
      oled.setTextSize(1);
      oled.print("Dallas "); 
      oled.print(toShow);
      oled.print("/");
      oled.print(NDallas);
      oled.print(" CRC ");
      oled.print("0x"); oled.println(addr[7],HEX);
      oled.println();
      oled.println(dallasFamily[toShow-1]); 
      oled.println();          
      oled.print(dallasTemp[toShow-1]); oled.print(" oC");
      oled.println();          
      oled.setTextSize(2);
      oled.setCursor(SCREEN_WIDTH/2,SCREEN_HEIGHT/2+2);
      if (dallasTemp[toShow-1]>-127 && dallasTemp[toShow-1]<85) 
        switch (dallasVeredict[toShow-1]) {
          case 0:
            oled.println("FAKE");
            break;
          case 1:
            oled.println("FAIR");
            break;
          case 2:
            oled.println("ORIG");
            break;
        }
      else
        oled.println("FAULT");

      displayBtnR("<< Voltar");
      if (NDallas>1) 
        displayBtnL("Prox");

      oled.display();

      do {
        processBtn();
        delay(200);
      } while (!btnL && !btnR);

      if (btnL) {
         toShow++;
         if (toShow>NDallas) toShow = 1;
      }
    } while (!btnR);
  }
}

void handleScanI2c() {
  String scanResult = "<html><body>I2C devices:<br><br>";

  for (byte i = 1; i < 127; i++) {
    Wire.beginTransmission(i);
    byte error = Wire.endTransmission();
    if (error == 0) {
      Wire.requestFrom(i, 1); // Ler um byte do dispositivo I2C
      if (Wire.available()) {
        byte valor = Wire.read();
        scanResult += "address 0x";
        scanResult += String(i, HEX);
        scanResult += " - Value: 0x";
        scanResult += String(valor, HEX);
        scanResult += " <form method='get' action='/seti2c'><input type='hidden' name='addr' value='";
        scanResult += String(i, HEX);
        scanResult += "'><input type='text' name='value' placeholder='New value'>&nbsp;<input type='submit' value='Enviar'></form><br>";
      }
    }
  }

  scanResult += "</body></html>";

  server.send(200, "text/html", scanResult);
}

void handleSetI2c() {
  String enderecoParam = server.arg("addr");
  String novoValorParam = server.arg("value");

  // Converter o novo valor de string hexadecimal para byte
  byte novoValor = (byte)strtol(novoValorParam.c_str(), NULL, 16);

  // Converter o endereço de string hexadecimal para byte
  byte dispositivoI2C = (byte)strtol(enderecoParam.c_str(), NULL, 16);

  // Enviar o novo valor para o dispositivo I2C
  Wire.beginTransmission(dispositivoI2C);
  Wire.write(novoValor);
  Wire.endTransmission();

  // Redirecionar de volta para a página de varredura após enviar o novo valor
  server.sendHeader("Location", "/scani2c");
  server.send(302, "text/plain", "");
}

void setup() {
  Led_Builtin = 0;
  //ESP_AppName="Gambaino FMT Diagnostic Tool";  
  Serial.begin(115200);
  delay(30);
  Serial.println();
  Serial.println("Starting Gambaino Diagnostic Tool");
  
  Wire.begin(PINSDA,PINSCL);

  pinMode(PINBTNL,INPUT_PULLUP);
  pinMode(PINBTNR,INPUT_PULLUP);

  setStatusSource(getStatus);
  ESPSetup(ssid,password,NULL);
  
  ina219.begin();

  // initialize OLED display with address 0x3C for 128x64
  oled.begin(SH1106_SWITCHCAPVCC, 0x3C);


  oled.clearDisplay(); // clear display

  oled.setTextSize(1);          // text size
  oled.setTextColor(WHITE);     // text color
  oled.setCursor(0, 10);        // position to display
  oled.println("TRICORDER"); // text to display
  oled.display();               // show on OLED

  dt.setWaitForConversion(false);
  dt.setResolution(12);
  dt.begin();

  // Definir rota para o comando de varredura I2C
  server.on("/scani2c", HTTP_GET, handleScanI2c);

  // Definir rota para definir um novo valor no dispositivo I2C
  server.on("/seti2c", HTTP_GET, handleSetI2c);
  delay(200);
}



void displayOpcao(String opcao,String opcao2) {
  oled.clearDisplay();
  oled.setTextColor(WHITE);     // text color
  oled.setTextSize(2);

  int16_t x, y;
  uint16_t w, h,h2;
  oled.getTextBounds(opcao, 0, 0, &x, &y, &w, &h);
  int16_t posX = (SCREEN_WIDTH - w)/2;
  
  h2 = h;
  oled.setCursor(posX,0);
  oled.print(opcao);  

  oled.getTextBounds(opcao2, 0, 0, &x, &y, &w, &h);
  posX = (SCREEN_WIDTH - w)/2;
  
  oled.setCursor(posX,h2+2);
  oled.print(opcao2);


  oled.setTextSize(1);
  displayBtnL("Exec");
  displayBtnR("Prox >>");

  oled.display();
}

void displayIP() {
  oled.clearDisplay();
  oled.setTextColor(WHITE);     // text color
  oled.setCursor(5, 10);        // position to display
  oled.setTextSize(1);
  if (wifiIsConnected()) {
    oled.print("IP: ");
    oled.println(WiFi.localIP());
  }
  else
    oled.print("conectando...");
  oled.display();
}

void loop() {
  handle_IOTK();

  if (millis()<1000) {
    oled.clearDisplay();
    oled.setTextColor(WHITE);     // text color
    oled.setCursor(5, 10);        // position to display
    oled.setTextSize(2);
    oled.print("Iniciando");
    oled.display();
    return;
  }

  processBtn();

  if (btnR) {
    opcao++;
    if (opcao>3) opcao = 0;
  }


  switch (opcao) {
      case 0:
        if (btnL)
          scanDallasAndShow();
        else 
          displayOpcao("Dallas","scan");
        break;

      case 1:
        if (btnL)
          scanDallasAndShow();
        else 
          displayOpcao("Dallas bus","Test");
        break;

      case 2:
        if (btnL)
          doScanDallas();
        else 
          displayOpcao("Hydrometer","Test");
        break;

      case 3: 
        displayIP();
        break;
  }
}



// sensor de linha de termometro
// entrar sem wifi e conectar sob demanda, na que tiver na hora.
