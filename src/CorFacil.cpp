#include "CorFacil.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <Preferences.h>
#define CORFACIL_NVS 1  // ESP32: calibração na NVS (sobrevive a desligar e a gravar outro programa)
#elif defined(ARDUINO_ARCH_AVR) || defined(ARDUINO_ARCH_MEGAAVR)
#include <EEPROM.h>
#define CORFACIL_EEPROM 1  // Uno, Nano, Mega: nos últimos bytes da EEPROM
#endif

using namespace corfacil;

namespace {
const uint8_t SLOTS_POR_BARRAMENTO = 9;  // sem multiplexador + 8 canais do TCA9548A
const uint8_t GANHO_INICIAL = 1;         // 4x até a calibração do branco escolher o melhor
const uint8_t CICLOS_NORMAL = 7;         // 16,8 ms: 52 leituras/s e cancela o pisca da lâmpada de 60 Hz
const uint8_t CICLOS_RAPIDO = 4;         // 9,6 ms: 83 leituras/s

uint8_t indiceBarramento(TwoWire *w) {
  if (w == &Wire) return 0;
#if defined(ARDUINO_ARCH_ESP32) && SOC_I2C_NUM > 1
  if (w == &Wire1) return 1;
#endif
  return 2;
}

#if defined(CORFACIL_EEPROM)
int enderecoEeprom(uint8_t slot) {
  return (int)EEPROM.length() - (int)(slot + 1) * (int)sizeof(Calibracao);
}
#endif
#if defined(CORFACIL_NVS)
const char *const NAMESPACE_NVS = "corfacil";
void chaveNvs(char *chave, uint8_t slot) {  // "cal0" ... "cal26"
  chave[0] = 'c';
  chave[1] = 'a';
  chave[2] = 'l';
  uint8_t i = 3;
  if (slot >= 10) chave[i++] = (char)('0' + slot / 10);
  chave[i++] = (char)('0' + slot % 10);
  chave[i] = 0;
}
#endif

// Número com casas decimais só com conta inteira: o print(float) do Arduino
// custa ~0,5 KB no Uno, e o Nano com bootloader antigo tem só 30 KB.
void imprimirDecimal(Print &p, float x, uint8_t casas) {
  if (x < 0.0f) {
    p.print('-');
    x = -x;
  }
  uint32_t escala = 1;
  for (uint8_t i = 0; i < casas; i++) escala *= 10;
  const uint32_t v = (uint32_t)(x * (float)escala + 0.5f);
  p.print(v / escala);
  if (!casas) return;
  p.print('.');
  const uint32_t f = v % escala;
  for (uint32_t d = escala / 10; d > 1 && f < d; d /= 10) p.print('0');
  p.print(f);
}

void imprimirPorcento(Print &p, float fracao) {
  p.print((int)(100.0f * fracao + 0.5f));
  p.print('%');
}
}  // namespace

// ================================================================ começar

bool CorFacil::iniciar() { return iniciar(SDA, SCL, Wire); }

bool CorFacil::iniciar(int sda, int scl) { return iniciar(sda, scl, Wire); }

bool CorFacil::iniciar(int sda, int scl, TwoWire &barramento) {
  bool pinosFixos = false;
#if !defined(ARDUINO_ARCH_ESP32)
  // AVR: os pinos do I2C são fixos no chip. Guarda os de verdade (para destravar o barramento).
  if (&barramento == &Wire) {
    pinosFixos = (sda != SDA || scl != SCL);
    sda = SDA;
    scl = SCL;
  } else {
    sda = scl = -1;
  }
#endif
  _slot = (uint8_t)(indiceBarramento(&barramento) * SLOTS_POR_BARRAMENTO + (_muxEndereco >= 0 ? _muxCanal + 1 : 0));
  zerar(_cal);
  _cls.preparar(_cal);
  _exposicaoMudou = false;
  const uint8_t ciclos = _ciclosPedido ? _ciclosPedido : CICLOS_NORMAL;
  const bool ok = _drv.iniciar(barramento, sda, scl, _hz, ciclos, GANHO_INICIAL, _muxEndereco, _muxCanal);
  _estavaConectado = ok;
  _reiniciosVistos = _drv.reinicios();
  _avisouTempo = false;
#if defined(CORFACIL_EEPROM)
  _semMemoria = enderecoEeprom(_slot) < 0;
#elif defined(CORFACIL_NVS)
  _semMemoria = false;
#else
  _semMemoria = true;
#endif
  carregar();
  reiniciarFiltro();
  avisar(EV_INICIO);
  if (pinosFixos) avisar(EV_PINOS_FIXOS);
  return ok;
}

