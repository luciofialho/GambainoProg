"""Bench test of the dissolved-CO2 states (docs/dissolved-co2.md).

Mirrors PressureControl.cpp: the four states, the gas-phase rate over the gCO2
window, the hybrid half-life with its deadband, the hop-task rule and the armed
half-life. The fermenter is simulated: CO2 generation, transfer between liquid
and headspace with a time constant, reliefs at a threshold, temperature steps.
The "measured" gas phase is ejected + headspace, as in the firmware.
Run: python tests/dissolved_co2_bench.py
"""
import math
import random
import statistics

R = 0.0831446      # L.bar/(mol.K)
PATM = 0.93
M = 44.01
DT = 60            # s, one step = one CO2 sample
# Firmware constants
EXIT, RETURN, RETURN_ARMED = 0.3, 0.5, 0.3
EXIT_HOLD, RETURN_HOLD, ARMED_HOLD = 180 * 60, 360 * 60, 60 * 60
ARMED_MAX_AGE = 7 * 86400
MIN_SAMPLES, HIST = 60, 71
TASK_QUIET = 80 * 60
STEP_GPLD = 50.0
DEADBAND = 0.05
BOOT_WAIT = 120
HENRY_WIN = 30     # samples (60 s): 30-min mean of Henry


def henry(p, t, v):
    tk = t + 273.15
    kh = 0.0334 * math.exp(2400 * (1 / tk - 1 / 298.15))
    return kh * (p + PATM) * 0.986923 * v


class Fermenter:
    """Physical side. Pressures in bar gauge, headspace CO2 above the start."""

    def __init__(self, beer=100.0, headspace=30.0, temp=20.0, threshold=1.0, f=0.938, tau_h=2.0):
        # tau_h: liquid-gas transfer time constant (batch 160, 30/09: ~2 h)
        self.v, self.vh, self.t, self.thr, self.f = beer, headspace, temp, threshold, f
        self.tau = tau_h * 3600
        self.n = threshold * 0.97 * headspace / (R * (temp + 273.15))  # gas moles
        self.dissolved = henry(self.p, temp, beer)
        self.ejected = 0.0
        self.reliefs = 0
        self.gen_gpld = 0.0      # generation, g/L/d
        self.extra_release = 0.0  # mol/s forced out of the liquid (nucleation)
        # Measurement error of the headspace CO2: the firmware uses the beer
        # temperature, the gas follows the cooling cycles; plus sensor noise.
        self.gas_temp_offset = 0.0
        self.noise_bar = 0.0

    @property
    def p(self):
        return self.n * R * (self.t + 273.15) / self.vh

    @p.setter
    def p(self, value):
        self.n = value * self.vh / (R * (self.t + 273.15))

    def gas_mols(self):
        return self.n

    def step(self, relief_enabled=True):
        # The CO2 is produced in the beer and leaves it by transfer to the gas.
        self.dissolved += self.gen_gpld * self.v / M / 86400 * DT
        eq = henry(self.p, self.t, self.v)
        transfer = (self.dissolved - eq) * (1 - math.exp(-DT / self.tau))
        forced = min(self.extra_release * DT, self.dissolved - transfer)
        self.dissolved -= transfer + forced
        self.n += transfer + forced
        if relief_enabled and self.p > self.thr:
            n_after = self.n * self.f
            self.ejected += self.n - n_after
            self.n = n_after
            self.reliefs += 1

    def measured_gas(self):
        tk = self.t + 273.15
        p_gas = self.n * R * (tk + self.gas_temp_offset) / self.vh + random.gauss(0.0, self.noise_bar)
        return self.ejected + p_gas * self.vh / (R * tk)

    def true_total(self):
        return self.ejected + self.gas_mols() + self.dissolved


