#define _POSIX_C_SOURCE 200809L 
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

/* o pai cria a memória compartilhada e lê o arquivo emat -> o processo filho lê a matriz na memória e cria a janela de visualização */

volatile sig_atomic_t interrompido = 0;

void handler_sigint(int sig){
    interrompido = 1;
}

struct header {
    unsigned char head[4];
    uint8_t dtype;
    uint32_t nlinhas;
    uint32_t ncols;
};

int main (void){

    sem_t *pode_ler;
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

    /* cria a memoria compartilhada*/
    unsigned char *memoria = mmap(NULL, tam_total, PROT_READ | PROT_WRITE , MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    if (memoria == MAP_FAILED){
        perror("mmap");
        sem_close(pode_ler);
        sem_unlink("/ex_g_sem");
        return 1;
    }

    /* interromper o processo de for feito um ctrl-c*/
    struct sigaction sa;
    sa.sa_handler = handler_sigint; /* define a função chamada quando o sinal for recebido*/
    sigemptyset(&sa.sa_mask); /* impede que o handler seja interrompido por outro sinal em execução*/
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL); /* associa o sinal configurado com a struct que possui a configuração*/


    /* cria o processo filho */
    pid_t pid = fork();

    if(pid == 0){  /* processo filho*/
       
        signal(SIGINT, SIG_DFL);

        sem_wait(pode_ler); /*espera o semaforo libear a leitura*/
        uint32_t buf[h.ncols * h.nlinhas];
        struct fenster f = { .title = "emat", .width = h.ncols, .height = h.nlinhas, .buf = buf};
        fenster_open(&f);

        while (fenster_loop(&f) == 0){
            for(uint32_t i = 0; i < h.nlinhas; i++){
                for(uint32_t j = 0; j < h.ncols; j++){
                    unsigned char valor = memoria[i * h.ncols + j]; /* encontra o byte correspondete a um determinado valor na matriz */
                    uint32_t cor = (valor << 16) | (valor << 8) | valor; /* gera todos os bytes em escala cinza*/
                    fenster_pixel(&f, j, i) = cor;
                }
            }
        }
        fenster_close(&f);
        exit(0);
    }

    total = 0;
    while(total < tam_total && !interrompido){
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

    if (!interrompido){
        sem_post(pode_ler);
    } 

    waitpid(pid, NULL, 0);
    sem_close(pode_ler);
    sem_unlink("/ex_g_sem");
    munmap(memoria, tam_total);

    return 0;  
    
}



