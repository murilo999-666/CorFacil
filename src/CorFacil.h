/*
 * CorFacil - sensor de cor TCS34725 fácil
 * =======================================
 * Separa PRETO, BRANCO, VERDE e VERMELHO com um comando. Feito para turmas de
 * robótica (OBR e afins): a calibração fica salva na placa, a borda da linha
 * preta não vira verde e o programa nunca trava esperando o sensor.
 *
 * O MÍNIMO:
 *
 *   #include <CorFacil.h>
 *   CorFacil sensor;
 *
 *   void setup() {
 *     sensor.iniciar();
 *   }
 *
 *   void loop() {
 *     if (sensor.lerCor() == VERDE) {
 *       // ...
 *     }
 *   }
 *
 * CALIBRAR (uma vez; fica salvo, desligar não apaga). Sensor parado em cima da
 * cor, na altura em que vai ficar no robô. Sempre o BRANCO primeiro:
 *
 *   sensor.calibrar(BRANCO);
 *   sensor.calibrar(PRETO);
 *   sensor.calibrar(VERDE);
 *   sensor.calibrar(VERMELHO);
 *
 * Ou abra o exemplo 02_CalibrarPeloMonitor e mande b, p, v e r pelo Monitor Serial.
 *
 * LIGAÇÃO: VIN no 5V (Uno, Nano, Mega) ou no 3V3 (ESP32), GND no GND,
 * SDA e SCL nos pinos do I2C (Uno/Nano: A4 e A5; Mega: 20 e 21; ESP32: 21 e 22).
 *
 * Tudo explicado no README: https://github.com/murilo999-666/CorFacil
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "CorFacil_Classificador.h"
#include "CorFacil_Cores.h"
#include "CorFacil_TCS34725.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "soc/soc_caps.h"  // SOC_I2C_NUM: quantos barramentos I2C a placa tem
#endif

#define CORFACIL_VERSAO "1.0.0"

class CorFacil {
 public:
  // nome: aparece nas mensagens do Monitor Serial (ex.: CorFacil esquerdo("esquerdo");)
  constexpr explicit CorFacil(const char *nome = "sensor") : _nome(nome) {}

  // ===================== 1. COMEÇAR (no setup) =====================

  // Liga o sensor nos pinos I2C padrão da placa e carrega a calibração salva.
  // Devolve false se o sensor não respondeu (o programa continua e tenta de novo sozinho).
  bool iniciar();
  // ESP32: escolhe os pinos do I2C. Ex.: sensor.iniciar(21, 22);
  bool iniciar(int sda, int scl);
  // ESP32 com 2 sensores: o segundo no outro barramento. Ex.: direito.iniciar(32, 33, Wire1);
  bool iniciar(int sda, int scl, TwoWire &barramento);

  // ===================== 2. LER A COR (no loop) =====================

  // PRETO, BRANCO, VERDE, VERMELHO ou INDEFINIDO (outra cor, ou a borda de um
  // marcador). Também pode dar SEM_SENSOR ou SEM_CALIBRACAO.
  // Não espera o sensor quando chamado sempre; depois de um delay() longo,
  // espera ~20 ms para responder com a cor de AGORA.
  Cor lerCor();
  // O nome da cor de agora, para mostrar: Serial.println(sensor.nomeCor());
  const char *nomeCor();
  // O nome de uma cor qualquer: sensor.nomeCor(VERDE) -> "VERDE"
  static const char *nomeCor(uint8_t cor);

  // ===================== 3. CALIBRAR =====================

  // Sensor parado em cima da cor (pode deslizar devagar). Leva ~1 s e grava na
  // memória da placa. BRANCO sempre primeiro. Devolve true se deu certo.
  bool calibrar(uint8_t cor);
  // true se BRANCO, PRETO, VERDE e VERMELHO já estão calibrados.
  bool calibrado();
  // true se essa cor já está calibrada. Ex.: if (!sensor.calibrado(VERDE)) ...
  bool calibrado(uint8_t cor);
  // Esquece toda a calibração (ou só uma cor).
  void apagarCalibracao();
  void apagarCalibracao(uint8_t cor);

  // ===================== 4. AJUDA PELO MONITOR SERIAL =====================

  // Liga as mensagens de ajuda (sensor achado ou não, resultado da calibração,
  // fio solto...). Ex.: Serial.begin(115200); sensor.mensagens(Serial);
  void mensagens(Print &saida);
  // Chame no loop: calibra pelos comandos b, p, v, r enviados no Monitor
  // Serial (c mostra a calibração, x apaga, ? ajuda).
  void calibrarPeloMonitor(Stream &serial = Serial);
  // Mostra a calibração guardada (números).
  void mostrarCalibracao(Print &saida = Serial);
  // Mostra uma linha com a cor e os números da leitura de agora.
  void mostrarLeitura(Print &saida = Serial);

  // ===================== 5. EXTRAS =====================

  // Multiplexador TCA9548A (vários sensores no mesmo I2C, como no Uno).
  // Chame ANTES de iniciar(). Ex.: esquerdo.usarMultiplexador(0);
  void usarMultiplexador(uint8_t canal, uint8_t endereco = 0x70);
  // Velocidade do I2C (padrão 400000). Fio com mais de ~30 cm: 100000. Antes de iniciar().
  void frequenciaI2C(uint32_t hz);
  // Leituras mais rápidas (83 por segundo em vez de 52), para robô acima de
  // ~50 cm/s. Chame ANTES de calibrar o BRANCO.
  void modoRapido(bool ligado = true);
  // Quantas leituras iguais seguidas o VERDE e o VERMELHO precisam (padrão 2).
  void confirmacoes(uint8_t leituras);
  // Lê o sensor se houver leitura nova (lerCor() já chama). true = leitura nova.
  bool atualizar();
  // A cor da última leitura, sem o filtro de leituras seguidas.
  Cor corAgora();
  // true se o sensor está respondendo no I2C.
  bool conectado();
  // Números da última leitura: brilho (0 = preto, 1 = branco), croma (quanta
  // cor; 0 = cinza), matiz (graus: 0 vermelho, 120 verde, 240 azul) e a
  // confiança da cor (0 a 1).
  float brilho();
  float croma();
  float matiz();
  float confianca();
  // Contagens cruas do sensor (vermelho, verde, azul e claro).
  void lerContagens(uint16_t &r, uint16_t &g, uint16_t &b, uint16_t &c);
  float leiturasPorSegundo();

 private:
  // Mensagens: só entram no programa se ele chamar mensagens() ou
  // calibrarPeloMonitor() (no Uno, o texto ocupa muita memória).
  enum Evento : uint8_t { EV_INICIO, EV_PINOS_FIXOS, EV_CALIBRANDO, EV_RESULTADO, EV_CONEXAO, EV_REINICIO, EV_POR_TEMPO };
  typedef void (CorFacil::*Mensageiro)(uint8_t evento, uint8_t cor, const corfacil::Relatorio *rel);

  const char *_nome;
  corfacil::TCS34725 _drv = {};
  corfacil::Classificador _cls = {};
  corfacil::FiltroEstavel _filtro = {};
  corfacil::Calibracao _cal = {};
  Cor _corAgora = INDEFINIDO;  // a cor da última leitura (o resto é recalculado quando pedem)
  Print *_saida = nullptr;
  Mensageiro _msg = nullptr;
  uint32_t _hz = 400000;
  int16_t _muxEndereco = -1;
  int8_t _muxCanal = -1;
  uint8_t _ciclosPedido = 0;  // 0 = o da calibração (ou 7); 4 = modo rápido; 7 = normal
  uint8_t _slot = 0;          // posição da calibração na memória
  bool _esperar = true, _temUltima = false, _estavaConectado = false, _avisouTempo = false;
  bool _exposicaoMudou = false, _ajudaMostrada = false, _apagarPendente = false, _semMemoria = false;
  uint32_t _msUltima = 0, _reiniciosVistos = 0, _apagarEm = 0;

  void vigiar();
  void reiniciarFiltro();
  bool salvar();
  bool carregar();
  void apagarMemoria();
  void aplicarExposicaoDaCalibracao();
  uint16_t ajustarExposicaoParaBranco();
  float mediaClaro(int n);
  corfacil::Resultado ultimaLeitura();
  void avisar(uint8_t evento, uint8_t cor = 0, const corfacil::Relatorio *rel = nullptr) {
    if (_msg) (this->*_msg)(evento, cor, rel);
  }
  void imprimirEvento(uint8_t evento, uint8_t cor, const corfacil::Relatorio *rel);
  void imprimirInicio();
  void imprimirResultado(Cor alvo, const corfacil::Relatorio &rel);
  void imprimirFaltam(Print &p);
  void imprimirAjuda(Print &p);
  void imprimirProximoPasso(Print &p);
  void prefixo(Print &p);
};
