# Genera el plano de la placa perforada (HTML con SVG -> PDF con Chrome o Edge) y comprueba sus conexiones
#
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 EA4ILN. Software libre, sin garantia: ver el archivo LICENSE.
# El plano que genera (docs/placa_perforada.pdf) es documentacion: CC BY-NC-SA 4.0 (ver LICENSE-HARDWARE.md).
#
# Uso: python gen_placa.py salida.html
# Rejilla: x = columna (0 = A, a la izquierda vista por el lado de los componentes), y = fila (0 = fila 1, arriba).
# Todas las medidas en pasos de 2,54 mm salvo que se diga.
import html, sys

COLS, ROWS = 21, 26
LETRAS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"

def hueco(x, y):
    return f"{LETRAS[x]}{y + 1}"

# ----------------------------------------------------------------------------
# Heltec V3: zocalos hembra de 18 pines. Pin 1 (GND, pad cuadrado) junto al USB, abajo.
# J3_IZQUIERDA: con la OLED hacia arriba y el USB abajo, J3 (GPIO7...GND) queda a la izquierda. COMPROBAR.
# ----------------------------------------------------------------------------
J3_IZQUIERDA = True
XJ3, XJ2 = (9, 18) if J3_IZQUIERDA else (18, 9)
Y_PIN1 = 25            # fila del pin 1 (el pin p esta en la fila Y_PIN1 + 1 - p)
J3 = ["GND", "3V3", "3V3", "GPIO37", "GPIO46", "GPIO45", "GPIO42", "GPIO41", "GPIO40", "GPIO39", "GPIO38", "GPIO1",
      "GPIO2", "GPIO3", "GPIO4", "GPIO5", "GPIO6", "GPIO7"]
J2 = ["GND", "5V", "Ve", "Ve", "GPIO44", "GPIO43", "RST", "GPIO0", "GPIO36", "GPIO35", "GPIO34", "GPIO33", "GPIO47",
      "GPIO48", "GPIO26", "GPIO21", "GPIO20", "GPIO19"]
# Borde de la placa de la Heltec (50,2 x 25,5 mm): 0,52 pasos fuera de cada fila de pines y ~0,89 mas alla del pin 18
HELTEC_RECT = (min(XJ3, XJ2) - 0.52, Y_PIN1 + 1 - 18 - 0.89, max(XJ3, XJ2) + 0.52, Y_PIN1 + 0.89 + 0.9)

def heltec_y(pin):
    return Y_PIN1 + 1 - pin

# Red de cada pin de la Heltec que se usa (los demas se sueldan al zocalo pero no se conectan)
RED_J3 = {1: "GND", 2: "3V3", 3: "3V3", 7: "SCL", 8: "SDA", 11: "AMP_SD", 13: "KEY_GPIO", 10: "GPS_RX",
          15: "DIT", 16: "DAH", 17: "ADC", 18: "TONO"}
RED_J2 = {1: "GND", 2: "5V", 12: "USB_DET", 13: "GPS_TX", 14: "ENC_DT", 17: "ENC_SW", 18: "ENC_CLK"}

# ----------------------------------------------------------------------------
# Piezas
# ----------------------------------------------------------------------------
class Pieza:
    def __init__(s, ref, tipo, valor, pines, cuerpo=None, alto=0, nota="", circulo=None, opcional=False, reserva=None):
        s.ref, s.tipo, s.valor, s.pines, s.cuerpo, s.alto, s.nota = ref, tipo, valor, pines, cuerpo, alto, nota
        s.circulo, s.opcional = circulo, opcional   # pines: lista de (nombre, x, y, red)
        s.reserva = reserva                         # None, o la reserva a la que pertenece ("USB", ...)
        if reserva:
            s.opcional = True

# Reservas: huecos para piezas que no se montan de entrada. La placa funciona sin ellas.
#   TRAFO: trafo de aislamiento 600:600 en la entrada del decodificador (quitar los 2 puentes de P10)
#   USB:   divisor 47k/68k del pin 5V a GPIO33 para detectar el USB (soldar R11, R12 y los cables A y B)
RESERVAS = ("TRAFO", "USB")

def activo(cond, montado):
    """cond: None (siempre), "X" (solo con la reserva X montada) o "-X" (solo sin ella)."""
    if not cond:
        return True
    if cond.startswith("-"):
        return cond[1:] not in montado
    return cond in montado

PIEZAS = []

def pieza(*a, **k):
    p = Pieza(*a, **k)
    PIEZAS.append(p)
    return p

def resistencia(ref, valor, a, b, red_a, red_b, nota="", de_pie=None, reserva=None):
    """Resistencia de 1/4 W tumbada entre a y b (4 pasos), o de pie (1 paso) con el cuerpo sobre de_pie."""
    (xa, ya), (xb, yb) = a, b
    if de_pie:
        cx, cy = de_pie
        return pieza(ref, "R", valor, [("1", xa, ya, red_a), ("2", xb, yb, red_b)],
                     circulo=(cx, cy, 0.5), alto=11, nota=nota or "de pie", reserva=reserva)
    if ya == yb:
        m = (xa + xb) / 2
        cuerpo = (m - 1.25, ya - 0.47, m + 1.25, ya + 0.47)
    else:
        m = (ya + yb) / 2
        cuerpo = (xa - 0.47, m - 1.25, xa + 0.47, m + 1.25)
    return pieza(ref, "R", valor, [("1", xa, ya, red_a), ("2", xb, yb, red_b)], cuerpo, 2.5, nota, reserva=reserva)

def diodo(ref, valor, anodo, catodo, red_a, red_k):
    (xa, ya), (xk, yk) = anodo, catodo
    if ya == yk:
        m = (xa + xk) / 2
        cuerpo = (m - 0.75, ya - 0.37, m + 0.75, ya + 0.37)
    else:
        m = (ya + yk) / 2
        cuerpo = (xa - 0.37, m - 0.75, xa + 0.37, m + 0.75)
    return pieza(ref, "D", valor, [("A", xa, ya, red_a), ("K", xk, yk, red_k)], cuerpo, 2)

def tira(ref, valor, x, y0, nombres, redes, nota="", dx=0, dy=1, alto=9):
    """Tira de pines macho de 2,54 mm (para los cables Dupont hacia la carcasa)."""
    pines = [(n, x + i * dx, y0 + i * dy, r) for i, (n, r) in enumerate(zip(nombres, redes))]
    xs = [p[1] for p in pines]
    ys = [p[2] for p in pines]
    return pieza(ref, "P", valor, pines, (min(xs) - 0.5, min(ys) - 0.5, max(xs) + 0.5, max(ys) + 0.5), alto, nota)

# --- Heltec (zocalos hembra) ---
pieza("H3", "Z", "zócalo hembra 1×18 (J3 de la Heltec)",
      [(str(p), XJ3, heltec_y(p), RED_J3.get(p, "")) for p in range(1, 19)],
      (XJ3 - 0.5, heltec_y(18) - 0.5, XJ3 + 0.5, heltec_y(1) + 0.5), 8.5)
pieza("H2", "Z", "zócalo hembra 1×18 (J2 de la Heltec)",
      [(str(p), XJ2, heltec_y(p), RED_J2.get(p, "")) for p in range(1, 19)],
      (XJ2 - 0.5, heltec_y(18) - 0.5, XJ2 + 0.5, heltec_y(1) + 0.5), 8.5)

