// Roda a biblioteca CorFacil e os exemplos de verdade no PC, ligados a um
// TCS34725 simulado (sensor_falso.h). Cada cenário é um "liga a placa"; a
// memória (NVS do ESP32 ou EEPROM do Uno) passa de um para o outro num arquivo.
//
// Uso: teste_biblioteca <cenario> <arquivo_da_memoria>
// Sai com código 0 se todas as conferências passaram. Veja rodar_testes.bat.

#include <map>
#include <string>
#include "Arduino.h"
#include "EEPROM.h"
#include "Preferences.h"
#include "Wire.h"
#include "sensor_falso.h"

namespace sim {
double us = 0;
int modo[64] = {0};
int nivel[64] = {0};
std::function<int(int)> entrada;
std::string saidaSerial;
std::deque<char> entradaSerial;
bool ecoSerial = true;
std::map<std::string, std::map<std::string, std::vector<uint8_t>>> nvs;
uint8_t eeprom[1024];
long escritasEeprom = 0;
Barramento barramentos[2];
Dispositivo *achar(Barramento &bus, uint8_t endereco) {
  for (Dispositivo *d : bus.disp) {
    if (d->endereco != endereco || !d->presente()) continue;
    if (d->canalMux < 0 || ((bus.mascaraMux >> d->canalMux) & 1)) return d;
  }
  return nullptr;
}
}  // namespace sim

SerialFalsa Serial;
TwoWire Wire(0);
#if defined(ARDUINO_ARCH_ESP32)
TwoWire Wire1(1);
#endif
EEPROMClass EEPROM;

// a biblioteca inteira, como a IDE compila
#include "../../../src/CorFacil_TCS34725.cpp"
#include "../../../src/CorFacil.cpp"

// os exemplos, cada um no seu canto (setup e loop de cada)
namespace ex01 {
#include "../../../examples/01_LerCor/01_LerCor.ino"
}
namespace ex02 {
#include "../../../examples/02_CalibrarPeloMonitor/02_CalibrarPeloMonitor.ino"
}
namespace ex03 {
#include "../../../examples/03_CalibrarComBotao/03_CalibrarComBotao.ino"
}
namespace ex05 {
#include "../../../examples/05_DoisSensores/05_DoisSensores.ino"
}

// ------------------------------------------------------------------- ajudantes
static int falhas = 0;
static void confere(bool ok, const std::string &o_que) {
  printf("%s %s\n", ok ? "  [OK]   " : "  [FALHA]", o_que.c_str());
  if (!ok) falhas++;
}
static bool saiu(const std::string &s) { return sim::saidaSerial.find(s) != std::string::npos; }
static void limpar() { sim::saidaSerial.clear(); }
static void enviar(const char *s) { while (*s) sim::entradaSerial.push_back(*s++); }
static void rodar(double segundos, const std::function<void()> &passo, const std::function<void()> &cada = nullptr) {
  const double fim = sim::us + segundos * 1e6;
  while (sim::us < fim) {
    passo();
    if (cada) cada();
    sim::us += 20.0;  // o resto do programa do robô
  }
}
static std::string ms(double us) { char b[32]; snprintf(b, sizeof b, "%.1f ms", us / 1000.0); return b; }

static void salvarMemoria(const char *arq) {
  FILE *f = fopen(arq, "wb");
  for (auto &ns : sim::nvs)
    for (auto &kv : ns.second) {
      fprintf(f, "nvs %s %s %zu", ns.first.c_str(), kv.first.c_str(), kv.second.size());
      for (uint8_t b : kv.second) fprintf(f, " %u", b);
      fprintf(f, "\n");
    }
  fprintf(f, "eeprom");
  for (int i = 0; i < 1024; i++) fprintf(f, " %u", sim::eeprom[i]);
  fprintf(f, "\n");
  fclose(f);
}
static void carregarMemoria(const char *arq) {
  FILE *f = fopen(arq, "rb");
  if (!f) return;
  char tipo[16], ns[64], k[64];
  while (fscanf(f, "%15s", tipo) == 1) {
    if (!strcmp(tipo, "nvs")) {
      size_t n;
      if (fscanf(f, "%63s %63s %zu", ns, k, &n) != 3) break;
      std::vector<uint8_t> v(n);
      for (size_t i = 0; i < n; i++) { unsigned u; if (fscanf(f, "%u", &u) == 1) v[i] = (uint8_t)u; }
      sim::nvs[ns][k] = v;
    } else {
      for (int i = 0; i < 1024; i++) { unsigned u; if (fscanf(f, "%u", &u) == 1) sim::eeprom[i] = (uint8_t)u; }
    }
  }
  fclose(f);
}

