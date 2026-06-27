#!/usr/bin/env bash
# run.sh - Compila e executa todos os componentes da estufa inteligente.
#
# Uso: ./run.sh [porta]   (padrao: 8080)
#
# Cada componente de fundo (gerenciador, sensores, atuadores) e iniciado em
# subprocesso proprio. Ao pressionar Ctrl+C ou ao encerrar o cliente, todos
# sao finalizados automaticamente.

set -euo pipefail

PORT=${1:-8080}
HOST="127.0.0.1"

# ---------- build ----------
echo "[run] Compilando..."
make -j"$(nproc)"

mkdir -p logs
echo ""

# ---------- limpeza ao sair ----------
PIDS=()
cleanup() {
    echo ""
    echo "[run] Encerrando componentes..."
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    echo "[run] Encerrado."
}
trap cleanup EXIT INT TERM

# ---------- gerenciador ----------
echo "[run] Iniciando gerenciador na porta $PORT...  (log: logs/gerenciador.log)"
./gerenciador "$PORT" >logs/gerenciador.log 2>&1 &
PIDS+=($!)
sleep 0.4   # aguarda o bind antes de conectar os clientes

# ---------- sensores ----------
for CAT in T U O; do
    echo "[run] Iniciando sensor $CAT...  (log: logs/sensor_$CAT.log)"
    ./sensor "$CAT" "$HOST" "$PORT" >logs/sensor_$CAT.log 2>&1 &
    PIDS+=($!)
    sleep 0.1
done

# ---------- atuadores ----------
for CAT in A R S I; do
    echo "[run] Iniciando atuador $CAT...  (log: logs/atuador_$CAT.log)"
    ./atuador "$CAT" "$HOST" "$PORT" >logs/atuador_$CAT.log 2>&1 &
    PIDS+=($!)
    sleep 0.1
done

echo ""
echo "[run] Todos os componentes ativos. Iniciando cliente..."
echo "      (Ctrl+C ou encerrar o cliente finaliza tudo)"
echo ""

# ---------- cliente (foreground - interativo) ----------
./cliente "$HOST" "$PORT"
