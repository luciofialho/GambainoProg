# Avaliação dos testes de velocidade (expansão e ventilação)

Data: 2026-10-02. Dados: `A:\Nuvem\Povoto\Calibracoes` (`expansion_speed (21).csv`,
17/09, 50 expansões; `venting_speed (5).csv`, 18/09, 10 liberações), fermentador
vazio, ar. Scripts da análise: `cal1.py`–`cal3.py` (scratchpad da sessão).

## 1. Como os testes funcionam hoje

- **Expansão:** abre a válvula de transferência por t ∈ {1, 2, 4, 6, 8, 10, 12, 14, 16, 30} s,
  5 ciclos (50 registros), espera fixa de 3 min e lê P2. Calcula
  `PL = P1·V/(V + Vr)` (equalização isotérmica, V = FMTVolume) e
  `R = (P2 − PL)/(P1 − PL)`. Os coeficientes a, b do tempo de abertura
  (`t = −ln(r)/(a − b·P)`) são digitados à mão.
- **Ventilação:** com a saída da válvula ligada à atmosfera, 10 liberações de 20 s
  do próprio fermentador, espera de 3 min; `F = P2/P1`. A parábola
  `F(P) = c·P² + d·P + e` é digitada à mão e escalada para o tanque por
  `F^(t·Vf/(20·Ve))`.
- **Duração:** expansão ≈ 50 × (180 s + t) ≈ **2 h 40 min**; ventilação ≈ 10 × 200 s ≈ **33 min**.
- **Leituras:** cada P1/P2 é uma leitura única (sem média). O CSV não tem
  data/hora, temperaturas nem os volumes usados.
- **`/calibration/fit-expansion`:** ajusta `R = c·t^−b` (lei de potência), que
  não é a forma usada pelo firmware (exponencial em t). Está obsoleto.

## 2. Achados

### 2.1 O CSV de 17/09 foi gerado com FMTVolume = 134,0 L

`PL/P1 = 0,985222` em todas as linhas: V = 134,0 L, não os 129,6 L pesados.
R depende diretamente de V; com o volume errado o resíduo de longa abertura some
artificialmente (R(30 s) ≈ 0) e os coeficientes ajustados ficam enviesados.
Recalculado com 129,6 L:

| t (s) | R (5 ciclos, P1 de 1,9 a 1,0 bar) |
|---|---|
| 1 | 0,64 0,59 0,56 0,60 0,55 |
| 2 | 0,35 0,29 0,27 0,29 0,25 |
| 4 | 0,15 0,13 0,15 0,11 0,16 |
| 6 | 0,13 0,10 0,13 0,11 0,10 |
| 8 | 0,10 0,07 0,07 0,05 0,10 |
| 10 | 0,10 0,08 0,08 0,05 0,07 |
| 14 | 0,04 0,04 0,02 0,04 0,04 |
| 30 | 0,032 0,033 0,029 0,051 0,053 |

### 2.2 R tem três componentes, não uma

Uma exponencial só não descreve os dados (a taxa aparente cai de 0,55/s em
t = 1–4 s para ~0,3/s depois). Modelo ajustado (50 pontos, V = 129,6 L):

```
R(t, P) = exp(−(a − b·P)·t)  +  H·exp(−t/τh)  +  p0/P
          escoamento            aquecimento        piso
```

| Parâmetro | Valor | ± (1σ) | Significado |
|---|---|---|---|
| a | 1,46 /s | 0,28 | escoamento (ar) |
| b | 0,34 /s/bar | 0,11 | |
| H | 0,18 | 0,07 | aquecimento do tanque na equalização |
| τh | 5,6 s | 2,1 | resfriamento do tanque com a válvula aberta |
| p0 | 0,052 bar | 0,012 | piso de pressão |

RMSE 0,022 (o ruído de cada ponto é ~0,015–0,02, ou ~0,5 mbar na pressão), contra
0,045–0,068 da exponencial única atual. O piso como constante (em vez de p0/P)
ajusta igual; a física (abaixo) favorece p0/P.

- **Escoamento:** mais rápido que o modelo atual. Com ar, 1% de resíduo de
  escoamento em 5,7 s a 1,9 bar e 3,9 s a 0,8 bar. O CO2 escoa ~21% mais devagar
  em mols (vazão crítica ∝ √(γ/M)): 1% em ~7,2 s a 1,9 bar e ~4,9 s a 0,8 bar.
  Os tempos usados hoje (7,4 s e 6,1 s) deixam ≤ ~1% de escoamento com CO2: a
  premissa r = 1% está coerente, por coincidência.
