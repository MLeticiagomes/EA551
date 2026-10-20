#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>

#include "matmul_ot.h"
#include "ea551-mat-txt-parser.h"

#define EPSILON 1e-6
#define NTHREADS    3       /* número de threads*/
#define NTHREADS_MULT 4

/* le o header (head, dtype, nlinhas, ncols) de um .emat  */
int ler_header(int fd, struct header *h) {
    size_t total, len;
    ssize_t n;

    total = 0; len = sizeof(h->head);
    while (total < len) {
        n = read(fd, (char*)&h->head + total, len - total);
        if (n < 0) { perror("read"); return -1; }
        if (n == 0) break;
        total += n;
    }

    if (h->head[0] != 0x45 || h->head[1] != 0x4D || h->head[2] != 0x41 || h->head[3] != 0x54) {
        fprintf(stderr, "arquivo .emat invalido\n");
        return -1;
    }

    total = 0; len = sizeof(h->dtype);
    while (total < len) {
        n = read(fd, (char*)&h->dtype + total, len - total);
        if (n < 0) { perror("read"); return -1; }
        if (n == 0) break;
        total += n;
    }

    total = 0; len = sizeof(h->nlinhas);
    while (total < len) {
        n = read(fd, (char*)&h->nlinhas + total, len - total);
        if (n < 0) { perror("read"); return -1; }
        if (n == 0) break;
        total += n;
    }

    total = 0; len = sizeof(h->ncols);
    while (total < len) {
        n = read(fd, (char*)&h->ncols + total, len - total);
        if (n < 0) { perror("read"); return -1; }
        if (n == 0) break;
        total += n;
    }

    return 0;
}

/* le os valores presentes na matriz*/
double *ler_matriz(int fd, uint32_t nlinhas, uint32_t ncols, uint8_t dtype) {
    
    long total_elem = (long)nlinhas * ncols;
    double *mat = malloc(total_elem * sizeof(double)); /* aloca um espaço de n linhas na memoria*/
    if (!mat) { perror("malloc"); exit(1); }

    for (uint32_t j = 0; j < total_elem; j++) { /* percorre todos os valores de entrada*/
        size_t total_char = 0;
        size_t len_elem;
        ssize_t n_elem;
        double valor;

        switch (dtype) { /* para cada valor de entrada identifica o seu tipo e realiza a leitura do valor*/
            case EA551_DTYPE_UINT8: { 
                uint8_t v;
                len_elem = sizeof(v);
                while (total_char < len_elem) {
                    n_elem = read(fd, (char*)&v + total_char, len_elem - total_char); /* onde leu, onde guardar na memoria, e a quantidade de bytes que faltam */
                    if (n_elem < 0) { perror("read"); exit(1); } /* se a quantidade de bytes lidos for menor q zero gera um erro*/
                    if (n_elem == 0) break; /* fim do arquivo*/
                    total_char += n_elem;
                }
                valor = (double) v;
                break;
            }
            case EA551_DTYPE_INT32: {
                int32_t v;
                len_elem = sizeof(v);
                while (total_char < len_elem) {
                    n_elem = read(fd, (char*)&v + total_char, len_elem - total_char);
                    if (n_elem < 0) { perror("read"); exit(1); }
                    if (n_elem == 0) break;
                    total_char += n_elem;
                }
                valor = (double) v;
                break;
            }
            case EA551_DTYPE_FLOAT32: {
                float v;
                len_elem = sizeof(v);
                while (total_char < len_elem) {
                    n_elem = read(fd, (char*)&v + total_char, len_elem - total_char);
                    if (n_elem < 0) { perror("read"); exit(1); }
                    if (n_elem == 0) break;
                    total_char += n_elem;
                }
                valor = (double) v;
                break;
            }
            default:
                fprintf(stderr, "dtype desconhecido\n");
                exit(1);
        }
        mat[j] = valor;
    }

    return mat;
}


struct thread_arg {
    const char *path;
    struct header h;   
    double *buf;     
    int ok;            /* 0 = sucesso, -1 = erro */
};

void *ler_matriz_thread(void *arg){ /* cada thread le o header da sua matriz e a sua matriz correspondente*/ 
    struct thread_arg *ta = (struct thread_arg *) arg;

    int fd = open(ta->path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        ta->ok = -1;
        return NULL;
    }

    if (ler_header(fd, &ta->h) < 0) {
        close(fd);
        ta->ok = -1;
        return NULL;
    }

    ta->buf = ler_matriz(fd, ta->h.nlinhas, ta->h.ncols, ta->h.dtype);
    close(fd);
    ta->ok = 0;
    return NULL;
}


struct mult_arg {
    const double *A, *B;
    double *C;
    uint32_t a_ncol, b_ncol;
    uint32_t linha_inicio, linha_fim;
};

void *multiplicar_thread(void *arg){ /* realiza a multiplicação das partes da matriz A e B de cada threads*/
    struct mult_arg *ma = (struct mult_arg *) arg;

    for (uint32_t i = ma->linha_inicio; i < ma->linha_fim; i++) {
        for (uint32_t k = 0; k < ma->a_ncol; k++) {
            double a_ik = ma->A[i * ma->a_ncol + k];
            const double *linha_b = &ma->B[k * ma->b_ncol];
            double *linha_c = &ma->C[i * ma->b_ncol];

            for (uint32_t j = 0; j < ma->b_ncol; j++) {
                linha_c[j] += a_ik * linha_b[j];
            }
        }
    }
    return NULL;
}