// linha de 10 mm (a mais fina da OBR), verde de 25 mm colado numa linha de 15 mm,
// faixa vermelha de 25 mm e disco laranja de 30 mm (checkpoint)
static const std::vector<Trecho> PISTA = {
    {40, 50, &PRETO_}, {95, 120, &VERDE_}, {120, 135, &PRETO_}, {175, 200, &VERMELHO_}, {240, 270, &LARANJA_}};

// Percorre a PISTA chamando lerCor() sem parar; confere onde cada cor aparece.
static void conferePista(CorFacil &s, TCSFalso &sensor, const std::string &rotulo, float v = 300.0f) {
  const double t0 = sim::us;
  sensor.cena = pista(PISTA, v, t0);
  std::vector<std::pair<float, int>> trocas;
  int ultima = s.lerCor();
  rodar(330.0 / v, [&]() {
    const int c = s.lerCor();
    if (c != ultima) {
      trocas.push_back({(float)((sim::us - t0) * 1e-6 * v), c});
      ultima = c;
    }
  });
  printf("  %s - trocas da cor (mm: cor):", rotulo.c_str());
  bool verdeFora = false, vermelhoFora = false, achouVerde = false, achouVermelho = false, preto1 = false,
       preto2 = false, pretoFora = false, estranha = false;
  for (auto &tc : trocas) {
    printf(" %.0f:%s", tc.first, CorFacil::nomeCor(tc.second));
    // atraso esperado: meia mancha + 2 leituras + borrão da integração
    if (tc.second == VERDE) { achouVerde = true; if (tc.first < 95 || tc.first > 135) verdeFora = true; }
    if (tc.second == VERMELHO) { achouVermelho = true; if (tc.first < 175 || tc.first > 215) vermelhoFora = true; }
    if (tc.second == PRETO) {
      if (tc.first >= 40 && tc.first <= 62) preto1 = true;
      else if (tc.first >= 115 && tc.first <= 150) preto2 = true;
      else pretoFora = true;
    }
    if (tc.second == SEM_SENSOR || tc.second == SEM_CALIBRACAO) estranha = true;
  }
  printf("\n");
  confere(preto1, rotulo + ": viu a linha preta de 10 mm");
  confere(preto2 && !pretoFora, rotulo + ": viu a linha depois do verde, e PRETO so nas linhas");
  confere(achouVerde && !verdeFora, rotulo + ": VERDE so em cima do marcador verde");
  confere(achouVermelho && !vermelhoFora, rotulo + ": VERMELHO so na faixa (o laranja nao vira vermelho)");
  confere(!estranha && ultima == BRANCO, rotulo + ": terminou no BRANCO");
}

static TCSFalso sensorA, sensorB;
static MuxFalso mux0(0);

// ------------------------------------------------------------------- cenários

static void calibrarPeloMonitor(TCSFalso &s) {
  const Mat *m[4] = {&BRANCO_, &PRETO_, &VERDE_, &VERMELHO_};
  const char *letra[4] = {"b\n", "p\n", "v\n", "r\n"};
  for (int i = 0; i < 4; i++) {
    s.cena = fixa(*m[i]);
    rodar(0.4, ex02::loop);
    enviar(letra[i]);
    rodar(3.0, ex02::loop);
  }
}

