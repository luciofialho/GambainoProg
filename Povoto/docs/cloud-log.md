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

Formato (uma linha JSON, até ~450 bytes):

| Chave | Conteúdo |
|---|---|
| `v` | versão do formato (1) |
| `p` | PovotoNum |
| `e` | epoch local do início da janela |
| `m` | modo (0 Off, 1 transferência, 2 Fermenting, 3 Conditioning) |
| `b`, `bn`, `bd`, `og` | número, nome, data e OG do batch |
| `t`, `ts`, `tsl` | temperatura, setpoint, slow target |
| `pr`, `ps`, `psl` | pressão, setpoint, slow target |
| `sg`, `abv`, `r`, `f` | SG, ABV, gCO2/L/d e flags dos gráficos (1 retido, 2 transição, 4 sintético) |
| `vol`, `co2`, `rph` | volume (L), g de CO2, reliefs por hora |
| `ss` | só no log sintético: dia 0 do perfil (epoch local) |

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
  POSTs da nuvem são pequenos. `/cloud` mostra o heap livre, o maior bloco e
  o mínimo desde o boot.
- Configuração em `/cloud` (URL e token, na NVS `sk_cloud`); contadores em
  `/cloud` e `/getstatus`.
- O LittleFS é montado com formatação se não montar: a partição do SideKick
  não guardava nada antes disso.