- **Aquecimento:** resíduo de 4,8% aos 7,4 s (6,0% aos 6,1 s), ou seja
  **k_aquecimento ≈ 1,05 (ar)**. É a primeira medição direta do k em função do
  tempo, independente de SG e de volume de cerveja.
- **Piso p0 ≈ 0,05 bar:** mesmo com 30 s de válvula aberta sobra uma diferença
  com R·P1 ≈ constante. Duas causas dão exatamente essa assinatura:
  1. **pressão residual no tanque** que não ventila (válvula de retenção na
     saída com pressão de abertura ~0,05 bar, ou ventilação incompleta):
     R = p0/P1;
  2. **offset no zero do sensor** de +0,05 bar: R = e/(P1 + e).

### 2.3 O piso explica o resto "sem causa" dos ensaios de volume

Com p0 = 0,052 bar e 129,6 L:

- rotina lenta (3 min, 1,84 → 1,56 bar): 129,6 × (1 + p0/P·(V+Vr)/V) =
  **133,6 L** (medido 133,5);
- rotina rápida (sem k, sem compensar r): 129,6 × 1,055 (aquecimento) × 1,031
  (piso) × 1,01 (resíduo) ≈ **142 L** (medido 141,2);
- lote 160 (CO2, 1,9 bar): kCO2 ≈ 1,08 pelo SG ≈ aquecimento (~1,05) × piso
  (1 + 0,05/1,8 ≈ 1,03).

O k = 1,08 usado hoje mistura aquecimento e piso. O piso depende da pressão
(2,8% a 1,8 bar, 6,5% a 0,8 bar), então um k fixo erra na fase a 0,8 bar.

### 2.4 Ventilação

- A parábola ajusta muito bem (RMSE 0,0014). Um orifício ideal (vazão crítica e
  subsônica, 1 parâmetro) não ajusta (RMSE 0,012): a dependência medida com P é
  mais plana. A parábola empírica é adequada.
- Escalada para o tanque de 2,01 L, a ventilação deixa < 0,5% em 5 s e ~0 em
  10 s. Com reliefs a cada vários minutos, a ventilação não afeta a contabilidade;
  só importa para o tempo mínimo de ventilação no modo de fluxo de gás.
- Com ar; o CO2 ventila ~21% mais devagar em mols.

## 3. Premissas novas para o modelo

1. **Piso p0 explícito**, separado do k. Se for pressão residual no tanque: mols
   ejetados por relief = (P_tanque − p0)·Vr/(k·R·Tref); rotina de volume com
   `P2 = (P1·V + p0·Vr)/(V + Vr)`. Se for offset do sensor: corrigir a calibração
   do sensor (não entra no modelo). **Precisa ser identificado antes.**
2. **k = só aquecimento** (~1,05 com ar aos 7 s), calibrado pelo teste de expansão
   com o fermentador vazio e V conhecido; transposição para CO2 como em
   `docs/expansion-tank-k.md`.
3. **Gás do teste:** os coeficientes de escoamento e ventilação medidos com ar
   valem ~21% mais rápidos que para CO2 (fator √(γ/M) da vazão crítica). Ou
   corrigir por esse fator, ou registrar o gás e calibrar com CO2.
4. **V verdadeiro no teste:** R e os coeficientes dependem do FMTVolume; o teste
   precisa do volume pesado (129,6 L).

## 4. Testes mais rápidos

- **Espera adaptativa:** em vez de 3 min fixos, amostrar a pressão durante a
  espera e encerrar quando a variação nos últimos 30 s ficar abaixo do ruído
  (~0,2 mbar), com mínimo de ~45 s e máximo de 180 s. A espera é 95% do tempo do
  teste.
- **Média das leituras:** P1 e P2 como média de ~10 leituras (os últimos ~10 s),
  reduzindo o ruído de ~0,5 mbar para ~0,15 mbar: menos pontos para a mesma
  precisão.
- **Expansão, desenho proposto:** t ∈ {1, 2, 3, 5, 7, 10, 15, 40} s, 3 ciclos
  (24 registros; o de 40 s mede o piso sem resto de aquecimento). Com espera
  adaptativa de ~60–90 s: **~35–45 min** (hoje 2 h 40).
- **Teste curto só de k e piso:** alternar o tempo de abertura da fermentação
  (~7 s) e 40 s, 4 vezes cada: 8 registros, **~12–15 min**.
- **Ventilação:** 6 liberações (2 → ~0,8 bar) com espera adaptativa: **~8–10 min**.
  Como a ventilação não pesa na contabilidade, pode ser rodada raramente.

## 5. Colunas propostas (os dois CSVs)

