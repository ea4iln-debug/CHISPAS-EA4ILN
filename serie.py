# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 EA4ILN
"""Lee el puerto serie de Chispas sin reiniciar la placa (DTR y RTS a False antes de abrir).

Busca solo el puerto del CP210x de la Heltec, pone la hora a cada linea, la guarda tambien en un log
(.pio/serie-AAAAMMDD-HHMMSS.log) y puede filtrar por expresion regular. Si la placa se reinicia o se
desconecta, espera y vuelve a abrir el puerto. Cierra antes el monitor serie de PlatformIO.

Uso: python serie.py [--puerto COM16] [--filtro REGEX] [--segundos N] [--enviar TEXTO] [--sin-log]
Ejemplos:
  python serie.py                          todo, hasta Ctrl+C
  python serie.py -f "DIAG|DECOD"          solo las lineas con [DIAG] o [DECOD]
  python serie.py -f "^\\$G" -s 30          30 s de NMEA del GPS
  python serie.py -e d -s 2                envia 'd' (orden serie) y lee 2 s
Desde Bash: ~/.platformio/penv/Scripts/python.exe serie.py ...
"""

import argparse
import os
import re
import sys
import time
from datetime import datetime

import serial
from serial.tools import list_ports

VID_CP210X, PID_CP210X = 0x10C4, 0xEA60


def buscar_puerto():
    puertos = [p for p in list_ports.comports() if p.vid == VID_CP210X and p.pid == PID_CP210X]
    if len(puertos) == 1:
        return puertos[0].device
    if not puertos:
        sys.exit("No hay ninguna placa (CP210x) conectada. Usa --puerto si es otro adaptador.")
    sys.exit("Hay varias placas: " + ", ".join(p.device for p in puertos) + ". Elige una con --puerto.")


def abrir(puerto, baudios):
    ser = serial.Serial()
    ser.port = puerto
    ser.baudrate = baudios
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
    return ser


def main():
    ap = argparse.ArgumentParser(description="Lee el serie de Chispas sin reiniciar la placa.")
    ap.add_argument("-p", "--puerto", help="puerto (por defecto, el del CP210x)")
    ap.add_argument("-b", "--baudios", type=int, default=115200)
    ap.add_argument("-f", "--filtro", help="muestra solo las lineas que cumplan esta expresion regular")
    ap.add_argument("-s", "--segundos", type=float, help="termina pasados estos segundos (por defecto, Ctrl+C)")
    ap.add_argument("-e", "--enviar", help="texto que se envia al abrir el puerto (ordenes serie)")
    ap.add_argument("--sin-log", action="store_true", help="no guarda el log")
    args = ap.parse_args()

    sys.stdout.reconfigure(errors="replace")
    puerto = args.puerto or buscar_puerto()
    filtro = re.compile(args.filtro) if args.filtro else None
    fin = time.time() + args.segundos if args.segundos else None

    log = None
    if not args.sin_log:
        carpeta = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".pio")
        os.makedirs(carpeta, exist_ok=True)
        ruta = os.path.join(carpeta, datetime.now().strftime("serie-%Y%m%d-%H%M%S.log"))
        log = open(ruta, "w", encoding="utf-8")
        print(f"[serie] {puerto}, log en {ruta}", file=sys.stderr)

    ser = None
    enviado = False
    try:
        while fin is None or time.time() < fin:
            if ser is None:
                try:
                    ser = abrir(puerto, args.baudios)
                except serial.SerialException:
                    time.sleep(1)
                    continue
                if args.enviar and not enviado:
                    ser.write(args.enviar.encode("ascii"))
                    enviado = True
            try:
                crudo = ser.readline()
            except serial.SerialException:
                print("[serie] puerto perdido; esperando a que vuelva...", file=sys.stderr)
                ser.close()
                ser = None
                continue
            if not crudo:
                continue
            linea = crudo.decode("utf-8", errors="replace").rstrip("\r\n")
            hora = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            if log:
                log.write(f"{hora} {linea}\n")
                log.flush()
            if filtro is None or filtro.search(linea):
                print(f"{hora} {linea}", flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        if ser:
            ser.close()
        if log:
            log.close()


if __name__ == "__main__":
    main()
