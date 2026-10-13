# Hoja de ruta de Chispas

Estado a 12 de octubre de 2026, con el firmware **v1.06b** (beta). Lo marcado ✅ está hecho y probado en placa.

## 1. Pruebas pendientes

Funciones que ya están en el firmware pero falta comprobar en la placa.

| | Prueba | Cómo |
|---|---|---|
| ⬜ | QSO LIBRE en los dos sentidos | Necesita palas en la placa 2 (o cambiar las palas de placa) |
| ⬜ | Expresiones de radioaficionado y SALVAPANTALLAS | Auriculares y nube de tormenta (con TORM!) sin ver aún; tiempo del salvapantallas en CONFIG |
| ⬜ | Salvapantallas y brillo (v1.05b) | Cara a los 15 s en Inicio, PROP, POTA, SOL y CONF (no en CW, LoRa, TRX ni con el portal/QR); brillo tenue al minuto en todas |
| ✅ | Control del amplificador (SD en GPIO38) | Placa 1, 12 oct 2026 (v1.04b por OTA): suena en CW, LoRa, TRX y el pitido de TONO; calla en el resto |
| ✅ | Estabilidad tras muchas horas | Placa 2, noche del 12 oct 2026: 8 h en POTA (233 consultas HTTPS); heap libre ±150 B, mínimo estable tras la 1.ª hora, mayor bloque y pilas sin cambios |
| ✅ | OTA del binario sin encoder a la v1.02b y a la v1.03b | Placa 2, 12 oct 2026 |
| ✅ | Batería después del decodificador | Placa 2, 12 oct 2026: la tensión se sigue leyendo bien tras DECODIFICAR y QSO |
| ✅ | QSO del TRX con emisora real | 12 oct 2026: manipular y decodificar en la misma sesión, sin descodificar lo propio |
| ✅ | Keyer iámbico | 12 oct 2026: apretón en modo A y B, y textos largos |
| ✅ | Xiegu X6200 con KEY | 12 oct 2026, en llave recta con el puente R-T de J4: TUNE, palas y memorias |
| ✅ | Dormir y despertar en la placa 1 | 12 oct 2026 (antes solo en la placa 2) |
| ✅ | Consumo en deep sleep | Despreciable, medido en µA (12 oct 2026) |

## 2. Hardware

### En camino
- **GPS NEO-6M** (llega el 13 oct): TX del GPS a GPIO47 (J2-13). Apagarlo cuando no se usa, por orden de preferencia:
  1. **Por software (a probar primero):** cable de GPIO39 (J3-10) al RX del GPS y órdenes UBX (`UBX-RXM-PMREQ` para
     dormir; si no despierta bien, `UBX-CFG-RST` con GNSS stop/start). Medir con el polímetro en serie con el VCC del
     GPS: si dormido baja de ~0,5 mA, no hace falta transistor.
  2. **Interruptor en el positivo** (GPIO3, BAJO = encendido): AO3401A, o sus equivalentes en SOT-23 IRLML6401,
     Si2301 o DMG2305UX, con R8 de 100 kΩ; o un PNP de patillas (BC327 o BC557) con 1 kΩ en la base y 10 kΩ de base a
     emisor, que resta ~0,1–0,2 V. No valen el BSS84 ni el BS250 (no conducen bien con 3,3 V).

### Por comprar
- Divisor de minijack 1 → 2, para seguir oyendo la emisora con el decodificador conectado.

### Opcional
- **Trafo de aislamiento 600:600**, solo si aparece zumbido por lazo de masa con la emisora.
- **C3 (10–100 nF) de GPIO6 a GND**, solo si el decodificador da ruido o letras falsas (por ejemplo, al transmitir
  con potencia): filtra la RF y el ruido de alta frecuencia y estabiliza la lectura del ADC.
- **Detección de USB y de carga:** divisor 100k/150k del pin 5V (J2-2) a GPIO33. Antes, comprobar que el pin 5V da
  0 V solo con batería.

