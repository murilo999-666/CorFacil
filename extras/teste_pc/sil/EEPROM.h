// EEPROM falsa (Arduino Uno: 1024 bytes, apagada = 0xFF). O teste grava num
// arquivo para simular "desligar e ligar".
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sim {
extern uint8_t eeprom[1024];
extern long escritasEeprom;  // bytes regravados (a EEPROM aguenta ~100 mil por byte)
}  // namespace sim

class EEPROMClass {
 public:
  uint16_t length() { return 1024; }
  uint8_t read(int a) { return sim::eeprom[a]; }
  void update(int a, uint8_t v) {
    if (sim::eeprom[a] != v) {
      sim::eeprom[a] = v;
      sim::escritasEeprom++;
    }
  }
  template <typename T>
  T &get(int a, T &t) {
    memcpy(&t, sim::eeprom + a, sizeof(T));
    return t;
  }
  template <typename T>
  const T &put(int a, const T &t) {
    const uint8_t *b = reinterpret_cast<const uint8_t *>(&t);
    for (size_t i = 0; i < sizeof(T); i++) update(a + (int)i, b[i]);
    return t;
  }
};
extern EEPROMClass EEPROM;
