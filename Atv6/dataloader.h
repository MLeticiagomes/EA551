#ifndef DATALOADER_H
#define DATALOADER_H
#include <stdint.h>

int  data_loader(int argc, char *argv[], void (*consumidor)(void));
int  ler_proxima_imagem(double *dst, int32_t *label);
long total_imagens(void);

#endif