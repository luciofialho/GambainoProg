# Expansion-tank filling factor k

In a relief the transfer valve opens for the expansion time that leaves 1% of
the pressure difference (about 6-7 s). The gas pushed into the expansion tank
is compressed and heats up: the flow work of the gas that enters becomes
internal energy of the tank. At the same pressure a warmer tank holds fewer
moles, so counting the tank with the beer temperature overestimates the
ejected CO2 and the headspace volume.

Energy balance of the tank while it fills (open volume, mixed gas, gas
entering with the fermenter's enthalpy, Q lost to the wall):

```
moles entering = dP_tank * Vr / (k R T),    k = gamma / (1 + q)
```

q is the fraction of the compression heat lost to the wall: q = 0 gives
k = gamma (1.29 CO2, 1.40 air), q = gamma - 1 gives k = 1 (isothermal). gamma
divides here (the tank receives gas); in the headspace, a closed gas that
expands, it is an exponent.

The same wall exchange pulls the gas toward the ambient temperature and leaves
the fraction phi of any temperature difference:

```
phi  = 1 - (gamma/k - 1) / (gamma - 1)          (~0.33 for k = 1.08, CO2)
Tref = Tamb + phi * (Tferm - Tamb)
moles in the tank = P_tank * (Vr/k) / (R Tref)
```

Relative to the fermenter gas counted at Tferm, the tank is an isothermal volume
`Vr/k * Tferm/Tref` (`accountingReliefVolume()` in PressureControl.cpp). With
Tamb = Tferm, or without a valid ambient reading, Tref = Tferm.

- Fermentation (`FMTData.expansionTankKCO2`, NVS key `kCO2`, gamma 1.29): moles
  of each relief and headspace volume from the pressure drop
  (`Vr_eff * f / (1 - f)`, and its inverse for the relief threshold). The
  Relief log has `TankReferenceTemperature`.
- Fast volume determination (gas chosen on the Calibration page: air with
  `FMTData.expansionTankKAir`, NVS key `kAir`, gamma 1.40; or CO2 with the
  fermenter purged, kCO2): the same expansion time; the pressure ratio is
  extended to full equalization, `f_eq = 1 - (1 - f) / (1 - r)` with r = 1%.
- Slow volume determination: the valve stays open 3 min, the tank cools back and
  equalizes; physical volume, no residual.

Valve timing, venting and the gas-flow model keep the physical volume. Both k
default to 1.08 (`EXPANSION_TANK_K_DEFAULT`), valid range 1.0-1.5. The model
assumes a headspace at least ~10 times the tank (smaller ones send colder gas
to the tank) and a linear wall exchange (same coefficient for the compression
excess and for the fermenter-ambient difference; not yet tested).

## Evidence (fermenter 129.6 L by water weighing, tank 2.01 L)

- Fast test, 20/09 (air, 24 reliefs, 1.92 to 1.25 bar): factor 0.98597, 141.2 L
  with the old formula. With the 1% residual compensated, k = 1.079 returns
  129.6 L.
- Slow test, 17/09 (air, 3 min open, 11 reliefs): factor ~0.98520, ~133.5 L
  (k = 1). The heating disappears with the long opening; ~3% remains that does
  not depend on the opening time (cause not identified).
- Batch 160 (CO2): the degassed SG of 01/10 (1.009) is matched with k ~1.08.
  The reading of 29/09 (1.0185 against 1.022 computed) is not explained by k.

## Calibration

Every fast volume test also calibrates k against the configured FMTVolume. Per
relief, the k of the test gas that makes the volume equal FMTVolume (with Tref
and the residual) is in the CSV (`k_relief`, `T_ref_K`; `k_para_FMTVolume`
from the cumulative factor). At the end, the Calibration page and the serial
log (`[VOLUME]`) show the median of the per-relief k, its standard deviation
and the number of reliefs, with buttons:

- "Save kAir" / "Save kCO2" (test gas): stores the median;
- with air, "Save estimated kCO2" = `1 + 1.3 * (kAir - 1)`.

Nothing is saved without the button, so a test run only to check a volume does
not change k. The saved value is only valid with the empty fermenter and the
correct FMTVolume; calibrate with the fermenter within ~3 C of the ambient
(then k barely depends on the Tref hypothesis) and repeat at a lower pressure
range: a k that changes with the pressure points to an effect proportional to
1/P (pressure-sensor offset or tank residual pressure).

kAir to kCO2: what carries over between gases is the fraction of heat lost, not
k. With the same fraction, kCO2 = 1.29 / (1 + 0.29 L), L = (1.40/kAir - 1)/0.40
(1.06 for kAir 1.079); CO2 loses heat more slowly, so ~1.12. The estimate
`1 + 1.3 (kAir - 1)` (1.10) is in the middle; the batch 160 SG gave ~1.08.

## kCO2 from the beer volume (Relief log)

Each relief is a small volume test with CO2. With the beer volume known
(fermenter weighed at transfer, `initialBeerVolume`, minus `dumpedVolume`), the
headspace is `FMTVolume - beer`, and `KCO2FromBeerVolume` is the k that makes
this relief's equalized drop factor (`adjustedEquilibriumPressure /
pressureOnReliefExtrapolated`) match it, with Tref (gamma 1.29). Diagnostics
only. With ~30 L of headspace, 0.3 L of beer-volume error is ~1% in k; the
krausen takes headspace, so use the start or the end of the fermentation.
