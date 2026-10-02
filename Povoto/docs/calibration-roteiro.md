# Roteiro de calibração dos testes de velocidade e do k

Objetivo: medir com ar, no fermentador vazio, o escoamento da válvula (a, b), o
aquecimento do tanque de expansão (k), o "piso" e a curva de ventilação, e
descobrir a causa do piso (sensor ou volume). Fundamentos:
`Povoto/docs/calibration-speed-evaluation.md` e `Povoto/docs/expansion-tank-k.md`.

Tempo total: ~2 h (a maior parte é o equipamento rodando sozinho).

**Não aperte os botões "Save ..." da página Calibration antes de eu analisar os
dados.** Os resultados ficam na página até reiniciar a placa; o que vale é o CSV.

## 1. Preparação

1. Grave o firmware novo na placa (entre lotes).
2. Confira na página **Settings**: FMT volume **129,6 L**, Relief volume
   **2,01 L**, k CO2 e k air (padrão 1,08).
3. Confira na página **Calibration**: "Flow factor CO2/air" = **0,79**.
4. Fermentador **vazio, limpo e seco**, fechado, modo **OFF**, sem tarefa ativa.
5. Confira se a temperatura ambiente aparece no status (o CSV a registra).
6. Anote: data, hora de início, temperatura ambiente, se há sol ou vento sobre o
   equipamento, e a leitura do seu manômetro no início de cada ensaio.

Em todos os ensaios: pressurize com ar, **espere ~10 min** para a temperatura do
gás assentar e só então aperte o botão. Não mexa no equipamento durante o ensaio.

## 2. Ensaios

| # | Ensaio | Pressão inicial | Botão (Calibration → Gas transfer speed) | Duração | Baixar |
|---|---|---|---|---|---|
| 1 | Expansão | ≥ 1,9 bar (ideal ~2,0) | Gas = Air, **Expansion** | ~40 min | Expansion CSV |
| 2 | k & piso, pressão alta | ≥ 1,9 bar (repressurize) | Gas = Air, **k & floor** | ~15 min | k & floor CSV |
| 3 | k & piso, pressão baixa | **~0,5 bar** (alivie até ~0,5 e espere 10 min) | Gas = Air, **k & floor** | ~15 min | k & floor CSV |
| 4 | Volume rápida | ≥ 1,9 bar (repressurize) | Gas = Air, **Volume determination (fast)** | ~1 h | Pressure history |
| 5 | Ventilação (opcional) | ≥ 1,9 bar, **saída da válvula ligada à atmosfera** | Gas = Air, **Venting** | ~10 min | Venting CSV |

Notas:

- O ensaio 3 é o que decide a causa do piso: se ele crescer ~4× em relação ao
  ensaio 2 (de ~1,9 para ~0,5 bar), a causa é o sensor; se ficar igual, é volume.
- Baixe o CSV de cada ensaio **antes** de começar o próximo do mesmo tipo (o 3
  substitui o 2 na memória da placa).
- O ensaio 5 só se a montagem com a saída para a atmosfera for prática; a
  ventilação pesa pouco na contabilidade.
- Depois de cada ensaio, copie (ou fotografe) o bloco de resultados que aparece
  na página Calibration.

## 3. Nomes dos arquivos

Salve em `A:\Nuvem\Povoto\Calibracoes` com a data na frente:

- `AAAA-MM-DD_1_expansao.csv`
- `AAAA-MM-DD_2_kpiso_alta.csv`
- `AAAA-MM-DD_3_kpiso_baixa.csv`
- `AAAA-MM-DD_4_volume_rapida.csv`
- `AAAA-MM-DD_5_ventilacao.csv` (se feito)
- `AAAA-MM-DD_notas.txt`: suas anotações e os blocos de resultado da página.

## 4. O que me passar

Abra uma conversa no Claude Code nesta pasta do projeto e diga:
*"fiz os ensaios do roteiro de calibração; arquivos em Calibracoes com a data
AAAA-MM-DD"*. Eu leio a memória e a documentação e analiso.

## 5. O que eu vou verificar e o que pode vir depois

| Verificação | Critério | Consequência |
|---|---|---|
| Piso no ensaio 3 contra o 2 | ∝ 1/P ou constante | sensor → recalibrar o sensor na faixa de trabalho (fase C); constante → rever volumes |
| Ajuste do ensaio 1 | RMSE ≲ 0,02; espera adaptativa terminando antes dos 180 s | aceitar a, b (CO2) e o k do aquecimento |
| k do aquecimento: ensaio 1 contra ensaio 2 | diferença ≲ 0,01 | gravar kAir (botão) |
| Volume rápida (ensaio 4) | com o kAir novo e o piso explicado, ~129,6 L | validação cruzada |
| Pressões aos 15/30/60 s | quanto tempo o fermentador leva para estabilizar | ajustar a espera mínima |
| Ventilação (ensaio 5) | RMSE da parábola | gravar c, d, e (CO2) |

Depois da análise eu indico quais botões apertar (ou faço a mudança da fase C).
O kCO2 continua 1,08 até a fase C; ele é refinado pelas colunas
`KCO2FromBeerVolume` (log Relief, se você pesar a cerveja transferida) e pelo SG
desgaseificado nas fermentações.
