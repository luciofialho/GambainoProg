# Volume do fermentador pelo barril

Código: `src/KegVolume.cpp`; página Calibration, seção "Keg volume".
Procedimento: `docs/manual-calibracao-manutencao.md`, P4b. É a rotina lenta de
volume da época do TE com o barril no lugar do tanque, sem os tempos fixos
(3 min aberta, 4 min de espera) nem o ajuste de 35 ciclos.

## Sequência

- Só em OFF, sem task nem outra rotina, a partir de 0,3 bar. Campos: volume do
  barril V_E (pesado com água) e número de reliefs (1–8, padrão 4).
- Barril vazio e seco na porta A, sem válvula de retenção na saída de
  ventilação. Zero do sensor antes (P1 do manual).
- Para cada relief:
  1. **P1**: espera a pressão estável — inclinação dos últimos 2 min abaixo de
     **0,1 mbar/min**, mínimo 2 min, máximo 15 min (ao bater o máximo, segue e
     marca "stable: no"). P1 = média dos últimos 10 s; T_F1 (Dallas do
     fermentador), T_E1 (ambiente: o barril acompanha o ambiente), Patm1.
  2. **Abre a válvula** (fermentador → barril) e espera a pressão estável de
     novo (mesmo critério; um barril de 50 L tem τ ≈ 60 s): P2, T_F2, T_E2,
     Patm2.
  3. **Fecha**: o barril ventila pela porta R. Espera ~6 constantes de tempo do
     barril pela válvula (curva atual, com ar), 3–15 min. O fermentador, fechado,
     continua a espera do P1 seguinte.
- Fim no número de reliefs (ou abaixo de 0,15 bar). Mudança de modo, task ou
  pressão acima da máxima aborta.

## Cálculo

Balanço de mols completo (ar ideal): o que o fermentador perde, o barril
ganha; o barril começa ventilado, à Patm do início, na temperatura ambiente:

```
V_F = V_E · [(P2 + Patm2)/T_E2 − Patm1/T_E1] / [(P1 + Patm1)/T_F1 − (P2 + Patm2)/T_F2]
```

Com as temperaturas e a Patm constantes, é a fórmula do plano,
`V_E · (T_F/T_E) · P2/(P1 − P2)` (manométricas), mostrada ao lado para
comparação. Sem leitura ambiente válida, usa a do fermentador.

Erro por relief (±0,5 mbar), fermentador de 130 L: 2,5% com 2 L, 0,31% com
19 L, 0,16% com 50 L. Os três níveis de pressão (1,9 / 1,2 / 0,6 bar) devem dar
o mesmo volume dentro de ~0,3–0,5%.

## Saídas

Volume por relief, média e desvio padrão na página; CSV `/calibration/keg.csv`
(pressões, temperaturas, Patm, esperas, estabilidade, volumes). Evento de
calibração `keg-volume`. **Nada é gravado**: aceito o resultado, digite-o como
FMT volume na página Settings. Os resultados ficam na RAM até o próximo uso
ou um reboot.

Modo debug: a equalização é simulada com o FMT volume (τ 30 s) e esperas
curtas.
