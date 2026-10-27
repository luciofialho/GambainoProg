# Ensaio de ventilação

Código: `src/VentingTest.cpp`; ajuste em `include/VentModel.h`
(`fitVentingTest`); página Calibration, seção "Venting test". Procedimento:
`docs/manual-calibracao-manutencao.md`, P5. Modelo: `docs/vent.md`.

## 1. Sequência

- Só em OFF, sem task nem outra rotina, a partir de ≥ 0,8 bar. Para calibrar,
  comece **acima da maior pressão de fermentação + ~3%** (dente de serra):
  ~2,05–2,10 bar para setpoint de 1,9, depois de ~15 min de espera (o sensor é
  certificado até 2 bar, mas mede razoavelmente um pouco acima). Escolhas: **etiqueta** (`as-is`,
  `muffler-cleaned`, `valve-cleaned`, `new-valve`, `after-cleaning-cycle`; depois
  de um ciclo de limpeza ela já vem em `after-cleaning-cycle`), gás (ar; CO2
  opcional) e volume do ensaio (padrão: o FMT volume; outro valor com água ou
  garrafas dentro do fermentador).
- **Patamares**: o primeiro é a pressão de partida; os seguintes, 1,9 / 1,5 /
  1,1 / 0,8 / 0,5 / **0,25** bar, os que estiverem pelo menos 0,15 bar abaixo
  da partida. O de 0,25 bar cobre as fermentações a 0,3 bar: abaixo de ~0,4 bar
  (subsônico) o τ cresce rápido e o valor da ponta superestimaria a vazão.
  Com o fermentador vazio de 130 L um ciclo de 0,5–8 s derruba só ~6% (τ ≈
  210–300 s): sem as descidas, a curva cobriria só 1,55–1,9 bar (decisão de
  11/10/2026).
- Em cada patamar: aberturas de **2, 0,5, 8, 1, 4 e 2 s** (alternadas, para a
  duração não andar junto com a pressão; o 2 s nas duas pontas dá a deriva).
- Entre patamares: uma abertura longa (3–240 s) que leva o P **assentado** ao patamar seguinte, calculada
  com o **próprio modelo do ajuste**: t = k·S(β; P1, P_alvo, e), a mesma integral da seção 2, com a curva em
  uso e o expoente politrópico e do passo anterior (na falta dele, a mediana das liberações de 4 s ou mais do
  patamar; na falta delas, 1,22). Ela também entra no ajuste. O modelo prevê o tempo de abertura dos passos
  dos ensaios de 20–21/10/2026 com erro de −1,5% a +2,5% (16 passos, dado o P1 e o P2 assentados de cada
  um), então espera-se pousar o P assentado a ±1–2% da queda do passo (3–6 mbar em passos de 100–300 mbar),
  mais o erro do nível da curva em uso (±1,5%) e o de P2 ainda não assentado (poucos mbar). A fórmula anterior
  (rebote constante r medido na liberação de 8 s e sem o resfriamento) pousava 18–47 mbar abaixo do alvo,
  porque o rebote de um passo de 10–30 s (14–18%) é menor que o da liberação de 8 s (19–27%).
- Cada espera termina quando a **inclinação** da pressão (reta de mínimos quadrados dos últimos 40 s) fica
  abaixo de **0,6 mbar/min**, ou de **0,3 mbar/min ao redor das liberações de 1 s ou menos** (a que acabou de
  terminar ou a seguinte), porque um erro de ~1 mbar no P assentado é 10% da queda de uma abertura de 0,5 s;
  mínimo 45 s, máximo 300 s. (Antes era a diferença de duas leituras únicas < 0,3 mbar em 30 s: com 0,2 mbar de
  ruído ela parava as esperas com a pressão ainda subindo 1–2 mbar/min, como mostrou a coluna `end_slope_mbar_min`
  dos ensaios de 22/10.) P1 e P2 = média dos últimos 10 s; P2 de uma
  liberação é o P1 da seguinte. `P_close` = a primeira leitura ≥ 0,5 s depois de
  fechar. A coluna `settled` do CSV diz `no` quando a espera terminou no máximo,
  sem a pressão estável (num ensaio com teto de 180 s, 8 de 21 esperas — as de 8 s,
  4 s e os passos — terminaram assim; por isso o teto subiu para 300 s).
- **Tempo real** de cada abertura: `esp_timer_get_time()` na linha do
  `digitalWrite` de abrir e de fechar (o fechamento é feito no loop).
- Fim: depois do último patamar, abaixo de 0,1 bar ou com 48 liberações. Com
  partida em ~2,0 bar: 6 patamares, 41 liberações, ~2 h 15 min (135–160 min; o ensaio com teto de 180 s levou
  ~2,7 min por liberação; o critério estrito soma ~20–25 min; no pior caso, todas as esperas no máximo, ~3,5 h). A 0,25 bar
  (τ ≈ 330 s com 130 L) a abertura de 0,5 s derruba só ~1,8 mbar: o ruído pesa
  mais nas aberturas curtas desse patamar, e os pesos P_abs² já as tratam
  com menos peso. A espera é a mesma (estável em 0,3 mbar/30 s, 45–300 s);
  a descida de 0,5 para 0,25 bar é uma abertura de ~55 s. Mudança de modo, task ou pressão acima da
  máxima aborta (o que já foi medido é ajustado e guardado).

