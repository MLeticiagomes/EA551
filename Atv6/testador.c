#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "stb_ds.h"          
#include "analisador.h"
#include "classif.h"
#include "matmul_otm.h"


#define BUFFER_MAX    32     /* = CAP do dataloader.c */
#define MATMUL_MAX    64     /* = MAX_THREADS do matmul_otm.c */
#define PRODUTOR_MAX  256    /* = NTHREADS_MAX do dataloader.c */

enum { MODO_NENHUM = -1, MODO_INFER, MODO_TEST };

struct config {
    int         modo;
    const char *pesos;
    const char *entrada;
    int         matmul_threads;
    int         buffer_tamanho;
    int         produtor_threads;
    int         visualizacao;
};

static const char *CHAVES_CONHECIDAS[] = {
    "modo", "pesos", "entrada", "matmul_threads",
    "buffer_tamanho", "produtor_threads", "visualizacao", NULL
};

static void erro_cfg(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s: erro: ", config_arquivo);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static const char *nome_tipo(Tipo t)
{
    switch (t) {
        case TIPO_INTEIRO: return "inteiro";
        case TIPO_REAL:    return "real";
        case TIPO_STRING:  return "string";
        default:           return "lista";
    }
}

static const Valor *achar(const char *chave)
{
    if (!tabela) return NULL;
    int i = shgeti(tabela, (char *)chave);
    return i >= 0 ? &tabela[i].value : NULL;
}

static int pegar_texto(const char *chave, int obrigatoria, const char **out)
{
    const Valor *v = achar(chave);
    if (!v) {
        if (obrigatoria) { erro_cfg("chave obrigatoria '%s' ausente", chave); return -1; }
        return 0;
    }
    if (v->tipo != TIPO_STRING) {
        erro_cfg("'%s' deve ser uma string entre aspas (encontrado: %s)", chave, nome_tipo(v->tipo));
        return -1;
    }
    *out = v->texto;
    return 0;
}


static int pegar_int(const char *chave, int min, int max, int *out)
{
    const Valor *v = achar(chave);
    if (!v) return 0;
    if (v->tipo != TIPO_INTEIRO) {
        erro_cfg("'%s' deve ser inteiro (encontrado: %s)", chave, nome_tipo(v->tipo));
        return -1;
    }
    if (v->inteiro < min || v->inteiro > max) {
        erro_cfg("'%s' = %d fora do intervalo permitido [%d, %d]", chave, v->inteiro, min, max);
        return -1;
    }
    *out = v->inteiro;
    return 0;
}

static int validar(struct config *c)
{
    int ruim = 0;

    c->modo = MODO_NENHUM;
    c->pesos = c->entrada = NULL;
    c->matmul_threads   = 4;
    c->buffer_tamanho   = BUFFER_MAX;
    c->produtor_threads = 30;
    c->visualizacao     = 0;

    /* chaves desconhecidas */
    for (int i = 0; i < shlen(tabela); i++) {
        int conhecida = 0;
        for (int k = 0; CHAVES_CONHECIDAS[k]; k++)
            if (strcmp(tabela[i].key, CHAVES_CONHECIDAS[k]) == 0) conhecida = 1;
        if (!conhecida) { erro_cfg("chave desconhecida '%s'", tabela[i].key); ruim = 1; }
    }

    /* modo */
    const char *modo = NULL;
    if (pegar_texto("modo", 1, &modo) < 0) ruim = 1;
    else if (!strcasecmp(modo, "infer") || !strcasecmp(modo, "inferencia")) c->modo = MODO_INFER;
    else if (!strcasecmp(modo, "test")  || !strcasecmp(modo, "teste"))      c->modo = MODO_TEST;
    else { erro_cfg("modo '%s' invalido (use \"infer\" ou \"test\")", modo); ruim = 1; }

    if (pegar_texto("pesos",   1, &c->pesos)   < 0) ruim = 1;
    if (pegar_texto("entrada", 1, &c->entrada) < 0) ruim = 1;

    if (pegar_int("matmul_threads",   1, MATMUL_MAX,   &c->matmul_threads)   < 0) ruim = 1;
    if (pegar_int("buffer_tamanho",   1, BUFFER_MAX,   &c->buffer_tamanho)   < 0) ruim = 1;
    if (pegar_int("produtor_threads", 1, PRODUTOR_MAX, &c->produtor_threads) < 0) ruim = 1;
    if (pegar_int("visualizacao",     0, 1,            &c->visualizacao)     < 0) ruim = 1;

    /* checagens no sistema de arquivos */
    struct stat st;
    if (c->pesos && access(c->pesos, R_OK) != 0) {
        erro_cfg("arquivo de pesos '%s' nao pode ser lido", c->pesos);
        ruim = 1;
    }
    if (c->entrada && c->modo != MODO_NENHUM) {
        if (stat(c->entrada, &st) != 0) {
            erro_cfg("entrada '%s' nao existe", c->entrada);
            ruim = 1;
        } else if (c->modo == MODO_TEST && !S_ISDIR(st.st_mode)) {
            erro_cfg("no modo test, 'entrada' deve ser uma pasta (com subpastas 0..9)");
            ruim = 1;
        } else if (c->modo == MODO_INFER && !S_ISREG(st.st_mode)) {
            erro_cfg("no modo infer, 'entrada' deve ser um arquivo .emat");
            ruim = 1;
        }
    }
    if (c->modo == MODO_INFER && c->visualizacao && !getenv("DISPLAY")) {
        erro_cfg("visualizacao = 1, mas a variavel DISPLAY (X11) nao esta definida");
        ruim = 1;
    }

    /* avisos sobre chaves que o modo escolhido ignora */
    if (c->modo == MODO_TEST && achar("visualizacao"))
        fprintf(stderr, "%s: aviso: 'visualizacao' e ignorada no modo test\n", config_arquivo);
    if (c->modo == MODO_INFER && (achar("buffer_tamanho") || achar("produtor_threads")))
        fprintf(stderr, "%s: aviso: 'buffer_tamanho'/'produtor_threads' sao ignoradas no modo infer\n", config_arquivo);

    return ruim ? -1 : 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Uso: %s config.yaml\n", argv[0]);
        return 2;
    }

    if (config_carregar(argv[1]) != 0) {
        fprintf(stderr, "configuracao invalida (erro de sintaxe); abortando\n");
        return 2;
    }

    struct config c;
    if (validar(&c) != 0) {
        fprintf(stderr, "configuracao invalida (valores); abortando\n");
        return 2;
    }

    printf("modo=%s pesos=%s entrada=%s matmul_threads=%d buffer=%d produtores=%d viz=%d\n",
           c.modo == MODO_TEST ? "test" : "infer", c.pesos, c.entrada,
           c.matmul_threads, c.buffer_tamanho, c.produtor_threads, c.visualizacao);
    fflush(stdout);

 
    matmul_set_threads(c.matmul_threads);
    dataloader_set_buffer(c.buffer_tamanho);
    dataloader_set_threads(c.produtor_threads);
    inferencia_set_visual(c.visualizacao);

    char *av[] = { "testador", (char *)c.entrada, (char *)c.pesos, NULL };

    if (c.modo == MODO_TEST)
        return teste(3, av);

    int fd = open(c.entrada, O_RDONLY);
    if (fd < 0) { perror("open entrada"); return 1; }
    if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2"); close(fd); return 1; }
    close(fd);
    return inferencia(3, av);
}