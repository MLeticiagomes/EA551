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

#include "matmul.h"
#include "ea551-mat-txt-parser.h"

#define EPSILON 1e-6


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
double **ler_matriz(int fd, uint32_t nlinhas, uint32_t ncols, uint8_t dtype) {
    double **mat = malloc(nlinhas * sizeof(double *)); /* aloca um espaço de n linhas na memoria*/
    if (!mat) { perror("malloc"); exit(1); }

    for (uint32_t i = 0; i < nlinhas; i++) {
        mat[i] = malloc(ncols * sizeof(double)); /* para cada valor aloca um espaço de n colunas na memoria*/
        if (!mat[i]) { perror("malloc"); exit(1); }

        for (uint32_t j = 0; j < ncols; j++) { /* percorre todos os valores de entrada*/
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
            mat[i][j] = valor;
        }
    }
    return mat;
}


double **multiplicar(double **A, uint32_t a_nlin, uint32_t a_ncol, double **B, uint32_t b_nlin, uint32_t b_ncol) {
    if (a_ncol != b_nlin) { /* verifica se a multiplicação pode ser realizada*/
        fprintf(stderr, "dimensoes incompativeis: A.ncols=%u, B.nlinhas=%u\n", a_ncol, b_nlin);
        return NULL;
    }

    double **C = malloc(a_nlin * sizeof(double *)); /* aloca memoria para a matriz C gerada*/
    if (!C) { perror("malloc"); return NULL; }

    for (uint32_t i = 0; i < a_nlin; i++) {
        C[i] = malloc(b_ncol * sizeof(double));
        if (!C[i]) { perror("malloc"); return NULL; }

        for (uint32_t j = 0; j < b_ncol; j++) { /* percorre os valores de umacoluna de b*/
            double soma = 0.0;
            for (uint32_t k = 0; k < a_ncol; k++) {  /* percorre os valores de uma coluna de a*/
                soma += A[i][k] * B[k][j]; /* multiplica todos os valores  que estão na coluna k de A e na linha k de B e soma os resultados das multiplicações*/
            }
            C[i][j] = soma; /* salva o resultado na matriz C */
        }
    }
    return C;
}

void liberar_matriz(double **mat, uint32_t nlinhas)
{
    if (!mat) return;
    for (uint32_t i = 0; i < nlinhas; i++) free(mat[i]); /* desaloca os valores da matriz da memoria*/
    free(mat);
}


int main(int argc, char *argv[]){

    if (argc < 4) { /* verifica se são passados as matrizes A, B, C*/
        fprintf(stderr, "Uso: %s matrizA.emat matrizB.emat matrizC.emat\n", argv[0]);
        return 1;
    }

    sem_t *pode_ler; /* semaforo para controlar a leitura da memoria compartilhada*/
    pode_ler = sem_open("/ex_g_sem", O_CREAT, 0600, 0);

    if (pode_ler == SEM_FAILED){
        perror("sem_open");
        return 1;
    }


    /* Extrair informações da matriz A*/

    int fd_A = open(argv[1], O_RDONLY); /* primeiro argumento passado*/

    struct header A;
    if (ler_header(fd_A, &A) < 0) { 
        close(fd_A); 
        sem_close(pode_ler);
        sem_unlink("/ex_g_sem"); 
        return 1; 
    }
       
    double **matA = ler_matriz(fd_A, A.nlinhas, A.ncols, A.dtype); /* le a matriz A */
    close(fd_A);


/* Extrair informações da matriz B*/

    int fd_B = open(argv[2], O_RDONLY);

    struct header B;
    if (ler_header(fd_B, &B) < 0) { 
        close(fd_B); 
        sem_close(pode_ler);
        sem_unlink("/ex_g_sem"); 
        return 1; 
    }
       
    double **matB = ler_matriz(fd_B, B.nlinhas, B.ncols, B.dtype);
    close(fd_B);


    /* matriz C */
    double **matC = multiplicar(matA, A.nlinhas, A.ncols, matB, B.nlinhas, B.ncols); /* gera a matriz C*/
    if (!matC) {
        liberar_matriz(matA, A.nlinhas);
        liberar_matriz(matB, B.nlinhas);
        sem_close(pode_ler);
        sem_unlink("/ex_g_sem");
        return 1;
    }

    size_t tam_total = (size_t)A.nlinhas * B.ncols * sizeof(double);


    /* cria a memoria compartilhada*/
    double *memoria = mmap(NULL, tam_total, PROT_READ | PROT_WRITE , MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (memoria == MAP_FAILED) {
        perror("mmap");
        liberar_matriz(matA, A.nlinhas);
        liberar_matriz(matB, B.nlinhas);
        liberar_matriz(matC, A.nlinhas);
        sem_close(pode_ler);
        sem_unlink("/ex_g_sem");
        return 1;
    }


    for (uint32_t i = 0; i < A.nlinhas; i++) { /* preenche os valores da matriz C na memoria*/
        for (uint32_t j = 0; j < B.ncols; j++) {
            memoria[i * B.ncols + j] = matC[i][j];
        }
    }

    sem_post(pode_ler); /* permite a leitura dos valores da memoria pelo processo filho*/
 
    /* processo filho que compara o valor presenta na memoria compartilhada  como o valor da matriz C*/

    pid_t pid = fork(); /* cri um processo filho */

    if ( pid == 0){

        sem_wait(pode_ler); /* trava o processo filho até o processo pai terminar a escrita*/

        int fd_C = open(argv[3], O_RDONLY);

        if (fd_C < 0) {
            perror("open");
            sem_close(pode_ler);
            sem_unlink("/ex_g_sem");
            return 1;
        }

        struct header C;
        if (ler_header(fd_C, &C) < 0) {
            close(fd_C);
            exit(1);
        }


        if (C.nlinhas != A.nlinhas || C.ncols != B.ncols) { /* verifica se a matriz C possui as dimensões esperadas*/
            fprintf(stderr, "matriz C tem dimensoes diferentes do resultado esperado\n");
            close(fd_C);
            exit(1);
        }

        double **respC = ler_matriz(fd_C, C.nlinhas, C.ncols, C.dtype); /* le a matriz C recebida na entrada*/
        close(fd_C);
     
        int corretude = 1;

        for(uint32_t i = 0; i < A.nlinhas; i++){ /* percorre todos os valores da matriz*/
            for(uint32_t j = 0; j < B.ncols; j++){
                double valor = memoria[i * B.ncols + j]; /* encontra o valor na memoria */
                double valor_esperado = respC[i][j]; /*  valor da matriz C recebida na entrda*/
                printf(" %g ", valor);
                if (fabs(valor_esperado -  valor) > EPSILON){ /* verifica se a diferença entre o valor esperado e o recebido é maior que epsilon (evitar diferenças devido a aproximações por ponto flutuante)*/
                    printf(" Valor esperado : %g | Valor obtido %g ", valor_esperado, valor); /* imprime as diferenças obtidas*/
                    corretude = 0;
                }
            }
        }

        if (corretude) { /* imprime uma mensagem caso haja/ nao haja erros*/
            printf("A multiplicacao foi gerada corretamente\n");
        }
        else {
            printf("A multiplicacao apresenta um erro\n");
        }

        liberar_matriz(respC, C.nlinhas);
        exit(0);

    }

    waitpid(pid, NULL, 0);

    liberar_matriz(matA, A.nlinhas);
    liberar_matriz(matB, B.nlinhas);
    liberar_matriz(matC, A.nlinhas);
    munmap(memoria, tam_total);
    sem_close(pode_ler);
    sem_unlink("/ex_g_sem");
 
    return 0;
    
}