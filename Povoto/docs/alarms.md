# Alarmes de equipamento

`PovotoAlarms.cpp` (`processPovotoAlarms()` no loop) escolhe o alarme ativo por
precedência e cuida da sinalização. Cada verificação fica no seu módulo.

| Precedência | Alarme | Condição | Bipes |
|---|---|---|---|
| 1 | Sensor de pressão (`checkPressureSensor()`, PressureControl.cpp) | Fermenting ou Conditioning, alvo de pressão definido, leitura inválida há 10 s | 4 por minuto |
| 2 | Termômetro do fermentador (`fermenterThermometerAlarm()`, TermperatureControl.cpp) | Fermenting ou Conditioning, alvo de temperatura definido, termômetro não atribuído ou sem leitura válida há 60 s | 3 por minuto |

Com os dois ativos, sinaliza só o de pressão. O `/getstatus` mostra o estado de
cada um ("Pressure sensor check" e "Fermenter thermometer alarm").

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
