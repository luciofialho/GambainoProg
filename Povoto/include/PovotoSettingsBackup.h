#ifndef POVOTO_SETTINGS_BACKUP_H
#define POVOTO_SETTINGS_BACKUP_H

#include <Arduino.h>

// JSON document used by the browser download/upload actions on /fmtdata.
String savePovotoSettingsBackup();
bool loadPovotoSettingsBackup(const String &settings);

#endif  // POVOTO_SETTINGS_BACKUP_H
