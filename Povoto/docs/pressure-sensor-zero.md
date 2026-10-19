# Zero do sensor de pressão

Código: `src/SensorZero.cpp`; página Calibration, seção "Sensor zero".
Procedimento de operação: `docs/manual-calibracao-manutencao.md`, P1.

## Por que pela corrente

`convertCurrentToPressure()` corta em 0 qualquer corrente abaixo de
`FMTData.pressure0Current` (e qualquer pressão negativa). Ler "0,000 bar" com o
fermentador aberto não prova que o zero está certo: um zero gravado acima da
corrente real fica escondido. A rotina trabalha com a corrente filtrada (a
mediana de 9 amostras do INA226) e com `convertCurrentToPressureUnclipped()`, a
mesma curva de calibração sem o corte.

## Rotina

- Só em OFF, sem task nem outra rotina de calibração, com o INA226 presente.
- **Não abre válvula nenhuma**: o operador abre o fermentador para a atmosfera
  (de preferência uma abertura em cima e outra embaixo) e confirma na página.
- Mede por 5 min (fixo, sem opção) uma amostra por segundo, só de
  leituras novas; uma leitura 0 (o filtro sinaliza instabilidade assim) é
  descartada. Mudança de modo, task ou sensor aborta.
- Resultado: corrente média, desvio padrão, amplitude das médias de 10 s,
  tendência (inclinação dos mínimos quadrados × duração), zero gravado e a
  diferença em mA e em mbar, sem corte. A conversão para mbar usa a inclinação
  da calibração perto do zero, `pressure1Bar / (pressure1Current − pressure0Current)`.
- **"Unstable reading"** (sem botão de gravar): desvio padrão > 1 mbar,
  amplitude das médias de 10 s > 1 mbar, |tendência| > 0,5 mbar ou mais de 5%
  das leituras descartadas. A amplitude é das médias de 10 s, não das leituras:
  com o ruído de uma leitura (~0,5 mbar), a amplitude de 300 leituras passaria
  de 1 mbar sem nenhum problema.
- Placa ligada há menos de 30 min no início da medição (aquecimento do
  transmissor e do INA226): aviso, sem botão de gravar.
- **"Save zero"** desloca `pressure0Current` e `pressure1Current` pela mesma diferença (um desvio de zero do
  transmissor ou do shunt é aditivo: o ganho fica). Nada é gravado sem o botão;
  o mesmo resultado não é gravado duas vezes. A gravação entra nos eventos de
  calibração.

## Ponto B (ganho)

A conversão é uma reta pelos pontos A (0 bar) e B (página Calibration). A corrente filtrada e a
pressão sem corte ficam no `/getstatus`. O ponto B se mede com a rotina "Point B reading"
(`startSensorPointB`, mesmo motor do zero):

- Só em OFF, sem task nem outra rotina, com o INA226 presente e a calibração válida. Não abre válvula:
  o operador mantém um patamar estável de pressão (acima de 0,5 bar e abaixo da pressão de segurança)
  com o regulador e **espera 5 min** antes de iniciar, para a temperatura do gás assentar.
- O operador digita a pressão lida no manômetro digital de referência e confirma que está estável.
- Mede por 2 min (fixo): uma amostra por segundo, blocos de 10 s, os mesmos critérios do zero
  (desvio padrão ≤ 1 mbar, amplitude das médias de 10 s ≤ 1 mbar, |tendência| ≤ 0,5 mbar ao longo
  dos 2 min, no máximo 5% das leituras descartadas, em mbar pela inclinação da reta gravada). Placa
  ligada há menos de 30 min no início: aviso, sem botão de gravar.
- Resultado: corrente média, desvio, amplitude, tendência, a pressão que a reta gravada lê nessa
  corrente contra a referência (mbar) e a diferença da corrente em relação ao ponto B gravado.
- **"Save point B"**: a pressão de referência e a corrente média passam a ser `pressure1Bar` e
  `pressure1Current` (exige corrente acima do ponto A e referência acima de 0,5 bar e abaixo da pressão
  de segurança). Nada é gravado sem o botão; o mesmo resultado não é gravado duas vezes; evento
  `sensor-point-b`. Faça o zero antes: o Save zero desloca os dois pontos.
- A incerteza do ganho passa a ser a do manômetro de referência. Um ganho constante também é absorvido
  pela curva do ensaio de ventilação e pela SG.

## Casas decimais

Os campos de corrente (pontos A e B) e a pressão do ponto B mostram 3 casas (0,001 mA ≈ 0,125 mbar, perto
do LSB do INA226 de ~0,0008 mA); as rotinas mostram 4. O Save da página só altera um ponto que difere do
gravado por mais que o arredondamento, para não regravar arredondado o que o Sensor zero e o Point B
guardam com mais dígitos.

## Eventos de calibração

`src/CalibrationEvents.cpp`: uma linha por evento em `/calevents.txt` na
partição `persist` ("hora local;tipo;detalhes"); acima de 16 KB a metade mais
antiga sai. A página Calibration mostra os 10 últimos; `/calibration/events.txt`
baixa o arquivo. A tag `te-final` não lê esse arquivo.
