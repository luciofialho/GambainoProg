# POVOTO – contexto para o Claude Code

Firmware ESP32 que monitora e controla fermentação de cerveja (pressão e temperatura). A partir de expansões controladas do headspace (FT) para um tanque de expansão (ET) de volume conhecido, estima o volume de headspace/cerveja, o balanço de CO2 e, principalmente, a densidade (SG). Converse em português.

## Arquivos principais
- `PressureControl.cpp`: leitura de pressão (INA226, shunt 3 Ω, sensor 4–20 mA 0–2 bar YD6080), ciclo de relief/expansão, headspace, CO2 dissolvido e SG.
- `datalog.cpp`: logs "Cold" (periódico, 30 s), "Relief" (um por relief) e "Recovery" (diagnóstico, marcado com [DIAG]).

## Regras de trabalho
- Há fermentação em andamento com o equipamento em produção. Nunca gravar na placa (upload/OTA) sem confirmação explícita.
- Mudanças de diagnóstico não podem alterar controle, headspace, CO2 nem SG.
- Preservar o final de linha CRLF dos arquivos.
- O usuário confere os números: verificar antes de afirmar.

## Estado atual (set/2026, fermentação 160)
- O headspace calculado varia com a temperatura ambiente: −0,16 L/°C nas tardes quentes, com correlação −0,69. A causa está na recuperação de pressão entre ~0,36 e 5 s depois de fechar a válvula (expoente politrópico efetivo); não é offset do sensor.
- O patch [DIAG] grava a curva de recuperação (15 pontos, ~0,4–60 s) e um headspace "sombra" com n = 1,29 + 0,0078·max(0, Tamb − 21), somente em log.
- Quando o device remoto para de enviar, a T ambiente fica travada em 25,0. Corrigir e validar em ENV_TEMP_VALID().
- Pendente para depois desta fermentação: a troca de modo do CO2 dissolvido (half-life → immediate) gera um salto de −1,4 ponto de SG no início (ar no headspace + Henry com pressão total).
- INA226: 3,47 mA / 16,73 mA onde se esperava 4 / 20 mA. Suspeita de shunt efetivo de ~2,49 Ω; é hardware, não o código.

Backlog e análise completos: `docs/povoto-backlog.md` e `docs/analise-160-headspace-vs-Tenv.md` (copiar do projeto Povoto no claude.ai).
