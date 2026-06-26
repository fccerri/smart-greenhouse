// protocol.hpp — Definicao do protocolo de aplicacao "ES" (Estufa Inteligente).
//
// O protocolo roda sobre TCP. Cada mensagem possui um HEADER fixo de 5 bytes
// seguido de um PAYLOAD de tamanho fixo que depende do campo Tipo.
//
// Layout do header (5 bytes):
//   byte 0-1 : ID do Protocolo  -> sempre {0x45, 0x53} = "ES"
//   byte 2   : Tipo da mensagem (ver enum Tipo)
//   byte 3   : ID do Remetente  (categoria do componente, ver enum Cat)
//   byte 4   : ID do Destinatario (categoria do componente)
//
// IMPORTANTE: como TCP e um fluxo de bytes (sem fronteiras de mensagem), o
// tamanho do payload NAO viaja no header; ele e deduzido do Tipo pela funcao
// payload_size(). Isso permite ao receptor ler exatamente os bytes certos.

#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <cstdint>
#include <cstddef>
#include <vector>

namespace proto {

// Identificador do protocolo: "ES" em ASCII.
constexpr uint8_t PROTO_ID_0 = 0x45;  // 'E'
constexpr uint8_t PROTO_ID_1 = 0x53;  // 'S'
constexpr size_t  HEADER_SIZE = 5;

// Tipos de mensagem (campo byte 2 do header).
enum class Tipo : uint8_t {
    CONEXAO            = 0x01,  // requisicao de conexao
    CONF_CONEXAO       = 0x02,  // ack de conexao
    DESCONEXAO         = 0x03,  // aviso de desconexao
    CONF_DESCONEXAO    = 0x04,  // ack de desconexao
    COMANDO_ATUACAO    = 0x05,  // Gerenciador -> Atuador (liga/desliga)
    CONF_ATUACAO       = 0x06,  // Atuador -> Gerenciador (status atual)
    ENVIO_LEITURA      = 0x07,  // Sensor -> Gerenciador (sem ack)
    REQUISICAO_LEITURA = 0x08,  // Cliente -> Gerenciador
    RESPOSTA_LEITURA   = 0x09,  // Gerenciador -> Cliente
    ENVIO_CONFIG       = 0x0A,  // Cliente -> Gerenciador (limites min/max)
    CONF_CONFIG        = 0x0B,  // Gerenciador -> Cliente (ack de config)
};

// Categorias de componente (campos remetente/destinatario do header e tambem
// usadas no payload de requisicao/configuracao). Como existe apenas um
// componente de cada tipo na estufa, a categoria identifica unicamente o
// componente.
enum class Cat : uint8_t {
    // Sensores
    TEMP   = 0x54,  // 'T' temperatura interna
    UMID   = 0x55,  // 'U' umidade do solo
    CO2    = 0x4F,  // 'O' nivel de CO2
    // Atuadores
    AQUEC  = 0x41,  // 'A' aquecedor
    RESFR  = 0x52,  // 'R' resfriador
    IRRIG  = 0x53,  // 'S' sistema de irrigacao
    INJCO2 = 0x49,  // 'I' injetor de CO2
    // Demais
    GER    = 0x47,  // 'G' gerenciador (servidor)
    CLI    = 0x43,  // 'C' cliente
};

// Estrutura logica de uma mensagem ja decodificada. O payload e mantido como
// bytes crus; helpers abaixo convertem para/de tipos concretos.
struct Mensagem {
    Tipo    tipo;
    uint8_t remetente;    // valor de Cat
    uint8_t destinatario; // valor de Cat
    std::vector<uint8_t> payload;
};

// Tamanho fixo (em bytes) do payload para cada tipo de mensagem. E o que
// resolve o "framing" sobre TCP.
size_t payload_size(Tipo t);

// Serializa a mensagem para um buffer de bytes pronto para envio.
std::vector<uint8_t> serializar(const Mensagem& m);

// Decodifica um header de HEADER_SIZE bytes. Retorna true se o ID de protocolo
// for valido. Nao le payload (quem chama le payload_size(tipo) bytes depois).
bool desserializar_header(const uint8_t* header, Mensagem& out);

// ---- Helpers de payload (ordem de rede / big-endian para portabilidade) ----

// float <-> 4 bytes big-endian (via uint32_t + htonl/ntohl).
std::vector<uint8_t> float_para_bytes(float v);
float bytes_para_float(const uint8_t* p);

// Helpers de alto nivel para montar mensagens comuns.
Mensagem msg_simples(Tipo t, Cat de, Cat para);                 // payload vazio
Mensagem msg_leitura(Cat sensor, float valor);                  // ENVIO_LEITURA
Mensagem msg_comando(Cat de, Cat atuador, bool ligar);          // COMANDO_ATUACAO
Mensagem msg_conf_atuacao(Cat atuador, bool ligado);            // CONF_ATUACAO
Mensagem msg_requisicao(Cat sensor);                            // REQUISICAO_LEITURA
Mensagem msg_resposta(Cat sensor, float valor);                 // RESPOSTA_LEITURA
Mensagem msg_config(Cat sensor, float minimo, float maximo);    // ENVIO_CONFIG

// Nome legivel da categoria, para logs.
const char* nome_cat(uint8_t c);
// Nome legivel do tipo, para logs.
const char* nome_tipo(Tipo t);

} // namespace proto

#endif // PROTOCOL_HPP
