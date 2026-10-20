import os
import time
import argparse
from typing import Dict, Any

import tqdm
import torch
import numpy as np


def read_mnist_txt_image(filename: str) -> torch.Tensor:
    """Lê uma imagem do MNIST em formato textual e retorna um tensor PyTorch"""
    with open(filename, 'r') as f:
        # Lista de listas de valores da imagem
        image_lines = [] 

        # Lê cada linha do arquivo
        for line in f:
            line_stripped = line.strip()
            if line_stripped.startswith("[") and (line_stripped.endswith("],") or line_stripped.endswith("]")):
                # Detectar primeira linha: 
                if line_stripped.startswith("[["):
                    offset = 2
                else:
                    offset = 1

                # Extrai as strings da linha
                line_strings = line_stripped[offset:-2].split(",")

                # Converte as strings para float
                line_values = [float(value) for value in line_strings]

                # Adiciona a lista de valores da imagem à lista de listas
                image_lines.append(line_values)

                if line_stripped.endswith("]]"):
                    # Fim da imagem
                    break
            else:
                raise ValueError(f"Linha inválida: {line_stripped}")
   
        return torch.tensor(image_lines).reshape(1, 1, 28, 28)

def get_dict_of_images(mnist_txt_path: str) -> Dict[str, int]:

    # Dict funciona como uma hash table: chave -> valor
    dict_of_images = {}

    # Percorre a árvore de diretórios 
    for root, _, files in tqdm.tqdm(os.walk(mnist_txt_path), desc="Indexando..."):
        for filename in files:
            if not filename.endswith(".txt"):
                continue

            filepath = os.path.join(root, filename)

            # A classe é o nome da pasta imediatamente acima do arquivo
            image_class = int(os.path.basename(root))

            # Preenche qual a classe da imagem
            dict_of_images[filepath] = image_class

    return dict_of_images

def train(args: Dict[str, Any]):
    print("Indexando imagens de treino...")
    dict_of_train_images = get_dict_of_images(os.path.join(args["mnist_txt_path"], "train"))

    print("Inicialização do modelo de rede neural...")
    model = torch.nn.Linear(28*28, 10)

    optimizer = torch.optim.SGD(model.parameters(), lr=args["learning_rate"])
    loss_function = torch.nn.CrossEntropyLoss()

    iterator = tqdm.tqdm(dict_of_train_images.items(), desc="Consumidor consumindo imagens do MNIST TXT e treinando.")
    predictions = []
    targets = []
    t_read = 0.0
    t_loop = 0.0
    for image_path, image_class in iterator:
        t0 = time.perf_counter()

        # le o arquivo .txt e parsear os valores.
        t_read_start = time.perf_counter()
        image_tensor = read_mnist_txt_image(image_path)
        t_read += time.perf_counter() - t_read_start

        image_tensor = image_tensor.view(1, -1)

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
    print(f"O consumo de imagens (read_mnist_txt_image) gastou ({read_pct:.1f}% do tempo do loop)")

    out_dir = args.get("weights_dir", ".")
    os.makedirs(out_dir, exist_ok=True)
    w_path = os.path.join(out_dir, "W.npy")
    np.save(w_path, model.weight.detach().cpu().numpy().astype(np.float32))
    print(f"Pesos da rede neural salvos em {w_path} (shape {tuple(model.weight.shape)})")

if __name__ == "__main__":
    # Cria um parser de argumentos na linha de comando
    parser = argparse.ArgumentParser()
    parser.add_argument("--mnist_txt_path", type=str, default="mnist_txt")
    parser.add_argument("--learning_rate", type=float, default=1e-4)
    parser.add_argument("--weights_dir", type=str, default=".")
    args = parser.parse_args()

    # Cria dicionário com argumentos fornecidos via linha de comando
    args_dict = vars(args)

    # Roda o treinamento da rede neural
    train(args_dict)

    print("Treinamento concluído.")