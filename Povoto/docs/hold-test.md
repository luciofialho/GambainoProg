# Teste estanque de longa duração (leak hold)

Código: `src/HoldTest.cpp`; página Calibration, aba **Leaks**. Complementa o teste de perda do
`docs/leak-hold-test.md` (aba Volume, 25 min, com a válvula aberta para o tanque, a única situação em que a
saída da solenoide está ligada a um tanque): aqui o fermentador fica fechado, a válvula fica desenergizada
(A para R) e a duração é a que o operador digitar, até 72 h. Com mais de um passo, o teste ventila o
fermentador entre os passos para medir a perda em várias pressões.

## O que faz

Segura a pressão pelo tempo previsto e registra a pressão, a temperatura do gás e a ambiente. A taxa de
perda vem da inclinação de `(P + Patm) / T` ao longo do tempo (a deriva de temperatura sai do cálculo).

Campos (o volume de gás é sempre o FMT volume configurado, só para converter a taxa em mL/min; ao testar apenas parte
do sistema, o mL/min fica alto demais e vale a perda de pressão):

| Campo | Uso |
|---|---|
| Duration (min) | 1 a 4320 (abaixo de 20 min por patamar o resultado não é confiável: serve para conferir a rotina) |
| Steps | 1 a 4 patamares de pressão (ver abaixo) |
| Label | texto livre (até 40 caracteres) que vai para o CSV e o evento |

O protetor de tela funciona durante este teste (as outras rotinas de calibração mantêm a tela acesa).

Só em OFF, sem task nem outra rotina, a partir de 0,3 bar. Pressurize, espere 15 min e aperte Start (o
navegador pede confirmação). Mudança de modo, task ou pressão acima da máxima aborta, e a válvula volta
desenergizada.

## Passos de pressão

Com n passos, o tempo total é dividido em n partes iguais. No fim de cada parte, menos a última, o
fermentador é ventilado pela porta A (que **deve estar aberta para a atmosfera, sem tanque**) para pousar na
**metade** da pressão (média de 10 s) em que estava, ou, se meiar n − 1 vezes levaria o último patamar abaixo de
0,3 bar, pelo fator mais brando que o deixa em 0,3 bar: `f = max(0,5; (0,3 / P inicial)^(1/(n−1)))`. Partindo de
2 bar, 3 patamares dão 2, 1 e 0,5 bar, e 4 patamares dão 2; 1,06; 0,56 e 0,30 bar. Se o fator ficasse acima de
0,9 (queda de menos de 10% por passo), o teste recusa a combinação. Não há ventilação depois do último patamar: o teste termina com a válvula desenergizada. Com
1 passo não há ventilação nenhuma (é o teste simples de antes).

- O tempo de abertura é o do modelo do teste de ventilação, `t = k · S` para ar, com o FMT volume, a
  temperatura do gás e o Effective Venting Exponent. Erra o alvo em alguns por cento; o que vale é a
  pressão efetiva de cada patamar, que sai na tabela.
- Cada patamar precisa de pelo menos 1 min (duração ≥ passos); abaixo de 20 min por patamar o assentamento
  descartado cai para um quarto do patamar e a taxa não é confiável (o navegador avisa na confirmação). O último
  patamar não fica abaixo de 0,3 bar pela regra acima. Se a ventilação não puder ser calculada, o evento
  `hold-test-vent` diz que não houve ventilação e o patamar continua na mesma pressão.
- O primeiro patamar é o único feito com a válvula parada desde a pressurização, sem nenhum acionamento
  antes: se o defeito intermitente da válvula voltar, aparece nele primeiro.

## Registro

- Tudo fica na PSRAM, até ~2.900 amostras (~70 KB): intervalo de 5 s até 10 min, 10 s até 6 h, 30 s até 24 h, 60 s até 48 h e 90 s até 72 h.
  Cada amostra é a média das leituras de 1 s do intervalo. Não há arquivo: uma reinicialização apaga o
  registro (baixe um CSV parcial de vez em quando nos testes longos).
- Temperatura do gás: o termômetro do headspace, se atribuído; senão o do fermentador (`gasTemperature()`).
- CSV `/calibration/holdtest.csv` (montado por partes, com o build): `label;step;t_s;t_step_s;P_bar;T_gas_C;
  T_env_C;patm_bar;build`. O resumo vai para o histórico de eventos de calibração (`hold-test`).

## Resultado

Para cada patamar, desde o assentamento até o fim do patamar: nº de amostras, pressão efetiva média,
mbar/min, K/min, %/min (da pressão absoluta, à temperatura constante), mL/min de gás à pressão atmosférica e
a perda %/min da primeira e da segunda metade do patamar (uma cauda térmica lenta aparece como diferença
entre elas). O assentamento descartado é de 2 min no primeiro patamar e de 10 min nos seguintes, qualquer que
seja a duração. Mais as janelas dos últimos 10 e 30 min do patamar atual. Com 3 ou 4 patamares que perdem
(> 0,01%/min), o expoente da perda contra a pressão manométrica (ajuste de ln(mL/min) contra ln(P)): um
orifício dá ~1–1,5.

Resolução: o teste de 600 s de 23/10 deu ±0,01%/min; patamares de horas devem dar ~0,001–0,002%/min (a
confirmar).

## Como usar para localizar

| Arranjo | O que mede |
|---|---|
| fermentador fechado + Povoto | o conjunto todo |
| fermentador isolado por um registro | só o conjunto acima do fermentador (olhe a perda de pressão: o mL/min usa o FMT volume e sai alto demais) |
| vários passos | o expoente da pressão (orifício: ~1–1,5) |

Em placa de bancada (modo debug) a rotina simula uma perda de 0,03%/min, limita o teste a 2 min e usa
amostras de 5 s.
