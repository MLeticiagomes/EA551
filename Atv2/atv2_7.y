%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"
#include "atv2_7.h"

int  yylex(void);
void yyerror(const char *s);
extern FILE *yyin;

Entrada *tabela = NULL;

static void imprimir_valor(Valor v);
static void imprimir_tabela(void);
%}

%union {
    char  *texto;
    int    valor_inteiro;
    float  valor_real;
    Valor *valor;
}

%token <texto>         CHAVE
%token <texto>         STRING
%token <valor_inteiro> INTEIRO
%token <valor_real>    REAL

%type <valor> valor escalar lista itens item

%%

programa:
      /* vazio */
    | programa atribuicao
    ;

atribuicao:
    CHAVE ':' valor {
        shput(tabela, $1, *$3);  /* insere na tabela hash */
        free($3);                
    }
    ;

valor:
      escalar { $$ = $1; }
    | lista   { $$ = $1; }
    ;

escalar:
      INTEIRO {
        $$ = malloc(sizeof(Valor));
        $$->tipo = TIPO_INTEIRO;
        $$->inteiro = $1;
        $$->itens = NULL;
    }
    | REAL {
        $$ = malloc(sizeof(Valor));
        $$->tipo = TIPO_REAL;
        $$->real = $1;
        $$->itens = NULL;
    }
    | STRING {
        $$ = malloc(sizeof(Valor));
        $$->tipo = TIPO_STRING;
        $$->texto = $1;
        $$->itens = NULL;
    }
    ;

lista:
    '[' itens ']' { $$ = $2; }
    ;

itens:
      item {
        $$ = malloc(sizeof(Valor));
        $$->tipo = TIPO_LISTA;
        $$->itens = NULL;
        arrput($$->itens, *$1);   
        free($1);
    }
    | itens ',' item {
        arrput($1->itens, *$3);   /* acrescenta mais um elemento */
        free($3);
        $$ = $1;
    }
    ;

item:
      escalar { $$ = $1; }
    | lista   { $$ = $1; }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Erro de sintaxe: %s\n", s);
}

static void imprimir_valor(Valor v) {
    switch (v.tipo) {
        case TIPO_INTEIRO: printf("%d", v.inteiro); break;
        case TIPO_REAL:    printf("%g", v.real);    break;
        case TIPO_STRING:  printf("\"%s\"", v.texto); break;
        case TIPO_LISTA: {
            printf("[");
            for (int i = 0; i < arrlen(v.itens); i++) {
                imprimir_valor(v.itens[i]);
                if (i < arrlen(v.itens) - 1) printf(", ");
            }
            printf("]");
            break;
        }
    }
}

static void imprimir_tabela(void) {
    for (int i = 0; i < shlen(tabela); i++) {
        printf("%s: ", tabela[i].key);
        imprimir_valor(tabela[i].value);
        printf("\n");
    }
}

int main(int argc, char **argv) {
    if (argc > 1) {
        yyin = fopen(argv[1], "r");
        if (!yyin) {
            perror("Erro ao abrir arquivo");
            return 1;
        }
    }

    if (yyparse() == 0) {
        printf("--- Tabela Hash ---\n");
        imprimir_tabela();
    }

    return 0;
}