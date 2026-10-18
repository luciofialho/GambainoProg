# Povoto sem tanque de expansão (TE): ventilação direta

Plano fechado em 11/10/2026, depois do debate a partir de `docs/avaliacao-sem-te.md`.
Manual de operação: `docs/manual-calibracao-manutencao.md`.

## 1. Decisão

- O Povoto passa a usar **só a ventilação direta**: a válvula ventila o
  fermentador direto para a atmosfera por um tempo estimado; os mols saem do
  modelo de vazão da válvula, calibrado com ar no fermentador vazio, convertido
  para CO2 pela teoria e afinado pela SG desgaseificada ao longo dos lotes.
- **Sem convivência dos dois métodos** no código. O TE fica numa tag
  (`te-final-<data>`) como fallback.
- Modelo híbrido (TE como referência periódica durante o lote): descartado.
  Monitor de sujeira dentro do lote com TE: descartado.
- Nenhuma instalação em produção com TE (o Aimbere ainda não está em produção).

### Fallback

- Volta: OTA do binário da tag + TE religado na porta A (o TE fica guardado no
  local). Só entre lotes (os contadores de um lote não passam de um método para o
  outro).
- Para a volta ser limpa, o firmware novo:
  - não apaga nem reaproveita chaves de NVS do TE (kCO2, kAir, Vr, dallasExpTank
    etc.) com outro significado; chaves novas para o que é novo;
  - não muda a tabela de partições;
  - não muda o formato de arquivos que a tag lê (`/co2buffers.bin` e outros na
    `persist`) sem mudar o magic/versão, para que a tag os recuse em vez de
    interpretá-los errado.
- Commits separados: o que é comum (barômetro, logs, alarmes, interface) em
  commits próprios, o que é da ventilação direta em outros. Assim os comuns podem
  ser aplicados (cherry-pick) num branch a partir da tag, se for preciso voltar.

### Nomes

- **Todo texto de tela, página web, `/getstatus` e mensagens de alarme em inglês.**
  Rótulos usados neste plano e no manual: "Sensor zero", "Save zero",
  "Unstable reading", "Venting test", "Valve cleaning", "Expansion tank volume";
  etiquetas do ensaio: `as-is`, `muffler-cleaned`, `valve-cleaned`, `new-valve`,
  `after-cleaning-cycle`; papel de Dallas `Headspace`.

- **vent**: o evento de ventilação direta (código: prefixo `directVent`).
- "relief" e "venting" deixam de ser usados para o evento novo; "venting" fica
  para o ensaio de ventilação.
- O log continua se chamando **Relief** (compatibilidade com análises e gráficos).

## 2. Conclusões do orçamento de incertezas

