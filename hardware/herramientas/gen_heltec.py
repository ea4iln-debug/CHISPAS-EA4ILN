# SPDX-License-Identifier: GPL-3.0-or-later
# Genera el simbolo (lib/heltec.kicad_sym) y la huella (lib/chispas.pretty/Heltec_WiFi_LoRa_32_V3.kicad_mod)
# de la Heltec WiFi LoRa 32 V3 enchufada en dos zocalos hembra 1x18.
# Geometria tomada de la placa perforada (docs/gen_placa.py), comprobada con la Heltec montada: filas a 9 pasos
# (22,86 mm), pin 1 (GND, cuadrado) junto al USB; vista desde arriba con el USB abajo, J3 a la izquierda.
# Pads: 1-18 = J3 pin 1-18, 19-36 = J2 pin 1-18.
# Uso: python gen_heltec.py   (desde cualquier carpeta)
from pathlib import Path

LIB = Path(__file__).resolve().parent.parent / "lib"

J3 = ["GND", "3V3", "3V3", "GPIO37", "GPIO46", "GPIO45", "GPIO42", "GPIO41", "GPIO40", "GPIO39", "GPIO38", "GPIO1",
      "GPIO2", "GPIO3", "GPIO4", "GPIO5", "GPIO6", "GPIO7"]
J2 = ["GND", "5V", "Ve", "Ve", "GPIO44", "GPIO43", "RST", "GPIO0", "GPIO36", "GPIO35", "GPIO34", "GPIO33", "GPIO47",
      "GPIO48", "GPIO26", "GPIO21", "GPIO20", "GPIO19"]

# Tipo electrico para el ERC: un pin de cada red de alimentacion es la salida (la Heltec da 3V3 y GND);
# el otro, pasivo, para que no choquen dos salidas.
TIPO = {(3, 1): "power_out", (3, 2): "power_out", (3, 3): "passive", (2, 1): "passive", (2, 2): "passive",
        (2, 3): "passive", (2, 4): "passive", (2, 7): "passive"}

P = 2.54
FILA = 9 * P              # separacion entre filas
LARGO, ANCHO = 50.2, 25.5  # placa Heltec
MARGEN_PIN18 = 0.89 * P    # borde de la placa mas alla del pin 18

# Modelos 3D. La Heltec (lib/chispas.3dshapes/Heltec_WiFi_LoRa_32_V3.step, de Onshape) tiene el eje X a lo largo,
# el USB hacia -X, la placa de z -1,65 a 0 y el plastico de las tiras macho hasta z -4,2; sus pines (centro en
# X -12,27, Y 9,51) se alinean con los pads. Encima de los zocalos de 8,5 mm (C2905422): z = 8,5 + 4,2.
# En el modelo, del pin 1 al borde del USB hay 2,7 mm (en la huella, 4,76): sin medir en la placa real.
ZOCALO = 8.5
MODELOS = [("${KIPRJMOD}/lib/chispas.3dshapes/Heltec_WiFi_LoRa_32_V3.step", (9.51, 12.27, ZOCALO + 4.2), -90)] + [
    ("${KICAD10_3DMODEL_DIR}/Connector_PinSocket_2.54mm.3dshapes/PinSocket_1x18_P2.54mm_Vertical.step",
     (x, -8.5 * P, 0), 180) for x in (-FILA / 2, FILA / 2)]   # zocalos: origen en el pin 1, y del 3D hacia arriba


def simbolo():
    alto = 19 * P
    pins = []
    for conector, nombres, x, ang, base in (("J3", J3, -15.24, 0, 0), ("J2", J2, 15.24, 180, 18)):
        for i, nombre in enumerate(nombres):
            num = base + i + 1
            y = (8.5 - i) * P   # pin 1 arriba
            tipo = TIPO.get((int(conector[1]), i + 1), "bidirectional")
            pins.append(
                f'      (pin {tipo} line (at {x:.2f} {y:.2f} {ang}) (length 3.81)\n'
                f'        (name "{nombre}" (effects (font (size 1.27 1.27))))\n'
                f'        (number "{num}" (effects (font (size 1.27 1.27)))))')
    caja = (f'      (rectangle (start -11.43 {alto / 2 + 1.27:.2f}) (end 11.43 {-alto / 2 - 1.27:.2f})\n'
            f'        (stroke (width 0.254) (type default)) (fill (type background)))')
    textos = (f'      (text "J3" (at -8.89 {alto / 2 + 2.54:.2f} 0) (effects (font (size 1.27 1.27))))\n'
              f'      (text "J2" (at 8.89 {alto / 2 + 2.54:.2f} 0) (effects (font (size 1.27 1.27))))')
    prop = lambda n, v, y, oculto=False: (
        f'    (property "{n}" "{v}" (at 0 {y:.2f} 0) (effects (font (size 1.27 1.27)){" (hide yes)" if oculto else ""}))')
    return f'''(kicad_symbol_lib
  (version 20241209)
  (generator "gen_heltec.py")
  (symbol "Heltec_WiFi_LoRa_32_V3"
    (exclude_from_sim no) (in_bom no) (on_board yes)
{prop("Reference", "U", alto / 2 + 7.62)}
{prop("Value", "Heltec_WiFi_LoRa_32_V3", alto / 2 + 5.08)}
{prop("Footprint", "chispas:Heltec_WiFi_LoRa_32_V3", -alto / 2 - 3.81, True)}
{prop("Datasheet", "https://heltec.org/project/wifi-lora-32-v3/", -alto / 2 - 5.08, True)}
{prop("Description", "Heltec WiFi LoRa 32 V3 en dos zocalos 1x18 (J3 pads 1-18, J2 pads 19-36)", -alto / 2 - 6.35, True)}
    (symbol "Heltec_WiFi_LoRa_32_V3_0_1"
{caja}
{textos}
    )
    (symbol "Heltec_WiFi_LoRa_32_V3_1_1"
{chr(10).join(pins)}
    )
  )
)
'''


