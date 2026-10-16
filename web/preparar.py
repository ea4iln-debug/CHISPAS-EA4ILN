# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 EA4ILN
#
# Monta la web de instalacion por USB (ESP Web Tools) en una carpeta lista para GitHub Pages o para probarla
# en local, con la version estable y, si se da, una beta:
#
#   python web/preparar.py .pio/web dist/1.20/chispas-1.20-usb.bin 1.20 [dist/1.30b/chispas-1.30b-usb.bin 1.30b]
#   python -m http.server -d .pio/web 8000        (Web Serial funciona en http://localhost)
#
# Parte cada imagen completa (-usb.bin, de publicar.sh) en bootloader, tabla de particiones, boot_app0 y app
# (firmware/<version>/) y escribe manifest.json (estable) y manifest-beta.json con esos trozos: asi la
# instalacion NO pisa la NVS (0x9000-0xE000: estacion, estadisticas, WiFi), salvo que se marque borrar en el
# dialogo. Los desplazamientos son los de la tabla de particiones de la V3 (default_8MB) y los de merge_bin en
# publicar.sh. Sin beta no hay manifest-beta.json y la pagina oculta el selector.

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

CAPTURAS = ("inicio.png", "cara.png", "cw.png", "qso_sim.png", "koch.png", "trx.png", "prop.png", "pota.png",
            "sol.png", "lora.png", "prototipo.jpg", "placa_perforada.jpg", "chispas_campo.jpg")

PAGINAS = ("index.html", "hardware.html", "funciones.html")
DOCS_PDF = ("esquema_protoboard.pdf", "placa_perforada.pdf")

RAIZ = Path(__file__).resolve().parent.parent


def manifiesto(imagen, version, salida, sello, canal):
    datos = imagen.read_bytes()
    if datos[0] != 0xE9 or datos[0x8000:0x8002] != b"\xaa\x50" or datos[0x10000] != 0xE9:
        sys.exit(f"{imagen} no parece una imagen completa de Chispas (bootloader, particiones y app)")
    carpeta = salida / "firmware" / version
    carpeta.mkdir(parents=True)
    partes = []
    for nombre, desde, hasta in TROZOS:
        (carpeta / nombre).write_bytes(datos[desde:hasta])
        partes.append({"path": f"firmware/{version}/{nombre}?{sello}", "offset": desde})
    m = {
        "name": "CHISPAS EA4ILN",
        "version": version,
        "canal": canal,                     # "estable" o "beta" (lo lee la pagina; ESP Web Tools lo ignora)
        "new_install_prompt_erase": True,   # Pregunta si borrar todo; por defecto se conserva la NVS
        "new_install_improv_wait_time": 0,  # Chispas no habla Improv
        "builds": [{"chipFamily": "ESP32-S3", "parts": partes}],
    }
    archivo = "manifest.json" if canal == "estable" else "manifest-beta.json"
    (salida / archivo).write_text(json.dumps(m, indent=2), encoding="utf-8")


def main():
    if len(sys.argv) not in (4, 6):
        sys.exit("uso: preparar.py <carpeta> <estable-usb.bin> <X.YY> [<beta-usb.bin> <X.YYb>]")
    salida = Path(sys.argv[1])

    # GitHub Pages deja 10 min en cache cada archivo: el sello (?AAAAMMDDhhmmss del despliegue) en los manifiestos,
    # los trozos y las capturas hace que el navegador pida los nuevos tras cada despliegue
    sello = time.strftime("%Y%m%d%H%M%S", time.gmtime())

    if salida.exists():
        shutil.rmtree(salida)
    salida.mkdir(parents=True)
    manifiesto(Path(sys.argv[2]), sys.argv[3], salida, sello, "estable")
    if len(sys.argv) == 6:
        manifiesto(Path(sys.argv[4]), sys.argv[5], salida, sello, "beta")

    for nombre in PAGINAS:
        pagina = (RAIZ / "web" / nombre).read_text(encoding="utf-8")
        (salida / nombre).write_text(pagina.replace("__SELLO__", sello), encoding="utf-8")
    (salida / "img").mkdir()
    for img in CAPTURAS:
        shutil.copy(RAIZ / "docs" / "img" / img, salida / "img")
    # Pagina de hardware: imagenes de la PCB (hardware/herramientas/imagenes_web.sh), PDF y foto del prototipo
    shutil.copytree(RAIZ / "web" / "hardware", salida / "hardware")
    (salida / "docs").mkdir()
    for pdf in DOCS_PDF:
        shutil.copy(RAIZ / "docs" / pdf, salida / "docs")
    shutil.copy(RAIZ / "docs" / "LICENCIAS_TERCEROS.txt", salida)
    beta = f" y beta {sys.argv[5]}" if len(sys.argv) == 6 else ""
    print(f"Web de Chispas: estable {sys.argv[3]}{beta} en {salida}")


if __name__ == "__main__":
    main()