double *multiplicar(double *A, uint32_t a_nlin, uint32_t a_ncol, double *B, uint32_t b_nlin, uint32_t b_ncol, double *C_esperada, int *corretude) {
    
    if (a_ncol != b_nlin) { /* verifica se a multiplicação pode ser realizada*/
        fprintf(stderr, "dimensoes incompativeis: A.ncols=%u, B.nlinhas=%u\n", a_ncol, b_nlin);
        return NULL;
    }

    size_t tam = (size_t)a_nlin * b_ncol;
    double *C = calloc(tam, sizeof(double));
    if (!C) { perror("malloc"); return NULL; }

    int n_threads = NTHREADS_MULT;
    if ((uint32_t)n_threads > a_nlin) n_threads = (int) a_nlin;  /* nunca mais threads que linhas */

    pthread_t th[NTHREADS_MULT];
    struct mult_arg margs[NTHREADS_MULT];


    uint32_t linhas_por_thread = a_nlin / n_threads;
    for (int t = 0; t < n_threads; t++) { /* divide as matrizes que serão multiplicadas entre as threads*/
        margs[t].A = A;
        margs[t].B = B;
        margs[t].C = C;
        margs[t].a_ncol = a_ncol;
        margs[t].b_ncol = b_ncol;
        margs[t].linha_inicio = (uint32_t)t * linhas_por_thread;
        margs[t].linha_fim = (t == n_threads - 1) ? a_nlin : margs[t].linha_inicio + linhas_por_thread;
        pthread_create(&th[t], NULL, multiplicar_thread, &margs[t]); 
    }
    for (int t = 0; t < n_threads; t++) {
        pthread_join(th[t], NULL);
    }

    *corretude = 1; /* verifica se a matriz gerada é igual a esperada*/
    for (uint32_t i = 0; i < a_nlin; i++) {
        for (uint32_t j = 0; j < b_ncol; j++) { /* percorre os valores de uma coluna de b*/
            double valor_esperado = C_esperada[i * b_ncol + j];
            double valor = C[i * b_ncol + j];
            if(fabs(valor_esperado - valor) > EPSILON){ /* verifica se o valor em uma posição específica é igual ao da matriz C esperada*/
                printf("Valor esperado: %g | Valor obtido: %g (posicao [%u][%u])\n", valor_esperado, valor, i, j); /* imprime as diferenças obtidas*/
                *corretude = 0;

            }
            
        }
    }
    return C;
}

int main(int argc, char *argv[]){
        

    if (argc < 4) { /* verifica se são passados as matrizes A, B, C*/
        fprintf(stderr, "Uso: %s matrizA.emat matrizB.emat matrizC.emat\n", argv[0]);
        return 1;
    }

    pthread_t t[NTHREADS];
    struct thread_arg targs[NTHREADS];
    /* associa os argumentos a targs*/
    targs[0].path = argv[1];
    targs[1].path = argv[2];
    targs[2].path = argv[3];

    /* cria as threads de ler as matrizes*/
    for (int i = 0; i < NTHREADS; i++) { 
        pthread_create(&t[i], NULL, ler_matriz_thread, &targs[i]);  /* associa* cada matriz a um thread -> thread 0 ira ler a matriz A, thread 1 a matriz B e thread 2 a matriz C/
    }
    for (int i = 0; i < NTHREADS; i++) {
        pthread_join(t[i], NULL);
    }

    for (int i = 0; i < NTHREADS; i++) {
        if (targs[i].ok < 0) {
            fprintf(stderr, "falha ao ler %s\n", targs[i].path);
            for (int j = 0; j < NTHREADS; j++) free(targs[j].buf);
            return 1;
        }
    }

    /* associa as matrizes aos argumentos passados*/
    struct header A = targs[0].h, B = targs[1].h, C_hdr = targs[2].h;
    double *matA = targs[0].buf;
    double *matB = targs[1].buf;
    double *matC_esperada = targs[2].buf;

    if (C_hdr.nlinhas != A.nlinhas || C_hdr.ncols != B.ncols) { /* verifica se a matriz C possui as dimensões esperadas*/
            fprintf(stderr, "matriz C tem dimensoes diferentes do resultado esperado\n");
            free(matA); free(matB); free(matC_esperada);
            exit(1);
        }

    int corretude = 0;
    double *matC = multiplicar(matA, A.nlinhas, A.ncols, matB, B.nlinhas, B.ncols, matC_esperada, &corretude);
    

    if (!matC) {
        free(matA); free(matB); free(matC_esperada);
        return 1;
    }

    /* verifica se a matriz gerada e a esperada são iguais*/
    if (corretude) {
        printf("A multiplicacao foi gerada corretamente\n");
    } else {
        printf("A multiplicacao apresenta um erro\n");
    }

    free(matA);
    free(matB);
    free(matC_esperada);
    free(matC);
   
    return 0;
    
}