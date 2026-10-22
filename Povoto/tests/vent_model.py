"""Python mirror of include/VentModel.h (direct venting, docs/vent.md).

Every function here has the same name, arguments and formula as its C++
counterpart, so the bench (tests/vent_bench.py) and the fit check
(tests/venting_fit.py) test the firmware's mathematics. Keep both in step.

Units: gauge and absolute pressures in bar, temperatures in K, volumes in L,
times in s, moles in mol.
"""
import math

R = 0.0831446          # bar*L/(mol*K)
V_REF = 100.0          # L: the stored curve is tau for 100 L of air at 20 C
T_REF = 293.15         # K
GAMMA_AIR = 1.40
GAMMA_CO2 = 1.29
M_AIR = 28.96
M_CO2 = 44.01
SIMPSON_INTERVALS = 8  # even; 16 changes the fit by < 1e-4 relative


def make_curve(k, beta, p_min=0.0, p_max=3.0):
    """Stored flow curve (VentModel::Curve): nozzle capacity k (s), outlet resistance beta and the
    pressure range of the test. tau(P) = k / psi_eff(P), see series_psi(). There is no t0: the
    opening delay of the valve is taken equal to its closing delay."""
    return {"k": k, "beta": beta, "p_min": p_min, "p_max": p_max}


def nozzle_psi(gamma, r):
    """Ideal-nozzle flow function for downstream/upstream pressure ratio r."""
    critical = (2.0 / (gamma + 1.0)) ** (gamma / (gamma - 1.0))
    if r <= critical:
        return math.sqrt(gamma) * (2.0 / (gamma + 1.0)) ** ((gamma + 1.0) / (2.0 * (gamma - 1.0)))
    r = min(r, 1.0)
    return math.sqrt(max(0.0, 2.0 * gamma / (gamma - 1.0) * (r ** (2.0 / gamma) - r ** ((gamma + 1.0) / gamma))))


def series_psi(p_up, patm, beta, gamma):
    """Effective flow function of the valve and its outlet (silencer) in series.

    The valve is an ideal nozzle, molar flow ~ p_up psi(r) with r = p_m / p_up, p_m the pressure
    between the valve and the outlet. The outlet is a dissipative restriction without choking,
    (p_m^2 - patm^2) / patm = beta n^2 (n in the units of p_up psi). The same flow goes through
    both: p_up^2 patm beta psi(r)^2 = p_up^2 r^2 - patm^2. Returns psi(r) (absolute pressures,
    bar); the root is bracketed and found by Newton with bisection fallback (about 5 iterations)."""
    if not (p_up > patm and beta > 0.0):
        return float("nan")
    crit = (2.0 / (gamma + 1.0)) ** (gamma / (gamma - 1.0))
    psi_c2 = gamma * (2.0 / (gamma + 1.0)) ** ((gamma + 1.0) / (gamma - 1.0))
    c = 2.0 * gamma / (gamma - 1.0)
    pu2 = p_up * p_up
    pa2 = patm * patm

    def terms(r):
        if r <= crit:
            psi2, dpsi2 = psi_c2, 0.0
        else:
            u = r ** (1.0 / gamma)
            psi2 = c * (u * u - r * u)
            dpsi2 = c * (2.0 * u * u / (gamma * r) - (1.0 + 1.0 / gamma) * u)
        return patm * beta * pu2 * psi2 - (pu2 * r * r - pa2), patm * beta * pu2 * dpsi2 - 2.0 * pu2 * r, psi2

    lo, hi = patm / p_up, 1.0
    r = min(max(math.sqrt(pa2 + patm * beta * pu2 * psi_c2) / p_up, lo), hi)
    for _ in range(60):
        f, df, _ = terms(r)
        if f > 0.0:
            lo = r
        else:
            hi = r
        nxt = r - f / df if df != 0.0 else 0.5 * (lo + hi)
        if not (lo < nxt < hi):
            nxt = 0.5 * (lo + hi)
        done = abs(nxt - r) < 1e-12
        r = nxt
        if done:
            break
    return math.sqrt(max(0.0, terms(r)[2]))