// ================================================================ ler a cor

bool CorFacil::atualizar() {
  if (!_drv.iniciado()) return false;
  const bool nova = _drv.atualizar();
  vigiar();
  if (!nova) return false;
  const uint32_t ms = millis();
  // O programa ficou parado (delay, calibração): as leituras de antes não são
  // "seguidas" desta. Recomeça o filtro, e lerCor() espera a cor de agora.
  const uint32_t intervaloMax = 4 * _drv.periodoUs() / 1000 + 5;
  if (!_temUltima || ms - _msUltima > intervaloMax) {
    _filtro.reiniciar();
    _esperar = true;
  }
  _temUltima = true;
  _msUltima = ms;
  _corAgora = ultimaLeitura().cor;
  _filtro.atualizar(_corAgora);
  return true;
}

// Classifica a última leitura do sensor. Os números (brilho, croma...) não
// ficam guardados: no Uno, cada sensor economiza 40 bytes de RAM assim.
Resultado CorFacil::ultimaLeitura() {
  const TCS34725::Leitura &l = _drv.leitura();
  Bruta b;
  b.r = l.r;
  b.g = l.g;
  b.b = l.b;
  b.c = l.c;
  b.saturada = l.saturada;
  return _cls.classificar(b);
}

Cor CorFacil::lerCor() {
  if (!_drv.iniciado()) return SEM_SENSOR;
  atualizar();
  if (!_drv.conectado()) return SEM_SENSOR;
  if (!_cls.pronto()) return SEM_CALIBRACAO;
  if (_esperar) {
    // Logo depois de ligar ou de uma pausa: espera as leituras novas que
    // confirmam a cor (PRETO e BRANCO: 1; VERDE e VERMELHO: 2). Na borda de um
    // marcador a cor pode não confirmar: aí fica INDEFINIDO.
    _esperar = false;
    const uint32_t t0 = millis();
    const uint32_t limite = (uint32_t)(_filtro.n + 2) * _drv.periodoUs() / 1000 + 10;
    while (!_filtro.confirmou() && millis() - t0 < limite) {
      if (!atualizar()) delay(1);
      if (!_drv.conectado()) return SEM_SENSOR;
    }
  }
  return _filtro.estavel();
}

const char *CorFacil::nomeCor() { return nomeCor(lerCor()); }

const char *CorFacil::nomeCor(uint8_t c) {
  switch (c) {
    case PRETO: return "PRETO";
    case BRANCO: return "BRANCO";
    case VERDE: return "VERDE";
    case VERMELHO: return "VERMELHO";
    case SEM_SENSOR: return "SEM SENSOR";
    case SEM_CALIBRACAO: return "SEM CALIBRACAO";
    default: return "INDEFINIDO";
  }
}

Cor CorFacil::corAgora() {
  if (!_drv.iniciado()) return SEM_SENSOR;
  atualizar();
  if (!_drv.conectado()) return SEM_SENSOR;
  if (!_cls.pronto()) return SEM_CALIBRACAO;
  return _corAgora;
}

bool CorFacil::conectado() {
  atualizar();
  return _drv.conectado();
}

float CorFacil::brilho() { return ultimaLeitura().brilho; }
float CorFacil::croma() { return ultimaLeitura().croma; }
float CorFacil::matiz() { return matizDe(ultimaLeitura()); }
float CorFacil::confianca() { return _cls.confianca(ultimaLeitura()); }
float CorFacil::leiturasPorSegundo() { return _drv.taxaHz(); }

