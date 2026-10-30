// TCS34725 simulado (do teste do projeto original sensor-cor-tcs34725): registradores,
// ciclo com os 2,4 ms de preparo, bit AINT, ganho com o erro de fábrica do
// datasheet, oscilador fora do nominal, sensor que reinicia sozinho, fio solto,
// clone sem AINT e multiplexador TCA9548A.
#pragma once
#include <algorithm>
#include <functional>
#include <random>
#include <vector>
#include "Arduino.h"
#include "Wire.h"

// fração do branco (R, G, B) saída do modelo espectral do projeto original
struct Mat { const char *nome; float x[3]; };
static const Mat BRANCO_ = {"branco", {1.000f, 1.000f, 1.000f}};
static const Mat PRETO_ = {"preto", {0.070f, 0.070f, 0.070f}};
static const Mat VERDE_ = {"verde", {0.166f, 0.340f, 0.165f}};
static const Mat VERMELHO_ = {"vermelho", {0.415f, 0.124f, 0.079f}};
static const Mat LARANJA_ = {"laranja", {0.659f, 0.234f, 0.105f}};

// cena(t_us, x[3]): o que o chão sob o sensor devolve da luz do LED
typedef std::function<void(double, float *)> Cena;

inline Cena fixa(const Mat &m) {
  return [m](double, float *x) { for (int c = 0; c < 3; c++) x[c] = m.x[c]; };
}

// muda de material no instante tTroca (us)
inline Cena troca(const Mat &antes, const Mat &depois, double tTroca) {
  return [antes, depois, tTroca](double t, float *x) {
    const Mat &m = t < tTroca ? antes : depois;
    for (int c = 0; c < 3; c++) x[c] = m.x[c];
  };
}

struct Trecho { float x0, x1; const Mat *m; };
// Pista 1D percorrida a v mm/s a partir de t0; mancha gaussiana sigma 3 mm.
inline Cena pista(std::vector<Trecho> tr, float v, double t0) {
  return [tr, v, t0](double t, float *x) {
    const float pos = (float)((t - t0) * 1e-6 * v);
    float soma = 0, acc[3] = {0, 0, 0};
    for (float dx = -10.5f; dx <= 10.5f; dx += 0.25f) {
      const float w = expf(-0.5f * dx * dx / 9.0f);
      const Mat *m = &BRANCO_;
      for (auto &q : tr) if (pos + dx >= q.x0 && pos + dx < q.x1) m = q.m;
      for (int c = 0; c < 3; c++) acc[c] += w * m->x[c];
      soma += w;
    }
    for (int c = 0; c < 3; c++) x[c] = acc[c] / soma;
  };
}

struct TCSFalso : sim::Dispositivo {
  uint8_t reg[32];
  uint8_t ptr = 0;
  bool autoInc = false;
  bool ligado = true;    // false = fio solto (NACK)
  bool comAint = true;   // false = clone que não marca AINT
  uint8_t idValor = 0x44;
  double osc = 1.0;      // passo de integração = 2,4 ms x osc (datasheet: 0,946 a 1,067)
  float ganhoReal[4] = {1.0f, 4.08f, 16.3f, 61.0f};  // ganho de verdade (datasheet: 4x = 3,8..4,2 etc.)
  float base[3] = {6.8f, 9.1f, 7.5f};  // contagens por passo no 1x sobre o branco (R, G, B)
  Cena cena;
  double tAen = -1;
  long ciclo = 0, ciclosFechados = 0, leiturasDados = 0, repetidas = 0, ultimoLido = -1;
  bool aint = false, avalid = false;
  std::mt19937 rng{7};

  TCSFalso() { endereco = 0x29; resetar(); cena = fixa(BRANCO_); }
  void resetar() {  // como depois de faltar energia: registradores de fábrica
    memset(reg, 0, sizeof reg);
    reg[0x01] = 0xFF;
    reg[0x03] = 0xFF;
    tAen = -1;
    aint = avalid = false;
  }
  bool presente() override { return ligado; }
  int nCiclos() const { return 256 - reg[0x01]; }
  double tCiclo() const { return 2400.0 * osc * (nCiclos() + 1); }
  double tInt() const { return 2400.0 * osc * nCiclos(); }
  float gauss(float s) { std::normal_distribution<float> d(0.0f, s); return d(rng); }

