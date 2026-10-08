# Povoto na nuvem – plano

Estado em 05/10/2026: Fase 1 implementada (código em `cloud/`,
`src/CloudLog.cpp` e `SideKick/src/Sidekick-cloud.cpp`; formato e
comportamento em `docs/cloud-log.md`; setup e deploy em `cloud/README.md`).
Publicada em 05/10/2026: https://povoto-cloud.povoto-cloud.workers.dev (com
login do Access) e https://povoto-public.povoto-cloud.workers.dev (mesmo
código, sem Access, só para o envio do SideKick e os links públicos; no lugar
das exceções de caminho no Access). Ainda sem teste em hardware. Fase 2 não
implementada.

Registro da conversa de planejamento de 03/10/2026. Objetivo: uma aplicação
pequena na nuvem que receba os dados dos Povotos e permita consultá-los, de
preferência com as mesmas páginas `/dashboard` e `/graphs` da placa
(`docs/web-dashboard.md`). Sem servidor próprio, custo zero ou mínimo, sem
amarra a um fornecedor (o Lovable foi descartado por isso).

## Decisões do usuário
- Escopo: a nuvem é uma interface remota para as tarefas que hoje se fazem
  na rede local. Não é arquivo nem relatório; o que for para guardar ou
  materializar vai para o Google Drive. Exceção: o log dos lotes antigos
  fica na nuvem, para consulta nos gráficos (item abaixo).
- Vários Povotos (hoje 4). Um SideKick atende n Povotos e há mais de um
  SideKick, em locais diferentes. Todos ficam sempre ligados e com internet.
- Envio pelo SideKick, não pelo Povoto: ele já recebe o log por ESP-NOW
  (IOTK_GLog) e já faz POST HTTPS para o Google Sheets e o Brewfather
  (`SideKick/src/Sidekick-log.cpp`). A nuvem é um terceiro destino.
  Na implementação, o Povoto monta um registro próprio a cada 5 min (pacote
  `CLOUDLOGPACKET`) em vez de o SideKick filtrar o log Cold: o Cold não tem
  nome do batch, OG, flags, slow targets nem reliefs por hora, e o SideKick
  dependeria do cabeçalho do Cold para achar as colunas.
- Só um log numérico a cada 5 min. Sem envio separado de "status ao vivo";
  o dashboard da nuvem mostra a última linha do log.
- Várias pessoas, com permissões diferentes por Povoto.
- Guardar as fermentações antigas (consultar batches passados).
- Desejado para depois: editar setpoints na nuvem e eles chegarem ao Povoto,
  mesmo com atraso.

## Proposta
- Cloudflare Workers + D1 (SQLite) + arquivos estáticos, no plano gratuito.
  Volume previsto: 4 Povotos x 288 linhas/dia ≈ 1.150 escritas/dia, contra
  100 mil/dia gratuitas no D1 e 100 mil requisições/dia no Worker.
- Login: Cloudflare Access (gratuito até 50 usuários), por código no e-mail ou
  pela conta Google, com sessão persistente por navegador de até ~1 mês.
  As permissões ficam numa tabela nossa no banco
  (`permissions(email, povoto_id, role)`, role = view | edit; povoto_id 0 =
  todos); o Worker usa o e-mail autenticado pelo Access e confere o JWT.
- Identidade do Povoto na nuvem (decidido): o local é um número e o par
  (local, PovotoNum) nunca se repete. Onde for preciso um número único, usar
  local*100 + PovotoNum. Cada SideKick tem um token e só grava nos Povotos do
  seu local.
- Portabilidade: TypeScript/JS com a API web padrão e o framework Hono (roda
  em Cloudflare, Node, Deno, Bun e Vercel). O acesso ao banco fica isolado
  numa camada fina. SQL do SQLite; dump `.sql` exportável. O que fica preso à
  Cloudflare é só `env.DB`, `wrangler.toml` e o Access.
- Código numa pasta `cloud/` no repositório. Deploy com
  `npx wrangler login` (uma vez), `npx wrangler d1 create` (uma vez) e
  `npx wrangler deploy` (a cada atualização), ou deploy automático ligado ao
  GitHub.

## Colunas do log (5 min)
- Do gráfico: SG, temperatura e setpoint, pressão e setpoint, gCO2/L/d, ABV,
  flags.
- Extras para o dashboard: slow target, volume, g de CO2, reliefs por hora.
- Do batch: número, nome, data, OG (em toda linha ou só quando mudarem).
- Fase 2: `spHash` e `rulesHash` (CRC32 em hexa, 8 caracteres cada).

