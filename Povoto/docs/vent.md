# Vent: ventilação direta do fermentador

Plano: `docs/sem-te-plano.md`; operação e calibração:
`docs/manual-calibracao-manutencao.md`. Código: `src/PressureControl.cpp`
(`planVent()`, `processReliefCycle()`, `processPressure()`), modelo em
`include/VentModel.h` (funções puras; espelho em Python, `tests/vent_model.py`).
Fallback e histórico (tag do método antigo, colunas que saíram): última seção.

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
t_estimado = mols(P_on → P_on·(1 − alvo)) / vazão(P̄)     (VentModel::ventSeconds)
t_seg      = o mesmo para a queda de segurança (2 × a queda alvo) com o menor Vh plausível
t          = máx(mín(t_estimado, t_seg), 1,5 s)
```

- Vh do tempo: o headspace aplicado (média de 24 h, valor retido ou EMA; antes
  dos 3 primeiros vents, FMTVolume − cerveja informada).
- Menor Vh plausível: o menor entre o aplicado, a EMA e FMTVolume − cerveja
  informada.
- **O mínimo de 1,5 s (`VENT_MIN_SECONDS`, fixo no código) vence a salvaguarda** (decisão de
  11/10/2026). Teto absoluto: 30 s.
- A queda obtida é aceita, mesmo diferente do alvo.
- Log Relief: `VentEstimatedSeconds`, `VentSafetySeconds`, `VentPlannedSeconds`,
  `VentLimit` (`estimate` / `minimum` / `safety`), `VentHeadspaceForTime`.

## 3. Modelo de vazão

- **Curva**: τ(P) = k / ψ_ef(P) (s, P manométrica), a constante de tempo da pressão absoluta de
  **100 L de ar a 20 °C**, medida pelo ensaio de ventilação (fermentador vazio, ar). É um modelo
  **físico de dois elementos em série** ("modelo K–β": K do bocal, β da restrição a jusante), não um polinômio (decisão de 21/10/2026; a cúbica da
  versão anterior era só empírica, sem sentido fora da faixa do ensaio e dependente da geometria):
  - **válvula**: bocal ideal (`VentModel::nozzlePsi`, γ 1,40 no ar), vazão molar ∝ P_m ψ(r), com
    r = P_i/P_m, P_i a pressão entre a válvula e a saída; sônica abaixo da razão crítica;
  - **saída**: restrição dissipativa **quadrática, sem estrangulamento**,
    (P_i² − Patm²)/Patm = β·n² (n: vazão em unidades de P_m·ψ);
  - a mesma vazão atravessa os dois: P_m²·Patm·β·ψ(r)² = P_m²·r² − Patm² (uma raiz em
    (Patm/P_m, 1), por Newton com bisecção, ~5 iterações: `VentModel::seriesPsi`). ψ_ef(P) = ψ(r).
    **k** é a capacidade do bocal e **β** a resistência que segura a vazão depois dele. Na faixa
    de uso o bocal nunca chega a ser sônico (r ≈ 0,55–0,95); por isso a curva é quase plana acima
    de ~1 bar e sobe em pressão baixa.
  - **sem faixa**: vale em qualquer pressão acima de 5 mbar manométricos (`TAU_MIN_GAUGE`);
    `pMin`–`pMax` só dizem onde o ensaio mediu (informativos, e usados para marcar vents fora
    da faixa medida).
  - Escolha entre 10 famílias nos dois primeiros ensaios de 20–21/10/2026 (48 e 41 liberações, com o
    silenciador): com um fator de escala livre por ensaio (o de 21/10 saiu 2,6% mais rápido), a
    válvula + saída quadrática (2 parâmetros) ficou no mesmo resíduo (70 ms ponderados) do
    modelo em série com orifício e Cd linear (3 parâmetros) e muito abaixo do cúbico (101 ms,
    4 parâmetros). Em baixa pressão (treino ≥ 0,4 bar, previsão < 0,4 bar) o viés a 0,15–0,30 bar
    foi +1,6% contra +12,8% da cúbica extrapolada. O Cd linear, quando liberado, vai a +0,8%: não é
    necessário. Ressalvas: a seleção foi feita nos mesmos dois ensaios e k e β são correlacionados
    (por ensaio, k = 77,7 e 84,6 s e β = 1,22 e 0,63, com curvas quase iguais).
  - **Ensaio sem o silenciador (21/10/2026 15:58, 48 liberações)**: previa-se que β caísse e a
    vazão subisse até ~27% a 0,2 bar. **Não foi o que se viu**: k = 80,4 s, β = 0,83, a mesma forma
    dos ensaios com silenciador (a forma dos dois primeiros descreve o terceiro com 67,8 ms contra
    67,6 ms do ajuste próprio), e só um fator uniforme de 4% mais rápido em todas as pressões
    (0,960; o ensaio de 21/10 de manhã já tinha dado 0,974 contra o de 20/10). Portanto **o
    silenciador não é a resistência que β representa**: β e k descrevem a válvula e suas
    conexões; o silenciador pesa ≤ ~3–4%, da ordem da variação entre ensaios.
  - k e β indicam o estado do conjunto válvula + conexões (e do silenciador, se pesar), não só um
    elemento; os dois ficam no histórico dos ensaios.
- Vazão molar do ar: `C_ar(P)·P_abs/√T`, `C_ar = 100 / (R·√293,15·τ(P))`.
- **Ar → CO2**: √(M_ar/M_CO2) (o bocal e a saída vão com 1/√M) vezes a razão da função de
  vazão do par (γ 1,29 / 1,40), `VentModel::co2AirRatio(β, P, Patm)`: **0,80–0,81 com a saída
  quadrática** (0,798 a 2 bar, 0,803 a 1 bar, 0,806 a 0,5 bar); 0,788 (sônico) a 0,796 (0,5 bar)
  só para o bocal sozinho (o valor da versão anterior, ~2% mais baixo; o fator de nível absorve).
  **Não validada com CO2** (sem ensaio com CO2, decisão do usuário): o erro da conversão entra no
  fator de nível, ajustado pela SG de fermentações reais.
- **Z do CO2**: `Z = 1 + B·P/(R·T)`, B pela correlação de Tsonopoulos
  (−0,129 L/mol a 20 °C): 0,992 a 0,5 bar, 0,985 a 1,9 bar. Na vazão, a vazão
  molar de um gás real num bocal é ∝ P/√(Z·T): o fator é **1/√Z** (+0,4% a
  +0,8%). Nos mols do headspace, n = P·V/(Z·R·T).
- **Fator de nível** `flowLevelFactor` (1,0): ajustado entre lotes pela SG
  desgaseificada (manual, P7).
- **Sem t0** (decisão de 21/10/2026): o atraso de abertura da válvula é tomado igual ao de
  fechamento (t0 = δ_abre − δ_fecha = 0), então o tempo aberto medido é o tempo de vazão. O t0
  ajustado nos quatro ensaios foi +4,7, +9,5, +12,6 e −89 ms (erro padrão 12–18 ms cada), com uma
  dispersão entre ensaios muito maior que o erro de cada um: o ajuste não o mede (o P assentado das
  liberações curtas pesa demais). Um t0 real de ±20 ms erra os mols em ±1,0% a 2 s de abertura
  (±1,3% a 1,5 s; ±0,7% a 3 s; `tests/vent_bench.py`). A suposição entra na incerteza (±1%).
- **Curva padrão** (antes do primeiro ensaio): os três ensaios com o silenciador de 20–21/10/2026
  (130,9 L, ar, Patm 0,9286 bar, 0,14–2,08 bar) ajustados juntos **sem t0 e sem a primeira
  liberação**, cada ensaio com a sua escala (1,000, 0,975 e 0,975; desvio de 1,5%): k = 81,6 s (a
  forma ajustada, 83,0 s, vezes a escala média 0,983) e β = 0,88. Ajuste: `python tests/venting_fit.py`.

## 4. Contagem

```
P̄    = média logarítmica de P_on,abs e da pressão lida logo após o fechamento (abs)
fator = (P_on/P̄)^((n−1)/(2n)),  n = 1,29        (resfriamento: √(T0/T) em P̄)
mols  = C_ar(P̄)·razão(P̄)/√Z(P̄)·nível · P̄/√T_gás · fator · t_real
```

- Sem Vh e sem ler o sensor durante o transiente.
- **T_gás**: o Dallas do headspace se atribuído e válido; senão Tferm
  (`docs/thermometers.md`). Henry e o CO2 dissolvido continuam com a do líquido.
- **Vapor no gás ventilado** (sem parâmetro; substitui a antiga "massa líquida"): o gás
  que sai é saturado de vapor na temperatura do líquido (Tferm). Fração de vapor
  `y = (p_água + p_etanol) / P_abs`, com P_abs = P̄ do vent + Patm
  (`VentModel::ventVaporFraction`):
  - `p_água = a_w · p_sat,água(Tferm)`, Buck, a_w = 0,98 (23,4 mbar a 20 °C; 12,3 a 10 °C);
  - `p_etanol = x_etanol · γ · p_sat,etanol(Tferm)`, Antoine, γ = 5 (diluição infinita em
    água), x_etanol pela fração molar a partir do ABV que o firmware já estima
    (`beerABV`; etanol 0,789 g/mL, resto do volume como água): 4,7 mbar com 5% ABV a
    20 °C, ~0 no início do lote.
  - Mols de CO2 = mols contados × (1 − y), tanto na contagem pela vazão quanto na do
    lado do fermentador (aplicada ao valor escolhido). O Vh aparente e a razão entre
    as duas contagens continuam com os mols totais (a queda inclui o vapor).
  - Exemplos: y = 2,0% (20 °C, 0,2 bar, sem ABV), 2,4% (5% ABV), 1,0% (5% ABV, 1,9 bar),
    0,75% (10 °C, 5% ABV, 1 bar). `python tests/vent_bench.py` confere as pressões de
    saturação e estes valores.
  - `/ventstatus` mostra a fração do último vent.
  - A mesma fração também é usada no Henry (`CO2DissolvedMols`): a lei vale para a pressão
    parcial do CO2, `P_abs·(1 − y)` (`docs/dissolved-co2.md`).
- Só em Fermenting; nos outros modos o vent controla a pressão sem contar
  (`VentCounted` = `none`).
- **Duas contagens, sempre**: a do modelo de vazão (`VentFlowMols`) e a do lado
  do fermentador (`VentFermenterSideMols` = queda assentada de P/Z × headspace de
  referência / RT, o aplicado: média de 24 h). A razão lado do fermentador /
  vazão (`ReliefDropRatio`, = queda medida / esperada) vai para o log Relief em
  todo vent. **Razão < 0,8**
  (`VENT_FERMENTER_SIDE_RATIO`): a saída passou menos que a curva diz (válvula
  que não abriu, saída parcial ou totalmente obstruída):
  o vent é contado pelo lado do fermentador (`VentCounted` = `fermenter-side`) e
  fica fora da média de 24 h e da EMA. Contado pela vazão, uma queda de 60% da
  esperada seria contada 67% a mais. A troca só vale onde a verificação vale
  (docs/alarms.md). **Limite da razão**: a referência é o Vh de 24 h, que vem do
  próprio modelo de vazão; a razão só enxerga mudanças bruscas (válvula que não
  abre, obstrução súbita, líquido adicionado). A sujeira gradual não aparece
  nela: aparece no `HeadSpaceDaily` subindo e no ensaio de fim de lote. O rebase
  da média de 24 h no Dump e a limpeza dela no Liquid / Dry Hopping / Dynamic
  Hopping continuam valendo; sem eles, uma adição de líquido daria razão ~0,83
  por até 24 h.
- **Fora da faixa calibrada**: se `P_on` passa da pressão máxima do ensaio
  (`pMax` da curva) ou a leitura de fechamento fica abaixo da mínima (`pMin`),
  com 5 mbar de tolerância, a curva usou o valor da ponta (acima) ou a
  extrapolação pelo bocal (abaixo; seção 3): coluna `VentOutOfRange`
  (`above` / `below`) e contagem em `/ventstatus`. Na ponta alta o erro é pequeno
  (sônico); na baixa o erro cresce com a distância de `pMin` (a escala não inclui
  a variação do coeficiente de descarga): refazer o ensaio cobrindo a faixa (manual, P5).
- **Bench** (`python tests/vent_bench.py`): contra uma simulação com o gás real
  expandindo pelo politrópico que define o 1,29 e a vazão instantânea, a
  contagem erra ≤ 0,11% a 0,5 / 1,0 / 1,9 bar com Vh de 20 / 30 / 50 L. Com uma
  curva plana é exata (< 0,002%): o resto vem de avaliar C em P̄ quando a cúbica
  de 18/09 muda ~8% dentro de um vent perto de 1,9 bar. Sensibilidades (1,9 bar,
  30 L): t0 real de ±20 ms (suposto 0) ±1,0%; válvula 3% mais lenta +3,0%; gás 2 K mais quente
  +0,25%; gás 6 K mais quente com a T do líquido +1,0%; expoente real 1,20 ou
  1,40: < 0,25% na contagem (mas ±7% no Vh aparente). Abaixo de `pMin` o bench confere a
  continuidade em `pMin`, o τ de ponta acima de `pMax`, a escala do bocal e a contagem de um
  vent a 0,25 bar (≤ 0,5%).

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
- O Vh aparente **herda o nível da vazão**: ele é proporcional aos mols contados,
  então mudar o `flowLevelFactor` (ajuste pela SG) desloca o Vh aparente, a média
  de 24 h e o headspace aplicado na mesma proporção; uma curva de vazão errada
  aparece também como um Vh errado. O significado de `HeadSpaceMeasured` não
  mudou (o valor de cada vent que alimentou a média).

## 6. Verificações (docs/alarms.md)

- **Razão < 0,8** (`ReliefCheck` = `partial`): contado pelo lado do fermentador,
  fora da média de 24 h e da EMA. Um vent isolado só fica marcado no log; **dois
  seguidos** abaixo de 0,8 dão alarme.
- **Vent sem efeito**, razão < 0,25 (`no-effect`): contado do mesmo jeito; conta
  para os dois seguidos.
- **Válvula presa aberta**: 30 s depois da leitura de fechamento, a pressão caiu
  mais de 5 mbar (a recuperação térmica e a fermentação só sobem a pressão):
  alarme na hora.

## 7. Diagnóstico: `/ventstatus`

Página própria (link na página Calibration), fora do `/getstatus`: o Povoto
monta o `/getstatus` no seu próprio handler, num buffer **estático** de
`MAXSTATUSLEN` + 1 (8193 B, `webStatusBuffer` em `Povoto.cpp`; o handler da
biblioteca, com o buffer na pilha, não é usado pelo Povoto), e a página fica
com ≥ 10% de folga (`tests/check_status_sizes.py`). Aumentar o `MAXSTATUSLEN`
custaria RAM estática no Povoto e pilha da task web nos firmwares que usam o
handler da biblioteca. Mostra os parâmetros e as curvas, a Patm e a
T_gás em uso, o próximo vent (tempo, Vh, Vh mínimo plausível, aviso de fora da
faixa), o último vent (tempos estimado/segurança/planejado/real, pressões,
P̄, resfriamento, constante de vazão, as duas contagens e a razão, o que foi
contado, Vh aparente, queda, fora da faixa), as verificações (razão, alarme,
válvula presa aberta, vents fora da faixa desde o boot) e o estágio do ciclo da
válvula. Atualiza a cada 10 s. O `/getstatus` fica com uma linha: os dois
alarmes do vent e a contagem de vents fora da faixa.

## 8. Parâmetros (páginas Settings e Calibration)

| Parâmetro | Chave NVS | Padrão |
|---|---|---|
| Queda alvo (%) — página Settings | `ventDrop` | 6 |
| Fator de nível — Calibration | `flowLevel` | 1,0 |
| Expoente efetivo de ventilação — Calibration | `ventExponent` | 1,29 |
| Curva de vazão k, β (blob de 20 bytes; editável na página Calibration, entre lotes, ou pelo "Save curve" do ensaio) | `ventFlow2` | ensaios de 20–21/10 (k 81,6; β 0,88) |
| Curva de referência (blob) | `ventFlowRef2` | nenhuma |

A abertura mínima (1,5 s) e a queda de segurança (2 × a alvo) são fixas no código. As chaves antigas
`ventMinSec`, `ventSafeDrop` e `liquidGasMass` ficam na NVS, sem uso (fallback).

## 9. Log Relief

O nome da planilha continua **Relief**. Colunas novas: `GasTemperature`,
`GasTemperatureSource`, `VentEstimatedSeconds`, `VentSafetySeconds`,
`VentPlannedSeconds`, `VentLimit`, `VentHeadspaceForTime`, `VentRealSeconds`,
`VentMeanPressure`, `VentCoolingFactor`, `VentFlowConstant`,
`FlowLevelFactor`, `VentFlowMols`, `VentFermenterSideMols`, `VentOutOfRange`,
`VentCounted`, `VentDropPercent`, `VentVaporFraction` (no lugar de `LiquidMassInGasVentingPercent`),
`VentTargetDropPercent`, `HeadSpaceApparent`. `EjectedMolsWithVaporAtLog` são os mols contados
do vent com o vapor (`EjectedMolsAtLog` é só o CO2).

## 10. Válvula: Valve da página Control e Relief once

- **Valve** (Auto / Open / Closed; não é gravado, um boot volta para Auto):
  - *Open* mantém a válvula aberta por no máximo 10 min e volta para Auto. A pressão cai e nada
    é contado nem entra no CO2; a verificação de válvula presa aberta fica desligada
    enquanto durar. Um relief em andamento termina antes.
  - *Closed* suspende os reliefs automáticos. O relief acima da pressão máxima de segurança
    **continua**, qualquer que seja a opção.
  - Uma rotina de calibração que comanda a válvula (ensaio de ventilação, limpeza, volume pelo
    tanque de expansão) cancela a opção (volta para Auto), e a página recusa Open/Closed
    enquanto ela roda.
- **Relief once** (POST): recusado durante uma rotina de calibração, com o Valve fora de Auto
  ou com um relief em andamento. Em OFF funciona.

## 11. Fallback e histórico

O método antigo, com tanque de expansão (TE), está na tag `te-final-2026-10-11`
(`docs/historico-te/`; fallback só entre lotes). Para a volta ser limpa, este firmware não
reaproveita as chaves de NVS do TE (`docs/NVS-storage.md`).

- A verificação de queda grande do TE (vazamento entre a válvula e o tanque) saiu.
- Colunas do log Relief que saíram: `TankReferenceTemperature`, `ReliefVolume`,
  `AdjustedEquilibriumPressure`, `TankPressureAtClose`, `GasVentingResidual`, `PreviousTank*`,
  `ExpansionTankResidualMoles`, `PressureDropFactor`, `Gas*` do ciclo do tanque,
  `VentingElapsedAtLogSeconds`, `KCO2FromBeerVolume`, `ExpansionTankTemperature` e
  `LiquidMassInGasVentingPercent` (agora `VentVaporFraction`).
