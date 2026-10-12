# Ensaio de ventilação

Código: `src/VentingTest.cpp`; ajuste em `include/VentModel.h`
(`fitVentingTest`); página Calibration, seção "Venting test". Procedimento:
`docs/manual-calibracao-manutencao.md`, P5. Modelo: `docs/vent.md`.

## 1. Sequência

- Só em OFF, sem task nem outra rotina, a partir de ≥ 0,8 bar (o normal é
  ~1,9 bar, depois de ~10 min de espera). Escolhas: **etiqueta** (`as-is`,
  `muffler-cleaned`, `valve-cleaned`, `new-valve`, `after-cleaning-cycle`; depois
  de um ciclo de limpeza ela já vem em `after-cleaning-cycle`), gás (ar; CO2
  opcional) e volume do ensaio (padrão: o FMT volume; outro valor com água ou
  garrafas dentro do fermentador).
- **Patamares**: o primeiro é a pressão de partida; os seguintes, 1,9 / 1,5 /
  1,1 / 0,8 / 0,5 bar, os que estiverem pelo menos 0,15 bar abaixo da partida.
  Com o fermentador vazio de 130 L um ciclo de 0,5–8 s derruba só ~6% (τ ≈
  210–300 s): sem as descidas, a curva cobriria só 1,55–1,9 bar (decisão de
  11/10/2026).
- Em cada patamar: aberturas de **2, 0,5, 8, 1, 4 e 2 s** (alternadas, para a
  duração não andar junto com a pressão; o 2 s nas duas pontas dá a deriva).
- Entre patamares: uma abertura longa (3–90 s), calculada pela curva atual
  para chegar ao patamar seguinte; ela também entra no ajuste.
- Cada espera termina com a pressão estável (variação < 0,3 mbar em 30 s;
  mínimo 45 s, máximo 180 s). P1 e P2 = média dos últimos 10 s; P2 de uma
  liberação é o P1 da seguinte. `P_close` = a primeira leitura ≥ 0,5 s depois de
  fechar.
- **Tempo real** de cada abertura: `esp_timer_get_time()` na linha do
  `digitalWrite` de abrir e de fechar (o fechamento é feito no loop).
- Fim: depois do último patamar, abaixo de 0,3 bar ou com 40 liberações;
  ~34 liberações, ~40–50 min. Mudança de modo, task ou pressão acima da
  máxima aborta (o que já foi medido é ajustado e guardado).

## 2. Ajuste

Cada liberação é integrada (não a taxa no ponto médio): o gás assentado
passa de P1 a P2; durante a abertura, a pressão real segue o politrópico com
o expoente da própria liberação, `ln(P_close/P1)/ln(P2/P1)` (fora de
1,0–1,67, a mediana das outras):

```
t_real − t0 = Σ a_k·J_k,   J_k = (V/100)·√(293,15/T0) ∫ √(T/T0)·P^k / P_abs dP_iso   (Simpson, 16 intervalos)
```

Mínimos quadrados lineares em (t0, a0..a3) com **pesos P_abs²**: um erro de
leitura da pressão vira um erro de tempo quase constante (~τ·σP/P_abs), não
proporcional à duração. Num ensaio sintético com 0,15 mbar de ruído
(`python tests/venting_fit.py --selftest`), a curva sai dentro de 0,1–0,2% e o
t0 dentro de ~4–5 ms; com pesos 1/t² (resíduo relativo) saíam 0,7–0,9% e 11 ms.

Saídas (página, CSV e eventos de calibração):

| Saída | Significado |
|---|---|
| a0..a3, faixa | a curva τ(P) de 100 L de ar a 20 °C, usada só na faixa do ensaio |
| t0 ± erro padrão | atraso mecânico da válvula (intercepto); o erro padrão sai da covariância do ajuste |
| resíduo de forma | rms dos resíduos relativos das aberturas ≥ 4 s |
| deriva ± erro padrão | média, por patamar, da diferença de vazão entre o último e o primeiro 2 s (~8 min de distância). Com o ruído de leitura, o erro padrão é ~0,7%: deriva < ~1,5% num ensaio não é separável do ruído; a deriva entre lotes vem do histórico |
| contra a referência / a curva gravada | vazão do ensaio ÷ vazão da outra curva − 1 a 1,8 / 1,2 / 0,8 bar |

O desvio padrão dos resíduos das aberturas curtas (~15–45 ms com 0,15 mbar)
mede o ruído da leitura, não a repetibilidade do t0: ele aparece só no script.

## 3. Histórico e gravação

- `/venttests.bin` na partição `persist`: os últimos **50 ensaios**, cada um com
  os dados brutos de cada liberação (tempo pedido e real, P1, P_close, P2, Tferm,
  espera, patamar, tipo), data/hora NTP, lote, Patm e fonte, Tamb, Tferm, gás,
  volume, versão do firmware (data de compilação), etiqueta e o ajuste. ~1,3 KB
  por ensaio, ~66 KB no total (o plano estimava 8 KB com menos liberações).
  Arquivo novo: a tag `te-final` não o lê.
- CSV: `/calibration/venting.csv?test=0` (o último), `?test=N` ou `?test=all`
  (todos, do mais antigo ao mais novo); `;` e vírgula decimal.
- `python tests/venting_fit.py arquivo.csv` refaz o ajuste fora da placa
  (mesmas equações, `tests/vent_model.py`) e compara com o da placa.
- **Nada é gravado sem botão**: "Save curve and t0" (a curva de vazão usada
  pelo vent) e "Save as reference curve" (a curva fixa das comparações). Só
  fora de Fermenting. Cada gravação vira um evento de calibração.

## 4. Modo debug

Sem válvula: a pressão é simulada (exponencial com a curva atual e o volume
do ensaio), com as mesmas esperas (5–10 s).