void CorFacil::lerContagens(uint16_t &r, uint16_t &g, uint16_t &b, uint16_t &c) {
  atualizar();
  const TCS34725::Leitura &l = _drv.leitura();
  r = l.r;
  g = l.g;
  b = l.b;
  c = l.c;
}

void CorFacil::confirmacoes(uint8_t leituras) { _filtro.n = leituras < 1 ? 1 : leituras; }

void CorFacil::usarMultiplexador(uint8_t canal, uint8_t endereco) {
  _muxCanal = (int8_t)(canal & 7);
  _muxEndereco = endereco;
}

void CorFacil::frequenciaI2C(uint32_t hz) { _hz = hz; }

void CorFacil::modoRapido(bool ligado) {
  _ciclosPedido = ligado ? CICLOS_RAPIDO : CICLOS_NORMAL;
  if (!_drv.iniciado()) return;
  if (_cal.brancoOk) {
    aplicarExposicaoDaCalibracao();
    _cls.preparar(_cal);
  } else {
    _drv.definirExposicao(_ciclosPedido, _drv.ganho());
  }
  reiniciarFiltro();
}

void CorFacil::reiniciarFiltro() {
  _filtro.reiniciar();
  _esperar = true;
  _temUltima = false;
}

// Percebe quando o sensor some, volta ou reinicia sozinho (queda de tensão).
void CorFacil::vigiar() {
  const bool ok = _drv.conectado();
  if (ok != _estavaConectado) {
    _estavaConectado = ok;
    reiniciarFiltro();
    avisar(EV_CONEXAO, ok);
  }
  if (_drv.reinicios() != _reiniciosVistos) {
    _reiniciosVistos = _drv.reinicios();
    reiniciarFiltro();
    avisar(EV_REINICIO);
  }
  if (_drv.modoPorTempo() && !_avisouTempo) {
    _avisouTempo = true;
    avisar(EV_POR_TEMPO);
  }
}

// ================================================================ calibrar

bool CorFacil::calibrar(uint8_t alvoNum) {
  const Cor alvo = (Cor)alvoNum;
  const int n = MAX_AMOSTRAS;
  Relatorio rel;
  relatorioVazio(rel, n);
  if (alvo != BRANCO && alvo != PRETO && alvo != VERDE && alvo != VERMELHO) {
    rel.avisos |= AV_COR_INVALIDA;
  } else if (!_drv.iniciado() || !_drv.conectado()) {
    rel.avisos |= AV_SEM_SENSOR;
  } else if (alvo != BRANCO && !_cal.brancoOk) {
    rel.avisos |= AV_SEM_BRANCO;
  }
  if (rel.avisos) {
    avisar(EV_RESULTADO, alvo, &rel);
    return false;
  }
  avisar(EV_CALIBRANDO, alvo);
  const uint8_t ganhoAntes = _drv.ganho(), ciclosAntes = _drv.ciclos();
  uint16_t avisosExposicao = 0;
  if (alvo == BRANCO) avisosExposicao = ajustarExposicaoParaBranco();
  Amostra amostras[MAX_AMOSTRAS];  // 320 bytes, só durante a calibração
  const uint32_t espera = 3 * _drv.periodoUs() / 1000 + 100;
  _drv.esperarLeitura(espera);  // descarta uma: garante ciclo inteiro na posição atual
  int m = 0;
  for (int i = 0; i < n; i++) {
    if (!_drv.esperarLeitura(espera)) break;
    const TCS34725::Leitura &l = _drv.leitura();
    amostras[m].r = l.r;
    amostras[m].g = l.g;
    amostras[m].b = l.b;
    amostras[m].c = l.c;
    m++;
  }
  if (m < n / 2) {
    rel.avisos |= AV_SEM_SENSOR;
  } else if (alvo == BRANCO) {
    calibrarBranco(rel, _cal, amostras, m, _drv.ganho(), _drv.ciclos(), _drv.limiteSaturacao());
  } else {
    calibrarCor(rel, _cal, alvo, amostras, m, _drv.limiteSaturacao());
  }
  rel.avisos |= avisosExposicao;
  if (rel.ok) {
    _exposicaoMudou = false;
    _cls.preparar(_cal);
    if (!salvar()) rel.avisos |= AV_NAO_SALVOU;
  } else if (alvo == BRANCO && (_drv.ganho() != ganhoAntes || _drv.ciclos() != ciclosAntes)) {
    _drv.definirExposicao(ciclosAntes, ganhoAntes);  // não deu: volta à exposição do branco que vale
    rel.avisos &= (uint16_t)~AV_EXPOSICAO;
  }
  reiniciarFiltro();
  avisar(EV_RESULTADO, alvo, &rel);
  return rel.ok;
}

