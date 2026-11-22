/* Confere API_KEY pelo digest SHA-256.
 * gcc -o chave chave.c sha256.c
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chave.h"

void sha256(const uint8_t *msg, uint64_t len, uint8_t out[32]);

/* Digest de "ea551". Troque pelo digest da sua senha. */
static const char ESPERADO[] =
    "6a3bb51137f4ef8c6353a8a529bffb4c54c4e61f2f00f002e8e449b37df3f5ab"; /* projeto_final_ml*/

int verificar_api_key(void) {
    static int resultado = -1;          /* -1 = ainda não verificou */
    if (resultado != -1) return resultado;

    const char *key = getenv("API_KEY");
    if (key == NULL) {
        fprintf(stderr, "API_KEY ausente\n");
        return resultado = 1;
    }

    uint8_t out[32];
    char hex[65];
    sha256((const uint8_t *)key, (uint64_t)strlen(key), out);
    for (int i = 0; i < 32; i++) {
        sprintf(hex + i * 2, "%02x", out[i]);
    }
    hex[64] = '\0';

    if (strcmp(hex, ESPERADO) != 0) {
        printf("%s\n", hex);
        fprintf(stderr, "recusada\n");
        return resultado = 1;
    }

    printf("%s\n", hex);
    printf("aceita\n");
    fflush(stdout);
    return resultado = 0;
}