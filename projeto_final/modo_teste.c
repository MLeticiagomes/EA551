#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "matmul_otm.h"
#include "dataloader.h"
#include "classif.h" 
#include "chave.h"

static double *W;   /* carregado no pai; o filho herda no fork */

static void consumidor_teste(void){

    long n = dataloader_quota(), acertos = 0;
    double x[16384], scores[10];
    int32_t label;

    for (long i = 0; i < n; i++) {
        ler_proxima_imagem(x, &label);
        int pred = classificar(W, x, scores);   /* usa multiplicar() do matmul */
        if (pred == label) acertos++;
    }
    dataloader_set_resultado(acertos);
}

int teste(int argc, char *argv[], int nconsumidores){

    uint32_t w_lin, w_col;
    W = inicializa_pesos(caminho_pesos(argc, argv), PESOS_EMAT, &w_lin, &w_col); 
    if (!W || w_lin != 10 || w_col != 16384) {
        fprintf(stderr, "pesos invalidos\n");
        return 1;
    }
    
    dataloader_set_consumers(nconsumidores);
    int r = data_loader(argc, argv, consumidor_teste);

    if (r == 0) {
        long n = total_imagens(), acertos = dataloader_resultado_total();
        printf("Acuracia no teste: %ld/%ld = %.4f\n", acertos, n, (double)acertos / n);
    }
    free(W);
    return r;
}