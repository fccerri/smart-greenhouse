// sensor.cpp - Processo de Sensor (parametrizado pela categoria).
//
// Uso: ./sensor <T|U|O> <ip> <porta>
//   T = temperatura interna, U = umidade do solo, O = nivel de CO2.
//
// Fluxo:
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
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>

#include "net.hpp"
#include "protocol.hpp"


namespace {

std::mutex g_mtx;
std::map<uint8_t, bool> g_atuador_ativo; // categoria atuador -> ligado?
std::atomic<bool> g_rodando{true};
std::atomic<bool> g_conexao_perdida_alertada{false};

void alertar_conexao_perdida(proto::Cat categoria) {
    if (!g_conexao_perdida_alertada.exchange(true)) {
        std::cerr << "[" << proto::nome_cat(static_cast<uint8_t>(categoria))
                  << "] ALERTA: conexao com o Gerenciador perdida!\n";
    }
}

bool ativo(proto::Cat atuador) {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_atuador_ativo.find(static_cast<uint8_t>(atuador));
    return it != g_atuador_ativo.end() && it->second;
}

// Thread que recebe os comandos de atuacao reenviados pelo Gerenciador.
void escutar_feedback(int fd, proto::Cat categoria) {
    const char* nome = proto::nome_cat(static_cast<uint8_t>(categoria));
    proto::Mensagem m;
    while (g_rodando && net::receber_msg(fd, m)) {
        if (m.tipo == proto::Tipo::COMANDO_ATUACAO) {
            // O atuador alvo vem em destinatario (remetente e o Gerenciador).
            bool ligar = m.payload[0] != 0;
            std::cout << "[" << nome << "] ALERTA: atuador "
                      << proto::nome_cat(m.destinatario) << " foi "
                      << (ligar ? "LIGADO" : "DESLIGADO")
                      << " pelo Gerenciador.\n";
            std::lock_guard<std::mutex> lk(g_mtx);
            g_atuador_ativo[m.destinatario] = ligar;
        }
    }
    if (g_rodando) {
        alertar_conexao_perdida(categoria);
    }
    g_rodando = false;
}

// Calcula o proximo valor simulado conforme a categoria e os atuadores ativos.
float evoluir(proto::Cat categoria, float valor, std::mt19937& rng) {
    std::uniform_real_distribution<float> ruido(-0.3f, 0.3f);
    float delta = ruido(rng);
    switch (categoria) {
        case proto::Cat::TEMP:  // tende a esquentar; resfriador esfria, aquecedor esquenta
            delta += 0.1f;
            if (ativo(proto::Cat::RESFR)) delta -= 0.6f;
            if (ativo(proto::Cat::AQUEC)) delta += 0.6f;
            break;
        case proto::Cat::UMID:  // solo seca naturalmente; irrigacao umedece
            delta += -0.3f;
            if (ativo(proto::Cat::IRRIG)) delta += 0.8f;
            break;
        case proto::Cat::CO2:   // plantas consomem CO2; injetor repoe
            delta += -1.5f;
            if (ativo(proto::Cat::INJCO2)) delta += 4.0f;
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

    proto::Cat categoria;
    float valor_inicial;
    switch (argv[1][0]) {
        case 'T': categoria = proto::Cat::TEMP; valor_inicial = 25.0f;  break;
        case 'U': categoria = proto::Cat::UMID; valor_inicial = 50.0f;  break;
        case 'O': categoria = proto::Cat::CO2;  valor_inicial = 400.0f; break;
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
    net::enviar_msg(fd, proto::msg_simples(proto::Tipo::CONEXAO, categoria, proto::Cat::GER));
    proto::Mensagem resp;
    if (!net::receber_msg(fd, resp) || resp.tipo != proto::Tipo::CONF_CONEXAO) {
        std::cerr << "[SENSOR] handshake falhou.\n";
        ::close(fd);
        return 1;
    }
    std::cout << "[" << proto::nome_cat(static_cast<uint8_t>(categoria))
              << "] conectado e identificado.\n";

    // Thread que escuta o feedback de atuacao.
    std::thread receptor(escutar_feedback, fd, categoria);

    // Laco de envio de leituras a cada 1s.
    std::mt19937 rng(std::random_device{}());
    float valor = valor_inicial;
    while (g_rodando) {
        valor = evoluir(categoria, valor, rng);
        if (!net::enviar_msg(fd, proto::msg_leitura(categoria, valor))) break;
        std::cout << "[" << proto::nome_cat(static_cast<uint8_t>(categoria))
                  << "] enviou leitura = " << valor << "\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    g_rodando = false;
    alertar_conexao_perdida(categoria);
    ::close(fd);
    if (receptor.joinable()) receptor.join();
    return 0;
}
