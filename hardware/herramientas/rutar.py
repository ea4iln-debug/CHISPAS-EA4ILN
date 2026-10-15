# SPDX-License-Identifier: GPL-3.0-or-later
# Rutado automatico de chispas.kicad_pcb con Freerouting y planos de masa.
# Ejecutar con el Python de KiCad y KiCad CERRADO (toca el .kicad_pro y el .kicad_pcb):
#   "C:/Program Files/KiCad/10.0/bin/python.exe" rutar.py [carpeta_del_proyecto] [pasadas]
# Pasos:
#   1. Clases de red en el .kicad_pro: Default 0,25 mm; Potencia (GND, +3.3V, altavoz) 0,4 mm (con 0,5 no cabe
#      la de 3V3 junto a los conectores del borde derecho; 0,4 mm sobra para los <200 mA del Chispas).
#   2. Borra las pistas y vias anteriores y las zonas de masa que puso este script.
#   3. Exporta a Specctra DSN (GND incluida: asi cada pad de masa queda unido por pista aunque el plano no llegue).
#   4. Freerouting (freerouting.sh) y vuelta de las pistas (SES).
#   5. Planos de GND en las dos caras, vias de cosido en rejilla donde caben, y relleno.
import json
import re
import subprocess
import sys
from pathlib import Path

import pcbnew

HERR = Path(__file__).resolve().parent
PROY = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else HERR.parent
PASADAS = sys.argv[2] if len(sys.argv) > 2 else "100"
PCB, PRO = PROY / "chispas.kicad_pcb", PROY / "chispas.kicad_pro"
TMP = PROY / ".revision"
TMP.mkdir(exist_ok=True)
mm = pcbnew.FromMM

