# Roadmap

<!-- project-docs: só pendências; aprovado é apagado. Ordem = prioridade. Próximo ID: R-051 -->

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

### R-001 [ajuste] Uma tabela por controle para o sinal e a pegada dos analógicos (Renan)
Ajusta: os sinais dos analógicos, que estavam espalhados entre os leitores de `InputHelpers.cpp` e o `PresentSticks` de `JoyShockInterface.cpp`; agora ficam em `JoyShockStickTable.cpp` (`docs/architecture.md` > *Where to change what*).
Esperado: todo controle entrega os dois analógicos à Unreal com para cima = +1 e direita = +1, a convenção do XInput, e os nodes `JSL4U Get Left/Right Stick` (e o `JSL4U Get Controller State`) dão os mesmos valores, inclusive o Joy-Con deitado girado para a pegada. O sinal de cada eixo de cada controle está escrito num lugar só, e mudar a linha de um controle não mexe em nenhum outro.
Como (escolha do usuário, 2026-11-06):
- Três passos, sempre nesta ordem: o leitor do protocolo deixa o número no sinal que o hardware manda, só com a calibração; a linha do controle na tabela dá o sinal de cada eixo (LX, LY, RX, RY); se é um Joy-Con deitado, o vetor já corrigido gira pelo sentido da linha (L anti-horário, R horário).
- Uma linha por controle que o plugin distingue; o DualSense Edge se apresenta como DualSense e usa a linha dele. O gyro do Joy-Con deitado (`GetJoyConGripUndoRotation`) lê o sentido da mesma tabela.
- Cada linha anota quem mediu e com qual controle, ou que é derivada do código anterior e falta medir.
- Os nodes devolvem para cima = +1 nos dois analógicos, e não o −1 que o jogo vê no direito (escolha do usuário, 2026-11-06): é o que o plugin manda, antes da inversão da Unreal.
Fatos:
- A Unreal inverte o `Gamepad_RightY` de todo controle, Xbox incluído, antes de entregá-lo ao jogo: `FSceneViewport::OnAnalogValueChanged` (`SceneViewport.cpp`, linha 1352 na 5.8). Por isso a leitura do `LV_JoyShockGyroAim` mostra −1 no direito para cima, e é o certo. Não houve regressão: o `21c383a` estava certo, e a hipótese de "Y direito negado duas vezes" era a soma de duas negações que se anulavam de propósito.
- Medido pelo Antônio em 2026-11-06 nessa leitura, em Standalone, empurrando para cima e para a direita: Pro Controller 2 (cabo e Bluetooth), DualShock 4 e par de Joy-Cons dão esquerdo +1 / +1 e direito −1 / +1; Joy-Con L e R deitados dão +1 / +1 como analógico esquerdo. Ou seja, o input da Unreal já estava certo em todos.
- O que estava errado eram os nodes: devolviam o `simple_state` cru, sem o giro do Joy-Con deitado e com o sinal do vertical direito diferente por família.
- Nos Joy-Cons os valores ficaram entre +0,7 e +0,9 em vez de 1: curso curto, fora do sinal, a investigar à parte (calibração? zona morta de 0,25 que a Unreal aplica no `Get Input Analog Key State`?).
- O pawn do `LV_JoyShockGyroAim` (R-049) invertia o vertical dos dois analógicos; o certo é inverter só um, porque a Unreal já inverte o direito.
Testes: no `LV_JoyShockGyroAim`, em Standalone, empurrar cada analógico para cima e para a direita: a seção de cima dá esquerdo +1 / +1 e direito −1 / +1, e a de baixo (os nodes) +1 em tudo, inclusive no Joy-Con deitado. A câmera sobe com os dois analógicos para cima. Aqui: Pro Controller 2 (USB e Bluetooth), Joy-Con L e R deitados e juntos, DualShock 4. Com o Renan: Pro do Switch 1, Joy-Con 2, DualSense, DualSense Edge, e o gyro do Joy-Con deitado.
Aprovado aqui em 2026-11-06, em Standalone: Pro Controller 2, DualShock 4, Joy-Cons deitados e em par, nas duas seções da leitura e na câmera. Falta só o que é do Renan.

