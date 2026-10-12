"""Redo the venting-test fit outside the board (docs/venting-test.md).

Usage:
  python tests/venting_fit.py venting_test.csv [--test N] [--fixed-t0]
  python tests/venting_fit.py --selftest

Reads the CSV of /calibration/venting.csv (';' separated, ',' decimals),
fits tau(P) cubic + t0 by the same weighted least squares as the firmware
(tests/vent_model.py mirrors include/VentModel.h) and prints, per test:
coefficients, t0, the residual of each release, the shape residual, the t0
dispersion, the drift and the comparison with the firmware's own fit.
--selftest builds a synthetic test (known curve, t0, cooling and noise) and
checks that the fit recovers them.
"""
import argparse
import csv
import math
import random
import statistics
import sys

import vent_model as vm


def number(text):
    text = (text or "").strip().replace(",", ".")
    return float(text) if text else float("nan")


def load(path):
    tests = {}
    with open(path, encoding="utf-8") as f:
        for row in csv.DictReader(f, delimiter=";"):
            t = tests.setdefault(row["test"], {"meta": row, "releases": []})
            t["releases"].append({
                "release": int(row["release"]), "level": int(row["level"]), "kind": row["kind"],
                "requested": number(row["t_requested_s"]), "t": number(row["t_real_s"]),
                "p1": number(row["P1_bar"]), "p_close": number(row["P_close_bar"]), "p2": number(row["P2_bar"]),
                "temp_k": number(row["temp_ferm_K"]),
            })
    return tests


def fit(releases, volume, patm, co2, fit_t0=True):
    valid = [r for r in releases
             if r["t"] > 0.2 and r["p2"] > 0.05 and r["p1"] > r["p2"] + 0.001 and r["temp_k"] > 200]
    measured = sorted(m for m in (vm.release_exponent(r["p1"], r["p_close"], r["p2"], patm, float("nan"))
                                  for r in valid if math.isfinite(r["p_close"])) if math.isfinite(m))
    fallback = statistics.median(measured) if measured else 1.0
    result = vm.fit_venting_test(valid, volume, patm, co2=co2, fit_t0=fit_t0, default_exponent=fallback)
    if result is None:
        return None, valid
    result["p_min"] = min(r["p2"] for r in valid)
    result["p_max"] = max(r["p1"] for r in valid)
    result["fallback_exponent"] = fallback
    return result, valid


def summary(result, valid):
    t0 = result["t0"]
    shape = [res / (r["t"] - t0) for r, res in zip(valid, result["residuals"]) if r["t"] >= 4.0]
    short = [res for r, res in zip(valid, result["residuals"]) if r["t"] <= 2.2]
    drifts = []
    short_sd = statistics.stdev(short) if len(short) > 1 else float("nan")
    for level in sorted({r["level"] for r in valid}):
        idx = [i for i, r in enumerate(valid)
               if r["level"] == level and r["kind"] == "level" and abs(r["requested"] - 2.0) < 0.01]
        if len(idx) >= 2:
            drifts.append((result["residuals"][idx[-1]] - result["residuals"][idx[0]]) / (2.0 - t0))
    return {
        "shape_rms_pct": 100 * math.sqrt(sum(x * x for x in shape) / len(shape)) if shape else float("nan"),
        "t0_se_ms": 1000 * result["t0_se"],
        "short_sd_ms": 1000 * short_sd,
        "drift_pct": 100 * statistics.mean(drifts) if drifts else float("nan"),
        # Each drift is a difference of two short residuals: sd * sqrt(2) / (2 - t0) per level.
        "drift_se_pct": 100 * short_sd * math.sqrt(2) / (2.0 - t0) / math.sqrt(len(drifts)) if drifts else float("nan"),
    }


def report(name, test, fixed_t0):
    meta = test["meta"]
    volume, patm, co2 = number(meta["volume_L"]), number(meta["patm_bar"]), meta["gas"] == "CO2"
    result, valid = fit(test["releases"], volume, patm, co2, fit_t0=not fixed_t0)
    print(f"\n=== test {name}: {meta['date_time']} {meta['label']} {meta['gas']} {volume:.2f} L, "
          f"Patm {patm:.5f} bar ({meta['patm_source']}), {len(valid)} valid releases ===")
    if result is None:
        print("fit failed (too few releases)")
        return
    a, t0 = result["a"], result["t0"]
    curve = vm.make_curve(a, t0, result["p_min"], result["p_max"])
    print("tau(P) = %.4f + %.4f P + %.4f P^2 + %.5f P^3 s (100 L air, 20 C), %.2f-%.2f bar"
          % (a[0], a[1], a[2], a[3], result["p_min"], result["p_max"]))
    print("t0 = %.1f ms; exponent of the releases without P_close: %.3f" % (1000 * t0, result["fallback_exponent"]))
    s = summary(result, valid)
    print("t0 standard error %.1f ms; shape residual %.3f%% rms (>= 4 s); residual sd of the releases <= 2 s "
          "%.1f ms; drift %+.3f%% +- %.3f%%" % (s["t0_se_ms"], s["shape_rms_pct"], s["short_sd_ms"], s["drift_pct"],
                                                s["drift_se_pct"]))
    print(" rel lvl kind   t_req   t_real      P1     P2   exp  resid_ms  resid%")
    for r, res, m in zip(valid, result["residuals"], result["exponents"]):
        print("%4d %3d %-5s %6.2f %8.4f %7.4f %6.4f %5.3f %8.1f %7.3f"
              % (r["release"], r["level"], r["kind"], r["requested"], r["t"], r["p1"], r["p2"], m,
                 1000 * res, 100 * res / (r["t"] - t0)))
    print("firmware: t0 standard error %s ms, drift %s +- %s %%" % (meta["t0_se_ms"], meta["drift_pct"], meta["drift_se_pct"]))
    fw = [number(meta[k]) for k in ("fit_a0", "fit_a1", "fit_a2", "fit_a3", "fit_t0_s")]
    if all(math.isfinite(x) for x in fw):
        fw_curve = vm.make_curve(fw[:4], fw[4], result["p_min"], result["p_max"])
        diffs = [100 * (vm.tau_ref(fw_curve, p) / vm.tau_ref(curve, p) - 1) for p in (1.8, 1.2, 0.8)]
        print("firmware fit: t0 %.1f ms; tau differs %+.4f / %+.4f / %+.4f %% at 1.8 / 1.2 / 0.8 bar"
              % (1000 * fw[4], *diffs))
    for p in (0.5, 1.0, 1.9):
        print("  %.1f bar: tau %.2f s (100 L air), CO2/air %.4f" % (p, vm.tau_ref(curve, p), vm.co2_air_ratio(p, patm)))


