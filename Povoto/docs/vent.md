# Vent: ventilação direta do fermentador

Plano: `docs/sem-te-plano.md`; operação e calibração:
`docs/manual-calibracao-manutencao.md`. Código: `src/PressureControl.cpp`
(`planVent()`, `processReliefCycle()`, `processPressure()`), modelo em
`include/VentModel.h` (funções puras; espelho em Python, `tests/vent_model.py`).
O método antigo, com tanque de expansão (TE), está na tag `te-final-2026-10-11`
(`docs/historico-te/`).

## 1. Ciclo

A válvula (saída `PINTRANSFERVALVE`, VT307) ventila o fermentador direto para a
atmosfera: porta P no fermentador, porta A na garrafa testemunha.

- **Disparo**: a pressão chega a `setPoint / √f`, com `f = 1 − alvo` (alvo:
  `ventTargetDropPercent`, 6%); o mesmo dente de serra de antes em torno do
  setpoint. Fora de Conditioning; nas janelas de task, espera o fim da janela.
  Acima de `maximumPressure`, vent de segurança em qualquer modo (sem contagem em
  Conditioning).
- **Abertura**: com uma leitura de pressão nova (`P_on`), o tempo é calculado
  (seção 2) e a válvula abre; o instante é medido com `esp_timer_get_time()` na
  mesma linha do `digitalWrite`.
- **Fechamento**: no loop, quando o tempo passa (um loop atrasado só aumenta a
  queda); o instante também é medido na linha do `digitalWrite`. Tempo real =
  fechamento − abertura.
- **Leitura**: a primeira leitura nova depois do bloqueio de 0,36 s + 0,4 s de
  ruído do solenoide (`pressureAfterRelief`, gás ainda frio). A pressão
  assentada sai do expoente politrópico 1,29 (`adjustedPressureForPostRelief`).
- Um vent nunca sobrepõe outro.

## 2. Tempo de abertura

```
t_estimado = mols(P_on → P_on·(1 − alvo)) / vazão(P̄) + t0     (VentModel::ventSeconds)
t_12%      = o mesmo para a queda de segurança (12%) com o menor Vh plausível
t          = máx(mín(t_estimado, t_12%), 1,5 s)
```

- Vh do tempo: o headspace aplicado (média de 24 h, valor retido ou EMA; antes
  dos 3 primeiros vents, FMTVolume − cerveja informada).
- Menor Vh plausível: o menor entre o aplicado, a EMA e FMTVolume − cerveja
  informada.
- **O mínimo de 1,5 s (`ventMinSeconds`) vence a salvaguarda** (decisão de
  11/10/2026). Teto absoluto: 30 s.
- A queda obtida é aceita, mesmo diferente do alvo.
- Log Relief: `VentEstimatedSeconds`, `VentSafetySeconds`, `VentPlannedSeconds`,
  `VentLimit` (`estimate` / `minimum` / `safety`), `VentHeadspaceForTime`.

## 3. Modelo de vazão

- **Curva**: τ(P) = a0 + a1·P + a2·P² + a3·P³ (s, P manométrica), a constante de
  tempo da pressão absoluta de **100 L de ar a 20 °C**, medida pelo ensaio de
  ventilação (fermentador vazio, ar). Fora da faixa de pressão do ensaio
  (`pMin`–`pMax`) vale o valor da ponta: a cúbica não é extrapolada (a de 18/09
  cai de 161 s a 1,9 bar para 113 s a 2,3 bar).
- Vazão molar do ar: `C_ar(P)·P_abs/√T`, `C_ar = 100 / (R·√293,15·τ(P))`.
- **Ar → CO2**: razão do bocal ideal em função da pressão (γ 1,40 / 1,29, massa
  molar): 0,788 no regime sônico (≥ ~0,85 bar), 0,790 a 0,8 bar, 0,796 a 0,5 bar.
- **Z do CO2**: `Z = 1 + B·P/(R·T)`, B pela correlação de Tsonopoulos
  (−0,129 L/mol a 20 °C): 0,992 a 0,5 bar, 0,985 a 1,9 bar. Na vazão, a vazão
  molar de um gás real num bocal é ∝ P/√(Z·T): o fator é **1/√Z** (+0,4% a
  +0,8%). Nos mols do headspace, n = P·V/(Z·R·T).
- **Fator de nível** `flowLevelFactor` (1,0): ajustado entre lotes pela SG
  desgaseificada (manual, P7).
- **Curva padrão** (antes do primeiro ensaio): o ensaio de 18/09 (129,6 L, ar,
  Patm 0,9286 bar, 10 liberações de 20 s de 1,98 a 0,36 bar, t0 não mensurável):
  a0..a3 = 318,6846; −296,7812; 218,9469; −55,9985. Os tempos para 6% batem com a
  tabela da seção 7 do plano (1,2 / 1,5 / 1,7 s com 20 L; 1,8 / 2,3 / 2,6 s com
  30 L; 3,0 / 3,8 / 4,3 s com 50 L, a 0,5 / 1,0 / 1,9 bar).

