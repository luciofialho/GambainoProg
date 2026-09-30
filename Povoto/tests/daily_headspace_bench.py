"""[DAILY-HS] Bench test of the 24-hour headspace average (docs/spec_headspace_24h.md, section 8).

Mirrors the firmware logic in PressureControl.cpp: hourly bins keyed by
hourId = epoch / 3600, evaluation of the last 24 hours, states valid/hold/ema,
selection of the applied value, the EMA, rebase and persistence across a boot.
Run: python tests/daily_headspace_bench.py
"""
import math
import random

BINS = 24          # DAILY_HS_BINS
MIN_HOURS = 18     # DAILY_HS_MIN_HOURS
RELIEF_S = 4 * 60  # one relief every 4 minutes


class DailyHeadspace:
    def __init__(self):
        self.bins = [[0, 0, 0.0] for _ in range(BINS)]  # hourId, count, sum
        self.held = math.nan
        self.value = math.nan
        self.hours = 0
        self.state = "ema"

    def evaluate(self, epoch):
        if epoch == 0:
            return
        hour_now = epoch // 3600
        total, hours = 0.0, 0
        for hour_id, count, total_sum in self.bins:
            if count == 0 or hour_id > hour_now or hour_id + (BINS - 1) < hour_now:
                continue
            total += total_sum / count
            hours += 1
        self.hours = hours
        self.value = total / hours if hours else math.nan
        if hours >= MIN_HOURS:
            self.held = self.value
            self.state = "valid"
        else:
            self.state = "hold" if math.isfinite(self.held) else "ema"

    def accumulate(self, epoch, headspace):
        if epoch == 0:
            return
        hour_id = epoch // 3600
        b = self.bins[hour_id % BINS]
        if b[0] != hour_id:
            b[:] = [hour_id, 0, 0.0]
        b[1] += 1
        b[2] += headspace
        self.evaluate(epoch)

    def rebase(self, delta, epoch):
        for b in self.bins:
            if b[1] > 0:
                b[2] += delta * b[1]
        if math.isfinite(self.held):
            self.held += delta
        if math.isfinite(self.value):
            self.value += delta
        self.evaluate(epoch)

    def restore(self, epoch):
        """Boot: bins and held value come from NVS; the cache starts empty."""
        self.value, self.hours = math.nan, 0
        self.state = "hold" if math.isfinite(self.held) else "ema"
        self.evaluate(epoch)

    def selected(self, ema):
        if self.state == "valid":
            return self.value
        if self.state == "hold":
            return self.held
        return ema


REBASE_T = 30 * 3600  # dump: the real headspace drops 1 L here


def true_headspace(t_s):
    base = 30.0 if t_s < REBASE_T else 29.0
    return base + math.sin(2 * math.pi * t_s / 86400.0)


