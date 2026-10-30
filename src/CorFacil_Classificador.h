/*
 * CorFacil_Classificador.h
 * ------------------------
 * A matemática que decide a cor. C++ puro, sem Arduino: o mesmo arquivo roda
 * na placa e no teste do PC (extras/teste_pc).
 *
 * É a parte de PRETO, BRANCO, VERDE e VERMELHO do ClassificadorCor.h que
 * funcionou no teste na mão (sensor-cor-tcs34725, outubro de 2026), com as
 * mesmas contas e os mesmos limites. O que mudou: só as 4 cores (sem laranja
 * e sem prata), as amostras da calibração guardadas em 16 bits (cabem na RAM
 * do Arduino Uno) e a calibração salva em 92 bytes.
 *
 * COMO FUNCIONA:
 *  1. Balanço pelo branco e pelo preto calibrados, canal por canal:
 *         p = (leitura / branco - preto) / (1 - preto)
 *     O preto vira (0,0,0) e o branco (1,1,1), seja qual for o tom do LED.
 *  2. Separa brilho de cor: brilho = média dos 3 canais; croma = vetor 2D que
 *     é ZERO para qualquer cinza (0° = vermelho, 120° = verde, 240° = azul).
 *     A borda da linha é preto misturado com branco, e isso é cinza: croma ≈ 0.
 *     Por isso a borda da linha não vira verde, nem quando fica escura.
 *  3. Para VERDE e VERMELHO calcula a fração t da cor sob o sensor (1 em cima
 *     dela, 0,5 com metade do sensor sobre ela). Vale se t ≥ 0,40, se a
 *     direção (matiz) bate e se a cor não está mais fraca que o branco
 *     misturado a ela.
 *  4. Sem cor: BRANCO se o brilho ≥ 0,5, PRETO se < 0,5. Cor que não é nenhuma
 *     das calibradas (azul, amarelo, laranja): INDEFINIDO.
 *  5. Filtro: VERDE, VERMELHO e INDEFINIDO só valem com 2 leituras seguidas
 *     iguais; PRETO e BRANCO valem com 1 (a linha é fina).
 */
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "CorFacil_Cores.h"

