#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 EA4ILN
#
# Prepara y sube una release de Chispas con los nombres exactos que busca el OTA. Tres pasos, y entre
# ellos la prueba en placa y la confirmacion del usuario:
#
#   ./publicar.sh             1) compila los dos entornos (./compilar.sh) y deja en dist/<ver>/ los cuatro
#                                binarios, LICENCIAS_TERCEROS.txt y un borrador de notas (notas.md) con los
#                                commits desde la ultima etiqueta. Editar notas.md en castellano.
#   ./publicar.sh --subir     2) crea la release en GitHub como BORRADOR (las placas no la ven por OTA)
#                                con los archivos de dist/<ver>/ y notas.md. Revisarla en la web.
#   ./publicar.sh --publicar  3) quita el borrador: la release (y la etiqueta v<ver>) quedan publicadas y
#                                las placas la encuentran por OTA.
#
# La version es la de FW_VERSION en src/main.cpp; si acaba en letra (beta), la release es prerelease.
# Necesita gh (GitHub CLI) con sesion iniciada (gh auth login).

cd "$(dirname "$0")" || exit 1

PY="$HOME/.platformio/penv/Scripts/python.exe"
ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
GH=gh
command -v gh >/dev/null 2>&1 || GH="/c/Program Files/GitHub CLI/gh.exe"

VER=$(grep -oE '^#define FW_VERSION "[^"]+"' src/main.cpp | grep -oE '"[^"]+"' | tr -d '"')
[ -n "$VER" ] || { echo "No encuentro FW_VERSION en src/main.cpp"; exit 1; }
ETIQUETA="v$VER"
DIST="dist/$VER"
PRE=""
[[ "$VER" =~ [a-z]$ ]] && PRE="--prerelease"

falla() { echo "ERROR: $*"; exit 1; }

case "$1" in
""|--preparar)
  [ -z "$(git status --porcelain)" ] || falla "hay cambios sin commit (git status)"
  git fetch -q origin || falla "git fetch"
  [ "$(git rev-parse HEAD)" = "$(git rev-parse origin/main)" ] || falla "HEAD no es origin/main (falta push o pull)"
  git rev-parse -q --verify "refs/tags/$ETIQUETA" >/dev/null && falla "ya existe la etiqueta $ETIQUETA (subir FW_VERSION)"
  grep -qE "@version $VER " src/main.cpp || falla "@version de la cabecera de main.cpp no es $VER"

  echo "== Release $ETIQUETA ${PRE:+(prerelease)}"
  ./compilar.sh || falla "no compila"

  rm -rf "$DIST"; mkdir -p "$DIST"
  for E in heltec_wifi_lora_32_V3:"" heltec_sin_encoder:"-sin-encoder"; do
    ENV=${E%%:*}; SUF=${E#*:}; B=.pio/build/$ENV
    cp "$B/firmware.bin" "$DIST/chispas-$VER$SUF.bin"
    "$PY" "$ESPTOOL" --chip esp32s3 merge_bin -o "$DIST/chispas-$VER$SUF-usb.bin" \
      --flash_mode dio --flash_size 8MB \
      0x0 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" 0xe000 "$BOOT_APP0" 0x10000 "$B/firmware.bin" \
      >/dev/null || falla "merge_bin de $ENV"
  done
  cp docs/LICENCIAS_TERCEROS.txt "$DIST/"
  git rev-parse HEAD > "$DIST/commit"

  ANTERIOR=$(git describe --tags --abbrev=0 2>/dev/null)
  {
    echo "Chispas $ETIQUETA ($( [ -n "$PRE" ] && echo beta || echo final ))."
    echo
    echo "## Cambios"
    echo
    git log --reverse --format='- %s' ${ANTERIOR:+$ANTERIOR..HEAD}
    echo
    echo "## Instalación"
    echo
    echo "- Por OTA: CONF > CONFIG > ACTUALIZAR FW."
    echo "- Por USB (imagen completa en 0x0): \`chispas-$VER-usb.bin\` (con encoder) o \`chispas-$VER-sin-encoder-usb.bin\`."
  } > "$DIST/notas.md"

  echo
  ls -l "$DIST" | awk 'NR>1 {printf "   %-40s %9s\n", $9, $5}'
  echo
  echo "Siguiente: probar en placa, editar $DIST/notas.md y, con la confirmacion, ./publicar.sh --subir"
  ;;

--subir)
  [ -f "$DIST/commit" ] || falla "no hay $DIST; ejecutar antes ./publicar.sh"
  [ "$(cat "$DIST/commit")" = "$(git rev-parse HEAD)" ] || falla "$DIST se preparo con otro commit; repetir ./publicar.sh"
  "$GH" auth status >/dev/null 2>&1 || falla "gh sin sesion: gh auth login"
  "$GH" release create "$ETIQUETA" --draft $PRE --target "$(cat "$DIST/commit")" \
    --title "Chispas $ETIQUETA" --notes-file "$DIST/notas.md" \
    "$DIST/chispas-$VER.bin" "$DIST/chispas-$VER-sin-encoder.bin" \
    "$DIST/chispas-$VER-usb.bin" "$DIST/chispas-$VER-sin-encoder-usb.bin" \
    "$DIST/LICENCIAS_TERCEROS.txt" || falla "gh release create"
  echo "Borrador creado. Revisarlo en GitHub y, si esta bien: ./publicar.sh --publicar"
  ;;

--publicar)
  "$GH" release edit "$ETIQUETA" --draft=false || falla "gh release edit"
  git fetch -q --tags origin
  echo "Publicada $ETIQUETA; las placas la encuentran en CONF > CONFIG > ACTUALIZAR FW."
  ;;

*)
  sed -n '5,18p' "$0" | sed 's/^# \{0,1\}//'
  exit 1
  ;;
esac