# --- Decodificador (arriba a la izquierda) ---
# P7 recibe el jack; P10 (2x2) lleva dos puentes de quitar y poner: 1-2 (punta -> entrada) y 3-4 (malla -> masa).
# Con el trafo de aislamiento: quitar los dos puentes y conectar el primario a 1 y 3 y el secundario a 2 y 4.
tira("P7", "entrada del decodificador → jack J3 (T, S)", 0, 0, ["T", "S"], ["TJ", "SJ"], dx=1, dy=0)
pieza("P10", "P", "2×2 pines con 2 puentes (trafo 600:600)",
      [("1", 0, 1, "TJ"), ("3", 1, 1, "SJ"), ("2", 0, 2, "DEC_IN"), ("4", 1, 2, "GND")], (-0.5, 0.5, 1.5, 2.5), 9,
      "sin trafo: puentes 1-2 y 3-4")
pieza("RV2", "RV", "trimmer 10 kΩ multivuelta (3296W)",
      [("3", 0, 4, "DEC_IN"), ("2", 1, 4, "DEC_CUR"), ("1", 2, 4, "GND")], (-0.87, 3.5, 2.87, 5.4), 10,
      "tornillo de ajuste arriba")
pieza("C2", "C", "1 µF film (paso 5 mm)", [("1", 1, 7, "DEC_CUR"), ("2", 3, 7, "BIAS")], (0.57, 6.0, 3.43, 8.0), 9)
resistencia("R5", "10 kΩ", (4, 3), (4, 7), "GND", "BIAS")
resistencia("R4", "10 kΩ", (5, 3), (5, 7), "3V3", "BIAS")
resistencia("R6", "1 kΩ", (3, 9), (7, 9), "BIAS", "ADC")
diodo("D1", "1N4148", (8, 8), (8, 5), "ADC", "3V3")
diodo("D2", "1N4148", (7, 3), (7, 6), "GND", "ADC")
# C3 (10 nF, opcional, no comprado): va de ADC a GND, igual que D2: se suelda por la cara de soldaduras sobre H4 y H7.

# --- Tono (arriba en el centro) ---
tira("P4", "volumen RV1 (pat. 3, 2, 1)", 11, 0, ["3", "2", "1"], ["VOL_IN", "VOL_CUR", "GND"], dx=1, dy=0)
tira("P3", "auriculares J2 (S, TN, T, R)", 15, 0, ["S", "TN", "T", "R"], ["GND", "TN", "AUR_T", "AUR_R"], dx=1, dy=0)
resistencia("R1", "1 kΩ", (10, 6), (10, 2), "TONO", "VOL_IN")
pieza("C1", "CE", "10 µF 16 V electrolítico", [("+", 12, 1, "VOL_CUR"), ("−", 12, 2, "AUD")], None, 11,
      "+ hacia P4", circulo=(12.0, 1.5, 1.0))
resistencia("R2", "220 Ω", (13, 4), (17, 4), "AUD", "AUR_T")
resistencia("R3", "220 Ω", (13, 5), (17, 5), "AUD", "AUR_R")
resistencia("R9", "10 kΩ", (16, 1), (16, 2), "TN", "GND", de_pie=(16, 2))

# --- Izquierda: palas, KEY, amplificador y BME280 ---
tira("P2", "palas J1 (DAH, DIT, GND)", 0, 10, ["DAH", "DIT", "GND"], ["DAH", "DIT", "GND"])
tira("P6", "KEY J4 (C, E) · aislado", 0, 14, ["C", "E"], ["KEY_C", "KEY_E"])
tira("P5", "amplificador U2 (VIN, GND, SD, A+)", 0, 17, ["VIN", "GND", "SD", "A+"], ["3V3", "GND", "AMP_SD", "TN"])
tira("P8", "BME280 (VIN, GND, SCL, SDA)", 0, 22, ["VIN", "GND", "SCL", "SDA"], ["3V3", "GND", "SCL", "SDA"])
pieza("U1", "U", "PC817 (DIP-4)", [("1", 4, 14, "KEY_A"), ("2", 4, 15, "GND"), ("3", 1, 15, "KEY_E"), ("4", 1, 14, "KEY_C")],
      (1.22, 13.6, 3.78, 15.4), 4, "punto (pat. 1) arriba a la derecha")
resistencia("R7", "330 Ω", (8, 13), (4, 13), "KEY_GPIO", "KEY_A")
resistencia("R10", "10 kΩ", (8, 12), (4, 12), "KEY_GPIO", "GND")
# Reserva USB: divisor del pin 5V (con USB, 4,94 V -> 2,92 V en GPIO33; sin USB, 0,7 V: el pin 5V no queda a 0)
resistencia("R11", "47 kΩ", (3, 20), (7, 20), "5V", "USB_DET", "reserva: detección de USB", reserva="USB")
resistencia("R12", "68 kΩ", (2, 22), (6, 22), "GND", "USB_DET", "reserva: detección de USB", reserva="USB")

# --- Derecha: encoder y GPS ---
tira("P1", "encoder KY-040 (CLK, SW, DT, +, GND)", 20, 8, ["CLK", "SW", "DT", "+", "GND"],
     ["ENC_CLK", "ENC_SW", "ENC_DT", "3V3", "GND"])
tira("P9", "GPS (VCC, RX, TX, GND)", 20, 14, ["VCC", "RX", "TX", "GND"], ["3V3", "GPS_RX", "GPS_TX", "GND"])

# Zonas
ZONA_AISLADA = (-0.5, 13.5, 2.45, 15.5)       # lado de la emisora del PC817

# ----------------------------------------------------------------------------
# Conexiones por el lado de las soldaduras
# PUENTES: hilo desnudo (o estaño) de agujero en agujero vecino. CABLES: hilo aislado de punto a punto.
# ----------------------------------------------------------------------------
def tramo(*puntos):
    """Ruta en L por puntos de paso: devuelve todos los agujeros recorridos."""
    camino = [puntos[0]]
    for (x1, y1) in puntos[1:]:
        x0, y0 = camino[-1]
        while (x0, y0) != (x1, y1):
            if x0 != x1:
                x0 += 1 if x1 > x0 else -1
            else:
                y0 += 1 if y1 > y0 else -1
            camino.append((x0, y0))
    return camino

h3 = lambda p: (XJ3, heltec_y(p))
h2 = lambda p: (XJ2, heltec_y(p))

