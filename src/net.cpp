// net.cpp — Implementacao dos helpers de socket TCP.

#include "net.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

namespace net {

bool send_all(int fd, const uint8_t* buf, size_t n) {
    size_t enviados = 0;
    while (enviados < n) {
        ssize_t r = ::send(fd, buf + enviados, n - enviados, 0);
        if (r <= 0) {
            if (r < 0 && errno == EINTR) continue;  // interrompido por sinal
            return false;
        }
        enviados += static_cast<size_t>(r);
    }
    return true;
}

bool recv_all(int fd, uint8_t* buf, size_t n) {
    size_t recebidos = 0;
    while (recebidos < n) {
        ssize_t r = ::recv(fd, buf + recebidos, n - recebidos, 0);
        if (r == 0) return false;                    // conexao fechada
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        recebidos += static_cast<size_t>(r);
    }
    return true;
}

bool enviar_msg(int fd, const proto::Mensagem& m) {
    std::vector<uint8_t> buf = proto::serializar(m);
    return send_all(fd, buf.data(), buf.size());
}

bool receber_msg(int fd, proto::Mensagem& out) {
    uint8_t header[proto::HEADER_SIZE];
    if (!recv_all(fd, header, proto::HEADER_SIZE)) return false;
    if (!proto::desserializar_header(header, out)) return false;

    size_t n = proto::payload_size(out.tipo);
    if (n > 0) {
        out.payload.resize(n);
        if (!recv_all(fd, out.payload.data(), n)) return false;
    }
    return true;
}

int conectar(const std::string& ip, uint16_t porta) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "[net] erro ao criar socket: " << std::strerror(errno) << "\n";
        return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(porta);
    if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "[net] endereco IP invalido: " << ip << "\n";
        ::close(fd);
        return -1;
    }

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[net] erro ao conectar em " << ip << ":" << porta << ": "
                  << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }
    return fd;
}

int criar_servidor(uint16_t porta) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << "[net] erro ao criar socket: " << std::strerror(errno) << "\n";
        return -1;
    }

    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(porta);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[net] erro no bind (porta " << porta << "): "
                  << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 16) < 0) {
        std::cerr << "[net] erro no listen: " << std::strerror(errno) << "\n";
        ::close(fd);
        return -1;
    }
    return fd;
}

} // namespace net
