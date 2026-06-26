// atuador.cpp — Processo de Atuador (parametrizado pela categoria).
//
// Uso: ./atuador <A|R|S|I> <ip> <porta>
//   A = aquecedor, R = resfriador, S = sistema de irrigacao, I = injetor de CO2.
//
// Fluxo (requisitos 2.1-2.3):
//   conecta -> CONEXAO -> espera CONF_CONEXAO -> aguarda COMANDO_ATUACAO,
//   liga/desliga o estado interno e responde CONF_ATUACAO com o status atual.

#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <string>

#include "net.hpp"
#include "protocol.hpp"

using namespace proto;

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Uso: " << argv[0] << " <A|R|S|I> <ip> <porta>\n";
        return 1;
    }

    Cat categoria;
    switch (argv[1][0]) {
        case 'A': categoria = Cat::AQUEC;  break;
        case 'R': categoria = Cat::RESFR;  break;
        case 'S': categoria = Cat::IRRIG;  break;
        case 'I': categoria = Cat::INJCO2; break;
        default:
            std::cerr << "Categoria de atuador invalida: use A, R, S ou I.\n";
            return 1;
    }
    std::string ip = argv[2];
    uint16_t porta = static_cast<uint16_t>(std::atoi(argv[3]));

    std::cout << std::unitbuf;  // descarrega cada log na hora

    int fd = net::conectar(ip, porta);
    if (fd < 0) return 1;

    // Handshake.
    net::enviar_msg(fd, msg_simples(Tipo::CONEXAO, categoria, Cat::GER));
    Mensagem resp;
    if (!net::receber_msg(fd, resp) || resp.tipo != Tipo::CONF_CONEXAO) {
        std::cerr << "[ATUADOR] handshake falhou.\n";
        ::close(fd);
        return 1;
    }
    const char* nome = nome_cat(static_cast<uint8_t>(categoria));
    std::cout << "[" << nome << "] conectado e identificado. Aguardando comandos...\n";

    // Laco de comandos.
    bool ligado = false;
    Mensagem m;
    while (net::receber_msg(fd, m)) {
        if (m.tipo == Tipo::COMANDO_ATUACAO) {
            ligado = m.payload[0] != 0;
            std::cout << "[" << nome << "] COMANDO recebido -> "
                      << (ligado ? "LIGADO" : "DESLIGADO") << "\n";
            net::enviar_msg(fd, msg_conf_atuacao(categoria, ligado));
        } else if (m.tipo == Tipo::DESCONEXAO) {
            net::enviar_msg(fd, msg_simples(Tipo::CONF_DESCONEXAO, categoria, Cat::GER));
            break;
        }
    }

    std::cout << "[" << nome << "] conexao encerrada.\n";
    ::close(fd);
    return 0;
}