static int cenarioCalibrar(const char *arq) {
  printf("== Primeira vez: calibrar pelo Monitor Serial (exemplo 02) ==\n");
  sim::barramentos[0].disp.push_back(&sensorA);
  ex02::setup();
  rodar(0.5, ex02::loop);
  confere(saiu("[sensor] sensor ok (ID 0x44)"), "achou o sensor ao ligar");
  confere(saiu("falta calibrar: BRANCO PRETO VERDE VERMELHO"), "disse o que falta calibrar");
  confere(saiu(">> sensor no BRANCO e envie b"), "mostrou o primeiro passo");
  confere(saiu("[sensor] SEM CALIBRACAO | RGBC"), "sem calibrar, a leitura diz SEM CALIBRACAO");
  calibrarPeloMonitor(sensorA);
  confere(saiu("BRANCO ok") && saiu("ganho 16x"), "BRANCO calibrado e ganho ajustado para 16x sozinho");
  confere(saiu("PRETO ok") && saiu("VERDE ok") && saiu("VERMELHO ok"), "PRETO, VERDE e VERMELHO calibrados");
  confere(saiu(">> calibrado! Passe"), "avisou que a calibracao ficou completa");
  confere(sensorA.reg[0x0F] == 2 && sensorA.reg[0x01] == 256 - 7, "sensor ficou em 16x e 7 ciclos");
  confere(ex02::sensor.calibrado(), "calibrado() = true");
  limpar();
  enviar("c\n");
  rodar(0.7, ex02::loop);
  confere(saiu("  VERDE    croma") && saiu("  VERMELHO croma") && saiu("% do branco"), "c mostra a calibracao");
  rodar(1.2, []() { ex02::sensor.lerCor(); });
  const float hz = ex02::sensor.leiturasPorSegundo();
  confere(hz > 48.0f && hz < 54.0f, "leituras por segundo perto de 52 (deu " + std::to_string(hz) + ")");
  conferePista(ex02::sensor, sensorA, "pista a 30 cm/s");
  confere(sensorA.repetidas == 0, "nenhum ciclo do sensor lido duas vezes (" + std::to_string(sensorA.repetidas) + ")");

  printf("-- sensor reinicia sozinho (queda de tensao) --\n");
  limpar();
  sensorA.cena = fixa(VERDE_);
  sensorA.resetar();
  rodar(2.0, []() { ex02::sensor.lerCor(); });
  confere(saiu("sensor reiniciou sozinho"), "percebeu o reinicio");
  confere(sensorA.reg[0x0F] == 2 && sensorA.reg[0x00] == 0x03, "reconfigurou o ganho 16x e ligou de novo");
  confere(ex02::sensor.lerCor() == VERDE, "voltou a ler (VERDE)");

  printf("-- fio solto por 1,5 s --\n");
  limpar();
  sensorA.ligado = false;
  rodar(1.5, []() { ex02::sensor.lerCor(); });
  confere(saiu("sensor sumiu do I2C"), "avisou que o sensor sumiu");
  confere(ex02::sensor.lerCor() == SEM_SENSOR, "lerCor() = SEM_SENSOR com o fio solto");
  sensorA.ligado = true;
  sensorA.resetar();  // voltou sem a energia de antes
  rodar(1.5, []() { ex02::sensor.lerCor(); });
  confere(saiu("sensor voltou"), "avisou que voltou");
  confere(sensorA.reg[0x0F] == 2 && ex02::sensor.lerCor() == VERDE, "voltou com o ganho certo e lendo VERDE");
  salvarMemoria(arq);
  return falhas;
}

static int cenarioReligar(const char *arq) {
  printf("== Religar a placa: a calibracao vem da memoria (exemplo 01) ==\n");
  carregarMemoria(arq);
  sim::barramentos[0].disp.push_back(&sensorA);
  ex01::setup();
  rodar(0.5, ex01::loop);
  confere(saiu("calibrado: BRANCO PRETO VERDE VERMELHO"), "carregou a calibracao salva");
  confere(sensorA.reg[0x0F] == 2, "voltou ao ganho 16x da calibracao");
  sensorA.cena = fixa(VERDE_);
  limpar();
  rodar(1.0, ex01::loop);
  confere(saiu("VERDE   <- achei o verde!"), "o exemplo 01 achou o verde");
  conferePista(ex01::sensor, sensorA, "pista depois de religar");

  printf("-- lerCor() depois de um delay longo --\n");
  sensorA.cena = troca(BRANCO_, VERDE_, sim::us + 250000.0);
  rodar(0.1, []() { ex01::sensor.lerCor(); });
  confere(ex01::sensor.lerCor() == BRANCO, "antes: BRANCO");
  delay(500);  // o sensor passou para o verde no meio do delay
  double t = sim::us;
  const int c = ex01::sensor.lerCor();
  const double dur = sim::us - t;
  confere(c == VERDE && dur < 45000.0, "depois de delay(500): VERDE (a cor de agora), em " + ms(dur));
  sensorA.cena = troca(VERDE_, PRETO_, sim::us + 100000.0);
  delay(300);
  t = sim::us;
  const int c2 = ex01::sensor.lerCor();
  confere(c2 == PRETO && sim::us - t < 25000.0, "depois de outro delay: PRETO na hora, em " + ms(sim::us - t));

  printf("-- lerCor() chamado sem parar nunca espera --\n");
  sensorA.cena = pista(PISTA, 300.0f, sim::us);
  double pior = 0;
  rodar(1.1, []() {}, [&]() {
    const double t0 = sim::us;
    ex01::sensor.lerCor();
    if (sim::us - t0 > pior) pior = sim::us - t0;
  });
  confere(pior < 2000.0, "lerCor() no loop rapido: pior chamada " + ms(pior));

  printf("-- robo com delay(100) no loop, a 10 cm/s --\n");
  const double t0 = sim::us;
  sensorA.cena = pista(PISTA, 100.0f, t0);
  bool viuVerde = false, verdeFora = false, viuVermelho = false;
  rodar(3.3, [&]() {
    const int k = ex01::sensor.lerCor();
    const float pos = (float)((sim::us - t0) * 1e-6 * 100.0);
    if (k == VERDE) { viuVerde = true; if (pos < 95 || pos > 130) verdeFora = true; }
    if (k == VERMELHO) viuVermelho = true;
    delay(100);
  });
  confere(viuVerde && !verdeFora && viuVermelho, "com delay(100) ainda ve o verde (so nele) e o vermelho");
  return falhas;
}