  void avancar() {
    if ((reg[0] & 0x03) != 0x03 || tAen < 0) return;
    while (tAen + (ciclo + 1) * tCiclo() <= sim::us) {
      fechar(tAen + (ciclo + 1) * tCiclo());
      ciclo++;
    }
  }

  void fechar(double fim) {
    const double ini = fim - tInt();
    float acc[3] = {0, 0, 0};
    for (int k = 0; k < 8; k++) {
      const double t = ini + (k + 0.5) / 8.0 * tInt();
      float x[3];
      cena(t, x);
      for (int c = 0; c < 3; c++) acc[c] += 0.02f + 0.98f * x[c];  // 2 % de luz interna LED -> sensor
    }
    const int n = nCiclos();
    const float g = ganhoReal[reg[0x0F] & 3];
    const float maximo = (float)std::min(65535, 1024 * n);
    float v[4];
    for (int c = 0; c < 3; c++) {
      float cont = base[c] * g * n * (acc[c] / 8.0f) * (1.0f + gauss(0.003f));
      cont += gauss(1.0f + 0.002f * cont);
      v[c] = std::max(0.0f, std::min(maximo, floorf(cont)));
    }
    v[3] = std::max(0.0f, std::min(maximo, floorf((v[0] + v[1] + v[2]) / 1.092f + gauss(2.0f))));
    const int ordem[4] = {3, 0, 1, 2};  // C, R, G, B
    for (int i = 0; i < 4; i++) {
      const uint16_t u = (uint16_t)v[ordem[i]];
      reg[0x14 + 2 * i] = u & 0xFF;
      reg[0x15 + 2 * i] = u >> 8;
    }
    avalid = true;
    if (comAint) aint = true;  // PERS = 0: todo ciclo
    ciclosFechados++;
  }

  void escreverReg(uint8_t r, uint8_t v) {
    if (r == 0x00) {
      const bool antes = (reg[0] & 0x03) == 0x03;
      reg[0] = v & 0x1B;
      const bool agora = (reg[0] & 0x03) == 0x03;
      if (agora && !antes) { tAen = sim::us; ciclo = 0; }
      if (!agora) tAen = -1;
    } else if (r < 0x12) {
      reg[r] = v;
    }
  }
  bool escrever(const uint8_t *b, int n) override {
    avancar();
    const uint8_t cmd = b[0];
    if (!(cmd & 0x80)) return true;
    const uint8_t tipo = (cmd >> 5) & 3;
    if (tipo == 3) {
      if ((cmd & 0x1F) == 0x06) aint = false;
      return true;
    }
    ptr = cmd & 0x1F;
    autoInc = (tipo == 1);
    for (int i = 1; i < n; i++) {
      escreverReg(ptr, b[i]);
      if (autoInc) ptr = (ptr + 1) & 0x1F;
    }
    return true;
  }
  int ler(uint8_t *b, int n) override {
    avancar();
    for (int i = 0; i < n; i++) {
      const uint8_t r = ptr;
      if (r == 0x13) b[i] = (aint ? 0x10 : 0) | (avalid ? 0x01 : 0);
      else if (r == 0x12) b[i] = idValor;
      else b[i] = reg[r];
      if (r == 0x14) {
        leiturasDados++;
        if (ultimoLido == ciclosFechados) repetidas++;
        ultimoLido = ciclosFechados;
      }
      if (autoInc) ptr = (ptr + 1) & 0x1F;
    }
    return n;
  }
};

struct MuxFalso : sim::Dispositivo {
  int bus;
  explicit MuxFalso(int b) : bus(b) { endereco = 0x70; }
  bool escrever(const uint8_t *b, int n) override { sim::barramentos[bus].mascaraMux = b[n - 1]; return true; }
  int ler(uint8_t *b, int n) override { for (int i = 0; i < n; i++) b[i] = sim::barramentos[bus].mascaraMux; return n; }
};
