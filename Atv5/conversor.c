#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h> 

#include "ea551-mat-txt-parser.h"

enum { SRC_U8, SRC_I32, SRC_F32, SRC_F64 };

struct NpyHeader{ /* struct com as informações do cabeçalho do arquivo npy*/
    char     header_npy[16];     
    int      fortran_order;  
    int      ndim;
    size_t   shape[8];
    long     data_offset;   
};

struct header { /* informações do arquivo emat*/
    unsigned char head[4];
    uint8_t dtype;
    uint32_t nlinhas;
    uint32_t ncols;
};

static int leitura(int fd, void *buf, size_t len){ /* leitura do arquivo npy*/
    size_t total = 0;
    ssize_t n;
    while (total < len) {
        n = read(fd, (char *)buf + total, len - total);
        if (n < 0) { perror("read"); return -1; }
        if (n == 0) { fprintf(stderr, "EOF inesperado\n"); return -1; }
        total += n;
    }
    return 0;
}

static int escrita(int fd, const void *buf, size_t len){ /* escruta no arquivo emat*/
    size_t total = 0;
    ssize_t n;
    while (total < len) {
        n = write(fd, (const char *)buf + total, len - total);
        if (n < 0) { perror("write"); return -1; }
        total += n;
    }
    return 0;
}


static int interpretar_dicionario(const char *txt, struct NpyHeader *npy)
{
    const char *p;

    /* descr */
    p = strstr(txt, "'descr'");
    if (!p || !(p = strchr(p + 7, ':')) || !(p = strchr(p, '\''))) return -1;
    p++;
    const char *q = strchr(p, '\'');
    if (!q || (size_t)(q - p) >= sizeof(npy->header_npy)) return -1;
    memcpy(npy->header_npy, p, q - p);
    npy->header_npy[q - p] = '\0';

    /* fortran_order */
    p = strstr(txt, "'fortran_order'");
    if (!p || !(p = strchr(p, ':'))) return -1;
    p++;
    while (*p == ' ') p++;
    npy->fortran_order = (strncmp(p, "True", 4) == 0);

    /* shape */
    p = strstr(txt, "'shape'");
    if (!p || !(p = strchr(p, '('))) return -1;
    p++;
    npy->ndim = 0;
    while (*p && *p != ')') {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ')') break;
        char *end;
        unsigned long long v = strtoull(p, &end, 10);
        if (end == p || npy->ndim >= 8) return -1;
        npy->shape[npy->ndim++] = (size_t)v;
        p = end;
    }
    return 0;
}


int ler_cabecalho(int fd, struct NpyHeader *npy) /* leitura das informaçoes do arquivo npy*/
{
    unsigned char pre[8];                 
    if (leitura(fd, pre, 8) < 0) return -1;

    if (memcmp(pre, "\x93NUMPY", 6) != 0) { /* identificador*/
        fprintf(stderr, "Nao e um arquivo .npy\n");
        return -1;
    }

    uint32_t hlen;
    long prefixo;
    if (pre[6] == 1) {
        unsigned char b[2];
        if (leitura(fd, b, 2) < 0) return -1;
        hlen = b[0] | (b[1] << 8);
        prefixo = 10;
    } else if (pre[6] == 2 || pre[6] == 3) {
        unsigned char b[4];
        if (leitura(fd, b, 4) < 0) return -1;
        hlen = b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
        prefixo = 12;
    } else {
        fprintf(stderr, "Versao %u do .npy nao suportada\n", pre[6]);
        return -1;
    }
    if (hlen == 0 || hlen > (1u << 20)) {
        fprintf(stderr, "Header invalido\n");
        return -1;
    }

    char *txt = malloc(hlen + 1);
    if (!txt) { perror("malloc"); return -1; }
    if (leitura(fd, txt, hlen) < 0) { free(txt); return -1; }
    txt[hlen] = '\0';

    int rc = interpretar_dicionario(txt, npy);
    free(txt);
    if (rc < 0) {
        fprintf(stderr, "Falha ao interpretar o header\n");
        return -1;
    }

    npy->data_offset = prefixo + hlen;
    return 0;
}