static int cenarioApagar(const char *arq) {
  printf("== Apagar a calibracao pelo Monitor (x) ==\n");
  carregarMemoria(arq);
  sim::barramentos[0].disp.push_back(&sensorA);
  ex02::setup();
  rodar(0.5, ex02::loop);
  confere(saiu("calibrado: BRANCO PRETO VERDE VERMELHO"), "ligou calibrado");
  enviar("x\n");
  rodar(0.7, ex02::loop);
  enviar("v\n");
  rodar(0.7, ex02::loop);
  confere(saiu("apagado: VERDE") && !ex02::sensor.calibrado(VERDE) && ex02::sensor.calibrado(VERMELHO),
          "x e v apagou so o verde");
  sensorA.cena = fixa(VERDE_);
  rodar(0.3, []() { ex02::sensor.lerCor(); });
  confere(ex02::sensor.lerCor() == INDEFINIDO, "sem o VERDE calibrado, o verde vira INDEFINIDO");
  enviar("x\n");
  rodar(0.7, ex02::loop);
  enviar("x\n");
  rodar(0.7, ex02::loop);
  confere(saiu("apagado: TUDO") && !ex02::sensor.calibrado(BRANCO), "x e x apagou tudo");
  confere(ex02::sensor.lerCor() == SEM_CALIBRACAO, "lerCor() = SEM_CALIBRACAO");
  salvarMemoria(arq);
  return falhas;
}

static int cenarioDepoisDeApagar(const char *arq) {
  printf("== Religar depois de apagar ==\n");
  carregarMemoria(arq);
  sim::barramentos[0].disp.push_back(&sensorA);
  ex01::setup();
  rodar(0.5, ex01::loop);
  confere(saiu("falta calibrar: BRANCO PRETO VERDE VERMELHO") && saiu("SEM CALIBRACAO"),
          "ligou sem calibracao (a memoria foi apagada mesmo)");
  return falhas;
}

static int cenarioBotao() {
  printf("== Calibrar com botao (exemplo 03) ==\n");
  sim::barramentos[0].disp.push_back(&sensorA);
  double ini = -1, fim = -1;
  sim::entrada = [&](int p) { return (p == ex03::BOTAO && sim::us >= ini && sim::us < fim) ? LOW : HIGH; };
  ex03::setup();
  rodar(0.3, ex03::loop);
  auto apertar = [&](const Mat &m) {
    sensorA.cena = fixa(m);
    ini = sim::us;
    fim = sim::us + 150000.0;
    rodar(6.0, ex03::loop);
  };
  apertar(BRANCO_);
  confere(ex03::sensor.calibrado(BRANCO) && ex03::passo == 1, "1o aperto: BRANCO calibrado");
  limpar();
  apertar(BRANCO_);  // aluno esqueceu de por o sensor no preto
  confere(!ex03::sensor.calibrado(PRETO) && ex03::passo == 1 && saiu("PRETO NAO calibrado"),
          "2o aperto em cima do branco: PRETO recusado, o passo nao anda");
  apertar(PRETO_);
  confere(ex03::sensor.calibrado(PRETO) && ex03::passo == 2, "de novo, no preto: PRETO calibrado");
  apertar(VERDE_);
  apertar(VERMELHO_);
  confere(ex03::sensor.calibrado() && ex03::passo == 0, "VERDE e VERMELHO: calibracao completa");
  sensorA.cena = fixa(VERDE_);
  rodar(0.3, ex03::loop);
  confere(sim::nivel[ex03::LED] == HIGH, "LED acende no VERDE");
  sensorA.cena = fixa(BRANCO_);
  rodar(0.3, ex03::loop);
  confere(sim::nivel[ex03::LED] == LOW, "LED apaga no BRANCO");
  return falhas;
}