## 2. Ajuste

Cada liberação é integrada (não a taxa no ponto médio): o gás assentado
passa de P1 a P2; durante a abertura, a pressão real segue o politrópico com
o expoente da própria liberação, `ln(P_close/P1)/ln(P2/P1)` (fora de
1,0–1,67, a mediana das outras):

```
t_real = k·S(β),   S = (V/100)·√(293,15/T0) ∫ √(T/T0) / (P_abs·ψ_ef(P)) dP_iso   (Simpson, 8 intervalos)
```

A **primeira liberação fica fora do ajuste** (continua no CSV): o P1 dela é a pressão do início, não o P2 assentado de uma liberação, e foi o ponto fora da curva em todos os ensaios (resíduo de −130 a −150 ms); `venting_fit.py` faz o mesmo (`--keep-first` a mantém).

Para cada β, os mínimos quadrados em k são fechados, com **pesos P_abs²**: um erro de
leitura da pressão vira um erro de tempo quase constante (~τ·σP/P_abs), não
proporcional à duração. O β sai de uma busca por seção áurea em ln β (de 0,01 a 30,
20 passos; ~22 avaliações do ajuste linear). Na placa o ajuste leva alguns segundos (o tempo
vai no evento de calibração, `fit ... ms`). **Não há t0** no modelo: o atraso de abertura é tomado
igual ao de fechamento (docs/vent.md, seção 3). Num ensaio sintético com 0,15 mbar de ruído
(`python tests/venting_fit.py --selftest`, até 0,17 bar), a curva sai dentro de
0,15% e o qui-quadrado reduzido em ~0,7.

Saídas (página, CSV e eventos de calibração):

| Saída | Significado |
|---|---|
| k, β, faixa | a curva τ(P) = k/ψ_ef(P) de 100 L de ar a 20 °C (k: válvula; β: saída/silenciador); a faixa só diz onde o ensaio mediu |
| qui-quadrado reduzido | Σ resíduo² / (ruído esperado)² / (n − 2): o resíduo de cada liberação convertido para pressão (pela sensibilidade do tempo a P1 e P2) contra o ruído de cada leitura (erro padrão da média de 10 s, medido na própria espera; mínimo 0,05 mbar) e 2 ms de tempo. ~1: o modelo acompanha os dados dentro do ruído; **> 3: a forma não acompanha** (ou as leituras não estabilizaram) |
| `Pinf_bar`, `Pinf_tau_s`, `Pinf_amp_mbar` (por liberação, no CSV) | pressão assentada **extrapolada** pela recuperação da espera: P(t) = P∞ − A·e^(−t/τ) ajustada a todas as leituras da espera (`PressureWait::fitRecovery`, espelhada em `vent_model.fit_recovery`; vazia se a espera foi curta ou o τ não se determina). **Só é gravada: o ajuste continua usando o P2 lido**, para comparar a repetibilidade dos dois antes de trocar. O ensaio de validação de espera (seção 5) deu τ de 34 a 66 s e A de 18–25% da queda até o fechamento nas liberações de 2–8 s (8–10% nos passos); com um vazamento de ~1 mbar/min a extrapolação não vale, porque a queda se confunde com a recuperação |
| `end_slope_mbar_min`, `temp_end_K` (por liberação, no CSV) | inclinação (mínimos quadrados) dos últimos 60 s da espera e temperatura do fermentador ao fim dela. Uma pressão ainda caindo (inclinação negativa) é vazamento ou o gás ainda esfriando (~10 mbar por K a 2 bar: um Dallas caindo 8 mK/min explica 0,08 mbar/min); com o vazamento de 22/10 de manhã eram −1,4 mbar/min a 1,9 bar, e depois do reparo −0,25 a −0,35. Serve para ver o vazamento em todo ensaio, sem a rotina de espera |
| resíduo de forma | rms dos resíduos relativos das aberturas ≥ 4 s (informação) |
| deriva ± erro padrão | média, por patamar, da diferença de vazão entre o último e o primeiro 2 s (~8 min de distância). Com o ruído de leitura, o erro padrão é ~0,7%: deriva < ~1,5% num ensaio não é separável do ruído; a deriva entre lotes vem do histórico |
| contra a referência / a curva gravada | vazão do ensaio ÷ vazão da outra curva − 1 a 1,8 / 1,2 / 0,8 bar |

O desvio padrão dos resíduos das aberturas curtas (~15–45 ms com 0,15 mbar)
mede o ruído da leitura: ele aparece só no script.

### Fermentador com restrição na saída da válvula

Para fermentadores com headspace pequeno (10–30 L) a vazão da válvula é alta demais: a abertura que dá a queda alvo
(6%) fica abaixo do mínimo de 1,5 s (0,3–1,4 s com 10–20 L) e o firmware perde o alvo. Uma restrição na saída
(tubo curto) reduz a vazão (docs/vent.md, seção 3). Com ela:
- digite antes do ensaio uma curva inicial para a vazão restringida, só para os passos pousarem nos patamares
  (Aimbere, tubo de 1,0 mm × 20 mm: k 590 s, β 0,09, faixa 0,1–2,1 bar); o ensaio mede a curva verdadeira;
