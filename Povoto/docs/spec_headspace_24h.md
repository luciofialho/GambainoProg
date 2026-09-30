# POVOTO – Especificação: headspace por média móvel de 24 h + rebase em eventos

Versão 2 (para produção) · 2026-09-29 · Contexto: `docs/analise-160-headspace-vs-Tenv.md` (rev. 9) e `docs/povoto-backlog.md`.

## 1. Motivação
- O headspace medido a cada relief oscila com o ciclo diário da temperatura ambiente. Em 29/09 (P ≈ 0,83 bar), as médias por hora foram de 29,5 a 31,6 L. O filtro atual (EMA, α = 0,05 ≈ 20 reliefs ≈ 75 min) segue essa oscilação: variou 1,9 L.
- O erro é proporcional à **variação** da T ambiente (histerese), e a integral dele num ciclo diário é ≈ 0. A média de 24 h dos mesmos dados ficou em 30,2–30,6 L.
- O expoente politrópico fixo (1,29) é o melhor valor na média diária. Não mudar.

## 2. O que muda (versão única, já em uso)
1. Toda medição válida de headspace por relief entra numa **média de 24 h com peso igual por hora** (média das médias horárias).
2. Quando a média for válida (≥ 18 h preenchidas), ela passa a ser o **headspace aplicado**: `CountersData.headSpaceVolume`, `beerVolume` e `pressureDropFactor`.
3. Antes disso, e sempre que não houver média válida nem valor retido, o comportamento atual (inicialização geométrica dos 3 primeiros reliefs + EMA) continua valendo, sem mudança.
4. **Rebase** das horas guardadas no dump (que já calcula ΔH); **limpeza** das horas em eventos que mudam o volume sem ΔH conhecido (Liquid, Dry Hopping, Dynamic Hopping).
5. Novos campos de log.

Efeito ao gravar no lote 160 em andamento: as horas começam vazias, então o sistema segue com o EMA atual por ~18 h e depois passa sozinho para a média. Não há degrau acumulado na SG: `CO2MolsProducedPerLiter` integra incrementos com o volume de cada evento, então a troca só afeta os incrementos seguintes. O único degrau é em `headSpaceCO2Mols` (≈ P·ΔVh/RT: 1 L a 0,8 bar ≈ 0,03 mol ≈ 0,01 ponto de SG), desprezível.

## 3. Separar o estado do EMA do valor aplicado
Hoje `applyFilteredHeadspace(h)` (PressureControl.cpp) grava `headspaceFiltered = h` e também aplica `h` em `CountersData.headSpaceVolume`, `beerVolume` e `pressureDropFactor`. Com a média, o EMA precisa continuar rodando por conta própria (fallback e log), sem ser sobrescrito pelo valor diário.
- Criar `static bool applyHeadspaceValue(float h)`, que faz só a parte de aplicar: validação, `CountersData.headSpaceVolume`, `beerVolume`, `pressureDropFactor`. `applyFilteredHeadspace(h)` passa a ser `headspaceFiltered = h; return applyHeadspaceValue(h);` (sem mudança de comportamento para quem já a chama).
- Em `processPressure(true)`, no bloco que hoje atualiza o EMA com `headspaceMeasured` (~linha 2000):
  - continuar atualizando `headspaceFiltered` e `headspaceFilterAlpha` exatamente como hoje;
  - adicionar `headspaceMeasured` à média de 24 h (seção 4) quando for válido e não for relief da determinação de volume;
  - decidir o valor aplicado: `dailyValid ? dailyValue : (dailyHeld ? dailyHeldValue : headspaceFiltered)` → `applyHeadspaceValue(...)`;
  - manter `headspaceUpdated` e `gasHeadspaceUpdateStatus` coerentes (status novo: `"updated_daily_average"` quando o aplicado veio da média).
- A inicialização geométrica dos 3 primeiros reliefs (`lnPressureDropAvg`, bloco do relief 3) continua igual. Os reliefs 1–3 também entram na média de 24 h.
- `restoreDerivedStateFromCounters()` (boot) continua aplicando `CountersData.headSpaceVolume` persistido. Ele já é o último valor aplicado, seja EMA ou média.

## 4. Média de 24 h
Estado (RAM + NVS, seção 6):
```
struct DailyHeadspaceBin_t { uint32_t hourId; uint16_t count; float sum; } __attribute__((packed));
DailyHeadspaceBin_t bins[24];
float heldValue;   // último valor diário válido (NAN = nenhum)
```
- `hourId = NTPEpoch() / 3600` (epoch local; `NTPEpoch()` já é usado em PressureControl.cpp). Se `NTPEpoch() == 0` (NTP inválido), **não** acumular a amostra.
- Ao acumular: `i = hourId % 24`; se `bins[i].hourId != hourId`, zerar a posição (hourId novo, count 0, sum 0); depois `sum += Vh; count++`.
- Cálculo, a cada acumulação e a cada log: considerar só as posições com `count > 0` e `hourNow − 23 ≤ hourId ≤ hourNow`. Posições com hourId > hourNow (salto de relógio) são ignoradas até serem reutilizadas.
  - `dailyHours` = número dessas posições; `dailyValue` = média de `sum/count`.
  - `dailyValid = dailyHours ≥ 18`.
  - Se válido: `heldValue = dailyValue`.
- Estado para log: `"valid"` (média aplicada), `"hold"` (média inválida mas `heldValue` finito: aplica `heldValue`) e `"ema"` (sem média nem valor retido: aplica o EMA).
- `hold` cobre a desaceleração da fermentação, subidas de pressão sem relief e reboots longos. O valor retido só muda quando a média voltar a ser válida ou num rebase/limpeza.
- Constantes nomeadas: `DAILY_HS_MIN_HOURS = 18`, `DAILY_HS_BINS = 24`.

