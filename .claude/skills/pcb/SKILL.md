---
name: pcb
description: Diseño de la PCB de Chispas en KiCad 10 para JLCPCB (hardware/) - guiar al usuario paso a paso, revisar esquema y placa con revisar.sh, importar piezas LCSC con importar_lcsc.sh, buscar piezas con buscar_jlc.py y colocar con colocar.py. Usar al seguir con la PCB, al añadir o cambiar piezas, o al preparar el pedido.
---

# PCB de Chispas (KiCad 10, JLCPCB)

Estado, decisiones, piezas LCSC y siguiente paso: **`docs/NOTAS.md`, sección "PCB"**. Leerla antes de seguir.

## Forma de trabajar (la pidió el usuario)
- El usuario dibuja en la interfaz de KiCad; Claude le da **pasos numerados** con teclas (A símbolo, P alimentación,
  L etiqueta, W cable, Q sin conexión, F8 actualizar PCB) y **tablas pin → conexión** por pieza. Masa siempre con
  P → GND, nunca con etiqueta.
- Cuando dice "hecho", revisar con `revisar.sh` y comprobar los **cambios respecto al último commit** (detecta
  cables borrados sin querer). Si todo está bien: commit y push.
- Lo que necesita medidas exactas (colocar piezas, contorno) lo hace Claude con scripts del Python de KiCad, con el
  **editor de placas cerrado** (si está abierto, KiCad pisa el archivo al guardar).
- Las decisiones de diseño (piezas, huellas, colocación, rutado) las toma Claude con criterio (Basic antes que
  Extended, stock, coste) y las explica en el resumen; no se pregunta. Los pedidos y compras los hace el usuario.
- Al terminar una sesión larga: actualizar NOTAS "PCB" (estado y siguiente paso) y CLAUDE.md.

## Herramientas (`hardware/herramientas/`, desde Git Bash)
```bash
hardware/herramientas/revisar.sh            # piezas, redes, cambios desde HEAD, ERC, DRC, PNG en hardware/.revision/
hardware/herramientas/importar_lcsc.sh C123 C456   # simbolo+huella+3D a lib/chispas, tipos de pin y agujeros NPTH
~/.platformio/penv/Scripts/python.exe -I hardware/herramientas/buscar_jlc.py "0805 10kΩ" "PC817"  # catalogo JLCPCB
"/c/Program Files/KiCad/10.0/bin/python.exe" hardware/herramientas/colocar.py   # coloca piezas y contorno (KiCad cerrado)
"/c/Program Files/KiCad/10.0/bin/python.exe" hardware/herramientas/rutar.py     # Freerouting + planos de masa (KiCad cerrado)
"/c/Program Files/KiCad/10.0/bin/python.exe" hardware/herramientas/serigrafia.py # textos de serigrafia (KiCad cerrado)
"/c/Program Files/KiCad/10.0/bin/python.exe" hardware/herramientas/fabricar.py   # Gerber zip, BOM, CPL y compra en hardware/fabricacion/
~/.platformio/penv/Scripts/python.exe -I hardware/herramientas/gen_heltec.py    # simbolo y huella de la Heltec
"/c/Program Files/KiCad/10.0/bin/python.exe" hardware/herramientas/modelo3d.py   # STEP de la placa montada (para la caja) en hardware/3d/
```
- `entorno.sh` crea `~/.venv-kicad` (easyeda2kicad y pymupdf, versiones en `requisitos.txt`) si falta en el
  equipo; lo llaman `revisar.sh` e `importar_lcsc.sh`, no hace falta ejecutarlo a mano.
- Ver las imágenes con Read: `hardware/.revision/placa.png` y `esquema.png`.
- Tras importar una pieza que ya está en el esquema: en KiCad, Herramientas > Actualizar símbolos desde la
  librería. Los pines quedan pasivos salvo que la pieza esté en `tipos_pines.py`: los circuitos integrados (alimentación, entradas y salidas), darlos de alta allí.
- `buscar_jlc.py`: preferir **Basic** (sin recargo); las Extended cuestan unos 3 $ por referencia distinta.

## Reglas
- Campo del número LCSC en los símbolos: **`LCSC Part`**. Lo que suelda el usuario (jacks, conectores, trimmer,
  potenciómetro, Heltec): "Excluir de la lista de materiales".
- Antes de dar por buena una huella importada, mirar la hoja de datos (pinout y orientación): `easyeda2kicad` no
  siempre acierta (agujeros de tetones como pads metalizados, tipos de pin).
- No cambiar pines del firmware desde la PCB sin avisar: la tabla de GPIO de CLAUDE.md manda.
