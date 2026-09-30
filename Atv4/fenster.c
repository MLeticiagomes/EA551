/* Template de uso da fenster.h. A matriz de demonstração está neste arquivo no variável img.
   gcc -o fenster fenster.c -lX11
   Fechar a janela encerra o programa. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "fenster.h"

enum { LADO = 8, ESCALA = 24, JANELA = LADO * ESCALA };

/* Cada célula é um pixel de 0 a 255. */
static const int img[LADO][LADO] = {
    {  0,   0,   0,   0,   0,   0,   0,   0},
    {  0, 255, 255, 255, 255, 255, 255,   0},
    {  0, 255,   0,   0,   0,   0, 255,   0},
    {  0, 255,   0, 255, 255,   0, 255,   0},
    {  0, 255,   0, 255, 255,   0, 255,   0},
    {  0, 255,   0,   0,   0,   0, 255,   0},
    {  0, 255, 255, 255, 255, 255, 255,   0},
    {  0,   0,   0,   0,   0,   0,   0,   0},
};

static void pinta(struct fenster *f)
{
    for (int i = 0; i < LADO; i++) {
        for (int j = 0; j < LADO; j++) {
            int v = img[i][j];
            uint32_t cor;

            if (v < 0)
                v = 0;
            if (v > 255)
                v = 255;

            // cada pixel é um uint32_t com os três canais de cor (RGB) 0x00RRGGBB, onde R, G e B são os bits 16, 8 e 0 do valor v
            cor = (uint32_t)v << 16 | (uint32_t)v << 8 | (uint32_t)v;

            // pinta o pixel na janela, note que cada pixel é um quadrado de ESCALA x ESCALA pixels para facilitar a visualização de uma matriz pequena
            for (int dy = 0; dy < ESCALA; dy++) {
                for (int dx = 0; dx < ESCALA; dx++)
                    /* f: janela. x: coluna j do pixel, vezes ESCALA, mais dx dentro do quadrado.
                       y: linha i, vezes ESCALA, mais dy. A atribuição grava cor nesse ponto. */
                    fenster_pixel(f, j * ESCALA + dx, i * ESCALA + dy) = cor;
            }
        }
    }
}

int main(void)
{
    /* Um uint32_t por pixel da janela. calloc zera: pixel não pintado fica preto. */
    uint32_t *buf = calloc((size_t)JANELA * JANELA, sizeof *buf);
    Display *dpy;

    if (!buf)
        return 1;

    /* Confere se há display. Fecha em seguida: fenster_open abre o próprio. */
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "nao abriu o display\n");
        free(buf);
        return 1;
    }
    XCloseDisplay(dpy);

    /* title, width e height são const: só dá para definir na inicialização.
       buf é o buffer que a janela vai ler. */
    struct fenster f = {
        .title = "fenster",
        .width = JANELA,
        .height = JANELA,
        .buf = buf,
    };

    pinta(&f);
    if (fenster_open(&f) != 0) {
        free(buf);
        return 1;
    }
    /* fenster_loop devolve 0 enquanto a janela está aberta. */
    while (fenster_loop(&f) == 0)
        fenster_sleep(16);
    fenster_close(&f);
    free(buf);
    return 0;
}
