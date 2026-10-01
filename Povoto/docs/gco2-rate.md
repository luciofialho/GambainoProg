# Taxa de CO2 (gCO2/L/d)

**Definição: saldo de CO2 liberado pela cerveja** (produção menos absorção) por litro e por dia: a variação do CO2 fora da cerveja. Implementação em `src/PressureControl.cpp` (`recomputeBeerCO2EvolutionFromCurrentState()` e funções vizinhas).

## 1. Fase gasosa

A cada amostra:

```
gás = totalMolsEjected + headSpaceCO2Mols + expansionTankInventoryMoles()
```

| Parcela | Origem |
|---|---|
| `CountersData.totalMolsEjected` | CO2 ventilado pelo tanque de expansão (reliefs), com a correção de massa líquida |
| `headSpaceCO2Mols` | P·Vh/(R·T) − P_início·Vh/(R·T_início), com pressão manométrica e o headspace aplicado; mínimo 0 |
| `expansionTankInventoryMoles()` | CO2 que ainda está no tanque de expansão, no ciclo atual |

O CO2 dissolvido **não** entra na taxa: ele é modelado, não medido, e numa transição de pressão ou temperatura o modelo erra por várias vezes (docs/dissolved-co2.md, seção 2a). O SG continua usando o balanço completo, com o dissolvido (seção 6).

**Fora das transições** (pressão e temperatura estáveis), o saldo liberado é a produção: a cerveja está em regime (lote 160, 30/09: 8,5–9,1 g/L/d nos ciclos da manhã). **Numa transição** ele difere da produção em sentido conhecido: numa subida de pressão ou num resfriamento a cerveja absorve e o valor fica abaixo (piso); numa descida de pressão ou num aquecimento ela libera e o valor fica acima (teto). Os valores medidos variam suavemente: 5,2 → 2,7 g/L/d numa subida de 0,8 para 1,5 bar; −0,25 a +0,18 no cold crash do 159.

**Transição** (`co2RateInTransition()`, `updateCO2Transition()` a cada amostra): em Fermenting, sem NTP é sempre transição. Com setpoint de pressão 0, só a temperatura conta.
- **Início:** pressão ou temperatura fora de `STABLE`, ou estáveis há menos de **1 h**. Também na primeira amostra depois do primeiro boot desta versão (estado anterior desconhecido) e na volta do Conditioning. No início ficam gravados o equilíbrio de Henry (média de 30 min) e o último valor estável, com a hora.
- **Direção:** o Henry nos setpoints contra o de antes do início (zona morta de 1%). Acima: **absorvendo**. Abaixo: **liberando**. Se a direção mudar durante a mesma transição: **mista**.
- **Fim**, quando valem as três:
  1. temperatura `STABLE` há ≥ 1 h;
  2. com setpoint de pressão, pressão `STABLE` e o primeiro relief depois disso há ≥ 1 h. No lote 160, o `STABLE` veio às 17:45 e o primeiro relief em 1,9 bar só às 18:41;
  3. a tendência da última hora (inclinação por mínimos quadrados de 61 valores por minuto ÷ valor ajustado atual, piso de 0,5 g/L/d) dentro do limite da direção **por 60 min seguidos**: absorvendo, ≤ +5%/h; liberando, ≥ −10%/h; desconhecida ou mista, as duas. No 160 a tendência ficou abaixo de +5%/h por 32 min (20:59–21:31) e o saldo voltou a subir até 4,65 às 22:00. Por isso os 60 min seguidos.
- **Limite:** 8 h depois da última mudança (início ou `StableSince` mais recente), com as duas `STABLE`.
- **Persistência:** o estado fica no NVS (`co2TrStart`, `co2TrDir`, `co2TrHenry`, `co2TrRate`, `co2TrRateAt`, `co2TrRelief`). Os valores por minuto da tendência e a contagem dos 60 min ficam só na RAM: depois de um reboot, a transição dura pelo menos mais 1 h.

