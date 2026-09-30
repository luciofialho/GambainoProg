# CO2 dissolvido: estados e cálculo

Implementação em `src/PressureControl.cpp` (bloco `CO2DissolvedState`, `recomputeDissolvedCO2MolsFromCurrentState()`, `gasPhaseCO2Rate()`). Bench: `python tests/dissolved_co2_bench.py`.

O CO2 dissolvido (`CountersData.CO2InSolution`, D) entra no balanço que dá a densidade e o gCO2/L/d:

```
total = totalMolsEjected + CO2InSolution + headSpaceCO2Mols + expansionTankInventoryMoles()
```

A fase gasosa medida é `G = totalMolsEjected + headSpaceCO2Mols + expansionTankInventoryMoles()` (`gasPhaseCO2Mols()`), que não depende do modelo de D. H é o equilíbrio de Henry de `CO2DissolvedMols(P, SG, T, volume)`.

## 1. Estados

| Valor (`co2Mode`) | Estado | Rótulo no log | D |
|---|---|---|---|
| 2 | inicial | `initial` | Henry médio de 30 min + supersaturação (seção 2a) |
| 1 | equilíbrio | `immediate` | Henry médio de 30 min + supersaturação (seção 2a) |
| 0 | half-life | `half-life` | híbrido pela medição (seção 3) |
| 3 | half-life armado | `half-life-armed` | igual ao half-life; volta ao equilíbrio mais fácil |

Os valores 0 e 1 mantêm o significado da versão anterior (um aparelho atualizado no meio do lote continua no mesmo modo). Um lote novo começa em 2.

## 2. Transições

| De | Para | Condição |
|---|---|---|
| inicial | equilíbrio | ≥ 3 reliefs em Fermenting |
| equilíbrio | half-life | taxa da fase gasosa < 0,3 g/L/d por **3 h** ininterruptas |
| half-life | equilíbrio | taxa > 0,5 g/L/d por **6 h** ininterruptas |
| half-life | armado | `BatchData.addedPlato` aumentou (página Batch) |
| armado | equilíbrio | taxa > 0,3 g/L/d por **1 h** ininterrupta |
| armado | half-life | 7 dias desde o armado (época NTP em `co2ArmedAt`; sem NTP, espera) |

Uma nova adição no estado armado renova os 7 dias. No equilíbrio ou no inicial, a adição não muda nada (já há geração).

"Ininterrupta": qualquer amostra fora da condição, ou sem decisão, reinicia a contagem. Na dúvida, o estado não muda.

**Taxa da fase gasosa** (`gasPhaseCO2Rate()`): a mesma janela e a mesma média nas pontas do gCO2/L/d (docs/gco2-rate.md), aplicadas a G em vez do total. Sem decisão (NAN) quando:
- há menos de 60 amostras (~1 h depois do boot);
- há uma task aberta, ou a última terminou há menos de 80 min (janela + 10 min);
- alguma amostra da janela tem um degrau externo: G subiu mais que o equivalente a 50 g/L/d em uma amostra sem relief (gás injetado fora de uma task);
- volume de cerveja inválido.

Fora de Fermenting não há decisão; no Conditioning o estado fica congelado (docs/conditioning.md). Calibração com os lotes 159 e 160: na fermentação ativa a taxa fica muito acima de 0,5; no fim do 159 o half-life entra em 14/09 03:26 e nenhuma sequência > 0,5 passou de 73 min até o fim do lote (dry hops, dynamic hops, cold crash), longe das 6 h. Os reboots do 160, que no critério antigo derrubavam o modo para half-life por ~30 min, não mudam o estado.

## 2a. Supersaturação (estados inicial e equilíbrio)

O CO2 é produzido dentro da cerveja e só sai para o gás enquanto a cerveja está acima da saturação. O fluxo de gás que sai é F = kLa·(D − Henry). Então:

```
D = Henry médio de 30 min + x,   x = τs · F,   τs = 1/kLa
```

- **Henry médio de 30 min**: média, nas amostras de 60 s dos últimos 30 min, de Henry na pressão e na temperatura de cada amostra. No ciclo de relief a média do dente de serra é constante; numa subida ou descida de pressão, ou num resfriamento, ela acompanha, com o mesmo atraso do fluxo.
- **F**: inclinação da fase gasosa medida (ejetado + headspace + tanque de expansão) nos últimos 30 min, mol/dia.
- **τs**: dois parâmetros na página de calibração, em horas, padrão 2,0: liberação (F > 0, gás saindo) e absorção (F < 0, cerveja absorvendo, por exemplo no resfriamento). 0 = equilíbrio instantâneo. |x| é limitado a metade do Henry.
- **x congelado**: com uma task ou um degrau externo na janela, ou depois de um boot (x vem dos contadores), x fica parado até F voltar. A mudança nesse momento sai da janela do gCO2 (sem pico na tela), mas **conta no SG**: é CO2 que está na cerveja. No primeiro boot desta versão x passa de 0 a τs·F (~1,7 mol a 9 g/L/d, ~0,75 pt de SG): é o CO2 que o modelo de equilíbrio instantâneo nunca contou, porque ele deixava o SG alto nesse valor durante toda a fermentação ativa. Num reboot comum, o x gravado mantém a continuidade e o salto é só o que x mudou durante a pausa.