namespace corfacil {

const int N_COLORIDAS = 2;  // VERDE, VERMELHO (índice em Calibracao::colorida)
inline int indiceColorida(uint8_t c) { return c == VERDE ? 0 : c == VERMELHO ? 1 : -1; }
inline Cor corColorida(int i) { return i == 0 ? VERDE : VERMELHO; }

const float PRETO_PADRAO = 0.06f;      // preto típico (fração do branco) enquanto o preto não é calibrado
const int MAX_AMOSTRAS = 40;           // amostras por calibração (~0,8 s)
const uint32_t MAGICA = 0x31434643UL;  // "CFC1": formato da calibração salva
const float GRAUS = 57.2957795f;

// Ajustes do classificador. Saíram do modelo e do teste do PC; são os mesmos
// do código que funcionou. Mude só sabendo o motivo e rode extras/teste_pc.
namespace par {
const float fracaoMin = 0.40f;            // t mínimo: pelo menos 40 % da cor sob o sensor
const float dominanciaMin = 0.40f;        // cor / (cor + branco misturado) mínima
const float brilhoMeio = 0.50f;           // abaixo = PRETO, acima = BRANCO (0 = preto, 1 = branco)
const float tolVerdeGraus = 25.0f;        // VERDE: nada parecido na pista, tolerância larga
const float tolVermelhoGraus = 12.0f;     // VERMELHO: o laranja do checkpoint fica perto
const float maxVerde = 2.5f;              // t máximo: "cor forte demais" não é esta cor
const float maxVermelho = 1.5f;
const float ladoLaranjaGraus = 3.0f;      // VERMELHO: desvio máximo para o lado do laranja (~6° à frente)
const float espMatizMinGraus = 1.5f;      // piso do espalhamento de matiz de uma cor
const float cromaIndefinido = 0.35f;      // fração do menor croma calibrado que, sobrando, vira INDEFINIDO
const float saturacaoIndefinido = 0.15f;  // e croma/brilho mínimo para INDEFINIDO (branco amarelado continua BRANCO)
const float ruidoMin = 0.004f;            // piso do ruído de croma
const float folgaRuido = 3.0f;            // folga extra, em ruídos, na direção da cor
inline float tolMatizGraus(int i) { return i == 0 ? tolVerdeGraus : tolVermelhoGraus; }
inline float fracaoMax(int i) { return i == 0 ? maxVerde : maxVermelho; }
}  // namespace par

// Uma leitura do sensor, em contagens.
struct Bruta {
  float r, g, b, c;
  bool saturada;
};

// Uma amostra de calibração (16 bits por canal: 40 amostras = 320 bytes de pilha).
// "Saturada" não é guardada: é C >= limite de saturação, a mesma conta do driver.
struct Amostra {
  uint16_t r, g, b, c;
};

// Uma cor calibrada, guardada como fração do branco: continua valendo quando o
// branco é recalibrado (outra luz, outro ganho).
struct Ref {
  float x[3];    // R, G, B divididos pelos do branco
  float esp;     // PRETO: ruído de croma das amostras; VERDE/VERMELHO: espalhamento do matiz (radianos)
  uint8_t ok;
  uint8_t reservado[3];
};

struct Calibracao {
  uint32_t magica;
  float branco[4];        // contagens médias no branco: R, G, B, C
  float ruidoBranco;      // ruído de croma medido no branco
  uint8_t ganho, ciclos;  // exposição em que o branco foi medido
  uint8_t brancoOk, reservado;
  Ref preto;
  Ref colorida[N_COLORIDAS];  // VERDE, VERMELHO
  uint32_t soma;              // soma de verificação de tudo o que vem antes
};
static_assert(sizeof(Ref) == 20, "formato da calibracao mudou");

inline void zerar(Calibracao &cal) {
  memset(&cal, 0, sizeof(cal));
  cal.magica = MAGICA;
}

struct Resultado {
  Cor cor;
  float brilho;          // 0 = preto calibrado, 1 = branco calibrado
  float croma;           // quanta cor (0 = cinza)
  float cx, cy;          // vetor de croma: a direção é o matiz (veja matizDe)
  float t, dom, dir;     // VERDE/VERMELHO: fração da cor, dominância e desvio de matiz (0..1)
  float nota1, nota2;    // notas da 1a e da 2a cor aceitas (menor = mais provável)
  bool saturada;         // mais claro que o limite do sensor (reflexo, sensor encostando)
  bool semCalibracao;    // falta calibrar o branco
};

// Matiz em graus (0 vermelho, 120 verde, 240 azul). Fica fora de classificar()
// porque só serve para mostrar: no Uno, cada conta a menos é memória a mais.
inline float matizDe(const Resultado &r) {
  float m = atan2f(r.cy, r.cx) * GRAUS;
  return m < 0.0f ? m + 360.0f : m;
}

inline float limitar01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Distância entre dois ângulos em graus (0..180), com a e b entre 0 e 360.
inline float distAngulo(float a, float b) {
  float d = fabsf(a - b);
  return d > 180.0f ? 360.0f - d : d;
}

// Brilho e croma de um ponto p.
inline void decompor(const float p[3], float &Y, float &cx, float &cy) {
  Y = (p[0] + p[1] + p[2]) * (1.0f / 3.0f);
  cx = p[0] - 0.5f * (p[1] + p[2]);
  cy = 0.8660254f * (p[1] - p[2]);
}

class Classificador {
 public:
  // Pré-calcula o que depende só da calibração. Chame de novo sempre que ela mudar.
  bool preparar(const Calibracao &cal) {
    _pronto = false;
    if (!cal.brancoOk) return false;
    for (int i = 0; i < 3; i++) {
      if (!(cal.branco[i] > 0.5f)) return false;
      _invW[i] = 1.0f / cal.branco[i];
      float k = cal.preto.ok ? cal.preto.x[i] : PRETO_PADRAO;
      if (!(k >= 0.0f) || k > 0.9f) k = PRETO_PADRAO;
      _k[i] = k;
      _inv1mk[i] = 1.0f / (1.0f - k);
    }
    _ruido = par::ruidoMin;
    if (cal.ruidoBranco > _ruido) _ruido = cal.ruidoBranco;
    if (cal.preto.ok && cal.preto.esp > _ruido) _ruido = cal.preto.esp;

    float menorCroma = -1.0f;
    float matiz[N_COLORIDAS] = {}, tolGraus[N_COLORIDAS] = {};
    for (int i = 0; i < N_COLORIDAS; i++) {
      Modelo &m = _m[i];
      const Ref &r = cal.colorida[i];
      m.ok = false;
      if (!r.ok) continue;
      float p[3];
      for (int c = 0; c < 3; c++) p[c] = (r.x[c] - _k[c]) * _inv1mk[c];
      decompor(p, m.Y, m.cx, m.cy);
      m.mod2 = m.cx * m.cx + m.cy * m.cy;
      m.mod = sqrtf(m.mod2);
      if (m.mod < 1e-3f) continue;  // calibrado em cima de um cinza: não serve
      matiz[i] = atan2f(m.cy, m.cx) * GRAUS;
      if (matiz[i] < 0.0f) matiz[i] += 360.0f;
      float tol = par::tolMatizGraus(i);
      float tolDados = 3.5f * r.esp * GRAUS;  // se a cor variou muito na calibração, alarga
      if (tolDados > tol) tol = tolDados;
      if (tol > 45.0f) tol = 45.0f;
      tolGraus[i] = tol;
      m.tanTol = tanf(tol / GRAUS);
      float esp = r.esp * GRAUS;
      if (esp < par::espMatizMinGraus) esp = par::espMatizMinGraus;
      m.espMatiz = esp / GRAUS;
      m.tMax = par::fracaoMax(i);
      m.tanLadoMais = m.tanLadoMenos = m.tanTol;
      m.ok = true;
      if (menorCroma < 0.0f || m.mod < menorCroma) menorCroma = m.mod;
    }
    // Duas cores com matiz perto: cada uma fica com a metade do caminho do seu lado.
    for (int i = 0; i < N_COLORIDAS; i++) {
      if (!_m[i].ok) continue;
      for (int j = 0; j < N_COLORIDAS; j++) {
        if (j == i || !_m[j].ok) continue;
        float d = matiz[j] - matiz[i];
        while (d > 180.0f) d -= 360.0f;
        while (d <= -180.0f) d += 360.0f;
        if (fabsf(d) >= 2.0f * tolGraus[i]) continue;
        float tm = tanf(0.5f * fabsf(d) / GRAUS);
        if (d > 0.0f && tm < _m[i].tanLadoMais) _m[i].tanLadoMais = tm;
        if (d < 0.0f && tm < _m[i].tanLadoMenos) _m[i].tanLadoMenos = tm;
      }
    }
    // O VERMELHO quase não pode desviar para o lado do laranja (matiz maior, rumo
    // ao amarelo): para o TCS34725 o laranja fica ~6 graus à frente.
    if (_m[1].ok) {
      float tl = tanf(par::ladoLaranjaGraus / GRAUS);
      if (tl < _m[1].tanLadoMais) _m[1].tanLadoMais = tl;
    }
    float base = (menorCroma > 0.0f) ? par::cromaIndefinido * menorCroma : 0.06f;
    _cromaIndef = base > 4.0f * _ruido ? base : 4.0f * _ruido;
    _pronto = true;
    return true;
  }