Durante a transição:
- regras "gCO2 < x": avaliam só quando a cerveja está **liberando**. O saldo é então um teto da produção, e "saldo < x" implica "produção < x". Absorvendo, desconhecida ou mista: não avaliam (docs/automatic-actions.md);
- Brewfather: sem o campo `bpm` (o saldo liberado desenharia uma falsa desaceleração);
- log Cold e `/getstatus`: `gCO2Source` = `transition`; o valor no log continua sendo o saldo medido;
- página inicial: mostra o **último valor estável**, com um ícone ⓘ cujo tooltip diz "Value is last stable reading at hh:mm. Preliminary unstable reading now is x.xx". Sem valor estável (boot, volta do Conditioning), mostra o saldo, e o tooltip diz "Transition: net CO2 release (production − absorption)".

Uma projeção do saldo foi testada no 160 e descartada. O ajuste exponencial (assíntota) deu 42–68 g/L/d nas primeiras 2 h; a reta (+1 h) só somava 0,5–1,5 com ruído.

## 2. Amostragem

- `processPressure()` roda a cada ~1 s (pelo `pressureControl()`) e ao fim de cada relief. Em cada chamada, uma amostra `{millis, total, pressão}` só entra se já tiverem passado **60 s** desde a anterior (`CO2_EVOLUTION_SAMPLE_MS`). Na prática, 1 amostra por minuto.
- Nenhuma amostra nos **2 primeiros minutos** depois do boot.
- Buffer circular de **71 amostras** (`CO2_EVOLUTION_HISTORY_SIZE`), cerca de 70 min. Cheio, a mais antiga é substituída.
- Cada amostra guarda a fase gasosa e um indicador de degrau externo. A mesma série dá a taxa exibida e a taxa que decide o estado do CO2 dissolvido (`GasCO2Rate`, vazia sem decisão: docs/dissolved-co2.md).

## 3. Cálculo da taxa

- Menos de 5 amostras: taxa = 0.
- Com n amostras, média de `k` amostras em cada ponta do buffer: `k = 1` se n < 9; senão `k = min(n/3, 10)`.
- Com as médias de CO2 total e de tempo (relativo à primeira amostra, o que tolera o rollover do `millis()`) no início e no fim:

```
taxa [g/L/d] = ΔCO2 [mol] × 44,01 [g/mol] × 86 400 000 [ms/d] / (volume de cerveja [L] × Δt [ms])
```

- A taxa é **com sinal** (`beerCO2EvolutionGramsPerLiterPerDay`). A exibição corta valores negativos em 0.
- Volume de cerveja inválido, CO2 total não finito ou Δt ≤ 0: taxa = 0.
- Mudança do headspace aplicado (média de 24 h, dump) muda `headSpaceCO2Mols` e aparece na taxa como um degrau pequeno (≈ P·ΔVh/RT; 1 L a 0,8 bar ≈ 0,03 mol).

Janela em função do tempo desde o boot:

| Amostras | Tempo após o boot | Janela |
|---|---|---|
| 5 | ~6–7 min | ~4 min, 1 amostra em cada ponta |
| 9 | ~10–11 min | média de 3 em cada ponta |
| 15 | ~17 min | ~14 min, média de 5 em cada ponta |
| 71 | ~72 min | ~70 min, média de 10 em cada ponta |

## 4. Depois de um reboot

**Buffers restaurados** (`saveCO2Buffers()` / `restoreCO2Buffers()` em `PressureControl.cpp`): o buffer do gCO2 (71 amostras da fase gasosa) e as 30 amostras de Henry do dissolvido (docs/dissolved-co2.md) são gravados na partição de dados `persist` (`povotoDataFS()`; em placas não migradas, no LittleFS único) (`/co2buffers.bin`, ~1,6 KB, gravação atômica por arquivo temporário) a cada 10 min e no início de um OTA, com a hora NTP e a idade de cada amostra. No boot a amostragem espera o NTP (até 5 min) e o arquivo é restaurado se:
- tiver no máximo **10 min** (`CO2_BUFFER_MAX_GAP_S`): OTA (~1 min), quedas rápidas de energia e travamentos ficam bem abaixo; um intervalo maior é uma queda de energia sem controle de temperatura e pressão, e a janela recomeça;
- for do mesmo lote, com o contador de reliefs não menor que o gravado (contadores não zerados), e o modo for Fermenting (gravado e atual).

