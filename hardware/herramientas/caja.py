# SPDX-License-Identifier: GPL-3.0-or-later
# Caja de CHISPAS para la PCB v1.0 (CadQuery): base y tapa en hardware/3d/ (STEP y STL), en las mismas coordenadas
# que hardware/3d/chispas.step (modelo3d.py): origen en el centro de la PCB, Z = 0 en su cara de abajo.
# Uso: ~/.venv-cad/Scripts/python.exe -I hardware/herramientas/caja.py
#
# Montaje: la PCB apoya en 4 columnas de la base; 4 tornillos M3 x 25 entran por debajo, atraviesan columna y PCB y
# roscan en las torretas de la tapa (agujero de 2,5 mm). Otros 2 (M3 x 30) cierran el compartimento de la derecha.
# Los jacks y el volumen salen por agujeros partidos: la parte de arriba es un tapon que cuelga de la tapa.
# Debajo de la PCB queda hueco para la bateria; a la derecha, compartimento para el GPS (bajo la tapa) y el altavoz
# (rejilla con 73 en Morse). Medidas sin comprobar en la pieza real: ver NOTAS "Caja".
from pathlib import Path

import cadquery as cq

SALIDA = Path(__file__).resolve().parent.parent / "3d"


def P(bx, by):
    """Coordenadas de la PCB en KiCad (mm) -> coordenadas del STEP."""
    return bx - 145.0, 130.0 - by


# ---- Medidas (mm) ----
PCB_TOP = 1.6
PARED = 2.0
HOLGURA = 0.2               # entre la PCB y la pared
COMP = 45.0                 # largo del compartimento de la derecha (GPS y altavoz)
COLUMNA = 11.0              # de la cara de arriba del suelo a la cara de abajo de la PCB (hueco de la bateria)
SUELO = 2.0
TAPA = 2.0
Z_TAPA = 20.0               # cara de abajo de la tapa (OLED arriba a 18,3: 1,7 de aire)
R_ESQUINA = 4.0

# Interior (coordenadas del STEP)
X0, X1 = P(100 - HOLGURA, 0)[0], P(190 + HOLGURA + COMP, 0)[0]
Y0, Y1 = P(0, 160 + HOLGURA)[1], P(0, 100 - HOLGURA)[1]
Z_SUELO = -COLUMNA          # cara de arriba del suelo
Z_BASE = Z_SUELO - SUELO    # cara de abajo de la caja

AGUJEROS_PCB = [P(104, 104), P(186, 104), P(104, 156), P(186, 156)]
ESQ_COMP = [(X1 - 2.75, Y1 - 2.75), (X1 - 2.75, Y0 + 2.75)]   # columnas del compartimento

# Jacks PJ-307: canon de 6 mm a 2,5 mm sobre la PCB. (x de la PCB, lado)
JACKS = [(115, "detras", "KEY OUT"), (141, "detras", "SND IN"), (115, "delante", "KEY IN"), (133, "delante", "SND OUT")]
Z_JACK = PCB_TOP + 2.5
VOL_X, Z_VOL = 150, PCB_TOP + 6.0           # potenciometro: rosca M7
USB = dict(y=0.0, z=PCB_TOP + 14.3, ancho=12.5, alto=7.0)   # boca para la funda del cable
ENCODER = P(162, 130)
TRIMMER = P(153.6, 103.2)                   # tornillo del 3296W (medido en el render)
PULSADORES = [P(105.06, 122.5), P(105.06, 137.5)]   # PRG y RST de la Heltec (posicion estimada)
Z_PULSADOR = PCB_TOP + 12.7 + 2.5           # parte de arriba del pulsador
OLED = (P(111 - 1, 0)[0], P(137 + 1, 0)[0], P(0, 134.75 + 1)[1], P(0, 120.75 - 1)[1])   # zona visible + 1 mm
ALTAVOZ = (X1 - 23.0, Y0 + 15.5, 28.6)     # centro y diametro del alojamiento
GPS = (X1 - 44.0, Y1 - 28.2, 36.0, 26.0)   # esquina (x, y) y tamano del marco del GPS (modulo de 35 x 25)
SMA = (Y0 + 15.0, Z_SUELO + 9.0)            # en la pared de la derecha


