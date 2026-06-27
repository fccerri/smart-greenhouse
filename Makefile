# Makefile         Estufa Inteligente (SSC0142)
# SO: Linux        Compilador: g++
# Compilar: make   Limpar: make clean

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Iinclude -pthread
SRC      := src
BIN      := gerenciador sensor atuador cliente

# Objetos da biblioteca compartilhada (protocolo + sockets).
COMUM    := $(SRC)/protocol.cpp $(SRC)/net.cpp

.PHONY: all clean
all: $(BIN)

gerenciador: $(SRC)/gerenciador.cpp $(COMUM)
	$(CXX) $(CXXFLAGS) -o $@ $^

sensor: $(SRC)/sensor.cpp $(COMUM)
	$(CXX) $(CXXFLAGS) -o $@ $^

atuador: $(SRC)/atuador.cpp $(COMUM)
	$(CXX) $(CXXFLAGS) -o $@ $^

cliente: $(SRC)/cliente.cpp $(COMUM)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(BIN) net.o
