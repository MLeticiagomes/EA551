#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <ftw.h>
#include <errno.h>
#include "ea551-mat-txt-parser.h"


#define CAP         32     /* tamanho do buffer */
#define NUM_PIXELS  784    /* 28*28, fixo para o MNIST */
#define NTHREADS    10      /* número de threads*/

/* memória compartilhada */
#define SHM_NAME    "/ea551_shm"
/* semáforos */
#define SEM_ESCRITA "/ea551_sem_escrita"
#define SEM_LEITURA "/ea551_sem_leitura"
#define SEM_MUTEX   "/ea551_sem_mutex"

struct shared { /* struct compartilhada*/
    double  buf[CAP][NUM_PIXELS]; 
    int32_t label[CAP]; /* guarda classes da imagem armazenada*/
    int     in_idx, out_idx; /* indices de onde a imagem será consumida e produzida*/
    long    total_imagens; /* quantas imagens serão produzidas*/
    int     num_pixels; 
};

/* informações do arquivo*/
struct header {
    unsigned char head[4];
    uint8_t dtype;
    uint32_t nlinhas;
    uint32_t ncols;
};

struct shared *S;

sem_t *contador_escrita, *contador_leitura, *mutex; /* semaforos */


static char **arquivos = NULL;
static size_t n_arquivos = 0, cap_arquivos = 0;

#include <dirent.h>

static void coletar_pasta(const char *pasta) /* percorrer o diretorio até chegar nos arquivos emat*/
{
    DIR *d = opendir(pasta);
    if (!d) { perror("opendir"); return; }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len > 5 && strcmp(entry->d_name + len - 5, ".emat") == 0) { /* verificar se o arquivo termina em .emat*/
            char caminho[1024];
            int ret = snprintf(caminho, sizeof(caminho), "%s/%s", pasta, entry->d_name);
            
            if (ret < 0 || (size_t)ret >= sizeof(caminho)) {
                fprintf(stderr, "aviso: caminho muito longo ou erro ao formatar, pulando arquivo\n");
                continue;
            }

            if (n_arquivos == cap_arquivos) {
                cap_arquivos = cap_arquivos ? cap_arquivos * 2 : 1024;
                char **novo = realloc(arquivos, cap_arquivos * sizeof(char *));
                if (!novo) { perror("realloc"); exit(1); }
                arquivos = novo;
            }
            arquivos[n_arquivos] = strdup(caminho);
            if (!arquivos[n_arquivos]) { perror("strdup"); exit(1); }
            n_arquivos++;
        }
    }
    closedir(d);
}
static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

/* extrai a classe (0-9) do nome da pasta-pai do arquivo */
static int label_from_path(const char *path)
{
    const char *fim_nome = strrchr(path, '/');
    if (!fim_nome) return -1;

    const char *inicio_dir = fim_nome;
    while (inicio_dir > path && *(inicio_dir - 1) != '/') inicio_dir--;

    char buf[16];
    size_t n = (size_t)(fim_nome - inicio_dir);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, inicio_dir, n);
    buf[n] = '\0';

    return atoi(buf);
}

/* header */

