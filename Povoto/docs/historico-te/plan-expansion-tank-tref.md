# Plano: temperatura de referência do tanque de expansão (T_ref) e calibração do k

Status: implementado (T_ref, calibração do k em toda rotina rápida, kCO2 estimado,
KCO2FromBeerVolume). Documentação atual: `docs/expansion-tank-k.md`.
Base: commit `30e75b9` (fator k e correção do resíduo, ver `docs/expansion-tank-k.md`).

## 1. Contexto

O Povoto é um fermentador pressurizado (ESP32-S3). O CO2 produzido é medido por
reliefs: quando a pressão do fermentador (TF) chega ao limiar, uma válvula de
transferência abre por alguns segundos e liga o headspace do TF a um tanque de
expansão (TE) inicialmente à pressão atmosférica. Depois o TE é ventilado. O
CO2 contado por relief é o que ficou no TE no fechamento:

```
n_relief = P_TE,fechamento(manométrica) · Vr / (R · T)
```

O volume de headspace (Vh) é estimado pela razão manométrica da queda de pressão
do TF: `Vh = Vr · f / (1 − f)`, com f estendido à equalização completa. A soma
ejetado + headspace + dissolvido dá o CO2 produzido, e daí o SG.

Volumes deste equipamento: TF 129,6 L (pesado com água duas vezes), TE 2,01 L
(pesado com balança de precisão). Tempo de abertura: o que deixa 1% da diferença
de pressão (`r = 1%`), ~6 s a 0,8 bar e ~7,4 s a 1,9 bar.

### O que já está no firmware (commit 30e75b9)

- Pressão do TE no fechamento pela física: `P_TE = (1 − r)·P_on − (P_on − P_after)`.
- Fator de enchimento k: a contabilidade usa o volume efetivo `Vr / k`.
  `kCO2` (fermentação) e `kAir` (rotina rápida de volume), padrão 1,08, faixa 1,0–1,5.
- Rotina rápida de volume: fator estendido à equalização, `f_eq = 1 − (1 − f)/(1 − r)`,
  e volume com `Vr/kAir`. Rotina lenta (válvula aberta 3 min): volume físico, sem resíduo.
- CSV da rotina com temperatura ambiente, modo, k, volume efetivo, fator equalizado
  e `kAr_para_FMTVolume`.

## 2. Física

### 2.1 Por que existe o k

Numa expansão curta, o gás empurrado para o TE esquenta: o trabalho de fluxo (P·v)
de cada porção que entra vira energia interna do TE, e as primeiras porções são
comprimidas pelas seguintes. O resfriamento correspondente acontece no headspace,
mas se divide por um volume muito maior (Vh/Ve ≈ 15 na fermentação, 65 com o TF vazio),
então é pequeno (1–4 K). O gás que atravessa a válvula não esfria de forma permanente:
o estrangulamento de um gás ideal conserva a entalpia (no CO2 real há ~1 K/bar de
Joule-Thomson).

Balanço de energia do TE (volume aberto, gás bem misturado, entrada com entalpia a T0):

```
cv·(n_f·T_f − n_0·T0) = cp·T0·Δn − Q
```

Sem perda de calor (Q = 0), com `n·T = P·V/R`:

```
Δn = ΔP_TE · Ve / (γ · R · T0)
```

Com perda de calor, definindo `q = Q / (calor que a compressão geraria)`:

```
Δn = ΔP_TE · Ve / (k · R · T0),     k = γ / (1 + q)
```

- `q = 0`: k = γ (1,29 CO2, 1,40 ar). Teto.
- `q = γ − 1`: k = 1 (isotérmico).
- O γ aparece dividindo, não como expoente: o TE é um sistema aberto que recebe gás.
  No headspace (massa fechada que se expande) o γ é expoente (`P·V^γ = const`).
- `Δn = ΔP·(Ve/k)/(R·T0)`: o TE se comporta como um tanque isotérmico de volume Ve/k.
  Por isso o volume efetivo `Vr/k` é exato dentro do modelo.