`data_hora` (local), `epoch`, `gas`, `FMTVolume`, `Vr`, `Patm`,
`temp_fermentador_P1`, `temp_fermentador_P2`, `temp_ambiente_P1`,
`temp_ambiente_P2`, `P1` (média), `P_fechamento` (≈0,5 s após fechar: dá o
expoente politrópico do headspace), `P_15s`, `P_30s`, `P_60s`, `P2` (média),
`espera_s`, `PL`, `R`, `R_x_P1` (diagnóstico do piso), `k_efetivo = 1/(1 − R)`.

## 6. Derivações no próprio firmware (proposta)

- Depois do teste de expansão: piso p0 = mediana de R·P1 das aberturas longas;
  k_aquecimento no tempo de abertura da fermentação =
  1/(1 − (R(t) − p0/P1 − escoamento(t))); a, b do escoamento pelos pontos curtos
  (t ≤ 4 s) já descontados piso e aquecimento. Mostrar e oferecer gravar, como na
  calibração do k da rotina de volume.
- Retirar o `/calibration/fit-expansion` (lei de potência).

## 7. Respostas do usuário (2026-10-02)

1. A saída de ventilação do tanque tem só um silenciador de baixa resistência
   (sem válvula de retenção): pressão residual no tanque é improvável.
2. Aberto para a atmosfera, o sensor lê 0,000 bar: offset puro de zero descartado.
3. Todos os testes foram, e serão, com ar; os valores para CO2 vêm por cálculo e
   são refinados pelos logs das fermentações.
4. **Os 129,6 L já incluem todos os volumes de gás ligados ao fermentador**
   (mangueiras, conexões, linha do sensor). Não é volume extra.
5. O manômetro de glicerina do tanque bate com o do tanque vizinho em várias
   pressões, após uma pausa (é lento).

Com isso, a causa mais provável do piso é um erro do sensor na faixa de trabalho
que não aparece no zero: para pequenas quedas, R·P = h(P) − P·h'(P), o intercepto
da tangente da curva de erro h(P) do sensor na pressão de trabalho. Um ganho
errado se cancela na razão; um intercepto local de ~0,05 bar não. Teste: o piso
em pressão baixa (cresce com 1/P se for o sensor).

## 8. Plano (revisão de 2026-10-02) — fase B implementada

Implementado: seções 8.1–8.3 (testes, saídas, botões, fator CO2/ar). O ajuste da
expansão alterna as etapas de aquecimento e escoamento por 5 passadas (nos dados
de 17/09: k a 1,5 bar 1,056 contra 1,063 do ajuste completo de 5 parâmetros).
Roteiro dos ensaios: `docs/calibration-roteiro.md` (cópia em
`A:\Nuvem\Povoto\Calibracoes\ROTEIRO-calibracao.md`).

Premissas: todos os ensaios com ar e o fermentador vazio (V = 129,6 L, já com
todas as linhas). Os valores para CO2 saem por cálculo e são refinados pelos logs.
Os parâmetros gravados continuam valendo **para CO2** (a fermentação não muda de
semântica); a rotina mostra o valor medido com ar e o convertido, e grava o
convertido. Conversão ar → CO2 do escoamento e da ventilação: fator
`f_CO2/ar` = 0,79 (vazão crítica ∝ √(γ/M)), parâmetro novo de FMT, editável.

### 8.1 Mudanças comuns aos testes de velocidade

- Escolha do gás (ar por padrão; CO2 só se um dia for usado).
- Espera adaptativa depois de cada fechamento e antes de cada abertura: lê a
  pressão do TF a cada ~1 s; encerra quando a variação nos últimos 30 s for menor
  que ~0,3 mbar; mínimo 45 s, máximo 180 s. Só o TF entra no critério (com a
  válvula fechada o TE não afeta P2; ele ventila em 5–10 s e volta à temperatura
  em ~6 s).
- P1 e P2 = média das leituras dos últimos 10 s da espera (sobre o filtro atual:
  INA com 16 conversões e mediana de 9 amostras, ~0,3 s).
- Pressão mínima de início: 1,9 bar para o teste completo de expansão e a
  ventilação; ≥0,3 bar para o teste curto de k e piso (para medi-lo em pressão baixa).
- Colunas (os dois CSVs): `data_hora`, `epoch`, `gas`, `FMTVolume`, `Vr`, `Patm`,
  `temp_ferm_P1`, `temp_ferm_P2`, `temp_amb_P1`, `temp_amb_P2`, `tempo_aberto_s`,
  `P1`, `P_fechamento` (~0,5 s após fechar), `P_15s`, `P_30s`, `P_60s`, `P2`,
  `espera_s`; expansão: `PL`, `R`, `R_x_P1`, `k_efetivo = 1/(1 − R)`; ventilação:
  `F = P2/P1`.