static int ler_header(int fd, struct header *h)
{
    size_t total = 0, len;
    ssize_t n;

    len = sizeof(h->head);
       while (total < len) {
            n = read(fd, (char*)&h->head + total, len - total);
            if (n < 0) {
                perror("read");
                break;
            }
            if (n == 0) {
                break;
            }
            total += n;
        }
        if (h->head[0] != 0x45 || h->head[1] != 0x4D || h->head[2] != 0x41 || h->head[3] != 0x54){ /* verificar se o arquivo é valido*/
            printf("arquivo .emat invalido\n");
            return 1;
        }

        /* leitura do type, numero de linhas e numero de colunas*/
        total = 0;
        len = sizeof(h->dtype);
        while (total < len) {
            n = read(fd, (char*)&h->dtype + total, len - total);
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
        len = sizeof(h->nlinhas);
        while (total < len) {
            n = read(fd, (char*)&h->nlinhas + total, len - total);
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
        len = sizeof(h->ncols);
        while (total < len) {
            n = read(fd, (char*)&h->ncols + total, len - total);
            if (n < 0) {
                perror("read");
                break;
            }
            if (n == 0) {
                break;
            }
            total += n;
        }
        return 0;
}

static size_t tam_elemento(uint8_t dtype)
{
    switch (dtype) {
        case EA551_DTYPE_UINT8:   return sizeof(uint8_t);
        case EA551_DTYPE_INT32:   return sizeof(int32_t);
        case EA551_DTYPE_FLOAT32: return sizeof(float);
        default: return 0;
    }
}



static void produzir_imagem(const char *caminho)
{
    int fd = open(caminho, O_RDONLY);
    if (fd < 0) { perror("open"); return; }
    /*  verifica se o header, numero de elementos e tipo estão corretos*/
    struct header h;
    if (ler_header(fd, &h) != 0) {
        fprintf(stderr, "aviso: header invalido em %s, pulando\n", caminho);
        close(fd);
        return;
    }

    long n_elem = (long)h.nlinhas * h.ncols;
    if (n_elem != NUM_PIXELS) {
        fprintf(stderr, "aviso: %s tem %ld pixels (esperado %d), pulando\n",
                caminho, n_elem, NUM_PIXELS);
        close(fd);
        return;
    }

    size_t esz = tam_elemento(h.dtype);
    if (esz == 0) {
        fprintf(stderr, "aviso: dtype desconhecido em %s, pulando\n", caminho);
        close(fd);
        return;
    }

    size_t bytes_totais = (size_t)n_elem * esz;
    unsigned char *raw = malloc(bytes_totais);
    if (!raw) { perror("malloc"); close(fd); return; }

    /* le todos os pixels*/
    size_t total = 0;
    while (total < bytes_totais) {
        ssize_t n = read(fd, raw + total, bytes_totais - total);
        if (n < 0) { perror("read"); free(raw); close(fd); return; }
        if (n == 0) break;
        total += (size_t)n;
    }
    close(fd);

    double local[NUM_PIXELS];
    switch (h.dtype) {  /* identifica o tipo de cada pixel*/
        case EA551_DTYPE_UINT8:
            for (long i = 0; i < n_elem; i++) local[i] = (double) raw[i];
            break;
        case EA551_DTYPE_INT32: {
            int32_t *p = (int32_t*) raw;
            for (long i = 0; i < n_elem; i++) local[i] = (double) p[i];
            break;
        }
        case EA551_DTYPE_FLOAT32: {
            float *p = (float*) raw;
            for (long i = 0; i < n_elem; i++) local[i] = (double) p[i];
            break;
        }
    }
    free(raw);

    int label = label_from_path(caminho);

    sem_wait(contador_escrita);
    sem_wait(mutex);
    /*salva no buffer a imagem produzida*/
    memcpy(S->buf[S->in_idx], local, sizeof(local));
    S->label[S->in_idx] = label;
    S->in_idx = (S->in_idx + 1) % CAP;

    sem_post(mutex);
    sem_post(contador_leitura);
}

struct thread_arg {
    size_t inicio, fim; /* intervalo dentro de arquivos */
};

void *f1(void *arg) /* cada thread realiza a leitura  de uma parte do arquivo e insere na memoria compartilhada com o consumidor*/
{
    struct thread_arg *ta = (struct thread_arg *) arg;
    for (size_t i = ta->inicio; i < ta->fim; i++) {
        produzir_imagem(arquivos[i]);
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Uso: %s pasta_train (ex: mnist_emat/train)\n", argv[0]);
        return 1;
    }

    for (int classe = 0; classe <= 9; classe++) {
        char pasta[1024];
        snprintf(pasta, sizeof(pasta), "%s/%d", argv[1], classe);
        coletar_pasta(pasta);
    }

    if (n_arquivos == 0) {
        fprintf(stderr, "nenhum arquivo .emat encontrado em %s\n", argv[1]);
        return 1;
    }
    qsort(arquivos, n_arquivos, sizeof(char *), cmp_str);

    /* memoria compartilhada */
    shm_unlink(SHM_NAME);
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (shm_fd < 0) { perror("shm_open"); return 1; }

    if (ftruncate(shm_fd, sizeof(struct shared)) < 0) {
        perror("ftruncate");
        close(shm_fd);
        return 1;
    }

    S = mmap(NULL, sizeof(struct shared), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (S == MAP_FAILED) { perror("mmap"); close(shm_fd); return 1; }

    S->in_idx = S->out_idx = 0;
    S->total_imagens = (long) n_arquivos;
    S->num_pixels = NUM_PIXELS;

    /* semaforos */
    sem_unlink(SEM_ESCRITA);
    sem_unlink(SEM_LEITURA);
    sem_unlink(SEM_MUTEX);

    contador_escrita = sem_open(SEM_ESCRITA, O_CREAT, 0600, CAP);
    contador_leitura = sem_open(SEM_LEITURA, O_CREAT, 0600, 0);
    mutex            = sem_open(SEM_MUTEX,   O_CREAT, 0600, 1);

    if (contador_escrita == SEM_FAILED || contador_leitura == SEM_FAILED || mutex == SEM_FAILED) {
        perror("sem_open");
        munmap(S, sizeof(*S));
        close(shm_fd);
        return 1;
    }

    /* consumidor Python*/
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) {
        execlp("python3", "python3", "train.py", NULL);
        perror("execlp");
        _exit(1);
    }

    /* threads produtoras*/
    pthread_t th[NTHREADS];
    struct thread_arg targs[NTHREADS];

    size_t por_thread = n_arquivos / NTHREADS;
    for (int i = 0; i < NTHREADS; i++) { /* divide o arquivo em partes que são tratadas por diferentes threads*/
        targs[i].inicio = (size_t)i * por_thread;
        targs[i].fim = (i == NTHREADS - 1) ? n_arquivos : targs[i].inicio + por_thread;
        pthread_create(&th[i], NULL, f1, &targs[i]);
    }
    for (int i = 0; i < NTHREADS; i++) {
        pthread_join(th[i], NULL);
    }

    waitpid(pid, NULL, 0);
    /* encerramento dos cemaforos e liberação de memoria*/
    sem_close(contador_escrita);
    sem_close(contador_leitura);
    sem_close(mutex);
    sem_unlink(SEM_ESCRITA);
    sem_unlink(SEM_LEITURA);
    sem_unlink(SEM_MUTEX);

    munmap(S, sizeof(*S));
    close(shm_fd);
    shm_unlink(SHM_NAME);

    for (size_t i = 0; i < n_arquivos; i++) free(arquivos[i]);
    free(arquivos);

    return 0;
}