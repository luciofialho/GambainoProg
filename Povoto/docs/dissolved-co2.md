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
| 2 | inicial | `initial` | Henry médio de 30 min (seção 2a) |
| 1 | equilíbrio | `immediate` | Henry médio de 30 min (seção 2a) |
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

## 2a. Dissolvido nos estados inicial e equilíbrio

```
D = média, nas amostras de 60 s dos últimos 30 min, de Henry na pressão e na temperatura de cada amostra
```

No ciclo de relief a média do dente de serra é constante; numa subida ou descida de pressão, ou numa mudança de temperatura, ela acompanha. A troca de CO2 entre a cerveja e o gás não é modelada:

- O gCO2/L/d é o saldo de CO2 liberado pela cerveja (docs/gco2-rate.md), medido, sem o dissolvido.
- No SG, a transição dá um erro temporário: numa subida de pressão ou num resfriamento a cerveja absorve devagar e o modelo conta antes; o erro se desfaz quando a cerveja volta ao equilíbrio (bench: +0,47 pt no pior momento de uma subida de 0,8 para 1,5 bar a 8,6 g/L/d, de volta ao nível anterior 10 h depois).

**Histórico** (lote 160, 30/09): o equilíbrio usava Henry no limiar de relief (setpoint/√f), que saltava quando o setpoint mudava (+4,29 mol às 12:18, −1,9 pt de SG). Depois foram testados Henry instantâneo (a produção parecia triplicar durante uma subida), um modelo de supersaturação x = τs·F (acertou a subida com a fermentação forte, errou a com ela fraca: 7,5 contra ~2,5 g/L/d) e um atraso de 2 h (acertou as duas, mas com só dois eventos). Estimar a produção durante as transições exigiria um parâmetro que varia com a atividade e que o Povoto não mede; por isso a taxa passou a ser o saldo liberado pela cerveja, com a transição sinalizada.

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

- As 30 amostras de Henry são restauradas do LittleFS num boot rápido (docs/gco2-rate.md, seção 4), e o dissolvido continua igual; sem o arquivo, o dissolvido usa Henry na pressão atual até a primeira amostra (2 min).

- `CountersData.co2DissolvedMode` (chave `co2Mode`, 0..3; outro valor lido vira 2) é gravado a cada troca de estado.
- `CountersData.co2ArmedAt` (chave `co2ArmedAt`): época NTP local do armado; 0 fora do armado ou armado sem NTP (é preenchido na primeira hora NTP válida).
- Depois de um reboot o estado volta igual; o buffer da taxa recomeça (1 h sem decisão) e as contagens de tempo recomeçam.
- **Intervenção manual**: página **Counters** (`/counters`, seção "States"), campo "Dissolved CO2 state". Usa o mesmo caminho das transições automáticas (`setCO2DissolvedState(..., "manual")`): grava, marca a hora do armado, invalida a baseline do gás e, ao entrar no equilíbrio, faz o rebase do buffer do gCO2. O estado escolhido vale até os critérios automáticos o mudarem (por exemplo, equilíbrio escolhido à mão com taxa < 0,3 volta a half-life em 3 h).

## 6. Logs e status

- Cold `DissolvedCO2Mode`: rótulo do estado. `DissolvedCO2CriteriaState`: `generating` / `between` / `idle` / `no-decision` (os tempos da condição saíram do log; o /getstatus ainda mostra).
- Cold `GasCO2Rate`: taxa da fase gasosa, vazia sem decisão.
- `CO2WithReliefsState`, `CO2WithoutReliefsState` ficam vazios e `Pressure10MinAgo` NaN até a revisão das colunas (critérios antigos removidos).
- Serial: `[CO2 STATE] de -> para (motivo)`, `[CO2 REBASE]`, `[CO2 DUMP]`.
- `/getstatus`: `CO2 dissolved estimation: <estado>; gas-phase rate <taxa> g/L/d (<decisão>) for <min> / <min> min`.

## 7. Limitações conhecidas

- Gás injetado fora de uma task, com D < H, conta como produção no half-life (no 159, 28/09: +0,8 mol). A taxa ignora o degrau, mas o balanço não. Use a task Gas.
- A injeção de CO2 na task Gas também entra no total (em qualquer estado, como antes).
- O erro do ar no headspace no início (sem correção) afeta o estado inicial e o equilíbrio como antes.
- Os limiares do armado (0,3 por 1 h) não têm dados de calibração: nenhum lote registrou adição de fermentáveis. Com hop creep, 0,3 por 1 h pode ser atingido sem refermentação (no 159 houve sequências > 0,5 de até 73 min); o custo é voltar ao equilíbrio (Henry no limiar), que foi o comportamento de sempre durante a fermentação.