**Por quê** (lote 160, 30/09): com Henry instantâneo, uma subida de setpoint (0,8 → 1,5 bar, sem reliefs) mostrava 18–25 g/L/d com a produção em ~8,6. O gás medido sozinho caiu de 9 para 4 g/L/d durante a subida: a cerveja usava o excesso que já tinha para acompanhar o equilíbrio mais alto, e o modelo instantâneo contava isso como produção nova. Com τs = 2 h o espelho do firmware mostra 8,8–11,8 na subida. Bench (o CO2 nasce na cerveja e sai com transferência de 2 h), para uma produção de 8,6: subida de setpoint 7,0–9,2 (instantâneo 4,6–17,6), resfriamento de 2 °C/h 8,1–9,0 (instantâneo 6,7–11,1). Atualização a partir do modelo instantâneo com a fermentação ativa: o CO2 contado estava 1,62 mol abaixo do real (SG 0,72 pt alto) e fica a +0,007 mol quando x entra, sem pico na tela. No fim da fermentação a cerveja ainda libera o excesso por horas: a saída para half-life leva ~13 h depois de a produção parar, e nesse tempo a liberação não conta como produção (0,11 mol contados para 0,15 gerados em 16 h).

Antes da versão com supersaturação, o equilíbrio usava Henry no limiar de relief (setpoint/√f), que saltava quando o setpoint mudava: em 30/09 12:18 (setpoint 0,8 → 1,9 por uma regra) foram +4,29 mol contados como produção, −1,9 pt de SG.

### Estimador de τs (recalibração)

A cada 10 amostras, numa janela de 3 h que termina 15 min atrás (F e Henry centrados em ±15 min, no mesmo instante), uma regressão supõe a produção R constante:

```
gás + Henry = c + R·t − τs_liberação·F⁺ − τs_absorção·F⁻
```

Cada termo só entra se F daquele sentido variou pelo menos 2,5 g/L/d na janela; sem variação (ciclo de relief estável) não há o que estimar e as colunas ficam vazias. A janela inteira precisa estar em Fermenting, em equilíbrio ou inicial, sem task e sem degrau externo. No 160 (subida de 30/09) a estimativa converge para 2,07 ± 0,09 h, com R = 9,4 g/L/d, coerente com os ciclos antes da subida.

**Como recalibrar** (também como dica na página de calibração): numa subida de pressão, descida de pressão ou resfriamento, ler no log Cold `TauDesEst` (gás saindo) ou `TauAbsEst` (absorvendo) nas linhas em que `TauEstFRangeDes`/`TauEstFRangeAbs` ≥ 2,5, o erro padrão é pequeno perto da estimativa e `TauEstRate` bate com o gCO2/L/d dos ciclos antes da mudança. Usar o valor do fim da mudança (quando a janela de 3 h a cobre) e repetir em alguns eventos antes de mudar o parâmetro.

## 3. Half-life híbrido

Sem geração confirmada, D só muda com a fase gasosa medida, limitado por H na pressão atual:

- **G sobe** (ΔG > 0): o gás sai primeiro do excesso do líquido, `liberado = min(ΔG, max(0, D − H))`, `D −= liberado`. O resto é produção.
- **G cai** (ΔG < 0): o gás vai para o líquido até o equilíbrio, `D += min(−ΔG, max(0, H − D))`. O resto é dívida (no `CO2MolsProducedPerLiter`).

Consequências:
- Cold crash: a queda de G é absorvida (H sobe com o frio), não aparece como consumo. No 159, o artefato do cold crash cai de +1,84 mol (0,8 pt, como no firmware antigo) para ~0.
- Hop creep: com D ≈ H, a geração lenta aparece em G e é contada. No bench, 94% de 0,2 g/L/d em 5 dias.
- Não há meia-vida: a velocidade vem da medição. O antigo `FMTData.co2TransferTime` foi removido (a chave `co2TransferTime` fica esquecida no NVS, sem efeito).

**Banda morta:** variações de G menores que 0,05 mol em relação à baseline são ignoradas (a baseline fica). Sem ela, a oscilação de G (ciclos de frio: o gás do headspace muda de temperatura mais que a cerveja; ruído do sensor) vira uma catraca: cada queda é absorvida e cada subida é contada como produção. No bench, +0,39 pt espúrios em 3 dias sem a banda; ~0 com ela. 0,05 mol ≈ 0,02 pt em 100 L.

