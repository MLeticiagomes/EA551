#!/bin/bash

flex ea551-mat-txt-parser.l && bison -d ea551-mat-txt-parser.y && \
gcc -o Questao4_1 Questao4_1.c ea551-mat-txt-parser.tab.c lex.yy.c || exit 1 # compila os arquivos usado
 
PASTA_ENTRADA="./lab4_dados_extraclasse/mnist_txt"
PASTA_SAIDA="./mnist_emat"

mkdir -p "$PASTA_SAIDA" # cria a pasta mnist_emat caso ela não exista

find "$PASTA_ENTRADA" -type f -name "*.txt" | while read -r arquivo; do # percorre as subpastas presentes dentro de lab4_dados_extraclasse/mnist_txt apra encontrar os arquivos txt 
    rel_path="${arquivo#$PASTA_ENTRADA/}"       

    dir_destino="$PASTA_SAIDA/$(dirname "$rel_path")"   # cria um caminho até os arquivos 
    mkdir -p "$dir_destino" # espelha esse caminho (as subpastas dentro de mnist_txt) na saída

    nome_base=$(basename "$arquivo" .txt) # extrai o nome de cada arquivo
    caminho_saida="$dir_destino/${nome_base}.emat" # utiliza o nome extraido como o nome dos arquivos de saída

    ./Questao4_1 "$caminho_saida" < "$arquivo" 2>/dev/null # executa o programa definindo os arquivos de entrada e saída
done