### Más adelante
- **PCB propia**, empezando desde cero.

## 3. Desarrollo del firmware

### Con el hardware nuevo
- **GPS:** locator y hora automáticos (UART por la matriz GPIO, NMEA a 9600 bd); el locator alimenta SOL y la hora
  sustituye al NTP cuando no hay WiFi.
- **BME280, lo que queda** (lo básico está hecho en la v1.03b):
  - Comprobar la tendencia y el aviso de tormenta con datos de varias horas.
  - Con el GPS: calibrar la altitud con la del GPS y reiniciar el historial de presión al cambiar de altitud
    (subiendo a un monte la presión baja y puede dar un falso aviso de tormenta).
  - Por decidir: mantener el historial de presión con Chispas dormido (despertar cada 15 min).

### Ideas decididas
- **POTA sin conexión:** lista de parques en un CSV guardado en la placa (partición de 1,5 MB reservada, LittleFS);
  buscar por referencia y, con GPS, el parque más cercano.
- **Cola de spots POTA:** publicar el spot propio al activar un parque (indicativo, referencia, frecuencia, CW) y, si
  no hay cobertura, guardarlo y enviarlo en cuanto haya WiFi. Antes, revisar las condiciones de la API de POTA para
  publicar.
- **Batería en `[DIAG]`:** añadir la tensión y el % a la línea de diagnóstico (p. ej. `| BAT 4.12V 90%`) para ver
  en los registros largos la curva de carga y descarga, y medir la autonomía con un registro en vez de a mano.

### Por decidir
- **Batería llena = 100 %:** tras cargar, la LiPo se relaja a ~4,10–4,15 V y la curva marca ~89–90 % con el LED de
  carga apagado (carga terminada). Propuesta: contar 4,15 V o más como 100 %.
- **SOTA:** las condiciones de su API exigen aprobación previa para software generado con IA.

### A estudiar (no prioritario)
- **Audio I2S:** tono senoidal con rampas (sin clics) y volumen por software con un módulo DAC + amplificador I2S
  (p. ej. MAX98357A, 3 GPIO). Sustituiría al PAM8302A y obligaría a rehacer la salida de auriculares.
- **GPS ATGM336H** en lugar del NEO-6M: más pequeño, más barato y de menor consumo (GPS + BeiDou), con la misma
  conexión (UART a 9600 bd, NMEA). Por ahora se sigue con el NEO-6M.

### Instalación y estructura del firmware (ideas)
- **Web de instalación por USB** con **ESP Web Tools** en GitHub Pages: botón «Instalar» por Web Serial (Chrome o
  Edge de escritorio; no Firefox ni Safari; en Windows sigue haciendo falta el driver CP210x). Dos botones (con y sin
  encoder) y grabación por partes (bootloader, particiones, boot_app0 y app) para **conservar la NVS** al reinstalar,
  salvo que se elija borrar; monitor serie en la misma página. GitHub no sirve los `.bin` de las releases a otra web
  (CORS): una **GitHub Action** los copia a la página al publicar cada release.
- **Firmware modular**, con la opción de **un solo binario con todo y módulos activados en el aparato** (la app ocupa
  bastante menos de la mitad de sus 3,2 MB): autodetección donde se pueda (el BME280 ya se detecta; el GPS, si llegan
  frases NMEA) y una pantalla **CONF > HARDWARE** (o un asistente al primer arranque) para lo que no se detecta
  (encoder, amplificador, KEY…). Un solo binario simplifica la OTA y haría desaparecer la variante sin encoder. La
  web podría llevar casillas de hardware (generando la NVS en el navegador, o mejor preguntándolo al primer arranque).
  Descartado: compilar a la carta en un servidor (lento, necesita un token o un servidor propio) y precompilar todas
  las combinaciones (2^N binarios).
