# Ações automáticas (automatic set points)

Regras que mudam os setpoints de temperatura e pressão, e/ou mandam um e-mail, quando a fermentação atinge condições definidas (estabilidade, SG, gCO2/L/d). Configuradas na página **Set Points** (`/setpoint`), seção "Automatic set points".

Princípio de projeto: **não disparar é mais conservador que disparar fora de hora**. Dado inválido, ausente ou ambíguo bloqueia o gatilho.

## Manual only e Trigger now

- Cada regra tem a opção **Manual only**, persistida na NVS e exportada no XML. Com ela marcada, os campos de gatilho de medida ficam desabilitados na página, mas seus valores são preservados para uma eventual volta ao modo automático. Uma regra manual pode não ter gatilhos de medida.
- Uma regra **Manual only** nunca dispara na avaliação periódica. O botão **Trigger now** também pode disparar uma regra automática manualmente, sem testar seus gatilhos de medida.
- **Trigger now** ocupa o lugar de **Reset**: aparece somente enquanto a regra não tiver disparado e, se depender da anterior, somente depois que a anterior tiver disparado. Há confirmação antes do envio. O servidor confere novamente essas condições; edições ainda não salvas na página não são aplicadas pelo botão.
- O disparo manual usa a mesma aplicação de setpoints, persistência de horário, log Serial e e-mail do disparo automático. Exige horário NTP válido, mas não exige o modo Fermenting. Sem NTP ou se a gravação do horário falhar, nenhum setpoint é aplicado.
- XML v2 continua compatível: `manualOnly="true"` ou `"false"` é um atributo opcional de `<rule>`; a ausência significa `false`.
- Na página, a regra 1 fica sempre visível. Uma regra posterior só é ocultada quando a anterior não tem gatilho de medida nem **Manual only** marcado e a própria regra está vazia (sem nome, gatilhos, ações, dependência ou horário de disparo). A visibilidade acompanha as edições ainda não salvas; ocultar não altera a execução nem apaga definições.

## 1. Estabilidade de temperatura e pressão

Cada grandeza tem um estado e um horário ("stable since"), persistidos em `counters`:

| Estado | Significado |
|---|---|
| `CHANGING_DIRECT` | setpoint mudou (valor direto); aguardando estabilizar |
| `CHANGING_SLOW` | setpoint mudou com rampa (slow); aguardando estabilizar |
| `STABLE` | estável desde `stableSince` |
| `UNSTABLE` | estava estável e saiu da faixa de tolerância; aguardando estabilizar de novo |

**Alvo**: o setpoint **final** — o slow setpoint enquanto a rampa está ativa, senão o setpoint direto. Os passos da rampa e o fim dela não mudam o alvo; qualquer mudança real (página web, tela de toque, ESP-NOW, regra automática) volta o estado para `CHANGING_*` e zera `stableSince`.

### Temperatura

- Entra em `STABLE`: |T − alvo| ≤ FMTOFFSET (0,3 °C).
- Sai (`UNSTABLE`): |T − alvo| > 3 × FMTOFFSET (0,9 °C).

### Pressão

Com f = `pressureDropFactor` (headspace / (headspace + tanque de expansão), 0 < f < 1): o relief dispara em alvo/√f e leva a pressão a alvo·√f.

- Entra em `STABLE` (sem rampa em andamento): alvo·√f ≤ P ≤ alvo/√f — o próprio ciclo do relief.
- Sai (`UNSTABLE`): P < min(alvo·f, alvo − 0,05) ou P > max(alvo/f, alvo + 0,05).
- A faixa de entrada é sempre mais estreita que a de saída, o que evita alternância na borda.
- Setpoint ≤ 0 ou f fora de (0, 1): não entra em `STABLE`.

Exemplos (f = 0,90): alvo 1,0 → entrada 0,949–1,054, saída 0,900–1,111; alvo 2,0 → entrada 1,897–2,108, saída 1,800–2,222.

### Regras comuns

- Horário **somente do NTP** (hora local, UTC−3, como o resto do log). Se o NTP não estiver válido (inclusive com Wi-Fi/WAN fora, quando `NTPEpoch()` retorna 0), a entrada em `STABLE` fica pendente e é registrada quando o NTP voltar — com esse horário, não o da estabilização real.
- Leitura inválida (sensor em falha, NaN) não muda o estado.
- Não há filtro de tempo: uma leitura fora da faixa de saída já derruba a estabilidade.
- Gravação na NVS só nas transições.
- Após reboot, o estado é restaurado e a avaliação continua.
- Primeiro boot com o firmware novo: estado `CHANGING_DIRECT` até estabilizar.
- **Intervenção manual** na página **Counters** (`/counters`, seção "States"): escolher o estado de temperatura e de pressão. `STABLE` com "stable for (h)" grava `stableSince` = agora − horas (vazio = agora, ou mantém se já estava `STABLE`); exige NTP válido, senão a página avisa e nada muda. Os outros estados zeram `stableSince`. Depois disso a máquina de estados segue normalmente: `CHANGING_*` volta a `STABLE` assim que a leitura entrar na faixa; `STABLE` fora da faixa de saída cai para `UNSTABLE`.

