// Teste do classificador (CorFacil_Classificador.h) no PC, com um sensor simulado.
// É o teste do projeto original (sensor-cor-tcs34725/teste_pc), com os números
// que ele mostrou virando conferências.
//
// O que é simulado (modelo espectral e datasheet):
//  - materiais como fração do branco em R, G, B (LED branco x refletância x resposta do TCS3472);
//  - "mancha" do sensor no chão: gaussiana (sigma 2 a 4,5 mm);
//  - luz parasita interna do módulo (2 % do branco), altura/LED (escala), textura do piso,
//    ruído por canal e luz ambiente quente;
//  - calibração com 40 amostras, 15 % delas pegando borda (como quem desliza o sensor).

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>
#include "../../src/CorFacil_Classificador.h"

using namespace corfacil;

const int N_CORES = 7;

struct Mat { const char *nome; float x[3]; };
static const Mat M_BRANCO = {"branco", {1.000f, 1.000f, 1.000f}};
static const Mat M_PRETO = {"preto", {0.070f, 0.070f, 0.070f}};
static const Mat M_VERDE = {"verde", {0.166f, 0.340f, 0.165f}};
static const Mat M_VERDE_ESC = {"verde escuro", {0.089f, 0.177f, 0.101f}};
static const Mat M_VERDE_AMAR = {"verde amarelado", {0.277f, 0.398f, 0.147f}};
static const Mat M_VERMELHO = {"vermelho", {0.415f, 0.124f, 0.079f}};
static const Mat M_VERM_ESC = {"vermelho escuro", {0.236f, 0.082f, 0.062f}};
static const Mat M_LARANJA = {"laranja", {0.659f, 0.234f, 0.105f}};
static const Mat M_VERM_LAR = {"vermelho alaranj.", {0.507f, 0.157f, 0.094f}};
static const Mat M_LAR_VERM = {"laranja avermelh.", {0.561f, 0.178f, 0.097f}};
static const Mat M_LAR_AMAR = {"laranja amarelado", {0.727f, 0.295f, 0.114f}};
static const Mat M_AMARELO = {"amarelo", {0.898f, 0.724f, 0.231f}};
static const Mat M_AZUL = {"azul", {0.100f, 0.169f, 0.461f}};
static const Mat M_PRATA = {"fita de aluminio", {0.820f, 0.820f, 0.825f}};
static const Mat M_SILVER = {"silver tape", {0.550f, 0.550f, 0.560f}};

// Branco no sensor: C = 2400 (~45 % do limite de saturação com 7 ciclos)
static const float W[4] = {761.0f, 1020.0f, 840.0f, 2400.0f};
static const float LIMITE_SAT = 0.75f * 1024.0f * 7.0f;  // 5376
static const float PARASITA = 0.02f;                       // luz interna LED->sensor

struct Cena {
  struct Trecho { float x0, x1; const Mat *m; };
  std::vector<Trecho> t;
  const Mat *em(float x) const {
    for (auto &q : t) if (x >= q.x0 && x < q.x1) return q.m;
    return &M_BRANCO;
  }
};

struct Condicao {
  float escala = 1.0f;    // altura/LED (1 = como na calibração)
  float ambiente = 0.0f;  // luz ambiente (fração do sinal do LED no branco)
  float sigmaMancha = 3.0f;
};

static std::mt19937 rng(12345);
static float gauss(float s) { std::normal_distribution<float> d(0.0f, s); return d(rng); }
static float unif(float a, float b) { std::uniform_real_distribution<float> d(a, b); return d(rng); }

static int falhas = 0;
static void confere(bool ok, const std::string &o_que) {
  printf("%s %s\n", ok ? "  [OK]   " : "  [FALHA]", o_que.c_str());
  if (!ok) falhas++;
}
static std::string pct(double v) { char b[32]; snprintf(b, sizeof b, "%.2f%%", v); return b; }

static void mistura(const Cena &cena, float xc, float sig, float out[3]) {
  float soma = 0, acc[3] = {0, 0, 0};
  for (float dx = -3.5f * sig; dx <= 3.5f * sig; dx += 0.1f) {
    float w = expf(-0.5f * dx * dx / (sig * sig));
    const Mat *m = cena.em(xc + dx);
    for (int c = 0; c < 3; c++) acc[c] += w * m->x[c];
    soma += w;
  }
  for (int c = 0; c < 3; c++) out[c] = acc[c] / soma;
}

