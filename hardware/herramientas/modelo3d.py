# SPDX-License-Identifier: GPL-3.0-or-later
# Modelo 3D de la placa montada (PCB + piezas + Heltec en sus zocalos) para disenar la caja:
# copia a la U1 de la placa los modelos 3D de la huella de la Heltec (gen_heltec.py) y exporta
# hardware/3d/chispas.step con el origen en el centro de la placa (Z = 0 en la cara de arriba).
# Uso (con el editor de placas cerrado):
#   "/c/Program Files/KiCad/10.0/bin/python.exe" hardware/herramientas/modelo3d.py
import subprocess
from pathlib import Path

import pcbnew

HW = Path(__file__).resolve().parent.parent
PCB = HW / "chispas.kicad_pcb"
SALIDA = HW / "3d" / "chispas.step"
CLI = Path(r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe")

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
SALIDA.parent.mkdir(exist_ok=True)
subprocess.run([str(CLI), "pcb", "export", "step", "--force", "--subst-models", "--include-silkscreen",
                "--user-origin", f"{cx}x{cy}mm", "-o", str(SALIDA), str(PCB)], check=True)
print(f"{SALIDA} ({SALIDA.stat().st_size / 1e6:.1f} MB)")