def caja_redondeada(x0, x1, y0, y1, z0, z1, r):
    return (cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))
            .edges("|Z").fillet(r))


def cilindro_y(x, y, z, r, largo):
    """Cilindro con el eje en Y, centrado en (x, y, z)."""
    return cq.Workplane("XZ").circle(r).extrude(largo / 2, both=True).translate((x, y, z))


def cilindro_x(x, y, z, r, largo):
    return cq.Workplane("YZ").circle(r).extrude(largo / 2, both=True).translate((x, y, z))


def prisma(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False).translate((x0, y0, z0))


# ---- Cascara cerrada (paredes hasta la tapa) ----
exterior = caja_redondeada(X0 - PARED, X1 + PARED, Y0 - PARED, Y1 + PARED, Z_BASE, Z_TAPA, R_ESQUINA)
interior = prisma(X0, X1, Y0, Y1, Z_SUELO, Z_TAPA + 1)   # esquinas vivas: la PCB las tiene (pared de 1,2 en ellas)
cascara = exterior.cut(interior)

# Agujeros de las paredes (antes de partir en base y tapones)
y_detras, y_delante = Y1 + PARED / 2, Y0 - PARED / 2
tapones = []   # prismas que se llevan la parte de arriba de cada agujero a la tapa
for bx, lado, _ in JACKS:
    x = P(bx, 0)[0]
    y = y_detras if lado == "detras" else y_delante
    cara = Y1 + PARED if lado == "detras" else Y0 - PARED
    cascara = cascara.cut(cilindro_y(x, y, Z_JACK, 3.25, PARED + 2))
    cascara = cascara.cut(cilindro_y(x, cara, Z_JACK, 5.0, 1.0))          # rebaje: pared de 1,5 mm
    tapones.append((x - 5.2, x + 5.2, y - PARED, y + PARED, Z_JACK, Z_TAPA))
x = P(VOL_X, 0)[0]
cascara = cascara.cut(cilindro_y(x, y_delante, Z_VOL, 3.7, PARED + 2))
# El cuerpo del potenciometro acaba en el borde de la PCB; su STEP trae la tuerca pegada al cuerpo (choca con la
# pared en las comprobaciones: es lo esperado, la pared va entre los dos). Rosca de 5 mm: asoman 2,8 para la tuerca.
tapones.append((x - 4.0, x + 4.0, y_delante - PARED, y_delante + PARED, Z_VOL, Z_TAPA))
# USB-C: ranura abierta por arriba (la cierra la tapa)
cascara = cascara.cut(prisma(X0 - PARED - 1, X0 + 1, USB["y"] - USB["ancho"] / 2, USB["y"] + USB["ancho"] / 2,
                             USB["z"] - USB["alto"] / 2, Z_TAPA + 1))
# SMA de la antena y rejilla del BME280 en la pared de la derecha
cascara = cascara.cut(cilindro_x(X1 + PARED / 2, SMA[0], SMA[1], 3.3, PARED + 2))
for i in range(5):
    y = Y1 - 12 - i * 4.0
    cascara = cascara.cut(prisma(X1 - 1, X1 + PARED + 1, y - 1.0, y + 1.0, Z_SUELO + 3, Z_SUELO + 13))

# Base = cascara sin los tapones; tapones (algo mas estrechos) a la tapa
base = cascara
for x0, x1, y0, y1, z0, z1 in tapones:
    base = base.cut(prisma(x0, x1, y0, y1, z0, z1 + 1))
piezas_tapon = [cascara.intersect(prisma(x0 + 0.15, x1 - 0.15, y0, y1, z0 + 0.1, z1)) for x0, x1, y0, y1, z0, z1 in tapones]

