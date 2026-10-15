# SPDX-License-Identifier: GPL-3.0-or-later
# Corrige el tipo electrico de los pines de los simbolos importados con easyeda2kicad (lib/chispas.kicad_sym),
# que llegan como "input" o "unspecified" y hacen saltar el ERC. Volver a ejecutarlo tras importar piezas.
# Uso: python tipos_pines.py
import re
from pathlib import Path

LIB = Path(__file__).resolve().parent.parent / "lib" / "chispas.kicad_sym"

# simbolo -> {numero de pin: tipo}; "*" = todos los demas
TIPOS = {
    "PAM8302AASCR": {"1": "input", "2": "no_connect", "3": "input", "4": "input", "5": "output", "6": "power_in",
                     "7": "power_in", "8": "output"},
    "PC817X2CSP9F": {"*": "passive"},
    "PJ-307_C2939173": {"*": "passive"},
    "RK097111080J": {"*": "passive"},
    "3296W-1-103": {"*": "passive"},
}

texto = LIB.read_text(encoding="utf-8")
# Trocear por simbolo de nivel superior: '  (symbol "NOMBRE"'
partes = re.split(r'(?m)^(?=  \(symbol ")', texto)
salida = []
for parte in partes:
    m = re.match(r'  \(symbol "([^"]+)"', parte)
    if m and m.group(1) in TIPOS:
        tipos = TIPOS[m.group(1)]

        def cambiar(mp):
            num = re.search(r'\(number "([^"]+)"', mp.group(0)).group(1)
            nuevo = tipos.get(num, tipos.get("*"))
            return re.sub(r'\(pin \w+', f'(pin {nuevo}', mp.group(0), count=1) if nuevo else mp.group(0)
        parte = re.sub(r'\(pin \w+ line.*?\(number "[^"]+"', cambiar, parte, flags=re.S)
    salida.append(parte)
LIB.write_text("".join(salida), encoding="utf-8")
print("Tipos de pin corregidos en", LIB.name)
