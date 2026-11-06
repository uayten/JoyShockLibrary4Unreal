# Roadmap

<!-- project-docs: só pendências; aprovado é apagado. Ordem = prioridade. Próximo ID: R-048 -->

## Em teste

### R-029 Metades do Joy-Con 2 registradas como dispositivos de hardware (Renan)
1. Conectar uma metade de Joy-Con 2 por Bluetooth: aparece no log a linha "RegisterDevice: ... HardwareDeviceIdentifier=JoyCon2Left" (ou Right), e Project Settings > Input lista JoyCon2Left e JoyCon2Right entre os dispositivos de hardware. Eles usam a máscara do Pro Controller 2 (9477: sem Lights), por serem da mesma família; o Renan deve dizer se os LEDs de jogador deveriam contar como Lights.

### R-002 Por Bluetooth, um Switch 2 pareado com outro aparelho é deixado em paz (Renan)
1. Precisa do console Switch 2. Parear o controle com o console e apertar um botão perto do PC: ele não conecta, e o log diz uma única vez "paired with another device ... Hold SYNC on it". Segurar o SYNC: ele conecta e pareia com o PC.

### R-003 Uma conexão Bluetooth do Switch 2 que falha não deixa resto (Renan)
Com a mão não dá: a conexão inteira leva meio segundo (medido em 2026-11-06, quatro tentativas, todas completas antes do controle desligar). Precisa do depurador, com qualquer controle Switch 2 no Bluetooth.
1. Pôr um breakpoint em `Switch2Ble::Connect`, na linha `Connection->Service = ServicesResult.Services().GetAt(0);` (`Switch2Bluetooth.cpp`, por volta da 593), abrir o editor e segurar o SYNC do controle: o depurador para ali.
2. Com o depurador parado, desligar o controle segurando o SYNC até as luzes apagarem, esperar uns 5 segundos e continuar (F5): o Output Log mostra um aviso de falha de `LogSwitch2Bluetooth` (falta de característica, inscrição recusada ou "Bluetooth connection to ... failed") e o editor segue rodando.
3. Tirar o breakpoint, ligar o controle e segurar o SYNC de novo: o log diz "Connected to Switch 2 controller ... over Bluetooth.", o controle aparece na demo, e nada no log fala em sharing violation ou acesso negado. Sem reiniciar o editor.
4. Repetir os passos 1 a 3 com o breakpoint na linha `if (!Subscribe(Connection->InputCharacteristic))` (por volta da 733), que é outra saída de falha: mesmo resultado.

### R-012 Um comando Bluetooth recusado pelo Switch 2 falha na hora (Renan)
1. Conectar cada metade do Joy-Con 2 por Bluetooth: o controle responde em até uns dois segundos, e o log não mostra nenhuma espera de cerca de um segundo antes de "was not acknowledged".

### R-017 Calibração do Joy-Con 2 lida a cada conexão (Renan)
1. Conectar cada metade do Joy-Con 2 por Bluetooth dez vezes: toda vez o log mostra uma linha de calibração do analógico, e o analógico chega à borda do alcance no espelho do controle.

## Em andamento

## A fazer

### R-045 [ajuste] O Pro Controller 2 troca entre cabo e Bluetooth sem desconectar
Ajusta: Plugging in a cable while on Bluetooth (docs/troubleshooting.md), que hoje não vale para o Switch 2.
Esperado: os dois tipos de controle seguem o mesmo padrão: ligar e tirar o cabo de um Pro Controller 2 mantém a conexão, com o mesmo jogador e o mesmo personagem, como já acontece com o DualShock 4.
Como:
- O jogo não recebe desconexão nem conexão na troca; o Connection Id e a vaga de jogador ficam os mesmos (escolha do agente)
- Uma pausa na entrada durante a troca é aceita: o Bluetooth do Switch 2 é uma conexão nova a cada vez, e subir leva alguns segundos (escolha do agente)
- Tirado o cabo, se o Bluetooth não voltar em 10 segundos (controle desligado, longe do PC), aí sim o jogo recebe a desconexão (escolha do agente)
Fatos de partida: em 2026-11-06, ligar o cabo deu "controle 1 desconectou" e "controle 2 conectou", com 3,5 s sem entrada; tirar deu o mesmo ao contrário, com 4,6 s. A vaga de jogador (jogador 2) se manteve, porque a identidade é o endereço do controle. O DualShock 4 troca sem pausa porque o cabo não derruba o Bluetooth dele.

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

