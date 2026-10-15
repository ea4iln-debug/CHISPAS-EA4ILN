# SPDX-License-Identifier: GPL-3.0-or-later
# Serigrafia de chispas.kicad_pcb. Ejecutar con el Python de KiCad y KiCad CERRADO:
#   "C:/Program Files/KiCad/10.0/bin/python.exe" serigrafia.py [carpeta_del_proyecto]
# - Oculta la referencia de las piezas SMD (las monta JLCPCB con el CPL; son las que se pisan) y la deja, a 1 mm,
#   en las de agujero pasante que suelda el usuario.
# - Cara de arriba: nombre de cada conector y la nota de los puentes del trafo.
# - Cara de abajo (vacia): nombre, version, repositorio, licencia y JLCJLCJLCJLC (sitio del numero de pedido).
# Los textos sueltos de serigrafia se borran y se rehacen en cada ejecucion.
import sys
from pathlib import Path

import pcbnew

PCB = (Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent) / "chispas.kicad_pcb"
VERSION_PCB = "PCB v1.0 (2026)"
mm = pcbnew.FromMM

b = pcbnew.LoadBoard(str(PCB))

# Todas las listas antes de cualquier b.Remove() (en KiCad 10.0.7 las que se piden despues llegan rotas).
# Los textos sueltos de serigrafia son solo los de este script: se borran y se rehacen.
huellas = list(b.GetFootprints())
viejos = [d for d in b.GetDrawings() if isinstance(d, pcbnew.PCB_TEXT) and d.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS)]
for d in viejos:
    b.Remove(d)

# Referencias visibles solo las que se citan en la serigrafia (puentes del trafo y trimmer); el resto de piezas que
# suelda el usuario llevan nombre funcional (KEY, PALAS, GPS...) y las SMD las coloca JLCPCB con el CPL.
REFS_VISIBLES = {"JP1", "JP2", "R8"}
for f in huellas:
    ref = f.Reference()
    ref.SetVisible(f.GetReference() in REFS_VISIBLES)
    ref.SetTextSize(pcbnew.VECTOR2I(mm(1.0), mm(1.0)))
    ref.SetTextThickness(mm(0.15))
    # Textos propios de la huella en serigrafia ("RK097" del potenciometro, "J2"/"J3" de la Heltec): a F.Fab
    # (SetVisible no tiene efecto en los textos de huella en KiCad 10). "USB" de la Heltec se queda.
    for g in f.GraphicalItems():
        g = g.Cast()
        if isinstance(g, pcbnew.PCB_TEXT) and g.GetLayer() == pcbnew.F_SilkS and g.GetText() != "USB":
            g.SetLayer(pcbnew.F_Fab)
    if f.Value().GetLayer() == pcbnew.F_SilkS:
        f.Value().SetLayer(pcbnew.F_Fab)


def texto(t, x, y, capa=pcbnew.F_SilkS, alto=1.2, rot=0, grueso=None):
    s = pcbnew.PCB_TEXT(b)
    s.SetText(t)
    s.SetLayer(capa)
    s.SetPosition(pcbnew.VECTOR2I(mm(x), mm(y)))
    s.SetTextSize(pcbnew.VECTOR2I(mm(alto), mm(alto)))
    s.SetTextThickness(mm(grueso or alto * 0.15))
    s.SetTextAngleDegrees(rot)
    if capa == pcbnew.B_SilkS:
        s.SetMirrored(True)
    b.Add(s)


# --- Cara de arriba: conectores (posiciones segun colocar.py)
texto("KEY OUT", 115, 114.6)
texto("DECOD", 141, 114.6)
texto("KEYER", 115, 145.4)
texto("AURIC.", 133, 145.4)
texto("VOL", 150, 150.4)
texto("BME280", 188, 117.5, rot=90)
texto("GPS", 188, 134.5, rot=90)
texto("ALTAVOZ", 176, 155.2, alto=1.0)
texto("TRAFO", 169.8, 107.4, alto=0.8)
texto("cortar=trafo", 176.2, 116, alto=0.8, rot=90)
texto("AUX", 139.9, 151, alto=0.8, rot=90)

# --- Cara de abajo: identificacion (entre las dos filas de la Heltec, sin vias de cosido)
texto("CHISPAS · EA4ILN", 126, 122.5, pcbnew.B_SilkS, alto=2.5)
texto(VERSION_PCB, 126, 126.5, pcbnew.B_SilkS, alto=1.5)
texto("github.com/ea4iln-debug/CHISPAS-EA4ILN", 126, 130, pcbnew.B_SilkS, alto=1.0)
texto("Hardware: CC BY-NC-SA 4.0", 126, 132.5, pcbnew.B_SilkS, alto=1.0)
texto("JLCJLCJLCJLC", 126, 136.5, pcbnew.B_SilkS, alto=1.0)

pcbnew.SaveBoard(str(PCB), b)
print("Serigrafia lista:", PCB)
