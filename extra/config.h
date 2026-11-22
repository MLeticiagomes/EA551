#ifndef CONFIG_H
#define CONFIG_H

struct Config {
    int  modo;            /* 1 = test, 0 = infer */
    char pesos[512], entrada[512];
    int  matmul_threads, buffer, prod_threads, visualizacao;
};

/* lê e valida o arquivo EA551-YAML; retorna 0 em sucesso, -1 em erro  */
int config_ler(const char *arquivo, struct Config *c);


int config_carregar(const char *arquivo);

#endif