bool CorFacil::calibrado() {
  return calibrada(_cal, BRANCO) && calibrada(_cal, PRETO) && calibrada(_cal, VERDE) && calibrada(_cal, VERMELHO);
}

bool CorFacil::calibrado(uint8_t c) { return calibrada(_cal, c); }

void CorFacil::apagarCalibracao() {
  zerar(_cal);
  apagarMemoria();
  _cls.preparar(_cal);
  reiniciarFiltro();
}

void CorFacil::apagarCalibracao(uint8_t c) {
  esquecer(_cal, c);
  if (_cal.brancoOk || _cal.preto.ok || _cal.colorida[0].ok || _cal.colorida[1].ok) salvar();
  else apagarMemoria();
  _cls.preparar(_cal);
  reiniciarFiltro();
}

// C médio de n leituras; -1 se o sensor não respondeu.
float CorFacil::mediaClaro(int n) {
  const uint32_t espera = 3 * _drv.periodoUs() / 1000 + 100;
  float soma = 0.0f;
  int k = 0;
  for (int i = 0; i < n; i++) {
    if (!_drv.esperarLeitura(espera)) continue;
    soma += (float)_drv.leitura().c;
    k++;
  }
  return k ? soma / k : -1.0f;
}

// Deixa o branco em ~20-65 % do limite de saturação mexendo no ganho (folga
// para reflexo e para o sensor chegando mais perto). Se já está nessa faixa, não mexe.
uint16_t CorFacil::ajustarExposicaoParaBranco() {
  uint16_t av = 0;
  const uint8_t ganhoAntes = _drv.ganho(), ciclosAntes = _drv.ciclos();
  float lim = _drv.limiteSaturacao();
  const float atual = mediaClaro(3);
  if (atual < 0.0f) return AV_SEM_SENSOR;
  if (atual >= 0.20f * lim && atual <= 0.65f * lim) return 0;
  uint8_t ciclos = _drv.ciclos();
  _drv.definirExposicao(ciclos, 0);
  float c1 = mediaClaro(3);
  if (c1 < 0.0f) return AV_SEM_SENSOR;
  while (c1 > 0.60f * lim && ciclos > 1) {  // claro demais até no 1x: encurta a integração
    ciclos = (uint8_t)(ciclos / 2 > 0 ? ciclos / 2 : 1);
    _drv.definirExposicao(ciclos, 0);
    lim = _drv.limiteSaturacao();
    c1 = mediaClaro(3);
  }
  uint8_t g = 0;
  for (int i = 3; i >= 1; i--) {
    if (c1 * TCS34725::ganhoX((uint8_t)i) * 1.1f <= 0.60f * lim) {
      g = (uint8_t)i;
      break;
    }
  }
  _drv.definirExposicao(ciclos, g);
  float cg = mediaClaro(3);
  if (cg > 0.85f * lim && g > 0) {
    g--;
    _drv.definirExposicao(ciclos, g);
    cg = mediaClaro(3);
  }
  if (g == 3 && cg < 0.10f * lim) av |= AV_BRANCO_FRACO;
  if (g != ganhoAntes || ciclos != ciclosAntes) av |= AV_EXPOSICAO;
  return av;
}

// Ao ligar: põe o sensor na exposição em que o branco foi medido. Se o
// programa pediu outro modo (modoRapido), as contagens do branco escalam junto
// (a contagem é proporcional ao tempo de integração) e o aviso pede para recalibrar.
void CorFacil::aplicarExposicaoDaCalibracao() {
  if (!_cal.brancoOk) return;
  if (_ciclosPedido && _cal.ciclos != _ciclosPedido && _cal.ciclos > 0) {
    const float k = (float)_ciclosPedido / (float)_cal.ciclos;
    for (int c = 0; c < 4; c++) _cal.branco[c] *= k;
    _cal.ciclos = _ciclosPedido;
    _exposicaoMudou = true;
  }
  _drv.definirExposicao(_cal.ciclos, _cal.ganho);
}

