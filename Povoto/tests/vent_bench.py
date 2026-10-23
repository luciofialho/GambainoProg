"""Bench of the direct-vent counting (docs/vent.md) against a synthetic truth.

Truth: a CO2 headspace of volume Vh at T0 vents through the valve for the
real open time; the effective opening is t - t0. During the opening the
pressure follows the empirical polytrope that defines the exponent 1.29
(P = P_on (P_iso / P_on)^1.29, P_iso the settled pressure of the moles left,
real gas), the temperature follows from P V = n Z R T, and the valve molar
flow is C_air(P) * ratio(P) / sqrt(Z) * P / sqrt(T) at the instantaneous
pressure and temperature. After closing, the gas warms back to
T0 with tau = 30 s. The firmware reads the pressure ~0.4-0.8 s after closing;
the exponent 1.29 was fitted on that same reading, so the main table reads at
the closing (the definition of the exponent) and the delay is a sensitivity.

Firmware: the opening time from the curve and the headspace (vent_seconds,
then max(min(t, t_12%), 1.5 s)), the moles from the log-mean pressure of
P_on and the pressure read after closing (vent_moles), and the apparent
headspace from the isothermal drop (isothermal_after with 1.29).

Run: python tests/vent_bench.py   (exit code 1 if a limit is exceeded)
"""
import math
import sys
import vent_model as vm

PATM = 0.928573            # main site (batch 160)
T0 = 293.15
EXPONENT = 1.29
TAU_THERMAL = 30.0         # s, recovery after closing (docs: 1.29 vs 24-h recovery)
CURVE = vm.make_curve(82.8, 1.06, 0.18, 2.10)  # fit of the 2 tests after the leak repair of 22/10/2026 (no t0)
T0_TRUTH = 0.0   # opening delay minus closing delay of the real valve; the firmware assumes 0
TARGET_DROP = 0.06
SAFETY_DROP = 0.12
MIN_SECONDS = 1.5
# Firmware vs truth, any scenario. With a flat curve the formula is exact
# (checked below: the valve alone is choked above ~0.83 bar); with the valve + silencer curve,
# C changes with the pressure inside one vent and C(P_mean) misses the trajectory mean slightly.
LIMIT_MOLES_PCT = 0.15
LIMIT_VH_PCT = 0.25
LIMIT_FLAT_PCT = 0.01


def true_vent(headspace, p_on_gauge, open_seconds, t0=T0_TRUTH, level=1.0, read_after=0.0,
              t_gas=T0, exponent=EXPONENT, dt=2e-4):
    """Returns (moles out, pressure read after closing, settled pressure), gauge bar.

    The state is the moles n in the headspace. Their settled (isothermal, real
    gas) pressure P_iso(n) and the actual pressure follow the empirical
    polytrope that defines the exponent, P = P_on (P_iso / P_on)^k; the actual
    temperature then comes from the real-gas law."""
    p_on = p_on_gauge + PATM

    def settled(n, t_k):  # P with n moles at t_k (real gas)
        q = n * vm.R * t_k / headspace
        for _ in range(4):
            q = n * vm.co2_z(q, t_k) * vm.R * t_k / headspace
        return q

    def state(n):  # actual pressure and temperature during the opening
        p = p_on * (settled(n, t_gas) / p_on) ** exponent
        t_k = t_gas
        for _ in range(4):
            t_k = p * headspace / (n * vm.co2_z(p, t_k) * vm.R)
        return p, t_k

    def rate(n):
        p, t_k = state(n)
        return (vm.flow_constant_air(CURVE, p - PATM, PATM) * vm.co2_air_ratio(CURVE["beta"], p - PATM, PATM)
                / math.sqrt(vm.co2_z(p, t_k)) * level * p / math.sqrt(t_k))

    n_on = p_on * headspace / (vm.co2_z(p_on, t_gas) * vm.R * t_gas)
    n = n_on
    effective = max(0.0, open_seconds - t0)
    steps = max(1, int(round(effective / dt)))
    h = effective / steps
    for _ in range(steps):  # midpoint (RK2)
        n -= h * rate(n - 0.5 * h * rate(n))
    _, t_close = state(n)
    t_read = t_gas - (t_gas - t_close) * math.exp(-read_after / TAU_THERMAL)
    return n_on - n, settled(n, t_read) - PATM, settled(n, t_gas) - PATM


