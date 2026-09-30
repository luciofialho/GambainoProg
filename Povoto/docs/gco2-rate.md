# Taxa de CO2 (gCO2/L/d)

Taxa líquida de produção de CO2 por litro de cerveja por dia, calculada a partir do balanço de CO2 do fermentador. Implementação em `src/PressureControl.cpp` (`recomputeBeerCO2EvolutionFromCurrentState()` e funções vizinhas).

## 1. CO2 total

A cada amostra, o CO2 total da fermentação (mol) é:

```
total = totalMolsEjected + CO2InSolution + headSpaceCO2Mols + expansionTankInventoryMoles()
```

| Parcela | Origem |
|---|---|
| `CountersData.totalMolsEjected` | CO2 ventilado pelo tanque de expansão (reliefs), com a correção de massa líquida |
| `CountersData.CO2InSolution` | CO2 dissolvido na cerveja (estados em docs/dissolved-co2.md) |
| `headSpaceCO2Mols` | P·Vh/(R·T) − P_início·Vh/(R·T_início), com pressão manométrica e o headspace aplicado; mínimo 0 |
| `expansionTankInventoryMoles()` | CO2 que ainda está no tanque de expansão, no ciclo atual |

## 2. Amostragem

- `processPressure()` roda a cada ~1 s (pelo `pressureControl()`) e ao fim de cada relief. Em cada chamada, uma amostra `{millis, total, pressão}` só entra se já tiverem passado **60 s** desde a anterior (`CO2_EVOLUTION_SAMPLE_MS`). Na prática, 1 amostra por minuto.
- Nenhuma amostra nos **2 primeiros minutos** depois do boot.
- Buffer circular de **71 amostras** (`CO2_EVOLUTION_HISTORY_SIZE`), cerca de 70 min. Cheio, a mais antiga é substituída.
- Cada amostra também guarda a fase gasosa (ejetado + headspace + tanque de expansão) e um indicador de degrau externo, usados pela taxa da fase gasosa que decide o estado do CO2 dissolvido (docs/dissolved-co2.md; coluna Cold `GasCO2Rate`).

## 3. Cálculo da taxa

- Menos de 5 amostras: taxa = 0.
- Com n amostras, média de `k` amostras em cada ponta do buffer: `k = 1` se n < 9; senão `k = min(n/3, 10)`.
- Com as médias de CO2 total e de tempo (relativo à primeira amostra, o que tolera o rollover do `millis()`) no início e no fim:

```
taxa [g/L/d] = ΔCO2 [mol] × 44,01 [g/mol] × 86 400 000 [ms/d] / (volume de cerveja [L] × Δt [ms])
```

- A taxa é **com sinal** (`beerCO2EvolutionGramsPerLiterPerDay`). A exibição corta valores negativos em 0.
- Volume de cerveja inválido, CO2 total não finito ou Δt ≤ 0: taxa = 0.
- **Rebase:** quando o CO2 dissolvido entra no equilíbrio (vindo do half-life ou do armado), o salto do modelo é somado a todas as amostras do buffer (`rebaseCO2Evolution()`), para não aparecer como produção. O mesmo vale para o CO2 que sai com a cerveja num dump durante o half-life.
- Mudança do headspace aplicado (média de 24 h, dump) muda `headSpaceCO2Mols` e aparece na taxa como um degrau pequeno (≈ P·ΔVh/RT; 1 L a 0,8 bar ≈ 0,03 mol).

Janela em função do tempo desde o boot:

| Amostras | Tempo após o boot | Janela |
|---|---|---|
| 5 | ~6–7 min | ~4 min, 1 amostra em cada ponta |
| 9 | ~10–11 min | média de 3 em cada ponta |
| 15 | ~17 min | ~14 min, média de 5 em cada ponta |
| 71 | ~72 min | ~70 min, média de 10 em cada ponta |

## 4. Depois de um reboot

O buffer fica só na RAM. Depois de um reboot, a taxa calculada fica em 0 por ~7 min e ruidosa até a janela crescer.

**Valor retido:** a cada amostra com janela madura (≥ 15 amostras, `CO2_EVOLUTION_MATURE_SAMPLES`) e NTP válido, a taxa e o horário NTP vão para `CountersData.co2RateHeld` / `co2RateHeldAt` (chaves `co2RateHeld` / `co2RateAt` em `pvt_counters`). Eles são gravados junto com os outros contadores (`writeCountersDataToNIV()`: a cada 5 reliefs, a cada 15 min de relé e no início de um OTA), sem gravações extras.

Depois do boot, o valor retido é **informado** no lugar do calculado enquanto:
1. a janela nova tiver menos de 15 amostras (~17 min); e
2. o valor retido tiver no máximo **2 h** (`CO2_RATE_HELD_MAX_AGE_S`), medidas pelo NTP.

Sem NTP válido não dá para saber a idade, e o valor retido não é usado. Um lote novo apaga o valor retido.

**Por que não persistir o buffer:** os contadores de CO2 (`totalMolsEjected`, `CO2InSolution`) só são gravados periodicamente. Num reboot inesperado perde-se o CO2 ejetado desde a última gravação (até ~4 reliefs, ~0,26 mol com tanque de 2 L a ~0,8 bar), que é da ordem da produção de uma janela inteira. Um buffer restaurado misturaria amostras de antes (total antigo) e de depois (total restaurado, sem esse CO2) e daria uma taxa subestimada, perto de zero, por até 70 min, justamente a direção que dispara "gCO2 < x". Com amostras só de depois do boot, uma defasagem constante no total não afeta a inclinação.

## 5. Quem usa o quê

| Uso | Valor |
|---|---|
| Página inicial, `/getstatus`, Brewfather (`bpm`, só se > 0) | informado (retido ou calculado), cortado em 0 — `getBeerCO2EvolutionGramsPerLiterPerDay()` |
| Colunas `gCO2/L/d` dos logs Cold e Relief | informado, com sinal — `getReportedCO2EvolutionGramsPerLiterPerDay()`; origem na coluna Cold `gCO2Source` (`calculated` / `held`) |
| Gatilho "gCO2/L/d < x" das ações automáticas | **só o calculado**, e só se > 0 (`beerCO2EvolutionGramsPerLiterPerDay`) — o valor retido nunca dispara regra |

O `/getstatus` mostra a origem e o número de amostras: `gCO2/L/d: 3.12 (held; 8 samples)`.