// ================================================================ memória

bool CorFacil::salvar() {
  _cal.magica = MAGICA;
  _cal.soma = somaVerificacao(_cal);
#if defined(CORFACIL_NVS)
  Preferences p;
  if (!p.begin(NAMESPACE_NVS, false)) return false;
  char chave[8];
  chaveNvs(chave, _slot);
  const size_t escritos = p.putBytes(chave, &_cal, sizeof(_cal));
  p.end();
  return escritos == sizeof(_cal);
#elif defined(CORFACIL_EEPROM)
  const int e = enderecoEeprom(_slot);
  if (e < 0) return false;
  EEPROM.put(e, _cal);  // só regrava os bytes que mudaram
  Calibracao volta;
  EEPROM.get(e, volta);
  return memcmp(&volta, &_cal, sizeof(volta)) == 0;
#else
  return false;
#endif
}

bool CorFacil::carregar() {
  Calibracao tmp;
#if defined(CORFACIL_NVS)
  Preferences p;
  if (!p.begin(NAMESPACE_NVS, false)) return false;
  char chave[8];
  chaveNvs(chave, _slot);
  const bool tamanhoCerto = p.getBytesLength(chave) == sizeof(tmp);
  if (tamanhoCerto) p.getBytes(chave, &tmp, sizeof(tmp));
  p.end();
  if (!tamanhoCerto) return false;
#elif defined(CORFACIL_EEPROM)
  const int e = enderecoEeprom(_slot);
  if (e < 0) return false;
  EEPROM.get(e, tmp);
#else
  return false;
#endif
  if (tmp.magica != MAGICA || tmp.soma != somaVerificacao(tmp)) return false;
  _cal = tmp;
  aplicarExposicaoDaCalibracao();
  _cls.preparar(_cal);
  reiniciarFiltro();
  return true;
}

void CorFacil::apagarMemoria() {
#if defined(CORFACIL_NVS)
  Preferences p;
  if (!p.begin(NAMESPACE_NVS, false)) return;
  char chave[8];
  chaveNvs(chave, _slot);
  p.remove(chave);
  p.end();
#elif defined(CORFACIL_EEPROM)
  const int e = enderecoEeprom(_slot);
  if (e < 0) return;
  const uint32_t vazio = 0xFFFFFFFFUL;
  EEPROM.put(e, vazio);
#endif
}

// ================================================================ mensagens
// Tudo daqui para baixo só vai para a placa se o programa chamar mensagens(),
// calibrarPeloMonitor(), mostrarCalibracao() ou mostrarLeitura().

void CorFacil::mensagens(Print &saida) {
  _saida = &saida;
  _msg = &CorFacil::imprimirEvento;
  if (_drv.iniciado()) imprimirInicio();
}

void CorFacil::prefixo(Print &p) {
  p.print('[');
  p.print(_nome);
  p.print(F("] "));
}

void CorFacil::imprimirEvento(uint8_t evento, uint8_t cor, const Relatorio *rel) {
  if (!_saida) return;
  Print &p = *_saida;
  switch (evento) {
    case EV_INICIO: imprimirInicio(); return;
    case EV_RESULTADO: imprimirResultado((Cor)cor, *rel); return;
    default: break;
  }
  prefixo(p);
  switch (evento) {
    case EV_PINOS_FIXOS: p.println(F("(nesta placa o I2C tem pinos fixos: usei SDA e SCL da placa)")); break;
    case EV_CALIBRANDO:
      p.print(F("calibrando "));
      p.print(nomeCor(cor));
      p.println(F("..."));
      break;
    case EV_CONEXAO: p.println(cor ? F("sensor voltou.") : F("! sensor sumiu do I2C (fio solto? mau contato?)")); break;
    case EV_REINICIO: p.println(F("! sensor reiniciou sozinho (faltou energia nele?) e foi reconfigurado")); break;
    case EV_POR_TEMPO: p.println(F("sensor sem AINT (clone?): lendo por tempo, funciona igual")); break;
  }
}

