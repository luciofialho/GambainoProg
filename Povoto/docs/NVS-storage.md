# NVS storage

Each read...FromEEPROM/write...ToNIV function directly calls Preferences
get/put methods. Each independent value has its own key. There are no field
registration tables or access dispatch functions.

FMTData.heater contains enabled, onMinutes and offMinutes and is stored as one
blob. FMTData.coolingCycle is also one blob. Counters are individual values;
totalMolsEjected and CO2InSolution use getDouble/putDouble (8 bytes).

To add a field, declare it and its default, then add its get/put calls to the
corresponding read/write functions. Keep keys within the NVS 15-character limit.
To remove a field, remove its declaration and get/put calls. Increment
PVT_NVS_SCHEMA_VERSION (in PovotoData.h) when obsolete keys should be removed.
Version 0 is reserved for missing/incomplete storage.

At startup all five active namespaces are read into RAM first. If
FMTData.nvsSchemaVersion differs from PVT_NVS_SCHEMA_VERSION, all five active
namespaces are cleared and rewritten using those RAM values. The new version
is persisted in pvt_settings/schemaVersion only after all writes succeed.
Equal versions cause no clear/rewrite. Failed rewrites are retried next boot.
Power loss may leave some fields at defaults; this operation is not atomic.
The old povoto namespace, Peers and Wi-Fi are not cleared.
Write functions return bool so the rewrite can detect storage failures.

The previous type/version-wrapped records are not migrated. Incompatible or
missing records use defaults. Group blobs are checked for size and valid times.
Namespaces remain pvt_settings, pvt_user, pvt_batch, pvt_setpoint
and pvt_counters.

Run structural checks with: python tests/check_storage_schema.py

Schema 2 merges calibration fields into FMTData/pvt_settings. The retired
pvt_calib namespace is cleared on schema change, with no migration. Pressure
point 2 defaults to zero for both pressure and current. The calibration-page
checkbox is derived from pressure2Bar and is not persisted separately.