static float cobertura(const Cena &cena, float xc, float sig, const Mat *alvo) {
  float soma = 0, acc = 0;
  for (float dx = -3.5f * sig; dx <= 3.5f * sig; dx += 0.1f) {
    float w = expf(-0.5f * dx * dx / (sig * sig));
    if (cena.em(xc + dx) == alvo) acc += w;
    soma += w;
  }
  return acc / soma;
}

static Amostra lerAmostra(const float sup[3], const Condicao &cd) {
  const float tinta[3] = {1.35f, 1.0f, 0.60f};  // luz ambiente quente (3000 K)
  float textura = 1.0f + gauss(0.02f);
  float v[3];
  for (int c = 0; c < 3; c++) {
    float sinal = PARASITA + (1.0f - PARASITA) * sup[c] * cd.escala * textura;
    sinal += cd.ambiente * sup[c] * tinta[c];
    sinal *= 1.0f + gauss(0.004f);
    float cont = W[c] * sinal;
    cont += gauss(1.0f + 0.002f * cont);
    if (cont < 0) cont = 0;
    if (cont > 65535) cont = 65535;
    v[c] = floorf(cont);
  }
  float cc = (v[0] + v[1] + v[2]) / 1.092f + gauss(2.0f);
  cc = floorf(cc < 0 ? 0 : cc);
  if (cc > 65535) cc = 65535;
  Amostra a = {(uint16_t)v[0], (uint16_t)v[1], (uint16_t)v[2], (uint16_t)cc};
  return a;
}
static Bruta ler(const float sup[3], const Condicao &cd) {
  const Amostra a = lerAmostra(sup, cd);
  Bruta b = {(float)a.r, (float)a.g, (float)a.b, (float)a.c, a.c >= LIMITE_SAT};
  return b;
}

static int amostrasCalibracao(const Mat &m, Amostra *a, int n) {
  for (int i = 0; i < n; i++) {
    Condicao cd;
    cd.escala = 1.0f + gauss(0.02f);
    float sup[3];
    if (unif(0, 1) < 0.15f) {
      float f = unif(0.5f, 0.9f);
      for (int c = 0; c < 3; c++) sup[c] = f * m.x[c] + (1 - f) * 1.0f;
    } else {
      for (int c = 0; c < 3; c++) sup[c] = m.x[c];
    }
    a[i] = lerAmostra(sup, cd);
  }
  return n;
}

static const char *nome(int c) {
  switch (c) {
    case PRETO: return "PRETO"; case BRANCO: return "BRANCO"; case VERDE: return "VERDE";
    case VERMELHO: return "VERMELHO"; default: return "INDEFINIDO";
  }
}

static Calibracao calibrarTudo() {
  Calibracao cal;
  zerar(cal);
  Amostra a[MAX_AMOSTRAS];
  const Mat *m[4] = {&M_BRANCO, &M_PRETO, &M_VERDE, &M_VERMELHO};
  const Cor c[4] = {BRANCO, PRETO, VERDE, VERMELHO};
  for (int i = 0; i < 4; i++) {
    int n = amostrasCalibracao(*m[i], a, 40);
    Relatorio r;
    if (i == 0) calibrarBranco(r, cal, a, n, 2, 7, (uint16_t)LIMITE_SAT);
    else calibrarCor(r, cal, c[i], a, n, (uint16_t)LIMITE_SAT);
    printf("  calibrou %-9s ok=%d usadas=%d/%d brilho=%.3f croma=%.3f matiz=%.1f avisos=0x%04x\n", nome(c[i]), r.ok,
           r.usadas, r.total, r.brilho, r.croma, r.matiz, r.avisos);
    confere(r.ok, std::string("calibrou ") + nome(c[i]));
  }
  return cal;
}