void CorFacil::imprimirInicio() {
  Print &p = *_saida;
  prefixo(p);
  p.print(F("CorFacil " CORFACIL_VERSAO ", I2C"));
  if (_drv.sda() >= 0) {
#if defined(ARDUINO_ARCH_ESP32)
    p.print(F(" SDA=GPIO"));
    p.print(_drv.sda());
    p.print(F(" SCL=GPIO"));
    p.print(_drv.scl());
#elif defined(PIN_A4) && defined(PIN_A5)
    if (_drv.sda() == PIN_A4 && _drv.scl() == PIN_A5) {
      p.print(F(" SDA=A4 SCL=A5"));
    } else {
      p.print(F(" SDA="));
      p.print(_drv.sda());
      p.print(F(" SCL="));
      p.print(_drv.scl());
    }
#else
    p.print(F(" SDA="));
    p.print(_drv.sda());
    p.print(F(" SCL="));
    p.print(_drv.scl());
#endif
  }
  if (_muxEndereco >= 0) {
    p.print(F(", TCA9548A canal "));
    p.print(_muxCanal);
  }
  p.println();
  prefixo(p);
  if (_drv.conectado()) {
    p.print(F("sensor ok (ID 0x"));
    p.print(_drv.id(), HEX);
    p.println(_drv.idConhecido() ? F(")") : F(", clone? funciona igual)"));
    if (!_drv.registrosConferem()) {
      prefixo(p);
      p.println(F("! registradores nao conferem: fio ruim?"));
    }
  } else {
    p.println(F("! sensor NAO respondeu. Confira VIN (5V no Uno/Nano/Mega, 3V3 no ESP32), GND, SDA e SCL"));
    p.print(F("    (nao trocados). Tentando de novo a cada 0,5 s. No I2C respondeu:"));
    TwoWire *w = _drv.barramento();
    bool achou = false;
    for (uint8_t a = 1; a < 127; a++) {
      w->beginTransmission(a);
      if (w->endTransmission() == 0) {
        p.print(F(" 0x"));
        p.print(a, HEX);
        achou = true;
      }
    }
    p.println(achou ? F("") : F(" ninguem"));
  }
  if (_semMemoria) {
    prefixo(p);
    p.println(F("! sem espaco na memoria: a calibracao vale ate desligar"));
  }
  if (_exposicaoMudou) {
    prefixo(p);
    p.println(F("! o modo rapido mudou: calibre o BRANCO de novo"));
  }
  if (calibrado()) {
    prefixo(p);
    p.println(F("calibrado: BRANCO PRETO VERDE VERMELHO"));
  } else {
    imprimirFaltam(p);
  }
}

void CorFacil::imprimirFaltam(Print &p) {
  prefixo(p);
  p.print(F("falta calibrar:"));
  const uint8_t ordem[4] = {BRANCO, PRETO, VERDE, VERMELHO};
  for (uint8_t i = 0; i < 4; i++) {
    if (calibrada(_cal, ordem[i])) continue;
    p.print(' ');
    p.print(nomeCor(ordem[i]));
  }
  p.println(F(" (veja o exemplo 02_CalibrarPeloMonitor)"));
}

