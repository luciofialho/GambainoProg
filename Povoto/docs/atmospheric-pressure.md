# Pressão atmosférica (Patm)

Código: `src/AtmosphericPressure.cpp`. A variável global `Patm` (bar) é usada no
Henry (CO2 dissolvido), nas correções politrópicas e, na ventilação direta, na
vazão da válvula (∝ pressão absoluta).

## Fontes, em ordem de prioridade

O Povoto **não tem barômetro próprio**: a pressão atmosférica vem sempre do SideKick (o BMP581 fica no
SideKick). O antigo rótulo `sensor` (BMP581 no I²C do Povoto) foi retirado; o número 0 dele continua
reservado, porque os registros de ensaio de ventilação já gravados guardam a fonte como 0, 1 ou 2.

| Fonte | Rótulo | Validade |
|---|---|---|
| Broadcast ESP-NOW do SideKick, pacote `'A'` ("hPa") | `sidekick` | pacote há no máximo 10 min (mesma validade da temperatura ambiente) |
| Altitude configurada na página Settings | `altitude` | sempre |

- Cada fonte medida é a **média móvel de 1 min** (as amostras do último minuto
  antes da mais recente) e precisa estar entre **700 e 1100 hPa**.
- Fonte inválida: a próxima assume, sem intervenção. A troca de fonte não mexe em
  nada acumulado; só afeta os cálculos seguintes.
- Troca automática para uma fonte de prioridade menor: aviso no `/getstatus`
  ("fell back from ... at ..."), sem alarme sonoro, até a fonte melhor voltar.
- A altitude continua sendo o fallback: confira-a mesmo com o SideKick ligado e não a
  mude no meio de um lote. Com a altitude, uma variação real da Patm durante uma rotina longa
  (por exemplo o volume do fermentador, 2–3 h) não é vista.

## Logs e status

- Cold e Relief: `AtmosphericPressure` (bar) e `AtmosphericPressureSource`
  (`sidekick` / `altitude`; `sensor` só em registros antigos).
- `/getstatus` (seção de pressão): o valor em uso e a fonte; cada fonte com valor,
  idade e validade (a do SideKick e a altitude).

## SideKick (envio)

Firmware do SideKick, `SideKick/src/Sidekick-ambient.cpp`:

- BMP581 no I²C (SDA 32, SCL 33), lido a 1 Hz;
  a cada 10 s, broadcast `'A'` com a média das leituras válidas desses 10 s.
- A temperatura ambiente (`'V'`, Dallas opcional do SideKick) está em docs/thermometers.md.
- O pacote `'A'` é novo: Povotos e SideKicks antigos o ignoram, e um Povoto
  novo com um SideKick antigo usa a altitude. O `'V'` continua com um número
  só: os Povotos antigos rejeitam qualquer outro formato.
- O driver do BMP581 (`Common/GambainoBMP581`) é usado só pelo SideKick; o Povoto não o inclui mais.
