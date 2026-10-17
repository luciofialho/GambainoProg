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
FMTData.co2TransferTime and the supersaturation fields were removed without a
schema change: their keys (`co2TransferTime`, `supersatTauD`, `supersatTauA`,
`co2Supersat`) may stay unused in NVS.
The expansion-tank fields of the te-final tag were removed without a schema
change: their keys (`expTimeA`, `expTimeB`, `targetResidual`, `kCO2`, `kAir`,
`flowCO2Air`, `ventResidualA`..`C`, `reliefVolume`, `dallasExpTank`) stay in
pvt_settings, unused, for a fallback to the tag (docs/sem-te-plano.md, section 1).
A schema rewrite copies them back after clearing the namespace. Never reuse
these keys with another meaning.
Pressure limits (Settings page): `maxPressure` (security pressure, 2-3 bar) and `maxSetPress`
(highest pressure set point, default 2.0, always below `maxPressure`; a missing or invalid value falls back to
the default kept below `maxPressure`). `lastExpTankVol` is the last expansion tank volume typed on the
fermenter volume routine (initial value of its field).
Direct vent (docs/vent.md): `ventDrop`, `flowLevel` (floats; `ventMinSec`, `ventSafeDrop` and
`liquidGasMass` are no longer read or written and stay in NVS, unused) and the blobs `ventCurve` / `ventRefCurve`
(VentModel::Curve, 28 bytes: a0..a3, t0, pMin, pMax, epoch); a blob of another
size or an invalid curve keeps the default (18/09) curve and no reference.
FMTData.dallasAddress holds the Dallas ROM address of each thermometer role,
one 8-byte blob per role: `dallasFerm` (fermenter), `dallasEnv` (environment),
`dallasHeadspace` (headspace); zeros = not assigned (`dallasExpTank` is kept, unused). A missing key or a
different size reads as not assigned, so roles can be added or retired without
touching the others. See docs/thermometers.md.
CountersData.co2CorrectionDebt (key `co2Debt`, double) keeps the debt of the
produced-CO2 integral across reboots; see docs/gco2-rate.md.
CountersData.co2TransStart / co2TransDir / co2TransHenryRef / co2TransRate /
co2TransRateAt / co2TransReliefAt (keys `co2TrStart`, `co2TrDir`, `co2TrHenry`,
`co2TrRate`, `co2TrRateAt`, `co2TrRelief`) keep the gCO2/L/d transition. A
missing `co2TrStart` reads as 1: a transition starts at the next sample.
See docs/gco2-rate.md.

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

Triggered action rules (internally "automatic set points") live in `pvt_autosp` (AutoSetpoints.cpp): one
blob per rule (`rule0`..`rule7`, NAN = empty field) and the trigger times
(`trig0`..`trig7`, local NTP epoch, 0 = not triggered). Trigger times are written only when a rule fires or is reset, so
saving definitions never re-arms a rule. Stored rules that fail validation, or
whose blob size differs from `AutoSetpointRule_t` (older layout), are dropped on
load. Starting a new batch clears all trigger times. The namespace is
cleared/rewritten with the others on schema change and factory reset, and is
not part of `povoto-settings.json` (rules have their own XML export).

The cloud keeps "Accept cloud edits" in `pvt_cloud` (`edits`, default true,
CloudSync.cpp; User Configuration page). The cloud log keeps its synthetic-log setting
in `pvt_cloud` (CloudLog.cpp):
`synth` (bool) and `synthStart` (local epoch of profile day 0). It is a
debug-only setting, outside the schema rewrite, factory reset and
`povoto-settings.json`. See docs/cloud-log.md.

Schema 2 merges calibration fields into FMTData/pvt_settings. The retired
pvt_calib namespace is cleared on schema change, with no migration. The
pressure conversion is a straight line through points A and B; the keys `p2Bar` and `p2Current` of the former third
point are no longer read or written and stay in NVS, unused.
