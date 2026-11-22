#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "stb_ds.h"      
#include "analisador.h"
#include "config.h"

extern Entrada *tabela;  /* tabela hash preenchida pelo parser */


#define MATMUL_MIN 1
#define MATMUL_MAX 64
#define BUFFER_MIN 1
#define BUFFER_MAX 32     /* CAP da struct compartilhada */
#define PROD_MIN   1
#define PROD_MAX   256

static const char *CHAVES[] = {
    "modo", "pesos", "entrada",
    "matmul_threads", "buffer", "prod_threads", "visualizacao"
};

/* busca um inteiro; se a chave não existe usa o padrão. Retorna 0 em sucesso, -1 em erro */
static int pegar_int(const char *arq, const char *chave, int padrao, int min, int max, int *saida)
{
    int i = shgeti(tabela, (char *)chave);
    if (i < 0) { *saida = padrao; return 0; }

    Valor v = tabela[i].value;
    if (v.tipo != TIPO_INTEIRO) {
        fprintf(stderr, "%s: '%s' deve ser um numero inteiro\n", arq, chave);
        return -1;
    }
    if (v.inteiro < min || v.inteiro > max) {
        fprintf(stderr, "%s: '%s' deve estar entre %d e %d (valor lido: %d)\n",
                arq, chave, min, max, v.inteiro);
        return -1;
    }
    *saida = v.inteiro;
    return 0;
}

/* busca um texto obrigatório e copia para dst. Retorna 0 em sucesso, -1 em erro */
static int pegar_texto(const char *arq, const char *chave, char *dst, size_t tam)
{
    int i = shgeti(tabela, (char *)chave);
    if (i < 0) {
        fprintf(stderr, "%s: chave obrigatoria '%s' ausente\n", arq, chave);
        return -1;
    }

    Valor v = tabela[i].value;
    if (v.tipo != TIPO_STRING) {
        fprintf(stderr, "%s: '%s' deve ser um texto entre aspas (ex.: %s: \"valor\")\n", arq, chave, chave);
        return -1;
    }
    if (strlen(v.texto) >= tam) {
        fprintf(stderr, "%s: '%s' muito longo (maximo %zu caracteres)\n", arq, chave, tam - 1);
        return -1;
    }
    strcpy(dst, v.texto);
    return 0;
}

/* confere se o caminho existe e é pasta (dir=1) ou arquivo comum (dir=0) */
static int checar_caminho(const char *arq, const char *chave, const char *caminho, int dir)
{
    struct stat st;
    if (stat(caminho, &st) != 0) {
        fprintf(stderr, "%s: '%s' aponta para '%s', que nao existe\n", arq, chave, caminho);
        return -1;
    }
    if (dir && !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "%s: '%s' deve ser uma pasta, mas '%s' nao e\n", arq, chave, caminho);
        return -1;
    }
    if (!dir && !S_ISREG(st.st_mode)) {
        fprintf(stderr, "%s: '%s' deve ser um arquivo, mas '%s' nao e\n", arq, chave, caminho);
        return -1;
    }
    return 0;
}

int config_ler(const char *arquivo, struct Config *c)
{
    
    if (config_carregar(arquivo) != 0) return -1;

    int erros = 0;

    /* chaves desconhecidas */
    for (int i = 0; i < shlen(tabela); i++) {
        int conhecida = 0;
        for (size_t k = 0; k < sizeof(CHAVES) / sizeof(CHAVES[0]); k++)
            if (strcmp(tabela[i].key, CHAVES[k]) == 0) conhecida = 1;
        if (!conhecida) {
            fprintf(stderr, "%s: chave desconhecida '%s'\n", arquivo, tabela[i].key);
            erros++;
        }
    }

    /* modo: "infer" ou "test" */
    char modo[16] = "";
    if (pegar_texto(arquivo, "modo", modo, sizeof(modo)) != 0) {
        erros++;
    } else if (strcmp(modo, "test") == 0) {
        c->modo = 1;
    } else if (strcmp(modo, "infer") == 0) {
        c->modo = 0;
    } else {
        fprintf(stderr, "%s: 'modo' deve ser \"infer\" ou \"test\" (valor lido: \"%s\")\n", arquivo, modo);
        erros++;
    }

    /* caminhos */
    if (pegar_texto(arquivo, "pesos", c->pesos, sizeof(c->pesos)) != 0) erros++;
    else if (checar_caminho(arquivo, "pesos", c->pesos, 0) != 0) erros++;

    int modo_ok = (strcmp(modo, "test") == 0 || strcmp(modo, "infer") == 0);
    if (pegar_texto(arquivo, "entrada", c->entrada, sizeof(c->entrada)) != 0) erros++;
    else if (modo_ok && checar_caminho(arquivo, "entrada", c->entrada, c->modo == 1) != 0) erros++;
    /* test: 'entrada' é uma pasta; infer: é um arquivo .emat */

    /*  parâmetros numéricos  */
    if (pegar_int(arquivo, "matmul_threads", 1,  MATMUL_MIN, MATMUL_MAX, &c->matmul_threads) != 0) erros++;
    if (pegar_int(arquivo, "buffer",         32, BUFFER_MIN, BUFFER_MAX, &c->buffer)         != 0) erros++;
    if (pegar_int(arquivo, "prod_threads",   30, PROD_MIN,   PROD_MAX,   &c->prod_threads)   != 0) erros++;
    if (pegar_int(arquivo, "visualizacao",   0,  0,          1,          &c->visualizacao)   != 0) erros++;

    if (erros > 0) {
        fprintf(stderr, "%s: %d erro(s) na configuracao\n", arquivo, erros);
        return -1;
    }
    return 0;
}