- o passo entre patamares chega a 100–175 s (teto de 240 s) e cada patamar de 6 aberturas derruba pouca pressão,
  então a pressão desce principalmente pelos passos;
- o teto de ventilação entre patamares do teste estanque é de 600 s (`docs/hold-test.md`).

### Qual curva usar (procedimento)

- A curva de cada fermentador fica **gravada na própria placa** (e no backup de configuração). O padrão do
  firmware só vale para uma placa nunca calibrada e **não é alterado por calibração**: o firmware serve a vários
  fermentadores.
- Depois de um ensaio válido (sem vazamento visível em `end_slope_mbar_min`), use **Use last test curve** na página
  de Calibração e **Save**, entre lotes. Não é preciso fazer média de ensaios: a repetibilidade de um ensaio
  (~0,4% no nível) é bem menor que a incerteza total (~0,8%, docs/vent.md, seção 4).
- k e β andam juntos: um ensaio isolado pode trocar um pelo outro (22/10/2026: 83,4/1,02; 82,0/1,09; 80,5/1,24),
  mas a vazão (tau) que descrevem é quase a mesma. Compare ensaios pelo **tau** (colunas de comparação acima), não
  por k e β.
- Ensaios de versões diferentes do ensaio (regra de parada da espera, tempo de abertura) não se misturam: vale o
  último, feito com a versão atual. Ao repetir o ensaio, se o tau ficar a menos de ~0,5% do anterior, mantenha a
  curva gravada; se divergir, investigue (vazamento, volume, temperatura) antes de trocar.

## 3. Histórico e gravação

- `/venttest6.bin` na partição `persist` (magic `VTH8`): os últimos **50
  ensaios**, cada um com os dados brutos de cada liberação (tempo pedido e real,
  P1, P_close, P2 e o ruído de P1 e P2, Tferm, espera, patamar, tipo), data/hora
  NTP, lote, Patm e fonte, Tamb, Tferm, gás, volume, versão do firmware (data de
  compilação), etiqueta e o ajuste. ~2 KB por ensaio, ~100 KB no total (o plano
  estimava 8 KB com menos liberações). Arquivo novo: a tag `te-final`, o histórico da cúbica (`/venttests.bin`, `VTH3`) e o da primeira versão do modelo novo (`/venttest2.bin`, `VTH4`) e os das versões com t0 (`/venttest3.bin`, `VTH5`) sem o P∞ (`/venttest4.bin`, `VTH6`) e sem a inclinação final (`/venttest5.bin`, `VTH7`) ficam intocados.
- Sempre com ar e o FMT volume configurado (a página não tem opções de gás nem de volume; o histórico
  continua gravando gás e volume). Decisão do usuário: sem ensaio com CO2 (caro); o erro da conversão
  ar → CO2 entra no fator de nível, ajustado pela SG de fermentações reais.
- Nomes dos CSV: `PovotoCalibration-Venting-FMT<n>-yyyymmddhhmm.csv` (hora local de início do ensaio;
  `nodate` sem NTP), `...-VentingHistory-...` com `?test=all` (hora do download) e prefixo `Incomplete-`
  durante o ensaio. Durante o ensaio, o link **Current test CSV** (`?test=current`) baixa as liberações
  concluídas, de uma cópia tirada sob trava (sem escrita na flash).
- CSV: `/calibration/venting.csv?test=0` (o último), `?test=N` ou `?test=all`
  (todos, do mais antigo ao mais novo); `;` e vírgula decimal.
- `python tests/venting_fit.py arquivo.csv` refaz o ajuste fora da placa
  (mesmas equações, `tests/vent_model.py`) e compara com o da placa.
- **Nada é gravado sem botão**: "Save curve" (a curva de vazão usada
  pelo vent) e "Save as reference curve" (a curva fixa das comparações). Só
  fora de Fermenting. Cada gravação vira um evento de calibração.

## 4. Modo debug

Sem válvula: a pressão é simulada (exponencial com a curva atual e o volume
do ensaio), com as mesmas esperas (5–10 s).

## 5. Ensaio de validação de espera (removido)

Uma rotina de uso único (`/settle/*`, 8 liberações com 20 min de espera, uma leitura por segundo) mediu a
recuperação da pressão depois de fechar a válvula. Resultados: recuperação exponencial com tau de 34–66 s e
amplitude de 18–25% da queda (liberações de 2–8 s), mais uma queda linear que era um **vazamento** (1,4–1,5
mbar/min a 1,9 bar), reparado em 22/10/2026 (depois 0,25–0,35 mbar/min). Esses resultados levaram à espera por
inclinação (seção 1) e às colunas `Pinf_*` e `end_slope_mbar_min`. O código foi removido da árvore principal; está
no commit marcado com a tag `vent-d-com-settle-2026-10-22`. `tests/recovery_fit_check.py` aceita o
`settle.csv` dessa rotina.
