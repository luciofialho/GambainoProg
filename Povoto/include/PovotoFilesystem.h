#ifndef POVOTO_FILESYSTEM_H
#define POVOTO_FILESYSTEM_H

#include <LittleFS.h>

// The old partition table has only "spiffs". In that case both functions
// return the same filesystem, so one firmware can run on unmigrated devices.
bool povotoFilesystemBegin();
bool povotoHasSeparateDataFS();
fs::LittleFSFS &povotoWebFS();
fs::LittleFSFS &povotoDataFS();

#endif
