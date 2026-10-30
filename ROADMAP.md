# Roadmap

<!-- project-docs: só pendências; aprovado é apagado. Ordem = prioridade. Próximo ID: R-035 -->

## Em teste

### R-029 Metades do Joy-Con 2 registradas como dispositivos de hardware (Renan)
1. Conectar uma metade de Joy-Con 2 por Bluetooth: aparece no log a linha "RegisterDevice: ... HardwareDeviceIdentifier=JoyCon2Left" (ou Right), e Project Settings > Input lista JoyCon2Left e JoyCon2Right entre os dispositivos de hardware. Eles usam a máscara do Pro Controller 2 (9477: sem Lights), por serem da mesma família; o Renan deve dizer se os LEDs de jogador deveriam contar como Lights.

### R-002 Por Bluetooth, um Switch 2 pareado com outro aparelho é deixado em paz
1. O Pro Controller 2, já pareado com este PC: desligar e apertar um botão: ele reconecta, como antes.
2. Parear com outro aparelho (um console Switch 2, se houver um à mão) e apertar um botão perto do PC: ele não conecta, e o log diz uma única vez "paired with another device ... Hold SYNC on it". Segurar o SYNC: ele conecta e pareia com o PC.
3. Um controle em modo SYNC que nunca foi pareado conecta e pareia, como antes.
O passo 2 precisa de um segundo aparelho; sem ele, os passos 1 e 3 ainda confirmam que nada quebrou.

### R-032 O magnetômetro do Switch 2 pelo cabo
1. Pro Controller 2 no cabo: o quadro Magnetometer do painel da demo se mexe quando o controle gira (antes ficava em zero no cabo).
2. Em repouso, ZL e ZR aparecem soltos -- conferir no espelho do controle. Se algum aparecer apertado, é a falha de que o item avisa: dizer qual.
3. Depois, atualizar `docs/hardware.md` e `docs/switch2.md`, que ainda dizem "Bluetooth only".

### R-011 Fechar o editor com um Switch 2 anunciando nunca dá crash
1. Segurar o SYNC do Pro Controller 2 para ele ficar anunciando, abrir o editor e fechar assim que terminar de carregar. Repetir algumas vezes: ele sempre fecha limpo.

### R-008 Um controle caindo enquanto o editor fecha não dá crash
1. Com um controle conectado, fechar o editor e puxar o cabo (ou desligar o controle) no mesmo instante. Repetir várias vezes: o editor sempre fecha limpo. Vale pedir ao Renan, com o depurador.

### R-010 Desligar um Switch 2 no Bluetooth nunca congela o jogo
1. Com o Pro Controller 2 no Bluetooth e outro controle em uso, ficar mexendo um analógico do outro e desligar o Switch 2: o outro controle nunca para de responder. Repetir algumas vezes.

### R-009 Calibração dos analógicos do Switch 2 conferida do mesmo jeito no cabo e no Bluetooth
1. Pro Controller 2 no cabo, depois no Bluetooth, analógicos em repouso: os dois aparecem centrados no espelho do controle, e a linha de log "SW2 left/right stick cal" mostra o mesmo centro nas duas conexões.

### R-003 Uma conexão Bluetooth do Switch 2 que falha não deixa resto
1. Conectar o Pro Controller 2 por Bluetooth, desligar, conectar de novo -- várias vezes na mesma sessão do editor: toda conexão dá certo.
2. Segurar o SYNC e desligar o controle logo que as luzes começarem a correr, para a tentativa falhar; depois conectar normalmente: dá certo sem reiniciar o editor.

### R-005 Um Switch 2 reconecta por Bluetooth logo depois de sair do cabo
1. Ligar o cabo do Pro Controller 2 e puxar, depois acordar o controle com um botão: ele reconecta por Bluetooth. Repetir algumas vezes.

### R-012 Um comando Bluetooth recusado pelo Switch 2 falha na hora
1. Conectar o Pro Controller 2 por Bluetooth e ler o log: as linhas de inicialização saem rápido, nenhuma espera cerca de um segundo antes de "was not acknowledged", e o controle responde em até uns dois segundos depois de conectar. (Um Joy-Con 2 também, com o Renan.)