Exemplo numérico (Vh = 20 L, Ve = 1 L, TF a 3 bar abs, TE a 1 bar abs, Patm = 1 bar):

| | Isotérmico (k = 1) | Adiabático CO2 (γ = 1,29) |
|---|---|---|
| Mols transferidos (bar·L a T0) | 1,905 | 1,493 |
| Pressão de equilíbrio (abs) | 2,905 | 2,925 |
| Queda no TF | 0,095 bar | 0,075 bar |
| Temperatura média do TE no fechamento | T0 | T0 + 52 K |

O firmware sem k contaria 1,925 (pressão medida no TF depois do relief, TE suposto a T0):
1,925/1,493 = 1,29 = γ. O Vh estimado seria 25,8 L em vez de 20 L (mesmo fator).
Note que k ≠ T_fechamento/T0 (1,17): a contagem é do gás que entrou, e o gás que já
estava no TE esquentou mais.

### 2.2 Por que o headspace pode ser tratado como adiabático e o TE não

A pressão do TF é lida 0,4 s após o fechamento. No lote 160, depois de um relief a
1,9 bar, o equilíbrio isotérmico seria ~1,843 bar; a pressão estava em 1,8106 em
0,4 s, 1,8113 em 1 s, 1,8146 em 5 s, 1,834 em 45 s. A constante de troca do headspace
é da ordem de 1 minuto (volume grande, gás quase parado, ΔT de 2–4 K). O TE tem 2 L,
jato turbulento e ΔT de 50–70 K: troca em poucos segundos. Com k = 1,08, q = 0,194,
ou seja, ~2/3 do calor sai para a parede durante o enchimento.

### 2.3 Temperatura de referência do TE (proposta)

Hoje o TE é contado com a temperatura do fermentador (T_ferm). Mas a parede do TE
está no ambiente (T_amb) e o gás que já estava no TE ficou ali desde o relief anterior.
A mesma troca de calor que remove ~2/3 do excesso da compressão também puxa ~2/3 da
diferença T_ferm − T_amb (supondo troca linear em `T_gás − T_parede`, mesmo coeficiente).
A troca não iguala em ~7 s; a temperatura de referência fica entre as duas:

```
T_ref = T_amb + φ · (T_ferm − T_amb)
φ = 1 − (γ/k − 1)/(γ − 1)          (fração não removida; φ ≈ 0,33 com k = 1,08, γ = 1,29)
n_relief = ΔP_TE · (Vr/k) / (R · T_ref)
```

Um único parâmetro calibrado (k) dá as duas correções. Com T_amb = T_ferm, volta ao
comportamento atual. Efeito estimado de contar com T_ferm em vez de T_ref:

| Situação | T_ferm | T_amb | T_ref | Erro atual |
|---|---|---|---|---|
| Lote 160 | 21 °C | 22 °C | 21,7 °C | 0,2% |
| Ambiente frio | 20 °C | 10 °C | 13,3 °C | conta 2,3% a menos |
| Ambiente quente | 20 °C | 30 °C | 26,7 °C | conta 2,2% a mais |
| Cold crash | 2 °C | 25 °C | 17,4 °C | conta 5,3% a mais |

## 3. Evidências

| Fonte | Gás | Resultado |
|---|---|---|
| Rotina rápida 20/09 (`pressure_history (59).csv`), TF vazio, 24 reliefs, 1,92 → 1,25 bar, ~27 °C | ar | fator 0,98597 → 141,2 L (sem k, sem compensar r). Com r = 1% compensado, kAir = 1,079 devolve 129,6 L |
| Rotina lenta 17/09 (`lento.csv`), 11 reliefs, 1,84 → 1,56 bar, ~21 °C, válvula aberta 3 min | ar | fator ~0,98520 → ~133,5 L (±1,6 L). Sem k, sem r. Aquecimento dissipado, como previsto, mas sobra ~3% |
| SG desgaseificado do lote 160, 01/10 (1,009 medido, 1,0061 calculado) | CO2 | exige k ≈ 1,08 |
| Vh estimado no lote 160 a 0,8 e a 1,9 bar (30/09, mesma cerveja) | CO2 | 31,46–31,62 L e 31,19–31,43 L: k independente da pressão dentro de ~1% |
| SG desgaseificado do lote 160, 29/09 (1,0185 medido, 1,022 calculado) | CO2 | **não explicado por k** (piora com k > 1); suspeita de supersaturação na fase ativa |

