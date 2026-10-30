/*
 * 02_CalibrarPeloMonitor
 * Calibra o sensor pelo Monitor Serial. Faça isso uma vez, e de novo se mudar
 * a altura do sensor, a luz do lugar ou a pista.
 *
 * 1. Grave este exemplo e abra o Monitor Serial em 115200.
 * 2. Sensor em cima do BRANCO, na altura em que vai ficar no robô: envie b
 * 3. Em cima do PRETO (a linha): envie p
 * 4. Em cima do VERDE: envie v
 * 5. Em cima do VERMELHO: envie r
 * 6. Passe o sensor sobre cada cor e confira a primeira palavra da linha.
 *
 * Outros comandos: c mostra a calibração, x apaga, ? mostra a ajuda.
 * A calibração fica salva na placa: depois é só gravar o programa do robô
 * (com o sensor ligado nos mesmos pinos).
 */
#include <CorFacil.h>

CorFacil sensor;

void setup() {
  Serial.begin(115200);
  sensor.mensagens(Serial);
  sensor.iniciar();
}

void loop() {
  sensor.calibrarPeloMonitor();  // atende os comandos b, p, v, r, c, x e ?
  sensor.mostrarLeitura();       // a cor e os números da leitura
  delay(300);
}