static void testeConfusao(const Classificador &cl, const char *titulo, float esc0, float esc1, float amb1) {
  printf("\n== %s (escala %.2f..%.2f, ambiente 0..%.0f%%) ==\n", titulo, esc0, esc1, amb1 * 100);
  printf("%-18s %8s %8s %8s %8s %8s\n", "material", "PRETO", "BRANCO", "VERDE", "VERMELHO", "INDEF");
  const Mat *mats[] = {&M_BRANCO, &M_PRETO,    &M_VERDE,    &M_VERDE_ESC, &M_VERDE_AMAR, &M_VERMELHO, &M_VERM_ESC, &M_VERM_LAR,
                       &M_LAR_VERM, &M_LARANJA, &M_LAR_AMAR, &M_AMARELO,  &M_AZUL,      &M_PRATA,    &M_SILVER};
  for (const Mat *m : mats) {
    int cont[N_CORES] = {0};
    const int N = 5000;
    for (int i = 0; i < N; i++) {
      Condicao cd;
      cd.escala = unif(esc0, esc1);
      cd.ambiente = unif(0, amb1);
      cont[cl.classificar(ler(m->x, cd)).cor]++;
    }
    printf("%-18s %7.1f%% %7.1f%% %7.1f%% %7.1f%% %7.1f%%\n", m->nome, 100.0 * cont[PRETO] / N, 100.0 * cont[BRANCO] / N,
           100.0 * cont[VERDE] / N, 100.0 * cont[VERMELHO] / N, 100.0 * cont[INDEFINIDO] / N);
  }
}

struct ResultadoVarredura {
  long falsoVerde = 0, falsoVermelho = 0, leiturasSemVerde = 0, leiturasSemVermelho = 0;
  long pontosV = 0, detV = 0, pontosR = 0, detR = 0, total = 0;
};

static bool cenaTem(const Cena &c, const Mat *m) {
  for (auto &q : c.t) if (q.m == m) return true;
  return false;
}

// A mancha anda pela cena em passos de 0,25 mm; em cada ponto, 40 leituras.
static ResultadoVarredura varrer(const Classificador &cl, const Cena &cena, Condicao cd, bool desenhar, const char *titulo) {
  ResultadoVarredura res;
  std::string tira;
  const bool temV = cenaTem(cena, &M_VERDE), temR = cenaTem(cena, &M_VERMELHO);
  for (float x = -15; x <= 55; x += 0.25f) {
    float sup[3];
    mistura(cena, x, cd.sigmaMancha, sup);
    const float cobV = cobertura(cena, x, cd.sigmaMancha, &M_VERDE);
    const float cobR = cobertura(cena, x, cd.sigmaMancha, &M_VERMELHO);
    int cont[N_CORES] = {0};
    for (int k = 0; k < 40; k++) {
      const Cor c = cl.classificar(ler(sup, cd)).cor;
      cont[c]++;
      res.total++;
      if (!temV) { res.leiturasSemVerde++; if (c == VERDE) res.falsoVerde++; }
      if (!temR) { res.leiturasSemVermelho++; if (c == VERMELHO) res.falsoVermelho++; }
      if (cobV >= 0.80f) { res.pontosV++; if (c == VERDE) res.detV++; }
      if (cobR >= 0.80f) { res.pontosR++; if (c == VERMELHO) res.detR++; }
    }
    if (desenhar && fmodf(x + 15, 1.0f) < 0.01f) {
      int best = 0;
      for (int c = 1; c < N_CORES; c++) if (cont[c] > cont[best]) best = c;
      tira += best == PRETO ? "#" : best == BRANCO ? "." : best == VERDE ? "V" : best == VERMELHO ? "R" : "?";
    }
  }
  if (desenhar) printf("  %-34s %s\n", titulo, tira.c_str());
  return res;
}

static Cena cenaLinha() { Cena c; c.t.push_back({0, 15, &M_PRETO}); return c; }
static Cena cenaLinha10() { Cena c; c.t.push_back({0, 10, &M_PRETO}); return c; }
static Cena cenaVerdeLinha() { Cena c; c.t.push_back({0, 25, &M_VERDE}); c.t.push_back({25, 40, &M_PRETO}); return c; }
static Cena cenaVermelho() { Cena c; c.t.push_back({0, 25, &M_VERMELHO}); return c; }
static Cena cenaLaranja() { Cena c; c.t.push_back({0, 30, &M_LARANJA}); return c; }
static Cena cenaPretoVerde() { Cena c; c.t.push_back({0, 15, &M_PRETO}); c.t.push_back({15, 40, &M_VERDE}); return c; }

