#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>   
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include "matmul_otm.h"


#define EPSILON 1e-6
#define NTHREADS    3       /* número de threads*/
#define MAX_THREADS 64

static int nthreads_mult = 4; /* threads da multiplicacao (configuravel) */

void matmul_set_threads(int n){
    if (n >= 1 && n <= MAX_THREADS) nthreads_mult = n;
}

static int ler_exato(int fd, void *dst, size_t len)
{
    size_t total = 0;
    while (total < len) {
        ssize_t n = read(fd, (char *)dst + total, len - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read");
            return -1;
        }
        if (n == 0) {
            fprintf(stderr, "fim de arquivo inesperado\n");
            return -1;
        }
        total += (size_t)n;
    }
    return 0;
}


/* le o header (head, dtype, nlinhas, ncols) de um .emat  */

int ler_header(int fd, struct header *h)
{
    if (!h) {
        fprintf(stderr, "Header invalido: ponteiro NULL\n");
        return -1;
    }

    /* Inicializa o cabeçalho */
    memset(h, 0, sizeof(*h));

    /* Lê os quatro bytes de identificação */
    if (ler_exato(fd, h->head, sizeof(h->head)) < 0)
        return -1;

    /* Verifica a identificação EMAT */
    if (h->head[0] != 0x45 ||
        h->head[1] != 0x4D ||
        h->head[2] != 0x41 ||
        h->head[3] != 0x54) {

        fprintf(stderr, "Arquivo .emat invalido\n");
        return -1;
    }

    /* Lê o tipo dos dados */
    if (ler_exato(fd, &h->dtype, sizeof(h->dtype)) < 0)
        return -1;

    /* Lê o número de linhas */
    if (ler_exato(fd, &h->nlinhas, sizeof(h->nlinhas)) < 0)
        return -1;

    /* Lê o número de colunas */
    if (ler_exato(fd, &h->ncols, sizeof(h->ncols)) < 0)
        return -1;

    /* Verifica dimensões válidas */
    if (h->nlinhas == 0 || h->ncols == 0) {
        fprintf(stderr, "Dimensoes invalidas no arquivo EMAT\n");
        return -1;
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
        ta->ok = -1; /* erro */
        return NULL;
    }

    if (ler_header(fd, &ta->h) < 0) {
        close(fd);
        ta->ok = -1;
        return NULL;
    }

    ta->buf = ler_matriz(fd, ta->h.nlinhas, ta->h.ncols, ta->h.dtype);
    close(fd);
    ta->ok = 0;  /* sucesso */
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

double *multiplicar(double *A, uint32_t a_nlin, uint32_t a_ncol, double *B, uint32_t b_nlin, uint32_t b_ncol) {
    
    if (a_ncol != b_nlin) { /* verifica se a multiplicação pode ser realizada*/
        fprintf(stderr, "dimensoes incompativeis: A.ncols=%u, B.nlinhas=%u\n", a_ncol, b_nlin);
        return NULL;
    }

    size_t tam = (size_t)a_nlin * b_ncol;
    double *C = calloc(tam, sizeof(double));
    if (!C) { perror("malloc"); return NULL; }

    int n_threads = nthreads_mult;   
    if ((uint32_t)n_threads > a_nlin) n_threads = (int) a_nlin;  /* nunca mais threads que linhas */

    if (n_threads <= 1) {
        struct mult_arg ma = { A, B, C, a_ncol, b_ncol, 0, a_nlin };
        multiplicar_thread(&ma);
        return C;
    }

    pthread_t th[MAX_THREADS];           
    struct mult_arg margs[MAX_THREADS];

    uint32_t por_thread = a_nlin / n_threads;
    int criadas = 0;

    for (int t = 0; t < n_threads; t++) {
        margs[t] = (struct mult_arg){ A, B, C, a_ncol, b_ncol,
            (uint32_t)t * por_thread,
            (t == n_threads - 1) ? a_nlin : (uint32_t)(t + 1) * por_thread };
        if (pthread_create(&th[t], NULL, multiplicar_thread, &margs[t]) != 0) {
            perror("pthread_create");
            multiplicar_thread(&margs[t]);
            th[t] = 0;
        } else criadas++;
    }
    for (int t = 0; t < n_threads; t++)
        if (th[t]) pthread_join(th[t], NULL);

    return C;
}

int comparar_matrizes(const double *C, const double *C_esperada, uint32_t nlin, uint32_t ncol){
    int ok = 1;
    for (uint32_t i = 0; i < nlin; i++)
        for (uint32_t j = 0; j < ncol; j++) {
            double e = C_esperada[i * ncol + j], v = C[i * ncol + j];
            if (fabs(e - v) > EPSILON) {
                printf("Valor esperado: %g | obtido: %g ([%u][%u])\n", e, v, i, j);
                ok = 0;
            }
        }
    return ok;
}

#ifdef MATMUL_STANDALONE
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
        pthread_create(&t[i], NULL, ler_matriz_thread, &targs[i]);  /* associa* cada matriz a um thread -> thread 0 ira ler a matriz A, thread 1 a matriz B e thread 2 a matriz C*/
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
    double *matC = multiplicar(matA, A.nlinhas, A.ncols, matB, B.nlinhas, B.ncols);
    corretude = matC && comparar_matrizes(matC, matC_esperada, A.nlinhas, B.ncols);
    

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
#endif