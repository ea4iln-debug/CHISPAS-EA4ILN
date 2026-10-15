# SPDX-License-Identifier: GPL-3.0-or-later
# Busca piezas en el catalogo de montaje de JLCPCB: codigo LCSC, Basic/Extended, existencias y precio.
# Uso: python buscar_jlc.py "0805 10kΩ" "PC817" ...
import json, sys, urllib.request

sys.stdout.reconfigure(encoding="utf-8")
URL ="https://jlcpcb.com/api/overseas-pcb-order/v1/shoppingCart/smtGood/selectSmtComponentList"

def buscar(kw, n=8):
    body = json.dumps({"keyword": kw, "currentPage": 1, "pageSize": n}).encode()
    req = urllib.request.Request(URL, body, {"Content-Type": "application/json", "User-Agent": "Mozilla/5.0"})
    d = json.load(urllib.request.urlopen(req, timeout=30))
    lst = (d.get("data") or {}).get("componentPageInfo", {}).get("list") or []
    print(f"=== {kw}")
    for c in lst:
        if c["componentCode"].startswith("C99"):
            continue
        p = c.get("componentPrices") or [{}]
        tipo = {"base": "BASIC", "expand": "ext"}.get(c.get("componentLibraryType"), c.get("componentLibraryType"))
        if c.get("preferredComponentFlag"):
            tipo += "(pref)"
        print(f"  {c['componentCode']:>10} {tipo:10} st={c.get('stockCount'):>8} ${p[0].get('productPrice')}  "
              f"{c.get('componentModelEn')} | {c.get('componentSpecificationEn')} | {(c.get('describe') or '')[:90]}")

for kw in sys.argv[1:]:
    try:
        buscar(kw)
    except Exception as e:
        print("ERR", kw, e)