| Item | Com TE | Ventilação direta | Conclusão |
|---|---|---|---|
| Temperatura | Tref = Tamb + φ(Tferm − Tamb), φ ≈ 0,33 não testado; correção ~1% com 5 K, 5% no cold crash | só a T do gás que entra na válvula, peso 0,5 (vazão ∝ 1/√T). Com o gás 2 K / 6 K / 10 K acima da cerveja: ~0,35% / ~1,0% / ~1,7% | ganho líquido. **Termômetro no headspace** opcional (4.7): T_gás = T_headspace quando atribuído e válido; senão, Tferm. Sem modelo intermediário e sem chave de configuração (a atribuição decide). Instalado só no FMT1 (sonda de 15 cm em poço longo pela porta tri-clamp); FMT2 e Aimbere ficam sem. Os logs do FMT1 dizem a utilidade real. Conhecido: sonda curta perto da tampa erra por condução pela haste (estimativa ~10–20% da diferença ambiente–gás com ~10 cm de imersão); conferir puxando a sonda 3–4 cm num dia com ambiente bem diferente da cerveja e isolar por fora a conexão. Sem Dallas na linha de gás |
| Resfriamento do headspace durante a abertura | dentro do k | depende só da queda relativa da pressão absoluta; calculado por (P/P0)^((n−1)/n), n = 1,29 | desprezível depois de modelado |
| Krausen | — | o líquido da espuma sai da cerveja; o gás das bolhas é headspace | desprezível |
| Patm | não entra (tudo manométrico) | vazão ∝ densidade ∝ P absoluta: 0,34% (1,9 bar) a 0,52% (0,5 bar) por 10 hPa | erro constante absorvido pela curva do ensaio; sobra a variação do tempo (~0,1–0,2% por lote). Barômetro (4.2) |
| Offset do sensor (5 mbar) | ∝ 1/P manométrica: 0,26% a 1,9 bar, 1,0% a 0,5 bar | ∝ 1/P absoluta: 0,17% e 0,33% | ganho |
| Forma da curva de vazão | — | quadrática: resíduo sistemático ~2% (onda); **cúbica: 0,22% rms** (nível do ruído de leitura), ensaio de 18/09 | usar curva cúbica (ou melhor); a "dispersão de 1,8%" era o formato da curva, não a válvula |
| Estabilidade da vazão | — | `venting_speed (1).csv` contra 18/09: mesma curva com fator 0,997 ± 0,008 (cúbica) | ~1% entre ensaios; o histórico vai medir entre lotes |
| Tempo de abertura | irrelevante (equaliza) | entra direto: mols ∝ (t − t0) | contar pelo tempo **real medido** (instantes de abertura e fechamento), não pelo comandado; t0 mecânico da VT307 (≤ 20 ms pelo datasheet) calibrado; variação aleatória some na soma |
| Sujeira | resíduo: sensibilidade 0,046·x | 1:1 no C | rebaixada: o TE sai 100% limpo depois das fermentações e o sensor (mesma linha) não mostra sinais; no lote 160 não há tendência. Confirmar com o ensaio antes/depois da limpeza |
| Dallas fermentador × ambiente (× headspace no FMT1) | — | volume pelo barril: V ∝ T_F/T_E; vazão: peso 0,5 da T_gás | ±0,5 °C do datasheet = 3σ; diferença típica ~0,2 °C (0,07% no volume, ~0,03% na vazão), 3σ ~0,7 °C; intercalibrar |
| CO2 real | escondido no kCO2 | Z ≈ 0,992 (0,5 bar) a 0,985 (1,9 bar); B ≈ −125 cm³/mol a 20 °C | modelar (vazão e mols do headspace) |

Sensibilidade de referência: 1% de CO2 ≈ 0,4 ponto de SG no fim (cerveja 1,050 → 1,010).
Perda temporária esperada: nos primeiros 3–5 lotes o nível vem do fator teórico
(~1%) e das primeiras leituras de SG (±1,5–2,5% cada).
O método só muda a contagem do CO2 ejetado; os erros do CO2 dissolvido e da
supersaturação (29/09: SG 1,0185 medida contra 1,022 calculada) continuam.

### Critério de sujeira (ensaio antes/depois da limpeza)

- < 1% de perda de vazão por lote: sem mais nada.
- 1–3%: monitor passivo do "Vh aparente" (mols pela vazão × RT ÷ queda isotérmica,
  média de 24 h; o volume de cerveja quase não muda na fermentação:
  105 kg / 1,050 ≈ 101 kg / 1,010 ≈ 100 L). Só cálculo e coluna de log.
- > 3% ou muito variável entre receitas: prevenção (separador antes da válvula).

Montagem atual (tri-clamp vertical → T horizontal em 1/4" → T com válvula de
segurança e sensor de um lado, solenoide do outro): o pescoço vertical protege;
sem vaso antes da válvula.

## 3. Estratégia de calibração da vazão

1. **Volume do fermentador**: pesagem com água (129,6 L no site principal) ou
   barril (4.6).
2. **Ensaio de ventilação com ar, fermentador vazio**: curva τ(P) cúbica (volume
   exato, então o nível sai certo), t0 e repetibilidade.
3. **Conversão para CO2 pela teoria**, em função da pressão (bocal ideal, não um
   fator fixo 0,79): CO2/ar = 0,788 no sônico (≥ ~0,85 bar), 0,790 a 0,8 bar,
   0,796 a 0,5 bar, 0,802 a 0,3 bar. Mais o Z do CO2. Incerteza do nível ~1%.