  bool pronto() const { return _pronto; }
  bool temCor(uint8_t c) const { int i = indiceColorida(c); return i >= 0 && _pronto && _m[i].ok; }
  float matizRef(int i) const {
    float m = atan2f(_m[i].cy, _m[i].cx) * GRAUS;
    return m < 0.0f ? m + 360.0f : m;
  }
  float cromaRef(int i) const { return _m[i].mod; }
  float brilhoRef(int i) const { return _m[i].Y; }
  // tolerância de matiz de cada lado, em graus (+ = rumo ao amarelo/verde, - = rumo ao azul/roxo)
  float ladoMaisGraus(int i) const { return atanf(_m[i].tanLadoMais) * GRAUS; }
  float ladoMenosGraus(int i) const { return atanf(_m[i].tanLadoMenos) * GRAUS; }
  float ruido() const { return _ruido; }
  float cromaIndefinido() const { return _cromaIndef; }
  float pretoRelativo() const { return (_k[0] + _k[1] + _k[2]) * (1.0f / 3.0f); }

  Resultado classificar(const Bruta &b) const {
    Resultado r;
    memset(&r, 0, sizeof(r));
    r.cor = INDEFINIDO;
    r.saturada = b.saturada;
    if (!_pronto) {
      r.semCalibracao = true;
      return r;
    }
    const float bruto[3] = {b.r, b.g, b.b};
    float p[3];
    for (int c = 0; c < 3; c++) p[c] = (bruto[c] * _invW[c] - _k[c]) * _inv1mk[c];
    float Y, cx, cy;
    decompor(p, Y, cx, cy);
    r.brilho = Y;
    r.croma = sqrtf(cx * cx + cy * cy);
    r.cx = cx;
    r.cy = cy;

    int melhor = -1;
    float nota1 = 1e9f, nota2 = 1e9f, tMelhor = 0.0f, dMelhor = 0.0f, dirMelhor = 0.0f;
    for (int i = 0; i < N_COLORIDAS; i++) {
      const Modelo &m = _m[i];
      if (!m.ok) continue;
      // t: quanto do croma da cor calibrada aparece na leitura (projeção)
      float t = (cx * m.cx + cy * m.cy) / m.mod2;
      if (t < par::fracaoMin || t > m.tMax) continue;
      // Dominância: a leitura vista como t × cor + beta × branco (+ preto, que
      // não soma nada). Cor clara demais (beta grande) não conta.
      float beta = Y - t * m.Y;
      if (beta < 0.0f) beta = 0.0f;
      float dom = t / (t + beta);
      if (dom < par::dominanciaMin) continue;
      // Desvio de direção (matiz) medido como distância ao "raio" da cor;
      // positivo = matiz maior que o calibrado (do vermelho rumo ao amarelo).
      float desvio = (m.cx * cy - m.cy * cx) / m.mod;
      float lado = desvio > 0.0f ? m.tanLadoMais : m.tanLadoMenos;
      float folga = t * m.mod * lado + (lado < m.tanTol ? 0.0f : par::folgaRuido * _ruido);
      float sDir = fabsf(desvio) / folga;
      if (sDir > 1.0f) continue;
      // Nota para escolher entre cores aceitas (menor = mais provável).
      float sigma2 = _ruido * _ruido + (t * m.mod * m.espMatiz) * (t * m.mod * m.espMatiz);
      float forte = (t > 1.0f) ? logf(t) / 0.25f : 0.0f;
      float nota = desvio * desvio / sigma2 + forte * forte;
      if (nota < nota1) {
        nota2 = nota1;
        nota1 = nota;
        melhor = i;
        tMelhor = t;
        dMelhor = dom;
        dirMelhor = sDir;
      } else if (nota < nota2) {
        nota2 = nota;
      }
    }
    if (r.saturada) {  // mais claro que o limite do sensor: reflexo, flash, sensor encostando no chão
      r.cor = BRANCO;
      return r;
    }
    if (melhor >= 0) {
      r.cor = corColorida(melhor);
      r.t = tMelhor;
      r.dom = dMelhor;
      r.dir = dirMelhor;
      r.nota1 = nota1;
      r.nota2 = nota2;
      return r;
    }
    // Tem cor, mas não é nenhuma das calibradas. Branco puxado para o amarelo
    // pela luz do ambiente tem croma, mas pouco perto do brilho dele: continua BRANCO.
    float brilhoRef = Y > 0.05f ? Y : 0.05f;
    if (r.croma >= _cromaIndef && r.croma >= par::saturacaoIndefinido * brilhoRef) {
      r.cor = INDEFINIDO;
      return r;
    }
    r.cor = (Y >= par::brilhoMeio) ? BRANCO : PRETO;
    return r;
  }

