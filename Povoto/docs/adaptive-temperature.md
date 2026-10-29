# Controle adaptativo de temperatura

A opção **Adaptive temperature control**, em Settings, aprende a resposta do
fermentador durante a operação normal. É desabilitada por padrão. O botão liga
ou desliga o recurso; os tempos de Cooling cycle e Heating cycle continuam
configurados como antes. `/temperaturestatus` mostra habilitação, estado,
estimativas, contagens, correção do centro e tendência.

## Limites de controle

O adaptativo aprende dois valores em °C: subida depois de ligar o frio e queda
depois de desligar. Uma compensação pode ficar em zero. Não é usado modelo de
segunda derivada: nos registros 999, 159 e 160 ele não melhorou consistentemente
a previsão. Planilhas e análises: `A:\Nuvem\Povoto\Calibracoes\Histerese`.

Para o alvo S e correção do centro C, a proposta é ligar em
`S + C + 0,3 − subida` e desligar em `S + C − 0,3 + queda`. As propostas são
limitadas, em cada chamada, pelos limiares do controle original:

- O limiar de acionamento nunca fica acima do original. Permanece o descanso
  mínimo, inclusive nas retomadas de `CHILL (paused)`.
- O limiar de desligamento nunca fica abaixo do original. Inclui a alteração
  original do limiar depois de cinco minutos ligado e os limites especiais de
  temperatura baixa. Qualquer corte por temperatura que o controle anterior
  faria continua acontecendo. O prazo máximo ligado permanece o mesmo.
- Existem pelo menos dois degraus de termômetro (0,125 °C) entre os limiares
  adaptativos; se a regra original não permite antecipar, prevalece a original.
- A faixa de ±0,3 °C é uma referência de regulação, não uma garantia de extremos.
  Os limites anteriores podem impedir centralização completa.

A garantia de não prolongamento compara pulsos iniciados no mesmo instante e
as regras originais aplicadas à temperatura observada. Antecipar uma partida
pode aumentar a duração contada desde seu início. Não há promessa de diminuir
horas acumuladas, número de partidas ou amplitude sem observar a operação.
Não foram ampliadas a faixa em temperaturas baixas nem as durações dos ciclos.

## Leitura, tendência e quantização

A biblioteca Dallas lê aproximadamente a cada segundo, em 12 bits
(0,0625 °C). O adaptativo coleta a cada segundo e usa a mediana de cada bloco
de 15 s. Nove pontos, separados por aproximadamente 15 s, formam a janela de
dois minutos da regressão linear. Ela é atualizada a cada bloco e descartada
na troca do estado efetivo das saídas (frio, calor, ambas desligadas).

A tendência só é confirmada quando a variação ajustada na janela corresponde
a pelo menos um degrau, e a diferença entre as médias dos três pontos finais
e iniciais confirma o sinal, com pelo menos meio degrau. Alternância de um
degrau sem tendência persistente fica como **unknown**, não como parada física
comprovada. Até completar a janela, a tendência também é desconhecida. Os
limiares e prazos originais não esperam por essa confirmação para desligar.

A velocidade é observada em todos os estados. Ela pode ser negativa com calor
ligado ou positiva com frio ligado. Representa a resposta conjunta de ambiente,
líquido, fermentação e equipamento; não identifica suas contribuições isoladas.

## Aprendizado por eventos

O controlador observa os níveis efetivos dos pinos de saída. A leitura existente
no instante observado da transição ancora cada evento, em vez da primeira linha
CHILL/IDLE do log de 30–60 s. As medianas seguintes fornecem extremos robustos.

- **Subida:** observa o pico após uma nova demanda, até dois blocos seguidos pelo
  menos um degrau abaixo dele. Média móvel exponencial de peso 0,5, com variação
  máxima de um degrau por atualização e compensação limitada a 0,15 °C. Só aplica
  a antecipação de partida após três observações completas e duas positivas
  consecutivas. Retomadas de uma pausa temporizada não treinam esse valor.
- **Queda:** depois de um pulso completo observado, acompanha o vale até dois
  blocos seguidos um degrau acima. Peso 0,5, atualização limitada a ±0,1 °C por
  observação; são aceitas respostas entre zero e 0,6 °C. Uma nova ação antes da
  recuperação fornece apenas um limite inferior: nos primeiros 15 min ele pode
  aumentar a estimativa, nunca diminuí-la como se fosse resposta completa.
  Ao completar 30 min sem recuperação, descarta-se a observação; uma queda
  ambiental indefinida não vira uma inércia crescente do motor.
