#include "displayUtils.h"
#include <LittleFS.h>
#include <TFT_eSPI.h>  
#include "User_Setup.h"
#include "PovotoCommon.h"
#include "Povoto_UI.h"
#include "PovotoData.h"
#include "PressureControl.h"
#include "PovotoTasks.h"
#include "Swiss_911_Extra_Compressed_Regular10pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular12pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular16pt7b.h"
#include "Swiss_911_Extra_Compressed_Regular18pt7b.h"
#include <WiFi.h>
#include <qrcode.h> 

volatile bool touchFlag = false;
static bool screenSaverActive = false;
unsigned long int lastClick = 0;

// Forward declaration — definição completa mais abaixo (após showConfigQRCode)
static bool kbActive = false;

// Task UI state
static bool          taskUIActive         = false;
static byte          taskUIScreen         = 0; // 0=seleção, 1=tarefa ativa
static unsigned long taskUIScreenOpenTime = 0; // quando a tela de seleção foi aberta
static unsigned long lastTaskUITimeUpdate = 0; // throttle de atualização do countdown

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
    // Se o teclado estiver aberto, fecha como CANCEL
    if (kbActive) {
      kbActive = false;
      Serial.println(">>> KB: fechado por screensaver (CANCEL) <<<");
    }
    tft.fillScreen(TFT_BLACK);
    digitalWrite(TFT_BL, LOW);      // Desliga backlight (maior economia!)
    tft.writecommand(0x10);         // Sleep IN (reduz consumo do controlador)
  }
  else {
    Serial.println(">>> SCREEN SAVER OFF <<<");
    // Hard reset do hardware para garantir recuperação após longos períodos de sleep.
    // O Sleep OUT (0x11) sozinho não é suficientemente confiável após sleeps longos:
    // o controlador fica num estado inconsistente e o display fica em branco.
    digitalWrite(TFT_BL, LOW);      // Mantém backlight desligado durante reset
    digitalWrite(TFT_RST, LOW);
    delay(10);
    digitalWrite(TFT_RST, HIGH);
    delay(120);                      // Aguarda estabilização pós-reset (datasheet)
    tft.init();                      // Re-inicializa completamente o controlador
    digitalWrite(TFT_BL, HIGH);     // Liga backlight só após init completo
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

// ─────────────────────────────────────────────────────────────
// TECLADO NUMÉRICO GENÉRICO
// ─────────────────────────────────────────────────────────────
// Layout (landscape 480×320):
//   Input bar : y =   0 ..  49  (50 px)
//   Row 0 (7,8,9): y =  50 .. 104  (55 px)
//   Row 1 (4,5,6): y = 105 .. 159  (55 px)
//   Row 2 (1,2,3): y = 160 .. 214  (55 px)
//   Row 3 (.,0,DEL):y = 215 .. 269 (55 px)
//   OK / CANCEL  : y = 270 .. 319  (50 px)
//   Colunas: 3 × 160 px = 480 px

#define KB_INPUT_H   50
#define KB_ROW_H     55
#define KB_ROWS       4
#define KB_COL_W    160
#define KB_ROW0_Y    KB_INPUT_H                        // 50
#define KB_BOTTOM_Y  (KB_ROW0_Y + KB_ROWS * KB_ROW_H) // 270
#define KB_BOTTOM_H  (320 - KB_BOTTOM_Y)               // 50
#define KB_DEBOUNCE_MS  350

// kbActive declarado no topo do arquivo (linha 17)
static char  kbBuffer[12];
static int   kbLen          = 0;
static bool  kbHasDot       = false;
static int   kbDecimals     = 1;      // casas decimais permitidas
static float kbMinVal       = 0.0f;
static float kbMaxVal       = 99.9f;
static int   kbMaxIntDigits = 2;      // dígitos inteiros máximos (derivado do maxVal)
static char  kbTitle[32]    = "Value:";
static void (*kbCallback)(float) = nullptr;
static unsigned long kbLastTouchMs = 0;
static bool  kbAllowEmpty   = false;    // permite buffer vazio como resposta válida
static float kbEmptyValue   = 0.0f;     // valor retornado quando buffer vazio
static bool  kbPinTheme     = false;    // usa cores rosa no teclado de PIN

static const char* kbLabels[KB_ROWS][3] = {
  {"7", "8", "9"},
  {"4", "5", "6"},
  {"1", "2", "3"},
  {".",  "0", "DEL"}
};

// Retorna true se o conteúdo atual do buffer é um valor dentro do range
static bool kbValueIsValid() {
  if (kbLen == 0) return kbAllowEmpty;
  float val = atof(kbBuffer);
  return val >= kbMinVal && val <= kbMaxVal;
}

static void updateKbDisplay() {
  uint16_t bg = kbPinTheme ? tft.color565(80, 20, 50) : tft.color565(20, 20, 80);
  tft.fillRect(0, 0, 480, KB_INPUT_H, bg);
  tft.drawRect(0, 0, 480, KB_INPUT_H, TFT_CYAN);
  tft.setTextColor(TFT_WHITE, bg);
  tft.setFreeFont(NULL);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(kbTitle, 8, KB_INPUT_H / 2, 4);
  char show[16];
  snprintf(show, sizeof(show), "%s|", kbBuffer);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(show, 472, KB_INPUT_H / 2, 4);
  tft.setTextDatum(TL_DATUM);

  // Redesenha botão OK: verde = válido, cinza = fora do range
  bool valid = kbValueIsValid();
  uint16_t okBg  = valid ? tft.color565(0, 100, 0)  : tft.color565(50, 50, 50);
  uint16_t okBdr = valid ? tft.color565(0, 220, 0)  : tft.color565(100, 100, 100);
  tft.fillRect(241, KB_BOTTOM_Y + 1, 238, KB_BOTTOM_H - 2, okBg);
  tft.drawRect(240, KB_BOTTOM_Y, 240, KB_BOTTOM_H, okBdr);
  tft.setTextColor(valid ? TFT_WHITE : tft.color565(120, 120, 120), okBg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("OK", 360, KB_BOTTOM_Y + KB_BOTTOM_H / 2, 4);
  tft.setTextDatum(TL_DATUM);
}

static void drawKeyboard() {
  uint16_t screenBg = kbPinTheme ? tft.color565(50, 15, 30)    : tft.color565(20, 20, 50);
  uint16_t btnBg    = kbPinTheme ? tft.color565(100, 40, 70)   : tft.color565(50, 50, 100);
  uint16_t btnBdr   = kbPinTheme ? tft.color565(200, 100, 150) : tft.color565(120, 120, 200);
  uint16_t delBg    = kbPinTheme ? tft.color565(120, 20, 40)   : tft.color565(80, 40, 40);
  tft.fillScreen(screenBg);
  updateKbDisplay(); // desenha barra de input e botão OK com estado correto

  for (int r = 0; r < KB_ROWS; r++) {
    for (int c = 0; c < 3; c++) {
      int bx = c * KB_COL_W;
      int by = KB_ROW0_Y + r * KB_ROW_H;
      const char* lbl = kbLabels[r][c];
      bool isDel  = (strcmp(lbl, "DEL") == 0);
      bool isDot  = (strcmp(lbl, ".")   == 0);
      bool dotOff = isDot && (kbDecimals == 0);  // ponto desabilitado em modo inteiro
      uint16_t bg = isDel ? delBg : (dotOff ? screenBg : btnBg);
      tft.fillRect(bx + 1, by + 1, KB_COL_W - 2, KB_ROW_H - 2, bg);
      if (!dotOff)
        tft.drawRect(bx, by, KB_COL_W, KB_ROW_H, btnBdr);
      tft.setTextDatum(MC_DATUM);
      tft.setFreeFont(NULL);
      if (isDel) {
        // Desenha seta para esquerda ← em vez de texto DEL
        int cx = bx + KB_COL_W / 2;
        int cy = by + KB_ROW_H / 2;
        tft.fillRect(cx - 6, cy - 5, 24, 10, TFT_WHITE);                         // cabo
        tft.fillTriangle(cx - 22, cy, cx - 4, cy - 14, cx - 4, cy + 14, TFT_WHITE); // ponta
      } else if (isDot && !dotOff) {
        // Círculo branco sólido como ponto decimal
        tft.fillCircle(bx + KB_COL_W / 2, by + KB_ROW_H / 2, 3, TFT_WHITE);
      } else if (!dotOff) {
        tft.setTextColor(TFT_WHITE, bg);
        tft.drawString(lbl, bx + KB_COL_W / 2, by + KB_ROW_H / 2, 4);
      }
    }
  }

  // CANCEL (left 240px)
  tft.fillRect(1, KB_BOTTOM_Y + 1, 238, KB_BOTTOM_H - 2, tft.color565(100, 0, 0));
  tft.drawRect(0, KB_BOTTOM_Y, 240, KB_BOTTOM_H, tft.color565(220, 0, 0));
  tft.setTextColor(TFT_WHITE, tft.color565(100, 0, 0));
  tft.setTextDatum(MC_DATUM);
  tft.drawString("CANCEL", 120, KB_BOTTOM_Y + KB_BOTTOM_H / 2, 4);

  // OK já desenhado por updateKbDisplay()
  tft.setTextDatum(TL_DATUM);
}

bool isTempKeyboardActive() { return kbActive; }

// ─────────────────────────────────────────────────────────────
// CONTROLE DE DESBLOQUEIO POR PIN
// ─────────────────────────────────────────────────────────────
#define PIN_UNLOCK_DURATION_MS  (30UL * 60UL * 1000UL)  // 30 minutos

static unsigned long pinUnlockedAt  = 0;   // 0 = nunca desbloqueado
static bool          pinUnlocked    = false;

// Ponteiro para o teclado que será aberto após o PIN correto
static void (*pendingKeyboardOpen)() = nullptr;

static bool isPinUnlocked() {
  if (!pinUnlocked) return false;
  if (millis() - pinUnlockedAt > PIN_UNLOCK_DURATION_MS) {
    pinUnlocked = false;
    Serial.println(">>> PIN: desbloqueio expirado <<<");
    return false;
  }
  return true;
}

static void openPinKeyboard(void (*afterUnlock)());

// Tenta abrir um teclado: pede PIN primeiro se necessário
static void requestKeyboard(void (*opener)()) {
  if (isPinUnlocked()) {
    opener();
  } else {
    pendingKeyboardOpen = opener;
    openPinKeyboard(opener);
  }
}

// Abre o teclado numérico genérico.
// title    : texto exibido na barra de entrada (max 31 chars)
// initVal  : valor inicial pré-preenchido (usa NOTaTEMP para vazio)
// minVal   : valor mínimo aceito pelo OK
// maxVal   : valor máximo aceito pelo OK
// decimals : número máximo de casas decimais (0 = inteiro)
// callback : chamado com o valor aprovado (não chamado no CANCEL)
void openNumKeyboard(const char* title, float initVal, float minVal, float maxVal,
                     int decimals, bool allowEmpty, float emptyValue, bool pinTheme, void (*callback)(float)) {
  kbDecimals   = decimals;
  kbMinVal     = minVal;
  kbMaxVal     = maxVal;
  kbCallback   = callback;
  kbAllowEmpty = allowEmpty;
  kbEmptyValue = emptyValue;
  kbPinTheme   = pinTheme;
  kbLen        = 0;
  kbHasDot     = false;
  kbBuffer[0]  = '\0';
  strncpy(kbTitle, title, sizeof(kbTitle) - 1);
  kbTitle[sizeof(kbTitle) - 1] = '\0';

  // Calcula quantos dígitos inteiros tem o limite superior (para limitar entrada)
  {
    int n = (int)fabsf(maxVal);
    kbMaxIntDigits = 1;
    while (n >= 10) { n /= 10; kbMaxIntDigits++; }
  }

  // Se initVal coincide com emptyValue, começa com buffer vazio
  if (initVal != emptyValue) {
    char fmt[8];
    snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
    snprintf(kbBuffer, sizeof(kbBuffer), fmt, initVal);
    kbLen    = strlen(kbBuffer);
    kbHasDot = (decimals > 0);
  }
  kbActive  = true;
  drawKeyboard(); // pode levar centenas de ms; o debounce é iniciado APÓS desenhar
  touchFlag     = false;                  // descarta o toque que abriu o teclado
  kbLastTouchMs = millis();              // debounce começa depois da tela estar pronta
}

// Wrapper de conveniência para temperatura target
static void doOpenTempKeyboard() {
  openNumKeyboard("Temp Target:", SetPointData.setPointTemp,
                  0.0f, 24.0f, 1, true, NOTaTEMP, false,
                  [](float val) {
                    SetPointData.setPointTemp     = val;
                    SetPointData.setPointTempSetEpoch = 0;
                    writeSetPointDataToNIV();
                    Serial.printf(">>> KB OK: setPointTemp = %.2f <<<\n", val);
                  });
}

// Wrapper de conveniência para slow temperature target
static void doOpenSlowTempKeyboard() {
  openNumKeyboard("Slow Target:", SetPointData.setPointSlowTemp,
                  0.0f, 24.0f, 1, true, NOTaTEMP, false,
                  [](float val) {
                    SetPointData.setPointSlowTemp = val;
                    writeSetPointDataToNIV();
                    Serial.printf(">>> KB OK: setPointSlowTemp = %.2f <<<\n", val);
                  });
}

// Wrapper de conveniência para pressao target
static void doOpenPressureKeyboard() {
  openNumKeyboard("Pressure Target:", SetPointData.setPointPressure,
                  0.0f, 2.0f, 2, false, 0.0f, false,
                  [](float val) {
                    SetPointData.setPointPressure = val;
                    SetPointData.setPointPressureSetEpoch = 0;
                    writeSetPointDataToNIV();
                    Serial.printf(">>> KB OK: setPointPressure = %.2f <<<\n", val);
                  });
}

void openTempKeyboard()     { requestKeyboard(doOpenTempKeyboard);     }
void openSlowTempKeyboard() { requestKeyboard(doOpenSlowTempKeyboard); }
void openPressureKeyboard() { requestKeyboard(doOpenPressureKeyboard); }

// Abre o teclado de PIN. após PIN correto, chama afterUnlock().
static void openPinKeyboard(void (*afterUnlock)()) {
  // Usa openNumKeyboard com callback especial
  // O PIN é inteiro, 4 dígitos, range 0-9999
  openNumKeyboard("PIN:", NOTaTEMP, 0.0f, 9999.0f, 0, false, NOTaTEMP, true,
                  [](float val) {
                    int entered = (int)val;
                    if (entered == FMTData.FMTKeypadPin) {
                      pinUnlocked   = true;
                      pinUnlockedAt = millis();
                      Serial.println(">>> PIN: desbloqueado <<<");
                      // Abre o teclado pendente
                      if (pendingKeyboardOpen) {
                        void (*toOpen)() = pendingKeyboardOpen;
                        pendingKeyboardOpen = nullptr;
                        toOpen();
                      }
                    } else {
                      Serial.println(">>> PIN: incorreto <<<");
                      // Mostra mensagem de erro brevemente
                      tft.fillScreen(tft.color565(60, 0, 0));
                      tft.setTextColor(TFT_WHITE, tft.color565(60, 0, 0));
                      tft.setTextDatum(MC_DATUM);
                      tft.setFreeFont(NULL);
                      tft.drawString("PIN incorreto!", 240, 160, 4);
                      delay(1500);
                      mainScreen();
                    }
                  });
}

// Processa um toque no modo teclado. Retorna true quando o teclado fecha (OK ou CANCEL).
static bool handleKeyboardTouch(uint16_t kx, uint16_t ky) {
  // Debounce: ignora toques muito rápidos
  if (millis() - kbLastTouchMs < KB_DEBOUNCE_MS) return false;
  kbLastTouchMs = millis();

  // --- Linha OK / CANCEL ---
  if (ky >= (uint16_t)KB_BOTTOM_Y) {
    kbActive = false;
    if (kx >= 240) {
      // OK — só executa se valor for válido
      if (kbValueIsValid()) {
        if (kbCallback) {
          if (kbLen == 0 && kbAllowEmpty)
            kbCallback(kbEmptyValue);
          else
            kbCallback(atof(kbBuffer));
        }
      } else {
        Serial.println(">>> KB OK ignorado: valor inválido ou fora do range <<<");
      }
    } else {
      Serial.println(">>> KB CANCEL <<<");
    }
    mainScreen();
    return true;
  }

  // --- Linhas de dígitos ---
  if (ky >= (uint16_t)KB_ROW0_Y && ky < (uint16_t)KB_BOTTOM_Y) {
    int row = (ky - KB_ROW0_Y) / KB_ROW_H;
    int col = kx / KB_COL_W;
    if (row < 0 || row >= KB_ROWS || col < 0 || col > 2) return false;

    const char* lbl = kbLabels[row][col];

    if (strcmp(lbl, "DEL") == 0) {
      if (kbLen > 0) {
        if (kbBuffer[kbLen - 1] == '.') kbHasDot = false;
        kbBuffer[--kbLen] = '\0';
      }
    } else if (strcmp(lbl, ".") == 0) {
      if (kbDecimals > 0 && !kbHasDot && kbLen < (int)(sizeof(kbBuffer) - 1)) {
        if (kbLen == 0) kbBuffer[kbLen++] = '0';
        kbBuffer[kbLen++] = '.';
        kbBuffer[kbLen]   = '\0';
        kbHasDot = true;
      }
    } else {
      // Verifica limite de casas decimais e dígitos inteiros
      bool canAdd = true;
      if (kbHasDot) {
        const char* dot = strchr(kbBuffer, '.');
        if (dot && (int)(kbBuffer + kbLen - dot - 1) >= kbDecimals) canAdd = false;
      } else {
        // Conta dígitos inteiros já digitados
        if (kbLen >= kbMaxIntDigits) canAdd = false;
      }
      if (canAdd && kbLen < (int)(sizeof(kbBuffer) - 1)) {
        kbBuffer[kbLen++] = lbl[0];
        kbBuffer[kbLen]   = '\0';
      }
    }
    updateKbDisplay();
  }
  return false;
}

// ─── Task UI ──────────────────────────────────────────────────────────────────

bool isTaskUIActive() { return taskUIActive; }

static void drawTaskButton(int x, int y, int w, int h, uint16_t color, const char* label) {
  const int r = 10;
  tft.fillRoundRect(x, y, w, h, r, color);
  tft.drawRoundRect(x, y, w, h, r, tft.color565(255, 255, 255)); // white border
  tft.setTextColor(TFT_WHITE, color);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&Swiss_911_Extra_Compressed_Regular16pt7b);
  tft.drawString(label, x + w / 2, y + h / 2);
}

void showTaskSelectionScreen() {
  taskUIActive      = true;
  taskUIScreen      = 0;
  taskUIScreenOpenTime = millis();
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&Swiss_911_Extra_Compressed_Regular18pt7b);
  tft.drawString("SELECT TASK TO START", 240, 22);

  // Row 1: Dump — full width
  drawTaskButton(5,   45,  470, 58, tft.color565(180,  40,  40),  "Dump");
  // Row 2: Gas | Liquid
  drawTaskButton(5,   115, 230, 58, tft.color565( 40,  80, 180),  "Gas");
  drawTaskButton(245, 115, 230, 58, tft.color565( 40, 160, 180),  "Liquid");
  // Row 3: Dry Hopping | Dynamic Hopping
  drawTaskButton(5,   185, 230, 58, tft.color565( 40, 150,  60),  "Dry Hopping");
  drawTaskButton(245, 185, 230, 58, tft.color565(140,  60, 160),  "Dynamic Hopping");
  // Cancel
  drawTaskButton(130, 258, 220, 52, tft.color565( 80,  80,  80),  "Cancel");
}