  // Confiança da cor de uma leitura (0..1), as mesmas contas do código original.
  float confianca(const Resultado &r) const {
    if (r.semCalibracao || r.cor == INDEFINIDO) return 0.0f;
    if (r.saturada) return 0.3f;
    if (r.cor == VERDE || r.cor == VERMELHO) {
      float c1 = limitar01((r.t - par::fracaoMin) / 0.3f);        // cor suficiente
      float c2 = limitar01((r.dom - par::dominanciaMin) / 0.3f);  // pouco branco misturado
      float c3 = limitar01(1.5f * (1.0f - r.dir));                // matiz no lugar
      float c4 = (r.nota2 < 1e8f) ? limitar01((r.nota2 - r.nota1) / 4.0f) : 1.0f;  // longe da 2a colocada
      float c = c1 < c2 ? c1 : c2;
      if (c3 < c) c = c3;
      if (c4 < c) c = c4;
      return c;
    }
    float c1 = limitar01(fabsf(r.brilho - par::brilhoMeio) / 0.3f);
    float c2 = limitar01(1.0f - r.croma / _cromaIndef);
    return c1 < c2 ? c1 : c2;
  }

 private:
  struct Modelo {
    bool ok;
    float Y, cx, cy, mod2, mod, tanTol, tMax, espMatiz, tanLadoMais, tanLadoMenos;
  };
  bool _pronto = false;
  float _invW[3] = {};
  float _k[3] = {};
  float _inv1mk[3] = {};
  float _ruido = 0.0f;
  float _cromaIndef = 0.0f;
  Modelo _m[N_COLORIDAS] = {};
};

// A cor estável só vira VERDE, VERMELHO ou INDEFINIDO depois de n leituras
// seguidas iguais: uma leitura isolada (borda, flash, poeira) não dispara
// nada. PRETO e BRANCO valem com nNeutras (1): a linha tem 10 a 20 mm, e a
// 30 cm/s fica só 2 ou 3 leituras sob o sensor.
class FiltroEstavel {
 public:
  uint8_t n = 2;
  uint8_t nNeutras = 1;

