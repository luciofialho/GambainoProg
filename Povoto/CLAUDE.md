# POVOTO – contexto para o Claude Code

Firmware ESP32 que monitora e controla fermentação de cerveja (pressão e temperatura). A partir de expansões controladas do headspace (FT) para um tanque de expansão (ET) de volume conhecido, estima o volume de headspace/cerveja, o balanço de CO2 e, principalmente, a densidade (SG). Converse em português.

## Arquivos principais
- `PressureControl.cpp`: leitura de pressão (INA226, shunt 3 Ω, sensor 4–20 mA 0–2 bar YD6080), ciclo de relief/expansão, headspace, CO2 dissolvido e SG.
- `datalog.cpp`: logs "Cold" (periódico, 30 s), "Relief" (um por relief) e "Recovery" (diagnóstico, marcado com [DIAG]).
- `PovotoTasks.cpp`: janelas de tarefa (Dump, Gas, Liquid, Dry Hopping, Dynamic Hopping).
- `AutoSetpoints.cpp` (+ `PovotoMail.cpp`): ações automáticas. Documentação: `docs/automatic-actions.md`.
- Persistência: `docs/NVS-storage.md`; teste estrutural: `python tests/check_storage_schema.py`.

## Regras de trabalho
- Pode haver fermentação em andamento com o equipamento em produção. Nunca gravar na placa (upload/OTA) sem confirmação explícita.
- Mudanças de diagnóstico não podem alterar controle, headspace, CO2 nem SG.
- Preservar o final de linha CRLF dos arquivos.
- O usuário confere os números: verificar antes de afirmar. Prefere versões completas (sem etapas intermediárias) e soluções com base física ou em análise de dados.

## Estado atual (29/09/2026, fermentação 160)
- O headspace medido a cada relief oscila com o ciclo diário da T ambiente (histerese: cai quando ela sobe, sobe quando ela desce; ±1 L). A média de 24 h cancela isso (30,2–30,6 L).
- Em implementação: `docs/spec_headspace_24h.md` (média de 24 h aplicada + rebase no dump + limpeza em Liquid/Dry Hopping).
- O expoente politrópico fixo 1,29 bate com a recuperação medida na média diária (τ ≈ 30 s). Não mudar.
- Hipóteses descartadas: offset do sensor com a temperatura; expoente dependente do nível da Tamb; calor do compressor de refrigeração no ET.
- INA226: 3,47 mA / 16,73 mA onde se esperava 4 / 20 mA. Suspeita de shunt efetivo de ~2,49 Ω; é hardware, não o código.

Backlog e análise completos: `docs/povoto-backlog.md` e `docs/analise-160-headspace-vs-Tenv.md` (cópias do projeto Povoto no claude.ai).