PUENTES = [
    # Masa: bus por la fila 26 (bajo el USB) entre los dos GND de la Heltec, rail en B18–B26 y subida bajo la Heltec (col. O)
    ("GND", tramo((1, 25), (20, 25))),
    ("GND", tramo((1, 17), (1, 25))),
    ("GND", tramo((14, 0), (14, 25))),
    ("GND", tramo((13, 0), (15, 0))),
    ("GND", tramo((16, 2), (15, 2), (15, 0))),
    ("GND", tramo((1, 2), (2, 2), (2, 4))),
    ("GND", tramo((2, 2), (7, 2), (7, 3))),
    ("GND", [(4, 2), (4, 3)]),
    ("GND", tramo((4, 12), (0, 12))),
    ("GND", tramo((4, 15), (4, 17), (1, 17))),
    ("GND", [(0, 18), (1, 18)]),
    ("GND", [(0, 23), (1, 23)]),
    ("GND", tramo((20, 17), (20, 25))),
    # 3V3: barra junto a los pines 2 y 3 de J3 (col. K), bajo la Heltec
    ("3V3", tramo((XJ3, heltec_y(2)), (10, heltec_y(2)), (10, heltec_y(2) - 4))),
    ("3V3", [h3(3), (10, heltec_y(3))]),
    ("3V3", tramo((5, 3), (6, 3), (6, 5), (8, 5))),
    # Decodificador
    ("TJ", [(0, 0), (0, 1)]),
    ("SJ", [(1, 0), (1, 1)]),
    ("DEC_IN", tramo((0, 2), (0, 4))),
    ("DEC_CUR", tramo((1, 4), (1, 7))),
    ("BIAS", tramo((5, 7), (3, 7), (3, 9))),
    ("ADC", tramo((7, 6), (7, 9))),
    ("ADC", tramo((7, 8), (8, 8), (8, 9), h3(17))),
    # Tono
    ("TONO", tramo(h3(18), (XJ3, 6), (10, 6))),
    ("VOL_IN", tramo((10, 2), (10, 0), (11, 0))),
    ("VOL_CUR", [(12, 1), (12, 0)]),
    ("AUD", tramo((12, 2), (12, 5), (13, 5))),
    ("AUD", [(12, 4), (13, 4)]),
    ("AUR_T", tramo((17, 4), (17, 0))),
    ("AUR_R", tramo((17, 5), (18, 5), (18, 0))),
    ("TN", [(16, 1), (16, 0)]),
    # Palas, KEY
    ("DAH", tramo(h3(16), (0, heltec_y(16)))),
    ("DIT", tramo(h3(15), (0, heltec_y(15)))),
    ("KEY_GPIO", [h3(13), (8, 13)]),
    ("KEY_GPIO", [(8, 12), (8, 13)]),
    ("KEY_A", [(4, 13), (4, 14)]),
    ("KEY_C", [(0, 14), (1, 14)]),
    ("KEY_E", [(0, 15), (1, 15)]),
    # Encoder y GPS
    ("ENC_CLK", tramo(h2(18), (20, heltec_y(18)))),
    ("ENC_SW", tramo(h2(17), (20, heltec_y(17)))),
    ("ENC_DT", tramo(h2(14), (19, heltec_y(14)), (19, 10), (20, 10))),
    ("GPS_TX", tramo(h2(13), (19, heltec_y(13)), (19, 16), (20, 16))),
    # Reserva USB (solo al montar R11 y R12)
    ("USB_DET", tramo((7, 20), (7, 22), (6, 22)), "USB"),
    ("GND", [(2, 22), (1, 22)], "USB"),
]

# Puentes de quitar y poner (jumper caps) sobre los pines de P10: se quitan al montar el trafo
CAPUCHONES = [
    ("TJ→DEC_IN", [(0, 1), (0, 2)], "-TRAFO"),
    ("SJ→GND", [(1, 1), (1, 2)], "-TRAFO"),
]

# Cables aislados (hilo fino con funda, por el lado de las soldaduras): red, desde, hasta
CABLES = [
    ("GND", (7, 2), (14, 1)),
    ("GND", (3, 12), (1, 19)),
    ("GND", (20, 12), (20, 18)),
    ("3V3", (10, 21), (6, 3)),
    ("3V3", (10, 22), (0, 17)),
    ("3V3", (10, 23), (0, 22)),
    ("3V3", (10, 20), (20, 11)),
    ("3V3", (10, 24), (20, 14)),
    ("GPS_RX", h3(10), (20, 15)),
    ("AMP_SD", h3(11), (0, 19)),
    ("SCL", h3(7), (0, 24)),
    ("SDA", h3(8), (0, 25)),
    ("TN", (16, 1), (0, 20)),
]
# Cables de las reservas (letras en el plano): red, desde, hasta, reserva
CABLES_RESERVA = [
    ("5V", h2(2), (3, 20), "USB"),
    ("USB_DET", h2(12), (7, 21), "USB"),
]
def red_igual(montado):
    m = {}
    if "TRAFO" not in montado:      # con los puentes de P10 puestos
        m.update({"TJ": "DEC_IN", "SJ": "GND"})
    return m

# ----------------------------------------------------------------------------
# Comprobaciones
# ----------------------------------------------------------------------------
def puentes(montado):
    for t in PUENTES:
        r, camino = t[0], t[1]
        if activo(t[2] if len(t) > 2 else None, montado):
            yield r, camino

def cables(montado):
    for r, a, b in CABLES:
        yield r, a, b
    for r, a, b, cond in CABLES_RESERVA:
        if activo(cond, montado):
            yield r, a, b

def comprobar(montado=frozenset()):
    errores = []
    red = {}     # agujero -> red
    duenio = {}  # agujero -> "REF.pin"
    igual = red_igual(montado)
    alias = lambda r: igual.get(r, r)
    def fijar(h, r, quien):
        if not (0 <= h[0] < COLS and 0 <= h[1] < ROWS):
            errores.append(f"{quien}: {h} fuera de la placa")
            return
        r = alias(r)
        if h in red and red[h] != r:
            errores.append(f"{quien}: {hueco(*h)} ya es de la red {red[h]!r}, no {r!r}")
        red[h] = r
    activas = [p for p in PIEZAS if not p.reserva or p.reserva in montado]
    for p in PIEZAS:   # los huecos de las reservas tampoco se pueden ocupar
        for n, x, y, r in p.pines:
            q = f"{p.ref}.{n}"
            if (x, y) in duenio:
                errores.append(f"{q}: {hueco(x, y)} ya lo usa {duenio[(x, y)]}")
            duenio[(x, y)] = q
    for p in activas:
        for n, x, y, r in p.pines:
            if r:
                fijar((x, y), r, f"{p.ref}.{n}")
            else:
                red.setdefault((x, y), "")
    # Cuerpos (de todas, tambien las reservas): que no se pisen, que no tapen patas y nada bajo la Heltec
    def rect_de(p):
        if p.cuerpo:
            return p.cuerpo
        if p.circulo:
            cx, cy, r = p.circulo
            return (cx - r, cy - r, cx + r, cy + r)
        return None
    def solapa(a, b, m=0.0):
        return a[0] < b[2] - m and b[0] < a[2] - m and a[1] < b[3] - m and b[1] < a[3] - m
    for i, p in enumerate(PIEZAS):
        ra = rect_de(p)
        if not ra:
            continue
        if p.tipo != "Z" and solapa(ra, HELTEC_RECT):
            errores.append(f"{p.ref}: el cuerpo queda bajo la Heltec")
        for q in PIEZAS[i + 1:]:
            rb = rect_de(q)
            if rb and solapa(ra, rb, 0.02) and {p.tipo, q.tipo} != {"Z"}:
                errores.append(f"{p.ref} y {q.ref}: los cuerpos se pisan")
        for q in PIEZAS:
            if q is p:
                continue
            for n, x, y, r in q.pines:
                if ra[0] < x < ra[2] and ra[1] < y < ra[3]:
                    errores.append(f"{p.ref}: el cuerpo tapa la pata {q.ref}.{n} ({hueco(x, y)})")
    # Union-find
    padre = {}
    def raiz(a):
        padre.setdefault(a, a)
        while padre[a] != a:
            padre[a] = padre[padre[a]]
            a = padre[a]
        return a
    def unir(a, b):
        padre[raiz(a)] = raiz(b)
    for r, camino in puentes(montado):
        for i, h in enumerate(camino):
            fijar(h, r, f"puente {r}")
            if i:
                (x0, y0), (x1, y1) = camino[i - 1], h
                if abs(x0 - x1) + abs(y0 - y1) != 1:
                    errores.append(f"puente {r}: {hueco(x0, y0)}→{hueco(x1, y1)} no son vecinos")
                unir(camino[i - 1], h)
    for nombre, (a, b), cond in CAPUCHONES:
        if activo(cond, montado):
            if red.get(a) != red.get(b):
                errores.append(f"puente {nombre}: une {red.get(a)!r} con {red.get(b)!r}")
            unir(a, b)
    for r, a, b in cables(montado):
        for h in (a, b):
            if alias(red.get(h)) != alias(r):
                errores.append(f"cable {r}: el extremo {hueco(*h)} es de la red {red.get(h)!r}")
        unir(a, b)
    # Internas de la Heltec: GND con GND y 3V3 con 3V3
    unir(h3(1), h2(1))
    unir(h3(2), h3(3))
    # Cada red, una sola pieza conectada
    por_red = {}
    for h, r in red.items():
        if r:
            por_red.setdefault(r, set()).add(raiz(h))
    for r, raices in sorted(por_red.items()):
        if len(raices) > 1:
            grupos = {}
            for h, rr in red.items():
                if rr == r:
                    grupos.setdefault(raiz(h), []).append(hueco(*h))
            errores.append(f"red {r}: {len(raices)} trozos sin unir: " + " | ".join(", ".join(g[:6]) for g in grupos.values()))
    # Redes que solo tienen un pin (salvo las que solo tocan la Heltec: pines sin usar o de reservas sin montar)
    for r in sorted(por_red):
        pines = [(p, n) for p in activas for n, x, y, rr in p.pines if alias(rr) == r]
        if len(pines) < 2 and not all(p.tipo == "Z" for p, n in pines):
            errores.append(f"red {r}: solo la toca {[f'{p.ref}.{n}' for p, n in pines]}")
    return errores, red, duenio


