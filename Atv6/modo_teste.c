#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "matmul_otm.h"
#include "dataloader.h"
#include "classif.h" 


static double *W;   /* carregado no pai; o filho herda no fork */

static void consumidor_teste(void){

    long n = total_imagens(), acertos = 0;
    double x[784], scores[10];
    int32_t label;

    for (long i = 0; i < n; i++) {
        ler_proxima_imagem(x, &label);
        int pred = classificar(W, x, scores);   /* usa multiplicar() do matmul */
        if (pred == label) acertos++;
    }
    printf("Acuracia no teste: %ld/%ld = %.4f\n", acertos, n, (double)acertos / n);
    fflush(stdout);  
}

int teste(int argc, char *argv[]){

    uint32_t w_lin, w_col;
    W = inicializa_pesos(caminho_pesos(argc, argv), PESOS_EMAT, &w_lin, &w_col); 
    if (!W || w_lin != 10 || w_col != 784) {
        fprintf(stderr, "pesos invalidos\n");
        return 1;
    }
    
    int r = data_loader(argc, argv, consumidor_teste);
    free(W);
    return r;
}