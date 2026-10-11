# Termômetros Dallas

Vários DS18B20 no mesmo barramento (pino `PINDALLAS`), cada um com um papel.
Código: `DallasSensors.cpp`; resolução das temperaturas em
`updateEnvironmentTemperatures()` (`TermperatureControl.cpp`).

| Papel | Com sensor atribuído | Sem sensor atribuído |
|---|---|---|
| Fermentador (`dallasTemperature`) | o sensor atribuído | sem leitura: controle de temperatura em IDLE |
| Ambiente (`environmentTemp`) | o sensor atribuído | o valor recebido por ESP-NOW (pacote `'V'`, expira em 10 min): o do SideKick (Dallas próprio, opcional) antes do do BrewCore |
| Tanque de expansão (`expansionTankTemp`) | o sensor atribuído | o mesmo valor do ambiente |
| Headspace (`gasTemperature()`) | o sensor atribuído, enquanto tiver leitura válida (60 s); depois, o do fermentador | o do fermentador |

- Com sensor atribuído não há fallback: se ele parar de responder, a última
  leitura válida vale por 60 s (a mesma tolerância do fermentador) e depois o
  valor passa a inválido (`NOTaTEMP`). O ambiente não volta ao ESP-NOW.
- Nada é escolhido automaticamente, nem na atualização a partir do firmware de
  um sensor só: todo papel é atribuído na página. Se o termômetro do fermentador
  falhar, outro sensor (o do ambiente, por exemplo) nunca assume o controle, o
  que poderia congelar a cerveja.
- Alarme: em Fermenting ou Conditioning, com alvo de temperatura definido, sem
  termômetro do fermentador (não atribuído, ou sem leitura válida há 60 s) o
  controle fica parado e o Povoto alarma (3 bipes por minuto, tela vermelha,
  LEDs; `docs/alarms.md`). Vale também no modo debug (os 18 °C fictícios
  esconderiam o problema); não toca com a temperatura sobrescrita na página de
  debug.
- O remetente do `'V'` é reconhecido pelo MAC do peer SideKick; qualquer outro
  é tratado como BrewCore. O `/getstatus` mostra a fonte em uso (`Dallas`,
  `SideKick`, `BrewCore` ou `none received`). docs/atmospheric-pressure.md.
- Sem ambiente válido (nada recebido há mais de 10 min e sem Dallas de
  ambiente), `environmentTemp` e `expansionTankTemp` ficam inválidos: os logs
  ficam vazios e o modelo do tanque usa Tref = Tferm (`docs/expansion-tank-k.md`).
- `expansionTankTemp` é o "Tamb" do modelo do tanque de expansão
  (`docs/expansion-tank-k.md`): Tref, k, volume da rotina de volume e
  kCO2FromBeerVolume. `environmentTemp` continua nos logs
  (EnvironmentTemperature), no Brewfather (`ext_temp`) e no teste de velocidade.
- **Headspace** (opcional; por ora só no FMT1: sonda de 15 cm em poço longo pela
  porta tri-clamp, ponta no gás, sem tocar o líquido nem o krausen alto): a
  temperatura do gás, `gasTemperature()`. Sem chave de configuração: atribuído e
  com leitura válida, vale ele; sem atribuição ou sem leitura válida por 60 s
  (a tolerância dos outros papéis), vale a do fermentador, sem alarme. Nunca
  assume o controle de temperatura. Henry e o CO2 dissolvido continuam com a
  temperatura do líquido. Origem: `gasTemperatureSource()`, `headspace` ou
  `liquid`. Uma sonda curta perto da tampa erra por condução pela haste
  (~10–20% da diferença ambiente–gás com ~10 cm de imersão): conferir puxando a
  sonda 3–4 cm num dia com o ambiente bem diferente da cerveja e isolar a
  conexão por fora. Log Cold: coluna `HeadspaceTemperature` (vazia sem sensor
  ou sem leitura). `/getstatus`: o papel, a leitura e "Gas temperature: ...
  (headspace / liquid)".
- Logs: coluna `ExpansionTankTemperature` no fim do Cold e do Relief. O CSV da
  rotina de volume trocou `temperatura_ambiente` por
  `temperatura_tanque_expansao` (é a temperatura usada no cálculo).

## Configuração

Página Settings (`/fmtdata`), seção "Thermometers (Dallas)": tabela com os IDs
(ROM, `XX-XX-XX-XX-XX-XX-XX-XX`) encontrados na última varredura, a leitura de cada um e o
papel atual, e um seletor por papel. Um sensor atribuído que não está no
barramento aparece como "not found" e continua selecionável, para que salvar a
página não apague a atribuição. O mesmo sensor não pode ter dois papéis.

Para identificar um sensor: aqueça-o com a mão e use "Rescan bus".

A varredura roda no loop principal (no boot e pelo botão "Rescan bus"), nunca no
handler web, para não disputar o OneWire com `processDallas()`. Em modo
parasita, a varredura no meio de uma conversão pode estragar uma leitura (85 °C
ou inválida), que as regras de validade descartam.

O `/getstatus` mostra cada papel (ID, OK / NO READING, leitura, se apareceu na
última varredura) e os sensores da varredura sem papel.

## Persistência

`FMTData.dallasAddress[4][8]` (índices `DALLAS_ROLE_*`), em `pvt_settings`, um
blob de 8 bytes por papel: `dallasFerm` (fermentador), `dallasEnv` (ambiente),
`dallasExpTank` (tanque de expansão), `dallasHeadspace` (headspace); zeros = não
atribuído. Chave ausente ou de
outro tamanho = não atribuído: incluir um papel novo é acrescentar uma chave, e
retirar um papel deixa a chave dele sem uso, sem afetar os demais. No `povoto-settings.json`:
`dallasFermenter`, `dallasEnvironment`, `dallasExpansionTank`, `dallasHeadspace`
(`XX-XX-XX-XX-XX-XX-XX-XX`, "" = não atribuído; aceita também 16 dígitos hex sem
hífen). Os endereços são do hardware: ao carregar o backup em outra placa,
os papéis apontam para sensores ausentes até serem reatribuídos.

## IOTK_Dallas

A biblioteca não foi alterada. Ela guarda um ponteiro para o endereço de cada
termômetro registrado e o lê a cada segundo; `DallasSensors.cpp` registra um
buffer fixo por papel e troca o conteúdo dele (sempre no loop principal). O slot
do fermentador é sempre registrado; os outros, na primeira vez que o papel
recebe um endereço (até lá não há tráfego extra no barramento). A biblioteca não
permite remover um registro: um papel desatribuído fica com endereço zero e lê
`DEVICE_DISCONNECTED_C`.
