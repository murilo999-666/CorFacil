#include "CorFacil_TCS34725.h"

namespace corfacil {

namespace reg {
const uint8_t CMD = 0x80, AUTO_INC = 0x20, ESPECIAL = 0x60, SF_LIMPA_INT = 0x06;
const uint8_t ENABLE = 0x00, ATIME = 0x01, AILTL = 0x04, PERS = 0x0C, CONFIG = 0x0D, CONTROL = 0x0F;
const uint8_t ID = 0x12, STATUS = 0x13, CDATAL = 0x14;
const uint8_t PON = 0x01, AEN = 0x02, ST_AVALID = 0x01, ST_AINT = 0x10;
}  // namespace reg

bool TCS34725::iniciar(TwoWire &wire, int sda, int scl, uint32_t hz, uint8_t ciclos, uint8_t ganho,
                       int16_t muxEndereco, int8_t muxCanal) {
  _w = &wire;
  _sda = sda;
  _scl = scl;
  _hz = hz;
  _ciclos = ciclos < 1 ? 1 : ciclos;
  _ganho = ganho > 3 ? 3 : ganho;
  _muxEndereco = muxEndereco;
  _muxCanal = muxCanal;
  comecarBarramento();
  recalcular();
  _ok = configurar();
  _tReconectar = millis();
  _tSaude = millis();
  return _ok;
}

void TCS34725::comecarBarramento() {
#if defined(ARDUINO_ARCH_ESP32)
  if (_sda >= 0 && _scl >= 0) {
    _w->begin(_sda, _scl, _hz);
  } else {
    _w->begin();
    _w->setClock(_hz);
  }
  _w->setTimeOut(10);  // ms: uma transação leva menos de 1 ms
#else
  _w->begin();
  _w->setClock(_hz);
#if defined(WIRE_HAS_TIMEOUT)
  _w->setWireTimeout(5000, true);  // us: sem isto, um fio ruim pode travar o Uno para sempre
#endif
#endif
}

bool TCS34725::atualizar() {
  if (_w == nullptr) return false;
  if (!_ok) {
    tentarReconectar();
    return false;
  }
  const uint32_t agora = micros();
  const uint32_t dt = agora - _tInicio;
  if (dt < _tPortaoUs) {  // ainda integrando: aproveita para a checagem de saúde
    if (millis() - _tSaude >= 1000) {
      _tSaude = millis();
      conferirConfiguracao();
    }
    return false;
  }
  if (agora - _tConsulta < 250) return false;  // não martela o I2C
  _tConsulta = agora;
  if (!_modoTempo) {
    uint8_t st = 0;
    if (!ler(reg::STATUS, &st, 1)) {
      falhou();
      return false;
    }
    if (st & reg::ST_AINT) {
      _semAint = 0;
    } else {
      if (dt < _tLimiteUs) return false;  // o ciclo ainda não fechou
      // Passou do tempo e o AINT não veio. Antes de concluir qualquer coisa,
      // confere se o sensor não reiniciou (aí ele está parado, de fábrica).
      if (!conferirConfiguracao()) return false;
      if (++_semAint >= 3) {
        _modoTempo = true;  // 3 ciclos seguidos sem AINT com o sensor rodando: clone, lê por tempo
      } else if (!(st & reg::ST_AVALID)) {
        _tInicio = micros();  // rodando, mas ainda sem dado válido: espera mais um ciclo
        return false;
      }
    }
  } else {
    if (dt < _tCicloMaxUs) return false;
    if (!conferirConfiguracao()) return false;  // sem AINT, é assim que se percebe um reinício
  }
  uint8_t d[8];
  if (!ler(reg::CDATAL, d, 8)) {
    falhou();
    return false;
  }
  _falhas = 0;
  if (!limparInterrupcao()) falhou();
  _tInicio = agora;
  montar((uint16_t)(d[2] | (d[3] << 8)), (uint16_t)(d[4] | (d[5] << 8)), (uint16_t)(d[6] | (d[7] << 8)),
         (uint16_t)(d[0] | (d[1] << 8)));
  return true;
}

bool TCS34725::esperarLeitura(uint32_t timeoutMs) {
  const uint32_t t0 = millis();
  do {
    if (atualizar()) return true;
    delay(1);
  } while (millis() - t0 < timeoutMs);
  return false;
}

void TCS34725::definirExposicao(uint8_t ciclos, uint8_t ganho) {
  _ciclos = ciclos < 1 ? 1 : ciclos;
  _ganho = ganho > 3 ? 3 : ganho;
  recalcular();
  if (!_ok) return;
  if (!escrever(reg::ATIME, (uint8_t)(256 - _ciclos)) || !escrever(reg::CONTROL, _ganho)) {
    falhou();
    return;
  }
  reiniciarIntegracao();
}

void TCS34725::recalcular() {
  // Cada ciclo = 2,4 ms de preparo (RGBC Init, datasheet fig. 9) + a integração.
  // O oscilador interno varia de 2,27 a 2,56 ms por passo (-5 % a +7 %).
  const uint32_t ciclo = 2400UL * (_ciclos + 1);
  _tPortaoUs = ciclo * 92 / 100;          // antes disso, nem pergunta
  _tLimiteUs = ciclo * 115 / 100 + 2000;  // depois disso, o AINT deveria ter vindo
  _tCicloMaxUs = ciclo * 108 / 100;       // leitura por tempo (clone sem AINT)
  uint32_t maximo = 1024UL * _ciclos;
  if (maximo > 65535UL) maximo = 65535UL;
  // Abaixo de 64 ciclos, a ams (nota DN40) manda considerar saturado a 75 % do máximo.
  _limiteSat = (_ciclos < 64) ? (uint16_t)(maximo * 3 / 4) : (uint16_t)maximo;
}

bool TCS34725::configurar() {
  uint8_t id = 0;
  if (!ler(reg::ID, &id, 1)) return false;
  _id = id;
  if (!escrever(reg::ENABLE, 0x00)) return false;  // começa de um estado conhecido
  if (!escrever(reg::ATIME, (uint8_t)(256 - _ciclos))) return false;
  if (!escrever(reg::CONTROL, _ganho)) return false;
  if (!escrever(reg::CONFIG, 0x00)) return false;  // WLONG desligado
  if (!escrever(reg::PERS, 0x00)) return false;    // AINT ao fim de todo ciclo
  if (!escreverLimiares()) return false;           // idem, por garantia
  if (!escrever(reg::ENABLE, reg::PON)) return false;
  delay(3);  // datasheet: 2,4 ms entre ligar (PON) e começar a medir (AEN)
  if (!escrever(reg::ENABLE, reg::PON | reg::AEN)) return false;
  if (!limparInterrupcao()) return false;
  _tInicio = micros();
  _semAint = 0;
  _falhas = 0;
  uint8_t at = 0, ct = 0;
  _registrosConferem = ler(reg::ATIME, &at, 1) && ler(reg::CONTROL, &ct, 1) && at == (uint8_t)(256 - _ciclos) &&
                       (ct & 0x03) == _ganho;
  return true;
}

// true = o sensor está ligado e com a configuração certa. false = ele
// reiniciou (faltou energia nele: volta com os registradores de fábrica) e já
// foi reconfigurado, ou o I2C falhou.
bool TCS34725::conferirConfiguracao() {
  uint8_t en = 0, at = 0, ct = 0;
  if (!ler(reg::ENABLE, &en, 1) || !ler(reg::ATIME, &at, 1) || !ler(reg::CONTROL, &ct, 1)) {
    falhou();
    return false;
  }
  if ((en & (reg::PON | reg::AEN)) == (reg::PON | reg::AEN) && at == (uint8_t)(256 - _ciclos) &&
      (ct & 0x03) == _ganho)
    return true;
  _reinicios++;
  if (!configurar()) {
    _ok = false;
    _tReconectar = millis();
  }
  return false;
}

void TCS34725::tentarReconectar() {
  if (millis() - _tReconectar < 500) return;
  _tReconectar = millis();
  _tentativas++;
  if (_tentativas % 4 == 0) liberarBarramento();
  if (configurar()) {
    _ok = true;
    _reconexoes++;
    _tentativas = 0;
    _tSaude = millis();
  }
}

void TCS34725::falhou() {
  if (++_falhas >= 4) {
    _ok = false;
    _tReconectar = millis();
  }
}

void TCS34725::montar(uint16_t r, uint16_t g, uint16_t b, uint16_t c) {
  _l.saturada = c >= _limiteSat;
  _l.r = r;
  _l.g = g;
  _l.b = b;
  _l.c = c;
  const uint32_t agora = micros();
  _l.numero++;
  // leituras/s numa janela de ~1 s; uma pausa longa (o programa parado
  // calibrando, ou num delay) recomeça a janela
  if (_l.numero == 1 || agora - _tUltimaUs > 6 * _tCicloMaxUs) {
    _janelaUs = agora;
    _janelaN = _l.numero;
  } else if (agora - _janelaUs >= 1000000UL || (_taxaHz == 0.0f && _l.numero - _janelaN >= 10)) {
    _taxaHz = 1e6f * (float)(_l.numero - _janelaN) / (float)(agora - _janelaUs);
    _janelaUs = agora;
    _janelaN = _l.numero;
  }
  _tUltimaUs = agora;
}

// Para a integração e começa um ciclo novo do zero. Reescreve tempo e ganho
// junto: se o sensor tiver reiniciado, já volta certo.
void TCS34725::reiniciarIntegracao() {
  bool ok = escrever(reg::ENABLE, reg::PON);
  ok = escrever(reg::ATIME, (uint8_t)(256 - _ciclos)) && ok;
  ok = escrever(reg::CONTROL, _ganho) && ok;
  ok = escrever(reg::ENABLE, reg::PON | reg::AEN) && ok;
  ok = limparInterrupcao() && ok;
  _tInicio = micros();
  if (!ok) falhou();
}

// Destrava o I2C se algum dispositivo ficou segurando o SDA (ex.: reset no
// meio de uma leitura): até 9 pulsos no SCL e um STOP. Os pinos imitam
// dreno aberto: só puxam para LOW; HIGH é o pull-up do módulo.
void TCS34725::liberarBarramento() {
  if (_sda < 0 || _scl < 0) return;
  _w->end();
#if defined(ARDUINO_ARCH_ESP32)
  pinMode(_sda, INPUT_PULLUP);
  pinMode(_scl, OUTPUT_OPEN_DRAIN);
  digitalWrite(_scl, HIGH);
  for (int i = 0; i < 9 && digitalRead(_sda) == LOW; i++) {
    digitalWrite(_scl, LOW);
    delayMicroseconds(5);
    digitalWrite(_scl, HIGH);
    delayMicroseconds(5);
  }
  pinMode(_sda, OUTPUT_OPEN_DRAIN);
  digitalWrite(_sda, LOW);
  delayMicroseconds(5);
  digitalWrite(_scl, HIGH);
  delayMicroseconds(5);
  digitalWrite(_sda, HIGH);
  delayMicroseconds(5);
#else
  // LOW: primeiro digitalWrite(LOW) (desliga o pull-up interno), depois OUTPUT.
  // Solto: INPUT. Assim o pino nunca empurra HIGH contra quem segura o fio.
  pinMode(_sda, INPUT);
  pinMode(_scl, INPUT);
  for (int i = 0; i < 9 && digitalRead(_sda) == LOW; i++) {
    digitalWrite(_scl, LOW);
    pinMode(_scl, OUTPUT);
    delayMicroseconds(5);
    pinMode(_scl, INPUT);
    delayMicroseconds(5);
  }
  digitalWrite(_sda, LOW);
  pinMode(_sda, OUTPUT);
  delayMicroseconds(5);
  pinMode(_scl, INPUT);
  delayMicroseconds(5);
  pinMode(_sda, INPUT);
  delayMicroseconds(5);
#endif
  comecarBarramento();
}

bool TCS34725::selecionarMux() {
  if (_muxEndereco < 0) return true;
  _w->beginTransmission((uint8_t)_muxEndereco);
  _w->write((uint8_t)(1u << (_muxCanal & 7)));
  return _w->endTransmission() == 0;
}

bool TCS34725::escrever(uint8_t r, uint8_t valor) {
  if (!selecionarMux()) return false;
  _w->beginTransmission(ENDERECO);
  _w->write((uint8_t)(reg::CMD | r));
  _w->write(valor);
  return _w->endTransmission() == 0;
}

// Limiar baixo 0xFFFF acima do alto 0x0000: pelo datasheet só o baixo vale, e
// "C < 0xFFFF" é verdade em todo ciclo. Junto com PERS = 0, AINT a cada ciclo.
bool TCS34725::escreverLimiares() {
  if (!selecionarMux()) return false;
  _w->beginTransmission(ENDERECO);
  _w->write((uint8_t)(reg::CMD | reg::AUTO_INC | reg::AILTL));
  _w->write((uint8_t)0xFF);
  _w->write((uint8_t)0xFF);
  _w->write((uint8_t)0x00);
  _w->write((uint8_t)0x00);
  return _w->endTransmission() == 0;
}

bool TCS34725::limparInterrupcao() {
  if (!selecionarMux()) return false;
  _w->beginTransmission(ENDERECO);
  _w->write((uint8_t)(reg::CMD | reg::ESPECIAL | reg::SF_LIMPA_INT));  // 0xE6
  return _w->endTransmission() == 0;
}

bool TCS34725::ler(uint8_t r, uint8_t *buf, uint8_t n) {
  if (!selecionarMux()) return false;
  _w->beginTransmission(ENDERECO);
  _w->write((uint8_t)(reg::CMD | reg::AUTO_INC | r));
  if (_w->endTransmission(false) != 0) return false;
  if (_w->requestFrom(ENDERECO, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = (uint8_t)_w->read();
  return true;
}

}  // namespace corfacil
