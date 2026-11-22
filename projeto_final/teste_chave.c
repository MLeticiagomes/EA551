#include <stdio.h>
#include "chave.h"

int main(void) {
    if (verificar_api_key() != 0) {
        printf("Falha na verificacao da API_KEY.\n");
        return 1;
    }

    printf("Verificacao concluida com sucesso!\n");
    return 0;
}