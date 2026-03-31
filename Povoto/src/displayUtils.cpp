#include "displayUtils.h"
#include <LittleFS.h>
#include <TFT_eSPI.h>  
#include "User_Setup.h"
#include "PovotoCommon.h"
#include "Povoto_UI.h"
#include "PovotoData.h"
#include "PressureControl.h"
#include <WiFi.h>
#include <qrcode.h> 

volatile bool touchFlag = false;
static bool screenSaverActive = false;
unsigned long int lastClick = 0;

void IRAM_ATTR touchIRQ() {
  touchFlag = true;   // só sinaliza
}

byte DisplayMode = 0; // 0 = normal, 1 = QR Code
unsigned long displayModeChangeTime = 0;

uint16_t read16(fs::File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(fs::File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

void drawBmp(const char *filename, int16_t x, int16_t y) {

  if ((x >= tft.width()) || (y >= tft.height())) return;

  fs::File bmpFS;

  // Open requested file on SD card
  bmpFS = LittleFS.open(filename, "r");

  if (!bmpFS)
  {
    Serial.print("File not found");
    return;
  }

  uint32_t seekOffset;
  uint16_t w, h, row, col;
  uint8_t  r, g, b;

  uint32_t startTime = millis();

  if (read16(bmpFS) == 0x4D42)
  {
    read32(bmpFS);
    read32(bmpFS);
    seekOffset = read32(bmpFS);
    read32(bmpFS);
    w = read32(bmpFS);
    h = read32(bmpFS);

    if ((read16(bmpFS) == 1) && (read16(bmpFS) == 24) && (read32(bmpFS) == 0))
    {
      y += h - 1;

      bool oldSwapBytes = tft.getSwapBytes();
      tft.setSwapBytes(true);
      bmpFS.seek(seekOffset);

      uint16_t padding = (4 - ((w * 3) & 3)) & 3;
      uint8_t lineBuffer[w * 3 + padding];

      for (row = 0; row < h; row++) {
        
        bmpFS.read(lineBuffer, sizeof(lineBuffer));
        uint8_t*  bptr = lineBuffer;
        uint16_t* tptr = (uint16_t*)lineBuffer;
        // Convert 24 to 16-bit colours
        for (uint16_t col = 0; col < w; col++)
        {
          b = *bptr++;
          g = *bptr++;
          r = *bptr++;
          *tptr++ = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }

        // Push the pixel row to screen, pushImage will crop the line if needed
        // y is decremented as the BMP image is drawn bottom up
        tft.pushImage(x, y--, w, 1, (uint16_t*)lineBuffer);
      }
      tft.setSwapBytes(oldSwapBytes);
      Serial.print("Loaded in "); Serial.print(millis() - startTime);
      Serial.println(" ms");
    }
    else Serial.println("BMP format not recognized.");
  }
  bmpFS.close();
}


uint8_t rd(uint8_t reg){
  Wire.beginTransmission(FT_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(FT_ADDR, 1);
  return Wire.available()? Wire.read(): 0xFF;
}

void resetDisplayHardware() {
  digitalWrite(TFT_RST, LOW);
  delay(100);
  digitalWrite(TFT_RST, HIGH);
  delay(100);
  tft.init();
  forceScreenSaver(false);
  mainScreen();
}

void initTFT() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH); // Acende a luz de fundo

  pinMode(TOUCH_IRQ, INPUT);
  attachInterrupt(digitalPinToInterrupt(TOUCH_IRQ), touchIRQ, FALLING);

  Serial.println("Iniciando TFT ST7796 SPI...");
  tft.init();
  TFTWaintingWifiConnection();
}

void TFTWaintingWifiConnection() {
  static byte last_color = 0; 
  byte c =  (millis() / 500) % 3;
  if (c != last_color) {
    last_color = c;
    switch (c) {
      case 0:
        tft.fillScreen(TFT_RED);
        break;
      case 1:
        tft.fillScreen(TFT_GREEN);
        break;
      case 2:
        tft.fillScreen(TFT_BLUE);
        break;
    }
  }
}

bool ftRead(uint8_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(FT_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(FT_ADDR, (uint8_t)len) != (int)len) return false;
  for (size_t i=0;i<len;i++) buf[i] = Wire.read();
  return true;
}

void screenSaver(bool enable) {
  if (enable) {
    Serial.println(">>> SCREEN SAVER ON <<<");
    tft.fillScreen(TFT_BLACK);
    digitalWrite(TFT_BL, LOW);      // Desliga backlight (maior economia!)
    tft.writecommand(0x10);         // Sleep IN (reduz consumo do controlador)
  }
  else {
    Serial.println(">>> SCREEN SAVER OFF <<<");
    tft.writecommand(0x11);         // Sleep OUT (acorda display)
    delay(120);                      // Espera display acordar (datasheet recomenda 120ms)
    digitalWrite(TFT_BL, HIGH);     // Liga backlight
    mainScreen();
  }
}

bool isScreenSaverActive() {
  return screenSaverActive;
}

void forceScreenSaver(bool enable) {
  if (enable != screenSaverActive) {
    screenSaverActive = enable;
    screenSaver(enable);
    if (!enable)
      lastClick = millis(); // reseta timer de screensaver para evitar que ele volte a ligar logo após um toque
  }
}

void showConfigQRCode() {
  char url[100];
  IPAddress ip = WiFi.localIP();
  sprintf(url, "http://%d.%d.%d.%d/config", ip[0], ip[1], ip[2], ip[3]);
  
  Serial.printf(">>> MOSTRANDO QR CODE: %s <<<\n", url);
  
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  
  // Título
  tft.drawString("CONFIG QR CODE", TFT_WIDTH/2, 20, 4);
  
  // Gerar QR code
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, ECC_LOW, url);
  
  // Calcular tamanho do módulo para caber na tela
  int scale = 6; // Cada módulo do QR será 6x6 pixels
  int qrSize = qrcode.size * scale;
  int qrX = (TFT_WIDTH - qrSize) / 2;
  int qrY = 60;
  
  // Desenhar borda branca ao redor
  int border = scale * 2;
  tft.fillRect(qrX - border, qrY - border, qrSize + 2*border, qrSize + 2*border, TFT_WHITE);
  
  // Desenhar QR code
  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      uint16_t color = qrcode_getModule(&qrcode, x, y) ? TFT_BLACK : TFT_WHITE;
      tft.fillRect(qrX + x * scale, qrY + y * scale, scale, scale, color);
    }
  }
  
  // Mostra URL abaixo
  tft.setTextDatum(MC_DATUM);
  tft.drawString(url, TFT_WIDTH/2, qrY + qrSize + border + 20, 2);
  
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Toque para voltar", TFT_WIDTH/2, TFT_HEIGHT - 20, 2);

  DisplayMode = 1;
  displayModeChangeTime = millis();

}

