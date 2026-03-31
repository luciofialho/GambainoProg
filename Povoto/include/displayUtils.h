#ifndef DISPLAY_UTILS_H
#define DISPLAY_UTILS_H

#include <Arduino.h>
#include <FS.h>
#include <Wire.h>

// Endereço I2C do touch FocalTech


// Declaração de tft externo (definido no main)
//extern TFT_eSPI tft;

void initTFT();
void TFTWaintingWifiConnection();

void processTouch();
void showConfigQRCode();
bool isScreenSaverActive();
void forceScreenSaver(bool enable);
void resetDisplayHardware();


// Funções de touch I2C
//bool readTouchI2C(uint16_t *x, uint16_t *y);

// Funções de leitura de arquivo BMP
uint16_t read16(fs::File &f);
uint32_t read32(fs::File &f);
void drawBmp(const char *filename, int16_t x, int16_t y);

// Função de leitura de registrador I2C
uint8_t rd(uint8_t reg);

#endif // DISPLAY_UTILS_H