- **Partir `main.cpp` en módulos** (keyer, Koch, Echo, LoRa, TRX, decodificador, WiFi, BME, GPS…) con su `.h`/`.cpp`
  y una interfaz común de disponible/activado; por fases, sin cambiar el comportamiento, compilando y probando en
  placa en cada paso. Prepara el Lite (LoRa o ESP-NOW como transporte intercambiable). Paso previo hecho el 13 oct
  2026: `main.cpp` ya está repartido en `src/parts/*.inc` (mismo código, una sola unidad de compilación).

## Versiones de Chispas (ideas)

### Chispas Pro (el actual, Heltec V3)
- **Bluetooth (BLE)**: el ESP32-S3 solo tiene BLE 5 (nada de auriculares Bluetooth ni puerto serie clásico).
  - **Teclado Bluetooth (HID):** lo manipulado con las palas se escribe en el móvil o el PC; con las teclas que espera
    **VBand**, practicar por internet con las palas, sin cables.
  - **Configuración y estadísticas desde el móvil** con una página **Web Bluetooth** (Chrome, sin instalar nada):
    memorias, indicativo y locator; estadísticas de Koch y Echo con gráficas y exportación.
  - **Hora y posición del móvil** para SOL y la línea gris en el campo, sin WiFi ni GPS.
  - **El móvil como pasarela** en una salida POTA: spots y propagación hacia Chispas, y publicar el spot propio.
  - **Teclado CW desde el móvil** (diseñado el 13 oct 2026, por decidir): servicio **Nordic UART (NUS)** por BLE,
    usable con apps ya existentes (*Serial Bluetooth Terminal* en Android; *Bluefruit Connect* o *nRF Connect*, también
    en iPhone). Opción BLUETOOTH en CONFIG, nombre `Chispas-XXXX` e indicador **BT** en la cabecera. Lo escrito sale
    según la pantalla: por **KEY** en TRX > OPERAR/QSO (a los wpm de TRX, como una memoria), por **LoRa** en QSO LIBRE,
    y por el **altavoz** en TX LIBRE; en el resto se ignora. `{MI}`, `{SU}` y `{WX}`; cola de ~200 caracteres; las
    palas o `/stop` lo cortan; órdenes opcionales (`/wpm 25`, `/su EA1ABC`, `/m1`). Chispas devuelve al móvil lo
    enviado y lo descodificado en TRX > QSO (registro del QSO). Solo transmite por KEY donde ya lo hace y con el corte
    de 10 s. Trabajo: NimBLE + NUS (~200–300 líneas), cola hacia el motor CW (trozos de 64) o QSO LIBRE; antes, medir
    la memoria con BLE y WiFi a la vez. Empezar por TRX y TX LIBRE.
  - Coste: unos 50–100 KB de RAM (hay ~249 KB libres), algo de flash y de consumo; mejor NimBLE que Bluedroid
    (comprobar con el core 2.0.17 y el S3).
- **Carcasa impresa en 3D con diseño propio:** OpenSCAD paramétrico a partir del plano de la placa perforada
  (`docs/gen_placa.py`); logo de chispa, «CHISPAS EA4ILN» en relieve, rejilla del altavoz con «73» en Morse y rótulos
  grabados junto a los jacks. Faltan las medidas de batería, altavoz, jacks, encoder, RV1, GPS, interruptor y SMA, el
  uso (mesa, mano o colgado) y el estilo. Probar por piezas antes de imprimir la caja entera.

### Chispas Ultimate
- **Palas táctiles integradas** (como el Morserino-32): GPIO4 y GPIO5 son táctiles en el S3; el jack de palas pasaría
  a GPIO39/40. Electrodos: tornillos de latón o inoxidable, chapa, trozos de PCB dorada, cinta de cobre por dentro de
  la pared o filamento conductor; 470 Ω–2 kΩ en serie. Firmware: `touchRead()` en la tarea del keyer, calibración al
  arrancar y sensibilidad en CONF. Pegas: humedad, guantes, sensibilidad según se sujete. Prueba previa barata: un
  programa en `pruebas/` con dos tornillos o cinta de cobre.