def selftest():
    """Synthetic test: known curve and t0, cooling exponent 1.35, noise 0.15 mbar on each settled
    pressure (the P2 of a release is the P1 of the next), 0.5 mbar on P_close, 2 ms on t."""
    random.seed(1)
    truth = vm.make_curve([318.6846, -296.7812, 218.9469, -55.9985], 0.025, 0.0, 3.0)
    volume, patm, temp_k, exponent = 129.6, 0.928573, 293.15, 1.35

    def release(p1, seconds):
        # Integrate t(P2) = t0 + sum a_k J_k exactly as the model, solving for P2 by bisection.
        lo, hi = 0.0, p1
        for _ in range(60):
            mid = 0.5 * (lo + hi)
            j = vm.release_integrals(volume, temp_k, p1, mid, patm, exponent, False)
            t = truth["t0"] + sum(c * x for c, x in zip(truth["a"], j))
            lo, hi = (lo, mid) if t < seconds else (mid, hi)
        p2 = 0.5 * (lo + hi)
        p_close = (p1 + patm) * ((p2 + patm) / (p1 + patm)) ** exponent - patm
        return p2, p_close

    releases, p, n = [], 1.9, 0
    noise = random.gauss(0, 0.00015)  # reading of the current settled pressure
    for level, target in enumerate([1.5, 1.1, 0.8, 0.5, None]):
        for d in (2, 0.5, 8, 1, 4, 2):
            p2, pc = release(p, d)
            n += 1
            next_noise = random.gauss(0, 0.00015)
            releases.append({"release": n, "level": level + 1, "kind": "level", "requested": d,
                             "t": d + random.gauss(0, 0.002), "p1": p + noise,
                             "p_close": pc + random.gauss(0, 0.0005), "p2": p2 + next_noise, "temp_k": temp_k})
            p, noise = p2, next_noise
        if target is None:
            break
        lo, hi = 1.0, 200.0
        for _ in range(50):
            mid = 0.5 * (lo + hi)
            lo, hi = (mid, hi) if release(p, mid)[0] > target else (lo, mid)
        p2, pc = release(p, hi)
        n += 1
        next_noise = random.gauss(0, 0.00015)
        releases.append({"release": n, "level": level + 1, "kind": "step", "requested": hi, "t": hi,
                         "p1": p + noise, "p_close": pc, "p2": p2 + next_noise, "temp_k": temp_k})
        p, noise = p2, next_noise
    result, valid = fit(releases, volume, patm, False)
    curve = vm.make_curve(result["a"], result["t0"], 0.0, 3.0)
    errors = [100 * (vm.tau_ref(curve, q) / vm.tau_ref(truth, q) - 1) for q in (1.8, 1.2, 0.8, 0.5)]
    s = summary(result, valid)
    print(f"selftest: {len(valid)} releases from 1.9 to {p:.2f} bar; t0 {1000 * result['t0']:.1f} ms (true 25.0); "
          f"tau error at 1.8/1.2/0.8/0.5 bar: " + " / ".join(f"{e:+.3f}%" for e in errors) +
          f"; t0 standard error {s['t0_se_ms']:.1f} ms; shape {s['shape_rms_pct']:.3f}% rms; "
          f"drift {s['drift_pct']:+.3f}% +- {s['drift_se_pct']:.3f}%")
    ok = abs(1000 * result["t0"] - 25.0) < 15.0 and max(abs(e) for e in errors) < 0.6
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", nargs="?")
    parser.add_argument("--test", help="test number in the CSV (default: all)")
    parser.add_argument("--fixed-t0", action="store_true", help="fit with t0 = 0")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if not args.csv:
        parser.error("give the CSV file or --selftest")
    tests = load(args.csv)
    for name, test in tests.items():
        if args.test is None or args.test == name:
            report(name, test, args.fixed_t0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