**Costura:** a série do gás restaurada é deslocada para continuar, no ritmo das suas últimas 10 amostras, até a fase gasosa atual. O CO2 ejetado perdido no reboot (não gravado nos contadores, até ~0,26 mol) não entra na taxa. No bench, um boot de 60 s com 0,26 mol perdidos deixa a tela em 8,59–8,60 g/L/d (produção 8,6). As amostras de Henry também são restauradas, então o dissolvido continua igual depois do boot.

**Espaço:** a partição `persist` não recebe imagem de filesystem, então um upload dos arquivos web não apaga o arquivo e não é preciso reservar espaço em `data/` (docs/graph-history.md). Um arquivo com magic 0 (zerado) é rejeitado como `zeroed file`.

O arquivo é apagado quando o balanço é reiniciado de propósito: contadores editados, lote novo, volta do Conditioning. O `/getstatus` mostra `CO2 buffers at boot: restored (gap N s, 71+211 samples)` ou o motivo de não ter restaurado.

Sem arquivo válido, a janela recomeça: a taxa calculada fica em 0 por ~7 min e ruidosa até a janela crescer, e vale o valor retido abaixo.

**Valor retido:** a cada amostra com janela madura (≥ 15 amostras, `CO2_EVOLUTION_MATURE_SAMPLES`) e NTP válido, a taxa e o horário NTP vão para `CountersData.co2RateHeld` / `co2RateHeldAt` (chaves `co2RateHeld` / `co2RateAt` em `pvt_counters`). Eles são gravados junto com os outros contadores (`writeCountersDataToNIV()`: a cada 5 reliefs, a cada 15 min de relé e no início de um OTA), sem gravações extras.

Depois do boot, o valor retido é **informado** no lugar do calculado enquanto:
1. a janela nova tiver menos de 15 amostras (~17 min); e
2. o valor retido tiver no máximo **2 h** (`CO2_RATE_HELD_MAX_AGE_S`), medidas pelo NTP.

Sem NTP válido não dá para saber a idade, e o valor retido não é usado. Um lote novo apaga o valor retido.

**Por que a costura:** os contadores de CO2 (`totalMolsEjected`, `CO2InSolution`) só são gravados periodicamente. Num reboot inesperado perde-se o CO2 ejetado desde a última gravação (até ~4 reliefs, ~0,26 mol), da ordem da produção de uma janela inteira. Um buffer restaurado sem ajuste misturaria amostras de antes (total antigo) e de depois (total sem esse CO2) e daria uma taxa subestimada por até 70 min, justamente a direção que dispara "gCO2 < x". A costura elimina esse degrau.

## 5. Quem usa o quê

| Uso | Valor |
|---|---|
| Página inicial, `/getstatus`, Brewfather (`bpm`, só se > 0) | informado (retido ou calculado), cortado em 0 — `getBeerCO2EvolutionGramsPerLiterPerDay()` |
| Colunas `gCO2/L/d` dos logs Cold e Relief | informado, com sinal — `getReportedCO2EvolutionGramsPerLiterPerDay()`; origem na coluna Cold `gCO2Source` (`calculated` / `held`) |
| Gatilho "gCO2/L/d < x" das ações automáticas | **só o calculado**, > 0, com janela ≥ 30 min e fora de transição (`getRuleCO2EvolutionGramsPerLiterPerDay()`), abaixo de x por 20 min seguidos — o valor retido nunca dispara regra (docs/automatic-actions.md) |

O `/getstatus` mostra a origem e o número de amostras: `gCO2/L/d: 3.12 (held; 8 samples)`.

## 6. Dívida do CO2 produzido

`CO2MolsProducedPerLiter` (que dá o SG) só cresce. Uma queda do CO2 total vira dívida (`CountersData.co2CorrectionDebt`, mol), paga pelas subidas seguintes antes de elas contarem como produção. A dívida é gravada com os contadores (chave `co2Debt`) e sobrevive a um reboot; antes ela ficava só na RAM, e um reboot deixava o SG permanentemente baixo depois de uma queda do total. Ela é zerada só quando o integral é definido explicitamente: lote novo ou edição do campo "CO2 Mols Produced Per Liter" na página Counters. Salvar a página Counters sem mudar um campo não mexe em nada: um campo só conta como editado se o valor enviado diferir do mostrado em pelo menos meio dígito da última casa.
