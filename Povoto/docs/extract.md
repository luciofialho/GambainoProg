# Extrato, SG e ABV a partir do CO2

Implementação em `src/PressureControl.cpp` (`calculateFermentationState()`, `SGToApparentPlato()`,
`ApparentPlatoToSG()`, `attenuationQuotient()`). Bench: `python tests/extract_bench.py`.

## Cálculo (por litro de cerveja)

```
OE    = SGToApparentPlato(OG) + addedPlato           (°P; no mosto, aparente = real)
SG0   = ApparentPlatoToSG(OE)
m0    = 1000 · 0,9982 · SG0                          (g/L; SG 20/20 → densidade)
mE0   = m0 · OE / 100
mCO2  = 44,0095 · CO2MolsProducedPerLiter
mFerm = mCO2 · 2,0665 / 0,9565                       (Balling)
mEtOH = mCO2 / 0,9565
mY    = mCO2 · 0,11 / 0,9565
m     = m0 − mCO2 − mY                               (cerveja clarificada e desgaseificada)
RE    = 100 · (mE0 − mFerm) / m                      (extrato real, °P)
q     = 1,220 + 0,001 · OE                           (quociente de atenuação de Balling)
AE    = (RE − (1 − 1/q) · OE) · q                    (extrato aparente, °P)
SG    = ApparentPlatoToSG(AE)
ABV   = 100 · mEtOH / m · 0,9982 · SG / 0,78924
```

- **Polinômio SG ↔ °P**: `°P = 135,997·SG³ − 630,272·SG² + 1111,14·SG − 616,868` (ASBC; citado em
  Quek et al., 2019, *Amylase* 3:1–18, doi:10.1515/amylase-2019-0001, eq. 3). A volta (°P → SG) é por
  Newton, com 4 iterações a partir da fórmula 258,6/258,2/227,1.
- **Fatores de Balling** (2,0665 g de extrato → 1 g de etanol + 0,9565 g de CO2 + 0,11 g de levedura):
  Šavel, Košin & Brož (2015), *Kvasný Průmysl* 61(4):120–128, doi:10.18832/kp2015015, eq. (8).
- **Quociente de atenuação** q = (OE − AE)/(OE − RE): mesma fonte, eq. (27), que reproduz a tabela de
  Hrach (1927), igual à de Balling (1846): q = 1,226 / 1,230 / 1,232 / 1,235 a 6 / 10 / 12 / 15 °P. Os
  autores medem q ≈ 1,239 em média (cervejas tchecas); o firmware usa a eq. (27).

## Mudança de 23/10/2026

Antes: `RE = 0,1808·OE + 0,8192·AE`, isto é q fixo = 1/0,8192 = 1,2207 (sem fonte direta). Agora q(OE)
pela eq. (27). O extrato real, o CO2 e o ABV não mudam; a SG calculada fica mais baixa, tanto mais
quanto maior a atenuação: −0,07 a −0,35 ponto no bench (OG 1,040–1,072, 0,2–0,6 mol/L de CO2).

**Gravar entre lotes**: o `flowLevelFactor` é ajustado pela SG desgaseificada (manual, P7); com a SG
calculada ~0,2–0,3 ponto mais baixa no fim, o fator do lote anterior fica levemente deslocado. Conferir
o ajuste no fim do primeiro lote com o firmware novo.