### R-034 [pesquisa] O Pro Controller 2 informa temperatura, e onde?
Esperado: se o relatório do Pro Controller 2 traz uma temperatura, em quais bytes e com qual escala; a resposta vale quando a leitura sobe enquanto o controle fica na mão por alguns minutos.
Fatos de partida: F-009. O parser lê os bytes 47-48 como `25 + raw / 127`; no Pro 2 o valor cru fica em torno de 9-15 e só oscila.

### R-036 [pesquisa] Um Switch 2 no Bluetooth que conecta e fica mudo, e o Windows que não solta o link
Esperado: saber se o JSL4U tem o mesmo silêncio e, se tiver, que ele se recupere sozinho e que um botão reconecte o controle depois.
Fatos de partida, vindos do Fofuxo's JoySense4Unity (F-013 de lá), no Pro Controller 2:
- Acontece ao tirar o cabo e acordar o controle por botão (duas vezes em 2026-11-01; na terceira, funcionou). A conexão abre, os comandos de setup são todos respondidos, mas a leitura da calibração dos analógicos falha e nenhum relatório de entrada chega, nem à DLL. Reenviar o setup não traz os relatórios de volta.
- O JoySense passou a tratar 3 s sem relatório (não só antes do primeiro) assim: reenvia o setup e, se continuar mudo, derruba o link para um botão reconectar.
- Derrubado o link, com a DLL sem nenhuma conexão aberta, o Windows seguiu com o controle conectado (`IsConnected` verdadeiro no nó `BTHLE\DEV_<endereço>`, lido com `Get-PnpDeviceProperty`). Conectado, o controle não anuncia, e o botão não o reconecta. Desligar e ligar o Bluetooth do Windows soltou. Quem segurava o link não se sabe; fechar o processo com o caso na mão responde se é um objeto WinRT esquecido aberto.
Verificar aqui: a sequência cabo, tirar o cabo, botão, algumas vezes; se o JSL4U detecta o silêncio; e se, ao desconectar, o nó `BTHLE\DEV_` deixa de estar conectado.
Testes: o usuário, com o Pro Controller 2 e o Steam fechado.

### R-037 [pesquisa] Quantos relatórios por segundo o Switch 2 entrega no Bluetooth, e se o gyro anda liso
Esperado: a taxa medida no JSL4U, e nenhum degrau no giro da câmera pelo Bluetooth.
Fatos de partida, do JoySense:
- Lá foram medidos uns 70 relatórios por segundo em 2026-11-01 de manhã e uns 225 a 240 à tarde, com a mesma DLL (que já pede `ThroughputOptimized`); não se sabe o que mudou. A DLL de diagnóstico tinha visto uns 250.
- Com menos relatórios que frames, um giro somado por relatório deixa frames com giro zero e outros com o dobro, e a câmera anda em degraus. O JoySense resolveu segurando a velocidade da última amostra até a próxima (no máximo 50 ms) e dando a cada frame o trecho de tempo que ele cobre.
Verificar aqui: a taxa no Bluetooth; e se algum caminho do JSL4U entrega giro acumulado por frame (um valor em graus/s lido a cada frame, como o Rotation Rate, já se comporta como a velocidade segurada).