Na rotina lenta, a temperatura ambiente fica em média 5 °C abaixo da do TF (o TF
recebe ar comprimido). Um TE mais frio guarda mais mols e puxaria o volume para
**baixo** (esperado ~127,4 L). O medido é 133,5 L: a parte não explicada sobe para
~5%. Ela não depende do tempo de abertura. Hipóteses: offset no zero do sensor de
pressão (~+0,08 bar; o erro cresceria com 1/P), o gás do TF esquentando mais do que a
sonda indica (~1,6 K não corrigidos em 70 min), ou pressão residual no TE antes de
cada relief (~0,08 bar; também ∝ 1/P). Descartados por darem o sinal oposto: vazamento,
volume extra de tubos do lado do TE.

## 4. Mudanças propostas

### 4.1 T_ref na contabilidade

Em `PressureControl.cpp`, onde o k já entra (`accountingReliefVolume()`,
`fermentationReliefVolume()`, `volumeRoutineReliefVolume()`):

- Ejetado por relief: `P_TE · (Vr/k) / (R · T_ref)`.
- Vh (fermentação e rotina): volume efetivo do TE relativo ao gás do TF contado a
  T_ferm: `Vr_ef = (Vr/k) · T_ferm / T_ref`; `Vh = Vr_ef · f/(1 − f)`. O inverso
  (`pressureDropFactor = Vh/(Vh + Vr_ef)`, usado no limiar do relief) com o mesmo Vr_ef.
- γ: 1,29 com kCO2, 1,40 com kAir.
- T_amb: `environmentTemp` (já medido). Se inválido, T_ref = T_ferm (comportamento atual).
- Tempos de válvula, ventilação e modelo de fluxo continuam com o volume físico.

### 4.2 Toda rotina rápida também calibra o k

Uma rotina dá uma equação: resolve o volume (k conhecido) ou o k (volume conhecido).
A rotina rápida passa a calcular as duas sempre:

- No início: escolha do gás, ar (padrão) ou CO2 (TF purgado). Define γ e qual k
  entra no cálculo do volume.
- Por relief: o k que faz o volume bater com o FMTVolume. Fator por relief
  `f_i = P_after,i / P_before,i` (manométricas, pressões lidas após a espera de 2 min,
  corrigidas pela temperatura do TF), `f_eq,i = 1 − (1 − f_i)/(1 − r)`,
  `Vr_ef necessário = FMTVolume · (1 − f_eq,i)/f_eq,i`, e o k que resolve
  `(Vr/k) · T_ferm / T_ref(k) = Vr_ef necessário` (busca por bisseção entre 1,0 e γ).
- No fim (tela, página Calibration e serial): volume com o k atual; k calibrado
  (mediana dos reliefs, dispersão, número de reliefs); botão "Gravar k" (kAir ou kCO2
  conforme o gás). Com ar, também "Gravar kCO2 estimado" (4.3).
- O botão só faz sentido com o TF vazio e o FMTVolume correto. Aviso na página:
  calibrar com o TF a até ~3 °C do ambiente (k sai quase sem depender da hipótese
  linear da T_ref) e repetir numa faixa de pressão mais baixa (~1 → 0,5 bar) para
  detectar efeitos ∝ 1/P. Nada é gravado sem confirmação.
- CSV: colunas novas `T_ref` e `k_relief`.

### 4.3 kCO2 estimado a partir do kAir

`kCO2 = kAir · 1,29/1,40` não funciona (dá < 1). O que se transpõe entre gases é a
fração de calor perdida. Com a mesma fração nos dois gases:
`L = (1,40/kAir − 1)/0,40`, `kCO2 = 1,29/(1 + 0,29·L)` → 1,06 para kAir = 1,079.
O CO2 perde calor mais devagar (mais capacidade térmica por mol, menor condutividade);
com uma constante ~1,8× maior, ~1,12. Faixa 1,06–1,12; o 1,08 do SG do lote 160 está
dentro. Estimativa proposta, simples e no meio da faixa:

