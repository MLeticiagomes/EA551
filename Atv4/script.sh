#!/bin/bash

flex ea551-mat-txt-parser.l && bison -d ea551-mat-txt-parser.y && \
gcc -o Questao4_1 Questao4_1.c ea551-mat-txt-parser.tab.c lex.yy.c || exit 1

PASTA_ENTRADA="./lab4_dados_extraclasse/mnist_txt"
PASTA_SAIDA="./mnist_emat"

mkdir -p "$PASTA_SAIDA"

find "$PASTA_ENTRADA" -type f -name "*.txt" | while read -r arquivo; do
    rel_path="${arquivo#$PASTA_ENTRADA/}"       

    dir_destino="$PASTA_SAIDA/$(dirname "$rel_path")"  
    mkdir -p "$dir_destino"

    nome_base=$(basename "$arquivo" .txt)
    caminho_saida="$dir_destino/${nome_base}.emat"

    ./Questao4_1 "$caminho_saida" < "$arquivo" 2>/dev/null
done