4. **Afinar o nível pela SG desgaseificada** nas fermentações: um fator
   multiplicativo (`flowLevelFactor`), média dos lotes.
- Ensaio com CO2 de verdade: opcional, só para validar a forma da curva.
- A SG só dá o nível; a forma com a pressão vem do ensaio.

## 4. Requisitos de implementação

### 4.0 Base

1. Tag `te-final-<data>` no commit atual; guardar o binário.
2. Branch novo a partir dela. Retirar o que é só do TE: contagem por
   P_TE·Vr/k/Tref, resíduo r, ventilação do TE (`accountExpansionTankVenting`,
   `ventingResidual`, inventário do tanque), k e Tref, `KCO2FromBeerVolume`,
   ensaios de expansão e de k e piso, rotina rápida de volume, papel de Dallas
   "tanque de expansão" (a chave NVS fica, sem uso). A rotina lenta de volume é
   reaproveitada como volume pelo barril (4.6).
3. Manter: sensor, controle de pressão e temperatura, balanço de CO2, Henry, SG,
   gCO2, tasks, logs, nuvem, tela, alarmes, média de 24 h do Vh, expoente
   politrópico 1,29 e a leitura de 0,4 s com extrapolação.
4. Conferir espaço de flash e limites de buffer (`tests/check_*.py`).

### 4.1 Barômetro (Henry também usa P absoluta)

Fontes, em ordem de prioridade:

O Povoto não tem barômetro; quem tem o BMP581 é o SideKick (docs/atmospheric-pressure.md).

1. **SideKick**: pressão no broadcast ESP-NOW junto com a temperatura ambiente
   (um sensor por site). Pacote compatível com SideKicks antigos (sem pressão).
   Mesma validade do ambiente (expira em 10 min).
2. **Altitude** configurada (comportamento atual).

Regras:
- Validade: faixa plausível (ex. 700–1100 hPa), leitura recente (SideKick: 10 min).
  Inválida → próxima fonte, sem intervenção.
- Filtro: média móvel de ~1 min.
- Troca de fonte não mexe em estado acumulado (só afeta as vazões seguintes e o
  Henry).
- Logs Cold e Relief: `AtmosphericPressure` (já existe) +
  `AtmosphericPressureSource` (`sidekick` / `altitude`). `/getstatus`:
  fonte, valor, idade e, com duas fontes válidas, a diferença entre elas.
- Na troca automática para uma fonte de prioridade menor: aviso no status (sem
  alarme sonoro).
- O ensaio de ventilação grava a Patm e a fonte usadas.
- A parte do SideKick (envio) é no firmware do SideKick, em item separado.

### 4.2 Vent

- **Alvo**: queda de 6% da pressão manométrica (parâmetro), o mesmo dente de serra
  de hoje em torno do setpoint (limiar setpoint/√f).
- **Tempo estimado**: t = τ(P)·ln(P_abs,on / P_abs,alvo) + t0, com τ da curva
  calibrada e o Vh atual (média de 24 h). **A queda obtida é aceita**, mesmo que
  não seja exatamente 6%.
- **Tempo mínimo**: 1,5 s (parâmetro). Se o estimado for menor, abre 1,5 s.
- **Salvaguarda**: tempo limitado ao que daria ~12% de queda com o menor Vh
  plausível. No início do lote, sem média, Vh = volume do fermentador − volume de
  cerveja informado.
- **Tempo real**: registrar o instante de abertura e o de fechamento na mesma
  linha do `digitalWrite` (`esp_timer_get_time()` ou `micros()`). O fechamento
  continua no loop (o atraso do loop só aumenta a queda). Timer de hardware:
  opcional, só para o controle.