TAU_MIN_GAUGE = 0.005  # bar: lowest gauge pressure tau is evaluated at


def tau_ref(curve, p_gauge, patm):
    """tau(P) = k / psi_eff(P) (s): air, V_REF, T_REF, P in gauge bar. A physical curve, so it has
    no range: p_min..p_max only say where the test measured it."""
    psi = series_psi(max(p_gauge, TAU_MIN_GAUGE) + patm, patm, curve["beta"], GAMMA_AIR)
    return curve["k"] / psi if psi > 0 else float("nan")


def flow_constant_air(curve, p_gauge, patm):
    """C_air(P), mol*K^0.5/(s*bar): air molar flow = C_air * P_abs / sqrt(T)."""
    tau = tau_ref(curve, p_gauge, patm)
    return V_REF / (R * math.sqrt(T_REF) * tau) if tau > 0 else float("nan")


def co2_air_ratio(beta, p_gauge, patm):
    """CO2/air molar flow at the same pressures and temperature: sqrt(M_air/M_CO2) (both the
    valve and the outlet go with 1/sqrt(M)) times the ratio of the nozzle functions of the pair."""
    p_up = p_gauge + patm
    psi_air = series_psi(p_up, patm, beta, GAMMA_AIR)
    if not psi_air > 0:
        return math.sqrt(M_AIR / M_CO2)
    return math.sqrt(M_AIR / M_CO2) * series_psi(p_up, patm, beta, GAMMA_CO2) / psi_air


def co2_second_virial(t_kelvin):
    """B of CO2 (L/mol), Tsonopoulos correlation (-0.129 L/mol at 20 C)."""
    tc, pc, omega = 304.13, 73.77, 0.224
    tr = t_kelvin / tc
    f0 = 0.1445 - 0.330 / tr - 0.1385 / tr ** 2 - 0.0121 / tr ** 3 - 0.000607 / tr ** 8
    f1 = 0.0637 + 0.331 / tr ** 2 - 0.423 / tr ** 3 - 0.008 / tr ** 8
    return (f0 + omega * f1) * R * tc / pc


def co2_z(p_abs, t_kelvin):
    """Compressibility of CO2, Z = 1 + B P / (R T)."""
    return 1.0 + co2_second_virial(t_kelvin) * p_abs / (R * t_kelvin)


def log_mean(a, b):
    if a <= 0 or b <= 0:
        return float("nan")
    if abs(a - b) < 1e-9 * a:
        return 0.5 * (a + b)
    return (a - b) / math.log(a / b)


def cooling_factor(p_on_abs, p_mean_abs, exponent):
    """sqrt(T0 / T) at the mean pressure of a polytropic expansion from p_on."""
    if not (exponent >= 1.0) or p_mean_abs <= 0:
        return 1.0
    return (p_on_abs / p_mean_abs) ** ((exponent - 1.0) / (2.0 * exponent))


def co2_flow_constant(curve, level, p_gauge, patm, t_kelvin):
    """Effective CO2 flow constant: C_air * ratio / sqrt(Z) * level."""
    p_abs = p_gauge + patm
    return (flow_constant_air(curve, p_gauge, patm) * co2_air_ratio(curve["beta"], p_gauge, patm)
            / math.sqrt(co2_z(p_abs, t_kelvin)) * level)


def vent_moles(curve, level, open_seconds, p_on_gauge, p_close_gauge, patm, t_gas_kelvin, exponent):
    """CO2 moles out of one vent: C(P_mean) P_mean / sqrt(T) * cooling * t.

    p_close_gauge is the pressure read right after closing (gas still cold);
    P_mean is the log mean of the absolute pressures at opening and closing.
    Returns (moles, p_mean_gauge, cooling)."""
    p_on = p_on_gauge + patm
    p_close = p_close_gauge + patm
    p_mean = log_mean(p_on, p_close)
    if not (p_mean > patm) or t_gas_kelvin <= 0:
        return 0.0, float("nan"), float("nan")
    cooling = cooling_factor(p_on, p_mean, exponent)
    c = co2_flow_constant(curve, level, p_mean - patm, patm, t_gas_kelvin)
    seconds = max(0.0, open_seconds)
    return c * p_mean / math.sqrt(t_gas_kelvin) * cooling * seconds, p_mean - patm, cooling


