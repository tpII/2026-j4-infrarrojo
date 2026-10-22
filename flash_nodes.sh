#!/usr/bin/env bash
set -e

PORT_A="/dev/cu.usbmodem112101"
PORT_B="/dev/cu.usbmodem112201"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=========================================="
echo " Flasheando Nodo A (0x1) en $PORT_A"
echo "=========================================="
arduino-cli compile --fqbn arduino:avr:uno "$REPO/node_A"
arduino-cli upload -p "$PORT_A" --fqbn arduino:avr:uno "$REPO/node_A"
echo " Nodo A flasheado exitosamente!"
echo ""

echo "=========================================="
echo " Flasheando Nodo B (0x2) en $PORT_B"
echo "=========================================="
arduino-cli compile --fqbn arduino:avr:uno "$REPO/node_B"
arduino-cli upload -p "$PORT_B" --fqbn arduino:avr:uno "$REPO/node_B"
echo " Nodo B flasheado exitosamente!"
echo ""

echo "Nodos flasheados, cheto."
