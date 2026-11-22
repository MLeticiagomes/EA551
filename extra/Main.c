#include <stdio.h>
#include <string.h>
#include "classif.h"

/* programa que usa a biblioteca libclassif.so */
int main(int argc, char *argv[])
{
    if (argc >= 2 && strcmp(argv[1], "inferencia") == 0)
        return inferencia(argc, argv);                    /* ./classif inferencia [W.npy] < img.emat */
    if (argc >= 3 && strcmp(argv[1], "teste") == 0)
        return teste(argc - 1, argv + 1);                 /* ./classif teste mnist_emat/test [W.npy] */
    if (argc >= 3 && strcmp(argv[1], "treino") == 0)
        return data_loader(argc - 1, argv + 1, NULL);     /* ./classif treino mnist_emat/train */

    fprintf(stderr, "Uso: %s inferencia [W.npy] < img.emat\n"
                    "     %s teste pasta_test [W.npy]\n"
                    "     %s treino pasta_train\n", argv[0], argv[0], argv[0]);
    return 1;
}