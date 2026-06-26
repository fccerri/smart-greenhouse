# Estufa Inteligente — SSC0142 (Redes de Computadores)

Implementação do protocolo de aplicação **"ES"** (Estufa Inteligente) sobre
**sockets TCP**. Cada componente do sistema é um **processo separado** do SO,
conforme exigido pela especificação do trabalho (Entrega 2).

## Ambiente de desenvolvimento (para avaliação)

- **SO:** Linux
- **Compilador:** `g++` 13.1.0 (padrão **C++17**)
- **Transporte:** TCP (sockets POSIX)

## Componentes

| Processo      | Papel                                                        |
|---------------|-------------------------------------------------------------|
| `gerenciador` | Servidor TCP. Aceita conexões, armazena leituras, aplica o controle automático dos atuadores e responde ao cliente. |
| `sensor`      | Sensor (temperatura `T`, umidade `U` ou CO2 `O`). Envia leitura a cada 1 s. |
| `atuador`     | Atuador (aquecedor `A`, resfriador `R`, irrigação `S`, injetor de CO2 `I`). Liga/desliga sob comando. |
| `cliente`     | Interface do operador (menu): requisita leituras e configura limites. |

## Compilação

```bash
make          # gera os 4 binários
make clean    # remove os binários
```

## Execução

Cada componente em um terminal. Exemplo com a porta `8080`:

```bash
# Terminal 1 — servidor
./gerenciador 8080

# Terminais 2-4 — sensores
./sensor T 127.0.0.1 8080
./sensor U 127.0.0.1 8080
./sensor O 127.0.0.1 8080

# Terminais 5-8 — atuadores
./atuador A 127.0.0.1 8080
./atuador R 127.0.0.1 8080
./atuador S 127.0.0.1 8080
./atuador I 127.0.0.1 8080

# Terminal 9 — cliente (menu interativo)
./cliente 127.0.0.1 8080
```

No cliente, configure limites estreitos (ex.: temperatura min=20, max=26) e
observe no terminal do Gerenciador os atuadores ligando/desligando conforme as
leituras cruzam os limites (requisito 3.3, com histerese).

## Estrutura do protocolo

**Header (5 bytes):** `ID Protocolo (2) = "ES"` · `Tipo (1)` · `Remetente (1)` ·
`Destinatário (1)`. O tamanho do payload **não viaja no header**: é deduzido do
campo `Tipo` (cada tipo tem payload de tamanho fixo), o que resolve o *framing*
sobre o fluxo de bytes do TCP. Ver `include/protocol.hpp`.

Valores (`float`) e o ID do protocolo são serializados em **ordem de rede
(big-endian)** para evitar problemas de endianness/padding.

## Organização do código

```
include/protocol.hpp  src/protocol.cpp   # tipos, IDs, (de)serialização
include/net.hpp        src/net.cpp        # sockets + framing (send_all/recv_all)
src/gerenciador.cpp                       # servidor (thread por conexão)
src/sensor.cpp  src/atuador.cpp  src/cliente.cpp
```
