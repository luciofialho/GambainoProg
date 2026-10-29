# Vigilancia externa do Povoto

O SideKick monitora o progresso do loop principal do Povoto por ESP-NOW.
O sinal de vida sai a cada 60 segundos, em todos os modos. Apenas Fermenting
(2) e Conditioning (3) armam a vigilancia. Apos 300 segundos sem um novo
sinal valido, o SideKick abre um incidente, solicita e-mail e sinaliza o
Led2 do BrewCore pelo cabo Serial2. Nao ha reset automatico nem mudanca
de controle, setpoints, pressao, temperatura, CO2 ou SG.

## Funcionamento

- Mudancas de modo, lote ou numero provocam envio no proximo ciclo principal,
  sem aguardar o minuto. Sem confirmacao, ha nova tentativa a cada 5 segundos.
- O pacote `J` contem `1,numero,modo,lote,boot,sequencia,desafio`. Tem menos de
  100 bytes, cabe em um unico fragmento e nao depende de NTP, HTTP ou cloud.
- O SideKick aceita apenas MACs presentes em `peerPovotos`. O numero informado
  vai de 0 a 99; a identidade da vigilancia e o MAC, em ate MAXFMTS (10) slots.
  A descoberta automatica existente aceita indices de 1 a 10. Para numeros
  fora dessa faixa, cadastrar manualmente o MAC do Povoto em um slot de peers
  do SideKick; o protocolo de saude preserva o numero real informado.
- O primeiro contato negocia um desafio aleatorio do SideKick. O pacote `K`
  devolve os campos recebidos e o desafio. A primeira resposta com um desafio
  novo nao confirma o estado: o Povoto precisa ecoa-lo em novo sinal de vida.
- O Povoto incrementa um contador de boot persistente uma vez por inicializacao.
  O SideKick rejeita boots anteriores, sequencias repetidas ou fora de ordem,
  versoes desconhecidas e campos invalidos. Frames na fila de recepcao por
  mais de 5 segundos nao contam como sinal de vida.
- A confirmacao do estado so sai depois de persistir uma mudanca relevante.
  `/getstatus` do Povoto mostra `state confirmed`, `state confirmation pending`
  ou `unavailable`. Antes de desligar deliberadamente um Povoto, selecionar Off
  e aguardar a confirmacao. Se a mudanca nao chegar, continua valendo o ultimo
  modo conhecido pelo SideKick.
- Um incidente so fecha apos pelo menos 60 segundos de progresso de sinais
  validos, sem intervalo maior que 90 segundos entre eles, ou imediatamente
  apos um modo confirmado fora de Fermenting/Conditioning.

## E-mails

O envio usa a configuracao existente da biblioteca IOTK_SimpleMail. A tarefa
LogSend executa SMTP e os envios HTTPS em sequencia, sem TLS simultaneo.
O loop de recepcao e vigilancia continua independente das operacoes de rede.

O primeiro alerta fica elegivel aos 5 minutos. A entrega depende da conclusao
da operacao de rede em andamento, de Wi-Fi/internet, memoria e servidor SMTP.
Falhas sao tentadas novamente em intervalos de pelo menos 60 segundos;
apos sucesso, ha lembretes a cada 15 minutos enquanto persistir o incidente.
Uma mensagem de encerramento e enviada se houve alerta aceito pelo SMTP.
O estado e revalidado antes de iniciar SMTP; uma transacao ja iniciada nao pode
ser cancelada, portanto uma recuperacao durante o envio gera o encerramento
subsequente. Aceitacao pelo SMTP nao comprova entrega na caixa de entrada.

As mensagens identificam numero, lote, ultimo modo, idade do ultimo sinal e
horario local do SideKick quando disponivel. Perda de comunicacao pode indicar
congelamento, falta de energia ou falha de rede; nao diagnostica a causa.

## BrewCore e LED

Usa o envelope Serial2 existente `RCCOMMANDPACKET`, protegido por CRC, com
verbos distintos dos comandos RF:

- `HEALTH1,sessao,sequencia,mascara`: estado completo dos slots em incidente.
- `HEALTHACK1,sessao,sequencia`: confirmacao apos persistencia no BrewCore.

O estado e enviado imediatamente quando muda, a cada minuto quando confirmado,
ou a cada 5 segundos sem confirmacao. O Led2 fica aceso enquanto houver ao menos
um incidente. Os pulsos dos avisos locais e do RF continuam atendidos por um
arbitro unico; encerrar um incidente nao apaga outras causas. Nao adiciona som.
O comando nao opera bombas, valvulas ou programas. A alimentacao de 12 V segue
as regras existentes: e necessario verificar fisicamente a luz em Standby.

O BrewCore preserva um alerta recebido mesmo se o cabo ou o SideKick falhar,
e tambem depois de reiniciar. O status indica comunicacao desatualizada apos
5 minutos. A mascara usa slots (bit 0 = slot 0); a pagina do SideKick mostra
o numero do Povoto associado a cada slot.

## Persistencia e reinicios

- Povoto: namespace NVS `pvt_health`, chave `boot` (uint32).
- SideKick: `sk_health`, chaves `p0` a `p9`, snapshots versionados com magic
  `0x48573101`. Guardam MAC, numero, modo, lote, boot e estado do incidente/e-mail.
- BrewCore: `pvt_attention`, chave `mask` (uint32).

Nao se grava cada heartbeat. O SideKick restaura os equipamentos conhecidos e
concede 5 minutos a partir de sua inicializacao antes de novos e-mails. Um alerta
visual ja aberto permanece. Horarios monotônicos anteriores ao reinicio nao sao
reutilizados; o diagnostico distingue espera por estado vivo. Equipamentos sem
estado conhecido aparecem como `NOT monitored`, sem falsa garantia de vigilancia.

Apagar toda a NVS do Povoto reinicia seu contador de boot. Isso exige reconciliar
explicitamente o registro correspondente do SideKick antes de retomar o uso;
um contador menor e rejeitado intencionalmente. Remover um peer nao desarma um
incidente existente. Para desativar normalmente, confirmar Off antes de remover
o equipamento. Firmware antigo sem esse protocolo nao recebe vigilancia por
inferencias a partir dos logs.

## Verificacao

`tests/health_watch_test.hpp` exercita a maquina de estados e o parser reais por
static_assert em C++14. Pode ser validado sem placa, inclusive com o compilador
Xtensa instalado pelo PlatformIO:

```powershell
& "$env:USERPROFILE/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe" -std=gnu++14 -fsyntax-only -x c++ -I ../Common/GambainoCommon tests/health_watch_test.hpp
python tests/check_status_sizes.py
python tests/check_log_sizes.py
python tests/check_storage_schema.py
python tests/check_eol.py
```

Compilar Povoto, SideKick e BrewCore com PlatformIO. Upload requer autorizacao
explicita: atualizar primeiro os receptores (BrewCore e SideKick), depois Povoto.
Em bancada, confirmar pareamento e os tres status; interromper por menos de
5 minutos (sem alerta), depois mais de 5 (LED/e-mail); verificar repeticao aos
15 minutos, recuperacao e Off com aviso inicialmente perdido. Repetir com dois
Povotos, SideKick reiniciado, cabo Serial2 desconectado, RF durante o alerta e
internet/SMTP indisponivel. Verificar luz em Standby e memoria/stack durante SMTP.
