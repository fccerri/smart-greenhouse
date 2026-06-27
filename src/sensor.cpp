// sensor.cpp - Processo de Sensor (parametrizado pela categoria).
//
// Uso: ./sensor <T|U|O> <ip> <porta>
//   T = temperatura interna, U = umidade do solo, O = nivel de CO2.
//
// Fluxo (requisitos 1.1-1.3):
//   conecta -> CONEXAO -> espera CONF_CONEXAO -> envia ENVIO_LEITURA a cada 1s.
//
// Simulacao acoplada: uma thread receptora escuta os COMANDO_ATUACAO que o
// Gerenciador reenvia (feedback). O valor simulado evolui com uma tendencia
// natural + o efeito dos atuadores ligados, deixando visivel a malha de
// controle (req 3.3) nas leituras seguintes.

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>

#include "net.hpp"
#include "protocol.hpp"

using namespace proto;

namespace {

std::mutex g_mtx;
std::map<uint8_t, bool> g_atuador_ativo; // categoria atuador -> ligado?
std::atomic<bool> g_rodando{true};

bool ativo(Cat atuador) {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_atuador_ativo.find(static_cast<uint8_t>(atuador));
    return it != g_atuador_ativo.end() && it->second;
}

// Thread que recebe os comandos de atuacao reenviados pelo Gerenciador.
void escutar_feedback(int fd) {
    Mensagem m;
    while (g_rodando && net::receber_msg(fd, m)) {
        if (m.tipo == Tipo::COMANDO_ATUACAO) {
            // O atuador alvo vem em destinatario (remetente e o Gerenciador).
            bool ligar = m.payload[0] != 0;
            std::lock_guard<std::mutex> lk(g_mtx);
            g_atuador_ativo[m.destinatario] = ligar;
        }
    }
    g_rodando = false;
}

// Calcula o proximo valor simulado conforme a categoria e os atuadores ativos.
float evoluir(Cat categoria, float valor, std::mt19937& rng) {
    std::uniform_real_distribution<float> ruido(-0.15f, 0.15f);
    float delta = ruido(rng);
    switch (categoria) {
        case Cat::TEMP:  // tende a esquentar; resfriador esfria, aquecedor esquenta
            delta += 0.4f;
            if (ativo(Cat::RESFR)) delta -= 1.4f;
            if (ativo(Cat::AQUEC)) delta += 1.4f;
            break;
        case Cat::UMID:  // solo seca naturalmente; irrigacao umedece
            delta += -0.6f;
            if (ativo(Cat::IRRIG)) delta += 1.6f;
            break;
        case Cat::CO2:   // plantas consomem CO2; injetor repoe
            delta += -3.0f;
            if (ativo(Cat::INJCO2)) delta += 8.0f;
            break;
        default: break;
    }
    return valor + delta;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Uso: " << argv[0] << " <T|U|O> <ip> <porta>\n";
        return 1;
    }

    Cat categoria;
    float valor_inicial;
    switch (argv[1][0]) {
        case 'T': categoria = Cat::TEMP; valor_inicial = 25.0f;  break;
        case 'U': categoria = Cat::UMID; valor_inicial = 50.0f;  break;
        case 'O': categoria = Cat::CO2;  valor_inicial = 400.0f; break;
        default:
            std::cerr << "Categoria de sensor invalida: use T, U ou O.\n";
            return 1;
    }
    std::string ip = argv[2];
    uint16_t porta = static_cast<uint16_t>(std::atoi(argv[3]));

    std::cout << std::unitbuf; // descarrega cada log na hora

    int fd = net::conectar(ip, porta);
    if (fd < 0) return 1;

    // Handshake.
    net::enviar_msg(fd, msg_simples(Tipo::CONEXAO, categoria, Cat::GER));
    Mensagem resp;
    if (!net::receber_msg(fd, resp) || resp.tipo != Tipo::CONF_CONEXAO) {
        std::cerr << "[SENSOR] handshake falhou.\n";
        ::close(fd);
        return 1;
    }
    std::cout << "[" << nome_cat(static_cast<uint8_t>(categoria))
              << "] conectado e identificado.\n";

    // Thread que escuta o feedback de atuacao.
    std::thread receptor(escutar_feedback, fd);

    // Laco de envio de leituras a cada 1s.
    std::mt19937 rng(std::random_device{}());
    float valor = valor_inicial;
    while (g_rodando) {
        valor = evoluir(categoria, valor, rng);
        if (!net::enviar_msg(fd, msg_leitura(categoria, valor))) break;
        std::cout << "[" << nome_cat(static_cast<uint8_t>(categoria))
                  << "] enviou leitura = " << valor << "\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    g_rodando = false;
    ::close(fd);
    if (receptor.joinable()) receptor.join();
    return 0;
}
