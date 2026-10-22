"""Redo the venting-test fit outside the board (docs/venting-test.md).

Usage:
  python tests/venting_fit.py venting_test.csv [--test N] [--keep-first]
  python tests/venting_fit.py --selftest

Reads the CSV of /calibration/venting.csv (';' separated, ',' decimals),
fits the valve + outlet model ("K-beta model") tau(P) = k / psi_eff(P) like the
firmware (tests/vent_model.py mirrors include/VentModel.h; there is no t0: the
opening delay is taken equal to the closing delay) and prints, per test:
k and beta, the residual of each release (in ms and as the equivalent
pressure in mbar), the reduced chi-square (residuals against the noise of each
reading; limit 3), the drift and the comparison with the firmware's own fit.
The first release is left out of the fit, as in the firmware (--keep-first
keeps it).
--selftest builds a synthetic test (known curve, cooling and noise) and
checks that the fit recovers it.
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
                "p_inf": number(row.get("Pinf_bar")), "tau": number(row.get("Pinf_tau_s")),
                "amp_mbar": number(row.get("Pinf_amp_mbar")), "wait": number(row.get("wait_s")),
                "end_slope": number(row.get("end_slope_mbar_min")), "temp_end_k": number(row.get("temp_end_K")),
                "noise1": number(row.get("P1_noise_mbar")) / 1000.0,
                "noise2": number(row.get("P2_noise_mbar")) / 1000.0,
            })
    return tests


KEEP_FIRST = False  # --keep-first: the firmware leaves the first release out of the fit


def fit(releases, volume, patm, co2):
    valid = [r for r in releases
             if r["t"] > 0.2 and r["p2"] > 0.05 and r["p1"] > r["p2"] + 0.001 and r["temp_k"] > 200
             and (KEEP_FIRST or r["release"] != 1)]
    measured = sorted(m for m in (vm.release_exponent(r["p1"], r["p_close"], r["p2"], patm, float("nan"))
                                  for r in valid if math.isfinite(r["p_close"])) if math.isfinite(m))
    fallback = statistics.median(measured) if measured else 1.0
    result = vm.fit_venting_test(valid, volume, patm, co2=co2, default_exponent=fallback)
    if result is None:
        return None, valid
    result["p_min"] = min(r["p2"] for r in valid)
    result["p_max"] = max(r["p1"] for r in valid)
    result["fallback_exponent"] = fallback
    return result, valid


CHI_SQUARE_LIMIT = 3.0


def summary(result, valid, volume=None, patm=None, co2=False):
    shape = [res / r["t"] for r, res in zip(valid, result["residuals"]) if r["t"] >= 4.0]
    short = [res for r, res in zip(valid, result["residuals"]) if r["t"] <= 2.2]
    drifts = []
    short_sd = statistics.stdev(short) if len(short) > 1 else float("nan")
    for level in sorted({r["level"] for r in valid}):
        idx = [i for i, r in enumerate(valid)
               if r["level"] == level and r["kind"] == "level" and abs(r["requested"] - 2.0) < 0.01]
        if len(idx) >= 2:
            drifts.append((result["residuals"][idx[-1]] - result["residuals"][idx[0]]) / 2.0)
    return {
        "shape_rms_pct": 100 * math.sqrt(sum(x * x for x in shape) / len(shape)) if shape else float("nan"),
        "short_sd_ms": 1000 * short_sd,
        "drift_pct": 100 * statistics.mean(drifts) if drifts else float("nan"),
        # Each drift is a difference of two short residuals: sd * sqrt(2) / 2 s per level.
        "drift_se_pct": 100 * short_sd * math.sqrt(2) / 2.0 / math.sqrt(len(drifts)) if drifts else float("nan"),
        "chi2": vm.reduced_chi_square(vm.make_curve(result["k"], result["beta"], result["p_min"], result["p_max"]), valid,
                                      result["residuals"], result["exponents"], volume, patm, co2)
                if volume is not None else float("nan"),
    }


def report(name, test):
    meta = test["meta"]
    volume, patm, co2 = number(meta["volume_L"]), number(meta["patm_bar"]), meta["gas"] == "CO2"
    result, valid = fit(test["releases"], volume, patm, co2)
    print(f"\n=== test {name}: {meta['date_time']} {meta['label']} {meta['gas']} {volume:.2f} L, "
          f"Patm {patm:.5f} bar ({meta['patm_source']}), {len(valid)} valid releases ===")
    if result is None:
        print("fit failed (too few releases)")
        return
    curve = vm.make_curve(result["k"], result["beta"], result["p_min"], result["p_max"])
    print("tau(P) = k / psi_eff(P): k %.3f s, beta %.4f (100 L air, 20 C), %.2f-%.2f bar"
          % (result["k"], result["beta"], result["p_min"], result["p_max"]))
    print("exponent of the releases without P_close: %.3f" % result["fallback_exponent"])
    s = summary(result, valid, volume, patm, co2)
    print("reduced chi-square %.2f%s; shape residual %.3f%% rms (>= 4 s); "
          "residual sd of the releases <= 2 s %.1f ms; drift %+.3f%% +- %.3f%%"
          % (s["chi2"], " (ABOVE %.0f: the model does not follow the data)" % CHI_SQUARE_LIMIT
             if s["chi2"] > CHI_SQUARE_LIMIT else "", s["shape_rms_pct"], s["short_sd_ms"], s["drift_pct"],
             s["drift_se_pct"]))
    print(" rel lvl kind   t_req   t_real      P1     P2   exp  resid_ms resid_mbar  noise1  noise2 (mbar)")
    for r, res, m in zip(valid, result["residuals"], result["exponents"]):
        _, s2 = vm.release_sensitivities(curve, volume, r["temp_k"], r["p1"], r["p2"], patm, m, co2)
        print("%4d %3d %-5s %6.2f %8.4f %7.4f %6.4f %5.3f %8.1f %10.3f %7.3f %7.3f"
              % (r["release"], r["level"], r["kind"], r["requested"], r["t"], r["p1"], r["p2"], m,
                 1000 * res, 1000 * res / s2, 1000 * r.get("noise1", float("nan")), 1000 * r.get("noise2", float("nan"))))
    extrapolated = [(r, 1000 * (r["p_inf"] - r["p2"])) for r in valid if math.isfinite(r["p_inf"])]
    if extrapolated:
        print("P_inf (extrapolated by the firmware from the recovery of each wait) minus the P2 read, mbar: mean %+.2f, by wait:"
              % statistics.mean(d for _, d in extrapolated))
        print("  " + " ".join("%d:%+.1f(tau %.0f)" % (r["release"], d, r["tau"]) for r, d in extrapolated))
    slopes = [(r["release"], r["end_slope"]) for r in valid if math.isfinite(r["end_slope"])]
    if slopes:
        print("slope of the last 60 s of each wait, mbar/min (negative = falling: a leak, or the gas still cooling at ~10 mbar/K):")
        print("  " + " ".join("%d:%+.2f" % x for x in slopes))
    print("firmware: chi-square %s, drift %s +- %s %%"
          % (meta.get("chi2_reduced", ""), meta["drift_pct"], meta["drift_se_pct"]))
    fw = [number(meta.get(k)) for k in ("fit_K_s", "fit_beta")]
    if all(math.isfinite(x) for x in fw):
        fw_curve = vm.make_curve(fw[0], fw[1], result["p_min"], result["p_max"])
        diffs = [100 * (vm.tau_ref(fw_curve, p, patm) / vm.tau_ref(curve, p, patm) - 1) for p in (1.8, 1.2, 0.8)]
        print("firmware fit: k %.3f, beta %.4f; tau differs %+.4f / %+.4f / %+.4f %% at 1.8 / 1.2 / 0.8 bar"
              % (fw[0], fw[1], *diffs))
    for p in (0.5, 1.0, 1.9):
        print("  %.1f bar: tau %.2f s (100 L air), CO2/air %.4f" % (p, vm.tau_ref(curve, p, patm),
                                                                   vm.co2_air_ratio(curve["beta"], p, patm)))


def selftest():
    """Synthetic test: known curve, cooling exponent 1.35, noise 0.15 mbar on each settled
    pressure (the P2 of a release is the P1 of the next), 0.5 mbar on P_close, 2 ms on t."""
    random.seed(1)
    truth = vm.make_curve(82.0, 0.9, 0.0, 3.0)
    volume, patm, temp_k, exponent = 129.6, 0.928573, 293.15, 1.35

    def release(p1, seconds):
        # Integrate t(P2) = k S exactly as the model, solving for P2 by bisection.
        lo, hi = 0.0, p1
        for _ in range(60):
            mid = 0.5 * (lo + hi)
            t = truth["k"] * vm.release_integral(volume, temp_k, p1, mid, patm, exponent, False, truth["beta"])
            lo, hi = (lo, mid) if t < seconds else (mid, hi)
        p2 = 0.5 * (lo + hi)
        p_close = (p1 + patm) * ((p2 + patm) / (p1 + patm)) ** exponent - patm
        return p2, p_close

    releases, p, n = [], 1.9, 0
    noise = random.gauss(0, 0.00015)  # reading of the current settled pressure
    for level, target in enumerate([1.5, 1.1, 0.8, 0.5, 0.25, None]):
        for d in (2, 0.5, 8, 1, 4, 2):
            p2, pc = release(p, d)
            n += 1
            next_noise = random.gauss(0, 0.00015)
            releases.append({"release": n, "level": level + 1, "kind": "level", "requested": d,
                             "t": d + random.gauss(0, 0.002), "p1": p + noise,
                             "p_close": pc + random.gauss(0, 0.0005), "p2": p2 + next_noise, "temp_k": temp_k,
                             "noise1": 0.00015, "noise2": 0.00015})
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
                         "p1": p + noise, "p_close": pc, "p2": p2 + next_noise, "temp_k": temp_k,
                         "noise1": 0.00015, "noise2": 0.00015})
        p, noise = p2, next_noise
    result, valid = fit(releases, volume, patm, False)
    curve = vm.make_curve(result["k"], result["beta"], 0.0, 3.0)
    errors = [100 * (vm.tau_ref(curve, q, patm) / vm.tau_ref(truth, q, patm) - 1) for q in (1.8, 1.2, 0.8, 0.5, 0.3)]
    s = summary(result, valid, volume, patm, False)
    print(f"selftest: {len(valid)} releases from 1.9 to {p:.2f} bar (the first one is left out); "
          f"tau error at 1.8/1.2/0.8/0.5/0.3 bar: " + " / ".join(f"{e:+.3f}%" for e in errors) +
          f"; shape {s['shape_rms_pct']:.3f}% rms; "
          f"drift {s['drift_pct']:+.3f}% +- {s['drift_se_pct']:.3f}%; reduced chi-square {s['chi2']:.2f}")
    ok = max(abs(e) for e in errors) < 0.6 and 0.3 < s["chi2"] < CHI_SQUARE_LIMIT
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", nargs="?")
    parser.add_argument("--test", help="test number in the CSV (default: all)")
    parser.add_argument("--keep-first", action="store_true", help="keep the first release in the fit (the firmware leaves it out)")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    global KEEP_FIRST
    KEEP_FIRST = args.keep_first
    if args.selftest:
        return selftest()
    if not args.csv:
        parser.error("give the CSV file or --selftest")
    tests = load(args.csv)
    for name, test in tests.items():
        if args.test is None or args.test == name:
            report(name, test)
    return 0


if __name__ == "__main__":
    sys.exit(main())
