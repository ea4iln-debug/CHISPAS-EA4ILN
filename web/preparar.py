# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 EA4ILN
#
# Monta la web de instalacion por USB (ESP Web Tools) en una carpeta lista para GitHub Pages o para probarla
# en local:
#
#   python web/preparar.py dist/1.20/chispas-1.20-usb.bin 1.20 .pio/web
#   python -m http.server -d .pio/web 8000        (Web Serial funciona en http://localhost)
#
# Parte la imagen completa (-usb.bin, de publicar.sh) en bootloader, tabla de particiones, boot_app0 y app, y
# escribe manifest.json con esos trozos: asi la instalacion NO pisa la NVS (0x9000-0xE000: estacion,
# estadisticas, WiFi), salvo que se marque borrar en el dialogo. Los desplazamientos son los de la tabla de
# particiones de la V3 (default_8MB) y los de merge_bin en publicar.sh.

import json
import shutil
import sys
import time
from pathlib import Path

TROZOS = [  # (archivo, desde, hasta); None = hasta el final
    ("bootloader.bin", 0x0000, 0x8000),
    ("particiones.bin", 0x8000, 0x9000),
    ("boot_app0.bin", 0xE000, 0x10000),  # otadata: arranca de app0, donde va la app
    ("chispas.bin", 0x10000, None),
]

RAIZ = Path(__file__).resolve().parent.parent


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__ or "uso: preparar.py <chispas-X.YY-usb.bin> <X.YY> <carpeta>")
    imagen, version, salida = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
    datos = imagen.read_bytes()
    if datos[0] != 0xE9 or datos[0x8000:0x8002] != b"\xaa\x50" or datos[0x10000] != 0xE9:
        sys.exit(f"{imagen} no parece una imagen completa de Chispas (bootloader, particiones y app)")

    # GitHub Pages deja 10 min en cache cada archivo: el sello (?AAAAMMDDhhmmss del despliegue) en el manifiesto,
    # los trozos y las capturas hace que el navegador pida los nuevos tras cada despliegue
    sello = time.strftime("%Y%m%d%H%M%S", time.gmtime())

    if salida.exists():
        shutil.rmtree(salida)
    (salida / "firmware").mkdir(parents=True)
    partes = []
    for nombre, desde, hasta in TROZOS:
        (salida / "firmware" / nombre).write_bytes(datos[desde:hasta])
        partes.append({"path": f"firmware/{nombre}?{sello}", "offset": desde})

    manifiesto = {
        "name": "Chispas EA4ILN",
        "version": version,
        "new_install_prompt_erase": True,   # Pregunta si borrar todo; por defecto se conserva la NVS
        "new_install_improv_wait_time": 0,  # Chispas no habla Improv
        "builds": [{"chipFamily": "ESP32-S3", "parts": partes}],
    }
    (salida / "manifest.json").write_text(json.dumps(manifiesto, indent=2), encoding="utf-8")

    pagina = (RAIZ / "web" / "index.html").read_text(encoding="utf-8")
    (salida / "index.html").write_text(pagina.replace("__SELLO__", sello), encoding="utf-8")
    (salida / "img").mkdir()
    for img in ("inicio.png", "cara.png", "koch.png", "trx.png", "prop.png", "pota.png", "sol.png", "lora.png"):
        shutil.copy(RAIZ / "docs" / "img" / img, salida / "img")
    shutil.copy(RAIZ / "docs" / "LICENCIAS_TERCEROS.txt", salida)
    print(f"Web de Chispas {version} en {salida}")


if __name__ == "__main__":
    main()
