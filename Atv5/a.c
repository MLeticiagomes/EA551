#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define CAP 16
#define NITEM 40
#define NW 3

struct shared {
    int buf[CAP];
    int in_idx, out_idx;
    sem_t sa, sb, sc;
};

struct shared *S;

void *f1(void *arg)
{
    long id = (long)arg;
    int base = (int)(id * (NITEM / NW));
    int qtd = (id == NW - 1) ? (NITEM - base) : (NITEM / NW);
    for (int k = 0; k < qtd; k++) {
        int valor = base + k;
        sem_wait(&S->sa);
        sem_wait(&S->sc);
        S->buf[S->in_idx] = valor;
        S->in_idx = (S->in_idx + 1) % CAP;
        sem_post(&S->sc);
        sem_post(&S->sb);
    }
    return NULL;
}

int main(void)
{
    S = mmap(NULL, sizeof(*S), PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (S == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    S->in_idx = S->out_idx = 0;
    sem_init(&S->sa, 1, CAP);
    sem_init(&S->sb, 1, 0);
    sem_init(&S->sc, 1, 1);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        for (int i = 0; i < NITEM; i++) {
            sem_wait(&S->sb);
            sem_wait(&S->sc);
            int v = S->buf[S->out_idx];
            S->out_idx = (S->out_idx + 1) % CAP;
            sem_post(&S->sc);
            sem_post(&S->sa);
            printf("%d\n", v);
            fflush(stdout);
        }
        _exit(0);
    }

    pthread_t th[NW];
    for (long i = 0; i < NW; i++)
        pthread_create(&th[i], NULL, f1, (void *)i);
    for (int i = 0; i < NW; i++)
        pthread_join(th[i], NULL);

    wait(NULL);
    sem_destroy(&S->sa);
    sem_destroy(&S->sb);
    sem_destroy(&S->sc);
    munmap(S, sizeof(*S));
    return 0;
}