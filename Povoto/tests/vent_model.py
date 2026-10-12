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
SIMPSON_INTERVALS = 16  # even


def make_curve(a, t0=0.0, p_min=0.0, p_max=3.0):
    """Stored flow curve (VentCurve): tau coefficients, t0 and the pressure range of the test."""
    return {"a": list(a), "t0": t0, "p_min": p_min, "p_max": p_max}


def tau_ref(curve, p_gauge):
    """tau(P) = a0 + a1 P + a2 P^2 + a3 P^3 (s): air, V_REF, T_REF.

    P is limited to the range of the test: the cubic is not extrapolated."""
    a = curve["a"]
    p = min(max(p_gauge, curve["p_min"]), curve["p_max"])
    return ((a[3] * p + a[2]) * p + a[1]) * p + a[0]


def flow_constant_air(curve, p_gauge):
    """C_air(P), mol*K^0.5/(s*bar): air molar flow = C_air * P_abs / sqrt(T)."""
    tau = tau_ref(curve, p_gauge)
    return V_REF / (R * math.sqrt(T_REF) * tau) if tau > 0 else float("nan")


def nozzle_psi(gamma, r):
    """Ideal-nozzle flow function for downstream/upstream pressure ratio r."""
    critical = (2.0 / (gamma + 1.0)) ** (gamma / (gamma - 1.0))
    if r <= critical:
        return math.sqrt(gamma) * (2.0 / (gamma + 1.0)) ** ((gamma + 1.0) / (2.0 * (gamma - 1.0)))
    r = min(r, 1.0)
    return math.sqrt(max(0.0, 2.0 * gamma / (gamma - 1.0) * (r ** (2.0 / gamma) - r ** ((gamma + 1.0) / gamma))))


def co2_air_ratio(p_gauge, patm):
    """CO2/air molar flow of an ideal nozzle at the same pressures and temperature."""
    r = patm / (p_gauge + patm)
    psi_air = nozzle_psi(GAMMA_AIR, r)
    if not psi_air > 0:
        return math.sqrt(M_AIR / M_CO2)
    return math.sqrt(M_AIR / M_CO2) * nozzle_psi(GAMMA_CO2, r) / psi_air


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
    return (flow_constant_air(curve, p_gauge) * co2_air_ratio(p_gauge, patm)
            / math.sqrt(co2_z(p_abs, t_kelvin)) * level)


