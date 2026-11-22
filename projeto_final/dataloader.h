#ifndef DATALOADER_H
#define DATALOADER_H
#include <stdint.h>

void dataloader_set_consumers(int n);
int  dataloader_consumer_id(void);
long dataloader_quota(void);
void dataloader_set_resultado(long v);
long dataloader_resultado_total(void);
int  data_loader(int argc, char *argv[], void (*consumidor)(void));
int  ler_proxima_imagem(double *dst, int32_t *label);
long total_imagens(void);


#endif