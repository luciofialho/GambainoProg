"""Python mirror of include/NucleationEstimate.h (nucleation window estimate,
docs/dissolved-co2.md section 8). Same constants and formulas, so the bench
(tests/dissolved_co2_bench.py) tests the firmware's mathematics. Keep both in step.

Units: gas in mol, times in ms (as the firmware), slope in mol/s.
"""
import math

BLOCKS = 18
BLOCK_MS = 600000
MIN_SAMPLES = 60
SETTLE_RUN = 3
SETTLE_PEAK_FRACTION = 0.10
SETTLE_SIGMAS = 2.0
NOISY_PEAK_SIGMAS = 4.0
VENTED_PRESSURE_BAR = 0.1
BLOCK_SIGMA_FACTOR = math.sqrt(2.0)

OK, NOT_SETTLED, NOISY_BASELINE, VENTED, NO_BASELINE, INTERRUPTED = (
    "ok", "not settled", "noisy baseline", "vented", "no baseline", "interrupted")
IDLE, TASK, OBSERVING = 0, 1, 2


def fit_reference(samples):
    """samples: list of (ms, gas, external_step), oldest first (the CO2 buffer)."""
    n = len(samples)
    if n < MIN_SAMPLES:
        return {"ok": False, "reason": f"{n} samples", "slope": math.nan, "sigma": math.nan}
    if any(ext or not math.isfinite(g) for _, g, ext in samples):
        return {"ok": False, "reason": "external step", "slope": math.nan, "sigma": math.nan}
    t0 = samples[0][0]
    xs = [(ms - t0) / 1000.0 for ms, _, _ in samples]
    ys = [g for _, g, _ in samples]
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    if not sxx > 0.0:
        return {"ok": False, "reason": "no time span", "slope": math.nan, "sigma": math.nan}
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    ss = sum(((y - my) - slope * (x - mx)) ** 2 for x, y in zip(xs, ys))
    sigma = math.sqrt(ss / (n - 2))
    return {"ok": True, "reason": "", "slope": slope, "sigma": sigma}


class Estimator:
    def __init__(self):
        self.reset_all()

    def reset_all(self):
        self.stage = IDLE
        self.result = None

    def observed_minutes(self, now):
        return (now - self.end_ms) / 60000.0 if self.stage == OBSERVING else 0.0

    # --- inputs ---------------------------------------------------------
    def task_started(self, now, gas, ref, gauge_pressure):
        if self.stage == OBSERVING:
            self._interrupt("new task")
            self._finish(now)
        self.ref = ref
        self.start_ms, self.start_gas = now, gas
        self.vented = gauge_pressure < VENTED_PRESSURE_BAR
        self.interrupted, self.interrupt_reason = False, ""
        self.stage = TASK

    def other_task_started(self, now):
        if self.stage == OBSERVING:
            self._interrupt("other task")
            self._finish(now)
        elif self.stage == TASK:
            self.stage = IDLE

    def task_cancelled(self):
        if self.stage == TASK:
            self.stage = IDLE

    def disturb(self, reason, now):
        if self.stage == TASK:
            self._interrupt(reason)
        elif self.stage == OBSERVING:
            self._interrupt(reason)
            self._finish(now)

    def task_ended(self, now, gas, configured_minutes, gauge_pressure):
        if self.stage != TASK:
            return
        if gauge_pressure < VENTED_PRESSURE_BAR:
            self.vented = True
        self.end_ms = now
        self.configured_min = configured_minutes
        self.task_min = (now - self.start_ms) / 60000.0
        self.during_task = (gas - self.start_gas) - self.ref["slope"] * ((now - self.start_ms) / 1000.0) \
            if self.ref["ok"] else 0.0
        self.block_start_ms, self.block_start_gas = now, gas
        self.blocks = []
        self.inside = self.after = 0.0
        self.stage = OBSERVING
        if not self.ref["ok"] or self.vented or self.interrupted:
            self._finish(now)

    def sample(self, now, gas, gauge_pressure, external_step):
        if not math.isfinite(gas):
            return
        if self.stage == TASK:
            if gauge_pressure < VENTED_PRESSURE_BAR:
                self.vented = True
        elif self.stage == OBSERVING:
            if external_step:
                self._interrupt("external step")
                self._finish(now)
                return
            if now - self.block_start_ms >= BLOCK_MS:
                self._close_block(now, gas)

    def take_result(self):
        r, self.result = self.result, None
        return r

    # --- internals ------------------------------------------------------
    def _interrupt(self, reason):
        if not self.interrupted:
            self.interrupted, self.interrupt_reason = True, reason[:31]

    def _close_block(self, now, gas):
        seconds = (now - self.block_start_ms) / 1000.0
        excess = (gas - self.block_start_gas) - self.ref["slope"] * seconds
        frm = (self.block_start_ms - self.end_ms) / 60000.0
        to = (now - self.end_ms) / 60000.0
        fraction = min(max((self.configured_min - frm) / (to - frm), 0.0), 1.0)
        self.inside += excess * fraction
        self.after += excess * (1.0 - fraction)
        self.blocks.append(excess)
        self.block_start_ms, self.block_start_gas = now, gas
        if len(self.blocks) >= BLOCKS:
            self._finish(now)

    def _finish(self, now):
        ref = self.ref
        peak, peak_index = 0.0, 0
        for i, b in enumerate(self.blocks):
            if b > peak:
                peak, peak_index = b, i
        sigma_block = BLOCK_SIGMA_FACTOR * ref["sigma"] if ref["ok"] else math.nan
        settled, window = False, math.nan
        if ref["ok"] and len(self.blocks) == BLOCKS and peak > 0.0:
            limit = max(SETTLE_PEAK_FRACTION * peak, SETTLE_SIGMAS * sigma_block)
            for j in range(peak_index + 1, len(self.blocks) - SETTLE_RUN + 1):
                if all(b < limit for b in self.blocks[j:j + SETTLE_RUN]):
                    settled, window = True, j * (BLOCK_MS / 60000.0)
                    break
        r = {
            "task_min": self.task_min, "configured_min": self.configured_min,
            "during_task": self.during_task, "inside": self.inside, "after": self.after,
            "baseline_slope": ref["slope"] if ref["ok"] else math.nan,
            "block_sigma": sigma_block, "peak_block": peak,
            "observed_min": (now - self.end_ms) / 60000.0, "estimated_min": math.nan, "reason": "",
        }
        if self.interrupted:
            r["quality"], r["reason"] = INTERRUPTED, self.interrupt_reason
        elif not ref["ok"]:
            r["quality"], r["reason"] = NO_BASELINE, ref["reason"]
        elif self.vented:
            r["quality"] = VENTED
        elif not (peak > 0.0 and peak >= NOISY_PEAK_SIGMAS * sigma_block):
            r["quality"] = NOISY_BASELINE
        elif not settled:
            r["quality"] = NOT_SETTLED
        else:
            r["quality"], r["estimated_min"] = OK, window
        self.result = r
        self.stage = IDLE
