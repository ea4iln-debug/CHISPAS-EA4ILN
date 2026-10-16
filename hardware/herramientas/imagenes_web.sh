#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 EA4ILN
#
# Imagenes de la PCB para la pagina de hardware de la web (web/hardware/): vista 3D, cara de arriba y de abajo,
# y el esquema de KiCad en PDF y PNG. Se guardan en el repositorio (la Action de la web no tiene KiCad):
# rehacerlas tras cambiar el esquema o la placa. Tarda un par de minutos (render 3D).
# Uso: hardware/herramientas/imagenes_web.sh
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
RAIZ="$(cd "$DIR/../.." && pwd)"
K="$(ls "/c/Program Files/KiCad/"*/bin/kicad-cli.exe 2>/dev/null | sort | tail -1)"
[ -x "$K" ] || { echo "No encuentro kicad-cli (C:/Program Files/KiCad/<version>/bin)" >&2; exit 1; }
PY="$("$DIR/entorno.sh" | tail -1)"
PCB="$RAIZ/hardware/chispas.kicad_pcb"
SAL="$RAIZ/web/hardware"
mkdir -p "$SAL"

"$K" pcb render -o "$SAL/pcb_3d.png" --quality high -w 1600 -h 1100 --rotate "-40,0,-25" --perspective "$PCB" | tail -1
"$K" pcb render -o "$SAL/pcb_arriba.png" --side top --quality basic -w 1600 -h 1100 "$PCB" | tail -1
"$K" pcb render -o "$SAL/pcb_abajo.png" --side bottom --quality basic -w 1600 -h 1100 "$PCB" | tail -1
"$K" sch export pdf -o "$SAL/esquema_pcb.pdf" "$RAIZ/hardware/chispas.kicad_sch" | tail -1
"$PY" -I -c "import pymupdf, sys; pymupdf.open(sys.argv[1])[0].get_pixmap(dpi=130).save(sys.argv[2])" \
    "$SAL/esquema_pcb.pdf" "$SAL/esquema_pcb.png"
ls -la "$SAL"
