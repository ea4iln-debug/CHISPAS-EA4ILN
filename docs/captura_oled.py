# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 EA4ILN
"""Captura la pantalla de Chispas por el puerto serie y la guarda en PNG (PROVISIONAL).

Envia la orden 'p' al firmware (v0.8c o posterior), lee el buffer de la OLED (128x64, formato SSD1306)
y lo guarda ampliado. Abre el puerto con DTR y RTS a False para no reiniciar la placa.
Cierra antes el monitor serie de PlatformIO: el puerto no se puede compartir.

Uso: python docs/captura_oled.py [--puerto COMx] [--escala 4] [--salida archivo.png] [--color azul|blanco]
Solo necesita pyserial (el PNG se escribe sin librerias extra).
"""

import argparse
import struct
import sys
import time
import zlib

import serial
from serial.tools import list_ports

ANCHO, ALTO = 128, 64
# Colores (encendido, apagado): azul como el de la OLED por defecto; blanco para documentos impresos
COLORES = {
    "azul": ((80, 180, 255), (4, 8, 16)),
    "blanco": ((255, 255, 255), (0, 0, 0)),
}


def leer_buffer(puerto, espera_s=3.0):
    ser = serial.Serial()
    ser.port = puerto
    ser.baudrate = 115200
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
    try:
        ser.reset_input_buffer()
        ser.write(b"p")
        datos = None
        limite = time.time() + espera_s
        while time.time() < limite:
            linea = ser.readline().decode("ascii", errors="replace").strip()
            if linea == "[CAPTURA] INICIO":
                datos = ""
            elif linea == "[CAPTURA] FIN" and datos is not None:
                break
            elif datos is not None and len(linea) == 128:
                datos += linea
        if datos is None or len(datos) != 2048:
            raise RuntimeError("No llego la captura completa (firmware v0.8c o posterior?)")
        return bytes.fromhex(datos)
    finally:
        ser.close()


def a_pixeles(buf):
    """Devuelve filas de 128 booleanos a partir del formato por paginas del SSD1306."""
    return [[bool(buf[x + (y // 8) * ANCHO] >> (y % 8) & 1) for x in range(ANCHO)] for y in range(ALTO)]


def guardar_png(pixeles, ruta, escala, color="azul"):
    encendido, apagado = (bytes(c) for c in COLORES[color])
    ancho, alto = ANCHO * escala, ALTO * escala
    crudo = bytearray()
    for fila in pixeles:
        linea = bytearray()
        for p in fila:
            linea += (encendido if p else apagado) * escala
        for _ in range(escala):
            crudo += b"\x00" + linea  # filtro 0 por fila

    def trozo(tipo, datos):
        return struct.pack(">I", len(datos)) + tipo + datos + struct.pack(">I", zlib.crc32(tipo + datos) & 0xFFFFFFFF)

    with open(ruta, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(trozo(b"IHDR", struct.pack(">IIBBBBB", ancho, alto, 8, 2, 0, 0, 0)))
        f.write(trozo(b"IDAT", zlib.compress(bytes(crudo), 9)))
        f.write(trozo(b"IEND", b""))


def buscar_puerto():
    """El puerto del CP210x de la Heltec (el numero de COM cambia de un equipo a otro)."""
    puertos = [p for p in list_ports.comports() if p.vid == 0x10C4 and p.pid == 0xEA60]
    if len(puertos) == 1:
        return puertos[0].device
    if not puertos:
        sys.exit("No hay ninguna placa (CP210x) conectada. Usa --puerto si es otro adaptador.")
    sys.exit("Hay varias placas: " + ", ".join(p.device for p in puertos) + ". Elige una con --puerto.")


def main():
    ap = argparse.ArgumentParser(description="Captura la OLED de Chispas por serie")
    ap.add_argument("--puerto", help="por defecto, el del CP210x (como serie.py)")
    ap.add_argument("--escala", type=int, default=4)
    ap.add_argument("--salida", default=time.strftime("captura_%Y%m%d_%H%M%S.png"))
    ap.add_argument("--color", choices=sorted(COLORES), default="azul")
    args = ap.parse_args()
    if not args.puerto:
        args.puerto = buscar_puerto()
    try:
        buf = leer_buffer(args.puerto)
    except (serial.SerialException, RuntimeError) as e:
        sys.exit(f"Error: {e}")
    guardar_png(a_pixeles(buf), args.salida, args.escala, args.color)
    print(f"Guardada: {args.salida} ({ANCHO * args.escala}x{ALTO * args.escala})")


if __name__ == "__main__":
    main()