  Cor atualizar(Cor c) {
    if (c == _candidata) {
      if (_seguidas < 255) _seguidas++;
    } else {
      _candidata = c;
      _seguidas = 1;
    }
    const uint8_t precisa = (c == PRETO || c == BRANCO) ? nNeutras : n;
    if (_seguidas >= precisa) {
      _estavel = c;
      _confirmou = true;
    }
    return _estavel;
  }

  Cor estavel() const { return _estavel; }
  bool confirmou() const { return _confirmou; }  // já teve uma cor confirmada desde reiniciar()
  void reiniciar(Cor c = INDEFINIDO) {
    _estavel = _candidata = c;
    _seguidas = 0;
    _confirmou = false;
  }

 private:
  Cor _estavel = INDEFINIDO, _candidata = INDEFINIDO;
  uint8_t _seguidas = 0;
  bool _confirmou = false;
};

// ---------------------------------------------------------------------------
// Calibração: transforma um punhado de leituras sobre uma cor numa Ref.
// ---------------------------------------------------------------------------

enum Aviso : uint16_t {
  AV_SEM_BRANCO = 1u << 0,       // calibre o branco primeiro
  AV_POUCAS_AMOSTRAS = 1u << 1,  // muita leitura descartada: sensor na borda ou mexendo
  AV_SATURADO = 1u << 2,         // amostras saturadas
  AV_PRETO_CLARO = 1u << 3,      // "preto" com mais de 35 % do branco
  AV_COR_FRACA = 1u << 4,        // quase sem cor: parece cinza
  AV_COR_ESCURA = 1u << 5,       // cor quase tão escura quanto o preto
  AV_RUIDO_ALTO = 1u << 7,       // leitura instável
  AV_BRANCO_FRACO = 1u << 8,     // pouca luz no branco (LED apagado? sensor longe?)
  AV_INVALIDO = 1u << 9,         // valor impossível (ex.: preto mais claro que o branco)
  AV_SEM_SENSOR = 1u << 10,      // sensor não respondeu
  AV_EXPOSICAO = 1u << 11,       // o ganho ou o tempo de integração mudou
  AV_NAO_SALVOU = 1u << 12,      // não deu para gravar na memória
  AV_MATIZ_ESTRANHO = 1u << 13,  // matiz longe do esperado para essa cor (era a cor certa?)
  AV_COR_INVALIDA = 1u << 14     // pediu para calibrar algo que não é PRETO, BRANCO, VERDE ou VERMELHO
};

struct Relatorio {
  bool ok;
  uint16_t avisos;
  int usadas, total;
  float brilho;       // 0..1 (preto: fração do branco)
  float croma, matiz;
  float espMatizGraus;
};

struct Resumo {
  float media[4];
  float mediana;
  int usadas, total, saturadas;
};

// Média robusta: usa só as amostras cujo C está a ±20 % da mediana. Joga fora
// as que pegaram borda (mais claras ou mais escuras que a cor).
inline bool dentroDaJanela(const Amostra &a, float mediana, uint16_t limite) {
  return a.c < limite && a.c >= 0.80f * mediana && a.c <= 1.20f * mediana;
}

inline void ordenarFloat(float *v, int n) {
  for (int i = 1; i < n; i++) {  // inserção (n é pequeno)
    float x = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > x) {
      v[j + 1] = v[j];
      j--;
    }
    v[j + 1] = x;
  }
}