static void ligarDoisSensores() {
  sensorB.base[0] = 4.0f;  // o 2o sensor e diferente (mais azulado e mais fraco no vermelho)
  sensorB.base[1] = 9.5f;
  sensorB.base[2] = 10.0f;
#if defined(ARDUINO_ARCH_ESP32)
  sim::barramentos[0].disp.push_back(&sensorA);
  sim::barramentos[1].disp.push_back(&sensorB);
#else
  sensorA.canalMux = 0;
  sensorB.canalMux = 1;
  sim::barramentos[0].disp.push_back(&mux0);
  sim::barramentos[0].disp.push_back(&sensorA);
  sim::barramentos[0].disp.push_back(&sensorB);
#endif
}

static int cenarioDois(const char *arq) {
  printf("== Dois sensores (exemplo 05) ==\n");
  ligarDoisSensores();
  ex05::setup();
  rodar(0.5, ex05::loop);
  confere(saiu("[esquerdo] sensor ok") && saiu("[direito] sensor ok"), "achou os dois sensores");
  const Mat *m[4] = {&BRANCO_, &PRETO_, &VERDE_, &VERMELHO_};
  const char *letra[4] = {"b", "p", "v", "r"};
  for (int i = 0; i < 4; i++) {
    sensorA.cena = sensorB.cena = fixa(*m[i]);
    rodar(0.3, ex05::loop);
    enviar(letra[i]);
    rodar(4.0, ex05::loop);
  }
  confere(ex05::esquerdo.calibrado() && ex05::direito.calibrado(), "os dois calibrados");
  sensorA.cena = fixa(VERDE_);
  sensorB.cena = fixa(BRANCO_);
  limpar();
  rodar(0.5, ex05::loop);
  confere(saiu("VERDE  |  BRANCO   -> VERDE na esquerda"), "verde so na esquerda");
  sensorA.cena = sensorB.cena = fixa(VERMELHO_);
  limpar();
  rodar(0.5, ex05::loop);
  confere(saiu("VERMELHO  |  VERMELHO   -> CHEGADA"), "os dois no vermelho: CHEGADA");
#if defined(ARDUINO_ARCH_ESP32)
  confere(sim::nvs["corfacil"].count("cal0") && sim::nvs["corfacil"].count("cal9"),
          "calibracoes separadas na NVS (cal0 = Wire, cal9 = Wire1)");
#else
  confere(sim::eeprom[1024 - 2 * 92] == 'C' && sim::eeprom[1024 - 3 * 92] == 'C',
          "calibracoes separadas no fim da EEPROM (canal 0 e canal 1 do TCA9548A)");
#endif
  salvarMemoria(arq);
  return falhas;
}

static int cenarioDoisReligar(const char *arq) {
  printf("== Dois sensores: religar ==\n");
  carregarMemoria(arq);
  ligarDoisSensores();
  ex05::setup();
  rodar(0.5, ex05::loop);
  confere(saiu("[esquerdo] calibrado: BRANCO") && saiu("[direito] calibrado: BRANCO"), "os dois carregaram a sua");
  sensorA.cena = fixa(VERMELHO_);
  sensorB.cena = fixa(VERDE_);
  limpar();
  rodar(0.5, ex05::loop);
  confere(saiu("VERMELHO  |  VERDE   -> VERDE na direita"), "cada um com a sua calibracao (sem trocar)");
  sensorA.cena = fixa(PRETO_);
  sensorB.cena = fixa(BRANCO_);
  limpar();
  rodar(0.5, ex05::loop);
  confere(saiu("PRETO  |  BRANCO"), "PRETO e BRANCO certos nos dois");
  return falhas;
}

static void calibrarDireto(CorFacil &s, TCSFalso &sensor) {
  const Mat *m[4] = {&BRANCO_, &PRETO_, &VERDE_, &VERMELHO_};
  const Cor c[4] = {BRANCO, PRETO, VERDE, VERMELHO};
  for (int i = 0; i < 4; i++) {
    sensor.cena = fixa(*m[i]);
    rodar(0.1, []() {});
    confere(s.calibrar(c[i]), std::string("calibrar(") + CorFacil::nomeCor(c[i]) + ") = true");
  }
}

