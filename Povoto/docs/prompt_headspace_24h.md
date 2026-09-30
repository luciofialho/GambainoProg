Implemente `docs/spec_headspace_24h.md` (headspace por média móvel de 24 h, já em uso, + rebase/limpeza em eventos) no firmware do POVOTO.

Leia antes: `CLAUDE.md`, `docs/spec_headspace_24h.md`, `docs/NVS-storage.md` e, para o porquê, `docs/analise-160-headspace-vs-Tenv.md`.

Regras:
1. Versão única, direto para produção (lote 160 em andamento). Siga a spec: fora o headspace aplicado passar para a média de 24 h quando ela for válida, nada muda em controle, CO2, SG nem nas colunas existentes dos logs (as novas vão no fim).
2. Antes de editar, mostre um plano curto: funções e arquivos (PressureControl.cpp/.h, PovotoData.h/.cpp, PovotoTasks.cpp, datalog.cpp/.h), a struct persistida e onde entra cada chamada. Se algo na spec conflitar com o código real, pare e me pergunte.
3. Marque o código novo com `[DAILY-HS]` e preserve o CRLF.
4. Compile (`pio run`). Rode `python tests/check_storage_schema.py`. Escreva e rode o teste de mesa em Python da seção 8 e mostre os números.
5. **Não grave na placa (upload/OTA) sem eu confirmar.**
6. No fim, me dê: resumo das mudanças, a lista das colunas novas na ordem (Relief e Cold) e qualquer decisão que a spec não cobria.
