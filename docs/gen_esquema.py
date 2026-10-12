# Genera el esquema de conexiones del prototipo en protoboard (HTML con SVG -> PDF con Edge)
#
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 EA4ILN. Software libre, sin garantia: ver el archivo LICENSE.
# El esquema que genera (docs/esquema_protoboard.pdf) es documentacion: CC BY-NC-SA 4.0 (ver LICENSE-HARDWARE.md).
import html, os, sys

OUT = sys.argv[1]

C_W = "#222"        # cables
C_3V3 = "#c62828"
C_GND = "#333"
C_OPT = "#d08700"
C_ISO = "#e65100"

def esc(t):
    return html.escape(str(t))


class S:
    def __init__(s, w, h, opt=False):
        s.w, s.h, s.e, s.opt = w, h, [], opt

    def add(s, x):
        s.e.append(x)

    def line(s, pts, color=C_W, width=1.6, dash=None):
        p = " ".join(f"{x},{y}" for x, y in pts)
        d = f' stroke-dasharray="{dash}"' if dash else ""
        s.add(f'<polyline points="{p}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linejoin="round"{d}/>')

    def text(s, x, y, t, size=11, anchor="start", weight="normal", fill="#222", italic=False):
        st = ' font-style="italic"' if italic else ""
        s.add(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" font-weight="{weight}" fill="{fill}"{st}>{esc(t)}</text>')

    def dot(s, x, y, color=C_W):
        s.add(f'<circle cx="{x}" cy="{y}" r="3.2" fill="{color}"/>')

    def rect(s, x, y, w, h, fill="#fff", stroke="#222", width=1.6, dash=None, rx=4):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        s.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"{d}/>')

    def gnd(s, x, y):
        s.line([(x, y), (x, y + 8)], C_GND)
        for i, w in enumerate((14, 9, 4)):
            s.line([(x - w, y + 8 + i * 4), (x + w, y + 8 + i * 4)], C_GND, 1.8)

    def vcc(s, x, y, label="3V3"):
        s.line([(x - 10, y), (x + 10, y)], C_3V3, 2.4)
        s.text(x, y - 5, label, 10, "middle", "bold", C_3V3)

    def tag(s, x, y, t, color, anchor="middle"):
        w = 7 * len(t) + 10
        x0 = x - w / 2 if anchor == "middle" else (x if anchor == "start" else x - w)
        s.add(f'<rect x="{x0}" y="{y-11}" width="{w}" height="15" rx="7" fill="{color}"/>')
        s.text(x0 + w / 2, y, t, 10, "middle", "bold", "#fff")

    # Resistencia IEC entre dos puntos alineados
    def res(s, x1, y1, x2, y2, ref, val, color=C_W, dash=None, lab_side=1):
        L, W = 38, 12
        if y1 == y2:
            m = (x1 + x2) / 2
            s.line([(x1, y1), (m - L / 2, y1)], color, dash=dash)
            s.line([(m + L / 2, y1), (x2, y1)], color, dash=dash)
            s.rect(m - L / 2, y1 - W / 2, L, W, "#fff", color, 1.6, dash, 1)
            s.text(m, y1 - 11, ref, 10, "middle", "bold", color)
            s.text(m, y1 + 21, val, 10, "middle", "normal", color)
        else:
            m = (y1 + y2) / 2
            s.line([(x1, y1), (x1, m - L / 2)], color, dash=dash)
            s.line([(x1, m + L / 2), (x1, y2)], color, dash=dash)
            s.rect(x1 - W / 2, m - L / 2, W, L, "#fff", color, 1.6, dash, 1)
            tx = x1 + 11 * lab_side
            a = "start" if lab_side > 0 else "end"
            s.text(tx, m - 2, ref, 10, a, "bold", color)
            s.text(tx, m + 11, val, 10, a, "normal", color)

    # Condensador; con polar=True, el primer punto es el +
    def cap(s, x1, y1, x2, y2, ref, val, polar=False, color=C_W, dash=None, lab_side=1):
        g, P = 4, 11
        if y1 == y2:
            m = (x1 + x2) / 2
            s.line([(x1, y1), (m - g, y1)], color, dash=dash)
            s.line([(m + g, y1), (x2, y1)], color, dash=dash)
            s.line([(m - g, y1 - P), (m - g, y1 + P)], color, 2.4)
            if polar:
                s.add(f'<rect x="{m+g}" y="{y1-P}" width="4" height="{2*P}" fill="{color}"/>')
                s.text(m - g - 9, y1 - 7, "+", 12, "middle", "bold", color)
            else:
                s.line([(m + g, y1 - P), (m + g, y1 + P)], color, 2.4)
            s.text(m, y1 - 16, ref, 10, "middle", "bold", color)
            s.text(m, y1 + 25, val, 10, "middle", "normal", color)
        else:
            m = (y1 + y2) / 2
            s.line([(x1, y1), (x1, m - g)], color, dash=dash)
            s.line([(x1, m + g), (x1, y2)], color, dash=dash)
            s.line([(x1 - P, m - g), (x1 + P, m - g)], color, 2.4)
            s.line([(x1 - P, m + g), (x1 + P, m + g)], color, 2.4)
            tx = x1 + 16 * lab_side
            a = "start" if lab_side > 0 else "end"
            s.text(tx, m - 2, ref, 10, a, "bold", color)
            s.text(tx, m + 11, val, 10, a, "normal", color)

    # Diodo vertical u horizontal: de anodo (xa,ya) a catodo (xk,yk)
    def diode(s, xa, ya, xk, yk, ref, val, lab_side=1):
        T = 9
        if xa == xk:
            m = (ya + yk) / 2
            d = 1 if yk > ya else -1
            s.line([(xa, ya), (xa, m - d * T)])
            s.line([(xa, m + d * T), (xa, yk)])
            s.add(f'<polygon points="{xa-T},{m-d*T} {xa+T},{m-d*T} {xa},{m+d*T}" fill="#fff" stroke="{C_W}" stroke-width="1.6"/>')
            s.line([(xa - T, m + d * T), (xa + T, m + d * T)], C_W, 2.4)
            tx = xa + 14 * lab_side
            a = "start" if lab_side > 0 else "end"
            s.text(tx, m - 2, ref, 10, a, "bold")
            s.text(tx, m + 11, val, 10, a)
        else:
            m = (xa + xk) / 2
            d = 1 if xk > xa else -1
            s.line([(xa, ya), (m - d * T, ya)])
            s.line([(m + d * T, ya), (xk, ya)])
            s.add(f'<polygon points="{m-d*T},{ya-T} {m-d*T},{ya+T} {m+d*T},{ya}" fill="#fff" stroke="{C_W}" stroke-width="1.6"/>')
            s.line([(m + d * T, ya - T), (m + d * T, ya + T)], C_W, 2.4)

    # Potenciometro vertical de (x,y1) [patilla 3] a (x,y2) [patilla 1]; cursor (2) a la derecha
    def pot(s, x, y1, y2, ref, val, pins=True):
        L, W = 54, 14
        m = (y1 + y2) / 2
        s.line([(x, y1), (x, m - L / 2)])
        s.line([(x, m + L / 2), (x, y2)])
        s.rect(x - W / 2, m - L / 2, W, L, "#fff", C_W, 1.6, None, 1)
        s.line([(x + 40, m), (x + 12, m)])
        s.add(f'<polygon points="{x+8},{m} {x+16},{m-5} {x+16},{m+5}" fill="{C_W}"/>')
        s.text(x - 12, m - 4, ref, 10, "end", "bold")
        s.text(x - 12, m + 9, val, 10, "end")
        if pins:
            s.text(x + 5, m - L / 2 - 4, "3", 9, "start", "normal", "#666")
            s.text(x + 5, m + L / 2 + 11, "1", 9, "start", "normal", "#666")
            s.text(x + 22, m - 5, "2", 9, "start", "normal", "#666")
        return (x + 40, m)

    # Bloque con patillas: pins = [(nombre, y)] en el lado indicado; devuelve el extremo de cada patilla
    def block(s, x, y, w, h, title, pins, side="left", fill="#f4f7fb", sub=None, dash=None, stroke="#222"):
        s.rect(x, y, w, h, fill, stroke, 1.8, dash, 6)
        s.text(x + w / 2, y + 16, title, 11, "middle", "bold")
        if sub:
            for i, t in enumerate(sub if isinstance(sub, list) else [sub]):
                s.text(x + w / 2, y + 30 + 12 * i, t, 9, "middle", "normal", "#555")
        ends = {}
        for name, py in pins:
            if side == "left":
                s.line([(x - 15, py), (x, py)], stroke)
                s.text(x + 6, py + 4, name, 10, "start", "bold")
                ends[name] = (x - 15, py)
            else:
                s.line([(x + w, py), (x + w + 15, py)], stroke)
                s.text(x + w - 6, py + 4, name, 10, "end", "bold")
                ends[name] = (x + w + 15, py)
        return ends

    def heltec(s, x, y, h, pins, side="right"):
        return s.block(x, y, 110, h, "Heltec V3", pins, side, "#e8f0e8", ["ESP32-S3"])

    def opt_region(s, x, y, w, h, label, bottom=False):
        s.rect(x, y, w, h, "#fff8e6", C_OPT, 1.6, "6 4", 8)
        s.text(x + 8, y + h - 7 if bottom else y + 13, label, 10, "start", "bold", C_OPT)

    def svg(s):
        body = "\n".join(s.e)
        return (f'<svg viewBox="0 0 {s.w} {s.h}" xmlns="http://www.w3.org/2000/svg" '
                f'font-family="Segoe UI, Arial, sans-serif">{body}</svg>')


def notes(s, x, y, lines, size=10, dy=15):
    for i, t in enumerate(lines):
        s.text(x, y + i * dy, "• " + t, size, "start", "normal", "#333")


# ----------------------------------------------------------------------------
# Vista general
# ----------------------------------------------------------------------------
def vista_general():
    s = S(1050, 500)
    # Placa
    s.rect(420, 30, 220, 450, "#e8f0e8", "#2e5e2e", 2.2, None, 10)
    s.text(530, 55, "Heltec WiFi LoRa 32 V3", 13, "middle", "bold", "#2e5e2e")
    s.text(530, 71, "ESP32-S3 · SX1262 · OLED", 10, "middle", "normal", "#2e5e2e")
    s.rect(480, 210, 100, 52, "#1b1b1b", "#1b1b1b", 1, None, 3)
    s.text(530, 240, "OLED", 11, "middle", "bold", "#9fd")
    s.text(530, 290, "PRG = GPIO0 (atrás / dormir)", 9, "middle", "normal", "#555")
    s.text(530, 304, "LED = GPIO35", 9, "middle", "normal", "#555")

    L = [("GPIO19", 105, "#1565c0"), ("GPIO20", 130, "#1565c0"), ("GPIO48", 155, "#1565c0"),
         ("GPIO4", 220, "#6a1b9a"), ("GPIO5", 245, "#6a1b9a"),
         ("GPIO47", 310, "#2e7d32"), ("GPIO3", 335, C_OPT),
         ("GPIO41", 405, C_OPT), ("GPIO42", 430, C_OPT)]
    R = [("GPIO7", 105, "#c2185b"), ("GPIO6", 190, "#00838f"), ("GPIO2", 275, C_ISO),
         ("3V3", 345, C_3V3), ("GND", 370, "#333"), ("BAT (JST)", 440, "#555")]
    for n, y, c in L:
        s.add(f'<rect x="414" y="{y-6}" width="12" height="12" fill="{c}" rx="2"/>')
        s.text(432, y + 4, n, 10, "start", "bold", c)
    for n, y, c in R:
        s.add(f'<rect x="634" y="{y-6}" width="12" height="12" fill="{c}" rx="2"/>')
        s.text(628, y + 4, n, 10, "end", "bold", c)

    def blk(x, y, w, h, title, sub, color, dash=None):
        s.rect(x, y, w, h, "#fff" if not dash else "#fff8e6", color, 2, dash, 8)
        s.text(x + 10, y + 18, title, 12, "start", "bold", color)
        for i, t in enumerate(sub):
            s.text(x + 10, y + 34 + 13 * i, t, 9.5, "start", "normal", "#444")

    # Izquierda
    blk(40, 75, 250, 100, "Encoder KY-040 (ENC1)", ["Menús: girar + pulsar", "CLK→19 · DT→20 · SW→48", "+ a 3V3 (nunca a 5 V)"], "#1565c0")
    blk(40, 195, 250, 70, "Palas (J1, jack estéreo)", ["Punta → DIT (4) · Anillo → DAH (5)", "Malla → GND · pull-up interno"], "#6a1b9a")
    blk(40, 285, 250, 70, "GPS NEO-6M (GPS1)", ["TX del GPS → GPIO47 (pendiente de llegar)", "Enable con P-MOSFET en GPIO3: opcional"], "#2e7d32")
    blk(40, 385, 250, 70, "BME280 (BME1, opcional)", ["I2C: SDA→41 · SCL→42 (Wire1), 0x76", "Montado y probado (v1.03b)"], C_OPT)
    for n, y, c in L:
        dash = "6 4" if c == C_OPT else None
        s.line([(290, y), (414, y)], c, 2, dash)
    # Derecha
    blk(760, 70, 250, 85, "Tono CW (GPIO7, PWM)", ["R1 → volumen RV1 → C1 → 220 Ω (R2, R3)", "TN de J2: ampli U2 (SD → GPIO38) + altavoz LS1", "Clavija en J2 = altavoz en silencio"], "#c2185b")
    blk(760, 165, 250, 70, "Decodificador (GPIO6, ADC1)", ["← altavoz ext. de la emisora (J3)", "RV2 nivel · C2 · 1,65 V · R6 + D1/D2"], "#00838f")
    blk(760, 250, 250, 70, "Salida KEY (GPIO2)", ["R7 330 Ω → PC817 (U1) → J4 · R10 a GND", "→ toma KEY de la emisora (aislada)"], C_ISO)
    blk(760, 330, 250, 55, "Raíles de la protoboard", ["Rojo = 3V3 · Azul = GND (nada a 5 V)"], "#555")
    blk(760, 410, 250, 60, "Batería LiPo (BAT1)", ["Interruptor SW1 en el cable +", "Comprobar polaridad del JST antes"], "#555")
    for n, y, c in R:
        ty = {105: 105, 190: 190, 275: 275, 345: 352, 370: 365, 440: 440}[y]
        s.line([(646, y), (700, y), (700, ty), (760, ty)], c, 2)
    s.text(530, 466, "Pines agrupados por función,", 9, "middle", "italic", "#555")
    s.text(530, 478, "no por su posición en la placa", 9, "middle", "italic", "#555")
    return s.svg()


# ----------------------------------------------------------------------------
# Encoder
# ----------------------------------------------------------------------------
def p_encoder():
    s = S(520, 330)
    h = s.heltec(20, 40, 210, [("3V3", 75), ("GPIO19", 110), ("GPIO20", 145), ("GPIO48", 180), ("GND", 215)])
    k = s.block(345, 40, 145, 210, "ENC1 · KY-040", [("+", 75), ("CLK", 110), ("DT", 145), ("SW", 180), ("GND", 215)], "left", "#eef3fb")
    s.add('<circle cx="447" cy="140" r="24" fill="#ddd" stroke="#555" stroke-width="1.5"/>')
    s.add('<circle cx="447" cy="140" r="12" fill="#bbb" stroke="#555" stroke-width="1.2"/>')
    s.line([(447, 140), (447, 118)], "#555", 2)
    for n, c in (("3V3", C_3V3), ("GPIO19", "#1565c0"), ("GPIO20", "#1565c0"), ("GPIO48", "#1565c0"), ("GND", C_GND)):
        pass
    pairs = [("3V3", "+", C_3V3), ("GPIO19", "CLK", C_W), ("GPIO20", "DT", C_W), ("GPIO48", "SW", C_W), ("GND", "GND", C_W)]
    for a, b, c in pairs:
        s.line([h[a], k[b]], c, 1.8)
    s.tag(240, 70, "3V3", C_3V3)
    s.tag(240, 105, "CLK", "#1565c0")
    s.tag(240, 140, "DT", "#1565c0")
    s.tag(240, 175, "SW", "#1565c0")
    notes(s, 20, 272, [
        "CLK y DT llevan pull-up de 10 kΩ en el módulo; SW no: INPUT_PULLUP en GPIO48.",
        "Orden físico de los pines del KY-040: CLK, DT, SW, +, GND.",
        "GPIO19/20 son USB D−/D+ del S3, pero funcionan como entradas (probado en placa).",
    ])
    return s.svg()


# ----------------------------------------------------------------------------
# Palas
# ----------------------------------------------------------------------------
def p_palas():
    s = S(520, 330)
    h = s.heltec(20, 50, 200, [("GPIO4", 90), ("GPIO5", 150), ("GND", 220)])
    j = s.block(280, 55, 70, 190, "J1", [("T", 90), ("R", 150), ("S", 220)], "left", "#f3eef8")
    s.text(315, 110, "jack", 9, "middle", "normal", "#555")
    s.text(315, 122, "3,5 mm", 9, "middle", "normal", "#555")
    s.text(315, 134, "estéreo", 9, "middle", "normal", "#555")
    for a, b in (("GPIO4", "T"), ("GPIO5", "R"), ("GND", "S")):
        s.line([h[a], j[b]])
    s.tag(205, 85, "DIT", "#6a1b9a")
    s.tag(205, 145, "DAH", "#6a1b9a")
    # Palas
    s.rect(375, 60, 125, 185, "#fff", "#6a1b9a", 1.4, "5 4", 8)
    s.text(437, 238, "Manipulador (clavija)", 9.5, "middle", "bold", "#6a1b9a")
    for y, n in ((90, "DIT"), (150, "DAH")):
        s.line([(350, y), (405, y)])
        s.add(f'<circle cx="409" cy="{y}" r="4" fill="#fff" stroke="#222" stroke-width="1.5"/>')
        s.add(f'<circle cx="451" cy="{y}" r="4" fill="#fff" stroke="#222" stroke-width="1.5"/>')
        s.line([(412, y - 2), (449, y - 14)], C_W, 2)
        s.line([(455, y), (480, y)])
        s.text(430, y - 18, n, 10, "middle", "bold", "#6a1b9a")
    s.line([(480, 90), (480, 220), (350, 220)])
    s.dot(480, 150)
    s.text(474, 200, "común", 9, "end", "normal", "#555")
    notes(s, 20, 275, [
        "Pull-up interno en GPIO4 y GPIO5: no hacen falta resistencias.",
        "Convención: punta = punto (DIT), anillo = raya (DAH); se puede invertir por firmware.",
        "Llave recta: punta y malla (va a GPIO4). Módulos de jack: L = punta, R = anillo, G = malla.",
    ])
    return s.svg()


# ----------------------------------------------------------------------------
# GPS
# ----------------------------------------------------------------------------
def p_gps():
    s = S(520, 345)
    h = s.heltec(20, 40, 230, [("3V3", 70), ("GPIO3", 135), ("GPIO47", 205), ("GND", 245)])
    s.text(75, 150, "(opcional)", 8.5, "middle", "italic", C_OPT)
    g = s.block(395, 40, 105, 230, "GPS1", [("VCC", 70), ("TX", 205), ("RX", 225), ("GND", 245)], "left", "#eef7ee",
                ["NEO-6M", "GY-GPS6MV2"])
    s.text(447, 130, "(pendiente", 9, "middle", "italic", "#2e7d32")
    s.text(447, 142, "de llegar)", 9, "middle", "italic", "#2e7d32")
    # Region opcional
    s.opt_region(160, 42, 218, 128, "OPCIONAL: enable del GPS", bottom=True)
    # Q1
    sx, dx, gy = 255, 325, 112
    s.line([h["3V3"], (sx, 70)], C_3V3, 1.8)
    s.line([(sx, 70), (268, 70), (268, 92)])
    s.line([(dx, 70), (312, 70), (312, 92)])
    s.line([(260, 92), (320, 92)], C_W, 3)
    s.line([(260, 101), (320, 101)], C_W, 2.2)
    s.line([(290, 101), (290, gy + 20)])
    s.add('<polygon points="290,93 285,84 295,84" fill="#222"/>')  # flecha canal P (hacia fuera)
    s.line([(290, 84), (290, 70)], "#222", 1)
    s.text(258, 64, "S", 10, "middle", "bold")
    s.text(322, 64, "D", 10, "middle", "bold")
    s.text(296, 128, "G", 10, "start", "bold")
    s.text(335, 108, "Q1", 10, "start", "bold", C_OPT)
    s.text(335, 120, "AO3401A", 9, "start", "normal", C_OPT)
    s.text(335, 131, "canal P", 9, "start", "normal", C_OPT)
    s.line([(dx, 70), g["VCC"]], C_3V3, 1.8)
    # R8 de puerta a 3V3
    s.res(205, 70, 205, 135, "R8", "100 kΩ", C_OPT, lab_side=-1)
    s.dot(205, 70)
    s.line([h["GPIO3"], (290, 135), (290, gy + 20)], C_OPT, 1.8)
    s.dot(205, 135, C_OPT)
    # Puente sin Q1
    s.line([(185, 70), (185, 26), (360, 26), (360, 70)], "#888", 1.4, "4 3")
    s.dot(185, 70)
    s.dot(360, 70)
    s.text(272, 20, "sin Q1: puente directo 3V3 → VCC", 9, "middle", "italic", "#666")
    s.line([h["GPIO47"], g["TX"]])
    s.tag(240, 200, "NMEA 9600 bd", "#2e7d32")
    s.line([g["RX"], (365, 225)])
    s.text(362, 229, "×", 13, "end", "bold", "#c00")
    s.text(347, 229, "sin conectar", 9, "end", "italic", "#888")
    s.line([h["GND"], g["GND"]])
    notes(s, 20, 287, [
        "Q1 en SOT-23: 1 = G, 2 = S, 3 = D (comprobar la serigrafía del adaptador DIP).",
        "GPIO3 en BAJO = GPS encendido. R8 lo deja apagado al arrancar.",
        "Si la placa se reinicia al encender el GPS: 1 kΩ en serie con G y 100 nF de G a 3V3 (no comprados).",
        "Al apagarlo: Serial1.end() para no alimentar el GPS por su TX.",
    ])
    return s.svg()


# ----------------------------------------------------------------------------
# BME280
# ----------------------------------------------------------------------------
def p_bme():
    s = S(520, 345)
    h = s.heltec(25, 45, 210, [("3V3", 80), ("GPIO41", 125), ("GPIO42", 170), ("GND", 215)])
    b = s.block(360, 45, 130, 210, "BME1 · BME280", [("VIN", 80), ("SDA", 125), ("SCL", 170), ("GND", 215)], "left", "#fff",
                ["GY-BME280-3.3"])
    s.line([h["3V3"], b["VIN"]], C_3V3, 1.8)
    s.line([h["GPIO41"], b["SDA"]])
    s.line([h["GPIO42"], b["SCL"]])
    s.line([h["GND"], b["GND"]])
    s.tag(250, 120, "SDA", C_OPT)
    s.tag(250, 165, "SCL", C_OPT)
    notes(s, 25, 280, [
        "Segundo bus I2C (Wire1) en 41/42 (J3-8/7). Montado y probado (12 oct 2026, v1.03b).",
        "Pull-ups ya en el módulo. El firmware distingue BME280 (0x60) de BMP280 (sin humedad).",
        "No usar el bus del OLED (pads 17/18): depende de Vext y la OLED de la placa 1 es delicada.",
    ])
    return s.svg()


# ----------------------------------------------------------------------------
# Tono: volumen, auriculares y salida al amplificador (TN de J2)
# ----------------------------------------------------------------------------
def p_tono():
    s = S(1050, 395)
    h = s.heltec(20, 60, 250, [("GPIO7", 100), ("GND", 290)])
    s.res(145, 100, 265, 100, "R1", "1 kΩ")
    s.line([(265, 100), (330, 100)])
    w = s.pot(330, 100, 250, "RV1", "10 kΩ log")
    s.text(330, 268, "volumen", 9, "middle", "italic", "#555")
    s.line([(330, 250), (330, 290)])
    s.line([w, (405, w[1])])
    s.cap(405, 175, 475, 175, "C1", "10 µF 16 V", polar=True)
    s.line([(475, 175), (525, 175)])
    s.dot(525, 175)
    s.line([(525, 175), (525, 130), (560, 130)])
    s.line([(525, 175), (525, 220), (560, 220)])
    s.res(560, 130, 660, 130, "R2", "220 Ω")
    s.res(560, 220, 660, 220, "R3", "220 Ω")
    s.line([(660, 130), (735, 130)])
    s.line([(660, 220), (735, 220)])
    # Jack conmutado
    j = s.block(750, 70, 130, 210, "J2 · jack conmutado", [("T", 130), ("R", 220), ("S", 260)], "left", "#fbeef3",
                ["3,5 mm, 5 patillas"])
    s.line([(750, 130), (735, 130)])
    for y, n in ((130, "TN"), (220, "RN")):
        s.line([(772, y), (800, y)], "#555", 1.4)
        s.line([(800, y), (858, y - 8)], "#555", 1.8)
        s.add(f'<circle cx="862" cy="{y}" r="3" fill="#fff" stroke="#555"/>')
        s.line([(865, y), (880, y)], "#555", 1.4)
        s.line([(880, y), (895, y)])
        s.text(888, y - 6, n, 10, "middle", "bold")
    s.text(815, 168, "se abren al", 8.5, "middle", "italic", "#555")
    s.text(815, 179, "meter clavija", 8.5, "middle", "italic", "#555")
    s.line([(895, 130), (965, 130), (965, 180)])
    s.text(905, 245, "RN: sin conectar", 9, "start", "italic", "#888")
    s.line([(895, 220), (902, 220)])
    # Salida al amplificador (5b): sin clavija, el tono llega a A+ de U2 por TN
    s.rect(925, 180, 95, 46, "#fff", "#c2185b", 1.8, None, 4)
    s.text(972, 198, "a A+ de U2", 10, "middle", "bold", "#c2185b")
    s.text(972, 213, "amplificador (5b)", 8.5, "middle", "italic", "#555")
    # Masa
    s.line([(735, 260), (720, 260), (720, 290)])
    s.line([h["GND"], (720, 290)])
    for x in (330, 720):
        s.dot(x, 290)
    s.gnd(525, 290)
    s.dot(525, 290)
    # Auriculares
    s.add('<path d="M790 300 q25 -28 50 0" fill="none" stroke="#c2185b" stroke-width="3"/>')
    s.rect(783, 298, 12, 18, "#c2185b", "#c2185b", 1, None, 3)
    s.rect(835, 298, 12, 18, "#c2185b", "#c2185b", 1, None, 3)
    s.text(815, 334, "auriculares", 9, "middle", "italic", "#c2185b")
    s.tag(205, 70, "PWM 850 Hz", "#c2185b")
    s.text(560, 192, "AUD", 9, "middle", "bold", "#c2185b")
    notes(s, 20, 342, [
        "C1: el + (patilla larga) hacia el potenciómetro. RV1: patilla 3 a R1, 1 a GND, 2 (cursor) a C1.",
        "Sin clavija, T está unido a TN y suena el altavoz (por U2); al meter los auriculares se abre TN y el altavoz calla.",
        "Montado y probado. Al principio TN iba a un buzzer piezo pasivo (BZ1), pero a 3,3 V sonaba demasiado bajo: descartado.",
    ], 10)
    s.text(530, 390, "RV1 regula a la vez el altavoz y los auriculares.", 10, "middle", "normal", "#333")
    return s.svg()


# ----------------------------------------------------------------------------
# Amplificador y altavoz (montados el 12 oct 2026), en el contacto TN de J2
# ----------------------------------------------------------------------------
def p_ampli():
    s = S(1050, 365)
    j = s.block(20, 100, 150, 80, "J2 · jack", [("TN", 140)], "right", "#fbeef3", ["contacto TN"])
    s.text(20, 205, "(R2 y el resto del circuito, como arriba)", 9, "start", "italic", "#555")
    s.line([j["TN"], (345, 140)])
    s.dot(280, 140)
    s.res(280, 140, 280, 240, "R9", "10 kΩ", lab_side=-1)
    s.gnd(280, 240)
    # U2: patillas de entrada a la izquierda; alimentacion arriba y abajo; salida a la derecha
    u = s.block(360, 50, 170, 200, "U2 · PAM8302A", [("SD", 80), ("A+", 140), ("A−", 190)], "left", "#fff",
                ["amplificador mono 2,5 W"])
    # SD a GPIO38: el firmware apaga U2 fuera de CW/LoRa/TRX y al dormir (si no, gasta ~4 mA siempre, tambien dormido)
    s.line([u["SD"], (200, 80)])
    s.text(195, 76, "GPIO38", 10, "end", "bold")
    s.text(195, 89, "J3-11", 9, "end", "normal", "#666")
    s.line([u["A−"], (315, 190), (315, 205)])
    s.gnd(315, 205)
    s.line([(410, 50), (410, 28)], C_3V3, 1.8)
    s.vcc(410, 28)
    s.text(416, 45, "VIN", 10, "start", "bold")
    s.line([(480, 250), (480, 262)])
    s.gnd(480, 262)
    s.text(486, 244, "GND", 10, "start", "bold")
    for y, n in ((120, "OUT+"), (170, "OUT−")):
        s.text(524, y + 4, n, 10, "end", "bold")
        s.line([(530, y), (680, y)])
    # Altavoz LS1
    s.line([(680, 120), (680, 135), (690, 135)])
    s.line([(680, 170), (680, 155), (690, 155)])
    s.rect(690, 125, 18, 40, "#fff", C_W, 1.8, None, 1)
    s.add('<polygon points="708,125 738,100 738,190 708,165" fill="#fff" stroke="#222" stroke-width="1.8"/>')
    s.text(750, 140, "LS1", 11, "start", "bold")
    s.text(750, 154, "altavoz 8 Ω", 9.5, "start")
    s.text(750, 167, "0,5–1 W, 28–40 mm", 9.5, "start")
    s.tag(610, 112, "salida en puente", "#c2185b")
    notes(s, 20, 300, [
        "TN → A+, R9 de A+ a GND (que la entrada no quede al aire con la clavija metida), A− a GND.",
        "VIN a 3V3 (el pin 5V no tiene tensión con batería). OUT+/OUT− van en puente: ninguna de las dos a GND.",
        "RV1 sigue de volumen. Si satura con poco giro: bajar el trimmer de ganancia de U2 (si lo trae) o 47 kΩ en serie con A+.",
        "Comprobar las patillas del módulo que llegue. Con volumen alto, vigilar que la OLED no parpadee (picos en el 3V3).",
        "SD a GPIO38: BAJO = apagado (< 1 µA). El firmware lo enciende solo para el tono; sin SD gastaría ~4 mA, también dormido.",
    ], 9.5, 14)
    return s.svg()


# ----------------------------------------------------------------------------
# Salida KEY
# ----------------------------------------------------------------------------
def p_key():
    s = S(1050, 345)
    s.rect(380, 30, 650, 225, "#fff3e8", "#fff3e8", 0, None, 6)
    s.text(1020, 48, "LADO DE LA EMISORA: no unir con GND de la Heltec", 10, "end", "bold", C_ISO)
    h = s.heltec(20, 70, 160, [("GPIO2", 110), ("GND", 200)])
    s.res(145, 110, 265, 110, "R7", "330 Ω")
    u = s.block(290, 75, 180, 160, "U1 · PC817", [], "left", "#fff")
    s.line([(265, 110), (290, 110)])
    s.line([h["GND"], (290, 200)])
    # R10: pull-down de GPIO2 (LED del PC817 apagado mientras el pin esta al aire: arranque, RST)
    s.dot(160, 110)
    s.res(160, 110, 160, 200, "R10", "10 kΩ")
    s.dot(160, 200)
    for t, x, y, a in (("1", 296, 106, "start"), ("2", 296, 196, "start"), ("4", 464, 106, "end"), ("3", 464, 196, "end")):
        s.text(x, y, t, 9, a, "normal", "#666")
    # LED interno
    s.line([(290, 110), (335, 110), (335, 130)])
    s.diode(335, 130, 335, 180, "", "")
    s.line([(335, 180), (335, 200), (290, 200)])
    for y in (145, 162):
        s.line([(352, y), (378, y)], "#e6a100", 1.6)
        s.add(f'<polygon points="384,{y} 376,{y-4} 376,{y+4}" fill="#e6a100"/>')
    # Fototransistor
    s.line([(400, 132), (400, 178)], C_W, 3)
    s.line([(400, 145), (425, 128), (425, 110), (470, 110)])
    s.line([(400, 165), (425, 182), (425, 200), (470, 200)])
    s.add('<polygon points="425,182 414,180 420,172" fill="#222"/>')
    s.line([(388, 60), (388, 250)], C_ISO, 1.6, "6 4")
    s.text(392, 248, "aislamiento", 9, "start", "italic", C_ISO)
    s.line([(470, 110), (485, 110)])
    s.line([(470, 200), (485, 200)])
    j = s.block(660, 80, 80, 150, "J4", [("T", 110), ("R", 155), ("S", 200)], "left", "#fff")
    s.text(700, 180, "jack 3,5", 9, "middle", "normal", "#555")
    s.line([(485, 110), j["T"]])
    s.line([(485, 200), j["S"]])
    # Puente R-T: unas emisoras manipulan por la punta y otras por el anillo (la uSDR, por el anillo)
    s.line([(620, 110), (620, 155), j["R"]])
    s.add('<circle cx="620" cy="110" r="3" fill="#222"/>')
    s.text(570, 104, "colector → punta", 9.5, "middle", "normal", C_ISO)
    s.text(570, 194, "emisor → malla", 9.5, "middle", "normal", C_ISO)
    s.text(612, 150, "puente R-T", 9, "end", "italic", C_ISO)
    # Cable y emisora
    s.line([(740, 155), (830, 155)], "#555", 4)
    s.text(785, 145, "cable M-M", 9, "middle", "italic", "#555")
    s.rect(830, 105, 180, 100, "#fff", C_ISO, 2, None, 8)
    s.text(920, 132, "Emisora", 12, "middle", "bold", C_ISO)
    s.text(920, 150, "toma KEY", 10, "middle")
    s.text(920, 166, "modo LLAVE RECTA", 10, "middle", "bold")
    s.text(920, 182, "(no keyer interno)", 9, "middle", "italic", "#555")
    s.tag(205, 80, "KEY", C_ISO)
    notes(s, 20, 280, [
        "GPIO2 en ALTO = LED del PC817 encendido (≈ 6 mA) = emisora manipulando. Debe estar en BAJO desde el arranque.",
        "PC817: 1 = ánodo, 2 = cátodo, 3 = emisor, 4 = colector (punto o muesca junto a la patilla 1).",
        "R10 deja el LED apagado mientras GPIO2 está al aire (arranque, RST): sin ella se midieron picos de 0,3–0,5 V.",
        "El firmware solo activa KEY en la pantalla TRX (OPERAR y QSO) y la corta si sigue activa más de 10 s.",
        "Puente R-T en J4: la uSDR manipula por el anillo; así vale para punta o anillo. Probado con la uSDR, en llave recta.",
    ])
    return s.svg()


# ----------------------------------------------------------------------------
# Decodificador
# ----------------------------------------------------------------------------
def p_deco():
    s = S(1050, 440)
    j = s.block(20, 95, 80, 170, "J3", [("T", 125), ("R", 185), ("S", 240)], "right", "#eaf6f7")
    s.text(60, 150, "jack", 9, "middle", "normal", "#555")
    s.text(60, 162, "3,5 mm", 9, "middle", "normal", "#555")
    # Anillo libre: con salida mono el anillo ya toca la malla; con salida estereo, puentearlo uniria L y R
    s.text(120, 181, "libre", 8.5, "start", "italic", "#888")
    s.text(20, 70, "← cable M-M desde la salida", 9.5, "start", "italic", "#00838f")
    s.text(20, 83, "de altavoz ext. de la emisora", 9.5, "start", "italic", "#00838f")
    s.line([j["T"], (200, 125)])
    w = s.pot(200, 125, 245, "RV2", "10 kΩ")
    s.text(210, 262, "trimmer: nivel", 9, "start", "italic", "#555")
    s.line([(200, 245), (200, 290)])
    s.line([w, (290, w[1])])
    s.cap(290, 185, 360, 185, "C2", "1 µF")
    s.line([(360, 185), (430, 185)])
    s.dot(430, 185)
    s.text(438, 178, "1,65 V", 9.5, "start", "bold", "#00838f")
    s.res(430, 60, 430, 185, "R4", "10 kΩ", lab_side=-1)
    s.res(430, 185, 430, 290, "R5", "10 kΩ", lab_side=-1)
    s.line([(430, 185), (480, 185)])
    s.res(480, 185, 590, 185, "R6", "1 kΩ")
    s.line([(590, 185), (660, 185)])
    s.dot(660, 185)
    s.diode(660, 185, 660, 60, "D1", "1N4148")
    s.diode(660, 290, 660, 185, "D2", "1N4148")
    s.line([(660, 185), (885, 185)])
    # C3 opcional
    s.dot(760, 185, C_OPT)
    s.cap(760, 185, 760, 290, "C3", "10 nF", color=C_OPT, dash="4 3")
    s.text(778, 262, "opcional,", 8.5, "start", "italic", C_OPT)
    s.text(778, 273, "no comprado", 8.5, "start", "italic", C_OPT)
    h = s.block(900, 35, 120, 280, "Heltec V3", [("3V3", 60), ("GPIO6", 185), ("GND", 290)], "left", "#e8f0e8", ["ESP32-S3"])
    for t, y in (("pin 2 · 3", 76), ("pin 17", 185), ("pin 1", 290)):
        s.text(1014, y + 4, t, 8.5, "end", "normal", "#666")
    s.text(960, 310, "conector J3 de la placa", 8.5, "middle", "italic", "#666")
    s.line([(430, 60), h["3V3"]], C_3V3, 1.8)
    s.dot(430, 60, C_3V3)
    s.dot(660, 60, C_3V3)
    s.line([j["S"], (140, 240), (140, 290), h["GND"]])
    for x in (200, 430, 660, 760):
        s.dot(x, 290)
    s.dot(140, 290)
    s.tag(820, 180, "ADC1", "#00838f")
    s.vcc(430, 50)
    s.line([(430, 50), (430, 60)], C_3V3, 1.6)
    notes(s, 20, 337, [
        "Pines de la Heltec en su conector J3 (no confundir con el jack J3): GPIO6 = pin 17, 3V3 = pin 2 o 3, GND = pin 1 (el del pad cuadrado).",
        "RV2 (trimmer 3296): patilla 3 a la punta de J3, patilla 1 a GND, cursor (2, la central) a C2. Empezar con el cursor cerca de GND.",
        "D1: banda (cátodo) a 3V3. D2: banda a GPIO6. C2 sin polaridad. R4 y R5 dejan la entrada en 1,65 V (mitad del ADC).",
        "Anillo de J3 sin conectar: con salida mono ya toca la malla; puentearlo con la punta uniría los dos canales de una salida estéreo.",
        "J3 une la masa de la emisora a GND. Si hay zumbido (lazo de masa): trafo de aislamiento 600:600 (no comprado).",
        "Al conectar J3 se calla el altavoz de la emisora: para seguir oyéndola hace falta un divisor 1 → 2 (no comprado).",
    ])
    return s.svg()


# ----------------------------------------------------------------------------
# Alimentacion
# ----------------------------------------------------------------------------
def p_alim():
    s = S(1050, 240)
    # Bateria
    x0 = 80
    s.line([(x0, 70), (x0, 100)], C_3V3, 1.8)
    s.line([(x0 - 22, 100), (x0 + 22, 100)], C_W, 2.6)
    s.line([(x0 - 12, 110), (x0 + 12, 110)], C_W, 4)
    s.line([(x0 - 22, 120), (x0 + 22, 120)], C_W, 2.6)
    s.line([(x0 - 12, 130), (x0 + 12, 130)], C_W, 4)
    s.line([(x0, 130), (x0, 170)])
    s.text(x0 + 30, 96, "+", 13, "start", "bold", C_3V3)
    s.text(x0 - 30, 112, "BAT1", 10, "end", "bold")
    s.text(x0 - 30, 125, "LiPo 3,7 V", 10, "end")
    s.text(x0 - 30, 138, "3000 mAh", 10, "end")
    s.line([(x0, 70), (250, 70)], C_3V3, 1.8)
    s.add('<circle cx="254" cy="70" r="4" fill="#fff" stroke="#222" stroke-width="1.5"/>')
    s.add('<circle cx="304" cy="70" r="4" fill="#fff" stroke="#222" stroke-width="1.5"/>')
    s.line([(257, 68), (300, 52)], C_W, 2)
    s.text(279, 44, "SW1 · SS12D00", 10, "middle", "bold")
    s.line([(308, 70), (520, 70)], C_3V3, 1.8)
    s.line([(x0, 170), (520, 170)])
    s.text(170, 64, "cable rojo (+)", 9, "middle", "italic", C_3V3)
    s.text(300, 164, "cable negro (−)", 9, "middle", "italic", "#333")
    b = s.block(535, 40, 200, 160, "Heltec V3", [("BAT +", 70), ("BAT −", 170)], "left", "#e8f0e8",
                ["conector JST 1,25 mm 2 pines", "(cargador integrado por USB)"])
    s.rect(790, 40, 230, 160, "#fff4f4", C_3V3, 1.6, None, 8)
    s.text(905, 62, "ANTES DE ENCHUFAR", 11, "middle", "bold", C_3V3)
    for i, t in enumerate(["Mira con el polímetro el + del conector", "de la batería y compáralo con la", "serigrafía de la Heltec. Hay baterías",
                           "con los cables al revés: invertida", "estropea la placa.", "", "Con SW1 apagado la batería no carga."]):
        s.text(800, 84 + 15 * i, t, 9.5, "start", "normal", "#333")
    return s.svg()


# ----------------------------------------------------------------------------
# Pagina HTML
# ----------------------------------------------------------------------------
PINES = [
    ("2", "Salida KEY", "R7 330 Ω → PC817 (U1); R10 10 kΩ a GND", "Salida, en BAJO desde el arranque; solo la activa la pantalla TRX", ""),
    ("3", "Enable del GPS", "Puerta de Q1 AO3401A + R8 100 kΩ a 3V3", "Salida, BAJO = encendido", "opc"),
    ("4", "Pala DIT", "Punta de J1", "Entrada con pull-up interno", ""),
    ("5", "Pala DAH", "Anillo de J1", "Entrada con pull-up interno", ""),
    ("6", "Entrada del decodificador", "R6 1 kΩ, D1/D2, polarización 1,65 V", "ADC1 por DMA a 8 kHz (pantalla TRX)", ""),
    ("7", "Tono CW", "R1 1 kΩ → RV1 → ampli U2 + altavoz / auriculares", "PWM (LEDC) 850 Hz (CONF: 400–1000)", ""),
    ("19", "Encoder CLK", "KY-040 CLK", "Entrada (pull-up en el módulo)", ""),
    ("20", "Encoder DT", "KY-040 DT", "Entrada (pull-up en el módulo)", ""),
    ("38", "Apagado del amplificador", "SD de U2 (PAM8302A)", "Salida, BAJO = apagado (fuera de CW/LoRa/TRX y al dormir)", ""),
    ("41", "BME280 SDA", "Wire1", "I2C, 0x76 (opcional; montado)", ""),
    ("42", "BME280 SCL", "Wire1", "I2C (opcional; montado)", ""),
    ("47", "RX del GPS", "TX del NEO-6M", "UART1 RX, 9600 bd", ""),
    ("48", "Pulsador del encoder", "KY-040 SW", "Entrada con pull-up interno", ""),
]

BOM = [
    ("R1", "Resistencia 1 kΩ", "Tono, antes de RV1", ""), ("R6", "Resistencia 1 kΩ", "Serie hacia GPIO6", ""),
    ("R2, R3", "Resistencia 220 Ω", "Auriculares T y R", ""), ("R7", "Resistencia 330 Ω", "LED del PC817", ""),
    ("R10", "Resistencia 10 kΩ", "Pull-down de GPIO2 (KEY)", ""),
    ("R4, R5", "Resistencia 10 kΩ", "Polarización 1,65 V", ""), ("RV1", "Potenciómetro 10 kΩ log (A10K)", "Volumen", ""),
    ("RV2", "Trimmer multivuelta 10 kΩ (3296)", "Nivel del decodificador", ""), ("C1", "Electrolítico 10 µF 16 V", "Bloqueo de continua", ""),
    ("C2", "Poliéster / X7R 1 µF 50 V", "Acoplo de la señal", ""), ("D1, D2", "1N4148", "Protección de GPIO6", ""),
    ("U1", "PC817 DIP-4", "Salida KEY aislada", ""), ("J1, J3, J4", "Módulo jack 3,5 mm estéreo", "Palas, audio, KEY", ""),
    ("J2", "Jack 3,5 mm estéreo conmutado (5 pat.)", "Auriculares", ""),
    ("ENC1", "Encoder KY-040", "Menús", ""), ("GPS1", "NEO-6M GY-GPS6MV2", "Posición / hora", "pendiente"),
    ("SW1", "Interruptor SS12D00", "On/off batería", ""), ("BAT1", "LiPo 3,7 V 3000 mAh JST 1,25", "Alimentación", ""),
    ("Q1, R8", "AO3401A + adaptador SOT-23 + 100 kΩ", "Enable del GPS", "opc"), ("BME1", "BME280 GY-BME280-3.3 (opcional)", "Sensor ambiental", ""),
    ("U2", "Módulo amplificador PAM8302A (mono, 2,5 W)", "Altavoz, desde el TN de J2", ""),
    ("LS1", "Altavoz 8 Ω 0,5–1 W, 28–40 mm", "Tono", ""), ("R9", "Resistencia 10 kΩ", "Pull-down de la entrada de U2", ""),
]

CSS = """
@page { size: A4 landscape; margin: 9mm; }
* { box-sizing: border-box; }
body { font-family: 'Segoe UI', Arial, sans-serif; color: #222; margin: 0; }
.page { width: 279mm; height: 191mm; page-break-after: always; display: flex; flex-direction: column; overflow: hidden; }
.page:last-child { page-break-after: auto; }
header { display: flex; justify-content: space-between; align-items: baseline; border-bottom: 2px solid #2e5e2e; padding-bottom: 2mm; margin-bottom: 3mm; }
header h1 { font-size: 15pt; margin: 0; color: #2e5e2e; }
header .r { font-size: 8.5pt; color: #666; text-align: right; }
h2 { font-size: 11pt; margin: 0 0 1mm 0; color: #2e5e2e; }
.grid2 { display: grid; grid-template-columns: 1fr 1fr; gap: 3mm 5mm; }
.panel { border: 1px solid #cfd8cf; border-radius: 2mm; padding: 2mm 3mm; break-inside: avoid; }
.panel.opt { border: 1.5px dashed #d08700; background: #fffdf6; }
.panel svg { width: 100%; height: auto; display: block; }
table { border-collapse: collapse; width: 100%; font-size: 8.6pt; }
th, td { border: 1px solid #ccc; padding: 1mm 1.6mm; text-align: left; vertical-align: top; }
th { background: #2e5e2e; color: #fff; }
tr.opc td { background: #fff6df; color: #6d4c00; }
tr.probar td:first-child { background: #e3f0ff; }
.badge { display: inline-block; font-size: 7.5pt; font-weight: bold; padding: 0 1.5mm; border-radius: 2mm; color: #fff; }
.b-opc { background: #d08700; } .b-probar { background: #1565c0; } .b-pend { background: #2e7d32; }
ul { margin: 1mm 0 0 0; padding-left: 5mm; font-size: 9pt; } li { margin-bottom: 1mm; }
.cols { display: grid; grid-template-columns: 1.1fr 1fr; gap: 5mm; flex: 1; }
.legend { font-size: 8.5pt; color: #444; margin-top: 1mm; }
.legend span { margin-right: 4mm; }
.sw { display:inline-block; width: 7mm; height: 0; border-top: 2px solid; vertical-align: middle; margin-right: 1mm; }
"""

def header(titulo, n, total):
    return (f'<header><h1>{esc(titulo)}</h1><div class="r">Chispas EA4ILN · prototipo en protoboard · '
            f'firmware v1.06b<br>12 oct 2026 · hoja {n} de {total}</div></header>')


def badge(k):
    return {"opc": '<span class="badge b-opc">OPCIONAL</span>', "probar": '<span class="badge b-probar">PROBAR</span>',
            "pendiente": '<span class="badge b-pend">PENDIENTE</span>'}.get(k, "")


def fit(svg, h):
    return svg.replace("<svg ", f'<svg style="height:{h}mm;width:100%" ', 1)


def main():
    T = 7
    pages = []
    pages.append(f'''<div class="page">{header("Esquema de conexiones · vista general", 1, T)}
<div class="panel">{fit(vista_general(), 128)}</div>
<div class="legend" style="margin-top:3mm">
<span><i class="sw" style="border-color:#c62828"></i>3V3</span><span><i class="sw" style="border-color:#222"></i>señal / GND</span>
<span><i class="sw" style="border-color:#d08700;border-top-style:dashed"></i>opcional</span><span><i class="sw" style="border-color:#e65100;border-top-style:dashed"></i>aislamiento</span>
<span class="badge b-opc">OPCIONAL</span> no comprado · <span class="badge b-pend">PENDIENTE</span> comprado, sin llegar</div>
<ul>
<li>Todo a <b>3V3</b>: ningún módulo ni raíl a 5 V (los GPIO del ESP32-S3 no admiten 5 V).</li>
<li>Los GPIO están por número, no por posición: busca cada uno en la serigrafía de tu placa. Mapa completo en la hoja 2; esquemas por bloques en las hojas 3 a 6.</li>
</ul></div>''')

    filas = "".join(
        f'<tr class="{k}"><td><b>GPIO{esc(g)}</b></td><td>{esc(f)} {badge(k)}</td><td>{esc(c)}</td><td>{esc(m)}</td></tr>'
        for g, f, c, m, k in PINES)
    pages.append(f'''<div class="page">{header("Mapa de pines", 2, T)}
<div class="cols">
<div><table><tr><th>GPIO</th><th>Función</th><th>Conecta a</th><th>Modo</th></tr>{filas}</table></div>
<div><h2>Reglas</h2><ul>
<li><b>No usar</b>: 0 (PRG), 1 (lectura de batería), 8–14 (LoRa), 17/18/21 (OLED), 26–32 (flash), 35 (LED), 36 (Vext), 37 (control de batería), 43/44 (USB serie), 45/46 (arranque).</li>
<li><b>Libres de reserva</b> (según el pinout publicado de la V3, sin probar en tu placa): 33, 34, 39, 40 (38 queda para el SD del amplificador).</li>
<li><b>GPIO19/20</b> son USB D−/D+ del S3, pero funcionan como entradas del encoder (probado con <code>prueba_pines</code>). Plan B: 39/40 o 33/34.</li>
<li><b>GPIO41/42</b> para el BME280 (Wire1, J3-8/7). No usar el bus del OLED (17/18).</li>
<li><b>GPIO3</b> es pin de arranque (origen del JTAG) pero solo cuenta con el eFuse grabado: la R8 de 100 kΩ a 3V3 no molesta. <b>GPIO45</b> nunca con pull-up: cambia la tensión de la flash y la placa no arranca.</li>
<li><b>GPIO2</b> maneja la emisora: en BAJO desde el arranque y con R10 a GND. El firmware solo la activa en la pantalla TRX (OPERAR y QSO), con corte de seguridad a los 10 s.</li>
<li>La Heltec V3 tapa casi toda una protoboard: mejor dejarla fuera y unirla con cables Dupont M-H.</li>
</ul></div></div></div>''')

    pages.append(f'''<div class="page">{header("Entradas: encoder, palas, GPS y BME280", 3, T)}
<div class="grid2">
<div class="panel"><h2>1 · Encoder KY-040</h2>{fit(p_encoder(), 72)}</div>
<div class="panel"><h2>2 · Entrada del manipulador (palas)</h2>{fit(p_palas(), 72)}</div>
<div class="panel"><h2>3 · GPS NEO-6M <span class="badge b-pend">PENDIENTE</span> + enable <span class="badge b-opc">OPCIONAL</span></h2>{fit(p_gps(), 72)}</div>
<div class="panel"><h2>4 · Sensor BME280 <span class="badge b-opc">OPCIONAL</span> (montado)</h2>{fit(p_bme(), 72)}</div>
</div></div>''')

    pages.append(f'''<div class="page">{header("Salida de audio: tono CW (volumen, auriculares, amplificador y altavoz)", 4, T)}
<div class="panel"><h2>5 · Tono CW: volumen y auriculares con silencio automático del altavoz (montado)</h2>{fit(p_tono(), 80)}</div>
<div class="panel" style="margin-top:3mm"><h2>5b · Amplificador y altavoz en el contacto TN de J2 (montado)</h2>{fit(p_ampli(), 66)}</div>
</div>''')

    pages.append(f'''<div class="page">{header("Salida KEY hacia la emisora y alimentación", 5, T)}
<div class="panel"><h2>6 · Salida KEY aislada hacia la emisora</h2>{fit(p_key(), 82)}</div>
<div class="panel" style="margin-top:3mm"><h2>8 · Alimentación por batería</h2>{fit(p_alim(), 58)}</div>
</div>''')

    pages.append(f'''<div class="page">{header("Entrada de audio del decodificador", 6, T)}
<div class="panel"><h2>7 · Entrada de audio desde la emisora (decodificador CW)</h2>{fit(p_deco(), 118)}</div>
<ul>
<li>El firmware lo usa en la pantalla <b>TRX</b>: DECODIFICAR y QSO. Tono en DECODIFICAR → menu → TONO: AUTO o fijo (400–1000 Hz).</li>
<li>Salida de la emisora: altavoz exterior o auriculares, con el volumen bajo. La barra de nivel no debe marcar «SATURA» (bajar RV2 o el volumen); sin nada conectado, con GPIO6 al aire, también puede salir.</li>
<li>Mientras se emite por KEY, el decodificador no escucha (para no descodificar el tono de la propia emisora).</li>
</ul></div>''')

    bom = "".join(f'<tr class="{"opc" if k=="opc" else ""}"><td><b>{esc(r)}</b></td><td>{esc(c)} {badge(k)}</td><td>{esc(p)}</td></tr>'
                  for r, c, p, k in BOM)
    pages.append(f'''<div class="page">{header("Componentes, montaje y comprobaciones", 7, T)}
<div class="cols">
<div><h2>Referencias del esquema → lista de la compra</h2><table><tr><th>Ref.</th><th>Componente</th><th>Para qué</th></tr>{bom}</table>
<p style="font-size:8.5pt;color:#444;margin:1.5mm 0 0">Además: protoboard de 830 puntos, cables Dupont M-M y M-H, 2 perillas, 2 cables minijack M-M estéreo (emisora → J3 y J4 → KEY).</p></div>
<div><h2>No está en la lista y conviene tener</h2><ul>
<li><b>Soldador y estaño</b>: J2 (5 patillas), SW1 en el cable de la batería y, si hace falta, las tiras de pines de la Heltec.</li>
<li><b>Divisor minijack 1 → 2</b>: para seguir oyendo la emisora con J3 conectado.</li>
<li>Opcionales sin comprar: C3 10 nF (filtro del ADC), trafo 600:600 (lazo de masa), 1 kΩ + 100 nF (arranque suave de Q1).</li>
</ul>
<h2 style="margin-top:3mm">Orden de montaje (un bloque cada vez)</h2><ul>
<li><b>0. Firmware</b> antes de cablear: palas a GPIO4/5 y GPIO2 como salida en BAJO desde el arranque. ✔</li>
<li><b>1. Raíles</b>: 3V3 y GND de la Heltec a los raíles. Polímetro: 3,3 V entre raíles y ningún cortocircuito. ✔</li>
<li><b>2. Encoder</b> y programa de prueba de pines (19, 20, 48 y reserva 38–40) por serie. ✔</li>
<li><b>3. Palas</b>: comprobar por serie DIT/DAH antes del keyer. ✔</li>
<li><b>4. Tono</b>: amplificador U2, R9 y altavoz LS1 en el TN de J2 (hoja 4), SD a GPIO38; RV1 al mínimo y subir. Luego auriculares (el altavoz debe callar). ✔</li>
<li><b>5. KEY</b> con R10: pantalla TRX → OPERAR. Primero GPIO2 con el polímetro (≈ 3,3 V con cada elemento, 0 V en reposo); luego, sin emisora, continuidad entre punta y malla de J4 al manipular. ✔ <i>Con la uSDR: puente R-T en J4 (manipula por el anillo).</i></li>
<li><b>6. Decodificador</b> (hoja 6): sin emisora, polímetro en GPIO6 ≈ 1,65 V. Luego RV2 al mínimo y volumen de la emisora bajo; pantalla TRX → DECODIFICAR: subir RV2 mirando la barra (y <code>[DECOD]</code> por serie con CONF → CONFIG → DEPURACION → DECOD en SÍ). ✔ <i>RV2 queda casi al máximo con 0,1–0,5 V de la emisora.</i></li>
<li><b>7. GPS</b> cuando llegue (y Q1/R8 si se monta el enable). <b>8. BME280</b> opcional. ✔</li>
<li><b>9. Batería</b> al final, comprobada la polaridad.</li>
</ul>
<p style="font-size:8pt;color:#777;margin-top:2mm">Probados en banco: encoder, palas, tono (R1, RV1, C1, R2, R3, J2, U2, R9 y LS1), KEY (R7, R10, U1, J4) con la uSDR, BME280 (BME1) y decodificador (C2, R4–R6, RV2, D1, D2) con audio Morse real. El resto son valores de partida sin probar. Esquema hecho a partir de la lista de la compra «opción A», con el buzzer sustituido por el amplificador U2 y el altavoz LS1.</p>
</div></div></div>''')

    doc = f'<!doctype html><html lang="es"><head><meta charset="utf-8"><title>Chispas · esquema de conexiones</title><style>{CSS}</style></head><body>{"".join(pages)}</body></html>'
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(doc)


main()
