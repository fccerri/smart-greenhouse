// protocol.cpp - Implementacao da (de)serializacao do protocolo "ES".

#include "protocol.hpp"

#include <arpa/inet.h>  // htonl, ntohl
#include <cstring>      // memcpy

namespace proto {

size_t payload_size(Tipo t) {
    switch (t) {
        case Tipo::CONEXAO:
        case Tipo::CONF_CONEXAO:
        case Tipo::DESCONEXAO:
        case Tipo::CONF_DESCONEXAO:
        case Tipo::CONF_CONFIG:
            return 0;
        case Tipo::COMANDO_ATUACAO:
        case Tipo::CONF_ATUACAO:
            return 1;  // booleano (0/1)
        case Tipo::ENVIO_LEITURA:
            return 4;  // float
        case Tipo::REQUISICAO_LEITURA:
            return 1;  // id do sensor
        case Tipo::RESPOSTA_LEITURA:
            return 5;  // id do sensor (1) + float (4)
        case Tipo::ENVIO_CONFIG:
            return 9;  // id da categoria (1) + min (4) + max (4)
    }
    return 0;
}

std::vector<uint8_t> serializar(const Mensagem& m) {
    std::vector<uint8_t> buf;
    buf.reserve(HEADER_SIZE + m.payload.size());
    buf.push_back(PROTO_ID_0);
    buf.push_back(PROTO_ID_1);
    buf.push_back(static_cast<uint8_t>(m.tipo));
    buf.push_back(m.remetente);
    buf.push_back(m.destinatario);
    buf.insert(buf.end(), m.payload.begin(), m.payload.end());
    return buf;
}

bool desserializar_header(const uint8_t* h, Mensagem& out) {
    if (h[0] != PROTO_ID_0 || h[1] != PROTO_ID_1) {
        return false;  // ID de protocolo invalido
    }
    out.tipo         = static_cast<Tipo>(h[2]);
    out.remetente    = h[3];
    out.destinatario = h[4];
    out.payload.clear();
    return true;
}

std::vector<uint8_t> float_para_bytes(float v) {
    uint32_t net;
    std::memcpy(&net, &v, sizeof(net));   // reinterpreta os bits do float
    net = htonl(net);                     // ordem de rede (big-endian)
    std::vector<uint8_t> out(4);
    std::memcpy(out.data(), &net, 4);
    return out;
}

float bytes_para_float(const uint8_t* p) {
    uint32_t net;
    std::memcpy(&net, p, 4);
    net = ntohl(net);
    float v;
    std::memcpy(&v, &net, sizeof(v));
    return v;
}

Mensagem msg_simples(Tipo t, Cat de, Cat para) {
    return Mensagem{t, static_cast<uint8_t>(de), static_cast<uint8_t>(para), {}};
}

Mensagem msg_leitura(Cat sensor, float valor) {
    Mensagem m{Tipo::ENVIO_LEITURA, static_cast<uint8_t>(sensor),
               static_cast<uint8_t>(Cat::GER), {}};
    m.payload = float_para_bytes(valor);
    return m;
}

Mensagem msg_comando(Cat de, Cat atuador, bool ligar) {
    Mensagem m{Tipo::COMANDO_ATUACAO, static_cast<uint8_t>(de),
               static_cast<uint8_t>(atuador), {}};
    m.payload.push_back(ligar ? 1 : 0);
    return m;
}

Mensagem msg_conf_atuacao(Cat atuador, bool ligado) {
    Mensagem m{Tipo::CONF_ATUACAO, static_cast<uint8_t>(atuador),
               static_cast<uint8_t>(Cat::GER), {}};
    m.payload.push_back(ligado ? 1 : 0);
    return m;
}

Mensagem msg_requisicao(Cat sensor) {
    Mensagem m{Tipo::REQUISICAO_LEITURA, static_cast<uint8_t>(Cat::CLI),
               static_cast<uint8_t>(Cat::GER), {}};
    m.payload.push_back(static_cast<uint8_t>(sensor));
    return m;
}

Mensagem msg_resposta(Cat sensor, float valor) {
    Mensagem m{Tipo::RESPOSTA_LEITURA, static_cast<uint8_t>(Cat::GER),
               static_cast<uint8_t>(Cat::CLI), {}};
    m.payload.push_back(static_cast<uint8_t>(sensor));
    auto fb = float_para_bytes(valor);
    m.payload.insert(m.payload.end(), fb.begin(), fb.end());
    return m;
}

Mensagem msg_config(Cat sensor, float minimo, float maximo) {
    Mensagem m{Tipo::ENVIO_CONFIG, static_cast<uint8_t>(Cat::CLI),
               static_cast<uint8_t>(Cat::GER), {}};
    m.payload.push_back(static_cast<uint8_t>(sensor));
    auto lo = float_para_bytes(minimo);
    auto hi = float_para_bytes(maximo);
    m.payload.insert(m.payload.end(), lo.begin(), lo.end());
    m.payload.insert(m.payload.end(), hi.begin(), hi.end());
    return m;
}

const char* nome_cat(uint8_t c) {
    switch (static_cast<Cat>(c)) {
        case Cat::TEMP:   return "Sensor-Temperatura";
        case Cat::UMID:   return "Sensor-Umidade";
        case Cat::CO2:    return "Sensor-CO2";
        case Cat::AQUEC:  return "Atuador-Aquecedor";
        case Cat::RESFR:  return "Atuador-Resfriador";
        case Cat::IRRIG:  return "Atuador-Irrigacao";
        case Cat::INJCO2: return "Atuador-InjetorCO2";
        case Cat::GER:    return "Gerenciador";
        case Cat::CLI:    return "Cliente";
    }
    return "Desconhecido";
}

const char* nome_tipo(Tipo t) {
    switch (t) {
        case Tipo::CONEXAO:            return "CONEXAO";
        case Tipo::CONF_CONEXAO:       return "CONF_CONEXAO";
        case Tipo::DESCONEXAO:         return "DESCONEXAO";
        case Tipo::CONF_DESCONEXAO:    return "CONF_DESCONEXAO";
        case Tipo::COMANDO_ATUACAO:    return "COMANDO_ATUACAO";
        case Tipo::CONF_ATUACAO:       return "CONF_ATUACAO";
        case Tipo::ENVIO_LEITURA:      return "ENVIO_LEITURA";
        case Tipo::REQUISICAO_LEITURA: return "REQUISICAO_LEITURA";
        case Tipo::RESPOSTA_LEITURA:   return "RESPOSTA_LEITURA";
        case Tipo::ENVIO_CONFIG:       return "ENVIO_CONFIG";
        case Tipo::CONF_CONFIG:        return "CONF_CONFIG";
    }
    return "TIPO_DESCONHECIDO";
}

} // namespace proto
