// cliente.cpp - Processo Cliente (interface do operador).
//
// Uso: ./cliente <ip> <porta>
//
// Fluxo (Monitoramento):
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


namespace {

// Le uma categoria de sensor a partir de uma letra digitada (T/U/O).
bool ler_sensor(proto::Cat& out) {
    std::cout << "  Sensor (T=temperatura, U=umidade, O=CO2): ";
    std::string s;
    std::getline(std::cin, s);
    if (s.empty()) return false;
    switch (s[0]) {
        case 'T': case 't': out = proto::Cat::TEMP; return true;
        case 'U': case 'u': out = proto::Cat::UMID; return true;
        case 'O': case 'o': out = proto::Cat::CO2;  return true;
        default:
            std::cout << "  Sensor invalido.\n";
            return false;
    }
}

void requisitar_leitura(int fd) {
    proto::Cat sensor;
    if (!ler_sensor(sensor)) return;

    net::enviar_msg(fd, proto::msg_requisicao(sensor));
    proto::Mensagem resp;
    if (!net::receber_msg(fd, resp) || resp.tipo != proto::Tipo::RESPOSTA_LEITURA) {
        std::cout << "  Sem resposta valida do Gerenciador.\n";
        return;
    }
    uint8_t sid = resp.payload[0];
    float valor = proto::bytes_para_float(resp.payload.data() + 1);
    std::cout << "  Ultima leitura de " << proto::nome_cat(sid) << " = " << valor << "\n";
}

void configurar_limites(int fd) {
    proto::Cat sensor;
    if (!ler_sensor(sensor)) return;

    float lo, hi;
    std::cout << "  Limite minimo: ";
    if (!(std::cin >> lo)) { std::cin.clear(); std::cin.ignore(10000, '\n'); return; }
    std::cout << "  Limite maximo: ";
    if (!(std::cin >> hi)) { std::cin.clear(); std::cin.ignore(10000, '\n'); return; }
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    net::enviar_msg(fd, proto::msg_config(sensor, lo, hi));
    proto::Mensagem resp;
    if (!net::receber_msg(fd, resp) || resp.tipo != proto::Tipo::CONF_CONFIG) {
        std::cout << "  Sem confirmacao de configuracao.\n";
        return;
    }
    std::cout << "  Configuracao confirmada para " << proto::nome_cat(static_cast<uint8_t>(sensor))
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
    net::enviar_msg(fd, proto::msg_simples(proto::Tipo::CONEXAO, proto::Cat::CLI, proto::Cat::GER));
    proto::Mensagem resp;
    if (!net::receber_msg(fd, resp) || resp.tipo != proto::Tipo::CONF_CONEXAO) {
        std::cerr << "[CLIENTE] handshake falhou.\n";
        ::close(fd);
        return 1;
    }
    std::cout << "[CLIENTE] conectado ao Gerenciador.\n";

    // Menu interativo.
    while (true) {
        std::cout << "\n=== Estufa Inteligente - Cliente ===\n"
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
    net::enviar_msg(fd, proto::msg_simples(proto::Tipo::DESCONEXAO, proto::Cat::CLI, proto::Cat::GER));
    proto::Mensagem fim;
    if (net::receber_msg(fd, fim) && fim.tipo == proto::Tipo::CONF_DESCONEXAO) {
        std::cout << "[CLIENTE] desconexao confirmada. Ate logo.\n";
    }
    ::close(fd);
    return 0;
}
