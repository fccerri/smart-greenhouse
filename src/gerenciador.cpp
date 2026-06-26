// gerenciador.cpp — Servidor da estufa (Gerenciador).
//
// Responsabilidades (requisitos 3.x):
//   3.1 aceitar conexao de sensores e atuadores (e do cliente);
//   3.2 receber leituras de todos os sensores e armazenar o ultimo valor;
//   3.3 ligar/desligar atuadores quando uma leitura sai dos limites min/max
//       configurados (controle bang-bang com histerese pela propria banda);
//   3.4 responder ao Cliente a ultima leitura de cada sensor.
//
// Modelo de concorrencia: uma thread por conexao (accept -> std::thread).
// O estado compartilhado e protegido por um unico std::mutex.
//
// Simulacao acoplada: ao acionar um atuador, o Gerenciador reenvia o mesmo
// COMANDO_ATUACAO para o sensor afetado, fechando a malha — assim o efeito do
// atuador aparece nas proximas leituras (ver sensor.cpp).

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#include "net.hpp"
#include "protocol.hpp"

using namespace proto;

namespace {

struct Limites { float minimo; float maximo; bool configurado = false; };

// Log atomico por linha: varias threads escrevem em stdout, entao cada linha e
// montada por completo e impressa sob lock para nao se intercalar.
std::mutex g_log_mtx;
template <typename... Args>
void logln(Args&&... args) {
    std::ostringstream os;
    (os << ... << args);
    std::lock_guard<std::mutex> lk(g_log_mtx);
    std::cout << os.str() << std::endl;
}

std::mutex g_mtx;                               // protege todo o estado abaixo
std::map<uint8_t, float>   g_ultima_leitura;    // categoria sensor -> ultimo valor
std::map<uint8_t, Limites> g_config;            // categoria sensor -> limites
std::map<uint8_t, int>     g_fd_sensor;         // categoria sensor -> socket fd
std::map<uint8_t, int>     g_fd_atuador;        // categoria atuador -> socket fd
std::map<uint8_t, bool>    g_estado_atuador;    // categoria atuador -> ligado?

// Qual sensor cada atuador influencia (para o feedback da simulacao).
Cat sensor_afetado(Cat atuador) {
    switch (atuador) {
        case Cat::AQUEC:
        case Cat::RESFR:  return Cat::TEMP;
        case Cat::IRRIG:  return Cat::UMID;
        case Cat::INJCO2: return Cat::CO2;
        default:          return Cat::TEMP;
    }
}

// Define o estado de um atuador. Deve ser chamada com g_mtx travado.
// So envia comando se o estado desejado for diferente do atual (idempotencia,
// evita "spam" de mensagens nos limites). Reenvia o comando ao sensor afetado
// para fechar a malha de simulacao.
void definir_atuador(Cat atuador, bool ligar) {
    uint8_t key = static_cast<uint8_t>(atuador);
    auto it = g_estado_atuador.find(key);
    if (it != g_estado_atuador.end() && it->second == ligar) {
        return;  // ja esta no estado desejado
    }
    g_estado_atuador[key] = ligar;

    // Envia ao atuador, se conectado.
    auto fa = g_fd_atuador.find(key);
    if (fa != g_fd_atuador.end()) {
        net::enviar_msg(fa->second, msg_comando(Cat::GER, atuador, ligar));
        logln("[GER] -> ", nome_cat(key), " : COMANDO_ATUACAO ",
              (ligar ? "LIGAR" : "DESLIGAR"));
    }
    // Feedback para o sensor afetado (fecha a malha da simulacao).
    Cat sa = sensor_afetado(atuador);
    auto fs = g_fd_sensor.find(static_cast<uint8_t>(sa));
    if (fs != g_fd_sensor.end()) {
        net::enviar_msg(fs->second, msg_comando(Cat::GER, atuador, ligar));
    }
}

// Logica de controle (req 3.3). Chamada a cada leitura recebida, com g_mtx
// travado. Compara a leitura com os limites e aciona os atuadores.
void controlar(Cat sensor, float valor) {
    auto it = g_config.find(static_cast<uint8_t>(sensor));
    if (it == g_config.end() || !it->second.configurado) {
        return;  // sem limites configurados ainda -> nao atua
    }
    float lo = it->second.minimo;
    float hi = it->second.maximo;

    switch (sensor) {
        case Cat::TEMP:
            // Aquecedor sobe a temperatura; resfriador a reduz.
            if (valor > hi) {                 // quente demais
                definir_atuador(Cat::RESFR, true);
                definir_atuador(Cat::AQUEC, false);
            } else if (valor < lo) {          // frio demais
                definir_atuador(Cat::AQUEC, true);
                definir_atuador(Cat::RESFR, false);
            } else {                          // dentro da banda -> desliga
                definir_atuador(Cat::AQUEC, false);
                definir_atuador(Cat::RESFR, false);
            }
            break;
        case Cat::UMID:
            // Histerese: liga ao cair abaixo do min, desliga ao passar do max.
            if (valor < lo)      definir_atuador(Cat::IRRIG, true);
            else if (valor > hi) definir_atuador(Cat::IRRIG, false);
            break;
        case Cat::CO2:
            if (valor < lo)      definir_atuador(Cat::INJCO2, true);
            else if (valor > hi) definir_atuador(Cat::INJCO2, false);
            break;
        default:
            break;
    }
}

// Remove um fd dos registros quando a conexao cai.
void desregistrar(int fd) {
    std::lock_guard<std::mutex> lk(g_mtx);
    for (auto it = g_fd_sensor.begin(); it != g_fd_sensor.end();) {
        if (it->second == fd) it = g_fd_sensor.erase(it); else ++it;
    }
    for (auto it = g_fd_atuador.begin(); it != g_fd_atuador.end();) {
        if (it->second == fd) it = g_fd_atuador.erase(it); else ++it;
    }
}

// Trata uma conexao do inicio ao fim (handshake + laco de mensagens).
void tratar_conexao(int fd) {
    Mensagem m;

    // 1) Handshake: primeira mensagem deve ser CONEXAO.
    if (!net::receber_msg(fd, m) || m.tipo != Tipo::CONEXAO) {
        std::cerr << "[GER] handshake invalido, fechando conexao.\n";
        ::close(fd);
        return;
    }
    uint8_t cat = m.remetente;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        Cat c = static_cast<Cat>(cat);
        if (c == Cat::TEMP || c == Cat::UMID || c == Cat::CO2)
            g_fd_sensor[cat] = fd;
        else if (c == Cat::AQUEC || c == Cat::RESFR ||
                 c == Cat::IRRIG || c == Cat::INJCO2)
            g_fd_atuador[cat] = fd;
        // Cliente nao precisa de registro de fd.
    }
    net::enviar_msg(fd, msg_simples(Tipo::CONF_CONEXAO, Cat::GER,
                                    static_cast<Cat>(cat)));
    logln("[GER] <- ", nome_cat(cat), " : CONEXAO  (respondido CONF_CONEXAO)");

