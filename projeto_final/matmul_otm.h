#ifndef MATMUL_OT_H
#define MATMUL_OT_H

#include <stdint.h>
#include "ea551-mat-txt-parser.h"


/* cabeçalho de um arquivo .emat */
struct header {
    unsigned char head[4];   /* "EMAT" */
    uint8_t dtype;
    uint32_t nlinhas;
    uint32_t ncols;
};

/* lê o header de um .emat; retorna 0 em sucesso, -1 em erro */
int ler_header(int fd, struct header *h);

/* lê nlinhas*ncols valores e devolve tudo como double (o chamador dá free) */
double *ler_matriz(int fd, uint32_t nlinhas, uint32_t ncols, uint8_t dtype);

/* C = A * B (A: a_nlin x a_ncol, B: b_nlin x b_ncol); retorna NULL em erro (o chamador dá free) */
double *multiplicar(double *A, uint32_t a_nlin, uint32_t a_ncol, double *B, uint32_t b_nlin, uint32_t b_ncol);

/* define o número de threads usadas na multiplicação */
void matmul_set_threads(int n);

/* compara duas matrizes com tolerância; retorna 1 se iguais */
int comparar_matrizes(const double *C, const double *C_esperada, uint32_t nlin, uint32_t ncol);

#endif