# ----------------------------------------------------------------------------
# Dibujo
# ----------------------------------------------------------------------------
P = 20.0          # pixeles por paso en el SVG
M = 56.0          # margen
COLOR_RED = {"GND": "#333", "3V3": "#c62828", "TONO": "#c2185b", "VOL_IN": "#c2185b",
             "VOL_CUR": "#c2185b", "AUD": "#c2185b", "AUR_T": "#c2185b", "AUR_R": "#c2185b", "TN": "#ad1457",
             "AMP_SD": "#6d4c41", "ADC": "#00838f", "BIAS": "#00838f", "DEC_IN": "#00838f", "DEC_CUR": "#00838f",
             "KEY_GPIO": "#e65100", "KEY_A": "#e65100", "KEY_C": "#e65100", "KEY_E": "#e65100",
             "DIT": "#6a1b9a", "DAH": "#6a1b9a", "ENC_CLK": "#1565c0", "ENC_DT": "#1565c0", "ENC_SW": "#1565c0",
             "GPS_TX": "#2e7d32", "GPS_RX": "#2e7d32", "SCL": "#d08700", "SDA": "#d08700", "TJ": "#00838f", "SJ": "#00838f",
             "5V": "#d08700", "USB_DET": "#d08700"}

def esc(t):
    return html.escape(str(t))

class Dib:
    def __init__(s, espejo):
        s.espejo, s.e = espejo, []
        s.w = COLS * P + 2 * M
        s.h = ROWS * P + 2 * M
    def X(s, x):
        return M + ((COLS - 1 - x) if s.espejo else x) * P + P / 2
    def Y(s, y):
        return M + y * P + P / 2
    def add(s, t):
        s.e.append(t)
    def rect(s, r, fill, stroke, sw=1.2, rx=3, dash=None, op=1):
        x0, y0, x1, y1 = r
        a, b = sorted((s.X(x0), s.X(x1)))
        d = f' stroke-dasharray="{dash}"' if dash else ""
        s.add(f'<rect x="{a:.1f}" y="{s.Y(y0):.1f}" width="{b - a:.1f}" height="{(y1 - y0) * P:.1f}" rx="{rx}" '
              f'fill="{fill}" fill-opacity="{op}" stroke="{stroke}" stroke-width="{sw}"{d}/>')
    def text(s, x, y, t, size=9, anchor="middle", weight="normal", fill="#222", px=False, rot=0):
        X, Y = (x, y) if px else (s.X(x), s.Y(y))
        tr = f' transform="rotate({rot} {X:.1f} {Y:.1f})"' if rot else ""
        s.add(f'<text x="{X:.1f}" y="{Y:.1f}" font-size="{size}" text-anchor="{anchor}" font-weight="{weight}" '
              f'fill="{fill}"{tr}>{esc(t)}</text>')
    def svg(s):
        return f'<svg viewBox="0 0 {s.w:.0f} {s.h:.0f}" xmlns="http://www.w3.org/2000/svg">' + "".join(s.e) + "</svg>"

def rejilla(d, red):
    # Placa
    d.add(f'<rect x="{M - 6}" y="{M - 6}" width="{COLS * P + 12}" height="{ROWS * P + 12}" rx="6" fill="#e9f1e4" stroke="#7a9a6a" stroke-width="1.5"/>')
    for x in range(COLS):
        d.text(x, -1.75, LETRAS[x], 8.5, fill="#667")
        d.text(x, ROWS + 0.45, LETRAS[x], 8.5, fill="#667")
    for y in range(ROWS):
        for X in (M - 34, M + COLS * P + 34):
            d.add(f'<text x="{X:.1f}" y="{d.Y(y) + 3:.1f}" font-size="8" text-anchor="middle" fill="#667">{y + 1}</text>')
    for x in range(COLS):
        for y in range(ROWS):
            d.add(f'<circle cx="{d.X(x):.1f}" cy="{d.Y(y):.1f}" r="4.2" fill="#d6c08a" stroke="#b39a5e" stroke-width="0.8"/>'
                  f'<circle cx="{d.X(x):.1f}" cy="{d.Y(y):.1f}" r="1.6" fill="#fff"/>')

