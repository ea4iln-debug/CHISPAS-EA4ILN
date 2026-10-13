#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 EA4ILN
#
# Compila Chispas y muestra solo lo importante: errores (con contexto), avisos de nuestro codigo (src/),
# cuantos avisos vienen de librerias, RAM, flash y el resultado de cada entorno.
#
# Uso:  ./compilar.sh            los dos entornos del firmware
#       ./compilar.sh enc        solo heltec_wifi_lora_32_V3 (placa 1, con encoder)
#       ./compilar.sh sin        solo heltec_sin_encoder (placa 2)
#       ./compilar.sh -v ...     ademas, la salida completa en .pio/compilar.log (siempre se guarda)
# Codigo de salida: 0 si todos compilan, 1 si alguno falla.

cd "$(dirname "$0")" || exit 1

PIO=pio
command -v pio >/dev/null 2>&1 || PIO="$HOME/.platformio/penv/Scripts/pio.exe"

VERBOSO=0
if [ "$1" = "-v" ]; then VERBOSO=1; shift; fi

case "$1" in
  ""|todos) ENTORNOS="heltec_wifi_lora_32_V3 heltec_sin_encoder" ;;
  enc)      ENTORNOS="heltec_wifi_lora_32_V3" ;;
  sin)      ENTORNOS="heltec_sin_encoder" ;;
  *)        ENTORNOS="$*" ;;
esac

LOG=.pio/compilar.log
mkdir -p .pio
FALLO=0
: > "$LOG"

for E in $ENTORNOS; do
  SALIDA=$("$PIO" run -e "$E" 2>&1)
  ESTADO=$?
  printf '%s\n' "$SALIDA" >> "$LOG"

  if [ $ESTADO -eq 0 ]; then RES="OK"; else RES="FALLO"; FALLO=1; fi
  RAM=$(printf '%s\n' "$SALIDA" | grep -E '^RAM:' | sed -E 's/.*\] *//')
  FLASH=$(printf '%s\n' "$SALIDA" | grep -E '^Flash:' | sed -E 's/.*\] *//')
  TIEMPO=$(printf '%s\n' "$SALIDA" | grep -oE 'Took [0-9.]+ seconds' | tail -1 | grep -oE '[0-9.]+')
  echo "== $E: $RES (${TIEMPO:-?} s)"
  [ -n "$RAM" ] && echo "   RAM   $RAM"
  [ -n "$FLASH" ] && echo "   Flash $FLASH"

  # Errores: la linea y las 3 siguientes (codigo y marca ^)
  printf '%s\n' "$SALIDA" | grep -E -A3 '(error|Error)[: ]|undefined reference|\*\*\*' | grep -vE '^(Compiling|Archiving|Indexing|Environment|=====|-----|--)' | head -60

  # Avisos de nuestro codigo, sin repetir; de las librerias solo el numero
  printf '%s\n' "$SALIDA" | grep -E '^(src|include)[/\\][^:]+:[0-9]+:[0-9]+: warning:' | sort -u | head -30
  OTROS=$(printf '%s\n' "$SALIDA" | grep -E 'warning:' | grep -cvE '^(src|include)[/\\]')
  [ "$OTROS" -gt 0 ] && echo "   ($OTROS avisos de librerias o del core; ver $LOG)"
done

[ $VERBOSO -eq 1 ] && echo "Salida completa: $LOG"
exit $FALLO
