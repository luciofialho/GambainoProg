# Avaliação: operar o Povoto sem tanque de expansão (TE)

Briefing para uma sessão nova. Escrito em 11/10/2026, ao fim de uma sessão
longa (termômetros Dallas, alarmes, análises de impacto). Leia também
`CLAUDE.md` (regras de trabalho) e os documentos citados abaixo.

## Objetivo

Avaliar, com números, se o Povoto pode dispensar o TE na fermentação:
ventilar o fermentador (TF) **direto para a atmosfera pela válvula, por um
tempo controlado**, contando os mols que saem por um modelo de vazão da
válvula, e usar o TE só na calibração. Nada de mexer no firmware sem o usuário
pedir: a primeira entrega é a análise e um plano de testes.

## Como funciona hoje (com TE)

- Hardware: uma única saída de solenoide (`PINTRANSFERVALVE`). Ligada, conecta
  TF → TE por ~6–7 s (tempo t(R,P) para deixar ~1% de resíduo); desligada, o TE
  ventila para a atmosfera por uma saída com silenciador de baixa resistência
  (sem válvula de retenção). **Confirmar com o usuário o tipo exato da válvula**
  (provavelmente 3 vias) e se ela permitiria ventilar o TF direto.
- Site principal: TF 129,6 L (pesado com água), TE 2,01 L (pesado). Fermenta a
  0,5, 1 e 2 bar. Site 3 (Aimbere): TF ~65 L (ver memória `povoto-site3-aimbere`).
- Headspace: razão de pressões manométricas, V = Vr_ef · f / (1 − f), com
  f = P_equilíbrio / P_antes. Independe da Patm e quase do ganho do sensor.
- CO2 expelido: pressão do TE no fechamento × Vr/k / RT, com k ≈ 1,08
  (aquecimento ~1,05 × piso ~1,03) e Tref do TE (`docs/historico-te/expansion-tank-k.md`).
  Quase todo o CO2 sai pelos reliefs (~37 de ~40 g/L numa cerveja 1,050 → 1,010).
- Sensibilidade: **1% de erro no CO2 expelido ≈ 0,4 ponto de SG** no fim.

## A ideia sem TE

- Abrir a válvula para a atmosfera por um tempo t; mols que saem = integral da
  vazão (sônica acima de ~0,85 bar manométrico, ∝ P_abs/√T; subsônica abaixo).
- Headspace = mols que saíram × RT / queda de pressão.
- Elimina: k, Tref do TE, resíduo de equalização, modelo de ventilação do TE.
- Acrescenta: dependência direta da vazão da válvula, da pressão **absoluta**
  (ganho do sensor e Patm entram direto) e do regime de escoamento.

## Números já levantados (sessão de 10–11/10)

- Ensaio de ventilação de 18/09 (`A:\Nuvem\Povoto\Calibracoes\venting_speed (5).csv`):
  TF vazio, ar, 10 liberações de 20 s de 1,98 a 0,36 bar. Constante de tempo da
  pressão absoluta τ = 213 → 301 s (cresce ao cair a pressão; sônico acima de
  ~0,85 bar). Vazão sônica ~1,7 bar·L/s com ar, ~1,4 com CO2 (fator teórico 0,79,
  nunca medido com CO2).
- Dispersão de cada liberação contra uma curva suave (quadrática em P): **~1,8%
  rms**, picos ±2–3%, com a válvula limpa.
- 30 L de headspace, 1,9 bar: relief normal derruba ~0,11 bar; ventilando direto
  pelo mesmo tempo (~7,4 s), ~0,32 bar (≈2,9×).
- Incerteza estimada da ventilação direta calibrada com CO2 (sem a conversão
  ar→CO2): sistemática ~1–2% (forma da curva, sensor/Patm, temperatura,
  abertura da solenoide), aleatória ~1–2% por liberação (cancela em centenas),
  **sujeira na válvula ao longo do lote: desconhecida** (o ponto decisivo).
  Com válvula limpa, comparável ao método com TE depois das calibrações (~1%).
- Sujeira: gotículas do krausen secam no furo da válvula (açúcar, proteína,
  lúpulo). Com TE, um furo menor só aumenta o resíduo (efeito ~0,5%); sem TE,
  reduz na mesma proporção o CO2 contado (3% menos furo → ~1,2 ponto de SG).
- Calibração do fator de vazão do CO2 pela SG medida (como já se faz com o
  kCO2): válida, mas cada leitura de SG desgaseificada (±0,5–1 ponto) fixa o
  fator a ~1,5–2,5%; o fator junta todos os erros; um fator por faixa de pressão
  (sônico/subsônico); não enxerga deriva dentro do lote. A estabilidade do fator
  entre lotes responde à viabilidade.

## Ferramentas já no firmware (branch `povoto-termometros-alarmes`)

- Ensaio de ventilação compara a vazão com a curva c, d, e configurada:
  página Calibration ("Valve flow against the configured curve", média e
  desvio) e CSV (`F_configurado_CO2`, `vazao_vs_config_pct`). Roteiro: repetir
  ao fim de um lote antes e depois de limpar a válvula → mede a sujeira.
- Verificação de cada relief (log Relief: `ReliefDropRatio`, `ReliefCheck`) e
  alarmes (`docs/alarms.md`).
- Roteiro de calibração de novembro: `docs/historico-te/calibration-roteiro.md`.

## Perguntas para a avaliação

1. A válvula atual permite ventilar o TF direto (sem TE)? Que mudança de
   hardware seria necessária?
2. Quanto a vazão da válvula muda num lote (ensaio antes/depois de limpar)?
   Qual limite torna a ideia viável (~1%?)?
3. Regime subsônico (0,5 bar): precisão da curva de vazão nessa faixa;
   influência do silenciador/saída e da Patm do dia.
4. Dessorção de CO2 da cerveja durante a medição pós-relief (afeta os dois
   métodos; com ventilação direta a queda é ~3× maior).
5. Controle de pressão: aberturas curtas e frequentes em vez de reliefs
   grandes? Efeito sobre espuma e sobre a resolução da medição do headspace.
6. Modelo híbrido: TE como referência periódica que recalibra a vazão durante
   a fermentação, maioria das ventilações direta.
7. Plano de testes com dados (o usuário prefere soluções com base física ou
   análise de dados), incluindo o que medir com CO2.

## Onde está o resto

- `docs/historico-te/calibration-speed-evaluation.md`: modelo de escoamento, aquecimento e
  piso; ensaios de velocidade.
- `docs/historico-te/expansion-tank-k.md`, `docs/historico-te/plan-expansion-tank-tref.md`: k e Tref.
- `include/GasFlowModel.h`, `src/PressureControl.cpp` (`processPressure`,
  `accountExpansionTankVenting`, ensaios de velocidade).
- Memórias: `povoto-expansion-tank-k-validation` (calibração de novembro),
  `povoto-site3-aimbere`.
- Dados: `A:\Nuvem\Povoto\Calibracoes`, `A:\Nuvem\Povoto\DeterminacaoVolume`,
  `A:\Nuvem\Povoto\Analises160`.
