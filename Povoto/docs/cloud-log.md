# Log da nuvem (Fase 1)

Caminho: Povoto → ESP-NOW → SideKick → HTTPS → Worker na Cloudflare
(`cloud/`, ver `cloud/README.md`). Plano e decisões: `docs/cloud-plan.md`.

## Povoto (`src/CloudLog.cpp`)

- Um registro por janela de 5 min (`epoch / 300`), alinhado ao relógio: o
  horário do registro é o início da janela, e os valores são os do momento do
  envio (o primeiro `loop()` dentro da janela).
- Enviado ao `peerSideKick` com o pacote `CLOUDLOGPACKET` ('G'), em modo
  diferente de Off e com batch ≠ 0, Wi-Fi conectado (canal do ESP-NOW) e NTP
  válido. Se o `sendEspNow` falhar, tenta de novo a cada 5 s dentro da mesma
  janela. Não há confirmação de entrega: a perda no rádio aparece como
  lacuna na conferência (`cloud/scripts/check-phase1.mjs`).
- Depois de um reboot, a janela atual pode ser enviada de novo; a nuvem
  guarda só a primeira.
- Em modo debug, a janela é o intervalo do log Cold quando ele for menor que
  5 min (30 s a 2 min), para acelerar o teste na bancada.
- Os valores seguem as regras do histórico dos gráficos (`graphCapturePoint`,
  a mesma do `/graphs`): inválido = `null`, e gCO2/L/d só fora de transição.

Dois tipos de linha por janela:

- **Histórico** (`CLOUDLOGPACKET`, 'G'): vai para a fila do SideKick e vira
  uma linha por janela na nuvem (tabela `logs`).
- **Estado do batch** (`CLOUDSTATEPACKET`, 'H'): dados sem histórico (nome do
  batch e contadores). O SideKick guarda só a última versão de cada Povoto, em
  RAM, fora da fila; a nuvem guarda só a última de cada batch (colunas de
  `batches`, migration 0002), para apresentação. O estado mais novo vence
  (`e`); um estado perdido é substituído pelo da janela seguinte. Se o SideKick
  reiniciar sem internet, o estado de um batch que já terminou pode não chegar
  (risco aceito).

Histórico (uma linha JSON, até ~400 bytes):

| Chave | Conteúdo |
|---|---|
| `v` | versão do formato (1) |
| `p` | PovotoNum |
| `e` | epoch local do início da janela |
| `m` | modo (0 Off, 1 transferência, 2 Fermenting, 3 Conditioning) |
| `b` | número do batch (nome, data e OG vão no estado; firmware antigo ainda os manda aqui em `bn`, `bd`, `og`, e a nuvem aceita) |
| `t`, `ts`, `tsl` | temperatura, setpoint, slow target |
| `pr`, `ps`, `psl` | pressão, setpoint, slow target |
| `sg`, `abv`, `r`, `f` | SG, ABV, gCO2/L/d e flags dos gráficos (1 retido, 2 transição, 4 sintético) |
| `vol`, `co2`, `rph` | volume (L), g de CO2, reliefs por hora |
| `ss` | só no log sintético: dia 0 do perfil (epoch local) |

Estado do batch (uma linha JSON, até ~260 bytes):

| Chave | Conteúdo |
|---|---|
| `v`, `k` | versão (1) e `"s"` (marca a linha como estado) |
| `p`, `e`, `b` | PovotoNum, epoch local da janela, número do batch |
| `bn`, `bd`, `og` | nome, data e OG do batch |
| `ct`, `ht` | tempo de chiller e de heater ligados (s, acumulado) |
| `mh`, `md`, `me` | mol de CO2 no headspace, dissolvido e expelido (acumulado) |
| `nx` | expansões (`totalReliefCount`) |
| `dv` | volume descartado por tarefas Dump (L) |

## Log sintético (modo debug)

- Página `/debugparams`, seção "Cloud log": a opção "Synthetic log" fica na
  NVS (`pvt_cloud`: `synth`, `synthStart`) e só vale em modo debug.
- Ao ligar, o dia 0 do perfil é a janela atual. Os registros levam o perfil
  de 14 dias do demo dos gráficos (`graphSyntheticPoint()`, repetido a cada
  14 dias), OG 1,054 e a flag 4; volume, CO2, reliefs e slow targets
  continuam reais. Só o registro da nuvem muda: controle, log Cold, gráficos
  da placa e Brewfather seguem com as medidas.
- `cloud/scripts/synthetic.mjs` repete as fórmulas; a conferência recalcula
  cada valor a partir de `e` e `ss`. Mudou uma, mude a outra.

## SideKick (`src/Sidekick-cloud.cpp`)

- Cada registro recebido vai para o fim de `/cloud/spool.txt` no LittleFS
  (uma linha por registro; até 600 KB, ~1.300 registros, mais de um dia de 4
  Povotos). `/cloud/sent` guarda até onde já foi enviado; quando tudo foi
  enviado, os dois arquivos são apagados. A fila sobrevive a reboot e a queda
  de internet.
- A task LogSend (a cada 15 s) envia até 8 linhas (~3 KB) por POST para a URL
  configurada, com `Authorization: Bearer <token>`, corpo NDJSON. 2xx avança;
  400 pula o lote (um lote que a nuvem recusa travaria a fila); rede, 401 e
  5xx tentam de novo.