void CorFacil::imprimirResultado(Cor alvo, const Relatorio &rel) {
  Print &p = *_saida;
  const uint16_t a = rel.avisos;
  prefixo(p);
  if (a & AV_COR_INVALIDA) {
    p.println(F("so da para calibrar BRANCO, PRETO, VERDE e VERMELHO"));
    return;
  }
  p.print(nomeCor(alvo));
  if (!rel.ok) {
    p.println(F(" NAO calibrado"));
  } else {
    p.print(F(" ok: "));
    if (alvo == BRANCO) {
      imprimirPorcento(p, _cal.branco[3] / _drv.limiteSaturacao());
      p.print(F(" do limite, ganho "));
      p.print(TCS34725::ganhoX(_drv.ganho()));
      p.print('x');
    } else if (alvo == PRETO) {
      imprimirDecimal(p, 100.0f * rel.brilho, 1);
      p.print(F("% do branco"));
    } else {
      p.print(F("croma "));
      imprimirDecimal(p, rel.croma, 3);
      p.print(F(", matiz "));
      p.print((int)(rel.matiz + 0.5f));
    }
    p.print(F(" ("));
    p.print(rel.usadas);
    p.print('/');
    p.print(rel.total);
    p.println(F(" leituras)"));
  }
  if (a & AV_SEM_SENSOR) p.println(F("  ! o sensor nao respondeu"));
  if (a & AV_SEM_BRANCO) p.println(F("  ! calibre o BRANCO primeiro"));
  if (a & AV_POUCAS_AMOSTRAS) p.println(F("  ! leituras variando: sensor na borda ou mexendo? Repita parado"));
  if (a & AV_SATURADO) p.println(F("  ! claro demais (limite do sensor): luz de fora ou sensor encostando?"));
  if (a & AV_PRETO_CLARO) p.println(F("  ! preto claro (+35% do branco): sensor alto ou luz entrando por baixo?"));
  if (a & AV_COR_FRACA) p.println(F("  ! quase sem cor: era a cor certa? Aproxime o sensor e tampe a luz de fora"));
  if (a & AV_COR_ESCURA) p.println(F("  ! cor muito escura: aproxime o sensor do chao"));
  if (a & AV_INVALIDO) p.println(F("  ! valor impossivel para essa cor: era a cor certa? Nada salvo"));
  if (a & AV_MATIZ_ESTRANHO) {
    p.print(F("  ! nao parece "));
    p.print(nomeCor(alvo));
    p.println(alvo == VERDE ? F(" (o verde da matiz 100 a 160): era a cor certa?")
                            : F(" (o vermelho da matiz 340 a 20): era a cor certa?"));
  }
  if (a & AV_RUIDO_ALTO) p.println(F("  ! leituras instaveis: fio solto? luz piscando? sensor balancando?"));
  if (a & AV_BRANCO_FRACO) p.println(F("  ! pouca luz: o LED do sensor acendeu? sensor longe do chao?"));
  if (a & AV_EXPOSICAO) {
    p.print(F("  ganho ajustado para "));
    p.print(TCS34725::ganhoX(_drv.ganho()));
    p.print(F("x, "));
    imprimirDecimal(p, _drv.integracaoMs(), 1);
    p.println(F(" ms (as outras cores continuam valendo)"));
  }
  if (a & AV_NAO_SALVOU) p.println(F("  ! nao gravou na memoria: vale ate desligar"));
}

void CorFacil::imprimirAjuda(Print &p) {
  p.println(F("\nComandos: b BRANCO (primeiro)  p PRETO  v VERDE  r VERMELHO"));
  p.println(F("          c mostra a calibracao  x apaga  ? ajuda"));
  p.println(F("Sensor na altura de uso, parado em cima da cor.\n"));
}

void CorFacil::imprimirProximoPasso(Print &p) {
  p.print(F(">> "));
  if (!_cal.brancoOk) p.println(F("sensor no BRANCO e envie b"));
  else if (!_cal.preto.ok) p.println(F("sensor no PRETO (linha) e envie p"));
  else if (!_cal.colorida[0].ok) p.println(F("sensor no VERDE e envie v"));
  else if (!_cal.colorida[1].ok) p.println(F("sensor no VERMELHO e envie r"));
  else p.println(F("calibrado! Passe o sensor sobre cada cor e confira"));
}

