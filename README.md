# CorFacil

Biblioteca do Arduino para o sensor de cor **TCS34725** que separa **PRETO, BRANCO, VERDE e VERMELHO** com um comando. Feita para turmas de robótica (OBR, seguidor de linha): usar o sensor fica tão simples quanto um `digitalWrite`.

```cpp
#include <CorFacil.h>

CorFacil sensor;

void setup() {
  sensor.iniciar();
}

void loop() {
  if (sensor.lerCor() == VERDE) {
    // achou o marcador verde
  }
}
```

- **Calibração em 4 passos** (branco, preto, verde, vermelho), salva na placa: desligar não apaga.
- **A borda da linha preta não vira verde.** O código separa o brilho da cor, e a borda (meio preto, meio branco) é cinza.
- **Nunca trava o programa** esperando o sensor.
- Funciona no **Arduino Uno, Nano, Mega e ESP32**. Não precisa de nenhuma outra biblioteca.

## Instalar

1. Baixe o arquivo **`CorFacil-1.0.0.zip`** na página [Releases](https://github.com/murilo999-666/CorFacil/releases/latest).
2. Na Arduino IDE: **Sketch → Incluir Biblioteca → Adicionar biblioteca .ZIP...** e escolha o arquivo baixado.
3. Os exemplos aparecem em **Arquivo → Exemplos → CorFacil**.

Ainda não está no Gerenciador de Bibliotecas da IDE; por enquanto, a instalação é pelo `.zip`.

## Ligação

| TCS34725 | Uno / Nano | Mega | ESP32 |
|---|---|---|---|
| VIN | 5V | 5V | **3V3** |
| GND | GND | GND | GND |
| SDA | A4 | 20 | GPIO 21 |
| SCL | A5 | 21 | GPIO 22 |

O pino **LED** do módulo fica solto: assim o LED branco fica aceso o tempo todo, que é o que a biblioteca espera. No ESP32, ligue o VIN no **3V3**: o módulo puxa o SDA e o SCL para a tensão do VIN, e o pino do ESP32 aguenta só 3,6 V.

## Primeiro uso

1. Abra **Arquivo → Exemplos → CorFacil → 02_CalibrarPeloMonitor**, grave na placa e abra o Monitor Serial em **115200**.
2. Coloque o sensor **na altura em que ele vai ficar no robô** e siga o que aparece na tela:

   | Sensor em cima do | Envie | Aparece (exemplo) |
   |---|---|---|
   | branco (**sempre primeiro**) | `b` | `BRANCO ok: 45% do limite, ganho 16x (40/40 leituras)` |
   | preto (a linha) | `p` | `PRETO ok: 8.8% do branco (40/40 leituras)` |
   | verde | `v` | `VERDE ok: croma 0.188, matiz 120 (40/40 leituras)` |
   | vermelho | `r` | `VERMELHO ok: croma 0.340, matiz 7 (40/40 leituras)` |

   Cada cor leva cerca de 1 segundo. Se algo estiver errado, a mensagem diz o quê (sensor na borda, preto claro demais, cor que não parece verde...).
3. Passe o sensor sobre cada cor e confira a primeira palavra da linha.
4. Grave o programa do robô. A calibração continua guardada na placa.

## Os comandos

| Comando | O que faz |
|---|---|
| `sensor.iniciar();` | liga o sensor e carrega a calibração salva (no `setup`) |
| `sensor.lerCor()` | a cor embaixo do sensor: `PRETO`, `BRANCO`, `VERDE`, `VERMELHO` ou `INDEFINIDO` |
| `sensor.nomeCor()` | o nome da cor, para mostrar: `Serial.println(sensor.nomeCor());` |
| `sensor.calibrar(BRANCO);` | calibra a cor que está embaixo (~1 s) e grava; devolve `true` se deu certo |
| `sensor.calibrado()` | `true` se as 4 cores já estão calibradas |
| `sensor.apagarCalibracao();` | esquece a calibração (ou só uma cor: `apagarCalibracao(VERDE)`) |
| `sensor.mensagens(Serial);` | liga as mensagens de ajuda no Monitor Serial |
| `sensor.calibrarPeloMonitor();` | no `loop`: calibra pelos comandos `b`, `p`, `v` e `r` do Monitor Serial |

O que o `lerCor()` devolve:

- **`INDEFINIDO`**: tem cor, mas não é nenhuma das calibradas (azul, amarelo, laranja), ou o sensor está na borda de um marcador. No robô: ande devagar.
- **`SEM_SENSOR`**: o sensor não respondeu (fio, alimentação, pinos). A biblioteca tenta de novo sozinha a cada 0,5 s.
- **`SEM_CALIBRACAO`**: falta calibrar pelo menos o branco.
- `Serial.println(sensor.lerCor());` mostra um número (1 = PRETO, 2 = BRANCO...). Para ver o nome, use `nomeCor()`.

VERDE e VERMELHO só valem depois de **2 leituras seguidas iguais** (uma leitura sozinha na borda não dispara nada); PRETO e BRANCO valem na hora, porque a linha é fina. Chamado sem parar no `loop`, o `lerCor()` volta na hora. Depois de um `delay()` longo, ele espera ~20 ms para responder com a cor de **agora**, e não com a de antes do `delay`.

### Extras

| Comando | Para que serve |
|---|---|
| `CorFacil esquerdo("esquerdo");` | dá um nome ao sensor nas mensagens (útil com 2 sensores) |
| `sensor.iniciar(21, 22);` | ESP32: escolhe os pinos do I2C |
| `direito.iniciar(32, 33, Wire1);` | ESP32: o segundo sensor no outro barramento I2C |
| `sensor.usarMultiplexador(0);` | sensor no canal 0 de um TCA9548A (antes do `iniciar`) |
| `sensor.modoRapido();` | 83 leituras/s em vez de 52, para robô acima de ~50 cm/s (antes de calibrar o branco) |
| `sensor.frequenciaI2C(100000);` | I2C mais lento, para fio com mais de ~30 cm (antes do `iniciar`) |
| `sensor.confirmacoes(3);` | quantas leituras iguais seguidas o VERDE e o VERMELHO precisam (padrão 2) |
| `sensor.corAgora()` | a cor da última leitura, sem o filtro de leituras seguidas |
| `sensor.brilho()`, `croma()`, `matiz()`, `confianca()` | os números da última leitura |
| `sensor.lerContagens(r, g, b, c);` | as contagens cruas do sensor |
| `sensor.mostrarLeitura();` / `mostrarCalibracao();` | imprimem os números no Monitor Serial |
| `sensor.conectado()`, `leiturasPorSegundo()` | diagnóstico |

## Exemplos

| Exemplo | O que mostra |
|---|---|
| `01_LerCor` | o nome da cor no Monitor Serial |
| `02_CalibrarPeloMonitor` | calibração pelo Monitor Serial (`b`, `p`, `v`, `r`), com os números da leitura |
| `03_CalibrarComBotao` | calibração sem computador: botão no pino 4 e o LED da placa |
| `04_LedsDasCores` | um LED para cada cor |
| `05_DoisSensores` | sensor esquerdo e direito: CHEGADA (os dois no vermelho) e VERDE de cada lado |

## Dois ou mais sensores

O TCS34725 tem endereço I2C fixo (0x29), então dois sensores não podem dividir os mesmos fios SDA e SCL:

- **ESP32**: um sensor em cada barramento (`Wire` e `Wire1`).
- **Uno, Nano, Mega** (só têm um I2C): multiplexador **TCA9548A** no SDA/SCL, um sensor em cada canal.

```cpp
CorFacil esquerdo("esquerdo");
CorFacil direito("direito");

void setup() {
  esquerdo.usarMultiplexador(0);  // canal 0 do TCA9548A
  direito.usarMultiplexador(1);   // canal 1
  esquerdo.iniciar();
  direito.iniciar();
}
```

Cada sensor guarda a sua própria calibração. Na OBR, **exija os dois sensores no VERMELHO** para a chegada: a faixa de chegada cruza a pista inteira, e o marcador laranja de checkpoint passa embaixo de um sensor só.

## Montagem e calibração: o que mais muda o resultado

- **Altura de 5 a 8 mm do chão, sempre a mesma.** Mais perto, o sensor vê uma área menor e pega menos luz de fora.
- **Saia preta em volta do sensor** (PLA preto, fechando os lados até ~2 mm do chão): a luz da sala muda menos a leitura.
- Sensor paralelo ao chão, LED e sensor virados para baixo.
- **Calibre na altura e na luz de uso.** Na OBR, nos 2 minutos de calibração na arena: a luz do local é diferente da do treino.
- Trocou a luz ou a altura? Calibre só o **branco** de novo: as outras cores são guardadas em relação ao branco e continuam valendo.
- **Competição com Uno:** coloque a calibração por botão (exemplo 03) dentro do programa do robô. Assim dá para calibrar na arena sem notebook.

## Onde a calibração fica guardada

- **Uno, Nano, Mega:** nos últimos bytes da EEPROM (92 bytes por sensor; no Uno, do byte 932 ao 1023). Se o seu programa também usa a EEPROM, use a partir do começo dela.
- **ESP32:** na memória NVS, no espaço `corfacil`.
- Gravar outro programa **não apaga** a calibração (no ESP32, desde que "Erase All Flash Before Sketch Upload" esteja em *Disabled*, que é o padrão).
- Cada sensor (barramento + canal do multiplexador) tem o seu lugar. O sensor precisa estar ligado nos mesmos pinos no programa que calibrou e no programa do robô.

## Tamanho no Arduino Uno

No Uno a matemática de ponto flutuante é feita por software, e a biblioteca ocupa uma parte boa da memória. As mensagens só entram no programa se ele chamar `mensagens()`, `calibrarPeloMonitor()` ou `mostrar...()`.

| Programa | Memória de programa (32 KB) | RAM (2 KB) |
|---|---|---|
| só ler a cor (`04_LedsDasCores`) | 14,2 KB (44 %) | 29 % |
| ler a cor + mensagens (`01_LerCor`) | 19,6 KB (61 %) | 42 % |
| calibrar com botão + mensagens (`03_CalibrarComBotao`) | 27,0 KB (84 %) | 42 % |
| dois sensores + mensagens (`05_DoisSensores`) | 28,1 KB (87 %) | 59 % |
| calibrar pelo Monitor Serial (`02_CalibrarPeloMonitor`) | 29,8 KB (92 %) | 42 % |

O Nano com o bootloader antigo (o da maioria dos clones; na IDE, "ATmega328P (Old Bootloader)") tem só 30 KB: todos os exemplos cabem, o 02 fica em 97 %.

Se o programa do robô não couber, calibre com o exemplo 02 (fica salvo) e tire o `mensagens(Serial)` do programa do robô. Durante a calibração, a biblioteca usa mais ~620 bytes de RAM por um instante: deixe pelo menos isso livre (a IDE mostra quanto sobra em "leaving ... bytes for local variables"). Com dois sensores no Uno, use `F("texto")` nos `Serial.print` para guardar os textos fora da RAM. No ESP32 não tem nada disso (25 % da memória).

## Problemas comuns

| Aparece | O que fazer |
|---|---|
| `sensor NAO respondeu` | Confira VIN (5V no Uno, 3V3 no ESP32), GND, SDA e SCL (não trocados) e a solda dos pinos do sensor. A linha "No I2C respondeu" diz o que o I2C achou: ninguém = fio, alimentação ou pino errado. O programa não trava: arrumou, ele acha sozinho. |
| `falta calibrar` | Use o exemplo 02 (ou o 03) e calibre as 4 cores. |
| `clone? funciona igual` | O chip tem outro ID. Continua funcionando; é só aviso. |
| `sensor reiniciou sozinho` | Faltou energia no sensor: motor derrubando a alimentação ou mau contato no VIN ou no GND. |
| `pouca luz` | LED do sensor apagado (pino LED no GND?) ou sensor longe do chão. |
| `preto claro` | Sensor alto demais ou luz entrando por baixo: abaixe o sensor ou feche a saia. |
| `quase sem cor` | Era a cor certa? Sensor longe ou luz de fora lavando a cor. |
| `nao parece VERDE` | O matiz saiu longe do verde (100 a 160). Era a folha certa? Calibre de novo em cima do verde. |
| VERDE vira `INDEFINIDO` em parte da pista | Outro verde (marcador diferente) ou o sensor mais longe ali: calibre em cima desse verde. |
| o laranja vira VERMELHO | Exija os 2 sensores no vermelho para a chegada (veja Limites). |
| `claro demais` | Reflexo, sol direto ou sensor encostando no chão. |

## Como a biblioteca decide a cor

1. **Balanço pelo branco e pelo preto.** Cada canal vira `p = (leitura / branco - preto) / (1 - preto)`: o preto calibrado vira 0, o branco vira 1, em R, G e B. O tom azulado do LED e a sensibilidade de cada canal deixam de importar.
2. **Brilho separado da cor.** Brilho = média dos 3 canais. Croma = o quanto os canais diferem entre si: zero para qualquer cinza.
3. **A borda da linha não vira verde.** Na borda, o sensor vê parte preto e parte branco, e essa mistura é cinza (croma ≈ 0), por mais escura que fique. Códigos que olham só o brilho e o canal mais forte confundem a borda escura com o verde.
4. **Fração da cor.** Para o verde e o vermelho calibrados, `t` = quanto daquela cor aparece na leitura (1 em cima dela, 0,5 com metade do sensor sobre ela). A cor vale se `t ≥ 0,40` e se a direção (matiz) bate.
5. **Vermelho e laranja.** Para o TCS34725 eles ficam a só ~6 graus de matiz, por isso o vermelho aceita no máximo 3 graus para o lado do laranja.
6. **Filtro**: VERDE e VERMELHO com 2 leituras seguidas; PRETO e BRANCO com 1.

A leitura do sensor não usa `delay`: o bit AINT do sensor avisa quando há leitura nova, os 4 canais saem num bloco só, e se o sensor reiniciar (queda de tensão com motor) ou sumir do I2C, a biblioteca percebe e reconfigura sozinha.

## De onde veio e o que foi testado

A matemática é a do código `sensor-cor-tcs34725` (outubro de 2026), que **funcionou no teste na mão** para preto, branco, verde e vermelho num ESP32. A biblioteca é esse código empacotado: as mesmas contas e os mesmos limites, só que com comandos simples, rodando também no Uno.

- **Mesma resposta que o código original:** 1.518 calibrações e 2,4 milhões de leituras simuladas (bordas, misturas, luz de fora, calibração na cor errada) passaram pelos dois códigos: a calibração saiu igual bit a bit e a cor saiu igual em todas as leituras.
- **Teste das cenas** (`extras/teste_pc/teste_classificador.cpp`): borda da linha preta com 0 verde ou vermelho falso em 202 mil leituras; verde cobrindo 80 % do sensor: 100 % VERDE; vermelho: 99,3 %; robô a 15, 30 e 50 cm/s: verde e vermelho achados em 100 % das passadas; linha de 10 mm achada 100 % até 30 cm/s (95,6 % a 50 cm/s; com `modoRapido()`, 100 % a 50 cm/s); laranja nunca vira VERMELHO estável.
- **A biblioteca e os exemplos rodando no PC** com um TCS34725 simulado (`extras/teste_pc/sil`), como ESP32 e como Uno: calibrar pelo Monitor, religar com a calibração da memória, apagar, calibrar com botão (e errar a cor), dois sensores (Wire1 e TCA9548A), clone sem AINT, ligar sem sensor, fio solto, sensor que reinicia sozinho, `lerCor()` depois de `delay(500)` e modo rápido.
- **Compila sem nenhum aviso** em Uno, Nano, Mega, ESP32, ESP32-S3, ESP32-C3 (core 3.3.12) e ESP32 com o core 2.x.
- **Ainda não rodou num sensor de verdade nesta forma de biblioteca.** O primeiro teste é o exemplo 02: calibrar as 4 cores e conferir cada uma.

Para rodar os testes no PC: `python -m pip install ziglang` e dois cliques em `extras/teste_pc/rodar_testes.bat`.

## Limites

- **Laranja avermelhado.** Se o laranja do checkpoint for puxado para o vermelho, ele fica a ~2 graus do vermelho e nenhum código separa isso com segurança com este sensor. A defesa é exigir os 2 sensores no vermelho na chegada.
- **Luz de fora forte, sem saia.** Com luz quente de 25 % do sinal do LED ainda passa; mais do que isso, o branco começa a errar.
- **Velocidade.** Linha de 10 mm: 100 % até 30 cm/s. Acima de ~50 cm/s, use `modoRapido()`.
- **Fita prata** (sala de resgate da OBR): não está nesta biblioteca. No projeto original ela só foi testada no PC.

## Licença

MIT: pode usar, copiar e mudar, inclusive em aula e em competição. Veja [LICENSE](LICENSE).
