# Histórico: método com tanque de expansão (TE)

Documentos do método antigo, em que cada relief expandia o headspace para um
tanque de expansão de volume conhecido. O Povoto passou à ventilação direta
(`docs/vent.md`, `docs/sem-te-plano.md`) em 11/10/2026.

O firmware desse método está na tag **`te-final-2026-10-11`** (commit 97e7209;
binários em `A:\Nuvem\Povoto\Firmware\te-final-2026-10-11`). Fallback, só
entre lotes: OTA do binário da tag, TE religado na porta A e o Dallas do TE
reatribuído; as chaves NVS do TE (kCO2, kAir, reliefVolume, ...) continuam
gravadas (`docs/NVS-storage.md`).

| Documento | Conteúdo |
|---|---|
| `expansion-tank-k.md` | fator de enchimento k e temperatura de referência do tanque |
| `plan-expansion-tank-tref.md` | plano da Tref do tanque |
| `calibration-speed-evaluation.md` | ensaios de velocidade (expansão, k e piso, ventilação de 20 s); o ensaio de ventilação atual é `docs/venting-test.md` |
| `calibration-roteiro.md` | roteiro de calibração de novembro com o TE |

Os caminhos citados dentro deles (`docs/...`) são os da época.
