#ifndef POVOTO_UI_H                                 
#define POVOTO_UI_H
#include <TFT_eSPI.h>  // Biblioteca TFT_eSPI
extern TFT_eSPI tft;

void mainScreen();
void screenData();
void invalidateScreenData();
// Batch date as shown on the main screen (dd/mm or n/a).
void formatBatchDateShort(const char *batchDate, char *buf, size_t bufSize);



#endif  // POVOTO_UI_H
