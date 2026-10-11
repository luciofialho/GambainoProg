#ifndef SIDEKICK_AMBIENT_H
#define SIDEKICK_AMBIENT_H
#include <stddef.h>

// Site sensors broadcast to the Povotos by ESP-NOW (Povoto/docs/atmospheric-pressure.md):
// - BMP581 barometer (I2C): atmospheric pressure, packet 'A' ("hPa");
// - Dallas thermometer, if one is installed: environment temperature,
//   packet 'V' ("C", the same format as the BrewCore's). The Povotos prefer
//   the SideKick's temperature to the BrewCore's.
// Both are optional and detected at boot.

void ambientSensorsBegin();   // setup(), after ESP-NOW is initialized
void ambientSensorsProcess(); // loop()
void appendAmbientSensorsStatus(char *st, size_t size);

#endif