def vent_seconds(curve, level, headspace, p_on_gauge, p_target_gauge, patm, t_gas_kelvin, exponent):
    """Opening time for the CO2 headspace to go from p_on to p_target (gauge, settled)."""
    p_on = p_on_gauge + patm
    p_target = p_target_gauge + patm
    if not (headspace > 0 and p_target < p_on and p_target > patm * 0.5 and t_gas_kelvin > 0):
        return float("nan")
    moles = headspace / (R * t_gas_kelvin) * (p_on / co2_z(p_on, t_gas_kelvin)
                                              - p_target / co2_z(p_target, t_gas_kelvin))
    p_mean = log_mean(p_on, p_target)
    rate = (co2_flow_constant(curve, level, p_mean - patm, patm, t_gas_kelvin) * p_mean
            / math.sqrt(t_gas_kelvin) * cooling_factor(p_on, p_mean, exponent))
    return moles / rate if rate > 0 else float("nan")


def isothermal_after(p_on_gauge, p_read_gauge, patm, exponent):
    """Settled pressure after a polytropic drop (adjustedPressureForPostRelief)."""
    p_on = p_on_gauge + patm
    p_read = p_read_gauge + patm
    return p_on * (p_read / p_on) ** (1.0 / exponent) - patm


def apparent_headspace(moles, p_on_gauge, p_after_iso_gauge, patm, t_gas_kelvin):
    """Vh = moles / delta(P / Z) * R T (CO2)."""
    p_on = p_on_gauge + patm
    p_after = p_after_iso_gauge + patm
    delta = p_on / co2_z(p_on, t_gas_kelvin) - p_after / co2_z(p_after, t_gas_kelvin)
    return moles * R * t_gas_kelvin / delta if delta > 0 else float("nan")


# ----- Venting test fit (VentModel::fitVentingTest) -----

def release_sensitivities(curve, volume, t0_kelvin, p1_gauge, p2_gauge, patm, exponent, co2):
    """(dt/dP1, -dt/dP2) of the release model, s/bar (releaseSensitivities)."""
    p1 = p1_gauge + patm
    p2 = p2_gauge + patm
    scale = volume / V_REF * math.sqrt(T_REF / t0_kelvin)
    x = p2 / p1
    p_abs2 = p1 * x ** exponent
    g2 = math.sqrt(x ** (exponent - 1.0)) / p_abs2
    g1 = 1.0 / p1
    if co2:
        g2 /= math.sqrt(co2_z(p2, t0_kelvin)) * co2_air_ratio(curve["beta"], p_abs2 - patm, patm)
        g1 /= math.sqrt(co2_z(p1, t0_kelvin)) * co2_air_ratio(curve["beta"], p1_gauge, patm)
    return scale * g1 * tau_ref(curve, p1_gauge, patm), scale * g2 * tau_ref(curve, p_abs2 - patm, patm)


FIT_PARAMETERS = 2     # k, beta


def reduced_chi_square(curve, releases, residuals, exponents, volume, patm, co2,
                       floor_bar=0.00005, timing_seconds=0.002):
    """sum r^2 / (s1^2 n1^2 + s2^2 n2^2 + st^2) / (n - 2) (reducedChiSquare)."""
    if len(releases) <= FIT_PARAMETERS:
        return float("nan")
    total = 0.0
    for r, res, m in zip(releases, residuals, exponents):
        s1, s2 = release_sensitivities(curve, volume, r["temp_k"], r["p1"], r["p2"], patm, m, co2)
        n1 = max(r.get("noise1", 0.0) if math.isfinite(r.get("noise1", float("nan"))) else 0.0, floor_bar)
        n2 = max(r.get("noise2", 0.0) if math.isfinite(r.get("noise2", float("nan"))) else 0.0, floor_bar)
        total += res * res / (s1 * s1 * n1 * n1 + s2 * s2 * n2 * n2 + timing_seconds ** 2)
    return total / (len(releases) - FIT_PARAMETERS)

