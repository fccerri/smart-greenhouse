// net.hpp — Helpers de sockets TCP (camada de transporte) e envio/recepcao de
// mensagens do protocolo "ES".
//
// Toda a logica de "framing" (descobrir onde uma mensagem termina dentro do
// fluxo TCP) esta concentrada em receber_msg().

#ifndef NET_HPP
#define NET_HPP

#include <cstdint>
#include <string>

#include "protocol.hpp"

namespace net {

// Envia exatamente n bytes (loop ate enviar tudo). Retorna false em erro.
bool send_all(int fd, const uint8_t* buf, size_t n);

// Recebe exatamente n bytes (loop ate completar). Retorna false se a conexao
// fechar antes de completar ou em erro.
bool recv_all(int fd, uint8_t* buf, size_t n);

// Serializa e envia uma mensagem completa. Retorna false em erro.
bool enviar_msg(int fd, const proto::Mensagem& m);

// Recebe uma mensagem completa: le 5 bytes de header, deduz o tamanho do
// payload pelo Tipo e le exatamente esses bytes. Retorna false se a conexao
// fechou ou o header for invalido.
bool receber_msg(int fd, proto::Mensagem& out);

// Cria um socket TCP cliente conectado a ip:porta. Retorna fd ou -1.
int conectar(const std::string& ip, uint16_t porta);

// Cria um socket TCP servidor (bind + listen) na porta dada. Retorna fd ou -1.
int criar_servidor(uint16_t porta);

} // namespace net

#endif // NET_HPP
