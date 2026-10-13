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
  fuera de OneDrive, sincronizado solo con Git (`git pull` al empezar, `git push` al terminar). La copia antigua de
  OneDrive (`Documentos\mrmorse\Mr.-Morse-EA4ILN`) no se usa ni se borra hasta migrar los tres (ver NOTAS: "Repositorio").
  Migrado: equipo 1. VS Code: Settings Sync con la cuenta GitHub `ea4iln-debug` (activado en el equipo 1).
- Software GPL-3.0-or-later (`LICENSE`); hardware y documentación CC BY-NC-SA 4.0 (`LICENSE-HARDWARE.md`).
  **Archivos de código nuevos: cabecera SPDX.** `include/wm_strings_chispas.h`: `MIT AND GPL-3.0-or-later`.
  Los `.bin` de las releases van con `docs/LICENCIAS_TERCEROS.txt` (regenerarlo si cambian las librerías).

## Versiones, releases y OTA (detalle en NOTAS: "Releases y OTA")
- `#define FW_VERSION` y `@version` en la cabecera de `main.cpp`. Formato `X.YYb` (b = beta; sin letra, final).
  **Preguntar siempre al usuario antes de subir la versión y antes de publicar una release** (llegan por OTA).
  Procedimiento: skill **`release`** (`./publicar.sh` en tres pasos, borrador antes de publicar).
- OTA: CONF > CONFIG > ACTUALIZAR FW; TLS con `include/certificados_github.h`.
- Depuración desde CONF > CONFIG > DEPURACION (órdenes serie, `[DIAG]`, `[DECOD]`, `[CW]`); apagada por defecto.

## Estado actual (13 oct 2026)
- **v1.06b** (última release, 12 oct) en las dos placas. Placa 1: protoboard con encoder, palas, KEY, amplificador,
  BME280 y batería; entorno `heltec_wifi_lora_32_V3`. Placa 2: desnuda, sin encoder; entorno `heltec_sin_encoder`
  (`-DSIN_ENCODER`), se actualiza por OTA.
- PlatformIO, `platform = espressif32 @ 6.13.0` (core Arduino 2.0.17), RadioLib ^6.6, Adafruit GFX/SSD1306,
  WiFiManager 2.0.17, ArduinoJson 7. **Un solo `.cpp` en `src/`** (el resto en `src/parts/*.inc`, ver "Código");
  `pio run` compila solo el firmware
  (`default_envs`); pruebas aparte en `pruebas/` (`prueba_pines`, `prueba_bateria`). Puerto COM según el equipo
  (driver CP210x).
- Pruebas con LED, pantalla, monitor serie y audio (altavoz y auriculares).

## Arquitectura (detalle en NOTAS: "Funciones" y "Manejo")
- Pantallas `PANTALLA_*`: 1 Inicio, 2 CW, 3 LoRa, 4 TRX, 5 PROP, 6 POTA, 7 Sol, 8 CONF. Cabecera: hora, nombre corto,
  batería (`PWR` sin batería).
- **Estilo común (v0.9h):** menús con título en negativo (`dibujarTituloMenu()`) y opciones `< ... >`; ajustes en
  listas (`dibujarListaOpciones()`); datos en páginas (`dibujarPiePaginas()`); pantallas de manipular/escuchar con
  pie corto y "menu". Inicio es una portada fija. **Mantener este estilo (fuente 5×7, sin imitar Meshtastic).**
- Entrada: encoder KY-040 por interrupción (`isrEncoder`), eventos `BOTON_*`/`ENC_*` en `procesarBoton()`.
  PRG corto = atrás, largo = dormir. Sin encoder: PRG corta = mover, larga = ejecutar, muy larga = dormir.
- Tareas FreeRTOS: `motorCW` (núcleo 1, prio 3; API `motorCWEmitir/Detener/Ocupado/Portadora`; solo las tareas
  llaman a `conmutarEmisionMorse()`), `keyer` (palas cada 1 ms), `decod` (núcleo 0, ADC por DMA a 8 kHz, Goertzel).
- NVS: `chispas` (entrenador, keyer, Echo, estadísticas, ajustes), `estacion` (indicativo, locator, UTC, QNH,
  memorias); WiFi en el NVS del ESP32. Borrar estadísticas solo borra estadísticas; el reset de fábrica, todo.
- Consumo: CPU a 80 MHz sin WiFi y 240 con WiFi; LoRa dormida fuera de Duo/QSO. **Al despertar el SX1262 hay que
  llamar a `radio.standby(RADIOLIB_SX126X_STANDBY_RC, true)` antes de `startReceive()`** (si no, no recibe).
- WiFi solo en PROP y POTA (y SOL si falta la hora); el portal (`Chispas-XXXX`, clave `chispas73`) no se abre solo.
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
| `wifi` `ota` `datos_web` | WiFi, NTP, portal y QR; OTA; hamqsl, POTA, `procesarWifi` |
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
| 19 / 20 / 48 | Encoder CLK / DT / SW |
| 41 / 42 | BME280 en Wire1 (SDA / SCL), 0x76 |
| 47 | RX desde el TX del GPS |
| 39 (a probar) | TX al GPS (dormirlo por UBX) |
| 3 (pendiente) | Enable del GPS con AO3401A (falta el transistor) |
- Libres: 40, 33, 34. **No usar:** 0, 1, 8–14, 17/18/21 (OLED), 26–32, 35, 36, 37, 43/44, 45/46.
- Módulos a **3V3 de J3** (pines 2/3; GND en el 1). `Ve` es Vext (OLED). `5V` solo con USB.
- Esquema (`docs/gen_esquema.py`) y placa perforada (`docs/gen_placa.py`) → PDF: skill **`esquema`**.
- PCB: se empezará desde cero más adelante; no hay nada de KiCad.

## Pendiente (detalle en NOTAS: "Pendiente")
1. GPS (llega el 13 oct): montarlo sin enable, leer NMEA por GPIO47; probar dormirlo por UBX (GPIO39).
2. Placa perforada: antes de soldar, confirmar la orientación de la Heltec (`J3_IZQUIERDA`) y el orden de pines del
   PAM8302A, el GPS y el BME280.
3. QSO LIBRE en los dos sentidos (cuando la placa 2 tenga palas); pila del motor CW tras Duo; gestos de la cara de
   auriculares y tormenta sin ver.
4. Aparcado: OLED negra a veces en la placa 1 (ver NOTAS); detección de USB y carga (divisor 5V → GPIO33).
5. Ahorro de tokens: `/clear` entre tareas y fotos recortadas (extensiones de VS Code revisadas el 13 oct 2026:
   no hace falta ninguna más; ver NOTAS, "Cómo trabajar").

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
  sin preguntar; `publicar.sh`, `gh release create/edit/delete` y `git push` siempre preguntan.
- **Heredocs de Bash:** la herramienta reduce `\\` a `\` (un `\n` acaba como salto de línea real y un `'\0'` como
  byte nulo). Código con barras invertidas: usar Edit o Write.
- **No reescribir el código (`main.cpp`, `parts/*.inc`) con `Get-Content`/`Set-Content` de PowerShell 5.1** (rompe
  las tildes y añade BOM). Desde Bash, `pio` y `python` no están en el PATH:
  `~/.platformio/penv/Scripts/pio.exe` y `~/.platformio/penv/Scripts/python.exe`.
- Skills del proyecto (`.claude/skills/`): **`cerrar-tarea`** al terminar cada tarea (compilar, CLAUDE.md
  resumen + NOTAS detalle, commit, push); **`placa`** para cargar y probar en placa (elige el entorno según la
  placa); **`release`**; **`esquema`**.
