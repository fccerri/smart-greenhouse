// cliente.cpp — Processo Cliente (interface do operador).
//
// Uso: ./cliente <ip> <porta>
//
// Fluxo (requisito 4 / Monitoramento):
//   conecta -> CONEXAO -> espera CONF_CONEXAO -> menu interativo:
//     1) requisitar a ultima leitura de um sensor (REQUISICAO_LEITURA);
//     2) configurar limites min/max de uma categoria (ENVIO_CONFIG);
//     0) sair (DESCONEXAO).

#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

#include "net.hpp"
#include "protocol.hpp"

using namespace proto;

namespace {

// Le uma categoria de sensor a partir de uma letra digitada (T/U/O).
bool ler_sensor(Cat& out) {
    std::cout << "  Sensor (T=temperatura, U=umidade, O=CO2): ";
    std::string s;
    std::getline(std::cin, s);
    if (s.empty()) return false;
    switch (s[0]) {
        case 'T': case 't': out = Cat::TEMP; return true;
        case 'U': case 'u': out = Cat::UMID; return true;
        case 'O': case 'o': out = Cat::CO2;  return true;
        default:
            std::cout << "  Sensor invalido.\n";
            return false;
    }
}

void requisitar_leitura(int fd) {
    Cat sensor;
    if (!ler_sensor(sensor)) return;

    net::enviar_msg(fd, msg_requisicao(sensor));
    Mensagem resp;
    if (!net::receber_msg(fd, resp) || resp.tipo != Tipo::RESPOSTA_LEITURA) {
        std::cout << "  Sem resposta valida do Gerenciador.\n";
        return;
    }
    uint8_t sid = resp.payload[0];
    float valor = bytes_para_float(resp.payload.data() + 1);
    std::cout << "  Ultima leitura de " << nome_cat(sid) << " = " << valor << "\n";
}

void configurar_limites(int fd) {
    Cat sensor;
    if (!ler_sensor(sensor)) return;

    float lo, hi;
    std::cout << "  Limite minimo: ";
    if (!(std::cin >> lo)) { std::cin.clear(); std::cin.ignore(10000, '\n'); return; }
    std::cout << "  Limite maximo: ";
    if (!(std::cin >> hi)) { std::cin.clear(); std::cin.ignore(10000, '\n'); return; }
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    net::enviar_msg(fd, msg_config(sensor, lo, hi));
    Mensagem resp;
    if (!net::receber_msg(fd, resp) || resp.tipo != Tipo::CONF_CONFIG) {
        std::cout << "  Sem confirmacao de configuracao.\n";
        return;
    }
    std::cout << "  Configuracao confirmada para " << nome_cat(static_cast<uint8_t>(sensor))
              << " (min=" << lo << ", max=" << hi << ").\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Uso: " << argv[0] << " <ip> <porta>\n";
        return 1;
    }
    std::string ip = argv[1];
    uint16_t porta = static_cast<uint16_t>(std::atoi(argv[2]));

    int fd = net::conectar(ip, porta);
    if (fd < 0) return 1;

    // Handshake.
    net::enviar_msg(fd, msg_simples(Tipo::CONEXAO, Cat::CLI, Cat::GER));
    Mensagem resp;
    if (!net::receber_msg(fd, resp) || resp.tipo != Tipo::CONF_CONEXAO) {
        std::cerr << "[CLIENTE] handshake falhou.\n";
        ::close(fd);
        return 1;
    }
    std::cout << "[CLIENTE] conectado ao Gerenciador.\n";

    // Menu interativo.
    while (true) {
        std::cout << "\n=== Estufa Inteligente — Cliente ===\n"
                  << " 1) Requisitar leitura de um sensor\n"
                  << " 2) Configurar limites (min/max) de uma categoria\n"
                  << " 0) Sair\n"
                  << "Opcao: ";
        std::string opc;
        if (!std::getline(std::cin, opc)) break;

        if (opc == "1")      requisitar_leitura(fd);
        else if (opc == "2") configurar_limites(fd);
        else if (opc == "0") break;
        else std::cout << "Opcao invalida.\n";
    }

    // Desconexao.
    net::enviar_msg(fd, msg_simples(Tipo::DESCONEXAO, Cat::CLI, Cat::GER));
    Mensagem fim;
    if (net::receber_msg(fd, fim) && fim.tipo == Tipo::CONF_DESCONEXAO) {
        std::cout << "[CLIENTE] desconexao confirmada. Ate logo.\n";
    }
    ::close(fd);
    return 0;
}
