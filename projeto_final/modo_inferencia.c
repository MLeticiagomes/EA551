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
#include "chave.h"

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
    
    double *scores = multiplicar(W, 10, 16384, x, 16384, 1);
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


int inferencia(int argc, char *argv[])
{
    sem_t *pode_ler = SEM_FAILED;
    unsigned char *memoria = MAP_FAILED;
    double *W = NULL;
    double *x = NULL;

    pid_t pid = -1;
    int resultado = 1;
    int leitura_ok = 0;

    struct header h;

    const size_t TAM_IMAGEM = 128 * 128;

    /* 1. Inicializa o semaforo */
    sem_unlink("/ex_g_sem");

    pode_ler = sem_open("/ex_g_sem", O_CREAT, 0600, 0);

    if (pode_ler == SEM_FAILED) {
        perror("sem_open");
        goto limpeza;
    }

    /* 2. Le o cabecalho EMAT */
    if (ler_header(STDIN_FILENO, &h) < 0) {
        fprintf(stderr, "Falha ao ler cabecalho da imagem\n");
        goto limpeza;
    }

    /* 3. Verifica o tipo da imagem */
    if (h.dtype != EA551_DTYPE_UINT8) {
        fprintf(stderr,
                "Tipo de imagem invalido: esperado UINT8\n");
        goto limpeza;
    }

    /* 4. Verifica as dimensoes da imagem */
    if (h.nlinhas != 128 || h.ncols != 128) {
        fprintf(stderr,
                "Dimensoes invalidas: esperado 128x128, recebido %ux%u\n",
                h.nlinhas, h.ncols);
        goto limpeza;
    }

    /* 5. Carrega os pesos do classificador */
    uint32_t w_lin, w_col;

    W = inicializa_pesos(
        caminho_pesos(argc, argv),
        PESOS_EMAT,
        &w_lin,
        &w_col
    );

    if (!W) {
        fprintf(stderr, "Falha ao carregar pesos\n");
        goto limpeza;
    }

    if (w_lin != 10 || w_col != TAM_IMAGEM) {
        fprintf(stderr,
                "Pesos incompativeis: esperado 10x16384\n");
        goto limpeza;
    }

    /* 6. Cria a memoria compartilhada */
    memoria = mmap(
        NULL,
        TAM_IMAGEM,
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS,
        -1,
        0
    );

    if (memoria == MAP_FAILED) {
        perror("mmap");
        goto limpeza;
    }

    /* 7. Configura o tratamento de SIGINT */
    interrompido = 0;
    signal(SIGINT, handler_sigint);

    /* 8. Cria o processo filho para visualizacao */
    if (g_visualizar) {

        pid = fork();

        if (pid < 0) {
            perror("fork");
            goto limpeza;
        }

        if (pid == 0) {

            /* PROCESSO FILHO */

            signal(SIGINT, SIG_DFL);

            /* Aguarda o pai terminar de carregar a imagem */
            while (sem_wait(pode_ler) < 0) {

                if (errno == EINTR)
                    continue;

                perror("sem_wait");
                exit(1);
            }

            const uint32_t ESCALA = 4;

            uint32_t largura = h.ncols * ESCALA;
            uint32_t altura  = h.nlinhas * ESCALA;

            uint32_t *buf = malloc(
                (size_t)largura * altura * sizeof(uint32_t)
            );

            if (!buf) {
                perror("malloc");
                exit(1);
            }

            struct fenster f = {
                .title = "emat",
                .width = largura,
                .height = altura,
                .buf = buf
            };

            fenster_open(&f);

            while (fenster_loop(&f) == 0) {

                for (uint32_t i = 0; i < h.nlinhas; i++) {

                    for (uint32_t j = 0; j < h.ncols; j++) {

                        unsigned char valor =
                            memoria[i * h.ncols + j];

                        uint32_t cor =
                            (valor << 16) |
                            (valor << 8) |
                            valor;

                        /* Amplia cada pixel da imagem */
                        for (uint32_t dy = 0; dy < ESCALA; dy++) {

                            for (uint32_t dx = 0; dx < ESCALA; dx++) {

                                fenster_pixel(
                                    &f,
                                    j * ESCALA + dx,
                                    i * ESCALA + dy
                                ) = cor;
                            }
                        }
                    }
                }
            }

            fenster_close(&f);
            free(buf);

            sem_close(pode_ler);
            exit(0);
        }
    }

    /* 9. PROCESSO PAI: le os pixels da imagem */

    size_t total = 0;

    while (total < TAM_IMAGEM && !interrompido) {

        ssize_t n = read(
            STDIN_FILENO,
            memoria + total,
            TAM_IMAGEM - total
        );

        if (n < 0) {

            if (errno == EINTR) {
                continue;
            }

            perror("read");
            break;
        }

        if (n == 0) {
            fprintf(stderr,
                    "Arquivo EMAT incompleto: %zu/%zu bytes lidos\n",
                    total, TAM_IMAGEM);
            break;
        }

        total += (size_t)n;
    }

    if (total == TAM_IMAGEM && !interrompido) {
        leitura_ok = 1;
    }

    /* 10. Libera o processo filho para visualizar a imagem */
    if (pid > 0 && leitura_ok) {

        if (sem_post(pode_ler) < 0) {
            perror("sem_post");
            leitura_ok = 0;
        }
    }

    /* 11. Realiza a inferencia */
    if (leitura_ok) {

        x = malloc(TAM_IMAGEM * sizeof(double));

        if (!x) {
            perror("malloc");
            leitura_ok = 0;
        }
        else {

            /* Converte os pixels UINT8 para double */
            for (size_t k = 0; k < TAM_IMAGEM; k++) {
                x[k] = (double)memoria[k];
            }

            double scores[10];

            int melhor = classificar(W, x, scores);

            if (melhor >= 0) {

                printf("\nResultados da classificacao:\n");

                for (int i = 0; i < 10; i++) {
                    printf("score[%d] = %f\n", i, scores[i]);
                }

                printf("Predicao: %d\n", melhor);

                resultado = 0;
            }
            else {
                fprintf(stderr, "Falha na classificacao\n");
            }
        }
    }

    /* 12. Se houve interrupcao ou erro, encerra o filho */
    if (pid > 0 && (!leitura_ok || interrompido)) {
        kill(pid, SIGTERM);
    }

    /* Aguarda o encerramento do processo filho */
    if (pid > 0) {

        while (waitpid(pid, NULL, 0) < 0) {

            if (errno == EINTR)
                continue;

            perror("waitpid");
            resultado = 1;
            break;
        }
    }

limpeza:

    /* 13. Libera os recursos */
    free(x);
    free(W);

    if (memoria != MAP_FAILED) {
        munmap(memoria, TAM_IMAGEM);
    }

    if (pode_ler != SEM_FAILED) {
        sem_close(pode_ler);
        sem_unlink("/ex_g_sem");
    }

    return resultado;
}