## Reaproveitar as páginas sem copiar código
- O deploy publica direto os arquivos de `Povoto/data/www` (+ `LCars.bmp`),
  sem cópia no repositório.
- Ajustes nas páginas, inofensivos na placa:
  - links relativos, para funcionarem sob `/p/<id>/dashboard/`;
  - uma flag de "somente leitura" no `status.json` que esconde os toques de
    `/batch`, `/tasks` e `/setpoint`;
  - em `/graphs`, um seletor de batch que só aparece se o servidor listar
    batches.
- O Worker responde `status.json`, `data.csv` e `meta.json` no mesmo formato
  da placa.
- As demais páginas (setpoints, batch, controle) são HTML montado em C++
  (`PovotoPages.cpp`); para reaproveitá-las, cada uma precisa antes virar
  HTML/JS estático alimentado por JSON.

## Link público de um batch
- Quem tem papel "editar" no Povoto cria um link `https://<app>/s/<token>`
  na página do batch; o token é aleatório (128 bits), não sequencial.
- O link é de um batch: mostra aquele lote (atualizando enquanto estiver
  em andamento) e nunca os seguintes do mesmo Povoto.
- Sem login: o caminho `/s/*` tem política Bypass no Cloudflare Access; o
  resto do app continua protegido.
- Mostra só a página de gráficos, com as quatro abas e Evolution como
  padrão, no modo somente leitura (sem menus, downloads de CSV nem seletor
  de batch). O Worker serve só os dados daquele batch.
- Sem prazo: vale enquanto o batch estiver gravado ou até ser revogado.
  Apagar o batch apaga os links dele.
- Tabela `shares(token, povoto_id, batch, created_by, created_at,
  revoked_at)`.

## Busca de batch
- Campo de busca na página inicial: o número do batch (ou parte do nome)
  lista os batches encontrados com Povoto, local, nome e data, sem precisar
  escolher o Povoto antes.
- Só aparecem os batches dos Povotos que o usuário pode ver. Se o mesmo
  número existir em mais de um Povoto, aparecem todos.

## Teste da Fase 1 (critério para começar a Fase 2)

Critérios, conferidos por SQL com `wrangler d1 execute` (sem página):
- Completude: linhas por Povoto por dia conforme o intervalo; buracos só em
  queda real de internet ou energia, preenchidos depois pelo reenvio.
- Sem duplicata: chave única (Povoto, horário); reenviar não cria linha.
- Valores corretos: iguais aos que o Povoto enviou (no log sintético,
  recalculados a partir do horário).
- Atraso: última linha com no máximo ~6 min em operação normal.
- SideKick estável: heap mínimo, `espnowQueueDrops` e reboots sem tendência;
  Google Sheets e Brewfather continuam recebendo normalmente.
- Consumo na Cloudflare próximo do previsto.

Situações a provocar: internet fora por ~1 h (a mais importante), reboot do
SideKick no meio do envio, reboot do Povoto, deploy do Worker durante o
envio. Duração: um lote inteiro ou pelo menos 2 semanas.

Dois testes complementares:
1. Bancada: placa em modo debug como Povoto 3 do SideKick 1, com log
   sintético e intervalo de log curto para acelerar. Grava no mesmo banco
   da nuvem; os dados de teste são apagados depois pela opção de apagar um
   batch.
2. Povotos reais com lote em andamento: os mesmos critérios de completude,
   nos SideKicks de produção.

### Log sintético (Povoto, só em modo debug)
- Opção "Synthetic log" em `/debugparams`, gravada na NVS (sobrevive ao
  reboot, que faz parte do teste) e ignorada fora do modo debug.
- Só o registro da nuvem leva o perfil sintético de fermentação, função só
  do horário do registro e do dia 0 (que vai no registro). O controle real,
  o log Cold, os gráficos da placa e o Brewfather não mudam.
- O perfil sai do demo de 14 dias dos gráficos (`graphSyntheticPoint()`,
  usado pelos dois). `cloud/scripts/synthetic.mjs` repete as fórmulas e a
  conferência recalcula cada valor.
- Registros marcados com a flag 4 (sintético).
- Em debug, a janela da nuvem acompanha o intervalo do log Cold quando ele
  for menor que 5 min, para acelerar o teste.
