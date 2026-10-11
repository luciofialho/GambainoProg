# Alarmes de equipamento

`PovotoAlarms.cpp` (`processPovotoAlarms()` no loop) escolhe o alarme ativo por
precedência e cuida da sinalização. Cada verificação fica no seu módulo.

| Precedência | Alarme | Condição | Bipes |
|---|---|---|---|
| 1 | Sensor de pressão (`checkPressureSensor()`, PressureControl.cpp) | Fermenting ou Conditioning, alvo de pressão definido, leitura inválida há 10 s | 4 por minuto |
| 2 | Relief (`evaluateReliefCheck()`, PressureControl.cpp) | Fermenting: dois reliefs seguidos com queda > 2× a esperada (vazamento) ou < 0,25× (relief sem efeito) | 5 por minuto |
| 3 | Termômetro do fermentador (`fermenterThermometerAlarm()`, TermperatureControl.cpp) | Fermenting ou Conditioning, alvo de temperatura definido, termômetro não atribuído ou sem leitura válida há 60 s | 3 por minuto |

Com mais de um ativo, sinaliza só o de maior precedência. O `/getstatus` mostra o
estado de cada um ("Pressure sensor check", "Relief check" e "Fermenter
thermometer alarm").

## Verificação de cada relief

A queda de cada relief (fator `adjustedEquilibriumPressure / pressureOnReliefExtrap`,
o mesmo da medição do headspace) é comparada com a esperada pelo headspace
**aplicado** (média de 24 h, referência congelada): queda esperada = Vr / (V + Vr),
com Vr o volume de contabilidade do tanque (k e Tref).

- **Vazamento** (> 2×): com a ligação válvula-tanque solta, o gás vai direto para a
  atmosfera pela válvula e a queda fica ~2,9× (a 1,9 bar) a ~3,8× (a 0,5 bar) a
  esperada com o tanque de 2 L (ensaio de ventilação de 18/09); por isso o limiar
  é 2× e não 4×.
- **Relief sem efeito** (< 0,25×): válvula que não abre ou tanque que não ventilou.
- **Quarentena:** o relief suspeito não alimenta a EMA, a média de 24 h nem as
  amostras iniciais; o CO2 dele vem do lado do fermentador (queda × headspace de
  referência / RT, aplicado via `tankPressureAtClose`). Dois seguidos do mesmo
  tipo disparam o alarme; um relief normal o apaga e volta a alimentar o
  headspace. Um suspeito isolado é descartado sem alarme.
- Não avalia: janelas de task, determinação de volume, primeiros 3 reliefs (sem
  headspace estabelecido), alarme do sensor de pressão ativo, pressão de debug
  sobrescrita, pressão no relief < 0,1 bar. Fora de Fermenting o alarme não vale.
- Log Relief: colunas `ReliefDropRatio` (medida/esperada) e `ReliefCheck` (ok,
  leak, no-effect ou vazio). Relief normal: nada muda no headspace nem no CO2.

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