void CorFacil::calibrarPeloMonitor(Stream &s) {
  if (_saida == nullptr) mensagens(s);
  if (!_ajudaMostrada) {
    _ajudaMostrada = true;
    imprimirAjuda(s);
    imprimirProximoPasso(s);
  }
  while (s.available() > 0) {
    const int lido = s.read();
    if (lido < 0) break;
    char ch = (char)lido;
    if (ch == '\r' || ch == '\n' || ch == ' ' || ch == '\t') continue;
    if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
    const uint8_t alvo = ch == 'b' ? BRANCO : ch == 'p' ? PRETO : ch == 'v' ? VERDE : ch == 'r' ? VERMELHO : INDEFINIDO;
    if (_apagarPendente) {
      _apagarPendente = false;
      if (millis() - _apagarEm < 5000 && (ch == 'x' || alvo != INDEFINIDO)) {
        if (ch == 'x') apagarCalibracao();
        else apagarCalibracao(alvo);
        prefixo(s);
        s.print(F("apagado: "));
        s.println(ch == 'x' ? "TUDO" : nomeCor(alvo));
        imprimirProximoPasso(s);
        continue;
      }
      s.println(F("apagar cancelado"));  // e o comando segue normal abaixo
    }
    if (alvo != INDEFINIDO) {
      calibrar(alvo);
      imprimirProximoPasso(s);
    } else if (ch == 'c') {
      mostrarCalibracao(s);
    } else if (ch == 'x') {
      _apagarPendente = true;
      _apagarEm = millis();
      s.println(F("apagar: envie x de novo (tudo) ou b/p/v/r (so essa cor)"));
    } else if (ch == '?' || ch == 'h') {
      imprimirAjuda(s);
      imprimirProximoPasso(s);
    } else {
      s.print(F("comando '"));
      s.print(ch);
      s.println(F("' nao existe (envie ?)"));
    }
  }
}

void CorFacil::mostrarCalibracao(Print &p) {
  prefixo(p);
  imprimirDecimal(p, _drv.integracaoMs(), 1);
  p.print(F(" ms, ganho "));
  p.print(TCS34725::ganhoX(_drv.ganho()));
  p.print(F("x, "));
  p.print((int)(_drv.taxaHz() + 0.5f));
  p.println(F(" leituras/s"));
  p.print(F("  BRANCO   "));
  if (_cal.brancoOk) {
    p.print(F("C "));
    p.print((long)(_cal.branco[3] + 0.5f));
    p.print(F(" ("));
    imprimirPorcento(p, _cal.branco[3] / _drv.limiteSaturacao());
    p.print(F(" do limite), ruido "));
    imprimirDecimal(p, _cls.ruido(), 4);
    p.println();
  } else {
    p.println(F("-"));
  }
  p.print(F("  PRETO    "));
  if (_cal.preto.ok) {
    imprimirDecimal(p, 100.0f * _cls.pretoRelativo(), 1);
    p.println(F("% do branco"));
  } else {
    p.println(F("- (usando 6%)"));
  }
  for (int k = 0; k < N_COLORIDAS; k++) {
    p.print(k == 0 ? F("  VERDE    ") : F("  VERMELHO "));
    if (_cls.temCor(corColorida(k))) {
      p.print(F("croma "));
      imprimirDecimal(p, _cls.cromaRef(k), 3);
      p.print(F(", matiz "));
      imprimirDecimal(p, _cls.matizRef(k), 1);
      p.println();
    } else {
      p.println(F("-"));
    }
  }
}

void CorFacil::mostrarLeitura(Print &p) {
  const Cor estavel = lerCor();
  prefixo(p);
  p.print(nomeCor(estavel));
  if (estavel == SEM_SENSOR) {
    p.println();
    return;
  }
  if (estavel != SEM_CALIBRACAO) {
    const Resultado r = ultimaLeitura();
    p.print(F(" (agora "));
    p.print(nomeCor(r.cor));
    p.print(F(") brilho "));
    imprimirDecimal(p, r.brilho, 2);
    p.print(F(" croma "));
    imprimirDecimal(p, r.croma, 3);
    if (r.croma >= 0.5f * _cls.cromaIndefinido()) {
      p.print(F(" matiz "));
      p.print((int)(matizDe(r) + 0.5f));
    }
    if (r.saturada) p.print(F(" SATURADO"));
  }
  const TCS34725::Leitura &l = _drv.leitura();
  p.print(F(" | RGBC "));
  p.print(l.r);
  p.print(' ');
  p.print(l.g);
  p.print(' ');
  p.print(l.b);
  p.print(' ');
  p.println(l.c);
}
