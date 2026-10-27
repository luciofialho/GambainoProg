# Deriva térmica do zero do sensor de pressão

Aba **Drift** da página Calibration. Código: `src/ZeroDrift.cpp`.

## Para que serve

Com o fermentador aberto à atmosfera a pressão manométrica real é zero, então qualquer variação da leitura é do sensor
(e do laço de 4–20 mA). O ensaio mede essa leitura ao longo de um ou mais dias, com a temperatura variando de verdade,
e ajusta a leitura contra a temperatura. Na página, a temperatura do gás do fermentador aparece como "Tank" (no CSV é `T_gas_C`). O resultado decide se vale compensar a deriva no firmware e com que entrada.

Os ensaios de volume de 26/10 pediram, para chegar a 131,312 L, um erro de leitura de −6, +3, +13 e +35 mbar com a
temperatura ambiente em 20,9, 20,5, 19,6 e 16,8 °C (cerca de 9 mbar/°C), e os quatro pontos medidos em 27/10 deram
8 mbar/°C. Este ensaio troca esses pontos esparsos por um registro contínuo.

## Como usar

1. Abra o fermentador à atmosfera (a pressão real tem de ser zero).
2. Modo OFF, sem tarefa em andamento. Marque a confirmação e pressione **Start**.
3. Deixe rodar. Acompanhe na página o tempo registrado e o mínimo e o máximo das temperaturas; pare com **Stop** quando a
   faixa de temperatura for suficiente. Para sozinho quando a memória enche (7 dias).
4. Baixe o CSV. Os dados ficam só na PSRAM: não reinicie a placa nem faça OTA antes de baixar.

A válvula fica desenergizada. O protetor de tela pode entrar. Se o modo deixar de ser OFF ou uma tarefa começar, o ensaio
termina e guarda o que registrou.

## O que é registrado

Uma amostra por minuto, média das leituras de 1 s do intervalo:

| Coluna | Significado |
|---|---|
| date_time | Hora local da amostra |
| t_s | Segundos desde o início |
| T_amb_C | Temperatura ambiente (Dallas, SideKick ou BrewCore: a fonte usada pelo ensaio de volume) |
| T_gas_C | Temperatura do gás do fermentador (com o fermentador vazio é o ar; o termômetro do líquido, se não houver o do headspace) |
| T_chip_C | Temperatura do chip do ESP32, só para registro (aquecida pelo próprio chip) |
| I_mA, I_min_mA, I_max_mA | Corrente filtrada do INA226: média, mínimo e máximo das leituras do intervalo |
| P_bar | Leitura sem corte (negativa abaixo do zero calibrado), a partir da corrente média |
| p0_mA, pB_mA, pB_bar | A reta do sensor no início do ensaio |
| build | Versão do firmware |

## Ajustes mostrados na página

Mínimos quadrados da leitura (mbar) contra a temperatura (°C), duas vezes: contra a ambiente e contra a do gás.
Cada linha mostra: número de amostras, faixa de temperatura, **inclinação** (mbar/°C), seu erro-padrão, a temperatura em
que a reta cruza zero, R², o desvio-padrão dos resíduos e a variação da reta ao longo da faixa. O ajuste só aparece com
10 amostras e 0,5 °C de faixa.

Quanto menor o desvio dos resíduos e o erro da inclinação, melhor a entrada. Uma inclinação estável entre ensaios de
dias diferentes sustenta a compensação; inclinações diferentes sugerem que a variável escolhida é só um substituto da
temperatura que o sensor realmente sente.

## Limites

- A faixa de temperatura do ensaio é o que limita a conclusão: um dia comum varia 3–5 °C.
- A temperatura ambiente pode vir de outro equipamento (ESP-NOW); se faltar, a coluna fica vazia e o ajuste correspondente
  não aparece.
- O ensaio não corrige nada: só mede.
