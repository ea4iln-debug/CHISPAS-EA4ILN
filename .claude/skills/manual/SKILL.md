---
name: manual
description: Cambiar el manual de usuario de CHISPAS (web/manual.html), regenerar su PDF (docs/manual_chispas.pdf) y revisarlo con capturas, con web/manual.sh. Usar al añadir o cambiar funciones que el manual explica, al tocar sus textos o dibujos del robot CHISPAS, o cuando el usuario hable del manual o del PDF.
---

# Manual de usuario

Documentación (CC BY-NC-SA 4.0). Detalle y decisiones en `docs/NOTAS.md`, "Releases y OTA" → "Manual de usuario".

## Reglas del contenido (decisiones del usuario)
- Para quien **nunca ha tenido un CHISPAS**: lenguaje llano, sin datos técnicos de hardware ni de programación
  (nada de GPIO, NVS, tareas…) y **sin datos personales**: ni capturas con el indicativo o el locator del usuario
  (por eso no salen `inicio.png` ni `config.png`) ni indicativos reales en los ejemplos (huecos ‹TU INDICATIVO›).
- **CHISPAS en mayúsculas y en masculino** («cansado», «contento»). El robot mascota también se llama **CHISPAS**
  (nunca «Chispín») y habla en primera persona en los pies de sus dibujos, con humor.
- Conectores: delante **Palas (KEY IN)**, **Auriculares (SND OUT)** y volumen; detrás **KEY OUT** y **SND IN (DECOD)**
  (la entrada de audio; así en todo, decisión del usuario del 23 oct); **USB-C** a la izquierda.
- Indicativo de fábrica **EA4ILN**: el manual avisa de cambiarlo (CONF › CONFIG › ID).
- Funciones solo en la beta: `<span class="chip-beta">beta</span>`. La portada dice para qué versión es: actualizarla.
- PDF en A4 con **fondo blanco** (se probó crema y el usuario prefirió blanco).

## Estructura de `web/manual.html`
- Mismo estilo oscuro que `index.html`; el bloque `@media print` es el del PDF (blanco, una sección por página,
  robot en el margen derecho de 44 mm). Portada `.portada`, índice `.indice` y 14 `<section class="seccion" id=…>`
  (ids: cw, chispas, primeros-pasos, entrenador, lora, trx, prop, pota, sol, conf, actualizar, cara, problemas,
  glosario). Cada una con `<div class="cuerpo">` y, dentro, `<figure class="robot">` (flota al margen).
- **Robot:** piezas en el `<svg>` oculto del principio: `#r-base` (cuerpo, viewBox 0 0 160 190, hombros en
  (50,112) y (108,114)) y caras `#x-normal`, `x-feliz`, `x-cerrados`, `x-guino`, `x-sorpresa`, `x-concentrado`,
  `x-preocupado`, `x-gafas`, `x-duda`, `x-travieso`. Cada pose: accesorios de detrás, `<use href="#r-base"/>`, una
  cara, brazos (cada uno dos `<path>` con la misma `d`: `r-b1` contorno y `r-b2` relleno, y una mano
  `<circle class="r-m" r="6">`) y accesorios de delante. Clases de color `r-*` en el `<style>`.
- Capturas de la OLED: `img/<nombre>.png?__SELLO__` desde `docs/img/`. **Toda imagen nueva va también en
  `CAPTURAS` de `web/preparar.py`** (si no, la web publicada no la tiene).

## Comprobar y generar
```bash
./web/manual.sh --vista entrenador trx   # .pio/manual/robots.png (todas las poses) y s_<id>.png por sección
./web/manual.sh --imprimir               # .pio/manual/imp.png: simulación de la maqueta del PDF
./web/manual.sh                          # PDF en docs/manual_chispas.pdf (tamaño y páginas)
```
- Mirar las capturas con Read antes de dar nada por bueno. Read **no abre PDF** (no hay poppler) y pdf.js en Chrome
  sin interfaz se queda colgado: revisar la maqueta con `--imprimir` y pedir al usuario que mire el PDF.
- **Regenerar el PDF siempre que cambie el manual**, sin esperar aprobación, y subir los dos juntos (la Action de
  la web se lanza con `web/**` y `docs/*.pdf`). Al terminar, abrir el HTML en el navegador
  (`cmd //c start "" .pio/manual/manual.html`) y decir al usuario qué secciones cambiaron, para que las mire.
- Si cambian las funciones del firmware, revisar también la sección del manual que las explica.