- TLS verificado com quatro raízes (`include/HttpsRootCAs.h`, copiadas do
  bundle do ESP-IDF): ISRG X1/X2 (Let's Encrypt; o workers.dev encadeava no
  X2 em outubro de 2026) e GTS R1/R4 (Google). O POST do Google Sheets usa a
  mesma lista: só com a GTS R1 ele falhava na verificação.
- Memória: cada conexão TLS precisa de ~50 KB, com dois buffers contíguos de
  ~16,7 KB, e o heap do SideKick é curto. Com a fila RAM do Google Sheets em
  20 posições de 2 KB, os handshakes falhavam por falta de memória e o
  próprio Wi-Fi caía durante eles. A fila foi reduzida para 8 posições, e os
  POSTs da nuvem são pequenos. `/getstatus` mostra o heap livre, o maior
  bloco e o mínimo desde o boot.
- Estado do batch: até 10 Povotos em RAM (400 bytes cada). Os estados mais
  novos que o último aceito vão no mesmo POST do histórico (uma conexão TLS
  só); um 2xx ou 400 os marca como enviados.
- Configuração na página "Connection settings" (link no `/getstatus`; URL e
  token, na NVS `sk_cloud`); contadores e o site do token (`/api/whoami`) em
  `/getstatus`. Apagar a URL desliga a nuvem (o token fica guardado).
- Nenhum destino vem no código: a URL do Google Sheets (Apps Script) e a do
  Brewfather também são campos dessa página (NVS `sidekick`: `gs_url`,
  `bf_url`); vazias, o envio para aquele destino fica desligado. Ao atualizar
  um SideKick de antes de outubro de 2026, preencha as duas.
- O LittleFS é montado com formatação se não montar: a partição do SideKick
  não guardava nada antes disso.

## Fase 2: set points e regras pela nuvem

O Povoto é o dono dos dados. A nuvem guarda uma cópia feita só de
snapshots enviados por ele e manda pedidos, que o Povoto aplica apenas se
foram feitos sobre a versão (hash) que ele ainda tem. Em caso de race, o
pedido da nuvem perde.

### Povoto (`src/CloudSync.cpp`)

- **Snapshots**, pela fila do SideKick (pacote `CLOUDLOGPACKET`, como o
  histórico): `{"v":1,"k":"sp","p","e","h","d":{t,ts,tv,p,ps,pv}}` para os
  set points e `{"v":1,"k":"r","p","e","h","i","d":{i,n,sh,ph,sg,co,t,tsl,p,psl,rq,mo,at}}`
  para cada regra (`at`: horário do disparo, 0 = não disparou). `h` é o CRC32
  (8 hex) do texto de `d` (set points) ou dos 8 textos de regra em ordem,
  cada um seguido de `\n` (regras). Enquanto uma rampa corre, o valor direto
  daquela grandeza fica fora (`null`): ele anda a cada passo.
- O hash é recalculado a cada 2 s; mudou (página local, TFT, regra
  automática, pedido da nuvem), o snapshot sai, uma linha a cada 300 ms (a
  fila ESP-NOW do SideKick tem 16 quadros).
- A linha de estado (5 min) leva `hs` e `hr` (hashes) e `ae` (aceita
  edições). Com a cópia diferente, a nuvem pede `snap` e o Povoto reenvia
  tudo.
- **Pedidos** (pacote `CLOUDCMDPACKET` 'Q', do SideKick):
  `{"k":"cmd","p","id","t","h","d"}`, com `t` = `sp`, `rule`, `reset`,
  `trigger` ou `snap`. Aceitos só do MAC do `peerSideKick` (os demais são
  descartados e contados); enfileirados no callback do rádio e aplicados no
  `loop()`.
- Aplicação: chave ligada, `h` igual ao hash atual e as mesmas validações
  da página e das regras (temperatura 0-42 °C, pressão 0-2 bar, rampas 1-8
  °C/dia e 0,1-2 bar/dia; regra disparada só aceita Reset). Set points com
  a semântica da página de set points.
- Resposta `{"v":1,"k":"ack","p","id","ok","m"}` pela fila, repetida uma vez;
  um pedido repetido (mesmo `id`) recebe a mesma resposta, sem reaplicar.
- **Accept cloud edits**: página Settings, abaixo do Log interval (NVS
  `pvt_cloud`: `edits`, ligada por padrão). Desligada, todo pedido que não
  seja `snap` é recusado.

### SideKick

- Sem nada a enviar, posta mesmo assim a cada 60 s (POLL_MS): a resposta do
  `/api/ingest` traz, depois da linha de resumo, uma linha por pedido
  pendente do site. Uma conexão TLS por minuto, no máximo.
- A task LogSend enfileira os pedidos (3 posições); o `loop()` manda cada um
  ao Povoto do `"p"` (logo depois de `"k"`), pelo MAC de `peerPovotos`.
  Contadores em `/getstatus` (recebidos, repassados, não entregues).

### Nuvem (`cloud/`, migração 0003)

- Tabelas: `setpoint_snapshots`, `rule_snapshots` (a mais nova vence),
  `povoto_sync` (hashes e chave da última linha de estado) e `requests`.
- Página `/p/<id>/setpoint` (também pelo toque nos targets do dashboard):
  cópia dos set points e das regras, formulários (papel `edit`), últimos
  pedidos. Só edita com a chave ligada no Povoto, a cópia em dia e nenhum
  outro pedido esperando; o formulário leva o hash da cópia mostrada.
- Prazos (`REQUEST_TIMEOUT_SECONDS`, 180 s): pedido não buscado pelo
  SideKick expira; buscado e sem resposta fica "unconfirmed" (uma resposta
  tardia ainda atualiza).
