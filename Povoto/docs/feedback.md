# Aba Feedback da calibração

Calibration → **Feedback**: valores aferidos durante o lote, só leitura, com botões "Use" para o expoente
politrópico e a janela de nucleação. Nada é aplicado automaticamente. Os indicadores ficam só nesta aba
(sem dashboard, cloud, TFT ou e-mail). Código: `src/Feedback.cpp`, funções puras em
`include/FeedbackMath.h`, espelho e bench em `tests/feedback_bench.py`.

## Premissas

- **`initialBeerVolume` não é usado para nada que exija precisão.** É uma estimativa grosseira do volume
  enchido; serve só para o tempo de abertura dos primeiros vents, antes de haver Vh medido.
- **Krausen e espuma** contam como headspace, não como líquido.
- **Sujeira** da válvula / silenciador não é monitorada: a rotina de limpeza é a prevenção.
- **Nenhuma task admite ar** (o dry hopping é feito por câmara estanque).
- **Calibração do vent dentro do lote: não há.** Todo sinal de pressão do vent determina só a razão
  V / (k · nível); sem um volume conhecido o nível da curva não pode ser verificado no lote. As âncoras
  continuam sendo o ensaio de ventilação e o `flowLevelFactor` pela DG degaseificada (manual P7). A forma
  da curva também não é verificada.

## 1. Conteúdo

| Seção | Mostra | Ação |
|---|---|---|
| Polytropic exponent | configurado; mediana e quantidade dos expoentes válidos (até 32), último; ar residual estimado | "Use" com ≥ 30 válidos e mediana em 1,00–1,30 |
| Nucleation window | configurada; última estimativa (`nucleation-estimate`) com a qualidade | "Use" só com qualidade `ok` e janela diferente da configurada |
| Vent | razão lado do fermentador / vazão (mediana dos últimos 32 vents em que a verificação valeu, último); Vh aparente (último, média de 24 h com estado e horas, EMA) | — |
| Leak | estado (`no data`, `ok`, `possible leak`), última janela (inclinação ± erro, mbar/min), janelas no lote, quantas seguidas; janela em curso ou o motivo da espera | — |

A seção Nucleation window da aba Physical continua lá; a Feedback só repete o valor configurado.

"Use" grava na hora, também durante o lote, sem rebase do headspace, e registra um evento de calibração
(`exponent-from-feedback`, `nucleation-from-feedback`) com o valor antigo e o novo. O expoente é gravado
com 3 casas, como a aba Physical o mostra. Trocar o expoente no lote desloca o Vh aparente (±7% entre 1,20
e 1,40, `docs/vent.md`) e a média de 24 h mistura os dois por até 24 h; a contagem de CO2 muda < 0,25%.

## 2. Expoente: só sem ar no headspace

O lote começa com o headspace só de ar (gamma 1,40), que puxa o expoente medido para cima. A fração de ar é
estimada só por pressões:

    ar = Π (P/Z depois / P/Z antes) · (P_início / T_início) / (P_agora / (Z · T_agora))

com as pressões assentadas de cada vent (P_on extrapolada; a medida quando não há referência de subida, o
que só deixa mais ar) e P_início, T_início do lote (`startPressure`, `startTemperature`). O volume do
headspace cancela. Conservador: tudo ar no início; um vent sem pressões utilizáveis não tira ar.

O expoente de um vent (estimado quando o seguinte abre, `PolytropicEstimatedExponent`) entra só se:
- o vent foi contado em Fermenting, fora de janela de task e com efeito;
- o ar estimado logo depois dele estava abaixo de 5%;
- nenhuma task aconteceu até o vent seguinte abrir;
- o resíduo rms do ajuste da recuperação ≤ 2 mbar (provisório: a definir com os logs de lotes);
- o ajuste existiu (≥ 8 leituras de 30 s depois de 3 min: intervalos de vent curtos não medem).

Com queda de 6% e início a 0 bar, o ar fica abaixo de 5% depois de ~70 vents a 1 bar e ~120 a 0,5 bar
(bench).

## 3. Possível vazamento

Inclinação de (P + Patm) / T_gás contra o tempo com a válvula fechada (a mesma conta do LeakHold), em
janelas de 60 min com uma leitura a cada 30 s (no mínimo 96). Fermentando ela é positiva; negativa de forma
sustentada só pode ser perda de gás.

- **Modos**: Fermenting e Conditioning. Off e Manual control não avaliam.
- **Invalida a janela em curso e zera as seguidas**: mudança de modo, rotina de calibração, sensor de pressão
  inválido ou em alarme, qualquer task (inclusive o repouso de nucleação), em Fermenting o setpoint de pressão
  mudando (`CHANGING_*`), vent (ou válvula aberta à mão), temperatura fora de estável.
- **Equilíbrio antes de voltar a avaliar**: a janela de nucleação configurada (no mínimo 30 min) depois de
  task, mudança de setpoint, mudança de modo ou boot; 3 min depois de um vent; temperatura `STABLE` há 2 h em
  Fermenting e 6 h em Conditioning (no frio o CO2 continua se dissolvendo por horas e derruba a pressão).
- **Janela parecida com vazamento**: inclinação < −0,05 mbar/min e mais negativa que 3 erros-padrão.
  **3 seguidas** → `possible leak`. Uma janela que não parece vazamento → `ok` e zera as seguidas.
- Limitação: com fermentação ativa só aparece vazamento maior que a geração (e os vents interrompem as
  janelas); funciona melhor no fim da fermentação e em Conditioning estável.
- Bench: sem vazamento, com ruído de 0,5 mbar (branco ou autocorrelacionado, rho 0,9) nenhuma janela em 2000
  parece vazamento; com 1 mbar e rho 0,95, 1,2% delas (3 seguidas: ~2e-6). Uma perda de −0,1 mbar/min é
  vista em todas; um resfriamento de 0,5 K/h sem perda dá inclinação ~0.

## 4. Persistência

NVS `pvt_feedback`, um blob: produto do ar, os últimos 32 expoentes e razões, os últimos valores, o estado
do vazamento. Gravado uma vez por hora quando mudou, no início de um OTA (`ElegantOTA.onStart`) e no novo
lote (quando é zerado). Um flash por USB ou queda de energia perde no máximo uma hora. A janela de vazamento
em curso fica só na RAM: um boot começa outra, depois do equilíbrio. O factory reset limpa o namespace.