### R-039 [ajuste] A luz de jogador do Switch 2 no cabo
Esperado: a luz de jogador acende pelo cabo como no console, venha o controle desligado ou do Bluetooth.
Fatos de partida, do JoySense: o setup do cabo não acendia a luz, e ela ficava apagada ligando direto no cabo e acesa, sobra do Bluetooth, vindo dele; o controle guarda a luz que recebeu. O comando `09:07` pelo cabo funciona com o controle já ligado (as quatro luzes acenderam), mas enviado no fim da lista do setup não acendeu: o primeiro comando depois do último da lista (`03:0D`) fica sem resposta e não faz nada, e o seguinte é aceito, mesmo sem espera. O JoySense passou a mandar a luz depois da lista e repetir até a resposta chegar. O JSL4U manda a luz pelo polling, depois do setup e com nova tentativa a cada 2 s, então provavelmente já está certo.
Verificar aqui: plugar o cabo com o controle desligado e ver a luz acender; e vindo do Bluetooth. E se algum outro comando enviado logo depois do setup do cabo (vibração, luz) se perde do mesmo jeito.

### R-040 [feature] Uma opção do editor para não procurar nem conectar controles
Esperado: uma opção que só vale no editor e desliga a busca e a conexão de controles do plugin, para o controle poder ser usado em outro programa com o editor aberto.
Como:
- Ao desligar, os controles já conectados são soltos, inclusive o link Bluetooth do Switch 2 (escolha do agente)
- Jogo empacotado e Standalone continuam procurando controles normalmente (escolha do agente)
- A opção fica guardada por usuário, não no projeto (escolha do agente)
Fatos de partida: o JoySense4Unity ganhou a mesma opção, e foi ela que acabou com a Unity aberta atrapalhando a Unreal. Um jogo com o plugin já recebe os controles Bluetooth do editor sozinho (D-008); esta opção é para um programa que não seja o plugin, como a Unity.
Em aberto: onde a opção fica (Editor Preferences ou um botão na barra do editor) e se vale na hora ou só ao reabrir o editor.

### R-042 [feature] A demo mostra quantos relatórios por segundo cada controle entrega
Esperado: no painel de cada controle, ver quantos dados por segundo a Unreal está recebendo dele, para comparar cabo com Bluetooth e um Bluetooth bom com um ruim.
Como:
- Contados no plugin, no ponto em que cada relatório chega, e não por frame do jogo, para a taxa não depender do FPS (escolha do agente)
- Um node `JSL4U*` que devolve a taxa de um Connection Id, e uma linha no painel da demo com o valor atualizado uma vez por segundo (escolha do agente)
Fatos de partida: é a medida de que o R-037 precisa (lá, de 70 a 240 relatórios por segundo no Bluetooth do Switch 2, conforme o dia).

### R-043 [feature] Fechar a Unreal solta o controle
Esperado: ao fechar a Unreal, o controle volta ao estado de quem não está conectado a nada: a luz de jogador apaga e, no cabo, ele mostra só que está carregando.
Como:
- Na saída, cada thread de leitura manda uma última vez "luz de jogador apagada", que é só escrita e não mexe na conexão; no Bluetooth, depois fecha o link, o que já acontece hoje (escolha do agente)
Fatos de partida: em 2026-11-05, com o Pro Controller 2 no cabo, depois de fechar o editor a luz de jogador 1 continuou acesa e a de carregamento ficou apagada. O controle guarda a última luz que recebeu (R-039).
Em aberto: se a luz de carregamento apagada era bateria cheia e não o controle preso ao editor; o Is Charging nunca foi medido com bateria cheia no cabo.

### R-046 [ajuste] Um controle que reconecta nasce num lugar livre da demo
Ajusta: o inicializador de jogadores da demo.
Esperado: um controle que desliga e liga de novo com o jogo rodando aparece num lugar vazio da cena, não em cima de um personagem que já está lá.
Fatos de partida: em 2026-11-06, em Standalone com quatro Joy-Cons, o Joy-Con (R) desligado e religado reapareceu sobreposto a outro controle já na cena. O plugin o reconectou em meio segundo, com giroscópio. Pode ter a mesma causa do R-019 e do R-028: a posição sairia de uma contagem ao vivo de jogadores, não de uma vaga livre.
