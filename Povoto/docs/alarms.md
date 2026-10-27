# Alarmes de equipamento

`PovotoAlarms.cpp` (`processPovotoAlarms()` no loop) escolhe o alarme ativo por
precedência e cuida da sinalização. Cada verificação fica no seu módulo.

| Precedência | Alarme | Condição | Bipes por toque (cadência na seção Sinalização) |
|---|---|---|---|
| 1 | Sensor de pressão (`checkPressureSensor()`, PressureControl.cpp) | Fermenting ou Conditioning, alvo de pressão definido, leitura inválida há 10 s | 4 |
| 2 | Válvula presa aberta (`checkValveStuckOpen()`, PressureControl.cpp) | Fermenting: a pressão caiu mais de 5 mbar nos 30 s depois da leitura de fechamento de um vent | 6 |
| 3 | Vent sem efeito (`evaluateVentCheck()`, PressureControl.cpp) | Fermenting: dois vents seguidos com razão < 0,8 (lado do fermentador / vazão; < 0,25 é rotulado `no-effect`) | 5 |
| 4 | Termômetro do fermentador (`fermenterThermometerAlarm()`, TermperatureControl.cpp) | Fermenting ou Conditioning, alvo de temperatura definido, termômetro não atribuído ou sem leitura válida há 2 min | 3 |

Com mais de um ativo, sinaliza só o de maior precedência. O `/getstatus` mostra o
estado de cada um ("Pressure sensor check", "Vent alarms: no effect ..., stuck
open ..." e "fermenter thermometer alarm"); os detalhes do vent estão em
`/ventstatus` (docs/vent.md).

## Verificação de cada vent

Cada vent é contado de dois jeitos (docs/vent.md): pelo modelo de vazão e pelo
lado do fermentador (queda assentada de P/Z, de `pressureOnReliefExtrap` à
pressão assentada pelo expoente 1,29, × headspace **aplicado** — média de 24 h,
referência congelada; antes dela, o inicial — / RT). A razão lado do
fermentador / vazão é a queda medida / esperada (= Vh de referência / Vh
aparente) e vai para o log em todo vent.

- **Razão < 0,8** (`partial`): a saída passou menos que a curva diz (saída
  parcialmente obstruída, por exemplo). O vent é contado pelo lado do
  fermentador e não alimenta a EMA, a média de 24 h nem as amostras iniciais.
  Um vent isolado só fica marcado no log, sem alarme; **dois seguidos** abaixo de
  0,8 disparam o alarme.
- **Alcance da verificação**: a referência é o Vh de 24 h, que vem do próprio
  modelo de vazão. A razão detecta só mudanças bruscas; a sujeira gradual aparece
  no `HeadSpaceDaily` subindo e no ensaio de fim de lote.
- **Vent sem efeito** (< 0,25, `no-effect`): a válvula não abriu ou a saída está
  obstruída. Contado do mesmo jeito (~0 se a válvula não abriu). Conta para os
  dois seguidos que disparam o alarme; um vent normal (≥ 0,8) o apaga.
- Não avalia: fora de Fermenting, janelas de task, alarme do sensor de pressão
  ativo, pressão de debug sobrescrita, pressão no vent < 0,1 bar, sem a
  referência da subida de pressão. Avalia desde o primeiro vent (o headspace
  inicial vem do volume de cerveja informado; com ar no headspace a vazão real
  é maior que a do CO2 e a razão fica perto de 1,27, longe de 0,25).
- Log Relief: `ReliefDropRatio` (lado do fermentador / vazão, em todo vent),
  `ReliefCheck` (ok, partial, no-effect ou vazio), `VentFlowMols`,
  `VentFermenterSideMols` e `VentCounted` (`flow` ou `fermenter-side`).

## Válvula presa aberta

Depois de um vent em Fermenting a pressão só sobe: o gás esquenta de volta
(~30 s) e a fermentação continua. Se 30 s depois da leitura de fechamento ela
estiver mais de 5 mbar abaixo dessa leitura, a válvula não fechou: alarme na
hora (o fermentador está perdendo a pressão). Não avalia em janelas de task (o
dump baixa a pressão de propósito), com o alarme do sensor ativo, com a pressão
de debug sobrescrita nem com a opção Open do Valve (página Control). O alarme
**apaga sozinho quando a pressão volta a subir**: com ele ativo, a cada 30 s se
compara a pressão com a de 30 s antes, e uma subida de mais de 5 mbar o apaga,
mesmo antes do vent seguinte. Um vent seguinte com recuperação normal também o
apaga; sair de Fermenting também. Pressão parada perto de zero com a válvula
aberta não apaga. E-mail com as pressões; tela "VALVE / OPEN".

## Leitura de pressão inválida

- O INA226 não responde: ausente no boot, ou `isConnected()` falha (sondado a
  cada 2 s), ou
- a corrente filtrada está fora de `[I(0 bar) - 1,5 mA, I(pressão máxima) + 1,5 mA]`.
  I(0 bar) é `pressure0Current`; I(pressão máxima) sai da calibração em linha
  reta pelos pontos A e B. Loop aberto (~0 mA), curto ou sensor com defeito caem fora.

Só detecta: o controle de pressão não muda.

## Modo debug

Os alarmes seguem só o valor considerado (o da página de debug, quando
sobrescrito), para poder testá-los na bancada:

- pressão: dispara com pressão negativa; qualquer outro valor não dispara (o
  INA226 e a corrente não contam);
- temperatura: dispara com 85 ou -127; qualquer outro valor não dispara
  (termômetro não atribuído não conta, já que o modo debug usa 18 °C fictícios).

As tolerâncias (10 s e 2 min) são as mesmas. Não bipa; tela, LEDs e e-mails
funcionam (o assunto do e-mail indica "debug mode").

## Sinalização do alarme ativo

- Buzzer: os bipes ao surgir (250 ms ligado/desligado) e depois, para todos os alarmes,
  **a cada minuto nos primeiros 10 min, a cada 15 min até completar 1 h e a cada hora
  depois**. No modo debug não bipa (placas de bancada); tela e LEDs sinalizam igual.
- Tela vermelha cheia com o problema em letras grandes. Teclado, tasks e batch
  info continuam abrindo por cima e, ao fechar, voltam a ela; o screensaver não
  apaga a tela.
- LED de venting, neopixel (vermelho) e attention LED piscando a 5 Hz. O neopixel
  deixa de mostrar o Wi-Fi enquanto isso.
- E-mail (`queuePovotoMail()`, mesmo destinatário das triggered actions): ao
  surgir e a cada hora enquanto durar (nenhum ao resolver). Lote e nome da cerveja vão no assunto e no início do corpo (o Povoto não conhece o site da nuvem). Traz o problema, as leituras
  (corrente e faixa válida, ou ID e leitura do termômetro), há quanto tempo está
  ativo, modo e IP.
- Ao resolver: tela principal, LEDs apagados, neopixel volta ao Wi-Fi.

## Attention LED

LED do painel frontal, pino 13 (`PINATTENTIONLED`): 5 Hz com alarme ativo; 1 Hz
enquanto uma task estiver em andamento (do início ao fim; a janela de descanso
depois dela não conta); apagado no resto do tempo.