static void testeVarreduras(const Classificador &cl) {
  printf("\n== Varreduras (1 caractere por mm: . branco  # preto  V verde  R vermelho  ? indefinido) ==\n");
  struct Caso { const char *nome; Cena cena; };
  Caso casos[] = {{"branco|linha 15mm|branco", cenaLinha()},
                  {"branco|verde 25|linha 15|branco", cenaVerdeLinha()},
                  {"branco|linha|verde|branco", cenaPretoVerde()},
                  {"branco|vermelho 25|branco", cenaVermelho()},
                  {"branco|laranja 30|branco", cenaLaranja()}};
  const float escalas[] = {0.75f, 1.0f, 1.3f};
  const float sigmas[] = {2.0f, 3.0f, 4.5f};
  const float ambs[] = {0.0f, 0.10f};
  ResultadoVarredura geral;
  for (auto &cs : casos) {
    Condicao cd;
    varrer(cl, cs.cena, cd, true, cs.nome);
    ResultadoVarredura tot;
    for (float e : escalas)
      for (float s : sigmas)
        for (float a : ambs) {
          cd.escala = e; cd.sigmaMancha = s; cd.ambiente = a;
          ResultadoVarredura r = varrer(cl, cs.cena, cd, false, "");
          tot.falsoVerde += r.falsoVerde; tot.falsoVermelho += r.falsoVermelho;
          tot.leiturasSemVerde += r.leiturasSemVerde; tot.leiturasSemVermelho += r.leiturasSemVermelho;
          tot.pontosV += r.pontosV; tot.detV += r.detV; tot.pontosR += r.pontosR; tot.detR += r.detR;
          tot.total += r.total;
        }
    printf("      ");
    if (tot.leiturasSemVerde) printf("VERDE falso %ld/%ld  ", tot.falsoVerde, tot.leiturasSemVerde);
    if (tot.leiturasSemVermelho) printf("VERMELHO falso %ld/%ld  ", tot.falsoVermelho, tot.leiturasSemVermelho);
    if (tot.pontosV) printf("VERDE achado %.2f%%  ", 100.0 * tot.detV / tot.pontosV);
    if (tot.pontosR) printf("VERMELHO achado %.2f%%", 100.0 * tot.detR / tot.pontosR);
    printf("\n");
    geral.falsoVerde += tot.falsoVerde; geral.falsoVermelho += tot.falsoVermelho;
    geral.pontosV += tot.pontosV; geral.detV += tot.detV; geral.pontosR += tot.pontosR; geral.detR += tot.detR;
    if (!strcmp(cs.nome, "branco|linha 15mm|branco"))
      confere(tot.falsoVerde == 0 && tot.falsoVermelho == 0,
              "borda da linha preta: nenhum VERDE ou VERMELHO falso em " + std::to_string(tot.total) + " leituras");
    if (!strcmp(cs.nome, "branco|laranja 30|branco"))
      confere(tot.falsoVerde == 0, "laranja nunca vira VERDE");
  }
  confere(geral.falsoVerde == 0, "nenhum VERDE onde nao tem verde (todas as cenas)");
  confere(100.0 * geral.detV / geral.pontosV >= 99.9, "verde cobrindo 80% do sensor: VERDE em " + pct(100.0 * geral.detV / geral.pontosV));
  confere(100.0 * geral.detR / geral.pontosR >= 99.0, "vermelho cobrindo 80%: VERMELHO em " + pct(100.0 * geral.detR / geral.pontosR));
}

// Robô passando por cima: uma leitura a cada periodoMs, filtro de 2 leituras iguais.
static double passagem(const Classificador &cl, const Cena &cena, Cor alvo, float mmPorS, float periodoMs) {
  int ocorreu = 0;
  const int N = 2000;
  for (int k = 0; k < N; k++) {
    Condicao cd;
    cd.escala = unif(0.8f, 1.2f);
    cd.sigmaMancha = unif(2.0f, 4.5f);
    cd.ambiente = unif(0.0f, 0.06f);
    FiltroEstavel f;
    f.n = 2;
    f.reiniciar(BRANCO);
    const float passo = mmPorS * periodoMs * 0.001f;
    bool viu = false;
    for (float x = -25.0f + unif(0, passo); x < 65.0f; x += passo) {
      float sup[3];
      mistura(cena, x, cd.sigmaMancha, sup);
      if (f.atualizar(cl.classificar(ler(sup, cd)).cor) == alvo) viu = true;
    }
    if (viu) ocorreu++;
  }
  return 100.0 * ocorreu / N;
}