def vista_superior(red):
    d = Dib(False)
    rejilla(d, red)
    # Zonas
    d.rect(ZONA_AISLADA, "#ffe0c2", "#e65100", 1.2, 4, "4 3", 0.6)
    d.text(ZONA_AISLADA[0] + 0.1, ZONA_AISLADA[3] + 0.45, "aislado (emisora)", 7, "start", "bold", "#e65100")
    # Piezas (cuerpos)
    for p in PIEZAS:
        if p.tipo == "Z":
            d.rect(p.cuerpo, "#222", "#000", 1, 2, None, 0.85)
            continue
        col = {"R": "#f2e3c6", "D": "#ff9f80", "C": "#ffe08a", "CE": "#5c7cfa", "U": "#333", "RV": "#4dabf7",
               "P": "#444"}.get(p.tipo, "#ccc")
        borde = "#d08700" if p.opcional else "#333"
        dash = "3 2" if p.opcional else None
        if p.circulo:
            cx, cy, r = p.circulo
            d.add(f'<circle cx="{d.X(cx):.1f}" cy="{d.Y(cy):.1f}" r="{r * P:.1f}" fill="{col}" fill-opacity="0.85" '
                  f'stroke="{borde}" stroke-width="1.2"' + (f' stroke-dasharray="{dash}"' if dash else "") + '/>')
        elif p.cuerpo:
            d.rect(p.cuerpo, col, borde, 1.2, 3 if p.tipo != "P" else 1, dash, 0.9)
        # Patas
        for n, x, y, r in p.pines:
            d.add(f'<circle cx="{d.X(x):.1f}" cy="{d.Y(y):.1f}" r="3" fill="#888" stroke="#222" stroke-width="0.8"/>')
            if p.tipo == "R" and p.circulo is None:
                pass
    # Patas unidas al cuerpo de las resistencias y diodos (hilo)
    for p in PIEZAS:
        if p.tipo in ("R", "D") and len(p.pines) == 2 and p.circulo is None:
            (_, xa, ya, _), (_, xb, yb, _) = p.pines
            d.add(f'<line x1="{d.X(xa):.1f}" y1="{d.Y(ya):.1f}" x2="{d.X(xb):.1f}" y2="{d.Y(yb):.1f}" stroke="#777" stroke-width="1.4"/>')
            if p.reserva:
                d.rect(p.cuerpo, "#fff8e6", "#d08700", 1.3, 3, "3 2")
            else:
                d.rect(p.cuerpo, {"R": "#f2e3c6", "D": "#ff9f80"}[p.tipo], "#333", 1.1, 3)
            if p.tipo == "D":
                # banda del catodo
                xk, yk = [(x, y) for n, x, y, r in p.pines if n == "K"][0]
                xa_, ya_ = [(x, y) for n, x, y, r in p.pines if n == "A"][0]
                if ya_ == yk:
                    bx = (p.cuerpo[2] - 0.25) if xk > xa_ else (p.cuerpo[0] + 0.1)
                    d.rect((bx, p.cuerpo[1], bx + 0.15, p.cuerpo[3]), "#222", "#222", 0.5, 0)
                else:
                    by = (p.cuerpo[3] - 0.25) if yk > ya_ else (p.cuerpo[1] + 0.1)
                    d.rect((p.cuerpo[0], by, p.cuerpo[2], by + 0.15), "#222", "#222", 0.5, 0)
        if p.tipo == "R" and p.circulo is not None:
            (_, xa, ya, _), (_, xb, yb, _) = p.pines
            d.add(f'<line x1="{d.X(xa):.1f}" y1="{d.Y(ya):.1f}" x2="{d.X(xb):.1f}" y2="{d.Y(yb):.1f}" stroke="#777" stroke-width="1.4"/>')
            cx, cy, r = p.circulo
            d.add(f'<circle cx="{d.X(cx):.1f}" cy="{d.Y(cy):.1f}" r="{r * P:.1f}" fill="#f2e3c6" stroke="#333" stroke-width="1.1"/>')
    # Heltec (silueta)
    d.rect(HELTEC_RECT, "#2e5e2e", "#1b3d1b", 1.6, 6, "6 4", 0.13)
    hx = (HELTEC_RECT[0] + HELTEC_RECT[2]) / 2
    d.text(hx, 12.2, "HELTEC V3", 13, "middle", "bold", "#1b3d1b")
    d.text(hx, 13.1, "(OLED hacia arriba)", 9, "middle", "normal", "#1b3d1b")
    d.text(hx, 14.4, "debajo: solo cables", 8.5, "middle", "italic", "#1b3d1b")
    d.text(hx, 15.2, "por la otra cara", 8.5, "middle", "italic", "#1b3d1b")
    d.text(hx, 24.0, "▼ USB", 10, "middle", "bold", "#1b3d1b")
    # Pines de la Heltec
    for p in PIEZAS:
        if p.tipo == "Z":
            for n, x, y, r in p.pines:
                izq = (x == min(XJ3, XJ2))
                nombre = (J3 if x == XJ3 else J2)[int(n) - 1]
                usado = bool(r)
                d.text(x + (0.62 if izq else -0.62), y + 0.25, f"{n} {nombre}" if izq else f"{nombre} {n}",
                       7, "start" if izq else "end", "bold" if usado else "normal", "#123312" if usado else "#7d9a7d")
                if n == "1":
                    d.add(f'<rect x="{d.X(x) - 4:.1f}" y="{d.Y(y) - 4:.1f}" width="8" height="8" fill="none" stroke="#ff0" stroke-width="1.5"/>')
    # Etiquetas
    etiquetas = {
        "P7": (0.5, -0.95, "P7 DECOD"), "RV2": (1.0, 5.05, "RV2"), "C2": (2.0, 7.35, "C2 1µ"),
        "R5": (4.0, 5.2, "R5"), "R4": (5.0, 5.2, "R4"), "R6": (5.0, 9.3, "R6 1k"), "D1": (8.0, 6.7, "D1"),
        "D2": (7.0, 4.7, "D2"),
        "P4": (12.0, -0.95, "P4 VOL"), "P3": (16.5, -0.95, "P3 AURIC."), "R1": (10.0, 4.2, "R1 1k"),
        "C1": (12.0, 1.75, "C1"), "R2": (15.0, 4.3, "R2 220"), "R3": (15.0, 5.3, "R3 220"),
        "R9": (16.85, 2.3, "R9"),
        "P2": (-0.45, 11.0, "P2 PALAS"), "P6": (-0.45, 14.5, "P6 KEY"), "P5": (-0.45, 18.5, "P5 AMPLI"),
        "P8": (-0.45, 23.5, "P8 BME280"), "U1": (2.5, 14.75, "U1"), "R7": (6.0, 13.3, "R7 330"),
        "R10": (6.0, 12.3, "R10 10k"), "P1": (20.45, 10.0, "P1 ENCODER"), "P9": (20.45, 15.5, "P9 GPS"),
        "R11": (5.0, 20.3, "R11 47k"), "R12": (4.0, 22.3, "R12 68k"),
    }
    for ref, (x, y, t) in etiquetas.items():
        rot = -90 if ref in ("P2", "P6", "P5", "P8") else (90 if ref in ("P1", "P9") else 0)
        fill = "#fff" if ref in ("U1", "RV2") else ("#b06f00" if ref in ("R11", "R12") else "#111")
        if rot:
            d.text(M - 14 if rot < 0 else M + COLS * P + 14, d.Y(y), t, 8, "middle", "bold", "#1a1a1a", px=True, rot=rot)
        else:
            d.text(x, y, t, 7.5, "middle", "bold", fill)
    # Nombres de los pines de las tiras
    for p in PIEZAS:
        if p.tipo == "P":
            for n, x, y, r in p.pines:
                d.text(x, y + 0.22, n, 5.6, "middle", "bold", "#fff")
    # Puentes de quitar y poner (jumper caps)
    for nombre, (a, b), cond in CAPUCHONES:
        x0, x1 = sorted((d.X(a[0]), d.X(b[0])))
        y0, y1 = sorted((d.Y(a[1]), d.Y(b[1])))
        d.add(f'<rect x="{x0 - 6:.1f}" y="{y0 - 6:.1f}" width="{x1 - x0 + 12:.1f}" height="{y1 - y0 + 12:.1f}" rx="2.5" '
              f'fill="#1565c0" fill-opacity="0.85" stroke="#0d3c7a" stroke-width="1"/>')
    d.text(2.15, 1.3, "P10", 7.5, "start", "bold", "#111")
    d.text(2.15, 2.05, "trafo", 6.5, "start", "normal", "#1565c0")
    d.text(4.3, 18.95, "reserva USB", 7, "start", "bold", "#d08700")
    # Marca de la pata 1 de U1
    d.add(f'<circle cx="{d.X(3.45):.1f}" cy="{d.Y(13.95):.1f}" r="2.2" fill="#fff"/>')
    # Polaridad de C1
    d.text(12.0, 0.9, "+", 10, "middle", "bold", "#fff")
    return d.svg()