# --- 1. Clases de red
pro = json.loads(PRO.read_text(encoding="utf-8"))
ns = pro["net_settings"]
base = next(c for c in ns["classes"] if c["name"] == "Default")
base.update(track_width=0.25, clearance=0.2, via_diameter=0.6, via_drill=0.3)
pot = dict(base, name="Potencia", track_width=0.4, clearance=0.2, via_diameter=0.8, via_drill=0.4, priority=0)
ns["classes"] = [base, pot]
ns["netclass_patterns"] = [{"netclass": "Potencia", "pattern": p} for p in ("GND", "+3.3V", "*J5-Pin*")]
# Minimo de pista 0,15 mm (JLCPCB fabrica 0,127): Freerouting estrecha alguna pista al entrar en un pad
pro["board"]["design_settings"]["rules"]["min_track_width"] = 0.15
PRO.write_text(json.dumps(pro, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

b = pcbnew.LoadBoard(str(PCB))

# --- 2. Limpiar rutado anterior
# Pedir las dos listas ANTES de borrar nada: tras un b.Remove(), las listas que se piden despues (Zones, Tracks)
# llegan como objetos SWIG sin tipo (KiCad 10.0.7)
zonas = [z for z in b.Zones() if z.GetZoneName().startswith("GND_auto") or z.GetZoneName() == "aislamiento_KEY"]
pistas = list(b.GetTracks())
for x in zonas + pistas:
    b.Remove(x)

# --- 3. DSN sin GND
dsn, ses = TMP / "chispas.dsn", TMP / "chispas.ses"
pcbnew.ExportSpecctraDSN(b, str(dsn))

# --- 4. Freerouting
if ses.exists():
    ses.unlink()
subprocess.run(["bash", str(HERR / "freerouting.sh"), str(dsn), str(ses), PASADAS], check=True)
if not ses.exists():
    sys.exit("Freerouting no genero el .ses")
pcbnew.ImportSpecctraSES(b, str(ses))

# --- 5. Planos de masa en las dos caras
gnd = b.FindNet("GND")
borde = b.GetBoardEdgesBoundingBox()
for capa, nombre in ((pcbnew.F_Cu, "GND_auto_F"), (pcbnew.B_Cu, "GND_auto_B")):
    z = pcbnew.ZONE(b)
    z.SetLayer(capa)
    z.SetNet(gnd)
    z.SetZoneName(nombre)
    z.SetLocalClearance(mm(0.3))
    z.SetMinThickness(mm(0.25))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(mm(0.3))
    z.SetThermalReliefSpokeWidth(mm(0.4))
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    o = z.Outline()
    o.NewOutline()
    for x, y in ((borde.GetLeft(), borde.GetTop()), (borde.GetRight(), borde.GetTop()),
                 (borde.GetRight(), borde.GetBottom()), (borde.GetLeft(), borde.GetBottom())):
        o.Append(x, y)
    b.Add(z)

# Zona sin planos de masa alrededor de la salida KEY aislada (jack PJ1 y patas 3-4 del PC817 U2): la masa del
# Chispas no debe acercarse al lado del transceptor
caja = b.FindFootprintByReference("PJ1").GetBoundingBox(False)
for n in ("3", "4"):
    caja.Merge(b.FindFootprintByReference("U2").FindPadByNumber(n).GetBoundingBox())
caja.Inflate(mm(1.0))
caja.Intersect(borde)
k = pcbnew.ZONE(b)
k.SetIsRuleArea(True)
k.SetZoneName("aislamiento_KEY")
capas = pcbnew.LSET()
capas.AddLayer(pcbnew.F_Cu)
capas.AddLayer(pcbnew.B_Cu)
k.SetLayerSet(capas)
(k.SetDoNotAllowZoneFills if hasattr(k, "SetDoNotAllowZoneFills") else k.SetDoNotAllowCopperPour)(True)
k.SetDoNotAllowTracks(False)
k.SetDoNotAllowVias(False)
k.SetDoNotAllowPads(False)
k.SetDoNotAllowFootprints(False)
o = k.Outline()
o.NewOutline()
for x, y in ((caja.GetLeft(), caja.GetTop()), (caja.GetRight(), caja.GetTop()),
             (caja.GetRight(), caja.GetBottom()), (caja.GetLeft(), caja.GetBottom())):
    o.Append(x, y)
b.Add(k)


# Vias de cosido GND (0,6/0,3) en rejilla de 4 mm, solo donde no tocan nada (pads, pistas, agujeros) con 0,4 mm de
# margen, a 1,5 mm del borde y fuera de la zona bajo la Heltec (patio de U1)
def cabe(x, y, r):
    c = pcbnew.SHAPE_CIRCLE(pcbnew.VECTOR2I(x, y), r)
    for f in b.GetFootprints():
        if f.GetReference() == "U1" and f.GetCourtyard(pcbnew.F_CrtYd).Contains(pcbnew.VECTOR2I(x, y)):
            return False
    if caja.Contains(pcbnew.VECTOR2I(x, y)):
        return False
    for f in b.GetFootprints():
        for p in f.Pads():
            if p.GetEffectiveShape(pcbnew.F_Cu).Collide(c, 0) or p.GetEffectiveShape(pcbnew.B_Cu).Collide(c, 0) \
               or (p.HasHole() and p.GetEffectiveHoleShape().Collide(c, 0)):
                return False
    for t in b.GetTracks():
        if t.GetEffectiveShape().Collide(c, 0):
            return False
    return True


VIA_D, MARGEN = mm(0.6), mm(0.4)
n_vias = 0
cosido = []
paso = mm(4)
y = borde.GetTop() + mm(1.5)
while y <= borde.GetBottom() - mm(1.5):
    x = borde.GetLeft() + mm(1.5)
    while x <= borde.GetRight() - mm(1.5):
        if cabe(x, y, VIA_D // 2 + MARGEN):
            v = pcbnew.PCB_VIA(b)
            v.SetPosition(pcbnew.VECTOR2I(x, y))
            v.SetWidth(VIA_D)
            v.SetDrill(mm(0.3))
            v.SetNet(gnd)
            b.Add(v)
            cosido.append(v)
            n_vias += 1
        x += paso
    y += paso
pcbnew.ZONE_FILLER(b).Fill(b.Zones())

# Fuera las vias de cosido que no quedan dentro del plano relleno de las DOS caras (unirian islas sueltas), y
# segundo relleno. Listas pedidas antes de borrar (ver arriba).
rellenos = [(z.GetLayer(), z.GetFilledPolysList(z.GetLayer())) for z in b.Zones() if z.GetZoneName().startswith("GND_auto")]
quitar = [v for v in cosido if not all(p.Contains(v.GetPosition()) for _, p in rellenos)]
for v in quitar:
    b.Remove(v)
print("Vias de cosido:", n_vias - len(quitar), f"(quitadas {len(quitar)} sueltas)")

pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(str(PCB), b)
print("Rutado y planos de masa listos:", PCB)