- Verificar: o modo debug é definido pelo IP (`updateDebugModeFromWiFi()`), e
  o auto-detect de peers exige o mesmo modo nos dois lados. Uma placa em
  debug pareada com o SideKick 1 de produção pode precisar do peer
  cadastrado à mão.

### Apagar um batch (nuvem)
Opção para apagar as linhas de log de um batch de um Povoto, só para quem
tem papel "editar" naquele Povoto. Serve para limpar os dados de teste e
batches gravados por engano. Desde 05/10/2026 a página não mostra o botão
(a rota `POST /p/<id>/batches/<n>/delete` continua no Worker). Registros que ainda chegarem daquele batch
(fila do SideKick, bancada ligada) o recriam: desligar o log antes.

## Fase 2: setpoints e ações automáticas pela nuvem (implementada)

Implementada em 06/10/2026, antes de terminar o teste da Fase 1, por decisão
do usuário: não é usada em lotes em andamento e cada Povoto tem a chave
"Accept cloud edits" (Settings, ligada por padrão). Protocolo e detalhes em
`docs/cloud-log.md`. Diferenças do desenho abaixo: os snapshots e as
respostas vão pela fila do SideKick como linhas comuns (`k` = `sp`, `r`,
`ack`); a consulta de 60 s é o próprio POST do log (os pedidos vêm na
resposta); a nuvem não recalcula hashes, compara o hash que o Povoto manda.

Desenho original:

Princípio: o Povoto é o dono do estado. A nuvem guarda uma cópia, atualizada
somente por snapshots do Povoto, e uma edição feita na nuvem é um pedido que
o Povoto aceita ou rejeita. A cópia pode atrasar alguns minutos, mas nunca
fica diferente de forma permanente. Em caso de race condition, o pedido da
nuvem perde.

### Partes e assinaturas
Duas partes, cada uma com um CRC32 calculado sobre um texto canônico (valores
com casas decimais fixas; campo vazio = vazio), para que o Worker em JS
calcule o mesmo valor:
- `spHash`: temperatura, temperatura slow, pressão, pressão slow e as
  velocidades das rampas. Com a rampa ativa, o valor direto daquela grandeza
  fica de fora (ele anda a cada passo da rampa); sem rampa, ou depois que ela
  termina, o valor direto entra. O hash muda uma vez no fim da rampa. O
  modo não entra: não é editável pela nuvem e aparece pelo log.
- `rulesHash`: as 8 regras automáticas e os horários de disparo. Um hash
  só para todas: a edição de uma regra é rejeitada se outra disparou no meio
  (pouco provável e de baixo impacto).

Os dois hashes vão como colunas no log de 5 min.

### Povoto → nuvem (cópia)
1. Quando um hash do log é diferente do hash da cópia, a resposta ao POST
   pede o snapshot daquela parte.
2. O SideKick pede o snapshot ao Povoto por ESP-NOW (protocolo em blocos de
   `sendEspNow`; as regras têm ~1 KB em binário, ~10 blocos) e o envia à
   nuvem.
3. O snapshot sobrescreve a cópia: uma linha por Povoto e por parte, sem
   histórico. Histórico, se um dia for preciso, vai para o Google Drive.
Isso também corrige divergências por mensagem perdida, factory reset ou
import de XML.

### Nuvem → Povoto (pedidos)
1. A página grava o pedido como pendente: `{parte, baseHash, conteúdo, quem,
   quando}`. Um pedido pendente por parte e por Povoto; a parte fica
   bloqueada no front ("aguardando o Povoto") e o pedido pode ser cancelado.
2. O SideKick consulta a nuvem a cada 60 s, numa rota leve que traz os
   pedidos de todos os Povotos do seu local. O custo cresce com o número
   de locais (~1.440 requisições/dia por SideKick), não com o de Povotos.
3. Validade de 3 min no lado da nuvem (cobre uma consulta perdida): o
   pedido não buscado nesse tempo expira e o front mostra "não entregue".
   Evita que um SideKick que voltou depois de horas sem internet entregue um
   pedido que o usuário já deu por perdido.
4. O SideKick repassa o pedido uma vez por ESP-NOW, sem prazo nem nova
   tentativa. O Povoto só aplica se o hash atual da parte for igual ao
   `baseHash` e as faixas forem válidas (as mesmas da página web). Um
   setpoint direto durante a rampa aplica o valor e cancela a rampa, como na
   página.
