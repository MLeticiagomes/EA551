import os
import time
import argparse
from typing import Dict, Any

import tqdm
import torch
import numpy as np

import threading
import ctypes
import posix_ipc
import mmap as pymmap
import sys


CAP = 32
NUM_PIXELS = 784

SHM_NAME    = "/ea551_shm"
SEM_ESCRITA = "/ea551_sem_escrita"
SEM_LEITURA = "/ea551_sem_leitura"
SEM_MUTEX   = "/ea551_sem_mutex"


class SharedStruct(ctypes.Structure):
    _fields_ = [
        ("buf",            (ctypes.c_double * NUM_PIXELS) * CAP),
        ("label",          ctypes.c_int32 * CAP),
        ("in_idx",         ctypes.c_int),
        ("out_idx",        ctypes.c_int),
        ("total_imagens",  ctypes.c_long),
        ("num_pixels",     ctypes.c_int),
    ]

def make_argv(lista):
    """Converte lista de strings em (argc, char** argv) para o C."""
    arr = (ctypes.c_char_p * (len(lista) + 1))(*[s.encode() for s in lista], None)
    return len(lista), arr


def read_mnist_emat_image(global_state):

    shared = global_state["shared"]
    sem_leitura = global_state["sem_leitura"]
    sem_mutex = global_state["sem_mutex"]
    sem_escrita = global_state["sem_escrita"]

    sem_leitura.acquire()
    sem_mutex.acquire()

    idx = shared.out_idx

    #  numpy enxerga diretamente os bytes do slot compartilhado
    pixel_array = np.frombuffer(shared.buf[idx], dtype=np.float64)
    label = int(shared.label[idx])

    # copia o slot e transforma em um tensor
    image_tensor = torch.from_numpy(pixel_array.copy()).reshape(1, 1, 28, 28)

    shared.out_idx = (idx + 1) % CAP

    sem_mutex.release()
    sem_escrita.release()

    return image_tensor, label


def train(args: Dict[str, Any], global_state):
    n_images = global_state["total_imagens"]

    print("Inicialização do modelo de rede neural...")
    model = torch.nn.Linear(28 * 28, 10)

    optimizer = torch.optim.SGD(model.parameters(), lr=args["learning_rate"])
    loss_function = torch.nn.CrossEntropyLoss()

    iterator = tqdm.tqdm(range(n_images), desc="Consumidor consumindo imagens do MNIST EMAT e treinando.")
    predictions = []
    targets = []
    t_read = 0.0
    t_loop = 0.0
    for _ in iterator:
        t0 = time.perf_counter()

        t_read_start = time.perf_counter()
        image_tensor, image_class = read_mnist_emat_image(global_state)
        t_read += time.perf_counter() - t_read_start

        image_tensor = image_tensor.view(1, -1).float()

        output = model(image_tensor)
        loss: torch.Tensor = loss_function(output, torch.tensor([image_class]))

        loss.backward()
        optimizer.step()
        optimizer.zero_grad()

        predictions.append(output.argmax(dim=1).item())
        targets.append(image_class)

        t_loop += time.perf_counter() - t0
        read_pct = 100.0 * t_read / t_loop if t_loop > 0 else 0.0

        iterator.set_postfix(read_pct=f"{read_pct:.1f}%")

    accuracy = (torch.tensor(predictions) == torch.tensor(targets)).float().mean().item()
    print(f"Acurácia de classificação de dígitos: {accuracy:.4f}")
    print(f"O consumo de imagens (read_mnist_emat_image) gastou ({read_pct:.1f}% do tempo do loop)")

    out_dir = args.get("weights_dir", ".")
    os.makedirs(out_dir, exist_ok=True)
    w_path = os.path.join(out_dir, "W.npy")
    np.save(w_path, model.weight.detach().cpu().numpy().astype(np.float32))
    print(f"Pesos da rede neural salvos em {w_path} (shape {tuple(model.weight.shape)})")

def consumidor_treino(args_dict):
    
    shm = posix_ipc.SharedMemory(SHM_NAME)
    mapfile = pymmap.mmap(shm.fd, shm.size)
    shm.close_fd()

    shared = SharedStruct.from_buffer(mapfile)

    sem_escrita = posix_ipc.Semaphore(SEM_ESCRITA)
    sem_leitura = posix_ipc.Semaphore(SEM_LEITURA)
    sem_mutex   = posix_ipc.Semaphore(SEM_MUTEX)

    global_state = {
        "shared": shared,
        "sem_escrita": sem_escrita,
        "sem_leitura": sem_leitura,
        "sem_mutex": sem_mutex,
        "total_imagens": shared.total_imagens,
    }

    train(args_dict, global_state)

    sem_escrita.close()
    sem_leitura.close()
    sem_mutex.close()
    del global_state, shared
    mapfile.close()
    print("Treinamento concluído.")
    sys.stdout.flush()
    sys.stderr.flush()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--mnist_emat_path", type=str, default="mnist_emat")
    parser.add_argument("--learning_rate", type=float, default=1e-4)
    parser.add_argument("--weights_dir", type=str, default=".")
    args = parser.parse_args()
    args_dict = vars(args)

    # biblioteca

    lib = ctypes.CDLL("./libclassif.so")

    CONSUMIDOR = ctypes.CFUNCTYPE(None)
    lib.data_loader.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p), CONSUMIDOR]
    lib.data_loader.restype = ctypes.c_int
    lib.matmul_set_threads(1)
    lib.teste.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_char_p)]
    lib.teste.restype = ctypes.c_int

    cb = CONSUMIDOR(lambda: consumidor_treino(args_dict))
    tqdm.tqdm.set_lock(threading.RLock())
    w_path = os.path.join(args.weights_dir, "W.npy")

    # produtor
    argc, argv = make_argv(["train.py", os.path.join(args.mnist_emat_path, "train"), w_path])
    r = lib.data_loader(argc, argv, cb)
    if r != 0:
        raise SystemExit(f"data_loader falhou ({r})")

    # teste
    argc, argv = make_argv(["train.py", os.path.join(args.mnist_emat_path, "test"), w_path])
    t0 = time.perf_counter()
    r = lib.teste(argc, argv)
    t_teste = time.perf_counter() - t0
    print(f"Tempo total da etapa de teste: {t_teste:.3f} s")
    if r != 0:
        raise SystemExit(f"teste falhou ({r})")