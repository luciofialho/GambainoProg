#include "PovotoFilesystem.h"

#include <esp_partition.h>

namespace {
fs::LittleFSFS persistentFS;
bool separateDataFS = false;

bool partitionIsBlank(const esp_partition_t *partition) {
  uint8_t block[256];
  for (size_t offset = 0; offset < partition->size; offset += sizeof(block)) {
    const size_t length = partition->size - offset < sizeof(block)
                              ? partition->size - offset : sizeof(block);
    if (esp_partition_read(partition, offset, block, length) != ESP_OK) return false;
    for (size_t i = 0; i < length; ++i) {
      if (block[i] != 0xff) return false;
    }
  }
  return true;
}
}

bool povotoFilesystemBegin() {
  const esp_partition_t *dataPartition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "persist");
  separateDataFS = dataPartition != nullptr;

  // Never format on mount failure: an interrupted update or damaged partition
  // must not silently erase live fermentation history.
  if (!LittleFS.begin(false, "/webfs", 10, "spiffs")) return false;
  if (separateDataFS && !persistentFS.begin(false, "/persistfs", 10, "persist")) {
    // Only a fully erased partition may be initialized automatically. Never
    // format an existing filesystem merely because a mount failed.
    if (!partitionIsBlank(dataPartition) ||
        !persistentFS.begin(true, "/persistfs", 10, "persist")) {
      LittleFS.end();
      return false;
    }
  }
  return true;
}

bool povotoHasSeparateDataFS() { return separateDataFS; }

fs::LittleFSFS &povotoWebFS() { return LittleFS; }

fs::LittleFSFS &povotoDataFS() {
  return separateDataFS ? persistentFS : LittleFS;
}