5. O Povoto responde "aplicado" ou "rejeitado: motivo", e o SideKick envia a
   resposta e o novo snapshot à nuvem na hora, sem esperar o log. Sem
   resposta, o front mostra "não confirmado". O hash do log é a rede de
   segurança.

Regras na nuvem: as mesmas restrições da placa (regra disparada fica somente
leitura até o Reset). Reset e Trigger now são pedidos com `baseHash`, como
as edições.

Fora do escopo: mudar o modo (Fermenting/Conditioning/novo lote) pela nuvem.

Origem dos pacotes: o Povoto descarta os pacotes da Fase 2 (pedidos e
pedidos de snapshot) cujo MAC de origem não seja o `peerSideKick` cadastrado
em `Peers`. É um filtro simples, contra pacote enviado por engano ou vindo de
outro local; sem criptografia (o MAC pode ser forjado, mas a rede local
também é aberta). Para isso o handler do Povoto precisa receber o MAC
(hoje o callback de `GambainoCommon.cpp` repassa só tipo e payload).

Mexe no controle: só depois do envio do log rodar bem.

## Ambientes e versões (decidido em 07/10/2026)

Sites e Povotos rodam versões diferentes ao mesmo tempo; a nuvem é uma só por
ambiente, sempre a mais nova, e é ela que convive com todas as versões de
firmware em uso. A bancada não cobre todas as combinações: a defesa contra
incompatibilidade são as regras abaixo, não um banco de ensaio.

- **Um banco só** (D1 `povoto`) para produção e desenvolvimento. Cada site tem
  `env` (`prod`/`dev`) em `sidekicks` (migration 0004): Lucida (1) e Aimbere
  (3) produção, Bancada (2) dev. `scripts/add-sidekick.mjs --dev|--prod`.
- **Duas nuvens**: produção `povoto.brewtal.one` (+ `public.brewtal.one`
  para os SideKicks) e desenvolvimento `dev.brewtal.one` (Worker
  `povoto-dev`, `npm run deploy:dev`, páginas marcadas "DEV").
- **A nuvem dev lê tudo e escreve só em sites dev.** Em site de produção ela
  mostra dashboards, gráficos, lotes e set points, mas bloqueia no código o
  que mexe no Povoto ou apaga dados: pedidos de set point e regras
  (reset/trigger incluídos), apagar lote, e o ingest (403; o SideKick guarda
  e reenvia). O único risco para a cerveja é a alteração de set points; o
  resto é visualização, cuja falha não causa dano. Links públicos e a
  página Users ficam liberados. A produção aceita tudo, inclusive sites dev.
- **Banco só cresce**: migrations só adicionam tabelas e colunas com padrão.
  Remover ou renomear só depois que nenhuma versão em uso dependa disso.
  Antes de cada migration remota, anotar o bookmark do Time Travel
  (`wrangler d1 time-travel info povoto`). Migration que transforma dados é
  ensaiada antes numa cópia local (`wrangler d1 export` → `--local`).
- **Leitor tolerante**: a nuvem ignora campos que não conhece e trata campo
  ausente como "não informado". `cloud/test/fixtures/` tem linhas reais de
  cada geração de firmware; `npm test` precisa passar antes de publicar, e
  nenhum arquivo sai de lá enquanto um Povoto puder rodar aquela versão.
- **SideKick neutro**: guarda e repassa linhas sem interpretar. Hoje ainda olha
  o tipo (`ack`, `sp`, `r`) para acordar o envio; a próxima versão do
  protocolo troca isso por um campo genérico de urgência.
- **Próximo**: número de protocolo na linha de estado (`"pv"`), para a nuvem
  oferecer a cada Povoto só o que ele entende (hoje deduz pela presença dos
  hashes da fase 2); mostrar a versão de firmware de cada Povoto e SideKick;
  conferir se o modo do dispositivo (DEBUG/Operational) bate com o `env`
  do site.

## Próximos passos
1. Feito: D1, deploy, Access, token do SideKick 1 e permissão do Lucio.
   Para outras pessoas: regra por e-mail no Access e linha em `permissions`.
2. Gravar o firmware novo no Povoto de bancada e nos SideKicks e configurar
   `/cloud` em cada SideKick. Subir também o LittleFS web dos Povotos
   (`dashboard.js`, `graphs.*` mudaram).
3. Teste da Fase 1 na bancada (log sintético) e nos Povotos reais;
   conferência com `cloud/scripts/check-phase1.mjs`.
4. Fase 2, depois que o envio rodar bem.