def release_exponent(p1_gauge, p_close_gauge, p2_gauge, patm, fallback):
    """Polytropic exponent of one release: ln(Pclose/P1) / ln(P2/P1), absolute."""
    try:
        m = math.log((p_close_gauge + patm) / (p1_gauge + patm)) / math.log((p2_gauge + patm) / (p1_gauge + patm))
    except (ValueError, ZeroDivisionError):
        return fallback
    return m if 1.0 <= m <= 1.67 else fallback


def release_integral(volume, t0_kelvin, p1_gauge, p2_gauge, patm, exponent, co2, beta):
    """S so that t = k S for one release from the settled P1 to the settled P2 (Simpson in
    P_iso): the time is the integral of tau(P) g dP, tau = k / psi_eff."""
    p1 = p1_gauge + patm
    p2 = p2_gauge + patm
    scale = volume / V_REF * math.sqrt(T_REF / t0_kelvin)
    n = SIMPSON_INTERVALS
    h = (p1 - p2) / n
    total = 0.0
    for i in range(n + 1):
        p_iso = p2 + i * h
        x = p_iso / p1
        p_abs = p1 * x ** exponent
        g = math.sqrt(x ** (exponent - 1.0)) / p_abs
        if co2:
            g /= math.sqrt(co2_z(p_iso, t0_kelvin)) * co2_air_ratio(beta, p_abs - patm, patm)
        w = 1 if i in (0, n) else (4 if i % 2 else 2)
        total += w * g / series_psi(p_abs, patm, beta, GAMMA_AIR)
    return scale * total * h / 3.0


BETA_MIN, BETA_MAX = 0.01, 30.0   # search range of the outlet resistance
BETA_ITERATIONS = 20              # golden-section steps on ln(beta): 5e-4 in ln(beta)


def _profile(rows, ts, ws):
    """Weighted least squares of t = k S for fixed beta: (sse, k)."""
    sxx = sum(w * s * s for w, s in zip(ws, rows))
    sxy = sum(w * s * t for w, s, t in zip(ws, rows, ts))
    if not sxx > 0.0:
        return None
    k = sxy / sxx
    return sum(w * (t - k * s) ** 2 for w, s, t in zip(ws, rows, ts)), k


def fit_venting_test(releases, volume, patm, co2=False, default_exponent=1.0):
    """releases: dicts with t (real open s), p1, p_close, p2 (gauge bar), temp_k.

    t_i = k S_i(beta) with S_i the release integral: for each beta the weighted least squares
    (weights P_abs^2, mean of P1 and P2: a pressure reading error is an almost constant error in
    time) gives k in closed form; beta is found by golden-section search on ln(beta).
    Returns dict with k, beta, residuals and exponents."""
    if len(releases) < FIT_PARAMETERS + 2:
        return None
    exps, ts, ws = [], [], []
    for r in releases:
        m = release_exponent(r["p1"], r["p_close"], r["p2"], patm, default_exponent) \
            if r.get("p_close") is not None and math.isfinite(r["p_close"]) else default_exponent
        exps.append(m)
        ts.append(r["t"])
        ws.append((0.5 * (r["p1"] + r["p2"]) + patm) ** 2)

    def evaluate(beta):
        rows = [release_integral(volume, r["temp_k"], r["p1"], r["p2"], patm, m, co2, beta)
                for r, m in zip(releases, exps)]
        return rows, _profile(rows, ts, ws)

    golden = (math.sqrt(5.0) - 1.0) / 2.0
    lo, hi = math.log(BETA_MIN), math.log(BETA_MAX)
    x1, x2 = hi - golden * (hi - lo), lo + golden * (hi - lo)
    f1, f2 = evaluate(math.exp(x1))[1], evaluate(math.exp(x2))[1]
    if f1 is None or f2 is None:
        return None
    for _ in range(BETA_ITERATIONS):
        if f1[0] < f2[0]:
            hi, x2, f2 = x2, x1, f1
            x1 = hi - golden * (hi - lo)
            f1 = evaluate(math.exp(x1))[1]
        else:
            lo, x1, f1 = x1, x2, f2
            x2 = lo + golden * (hi - lo)
            f2 = evaluate(math.exp(x2))[1]
        if f1 is None or f2 is None:
            return None
    beta = math.exp(0.5 * (lo + hi))
    rows, best = evaluate(beta)
    if best is None:
        return None
    _, k = best
    residuals = [t - k * s for t, s in zip(ts, rows)]
    return {"k": k, "beta": beta, "residuals": residuals, "exponents": exps}


