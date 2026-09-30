# POVOTO – backlog (atualizado em 2026-09-29, noite – rev. 10)

Status: [ ] a fazer · [~] em andamento · [x] feito. Prioridade: P1 alta · P2 média · P3 baixa.
Contexto técnico: `analise-160-headspace-vs-Tenv.md` (rev. 9).

## 1. Agora, remoto, durante a fermentação em curso
- [x] P1 Patch de diagnóstico (log Recovery + headspace "sombra"): em produção desde 28/09 ~20:00; funcionando.
- [x] P1 Recepção da T ambiente vinda do BrewCore. Em 29/09 variou 20,5–28,3 °C, sem travar.
- [x] P2 Timestamp do log em UTC após reboot: correção aplicada em 2026-09-29; falta compilar e gravar.
- [~] P1 **Headspace por média móvel de 24 h, já aplicada** (versão única para produção): spec `docs/spec_headspace_24h.md` (v2) e prompt `docs/prompt_headspace_24h.md`, na pasta do firmware (Prog/Povoto/docs). Média das médias horárias, válida com ≥18 h; estados valid/hold/ema; EMA atual como fallback; rebase no Dump (ΔH da lei de Boyle), limpeza em Liquid/Dry Hopping/Dynamic Hopping; blob "dailyHs" em pvt_counters; colunas novas no fim dos logs Relief e Cold. Implementação pelo Claude Code no VS Code. No lote 160, só passa a valer ~18 h depois da gravação.

## 2. Testes de bancada (quando voltar ao equipamento)
- [ ] P1 INA226: multímetro em série no loop, comparado com a leitura do INA em duas pressões; medir o shunt de 3 Ω desligado; conferir as ligações de medição e se o R100 do módulo foi removido. Hipótese: shunt efetivo de ~2,49 Ω.
- [ ] P3 (opcional; o usuário prefere evitar) Termômetros no ET e na tampa. Só se a média de 24 h não bastar.
- [ ] P3 (opcional) Isolar ou aquecer só o ET numa fase estável.
- [ ] P2 Anotar onde ficam o sensor e a válvula (tampa? tubo? comprimento e volume morto).

## 3. Firmware – correções depois desta fermentação
- [ ] P1 **Ajuste pós-purga em `applyDumpWindowHeadspaceRecalc`** (H_after = H_before·P1abs/P2abs, pela lei de Boyle). Hoje `endDumpTask()` lê P2 no fim da tarefa (manual ou timeout): cedo demais → gás ainda frio (τ ≈ 30 s) → ΔH superestimado (queda de 3% em P lida cedo → ΔH ~30% maior); tarde demais → a fermentação já recompôs parte da pressão → ΔH subestimado. Opções: (a) ler P2 2–3 min depois do fim da purga, descontando a taxa medida antes; (b) corrigir P2 com o expoente 1,29: P2_iso = P1abs·(P2abs/P1abs)^(1/1,29). A v2 da média de 24 h passa a logar P1, P2 e os tempos do dump para avaliar.
- [x] P2 Rebase da taxa (gCO2/L/d) na troca half-life→immediate (`rebaseCO2Evolution`). Falta compilar e testar.
- [x] P3 Modo do dissolvido persistido nos contadores (`co2Mode`). Falta compilar e testar.
- [ ] P3 (descartado por ora) Correção pela Tamb (atraso de 120 min ou derivada −0,65 L por K/h): empírica, sem base física firme. Só retomar se a média de 24 h não bastar.
- [ ] P3 (cosmético) Contador de ar no headspace (Henry com P − p_ar) para as primeiras ~12–18 h. Não afeta a SG final.
- [ ] P3 Immediate com setpoint maior: avaliar usar a pressão atual até os reliefs voltarem.
- [ ] P3 Reboot: persistir o histórico da taxa.
- [ ] P3 Moles no ET: usar a T do ET (ou ambiente) em vez da T da cerveja (≤1% do CO2 ejetado).
- [ ] P3 Remover o clamp p < 0 / I ≤ I0, ou registrar a leitura bruta.
- [ ] P3 `tests/check_storage_schema.py` falha em FMTData (ventResidual, ventResidual05, ventResidual18).
- [ ] P3 ENV_TEMP_VALID: checar a idade da última T ambiente recebida.

## 4. Análises pendentes
- [x] P1 Onde nasce a dependência da T ambiente: histerese (rev. 7).
- [x] P2 Sombra com limiar: falhou. Expoente: 1,29 fixo é o melhor na média diária (rev. 8).
- [x] P2 Hipótese do compressor de refrigeração aquecendo o ET: não se sustenta (rev. 9).
- [ ] P1 Com os dados até o fim da fase ativa: confirmar que a média de 24 h fica estável (hoje ±0,2 L), inclusive na desaceleração e na subida do alvo de pressão (anotar o horário).
- [ ] P3 Nível absoluto do headspace: calibrar contra um volume conhecido.