def vent_moles(curve, level, open_seconds, p_on_gauge, p_close_gauge, patm, t_gas_kelvin, exponent):
    """CO2 moles out of one vent: C(P_mean) P_mean / sqrt(T) * cooling * (t - t0).

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
    seconds = max(0.0, open_seconds - curve["t0"])
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
    return moles / rate + curve["t0"] if rate > 0 else float("nan")


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
        g2 /= math.sqrt(co2_z(p2, t0_kelvin)) * co2_air_ratio(p_abs2 - patm, patm)
        g1 /= math.sqrt(co2_z(p1, t0_kelvin)) * co2_air_ratio(p1_gauge, patm)
    return scale * g1 * tau_ref(curve, p1_gauge), scale * g2 * tau_ref(curve, p_abs2 - patm)


def reduced_chi_square(curve, releases, residuals, exponents, volume, patm, co2,
                       floor_bar=0.00005, timing_seconds=0.002):
    """sum r^2 / (s1^2 n1^2 + s2^2 n2^2 + st^2) / (n - 5) (reducedChiSquare)."""
    if len(releases) <= 5:
        return float("nan")
    total = 0.0
    for r, res, m in zip(releases, residuals, exponents):
        s1, s2 = release_sensitivities(curve, volume, r["temp_k"], r["p1"], r["p2"], patm, m, co2)
        n1 = max(r.get("noise1", 0.0) if math.isfinite(r.get("noise1", float("nan"))) else 0.0, floor_bar)
        n2 = max(r.get("noise2", 0.0) if math.isfinite(r.get("noise2", float("nan"))) else 0.0, floor_bar)
        total += res * res / (s1 * s1 * n1 * n1 + s2 * s2 * n2 * n2 + timing_seconds ** 2)
    return total / (len(releases) - 5)

def release_exponent(p1_gauge, p_close_gauge, p2_gauge, patm, fallback):
    """Polytropic exponent of one release: ln(Pclose/P1) / ln(P2/P1), absolute."""
    try:
        m = math.log((p_close_gauge + patm) / (p1_gauge + patm)) / math.log((p2_gauge + patm) / (p1_gauge + patm))
    except (ValueError, ZeroDivisionError):
        return fallback
    return m if 1.0 <= m <= 1.67 else fallback


def release_integrals(volume, t0_kelvin, p1_gauge, p2_gauge, patm, exponent, co2):
    """J_k, k = 0..3: (t - t0) = sum a_k J_k for one release (Simpson in P_iso)."""
    p1 = p1_gauge + patm
    p2 = p2_gauge + patm
    scale = volume / V_REF * math.sqrt(T_REF / t0_kelvin)
    n = SIMPSON_INTERVALS
    h = (p1 - p2) / n
    j = [0.0, 0.0, 0.0, 0.0]
    for i in range(n + 1):
        p_iso = p2 + i * h
        x = p_iso / p1
        p_abs = p1 * x ** exponent
        sqrt_t = math.sqrt(x ** (exponent - 1.0))
        g = sqrt_t / p_abs
        if co2:
            g /= math.sqrt(co2_z(p_iso, t0_kelvin)) * co2_air_ratio(p_abs - patm, patm)
        w = 1 if i in (0, n) else (4 if i % 2 else 2)
        pg = p_abs - patm
        for k in range(4):
            j[k] += w * g * pg ** k
    return [scale * v * h / 3.0 for v in j]


def solve(matrix, rhs):
    n = len(rhs)
    m = [row[:] + [rhs[i]] for i, row in enumerate(matrix)]
    for col in range(n):
        pivot = max(range(col, n), key=lambda r: abs(m[r][col]))
        if abs(m[pivot][col]) < 1e-300:
            return None
        m[col], m[pivot] = m[pivot], m[col]
        for r in range(n):
            if r != col:
                f = m[r][col] / m[col][col]
                for k in range(col, n + 1):
                    m[r][k] -= f * m[col][k]
    return [m[i][n] / m[i][i] for i in range(n)]


def fit_venting_test(releases, volume, patm, co2=False, fit_t0=True, default_exponent=1.0):
    """releases: dicts with t (real open s), p1, p_close, p2 (gauge bar), temp_k.

    Weighted least squares on t_i = t0 + sum a_k J_ik with weights P_abs^2
    (mean of P1 and P2): a pressure reading error is an almost constant error
    in time. Returns dict with a, t0, residuals and exponents."""
    if len(releases) < (5 if fit_t0 else 4) + 2:
        return None
    rows, ys, ws, exps = [], [], [], []
    for r in releases:
        m = release_exponent(r["p1"], r["p_close"], r["p2"], patm, default_exponent) \
            if r.get("p_close") is not None and math.isfinite(r["p_close"]) else default_exponent
        j = release_integrals(volume, r["temp_k"], r["p1"], r["p2"], patm, m, co2)
        rows.append(([1.0] if fit_t0 else []) + j)
        ys.append(r["t"])
        ws.append((0.5 * (r["p1"] + r["p2"]) + patm) ** 2)
        exps.append(m)
    n = len(rows[0])
    ata = [[sum(w * row[i] * row[k] for row, w in zip(rows, ws)) for k in range(n)] for i in range(n)]
    aty = [sum(w * row[i] * y for row, w, y in zip(rows, ws, ys)) for i in range(n)]
    x = solve(ata, aty)
    if x is None:
        return None
    t0 = x[0] if fit_t0 else 0.0
    a = x[1:] if fit_t0 else x
    residuals = [y - sum(c * v for c, v in zip(x, row)) for row, y in zip(rows, ys)]
    t0_se = float("nan")
    if fit_t0:
        first = solve(ata, [1.0] + [0.0] * (n - 1))  # first column of (A'WA)^-1
        s2 = sum(w * r * r for w, r in zip(ws, residuals)) / (len(rows) - n)
        t0_se = math.sqrt(max(0.0, s2 * first[0])) if first else float("nan")
    return {"a": a, "t0": t0, "residuals": residuals, "exponents": exps, "t0_se": t0_se}