- **Centro:** integra temperatura no tempo, a cada segundo. De uma nova demanda
  de frio à seguinte, havendo pelo menos dez minutos e nenhum aquecimento,
  corrige o centro por 25% de `alvo − média`, limitado a ±0,05 °C por ciclo e
  ±0,2 °C acumulados. Pausas intermediárias não são ciclos completos.

Os ganhos e limites são valores iniciais conservadores da implementação,
testados em cenários do controlador. A previsão histórica não é uma validação
em malha fechada desses ganhos. Quando o centro encontra os limites originais,
a correção fica limitada; não autoriza resfriamento adicional para eliminar o
erro restante.

## Mudança de alvo e situações sem aprendizado

Qualquer mudança efetiva do alvo apaga subida, queda, centro, contagens e janela
de velocidade. Não há transferência parcial entre 19 °C e 13 °C. Durante rampas
ou aproximação ao novo alvo são usados os critérios originais. O modelo fica
apto ao entrar na faixa de ±0,3 °C, mas não aprende a cauda de um pulso iniciado
fora dela: espera transições completas em regulação. A oscilação normal ao
redor de um alvo constante não apaga o aprendizado.

O aprendizado é RAM-only: reinício, falha de sensor, salto superior a 0,5 °C
entre observações ou interrupção do acompanhamento superior a cinco segundos
reiniciam a confiança. Modo Off, transferência, rampas e overrides das saídas
suspendem o recurso e descartam observações. Retomar o automático começa pela
regra anterior. Fermenting, Conditioning e Manual control sem overrides podem
usar o adaptativo. Não modifica pressão, headspace, CO2 ou SG.

## Evitar a alternância frio/calor

Depois de desligar o frio, além da regra original de descanso e tolerância do
aquecedor, aplica-se uma espera de recuperação de no mínimo 20 min. Ela pode
usar o tempo aprendido da recuperação mais dois minutos, limitada a uma hora.
Uma observação de vale ainda em andamento pode estender a espera até 30 min.
Uma tendência positiva confirmada, a até 0,6 °C abaixo do alvo, permite aguardar
a recuperação natural até o limite de uma hora.

Um desvio maior que 0,9 °C abaixo do alvo libera essa espera adaptativa; as
regras originais de aquecimento e descanso ainda precisam permitir aquecer.
Após uma hora a espera adaptativa sempre termina, mesmo sem tendência confiável.
Assim, ambiente frio persistente pode exigir calor. Se ele ligar, a observação
do vale é interrompida e não usa a ação do aquecedor como recuperação natural.

Desabilitar a opção volta aos limiares originais sem alterar o estado ou os
timers do ciclo. A espera de recuperação já iniciada é preservada, com limite
de tempo e liberação por desvio, para a desativação não provocar calor imediato
sobre frio residual. A janela de tendência é descartada na desativação. O
controle de corte do aquecedor continua sendo o original; não foi implementado
aprendizado de inércia do aquecedor.

## Persistência, diagnóstico e testes

Somente a opção `FMTData.adaptiveTemperature` é persistida: chave booleana
`adaptiveTemp` em `pvt_settings`, ausente = false. Backup JSON inclui
`adaptiveTemperature`; arquivos antigos mantêm a opção atual. Não muda schema,
partições ou formato de arquivo da persist. Estimativas não são gravadas na NVS
a cada ciclo.

O diagnóstico separado `/temperaturestatus` mantém o orçamento de `/getstatus`
e o esquema dos logs. Valores **unknown** de velocidade são diagnósticos sem
autorização de tendência para o controle. Os ganhos são conferíveis em
`include/AdaptiveTemperature.h`.

Teste do mesmo C++ usado no firmware: `tests/adaptive_temperature_test.hpp`.
É `.hpp` porque `src_dir=.` do PlatformIO não deve incorporar o main de teste
ao firmware. Compilar com C++11 ou superior, por exemplo:

```text
c++ -std=c++11 -x c++ tests/adaptive_temperature_test.hpp -o adaptive-test
./adaptive-test
```

O teste verifica limites originais, aprendizado e centro, mudanças de alvo,
suspensão, oscilação de um degrau, tendência em todos os estados, dados
interrompidos, recuperação com saída do modo adaptativo, falhas e rollover de
millis. Compilar o firmware com `pio run -e Povoto`; nenhuma gravação em placa
é parte dessa verificação.
