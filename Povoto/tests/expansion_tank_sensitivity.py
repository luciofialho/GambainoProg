"""Sensitivity of the fermenter volume by the expansion tank (docs/expansion-tank-volume.md).

Same balance as ExpansionTank.cpp (computeRelief): the fermenter loses what the tank
gains,

  V_F = V_E * [(P2+Patm2)/T_E2 - Patm1/T_E1] / [(P1+Patm1)/T_F1 - (P2+Patm2)/T_F2]

The starting point is the table of a debug run (19 L tank, 10 reliefs, from 2.0
to 0.5 bar, fermenter 25.2 C, tank 20.0 C). Every source of error is applied to
one quantity at a time and the change of V_F is printed per relief, in %. A
random error (different in each relief) shrinks with sqrt(n) in the mean of n
reliefs; a systematic one (the same in all) does not.

Run: python tests/expansion_tank_sensitivity.py
"""
import math

PATM = 0.93          # bar
VE = 19.0            # L, expansion tank
TF = 25.19 + 273.15  # K, fermenter (Dallas)
TE = 20.00 + 273.15  # K, tank (environment)
# (P1, P2) gauge bar of the 10 reliefs of the run
RELIEFS = [(2.0000, 1.7398), (1.7397, 1.5134), (1.5133, 1.3165), (1.3164, 1.1451), (1.1451, 0.9961),
           (0.9960, 0.8665), (0.8664, 0.7537), (0.7537, 0.6557), (0.6556, 0.5704), (0.5703, 0.4961)]


def volume(p1, p2, patm1=PATM, patm2=PATM, tf1=TF, tf2=TF, te1=TE, te2=TE, ve=VE):
    gain = (p2 + patm2) / te2 - patm1 / te1
    loss = (p1 + patm1) / tf1 - (p2 + patm2) / tf2
    return ve * gain / loss


def table(title, note, effect, columns):
    """effect(p1, p2, value) -> V with the error applied; columns: values of the error."""
    print(f"\n{title}\n  {note}")
    print("  relief  P1     P2     dP    V (L)  " + "".join(f"{label:>10}" for label, _ in columns))
    means = [0.0] * len(columns)
    for i, (p1, p2) in enumerate(RELIEFS, 1):
        v0 = volume(p1, p2)
        cells = []
        for k, (_, value) in enumerate(columns):
            d = 100.0 * (effect(p1, p2, value) / v0 - 1.0)
            means[k] += d / len(RELIEFS)
            cells.append(f"{d:+9.3f}%")
        print(f"  {i:>4}  {p1:6.4f} {p2:6.4f} {p1 - p2:5.3f} {v0:7.2f} " + " ".join(cells))
    print("  mean of the 10 " + " " * 28 + " ".join(f"{m:+9.3f}%" for m in means))


print(f"Base: tank {VE} L, Patm {PATM} bar, T fermenter {TF - 273.15:.2f} C, T tank {TE - 273.15:.2f} C")
print("V of the first relief:", f"{volume(*RELIEFS[0]):.2f} L (simple formula: {VE * TF / TE * RELIEFS[0][1] / (RELIEFS[0][0] - RELIEFS[0][1]):.2f} L)")

# 1. The fermenter thermometer reads another value after the relief (T_F2 vs T_F1). RANDOM per relief.
table("1. Fermenter temperature differs between P1 and P2 (T_F2 = T_F1 + dT)",
      "random: the Dallas moves one step of 0.0625 K, or the gas really cools",
      lambda p1, p2, d: volume(p1, p2, tf2=TF + d),
      [("0.0625 K", 0.0625), ("0.125 K", 0.125), ("0.2 K", 0.2), ("0.5 K", 0.5)])

# 2. Pressure noise on the mean of P2 (P1 of the next relief is the same pressure). RANDOM.
table("2. Error of the settled pressure P2 (P1 unchanged)",
      "random: noise of the 10-s mean (the manual assumes about +-0.5 mbar)",
      lambda p1, p2, d: volume(p1, p2 + d / 1000.0),
      [("0.1 mbar", 0.1), ("0.3 mbar", 0.3), ("0.5 mbar", 0.5), ("1 mbar", 1.0)])

# 3. Zero offset of the pressure sensor: the same error in P1 and P2 of every relief. SYSTEMATIC.
table("3. Zero offset of the pressure sensor (P1 and P2 both read +d)",
      "systematic: the same in all reliefs (this is why the sensor zero comes first)",
      lambda p1, p2, d: volume(p1 + d / 1000.0, p2 + d / 1000.0),
      [("0.5 mbar", 0.5), ("1 mbar", 1.0), ("2 mbar", 2.0), ("5 mbar", 5.0)])

# 4. Tank temperature (the environment thermometer): SYSTEMATIC.
table("4. Tank temperature wrong (T_E1 and T_E2 both +d)",
      "systematic: the tank follows the room; the environment sensor may be off or the tank warmer",
      lambda p1, p2, d: volume(p1, p2, te1=TE + d, te2=TE + d),
      [("0.2 K", 0.2), ("0.5 K", 0.5), ("1 K", 1.0), ("2 K", 2.0)])

# 5. Atmospheric pressure: the same Patm in both readings is a common offset; a change between them matters. RANDOM.
table("5. Patm changes between the readings (Patm2 = Patm1 + d)",
      "random: a weather front during the run; Patm1 is the tank's starting atmosphere",
      lambda p1, p2, d: volume(p1, p2, patm2=PATM + d / 1000.0),
      [("0.2 hPa", 0.2), ("0.5 hPa", 0.5), ("1 hPa", 1.0), ("2 hPa", 2.0)])

# 6. Tank volume: proportional. SYSTEMATIC.
print("\n6. Tank volume error: V_F is proportional to V_E (+0.1% in V_E = +0.1% in V_F, +0.5% = +0.5%),")
print("   in every relief and in the mean. A tank weighed to 0.1 kg of water (19 L) is +-0.5%.")

print("""
Reading:
 - dT between P1 and P2 and the pressure noise are RANDOM: they get larger in the last reliefs, when dP is small
   (the relative error is about (P2+Patm)/dP * dT/T and (noise/dP)), and they average out with sqrt(n).
 - The zero offset, the tank temperature and the tank volume are SYSTEMATIC: the mean keeps them whole.
   Measure the sensor zero (P1 of the manual), the tank volume and the tank temperature well.
""")

# Effect of dT on the mean of n reliefs when it is random (a step of 0.0625 K up or down).
dev = []
for p1, p2 in RELIEFS:
    v0 = volume(p1, p2)
    dev.append(100.0 * (volume(p1, p2, tf2=TF + 0.0625) / v0 - 1.0))
rms = math.sqrt(sum(d * d for d in dev) / len(dev))
print(f"One Dallas step (0.0625 K) in T_F2, random sign: rms {rms:.3f}% per relief, "
      f"{rms / math.sqrt(len(dev)):.3f}% in the mean of {len(dev)} reliefs.")