# Vapor in the vented gas (docs/vent.md): the gas that leaves is saturated with
# the vapor of the liquid, so 1 - y of the counted moles is CO2.
WATER_ACTIVITY = 0.98
ETHANOL_ACTIVITY_COEFFICIENT = 5.0


def water_saturation_pressure(t_celsius):
    """bar (Buck)."""
    return 0.0061121 * math.exp((18.678 - t_celsius / 234.5) * (t_celsius / (257.14 + t_celsius)))


def ethanol_saturation_pressure(t_celsius):
    """bar (Antoine, 1-78 C)."""
    return 0.00133322 * 10.0 ** (8.20417 - 1642.89 / (230.300 + t_celsius))


def ethanol_mole_fraction(abv_percent):
    abv = min(max(abv_percent, 0.0), 30.0)
    ethanol = 0.789 * abv / 46.07
    water = 0.998 * (100.0 - abv) / 18.015
    return ethanol / (ethanol + water)


def vent_vapor_fraction(liquid_celsius, abv_percent, p_abs):
    if not math.isfinite(liquid_celsius) or liquid_celsius < -5.0 or liquid_celsius > 60.0 or not p_abs > 0.1:
        return 0.0
    abv = abv_percent if math.isfinite(abv_percent) else 0.0
    vapor = (WATER_ACTIVITY * water_saturation_pressure(liquid_celsius) +
             ethanol_mole_fraction(abv) * ETHANOL_ACTIVITY_COEFFICIENT * ethanol_saturation_pressure(liquid_celsius))
    return min(max(vapor / p_abs, 0.0), 0.5)


# ----- Exponential recovery of a wait (PressureWait::fitRecovery) -----

def fit_recovery(t, p):
    """P(t) = p_inf - amp exp(-t / tau) fitted to all the samples (t in s, p in the unit wanted): for a tau,
    p_inf and amp are linear least squares; tau by golden-section search on ln(tau) in 8-400 s (24 steps).
    Returns (p_inf, tau, amp) or None (fewer than 20 samples, less than 40 s, tau at the edge)."""
    if len(t) < 20 or max(t) < 40.0:
        return None
    lo0, hi0 = math.log(8.0), math.log(400.0)
    n = len(t)

    def sse(tau):
        u = [math.exp(-x / tau) for x in t]
        su, sp = sum(u), sum(p)
        suu = sum(x * x for x in u)
        sup = sum(x * y for x, y in zip(u, p))
        det = n * suu - su * su
        if not det > 1e-12:
            return float("inf"), None, None
        m = (n * sup - su * sp) / det
        a = (sp - m * su) / n
        return sum((y - (a + m * x)) ** 2 for x, y in zip(u, p)), a, m

    golden = (math.sqrt(5.0) - 1.0) / 2.0
    lo, hi = lo0, hi0
    x1, x2 = hi - golden * (hi - lo), lo + golden * (hi - lo)
    f1, f2 = sse(math.exp(x1))[0], sse(math.exp(x2))[0]
    for _ in range(24):
        if f1 < f2:
            hi, x2, f2 = x2, x1, f1
            x1 = hi - golden * (hi - lo)
            f1 = sse(math.exp(x1))[0]
        else:
            lo, x1, f1 = x1, x2, f2
            x2 = lo + golden * (hi - lo)
            f2 = sse(math.exp(x2))[0]
    ln_tau = 0.5 * (lo + hi)
    e, a, m = sse(math.exp(ln_tau))
    if a is None or ln_tau - lo0 < 0.02 or hi0 - ln_tau < 0.02:
        return None
    return a, math.exp(ln_tau), -m