def firmware_seconds(headspace, p_on, t_gas=T0):
    estimate = vm.vent_seconds(CURVE, 1.0, headspace, p_on, p_on * (1 - TARGET_DROP), PATM, t_gas, EXPONENT)
    safety = vm.vent_seconds(CURVE, 1.0, headspace, p_on, p_on * (1 - SAFETY_DROP), PATM, t_gas, EXPONENT)
    return max(min(estimate, safety), MIN_SECONDS), estimate


def run_case(setpoint, headspace, quiet=False, **truth):
    p_on = setpoint / math.sqrt(1 - TARGET_DROP)
    seconds, estimate = firmware_seconds(headspace, p_on)
    t_firmware = truth.pop("t_firmware", T0)
    moles_true, p_read, p_settled = true_vent(headspace, p_on, seconds, **truth)
    moles_fw, p_mean, cooling = vm.vent_moles(CURVE, 1.0, seconds, p_on, p_read, PATM, t_firmware, EXPONENT)
    p_iso = vm.isothermal_after(p_on, p_read, PATM, EXPONENT)
    vh = vm.apparent_headspace(moles_fw, p_on, p_iso, PATM, t_firmware)
    drop = 1 - p_settled / p_on
    row = dict(setpoint=setpoint, vh=headspace, t=seconds, est=estimate, drop=100 * drop,
               err_n=100 * (moles_fw / moles_true - 1), err_vh=100 * (vh / headspace - 1),
               err_iso=1000 * (p_iso - p_settled), n=moles_true)
    if not quiet:
        print(f"{setpoint:4.1f} bar {headspace:4.0f} L  t {seconds:5.2f} s (est {estimate:5.2f})  "
              f"drop {row['drop']:5.2f}%  moles {moles_true:.4f}  count {row['err_n']:+.3f}%  "
              f"Vh {row['err_vh']:+.3f}%  P_iso {row['err_iso']:+.2f} mbar")
    return row


def vapor_checks():
    """Vapor fraction of the vented gas: reference pressures and the CO2 correction."""
    print("\nVapor in the vented gas (y = (a_w p_water + x gamma p_ethanol) / P_abs):")
    ok = True
    for t, want in ((20.0, 23.39), (10.0, 12.28)):  # mbar, saturated water (Buck)
        got = 1000 * vm.water_saturation_pressure(t)
        good = abs(got - want) < 0.05
        ok = ok and good
        print(f"  p_water {t:4.1f} C {got:6.2f} mbar (reference {want}) {'ok' if good else 'FAIL'}")
    got = 1000 * vm.ethanol_saturation_pressure(20.0)
    good = abs(got - 58.7) < 1.0  # literature 58.7 mbar at 20 C
    ok = ok and good
    print(f"  p_ethanol 20.0 C {got:6.2f} mbar (literature 58.7) {'ok' if good else 'FAIL'}")
    x = vm.ethanol_mole_fraction(5.0)
    print(f"  x_ethanol at 5% ABV {x:.4f}; p_ethanol term {1000 * x * vm.ETHANOL_ACTIVITY_COEFFICIENT * vm.ethanol_saturation_pressure(20.0):.2f} mbar at 20 C")
    for t, abv, p_gauge in ((20.0, 0.0, 0.2), (20.0, 5.0, 0.2), (20.0, 5.0, 1.9), (10.0, 5.0, 1.0), (10.0, 0.0, 0.2)):
        y = vm.vent_vapor_fraction(t, abv, p_gauge + PATM)
        print(f"  {t:4.1f} C, {abv:3.1f}% ABV, {p_gauge:.1f} bar gauge: y {100 * y:5.2f}%  (CO2 moles = counted x {1 - y:.4f})")
    y0 = vm.vent_vapor_fraction(20.0, 0.0, 1.0 + PATM)
    y1 = vm.vent_vapor_fraction(20.0, 5.0, 1.0 + PATM)
    good = 0.0 < y0 < y1 < 0.05 and vm.vent_vapor_fraction(float("nan"), 5.0, 2.0) == 0.0
    ok = ok and good
    print(f"  ethanol raises y, bad input gives 0: {'ok' if good else 'FAIL'}")
    return ok