```
kCO2 ≈ 1 + 1,3 · (kAir − 1)          (1,10 para kAir = 1,079)
```

### 4.4 kCO2 pelo volume de cerveja, no log Relief

Cada relief da fermentação é um pequeno ensaio de volume com CO2. Se o volume de
cerveja for conhecido (fermentador pesado na transferência; campo `initialBeerVolume`),
o Vh real é `FMTVolume − (initialBeerVolume − dumpedVolume)`. Coluna nova no log
Relief, como foi feito para o expoente politrópico:

- `KCO2FromBeerVolume`: o k que resolve, com o fator instantâneo do relief
  (`adjustedEquilibriumPressure / pressureOnReliefExtrapolated`), a mesma equação de 4.2
  com o Vh real no lugar do FMTVolume (γ = 1,29).
- Vazio se `initialBeerVolume` não foi informado. Só diagnóstico; não altera controle
  nem contabilidade.
- Sensibilidade: com Vh ~30 L, 0,3 L de erro no volume de cerveja ≈ 1% no k. Exige
  pesagem. O krausen ocupa headspace: usar o início ou o fim da fermentação.

### 4.5 Documentação

Atualizar `docs/expansion-tank-k.md` com o modelo da T_ref, o procedimento de
calibração, a estimativa do kCO2 e o método pelo volume de cerveja.

## 5. Validação proposta

1. Calibrar o kAir com a rotina rápida (TF vazio, TF a ≤3 °C do ambiente).
2. Rodar a rotina rápida normal com diferença grande TF–ambiente (TF refrigerado):
   deve devolver ~129,6 L se a T_ref linear estiver certa.
3. Repetir a calibração numa faixa de pressão baixa: se o k mudar, há efeito ∝ 1/P
   (offset do sensor ou resíduo no TE) a resolver antes.
4. Rotina lenta a ~0,5 bar: se o erro de ~5% triplicar, é offset/resíduo; se ficar
   ~5%, é térmico.
5. No próximo lote: pesar a cerveja transferida, acompanhar `KCO2FromBeerVolume` e
   medir SG desgaseificado a cada 12–24 h.

## 6. Limitações e pontos a revisar

- Hipótese linear da T_ref: mesmo coeficiente de troca para o excesso da compressão e
  para a diferença TF–ambiente. Plausível, não testada.
- φ derivado de k: k foi inferido com fatores misturados (os ~5% sem explicação e o
  ambiente no ensaio rápido). φ ≈ 1/3 é ordem de grandeza.
- Vh/Ve pequeno (TF muito cheio): o gás chega ao TE mais frio (~3% no k com Vh/Ve = 5,
  ~6% com 2,5). O modelo supõe Vh/Ve ≥ ~10.
- O sensor de temperatura ambiente mede o ar do ambiente, não a parede do TE.
- kAir → kCO2 é aproximado (faixa 1,06–1,12).
- A discrepância de 29/09 (SG calculado alto em fase ativa) não é explicada e piora
  com k > 1.
- Interação com `liquidMassInGasVentingPercent` (0,6%), aplicado depois.
- A T_ref muda a contabilidade sempre que T_amb ≠ T_ferm: gravar na placa só entre lotes.

## 7. Perguntas para a revisão

1. O balanço de energia do TE (seção 2.1) e a forma `k = γ/(1 + q)` estão corretos?
2. Faz sentido derivar φ do k (seção 2.3) ou seria melhor um segundo parâmetro?
3. Há outra explicação física para os ~5% da rotina lenta (seção 3)?
4. A transposição kAir → kCO2 (seção 4.3) é razoável como estimativa inicial?
5. O método do volume de cerveja (seção 4.4) é robusto o bastante com o fator
   instantâneo de cada relief (CO2 saindo da cerveja logo após a queda de pressão)?