**Baseline de G inválida** (a próxima amostra só define a baseline): nos 2 primeiros minutos depois do boot, na troca de estado, no restore dos contadores (reboot ou edição na página Counters) e quando o headspace aplicado muda (o `headSpaceCO2Mols` é recalculado com outro volume, não é movimento de gás).

## 4. Tasks

| Task | No half-life / armado |
|---|---|
| Dry hopping (4), Dynamic hopping (5), inclusive a janela de nucleação | todo gás que aparece sai do líquido: `D −= ΔG` até 0 (a nucleação libera CO2 dissolvido, não é produção). Depois da janela, a reabsorção segue a seção 3. |
| Dump (1) | D fica parado; no fim, D é escalado pelo volume de cerveja depois/antes (`scaleDissolvedCO2ForBeerVolume()`), com rebase do buffer do gCO2 |
| Gas (2), Liquid (3) | D fica parado (gás/volume mudam de forma desconhecida) |

Nos estados de equilíbrio e inicial, D segue Henry durante as tasks, como antes.

Reliefs abertos durante qualquer task (inclusive a janela de nucleação) não medem o headspace: nem a EMA nem a média de 24 h. Status `skipped_task_window` no log Relief.

Nucleação mais longa que a janela: o que sai depois da janela, com D < H, conta como produção e volta como dívida quando o gás é reabsorvido. No bench (0,27 mol em 90 min, janela de 40 min): +0,06 pt no pico, transitório.

## 5. Persistência

- `CountersData.co2Supersat` (chave `co2Supersat`): x, gravado com os contadores; no boot o dissolvido continua com ele até F voltar (~30 min).
- `FMTData.supersatTauDesorbHours` / `supersatTauAbsorbHours` (chaves `supersatTauD` / `supersatTauA`, 0–24 h, padrão 2,0), também no backup JSON.

- `CountersData.co2DissolvedMode` (chave `co2Mode`, 0..3; outro valor lido vira 2) é gravado a cada troca de estado.
- `CountersData.co2ArmedAt` (chave `co2ArmedAt`): época NTP local do armado; 0 fora do armado ou armado sem NTP (é preenchido na primeira hora NTP válida).
- Depois de um reboot o estado volta igual; o buffer da taxa recomeça (1 h sem decisão) e as contagens de tempo recomeçam.
- **Intervenção manual**: página **Counters** (`/counters`, seção "States"), campo "Dissolved CO2 state". Usa o mesmo caminho das transições automáticas (`setCO2DissolvedState(..., "manual")`): grava, marca a hora do armado, invalida a baseline do gás e, ao entrar no equilíbrio, faz o rebase do buffer do gCO2. O estado escolhido vale até os critérios automáticos o mudarem (por exemplo, equilíbrio escolhido à mão com taxa < 0,3 volta a half-life em 3 h).

## 6. Logs e status

- Cold `DissolvedCO2Mode`: rótulo do estado. `DissolvedCO2CriteriaState`: `generating` / `between` / `idle` / `no-decision`. `DissolvedCO2CriteriaElapsedMillis` / `DissolvedCO2ConfirmationMillis`: tempo da condição em curso / tempo exigido.
- Cold `GasCO2Rate`: taxa da fase gasosa, vazia sem decisão.
- Cold, supersaturação: `GasFlux` (F, g/L/d), `Supersat` (x, mol), `TauDesEst`/`TauDesSE` e `TauAbsEst`/`TauAbsSE` (h, vazios sem variação suficiente), `TauEstRate` (produção da regressão, g/L/d), `TauEstFRangeDes`/`TauEstFRangeAbs` (variação de F na janela, g/L/d).
- `/getstatus`: `Supersaturation: x mol; gas flux F g/L/d; tau_s out / in` e a última estimativa válida com a hora.
- `CO2WithReliefsState`, `CO2WithoutReliefsState` ficam vazios e `Pressure10MinAgo` NaN até a revisão das colunas (critérios antigos removidos).
- Serial: `[CO2 STATE] de -> para (motivo)`, `[CO2 REBASE]`, `[CO2 DUMP]`.
- `/getstatus`: `CO2 dissolved estimation: <estado>; gas-phase rate <taxa> g/L/d (<decisão>) for <min> / <min> min`.

## 7. Limitações conhecidas

- Gás injetado fora de uma task, com D < H, conta como produção no half-life (no 159, 28/09: +0,8 mol). A taxa ignora o degrau, mas o balanço não. Use a task Gas.
- A injeção de CO2 na task Gas também entra no total (em qualquer estado, como antes).
- O erro do ar no headspace no início (sem correção) afeta o estado inicial e o equilíbrio como antes.
- Os limiares do armado (0,3 por 1 h) não têm dados de calibração: nenhum lote registrou adição de fermentáveis. Com hop creep, 0,3 por 1 h pode ser atingido sem refermentação (no 159 houve sequências > 0,5 de até 73 min); o custo é voltar ao equilíbrio (Henry no limiar), que foi o comportamento de sempre durante a fermentação.
