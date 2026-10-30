/*
 * 03_CalibrarComBotao
 * Calibra sem computador, com um botão e o LED da placa.
 *
 * Botão entre o pino 4 e o GND (não precisa de resistor).
 * Coloque o sensor em cima da cor e aperte. Cada aperto calibra a próxima:
 *    1º aperto: BRANCO   2º: PRETO   3º: VERDE   4º: VERMELHO
 *
 * O LED pisca 1, 2, 3 ou 4 vezes (é a cor que vai calibrar), depois:
 *   - fica aceso 1 segundo: deu certo;
 *   - pisca rápido: não deu (aperte de novo com o sensor parado na cor).
 * Depois disso, o LED acende sempre que o sensor vê VERDE.
 */
#include <CorFacil.h>

#ifndef LED_BUILTIN
#define LED_BUILTIN 2  // ESP32 DevKit
#endif

const int BOTAO = 4;
const int LED = LED_BUILTIN;

CorFacil sensor;

Cor ordem[] = {BRANCO, PRETO, VERDE, VERMELHO};
int passo = 0;  // qual cor o próximo aperto calibra (0 = BRANCO)

void piscar(int vezes, int tempo) {
  for (int i = 0; i < vezes; i++) {
    digitalWrite(LED, HIGH);
    delay(tempo);
    digitalWrite(LED, LOW);
    delay(tempo);
  }
}

void setup() {
  pinMode(BOTAO, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  Serial.begin(115200);
  sensor.mensagens(Serial);  // opcional: mostra o resultado no Monitor Serial
  sensor.iniciar();
}

void loop() {
  if (digitalRead(BOTAO) == LOW) {  // apertou
    piscar(passo + 1, 250);          // diz qual cor vai calibrar

    if (sensor.calibrar(ordem[passo])) {
      digitalWrite(LED, HIGH);  // deu certo
      delay(1000);
      digitalWrite(LED, LOW);
      passo = passo + 1;
      if (passo == 4) {
        passo = 0;  // depois do VERMELHO, o próximo aperto recomeça no BRANCO
      }
    } else {
      piscar(10, 50);  // não deu
    }

    while (digitalRead(BOTAO) == LOW) {  // espera soltar o botão
      delay(10);
    }
  }

  if (sensor.lerCor() == VERDE) {
    digitalWrite(LED, HIGH);
  } else {
    digitalWrite(LED, LOW);
  }
}
