# SPDX-License-Identifier: GPL-3.0-or-later
# Primera colocacion de las piezas en chispas.kicad_pcb y contorno de la placa.
# Ejecutar con el Python de KiCad y el editor de placas CERRADO:
#   "C:/Program Files/KiCad/10.0/bin/python.exe" colocar.py
# Vista desde arriba: borde superior = DETRAS (KEY y decodificador, al transceptor), inferior = DELANTE (palas,
# auriculares y volumen), izquierdo = USB de la Heltec (en horizontal: asi la OLED se lee derecha).
# Coordenadas en mm; esquina superior izquierda de la placa en (X0, Y0).
from pathlib import Path
import pcbnew

PCB = Path(__file__).resolve().parent.parent / "chispas.kicad_pcb"
X0, Y0, ANCHO, ALTO = 100.0, 100.0, 90.0, 60.0

# Jack PJ-307: la clavija entra por el lado -X de la huella y su cañon acaba a 8,98 mm del centro.
JACK_DETRAS = -90   # abre hacia -Y (borde superior)
JACK_DELANTE = 90   # abre hacia +Y (borde inferior)
D_JACK = 9.0

# Heltec: en la huella el USB queda en +Y a 26,35 mm del centro; con -90 apunta a -X (borde izquierdo).
HELTEC_ROT = -90
X_HELTEC = X0 + 26.35

COLOCACION = {
    # ref: (x, y, rotacion)
    "U1": (X_HELTEC, Y0 + ALTO / 2, HELTEC_ROT),
    # Agujeros M3 a 4 mm de los bordes
    "H1": (X0 + 4, Y0 + 4, 0), "H2": (X0 + ANCHO - 4, Y0 + 4, 0),
    "H3": (X0 + 4, Y0 + ALTO - 4, 0), "H4": (X0 + ANCHO - 4, Y0 + ALTO - 4, 0),
    # DETRAS: KEY (PJ1) con su optoacoplador, decodificador (PJ4) con trimmer, puentes y trafo
    "PJ1": (115, Y0 + D_JACK, JACK_DETRAS),
    "U2": (128, 106, 0), "R1": (125, 111.5, 0), "R2": (131, 111.5, 0),
    "PJ4": (141, Y0 + D_JACK, JACK_DETRAS),
    "R8": (157, 104.5, 0), "J6": (166, 110, 90), "JP1": (179, 113, 0), "JP2": (179, 117, 0),
    "C8": (153, 114, 0), "R9": (157.5, 114, 0), "R10": (162, 114, 0), "R11": (166.5, 114, 0),
    "D1": (154.5, 119, 0), "D2": (160.5, 119, 0), "C9": (166, 119, 0),
    # Conectores internos (cables): encoder, GPS, BME280 y tira de pines libres
    "J1": (156, 128, 0), "J2": (174, 128, 0), "J3": (156, 138, 0), "J4": (171, 139, 90),
    # DELANTE: palas (PJ2) con filtros, auriculares (PJ3), volumen (R7) y amplificador
    "PJ2": (115, Y0 + ALTO - D_JACK, JACK_DELANTE), "C1": (124, 150, 90), "C2": (124, 155, 90),
    "PJ3": (133, Y0 + ALTO - D_JACK, JACK_DELANTE),
    "R7": (150, Y0 + ALTO - 5, 0),   # cara del cuerpo (y local +5) en el borde; la rosca M7 atraviesa el panel
    "R3": (154, 144.5, 0), "R4": (158.5, 144.5, 0), "R5": (158.5, 147.5, 0), "C4": (163, 144.5, 0),
    "R6": (163, 147.5, 0), "C6": (163, 150.5, 0), "C7": (163, 153.5, 0),
    "U3": (168, 150, 0), "C3": (167.5, 145, 0), "C5": (171.5, 145, 0), "J5": (175, 150, 0),
}

b = pcbnew.LoadBoard(str(PCB))
mm = pcbnew.FromMM
for f in b.GetFootprints():
    ref = f.GetReference()
    if ref not in COLOCACION:
        print("SIN COLOCAR:", ref)
        continue
    x, y, rot = COLOCACION[ref]
    f.SetOrientationDegrees(rot)
    f.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
    # Agujeros de los tetones de plastico (pads sin numero) sin metalizar, como en lib/chispas.pretty
    for pad in f.Pads():
        if pad.GetNumber() == "" and pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
            pad.SetAttribute(pcbnew.PAD_ATTRIB_NPTH)

# Contorno: se rehace (borra lo que haya en Edge.Cuts)
for d in list(b.GetDrawings()):
    if d.GetLayer() == pcbnew.Edge_Cuts:
        b.Remove(d)
r = pcbnew.PCB_SHAPE(b)
r.SetShape(pcbnew.SHAPE_T_RECT)
r.SetStart(pcbnew.VECTOR2I(mm(X0), mm(Y0)))
r.SetEnd(pcbnew.VECTOR2I(mm(X0 + ANCHO), mm(Y0 + ALTO)))
r.SetLayer(pcbnew.Edge_Cuts)
r.SetWidth(mm(0.1))
b.Add(r)

pcbnew.SaveBoard(str(PCB), b)

# Comprobaciones: pin 1 de la Heltec (GND, junto al USB) a la izquierda, y solapes de patios
u1 = b.FindFootprintByReference("U1")
p1 = u1.FindPadByNumber("1").GetPosition()
print(f"Heltec pad 1 en ({pcbnew.ToMM(p1.x):.2f}, {pcbnew.ToMM(p1.y):.2f})")