void processTouch() {
  uint16_t x, y;
  uint16_t raw_x, raw_y;

  if (isVolumeDeterminationActive()) {
    lastClick = millis();
    if (screenSaverActive) {
      forceScreenSaver(false);
    }
  }
  
  if (!touchFlag) {
    if (!screenSaverActive && !isVolumeDeterminationActive() && (millis()-lastClick > (unsigned long)FMTData.FMTScreensaverTime * 1000UL)) {
      forceScreenSaver(true);
    }
  }
  else {
    lastClick = millis();
    touchFlag = false;
    Serial.println(">>> TOUCH IRQ DETECTED <<<");

    if (screenSaverActive) {
      forceScreenSaver(false);
      return;
    }

    if (DisplayMode==1 && millis() - displayModeChangeTime > 2000) {
      // Se estiver mostrando QR code, volta para tela principal
      mainScreen();
      DisplayMode = 0;
      return;
    }

    uint8_t status;
    if (ftRead(0x02, &status, 1)) {

      uint8_t touches = status & 0x0F;  // 0..5
      if (touches == 0 || touches > 5) return;

      // Cada ponto tem 6 bytes a partir de 0x03
      uint8_t buf[6 * 5];
      size_t need = 6 * touches;
      if (!ftRead(0x03, buf, need)) return;

      // Detecta toques simultâneos nos cantos
      bool topLeft = false;
      bool bottomRight = false;
      
      for (uint8_t i = 0; i < touches; i++) {
        uint8_t* p = &buf[i * 6];
        uint16_t y = ((p[0] & 0x0F) << 8) | p[1]; // XH(3:0) + XL
        uint16_t x = ((p[2] & 0x0F) << 8) | p[3]; // YH(3:0) + YL
        uint8_t  id = p[4] & 0x0F;               // ID do toque (0..4)
        uint8_t  area = p[5];                    // tamanho/força (se usado)   
        if (y >= TFT_WIDTH) y = TFT_WIDTH - 1;
        if (x >= TFT_HEIGHT) x = TFT_HEIGHT - 1;
        x = TFT_HEIGHT - x; // Inverte eixo X para coordenadas da tela
        y = TFT_WIDTH - y; // Inverte eixo Y para coordenadas da tela
Serial.printf("RAW: %02X %02X %02X %02X %02X %02X  |  ",
                p[0],p[1],p[2],p[3],p[4],p[5]);
                y=TFT_WIDTH - y; // Inverte eixo Y para coordenadas da tela
                tft.fillCircle(x, y, 5, TFT_RED);
        Serial.printf("Touch #%u: ID=%u X=%u Y=%u Area=%u\n", i, id, x, y, area);
        
        // Verifica toque no canto superior esquerdo
        if (x < 50 && y < 50) {
          topLeft = true;
        }
        
        // Verifica toque no canto inferior direito
        if (x > TFT_HEIGHT - 50 && y > TFT_WIDTH - 50) {
          bottomRight = true;
        }

        if (x <10 && y<10) {
          digitalWrite(TFT_RST,LOW);
          delay(100);
          digitalWrite(TFT_RST,HIGH);
          Serial.println(">>> RESET DISPLAY <<<");
          delay(100);
          tft.init();
        }

      }
      
      // Se ambos os cantos foram tocados simultaneamente, mostra QR code
      if (topLeft && bottomRight && touches >= 2) {
        showConfigQRCode();
        delay(100); // Debounce
      }
    }       
  }

  }