    // 2) Laco principal de mensagens.
    while (net::receber_msg(fd, m)) {
        switch (m.tipo) {
            case Tipo::ENVIO_LEITURA: {
                float v = bytes_para_float(m.payload.data());
                std::lock_guard<std::mutex> lk(g_mtx);
                g_ultima_leitura[m.remetente] = v;
                logln("[GER] <- ", nome_cat(m.remetente), " : leitura = ", v);
                controlar(static_cast<Cat>(m.remetente), v);
                break;
            }
            case Tipo::CONF_ATUACAO: {
                bool ligado = m.payload[0] != 0;
                logln("[GER] <- ", nome_cat(m.remetente), " : CONF_ATUACAO status=",
                      (ligado ? "LIGADO" : "DESLIGADO"));
                break;
            }
            case Tipo::REQUISICAO_LEITURA: {
                uint8_t sensor = m.payload[0];
                float v = 0.0f;
                {
                    std::lock_guard<std::mutex> lk(g_mtx);
                    auto it = g_ultima_leitura.find(sensor);
                    if (it != g_ultima_leitura.end()) v = it->second;
                }
                net::enviar_msg(fd, msg_resposta(static_cast<Cat>(sensor), v));
                logln("[GER] <- Cliente : REQUISICAO_LEITURA de ",
                      nome_cat(sensor), " -> respondido ", v);
                break;
            }
            case Tipo::ENVIO_CONFIG: {
                uint8_t sensor = m.payload[0];
                float lo = bytes_para_float(m.payload.data() + 1);
                float hi = bytes_para_float(m.payload.data() + 5);
                {
                    std::lock_guard<std::mutex> lk(g_mtx);
                    g_config[sensor] = Limites{lo, hi, true};
                }
                net::enviar_msg(fd, msg_simples(Tipo::CONF_CONFIG,
                                                Cat::GER, Cat::CLI));
                logln("[GER] <- Cliente : CONFIG ", nome_cat(sensor),
                      " min=", lo, " max=", hi, "  (respondido CONF_CONFIG)");
                break;
            }
            case Tipo::DESCONEXAO: {
                net::enviar_msg(fd, msg_simples(Tipo::CONF_DESCONEXAO, Cat::GER,
                                                static_cast<Cat>(m.remetente)));
                logln("[GER] <- ", nome_cat(m.remetente),
                      " : DESCONEXAO (respondido CONF_DESCONEXAO)");
                goto fim;
            }
            default:
                logln("[GER] tipo inesperado: ", nome_tipo(m.tipo),
                      " de ", nome_cat(m.remetente));
                break;
        }
    }

fim:
    logln("[GER] conexao encerrada (", nome_cat(m.remetente), ")");
    desregistrar(fd);
    ::close(fd);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Uso: " << argv[0] << " <porta>\n";
        return 1;
    }
    uint16_t porta = static_cast<uint16_t>(std::atoi(argv[1]));

    std::cout << std::unitbuf;  // descarrega cada log na hora (util ao redirecionar)

    int srv = net::criar_servidor(porta);
    if (srv < 0) return 1;
    std::cout << "[GER] Gerenciador ouvindo na porta " << porta << "\n";

    while (true) {
        int cli = ::accept(srv, nullptr, nullptr);
        if (cli < 0) continue;
        std::thread(tratar_conexao, cli).detach();
    }
    // (servidor roda indefinidamente; encerrar com Ctrl+C)
}
