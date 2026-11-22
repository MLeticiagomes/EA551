#ifndef ATV2_7_H
#define ATV2_7_H

typedef enum { TIPO_INTEIRO, TIPO_REAL, TIPO_STRING, TIPO_LISTA } Tipo;

typedef struct Valor {
    Tipo  tipo;
    int   inteiro;
    float real;
    char *texto;
    struct Valor *itens;   /* array stb_ds */
} Valor;

/* entrada do hashmap stb_ds */
typedef struct { char *key; Valor value; } Entrada;

extern Entrada     *tabela;          /* chave -> valor */
extern const char  *config_arquivo;  /* nome do arquivo, usado nas mensagens */
extern int          erros;           /* contador de erros léxicos/sintáticos */
extern int          yylineno;
extern char        *yytext;

int  config_carregar(const char *arquivo);  /* 0 = ok, -1 = erro */
void config_imprimir(void);

#endif