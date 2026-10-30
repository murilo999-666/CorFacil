/*
 * CorFacil_TCS34725.h
 * -------------------
 * Leitura rápida do TCS34725 que nunca trava a placa, só com a Wire.
 * Não precisa da biblioteca da Adafruit. É o TCS34725Rapido do código que
 * funcionou, sem as partes do pino LED (luz ambiente e fita prata) e com o
 * I2C adaptado para AVR (Uno, Nano, Mega) além do ESP32.
 *
 *  - A getRawData() da Adafruit para o programa por um tempo de integração
 *    inteiro (delay) a cada leitura. Aqui atualizar() volta na hora e só
 *    entrega leitura nova quando o sensor fechou um ciclo.
 *  - "Leitura nova" vem do bit AINT do STATUS. O datasheet diz que o estado da
 *    interrupção sempre aparece no STATUS, mesmo com o pino INT desligado, e
 *    com PERS = 0 ele liga ao fim de cada ciclo. Clone sem AINT: lê por tempo.
 *  - Os 4 canais saem de um único bloco de 8 bytes (todos do mesmo ciclo).
 *  - Se o sensor reiniciar (queda de tensão com motor), percebe e reconfigura;
 *    se sumir do I2C, tenta de novo a cada 0,5 s.
 *  - Opcional: multiplexador TCA9548A (vários TCS34725 no mesmo I2C).
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace corfacil {

class TCS34725 {
 public:
  static const uint8_t ENDERECO = 0x29;  // fixo no TCS34725

  struct Leitura {
    uint16_t r, g, b, c;  // contagens
    bool saturada;        // C perto do máximo: mais claro que o sensor aguenta
    uint32_t numero;      // conta as leituras desde o início
  };

  static uint8_t ganhoX(uint8_t i) { return i == 0 ? 1 : i == 1 ? 4 : i == 2 ? 16 : 60; }

  // ciclos: integração = 2,4 ms x ciclos. ganho: 0 = 1x, 1 = 4x, 2 = 16x, 3 = 60x.
  // muxEndereco < 0: sem multiplexador. Devolve false se o sensor não respondeu;
  // mesmo assim atualizar() continua tentando.
  bool iniciar(TwoWire &wire, int sda, int scl, uint32_t hz, uint8_t ciclos, uint8_t ganho, int16_t muxEndereco,
               int8_t muxCanal);
  // Chame sempre. true = leitura nova em leitura().
  bool atualizar();
  // Bloqueia até a próxima leitura (para calibrar). false = não veio a tempo.
  bool esperarLeitura(uint32_t timeoutMs);
  // Muda integração e ganho. A próxima leitura já vem inteira com o valor novo.
  void definirExposicao(uint8_t ciclos, uint8_t ganho);

  const Leitura &leitura() const { return _l; }
  bool conectado() const { return _ok; }
  bool iniciado() const { return _w != nullptr; }
  uint8_t id() const { return _id; }
  bool idConhecido() const { return _id == 0x44 || _id == 0x4D || _id == 0x10; }
  bool registrosConferem() const { return _registrosConferem; }
  uint8_t ciclos() const { return _ciclos; }
  uint8_t ganho() const { return _ganho; }
  float integracaoMs() const { return 2.4f * _ciclos; }
  uint32_t periodoUs() const { return 2400UL * (_ciclos + 1); }  // integração + 2,4 ms de preparo
  uint16_t limiteSaturacao() const { return _limiteSat; }
  float taxaHz() const { return _taxaHz; }
  bool modoPorTempo() const { return _modoTempo; }
  uint32_t reinicios() const { return _reinicios; }  // vezes que o sensor perdeu a configuração
  uint32_t reconexoes() const { return _reconexoes; }
  int sda() const { return _sda; }
  int scl() const { return _scl; }
  TwoWire *barramento() const { return _w; }

 private:
  TwoWire *_w = nullptr;
  int _sda = -1, _scl = -1;
  uint32_t _hz = 400000;
  uint8_t _ciclos = 7, _ganho = 1;
  int16_t _muxEndereco = -1;
  int8_t _muxCanal = -1;
  Leitura _l = {};
  bool _ok = false, _modoTempo = false, _registrosConferem = true;
  uint8_t _id = 0, _semAint = 0, _falhas = 0;
  uint16_t _limiteSat = 0, _tentativas = 0;
  uint32_t _tInicio = 0, _tConsulta = 0, _tSaude = 0, _tReconectar = 0;
  uint32_t _tPortaoUs = 0, _tLimiteUs = 0, _tCicloMaxUs = 0;
  uint32_t _reinicios = 0, _reconexoes = 0;
  float _taxaHz = 0.0f;
  uint32_t _tUltimaUs = 0, _janelaUs = 0, _janelaN = 0;

  void comecarBarramento();
  void recalcular();
  bool configurar();
  bool conferirConfiguracao();
  void tentarReconectar();
  void falhou();
  void montar(uint16_t r, uint16_t g, uint16_t b, uint16_t c);
  void reiniciarIntegracao();
  void liberarBarramento();
  bool selecionarMux();
  bool escrever(uint8_t reg, uint8_t valor);
  bool escreverLimiares();
  bool limparInterrupcao();
  bool ler(uint8_t reg, uint8_t *buf, uint8_t n);
};

}  // namespace corfacil
