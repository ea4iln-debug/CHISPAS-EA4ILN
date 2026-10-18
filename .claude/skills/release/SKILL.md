---
name: release
description: Subir la versión del firmware de Chispas (FW_VERSION) y publicar una release en GitHub que llega a las placas por OTA, con publicar.sh. Usar cuando el usuario hable de nueva versión, release, publicar o probar el OTA.
---

# Versión y release

**Las releases llegan a las placas por OTA: preguntar al usuario antes de subir la versión y antes de cada paso
de publicación.** Detalle e historial en `docs/NOTAS.md`, sección "Releases y OTA".

## Versión
- Formato `X.YYb` (b = beta; sin letra, final). Orden: 1.00b < 1.01b < 1.01 (`compararVersiones()`).
- Cambiar los dos sitios de `src/main.cpp`: `#define FW_VERSION "<ver>"` y `@version <ver> - <resumen>` de la
  cabecera. Commit y push antes de preparar la release (`publicar.sh` exige HEAD = `origin/main`).

## Publicar con `./publicar.sh` (necesita `gh` con sesión; equipos 1 y 2)
1. `./publicar.sh`: comprueba git y versión, compila y deja en `dist/<ver>/` los binarios
   (`chispas-<ver>.bin` y `chispas-<ver>-usb.bin`; el primero, nombre exacto que busca el OTA), `LICENCIAS_TERCEROS.txt` y `notas.md`.
2. **Probar en placa** (skill `placa`) y reescribir `dist/<ver>/notas.md` en castellano para el usuario final:
   novedades agrupadas por pantalla o función, sin jerga de código ni nombres de commits; la sección de
   instalación se deja. Enseñar las notas al usuario.
3. Con su confirmación: `./publicar.sh --subir` → release en **borrador** (las placas no la ven). Pasarle el enlace.
4. Con su confirmación: `./publicar.sh --publicar` → publicada (etiqueta `v<ver>`, prerelease si es beta).
   Si es beta, rehacer antes "Novedades de la beta" en `web/index.html` (respecto a la última final) y hacer commit
   **sin push**; el push, *después* de `--publicar` (la release rehace la web desde el commit de la etiqueta; el
   push la vuelve a hacer desde `main` con el texto nuevo). **Si la etiqueta cae en el mismo commit que ya se
   desplegó con un push** (todo empujado antes de `--publicar`), Pages da el despliegue de la release por repetido
   (mismo `pages_build_version`) y la web se queda con la beta anterior (pasó con la 1.32b): comprobar
   `manifest-beta.json` y, si hace falta, `gh workflow run web.yml --ref main` con un commit nuevo en `main`.
5. Anotar la release en NOTAS ("Releases y OTA") y el "Estado actual" de CLAUDE.md.

Si se han cambiado librerías, regenerar antes `docs/LICENCIAS_TERCEROS.txt`.

## Probar el OTA (después de publicar)
Bajar la placa a la **última estable publicada** (decisión del usuario, 18 oct 2026; nada de compilar un 0.99):
`gh release download v<estable> -p "chispas-<estable>-usb.bin"`, partirlo como `web/preparar.py` (`TROZOS`:
`0x0`–`0x8000`, `0x8000`–`0x9000`, `0xE000`–`0x10000`, `0x10000`–fin; la NVS de `0x9000` no se toca) y grabarlo con
`python ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port COMx write_flash 0x0 bl.bin
0x8000 pt.bin 0xE000 ota.bin 0x10000 app.bin` (sin `-I`: esptool carga su módulo de su carpeta). Luego CONF >
CONFIG > ACTUALIZAR FW e instalar.
