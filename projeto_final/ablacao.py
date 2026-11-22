#!/usr/bin/env python3
"""
Ablação de hiperparâmetros no teste (EA551, item 7.4).

Parte da configuração inicial do relatório e varia UM parâmetro por vez:
  - produtor_threads
  - consumidores
  - matmul_threads
  - buffer_tamanho

Para cada experimento gera um arquivo EA551-YAML em experimentos/, executa
o ./testador, mede o tempo de parede (o mesmo que o `time` do shell) e
salva tudo em resultados.csv. Depois plota os gráficos (tempo x parâmetro).

Uso:
    python3 ablacao.py                  # varredura completa (mais pontos)
    python3 ablacao.py --so-relatorio   # só os valores já usados no relatório
    python3 ablacao.py --reps 5         # 5 repetições por ponto
    python3 ablacao.py --plot-only      # só replota a partir do CSV

Requer: matplotlib  (pip install matplotlib)
O testador herda as variáveis de ambiente (chave lida por
verificar_api_key), então rode no mesmo terminal em que ele funciona.
"""
import argparse
import csv
import os
import re
import statistics
import subprocess
import sys
import time

# --------------------------------------------------------------------------
# Configuração inicial (caso 1 do relatório)
# --------------------------------------------------------------------------
BASE = {
    "matmul_threads": 1,
    "buffer_tamanho": 16,
    "produtor_threads": 4,
    "consumidores": 4,
}

# Valores já testados no relatório (inclui o valor da configuração inicial)
SWEEPS_RELATORIO = {
    "produtor_threads": [4, 8, 16, 32],
    "consumidores":     [4, 8, 16, 32],
    "matmul_threads":   [1, 4, 16, 64],
    "buffer_tamanho":   [16, 32, 64, 128],
}

# Varredura completa: os valores do relatório + pontos intermediários e
# extremos, para mostrar a curva inteira. Os máximos respeitam os limites do
# código (produtores <= 256, consumidores <= 64, matmul <= 64, buffer <= 128).
SWEEPS_COMPLETO = {
    "produtor_threads": [1, 2, 4, 8, 16, 32, 64],
    "consumidores":     [1, 2, 4, 8, 16, 32, 64],
    "matmul_threads":   [1, 2, 4, 8, 16, 32, 64],
    "buffer_tamanho":   [1, 2, 4, 8, 16, 32, 64, 128],
}

TITULOS = {
    "produtor_threads": "Número de produtores",
    "consumidores":     "Número de consumidores",
    "matmul_threads":   "Threads no matmul",
    "buffer_tamanho":   "Tamanho do buffer",
}

PESOS = "W.npy"
ENTRADA = "mnist_emat/test"
DIR_EXP = "experimentos"
CSV_SAIDA = "resultados.csv"


# --------------------------------------------------------------------------
# Execução dos experimentos
# --------------------------------------------------------------------------
def escrever_yaml(cfg, caminho):
    with open(caminho, "w") as f:
        f.write("# EA551-YAML: modo teste sobre a pasta do MNIST\n")
        f.write('modo: "test"\n')
        f.write(f'pesos: "{PESOS}"                 # .npy gerado pelo train.py\n')
        f.write(f'entrada: "{ENTRADA}"     # pasta com subpastas 0..9\n\n')
        for k in ("matmul_threads", "buffer_tamanho",
                  "produtor_threads", "consumidores"):
            f.write(f"{k}: {cfg[k]}\n")


def rodar_uma_vez(testador, yaml_path):
    """Executa o testador; devolve (tempo_em_segundos, acuracia ou None)."""
    t0 = time.perf_counter()
    p = subprocess.run([testador, yaml_path], capture_output=True, text=True)
    dt = time.perf_counter() - t0
    if p.returncode != 0:
        sys.stderr.write(f"\nFALHA ({yaml_path}), codigo {p.returncode}:\n"
                         f"{p.stdout}\n{p.stderr}\n")
        sys.exit(1)
    m = re.search(r"Acuracia no teste:\s*(\d+)/(\d+)", p.stdout)
    acc = int(m.group(1)) / int(m.group(2)) if m else None
    return dt, acc


