#ifndef POVOTO_CLOUD_LOG_H
#define POVOTO_CLOUD_LOG_H

#include <Arduino.h>

// One history record and one batch state per 5-minute slot to the SideKick,
// which forwards them to the cloud (docs/cloud-log.md).
void cloudLogBegin();
void maybeSendCloudLog();

// Synthetic log (debug mode only): the record carries the synthetic
// fermentation profile instead of the measurements. Stored in NVS so it
// survives reboots; ignored outside debug mode.
bool cloudSyntheticLogStored();
bool cloudSyntheticLogActive();
uint32_t cloudSyntheticLogStart(); // local epoch of profile day 0
bool setCloudSyntheticLog(bool enabled);

// Diagnostics for the debug page.
uint32_t cloudLogSlotSeconds();
uint32_t cloudLogLastSentEpoch();
unsigned long cloudLogSentCount();
unsigned long cloudLogSendErrors();

#endif
