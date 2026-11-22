#ifndef CLASSIF_H
#define CLASSIF_H

#include <stdint.h>
#include "dataloader.h"
#include "chave.h"

#define PESOS_EMAT "W.emat"   /* arquivo .emat gerado a partir do W.npy na inicialização */

int npy_para_emat(const char *caminho_npy, const char *caminho_emat);


const char *caminho_pesos(int argc, char *argv[]);
double *carrega_pesos(const char *caminho, uint32_t *nl, uint32_t *nc);
double *inicializa_pesos(const char *npy, const char *emat, uint32_t *nl, uint32_t *nc);

/* multiplica W (10x16384) pela imagem (16384x1) e devolve o dígito previsto */
int classificar(double *W, double *x, double *scores_out);

/* modos do programa */
int inferencia(int argc, char *argv[]);
int teste(int argc, char *argv[], int nconsumidores);

void dataloader_set_buffer(int n);     /* tamanho do buffer circular (1..32) */
void dataloader_set_threads(int n);    /* threads produtoras (1..256) */
void inferencia_set_visual(int v);     /* 1 = abre a janela X11, 0 = nao abre */

#endif