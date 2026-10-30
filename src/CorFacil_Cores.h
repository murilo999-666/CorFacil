/*
 * CorFacil_Cores.h
 * As respostas de lerCor(). Ficam fora da classe para o programa poder
 * escrever só VERDE, como escreve HIGH no digitalWrite.
 */
#pragma once
#include <stdint.h>

enum Cor : uint8_t {
  INDEFINIDO = 0,  // tem cor, mas não é nenhuma das calibradas (ou é a borda de um marcador)
  PRETO,
  BRANCO,
  VERDE,
  VERMELHO,
  SEM_SENSOR,      // o sensor não respondeu no I2C (fio, alimentação, pinos)
  SEM_CALIBRACAO   // falta calibrar pelo menos o BRANCO
};
