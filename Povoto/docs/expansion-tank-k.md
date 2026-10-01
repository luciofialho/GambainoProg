# Expansion-tank filling factor k

In a relief the transfer valve opens for the expansion time that leaves 1% of
the pressure difference (about 6-7 s). The gas pushed into the expansion tank
is compressed and heats up: the flow work of the gas that enters becomes
internal energy of the tank. At the same pressure a warmer tank holds fewer
moles, so counting the tank with the beer temperature overestimates the
ejected CO2 and the headspace volume.

The accounting uses an effective expansion volume `FMTReliefVolume / k`
(`accountingReliefVolume()` in PressureControl.cpp):

- fermentation (`FMTData.expansionTankKCO2`, NVS key `kCO2`): moles of each
  relief (`P_tank * Vr/k / (R T)`) and headspace volume from the pressure drop
  (`Vr/k * f / (1 - f)`, and its inverse for the relief threshold);
- fast volume determination (`FMTData.expansionTankKAir`, NVS key `kAir`),
  which uses the same expansion time with air. Its pressure ratio is extended
  to full equalization, `f_eq = 1 - (1 - f) / (1 - r)` with r = 1%, as in a
  relief;
- slow volume determination: the valve stays open 3 min, the tank cools back
  and equalizes, so k = 1 and no residual.

Valve timing, venting and the gas-flow model keep the physical volume.

k = 1 is an isothermal tank; the adiabatic limit is the gas's heat-capacity
ratio (1.29 for CO2, 1.40 for air). Both defaults are 1.08
(`EXPANSION_TANK_K_DEFAULT`), valid range 1.0-1.5.

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

The fast test CSV has `kAr_para_FMTVolume`: the kAir that would make the test
return the configured FMTVolume (empty fermenter). The serial log prints it at
the end of the test (`[VOLUME]`). kCO2 is not derived from kAir; check it with
degassed SG readings or a fast test with the empty fermenter purged with CO2.
