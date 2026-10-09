# Conditioning

No modo Conditioning o fermentador vira uma geladeira simples: só a temperatura é controlada.

## Entrada

Pela tela de toque (Batch info → "Begin Conditioning") ou pela página Set Points (modo Conditioning). Os dois caminhos chamam `enterConditioning()` (`src/PovotoData.cpp`):

- cancela uma task aberta, sem o processamento de fim de task;
- pressure target = 0 e rampa de pressão desligada (na página Set Points, depois de ler os campos do formulário, então o 0 prevalece);
- grava os contadores (o balanço de CO2 fica como está);
- pede o registro final: uma última linha Cold e um último ponto no Brewfather.

## O que continua

| Item | Comportamento |
|---|---|
| Temperatura (frio, calor, rampa, ciclos) | igual ao Fermenting |
| Relief de segurança (`maximumPressure`, com alarme sonoro) | ativo; a cerveja esquentando libera CO2 e o tanque está fechado |
| Leitura de pressão, tela, páginas, `/getstatus` | ativas |
| Estabilidade de temperatura | continua sendo acompanhada (as regras automáticas não rodam) |

## O que para

| Item | Onde |
|---|---|
| Relief no setpoint (gas-flow e o relief simples) | `gasFlowMode()` só em Fermenting; o relief simples exclui Conditioning |
| Balanço de CO2: ejetado, headspace, dissolvido, SG, ABV | `processPressure()` só recalcula SG/ABV a partir dos contadores congelados |
| Medição do headspace (EMA e média de 24 h), captura de Recovery | idem |
| Estados do CO2 dissolvido (transições, expiração do armado) | `updateCO2DissolvedStateFromEvents()` retorna |
| gCO2/L/d informado | 0 |
| Logs Cold, Relief, Recovery | bloqueados, exceto a linha Cold final (o log Task registra a tarefa cancelada na entrada) |
| Brewfather | bloqueado, exceto o ponto final (sem `bpm`, porque a taxa é 0) |
| Regras automáticas | já só rodavam em Fermenting |
| Tasks | bloqueadas (página Tasks mostra o aviso; o toque no lado esquerdo não abre o menu) |

O registro final fica só na RAM: um reboot antes do envio o descarta. A linha Cold sai no próximo intervalo de log; o ponto do Brewfather sai logo, sem esperar os 10 min.

## Volta para Fermenting

Pela página Set Points. O lote **não** é zerado (`resumeFermentingFromConditioning()`):
- o CO2 dissolvido em equilíbrio passa para half-life (a taxa o devolve ao equilíbrio se a fermentação recomeçar);
- as referências recomeçam: buffer do gCO2, base do CO2 produzido, baseline do gás. O que mudou durante o Conditioning (temperatura, relief de segurança) não entra como produção nem como dívida;
- o pressure target continua 0 até ser definido de novo.

O lote só é zerado ao entrar em Fermenting vindo de Off ou de Brewing/Transfering, ou pelo fim da transferência do BrewCore. Atenção: Conditioning → Off → Fermenting zera o lote.

## Limitações

- Mudanças de volume sem task (amostras pela torneira) não são registradas.
- Com a pressão caindo no frio (reabsorção do CO2), o Povoto não tem como compensar: não injeta CO2.
