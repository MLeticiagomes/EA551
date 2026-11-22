#ifndef ESTRUTURAS_H
#define ESTRUTURAS_H


typedef enum {
    TIPO_INTEIRO,
    TIPO_REAL,
    TIPO_STRING,
    TIPO_LISTA
} TipoValor;


typedef struct Valor {
    TipoValor tipo;
    int    inteiro;
    float  real;
    char  *texto;
    struct Valor *itens;  
} Valor;


typedef struct {
    char  *key;
    Valor  value;
} Entrada;

#endif