def extrapolation_checks():
    """tau over the whole pressure range (a physical curve, no extrapolation rule): continuous, rising
    towards low pressure (subsonic), the CO2/air ratio between the two limits, and the count of a vent
    below the range of the test."""
    print("\nCurve over the pressure range (measured %.2f-%.2f bar, physical outside it):" % (CURVE["p_min"], CURVE["p_max"]))
    ok = True
    previous = vm.tau_ref(CURVE, 2.5, PATM)
    for p in (2.0, 1.5, 1.0, 0.8, 0.5, 0.3, 0.25, 0.15, 0.05):
        tau = vm.tau_ref(CURVE, p, PATM)
        ratio = vm.co2_air_ratio(CURVE["beta"], p, PATM)
        good = math.isfinite(tau) and tau >= previous - 1e-9 and 0.78 < ratio < 0.83
        ok = ok and good
        print(f"  {p:.2f} bar: tau {tau:7.1f} s, CO2/air {ratio:.4f} {'ok' if good else 'FAIL'}")
        previous = tau
    good = abs(vm.tau_ref(CURVE, 1.0 + 1e-9, PATM) / vm.tau_ref(CURVE, 1.0, PATM) - 1) < 1e-6
    ok = ok and good
    print(f"  continuous: {'ok' if good else 'FAIL'}")
    row = run_case(0.25, 30, quiet=True)
    good = abs(row["err_n"]) <= 0.5 and abs(row["err_vh"]) <= 0.5
    ok = ok and good
    print(f"  vent at 0.25 bar, 30 L (below the measured range): t {row['t']:.2f} s, count {row['err_n']:+.3f}%, "
          f"Vh {row['err_vh']:+.3f}% (limit 0.5%) {'ok' if good else 'FAIL'}")
    return ok


def main():
    global CURVE
    ok = True
    print("Vents with the firmware rule t = max(min(t_6%, t_12%), 1.5 s); truth with the same valve curve,")
    print("Patm %.4f bar, gas %.2f K, t0 %.0f ms" % (PATM, T0, 1000 * T0_TRUTH))
    rows = [run_case(sp, vh) for sp in (0.5, 1.0, 1.9) for vh in (20, 30, 50)]
    worst_n = max(abs(r["err_n"]) for r in rows)
    worst_vh = max(abs(r["err_vh"]) for r in rows)
    print(f"worst: count {worst_n:.3f}% (limit {LIMIT_MOLES_PCT}%), Vh {worst_vh:.3f}% (limit {LIMIT_VH_PCT}%)")
    ok = worst_n <= LIMIT_MOLES_PCT and worst_vh <= LIMIT_VH_PCT

    curve, CURVE = CURVE, vm.make_curve(120.0, 1e-3, CURVE["p_min"], CURVE["p_max"])
    flat = [run_case(sp, 30, quiet=True) for sp in (1.0, 1.9)]  # nearly no outlet: choked, tau constant above ~0.83 bar
    CURVE = curve
    worst_flat = max(abs(r["err_n"]) for r in flat)
    print(f"flat curve (valve alone, choked, 1.0 and 1.9 bar): worst count {worst_flat:.4f}% (limit {LIMIT_FLAT_PCT}%): "
          "Z, CO2/air ratio, cooling and t0 are exact")
    ok = ok and worst_flat <= LIMIT_FLAT_PCT

    print("\nSensitivity (1.9 bar, 30 L; the count is the moles, the error is firmware vs truth):")
    for label, kw in [("reading 0.4 s after closing", dict(read_after=0.4)),
                      ("reading 0.8 s after closing", dict(read_after=0.8)),
                      ("real t0 +20 ms (assumed 0)", dict(t0=0.020)),
                      ("real t0 -20 ms (assumed 0)", dict(t0=-0.020)),
                      ("valve 3% slower than the curve", dict(level=0.97)),
                      ("gas 2 K warmer than assumed", dict(t_gas=T0 + 2)),
                      ("real exponent 1.20", dict(exponent=1.20)),
                      ("real exponent 1.40", dict(exponent=1.40))]:
        r = run_case(1.9, 30, quiet=True, **kw)
        print(f"  {label:34s} count {r['err_n']:+.3f}%  Vh {r['err_vh']:+.3f}%")
    for sp in (0.5, 1.9):
        r = run_case(sp, 30, quiet=True, t_gas=T0 + 6, t_firmware=T0)
        print(f"  {sp} bar, gas 6 K warmer, liquid T used   count {r['err_n']:+.3f}%  Vh {r['err_vh']:+.3f}%")
    ok = extrapolation_checks() and ok
    ok = vapor_checks() and ok
    print("\nRESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
