# Volume do fermentador pelo tanque de expansão

Código: `src/ExpansionTank.cpp`; página Calibration, seção "Fermenter volume".
Procedimento: `docs/manual-calibracao-manutencao.md`, P4b. É a rotina lenta de
volume da época do TE com um tanque simples (sem pesagem), sem os tempos fixos
(3 min aberta, 4 min de espera) nem o ajuste de 35 ciclos.

## Sequência

- Só em OFF, sem task nem outra rotina, a partir de 0,3 bar. Único campo: volume do
  tanque de expansão V_E (conhecido), cujo valor inicial é o último usado (NVS
  `lastExpTankVol`). Sem campo de reliefs: o número vem da razão dos volumes,
  `f = V_F/(V_F + V_E)` por relief (V_F = FMT volume configurado), para descer da pressão de
  partida P a ~0,5 bar numa só execução: `n = ceil(ln(0,5/P) / ln f)`, de 1 a 12. O
  número planejado aparece na página (antes de iniciar) e no status.
- Tanque vazio e seco na porta A, sem válvula de retenção na saída de ventilação.
  Zero do sensor antes (P1 do manual). Pressurize a ~2,0 bar, espere 15 min e aperte Start.
- Para cada relief:
  1. **P1**: espera a pressão estável (a espera conta desde o fechamento da válvula do relief anterior, durante
     a ventilação do tanque) — inclinação dos últimos 2 min abaixo de
     **0,3 mbar/min** (era 0,1: abaixo da deriva do próprio sensor e do gás; a página
     mostra a inclinação atual e o limite), mínimo 2 min, máximo 15 min (ao bater o máximo, segue e
     marca "stable: no"). P1 = média dos últimos 10 s; T_F1 (Dallas do
     fermentador), T_E1 (ambiente: o tanque acompanha o ambiente), Patm1. Durante a espera registra, aos 60, 120,
     180 e 300 s depois do fechamento, a pressão (média de 10 s) e a temperatura do fermentador
     (colunas `P1_*s_bar` e `Tferm_P1_*s_C` do CSV): só registro, para avaliar a recuperação do P1; o cálculo
     continua usando o P1 do fim da espera.
  2. **Abre a válvula** (fermentador → tanque) e lê P2 num **tempo fixo de 8 min** (sem critério
     de inclinação). Depois do mínimo de pressão (10–27 mbar abaixo do valor final, a ~17–27 s) a pressão
     se recupera com constante de tempo de 65–87 s (ensaio de 23/10, sem vazamento; em 21/10 a perda de gás
     escondia a cauda); esperas mais longas só acumulam deriva (reliefs de 766 e 888 s em 21/10).
     **P2 é a assíntota** de uma exponencial `P(t) = P∞ − A·exp(−t/τ)` ajustada aos 6 checkpoints
     (30, 60, 120, 180, 300 e 480 s; mínimos quadrados para P∞ e A em cada τ de 20 a 300 s, e o melhor τ):
     aos 300 s ainda faltavam 0,5–1 mbar (28/10, τ de 55–85 s), por isso a leitura passou para 480 s, onde a cauda estimada fica abaixo de 0,1 mbar
     (0,2–0,9% de volume por mbar, pela amplificação de ~7,7×).
     Sem ajuste possível (checkpoint ausente, A ≤ 0 ou cauda acima de 3 mbar) usa a média dos últimos 10 s.
     O relief fica "stable: no" (fora da média) se a cauda estimada passar de 0,3 mbar, ou se a pressão
     tiver caído mais de 1 mbar entre 300 s e 480 s (sinal de perda). No ensaio de 20/10 a espera de ~2 min
     (limite de 0,3 mbar/min) subestimava o volume em 1,3–2%. P2, T_F2, T_E2, Patm2. Durante a
     espera registra, aos 30, 60, 120, 180, 300 e 480 s, a pressão (média de 10 s), a temperatura do
     fermentador e a ambiente (colunas `P2_*s_bar`, `Tferm_*s_C`, `Tenv_*s_C` do CSV). Registra também a menor pressão lida com a válvula aberta, o instante
     dela e o número de leituras (`Pmin_open_bar`, `t_Pmin_s`, `open_samples`), para avaliar se a
     liberação turbulenta cria uma zona de baixa pressão no sensor.
  3. **Fecha**: o tanque ventila pela porta R. Espera ~6 constantes de tempo do
     tanque pela válvula (curva atual, com ar), 3–15 min. O fermentador, fechado,
     continua a espera do P1 seguinte.
