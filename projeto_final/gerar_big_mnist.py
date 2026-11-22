#!/usr/bin/env python3
"""Gera big_mnist_txt 128x128 a partir de um mnist_txt 28x28.

Ampliação por vizinho mais próximo: cada pixel de saída copia o pixel
da imagem original em (y * 28 // 128, x * 28 // 128). O rótulo continua
sendo o nome da pasta do dígito.

Treino e teste: uma imagem de saída por imagem de entrada
(60 mil e 10 mil no conjunto cheio).

Não gera .emat.
A árvore de texto do conjunto cheio ocupa vários GB.

Script gerado pelo Cursor.
"""

from __future__ import annotations

import argparse
import os
import sys
from typing import List

from tqdm import tqdm


def read_matrix(path: str) -> List[List[int]]:
    rows: List[List[int]] = []
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            stripped = line.strip()
            if not stripped:
                continue
            if not (stripped.startswith("[") and (stripped.endswith("],") or stripped.endswith("]"))):
                raise ValueError(f"Linha inválida em {path}: {stripped[:80]}")
            offset = 2 if stripped.startswith("[[") else 1
            body = stripped[offset:-2]
            if body == "":
                rows.append([])
            else:
                rows.append([int(float(value)) for value in body.split(",")])
            if stripped.endswith("]]"):
                break
    if not rows or any(len(row) != len(rows[0]) for row in rows):
        raise ValueError(f"Matriz inválida em {path}")
    return rows


def upscale_nearest(src: List[List[int]], dst_n: int) -> List[List[int]]:
    src_n = len(src)
    out: List[List[int]] = []
    for y in range(dst_n):
        sy = min(src_n - 1, (y * src_n) // dst_n)
        row_src = src[sy]
        row = [row_src[min(src_n - 1, (x * src_n) // dst_n)] for x in range(dst_n)]
        out.append(row)
    return out


def format_matrix(rows: List[List[int]]) -> str:
    lines: List[str] = []
    last = len(rows) - 1
    for i, row in enumerate(rows):
        body = ", ".join(str(value) for value in row)
        if i == 0:
            lines.append(f"[[{body}],")
        elif i == last:
            lines.append(f"[{body}]]")
        else:
            lines.append(f"[{body}],")
    return "\n".join(lines) + "\n"


def list_images(split_dir: str) -> List[tuple[str, str]]:
    found: List[tuple[str, str]] = []
    for root, _, files in os.walk(split_dir):
        digit = os.path.basename(root)
        if not digit.isdigit():
            continue
        for name in files:
            if name.endswith(".txt"):
                found.append((os.path.join(root, name), digit))
    found.sort()
    return found


def write_one(dst_path: str, text: str) -> None:
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    with open(dst_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def convert_split(src_split: str, dst_split: str, dst_n: int) -> int:
    images = list_images(src_split)
    written = 0
    for src_path, digit in tqdm(images, desc=os.path.basename(src_split), unit="img"):
        matrix = upscale_nearest(read_matrix(src_path), dst_n)
        text = format_matrix(matrix)
        stem = os.path.splitext(os.path.basename(src_path))[0]
        write_one(os.path.join(dst_split, digit, f"{stem}.txt"), text)
        written += 1
    return written


def main() -> int:
    parser = argparse.ArgumentParser(description="Amplia mnist_txt 28x28 para big_mnist_txt.")
    parser.add_argument("--mnist_txt", required=True, help="Pasta com train/ e test/ em 28x28.")
    parser.add_argument("--out", default="big_mnist_txt", help="Pasta de saída.")
    parser.add_argument("--size", type=int, default=128, help="Lado da imagem de saída.")
    args = parser.parse_args()

    if args.size < 1:
        print("size precisa ser positivo", file=sys.stderr)
        return 1

    train_src = os.path.join(args.mnist_txt, "train")
    test_src = os.path.join(args.mnist_txt, "test")
    if not os.path.isdir(train_src) or not os.path.isdir(test_src):
        print(f"Esperava {train_src} e {test_src}", file=sys.stderr)
        return 1

    print(
        f"Gerando {args.size}x{args.size} em {args.out}. "
        "Isso ocupa vários GB no conjunto cheio.",
        flush=True,
    )
    n_train = convert_split(train_src, os.path.join(args.out, "train"), args.size)
    n_test = convert_split(test_src, os.path.join(args.out, "test"), args.size)
    print(f"Pronto: {n_train} treino, {n_test} teste.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
