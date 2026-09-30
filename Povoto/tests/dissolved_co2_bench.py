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
SUPERSAT_WIN = 30  # samples (60 s): mean pressure and gas flux
TAU_DES = 2.0      # h, calibration page default
TAU_ABS = 2.0


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

    def __init__(self, fer, state=1, tau_des=TAU_DES, tau_abs=TAU_ABS):
        self.fer = fer
        self.state = state          # 0 half-life, 1 equilibrium, 2 initial, 3 armed
        self.d = fer.dissolved
        self.tau_des, self.tau_abs = tau_des, tau_abs
        self.ss = []                # (now, gas, pressure), last SUPERSAT_WIN samples
        self.x = 0.0                # supersaturation applied
        self.x_held = True
        self.pending = 0.0          # x step after a hold (model step)
        self.total_hist = []        # (now, gas + dissolved): displayed gCO2/L/d
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
        self.ss, self.total_hist, self.x_held = [], [], True

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
            hm = sum(s[3] for s in self.ss) / len(self.ss) if self.ss else henry(fer.p, fer.t, fer.v)
            self.d = max(0.0, hm + self.x)
            if self.pending:
                self.total_hist = [(t, v + self.pending) for t, v in self.total_hist]
                self.pending = 0.0
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

    def supersat_sample(self, gas):
        fer = self.fer
        self.total_hist.append((self.now, gas + self.d))
        self.total_hist = self.total_hist[-HIST:]
        h = self.total_hist
        n = len(h)
        if n >= 5:
            k = 1 if n < 9 else min(n // 3, 10)
            a = sum(v for _, v in h[:k]) / k
            b = sum(v for _, v in h[-k:]) / k
            ta = sum(t for t, _ in h[:k]) / k
            tb = sum(t for t, _ in h[-k:]) / k
            self.display = (b - a) * M * 86400 / (fer.v * (tb - ta))
        self.ss.append((self.now, gas, fer.p, henry(fer.p, fer.t, fer.v)))
        self.ss = self.ss[-SUPERSAT_WIN:]
        if len(self.ss) >= 25 and self.ss[-1][0] - self.ss[0][0] >= 27 * 60 and self.task == 0:
            xs = [(s[0] - self.ss[0][0]) / 86400 for s in self.ss]
            ys = [s[1] for s in self.ss]
            m = len(xs)
            sx, sy = sum(xs), sum(ys)
            f = (m * sum(x * y for x, y in zip(xs, ys)) - sx * sy) / (m * sum(x * x for x in xs) - sx * sx)
            hm = sum(s[3] for s in self.ss) / m
            nx = f * (self.tau_des if f >= 0 else self.tau_abs) / 24
            nx = max(-0.5 * hm, min(0.5 * hm, nx))
            if self.x_held:
                self.pending += nx - self.x
                self.x_held = False
            self.x = nx
        else:
            self.x_held = True

    def sample(self):
        fer = self.fer
        if self.now < BOOT_WAIT:
            return
        gas = fer.measured_gas()
        self.supersat_sample(gas)
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

# ---- Supersaturation (docs/dissolved-co2.md). The simulated beer transfers
# CO2 with a 2-h time constant, the physics the model assumes.
def supersat_case(tau_fw, change, hours):
    fer = Fermenter(temp=20.0, threshold=0.826, tau_h=2.0)
    fer.gen_gpld = 8.6
    fw = Firmware(fer, state=1, tau_des=tau_fw, tau_abs=tau_fw)
    run(fw, 12 * 3600)                     # steady relief cycle
    steady = []
    run(fw, 3 * 3600, each=lambda f: steady.append(f.display))
    seen = []
    if change:
        change(fer)
        run(fw, hours * 3600, each=lambda f: seen.append(f.display))
    else:
        def each(f):
            cool_ramp(f)
            seen.append(f.display)
        run(fw, hours * 3600, each=each)
    return steady, seen


def raise_setpoint(fer):
    fer.thr = 1.548                        # set point 0.8 -> 1.5 bar


def cool(fer):
    fer.t = 15.0                           # cooling 20 -> 15 C as a step (not physical)


cool_ramp_steps = [0]


def cool_ramp(fw):
    # 2 C/h from 20 to 15 C, as a chiller does
    fw.fer.t = max(15.0, fw.fer.t - 2.0 / 60.0)


for label, change, hours in (("set point rise", raise_setpoint, 4), ("cooling ramp 20->15 C, 2 C/h", None, 5)):
    res = {}
    for tau in (0.0, 2.0):
        steady, seen = supersat_case(tau, change, hours)
        res[tau] = (statistics.pstdev(steady), max(seen), min(seen), statistics.mean(seen))
    s0, s2 = res[0.0], res[2.0]
    check(f"{label}: gCO2 stays near the generation (8.6)",
          abs(s2[3] - 8.6) < 1.5 and s2[1] < 8.6 * 1.5 and abs(s2[3] - 8.6) < abs(s0[3] - 8.6),
          f"tau 2 h: mean {s2[3]:.2f}, max {s2[1]:.2f}, min {s2[2]:.2f} | tau 0: mean {s0[3]:.2f}, max {s0[1]:.2f}, min {s0[2]:.2f}")
    if label == "set point rise":
        check("steady relief cycle: no extra noise", s2[0] <= s0[0] + 0.3,
              f"sd tau 2 h {s2[0]:.2f}, tau 0 {s0[0]:.2f} g/L/d")

# Reboot in active fermentation: x is held until the flux is back; the change
# is a model step, not production.
fer = Fermenter(temp=20.0, threshold=0.826, tau_h=2.0)
fer.gen_gpld = 8.6
fw = Firmware(fer, state=1)
run(fw, 12 * 3600)
fw.reboot()
seen = []
run(fw, 3 * 3600, each=lambda f: seen.append(f.display))
after = [v for v in seen[75:] if math.isfinite(v)]   # full window after the boot
check("reboot keeps the rate", after and max(abs(v - 8.6) for v in after) < 1.5,
      f"after the window refills: {min(after):.2f}..{max(after):.2f} g/L/d")

# Upgrade: 12 h with the instant-equilibrium model (x never counted), then the
# new version boots with x = 0. When the flux is back, x enters the dissolved
# CO2: the counted total must match the true one (SG corrected), with no spike
# on the display.
fer = Fermenter(temp=20.0, threshold=0.826, tau_h=2.0)
fer.gen_gpld = 8.6
old = Firmware(fer, state=1, tau_des=0.0, tau_abs=0.0)
run(old, 12 * 3600)
bias_before = (fer.measured_gas() + old.d) - fer.true_total()
new = Firmware(fer, state=1)
new.d = old.d
new.now = 0
seen = []
run(new, 2 * 3600, each=lambda f: seen.append(f.display))
bias_after = (fer.measured_gas() + new.d) - fer.true_total()
shown = [v for v in seen[40:] if math.isfinite(v)]
check("upgrade: SG corrected, no spike", abs(bias_after) < 0.25 and max(shown) < 8.6 * 1.3,
      f"counted - true: {bias_before:+.3f} mol before, {bias_after:+.3f} mol after "
      f"({pts(bias_before, fer.v):+.2f} -> {pts(bias_after, fer.v):+.2f} pt); display max {max(shown):.2f}")

print(f"\n{sum(c for _, c in results)}/{len(results)} passed")
raise SystemExit(0 if all(c for _, c in results) else 1)