def vista_inferior(red):
    d = Dib(True)
    rejilla(d, red)
    d.rect(HELTEC_RECT, "#2e5e2e", "#1b3d1b", 1.4, 6, "6 4", 0.07)
    # Pads soldados (patas)
    for p in PIEZAS:
        for n, x, y, r in p.pines:
            d.add(f'<circle cx="{d.X(x):.1f}" cy="{d.Y(y):.1f}" r="5" fill="#bbb" stroke="#555" stroke-width="1"/>')
    # Puentes desnudos
    for t in PUENTES:
        r, camino = t[0], t[1]
        pts = " ".join(f"{d.X(x):.1f},{d.Y(y):.1f}" for x, y in camino)
        col = COLOR_RED.get(r, "#555")
        if len(t) > 2:
            d.add(f'<polyline points="{pts}" fill="none" stroke="#d08700" stroke-width="4" stroke-linecap="round" stroke-linejoin="round" stroke-dasharray="3 3"/>')
            continue
        d.add(f'<polyline points="{pts}" fill="none" stroke="#9aa0a6" stroke-width="5.5" stroke-linecap="round" stroke-linejoin="round"/>')
        d.add(f'<polyline points="{pts}" fill="none" stroke="{col}" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/>')
    # Cables aislados
    lista = [(str(i), r, a, b, False) for i, (r, a, b) in enumerate(CABLES, 1)]
    lista += [(LETRAS[i], r, a, b, True) for i, (r, a, b, c) in enumerate(CABLES_RESERVA)]
    for i, r, a, b, res in lista:
        col = "#d08700" if res else COLOR_RED.get(r, "#555")
        x1, y1, x2, y2 = d.X(a[0]), d.Y(a[1]), d.X(b[0]), d.Y(b[1])
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        # curva suave para distinguirlos de los puentes
        dx, dy = x2 - x1, y2 - y1
        L = max((dx * dx + dy * dy) ** 0.5, 1)
        k = min(26, L * 0.18)
        cx, cy = mx - dy / L * k, my + dx / L * k
        d.add(f'<path d="M{x1:.1f},{y1:.1f} Q{cx:.1f},{cy:.1f} {x2:.1f},{y2:.1f}" fill="none" stroke="#fff" stroke-width="5" stroke-linecap="round"/>')
        d.add(f'<path d="M{x1:.1f},{y1:.1f} Q{cx:.1f},{cy:.1f} {x2:.1f},{y2:.1f}" fill="none" stroke="{col}" stroke-width="2.6" stroke-linecap="round" stroke-dasharray="{"2 3" if res else "7 3"}"/>')
        for X, Y in ((x1, y1), (x2, y2)):
            d.add(f'<circle cx="{X:.1f}" cy="{Y:.1f}" r="3" fill="{col}"/>')
        tx, ty = (mx + cx) / 2, (my + cy) / 2
        d.add(f'<circle cx="{tx:.1f}" cy="{ty:.1f}" r="7.5" fill="#fff" stroke="{col}" stroke-width="1.4"/>')
        d.text(tx, ty + 3.2, i, 8.5, "middle", "bold", col, px=True)
    # Rotulos de los conectores
    for p in PIEZAS:
        if p.tipo in ("P", "Z"):
            continue
    d.text(d.w / 2, 14, "VISTA POR EL LADO DE LAS SOLDADURAS (espejo: la columna A queda a la derecha)", 10, "middle", "bold", "#333", px=True)
    return d.svg()


# ----------------------------------------------------------------------------
# Pagina
# ----------------------------------------------------------------------------
NOMBRE_RED = {
    "GND": "masa", "3V3": "3,3 V", "TONO": "GPIO7 (tono)", "VOL_IN": "entrada del volumen",
    "VOL_CUR": "cursor del volumen", "AUD": "audio tras C1", "AUR_T": "auriculares T", "AUR_R": "auriculares R",
    "TN": "TN del jack → amplificador", "AMP_SD": "GPIO38 (SD del amplificador)", "ADC": "GPIO6 (ADC)",
    "BIAS": "polarización 1,65 V", "DEC_IN": "entrada del decodificador", "DEC_CUR": "cursor de RV2",
    "KEY_GPIO": "GPIO2 (KEY)", "KEY_A": "ánodo del PC817", "KEY_C": "colector (emisora)", "KEY_E": "emisor (emisora)",
    "DIT": "GPIO4 (DIT)", "DAH": "GPIO5 (DAH)", "ENC_CLK": "GPIO19 (CLK)", "ENC_SW": "GPIO20 (SW)",
    "ENC_DT": "GPIO48 (DT)", "GPS_TX": "GPIO47 (TX del GPS)", "GPS_RX": "GPIO39 (RX del GPS)", "SCL": "GPIO42 (SCL)", "SDA": "GPIO41 (SDA)",
    "TJ": "punta del jack del decodificador", "SJ": "malla del jack del decodificador", "5V": "5 V (pin 5V, solo con USB)",
    "USB_DET": "GPIO33 (detección de USB)",
}

def quien_en(h):
    for p in PIEZAS:
        for n, x, y, r in p.pines:
            if (x, y) == h:
                if p.tipo == "Z":
                    nombre = (J3 if x == XJ3 else J2)[int(n) - 1]
                    return f"Heltec {'J3' if x == XJ3 else 'J2'}-{n} ({nombre})"
                return f"{p.ref}.{n}"
    return ""

