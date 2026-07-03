// gerenciador.cpp - Servidor da estufa (Gerenciador).
//
// Responsabilidades:
//   - aceitar conexao de sensores e atuadores (e do cliente);
//   - receber leituras de todos os sensores e armazenar o ultimo valor;
//   - ligar/desligar atuadores quando uma leitura sai dos limites min/max
//     configurados (controle bang-bang com histerese pela propria banda);
//   - responder ao Cliente a ultima leitura de cada sensor.
//
// Modelo de concorrencia: uma thread por conexao (accept -> std::thread).
// O estado compartilhado e protegido por um unico std::mutex.
//
// Simulacao acoplada: ao acionar um atuador, o Gerenciador reenvia o mesmo
// COMANDO_ATUACAO para o sensor afetado, fechando a malha - assim o efeito do
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
proto::Cat sensor_afetado(proto::Cat atuador) {
    switch (atuador) {
        case proto::Cat::AQUEC:
        case proto::Cat::RESFR:  return proto::Cat::TEMP;
        case proto::Cat::IRRIG:  return proto::Cat::UMID;
        case proto::Cat::INJCO2: return proto::Cat::CO2;
        default:                 return proto::Cat::TEMP;
    }
}

// Define o estado de um atuador. Deve ser chamada com g_mtx travado.
// So envia comando se o estado desejado for diferente do atual (idempotencia,
// evita "spam" de mensagens nos limites). Reenvia o comando ao sensor afetado
// para fechar a malha de simulacao.
void definir_atuador(proto::Cat atuador, bool ligar) {
    uint8_t key = static_cast<uint8_t>(atuador);
    auto it = g_estado_atuador.find(key);
    if (it != g_estado_atuador.end() && it->second == ligar) {
        return;  // ja esta no estado desejado
    }
    g_estado_atuador[key] = ligar;

    // Envia ao atuador, se conectado.
    auto fa = g_fd_atuador.find(key);
    if (fa != g_fd_atuador.end()) {
        net::enviar_msg(fa->second, proto::msg_comando(proto::Cat::GER, atuador, ligar));
        logln("[GER] -> ", proto::nome_cat(key), " : COMANDO_ATUACAO ",
              (ligar ? "LIGAR" : "DESLIGAR"));
    }
    // Feedback para o sensor afetado (fecha a malha da simulacao).
    proto::Cat sa = sensor_afetado(atuador);
    auto fs = g_fd_sensor.find(static_cast<uint8_t>(sa));
    if (fs != g_fd_sensor.end()) {
        net::enviar_msg(fs->second, proto::msg_comando(proto::Cat::GER, atuador, ligar));
    }
}

