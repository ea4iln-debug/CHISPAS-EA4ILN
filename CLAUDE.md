# Chispas EA4ILN — contexto del proyecto

Resumen para cada sesión. El detalle (historial de pruebas, medidas, por qué de cada decisión, descripción completa
de cada función) está en **`docs/NOTAS.md`**, con las mismas secciones: leer solo la del tema que se vaya a tocar.
Ideas futuras: `ROADMAP.md`. Para GitHub: `README.md`.

## Qué es
**Chispas** ("chispas" era el apodo del radiotelegrafista de a bordo): aparato portátil de CW sobre una **Heltec
WiFi LoRa 32 V3** (ESP32-S3FN8, SX1262 a 868 MHz, OLED 128×64, sin PSRAM, 8 MB de flash). Entrenador Koch (LCWO) y
Echo, keyer iámbico, práctica y QSO por LoRa, conexión con el transceptor (KEY, tune, memorias, decodificador),
propagación, spots POTA, sol y línea gris, sensor BME280, configuración en el aparato y OTA desde GitHub.

## Repositorio y licencias
- `https://github.com/ea4iln-debug/CHISPAS-EA4ILN`. Tres equipos; en cada uno, clon en **`C:\proyectos\CHISPAS-EA4ILN`**,
  fuera de OneDrive, sincronizado solo con Git (`git pull` al empezar, `git push` al terminar). Migrados los tres
  (13 oct); las copias antiguas de OneDrive (`Documentos\mrmorse\Mr.-Morse-EA4ILN` y, en el equipo 3,
  `Documentos\PlatformIO\Projects\Chispas-EA4ilN`) ya no se usan (ver NOTAS: "Repositorio").
  VS Code: Settings Sync con la cuenta GitHub `ea4iln-debug` (activado en los tres equipos).
  **`./comprobar_equipo.sh [--compilar]`** dice si un equipo está listo (pasos de migración en NOTAS).
- Software GPL-3.0-or-later (`LICENSE`); hardware y documentación CC BY-NC-SA 4.0 (`LICENSE-HARDWARE.md`).
  **Archivos de código nuevos: cabecera SPDX.** `include/wm_strings_chispas.h`: `MIT AND GPL-3.0-or-later`.
  Los `.bin` de las releases van con `docs/LICENCIAS_TERCEROS.txt` (regenerarlo si cambian las librerías).

## Versiones, releases y OTA (detalle en NOTAS: "Releases y OTA")
- `#define FW_VERSION` y `@version` en la cabecera de `main.cpp`. Formato `X.YYb` (b = beta; sin letra, final).
  **Preguntar siempre al usuario antes de subir la versión y antes de publicar una release** (llegan por OTA).
  Procedimiento: skill **`release`** (`./publicar.sh` en tres pasos, borrador antes de publicar).
- OTA: CONF > CONFIG > ACTUALIZAR FW; TLS con `include/certificados_github.h`.
- Depuración desde CONF > CONFIG > DEPURACION (órdenes serie, `[DIAG]`, `[DECOD]`, `[CW]`); apagada por defecto.

## Estado actual (15 oct 2026)
- **v1.10** (release del 15 oct, primera final; WIFI DATOS MANUAL/AUTO y reinicio de la OTA sin avance); instalada por OTA en las dos placas el 15 oct (placa 1 vista por serie: 27 s, sin cortes). Antes,
  v1.09b (14 oct). **Nombres de las placas desde el 15 oct** (antes, al revés; lo anterior en NOTAS usa los viejos):
  **placa 1 = placa perforada** (MAC `08:c0`, montada y probada el 15 oct) y **placa 2 = protoboard** (MAC `50:74`,
  OLED con defecto de hardware). Las dos con encoder, entorno `heltec_wifi_lora_32_V3`.
- **Fin del soporte sin encoder:** la v1.10 es la última con `heltec_sin_encoder` (`-DSIN_ENCODER`); desde la 1.11
  no se compila ni se publica su binario.
- PlatformIO, `platform = espressif32 @ 6.13.0` (core Arduino 2.0.17), RadioLib ^6.6, Adafruit GFX/SSD1306,
  WiFiManager 2.0.17, ArduinoJson 7. **Un solo `.cpp` en `src/`** (el resto en `src/parts/*.inc`, ver "Código");
  `pio run` compila solo el firmware
  (`default_envs`); pruebas aparte en `pruebas/` (`prueba_pines`, `prueba_bateria`, `prueba_gps`). Puerto COM según el equipo
  (driver CP210x).
