#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Autorrutado con Freerouting sin instalar nada en el sistema: baja (si faltan) un Java portatil (Eclipse Temurin
# JRE 25, el que pide Freerouting 2.4) y freerouting.jar a ~/.freerouting y ruta un archivo Specctra DSN.
# Uso: hardware/herramientas/freerouting.sh entrada.dsn salida.ses [pasadas]
set -e
FR_VERSION=2.4.1
JAVA_VERSION=25
DEST="$HOME/.freerouting"
JAR="$DEST/freerouting-$FR_VERSION.jar"
mkdir -p "$DEST"
JAVA="$(ls "$DEST"/jre$JAVA_VERSION/*/bin/java.exe 2>/dev/null | head -1)"
if [ -z "$JAVA" ]; then
    echo "Bajando Java $JAVA_VERSION portatil (Temurin) ..." >&2
    curl -sSL -o "$DEST/jre.zip" "https://api.adoptium.net/v3/binary/latest/$JAVA_VERSION/ga/windows/x64/jre/hotspot/normal/eclipse"
    mkdir -p "$DEST/jre$JAVA_VERSION" && (cd "$DEST/jre$JAVA_VERSION" && unzip -q ../jre.zip) && rm "$DEST/jre.zip"
    JAVA="$(ls "$DEST"/jre$JAVA_VERSION/*/bin/java.exe | head -1)"
fi
if [ ! -f "$JAR" ]; then
    echo "Bajando Freerouting $FR_VERSION ..." >&2
    curl -sSL -o "$JAR" "https://github.com/freerouting/freerouting/releases/download/v$FR_VERSION/freerouting-$FR_VERSION.jar"
fi
[ $# -ge 2 ] || { echo "Listo: $JAVA, $JAR. Uso: $0 entrada.dsn salida.ses [pasadas]"; exit 0; }
"$JAVA" -jar "$JAR" -de "$1" -do "$2" -mp "${3:-100}" --gui.enabled=false