### R-006 Um Joy-Con no modo 0x3F é recuperado algumas vezes, não a cada relatório
1. Quatro Joy-Cons no Bluetooth, jogar alguns minutos usando o giroscópio: o giroscópio continua funcionando, e se o log mostrar "0x3F mode reset detected", vêm depois no máximo duas linhas "still in 0x3F" por episódio.
2. Desligar e ligar um Joy-Con com o jogo rodando: ele reconecta com giroscópio e não volta a desconectar sozinho.

### R-007 A inicialização do Switch 2 roda uma vez por conexão
1. Pro Controller 2 no cabo, depois no Bluetooth: jogar alguns minutos. Nenhum travamento no input, e o log mostra "Running Switch 2 ... init sequence" uma vez por conexão.
2. Com o editor aberto, jogar em Standalone com o controle no cabo: vibração e luzes de jogador funcionam na janela do Standalone.

### R-015 O movimento do Joy-Con volta sozinho quando o sensor começa morto
1. Conectar quatro Joy-Cons por Bluetooth, um depois do outro, e abrir o log (Window > Output Log): todo Joy-Con mostra o giroscópio se mexendo no espelho do controle em poucos segundos.
2. Se algum ficar sem giroscópio: o log diz "toggling the IMU enable in-stream" e depois "resumed IMU samples" para aquele controle, e o giroscópio dele começa a se mexer.

### R-016 Um controle passa entre cabo e Bluetooth sem desconectar
1. Com um DualShock 4 no Bluetooth, ligar o cabo: ele continua funcionando, com o mesmo número de jogador.
2. Puxar o cabo: ele volta para o Bluetooth e continua funcionando, com o mesmo número de jogador.
3. Ligar e puxar o cabo dez vezes, rápido: o editor nunca congela e o controle nunca precisa ser desligado e ligado.

### R-017 Calibração do Joy-Con 2 lida a cada conexão (Renan)
1. Conectar cada metade do Joy-Con 2 por Bluetooth dez vezes: toda vez o log mostra uma linha de calibração do analógico, e o analógico chega à borda do alcance no espelho do controle.

## Em andamento

## A fazer

### R-001 [ajuste] Um só sinal de Y do analógico por camada, em todas as famílias de controle
Ajusta: a normalização dos analógicos (`docs/architecture.md` > *Where to change what*).
Esperado: o estado de baixo nível do JSL (`simple_state`, os getters diretos e os nodes de Blueprint que o leem) informa o Y do analógico direito com o mesmo sinal do esquerdo em toda família, e os comentários dizem o que o código faz de fato. O `FJoyShockInterface` então apresenta os analógicos sem correções por família.
Fatos de partida:
- Pro Controller do Switch 2 por USB: no relatório cru, os dois analógicos crescem para cima; nenhum eixo precisa ser negado. Medido em hardware com o Stick Overlay do FofuxoJoySense4Unity (o F-006 dele), 2026-10-08.
- `parse_switch2_report` (`InputHelpers.cpp`, o bloco `bWhole`) nega o Y direito "para fazer cima positivo nos dois analógicos". Pelo fato acima, faz o contrário: depois dele, o analógico direito lê cima como negativo.
- `FJoyShockInterface` (`JoyShockInterface.cpp`, fim de `PresentSticks`) nega o Y direito de novo para todo controle que não é Sony (`bSonyPad ? State.stickRY : -State.stickRY`). As duas negações se cancelam, e por isso a Unreal vê o sinal certo. O próprio comentário diz que a camada de baixo nível discorda entre a Sony e as outras famílias.
- O ramo do Pro Controller do Switch 1 (`InputHelpers.cpp`, `left_right == 3`) acrescenta a mesma negação, com o mesmo raciocínio; o Joy-Con direito é corrigido uma vez, na interface. Conferir os dois em hardware antes de mexer.
Antes de mudar: tudo o que lê o estado de baixo nível diretamente (`JoyShockBlueprintLibrary.cpp` monta `RightStick` a partir de `stickRY`) vai inverter o sinal. Para verificar, precisa de um Pro do Switch 2, um Pro do Switch 1, um Joy-Con direito e um controle Sony, testados em Standalone.