def pagina(errores):
    css = """
@page { size: A4 landscape; margin: 8mm; }
* { box-sizing: border-box; }
body { font-family: 'Segoe UI', Arial, sans-serif; color: #222; margin: 0; font-size: 9pt; }
.page { width: 281mm; height: 193mm; page-break-after: always; display: flex; flex-direction: column; overflow: hidden; }
.page:last-child { page-break-after: auto; }
header { display: flex; justify-content: space-between; align-items: baseline; border-bottom: 2px solid #2e5e2e; padding-bottom: 1.5mm; margin-bottom: 2.5mm; }
header h1 { font-size: 14pt; margin: 0; color: #2e5e2e; }
header .r { font-size: 8pt; color: #666; text-align: right; }
h2 { font-size: 10.5pt; margin: 0 0 1mm 0; color: #2e5e2e; }
.fila { display: flex; gap: 5mm; flex: 1; min-height: 0; }
.fila > .svg { flex: 0 0 auto; height: 100%; }
.fila > .svg svg { height: 100%; width: auto; }
.lado { flex: 1; min-width: 0; font-size: 8.4pt; }
.lado ul { margin: 0.5mm 0 2mm 0; padding-left: 4mm; } .lado li { margin: 0.4mm 0; }
table { border-collapse: collapse; width: 100%; font-size: 7.8pt; }
th, td { border: 1px solid #cfd8cf; padding: 0.6mm 1.4mm; text-align: left; vertical-align: top; }
th { background: #eef4ee; }
.cols { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 4mm; }
.err { color: #c00; font-weight: bold; }
.ok { color: #2e7d32; font-weight: bold; }
.nota { color: #555; font-style: italic; }
.chip { display: inline-block; width: 9px; height: 9px; border-radius: 2px; vertical-align: middle; margin-right: 1mm; }
"""
    cab = lambda t: (f'<header><h1>Chispas · placa perforada · {t}</h1><div class="r">EA4ILN · placa de pads a doble cara, paso 2,54 mm · '
                     f'{COLS}×{ROWS} agujeros ({COLS * 2.54:.0f} × {ROWS * 2.54:.0f} mm)<br>CC BY-NC-SA 4.0</div></header>')
    estado = ('<p class="ok">Comprobación automática (sin reservas y con todas montadas): todas las redes unidas, sin cruces ni piezas que se pisen.</p>' if not errores else
              '<p class="err">ERRORES:</p><ul>' + "".join(f'<li class="err">{esc(e)}</li>' for e in errores) + "</ul>")

    # Pagina 1: componentes
    p1 = f"""<div class="page">{cab("lado de los componentes")}
<div class="fila"><div class="svg">{vista_superior(None)}</div><div class="lado">
<h2>Cómo leer el plano</h2>
<ul>
<li>Vista por el lado de los componentes. Columnas <b>A–{LETRAS[COLS - 1]}</b>, filas <b>1–{ROWS}</b>: un agujero se nombra como <b>C12</b>.</li>
<li>La Heltec va en dos <b>zócalos hembra</b> de 18 pines (columnas {LETRAS[XJ3]} y {LETRAS[XJ2]}), con el <b>USB abajo</b> y la OLED hacia arriba. El pin 1 de cada fila (GND, recuadro amarillo) es el más cercano al USB.</li>
<li>Bajo la Heltec no hay piezas: solo cables por la otra cara (el conector de la batería está debajo de la Heltec).</li>
<li>Los conectores <b>P1–P9</b> son tiras de pines macho: de ellos salen cables Dupont a los jacks, el encoder, el volumen, el amplificador, el BME280 y el GPS, que van en la carcasa.</li>
<li>Resistencias tumbadas a 4 pasos (10,16 mm), salvo R9, de pie. Diodos a 3 pasos con la <b>banda</b> donde se dibuja.</li>
<li><b>C1</b>: el + hacia P4 (fila 2). <b>U1</b>: el punto (pata 1) en E15, arriba a la derecha del chip.</li>
<li><b>RV2</b> con el tornillo de ajuste accesible (dejar un agujero en la tapa encima o ajustarlo antes de cerrar).</li>
<li><span class="chip" style="background:#ffe0c2;border:1px dashed #e65100"></span>Zona de la emisora (salida del PC817): nada de esta placa se une a ella.</li>
<li><span class="chip" style="background:#1565c0"></span><b>P10</b> lleva dos <b>puentes de quitar y poner</b> (jumper caps): sin trafo, unen la punta del jack con la entrada (1-2) y la malla con masa (3-4).</li>
</ul>
<h2>Reservas (la placa funciona sin ellas)</h2>
<ul>
<li><b>Trafo de aislamiento 600:600</b> (solo si hay zumbido por lazo de masa): quitar los dos puentes de P10 y conectar el primario a 1 y 3 y el secundario a 2 y 4. El trafo va en la carcasa.</li>
<li><b>Detección de USB</b> (R11 47 kΩ, R12 68 kΩ, en naranja): soldarlas con el puente de B23 y los cables A (5V) y B (GPIO33). Aparcada: sin USB quedan 0,7 V en GPIO33, poco margen (ver NOTAS). Necesita firmware.</li>
<li><b>C3</b> (10 nF, ADC a masa): por la cara de soldaduras, sobre las patas de D2 (H4 y H7).</li>
</ul>
<h2>Antes de soldar nada</h2>
<ul>
<li><b>Comprueba la orientación de la Heltec</b>: con la OLED hacia arriba y el USB hacia ti, la fila de la izquierda debe ser la de 3V3/GPIO7 (J3) y la de la derecha la de 5V/GPIO19 (J2). Si es al revés, avísame y regenero el plano en espejo.</li>
<li>Cuenta los pasos entre las dos filas de pines: deben ser <b>9</b> (22,86 mm).</li>
<li>Comprueba el orden de pines de tus módulos (KY-040, BME280, GPS, PAM8302A) con las tablas de la página 3.</li>
</ul>
{estado}
</div></div></div>"""

    # Pagina 2: soldaduras
    filas_cables = "".join(
        f'<tr><td><b>{i}</b></td><td><span class="chip" style="background:{COLOR_RED.get(r, "#555")}"></span>{esc(NOMBRE_RED.get(r, r))}</td>'
        f'<td>{hueco(*a)} <span class="nota">{esc(quien_en(a))}</span></td><td>{hueco(*b)} <span class="nota">{esc(quien_en(b))}</span></td></tr>'
        for i, (r, a, b) in enumerate(CABLES, 1))
    filas_cables += "".join(
        f'<tr style="color:#9a5a00"><td><b>{LETRAS[i]}</b></td><td><span class="chip" style="background:#d08700"></span>{esc(NOMBRE_RED.get(r, r))} (reserva {c})</td>'
        f'<td>{hueco(*a)} <span class="nota">{esc(quien_en(a))}</span></td><td>{hueco(*b)} <span class="nota">{esc(quien_en(b))}</span></td></tr>'
        for i, (r, a, b, c) in enumerate(CABLES_RESERVA))
    p2 = f"""<div class="page">{cab("lado de las soldaduras")}
<div class="fila"><div class="svg">{vista_inferior(None)}</div><div class="lado">
<h2>Puentes (línea gruesa gris)</h2>
<ul>
<li>Hilo de cobre estañado desnudo (o una pata cortada de resistencia) tumbado sobre los pads y soldado en cada uno, o un puente de estaño entre pads vecinos si son 1–2 saltos.</li>
<li>El color interior indica la red: <span class="chip" style="background:#333"></span>masa, <span class="chip" style="background:#c62828"></span>3,3 V, y el resto, la señal.</li>
<li>El bus de masa recorre la <b>fila {ROWS}</b> entera (bajo el USB) y sube por la <b>columna {LETRAS[14]}</b> por debajo de la Heltec hasta el tono.</li>
<li><span class="chip" style="background:#fff;border:2px dashed #d08700"></span>En naranja discontinuo, los puentes y cables de las reservas (letras A, B): no se montan de entrada.</li>
</ul>
<h2>Cables aislados (línea discontinua con número)</h2>
<table><tr><th>#</th><th>Red</th><th>Desde</th><th>Hasta</th></tr>{filas_cables}</table>
<p class="nota">Hilo fino con funda (wire-wrap de 0,25 mm² o similar), pegado a la placa. Pueden pasar por encima de los puentes: la funda los aísla.</p>
</div></div></div>"""

    # Pagina 3: listas
    filas_bom = []
    for p in PIEZAS:
        if p.tipo == "Z":
            continue
        filas_bom.append(f"<tr><td><b>{p.ref}</b></td><td>{esc(p.valor)}</td><td>{', '.join(f'{n}: {hueco(x, y)}' for n, x, y, r in p.pines)}</td>"
                         f"<td>{esc(p.nota)}</td></tr>")
    conectores = [
        ("P1", "Encoder KY-040", "CLK, DT, SW, +, GND", "Mismo orden que el módulo: cable recto de 5 hilos."),
        ("P2", "Jack de palas J1", "DAH, DIT, GND", "DIT = punta, DAH = anillo, GND = malla (módulo: L, R, G)."),
        ("P3", "Jack de auriculares J2 (5 patillas)", "S, TN, T, R", "TN: contacto que se abre al meter la clavija. RN libre."),
        ("P4", "Potenciómetro de volumen RV1", "3, 2, 1", "Pata 3 a R1, 2 (cursor) a C1, 1 a masa."),
        ("P5", "Amplificador PAM8302A (U2)", "VIN, GND, SD, A+", "A− a GND en el propio módulo; altavoz en sus bornas."),
        ("P6", "Jack KEY J4", "C, E", "C → punta y anillo (puente R-T en el jack); E → malla. Aislado."),
        ("P7", "Jack del decodificador J3", "T, S", "Anillo libre."),
        ("P10", "Puentes (o trafo 600:600)", "1 T, 2 entrada · 3 S, 4 masa", "Sin trafo: puentes 1-2 y 3-4. Con trafo: primario 1-3, secundario 2-4."),
        ("P8", "BME280 (GY-BME280)", "VIN, GND, SCL, SDA", "Orden del módulo. Lejos de la Heltec (calor)."),
        ("P9", "GPS NEO-6M (GY-GPS6MV2)", "VCC, RX, TX, GND", "Orden del módulo (comprobar la serigrafía)."),
    ]
    filas_con = "".join(f"<tr><td><b>{a}</b></td><td>{esc(b)}</td><td>{esc(c)}</td><td>{esc(d)}</td></tr>" for a, b, c, d in conectores)
    p3 = f"""<div class="page">{cab("componentes y conectores")}
<div class="cols" style="grid-template-columns: 1.25fr 1fr;">
<div><h2>Piezas en la placa</h2>
<table><tr><th>Ref.</th><th>Pieza</th><th>Patas</th><th>Nota</th></tr>{"".join(filas_bom)}
<tr><td><b>H3, H2</b></td><td>2 × zócalo hembra 1×18, 2,54 mm</td><td>columnas {LETRAS[XJ3]} y {LETRAS[XJ2]}, filas {heltec_y(18) + 1}–{heltec_y(1) + 1}</td><td>soldar los 18 pines aunque no se usen todos</td></tr>
</table></div>
<div><h2>Conectores hacia la carcasa (de arriba abajo o de izquierda a derecha)</h2>
<table><tr><th>Con.</th><th>Va a</th><th>Pines</th><th>Nota</th></tr>{filas_con}</table>
<h2 style="margin-top:3mm">Fuera de la placa</h2>
<ul>
<li>Batería: de la LiPo al interruptor SW1 y al conector JST de la Heltec (por debajo de ella): no pasa por esta placa.</li>
<li>Antena LoRa: el cable IPEX → SMA de la Heltec va directo a la carcasa.</li>
<li>Altavoz LS1: a las bornas del PAM8302A.</li>
</ul>
<h2>Material</h2>
<ul>
<li>Placa de pads a doble cara, cortada a {COLS}×{ROWS} agujeros (de una de 7×9 cm).</li>
<li>2 zócalos hembra 1×18; tiras macho: 5 + 3 + 4 + 3 + 4 + 2 + 2 + 4 + 4 pines (31) y una de 2×2 (P10) con 2 puentes (jumper caps).</li>
<li>Hilo estañado desnudo (0,4–0,5 mm) e hilo fino aislado; cables Dupont hembra para la carcasa.</li>
</ul></div></div></div>"""

    # Pagina 4: montaje
    p4 = f"""<div class="page">{cab("montaje y comprobaciones")}
<div class="cols">
<div><h2>Orden de montaje</h2>
<ol>
<li>Cortar la placa y limar los bordes. Marcar con rotulador la columna A y la fila 1.</li>
<li><b>Zócalos de la Heltec</b>: insertar los dos con la Heltec puesta encima (así quedan paralelos) y soldar primero las esquinas.</li>
<li><b>Puentes de masa y 3,3 V</b> (bus de la fila {ROWS}, rail de la columna B, columna {LETRAS[14]}, barra de la columna K).</li>
<li>Piezas bajas: resistencias y diodos (ojo a la banda). Luego C2, U1 (pata 1), C1 (polaridad), RV2 y R9.</li>
<li>Tiras de pines P1–P10 y los dos puentes de P10.</li>
<li>Puentes de señal y, al final, los cables aislados por orden de número.</li>
</ol>
<h2>Sin la Heltec puesta: comprobar con el polímetro</h2>
<ul>
<li>Continuidad: los dos pines 1 (GND) de los zócalos con todos los GND de P1–P9.</li>
<li><b>Sin continuidad</b> entre 3V3 y GND, ni entre P6 (C, E) y nada del resto.</li>
<li>Cada señal desde su pin del zócalo hasta su conector (tabla de la página 3).</li>
</ul></div>
<div><h2>Primer encendido</h2>
<ul>
<li>Poner la Heltec <b>sin módulos conectados</b>, alimentar por USB y comprobar que arranca la v1.06b.</li>
<li>Medir 3,3 V en el VIN de P5 y P8, y unos 1,65 V en GPIO6 (R4/R5).</li>
<li>Conectar los módulos uno a uno y probar cada función: encoder, palas (TX LIBRE), tono por altavoz y auriculares, KEY (TUNE con el polímetro en P6), BME280 (STATUS), decodificador, GPS.</li>
</ul>
<h2>Cuidados</h2>
<ul>
<li>Los pads vecinos que no se unen se tocan a 2,54 mm: revisar con lupa que no haya estaño entre ellos.</li>
<li>Ningún hilo del lado de la emisora (P6) cerca de masa.</li>
<li>El GPS va siempre alimentado: se duerme por UBX con GPIO39.</li>
</ul></div>
<div><h2>Si cambia algo</h2>
<ul>
<li>El plano lo genera <code>docs/gen_placa.py</code>: piezas, puentes y cables están en listas y el script comprueba que cada red queda unida y que nada se pisa.</li>
<li>Para la Heltec al revés, <code>J3_IZQUIERDA = False</code>.</li>
<li>Si cambia un pin en el firmware, cambiarlo también aquí y en <code>docs/gen_esquema.py</code>.</li>
<li>Las reservas se comprueban montadas y sin montar: las dos variantes tienen que salir sin errores.</li>
</ul>
<p class="nota">Plano sin probar: hecho a partir del esquema del prototipo (probado en protoboard). Medidas de los módulos y su orden de pines, a comprobar con los que tengas.</p>
</div></div></div>"""
    return f'<!doctype html><html lang="es"><head><meta charset="utf-8"><title>Chispas · placa perforada</title><style>{css}</style></head><body>{p1}{p2}{p3}{p4}</body></html>'


if __name__ == "__main__":
    errores = []
    for montado in (frozenset(), frozenset(RESERVAS)):
        e, red, duenio = comprobar(montado)
        nombre = "con " + " + ".join(sorted(montado)) if montado else "sin reservas"
        print(f"{nombre}: {len(e)} errores")
        errores += [f"({nombre}) {x}" for x in e]
    for e in errores:
        print("ERROR:", e)
    print(f"{len(PIEZAS)} piezas, {len(PUENTES)} puentes, {len(CABLES)} cables")
    if len(sys.argv) > 1:
        with open(sys.argv[1], "w", encoding="utf-8") as f:
            f.write(pagina(errores))