- **Contagem** (sem Vh e sem ler o sensor durante o transiente): mols =
  C_CO2(P̄)·P̄/√(Z·T_gás)·fator_resfriamento·flowLevelFactor·(t_real − t0), com P̄
  a média logarítmica de P_abs,on e da pressão logo após o fechamento (a leitura de
  0,4 s com a extrapolação e o expoente 1,29 que já existem). A vazão molar de um
  gás real num bocal é ∝ P/√(Z·T): o fator é 1/√Z (corrigido em 12/10/2026; antes
  estava "·Z·").
- **Vh aparente** de cada vent (mols × RT ÷ queda isotérmica): alimenta a média de
  24 h (mesmas regras de janela de task de hoje) e a coluna do monitor passivo.
- **Queda pequena** (manter o `ReliefCheck` `RELIEF_CHECK_NO_EFFECT` que já
  existe, adaptado ao vent: queda obtida muito menor que a esperada pela curva,
  2 seguidas → alarme, `docs/alarms.md`): válvula não abriu, saída obstruída.
  O vent com queda pequena **não é contado** (o modelo
  contaria mols que não saíram) nem alimenta o Vh.
- **Retirar o `RELIEF_CHECK_LEAK`** (queda elevada): sem TE não há causa no
  equipamento para ela; tudo relacionado sai.
- **Válvula presa aberta** (novo): pressão continuando a cair depois do
  fechamento, além da recuperação térmica esperada → alarme.
- Correção de massa líquida (`liquidMassInGasVentingPercent`): mantida.
- **T_gás** (vazão e mols do headspace): temperatura do Dallas do headspace se
  atribuído e válido; senão, Tferm. Henry e o CO2 dissolvido continuam com a
  temperatura do líquido.
- Log Relief: tempo estimado, tempo real, t0, P̄, T_gás usada e origem
  (`headspace` / `liquid`), Patm e fonte, mols, Vh aparente,
  queda obtida contra o alvo.

### 4.3 Modelo de vazão

- Curva τ(P) (ou taxa = 1/τ) **cúbica** na pressão manométrica, ajustada
  integrando cada liberação do ensaio (não pela taxa no ponto médio).
- Conversão ar → CO2 pela razão do bocal ideal em função da pressão.
- Z do CO2 na vazão (1/√Z: vazão molar ∝ P/√(Z·T)) e nos mols do headspace
  (1/Z inteiro: n = P·V/(Z·R·T)). Corrigido em 12/10/2026.
- Resfriamento durante o vent calculado pela queda (expoente 1,29).
- t0 descontado.

### 4.4 Ensaio de ventilação

- Durações alternadas em pressões parecidas, repetidas (ex. 0,5, 1, 2, 4, 8 s,
  3 ciclos), espera adaptativa até a pressão estabilizar, P1 e P2 com média de
  amostras. **Tempo real** de cada abertura (instantes medidos).
- Ajuste: curva cúbica (forma), t0 (interseção) e dispersão do t0
  (repetibilidade); verificação de deriva ao longo do ensaio (repetições da mesma
  duração no começo e no fim).
- Fermentador vazio e ar para a calibração. Opcional: água estimada ou garrafas
  PET (resolução melhor do t0).
- **Histórico dos últimos 50 ensaios** (partição `persist`, ~8 KB): dados brutos
  por liberação (t real, P1, P2), data/hora NTP, lote, Patm e fonte, Tamb, Tferm,
  gás, volume, versão do firmware e **etiqueta** escolhida na hora (como está /
  silenciador limpo / válvula limpa / válvula nova / pós-ciclo de limpeza).
  Comparação contra uma curva de referência fixa, em 1,8 / 1,2 / 0,8 bar.
  Exportação CSV. Botão para gravar a curva e o t0 (nada gravado sem o botão).

### 4.5 Ciclo de limpeza da válvula

- Só em modo OFF. Página de calibração e manutenção.
- Ligação: líquido pressurizado (alguns décimos de bar) **só pela porta A**; P e R
  livres para o dreno (P solta do fermentador; silenciador retirado).
- Ciclo: alterna energizada (A→P, contracorrente no assento) e desenergizada
  (A→R, lava a câmara do êmbolo), ex. 2 s + 2 s por ~4 min.
