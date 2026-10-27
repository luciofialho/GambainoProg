"""Bench test of the Calibration Feedback tab (docs/feedback.md).

Line-by-line mirror of include/FeedbackMath.h (keep both in step), and checks of the numbers the
design rests on: vents until the air in the headspace is below 5%, the leak fit against a plain
least squares, false "leak-like" windows with sensor noise, detection of a real loss and the
temperature correction.
Run: python tests/feedback_bench.py
"""
import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from vent_model import co2_z  # noqa: E402

AIR_FRACTION_LIMIT = 0.05
EXPONENT_RMSE_LIMIT = 0.002
EXPONENT_MIN_COUNT = 30
RING = 32
LEAK_SLOPE_LIMIT = -0.05
LEAK_SIGMAS = 3.0
LEAK_WINDOWS = 3


def vent_kept_fraction(p_on_gauge, p_after_gauge, patm, t_kelvin):
    p_on, p_after = p_on_gauge + patm, p_after_gauge + patm
    if not (p_on > 0 and p_after > 0 and p_after < p_on and t_kelvin > 0):
        return 1.0
    kept = (p_after / co2_z(p_after, t_kelvin)) / (p_on / co2_z(p_on, t_kelvin))
    return kept if 0 < kept < 1 else 1.0


def air_fraction(kept_product, start_abs, start_kelvin, now_abs, now_kelvin):
    if not (kept_product > 0 and start_abs > 0 and start_kelvin > 0 and now_abs > 0 and now_kelvin > 0):
        return 1.0
    now_moles = now_abs / (co2_z(now_abs, now_kelvin) * now_kelvin)
    return min(1.0, kept_product * (start_abs / start_kelvin) / now_moles)


def median(values):
    if not values:
        return math.nan
    s = sorted(values[:RING])
    n = len(s)
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


class LeakFit:
    def __init__(self):
        self.n = self.sx = self.sy = self.sxx = self.sxy = self.syy = self.st = self.y0 = 0.0

    def add(self, minutes, p_abs, t_kelvin):
        y_abs = p_abs / t_kelvin
        if self.n == 0:
            self.y0 = y_abs
        y = y_abs - self.y0
        self.n += 1
        self.sx += minutes
        self.sy += y
        self.sxx += minutes * minutes
        self.sxy += minutes * y
        self.syy += y * y
        self.st += t_kelvin

    def slope(self):
        den = self.n * self.sxx - self.sx * self.sx
        if self.n < 3 or not den > 0:
            return None
        b = (self.n * self.sxy - self.sx * self.sy) / den
        a = (self.sy - b * self.sx) / self.n
        sse = max(0.0, self.syy - a * self.sy - b * self.sxy)
        scale = 1000.0 * self.st / self.n
        return b * scale, math.sqrt(sse / (self.n - 2) * self.n / den) * scale


def leak_like(slope, sigma):
    return slope < LEAK_SLOPE_LIMIT and slope < -LEAK_SIGMAS * sigma


# ----- checks -----

PATM = 0.93
T = 293.15


def vents_until_clean(set_point, drop_percent, start_gauge=0.0):
    """Vents until air < 5%: the fermentation raises the pressure from start_gauge (air only) to the
    firing pressure; each vent drops it by drop_percent of the gauge pressure."""
    f = 1 - drop_percent / 100
    p_fire, p_after = set_point / math.sqrt(f), set_point * math.sqrt(f)
    product, vents = 1.0, 0
    while True:
        product *= vent_kept_fraction(p_fire, p_after, PATM, T)
        vents += 1
        if air_fraction(product, start_gauge + PATM, T, p_after + PATM, T) < AIR_FRACTION_LIMIT:
            return vents


def window(slope_mbar_min, noise_mbar, rho=0.0, temp_drift_k_h=0.0, minutes=60, sample_s=30, rng=random):
    fit, e = LeakFit(), 0.0
    p0 = 0.5 + PATM
    for i in range(int(minutes * 60 / sample_s)):
        t_min = i * sample_s / 60
        tk = T + temp_drift_k_h * t_min / 60
        e = rho * e + math.sqrt(1 - rho * rho) * rng.gauss(0, noise_mbar / 1000)
        p_abs = (p0 + slope_mbar_min / 1000 * t_min) * tk / T + e  # gas follows T; loss at constant T
        fit.add(t_min, p_abs, tk)
    return fit.slope()


def main():
    failures = 0

    def check(cond, text):
        nonlocal failures
        print(("ok   " if cond else "FAIL ") + text)
        failures += 0 if cond else 1

    for sp in (1.0, 0.5):
        n = vents_until_clean(sp, 6.0)
        print(f"vents until air < 5% at {sp} bar, 6% drop, start 0 bar: {n}")
    check(60 <= vents_until_clean(1.0, 6.0) <= 80, "about 70 vents at 1 bar (docs/feedback.md)")
    check(105 <= vents_until_clean(0.5, 6.0) <= 130, "about 120 vents at 0.5 bar")

    check(median([3, 1, 2]) == 2 and median([4, 1, 3, 2]) == 2.5 and math.isnan(median([])), "median")

    rng = random.Random(1)
    xs = [i * 0.5 for i in range(120)]
    ys = [0.002 - 0.00003 * x + rng.gauss(0, 1e-5) for x in xs]
    fit = LeakFit()
    for x, y in zip(xs, ys):
        fit.add(x, y * T, T)
    mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs)
    check(abs(fit.slope()[0] - b * 1000 * T) < 1e-9, "leak fit slope = plain least squares")

    trials = 2000
    for noise, rho in ((0.5, 0.0), (0.5, 0.9), (1.0, 0.95)):
        false = sum(leak_like(*window(0.0, noise, rho, rng=rng)) for _ in range(trials))
        rate = false / trials
        print(f"no leak, noise {noise} mbar, rho {rho}: {false}/{trials} windows leak-like")
        check(rate ** LEAK_WINDOWS < 1e-4, f"false 'possible leak' ({LEAK_WINDOWS} in a row) < 1e-4 "
              f"(noise {noise}, rho {rho}: {rate ** LEAK_WINDOWS:.1e})")
    detected = sum(leak_like(*window(-0.1, 0.5, 0.9, rng=rng)) for _ in range(trials))
    print(f"leak -0.1 mbar/min, noise 0.5 mbar, rho 0.9: {detected}/{trials} windows leak-like")
    check(detected / trials > 0.95, "a -0.1 mbar/min loss is leak-like in > 95% of the windows")
    slope, _ = window(0.0, 0.0, temp_drift_k_h=-0.5, rng=rng)
    check(abs(slope) < 1e-6, "cooling of 0.5 K/h without loss: slope ~0 (T correction)")

    print("RESULT:", "FAIL" if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