- Retirar `/calibration/fit-expansion`.

### 8.2 Rotinas, saídas e uso

**(1) Teste de expansão completo** — aberturas 1, 2, 3, 5, 7, 10, 15 e 40 s × 3
ciclos (24 registros, ~35–45 min). No fim, ajuste em etapas (todas com ar):

1. piso: mediana de R·P1 dos pontos de 40 s (mostrado também como R constante);
2. aquecimento H, τ: regressão de ln(R − piso) contra t nos pontos de 5–15 s;
3. escoamento a, b: dos pontos de 1–3 s, `−ln(R − aquecimento − piso)/t = a − b·P`.

| Saída na tela | Uso | Gravação |
|---|---|---|
| a, b (ar) e convertidos para CO2 (× 0,79) | coeficientes do tempo de abertura (Calibration) | botão "Gravar a, b (CO2)" |
| H, τ (ar); k_aquecimento nos tempos de abertura da fermentação a 0,8 e 1,9 bar | kAir (Settings): valor a 1,5 bar | botão "Gravar kAir" |
| kCO2 estimado = 1 + 1,3·(kAir − 1) | kCO2 (Settings) | botão "Gravar kCO2 estimado" |
| piso (R·P1 e R) | diagnóstico (sensor ou volume) | não grava |
| expoente do headspace (de P1, P_fechamento, P2) | diagnóstico (ar, fermentador vazio; o da fermentação é com CO2 e cerveja) | não grava |
| RMSE e número de pontos | qualidade do ajuste | — |

**(2) Teste curto de k e piso (novo)** — alterna o tempo de abertura da
fermentação na pressão atual e 40 s, 4 vezes cada (8 registros, ~12–15 min);
pode começar em ≥0,3 bar.

| Saída | Uso | Gravação |
|---|---|---|
| piso nessa pressão (R·P1 e R) | comparar com outra pressão: ∝ 1/P = sensor, constante = volume | não grava |
| k_aquecimento no tempo da fermentação (descontado o piso) | kAir | botão "Gravar kAir" |

**(3) Teste de ventilação** — 6 liberações de 20 s (2 → ~0,8 bar, ~8–10 min).

| Saída | Uso | Gravação |
|---|---|---|
| parábola F(P) = c·P² + d·P + e (ar, mínimos quadrados) e convertida para CO2 (F_CO2 = F_ar^0,79) | coeficientes de ventilação (Calibration) | botão "Gravar c, d, e (CO2)" |

**(4) Rotina de volume (rápida e lenta)** — sem mudança de saídas (volume, k
calibrado contra o FMTVolume). Na rotina rápida com ar, o resíduo compensado passa
a ser o do ar no tempo de abertura (`exp(−(a, b do CO2)/0,79 ·t)`), não 1% fixo.
Diferença entre os k: o da rotina de volume inclui o piso (ela calibra contra o
volume); o dos testes de expansão é só aquecimento.

### 8.3 Tabela de parâmetros

| Parâmetro | Tela | Origem | Uso |
|---|---|---|---|
| a, b (tempo de abertura, CO2) | Calibration | teste (1), convertido | abertura na fermentação; rotina rápida (convertido de volta para ar) |
| resíduo alvo r (1%) | Calibration | usuário | tempo de abertura e compensação |
| c, d, e (ventilação, CO2) | Calibration | teste (3), convertido | ventilação do TE na fermentação |
| fator CO2/ar (0,79) | Calibration (novo) | cálculo | conversões acima |
| kAir | Settings | testes (1)/(2) (só aquecimento) ou rotina rápida (aquecimento + piso) | rotina rápida de volume |
| kCO2 | Settings | estimado do kAir; refinado por `KCO2FromBeerVolume` e SG | fermentação |
| FMTVolume, Vr | Settings | pesagem (129,6 / 2,01) | todos |
| expoente do headspace (1,29) | Settings | inalterado; a rotina só mostra o valor do ar | fermentação |
| piso | — | testes (1)/(2) | diagnóstico (fase C) |

### 8.4 Fase C (depois de medir o piso em pressão baixa)

- Piso ∝ 1/P (sensor): recalibrar o sensor na faixa de trabalho; depois o kCO2
  passa a ser só aquecimento (~1,04–1,06 transposto do ar).
- Piso constante: rever V ou Vr (já confirmados; improvável).
- Enquanto isso, o kCO2 = 1,08 continua (ele absorve aquecimento + piso a ~1,8 bar;
  a 0,8 bar o piso pesaria ~6,5% em vez de ~3%).
