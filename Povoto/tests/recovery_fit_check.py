"""Check of the exponential recovery fit (PressureWait::fitRecovery, mirrored by vent_model.fit_recovery).

Usage:  python tests/recovery_fit_check.py [settle.csv]
Synthetic recoveries with noise: p_inf, tau and amp must come back. With the file of the removed settle test (docs/venting-test.md,
section 5; tag vent-d-com-settle-2026-10-22): the fit of the first 120, 180 and 300 s of each wait against the exp + linear decline fit of the 20 min.
"""
import csv
import random
import sys

import vent_model as vm


def synthetic():
    random.seed(3)
    ok = True
    for p_inf, amp, tau in ((1.9000, 0.0309, 49.0), (1.8710, 0.0077, 65.0), (1.1229, 0.0635, 43.0), (0.5634, 0.0481, 34.0)):
        t = [1.0 * i + 0.5 for i in range(240)]
        p = [p_inf - amp * __import__("math").exp(-x / tau) + random.gauss(0, 0.00012) for x in t]
        r = vm.fit_recovery(t, p)
        good = r is not None and abs(r[0] - p_inf) < 0.0004 and abs(r[1] / tau - 1) < 0.15 and abs(r[2] / amp - 1) < 0.15
        ok = ok and good
        print("  p_inf %.4f amp %.4f tau %4.1f -> %s  %s" % (p_inf, amp, tau, "None" if r is None else "p_inf %.4f tau %.1f amp %.4f" % r, "ok" if good else "FAIL"))
    return ok


def settle_file(path):
    n = lambda s: float(s.replace(",", ".")) if s else float("nan")
    waits = {}
    for r in csv.DictReader(open(path, encoding="utf-8"), delimiter=";"):
        waits.setdefault(int(r["release"]), []).append(r)
    print("settle test file (extrapolated P_inf - mean of the last 60 s, mbar; the file has a decline of ~1 mbar/min, so the numbers are biased by it):")
    for k in sorted(waits):
        if k == 0:
            continue
        v = waits[k]
        t = [n(r["seconds_since_close"]) for r in v]
        p = [n(r["pressure_bar"]) for r in v]
        last = [y for x, y in zip(t, p) if x >= 1140]
        ref = sum(last) / len(last)
        cells = []
        for h in (120, 180, 300):
            tt = [x for x in t if x <= h]
            r = vm.fit_recovery(tt, p[:len(tt)])
            cells.append("%ds %s" % (h, "None" if r is None else "%+.1f (tau %.0f)" % (1000 * (r[0] - ref), r[1])))
        print("  release %d: " % k + "  ".join(cells))


if __name__ == "__main__":
    print("synthetic recoveries:")
    ok = synthetic()
    if len(sys.argv) > 1:
        settle_file(sys.argv[1])
    print("RESULT:", "PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)
