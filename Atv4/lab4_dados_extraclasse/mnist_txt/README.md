# MNIST em `.txt`

70.000 imagens do MNIST, cada uma em um arquivo `.txt`. O formato é de imagens 28×28 de um canal, um pixel por valor de 0 a 255.

```
[[0, 0, 0, ...],
[0, 0, 0, ...],
...
[0, 0, 0, ...]]
```

## Pastas

A divisão é a do MNIST. Dentro de cada conjunto, uma pasta por dígito.

| Pasta    | Origem | Quantidade |
|----------|--------|------------|
| `train/` | treino | 60.000     |
| `test/`  | teste  | 10.000     |

`train/5/00000.txt` é um 5 do treino. O nome do arquivo continua sendo o índice da imagem no MNIST.