static double ler_valor(const void *buf, int src, size_t k){ /* identificar o tipo do arquivo lido*/
    switch (src) {
        case SRC_U8:  return ((const uint8_t *)buf)[k];
        case SRC_I32: return ((const int32_t *)buf)[k];
        case SRC_F32: return ((const float   *)buf)[k];
        default:      return ((const double  *)buf)[k];
    }
}


int main (int argc, char *argv[]){
    
    if (argc != 3) {
        fprintf(stderr, "Uso: %s W.npy W.emat\n", argv[0]);
        return 1;
    }

    int fd_in = open(argv[1], O_RDONLY);
    if (fd_in < 0) { perror("open entrada"); return 1; }

    struct NpyHeader npy;
    if (ler_cabecalho(fd_in, &npy) < 0) return 1;
    if (npy.ndim != 2) {
        fprintf(stderr, "Esperava matriz 2D, mas ndim = %d\n", npy.ndim);
        return 1;
    }
    size_t linhas  = npy.shape[0];
    size_t colunas = npy.shape[1];

    /* converter os tipos do arquivo npy para o arquivo emat */
    int src;
    size_t src_sz;
    struct header h; 
    if      (strcmp(npy.header_npy, "|u1") == 0) { src = SRC_U8;  src_sz = 1; h.dtype = EA551_DTYPE_UINT8;   }
    else if (strcmp(npy.header_npy, "<i4") == 0) { src = SRC_I32; src_sz = 4; h.dtype = EA551_DTYPE_INT32;   }
    else if (strcmp(npy.header_npy, "<f4") == 0) { src = SRC_F32; src_sz = 4; h.dtype = EA551_DTYPE_FLOAT32; }
    else if (strcmp(npy.header_npy, "<f8") == 0) { src = SRC_F64; src_sz = 8; h.dtype = EA551_DTYPE_FLOAT32; } 
    else {
        fprintf(stderr, "descr '%s' nao suportado\n", npy.header_npy);
        return 1;
    }

    size_t n_elem = linhas * colunas;
    void *dados = malloc(n_elem * src_sz);
    if (!dados) { perror("malloc"); return 1; }
    if (leitura(fd_in, dados, n_elem * src_sz) < 0) return 1;
    close(fd_in);

    
    h.head[0] = 0x45; /* 'E' */
    h.head[1] = 0x4D; /* 'M' */
    h.head[2] = 0x41; /* 'A' */
    h.head[3] = 0x54; /* 'T' */
    h.nlinhas = (uint32_t) linhas;
    h.ncols   = (uint32_t) colunas;

    int fd_out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if(fd_out < 0){
        perror("open");
        return 1;
    }
    /* escreve as informações do cabeçalho no emat*/
    if (escrita(fd_out, h.head,     sizeof(h.head))    < 0) return 1; 
    if (escrita(fd_out, &h.dtype,   sizeof(h.dtype))   < 0) return 1;
    if (escrita(fd_out, &h.nlinhas, sizeof(h.nlinhas)) < 0) return 1;
    if (escrita(fd_out, &h.ncols,   sizeof(h.ncols))   < 0) return 1;


    for (size_t i = 0; i < linhas; i++) { /* escreve o arquivo emat de acordo com o tipo*/
        for (size_t j = 0; j < colunas; j++) {
            size_t idx = npy.fortran_order ? j * linhas + i : i * colunas + j;
            double valor = ler_valor(dados, src, idx);

            switch (h.dtype) {
                case EA551_DTYPE_UINT8: {
                    uint8_t v = (uint8_t) valor;
                    if (escrita(fd_out, &v, sizeof(v)) < 0) return 1;
                    break;
                }
                case EA551_DTYPE_INT32: {
                    int32_t v = (int32_t) valor;
                    if (escrita(fd_out, &v, sizeof(v)) < 0) return 1;
                    break;
                }
                case EA551_DTYPE_FLOAT32: {
                    float v = (float) valor;
                    if (escrita(fd_out, &v, sizeof(v)) < 0) return 1;
                    break;
                }
            }
        }
    }

    if (close(fd_out) < 0) { perror("close"); return 1; }
    free(dados);
    return 0;


}