def main():
    random.seed(160)
    start = 1_790_000_000 + 17 * 60  # arbitrary epoch, 17 min past an hour
    daily = DailyHeadspace()
    ema, alpha = math.nan, 0.05
    rows = []

    def relief(t_s, jump_s=0):
        nonlocal ema, alpha
        measured = true_headspace(t_s) + random.gauss(0.0, 0.15)
        epoch = start + t_s + jump_s
        daily.accumulate(epoch, measured)
        if math.isnan(ema):
            ema = measured
        else:
            ema += alpha * (measured - ema)
            alpha = max(0.05, alpha / (1 + alpha))
        rows.append((t_s / 3600.0, measured, ema, daily.value, daily.hours, daily.state,
                     daily.selected(ema)))

    # 1) 40 h of reliefs, with a -1 L rebase (dump) at 30 h.
    rebase_before = rebase_after = None
    t = 0
    while t <= 40 * 3600:
        if rebase_before is None and t >= REBASE_T:
            rebase_before = daily.value
            daily.rebase(-1.0, start + t)
            rebase_after = daily.value
        relief(t)
        t += RELIEF_S

    first_valid = next(r for r in rows if r[5] == "valid")
    states_before = {r[5] for r in rows if r[0] < first_valid[0]}
    valid_18_24 = [r[3] for r in rows if first_valid[0] <= r[0] < 24.0]
    window = [r for r in rows if 24.0 <= r[0] < 30.0]
    daily_24_30 = [r[3] for r in window]
    ema_24_30 = [r[2] for r in window]
    after = [r[3] for r in rows if 30.0 <= r[0] <= 40.0]

    print("1) 40 h, reliefs every 4 min, Vh = 30 + sin(2*pi*t/24h) + N(0, 0.15); start 17 min past the hour")
    print(f"   states before valid: {sorted(states_before)}; first 'valid' at {first_valid[0]:.2f} h "
          f"(18 hours with samples, the first one partial), daily {first_valid[3]:.3f} L")
    print(f"   {first_valid[0]:.1f}-24 h (window shorter than a day): daily {min(valid_18_24):.3f} .. "
          f"{max(valid_18_24):.3f} L")
    print(f"   24-30 h: daily {min(daily_24_30):.3f} .. {max(daily_24_30):.3f} L "
          f"(max |error| {max(abs(v - 30) for v in daily_24_30):.3f} L)")
    print(f"   24-30 h: EMA   {min(ema_24_30):.3f} .. {max(ema_24_30):.3f} L (for comparison)")
    print(f"2) rebase -1 L at 30 h: daily {rebase_before:.3f} -> {rebase_after:.3f} L "
          f"(delta {rebase_after - rebase_before:+.3f}); 30-40 h: {min(after):.3f} .. {max(after):.3f} L")

    # 3) Boot at 40 h with the persisted bins: same hour, stays valid.
    end_epoch = start + 40 * 3600
    persisted = DailyHeadspace()
    persisted.bins = [b[:] for b in daily.bins]
    persisted.held = daily.held
    persisted.restore(end_epoch)
    print(f"3) boot at 40 h: state '{persisted.state}', daily {persisted.value:.3f} L, "
          f"{persisted.hours} h (before boot: '{daily.state}', {daily.value:.3f} L)")

    # 4) 10 h without reliefs: hourly evaluations (as the Cold log does).
    value_at_stop = daily.value
    states, last_valid = [], math.nan
    for h in range(1, 11):
        daily.evaluate(end_epoch + h * 3600)
        if daily.state == "valid":
            last_valid = daily.value
        states.append((h, daily.hours, daily.state, daily.value))
    print(f"4) 10 h without reliefs (daily at stop {value_at_stop:.3f} L); hours since stop: hours/state/daily:")
    print("   " + ", ".join(f"{h}h:{n}/{s}/{v:.2f}" for h, n, s, v in states))
    applied_in_hold = daily.selected(math.nan)
    print(f"   applied in 'hold' = held {applied_in_hold:.3f} L (last valid value {last_valid:.3f} L)")

    # 5) Clock jump of -3 h, then 6 h of reliefs.
    rows.clear()
    t0 = 50 * 3600
    for k in range(0, 6 * 3600, RELIEF_S):
        relief(t0 + k, jump_s=-3 * 3600)
    values = [r[3] for r in rows if math.isfinite(r[3])]
    applied = [r[6] for r in rows]
    print(f"5) clock -3 h, 6 h of reliefs: states {sorted({r[5] for r in rows})}, "
          f"daily {min(values):.3f} .. {max(values):.3f} L, applied {min(applied):.3f} .. {max(applied):.3f} L, "
          f"hours {min(r[4] for r in rows)} .. {max(r[4] for r in rows)}")

    ok = (states_before <= {"ema"} and 16.5 <= first_valid[0] <= 18.5
          and max(abs(v - 30) for v in daily_24_30) <= 0.05
          and abs((rebase_after - rebase_before) + 1.0) < 1e-6
          and all(abs(v - 29) <= 0.05 for v in after)
          and persisted.state == "valid"
          and states[-1][2] == "hold" and applied_in_hold == last_valid
          and all(27.5 < v < 30.5 for v in values + applied))
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