- Pruebas con LED, pantalla, monitor serie y audio (altavoz y auriculares).
- Placa perforada (placa 1) montada completa el 15 oct, con el jack de auriculares: todo bien, circuito correcto.
  **Su P1 (encoder) va CLK, SW, DT** (cable con DT y SW cruzados); el plano, desde el 15 oct, CLK, DT, SW con cable recto.

## Arquitectura (detalle en NOTAS: "Funciones" y "Manejo")
- Pantallas `PANTALLA_*`: 1 Inicio, 2 CW, 3 LoRa, 4 TRX, 5 PROP, 6 POTA, 7 Sol, 8 CONF. Cabecera: hora, nombre corto,
  batería (`PWR` sin batería).
- **Estilo común (v0.9h):** menús con título en negativo (`dibujarTituloMenu()`) y opciones `< ... >`; ajustes en
  listas (`dibujarListaOpciones()`); datos en páginas (`dibujarPiePaginas()`); pantallas de manipular/escuchar con
  pie corto y "menu". Inicio es una portada fija. **Mantener este estilo (fuente 5×7, sin imitar Meshtastic).**
- Entrada: encoder KY-040 por interrupción (`isrEncoder`), eventos `BOTON_*`/`ENC_*` en `procesarBoton()`.
  PRG corto = atrás, largo = dormir; despiertan PRG y el eje del encoder. Sin encoder: PRG corta = mover, larga = ejecutar, muy larga = dormir.
- Tareas FreeRTOS: `motorCW` (núcleo 1, prio 3; API `motorCWEmitir/Detener/Ocupado/Portadora`; solo las tareas
  llaman a `conmutarEmisionMorse()`), `keyer` (palas cada 1 ms), `decod` (núcleo 0, ADC por DMA a 8 kHz, Goertzel).
- LittleFS (partición `spiffs`): lista de parques POTA de España (`/parques.bin`), se baja sola cada 30 días.
- NVS: `chispas` (entrenador, keyer, Echo, estadísticas, ajustes), `estacion` (indicativo, locator, UTC, QNH,
  memorias); WiFi en el NVS del ESP32. Borrar estadísticas solo borra estadísticas; el reset de fábrica, todo.
- Consumo: CPU a 80 MHz sin WiFi y 240 con WiFi; LoRa dormida fuera de Duo/QSO. **Al despertar el SX1262 hay que
  llamar a `radio.standby(RADIOLIB_SX126X_STANDBY_RC, true)` antes de `startReceive()`** (si no, no recibe).
- Hora: `ponerHora()` con fuente `fuenteHora` (GPS > WIFI > MANUAL: se queda la más exacta; la manual se aplica siempre).
  NTP solo si la hora no viene del GPS.
- GPS (opcional, AUSENTE si no contesta al arrancar): duerme por UBX; se despierta al arrancar, cada 10 min y en
  STATUS > GPS; antes del deep sleep, PMREQ y GPIO39 retenido en ALTO. Historial de presión a nivel del mar con su altitud.
- WiFi de PROP, POTA y SOL según CONFIG > **WIFI DATOS** (15 oct): `MANUAL` (defecto) solo con ACTUALIZAR, con aviso
  a pantalla completa, y se apaga al acabar; `AUTO`, al entrar y con refresco. Las descargas HTTPS bloquean el `loop()`
  unos segundos (por eso el aviso). El portal (`Chispas-XXXX`, clave `chispas73`) no se abre solo.
- KEY (GPIO2) solo con `keyHabilitada` (OPERAR y QSO de TRX); corte de seguridad a los 10 s.
- Amplificador: SD (GPIO38) en ALTO solo en CW, LoRa, TRX y pitido de prueba; en BAJO retenido al dormir.
- Dormir (deep sleep, despierta con PRG): KEY, Vext, divisor y SD retenidos con `gpio_hold`; se liberan en `setup()`.
- Batería: GPIO37 **HIGH** activa el divisor (Meshtastic dice LOW), 2,5 dB, factor 4,9 calibrado; "sin batería"
  por dispersión de 200 lecturas.