// Logica de controle. Chamada a cada leitura recebida, com g_mtx
// travado. Compara a leitura com os limites e aciona os atuadores.
void controlar(proto::Cat sensor, float valor) {
    auto it = g_config.find(static_cast<uint8_t>(sensor));
    if (it == g_config.end() || !it->second.configurado) {
        return;  // sem limites configurados ainda -> nao atua
    }
    float lo = it->second.minimo;
    float hi = it->second.maximo;

    switch (sensor) {
        case proto::Cat::TEMP: {
            // Histerese com ponto medio: liga nos limites, desliga no centro
            // da banda. Margem de seguranca = (hi - lo) / 2 antes de religar.
            float meio = (lo + hi) / 2.0f;
            if (valor > hi) {                 // quente demais
                definir_atuador(proto::Cat::RESFR, true);
                definir_atuador(proto::Cat::AQUEC, false);
            } else if (valor < lo) {          // frio demais
                definir_atuador(proto::Cat::AQUEC, true);
                definir_atuador(proto::Cat::RESFR, false);
            } else if (valor <= meio) {       // resfriou o suficiente
                definir_atuador(proto::Cat::RESFR, false);
            } else {                          // aqueceu o suficiente
                definir_atuador(proto::Cat::AQUEC, false);
            }
            break;
        }
        case proto::Cat::UMID: {
            // Histerese com ponto medio: liga abaixo do min, desliga no centro.
            float meio = (lo + hi) / 2.0f;
            if (valor < lo)           definir_atuador(proto::Cat::IRRIG, true);
            else if (valor >= meio)   definir_atuador(proto::Cat::IRRIG, false);
            break;
        }
        case proto::Cat::CO2: {
            // Histerese com ponto medio: liga abaixo do min, desliga no centro.
            float meio = (lo + hi) / 2.0f;
            if (valor < lo)           definir_atuador(proto::Cat::INJCO2, true);
            else if (valor >= meio)   definir_atuador(proto::Cat::INJCO2, false);
            break;
        }
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
    proto::Mensagem m;

    // 1) Handshake: primeira mensagem deve ser CONEXAO.
    if (!net::receber_msg(fd, m) || m.tipo != proto::Tipo::CONEXAO) {
        std::cerr << "[GER] handshake invalido, fechando conexao.\n";
        ::close(fd);
        return;
    }
    uint8_t cat = m.remetente;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        proto::Cat c = static_cast<proto::Cat>(cat);
        if (c == proto::Cat::TEMP || c == proto::Cat::UMID || c == proto::Cat::CO2)
            g_fd_sensor[cat] = fd;
        else if (c == proto::Cat::AQUEC || c == proto::Cat::RESFR ||
                 c == proto::Cat::IRRIG || c == proto::Cat::INJCO2)
            g_fd_atuador[cat] = fd;
        // Cliente nao precisa de registro de fd.
    }
    net::enviar_msg(fd, proto::msg_simples(proto::Tipo::CONF_CONEXAO, proto::Cat::GER,
                                    static_cast<proto::Cat>(cat)));
    logln("[GER] <- ", proto::nome_cat(cat), " : CONEXAO  (respondido CONF_CONEXAO)");

    // 2) Laco principal de mensagens.
    while (net::receber_msg(fd, m)) {
        switch (m.tipo) {
            case proto::Tipo::ENVIO_LEITURA: {
                float v = proto::bytes_para_float(m.payload.data());
                std::lock_guard<std::mutex> lk(g_mtx);
                g_ultima_leitura[m.remetente] = v;
                logln("[GER] <- ", proto::nome_cat(m.remetente), " : leitura = ", v);
                controlar(static_cast<proto::Cat>(m.remetente), v);
                break;
            }
            case proto::Tipo::CONF_ATUACAO: {
                bool ligado = m.payload[0] != 0;
                logln("[GER] <- ", proto::nome_cat(m.remetente), " : CONF_ATUACAO status=",
                      (ligado ? "LIGADO" : "DESLIGADO"));
                break;
            }
            case proto::Tipo::REQUISICAO_LEITURA: {
                uint8_t sensor = m.payload[0];
                float v = 0.0f;
                {
                    std::lock_guard<std::mutex> lk(g_mtx);
                    auto it = g_ultima_leitura.find(sensor);
                    if (it != g_ultima_leitura.end()) v = it->second;
                }
                net::enviar_msg(fd, proto::msg_resposta(static_cast<proto::Cat>(sensor), v));
                logln("[GER] <- Cliente : REQUISICAO_LEITURA de ",
                      proto::nome_cat(sensor), " -> respondido ", v);
                break;
            }
            case proto::Tipo::ENVIO_CONFIG: {
                uint8_t sensor = m.payload[0];
                float lo = proto::bytes_para_float(m.payload.data() + 1);
                float hi = proto::bytes_para_float(m.payload.data() + 5);
                {
                    std::lock_guard<std::mutex> lk(g_mtx);
                    g_config[sensor] = Limites{lo, hi, true};
                }
                net::enviar_msg(fd, proto::msg_simples(proto::Tipo::CONF_CONFIG,
                                                proto::Cat::GER, proto::Cat::CLI));
                logln("[GER] <- Cliente : CONFIG ", proto::nome_cat(sensor),
                      " min=", lo, " max=", hi, "  (respondido CONF_CONFIG)");
                break;
            }
            case proto::Tipo::DESCONEXAO: {
                net::enviar_msg(fd, proto::msg_simples(proto::Tipo::CONF_DESCONEXAO, proto::Cat::GER,
                                                static_cast<proto::Cat>(m.remetente)));
                logln("[GER] <- ", proto::nome_cat(m.remetente),
                      " : DESCONEXAO (respondido CONF_DESCONEXAO)");
                goto fim;
            }
            default:
                logln("[GER] tipo inesperado: ", proto::nome_tipo(m.tipo),
                      " de ", proto::nome_cat(m.remetente));
                break;
        }
    }

fim:
    logln("[GER] conexao encerrada (", proto::nome_cat(m.remetente), ")");
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
