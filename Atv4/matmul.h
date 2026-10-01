#ifndef MATMUL_H
#define MATMUL_H

#include <stdint.h>
#include <stddef.h>

struct header {
    unsigned char head[4];
    uint8_t dtype;
    uint32_t nlinhas;
    uint32_t ncols;
};

int ler_header(int fd, struct header *h);
double **ler_matriz(int fd, uint32_t nlinhas, uint32_t ncols, uint8_t dtype);
double **multiplicar(double **A, uint32_t a_nlin, uint32_t a_ncol, double **B, uint32_t b_nlin, uint32_t b_ncol);
void liberar_matriz(double **mat, uint32_t nlinhas);

#endif