def executar(testador, reps, sweeps):
    os.makedirs(DIR_EXP, exist_ok=True)
    cache = {}      # configuração -> (media, desvio, acuracia)
    linhas = []

    # execução de aquecimento (enche o cache de arquivos do SO), descartada
    aq = os.path.join(DIR_EXP, "aquecimento.yaml")
    escrever_yaml(BASE, aq)
    print("aquecimento...", flush=True)
    rodar_uma_vez(testador, aq)

    for param, valores in sweeps.items():
        for v in valores:
            cfg = dict(BASE)
            cfg[param] = v
            chave = tuple(sorted(cfg.items()))

            if chave not in cache:
                nome = (f"exp_p{cfg['produtor_threads']}"
                        f"_c{cfg['consumidores']}"
                        f"_m{cfg['matmul_threads']}"
                        f"_b{cfg['buffer_tamanho']}.yaml")
                yaml_path = os.path.join(DIR_EXP, nome)
                escrever_yaml(cfg, yaml_path)

                tempos, acc = [], None
                for _ in range(reps):
                    dt, acc = rodar_uma_vez(testador, yaml_path)
                    tempos.append(dt)
                media = statistics.mean(tempos)
                desvio = statistics.stdev(tempos) if len(tempos) > 1 else 0.0
                cache[chave] = (media, desvio, acc)

            media, desvio, acc = cache[chave]
            print(f"{param:18s} = {v:<3d} -> {media:6.2f} s "
                  f"(+-{desvio:.2f})  acc={acc}", flush=True)
            linhas.append({"parametro": param, "valor": v,
                           "tempo_medio_s": media, "desvio_s": desvio,
                           "acuracia": acc})

    with open(CSV_SAIDA, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["parametro", "valor",
                                          "tempo_medio_s", "desvio_s",
                                          "acuracia"])
        w.writeheader()
        w.writerows(linhas)
    print(f"\nresultados salvos em {CSV_SAIDA}")

    # sanidade: a acurácia não pode depender dos hiperparâmetros
    accs = {l["acuracia"] for l in linhas}
    if len(accs) > 1:
        print(f"ATENCAO: acuracias diferentes entre experimentos: {accs}")


# --------------------------------------------------------------------------
# Gráficos
# --------------------------------------------------------------------------
def ler_csv():
    dados = {}
    with open(CSV_SAIDA) as f:
        for r in csv.DictReader(f):
            dados.setdefault(r["parametro"], []).append(
                (int(r["valor"]), float(r["tempo_medio_s"]),
                 float(r["desvio_s"])))
    return dados


def desenhar(ax, param, pontos):
    pontos.sort()
    xs = [p[0] for p in pontos]
    ys = [p[1] for p in pontos]
    es = [p[2] for p in pontos]
    ax.errorbar(xs, ys, yerr=es, marker="o", capsize=3, linewidth=1.5)
    for x, y in zip(xs, ys):                       # valor sobre cada ponto
        ax.annotate(f"{y:.2f}", (x, y), textcoords="offset points",
                    xytext=(0, 7), ha="center", fontsize=8)
    ax.set_xscale("log", base=2)
    ax.set_xticks(xs)
    ax.set_xticklabels([str(x) for x in xs])
    ax.minorticks_off()
    ax.set_xlabel(TITULOS[param])
    ax.set_ylabel("Tempo de teste (s)")
    ax.set_title(f"Variando: {TITULOS[param].lower()}")
    ax.set_ylim(0, max(ys) * 1.2)
    ax.grid(True, alpha=0.3)
    # destaca o ponto da configuração inicial
    base_x = BASE[param]
    if base_x in xs:
        i = xs.index(base_x)
        ax.plot(xs[i], ys[i], "o", markersize=11, markerfacecolor="none",
                markeredgecolor="red", label="config. inicial")
        ax.legend()


def plotar():
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    dados = ler_csv()
    params = list(TITULOS)

    # um PNG por experimento (para colar no relatório)
    for param in params:
        fig, ax = plt.subplots(figsize=(5.5, 3.8))
        desenhar(ax, param, dados[param])
        fig.tight_layout()
        fig.savefig(f"grafico_{param}.png", dpi=150)
        plt.close(fig)

    # painel 2x2 com os quatro
    fig, axs = plt.subplots(2, 2, figsize=(11, 8))
    for ax, param in zip(axs.flat, params):
        desenhar(ax, param, dados[param])
    cfg_txt = ", ".join(f"{k}={v}" for k, v in BASE.items())
    fig.suptitle(f"Ablação no teste (config. inicial: {cfg_txt})", fontsize=10)
    fig.tight_layout()
    fig.savefig("ablacao.png", dpi=150)
    plt.close(fig)
    print("gráficos: ablacao.png e grafico_<parametro>.png")


# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--testador", default="./testador")
    ap.add_argument("--reps", type=int, default=3,
                    help="repetições por configuração (padrão: 3)")
    ap.add_argument("--so-relatorio", action="store_true",
                    help="usa só os valores já presentes no relatório")
    ap.add_argument("--plot-only", action="store_true",
                    help="não executa; só plota a partir de resultados.csv")
    args = ap.parse_args()

    if not args.plot_only:
        sweeps = SWEEPS_RELATORIO if args.so_relatorio else SWEEPS_COMPLETO
        executar(args.testador, args.reps, sweeps)
    plotar()


if __name__ == "__main__":
    main()