void showActiveTaskScreen() {
  taskUIActive = true;
  taskUIScreen = 1;
  tft.fillScreen(TFT_BLACK);

  static const char* const taskNames[] = {"", "Dump", "Gas", "Liquid", "Dry Hopping", "Dynamic Hopping"};
  const char* name = (taskWindowType >= 1 && taskWindowType <= 5) ? taskNames[taskWindowType] : "?";

  tft.setTextColor(tft.color565(180, 180, 180), TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&Swiss_911_Extra_Compressed_Regular18pt7b);
  char title[48];
  snprintf(title, sizeof(title), "on going task: %s", name);
  tft.drawString(title, 240, 22);

  unsigned long remaining = (taskWindowEndTime > millis()) ? (taskWindowEndTime - millis()) / 1000UL : 0;
  char timeStr[40];
  snprintf(timeStr, sizeof(timeStr), "%lu min %02lu s remaining", remaining / 60, remaining % 60);
  tft.fillRect(0, 50, 480, 38, TFT_BLACK);
  tft.setTextColor(tft.color565(130, 130, 130), TFT_BLACK);
  tft.setFreeFont(&Swiss_911_Extra_Compressed_Regular10pt7b);
  tft.drawString(timeStr, 240, 68);
  lastTaskUITimeUpdate = millis();

  drawTaskButton(30,  115, 420, 85, tft.color565( 40, 140,  60),  "Tarefa Concluida");
  drawTaskButton(130, 225, 220, 55, tft.color565( 80,  80,  80),  "Cancel");
}