// k-ésimo menor C (k = 0 é o menor) entre as amostras não saturadas, sem copiar
// nem ordenar nada: no Uno, a pilha da calibração não aguenta mais um vetor.
inline uint16_t kEsimoC(const Amostra *a, int n, uint16_t limite, int k) {
  for (int i = 0; i < n; i++) {
    if (a[i].c >= limite) continue;
    int menores = 0, iguais = 0;
    for (int j = 0; j < n; j++) {
      if (a[j].c >= limite) continue;
      if (a[j].c < a[i].c) menores++;
      else if (a[j].c == a[i].c) iguais++;
    }
    if (menores <= k && k < menores + iguais) return a[i].c;
  }
  return 0;
}

inline Resumo resumir(const Amostra *a, int n, uint16_t limite) {
  Resumo s;
  memset(&s, 0, sizeof(s));
  if (n > MAX_AMOSTRAS) n = MAX_AMOSTRAS;
  s.total = n;
  int m = 0;
  for (int i = 0; i < n; i++) {
    if (a[i].c >= limite) s.saturadas++;
    else m++;
  }
  if (m == 0) return s;
  s.mediana = (m & 1) ? (float)kEsimoC(a, n, limite, m / 2)
                      : 0.5f * ((float)kEsimoC(a, n, limite, m / 2 - 1) + (float)kEsimoC(a, n, limite, m / 2));
  if (!(s.mediana >= 1.0f)) return s;
  double media[4] = {0, 0, 0, 0};
  int k = 0;
  for (int i = 0; i < n; i++) {
    if (!dentroDaJanela(a[i], s.mediana, limite)) continue;
    k++;
    const double v[4] = {(double)a[i].r, (double)a[i].g, (double)a[i].b, (double)a[i].c};
    for (int c = 0; c < 4; c++) media[c] += (v[c] - media[c]) / k;  // média corrida (Welford)
  }
  s.usadas = k;
  for (int c = 0; c < 4; c++) s.media[c] = (float)media[c];
  return s;
}

// p de uma amostra, com o branco e o preto dados.
inline void balancear(const Amostra &a, const float W[3], const float k[3], float p[3]) {
  const float v[3] = {(float)a.r, (float)a.g, (float)a.b};
  for (int c = 0; c < 3; c++) p[c] = (v[c] / W[c] - k[c]) / (1.0f - k[c]);
}

