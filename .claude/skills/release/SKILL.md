---
name: release
description: Subir la versión del firmware de Chispas (FW_VERSION) y publicar una release en GitHub que llega a las placas por OTA, con publicar.sh. Usar cuando el usuario hable de nueva versión, release, publicar o probar el OTA, o al cerrar una tarea con firmware nuevo probado.
---

# Versión y release

Las releases llegan a las placas por OTA. **Es lo único que se pregunta al usuario** (decisión del 18 oct 2026):
una sola pregunta, con AskUserQuestion, *antes* de tocar `FW_VERSION`. Con el sí, todo lo demás (versión, commit,
borrador, notas, publicar, web) se hace seguido, sin más confirmaciones. Historial en `docs/NOTAS.md`, "Releases y OTA".

## 0. La pregunta
Cuando hay firmware nuevo probado en placa y sin publicar (o el usuario lo pide), preguntar una vez:
"¿Publico la v<sig>?" con opciones **Sí, beta (Recomendado)** / **Sí, final** / **Todavía no**, y en la descripción,
en dos líneas, qué trae. Si dice "todavía no", no volver a preguntar hasta que haya algo nuevo.
- Versión siguiente: `X.YY` + 0,01 (dos cifras siempre; la OTA compara números). Beta con `b`; final sin letra
  (una final tras la 1.33b sería la 1.34, no la 1.33).

## 1. Versión
Cambiar los dos sitios de `src/main.cpp`: `#define FW_VERSION "<ver>"` y `@version <ver> - <resumen>`. Commit
("v<ver>: <resumen>") y `git push` (`publicar.sh` exige HEAD = `origin/main` y árbol limpio).

## 2. Preparar y subir el borrador
1. `./publicar.sh` → compila y deja en `dist/<ver>/` los binarios (`chispas-<ver>.bin`, nombre exacto que busca el
   OTA, y `chispas-<ver>-usb.bin`), `LICENCIAS_TERCEROS.txt` y `notas.md`. Si cambiaron librerías, regenerar antes
   `docs/LICENCIAS_TERCEROS.txt`.
2. Reescribir `dist/<ver>/notas.md` en castellano para el usuario final: novedades por pantalla o función, sin jerga
   de código ni nombres de commits; la sección de instalación se deja. Usar Write, no heredocs.
3. `./publicar.sh --subir` (borrador, las placas no lo ven) y enseguida `./publicar.sh --publicar` (etiqueta
   `v<ver>`, prerelease si es beta).

## 3. Web (después de publicar)
La Action `web.yml` se lanza con la release, pero si la etiqueta cae en un commit ya desplegado, Pages la da por
repetida y la web se queda con la versión anterior (pasó con la 1.32b). Por eso, **después** de `--publicar`:
1. Si es beta, rehacer "Novedades de la beta" en `web/index.html` (respecto a la última final). Commit junto con
   NOTAS ("Releases y OTA") y el "Estado actual" de CLAUDE.md, y `git push`: el push toca `web/` y rehace la web
   desde `main`, ya con la release publicada.
2. Comprobar a los ~30 s: `gh run list -w web.yml -L 2` y
   `curl -s https://ea4iln-debug.github.io/CHISPAS-EA4ILN/manifest-beta.json | grep version` (o `manifest.json` si es
   final). Si sigue la anterior: `gh workflow run web.yml --ref main` y volver a mirar.

## 4. Probar el OTA (solo si cambia `ota.inc`, las particiones o el usuario lo pide)
Nada de compilar un 0.99 (se coló en un commit con la 1.31b): bajar la placa a la **última estable publicada**
(decisión del usuario, 18 oct 2026). En el scratchpad: `gh release download v<estable> -p "chispas-<estable>-usb.bin"`,
partirlo como `web/preparar.py` (`TROZOS`: `0x0`–`0x8000`, `0x8000`–`0x9000`, `0xE000`–`0x10000`, `0x10000`–fin; la
NVS de `0x9000` no se toca) y grabarlo con `~/.platformio/penv/Scripts/python.exe
~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port COMx write_flash 0x0 bl.bin 0x8000 pt.bin
0xE000 ota.bin 0x10000 app.bin` (sin `-I`: esptool carga su módulo de su carpeta). Luego pedir al usuario, en una
línea, CONF > CONFIG > ACTUALIZAR FW, y comprobar por `serie.py` que arranca la versión nueva.

## 5. Informar
Enlace de la release, versión que muestra la web y, si se probó, el OTA.
