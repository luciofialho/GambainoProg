# Pressão atmosférica (Patm)

Código: `src/AtmosphericPressure.cpp`. A variável global `Patm` (bar) é usada no
Henry (CO2 dissolvido), nas correções politrópicas e, na ventilação direta, na
vazão da válvula (∝ pressão absoluta).

## Fontes, em ordem de prioridade

| Fonte | Rótulo | Validade |
|---|---|---|
| BMP581 no barramento I²C do Povoto (próxima versão da placa; endereço 0x47 ou 0x46) | `sensor` | leitura há no máximo 60 s |
| Broadcast ESP-NOW do SideKick, pacote `'A'` ("hPa") | `sidekick` | pacote há no máximo 10 min (mesma validade da temperatura ambiente) |
| Altitude configurada na página Settings | `altitude` | sempre |

- Cada fonte medida é a **média móvel de 1 min** (as amostras do último minuto
  antes da mais recente) e precisa estar entre **700 e 1100 hPa**.
- Fonte inválida: a próxima assume, sem intervenção. A troca de fonte não mexe em
  nada acumulado; só afeta os cálculos seguintes.
- Troca automática para uma fonte de prioridade menor: aviso no `/getstatus`
  ("fell back from ... at ..."), sem alarme sonoro, até a fonte melhor voltar.
- Sem BMP581 no boot, o Povoto tenta de novo a cada 5 min.
- A altitude continua sendo o fallback: confira-a mesmo com barômetro e não a
  mude no meio de um lote.

## Logs e status

- Cold e Relief: `AtmosphericPressure` (bar) e `AtmosphericPressureSource`
  (`sensor` / `sidekick` / `altitude`).
- `/getstatus` (seção de pressão): o valor em uso e a fonte; cada fonte com valor,
  idade e validade; a diferença de cada uma para a altitude e, com as duas
  medidas, sensor − sidekick.

## SideKick (envio)

Firmware do SideKick, `SideKick/src/Sidekick-ambient.cpp`:

- BMP581 no I²C dos antigos pinos do display (SDA 27, SCL 14), lido a 1 Hz;
  a cada 10 s, broadcast `'A'` com a média das leituras válidas desses 10 s.
- Termômetro Dallas opcional no GPIO 32: se instalado, broadcast `'V'` (a
  temperatura ambiente, mesmo formato do BrewCore) a cada minuto. O Povoto
  prefere o `'V'` do SideKick (reconhecido pelo MAC do peer SideKick) ao do
  BrewCore (docs/thermometers.md).
- Os dois são detectados no boot; o Dallas é procurado de novo a cada minuto.
- O pacote `'A'` é novo: Povotos e SideKicks antigos o ignoram, e um Povoto
  novo com um SideKick antigo usa a altitude. O `'V'` continua com um número
  só: os Povotos antigos rejeitam qualquer outro formato.
- O driver do BMP581 (`Common/GambainoBMP581`) é o mesmo nos dois firmwares.