static void testePassagens(const Classificador &cl, const char *titulo, float periodoMs) {
  printf("\n== Passagens do robo (leitura a cada %.1f ms): %s ==\n", periodoMs, titulo);
  printf("  %-26s %9s %9s %9s %9s\n", "cena", "15 cm/s", "30 cm/s", "50 cm/s", "80 cm/s");
  struct Caso { const char *nome; Cena cena; Cor alvo; };
  Caso casos[] = {{"linha 10mm (PRETO achado)", cenaLinha10(), PRETO},
                  {"linha (VERDE falso)", cenaLinha(), VERDE},
                  {"linha (VERMELHO falso)", cenaLinha(), VERMELHO},
                  {"verde+linha (VERDE achado)", cenaVerdeLinha(), VERDE},
                  {"vermelho (VERMELHO achado)", cenaVermelho(), VERMELHO},
                  {"laranja (VERMELHO falso)", cenaLaranja(), VERMELHO}};
  const float velocidades[] = {150, 300, 500, 800};
  for (auto &cs : casos) {
    printf("  %-26s", cs.nome);
    double r[4];
    for (int i = 0; i < 4; i++) {
      r[i] = passagem(cl, cs.cena, cs.alvo, velocidades[i], periodoMs);
      printf(" %8.2f%%", r[i]);
    }
    printf("\n");
    const bool rapido = periodoMs < 15.0f;
    const std::string onde = std::string(" (") + titulo + ")";
    if (cs.alvo == PRETO) confere(r[0] >= 99.9 && r[1] >= 99.9, std::string("linha de 10 mm achada ate 30 cm/s") + onde);
    if (!strcmp(cs.nome, "linha (VERDE falso)") || !strcmp(cs.nome, "linha (VERMELHO falso)") ||
        !strcmp(cs.nome, "laranja (VERMELHO falso)"))
      confere(r[0] + r[1] + r[2] + r[3] == 0.0, std::string(cs.nome) + ": nunca" + onde);
    if (!strcmp(cs.nome, "verde+linha (VERDE achado)") || !strcmp(cs.nome, "vermelho (VERMELHO achado)"))
      confere(r[0] >= 99.9 && r[1] >= 99.9 && r[2] >= 99.9 && (!rapido || r[3] >= 99.9),
              std::string(cs.nome) + (rapido ? ": 100% ate 80 cm/s" : ": 100% ate 50 cm/s") + onde);
  }
}

int main() {
  printf("=== Teste do classificador da CorFacil (sensor simulado) ===\n\n");
  Calibracao cal = calibrarTudo();
  Classificador cl;
  confere(cl.preparar(cal), "preparou o classificador");
  printf("  ruido de croma=%.4f  croma p/ INDEFINIDO=%.3f  preto=%.3f do branco\n", cl.ruido(), cl.cromaIndefinido(),
         cl.pretoRelativo());
  for (int i = 0; i < N_COLORIDAS; i++)
    printf("  %-9s matiz %.1f  croma %.3f  brilho %.3f  tolerancia -%.1f/+%.1f graus\n", nome(corColorida(i)),
           cl.matizRef(i), cl.cromaRef(i), cl.brilhoRef(i), cl.ladoMenosGraus(i), cl.ladoMaisGraus(i));
  testeConfusao(cl, "Materiais puros", 0.75f, 1.30f, 0.05f);
  testeVarreduras(cl);
  testePassagens(cl, "7 ciclos, 19,2 ms", 19.2f);
  testePassagens(cl, "modo rapido, 4 ciclos, 12 ms", 12.0f);
  printf("\n== Luz ambiente forte (sem tampa), calibrado no escuro ==\n");
  testeConfusao(cl, "ambiente ate 25%", 0.9f, 1.1f, 0.25f);
  printf(falhas ? "\n== %d FALHA(S) ==\n" : "\n== tudo certo ==\n", falhas);
  return falhas ? 1 : 0;
}
