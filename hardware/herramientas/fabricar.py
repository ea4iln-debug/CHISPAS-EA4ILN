# SPDX-License-Identifier: GPL-3.0-or-later
# Archivos para el pedido de JLCPCB en hardware/fabricacion/:
#   chispas_gerber.zip   Gerber (2 capas) y taladros Excellon (PTH y NPTH aparte)
#   chispas_bom.csv      BOM de montaje: Comment, Designator, Footprint, LCSC (solo lo que monta JLCPCB)
#   chispas_cpl.csv      posiciones: Designator, Mid X, Mid Y, Layer, Rotation (mismas piezas que la BOM)
#   compra_lcsc.csv      lo que suelda el usuario (excluido de la BOM), por placa y para N placas
# Ejecutar con el Python de KiCad (no hace falta cerrar KiCad: solo lee):
#   "C:/Program Files/KiCad/10.0/bin/python.exe" fabricar.py [placas]
import csv
import glob
import subprocess
import sys
import zipfile
from collections import defaultdict
from pathlib import Path

import pcbnew

HW = Path(__file__).resolve().parent.parent
PCB = HW / "chispas.kicad_pcb"
SAL = HW / "fabricacion"
PLACAS = int(sys.argv[1]) if len(sys.argv) > 1 else 10
CLI = sorted(glob.glob("C:/Program Files/KiCad/*/bin/kicad-cli.exe"))[-1]
# Correccion de giro para el CPL por huella (grados que se suman), si la vista previa de JLCPCB lo pide.
# Comprobar con la marca fisica de la pieza (raya del catodo, punto del pin 1 en el cuerpo), NO con el punto rosa:
# el 15 oct 2026 se giraron los diodos por el punto rosa y quedaron al reves; sin correccion salen bien
# (raya del catodo sobre la barra de la serigrafia). U2 y U3 tambien bien sin correccion.
GIRO_JLC = {}
# Piezas para soldar que no estan en el esquema (por placa)
EXTRA = [("Zocalo hembra 1x18 2,54 mm (Heltec)", "C2905422", 2)]

SAL.mkdir(exist_ok=True)
tmp = SAL / "gerber"
tmp.mkdir(exist_ok=True)
for f in tmp.iterdir():
    f.unlink()


def cli(*a):
    r = subprocess.run([CLI, *map(str, a)], capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"kicad-cli {' '.join(map(str, a[:3]))}: {r.stderr or r.stdout}")


# --- Gerber y taladros
cli("pcb", "export", "gerbers", PCB, "-o", tmp,
    "--layers", "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts")
cli("pcb", "export", "drill", PCB, "-o", tmp, "--format", "excellon", "--excellon-separate-th",
    "--excellon-units", "mm", "--excellon-zeros-format", "decimal")
with zipfile.ZipFile(SAL / "chispas_gerber.zip", "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(tmp.iterdir()):
        z.write(f, f.name)

# --- Posiciones de KiCad (coordenadas como los Gerber: Y hacia arriba)
pos = SAL / "pos_kicad.csv"
cli("pcb", "export", "pos", PCB, "-o", pos, "--format", "csv", "--units", "mm", "--side", "both")
filas_pos = {r["Ref"]: r for r in csv.DictReader(pos.open(encoding="utf-8"))}
pos.unlink()

b = pcbnew.LoadBoard(str(PCB))
montaje = defaultdict(list)     # (huella, lcsc) -> refs (agrupar por LCSC: "1u" y "1U" son la misma pieza)
valor = {}
compra = defaultdict(list)
for f in b.GetFootprints():
    ref = f.GetReference()
    lcsc = f.GetFieldText("LCSC Part") if f.HasField("LCSC Part") else ""
    huella = str(f.GetFPID().GetLibItemName())
    if f.IsExcludedFromBOM() or f.IsDNP():
        if not ref.startswith(("H", "JP")):   # agujeros y puentes de soldadura no se compran
            compra[(f.GetValue(), huella, lcsc)].append(ref)
        continue
    if not lcsc:
        sys.exit(f"{ref} va a montaje y no tiene LCSC Part")
    montaje[(huella, lcsc)].append(ref)
    valor.setdefault((huella, lcsc), f.GetValue())

orden = lambda r: (r.rstrip("0123456789"), int(r[len(r.rstrip("0123456789")):] or 0))
with (SAL / "chispas_bom.csv").open("w", newline="", encoding="utf-8") as fh:
    w = csv.writer(fh)
    w.writerow(["Comment", "Designator", "Footprint", "LCSC"])
    for (huella, lcsc), refs in sorted(montaje.items(), key=lambda kv: orden(sorted(kv[1], key=orden)[0])):
        w.writerow([valor[(huella, lcsc)], ",".join(sorted(refs, key=orden)), huella, lcsc])

with (SAL / "chispas_cpl.csv").open("w", newline="", encoding="utf-8") as fh:
    w = csv.writer(fh)
    w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
    for refs in montaje.values():
        for ref in sorted(refs, key=orden):
            r = filas_pos[ref]
            w.writerow([ref, f"{float(r['PosX']):.3f}mm", f"{float(r['PosY']):.3f}mm",
                        "Top" if r["Side"] == "top" else "Bottom",
                        f"{(float(r['Rot']) + GIRO_JLC.get(r['Package'], 0)) % 360:.0f}"])

with (SAL / "compra_lcsc.csv").open("w", newline="", encoding="utf-8") as fh:
    w = csv.writer(fh)
    w.writerow(["Pieza", "Referencias", "LCSC", "Por placa", f"Para {PLACAS} placas"])
    for (val, huella, lcsc), refs in sorted(compra.items(), key=lambda kv: orden(sorted(kv[1], key=orden)[0])):
        w.writerow([f"{val} ({huella})", ",".join(sorted(refs, key=orden)), lcsc, len(refs), len(refs) * PLACAS])
    for nombre, lcsc, n in EXTRA:
        w.writerow([nombre, "", lcsc, n, n * PLACAS])

print(f"Montaje: {sum(len(r) for r in montaje.values())} piezas, {len(montaje)} referencias LCSC distintas")
print(f"Archivos en {SAL}: chispas_gerber.zip, chispas_bom.csv, chispas_cpl.csv, compra_lcsc.csv")
