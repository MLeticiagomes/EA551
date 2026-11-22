#!/usr/bin/env python3
"""
Baseline lento do Modo Teste (referência de funcionamento, não de performance)

- Lê cada imagem de teste via leitura lenta de texto
- Multiplica pesos x imagem com loops Python puros
- Reporta acurácia e tempos para referência
"""
import argparse
import os
import time
from typing import List

import numpy as np
import tqdm

from train_v2_original import get_dict_of_images
from train_v2_original import read_mnist_txt_image as read_txt


def matmul_ingenuo(W: List[List[float]], x: List[float]) -> List[float]:
	"""y = W @ x com três laços explícitos (W: 10×784, x: 784)."""
	nout = len(W)
	nin = len(x)
	y = [0.0] * nout
	for j in range(nout):
		s = 0.0
		row = W[j]
		for i in range(nin):
			s += row[i] * x[i]
		y[j] = s
	return y


def argmax(v: List[float]) -> int:
	best_i = 0
	best = v[0]
	for i in range(1, len(v)):
		if v[i] > best:
			best = v[i]
			best_i = i
	return best_i


def main() -> None:
	parser = argparse.ArgumentParser(
		description="Teste lento do classificador (Python puro + read_txt)."
	)
	parser.add_argument("--mnist_txt_path", type=str, default="mnist_txt")
	parser.add_argument("--weights", type=str, default="W.npy")
	parser.add_argument(
		"--max_images",
		type=int,
		default=0,
		help="Se >0, limita quantas imagens processar.",
	)
	args = parser.parse_args()

	test_root = os.path.join(args.mnist_txt_path, "test")

	print(f"Carregando pesos .npy {args.weights}...")
	W_np = np.load(args.weights)
	W = W_np.astype(np.float64).tolist()

	print("Indexando imagens de teste...")
	images = get_dict_of_images(test_root)
	items = list(images.items())
	if args.max_images > 0:
		items = items[: args.max_images]
		print(f"(limitado a {len(items)} imagens)")

	correct = 0
	t_read = 0.0
	t_matmul = 0.0
	t0_all = time.perf_counter()

	iterator = tqdm.tqdm(items, desc="test.py (lento)")
	for path, label in iterator:
		t_r0 = time.perf_counter()
		# Carrega com loader lento
		img = read_txt(path)
		t_read += time.perf_counter() - t_r0

		# Lineariza 1×1×28×28 → 784 floats em lista Python
		flat = img.view(-1).detach().cpu().tolist()

		t_m0 = time.perf_counter()
		# Multiplica pesos pela imagem linearizada
		scores = matmul_ingenuo(W, flat)
		# Pega a classe com maior score no vetor de saída
		pred = argmax(scores)
		t_matmul += time.perf_counter() - t_m0

		if pred == label:
			correct += 1

	elapsed = time.perf_counter() - t0_all
	n = len(items)
	acc = correct / n if n else 0.0
	print(f"imagens={n} acertos={correct} acuracia={acc:.4f}")
	print(f"tempo_total={elapsed:.3f}s  leitura={t_read:.3f}s  matmul={t_matmul:.3f}s")


if __name__ == "__main__":
	main()
