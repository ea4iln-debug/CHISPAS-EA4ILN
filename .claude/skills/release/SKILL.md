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
5. Anotar la release en NOTAS ("Releases y OTA") y el "Estado actual" de CLAUDE.md.

Si se han cambiado librerías, regenerar antes `docs/LICENCIAS_TERCEROS.txt`.

## Probar el OTA sin publicar
Compilar temporalmente con `FW_VERSION "0.99"`, cargar por USB y en CONF > CONFIG > ACTUALIZAR FW instalar la
última release. Volver a dejar la versión real (no hacer commit del 0.99).