- Fim no número planejado (ou abaixo de 0,15 bar). O último relief termina assim que o P2 é lido: a válvula é
  desenergizada (o tanque ventila sozinho pela porta R) e a rotina é encerrada, sem esperar a ventilação. Mudança de modo, task ou
  pressão acima da máxima aborta. Dura cerca de 2–3 h.

## Cálculo

Balanço de mols completo (ar ideal): o que o fermentador perde, o tanque
ganha; o tanque começa ventilado, à Patm do início, na temperatura ambiente:

```
V_F = V_E · [(P2 + Patm2)/T_E2 − Patm1/T_E1] / [(P1 + Patm1)/T_F1 − (P2 + Patm2)/T_F2]
```

Com as temperaturas e a Patm constantes, é a fórmula do plano,
`V_E · (T_F/T_E) · P2/(P1 − P2)` (manométricas), mostrada ao lado para
comparação. Sem leitura ambiente válida, usa a do fermentador.

Erro por relief (±0,5 mbar), fermentador de 130 L: 2,5% com 2 L, 0,31% com
19 L, 0,16% com 50 L. Os níveis de pressão devem dar o mesmo volume dentro de
~0,3–0,5%.

### Média do ensaio: ponderada por (P1 − P2)²

O resultado é a média dos reliefs estáveis **ponderada por (P1 − P2)²** (manométricas), o mesmo critério para
qualquer ensaio, sem descartar nenhum relief à mão. O erro de um relief que vem do ruído da pressão vai com
1/(P1 − P2): a 1,8 bar a queda é de ~230 mbar e a 0,5 bar é de ~65 mbar, então um erro de 1 mbar vale 0,4% num
e 1,5% no outro. O peso é o inverso da variância desse erro, e o primeiro relief pesa cerca de 26% enquanto o último pesa
~2%. O desvio padrão também é ponderado (pesos de confiabilidade, `Σw(V − V̄)² / (Σw − Σw²/Σw)`). A página mostra a
média, o desvio, a faixa e o peso do maior relief; o CSV traz a coluna `weight` (parte de cada relief, 0 para os
instáveis). Nos três ensaios de 23/10 e 26/10 (referência pesada: 130,925 kg = 131,312 L) a média ponderada deu
131,28, 130,65 e 131,72 L, em média 131,21 L (−0,07%), com desvio de 0,54 L entre os ensaios; a média simples dava
130,98 L (−0,25%) e a média sem o último relief, 131,11 L.

## Saídas e gravação

- Reliefs que não estabilizaram (`settled1`/`settled2` falsos, "stable: no") ficam na tabela e
  no CSV, mas **fora da média e do desvio padrão**. A página mostra "n of m reliefs used",
  o FMT volume configurado ao lado do resultado, o volume e o peso por relief.
- O botão **Save as new FMT volume** só aparece com a rotina parada e **pelo menos 2 reliefs
  estáveis**; pede confirmação no navegador, grava o `FMTVolume` e um evento de calibração
  `fmt-volume-saved` (volume anterior e novo, desvio, reliefs usados, tanque). Antes, o
  resultado só vai para a RAM (até o próximo uso ou um reboot). Evento ao fim da rotina:
  `fermenter-volume`.
- CSV `/calibration/expansion.csv`: `PovotoCalibration-FermenterVolume-FMT<n>-yyyymmddhhmm.csv`
  (hora local de início; `nodate` sem NTP; prefixo `Incomplete-` se baixado durante a rotina).
  Com a rotina em andamento, o CSV é montado de uma cópia dos reliefs medidos tirada sob
  trava (a página roda na task web, a rotina no loop), sem escrita na flash. Um relief entra na
  tabela, no CSV, na média e no resultado assim que o P2 e o volume são calculados, sem esperar a
  ventilação do tanque; `vent_s` fica vazio até a ventilação terminar (e continua vazio no último relief e se o
  ensaio for abortado durante ela).

Modo debug: a equalização é simulada com o FMT volume (τ 3 s) e esperas
curtas (10–30 s); ao fim da espera máxima o relief é dado como estável (o critério
de 120 s não cabe nela), e a média deve devolver o FMT volume configurado.
