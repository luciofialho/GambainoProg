# Teste de perda de gás (leak hold)

Código: `src/LeakHold.cpp`; página Calibration, seção "Leak test".

## Por que existe

No ensaio de volume do fermentador (`docs/expansion-tank-volume.md`), V ∝ α/(1−α) com
α = P2/P1: um erro relativo de 0,3% em P2 vira ~2,3% no volume (amplificação ~7,7×). Em 21/10 os
reliefs de 1 e 5 esperaram 888 e 766 s e o P2 caiu 5,4 e 3,3 mbar depois dos 300 s (0,4–0,55 mbar/min),
sem queda equivalente no fermentador fechado (P1 do relief seguinte a ≤1,1 mbar do P2 anterior).
Uma perda assim, escondida pela recuperação térmica nos outros reliefs, explicaria uma parte do
desvio de ~2% do volume. O teste mede a perda de forma direta.

## Sequência

- Só em OFF, sem task nem outra rotina, a partir de 0,5 bar. Campo: volume do tanque de expansão
  (o mesmo `lastExpTankVolume` da rotina de volume; só entra na conversão para mL/min).
  Pressurize a ~1,5–2 bar, espere 15 min e aperte Start.
- **Fase 1, fermentador fechado (10 min):** a válvula fica desenergizada (o tanque ventila pela
  porta R), o fermentador isolado. A perda daqui é do fermentador.
- **Fase 2, válvula aberta (15 min):** a válvula é energizada e o fermentador se equaliza com o
  tanque (a recuperação térmica tem constante de tempo de ~45 s). A perda daqui é do conjunto
  fermentador + tanque + válvula + conexões.
- A cada 10 s guarda a média da pressão, a temperatura do fermentador (Dallas), a ambiente e a
  Patm. Mudança de modo, task ou pressão acima da máxima aborta (a válvula volta desenergizada).

## Cálculo

Em cada fase, depois do tempo de assentamento (120 s na fase 1, 300 s na fase 2), ajusta uma reta a
`y = (P + Patm) / T_ferm` contra o tempo. A perda relativa é `−inclinação / média(y)`, por minuto;
isso desconta a deriva de temperatura (que também sai em K/min). O fluxo em mL/min de gás à pressão
atmosférica é `perda × V_gás × P_abs / Patm`, com V_gás = FMT volume (fase 1) ou FMT volume + tanque
(fase 2).

Perda do lado do tanque = fluxo da fase 2 − fluxo da fase 1 × (P_manométrica fase 2 / fase 1): o
fluxo de um vazamento cresce com a pressão, então o fermentador é escalado pelas pressões.

Referência (ensaio de 21/10, reliefs 1 e 5): 0,4–0,55 mbar/min em ~150 L, ou cerca de 75 mL/min de
gás. Um vazamento disso aparece como bolhas contínuas com água e sabão.

## Interpretação

| Fase 1 (fermentador) | Fase 2 (conjunto) | Conclusão |
|---|---|---|
| ~0 | ~0 | não há perda mensurável; o desvio do volume tem outra causa |
| ~0 | perda | vazamento no tanque, na válvula (assento da porta R) ou nas conexões |
| perda | perda proporcional | vazamento no fermentador |

A resolução é de ~0,02 mbar/min (10 min × ruído de 0,3 mbar na média de 10 s). A recuperação
térmica da fase 2 está fora do ajuste; se a temperatura ainda mexer (K/min na tabela), o resultado
perde precisão.

## Saídas

- Tabela na página: pressão média, mbar/min, K/min, %/min e mL/min de cada fase, e a perda do lado
  do tanque.
- CSV `/calibration/leak.csv`: `PovotoCalibration-LeakHold-FMT<n>-yyyymmddhhmm.csv`, colunas
  `phase;t_s;P_bar;T_ferm_C;T_env_C;patm_bar` (uma linha a cada 10 s).
- Evento de calibração `leak-hold` ao fim (valores das duas fases e do lado do tanque).

Em placa de bancada (modo debug) a rotina simula a pressão, com um vazamento de 0,03%/min, e
encurta as fases (40 s e 60 s).