## 2. Regras

Até **8 regras**. Cada uma:

| Campo | Faixa | Observação |
|---|---|---|
| Name | até 80 bytes UTF-8 | descrição livre; aparece ao lado de "Rule N" e no e-mail |
| **Gatilhos** (AND dos preenchidos) | | |
| Temp. stable for (h) | 1–360 | temperatura `STABLE` há mais que isso |
| Press. stable for (h) | 1–360 | pressão `STABLE` há mais que isso |
| SG < x | 0,990–1,200 | 3 casas decimais |
| gCO2/L/d < x | 0,5–20 | fora de transição de pressão ou temperatura (as duas estáveis há pelo menos 2 h: o saldo de CO2 liberado pela cerveja difere da produção enquanto ela absorve ou libera; docs/gco2-rate.md); só o valor calculado, > 0, com janela de pelo menos 30 min (31 amostras; ~32 min após o boot), e abaixo de x **sem interrupção por 20 min** (qualquer avaliação sem valor utilizável ou ≥ x recomeça a contagem). O valor retido de antes de um reboot nunca conta. Janelas curtas cobrem poucos ciclos de relief: no lote 160 (30/09 12:18) a primeira taxa após o boot, 7,19 com 4 min de janela e 8,8 real, disparou uma regra "< 8". Mediana do erro contra 6 h: ~1,2 g/L/d com 15 min, ~0,7 com 30 min |
| Requires rule N−1 | regras 2 a 8 | condição extra: a regra anterior precisa já ter disparado |
| **Novos setpoints** (opcionais) | | |
| Temperature / Temperature slow | 0–42 °C | |
| Pressure / Pressure slow | 0–2 bar | mesmo limite do teclado da tela |

- Campo vazio = critério ignorado / sem ação.
- Regra automática com qualquer ação exige pelo menos um gatilho de medida (o "requires rule" sozinho não basta). Regra manual pode não ter gatilhos de medida. Regra vazia (ou só com nome) não dispara automaticamente.
- Regra **sem ações** é válida: dispara, grava o horário e só manda o e-mail.
- Ações seguem a mesma semântica da página de setpoints: só o direto aplica o valor e cancela a rampa; só o slow faz a rampa a partir do setpoint atual; os dois saltam para o direto e fazem a rampa até o slow. As velocidades das rampas são as da página de setpoints.
- Uma regra disparada fica somente leitura, com botão **Reset** (zera o horário e libera edição).
- **Novo lote** (mudança para Fermenting pela página ou `TRANSFEREND` via ESP-NOW, que chamam `resetCountersForNewBatch()`): todos os horários de disparo são zerados; as definições ficam.

## 3. Avaliação e disparo

- A cada **10 s**, no `loop()` (`evaluateAutoSetpoints()`), **somente em modo Fermenting** e com NTP válido.
- Critério com medida inválida é falso.
- Regras avaliadas em ordem de índice, **no máximo um disparo por ciclo** — assim uma regra que muda um setpoint reinicia a estabilidade antes da próxima ser avaliada.
- No disparo:
  1. grava o horário (`triggeredAt`) na NVS **antes** de aplicar — um reboot entre os dois pode perder as ações, mas a regra nunca dispara duas vezes;
  2. aplica as ações e grava os setpoints; ação de temperatura/pressão reinicia a estabilidade correspondente mesmo se o valor não mudou;
  3. escreve `[AUTOSP] …` no Serial com regra, horário, critérios com os valores medidos e ações;
  4. coloca o e-mail na fila.

## 4. E-mail

- Biblioteca `IOTK_SimpleMail` (mesma do BrewCore; servidor, conta e destinatário estão no `.cpp` da lib, em `A:\Nuvem\Arduino\Sketches\libraries\IOTK_SimpleMail`, fora do git).
- Mudanças na lib: `sendSimpleMail()` retorna `bool` (true só com resposta 250 ao DATA); cabeçalhos sempre com `charset=UTF-8`; assunto não ASCII codificado em RFC 2047.
- No Povoto (`PovotoMail.cpp`), `sendSimpleMail()` roda numa **task FreeRTOS própria** (core 0, prioridade 1, stack 16 KB), criada no primeiro envio. O `loop()` só enfileira (timeout 0; fila de 3; se cheia, descarta e registra no Serial).
- Até 3 tentativas com 60 s de intervalo; antes de cada uma exige Wi-Fi conectado e ≥ 45 KB de heap contíguo. Resultado no Serial (`[MAIL] …`); falha não desfaz a aplicação.
- Assunto: `[POVOTO n] Automatic set point rule k triggered: <nome>`.

## 5. Exportar / importar XML