- Fases com confirmação manual: detergente neutro → água → álcool 70% → ar.
- Registro no histórico; o próximo ensaio já vem com a etiqueta "pós-ciclo de
  limpeza".
- Na primeira vez, confirmar que a VT307 comuta com A pressurizada.

### 4.6 Volume do fermentador pelo barril (Boyle lento)

- Barril pesado com água (19–50 L) na porta A.
- Válvula aberta até a pressão estabilizar (50 L tem τ ≈ 60 s; não 3 min fixos).
- Esperar a pressão estabilizar (ex. < 0,1 mbar/min) antes de P1 e de P2.
- V_F = V_E·(T_F/T_E)·P2/(P1 − P2), manométricas, com T_F (Dallas do
  fermentador) e T_E (ambiente) em cada momento; com T diferentes antes e depois,
  balanço de mols completo (Patm do barômetro).
- 3–5 reliefs partindo de 1,9 / 1,2 / 0,6 bar; zero do sensor antes; ventilação
  completa do barril entre reliefs (sem retenção).
- Erro por relief (±0,5 mbar), fermentador de 130 L: 2,5% com 2 L; 0,31% com
  19 L; 0,16% com 50 L.

### 4.6b Zero do sensor de pressão (rotina nova)

Motivo: `convertCurrentToPressure()` corta em 0 qualquer corrente abaixo de
`FMTData.pressure0Current` (e qualquer pressão negativa). Ler "0,000 bar" no
status não prova que o zero está certo: um zero deslocado para baixo fica
escondido. A rotina trabalha com a **corrente bruta**, não com a pressão.

- Só em OFF, sem lote ativo. **A rotina não abre a válvula**: o operador abre o
  fermentador para a atmosfera (de preferência uma abertura em cima e outra
  embaixo, com bastante circulação, sem gradiente) e confirma na página. A
  rotina só mede a corrente (mediana já filtrada) por **5 min** (parâmetro),
  o que serve também de teste de estabilidade do sensor. Exigir a placa ligada
  há pelo menos 30 min (aquecimento do transmissor e do INA226); antes disso,
  avisar e não oferecer o botão de gravar.