inline void pretoAtual(const Calibracao &cal, float k[3]) {
  for (int c = 0; c < 3; c++) k[c] = cal.preto.ok ? cal.preto.x[c] : PRETO_PADRAO;
}

// Ruído de croma (desvio padrão) das amostras em volta da própria média.
inline float ruidoCroma(const Amostra *a, int n, const Resumo &s, const float W[3], const float k[3],
                        uint16_t limite) {
  double sx = 0, sy = 0, sxx = 0, syy = 0;
  int q = 0;
  for (int i = 0; i < n && i < MAX_AMOSTRAS; i++) {
    if (!dentroDaJanela(a[i], s.mediana, limite)) continue;
    float p[3], Y, cx, cy;
    balancear(a[i], W, k, p);
    decompor(p, Y, cx, cy);
    sx += cx; sy += cy; sxx += (double)cx * cx; syy += (double)cy * cy;
    q++;
  }
  if (q < 2) return 0.0f;
  double vx = (sxx - sx * sx / q) / (q - 1), vy = (syy - sy * sy / q) / (q - 1);
  double v = 0.5 * (vx + vy);
  return v > 0 ? (float)sqrt(v) : 0.0f;
}

inline void relatorioVazio(Relatorio &rel, int total) {
  memset(&rel, 0, sizeof(rel));
  rel.total = total;
}

// BRANCO: guarda as contagens médias e a exposição. É a referência de tudo.
// Os resultados saem em rel (por referência: no Uno, cada cópia custa pilha).
inline void calibrarBranco(Relatorio &rel, Calibracao &cal, const Amostra *a, int n, uint8_t ganho, uint8_t ciclos,
                           uint16_t limite) {
  relatorioVazio(rel, n);
  Resumo s = resumir(a, n, limite);
  rel.usadas = s.usadas;
  if (s.saturadas * 4 > n) rel.avisos |= AV_SATURADO;
  if (s.usadas < 8 || !(s.media[0] > 0.5f && s.media[1] > 0.5f && s.media[2] > 0.5f)) {
    rel.avisos |= AV_POUCAS_AMOSTRAS;
    return;
  }
  if (s.usadas * 10 < n * 6) rel.avisos |= AV_POUCAS_AMOSTRAS;
  for (int c = 0; c < 4; c++) cal.branco[c] = s.media[c];
  cal.ganho = ganho;
  cal.ciclos = ciclos;
  cal.brancoOk = 1;
  cal.magica = MAGICA;
  float k[3];
  pretoAtual(cal, k);
  cal.ruidoBranco = ruidoCroma(a, n, s, cal.branco, k, limite);
  if (cal.ruidoBranco > 0.02f) rel.avisos |= AV_RUIDO_ALTO;
  rel.ok = true;
  rel.brilho = 1.0f;
  return;
}

