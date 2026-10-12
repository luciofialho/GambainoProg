# Alarmes de equipamento

`PovotoAlarms.cpp` (`processPovotoAlarms()` no loop) escolhe o alarme ativo por
precedência e cuida da sinalização. Cada verificação fica no seu módulo.

| Precedência | Alarme | Condição | Bipes |
|---|---|---|---|
| 1 | Sensor de pressão (`checkPressureSensor()`, PressureControl.cpp) | Fermenting ou Conditioning, alvo de pressão definido, leitura inválida há 10 s | 4 por minuto |
| 2 | Válvula presa aberta (`checkValveStuckOpen()`, PressureControl.cpp) | Fermenting: a pressão caiu mais de 5 mbar nos 30 s depois da leitura de fechamento de um vent | 6 por minuto |
| 3 | Vent sem efeito (`evaluateVentCheck()`, PressureControl.cpp) | Fermenting: dois vents seguidos com razão < 0,25 (lado do fermentador / vazão) | 5 por minuto |
| 4 | Termômetro do fermentador (`fermenterThermometerAlarm()`, TermperatureControl.cpp) | Fermenting ou Conditioning, alvo de temperatura definido, termômetro não atribuído ou sem leitura válida há 60 s | 3 por minuto |

Com mais de um ativo, sinaliza só o de maior precedência. O `/getstatus` mostra o
estado de cada um ("Pressure sensor check", "Vent check ... stuck open" e
"fermenter thermometer alarm").

## Verificação de cada vent

Cada vent é contado de dois jeitos (docs/vent.md): pelo modelo de vazão e pelo
lado do fermentador (queda assentada de P/Z, de `pressureOnReliefExtrap` à
pressão assentada pelo expoente 1,29, × headspace **aplicado** — média de 24 h,
referência congelada; antes dela, o inicial — / RT). A razão lado do
fermentador / vazão é a queda medida / esperada (= Vh de referência / Vh
aparente) e vai para o log em todo vent.

- **Razão < 0,5** (`partial`): a saída passou bem menos que a curva diz (saída
  parcialmente obstruída, por exemplo). O vent é contado pelo lado do
  fermentador e não alimenta a EMA, a média de 24 h nem as amostras iniciais.
  Sem alarme.
- **Vent sem efeito** (< 0,25, `no-effect`): a válvula não abriu, a saída está
  obstruída ou o TE foi religado por engano. Contado do mesmo jeito (~0 se a
  válvula não abriu). Dois seguidos disparam o alarme; um vent normal (≥ 0,5) o
  apaga.
- A verificação de queda grande do TE (vazamento entre a válvula e o tanque)
  saiu: sem TE não há causa no equipamento para ela.
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
dump baixa a pressão de propósito), com o alarme do sensor ativo nem com a
pressão de debug sobrescrita. Um vent seguinte com recuperação normal apaga o
alarme; sair de Fermenting também. E-mail com as pressões; tela "VALVE / OPEN".

## Leitura de pressão inválida

- O INA226 não responde: ausente no boot, ou `isConnected()` falha (sondado a
  cada 2 s), ou
- a corrente filtrada está fora de `[I(0 bar) - 1,5 mA, I(pressão máxima) + 1,5 mA]`.
  I(0 bar) é `pressure0Current`; I(pressão máxima) sai da calibração em linha
  reta pelos pontos 0-1 (ou 1-2 acima do ponto 1, se o ponto 2 estiver
  calibrado). Loop aberto (~0 mA), curto ou sensor com defeito caem fora.

Só detecta: o controle de pressão não muda.

## Modo debug

Os alarmes seguem só o valor considerado (o da página de debug, quando
sobrescrito), para poder testá-los na bancada:

- pressão: dispara com pressão negativa; qualquer outro valor não dispara (o
  INA226 e a corrente não contam);
- temperatura: dispara com 85 ou -127; qualquer outro valor não dispara
  (termômetro não atribuído não conta, já que o modo debug usa 18 °C fictícios).

As tolerâncias (10 s e 60 s) são as mesmas. Não bipa; tela, LEDs e e-mails
funcionam (o assunto do e-mail indica "debug mode").

## Sinalização do alarme ativo

- Buzzer: os bipes ao surgir e depois a cada minuto (250 ms ligado/desligado).
  No modo debug não bipa (placas de bancada); tela e LEDs sinalizam igual.
- Tela vermelha cheia com o problema em letras grandes. Teclado, tasks e batch
  info continuam abrindo por cima e, ao fechar, voltam a ela; o screensaver não
  apaga a tela.
- LED de venting, neopixel (vermelho) e attention LED piscando a 5 Hz. O neopixel
  deixa de mostrar o Wi-Fi enquanto isso.
- E-mail (`queuePovotoMail()`, mesmo destinatário das ações automáticas): ao
  surgir e a cada hora enquanto durar (nenhum ao resolver). Lote e nome da cerveja vão no assunto e no início do corpo (o Povoto não conhece o site da nuvem). Traz o problema, as leituras
  (corrente e faixa válida, ou ID e leitura do termômetro), há quanto tempo está
  ativo, modo e IP.
- Ao resolver: tela principal, LEDs apagados, neopixel volta ao Wi-Fi.

## Attention LED

LED do painel frontal, pino 13 (`PINATTENTIONLED`): 5 Hz com alarme ativo; 1 Hz
enquanto uma task estiver em andamento (do início ao fim; a janela de descanso
depois dela não conta); apagado no resto do tempo.
