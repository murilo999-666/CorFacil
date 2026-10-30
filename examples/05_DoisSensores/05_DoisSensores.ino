/*
 * 05_DoisSensores
 * Dois sensores, esquerdo e direito, como num seguidor de linha da OBR.
 * Mostra as duas cores e avisa a CHEGADA (os dois no VERMELHO) e o VERDE de
 * cada lado.
 *
 * O TCS34725 tem endereço I2C fixo (0x29): dois sensores não podem dividir os
 * mesmos fios SDA e SCL.
 *  - ESP32: um sensor em cada barramento I2C. Esquerdo no SDA 21 / SCL 22,
 *    direito no SDA 32 / SCL 33 (ESP32 DevKit; no ESP32-S3 troque os pinos).
 *  - Uno, Nano, Mega (só têm um I2C): multiplexador TCA9548A no A4/A5.
 *    Esquerdo no canal 0 (SD0/SC0), direito no canal 1 (SD1/SC1).
 *
 * Calibrar: os DOIS sensores em cima da cor e envie b, p, v ou r pelo Monitor
 * Serial (115200). Cada um guarda a sua calibração.
 *
 * F("texto") guarda o texto na memória de programa em vez da RAM. No Uno, com
 * dois sensores, a RAM fica apertada: use F() nos textos do Serial.print.
 */
#include <CorFacil.h>

CorFacil esquerdo("esquerdo");
CorFacil direito("direito");

void setup() {
  Serial.begin(115200);
  esquerdo.mensagens(Serial);
  direito.mensagens(Serial);

#if defined(ESP32) && SOC_I2C_NUM > 1
  esquerdo.iniciar();               // Wire, pinos padrão da placa
  direito.iniciar(32, 33, Wire1);   // Wire1, SDA 32 e SCL 33
#else
  esquerdo.usarMultiplexador(0);    // canal 0 do TCA9548A
  direito.usarMultiplexador(1);     // canal 1
  esquerdo.iniciar();
  direito.iniciar();
#endif
}

void loop() {
  // Calibração: a letra que chega pelo Monitor Serial calibra os dois sensores
  if (Serial.available() > 0) {
    char letra = Serial.read();
    int cor = INDEFINIDO;
    if (letra == 'b') cor = BRANCO;
    if (letra == 'p') cor = PRETO;
    if (letra == 'v') cor = VERDE;
    if (letra == 'r') cor = VERMELHO;
    if (cor != INDEFINIDO) {
      esquerdo.calibrar(cor);
      direito.calibrar(cor);
    }
  }

  int corEsq = esquerdo.lerCor();
  int corDir = direito.lerCor();

  Serial.print(esquerdo.nomeCor(corEsq));
  Serial.print(F("  |  "));
  Serial.print(direito.nomeCor(corDir));

  if (corEsq == VERMELHO && corDir == VERMELHO) {
    Serial.print(F("   -> CHEGADA"));
  } else if (corEsq == VERDE && corDir == VERDE) {
    Serial.print(F("   -> VERDE dos dois lados"));
  } else if (corEsq == VERDE) {
    Serial.print(F("   -> VERDE na esquerda"));
  } else if (corDir == VERDE) {
    Serial.print(F("   -> VERDE na direita"));
  }
  Serial.println();

  delay(100);
}