- Estabilidade: se a corrente variar demais ao longo da medição (desvio padrão
  ou amplitude acima de um limite, ex. equivalente a 1 mbar, ou tendência
  monotônica: inclinação da reta ajustada ao longo dos 5 min acima de ~0,5 mbar),
  avisar ("Unstable reading: fermenter not open, gas flowing or sensor
  drifting") e não oferecer o botão de gravar.
- Mostra: corrente média, desvio, amplitude e tendência, `pressure0Current` gravado, a diferença em mA
  e em mbar **sem corte** (pela inclinação da calibração perto do zero:
  `pressure1Bar / (pressure1Current − pressure0Current)`).
- Botão "Save zero": desloca **os dois pontos de corrente**
  (`pressure0Current`, `pressure1Current`) pela mesma
  diferença, preservando o ganho (um desvio de zero no transmissor/shunt é
  aditivo). Nada gravado sem o botão. Registrar data e diferença no histórico de
  ensaios.
- Conferência de ganho (mesma página): com o fermentador fechado e
  pressurizado, mostrar a pressão **sem corte** e a corrente para comparar com o
  **manômetro analógico do fermentador** (conferência grosseira, só pega falhas
  de alguns %). Sem manômetro de referência digital: um ganho constante é
  absorvido pela curva do ensaio (mesmo sensor no ensaio e no uso) e pela SG, e
  cancela no volume pelo barril; deriva de ganho aparece no histórico do ensaio
  de ventilação. Rever a compra (0–250 kPa, ≤ 0,25% FS) só se o histórico
  mostrar mudanças que precisem separar sensor de válvula. Sem gravar
  (recalibrar o ganho é a calibração dos pontos A e B que já existe).
- `/getstatus`: corrente bruta e pressão sem corte, além da pressão de controle.

### 4.7 Hardware

- Porta A para a atmosfera por uma **garrafa PET seca** (gás entra pelo fundo,
  topo bem aberto; silenciador de baixa resistência no topo, se houver):
  testemunha de líquido ejetado. Sem água. Porta R com silenciador ou filtro.
- Fonte da bobina estável.
- **Retirar o termômetro Dallas do TE** do barramento (junto com o TE). O papel
  "tanque de expansão" sai da página Settings e do código; a chave NVS
  `dallasExpTank` fica gravada, sem uso (compatibilidade com o fallback). No
  fallback, religar o Dallas junto com o TE.
- **Termômetro Dallas no headspace** (opcional; por ora só no FMT1: sonda de
  15 cm em poço longo pela porta tri-clamp, ponta no gás, sem tocar o líquido
  nem o krausen alto). Papel novo `Headspace` na página Settings (seção
  Thermometers), chave NVS nova `dallasHeadspace` (não reaproveitar
  `dallasExpTank`), campo `dallasHeadspace` no `povoto-settings.json`.
  - Sem chave de configuração: atribuído e com leitura válida → T_gás; sem
    atribuição, ou sem leitura válida por 60 s (mesma tolerância dos outros
    papéis) → Tferm, sem alarme (só indicação no `/getstatus`).
  - Nunca assume o controle de temperatura.
  - Log Cold: coluna nova `HeadspaceTemperature` (vazia sem sensor). `/getstatus`:
    papel, leitura e se está em uso. Rodar `tests/check_log_sizes.py` e
    `check_status_sizes.py`.
  - Atualizar `docs/thermometers.md`.

### 4.8 Documentação

- Corrigir em `docs/expansion-tank-k.md` a frase de que o krausen ocupa headspace.
- Docs só do TE (`expansion-tank-k.md`, `plan-expansion-tank-tref.md`, partes de
  `calibration-roteiro.md` e `calibration-speed-evaluation.md`): mover para
  `docs/historico-te/`, com nota apontando a tag.
- Manual: `docs/manual-calibracao-manutencao.md` (retirar os "(a implementar)"
  conforme ficarem prontos).
- Atualizar `CLAUDE.md`.

## 5. Sequência

1. **Agora** (lote terminando, só controle de temperatura): implementar tudo no
   branch, sem gravar na placa. Revisão à distância.
2. **Visita ao local**, no intervalo entre lotes (~3–4 h; checklist no manual,
   seção 8): OTA do firmware novo, troca do TE pela garrafa testemunha, P1, P2,
   P5 com ar no fermentador vazio, TE guardado no local.
3. **Antes de iniciar o lote**: análise do ensaio (à distância), gravação da curva
   e do t0.
4. **Durante o lote**: acompanhamento à distância; ajustes só por OTA e só entre
   lotes.
5. **Fim do lote**: SG desgaseificada e manutenção de fim de lote (manual, P8).

## 6. Limpeza (VT307: corpo de alumínio, vedações provavelmente NBR)

- Sem soda e sem Vanish em pó (percarbonato, pH ~10–11, ataca alumínio).
- Detergente neutro; álcool 70% para resinas de lúpulo; enxágue; secagem com ar
  ciclando a válvula.

## 7. Números de referência

- Vent de 6% (CO2, τ do ensaio de 18/09 convertido por 0,79):

| Vh | 0,5 bar | 1,0 bar | 1,9 bar |
|---|---|---|---|
| 20 L | 1,2 s | 1,5 s | 1,7 s |
| 30 L | 1,8 s | 2,2 s | 2,6 s |
| 50 L | 2,9 s | 3,7 s | 4,3 s |

- 1,9 bar, 30 L, 6%: ~0,14 mol por vent; com 10 g/L/d em 100 L, ~160 vents/dia.
- `venting_speed (1).csv` (5–40 s, um ciclo): t0 ajustado −0,40 ± 0,15 s (cúbica)
  ou −0,21 ± 0,18 s (quadrática), fisicamente implausível; 6 pontos, duração e
  pressão correlacionadas, leitura única. Inconclusivo: o t0 sai do ensaio novo.