## Código (detalle en NOTAS: "Organización del código")
`main.cpp` (cabecera, `FW_VERSION`, includes, periféricos, dormir, `setup`, `loop`) incluye en este orden los
`src/parts/*.inc` (trozos seguidos del antiguo `main.cpp`: cada uno usa lo de los anteriores, no compilan sueltos).
Abrir solo el que toca; los avisos de IntelliSense en los `.inc` no son errores.
| Archivo | Contenido |
|---|---|
| `pines` `morse` `globales` | Pines; diccionario, Koch y caracteres de la trama LoRa; objetos y variables globales |
| `audio` `motor_cw` `keyer` | Tono, LED y SD; tarea `motorCW`; tarea `keyer` y palas |
| `estadisticas` `echo` `bme280` | Estadísticas por carácter; modo Echo; sensor |
| `memoria_nvs` `bateria` `oled_comun` | NVS; batería; cabecera, pies, `dibujarTituloMenu`… |
| `lora` `decodificador` `trx` | TX/RX, Duo y QSO; tarea `decod`; TRX + `procesarRecepcionLoRa`, `salirModoLoRa` |
| `wifi` `ota` `datos_web` `gps` `parques` | WiFi, NTP, portal y QR; OTA; hamqsl, POTA, `procesarWifi`; GPS (ventanas, hora, locator, altitud); parques POTA cercanos (LittleFS) |
| `pant_inicio_cw` `pant_lora_trx` `pant_prop_pota` | Dibujo de Inicio y CW; de LoRa y TRX; menús y páginas de PROP y POTA |
| `sol` `conf` `estados_cw` | SOL y ambiente; CONF, STATUS, OTA y `actualizarPantalla()`; máquina de estados CW |
| `cara` `entrada` `eventos` | Salvapantallas; encoder, ISR y eventos de botón; `procesarBoton()` |
- Archivo nuevo en `src/parts/`: extensión `.inc`, cabecera SPDX, y su `#include` en `main.cpp` detrás de lo que use.

## Hardware (detalle en NOTAS: "Prototipo en protoboard" y "Placa perforada")
| GPIO | Uso |
|---|---|
| 2 | KEY (PC817 330 Ω, 10 kΩ a GND, J4 con puente R-T) |
| 4 / 5 | Pala DIT / DAH (pull-up interno) |
| 6 | Entrada del decodificador (ADC1) |
| 7 | Tono PWM → RV1 → PAM8302A + altavoz (TN del jack) y auriculares |
| 38 | SD del PAM8302A (BAJO = apagado) |
| 19 / 20 / 48 | Encoder CLK / SW / DT (SW en pin RTC: despierta del deep sleep; cambiado el 13 oct) |
| 41 / 42 | BME280 en Wire1 (SDA / SCL), 0x76 |
| 47 / 39 | GPS NEO-6M en UART1 9600 bd: RX desde su TX / TX a su RX (UBX: dormir con PMREQ, despertar con 0xFF) |
- Libres: 3 (único con ADC y RTC: reserva), 33, 34, 40. **No usar:** 0, 1, 8–14, 17/18/21 (OLED), 26–32, 35, 36, 37, 43/44, 45/46.
- Módulos a **3V3 de J3** (pines 2/3; GND en el 1). `Ve` es Vext (OLED). `5V` solo con USB.
- Esquema (`docs/gen_esquema.py`) y placa perforada (`docs/gen_placa.py`) → PDF: skill **`esquema`**.
- **PCB en curso (15 oct)**: KiCad 10, `hardware/` (5 placas JLCPCB, SMD montado; ver NOTAS: "PCB"). Esquema
  completo; placa de 90 × 60 mm colocada y rutada (DRC limpio salvo serigrafía). Serigrafía hecha y archivos de pedido en `hardware/fabricacion/` (fabricar.py). Siguiente: pedido y rotaciones en la vista previa de JLCPCB. Skill **`pcb`** (scripts en `hardware/herramientas/`).

## Pendiente (detalle en NOTAS: "Pendiente")
1. Parques POTA cercanos hechos (14 oct; lista de España en LittleFS): falta probarlos con fijo del GPS en la calle.
   Del GPS falta medir el GPS dormido en µA (capturas nuevas hechas el 14 oct).
