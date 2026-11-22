#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h> 
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include "fenster.h"
#include "matmul_otm.h"
#include "classif.h" 


#define PESOS_PADRAO "W.npy"

/* o pai cria a memória compartilhada e lê o arquivo emat -> o processo filho lê a matriz na memória e cria a janela de visualização */

volatile sig_atomic_t interrompido = 0;

const char *caminho_pesos(int argc, char *argv[])
{
    if (argc > 2) return argv[2];             
    const char *env = getenv("CLASSIF_PESOS"); /* variável de ambiente */
    return env ? env : PESOS_PADRAO;           /* padrão */
}

void handler_sigint(int sig){ /* indicar e houve uma interrupção*/
    interrompido = 1;
}


double *carrega_pesos(const char *caminho, uint32_t *nl, uint32_t *nc){
    int fd = open(caminho, O_RDONLY);
    if (fd < 0) { perror("open"); return NULL; }

    struct header hw;
    if (ler_header(fd, &hw) < 0) {
        fprintf(stderr, "arquivo de pesos invalido\n");
        close(fd);
        return NULL;
    }

    *nl = hw.nlinhas;
    *nc = hw.ncols;
    double *W = ler_matriz(fd, hw.nlinhas, hw.ncols, hw.dtype);
    close(fd);
    return W;
}

double *inicializa_pesos(const char *npy, const char *emat, uint32_t *nl, uint32_t *nc){
    if (npy_para_emat(npy, emat) != 0) {
        fprintf(stderr, "falha ao converter %s para %s\n", npy, emat);
        return NULL;
    }
    return carrega_pesos(emat, nl, nc);
}


int classificar(double *W, double *x, double *scores_out){
    
    double *scores = multiplicar(W, 10, 784, x, 784, 1);
    if (!scores) return -1;

    int melhor = 0;
    for (int i = 1; i < 10; i++)
        if (scores[i] > scores[melhor]) melhor = i;

    if (scores_out) memcpy(scores_out, scores, 10 * sizeof(double));
    free(scores);
    return melhor;
}

static int g_visualizar = 1;

void inferencia_set_visual(int v) { g_visualizar = v ? 1 : 0; }