def huella():
    # Origen en el centro de la placa Heltec; y crece hacia el USB (abajo).
    y_pin = lambda p: (8.5 - (p - 1)) * P   # pin 1 en y = +21,59 (junto al USB)
    y_arriba = y_pin(18) - MARGEN_PIN18
    y_abajo = y_arriba + LARGO
    pads = []
    for x, base in ((-FILA / 2, 0), (FILA / 2, 18)):
        for p in range(1, 19):
            forma = "rect" if p == 1 else "circle"
            pads.append(f'  (pad "{base + p}" thru_hole {forma} (at {x:.3f} {y_pin(p):.3f}) (size 1.7 1.7) (drill 1)'
                        f' (layers "*.Cu" "*.Mask"))')
    lineas = []

    def rect(x1, y1, x2, y2, capa, ancho):
        lineas.append(f'  (fp_rect (start {x1:.3f} {y1:.3f}) (end {x2:.3f} {y2:.3f})'
                      f' (stroke (width {ancho}) (type solid)) (fill no) (layer "{capa}"))')

    # Zocalos hembra (serigrafia) y placa Heltec (fabricacion y patio)
    for x in (-FILA / 2, FILA / 2):
        rect(x - P / 2, y_pin(18) - P / 2, x + P / 2, y_pin(1) + P / 2, "F.SilkS", 0.12)
    rect(-ANCHO / 2, y_arriba, ANCHO / 2, y_abajo, "F.Fab", 0.1)
    rect(-ANCHO / 2 - 0.25, y_arriba - 0.25, ANCHO / 2 + 0.25, y_abajo + 0.25, "F.CrtYd", 0.05)
    modelos = [f'  (model "{f}" (offset (xyz {x:.3f} {y:.3f} {z:.3f})) (scale (xyz 1 1 1)) (rotate (xyz 0 0 {rz})))'
               for f, (x, y, z), rz in MODELOS]
    texto = lambda t, x, y, capa, tam=1.0: (f'  (fp_text user "{t}" (at {x:.2f} {y:.2f}) (layer "{capa}")'
                                            f' (effects (font (size {tam} {tam}) (thickness {tam * 0.15:.2f}))))')
    return f'''(footprint "Heltec_WiFi_LoRa_32_V3"
  (version 20241229)
  (generator "gen_heltec.py")
  (layer "F.Cu")
  (descr "Heltec WiFi LoRa 32 V3 en dos zocalos hembra 1x18, filas a 22,86 mm. Pads 1-18 = J3, 19-36 = J2. Pin 1 (GND) junto al USB.")
  (attr through_hole)
  (property "Reference" "REF**" (at 0 {y_arriba - 1.5:.2f} 0) (layer "F.SilkS")
    (effects (font (size 1 1) (thickness 0.15))))
  (property "Value" "Heltec_WiFi_LoRa_32_V3" (at 0 0 90) (layer "F.Fab")
    (effects (font (size 1 1) (thickness 0.15))))
{chr(10).join(lineas)}
{texto("J3", -FILA / 2, y_arriba - 1.5, "F.SilkS")}
{texto("J2", FILA / 2, y_arriba - 1.5, "F.SilkS")}
{texto("USB", 0, y_abajo - 1.2, "F.SilkS")}
{texto("OLED (Heltec)", 0, -10, "F.Fab")}
{chr(10).join(pads)}
{chr(10).join(modelos)}
)
'''


(LIB / "heltec.kicad_sym").write_text(simbolo(), encoding="utf-8")
(LIB / "chispas.pretty" / "Heltec_WiFi_LoRa_32_V3.kicad_mod").write_text(huella(), encoding="utf-8")
print("Generados heltec.kicad_sym y Heltec_WiFi_LoRa_32_V3.kicad_mod")
