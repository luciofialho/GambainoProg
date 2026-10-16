# Volume do fermentador pelo tanque de expansão

Código: `src/KegVolume.cpp`; página Calibration, seção "Fermenter volume" (antiga "Keg volume").
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
  1. **P1**: espera a pressão estável — inclinação dos últimos 2 min abaixo de
     **0,1 mbar/min**, mínimo 2 min, máximo 15 min (ao bater o máximo, segue e
     marca "stable: no"). P1 = média dos últimos 10 s; T_F1 (Dallas do
     fermentador), T_E1 (ambiente: o tanque acompanha o ambiente), Patm1.
  2. **Abre a válvula** (fermentador → tanque) e espera a pressão estável de
     novo (mesmo critério): P2, T_F2, T_E2, Patm2.
  3. **Fecha**: o tanque ventila pela porta R. Espera ~6 constantes de tempo do
     tanque pela válvula (curva atual, com ar), 3–15 min. O fermentador, fechado,
     continua a espera do P1 seguinte.
- Fim no número planejado (ou abaixo de 0,15 bar). Mudança de modo, task ou
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

## Saídas e gravação

- Reliefs que não estabilizaram (`settled1`/`settled2` falsos, "stable: no") ficam na tabela e
  no CSV, mas **fora da média e do desvio padrão**. A página mostra "n of m reliefs used",
  o FMT volume configurado ao lado do resultado e o volume por relief.
- O botão **Save as new FMT volume** só aparece com a rotina parada e **pelo menos 2 reliefs
  estáveis**; pede confirmação no navegador, grava o `FMTVolume` e um evento de calibração
  `fmt-volume-saved` (volume anterior e novo, desvio, reliefs usados, tanque). Antes, o
  resultado só vai para a RAM (até o próximo uso ou um reboot). Evento ao fim da rotina:
  `fermenter-volume`.
- CSV `/calibration/keg.csv`: `PovotoCalibration-FermenterVolume-FMT<n>-yyyymmddhhmm.csv`
  (hora local de início; `nodate` sem NTP; prefixo `Incomplete-` se baixado durante a rotina).
  Com a rotina em andamento, o CSV é montado de uma cópia dos reliefs concluídos tirada sob
  trava (a página roda na task web, a rotina no loop), sem escrita na flash.

Modo debug: a equalização é simulada com o FMT volume (τ 30 s) e esperas
curtas.
