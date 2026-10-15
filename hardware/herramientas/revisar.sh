#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Revisa esquema y placa (piezas, redes, cambios desde el ultimo commit, ERC, DRC e imagenes). Ver revisar.py.
# Uso: hardware/herramientas/revisar.sh [carpeta_salida]
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
PY="$("$DIR/entorno.sh" | tail -1)"
"$PY" -I "$DIR/revisar.py" "$@"
