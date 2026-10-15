#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Crea (si falta) el entorno de Python para las herramientas de la PCB: ~/.venv-kicad con easyeda2kicad y pymupdf
# en las versiones de requisitos.txt. Usa el Python que trae PlatformIO. Si ya existe y esta al dia, no hace nada.
# Uso: hardware/herramientas/entorno.sh   (imprime la ruta del python del entorno en la ultima linea)
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
VENV="$HOME/.venv-kicad"
PY="$VENV/Scripts/python.exe"
BASE="$HOME/.platformio/python3/python.exe"
if [ ! -x "$PY" ]; then
    [ -x "$BASE" ] || { echo "Falta $BASE (instalar PlatformIO)" >&2; exit 1; }
    echo "Creando $VENV ..." >&2
    "$BASE" -m venv "$VENV"
fi
# Instalar solo si falta algo o cambia una version
if ! "$PY" -m pip freeze 2>/dev/null | grep -qxF -f "$DIR/requisitos.txt" \
   || [ "$("$PY" -m pip freeze | grep -cxF -f "$DIR/requisitos.txt")" -ne "$(grep -c . "$DIR/requisitos.txt")" ]; then
    echo "Instalando requisitos ..." >&2
    "$PY" -m pip install -q --disable-pip-version-check -r "$DIR/requisitos.txt" >&2
fi
echo "$PY"