2. QSO LIBRE en los dos sentidos (las dos placas tienen palas); pila del motor CW tras Duo; cara de tormenta sin ver
   (la de auriculares, vista el 13 oct).
3. OLED negra a veces en la placa 2 (protoboard; era la "placa 1" hasta el 15 oct): **defecto de hardware de esa
   Heltec** (ver NOTAS). Aparcado: detección de USB (probada el 14 oct con 47k/68k → GPIO33:
   funciona, pero sin USB el pin 5V da 1,3 V y quedan 0,7 V, poco margen; si se retoma, GPIO3 con ADC).
4. Reinicio de la OTA si la descarga no avanza en 30 s: compila, sin probar (la OTA de la v1.10 fue limpia y no saltó; forzarlo cortando la WiFi a mitad).
5. Desde la 1.11: quitar `heltec_sin_encoder` de `platformio.ini`, `compilar.sh` y `publicar.sh` (¿y el código
   `#ifdef SIN_ENCODER`? preguntar).
6. Ahorro de tokens: `/clear` entre tareas y fotos recortadas (extensiones de VS Code revisadas el 13 oct 2026:
   no hace falta ninguna más; ver NOTAS, "Cómo trabajar").
7. Batería de la placa 1 (perforada): marca 90 % con la carga terminada (15 oct). Comprobar con polímetro si el
   factor 4,9 (calibrado en la otra Heltec) vale para esta; si no, factor por placa según la MAC (ver NOTAS).
8. Encoder de la placa 1 con pasos en falso a rachas (15 oct): contacto intermitente en DT (a veces CLK); GPS y
   decodificador descartados por traza. Revisar soldaduras/continuidad de P1, J2-14/18 y la masa, o probar otro KY-040.

## Decisiones que no hay que deshacer
- **SOTA: no programar el cliente** (sus condiciones prohíben software generado por IA sin aprobación previa).
- LoRa con tramas digitales (no audio); ESP-NOW descartado aquí (idea para "Chispas Lite", ver `ROADMAP.md`).
- Sin expansor MCP23017: GPIO directos.
- No cambiar los nombres de los espacios de NVS sin migrar (se pierden los datos de las placas).

## Cómo trabajar
- Responder en español, directo y sin relleno. Separar lo probado en placa de lo razonado.
- Compilar antes de dar un cambio por bueno, con **`./compilar.sh`** (los dos entornos; `enc` o `sin` para uno):
  solo saca errores, avisos de `src/`, RAM, flash y OK/FALLO; la salida completa queda en `.pio/compilar.log`. Cambios con diff claro; no reestructurar sin avisar.
- Antes de afirmar algo sobre RadioLib, WiFiManager o el core de Espressif, comprobar la versión instalada.
- Leer el serie sin reiniciar la placa: **`serie.py`** (busca el CP210x, DTR/RTS a False, hora y log en
  `.pio/serie-*.log`; `-f REGEX` filtra, `-s N` segundos, `-e TEXTO` envía órdenes serie).
- Permisos de Claude Code compartidos en `.claude/settings.json` (viaja por Git): compilar, serie, git de lectura
  sin preguntar; también commit y push (los hace Claude cuando le parezca, sin preguntar; 14 oct). `publicar.sh`,
  `gh release create/edit/delete` y `git push --force` siempre preguntan: **solo las releases se consultan**.
- **Heredocs de Bash:** la herramienta reduce `\\` a `\` (un `\n` acaba como salto de línea real y un `'\0'` como
  byte nulo). Código con barras invertidas: usar Edit o Write.
- **No reescribir el código (`main.cpp`, `parts/*.inc`) con `Get-Content`/`Set-Content` de PowerShell 5.1** (rompe
  las tildes y añade BOM). Desde Bash, `pio` y `python` no están en el PATH:
  `~/.platformio/penv/Scripts/pio.exe` y `~/.platformio/penv/Scripts/python.exe`.
- Skills del proyecto (`.claude/skills/`): **`cerrar-tarea`** al terminar cada tarea (compilar, CLAUDE.md
  resumen + NOTAS detalle, commit, push); **`placa`** para cargar y probar en placa (elige el entorno según la
  placa); **`release`**; **`esquema`**; **`pcb`** (diseño de la PCB en KiCad).
