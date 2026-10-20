# SPDX-License-Identifier: GPL-3.0-or-later
# Modelo 3D de la placa montada (PCB + piezas + Heltec en sus zocalos) para disenar la caja:
# copia a la U1 de la placa los modelos 3D de la huella de la Heltec (gen_heltec.py) y exporta
# hardware/3d/chispas.step con el origen en el centro de la placa (Z = 0 en la cara de arriba).
# Uso (con el editor de placas cerrado):
#   "/c/Program Files/KiCad/10.0/bin/python.exe" hardware/herramientas/modelo3d.py
#
# Las piezas importadas de LCSC (easyeda2kicad) usan en la placa el .wrl, pero su .step tiene otro origen: el jack
# sale 6,25 mm corrido y 2,5 mm hundido, y el cuerpo del encoder bajo la placa (comprobado con renders de KiCad
# usando los .step, 20 oct). Se exporta desde una copia de la placa con los .step y estas correcciones (mm).
import shutil
import subprocess
from pathlib import Path

import pcbnew

HW = Path(__file__).resolve().parent.parent
PCB = HW / "chispas.kicad_pcb"
TEMP = HW / "_modelo3d.kicad_pcb"
SALIDA = HW / "3d" / "chispas.step"
CLI = Path(r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe")
CORRECCION = {"AUDIO-TH_PJ-307": (-6.25, 0, 2.5), "SW-TH_PEC11R-4XXXF-SXXXX": (0, 0, 4.5)}

b = pcbnew.LoadBoard(str(PCB))
lib = pcbnew.FootprintLoad(str(HW / "lib" / "chispas.pretty"), "Heltec_WiFi_LoRa_32_V3")
u1 = b.FindFootprintByReference("U1")
nuevos = [(m.m_Filename, m.m_Offset.x, m.m_Offset.y, m.m_Offset.z, m.m_Rotation.z) for m in lib.Models()]
actuales = [(m.m_Filename, m.m_Offset.x, m.m_Offset.y, m.m_Offset.z, m.m_Rotation.z) for m in u1.Models()]
if nuevos != actuales:
    u1.Models().clear()
    for m in lib.Models():
        u1.Models().push_back(m)
    b.Save(str(PCB))
    print("U1: modelos 3D actualizados en la placa")

caja = b.GetBoardEdgesBoundingBox()
cx, cy = pcbnew.ToMM(caja.GetCenter().x), pcbnew.ToMM(caja.GetCenter().y)
print(f"Placa {pcbnew.ToMM(caja.GetWidth()):.2f} x {pcbnew.ToMM(caja.GetHeight()):.2f} mm, centro ({cx:.2f}, {cy:.2f})")

# Copia con los .step de lib/chispas.3dshapes (fp.Models() da copias: se rehace la lista)
for fp in b.Footprints():
    modelos = []
    for m in fp.Models():
        if "chispas.3dshapes/" in m.m_Filename and m.m_Filename.endswith(".wrl"):
            m.m_Filename = m.m_Filename[:-4] + ".step"
            dx, dy, dz = CORRECCION.get(Path(m.m_Filename).stem, (0, 0, 0))
            m.m_Offset = pcbnew.VECTOR3D(m.m_Offset.x + dx, m.m_Offset.y + dy, m.m_Offset.z + dz)
        modelos.append(m)
    fp.Models().clear()
    for m in modelos:
        fp.Models().push_back(m)
b.Save(str(TEMP))
shutil.copy(HW / "chispas.kicad_pro", TEMP.with_suffix(".kicad_pro"))

SALIDA.parent.mkdir(exist_ok=True)
try:
    subprocess.run([str(CLI), "pcb", "export", "step", "--force", "--include-silkscreen",
                    "--user-origin", f"{cx}x{cy}mm", "-o", str(SALIDA), str(TEMP)], check=True)
finally:
    for f in HW.glob("_modelo3d.*"):
        f.unlink()
print(f"{SALIDA} ({SALIDA.stat().st_size / 1e6:.1f} MB)")