### R-020 [pesquisa] Os controles do plugin são limpos entre sessões de PIE?
Esperado: responder se `_joyshocks` e as vagas de jogador sobrevivem de uma sessão de Play In Editor para a seguinte, e se isso importa. Vale como resposta: um log de duas sessões de PIE seguidas com os mesmos controles, mostrando os ids e as vagas de jogador deles.
Fatos de partida: `JslDisconnectAndDisposeAll` existe, mas talvez não esteja ligado ao fim do PIE (anotado em 2026-07-19).
Resposta (2026-11-06): sim, e importa.
- Pelo código: o módulo, os controles (`_joyshocks`), as vagas (`PlayerSlotByPrimary`) e os pares de Joy-Con vivem enquanto o editor estiver aberto; no fim de um Play só o `UJoyShockSubsystem` sai, desligando os eventos e parando a vibração. Nada chama `JslDisconnectAndDisposeAll` no fim do PIE, e deve continuar assim: refazer a conexão a cada Play mexeria nos links Bluetooth (invariante do CLAUDE.md).
- Medido pelo Antônio, PIE seguido de PIE: depois do `LV_JoyShockGyroAim`, que manda os controles não escolhidos para o jogador 1, o `LV_JoyShockLocalMultiplayer` começou com todos no jogador 1 (luz 2) menos o escolhido, e com o par de Joy-Cons ainda junto. O log de início de sessão (`UJoyShockSubsystem: listening for device changes`) agora lista cada controle com id, tipo, jogador e par, e mostrou `[#3 Joy-Con (L), player 1] [#1 Joy-Con (R), player 0] [#4 Joy-Con (R), player 1, joined to #5] ...`, onde as sessões anteriores davam vagas automáticas 0 a 4.
- Importa porque um jogo empacotado sempre começa do zero: o segundo Play no editor herda as escolhas do primeiro, até de outro level, e não reproduz o jogo.
Decidido (escolha do usuário, 2026-11-06): no fim de cada jogo (`UJoyShockSubsystem::Deinitialize`), as vagas voltam ao automático com `ResetPlayerAssignmentsToAutomatic`; os controles continuam conectados e os pares de Joy-Con continuam juntos.
Também decidido (pedido do usuário, 2026-11-06): o `LV_JoyShockGyroAim` não reatribui mais controles; passa o pawn para o jogador do controle escolhido, e as luzes ficam como estão. Aprovado em hardware.
Falta: ver a volta ao automático acontecer. Só compilou: nenhum level da demo reatribui controle agora. Teste: um Blueprint que chame `JSL4U Assign Controller To Player Index` (por exemplo, um controle para o jogador 2), Play, parar, e ver as luzes e o log de início do Play seguinte voltarem às vagas automáticas, com o par de Joy-Cons ainda junto.

## Em andamento

## A fazer

### R-040 [feature] Uma opção do editor para não procurar nem conectar controles
Esperado: uma opção que só vale no editor e desliga a busca e a conexão de controles do plugin, para o controle poder ser usado em outro programa com o editor aberto.
Como:
- Ao desligar, os controles já conectados são soltos, inclusive o link Bluetooth do Switch 2 (escolha do agente)
- Jogo empacotado e Standalone continuam procurando controles normalmente (escolha do agente)
- A opção fica guardada por usuário, não no projeto (escolha do agente)
Fatos de partida: o JoySense4Unity ganhou a mesma opção, e foi ela que acabou com a Unity aberta atrapalhando a Unreal. Um jogo com o plugin já recebe os controles Bluetooth do editor sozinho (D-008); esta opção é para um programa que não seja o plugin, como a Unity.
Em aberto: onde a opção fica (Editor Preferences ou um botão na barra do editor) e se vale na hora ou só ao reabrir o editor.

### R-048 [pesquisa] O Pro Controller 2 pode manter o Bluetooth com o cabo ligado?
Esperado: saber se dá para o Pro Controller 2 voltar ao Bluetooth sozinho ao tirar o cabo, como o DualShock 4, sem apertar botão. Conta como resposta um teste com o Pro Controller 2 do usuário que mostre quem derruba o Bluetooth quando o cabo entra (o próprio controle ou um comando da preparação USB do plugin) e se algum comando ou ordem de preparação o mantém.
Fatos de partida: em 2026-11-06 o Bluetooth caiu 13 ms depois de o plugin abrir o USB, e depois de tirar o cabo o controle não anunciou no Bluetooth em nenhum dos 10 s seguintes até um botão ser apertado. Com o R-045, apertar um botão o traz de volta como o mesmo jogador.

### R-050 [ajuste] Is Charging e a corrente da bateria do Switch 2 leem os bytes certos
Ajusta: JSL4U Get Switch 2 Sensors e Is Charging (docs/controllers.md).
Esperado: Is Charging diz se o controle está carregando, e fica falso quando a carga termina com o cabo ligado; a corrente da bateria mostra uma corrente, ou sai do node se o controle não a informa.
Fatos de partida: o plugin lê os bytes 0x21-0x22 do relatório (packet[34-35]) como corrente em centésimos de ampère, mas pelo ndeadly o 0x21 é o estado de carga (sobe até 0x34 carregando, 0x20 cheio) e a corrente fica em 0x22-0x23, só com o bit 5 (0x20) da seleção de recursos, e só nos Joy-Con 2. Bate com o medido em 2026-11-06: o "0,52 A" é 0x34 e o "0,32 A" depois que a luz apagou é 0x20; pedir o bit 0x20 no Bluetooth não fez corrente aparecer no Pro Controller 2. Hoje Is Charging continua verdadeiro com a carga completa.

