# SPDX-License-Identifier: GPL-3.0-or-later
# Revision del proyecto KiCad de Chispas desde la terminal (sin abrir KiCad):
#   - piezas: valor, huella, LCSC Part y si se excluyen de la BOM
#   - redes con sus pines, y diferencias con la version del ultimo commit (git HEAD)
#   - resumen de ERC y DRC (los avisos de cada tipo, con su posicion)
#   - imagenes PNG del esquema y de la placa (si esta pymupdf: python del entorno de entorno.sh)
# Uso: revisar.sh [carpeta_salida]   (por defecto hardware/.revision, ignorada por git)
import glob
import os
import re
import subprocess
import sys
from pathlib import Path

HW = Path(__file__).resolve().parent.parent
SCH, PCB = HW / "chispas.kicad_sch", HW / "chispas.kicad_pcb"
SAL = Path(sys.argv[1]) if len(sys.argv) > 1 else HW / ".revision"
SAL.mkdir(parents=True, exist_ok=True)
sys.stdout.reconfigure(encoding="utf-8")

cands = sorted(glob.glob("C:/Program Files/KiCad/*/bin/kicad-cli.exe"))
if not cands:
    sys.exit("No encuentro kicad-cli (C:/Program Files/KiCad/<version>/bin)")
CLI = cands[-1]


def cli(*args):
    subprocess.run([CLI, *map(str, args)], capture_output=True, text=True)


def leer_netlist(f):
    t = Path(f).read_text(encoding="utf-8")
    comps = []
    for c in re.split(r"\(comp\s", t[t.index("(components"):t.index("(libparts")])[1:]:
        g = lambda pat: (m.group(1) if (m := re.search(pat, c)) else "-")
        comps.append((g(r'\(ref "([^"]+)"'), g(r'\(value "([^"]*)"'), g(r'\(footprint "([^"]*)"'),
                      g(r'\(name "LCSC Part"\)\s*"([^"]*)"'), "excl" if "exclude_from_bom" in c else ""))
    redes = {}
    for blk in re.split(r"\(net\s", t[t.index("(nets"):])[1:]:
        nombre = re.search(r'\(name "([^"]*)"', blk).group(1)
        nodos = tuple(sorted(f"{r}.{p}" for r, p in re.findall(r'\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', blk)))
        redes[nombre] = nodos
    return comps, redes


def anonimo(redes):
    # Nombres automaticos (Net-..., unconnected-...) dependen de la anotacion: comparar solo por pines
    return {(n if not n.startswith(("Net-", "unconnected-")) else "", p) for n, p in redes.items()}


# --- Esquema: netlist actual y la del ultimo commit
cli("sch", "export", "netlist", "--format", "kicadsexpr", SCH, "-o", SAL / "red.net")
comps, redes = leer_netlist(SAL / "red.net")
print("== PIEZAS")
for ref, val, fp, lcsc, ex in sorted(comps, key=lambda c: (re.sub(r"\d", "", c[0]), int(re.sub(r"\D", "", c[0]) or 0))):
    print(f"  {ref:5} {val:24} {fp:58} {lcsc:9} {ex}")
print("== REDES (sin los pines libres de la Heltec)")
for n, p in sorted(redes.items()):
    if not n.startswith("unconnected-(U1"):
        print(f"  {n}: {' '.join(p)}")

viejo = subprocess.run(["git", "-C", str(HW), "show", "HEAD:hardware/chispas.kicad_sch"], capture_output=True)
if viejo.returncode == 0:
    (SAL / "head.kicad_sch").write_bytes(viejo.stdout)
    cli("sch", "export", "netlist", "--format", "kicadsexpr", SAL / "head.kicad_sch", "-o", SAL / "head.net")
    _, redes_v = leer_netlist(SAL / "head.net")
    a, b = anonimo(redes_v), anonimo(redes)
    print("== CAMBIOS DE CONEXIONES RESPECTO AL ULTIMO COMMIT" + ("" if a != b else ": ninguno"))
    for x in sorted(a - b):
        print("  ANTES", x)
    for x in sorted(b - a):
        print("  AHORA", x)


def resumen(informe, nombre):
    t = Path(informe).read_text(encoding="utf-8")
    print(f"== {nombre}: " + " | ".join(re.findall(r"\*\* ((?:ERC messages|Found).*?)(?: \*\*)?$", t, re.M)))
    tipos = {}
    for bloque in re.split(r"\n(?=\[)", t):
        m = re.match(r"\[(\w+)\]: (.*)", bloque)
        if m:
            tipos.setdefault(m.group(1), []).append(bloque)
    for tipo, bloques in sorted(tipos.items()):
        print(f"  {len(bloques):3} [{tipo}] {re.match(r'.*: (.*)', bloques[0]).group(1)}")
        if tipo in ("unconnected_items", "silk_edge_clearance", "silk_overlap", "silk_over_copper"):
            continue   # esperables mientras no se rute / cosmeticos: solo el recuento
        for b in bloques[:6]:
            for linea in re.findall(r"@\(.*", b):
                print("        " + linea)


cli("sch", "erc", SCH, "-o", SAL / "erc.txt")
resumen(SAL / "erc.txt", "ERC")
if PCB.exists():
    cli("pcb", "drc", PCB, "-o", SAL / "drc.txt")
    resumen(SAL / "drc.txt", "DRC")

# --- Imagenes
cli("sch", "export", "pdf", SCH, "-o", SAL / "esquema.pdf")
if PCB.exists():
    cli("pcb", "export", "pdf", PCB, "--layers", "F.Cu,B.Cu,F.SilkS,F.Fab,F.CrtYd,Edge.Cuts", "--mode-single",
        "-o", SAL / "placa.pdf")
try:
    import pymupdf
    pymupdf.open(SAL / "esquema.pdf")[0].get_pixmap(dpi=110).save(SAL / "esquema.png")
    if PCB.exists():
        # Recorte al contorno de la placa (Edge.Cuts) con 3 mm de margen
        t = PCB.read_text(encoding="utf-8")
        xs, ys = [], []
        for m in re.finditer(r'\(gr_\w+\s*\(start ([\d.\-]+) ([\d.\-]+)\)\s*\(end ([\d.\-]+) ([\d.\-]+)\).*?\(layer "Edge\.Cuts"\)',
                             t, re.S):
            x1, y1, x2, y2 = map(float, m.groups())
            xs += [x1, x2]
            ys += [y1, y2]
        k = 72 / 25.4
        pag = pymupdf.open(SAL / "placa.pdf")[0]
        clip = pymupdf.Rect((min(xs) - 3) * k, (min(ys) - 3) * k, (max(xs) + 3) * k, (max(ys) + 3) * k) if xs else None
        pag.get_pixmap(dpi=250, clip=clip).save(SAL / "placa.png")
    print(f"== IMAGENES en {SAL}: esquema.png" + (", placa.png" if PCB.exists() else ""))
except ImportError:
    print("== IMAGENES: falta pymupdf (usar revisar.sh, que usa el entorno de entorno.sh)")
