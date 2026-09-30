# Fermentação 160: erro de headspace vs. T ambiente (rev. 9, 2026-09-29)

Fontes: Analises160/ReliefsIniciais.xlsx, AteRelief469.xlsx, LogsParaAnalise.xlsx (Relief 1–783, Recovery 418–783, Cold até 29/09 19:50), PressureControl.cpp, datalog.cpp, AnuncioSensor.pdf. Trechos excluídos: "Ajuste por envTemp" (reliefs 351–376, tinha um erro) e o trecho com T ambiente travada em 25,0 (reliefs 377–417).

## Rev. 9 – hipótese do compressor de refrigeração (não se sustenta)
- Hipótese: o compressor da refrigeração aquece o ET mais do que o ambiente e, por só ligar quando o líquido esquenta, traria um atraso próprio.
- Teste: tempo em CHILL numa janela recente, pela diferença do acumulador ChillTime(h). Janelas de 30–180 min, deslocadas 0–120 min para trás.
- A refrigeração ficou ligada só 4–15% do tempo por hora em 29/09.
- Correlação com o headspace: |r| ≤ 0,13. Como correção, no melhor caso 0,60 → 0,48 L em 29/09 e piora fora da amostra (28/09: 0,46 → 0,50–0,53 L). Somada à derivada da Tamb: não acrescenta nada.
- Relief com o compressor ligado vs. desligado: diferença de −0,05 ± 0,06 L. Nada.
- A refrigeração acompanha o nível da Tamb (r = 0,55–0,73), não a derivada (r ≤ 0,23); é a derivada que explica o erro.

## Rev. 8 – decisão: média de 24 h, n fixo em 1,29
- Expoente: n efetivo pela recuperação medida (τ = 30 s, 361 reliefs): média 1,290 (desvio 0,036); ~1,27 à noite, até 1,345 às 14 h. 1,29 fixo é o melhor na média diária.
- Constante de tempo de 120 min: mal determinada; 2–4 h sugere massa térmica grande (fermentador/headspace).
- Forma com derivada: Vh + c·dTamb/dt (derivada nas últimas 3–4 h), c ≈ −0,65 L por (K/h). Empírico.
- Heurística escolhida: média móvel de 24 h (média das médias horárias). Um erro proporcional a dTamb/dt tem integral ≈ 0 num ciclo diário. Resultado: 30,2–30,6 L desde 28/09 20 h. Atraso de ~12 h.
- Sensibilidade da SG: ±1 L de headspace → ~0,01 ponto de SG.
- Variação de volume pela fermentação: a perda de massa de CO2 é compensada pela queda de densidade; líquido de 0 a +0,5 L, dentro da incerteza.
- Tamb 10 °C mais alta no fim: efeito da derivada desprezível; efeito do nível entre −0,8 e +1 L (incerto).

## Rev. 7 – ciclo diário completo de 29/09 (P ≈ 0,826 bar, Tamb 20,5–28,3 °C)
- Histerese: Vh cai quando a Tamb sobe (30,4 → 29,5 L) e sobe acima da base quando desce (até 31,6 L). Mesma Tamb (23,5 °C): 29,9 L de manhã, 31,6 L à noite.
- Sombra com limiar no nível da Tamb: falhou.
- Curva completa (Recovery + Cold): τ ≈ 30 s; A ≈ 14 mbar (13,5 à noite, 16,6 às 14 h). A queda da manhã é recuperação térmica variável; o excesso da noite (+1,5 L) persiste em todos os métodos.

## Conclusões anteriores (rev. 1–6), ainda válidas
- Offset do sensor com a temperatura descartado (YD6080: ≤0,6 mbar/K → ≤0,06 L/K).
- Ar ventado não é contado a mais. Erro real: Henry com pressão total durante a lavagem do ar (transitório; degrau de −1,36 ponto de SG em 27/09 13:26; não afeta a SG final).
- Timestamp em UTC após certos reboots (IOTK_NTP); correção aplicada no firmware.
- INA226: 3,47/16,73 mA em vez de 4/20 → shunt efetivo ≈ 2,49 Ω (hipótese). O ganho não afeta Vh.
