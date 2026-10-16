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
- Mede por 5 min (campo da página, 1–15 min) uma amostra por segundo, só de
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

## Ganho

A conversão é uma reta pelos pontos A (0 bar) e B (página Calibration). O antigo ponto C (curva
quadrática) e a conferência de ganho da seção foram retirados; a corrente filtrada e a pressão sem
corte continuam no `/getstatus` para comparar com o manômetro analógico. A duração da rotina é fixa
em 5 min (os critérios de estabilidade foram pensados para ela).

## Eventos de calibração

`src/CalibrationEvents.cpp`: uma linha por evento em `/calevents.txt` na
partição `persist` ("hora local;tipo;detalhes"); acima de 16 KB a metade mais
antiga sai. A página Calibration mostra os 10 últimos; `/calibration/events.txt`
baixa o arquivo. A tag `te-final` não lê esse arquivo.