// PRETO, VERDE, VERMELHO: guarda como fração do branco atual.
inline void calibrarCor(Relatorio &rel, Calibracao &cal, Cor alvo, const Amostra *a, int n, uint16_t limite) {
  relatorioVazio(rel, n);
  if (!cal.brancoOk) {
    rel.avisos |= AV_SEM_BRANCO;
    return;
  }
  Resumo s = resumir(a, n, limite);
  rel.usadas = s.usadas;
  if (s.saturadas * 4 > n) rel.avisos |= AV_SATURADO;
  if (s.usadas < 8) {
    rel.avisos |= AV_POUCAS_AMOSTRAS;
    return;
  }
  if (s.usadas * 10 < n * 6) rel.avisos |= AV_POUCAS_AMOSTRAS;
  float x[3];
  for (int c = 0; c < 3; c++) x[c] = s.media[c] / cal.branco[c];

  if (alvo == PRETO) {
    if (x[0] > 0.9f || x[1] > 0.9f || x[2] > 0.9f) {
      rel.avisos |= AV_INVALIDO;  // "preto" quase igual ao branco
      return;
    }
    float media = (x[0] + x[1] + x[2]) / 3.0f;
    if (media > 0.35f) rel.avisos |= AV_PRETO_CLARO;
    for (int c = 0; c < 3; c++) cal.preto.x[c] = x[c];
    cal.preto.esp = ruidoCroma(a, n, s, cal.branco, x, limite);
    cal.preto.ok = 1;
    if (cal.preto.esp > 0.02f) rel.avisos |= AV_RUIDO_ALTO;
    rel.ok = true;
    rel.brilho = media;
    return;
  }

  int i = indiceColorida(alvo);
  if (i < 0) {
    rel.avisos |= AV_INVALIDO;
    return;
  }
  float k[3], p[3], Y, cx, cy;
  pretoAtual(cal, k);
  for (int c = 0; c < 3; c++) p[c] = (x[c] - k[c]) / (1.0f - k[c]);
  decompor(p, Y, cx, cy);
  float mod = sqrtf(cx * cx + cy * cy);
  rel.brilho = Y;
  rel.croma = mod;
  rel.matiz = atan2f(cy, cx) * GRAUS;
  if (rel.matiz < 0.0f) rel.matiz += 360.0f;
  if (mod < 0.02f) {  // isso é cinza: calibrou em cima do preto ou do branco?
    rel.avisos |= AV_COR_FRACA | AV_INVALIDO;
    return;
  }
  if (mod < 0.05f) rel.avisos |= AV_COR_FRACA;
  if (Y < 0.04f) rel.avisos |= AV_COR_ESCURA;
  // Só um aviso (salva igual): VERDE fica entre ~100 e 160 graus, VERMELHO perto de 0.
  if (alvo == VERDE && (rel.matiz < 70.0f || rel.matiz > 180.0f)) rel.avisos |= AV_MATIZ_ESTRANHO;
  if (alvo == VERMELHO && distAngulo(rel.matiz, 0.0f) > 40.0f) rel.avisos |= AV_MATIZ_ESTRANHO;

  // espalhamento entre as amostras: desvio de direção
  double se = 0, see = 0;
  int q = 0;
  for (int j = 0; j < n && j < MAX_AMOSTRAS; j++) {
    if (!dentroDaJanela(a[j], s.mediana, limite)) continue;
    float pj[3], Yj, cxj, cyj;
    balancear(a[j], cal.branco, k, pj);
    decompor(pj, Yj, cxj, cyj);
    float e = (cxj * cy - cyj * cx) / mod;
    se += e; see += (double)e * e;
    q++;
  }
  Ref &ref = cal.colorida[i];
  for (int c = 0; c < 3; c++) ref.x[c] = x[c];
  double var = q > 1 ? (see - se * se / q) / (q - 1) : 0.0;
  ref.esp = q > 1 ? (float)sqrt(var > 0.0 ? var : 0.0) / mod : 0.0f;
  ref.ok = 1;
  rel.espMatizGraus = ref.esp * GRAUS;
  if (rel.espMatizGraus > 6.0f) rel.avisos |= AV_RUIDO_ALTO;
  rel.ok = true;
  return;
}

// Esquece uma cor (ou o branco). O resto da calibração continua.
inline void esquecer(Calibracao &cal, uint8_t alvo) {
  if (alvo == BRANCO) cal.brancoOk = 0;
  else if (alvo == PRETO) cal.preto.ok = 0;
  else if (indiceColorida(alvo) >= 0) cal.colorida[indiceColorida(alvo)].ok = 0;
}

inline bool calibrada(const Calibracao &cal, uint8_t c) {
  if (c == BRANCO) return cal.brancoOk != 0;
  if (c == PRETO) return cal.preto.ok != 0;
  int i = indiceColorida(c);
  return i >= 0 && cal.colorida[i].ok != 0;
}

// Soma de verificação (FNV-1a) de tudo antes do campo soma.
inline uint32_t somaVerificacao(const Calibracao &cal) {
  const uint8_t *b = reinterpret_cast<const uint8_t *>(&cal);
  const unsigned n = (unsigned)offsetof(Calibracao, soma);
  uint32_t h = 2166136261UL;
  for (unsigned i = 0; i < n; i++) {
    h ^= b[i];
    h *= 16777619UL;
  }
  return h;
}

}  // namespace corfacil
