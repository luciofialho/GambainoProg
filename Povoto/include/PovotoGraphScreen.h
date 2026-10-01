#ifndef POVOTO_GRAPH_SCREEN_H
#define POVOTO_GRAPH_SCREEN_H

#include <Arduino.h>

enum class GraphScreenView : uint8_t {
  Temperature, Pressure, Attenuation, Evolution
};

bool isGraphScreenActive();
bool showGraphScreen(GraphScreenView initial = GraphScreenView::Temperature);
void redrawGraphScreen();
void closeGraphScreen();
void dismissGraphScreenForScreenSaver();
void updateGraphScreenIfNeeded();
void handleGraphScreenTouch(uint16_t x, uint16_t y);
void notifyGraphScreenTouchReleased();

#endif
