// Preferences falsa (NVS do ESP32): guarda em memória; o teste grava num arquivo
// para simular "desligar e ligar".
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace sim {
extern std::map<std::string, std::map<std::string, std::vector<uint8_t>>> nvs;
}

class Preferences {
 public:
  bool begin(const char *ns, bool = false) {
    _ns = ns;
    return _ns.size() <= 15;
  }
  void end() {}
  size_t putBytes(const char *k, const void *v, size_t n) {
    if (strlen(k) > 15) return 0;
    const uint8_t *b = (const uint8_t *)v;
    sim::nvs[_ns][k] = std::vector<uint8_t>(b, b + n);
    return n;
  }
  size_t getBytesLength(const char *k) {
    auto &m = sim::nvs[_ns];
    auto it = m.find(k);
    return it == m.end() ? 0 : it->second.size();
  }
  size_t getBytes(const char *k, void *v, size_t n) {
    auto &m = sim::nvs[_ns];
    auto it = m.find(k);
    if (it == m.end()) return 0;
    size_t q = it->second.size() < n ? it->second.size() : n;
    memcpy(v, it->second.data(), q);
    return q;
  }
  bool remove(const char *k) { return sim::nvs[_ns].erase(k) > 0; }
  bool clear() {
    sim::nvs[_ns].clear();
    return true;
  }

 private:
  std::string _ns;
};