class Firmware:
    """Logic of PressureControl.cpp (dissolved-CO2 part)."""

    def __init__(self, fer, state=1):
        self.fer = fer
        self.state = state          # 0 half-life, 1 equilibrium, 2 initial, 3 armed
        self.d = fer.dissolved
        self.hs = []                # (now, Henry), last HENRY_WIN samples
        self.gas_hist = []          # (now, gas): displayed gCO2/L/d = gas leaving the beer
        self.display = math.nan
        self.base = None
        self.hist = []
        self.since = None
        self.hold = 0
        self.rate = math.nan
        self.last_relief = fer.reliefs
        self.now = 10**6            # s since boot
        self.task = 0
        self.last_task = None
        self.armed_at = 0
        self.epoch = 1_800_000_000
        self.events = []

    def set_state(self, nxt, why):
        if nxt == self.state:
            return
        self.events.append((self.now, self.state, nxt, why))
        self.state = nxt
        self.armed_at = self.epoch if nxt == 3 else 0
        self.since, self.hold, self.base = None, 0, None

    def reboot(self):
        self.now, self.base, self.hist, self.since, self.hold = 0, None, [], None, 0
        self.hs, self.gas_hist = [], []

    def fermentables_added(self):
        if self.state == 0:
            self.set_state(3, 'fermentables added')
        elif self.state == 3:
            self.armed_at = self.epoch

    def total(self):
        return self.fer.measured_gas() + self.d

    def update_dissolved(self):
        fer = self.fer
        if self.state in (1, 2):
            self.d = sum(h for _, h in self.hs) / len(self.hs) if self.hs else henry(fer.p, fer.t, fer.v)
            self.base = None
            return
        if self.now < BOOT_WAIT:
            self.base = None
            return
        gas = fer.measured_gas()
        h = henry(fer.p, fer.t, fer.v)
        hop = self.task in (4, 5)
        other = self.task not in (0, 4, 5)
        if self.base is not None and not other:
            dg = gas - self.base
            if abs(dg) < DEADBAND:
                return
            if dg > 0:
                rel = self.d if hop else max(0.0, self.d - h)
                self.d -= min(dg, rel)
            elif dg < 0:
                self.d += min(-dg, max(0.0, h - self.d))
            self.d = max(0.0, self.d)
        self.base = gas

    def events_check(self):
        if self.state == 2 and self.fer.reliefs >= 3:
            self.set_state(1, 'reliefs started')
        if self.state == 3 and self.epoch - self.armed_at > ARMED_MAX_AGE:
            self.set_state(0, 'armed expired')

    def rate_sample(self, gas):
        fer = self.fer
        self.gas_hist.append((self.now, gas))
        self.gas_hist = self.gas_hist[-HIST:]
        h = self.gas_hist
        n = len(h)
        if n >= 5:
            k = 1 if n < 9 else min(n // 3, 10)
            a = sum(v for _, v in h[:k]) / k
            b = sum(v for _, v in h[-k:]) / k
            ta = sum(t for t, _ in h[:k]) / k
            tb = sum(t for t, _ in h[-k:]) / k
            self.display = (b - a) * M * 86400 / (fer.v * (tb - ta))
        self.hs.append((self.now, henry(fer.p, fer.t, fer.v)))
        self.hs = self.hs[-HENRY_WIN:]

    def sample(self):
        fer = self.fer
        if self.now < BOOT_WAIT:
            return
        gas = fer.measured_gas()
        self.rate_sample(gas)
        ext = False
        if self.hist and fer.reliefs == self.last_relief:
            ext = (gas - self.hist[-1][1]) * M * 86400 / (fer.v * DT) > STEP_GPLD
        self.last_relief = fer.reliefs
        self.hist.append((self.now, gas, ext))
        self.hist = self.hist[-HIST:]
        quiet = self.last_task is None or self.now - self.last_task >= TASK_QUIET
        rate = math.nan
        if len(self.hist) >= MIN_SAMPLES and self.task == 0 and quiet and not any(h[2] for h in self.hist):
            k = min(len(self.hist) // 3, 10)
            a = sum(h[1] for h in self.hist[:k]) / k
            b = sum(h[1] for h in self.hist[-k:]) / k
            ta = sum(h[0] for h in self.hist[:k]) / k
            tb = sum(h[0] for h in self.hist[-k:]) / k
            rate = (b - a) * M * 86400 / (fer.v * (tb - ta))
        self.rate = rate
        armed = self.state == 3
        ret = RETURN_ARMED if armed else RETURN
        nxt, hold = self.state, 0
        if math.isfinite(rate):
            if self.state == 1 and rate < EXIT:
                nxt, hold = 0, EXIT_HOLD
            elif self.state in (0, 3) and rate > ret:
                nxt, hold = 1, ARMED_HOLD if armed else RETURN_HOLD
        if nxt == self.state:
            self.since = None
            return
        self.since = self.since if self.since is not None else self.now
        if self.now - self.since >= hold:
            self.set_state(nxt, 'rate')

    def tick(self):
        self.now += DT
        self.epoch += DT
        self.events_check()
        self.update_dissolved()
        self.sample()


def run(fw, seconds, relief=True, each=None):
    for _ in range(int(seconds // DT)):
        fw.fer.step(relief and fw.task == 0)
        fw.tick()
        if each:
            each(fw)


def pts(mols, v):
    return mols * 0.444 * 100 / v


results = []


def check(name, cond, detail):
    results.append((name, bool(cond)))
    print(f"{'PASS' if cond else 'FAIL'}  {name}: {detail}")


# 1. Active fermentation stays in equilibrium; the end of it goes to half-life.
fer = Fermenter()
fw = Firmware(fer, state=1)
fer.gen_gpld = 8.0
run(fw, 2 * 86400)
check("active stays equilibrium", fw.state == 1 and not fw.events,
      f"rate {fw.rate:.2f} g/L/d, events {fw.events}")
fer.gen_gpld = 0.1
# The beer keeps releasing its supersaturation for some hours (tau ~2 h), then
# the 3-h exit hold runs.
t_stop = fw.now
end_total = []
run(fw, 16 * 3600, each=lambda f: end_total.append(f.fer.measured_gas() + f.d))
exit_ev = [e for e in fw.events if e[2] == 0]
check("end of fermentation -> half-life", fw.state == 0 and exit_ev,
      f"after {(exit_ev[0][0] - t_stop) / 3600 if exit_ev else math.nan:.1f} h; counted in 16 h "
      f"{end_total[-1] - end_total[0]:+.3f} mol (generation {0.1 * fer.v / M * 16 / 24:.3f})")

# 2. Cold crash in half-life: absorption is not production (true production 0).
fer = Fermenter(temp=20.0)
fer.gen_gpld = 0.0
fw = Firmware(fer, state=0)
fw.d = fer.dissolved
run(fw, 3600)
t0, s0 = fw.total(), fer.true_total()
for temp in (15, 10, 5, 2):
    fer.t = temp
    run(fw, 12 * 3600)
err = (fw.total() - t0) - (fer.true_total() - s0)
check("cold crash conserved", abs(pts(err, fer.v)) < 0.05 and fw.state == 0,
      f"counted {fw.total() - t0:+.3f} mol, true {fer.true_total() - s0:+.3f} mol ({pts(err, fer.v):+.3f} pt), state {fw.state}")

# 3. Oscillating gas phase (cooling cycles) without generation: no ratchet.
random.seed(1)
fer = Fermenter(temp=5.0)
fer.noise_bar = 0.003
fw = Firmware(fer, state=0)
fw.d = fer.dissolved - 1.0                 # undersaturated after a cold crash
run(fw, 3600)
t0 = fw.total()
k = 0


def oscillate(f):
    global k
    k += 1
    f.fer.gas_temp_offset = 2.0 * math.sin(2 * math.pi * k * DT / 5400)  # 90-min cooling cycle


run(fw, 3 * 86400, each=oscillate)
check("oscillation does not ratchet", abs(pts(fw.total() - t0, fer.v)) < 0.1,
      f"counted {fw.total() - t0:+.3f} mol ({pts(fw.total() - t0, fer.v):+.3f} pt) in 3 days")

# 4. Hop creep 0.2 g/L/d in half-life: counted (slowly), state stays half-life.
fer = Fermenter(temp=20.0)
fw = Firmware(fer, state=0)
fw.d = fer.dissolved
run(fw, 3600)
t0, s0 = fw.total(), fer.true_total()
fer.gen_gpld = 0.2
run(fw, 5 * 86400)
counted, true = fw.total() - t0, fer.true_total() - s0
check("hop creep counted, stays half-life", fw.state == 0 and counted > 0.7 * true,
      f"counted {counted:+.3f} mol of {true:+.3f} true ({counted / true:.0%}), state {fw.state}")

# 5. Dry hopping nucleation inside the task window: not production.
fer = Fermenter(temp=20.0)
fw = Firmware(fer, state=0)
fw.d = fer.dissolved
run(fw, 3600)
t0, s0 = fw.total(), fer.true_total()
fw.task = 4
fer.extra_release = 0.27 / (20 * 60)      # 0.27 mol in 20 min (batch 159)
run(fw, 20 * 60, relief=False)
fer.extra_release = 0.0
run(fw, 20 * 60, relief=False)            # rest of the 10 + 30 min window
fw.task, fw.last_task = 0, fw.now
run(fw, 2 * 86400)                        # reabsorption and reliefs
err = (fw.total() - t0) - (fer.true_total() - s0)
check("nucleation in window not production", abs(pts(err, fer.v)) < 0.05 and fw.state == 0,
      f"counted {fw.total() - t0:+.3f} mol, true {fer.true_total() - s0:+.3f} ({pts(err, fer.v):+.3f} pt)")

# 6. Nucleation longer than the window: the tail becomes production, then debt.
fer = Fermenter(temp=20.0)
fw = Firmware(fer, state=0)
fw.d = fer.dissolved
run(fw, 3600)
t0, s0 = fw.total(), fer.true_total()
fw.task = 4
fer.extra_release = 0.27 / (90 * 60)      # 0.27 mol in 90 min, window 40 min
run(fw, 40 * 60, relief=False)
fw.task, fw.last_task = 0, fw.now
run(fw, 50 * 60)
fer.extra_release = 0.0
peak = fw.total() - t0
run(fw, 2 * 86400)
check("long nucleation: bounded, transient", pts(peak, fer.v) < 0.1 and fw.state == 0,
      f"counted at the end of nucleation {peak:+.3f} mol ({pts(peak, fer.v):+.3f} pt), after 2 days {fw.total() - t0:+.3f}")

# 7. Refermentation without addition: back to equilibrium after ~7 h.
fer = Fermenter(temp=20.0)
fw = Firmware(fer, state=0)
fw.d = fer.dissolved
run(fw, 3 * 3600)
start = fw.now
fer.gen_gpld = 1.5
run(fw, 12 * 3600)
ev = [e for e in fw.events if e[2] == 1]
check("refermentation returns (not armed)", fw.state == 1 and ev,
      f"after {(ev[0][0] - start) / 3600 if ev else math.nan:.1f} h")

# 8. Armed by added fermentables: back to equilibrium after ~2-3 h at 0.5 g/L/d.
fer = Fermenter(temp=20.0)
fw = Firmware(fer, state=0)
fw.d = fer.dissolved
run(fw, 3 * 3600)
fw.fermentables_added()
start = fw.now
fer.gen_gpld = 0.5
run(fw, 12 * 3600)
ev = [e for e in fw.events if e[2] == 1]
check("armed returns sooner", fw.state == 1 and ev and ev[0][1] == 3,
      f"after {(ev[0][0] - start) / 3600 if ev else math.nan:.1f} h")

# 9. Armed without refermentation expires after 7 days.
fer = Fermenter(temp=20.0)
fw = Firmware(fer, state=0)
fw.d = fer.dissolved
fw.fermentables_added()
run(fw, 7 * 86400 + 3600)
check("armed expires", fw.state == 0 and fw.events[-1][3] == 'armed expired', f"events {[e[3] for e in fw.events]}")

# 10. Reboot in half-life keeps the state and the dissolved amount.
fer = Fermenter(temp=20.0)
fw = Firmware(fer, state=0)
fw.d = fer.dissolved - 0.3
run(fw, 3600)
d0 = fw.d
fw.reboot()
run(fw, 3 * 3600)
check("reboot keeps half-life", fw.state == 0 and abs(fw.d - d0) < 0.05, f"D {d0:.3f} -> {fw.d:.3f} mol")

# 11. Gas added outside a task: no rate decision while it is in the window.
fer = Fermenter(temp=20.0)
fw = Firmware(fer, state=0)
fw.d = fer.dissolved
run(fw, 3 * 3600)
fer.thr = 3.0                              # no relief after the injection
fer.p += 0.3                               # CO2 injected
decisions = []
run(fw, 60 * 60, each=lambda f: decisions.append(f.rate))
check("external step: no decision", all(not math.isfinite(r) for r in decisions[1:]) and fw.state == 0,
      f"finite rates in the next hour: {sum(math.isfinite(r) for r in decisions[1:])}")

# ---- gCO2/L/d = gas leaving the beer; dissolved = 30-min mean of Henry
# (docs/gco2-rate.md, docs/dissolved-co2.md). The simulated beer produces the
# CO2 and transfers it to the gas with a 2-h time constant.
def active_fermenter():
    fer = Fermenter(temp=20.0, threshold=0.826, tau_h=2.0)
    fer.gen_gpld = 8.6
    fw = Firmware(fer, state=1)
    run(fw, 12 * 3600)
    return fer, fw


fer, fw = active_fermenter()
steady = []
run(fw, 3 * 3600, each=lambda f: steady.append(f.display))
check("steady relief cycle: gCO2 = production",
      abs(statistics.mean(steady) - 8.6) < 0.3 and statistics.pstdev(steady) < 0.3,
      f"mean {statistics.mean(steady):.2f}, sd {statistics.pstdev(steady):.2f} g/L/d (production 8.6)")

# Set point rise: the gas leaving the beer dips (the beer absorbs), no spike.
fer.thr = 1.548
seen = []
run(fw, 4 * 3600, each=lambda f: seen.append(f.display))
check("set point rise: no spike, gas leaving below production",
      max(seen) < 8.6 * 1.1 and min(seen) > 0.0,
      f"{min(seen):.2f}..{max(seen):.2f} g/L/d while rising (production 8.6)")

# After the transition the SG error undoes itself: counted total vs true.
fer, fw = active_fermenter()
bias0 = (fer.measured_gas() + fw.d) - fer.true_total()
fer.thr = 1.548
run(fw, 3 * 3600)
bias_rise = (fer.measured_gas() + fw.d) - fer.true_total()
run(fw, 10 * 3600)
bias_end = (fer.measured_gas() + fw.d) - fer.true_total()
check("SG error of a pressure rise undoes itself",
      abs(bias_end - bias0) < 0.2 * max(abs(bias_rise - bias0), 0.1),
      f"counted - true: {bias0:+.3f} before, {bias_rise:+.3f} during, {bias_end:+.3f} 10 h after "
      f"({pts(bias_rise - bias0, fer.v):+.2f} pt at most)")

# Cooling ramp 2 C/h, 20 -> 15 C: the beer absorbs, the gas leaving dips.
fer, fw = active_fermenter()
seen = []


def cooling(f):
    f.fer.t = max(15.0, f.fer.t - 2.0 / 60.0)
    seen.append(f.display)


run(fw, 5 * 3600, each=cooling)
check("cooling ramp: no spike", max(seen) < 8.6 * 1.1,
      f"{min(seen):.2f}..{max(seen):.2f} g/L/d while cooling (production 8.6)")

# Reboot without the buffers: the window refills from zero.
fer, fw = active_fermenter()
fw.reboot()
seen = []
run(fw, 3 * 3600, each=lambda f: seen.append(f.display))
after = [v for v in seen[75:] if math.isfinite(v)]
check("reboot keeps the rate", after and max(abs(v - 8.6) for v in after) < 0.5,
      f"after the window refills: {min(after):.2f}..{max(after):.2f} g/L/d")


# Quick reboot with the buffers restored (PressureControl.cpp, restoreCO2Buffers):
# the saved gas samples are shifted in time by the gap and stitched to the
# current gas phase at their last rate (10 samples). The reboot also loses
# 0.26 mol of ejected CO2 not yet saved in the counters.
def stitch_restore(fw, gap_s, lost_ejected):
    fer = fw.fer
    saved_gas = list(fw.gas_hist)
    saved_hs = list(fw.hs)
    for _ in range(int(gap_s // DT)):          # the beer goes on during the boot
        fer.step(True)
    fer.ejected -= lost_ejected                # ejected since the last counters save
    t_off = fw.now + gap_s
    fw.reboot()
    fw.now = BOOT_WAIT
    tail = saved_gas[-10:]
    n = len(tail)
    sx = sum(t for t, _ in tail); sy = sum(v for _, v in tail)
    slope = (n * sum(t * v for t, v in tail) - sx * sy) / (n * sum(t * t for t, _ in tail) - sx * sx)
    last_t = saved_gas[-1][0] - t_off + fw.now
    shift = fer.measured_gas() - (saved_gas[-1][1] + slope * (fw.now - last_t))
    fw.gas_hist = [(t - t_off + fw.now, v + shift) for t, v in saved_gas]
    fw.hs = [(t - t_off + fw.now, h) for t, h in saved_hs]


fer, fw = active_fermenter()
before = []
run(fw, 3600, each=lambda f: before.append(f.display))
stitch_restore(fw, 60, 0.26)
after = []
run(fw, 2 * 3600, each=lambda f: after.append(f.display))
check("quick reboot, buffers restored: display continues",
      max(abs(v - 8.6) for v in after if math.isfinite(v)) < 0.6,
      f"before {min(before):.2f}..{max(before):.2f}, after {min(after):.2f}..{max(after):.2f} g/L/d "
      f"(0.26 mol of ejected lost)")

print(f"\n{sum(c for _, c in results)}/{len(results)} passed")
raise SystemExit(0 if all(c for _, c in results) else 1)