# Columnas de la PCB (tornillo por debajo, cabeza embutida) y del compartimento
for x, y in AGUJEROS_PCB:
    col = cq.Workplane("XY").circle(3.5).extrude(COLUMNA + SUELO).translate((x, y, Z_BASE))
    base = base.union(col)
    base = base.cut(cq.Workplane("XY").circle(1.7).extrude(40).translate((x, y, Z_BASE - 1)))
    base = base.cut(cq.Workplane("XY").circle(3.0).extrude(3.0 + 1).translate((x, y, Z_BASE - 1)))
for x, y in ESQ_COMP:
    col = cq.Workplane("XY").circle(2.75).extrude(14.0 - Z_BASE).translate((x, y, Z_BASE))
    base = base.union(col)
    base = base.cut(cq.Workplane("XY").circle(1.7).extrude(40).translate((x, y, Z_BASE - 1)))
    base = base.cut(cq.Workplane("XY").circle(3.0).extrude(3.0 + 1).translate((x, y, Z_BASE - 1)))

# ---- Tapa ----
tapa = caja_redondeada(X0 - PARED, X1 + PARED, Y0 - PARED, Y1 + PARED, Z_TAPA, Z_TAPA + TAPA, R_ESQUINA)
for t in piezas_tapon:
    tapa = tapa.union(t)
# Torretas hasta la PCB y hasta las columnas del compartimento
for x, y in AGUJEROS_PCB:
    tapa = tapa.union(cq.Workplane("XY").circle(3.0).extrude(Z_TAPA - PCB_TOP).translate((x, y, PCB_TOP)))
    tapa = tapa.cut(cq.Workplane("XY").circle(1.25).extrude(Z_TAPA - PCB_TOP - 1).translate((x, y, PCB_TOP - 0.1)))
for x, y in ESQ_COMP:
    tapa = tapa.union(cq.Workplane("XY").circle(2.75).extrude(Z_TAPA - 14.2).translate((x, y, 14.2)))
    tapa = tapa.cut(cq.Workplane("XY").circle(1.25).extrude(Z_TAPA - 14.2 - 0.5).translate((x, y, 14.1)))
# Ventana de la OLED con chaflan por fuera
x0, x1, y0, y1 = OLED
tapa = tapa.cut(prisma(x0, x1, y0, y1, Z_TAPA - 1, Z_TAPA + TAPA + 1))
tapa = tapa.cut(cq.Workplane("XY").rect(x1 - x0, y1 - y0).workplane(offset=1.0).rect(x1 - x0 + 2, y1 - y0 + 2)
                .loft().translate(((x0 + x1) / 2, (y0 + y1) / 2, Z_TAPA + TAPA - 1.0)))
# Eje del encoder y ajuste del trimmer
tapa = tapa.cut(cq.Workplane("XY").circle(3.75).extrude(10).translate((*ENCODER, Z_TAPA - 1)))
tapa = tapa.cut(cq.Workplane("XY").circle(1.75).extrude(10).translate((*TRIMMER, Z_TAPA - 1)))
# Pulsadores PRG y RST: lengueta flexible (0,8 mm de ranura, 1,2 de grueso) con un tetón que llega al pulsador
for x, y in PULSADORES:
    sentido = 1 if y > 0 else -1          # la lengueta crece hacia fuera de la ventana de la OLED
    largo, ancho = 11.0, 5.0
    ya, yb = sorted((y - sentido * 2.5, y + sentido * largo))
    ranura = (prisma(x - ancho / 2 - 0.8, x + ancho / 2 + 0.8, ya - 0.8, yb, Z_TAPA - 1, Z_TAPA + TAPA + 1)
              .cut(prisma(x - ancho / 2, x + ancho / 2, ya, yb + 1, Z_TAPA - 2, Z_TAPA + TAPA + 2)))
    if sentido < 0:
        ranura = (prisma(x - ancho / 2 - 0.8, x + ancho / 2 + 0.8, ya, yb + 0.8, Z_TAPA - 1, Z_TAPA + TAPA + 1)
                  .cut(prisma(x - ancho / 2, x + ancho / 2, ya - 1, yb, Z_TAPA - 2, Z_TAPA + TAPA + 2)))
    tapa = tapa.cut(ranura)
    tapa = tapa.cut(prisma(x - ancho / 2, x + ancho / 2, ya, yb, Z_TAPA - 1, Z_TAPA + 0.8))   # adelgaza a 1,2
    tapa = tapa.union(cq.Workplane("XY").circle(1.5).extrude(Z_TAPA + 0.8 - (Z_PULSADOR + 0.3))
                      .translate((x, y, Z_PULSADOR + 0.3)))
    tapa = tapa.union(cq.Workplane("XY").circle(2.0).extrude(0.6).translate((x, y, Z_TAPA + TAPA)))   # botón
