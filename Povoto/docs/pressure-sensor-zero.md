# Zero do sensor de pressão

Código: `src/SensorZero.cpp`; página Calibration, aba **Pressure**, seção "Sensor zero".
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
  amplitude das médias de 10 s > 3 mbar, |tendência| > 2 mbar ou mais de 5%
  das leituras descartadas. A amplitude é das médias de 10 s, não das leituras:
  com o ruído de uma leitura (~0,5 mbar), a amplitude de 300 leituras passaria
  de 1 mbar sem nenhum problema. Os limites de amplitude e tendência (antes 1 e
  0,5 mbar) foram afrouxados após testes na bancada: a placa recém-aquecida
  mostrou deriva lenta repetível de ~1,3 mbar em 5 min, abaixo da precisão do
  sensor (~5-10 mbar), e gás fluindo ou fermentador fechado dão desvios maiores.
- Aquecimento (transmissor e INA226): a página orienta manter a placa ligada há pelo menos 30 min.
  O firmware não mede nem exige: quem calibra é responsável por isso (um reinício por OTA não
  esfria a placa, e o relógio de boot não saberia).
- A rotina registra a média da temperatura ambiente (a da compensação, abaixo; vazia se menos da metade das
  amostras a tiver). Com a compensação ativa, o zero gravado é levado a essa temperatura antes da diferença:
  a diferença mostrada é só o que a deriva térmica não explica.
- **"Save zero"**: `pressure0Current` passa a ser a corrente média e `pressure0Celsius` a temperatura média;
  `pressure1Current` se desloca pela mesma diferença (um desvio de zero do transmissor ou do shunt é aditivo:
  o ganho fica; `pressure1Celsius` não muda). Com a compensação ativa e sem temperatura no ensaio, não grava.
  Nada é gravado sem o botão;
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
  (desvio padrão ≤ 1 mbar, amplitude das médias de 10 s ≤ 3 mbar, |tendência| ≤ 2 mbar ao longo
  dos 2 min, no máximo 5% das leituras descartadas, em mbar pela inclinação da reta gravada). Placa
  ligada há pelo menos 30 min: orientação na página, sem bloqueio.
- Resultado: corrente média, desvio, amplitude, tendência, a pressão que a reta gravada lê nessa
  corrente contra a referência (mbar) e a diferença da corrente em relação ao ponto B gravado.
- **"Save point B"**: a pressão de referência, a corrente média e a temperatura ambiente média passam a ser
  `pressure1Bar`, `pressure1Current` e `pressure1Celsius` (exige corrente acima do ponto A e referência acima de 0,5 bar e abaixo da pressão
  de segurança). Nada é gravado sem o botão; o mesmo resultado não é gravado duas vezes; evento
  `sensor-point-b`. Faça o zero antes: o Save zero desloca os dois pontos.
- A incerteza do ganho passa a ser a do manômetro de referência. Um ganho constante também é absorvido
  pela curva do ensaio de ventilação e pela SG.

## Compensação da deriva térmica do zero

Última seção da aba Pressure. O zero do sensor deriva com a temperatura ambiente (FMT1, 27/10: −7,5 mbar/°C,
desvio dos resíduos 0,8 mbar, sem atraso mensurável: deslocar ou filtrar a temperatura só piora o ajuste).
O ganho não é compensado.

- Campos: temperatura de referência dos pontos A e B (`pressure0Celsius`, `pressure1Celsius`; vazio =
  desconhecida), na terceira coluna ao lado da corrente, e a deriva `pressureDriftSlope` (mbar/°C, 0 = sem
  compensação). Os três são editáveis; as rotinas de zero e de ponto B gravam as temperaturas, e o botão
  "Use test slope" copia a inclinação do ensaio de deriva (`docs/zero-drift.md`) para o campo (grava com Save).
- Conversão (`convertCurrentToPressureAt`): cada ponto é levado da sua temperatura de referência à
  temperatura ambiente T, deslocando a corrente em `s / 1000 × (I_B − I_A) / P_B × (T − T_ponto)` mA; a
  reta passa pelos dois pontos deslocados. Com os pontos em temperaturas diferentes o ganho sai dos dois
  levados à mesma temperatura.
- Ativa só com a deriva diferente de 0 e as duas temperaturas de referência. O Save recusa deriva sem as duas.
- T é a temperatura ambiente (Dallas, SideKick ou BrewCore); a última válida vale por 10 min e depois disso a
  conversão usa a reta gravada (sem compensação) e a página avisa.
- O alarme do sensor (faixa de corrente) usa a reta gravada: a margem de 1,5 mA cobre a deriva.
- "Use test slope" só aparece com o ensaio parado, faixa de temperatura ≥ 2 °C, R² ≥ 0,8 e |inclinação| ≥ 3
  erros-padrão. O erro-padrão supõe resíduos independentes, e as amostras de 1 min são correlacionadas: a
  faixa e o R² são os critérios que pesam.
- Mudanças digitadas entram nos eventos (`drift-compensation-edited`).

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