static void handleTaskTouch(uint16_t x, uint16_t y) {
  if (taskUIScreen == 0) {
    // Seleção de tarefa
    if (y >= 45 && y <= 103 && x >= 5 && x <= 475) {
      startDumpTask(); showActiveTaskScreen(); return;
    }
    if (y >= 115 && y <= 173 && x >= 5   && x <= 235) {
      startGasTask(); showActiveTaskScreen(); return;
    }
    if (y >= 115 && y <= 173 && x >= 245 && x <= 475) {
      startLiquidTask(); showActiveTaskScreen(); return;
    }
    if (y >= 185 && y <= 243 && x >= 5   && x <= 235) {
      startDryHoppingTask(); showActiveTaskScreen(); return;
    }
    if (y >= 185 && y <= 243 && x >= 245 && x <= 475) {
      startDynamicHoppingTask(); showActiveTaskScreen(); return;
    }
    if (y >= 258 && y <= 310 && x >= 130 && x <= 350) {
      taskUIActive = false; mainScreen(); return;
    }
  } else {
    // Tarefa ativa: concluir ou cancelar
    if (y >= 115 && y <= 200 && x >= 30 && x <= 450) {
      switch (taskWindowType) {
        case 1: endDumpTask();          break;
        case 2: endGasTask();           break;
        case 3: endLiquidTask();        break;
        case 4: endDryHoppingTask();    break;
        case 5: endDynamicHoppingTask(); break;
        default: break;
      }
      taskUIActive = false; mainScreen(); return;
    }
    if (y >= 225 && y <= 280 && x >= 130 && x <= 350) {
      taskWindowType    = 0;
      taskWindowEndTime = 0;
      taskUIActive = false; mainScreen(); return;
    }
  }
}