- **Save & export XML**: salva as regras da tela (mesmas validações do Save rules) e só então baixa `povoto-autosetpoints.xml`. Sem horários de disparo.
- **Import XML**: o navegador valida o XML e mostra o erro; o firmware valida as faixas de novo. Importar **substitui todas as regras e zera todos os horários**. Regra ausente no arquivo entra vazia. Depois recarrega a página.
- Formato (version 2; `requiresPrevious` é opcional):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<povotoAutoSetpoints version="2">
  <rule index="1" name="Cold crash" manualOnly="false">
    <trigger stableHours="24" pressureStableHours="" sgBelow="1.010" co2RateBelow=""/>
    <action pressure="" pressureSlow="" temperature="" temperatureSlow="2"/>
  </rule>
  <rule index="2" name="Aumenta pressão">
    <trigger stableHours="12" pressureStableHours="" sgBelow="" co2RateBelow="" requiresPrevious="true"/>
    <action pressure="" pressureSlow="1.2" temperature="" temperatureSlow=""/>
  </rule>
</povotoAutoSetpoints>
```

## 6. Persistência (NVS)

| Namespace | Chaves | Conteúdo |
|---|---|---|
| `pvt_autosp` | `rule0`..`rule7` | definição de cada regra (blob `AutoSetpointRule_t`) |
| `pvt_autosp` | `trig0`..`trig7` | horário do disparo (epoch local; 0 = não disparou) |
| `pvt_counters` | `tempState`, `tempStableAt`, `pressState`, `pressStableAt` | estabilidade |

- Blobs antigos sem `manualOnly` são ignorados no boot, e seus horários de disparo são zerados para liberar a edição das regras. Refaça as regras na página ou importe um XML; XMLs antigos continuam compatíveis e entram como `manualOnly=false`. Outros tamanhos ou regras inválidas também são descartados.
- `pvt_autosp` entra no factory reset e na reescrita de schema; não faz parte do `povoto-settings.json`.

## 7. Observabilidade e testes

- **Log Cold**: colunas `TempState`, `TempStableSince`, `PressState`, `PressStableSince` depois de `TemperatureMode`.
- **`/getstatus`**, seção de pressão:
  - "Stability": estado, desde quando e há quantas horas, mais as faixas de entrada/saída da pressão;
  - abaixo de gCO2/L/d: tempo de expansão calculado (na pressão atual e no limiar do relief) e espera restante após o último fechamento.
- **Página de debug** (`/debugparams`, só em modo debug):
  - Pressure / Temperature: vazio = lê INA / Dallas; preenchido = override com esse valor;
  - Add to stability time (h): recua o "stable since" de temperatura e pressão (só em `STABLE`) para simular o tempo passando.
- **Página Counters** (`/counters`, sempre disponível): estado de estabilidade e horas de estável, para intervenção (seção 1).

### Testes manuais

1. Mudar o setpoint de temperatura pela web, tela e ESP-NOW → `CHANGING_DIRECT`; com slow → `CHANGING_SLOW`; `STABLE` ao entrar em ±0,3 °C do alvo final; `UNSTABLE` além de ±0,9 °C.
2. Pressão: `STABLE` dentro da faixa de entrada; `UNSTABLE` fora da de saída (conferir as faixas no `/getstatus`).
3. Reboot durante `CHANGING_*` e durante `STABLE`: estado e horário preservados.
4. NTP indisponível: a estabilização fica pendente e é gravada quando o NTP volta; nenhuma regra dispara.
5. Regra com ação e sem gatilho: recusada. Valores fora das faixas: erro com o número da regra.
6. Regra com SG < (SG atual + 0,005) em Fermenting: dispara em até 10 s (`[AUTOSP]`, setpoints alterados, campos travados, e-mail). Em Conditioning: não dispara.
7. Reboot após o disparo: não dispara de novo; Reset libera.
8. "Requires rule": a regra 2 espera a 1 disparar.
9. Add to stability time (h) para fazer uma regra de horas disparar.
10. Save & export XML, alterar regras, Import XML: regras restauradas, horários zerados.
11. Wi-Fi desligado no disparo: 3 tentativas `[MAIL]` e "giving up", sem afetar o controle.

## 8. Código

| Arquivo | Conteúdo |
|---|---|
| `include/AutoSetpoints.h`, `src/AutoSetpoints.cpp` | regras, validação, NVS, avaliação e disparo |
| `src/TermperatureControl.cpp` | estabilidade de temperatura |
| `src/PressureControl.cpp` | estabilidade de pressão, faixas, `/getstatus` |
| `src/PovotoData.cpp` | campos em `counters`, `pvt_autosp` no init / factory reset / schema |
| `src/PovotoPages.cpp` | seção da página, export/import, página de debug |
| `include/PovotoMail.h`, `src/PovotoMail.cpp` | fila e task de e-mail |
| `src/datalog.cpp` | colunas do log Cold |

Rotas: `POST /setpoint/auto/update` (`?noredirect=1` para o Save & export), `POST /setpoint/auto/reset?rule=N`, `GET /setpoint/auto/export`, `POST /setpoint/auto/import`. Registradas antes de `/setpoint`, porque o ESPAsyncWebServer casa rotas por prefixo.

Nota de build: nos arquivos que incluem `IOTK_NTP.h` ou `IOTK_SimpleMail.h`, `IOTK_ESPAsyncServer.h` vem antes; sem isso o Library Dependency Finder do PlatformIO perde o `NTPClient` do grafo e o build falha.
