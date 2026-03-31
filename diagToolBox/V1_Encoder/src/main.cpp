#include <IOTK.h>
#include <IOTK_ESP32.h>
#include <IOTK_NTP.h>
//#include <IOTK_WiFi.h>
#include <Adafruit_INA219.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <IOTK_Dallas.h>
#include <OneWire.h>



Adafruit_INA219 ina219_1(0x40);
Adafruit_INA219 ina219_2(0x41);


#define SCREEN_WIDTH 128 // OLED display width,  in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define PINOLEDA 5
#define PINOLEDB 4

#define PINLEDG 18
#define PINLEDR 22
#define PINBTN 21
#define PINENCODERA 26
#define PINENCODERB 27

#define PINDALLAS 14

Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

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

volatile byte encoderB = 0;
volatile int cont;
volatile unsigned long int lastISRA = 0;
volatile unsigned long int lastISRB = 0;
volatile byte lastEncA = 0;
volatile byte lastEncB = 0;


int lastCont = 255;
byte btn,lastBtn;
byte opcao,funcao,nivel = 0;
bool onClick = false;
byte NDallas=0;
long int pulses,holdPulses=0;

uint8_t addr[8];
  

void IRAM_ATTR encoderProc() {
  if (1 /*MILLISDIFF(lastISRA,50)*/) {
    lastISRA = millis();
    byte encA = digitalRead(PINENCODERA);
    byte encB = digitalRead(PINENCODERB);

    if (encA !=  lastEncA) {
      lastEncA = encA;
      if (encA)
        if (lastEncB) cont--; else cont++;
    }

    if (encB != lastEncB) 
      lastEncB = encB;
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
  oled.println();
  oled.print("Found: "); oled.println(n);
  oled.display();
}

void doScanDallas() {
  DallasScanResult = "";
  dt.requestTemperatures();
  NDallas = 0;
  scanDallasOLedMsg(0);
  digitalWrite(PINLEDG,LOW);
  for (byte i = 0; i<4;i++) {
    digitalWrite(PINLEDR,!(i&1));
    delay(300);
  }

  while (ds.search(addr)) {
    digitalWrite(PINLEDG,HIGH);
    scanDallasOLedMsg(NDallas+1);
    dallasTemp[NDallas] = dt.getTempC(addr);
    delay(200);
    digitalWrite(PINLEDG,LOW);
    DallasScanResult += String(NDallas+1) + " --> " + checkupDallas(&ds,addr,dallasFamily[NDallas],&(dallasVeredict[NDallas])) + "<BR>";
    NDallas++;
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

void setup()
{
  Led_Builtin = 0;
  //ESP_AppName="Gambaino FMT Diagnostic Tool";  
  Serial.begin(115200);
  delay(30);
  Serial.println();
  Serial.println("Starting Gambaino Diagnostic Tool");
  
  Wire.begin(PINOLEDA,PINOLEDB);

  setStatusSource(getStatus);
  ESPSetup(ssid,password,NULL);
  
  ina219_1.begin();
  ina219_1.setCalibration_16V_40mA();
  ina219_2.begin();
  ina219_2.setCalibration_16V_40mA();

  pinMode(Led_Builtin,OUTPUT);
  pinMode(PINLEDR, OUTPUT);
  pinMode(PINLEDG, OUTPUT);
  pinMode(PINBTN, INPUT_PULLUP);
  pinMode(PINENCODERA, INPUT_PULLUP);
  pinMode(PINENCODERB, INPUT_PULLUP);

  // initialize OLED display with address 0x3C for 128x64
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true);
  }
  Serial.println("SSD1306 allocation success");  

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



void loop() {
  handle_IOTK();
  encoderProc();

  if (millis()<1000) {
    oled.clearDisplay();
    oled.setTextColor(WHITE);     // text color
    oled.setCursor(5, 10);        // position to display
    oled.setTextSize(2);
    oled.print("Iniciando");
    oled.display();
    cont = 0;
    lastCont = 0; 
    return;
  }


  btn  = !digitalRead(PINBTN);

  if (btn) {
    digitalWrite(PINLEDG,HIGH);
    digitalWrite(PINLEDR,HIGH);
    if (!onClick) {
      onClick = true;
      if (nivel == 0) {
        nivel = 1;
        funcao = opcao;
        opcao = 1;
        pulses = 0;
        holdPulses = 0;
        if (funcao==1) 
          doScanDallas();
      }
      else if (opcao==0) {
        nivel = 0;
        opcao = funcao;
      }
      else if (funcao==2 && opcao==1) {
        holdPulses = pulses;
        pulses = 0;
      }
    }
  }
  else {
    onClick = false;
    if (funcao != 1 || nivel == 0 || opcao == 0 || NDallas == 0) {
      digitalWrite(PINLEDG,LOW);
      digitalWrite(PINLEDR,LOW);
    }
  }
  
  if (cont < lastCont - 1 || cont > lastCont + 1) {
    byte cycle;
  
    if (nivel==0) 
      cycle = 3;
    else
      if (funcao==0)
        cycle = 2;
      else if (funcao==1)
        cycle = 1+NDallas;
      else if (funcao==2)
        cycle = 2;

    if (cont > lastCont) 
      opcao = (opcao + 1) % cycle;
    else
      if (opcao==0)
        opcao = cycle-1;
      else
        opcao -= 1;

    lastCont = cont;
  }

  if (nivel == 0) {
    oled.clearDisplay();
    oled.setTextColor(WHITE);     // text color
    oled.setCursor(5, 10);        // position to display
    oled.setTextSize(3);
    switch (opcao) {
      case 0: 
        oled.println("Ready");
        oled.setTextSize(2);
        oled.println();
        oled.setTextSize(1);
        if (wifiIsConnected())
          oled.print(WiFi.localIP());
        else
          oled.print("conectando...");
        break;
      case 1:
        oled.println("Scan");
        oled.println("Dallas");
        break;
      case 2:
        oled.setTextSize(2);
        oled.println("Hydrometer");
        oled.println("Test");
        break;
    }
    oled.display();
  }
  else {
    oled.clearDisplay();
    oled.setTextColor(WHITE);     // text color
    oled.setCursor(0, 0);        // position to display
    oled.setTextSize(2);
    if (opcao==0) {
      oled.setTextSize(3);
      oled.print("Voltar");
    }
    else {
      if (funcao==0) {
        oled.setTextSize(1);
        oled.print("IP: "); oled.println(WiFi.localIP());
        oled.print("Batteria (v): "); oled.println(analogRead(34));
      }
      else if (funcao==1) {
        if (NDallas==0) {
          oled.setTextSize(2);          
          oled.println("No Dallas");
          oled.println("scanned");
          oled.display();
          for (byte i=0; i<8; i++) {
            digitalWrite(PINLEDR,i&1);
            digitalWrite(PINLEDG,!(i&1));
            delay(400);
          }
          nivel = 0;
          opcao = 1;
        }
        else {
          oled.setTextSize(1);
          oled.print("Dallas "); 
          oled.print(opcao);
          oled.print(" / ");
          oled.print(NDallas);
          oled.print(" CRC ");
          oled.print("0x"); oled.println(addr[7],HEX);
          oled.println();
          oled.println(dallasFamily[opcao-1]); 
          oled.println();          
          oled.print("temp = "); oled.println(dallasTemp[opcao-1]);
          oled.println();          
          oled.setTextSize(2);
          switch (dallasVeredict[opcao-1]) {
            case 0:
              oled.println("     BAD");
              digitalWrite(PINLEDG,LOW);
              digitalWrite(PINLEDR,HIGH);            
              break;
            case 1:
              oled.println("     FAIR");
              digitalWrite(PINLEDG,HIGH);
              digitalWrite(PINLEDR,HIGH);            
              break;
            case 2:
              oled.println("     GOOD");
              digitalWrite(PINLEDG,HIGH);
              digitalWrite(PINLEDR,LOW);            
              break;
          }
        }
      }
      else if (funcao==2) {
        oled.setTextSize(1);
        oled.print("Count: ");
        oled.setTextSize(2);
        oled.println(pulses);
        oled.setTextSize(1);
        oled.println();
        oled.setTextSize(2);
        if (holdPulses!=0) {
          oled.setTextSize(1);
          oled.print(" Held: ");
          oled.setTextSize(2);
          oled.print(holdPulses);
        }
        oled.println();
        pulses+=129;
        oled.setTextSize(1);
        oled.println();
        oled.println("press to hold & reset");
      }

    }

    oled.display();
  }

  

}
