# POVOTO – contexto para o Claude Code

Firmware ESP32 que monitora e controla fermentação de cerveja (pressão e temperatura). A válvula ventila o fermentador direto para a atmosfera (**vent**) por um tempo estimado; o CO2 que sai é contado por um modelo de vazão da válvula (calibrado com ar no fermentador vazio, convertido para CO2 pela teoria e afinado pela SG desgaseificada). Daí saem o headspace, o balanço de CO2 e, principalmente, a densidade (SG). Converse em português.

O método antigo, com tanque de expansão (TE), está na tag `te-final-2026-10-11` (fallback só entre lotes; `docs/historico-te/README.md`). Plano da mudança: `docs/sem-te-plano.md`; manual de calibração e manutenção: `docs/manual-calibracao-manutencao.md`.

## Arquivos principais
- `PressureControl.cpp`: leitura de pressão (INA226, shunt 3 Ω, sensor 4–20 mA 0–2 bar YD6080), ciclo do vent (`planVent`, `processReliefCycle`, `processPressure`), verificação do vent e válvula presa aberta, headspace, CO2 dissolvido e SG. Modelo do vent: `include/VentModel.h` (funções puras; espelho `tests/vent_model.py`). Documentação: `docs/vent.md`.
- Rotinas de calibração (página Calibration, só em OFF): zero do sensor (`SensorZero.cpp`, `docs/pressure-sensor-zero.md`), ensaio de ventilação (`VentingTest.cpp`, `docs/venting-test.md`, histórico de 50 em `/venttests.bin`), volume do fermentador pelo tanque de expansão (`KegVolume.cpp`, `docs/keg-volume.md`), ciclo de limpeza da válvula (`ValveCleaning.cpp`, `docs/valve-cleaning.md`). Eventos de calibração: `CalibrationEvents.cpp`.
- Pressão atmosférica (BMP581 → SideKick → altitude): `AtmosphericPressure.cpp`, `docs/atmospheric-pressure.md`. O SideKick (`SideKick/src/Sidekick-ambient.cpp`) envia a pressão (pacote `'A'`) e, com Dallas, a temperatura ambiente (`'V'`).
- `datalog.cpp`: logs "Cold" (periódico), "Relief" (um por vent; o nome ficou) e "Task".
- `PovotoTasks.cpp`: janelas de tarefa (Dump, Gas, Liquid, Dry Hopping, Dynamic Hopping).
- `AutoSetpoints.cpp` (+ `PovotoMail.cpp`): ações disparadas (triggered actions; nomes internos, chaves NVS, rotas e XML continuam "AutoSetpoints"). Documentação: `docs/triggered-actions.md`.
- Persistência: `docs/NVS-storage.md` (as chaves do TE ficam gravadas, sem uso: nunca reaproveitá-las); teste estrutural: `python tests/check_storage_schema.py`.
- CO2 dissolvido (estados inicial/equilíbrio/half-life/armado): `docs/dissolved-co2.md`; bench: `python tests/dissolved_co2_bench.py`. Estimativa (só registro) da janela de nucleação após dry hop: seção 8 do mesmo doc, `NucleationEstimate.h/.cpp`.
- Termômetros Dallas (fermentador, ambiente, headspace; papéis na página Settings): `docs/thermometers.md`.
- Alarmes de equipamento (sensor de pressão, válvula presa aberta, vent sem efeito, termômetro; bipes, tela, LEDs, attention LED): `docs/alarms.md`.
- Modo Conditioning (geladeira simples, balanço congelado, tasks bloqueadas): `docs/conditioning.md`.
- Taxa gCO2/L/d: `docs/gco2-rate.md`. Ao acrescentar colunas de log ou linhas no /getstatus, rodar `python tests/check_log_sizes.py` e `python tests/check_status_sizes.py` (limites de buffer; o /getstatus guarda ≥ 10% de folga do MAXSTATUSLEN (buffer estático `webStatusBuffer` do handler próprio do Povoto): diagnósticos novos do vent vão para o `/ventstatus`).
- Benches do vent: `python tests/vent_bench.py` (contagem contra simulação) e `python tests/venting_fit.py --selftest` (ajuste do ensaio).

## Regras de trabalho
- Pode haver fermentação em andamento com o equipamento em produção. Nunca gravar na placa (upload/OTA) sem confirmação explícita.
- Mudanças de diagnóstico não podem alterar controle, headspace, CO2 nem SG.
- Preservar o final de linha CRLF dos arquivos (alguns poucos, como `PovotoSettingsBackup.cpp` e `SideKick/src/Sidekick.cpp`, são LF: manter).
- Compatibilidade com o fallback: não apagar nem reaproveitar chaves NVS do TE, não mudar partições, mudar o magic de qualquer arquivo da `persist` cujo formato mude.
- Todo texto de tela, página web, `/getstatus` e alarme em inglês.
- O usuário confere os números: verificar antes de afirmar. Prefere versões completas (sem etapas intermediárias) e soluções com base física ou em análise de dados.
- Build: `IOTK_NTP.h` não pode ser incluído em arquivos que o LDF do PlatformIO varre primeiro (ordem alfabética, ex. `AtmosphericPressure.cpp`): ele monta mal os includes do `__IOTK_ESPAsyncServer`. Usar a declaração `unsigned long int NTPEpoch();`.

## Estado atual (11/10/2026)
- Branch `povoto-vent`: firmware só de vent, implementado e não gravado em placa nenhuma. Próximo passo: visita ao local (manual, seção 8), ensaio de ventilação com ar, análise do CSV e gravação da curva e do t0.
- Curva padrão até o primeiro ensaio: a do ensaio de 18/09 (t0 não medido).
- O expoente politrópico fixo 1,29 (recuperação medida na média diária, τ ≈ 30 s) continua na contagem (resfriamento) e na pressão assentada. Não mudar.
- INA226: 3,47 mA / 16,73 mA onde se esperava 4 / 20 mA. Suspeita de shunt efetivo de ~2,49 Ω; é hardware, não o código.

Backlog e análise completos: `docs/povoto-backlog.md` e `docs/analise-160-headspace-vs-Tenv.md` (cópias do projeto Povoto no claude.ai).
