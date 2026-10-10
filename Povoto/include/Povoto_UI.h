#ifndef POVOTO_UI_H                                 
#define POVOTO_UI_H
#include <TFT_eSPI.h>  // Biblioteca TFT_eSPI
extern TFT_eSPI tft;

void mainScreen();
void screenData();
void invalidateScreenData();
// Full-screen alarm error (PovotoAlarms.cpp): two big words and three lines;
// nullptr returns to the main screen. The text must outlive the screen.
struct ErrorScreenText {
  const char *big1;
  const char *big2;
  const char *line1;
  const char *line2;
  const char *line3;
};
void showErrorScreen(const ErrorScreenText *text);
// Batch date as shown on the main screen (dd/mm or n/a).
void formatBatchDateShort(const char *batchDate, char *buf, size_t bufSize);



#endif  // POVOTO_UI_H
