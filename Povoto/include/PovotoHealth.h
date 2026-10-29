#pragma once
#include <stddef.h>
#include <stdint.h>
void povotoHealthProcess();
void povotoHealthAck(const char *payload, const uint8_t *mac);
void povotoHealthStatus(char *out, size_t size);