### R-026 [feature] Modelos dos Pro Controllers no espelho de controles da demo
Esperado: o espelho de controles da demo tem um modelo para o Pro Controller do Nintendo Switch e o Pro Controller do Nintendo Switch 2; hoje o par de Joy-Cons, o Joy-Con Grip, o DualShock 4, o DualSense e o controle Xbox estão modelados, e os Joy-Con 2 pegam emprestados os modelos do Joy-Con do Switch 1.
Como: o usuário modela.
Origem: README *Planned future updates*, trazido para cá pelo R-013.

### R-027 [pesquisa] Como ler o nível de bateria de 0 a 9 que o próprio Switch 2 informa?
Esperado: um jeito de obter o nível de bateria de 0 a 9 que os relatórios 0x07-0x09 trazem, sem trocar o formato do relatório que o plugin usa (o 0x05) e sem mandar comando com o Bluetooth no ar; ou a conclusão de que trocar para o 0x09 vale o custo. Conta como resposta o nível lido no Pro Controller 2 batendo com a carga (cheio depois da luz de carga apagar). Respondido, o item volta a ser a feature: o Battery Percent do Switch 2 seguindo a carga de verdade em vez da reta entre 3,0 V e 4,2 V.
Fatos de partida (2026-11-06):
- No Pro Controller 2 do usuário, a tensão no Bluetooth foi de 3,713 V (com mais da metade, pela suspeita do usuário) a 3,728 V cheio, depois de 17 min no cabo até a luz de carga apagar. Uns 15 mV entre a metade e o cheio: uma curva genérica de lítio (cheio a 4,2 V) deu 19% com o controle quase cheio. No cabo a tensão quase não se mexe (3,73-3,75 V).
- Pela pesquisa do ndeadly (github.com/ndeadly/switch2_controller_research, `hid_reports.md`), os relatórios 0x07, 0x08 e 0x09 (Joy-Con 2 L, R e Pro Controller 2) trazem no byte 0x1 o próprio nível de bateria do controle, de 0 a 9, mais os bits de energia externa e carregando. O relatório 0x05, que o plugin usa, não traz. O SDL não lê bateria nenhuma do Switch 2.
- O Switch2Connect (github.com/TommyWabg/Switch2Connect, de onde vieram a leitura de corrente e de temperatura do plugin) lê a mesma tensão do relatório 0x05 e só mostra três ícones: alto acima de 3,25 V, médio acima de 3,125 V, baixo abaixo disso (lido por resumo do código, não linha a linha). Não decide carregando. Instalá-lo não serve de referência: lê o mesmo dado que o plugin, instala drivers e toma o controle para si.
Como: preferir o nível 0-9 do próprio controle a inventar uma curva (escolha do usuário, 2026-11-06: partir do que outros já descobriram). Pistas: o comando 0x0B do ndeadly (subcomandos de tensão e estado de carga; procurar um de nível), lido uma vez na conexão; e o F-011, em que o Pro Controller 2 já chegou ao plugin mandando 0x09. O Switch2Connect diz que o 0x09 do Pro Controller 2 tem o bloco de movimento não documentado.

### R-036 [pesquisa] Um Switch 2 no Bluetooth que conecta e fica mudo, e o Windows que não solta o link
Esperado: saber se o JSL4U tem o mesmo silêncio e, se tiver, que ele se recupere sozinho e que um botão reconecte o controle depois.
Fatos de partida, vindos do Fofuxo's JoySense4Unity (F-013 de lá), no Pro Controller 2:
- Acontece ao tirar o cabo e acordar o controle por botão (duas vezes em 2026-11-01; na terceira, funcionou). A conexão abre, os comandos de setup são todos respondidos, mas a leitura da calibração dos analógicos falha e nenhum relatório de entrada chega, nem à DLL. Reenviar o setup não traz os relatórios de volta.
- O JoySense passou a tratar 3 s sem relatório (não só antes do primeiro) assim: reenvia o setup e, se continuar mudo, derruba o link para um botão reconectar.
- Derrubado o link, com a DLL sem nenhuma conexão aberta, o Windows seguiu com o controle conectado (`IsConnected` verdadeiro no nó `BTHLE\DEV_<endereço>`, lido com `Get-PnpDeviceProperty`). Conectado, o controle não anuncia, e o botão não o reconecta. Desligar e ligar o Bluetooth do Windows soltou. Quem segurava o link não se sabe; fechar o processo com o caso na mão responde se é um objeto WinRT esquecido aberto.
Verificar aqui: a sequência cabo, tirar o cabo, botão, algumas vezes; se o JSL4U detecta o silêncio; e se, ao desconectar, o nó `BTHLE\DEV_` deixa de estar conectado.
Testes: o usuário, com o Pro Controller 2 e o Steam fechado.