### R-018 [ajuste] O widget de nome do controle da demo acompanha as trocas de controle
Ajusta: `WBP_ControllerName` (demo, `Content/`).
Esperado: em Standalone, o widget de cada personagem mostra o nome do controle que o comanda, inclusive o primeiro personagem e os controles que conectam depois.
Fatos de partida (diagnosticado em 2026-07-23):
- O widget pergunta uma única vez, no Event Construct, quais controles pertencem ao jogador dele. Em Standalone, o primeiro personagem é possuído antes de a enumeração achar qualquer controle, então ele não recebe nenhum e nunca mais pergunta. O PIE esconde isso porque o processo do editor enumerou muito antes.
- Correção receitada, não aplicada: o widget assina o Listen For Controller Changes do JoyShock Subsystem (e, opcionalmente, o Listen For Joy-Con Pairing Changes) e refaz a busca do nome a cada evento. O usuário edita o Blueprint.
Testes:
- Em Standalone com dois controles: os dois widgets mostram o nome do seu controle; desligar e ligar um controle: o widget dele acompanha.

### R-019 [ajuste] A demo cria um jogador local por controle, nada além
Ajusta: o inicializador de jogadores da demo.
Esperado: com um controle conectado há um jogador local, e o controle comanda o jogador 0.
Fatos de partida (log de 2026-07-22, não corrigido):
- Com um controle, o inicializador criou dois jogadores locais e pôs o controle no índice 1, deixando o jogador 0 sem controle.
- Suspeita: o ramo "o primeiro controle reaproveita o jogador 0, os outros criam um novo" não funciona, ou o handler roda duas vezes (um ForEach antigo mais o Listen For Controllers). O índice de destino deveria vir de um contador idempotente por Connection Id, não de uma contagem ao vivo.
- Provado por medição, não reinvestigar: o plugin despacha corretamente o input do segundo controle (F-004).
Testes:
- Em Standalone, um controle: um personagem, comandado por ele. Conectar um segundo: aparece um segundo personagem, comandado pelo segundo controle.

### R-020 [pesquisa] Os controles do plugin são limpos entre sessões de PIE?
Esperado: responder se `_joyshocks` e as vagas de jogador sobrevivem de uma sessão de Play In Editor para a seguinte, e se isso importa. Vale como resposta: um log de duas sessões de PIE seguidas com os mesmos controles, mostrando os ids e as vagas de jogador deles.
Fatos de partida: `JslDisconnectAndDisposeAll` existe, mas talvez não esteja ligado ao fim do PIE (anotado em 2026-07-19).

### R-021 [feature] Acessório de alça do Joy-Con na demo
Esperado: a demo tem um modelo da alça que desliza no trilho do Joy-Con, para jogar de lado com uma mão.
Como: o usuário modela.

### R-022 [feature] Modelo do Joy-Con Grip com os dois Joy-Cons encaixados
Esperado: o modelo do Joy-Con Grip da demo mostra os dois Joy-Cons encaixados nele, em vez do grip sozinho.
Como: o usuário modela.

### R-023 [feature] Um pawn da demo comandado por vários controles ao mesmo tempo
Esperado: a demo tem um pawn que um ou mais controles podem comandar ao mesmo tempo.
Como:
- Montado no jogo, não no plugin: um pawn lê vários Connection Ids e é alimentado por todos. A junção de Joy-Cons do plugin já funde uma metade esquerda e uma direita numa vaga de jogador (escolha do agente).
Em aberto: ou o plugin deveria generalizar a junção para qualquer conjunto de controles?

### R-024 [feature] Botões no HUD da demo para criar, apagar e reatribuir um pawn
Esperado: o HUD da demo tem botões para criar um pawn jogável, apagá-lo e atribuí-lo a outro controle.
Como:
- Reatribuir mantém o roteamento de jogadores do plugin e remapeia a identidade no jogo ("o jogador 1 é o chef vermelho"), como faz a maioria dos party games. O plugin não tem node para prender um controle numa vaga de jogador escolhida: é o índice de jogador dele que roteia o input, então o `Create Player` tem que receber esse índice (escolha do agente).
- Segue a arquitetura da demo do D-004.
Depende de: R-023

### R-025 [feature] Os materiais de LED do Joy-Con acompanham o controle
Esperado: os LEDs de jogador nos modelos de controle da demo acendem exatamente como os do controle físico.
Como: ler `FJSL4UControllerInfo.PlayerLedNumber`, que é uma posição (1-4).
Em aberto: a reformulação da demo de 2026-07-19 já espelha o LED de pareamento no modelo -- sobrou alguma coisa deste item?

