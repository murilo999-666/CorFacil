/*
 * 04_LedsDasCores
 * Um LED para cada cor: o verde acende no VERDE, o vermelho no VERMELHO e o
 * amarelo no PRETO (a linha). No BRANCO fica tudo apagado.
 *
 * Cada LED com um resistor de 220 ohms até o GND.
 * Antes: calibre com o exemplo 02_CalibrarPeloMonitor.
 */
#include <CorFacil.h>

#if defined(ESP32)
const int LED_VERDE = 25;  // ESP32 DevKit (no ESP32-S3 ou C3, escolha outros pinos)
const int LED_VERMELHO = 26;
const int LED_AMARELO = 27;
#else
const int LED_VERDE = 5;  // Uno, Nano, Mega
const int LED_VERMELHO = 6;
const int LED_AMARELO = 7;
#endif

CorFacil sensor;

void setup() {
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(LED_AMARELO, OUTPUT);
  sensor.iniciar();
}

void loop() {
  int cor = sensor.lerCor();

  if (cor == VERDE) {
    digitalWrite(LED_VERDE, HIGH);
  } else {
    digitalWrite(LED_VERDE, LOW);
  }

  if (cor == VERMELHO) {
    digitalWrite(LED_VERMELHO, HIGH);
  } else {
    digitalWrite(LED_VERMELHO, LOW);
  }

  if (cor == PRETO) {
    digitalWrite(LED_AMARELO, HIGH);
  } else {
    digitalWrite(LED_AMARELO, LOW);
  }
}
