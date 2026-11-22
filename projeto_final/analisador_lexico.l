%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "analisador.h"
#include "analisador_sintatico.tab.h"
%}

%option noyywrap yylineno nounput noinput


digitos     -?[0-9]+
real        -?[0-9]+\.[0-9]+
chave       [a-zA-Z_][a-zA-Z0-9_]*
espaco      [ \t\r\n]+
comentario  #.*

%%

{comentario}   {  }
{espaco}       {  }

{real}         { yylval.valor_real = atof(yytext); return REAL; }
{digitos}      { yylval.valor_inteiro = atoi(yytext); return INTEIRO; }

{chave}        { yylval.texto = strdup(yytext); return CHAVE; }

\"[^\"]*\"     {
                 yylval.texto = strdup(yytext + 1);
                 yylval.texto[strlen(yylval.texto) - 1] = '\0';
                 return STRING;
               }

":"            { return ':'; }
","            { return ','; }
"["            { return '['; }
"]"            { return ']'; }

.              { fprintf(stderr, "%s:%d: caractere invalido '%s'\n", config_arquivo, yylineno, yytext); erros++;  }

%%