### Chispas Lite
Un ESP32 cualquiera con OLED, encoder, palas y tono, **sin LoRa**. Hace casi todo lo del Pro; lo que pierde es el
alcance de la radio.
- **Igual que en el Pro** (no dependen de la LoRa): Koch (los dos modos de corrección, estadísticas y refuerzo), Echo,
  TX LIBRE (keyer A/B); PROP, POTA y SOL con NTP; webconf con QR; OTA (con sus propios binarios); CONF, STATUS,
  depuración, salvapantallas y dormir; TRX con KEY por PC817, memorias y TUNE.
- **Duo y QSO por ESP-NOW** (el WiFi del chip, sin router), con las mismas tramas: 50–200 m (más con línea de vista y
  el modo de largo alcance) frente a los kilómetros de la LoRa. **El Pro también tiene WiFi: podría hablar ESP-NOW y
  practicar con un Lite.** ESP-NOW y WiFi comparten radio y canal: durante Duo o QSO no se usa el WiFi (como ahora).
- **Según la placa:**
  - Decodificador: el ADC por DMA está escrito para el S3; en un ESP32 clásico o un C3 hay que adaptar la captura
    (el Goertzel y el resto sirven).
  - Batería: muchas placas no traen cargador ni divisor; añadir un TP4056 y un divisor.
  - Bluetooth: un ESP32 **clásico** tiene Bluetooth clásico además de BLE; se podría estudiar sacar el tono a
    auriculares Bluetooth (A2DP), que el S3 no puede. Experimental.
  - BME280, GPS y amplificador: opcionales, igual que en el Pro, si hay pines.
- **No tendría:** LoRa (ni largo alcance ni Duo/QSO por radio con un Pro lejano) ni OLED integrada, salvo placa que la
  traiga.
- **Coste:** la Heltec V3 cuesta unos 20–25 €; un ESP32 con OLED, encoder y cargador, unos 12–15 €. El ahorro está más
  en lo que se deja fuera (LoRa, GPS, BME280, amplificador) que en la placa: el Lite tiene sentido como versión más
  sencilla y barata para clubes o alumnos.
- **Firmware:** el mismo `main.cpp` con un entorno `chispas_lite` (como `heltec_sin_encoder`); lo principal, separar
  la capa de transporte (LoRa / ESP-NOW), ya decidida como intercambiable.

## 4. Documentación y publicación
- Al montar el GPS: esquema (quitar PENDIENTE), README y capturas.
- Próxima release (v1.07b) con el GPS.

## Orden propuesto
1. **Sin piezas:** QSO LIBRE en los dos sentidos; tendencia del BME280 con varias horas de datos (placa 1).
2. **Salvapantallas y brillo:** probar la v1.05b.
3. **Al llegar el GPS:** montaje, locator y hora automáticos; release v1.07b.
4. **Después:** POTA sin conexión, cola de spots y PCB.

## Hecho
- ✅ Entrenador Koch estilo LCWO con corrección por letra o por total, Echo y TX libre (keyer iámbico A/B).
- ✅ Estadísticas por carácter y por lección (Koch y Echo), y refuerzo de las letras con más errores.
- ✅ Práctica en pareja y QSO libre por LoRa.
- ✅ Tono por altavoz (amplificador PAM8302A) y auriculares, con volumen; SD del amplificador en GPIO38 desde la
  v1.04b. El buzzer piezo del principio sonaba demasiado bajo y se descartó.
- ✅ Salida KEY aislada con tune y memorias (probada con una uSDR).
- ✅ Decodificador CW del audio de la emisora (probado con audio Morse real).
- ✅ Propagación, spots POTA, sol y línea gris.
- ✅ Configuración en el aparato, página de estado y depuración desde el menú.
- ✅ Sensor ambiental BME280 (v1.03b): página AMBIENTE en SOL, tendencia y aviso de tormenta, altitud barométrica
  calibrable y macro {WX}.
- ✅ Actualización por WiFi desde las releases de GitHub.
- ✅ Consumo en reposo despreciable.
