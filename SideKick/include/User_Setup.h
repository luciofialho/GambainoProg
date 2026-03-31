#define ST7796_DRIVER

// Configuração para corrigir cores invertidas
#define TFT_INVERSION_OFF
// ou tente: #define TFT_INVERSION_OFF

#define USE_TOUCH

#define TFT_CS   5
#define TFT_DC   4    // Data/Command do display
#define TFT_RST  2    // Reset do display

#define TFT_SCLK 18
#define TFT_MOSI 23
#define TFT_MISO 19   // Adicionando MISO para SPI

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT7
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY  40000000
#define SPI_READ_FREQUENCY  16000000
#define SPI_TOUCH_FREQUENCY 2500000

#define TFT_WIDTH 320
#define TFT_HEIGHT 480


// Touch (XPT2046) - Configuração padrão
#define TOUCH_CS 21
#define TOUCH_CLK 18
#define TOUCH_DIN 23
#define TOUCH_DO 19

#define TOUCH_X_OFFSET 0
#define TOUCH_Y_OFFSET 0
#define TOUCH_X_SIZE 480
#define TOUCH_Y_SIZE 320

// Configurações de calibração alternativas para ST7796:
// Se o touch estiver invertido ou com escala errada, teste estas opções:
// #define TOUCH_X_SIZE 320  // Para orientação diferente
// #define TOUCH_Y_SIZE 480