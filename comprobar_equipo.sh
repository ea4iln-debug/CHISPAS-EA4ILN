#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 EA4ILN
#
# Comprueba que este equipo esta listo para trabajar en Chispas: carpeta, git, PlatformIO, pyserial, gh,
# navegador para los PDF y placa conectada. Dice OK, AVISO o FALTA en cada punto.
#
# Uso:  ./comprobar_equipo.sh              comprobaciones rapidas
#       ./comprobar_equipo.sh --compilar   ademas, compila los dos entornos (./compilar.sh)
# Codigo de salida: el numero de FALTA (0 = todo listo).

cd "$(dirname "$0")" || exit 1

FALTAN=0
ok()    { echo "  OK     $*"; }
aviso() { echo "  AVISO  $*"; }
falta() { echo "  FALTA  $*"; FALTAN=$((FALTAN + 1)); }

PIO="$HOME/.platformio/penv/Scripts/pio.exe"
PY="$HOME/.platformio/penv/Scripts/python.exe"
PKG="$HOME/.platformio/packages"
GH=gh
command -v gh >/dev/null 2>&1 || GH="/c/Program Files/GitHub CLI/gh.exe"

echo "== Carpeta y git"
DIR=$(pwd -W 2>/dev/null || pwd)
case "$DIR" in
  *OneDrive*) falta "el clon esta en OneDrive ($DIR); debe estar en C:/proyectos/CHISPAS-EA4ILN" ;;
  [Cc]:/proyectos/CHISPAS-EA4ILN) ok "clon en $DIR" ;;
  *) aviso "clon en $DIR (lo acordado es C:/proyectos/CHISPAS-EA4ILN)" ;;
esac
URL=$(git remote get-url origin 2>/dev/null)
[[ "$URL" == *ea4iln-debug/CHISPAS-EA4ILN* ]] && ok "origin: $URL" || falta "origin no es ea4iln-debug/CHISPAS-EA4ILN ($URL)"
NOMBRE=$(git config user.name); CORREO=$(git config user.email)
[ -n "$NOMBRE" ] && [ -n "$CORREO" ] && ok "git user: $NOMBRE <$CORREO>" || falta "git config user.name / user.email"
if git fetch -q origin 2>/dev/null; then
  DETRAS=$(git rev-list --count HEAD..origin/main); DELANTE=$(git rev-list --count origin/main..HEAD)
  [ "$DETRAS" = 0 ] && ok "al dia con origin/main" || aviso "$DETRAS commits por bajar (git pull)"
  [ "$DELANTE" = 0 ] || aviso "$DELANTE commits sin subir (git push)"
else
  falta "git fetch (red o credenciales)"
fi
[ -z "$(git status --porcelain)" ] || aviso "hay cambios sin commit"
ANTIGUA="$USERPROFILE/OneDrive/Documentos/mrmorse/Mr.-Morse-EA4ILN"
if [ -d "$ANTIGUA/.git" ]; then
  SIN_SUBIR=$(git -C "$ANTIGUA" log --oneline origin/main.. 2>/dev/null | wc -l)
  CAMBIOS=$(git -C "$ANTIGUA" status --porcelain 2>/dev/null | grep -v LEEME_CARPETA_MOVIDA | wc -l)
  if [ "$SIN_SUBIR" -gt 0 ] || [ "$CAMBIOS" -gt 0 ]; then
    falta "la copia antigua de OneDrive tiene $SIN_SUBIR commits sin subir y $CAMBIOS cambios: pasarlos antes de borrarla"
  else
    aviso "queda la copia antigua de OneDrive (sin nada pendiente); no borrarla hasta migrar los tres equipos"
  fi
fi

echo "== PlatformIO y Python"
[ -x "$PIO" ] && ok "pio: $("$PIO" --version 2>/dev/null)" || falta "PlatformIO (extension de VS Code) no instalado"
# PlatformIO puede tenerla en espressif32/ o, si alli hay otra version, en espressif32@6.13.0/
VERPLAT=$(grep -h -m1 '"version"' "$HOME"/.platformio/platforms/espressif32*/platform.json 2>/dev/null \
  | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | sort -u | tr '\n' ' ')
[[ " $VERPLAT" == *" 6.13.0 "* ]] && ok "plataforma espressif32 6.13.0" \
  || falta "plataforma espressif32 6.13.0 (hay: ${VERPLAT:-ninguna}; se instala al compilar)"
[ -f "$PKG/framework-arduinoespressif32/tools/partitions/boot_app0.bin" ] && ok "framework Arduino (boot_app0.bin)" \
  || falta "framework-arduinoespressif32 (se instala al compilar)"
[ -f "$PKG/tool-esptoolpy/esptool.py" ] && ok "esptool.py" || falta "tool-esptoolpy (se instala al cargar en placa)"
SER=$("$PY" -c "import serial; print(serial.VERSION)" 2>/dev/null)
[ -n "$SER" ] && ok "pyserial $SER" || falta "pyserial: $PY -m pip install pyserial"

echo "== GitHub CLI (releases)"
if [ -x "$GH" ] || command -v gh >/dev/null 2>&1; then
  if "$GH" auth status >/dev/null 2>&1; then
    ok "gh con sesion ($("$GH" api user -q .login 2>/dev/null))"
  else
    falta "gh sin sesion: en una terminal, & \"C:\\Program Files\\GitHub CLI\\gh.exe\" auth login"
  fi
else
  aviso "gh no instalado (solo hace falta para publicar.sh): winget install GitHub.cli"
fi

echo "== Navegador para los PDF (skill esquema)"
EDGE="/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
CHROME="/c/Program Files/Google/Chrome/Application/chrome.exe"
if [ -f "$EDGE" ]; then ok "Edge"; elif [ -f "$CHROME" ]; then ok "Chrome"; else aviso "ni Edge ni Chrome"; fi

echo "== Placa"
PUERTOS=$("$PY" -c "from serial.tools import list_ports
print(' '.join(p.device for p in list_ports.comports() if p.vid == 0x10C4 and p.pid == 0xEA60))" 2>/dev/null)
[ -n "$PUERTOS" ] && ok "CP210x en $PUERTOS" || aviso "ninguna placa conectada (o falta el driver CP210x de Silicon Labs)"

if [ "$1" = "--compilar" ]; then
  echo "== Compilacion"
  ./compilar.sh || falta "no compila (ver .pio/compilar.log)"
fi

echo
[ $FALTAN -eq 0 ] && echo "Equipo listo." || echo "Faltan $FALTAN cosas."
exit $FALTAN