static CorFacil teste("teste");

static int cenarioClone() {
  printf("== Clone sem AINT e com outro ID ==\n");
  sensorA.comAint = false;
  sensorA.idValor = 0x5A;  // ID desconhecido (0x44 = TCS34725, 0x4D = TCS34727)
  sensorA.osc = 1.06;  // oscilador no limite do datasheet
  sim::barramentos[0].disp.push_back(&sensorA);
  Serial.begin(115200);
  teste.mensagens(Serial);
  teste.iniciar();
  rodar(0.5, []() { teste.lerCor(); });
  confere(saiu("clone? funciona igual"), "avisou o ID diferente");
  confere(saiu("lendo por tempo"), "percebeu que nao tem AINT e le por tempo");
  calibrarDireto(teste, sensorA);
  rodar(1.2, []() { teste.lerCor(); });
  confere(teste.leiturasPorSegundo() > 40.0f, "le a mais de 40/s (" + std::to_string(teste.leiturasPorSegundo()) + ")");
  conferePista(teste, sensorA, "clone, pista a 30 cm/s");
  return falhas;
}

static int cenarioSemSensor() {
  printf("== Sem sensor ao ligar, ligado depois ==\n");
  sensorA.ligado = false;
  sim::barramentos[0].disp.push_back(&sensorA);
  ex01::setup();
  rodar(1.0, ex01::loop);
  confere(saiu("sensor NAO respondeu") && saiu("respondeu: ninguem"), "explicou que o sensor nao respondeu");
  confere(saiu("SEM SENSOR"), "lerCor() = SEM_SENSOR");
  sensorA.ligado = true;
  limpar();
  rodar(1.5, ex01::loop);
  confere(saiu("sensor voltou") && saiu("SEM CALIBRACAO"), "achou o sensor sozinho quando ligou o fio");
  return falhas;
}

static int cenarioRapido() {
  printf("== modoRapido() e aviso de cor estranha ==\n");
  sim::barramentos[0].disp.push_back(&sensorA);
  Serial.begin(115200);
  teste.mensagens(Serial);
  teste.modoRapido();
  teste.iniciar();
  calibrarDireto(teste, sensorA);
  confere(sensorA.reg[0x01] == 256 - 4, "4 ciclos de integracao no sensor");
  rodar(1.2, []() { teste.lerCor(); });
  confere(teste.leiturasPorSegundo() > 78.0f && teste.leiturasPorSegundo() < 88.0f,
          "~83 leituras/s (" + std::to_string(teste.leiturasPorSegundo()) + ")");
  conferePista(teste, sensorA, "modo rapido, pista a 50 cm/s", 500.0f);
  limpar();
  sensorA.cena = fixa(VERMELHO_);
  rodar(0.1, []() {});
  teste.calibrar(VERDE);
  confere(saiu("! nao parece VERDE"), "calibrar VERDE em cima do vermelho avisa");
  limpar();
  confere(!teste.calibrar(7), "calibrar(7) = false");
  confere(saiu("so da para calibrar BRANCO, PRETO, VERDE e VERMELHO"), "explica a cor invalida");
  return falhas;
}

int main(int argc, char **argv) {
  memset(sim::eeprom, 0xFF, sizeof sim::eeprom);
  const std::string c = argc > 1 ? argv[1] : "";
  const char *arq = argc > 2 ? argv[2] : "memoria_teste.txt";
#if defined(ARDUINO_ARCH_ESP32)
  printf("[placa simulada: ESP32]\n");
#else
  printf("[placa simulada: Arduino Uno]\n");
#endif
  int r = -1;
  if (c == "calibrar") r = cenarioCalibrar(arq);
  else if (c == "religar") r = cenarioReligar(arq);
  else if (c == "apagar") r = cenarioApagar(arq);
  else if (c == "depois_apagar") r = cenarioDepoisDeApagar(arq);
  else if (c == "botao") r = cenarioBotao();
  else if (c == "dois") r = cenarioDois(arq);
  else if (c == "dois_religar") r = cenarioDoisReligar(arq);
  else if (c == "clone") r = cenarioClone();
  else if (c == "sem_sensor") r = cenarioSemSensor();
  else if (c == "rapido") r = cenarioRapido();
  if (r < 0) {
    printf("cenario desconhecido: %s\n", c.c_str());
    return 2;
  }
  printf(r ? "== %d FALHA(S) ==\n" : "== tudo certo ==\n", r);
  return r ? 1 : 0;
}
