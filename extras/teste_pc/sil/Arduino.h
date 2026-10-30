// Arduino.h falso: roda a biblioteca e os exemplos no PC, com o tempo simulado.
// Compile com -DARDUINO_ARCH_ESP32 -DESP32 (memória NVS, Wire1) ou com
// -DARDUINO_ARCH_AVR (EEPROM, um I2C só, pinos fixos A4/A5).
#pragma once
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <vector>

using std::isnan;

namespace sim {
extern double us;            // relógio simulado (microssegundos)
extern int modo[64];         // pinMode de cada pino
extern int nivel[64];        // digitalWrite de cada pino
extern std::function<int(int)> entrada;  // digitalRead de pino de entrada (botão); nulo = HIGH
extern std::string saidaSerial;
extern std::deque<char> entradaSerial;
extern bool ecoSerial;
}  // namespace sim

#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define OUTPUT_OPEN_DRAIN 0x13
#define LOW 0
#define HIGH 1
#define DEC 10
#define HEX 16

#if defined(ARDUINO_ARCH_AVR)
#define PIN_A4 (18)
#define PIN_A5 (19)
static const uint8_t SDA = PIN_A4;
static const uint8_t SCL = PIN_A5;
#define LED_BUILTIN 13
#else
static const uint8_t SDA = 21;
static const uint8_t SCL = 22;
#endif

inline unsigned long micros() { sim::us += 1.0; return (unsigned long)(uint32_t)(uint64_t)sim::us; }
inline unsigned long millis() { sim::us += 1.0; return (unsigned long)(uint32_t)(uint64_t)(sim::us / 1000.0); }
inline void delay(unsigned long ms) { sim::us += 1000.0 * ms; }
inline void delayMicroseconds(unsigned int u) { sim::us += u; }
inline void yield() { sim::us += 1.0; }
inline void pinMode(int p, int m) { if (p >= 0 && p < 64) sim::modo[p] = m; }
inline void digitalWrite(int p, int v) { if (p >= 0 && p < 64) sim::nivel[p] = v; }
inline int digitalRead(int p) {
  if (p < 0 || p >= 64) return LOW;
  if (sim::modo[p] == OUTPUT || sim::modo[p] == OUTPUT_OPEN_DRAIN) return sim::nivel[p];
  return sim::entrada ? sim::entrada(p) : HIGH;
}

class __FlashStringHelper;
#define F(s) (reinterpret_cast<const __FlashStringHelper *>(s))

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t c) = 0;
  size_t escrever(const char *s) {
    size_t n = 0;
    while (*s) n += write((uint8_t)*s++);
    return n;
  }
  size_t print(const __FlashStringHelper *s) { return escrever(reinterpret_cast<const char *>(s)); }
  size_t print(const char *s) { return escrever(s); }
  size_t print(char c) { return write((uint8_t)c); }
  size_t print(unsigned char v, int base = DEC) { return print((unsigned long)v, base); }
  size_t print(int v, int base = DEC) { return print((long)v, base); }
  size_t print(unsigned int v, int base = DEC) { return print((unsigned long)v, base); }
  size_t print(long v, int base = DEC) {
    if (base != DEC) return print((unsigned long)v, base);
    char b[24];
    snprintf(b, sizeof b, "%ld", v);
    return escrever(b);
  }
  size_t print(unsigned long v, int base = DEC) {
    char b[40];
    snprintf(b, sizeof b, base == HEX ? "%lX" : "%lu", v);
    return escrever(b);
  }
  size_t print(double v, int casas = 2) {
    char b[64];
    snprintf(b, sizeof b, "%.*f", casas, v);
    return escrever(b);
  }
  size_t println() { return escrever("\r\n"); }
  template <typename T>
  size_t println(T v) { size_t n = print(v); return n + println(); }
  template <typename T>
  size_t println(T v, int b) { size_t n = print(v, b); return n + println(); }
};

class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
};

class SerialFalsa : public Stream {
 public:
  void begin(unsigned long) {}
  int available() override { return (int)sim::entradaSerial.size(); }
  int read() override {
    if (sim::entradaSerial.empty()) return -1;
    char c = sim::entradaSerial.front();
    sim::entradaSerial.pop_front();
    return (unsigned char)c;
  }
  int peek() override { return sim::entradaSerial.empty() ? -1 : (unsigned char)sim::entradaSerial.front(); }
  size_t write(uint8_t c) override {
    sim::saidaSerial += (char)c;
    if (sim::ecoSerial && c != '\r') fputc(c, stdout);
    return 1;
  }
  explicit operator bool() const { return true; }
};
extern SerialFalsa Serial;
