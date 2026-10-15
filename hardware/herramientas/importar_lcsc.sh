#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Importa piezas de LCSC (simbolo, huella y 3D) a la libreria del proyecto hardware/lib/chispas y las deja listas:
# tipos de pin corregidos (tipos_pines.py) y agujeros de tetones de plastico sin metalizar.
# Uso: hardware/herramientas/importar_lcsc.sh C113367 C66405 ...
# Despues: en KiCad, Herramientas > Actualizar simbolos desde la libreria (si la pieza ya estaba en el esquema).
set -e
[ $# -gt 0 ] || { echo "Uso: $0 C12345 [C67890 ...]" >&2; exit 1; }
DIR="$(cd "$(dirname "$0")" && pwd)"
HW="$(cd "$DIR/.." && pwd)"
PY="$("$DIR/entorno.sh" | tail -1)"
VENV_BIN="$(dirname "$PY")"
# easyeda2kicad necesita la ruta absoluta (con relativa falla --project-relative); se ejecuta desde hardware/
HW_WIN="$(cygpath -m "$HW" 2>/dev/null || echo "$HW")"
( cd "$HW" && "$VENV_BIN/easyeda2kicad.exe" --full --lcsc_id "$@" --output "$HW_WIN/lib/chispas" --project-relative )
"$HOME/.platformio/penv/Scripts/python.exe" -I "$DIR/tipos_pines.py"
# Pads sin numero con taladro = agujeros de tetones: sin metalizar
sed -i 's/(pad "" thru_hole circle \(([^)]*)\) (size \([0-9.]*\) \([0-9.]*\)) (drill \([0-9.]*\)) (layers \*\.Cu \*\.Mask))/(pad "" np_thru_hole circle \1 (size \4 \4) (drill \4) (layers *.Cu *.Mask))/' "$HW"/lib/chispas.pretty/*.kicad_mod
echo "Importado: $*. Revisar en tipos_pines.py si la pieza nueva necesita tipos de pin propios."
