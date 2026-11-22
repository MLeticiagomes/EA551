import os
import time
import argparse
import random  # novo V2
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
        
        # Retorna tensor PyTorch. Note as dimensões: (1, 1, 28, 28)
        # 1: batch size (quantidade de imagens em paralelo)
        # 1: número de canais (preto e branco)
        # 28: altura da imagem
        # 28: largura da imagem
        return torch.tensor(image_lines).reshape(1, 1, 28, 28)

def get_dict_of_images(mnist_txt_path: str) -> Dict[str, int]:
    """
    Indexa as imagens .txt do MNIST sob mnist_txt_path (os.walk).
    Retorna dicionário de caminhos -> classes (int 0..9),
    com a classe lida do nome da pasta pai de cada arquivo.
    """
    # Dict funciona como uma hash table: chave -> valor
    dict_of_images = {}

    # Percorre a árvore de diretórios "na mão"
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

    # Inicialização da rede neural: é somente uma multiplicação de matrizes 
    # Cada elemento da matriz de pesos é um peso da conexão entre um pixel da imagem e um neurônio de saída classificador de digitos!
    print("Inicialização do modelo de rede neural...")
    model = torch.nn.Linear(28*28, 10)

    # Loop de treinamento: o consumidor dos dados do MNIST TXT
    optimizer = torch.optim.SGD(model.parameters(), lr=args["learning_rate"])
    loss_function = torch.nn.CrossEntropyLoss()

    
    # embaralhar a ordem das imagens para evitar viés.
    items = list(dict_of_train_images.items())
    random.shuffle(items)
    iterator = tqdm.tqdm(items, desc="Consumidor consumindo imagens do MNIST TXT e treinando.")

    predictions = []
    targets = []
    t_read = 0.0
    t_loop = 0.0
    for image_path, image_class in iterator:
        t0 = time.perf_counter()

        # ler o arquivo .txt e parsear os valores.
        t_read_start = time.perf_counter()
        image_tensor = read_mnist_txt_image(image_path)
        t_read += time.perf_counter() - t_read_start

        # Linearização da imagem: transforma a imagem de 28x28 para 784 (28*28) para permitir a multiplicação de matrizes.
        image_tensor = image_tensor.view(1, -1)


        # Forward pass: computação da saída da rede neural.
        output = model(image_tensor)

        # Computação do erro: a função de perda (loss function)
        loss: torch.Tensor = loss_function(output, torch.tensor([image_class]))

        # Backward pass: computação do gradiente. A fase que ajusta os pesos da rede neural para minimizar o erro.
        loss.backward()
        optimizer.step()
        optimizer.zero_grad()

        # Acumular as previsões e os targets para o cálculo da acurácia
        predictions.append(output.argmax(dim=1).item())     
        targets.append(image_class)
        
        # Computa quantos % do loop foi gasto no consumo de imagens
        t_loop += time.perf_counter() - t0
        read_pct = 100.0 * t_read / t_loop if t_loop > 0 else 0.0

        # Atualizar o progresso do loop
        iterator.set_postfix(read_pct=f"{read_pct:.1f}%")
        
    # Reporta a acurácia depois de uma época de treinamento 
    accuracy = (torch.tensor(predictions) == torch.tensor(targets)).float().mean().item()
    print(f"Acurácia de classificação de dígitos: {accuracy:.4f}")
    print(f"O consumo de imagens (read_mnist_txt_image) gastou ({read_pct:.1f}% do tempo do loop)")

    # Salvar pesos em .npy 
    
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