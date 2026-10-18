#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 EA4ILN
#
# Manual de usuario (web/manual.html): PDF y capturas de comprobacion con Chrome o Edge sin interfaz.
#
#   ./web/manual.sh                 PDF en docs/manual_chispas.pdf
#   ./web/manual.sh --vista [ids]   capturas en .pio/manual/: robots.png (todas las poses de CHISPAS en rejilla)
#                                   y s_<id>.png de cada seccion pedida (ids de <section>: cw, chispas,
#                                   primeros-pasos, entrenador, lora, trx, prop, pota, sol, conf, actualizar,
#                                   cara, problemas, glosario; "portada" = portada e indice)
#   ./web/manual.sh --imprimir      simula la version impresa (CSS de print en pantalla, 180 mm) en imp.png
#
# Las imagenes del manual salen de docs/img/ (la web las copia a img/ con preparar.py): se trabaja en una copia
# en .pio/manual con img/. Sin poppler, Read no abre PDF: para revisar, mirar las capturas.

set -euo pipefail
cd "$(dirname "$0")/.."

NAV="/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
[ -f "$NAV" ] || NAV="/c/Program Files/Google/Chrome/Application/chrome.exe"
PY=~/.platformio/penv/Scripts/python.exe
TMP=.pio/manual
mkdir -p "$TMP/img"
cp web/manual.html "$TMP/"
cp docs/img/* "$TMP/img/"
URL="file:///$(cygpath -m "$PWD/$TMP")"

captura() {  # captura <html> <png> <ancho> <alto>
  "$NAV" --headless --disable-gpu --hide-scrollbars --virtual-time-budget=8000 --window-size="$3,$4" \
    --screenshot="$(cygpath -w "$PWD/$TMP/$2")" "$URL/$1" >/dev/null 2>&1
  echo "  $TMP/$2"
}

case "${1:-}" in
  --vista)
    shift
    "$PY" -I - "$TMP/manual.html" <<'EOF'
import re, sys, pathlib
p = pathlib.Path(sys.argv[1]); h = p.read_text(encoding="utf-8")
estilo = re.search(r"<style>(.*?)</style>", h, re.S).group(1)
piezas = re.search(r'(<svg width="0".*?</svg>)', h, re.S).group(1)
svgs = re.findall(r'(<svg (?:class="robot-[a-z]+" )?viewBox="0 0 160 190".*?</svg>)', h, re.S)
nombres = [re.search(r'aria-label="([^"]*)"', s).group(1) for s in svgs]
celdas = "".join(f'<div class="c">{s}<p>{i}: {n}</p></div>' for i, (s, n) in enumerate(zip(svgs, nombres)))
(p.parent / "robots.html").write_text(
    f'<!doctype html><meta charset=utf-8><style>{estilo} body{{background:#f4f5f8}} .g{{display:grid;'
    f'grid-template-columns:repeat(6,1fr);gap:8px;padding:8px}} .c{{background:#fff;border:1px solid #ccc}}'
    f' .c svg{{width:100%;height:auto;position:static}} p{{font:11px sans-serif;color:#222;margin:3px}}</style>'
    f'{piezas}<div class="g">{celdas}</div>', encoding="utf-8")
print(f"  {len(svgs)} poses")
EOF
    captura robots.html robots.png 1400 1500
    for s in "$@"; do
      if [ "$s" = portada ]; then oculta=".seccion{display:none}"; else oculta=".portada,.indice,.seccion:not(#$s){display:none}"; fi
      sed "s#</style>#$oculta</style>#" "$TMP/manual.html" > "$TMP/s_$s.html"
      captura "s_$s.html" "s_$s.png" 1280 4200
    done
    ;;
  --imprimir)
    sed 's/@media print {/@media screen {/; s#</style>#body{width:180mm;margin:0 auto;font-size:10.5pt} .seccion{border-top:4px dashed red !important;margin-top:20px}</style>#' \
      "$TMP/manual.html" > "$TMP/imp.html"
    captura imp.html imp.png 800 3000
    ;;
  "")
    SALIDA="$(cygpath -w "$PWD/docs/manual_chispas.pdf")"  # Ruta absoluta: con relativa, Chrome no la encuentra
    "$NAV" --headless --disable-gpu --no-pdf-header-footer --virtual-time-budget=15000 \
      --print-to-pdf="$SALIDA" "$URL/manual.html" >/dev/null 2>&1
    "$PY" -I -c "import re;d=open('docs/manual_chispas.pdf','rb').read();print('docs/manual_chispas.pdf:', len(d)//1024, 'KB,', len(re.findall(rb'/Type\s*/Page(?!s)',d)), 'paginas')"
    ;;
  *) sed -n '5,13p' "$0"; exit 1 ;;
esac