# Altavoz: aro para centrarlo y rejilla con 73 en Morse (--... ...--)
ax, ay, ad = ALTAVOZ
tapa = tapa.union(cq.Workplane("XY").circle(ad / 2 + 1.2).circle(ad / 2).extrude(3.0).translate((ax, ay, Z_TAPA - 3.0)))
for fila, simbolos in ((3.5, "--..."), (-3.5, "...--")):
    largo_fila = sum(6 if s == "-" else 2 for s in simbolos) + 1.8 * (len(simbolos) - 1)
    x = ax - largo_fila / 2
    for s in simbolos:
        lon = 6 if s == "-" else 2
        forma = cq.Workplane("XY").slot2D(lon, 2.0) if s == "-" else cq.Workplane("XY").circle(1.0)
        tapa = tapa.cut(forma.extrude(TAPA + 2).translate((x + lon / 2, ay + fila, Z_TAPA - 1)))
        x += lon + 1.8
# Marco del GPS (antena hacia arriba, pegado a la tapa)
gx, gy, gl, ga = GPS
marco = prisma(gx - 1.2, gx + gl + 1.2, gy - 1.2, gy + ga + 1.2, Z_TAPA - 2.0, Z_TAPA).cut(
    prisma(gx, gx + gl, gy, gy + ga, Z_TAPA - 3, Z_TAPA + 1))
tapa = tapa.union(marco)
# Rotulos grabados (0,6 mm)
def rotulo(texto, x, y, tam=3.2, giro=0):
    global tapa
    t = (cq.Workplane("XY").text(texto, tam, -0.6, kind="bold", halign="center", valign="center")
         .rotate((0, 0, 0), (0, 0, 1), giro).translate((x, y, Z_TAPA + TAPA)))
    tapa = tapa.cut(t)
for bx, lado, nombre in JACKS:
    rotulo(nombre, P(bx, 0)[0], Y1 - 2.5 if lado == "detras" else Y0 + 2.5, 2.6)
rotulo("VOL", P(VOL_X, 0)[0], Y0 + 2.5, 2.6)
rotulo("MENU / CLICK", ENCODER[0], ENCODER[1] - 8.0, 2.4)
rotulo("EXIT", X0 - 0.4, PULSADORES[0][1] + 5.0, 2.2, 90)
rotulo("RST", X0 - 0.4, PULSADORES[1][1] - 5.0, 2.2, 90)
rotulo("CHISPAS", ax, Y1 - 9.0, 4.5)
rotulo("EA4ILN", ax, Y1 - 15.0, 3.0)

# ---- Salida ----
SALIDA.mkdir(exist_ok=True)
for nombre, pieza in (("caja_base", base), ("caja_tapa", tapa)):
    cq.exporters.export(pieza, str(SALIDA / f"{nombre}.step"))
    cq.exporters.export(pieza, str(SALIDA / f"{nombre}.stl"), tolerance=0.05, angularTolerance=0.2)
conjunto = cq.Assembly().add(base, name="base", color=cq.Color(0.31, 0.34, 0.20)).add(
    tapa, name="tapa", color=cq.Color(0.36, 0.40, 0.24))
conjunto.save(str(SALIDA / "caja.step"))
bb = base.union(tapa).val().BoundingBox()
print(f"Caja {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm; piezas en {SALIDA}")
