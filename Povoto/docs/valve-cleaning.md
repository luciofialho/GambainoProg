# Ciclo de limpeza da válvula

Código: `src/ValveCleaning.cpp`; página **Control**, seção "Valve cleaning" (antes ficava na Calibration; as rotas `/calibration/cleaning/*` e os eventos são os mesmos).
Procedimento: `docs/manual-calibracao-manutencao.md`, P10.

- Só em OFF, sem task nem outra rotina. Durante uma rodada nenhum vent automático abre a válvula.
- **O programa não controla fases**: quais líquidos, em que ordem e quando trocar é decisão do operador
  (sem caixa de confirmação, sem contagem de fases). Cada rodada é só "Start valve cleaning" com o
  tempo escolhido (0,5–15 min, padrão 4).
- Ligação: líquido pressurizado (alguns décimos de bar) **só na porta A**;
  P solta do fermentador e R sem silenciador, as duas para o dreno.
- A rodada alterna a válvula energizada (A→P, contracorrente no assento) e
  desenergizada (A→R, lava a câmara do êmbolo), **2 s + 2 s**, até o fim do tempo.
- **LED de venting** aceso enquanto a válvula está fechada (desenergizada, A→R) durante a rodada;
  apaga com a válvula aberta e no fim.
- Sequência sugerida (na página e no manual): detergente neutro → água → álcool 70% → ar (secagem),
  uma rodada para cada.
- Fim normal: evento de calibração `valve-cleaning` ("x of y min, n cycles"); o próximo ensaio de
  ventilação já vem com a etiqueta `after-cleaning-cycle` (qualquer rodada completa a aciona).
  Abortar (botão, mudança de modo ou task) fecha a válvula e registra `valve-cleaning-aborted`,
  que não muda a etiqueta.
- Sem soda e sem percarbonato (corpo de alumínio); bobina seca.
- Na primeira vez, conferir que a VT307 comuta com A pressurizada (o líquido
  sai alternadamente por P e por R).
