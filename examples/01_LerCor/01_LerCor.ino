/*
 * 01_LerCor
 * Mostra no Monitor Serial (115200) a cor que está embaixo do sensor.
 *
 * Antes: calibre uma vez com o exemplo 02_CalibrarPeloMonitor.
 * A calibração fica salva na placa.
 *
 * Ligação:  TCS34725 | Uno / Nano | Mega | ESP32
 *           VIN      | 5V         | 5V   | 3V3
 *           GND      | GND        | GND  | GND
 *           SDA      | A4         | 20   | GPIO 21
 *           SCL      | A5         | 21   | GPIO 22
 */
#include <CorFacil.h>

CorFacil sensor;

void setup() {
  Serial.begin(115200);
  sensor.mensagens(Serial);  // mensagens de ajuda (sensor achado, o que falta calibrar...)
  sensor.iniciar();
}

void loop() {
  int cor = sensor.lerCor();  // PRETO, BRANCO, VERDE, VERMELHO ou INDEFINIDO

  Serial.print(sensor.nomeCor(cor));

  if (cor == VERDE) {
    Serial.print("   <- achei o verde!");
  }
  Serial.println();

  delay(200);
}
