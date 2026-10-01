
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h> 

#include "ea551-mat-txt-parser.h"

struct header {
    unsigned char head[4];
    uint8_t dtype;
    uint32_t nlinhas;
    uint32_t ncols;
};

static const char *dtype_name(uint8_t dtype) // associa os tipos de caracter aos seus nomes
{
    switch (dtype) {
        case EA551_DTYPE_UINT8:   return "uint8";
        case EA551_DTYPE_INT32:   return "int32";
        case EA551_DTYPE_FLOAT32: return "float32";
        default:                  return "desconhecido";
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2){
        fprintf(stderr, "Uso: %s arquivo_saida.emat < arquivo_entrada.txt\n", argv[0]);
        return 1;
    }

    int c = getchar();
    if (c == EOF) return 1;
    ungetc(c, stdin);

    if (c == '[') {          /* stdin = bytes do .txt */
        
        yyin = stdin;
        // Chama o parser para interpretar a stream de caracteres de entrada como uma matriz na memória RAM
        // Note que ea551_mat é definido em ea551-mat-txt-parser.h
        if (yyparse() != 0 || !ea551_mat) return 1;

        // Chegamos aqui se o parser rodou sem erros e ea551_mat é um ponteiro válido para a matriz na memória RAM
        // print_matrix(ea551_mat); eu comentei essa linha pois ao executar o arquivo .h ele ja chama o print_matriz dentro do proprio codigo, por essa razão as especificações da matriz e a matriz estavam sendo impressas duas vezes no terminal

        ptrdiff_t nlinhas = arrlen(ea551_mat->linhas);
        ptrdiff_t ncols   = nlinhas ? arrlen(ea551_mat->linhas[0]) : 0;


        /* TODO: gravar ea551_mat em stdout como .emat
           (header com dtype / nlinhas / ncols + sequência de bytes no tipo de ea551_mat->dtype) */
        struct header h; // atribui os valores do head, numero de linhas, numero de colunas e tipo a struct
        h.head[0] = 0x45; /* 'E' */
        h.head[1] = 0x4D; /* 'M' */
        h.head[2] = 0x41; /* 'A' */
        h.head[3] = 0x54; /* 'T' */
        h.dtype   = (uint8_t) ea551_mat->dtype;
        h.nlinhas = (uint32_t) nlinhas;
        h.ncols   = (uint32_t) ncols;

        int fd_out = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if(fd_out < 0){
            perror("open");
            return 1;
        }

        /* escreve o header*/
        size_t len;
        ssize_t n;
        size_t total = 0;

        len = sizeof(h.head);
        while (total < len){
            n = write(fd_out, (char*)h.head + total, len - total);
            if (n< 0) { 
                perror("write"); 
                break;}
            total += n;
        }
        
        total = 0;
        len = sizeof(h.dtype);
        while (total < len){
            n = write(fd_out, (char*)&h.dtype + total, len - total);
            if (n< 0) { 
                perror("write"); 
                break;}
            total += n;
        }
                
        total = 0;
        len = sizeof(h.nlinhas);
        while (total < len){
            n = write(fd_out, (char*)&h.nlinhas + total, len - total);
            if (n< 0) { 
                perror("write"); 
                break;}
            total += n;
        }
        
        total = 0;
        len = sizeof(h.ncols);
        while (total < len){
            n = write(fd_out, (char*)&h.ncols + total, len - total);
            if (n< 0) { 
                perror("write"); 
                break;}
            total += n;
        }
       
        /*conteudo do arquivo*/
        for(ptrdiff_t i = 0; i <nlinhas; i++){ // escreve no arquivo em binario o valor de cada elemento da matriz de acordo com o seu tipo
            for(ptrdiff_t j = 0; j < ncols; j++){
                double valor = ea551_mat->linhas[i][j];

                switch (ea551_mat->dtype){
                    case EA551_DTYPE_UINT8:{
                        uint8_t v = (uint8_t) valor;
                        size_t total_char = 0;
                        len = sizeof(v);
                        while (total_char < len){
                            n = write(fd_out, (char*)&v + total_char, len - total_char);
                            if (n< 0) { perror("write"); break;}
                            total_char += n;
                        }
                        break;
                    }
                                     
                    case EA551_DTYPE_INT32:{
                        uint32_t v = (uint32_t) valor;
                        size_t total_char = 0;
                        len = sizeof(v);
                        while (total_char < len){
                            n = write(fd_out, (char*)&v + total_char, len - total_char);
                            if (n< 0) { perror("write"); break;}
                            total_char += n;
                        }
                        break;
                    }

                    case EA551_DTYPE_FLOAT32:{
                        float v = (float) valor;
                        size_t total_char = 0;
                        len = sizeof(v);
                        while (total_char < len){
                            n = write(fd_out, (char*)&v + total_char, len - total_char);
                            if (n< 0) { perror("write"); break;}
                            total_char += n;
                        }
                        break;
                    }
                   
                }
            }
        }

        close(fd_out);
        ea551_mat_free(ea551_mat);
        return 0;
    }

    else{
        /* verificar se o arquivo é um emat*/ 
        struct header h;
        size_t total = 0;
        size_t len = sizeof(h.head);
        ssize_t n;
        while (total < len) {
            n = read(STDIN_FILENO, (char*)&h.head + total, len - total);
            if (n < 0) {
                perror("read");
                break;
            }
            if (n == 0) {
                break;
            }
            total += n;
        }
        if (h.head[0] != 0x45 || h.head[1] != 0x4D || h.head[2] != 0x41 || h.head[3] != 0x54){ /* verificar se o arquivo é valido*/
            printf("arquivo .emat invalido\n");
            return 1;
        }

        /* leitura do type, numero de linhas e numero de colunas*/
        total = 0;
        len = sizeof(h.dtype);
        while (total < len) {
            n = read(STDIN_FILENO, (char*)&h.dtype + total, len - total);
            if (n < 0) {
                perror("read");
                break;
            }
            if (n == 0) {
                break;
            }
            total += n;
        }

        total = 0;
        len = sizeof(h.nlinhas);
        while (total < len) {
            n = read(STDIN_FILENO, (char*)&h.nlinhas + total, len - total);
            if (n < 0) {
                perror("read");
                break;
            }
            if (n == 0) {
                break;
            }
            total += n;
        }

        total = 0;
        len = sizeof(h.ncols);
        while (total < len) {
            n = read(STDIN_FILENO, (char*)&h.ncols + total, len - total);
            if (n < 0) {
                perror("read");
                break;
            }
            if (n == 0) {
                break;
            }
            total += n;
        }
        
        printf("Header: %02X, %02X, %02X, %02X, %u linhas, %u colunas, tipo %s\n", h.head[0], h.head[1],h.head[2],h.head[3], h.nlinhas, h.ncols, dtype_name(h.dtype)); // imprime as especificações
        for (uint32_t i = 0; i <h.nlinhas; i++){
            for(uint32_t j = 0; j < h.ncols; j++){ // imprime os valores do arquivo emat de acordo com o seu tipo
                double valor;
                switch (h.dtype){
                    case EA551_DTYPE_UINT8: {
                        size_t total_char = 0;
                        uint8_t v;
                        len = sizeof(v);
                        while (total_char < len) {
                            n = read(STDIN_FILENO, (char*)&v + total_char, len - total_char);
                            if (n < 0) {
                                perror("read");
                                break;
                            }
                            if (n == 0) {
                                break;
                            }
                            total_char += n;
                        }
                        valor = (double) v;
                        break;
                    }
                                     
                    case EA551_DTYPE_INT32: {
                        size_t total_char = 0;
                        int32_t v;
                        len = sizeof(v);
                        while (total_char < len) {
                            n = read(STDIN_FILENO, (char*)&v + total_char, len - total_char);
                            if (n < 0) {
                                perror("read");
                                break;
                            }
                            if (n == 0) {
                                break;
                            }
                            total_char += n;
                        }
                        valor = (double) v;
                        break;
                    }

                    case EA551_DTYPE_FLOAT32: {
                        size_t total_char = 0;
                        float v;
                        len = sizeof(v);
                        while (total_char < len) {
                            n = read(STDIN_FILENO, (char*)&v + total_char, len - total_char);
                            if (n < 0) {
                                perror("read");
                                break;
                            }
                            if (n == 0) {
                                break;
                            }
                            total_char += n;
                        }
                        valor = (double) v;
                        break;
                    }
                }

                printf("%g ", valor);
            }
            
            printf("\n");
        }
        return 0;
    }
    
    return 1;
}

