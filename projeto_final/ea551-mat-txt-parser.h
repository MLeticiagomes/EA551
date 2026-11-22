#ifndef EA551_MAT_TXT_PARSER_H
#define EA551_MAT_TXT_PARSER_H

#include <stddef.h>
#include <stdio.h>
#include "stb_ds.h"

/* Ordem crescente: na reducao o dtype so sobe (nunca desce).
   uint8 ⊂ int32 ⊂ float32 — o maior visto na matriz e o tipo final. */
typedef enum {
    EA551_DTYPE_UINT8 = 0,
    EA551_DTYPE_INT32 = 1,
    EA551_DTYPE_FLOAT32 = 2
} ea551_dtype;

/* Matriz em RAM (.emat). linhas[i][j] = linha i, coluna j. */
typedef struct {
    ea551_dtype dtype;
    double **linhas;
} emat;

extern emat *ea551_mat;
extern FILE *yyin;
int yyparse(void);

void ea551_mat_free(emat *m);
const char *ea551_dtype_str(ea551_dtype d);
size_t ea551_dtype_size(ea551_dtype d);

/* stderr: tipo, nlinhas, ncols e os valores. print no stderr para não atrapalhar saída .emat no stdout */
static inline void print_matrix(const emat *m)
{
    if (!m || !m->linhas) {
        fprintf(stderr, "print_matrix: matriz vazia\n");
        return;
    }
    ptrdiff_t nlinhas = arrlen(m->linhas);
    ptrdiff_t ncols   = nlinhas ? arrlen(m->linhas[0]) : 0;
    fprintf(stderr, "%td linhas, %td colunas, tipo %s\n",
            nlinhas, ncols, ea551_dtype_str(m->dtype));
    for (ptrdiff_t i = 0; i < nlinhas; i++) {
        for (ptrdiff_t j = 0; j < arrlen(m->linhas[i]); j++)
            fprintf(stderr, "%g ", m->linhas[i][j]);
        fprintf(stderr, "\n");
    }
}

#endif
