---
name: esquema
description: Regenerar el esquema del prototipo (docs/esquema_protoboard.pdf) o el plano de la placa perforada (docs/placa_perforada.pdf) con gen_esquema.py / gen_placa.py y Edge o Chrome sin interfaz. Usar al cambiar pines, componentes o el montaje.
---

# Esquema y placa perforada

Los PDF son documentación (CC BY-NC-SA 4.0). Detalle en `docs/NOTAS.md`, secciones "Prototipo en protoboard" y
"Placa perforada".

## Generar
```bash
PY=~/.platformio/penv/Scripts/python.exe
TMP=<scratchpad>
$PY docs/gen_esquema.py "$TMP/esquema.html"     # o: $PY docs/gen_placa.py "$TMP/placa.html"
```
`gen_placa.py` comprueba que cada red queda unida, que nada se pisa y que nada va bajo la Heltec, con y sin las
reservas: si avisa de algo, corregir el script antes de seguir.

PDF con el navegador sin interfaz (Edge en el equipo 1; Chrome en el 2, que no tiene Edge):
```bash
NAV="/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
[ -f "$NAV" ] || NAV="/c/Program Files/Google/Chrome/Application/chrome.exe"
"$NAV" --headless --disable-gpu --no-pdf-header-footer --print-to-pdf="$(cygpath -w docs/esquema_protoboard.pdf)" \
  "file:///$(cygpath -m "$TMP/esquema.html")"
```
(Para la placa, `docs/placa_perforada.pdf` y `placa.html`.)

## Revisar
- Las hojas tienen altura fija y ocultan lo que no cabe: **mirar el PDF**, no basta con el HTML. Leerlo con Read
  (páginas cambiadas) y comprobar que no hay nada cortado ni solapado.
- Si cambia un pin: actualizar también la tabla de GPIO de `CLAUDE.md`, `src/parts/pines.inc` y NOTAS.
- Antes de soldar la placa perforada: orientación de la Heltec (`J3_IZQUIERDA`) y orden de pines de los módulos
  confirmados (fotos del 14 oct en NOTAS: PAM8302A de 5 pines A+, A−, SD, Vin, Gnd). Si cambia un módulo, revisarlo.
