# NVS storage

Each read...FromEEPROM/write...ToNIV function directly calls Preferences
get/put methods. Each independent value has its own key. There are no field
registration tables or access dispatch functions.

FMTData.heater contains enabled, onMinutes and offMinutes and is stored as one
blob. FMTData.coolingCycle is also one blob. Counters are individual values;
totalMolsEjected and CO2InSolution use getDouble/putDouble (8 bytes).
CountersData.dailyHs (24-hour headspace average, 24 hourly bins + held value,
244 bytes) is the blob `dailyHs`; a different size or non-finite sums restart
it empty. Besides writeCountersDataToNIV(), writeDailyHeadspaceToNIV() writes
only this key, on a new hour, a rebase (dump) or a clear (liquid, dry/dynamic
hopping). See docs/spec_headspace_24h.md.
CountersData.co2RateHeld / co2RateHeldAt (keys `co2RateHeld`, `co2RateAt`) keep
the last gCO2/L/d from a mature window and its NTP time; see docs/gco2-rate.md.
CountersData.co2DissolvedMode (key `co2Mode`, 0..3; other values read as 2)
and co2ArmedAt (key `co2ArmedAt`) keep the dissolved-CO2 state; see
docs/dissolved-co2.md.
CountersData.co2Supersat (key `co2Supersat`) and FMTData.supersatTauDesorbHours /
supersatTauAbsorbHours (keys `supersatTauD` / `supersatTauA`) belong to the
supersaturation model; see docs/dissolved-co2.md. FMTData.co2TransferTime was
removed without a schema change: its key `co2TransferTime` stays unused in NVS.
CountersData.co2CorrectionDebt (key `co2Debt`, double) keeps the debt of the
produced-CO2 integral across reboots; see docs/gco2-rate.md.

To add a field, declare it and its default, then add its get/put calls to the
corresponding read/write functions. Keep keys within the NVS 15-character limit.
For any field added to `FMTData_t` or `UserConfigurationData_t`, also update
both `savePovotoSettingsBackup()` and `loadPovotoSettingsBackup()` in
`PovotoSettingsBackup.cpp`. These routines generate
and read the `povoto-settings.json` browser download; the file is not kept in
LittleFS. Import trusts exported values and does not repeat the page
validations. Fields absent from an older backup retain the current value on the
device; fields no longer used by the firmware are ignored. The backup JSON
version is informational and is accepted across versions.
To remove a field, remove its declaration and get/put calls. Increment
PVT_NVS_SCHEMA_VERSION (in PovotoData.h) when obsolete keys should be removed.
Version 0 is reserved for missing/incomplete storage.

At startup all five active namespaces are read into RAM first. If
FMTData.nvsSchemaVersion differs from PVT_NVS_SCHEMA_VERSION, all five active
namespaces are cleared and rewritten using those RAM values. The new version
is persisted in pvt_settings/schemaVersion only after all writes succeed.
Equal versions cause no clear/rewrite. Failed rewrites are retried next boot.
Power loss may leave some fields at defaults; this operation is not atomic.
The old povoto namespace and Peers are not cleared. Wi-Fi credentials are
stored separately in `pvt_wifi` (`ssid` and `password`) and are not touched by
schema rewrites.
Write functions return bool so the rewrite can detect storage failures.

The previous type/version-wrapped records are not migrated. Incompatible or
missing records use defaults. Group blobs are checked for size and valid times.
Namespaces remain pvt_settings, pvt_user, pvt_batch, pvt_setpoint
and pvt_counters. The local Wi-Fi configuration uses pvt_wifi.

`POST /factoryreset` clears every Povoto namespace, including pvt_wifi, then
persists the factory defaults for the data namespaces.

Run structural checks with: python tests/check_storage_schema.py

Automatic set point rules live in `pvt_autosp` (AutoSetpoints.cpp): one
blob per rule (`rule0`..`rule7`, NAN = empty field) and the trigger times
(`trig0`..`trig7`, local NTP epoch, 0 = not triggered). Trigger times are written only when a rule fires or is reset, so
saving definitions never re-arms a rule. Stored rules that fail validation, or
whose blob size differs from `AutoSetpointRule_t` (older layout), are dropped on
load. Starting a new batch clears all trigger times. The namespace is
cleared/rewritten with the others on schema change and factory reset, and is
not part of `povoto-settings.json` (rules have their own XML export).

Schema 2 merges calibration fields into FMTData/pvt_settings. The retired
pvt_calib namespace is cleared on schema change, with no migration. Pressure
point 2 defaults to zero for both pressure and current. The calibration-page
checkbox is derived from pressure2Bar and is not persisted separately.