void updateTaskUIIfActive() {
  if (!taskUIActive || taskUIScreen != 1) return;
  // Se a tarefa expirou (checkTaskExpiration já zerou o tipo), fecha a UI
  if (taskWindowType == 0) {
    taskUIActive = false;
    mainScreen();
    return;
  }
  if (millis() - lastTaskUITimeUpdate < 1000) return;
  lastTaskUITimeUpdate = millis();

  unsigned long remaining = (taskWindowEndTime > millis()) ? (taskWindowEndTime - millis()) / 1000UL : 0;
  char timeStr[40];
  snprintf(timeStr, sizeof(timeStr), "%lu min %02lu s remaining", remaining / 60, remaining % 60);
  tft.fillRect(0, 50, 480, 38, TFT_BLACK);
  tft.setTextColor(tft.color565(130, 130, 130), TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&Swiss_911_Extra_Compressed_Regular10pt7b);
  tft.drawString(timeStr, 240, 68);
}

// ─────────────────────────────────────────────────────────────────────────────

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
    unsigned long ssTimeout = (unsigned long)FMTData.FMTScreensaverTime * 1000UL;
    if (!screenSaverActive && !isVolumeDeterminationActive() && !taskUIActive && (millis()-lastClick > ssTimeout)) {
      forceScreenSaver(true);
    }
    // Sai da tela de seleção de tarefa se ficar idle por mais que o timeout do screensaver
    if (taskUIActive && taskUIScreen == 0 && (millis() - taskUIScreenOpenTime > ssTimeout)) {
      taskUIActive = false;
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
        Serial.printf("Touch #%u: ID=%u X=%u Y=%u Area=%u\n", i, id, x, y, area);

        // === TECLADO NUMÉRICO: consome o toque e ignora lógica de cantos ===
        if (kbActive) {
          if (i == 0) handleKeyboardTouch(x, y);
          continue;
        }

        // === TASK UI: consome o toque quando a UI de tarefas está ativa ===
        if (taskUIActive) {
          if (i == 0) handleTaskTouch(x, y);
          continue;
        }

        // Zona de toque: lado esquerdo da tela → abre UI de tarefas
        if (touches == 1 && x < 80) {
          Serial.println(">>> TOUCH: abrindo UI de tarefas <<<");
          if (taskWindowType != 0) {
            showActiveTaskScreen();
          } else {
            showTaskSelectionScreen();
          }
          return;
        }

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

        // Zona de toque: label "Target" da temperatura (~screen x=270..430, y=88..122)
        if (touches == 1 && x >= 260 && x <= 430 && y >= 85 && y <= 125) {
          Serial.println(">>> TOUCH: abrindo teclado de temperatura target <<<");
          openTempKeyboard();
          return;
        }

        // Zona de toque: label "Slow target" da temperatura (~screen x=270..430, y=126..165)
        if (touches == 1 && x >= 260 && x <= 430 && y >= 126 && y <= 165) {
          Serial.println(">>> TOUCH: abrindo teclado de slow temperature target <<<");
          openSlowTempKeyboard();
          return;
        }

        // Zona de toque: label "Target" da pressão (~screen x=270..430, y=230..262)
        if (touches == 1 && x >= 260 && x <= 430 && y >= 230 && y <= 262) {
          Serial.println(">>> TOUCH: abrindo teclado de pressao target <<<");
          openPressureKeyboard();
          return;
        }

      }
      
      // Se ambos os cantos foram tocados simultaneamente, mostra QR code
      if (!kbActive && topLeft && bottomRight && touches >= 2) {
        showConfigQRCode();
        delay(100); // Debounce
      }
    }       
  }

  }
