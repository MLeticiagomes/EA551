%{
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "ea551-mat-txt-parser.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

emat *ea551_mat;
extern int yylineno;
int yylex(void);
void yyerror(const char *s);

static ea551_dtype dtype_max(ea551_dtype a, ea551_dtype b)
{
    return a > b ? a : b;
}
%}

%union {
    double dval;
    struct { double valor; ea551_dtype dtype; } valor;
    struct { double *valores; ea551_dtype dtype; } linha;
    struct { double **linhas; ea551_dtype dtype; } linhas;
}

%token VIRGULA COLCHETE_ESQ COLCHETE_DIR
%token <dval> UINT8_LIT INT32_LIT FLOAT_LIT

%type  <valor>  valor
%type  <linha>  valores linha
%type  <linhas> linhas

%%

/* [[n, n], [n, n]] — lista de listas */
matriz
    : COLCHETE_ESQ linhas COLCHETE_DIR {
        ea551_mat = malloc(sizeof(*ea551_mat));
        ea551_mat->dtype  = $2.dtype;
        ea551_mat->linhas = $2.linhas;
        print_matrix(ea551_mat);
      }
    ;

linhas
    : linha {
        $$.linhas = NULL;
        arrput($$.linhas, $1.valores);
        $$.dtype = $1.dtype;
      }
    | linhas VIRGULA linha {
        ptrdiff_t expect = arrlen($1.linhas[0]);
        ptrdiff_t got    = arrlen($3.valores);
        if (got != expect) {
            fprintf(stderr, "linha %d: esperava %td colunas, veio %td\n",
                    yylineno, expect, got);
            YYERROR;
        }
        $$ = $1;
        arrput($$.linhas, $3.valores);
        $$.dtype = dtype_max($1.dtype, $3.dtype);  /* promove o tipo da matriz */
      }
    ;

linha
    : COLCHETE_ESQ valores COLCHETE_DIR { $$ = $2; }
    ;

valores
    : valor {
        $$.valores = NULL;
        arrput($$.valores, $1.valor);
        $$.dtype = $1.dtype;
      }
    | valores VIRGULA valor {
        $$ = $1;
        arrput($$.valores, $3.valor);
        $$.dtype = dtype_max($1.dtype, $3.dtype);  /* promove o tipo da linha */
      }
    ;

/* o literal ja nasce com um tipo; a arvore so guarda o maximo visto */
valor
    : UINT8_LIT  { $$.valor = $1; $$.dtype = EA551_DTYPE_UINT8; }
    | INT32_LIT  { $$.valor = $1; $$.dtype = EA551_DTYPE_INT32; }
    | FLOAT_LIT  { $$.valor = $1; $$.dtype = EA551_DTYPE_FLOAT32; }
    ;

%%

void yyerror(const char *s)
{
    fprintf(stderr, "linha %d: %s\n", yylineno, s);
}

const char *ea551_dtype_str(ea551_dtype d)
{
    switch (d) {
        case EA551_DTYPE_UINT8:   return "uint8";
        case EA551_DTYPE_INT32:   return "int32";
        case EA551_DTYPE_FLOAT32: return "float32";
    }
    return "?";
}

size_t ea551_dtype_size(ea551_dtype d)
{
    switch (d) {
        case EA551_DTYPE_UINT8:   return sizeof(uint8_t);
        case EA551_DTYPE_INT32:   return sizeof(int32_t);
        case EA551_DTYPE_FLOAT32: return sizeof(float);
    }
    return 0;
}

void ea551_mat_free(emat *m)
{
    if (!m) return;
    for (ptrdiff_t i = 0; i < arrlen(m->linhas); i++) arrfree(m->linhas[i]);
    arrfree(m->linhas);
    free(m);
}
