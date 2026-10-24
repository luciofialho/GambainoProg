"""Bench for the extract calculation (calculateFermentationState in src/PressureControl.cpp).

Mirrors the firmware: ASBC polynomial SG <-> apparent Plato (inverted by Newton),
Balling's factors (2.0665 g extract -> 1 g ethanol + 0.9565 g CO2 + 0.11 g yeast)
and Balling's attenuation quotient q(OE) = 1.220 + 0.001 * OE
(Savel, Kosin & Broz 2015, eq. 27). See docs/extract.md.

Run: python tests/extract_bench.py
"""


def sg_to_plato(sg):
    return ((135.997 * sg - 630.272) * sg + 1111.14) * sg - 616.868


def plato_to_sg(p):
    sg = 1.0 + p / (258.6 - (p / 258.2) * 227.1)
    for _ in range(4):
        d = 1111.14 - 1260.544 * sg + 407.991 * sg * sg
        sg -= (sg_to_plato(sg) - p) / d
    return sg


def attenuation_quotient(oe):
    return 1.220 + 0.001 * max(oe, 0.0)


def fermentation_state(og, co2_mol_per_l, added_plato=0.0, q_fixed=None):
    oe = sg_to_plato(og) + added_plato
    sg0 = plato_to_sg(oe)
    m0 = 1000.0 * sg0 * 0.9982
    me0 = m0 * oe / 100.0
    mco2 = 44.0095 * max(0.0, co2_mol_per_l)
    mferm = mco2 * 2.0665 / 0.9565
    myeast = mco2 * 0.11 / 0.9565
    meth = mco2 / 0.9565
    m = m0 - mco2 - myeast
    real = 100.0 * (me0 - mferm) / m
    q = q_fixed if q_fixed else attenuation_quotient(oe)
    app = (real - (1.0 - 1.0 / q) * oe) * q
    sg = plato_to_sg(app)
    abw = 100.0 * meth / m
    abv = abw * sg * 0.9982 / 0.78924
    return dict(oe=oe, real=real, app=app, sg=sg, abv=abv, q=q)


def main():
    ok = True
    # Round trip of the polynomial inversion.
    for p in (0.0, 2.5, 8.0, 12.0, 16.0, 20.0):
        err = abs(sg_to_plato(plato_to_sg(p)) - p)
        ok &= err < 1e-6
    print("polynomial inversion: %s" % ("ok" if ok else "FAIL"))

    # Table values of q (Savel et al. 2015, Table 2, Hrach 1927).
    for oe, q_ref in ((6, 1.226), (10, 1.230), (12, 1.232), (15, 1.235)):
        good = abs(attenuation_quotient(oe) - q_ref) < 5e-4
        ok &= good
        print("q(%2d P) = %.3f (table %.3f) %s" % (oe, attenuation_quotient(oe), q_ref, "ok" if good else "FAIL"))

    # Start of fermentation: no CO2, apparent = real = original extract.
    s = fermentation_state(1.048, 0.0)
    good = abs(s["app"] - s["oe"]) < 1e-4 and abs(s["sg"] - 1.048) < 1e-5
    ok &= good
    print("no CO2: SG %.5f, AE %.3f = OE %.3f %s" % (s["sg"], s["app"], s["oe"], "ok" if good else "FAIL"))

    # Effect of the change from the fixed q = 1/0.8192 (previous firmware).
    print("\nOG     CO2 mol/L  RE (P)  AE q(OE)  AE fixed  SG q(OE)  SG fixed  dSG (points)  ABV")
    for og in (1.040, 1.048, 1.060, 1.072):
        for c in (0.2, 0.4, 0.6):
            a = fermentation_state(og, c)
            b = fermentation_state(og, c, q_fixed=1.0 / 0.8192)
            print("%.3f  %.1f        %6.3f  %8.3f  %8.3f  %.5f   %.5f   %+6.2f        %.2f%%"
                  % (og, c, a["real"], a["app"], b["app"], a["sg"], b["sg"], 1000 * (a["sg"] - b["sg"]), a["abv"]))
    print("\nRESULT: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