## 5. Eventos
**Dump (type 1):** `applyDumpWindowHeadspaceRecalc()` calcula `headAfter = H_before·P1abs/P2abs` e hoje só aplica quando `headAfter > H_before`. Quando aplicar:
- `ΔH = headAfter − headspaceBeforeL`;
- `rebaseDailyHeadspace(ΔH, "dump")`: `sum_i += ΔH·count_i` em todas as posições com `count > 0`, e `heldValue += ΔH` se finito;
- EMA: manter o comportamento atual (`applyFilteredHeadspace(headAfter)`, α = 0,5);
- o valor aplicado passa a ser `dailyValue`/`heldValue` já com o rebase; ou `headAfter`, se o estado for "ema";
- persistir a média logo depois do rebase.
Não mexer agora no cálculo do ΔH (ver seção 9).

**Liquid (3), Dry Hopping (4), Dynamic Hopping (5):** mudam o volume sem ΔH estimado. Em `endLiquidTask/endDryHoppingTask/endDynamicHoppingTask` (PovotoTasks.cpp):
- `clearDailyHeadspace("liquid"/"dryhop"/"dynhop")`: zerar as 24 posições e `heldValue = NAN`;
- `headspaceFilterAlpha = 0,5`, para o EMA reconvergir rápido (ele já decai sozinho para 0,05);
- persistir.
O sistema volta ao estado "ema" e retoma a média depois de 18 h preenchidas.

**Gas (2):** nada.

**Novo lote** (`resetCountersForNewBatch()` em PovotoData.cpp): zerar tudo e persistir. Não zerar dentro de `resetHeadspaceFilterTracking()`, que também é chamado no boot.

Expor em PressureControl.h: `rebaseDailyHeadspace(float deltaL, const char *reason)` e `clearDailyHeadspace(const char *reason)`. Cada chamada imprime no Serial `[DAILY-HS] rebase/clear motivo ΔH antes→depois`.

## 6. Persistência (NVS, namespace pvt_counters)
- Siga `docs/NVS-storage.md`. Opção recomendada: adicionar a `CountersData_t` (PovotoData.h) um membro `DailyHeadspace_t dailyHs;` (as 24 posições + `heldValue`, empacotado, ~244 bytes). Ler e gravar como blob `"dailyHs"` com `getBytes/putBytes` em `readCountersDataFromEEPROM()`/`writeCountersDataToNIV()`, conferindo o tamanho na leitura (tamanho diferente → defaults: tudo zerado, `heldValue = NAN`). Assim `tests/check_storage_schema.py` continua cobrindo o campo.
- Gravação dedicada `writeDailyHeadspaceToNIV()` (como `writePressureStabilityToNIV`): chamar só ao virar a hora (a primeira acumulação de um hourId novo), no rebase e na limpeza. Não gravar a cada relief. `writeCountersDataToNIV()` também grava o blob (a frequência dela não muda).
- **Não** incrementar `PVT_NVS_SCHEMA_VERSION`: a chave nova só é adicionada. Confirmar que não há outro motivo para mudar a versão.
- CountersData não entra em `povoto-settings.json`; nada a mudar no backup.

## 7. Logs
Acrescentar **no fim** das colunas (sem mudar a ordem das existentes):
- **Relief:** `HeadSpaceMeasured` (instantâneo do relief, NaN se inválido), `HeadSpaceEMA` (headspaceFiltered depois da atualização), `HeadSpaceDaily` (dailyValue, NaN se sem horas), `DailyHours`, `DailyState` (valid/hold/ema). `HeadSpaceVolume` continua sendo o valor aplicado. Adicionar os campos a `ReliefLogData` (datalog.h) e preencher em `processPressure(true)`.
- **Cold:** `HeadSpaceEMA`, `HeadSpaceDaily`, `DailyHours`, `DailyState`.
- **Dump:** incluir no `Serial.printf` existente e numa linha do log Relief ou Cold: P1, P2, `millis()` do início e do fim do dump e ΔH aplicado.
- Manter o log Recovery [DIAG] e o headspace sombra.

## 8. Critérios de aceite
- Compila sem warnings novos. `python tests/check_storage_schema.py` passa, ou falha só no problema já conhecido (ventResidual*, se ainda existir).
- Teste de mesa em Python replicando a lógica (script em `tests/`):
  - reliefs a cada 4 min durante 40 h com Vh = 30 + sin(2π·t/24 h) + ruído de 0,15 L: estado "ema" até ~18 h, depois "valid", com dailyValue em 30 ± 0,05 L a partir de 24 h;
  - rebase de −1 L em t = 30 h: dailyValue cai 1 L imediatamente;
  - 10 h sem reliefs: "hold" com o último valor;
  - salto de −3 h no relógio: sem exceção nem valor absurdo.
- Boot com média válida persistida: continua "valid" na mesma hora, sem voltar ao EMA.
- Preservar CRLF. Marcar o código novo com `[DAILY-HS]`.

## 9. Fora do escopo (backlog)
- Momento de leitura de P2 no dump: `endDumpTask()` usa `ControlData.pressure` no fim da tarefa (manual ou timeout). Se ela for lida logo após a purga, o gás ainda está frio (τ ≈ 30 s) e o ΔH sai superestimado; se for lida muito depois, a fermentação já recompôs parte da pressão e o ΔH sai subestimado. Corrigir depois (ler 2–3 min após o fim da purga, descontando a taxa medida antes, ou aplicar o expoente 1,29). Por isso o log da seção 7.
- Correção pela T ambiente, contador de ar e expoente variável: fora.
