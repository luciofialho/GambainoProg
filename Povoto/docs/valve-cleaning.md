# Ciclo de limpeza da válvula

Código: `src/ValveCleaning.cpp`; página **Control**, seção "Valve cleaning" (antes ficava na Calibration; as rotas `/calibration/cleaning/*` e os eventos são os mesmos).
Procedimento: `docs/manual-calibracao-manutencao.md`, P10.

- Só em OFF, sem task nem outra rotina. Enquanto a sessão está aberta (de uma
  fase até a seguinte), nenhum vent automático abre a válvula.
- Ligação: líquido pressurizado (alguns décimos de bar) **só na porta A**;
  P solta do fermentador e R sem silenciador, as duas para o dreno.
- Cada fase alterna a válvula energizada (A→P, contracorrente no assento) e
  desenergizada (A→R, lava a câmara do êmbolo), **2 s + 2 s**, por 4 min (campo
  da página, 0,5–15 min).
- Fases, cada uma começada pelo operador depois de trocar o líquido em A
  (caixa de confirmação): detergente neutro → água → álcool 70% → ar (secagem).
- No fim da quarta fase: evento de calibração `valve-cleaning` ("4 of 4
  phases"), e o próximo ensaio de ventilação já vem com a etiqueta
  `after-cleaning-cycle`. Abortar (botão, mudança de modo ou task) fecha a
  válvula e registra `valve-cleaning-aborted`, que não muda a etiqueta.
- Sem soda e sem percarbonato (corpo de alumínio); bobina seca.
- Na primeira vez, conferir que a VT307 comuta com A pressurizada (o líquido
  sai alternadamente por P e por R).