int inferencia (int argc, char *argv[]){  

    sem_t *pode_ler; /* semaforo*/
    sem_unlink("/ex_g_sem");
    pode_ler = sem_open("/ex_g_sem", O_CREAT, 0600, 0);

    if (pode_ler == SEM_FAILED){
        perror("sem_open");
        return 1;
    }

    /* inicia a leitura do arquivo emat e extrai o header, tipo, numero de linhas e o numero de colunas*/
    struct header h;
    size_t total = 0;
    size_t len = sizeof(h.head);
    ssize_t n;

    while (total < len) {
        n = read(STDIN_FILENO, (char*)&h.head + total, len - total);
        if (n < 0) {
            perror("read");
            break;
        }
        if (n == 0) {
            break;
        }
        total += n;
    }

    if (h.head[0] != 0x45 || h.head[1] != 0x4D || h.head[2] != 0x41 || h.head[3] != 0x54){ /* verificar se o arquivo é valido*/
        sem_close(pode_ler);
        sem_unlink("/ex_g_sem");
        return 1;
    }

    /* leitura do type, numero de linhas e numero de colunas*/
    total = 0;
    len = sizeof(h.dtype);
    while (total < len) {
        n = read(STDIN_FILENO, (char*)&h.dtype + total, len - total);
        if (n < 0) {
            perror("read");
            break;
        }
        if (n == 0) {
            break;
        }
        total += n;
    }

    total = 0;
    len = sizeof(h.nlinhas);
    while (total < len) {
        n = read(STDIN_FILENO, (char*)&h.nlinhas + total, len - total);
        if (n < 0) {
            perror("read");
            break;
        }
        if (n == 0) {
            break;
        }
        total += n;
    }

    total = 0;
    len = sizeof(h.ncols);
    while (total < len) {
        n = read(STDIN_FILENO, (char*)&h.ncols + total, len - total);
        if (n < 0) {
           perror("read");
            break;
        }
        if (n == 0) {
            break;
        }
        total += n;
    }

    size_t tam_total = (size_t)h.nlinhas * h.ncols;

    /* carrega a matriz de pesos W (10 x 784) */
    uint32_t w_lin, w_col;
    double *W = inicializa_pesos(caminho_pesos(argc, argv), PESOS_EMAT, &w_lin, &w_col); 
    if (!W || w_lin != 10 || w_col != tam_total) {
        fprintf(stderr, "pesos incompativeis com a imagem\n");
        sem_close(pode_ler);
        sem_unlink("/ex_g_sem");
        return 1;
    }

    /* cria a memoria compartilhada*/
    unsigned char *memoria = mmap(NULL, tam_total, PROT_READ | PROT_WRITE , MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (memoria == MAP_FAILED){
        perror("mmap");
        sem_close(pode_ler);
        sem_unlink("/ex_g_sem");
        return 1;
    }

    signal(SIGINT, handler_sigint); /* caso haja uma interrupção é ativada a função handler_sigint*/


    /* cria o processo filho */
    pid_t pid = -1;
    if (g_visualizar) pid = fork();

    if(pid == 0){  /* processo filho*/
       
        signal(SIGINT, SIG_DFL); /* interrupçao*/

        sem_wait(pode_ler); /*espera o semaforo liberar a leitura*/

        const uint32_t ESCALA = 24; /* aumentar a imagem gerada*/
        uint32_t largura = h.ncols * ESCALA;
        uint32_t altura  = h.nlinhas * ESCALA;

        uint32_t *buf = malloc((size_t)largura * altura * sizeof(uint32_t)); /* aloca na memoria o que será a tela */
        if (!buf) {
            perror("malloc");
            exit(1);
        }

        struct fenster f = { .title = "emat", .width = largura, .height = altura, .buf = buf};
        fenster_open(&f);

        while (fenster_loop(&f) == 0){
            for(uint32_t i = 0; i < h.nlinhas; i++){
                for(uint32_t j = 0; j < h.ncols; j++){
                    unsigned char valor = memoria[i * h.ncols + j]; /* encontra o byte correspondente a um determinado valor na matriz */
                    uint32_t cor = (valor << 16) | (valor << 8) | valor; /* gera todos os bytes em escala cinza*/

                    /* desenha um bloco ESCALA x ESCALA para ampliar cada pixel original */
                    for (uint32_t dy = 0; dy < ESCALA; dy++){
                        for (uint32_t dx = 0; dx < ESCALA; dx++){
                            fenster_pixel(&f, j * ESCALA + dx, i * ESCALA + dy) = cor;
                        }
                    }
                }
            }
        }
        fenster_close(&f);
        free(buf);
        exit(0);
    }

    total = 0;
    while(total < tam_total && !interrompido){ /* lê todos os valores do arquivo de entrada até o fim, ou até q haja uma interrupção*/
        n = read(STDIN_FILENO, memoria + total, tam_total - total);
        if( n < 0){
            if(errno == EINTR){
                continue;
            }
            perror("read");
            break;
        }

        if(n == 0){
            break;
        }

        total += n;
    }

    if (!interrompido){ /* se não houver uma interrupção o processo pai sinaliza  para que o processo filho possa ler a memoria compartilhada*/
        sem_post(pode_ler);

        /* inferência: scores = W (10 x 784) * imagem linearizada (784 x 1) */


        double *x = malloc(tam_total * sizeof(double));
        if (!x) { perror("malloc"); }
        else {
            for (size_t k = 0; k < tam_total; k++) x[k] = memoria[k];

            double scores[10];                        
            int melhor = classificar(W, x, scores);    
            if (melhor >= 0) {                        
                for (int i = 0; i < 10; i++)
                    printf("score[%d] = %f\n", i, scores[i]);
                printf("Predicao: %d\n", melhor);
            }
            free(x);
        }
    
        fflush(stdout);
    } else {
        kill(pid, SIGTERM); 
    }

    if (pid > 0) waitpid(pid, NULL, 0);
    free(W);
    sem_close(pode_ler);
    sem_unlink("/ex_g_sem");
    munmap(memoria, tam_total);

    return 0;  
    
}