### R-026 [feature] Modelos do Pro Controller e do Xbox no espelho de controles da demo
Esperado: o espelho de controles da demo tem um modelo para o Pro Controller do Nintendo Switch, o Pro Controller do Nintendo Switch 2 e um controle Xbox; hoje só o par de Joy-Cons, o Joy-Con Grip, o DualShock 4 e o DualSense estão modelados, e os Joy-Con 2 pegam emprestados os modelos do Joy-Con do Switch 1.
Como: o usuário modela.
Origem: README *Planned future updates*, trazido para cá pelo R-013.

### R-027 [feature] Porcentagem de bateria do Switch 2 por curva de carga
Esperado: o Battery Percent de um controle do Switch 2 segue a curva de descarga de uma célula de lítio em vez de uma reta entre 3,0 V e 4,2 V, para o número valer a pena ser mostrado e não só uma barra.
Fatos de partida: o controle informa a tensão da célula e mais nada, e a tensão cai sob carga -- lê baixo no meio da vibração.
Origem: README *Planned future updates*, trazido para cá pelo R-013.

### R-028 [ajuste] Um controle que chega tem que comandar o seu personagem
Ajusta: o inicializador de jogadores da demo.
Esperado: todo controle que conecta comanda o seu personagem, sem parar e dar play de novo.
Fatos de partida:
- Relatado uma vez com um Joy-Con 2: o espelho apareceu e as leituras de movimento funcionaram, mas o pawn não se mexeu. Parar e dar play de novo resolveu, o que não é opção num build empacotado.
- Não diagnosticado. Suspeita no caminho de atribuição da demo, não no plugin, já que o controle estava visivelmente conectado e mandando dados. Pode ter a mesma causa do R-019.
Origem: README *Planned future updates*, trazido para cá pelo R-013.

### R-031 [ajuste] Controles do Switch 2 informam que estão carregando
Ajusta: o informe de bateria (`docs/controllers.md` > *Battery*).
Esperado: o Is Charging de um controle do Switch 2 vira verdadeiro quando o cabo entra e falso quando sai, e o Wait For Battery Changes dispara o On Charging Changed para ele.
Fatos de partida: F-009. `parse_switch2_report` (`InputHelpers.cpp`) preenche tensão, porcentagem e nível, mas nunca `battery_charging`; ainda não se sabe onde o relatório traz o carregamento (o Switch2Connect do F-006 é a referência a conferir).
- O Switch2Connect lê os dois bytes depois da tensão (packet[34..35] aqui) como `battery_current`, sem sinal, dividido por 100, e nunca usa. Desde 2026-10-29 o plugin os lê com sinal em `Battery Current` no Get Switch 2 Sensors (commit "read the Switch 2 battery current"); o `battery_charging` ainda não é derivado disso.
- Próximo: mostrar o Battery Current no painel da demo e ler com o cabo ligado e desligado. O NodeScribe não consegue inserir um node numa cadeia de execução existente, e o `replace` recusa neste EventGraph (a leitura dele perde duas reconvergências), então a linha precisa de uma edição à mão no editor ou de uma mudança no NodeScribe. Depois, definir o `battery_charging` pelo sinal que aparecer durante a carga.
Testes: o usuário, com o Pro Controller 2 no cabo e no Bluetooth.

### R-033 [ajuste] Um Switch 2 no Bluetooth chega a um jogo Standalone aberto pelo editor
Ajusta: `Switch2Bluetooth.cpp` / `discover_bluetooth_switch2`, junto ao empréstimo de USB que o editor e o Standalone já passam um para o outro.
Esperado: com o editor segurando um controle do Switch 2 por Bluetooth, jogar em Standalone entrega o controle ao jogo, e ele volta para o editor quando o jogo fecha.
Fatos de partida: F-010.
Testes: o usuário, com o Pro Controller 2 no Bluetooth, em Standalone aberto pelo editor.

### R-034 [pesquisa] O Pro Controller 2 informa temperatura, e onde?
Esperado: se o relatório do Pro Controller 2 traz uma temperatura, em quais bytes e com qual escala; a resposta vale quando a leitura sobe enquanto o controle fica na mão por alguns minutos.
Fatos de partida: F-009. O parser lê os bytes 47-48 como `25 + raw / 127`; no Pro 2 o valor cru fica em torno de 9-15 e só oscila.