## 4. Contagem

```
P̄    = média logarítmica de P_on,abs e da pressão lida logo após o fechamento (abs)
fator = (P_on/P̄)^((n−1)/(2n)),  n = 1,29        (resfriamento: √(T0/T) em P̄)
mols  = C_ar(P̄)·razão(P̄)/√Z(P̄)·nível · P̄/√T_gás · fator · (t_real − t0)
```

- Sem Vh e sem ler o sensor durante o transiente.
- **T_gás**: o Dallas do headspace se atribuído e válido; senão Tferm
  (`docs/thermometers.md`). Henry e o CO2 dissolvido continuam com a do líquido.
- Correção de massa líquida (`liquidMassInGasVentingPercent`) aplicada depois.
- Só em Fermenting (`VentCounted` = `flow`); nos outros modos o vent controla a
  pressão sem contar (`none`).
- **Bench** (`python tests/vent_bench.py`): contra uma simulação com o gás real
  expandindo pelo politrópico que define o 1,29, a vazão instantânea e o t0, a
  contagem erra ≤ 0,11% a 0,5 / 1,0 / 1,9 bar com Vh de 20 / 30 / 50 L. Com uma
  curva plana é exata (< 0,002%): o resto vem de avaliar C em P̄ quando a cúbica
  de 18/09 muda ~8% dentro de um vent perto de 1,9 bar. Sensibilidades (1,9 bar,
  30 L): t0 10 ms errado +0,28%; válvula 3% mais lenta +3,0%; gás 2 K mais quente
  +0,25%; gás 6 K mais quente com a T do líquido +1,0%; expoente real 1,20 ou
  1,40: < 0,25% na contagem (mas ±7% no Vh aparente).

## 5. Vh aparente e headspace

`Vh_aparente = mols·R·T_gás / Δ(P/Z)`, com Δ entre `P_on` extrapolada (a
subida pela fermentação durante a abertura, como antes) e a pressão assentada.

- Alimenta a média de 24 h e a EMA com as regras de antes (janelas de task não
  medem; os 3 primeiros vents inicializam a EMA pela média geométrica).
- Coluna `HeadSpaceApparent` (sempre) e `HeadSpaceMeasured` (quando alimentou a
  média): o monitor passivo de sujeira é a média de 24 h (`HeadSpaceDaily`); o
  volume de cerveja quase não muda na fermentação.
- O atraso da leitura (0,4–0,8 s, recuperação térmica já começando) deixa o Vh
  aparente 0,2–0,5% alto no bench, mas o 1,29 foi ajustado com essa mesma
  leitura.

## 6. Verificações (docs/alarms.md)

- **Vent sem efeito**: queda assentada < 0,25× a esperada pelos mols contados e
  pelo headspace de referência. Não é contado pelo modelo de vazão: os mols são a
  perda do lado do fermentador (queda × Vh de referência / RT; ~0 se a válvula
  não abriu) e o Vh não é alimentado. Dois seguidos: alarme.
- **Válvula presa aberta**: 30 s depois da leitura de fechamento, a pressão caiu
  mais de 5 mbar (a recuperação térmica e a fermentação só sobem a pressão):
  alarme na hora.
- A antiga verificação de queda grande (vazamento entre válvula e TE) saiu.

## 7. Parâmetros (página Calibration)

| Parâmetro | Chave NVS | Padrão |
|---|---|---|
| Queda alvo (%) | `ventDrop` | 6 |
| Abertura mínima (s) | `ventMinSec` | 1,5 |
| Queda de segurança (%) | `ventSafeDrop` | 12 |
| Fator de nível | `flowLevel` | 1,0 |
| Curva de vazão e t0 (blob) | `ventCurve` | 18/09 |
| Curva de referência (blob) | `ventRefCurve` | nenhuma |
| Massa líquida na ventilação (%) | `liquidGasMass` | 0,6 |

## 8. Log Relief

O nome da planilha continua **Relief**. Colunas novas: `GasTemperature`,
`GasTemperatureSource`, `VentEstimatedSeconds`, `VentSafetySeconds`,
`VentPlannedSeconds`, `VentLimit`, `VentHeadspaceForTime`, `VentRealSeconds`,
`VentT0Seconds`, `VentMeanPressure`, `VentCoolingFactor`, `VentFlowConstant`,
`FlowLevelFactor`, `VentFlowMols`, `VentCounted`, `VentDropPercent`,
`VentTargetDropPercent`, `HeadSpaceApparent`. Saíram as do TE
(`TankReferenceTemperature`, `ReliefVolume`, `AdjustedEquilibriumPressure`,
`TankPressureAtClose`, `GasVentingResidual`, `PreviousTank*`,
`ExpansionTankResidualMoles`, `PressureDropFactor`, `Gas*` do ciclo do tanque,
`VentingElapsedAtLogSeconds`, `KCO2FromBeerVolume`,
`ExpansionTankTemperature`).
