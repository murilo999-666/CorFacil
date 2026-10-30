// Wire falsa: entrega as transações I2C aos dispositivos simulados (sensor, multiplexador).
#pragma once
#include <cstdint>
#include <vector>
#include "Arduino.h"

namespace sim {
struct Dispositivo {
  uint8_t endereco = 0;
  int canalMux = -1;  // -1 = direto no barramento; 0..7 = atrás do TCA9548A
  virtual ~Dispositivo() {}
  virtual bool presente() { return true; }
  virtual bool escrever(const uint8_t *b, int n) = 0;  // false = NACK
  virtual int ler(uint8_t *b, int n) = 0;              // bytes entregues (0 = NACK)
};
struct Barramento {
  std::vector<Dispositivo *> disp;
  uint8_t mascaraMux = 0;  // canais ligados no TCA9548A (se houver)
  long transacoes = 0, inicios = 0, fins = 0;
  uint32_t hz = 0;
  int sda = -1, scl = -1;
};
extern Barramento barramentos[2];
Dispositivo *achar(Barramento &bus, uint8_t endereco);
}  // namespace sim

#if defined(ARDUINO_ARCH_AVR)
#define WIRE_HAS_TIMEOUT 1
#endif

class TwoWire {
 public:
  explicit TwoWire(int n) : _n(n) {}
  bool begin(int sda, int scl, uint32_t hz) {
    sim::barramentos[_n].inicios++;
    sim::barramentos[_n].sda = sda;
    sim::barramentos[_n].scl = scl;
    _hz = sim::barramentos[_n].hz = hz;
    return true;
  }
  void begin() { sim::barramentos[_n].inicios++; }
  void end() { sim::barramentos[_n].fins++; }
  void setClock(uint32_t hz) { _hz = sim::barramentos[_n].hz = hz; }
  void setTimeOut(uint16_t) {}
  void setWireTimeout(uint32_t, bool) {}
  void beginTransmission(uint8_t a) { _end = a; _tx.clear(); }
  void beginTransmission(int a) { beginTransmission((uint8_t)a); }
  size_t write(uint8_t b) { _tx.push_back(b); return 1; }
  uint8_t endTransmission(bool = true) {
    tempo(_tx.size() + 1);
    sim::barramentos[_n].transacoes++;
    sim::Dispositivo *d = sim::achar(sim::barramentos[_n], _end);
    if (!d) return 2;  // NACK no endereço
    if (_tx.empty()) return 0;
    return d->escrever(_tx.data(), (int)_tx.size()) ? 0 : 3;
  }
  uint8_t requestFrom(uint8_t a, uint8_t n) {
    tempo(n + 1);
    sim::barramentos[_n].transacoes++;
    _rx.clear();
    _pos = 0;
    sim::Dispositivo *d = sim::achar(sim::barramentos[_n], a);
    if (!d) return 0;
    _rx.resize(n);
    int q = d->ler(_rx.data(), n);
    _rx.resize(q > 0 ? q : 0);
    return (uint8_t)_rx.size();
  }
  int read() { return _pos < _rx.size() ? _rx[_pos++] : -1; }

 private:
  int _n;
  uint32_t _hz = 100000;
  uint8_t _end = 0;
  std::vector<uint8_t> _tx, _rx;
  size_t _pos = 0;
  void tempo(size_t bytes) { sim::us += (bytes * 9.0 + 3.0) * 1e6 / (_hz ? _hz : 100000) + 15.0; }
};

extern TwoWire Wire;
#if defined(ARDUINO_ARCH_ESP32)
extern TwoWire Wire1;
#endif
