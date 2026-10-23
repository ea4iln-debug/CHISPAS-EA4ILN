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
  **Única pregunta al usuario: si se publica versión** (una vez, antes de tocar `FW_VERSION`; las releases llegan por
  OTA). Con el sí, todo seguido: skill **`release`** (`./publicar.sh`, borrador y publicar, web).
- OTA: CONF > CONFIG > ACTUALIZAR FW; TLS con `include/certificados_github.h`.
- **Web de instalación por USB** (16 oct): https://ea4iln-debug.github.io/CHISPAS-EA4ILN/ (`web/`, ESP Web Tools; probada en la placa 1);
  la Action `web.yml` la rehace sola al publicar una release (parte el `-usb.bin` para conservar la NVS). Selector
  **Estable / Beta** (la beta solo si es posterior a la última final) y "Novedades de la beta": texto fijo en
  `web/index.html`, **rehacerlo con cada beta**. Portada de presentación (16 oct): qué hace, Constrúyelo (caja en 3D,
  concepto) e Instálalo; `web/funciones.html` explica cada función. En la web, **CHISPAS en mayúsculas y en masculino**. **Página de hardware** (`web/hardware.html`, 16 oct): fases, piezas,
  pines y PCB (sin Gerber hasta probarla); imágenes de la PCB con `hardware/herramientas/imagenes_web.sh` (rehacerlas si cambia).
  **Manual de usuario** (18 oct): `web/manual.html` y `docs/manual_chispas.pdf` (**regenerar el PDF si cambia**, skill `manual`;
  cómo, en NOTAS); mascota: el robot CHISPAS; la entrada de audio se llama **SND IN (DECOD)** (23 oct; en web, manual y caja).
- Depuración desde CONF > CONFIG > DEPURACION (órdenes serie, `m<texto>` simula las palas, `[DIAG]`, `[DECOD]`, `[CW]`); apagada por defecto.

## Estado actual (18 oct 2026)
- **v1.34b** publicada (beta, 21 oct): velocidad EFECTIVA y filtro de pulsadores (ver abajo); en la placa 1 por OTA (probado). Antes, **v1.33b** (beta, 18 oct): pantalla TIEMPO (ver abajo); en la placa 1 por USB. Antes, **v1.32b** (beta, 18 oct): AUTOSPOT POTA con activación en curso (ver abajo); en la placa 1 por OTA desde
  la 1.20 (probado). Antes, **v1.31b** (beta, 18 oct; los
  arreglos del 18 oct de abajo). Antes, **v1.30b** (beta, 16 oct; ver abajo). **v1.20** (release del 16 oct, final): sin la versión sin encoder (entorno, binario y código `#ifdef SIN_ENCODER`)
  y batería a 4,985. En la placa 1 por USB y en la placa 2 por OTA (16 oct; antes, reinicio por OTA sin avance probado).
  Antes, v1.10 (15 oct, primera final; WIFI DATOS MANUAL/AUTO y reinicio de la OTA sin avance). **Nombres de las placas desde el 15 oct** (antes, al revés; lo anterior en NOTAS usa los viejos):
  **placa 1 = placa perforada** (MAC `08:c0`, montada y probada el 15 oct) y **placa 2 = Heltec nueva desnuda**
  (MAC `44:1b:f6:fa:ab:fc`, desde el 23 oct; para montar en la PCB): **v1.34b de la release** (no `main`) por USB,
  ORDENES SERIE activadas en la NVS (se maneja toda por serie) y con batería; sin encoder ni periféricos. La antigua placa 2 (protoboard,
  MAC `50:74`, OLED averiada) se devuelve con el firmware de fábrica de Heltec. Un solo entorno, `heltec_wifi_lora_32_V3`.
- **Sin soporte sin encoder** desde la 1.20: un solo entorno (`heltec_wifi_lora_32_V3`); la v1.10 fue la última con
  `heltec_sin_encoder`. **Versiones X.YY con dos cifras** (la OTA compara números: "1.2" sería menor que "1.10").
- PlatformIO, `platform = espressif32 @ 6.13.0` (core Arduino 2.0.17), RadioLib ^6.6, Adafruit GFX/SSD1306,
  WiFiManager 2.0.17, ArduinoJson 7. **Un solo `.cpp` en `src/`** (el resto en `src/parts/*.inc`, ver "Código");
  `pio run` compila solo el firmware
  (`default_envs`); pruebas aparte en `pruebas/` (`prueba_pines`, `prueba_bateria`, `prueba_gps`). Puerto COM según el equipo
  (driver CP210x).
- Pruebas con LED, pantalla, monitor serie y audio (altavoz y auriculares).
- Placa perforada (placa 1) montada completa el 15 oct, con el jack de auriculares: todo bien, circuito correcto.
  **Su P1 (encoder) va CLK, SW, DT** (cable con DT y SW cruzados); el plano, desde el 15 oct, CLK, DT, SW con cable recto.

## Arquitectura (detalle en NOTAS: "Funciones" y "Manejo")
- Pantallas `PANTALLA_*`: 1 Inicio, 2 CW, 3 LoRa, 4 TRX, 5 PROP, 6 POTA, 7 Sol, 8 TIEMPO (cabecera `WX`), 9 CONF. Cabecera: hora, nombre corto,
  batería (`PWR` sin batería).
- **Estilo común (v0.9h):** menús con título en negativo (`dibujarTituloMenu()`) y opciones `< ... >`; ajustes en
  listas (`dibujarListaOpciones()`); datos en páginas (`dibujarPiePaginas()`); pantallas de manipular/escuchar con
  pie corto y "menu". Inicio es una portada fija. **Mantener este estilo (fuente 5×7, sin imitar Meshtastic).**
- Entrada: encoder KY-040 por interrupción (`isrEncoder`), eventos `BOTON_*`/`ENC_*` en `procesarBoton()`.
  PRG corto = atrás, largo = dormir; despiertan PRG y el eje del encoder.
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
- Batería: GPIO37 **HIGH** activa el divisor (Meshtastic dice LOW), 2,5 dB, factor 4,985 para todas (calibrado en la placa 1 el 15 oct; antes 4,9); "sin batería"
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
| `pant_inicio_cw` `pant_lora_trx` `autospot` `pant_prop_pota` | Dibujo de Inicio y CW; de LoRa y TRX; autospot POTA y aviso de renovarlo; menús y páginas de PROP y POTA |
| `sol` `tiempo` | SOL y línea gris; TIEMPO (Open-Meteo) y AMBIENTE (`procesarBME`) |
| `conf` `estados_cw` | CONF, STATUS, OTA y `actualizarPantalla()`; máquina de estados CW |
| `cara` `entrada` `eventos` | Salvapantallas; encoder, ISR y eventos de botón; `procesarBoton()` |
- Archivo nuevo en `src/parts/`: extensión `.inc`, cabecera SPDX, y su `#include` en `main.cpp` detrás de lo que use.

## Hardware (detalle en NOTAS: "Prototipo en protoboard" y "Placa perforada")
| GPIO | Uso |
|---|---|
| 2 | KEY (PC817 330 Ω, 10 kΩ a GND, J4 con puente R-T) |
| 4 / 5 | Pala DIT / DAH (pull-up interno) |
| 6 | Entrada del decodificador (ADC1) |
| 7 | Tono sigma-delta → R1, RV1 → PAM8302A + altavoz (TN del jack) y auriculares |
| 38 | SD del PAM8302A (BAJO = apagado) |
| 19 / 20 / 48 | Encoder CLK / SW / DT (SW en pin RTC: despierta del deep sleep; cambiado el 13 oct) |
| 41 / 42 | BME280 en Wire1 (SDA / SCL), 0x76 |
| 47 / 39 | GPS NEO-6M en UART1 9600 bd: RX desde su TX / TX a su RX (UBX: dormir con PMREQ, despertar con 0xFF) |
- Libres: 3 (único con ADC y RTC: reserva), 33, 34, 40. **No usar:** 0, 1, 8–14, 17/18/21 (OLED), 26–32, 35, 36, 37, 43/44, 45/46.
- Módulos a **3V3 de J3** (pines 2/3; GND en el 1). `Ve` es Vext (OLED). `5V` solo con USB.
- Esquema (`docs/gen_esquema.py`) y placa perforada (`docs/gen_placa.py`) → PDF: skill **`esquema`**.
- **PCB en curso (15 oct)**: KiCad 10, `hardware/` (5 placas JLCPCB, el mínimo; SMD montado; ver NOTAS: "PCB"). Esquema
  completo; placa de 90 × 60 mm colocada y rutada (DRC limpio salvo serigrafía). Serigrafía hecha y archivos de pedido en `hardware/fabricacion/` (fabricar.py). Pedidos a JLCPCB (5 placas) y LCSC hechos y combinados (15 oct); R7 (volumen) es el ALPS C97444 en esquema y BOM (16 oct, decisión del usuario); para estas 5 placas se compró el CTR C18213426 porque no había stock (lineal, encaje sin comprobar). Siguiente: recibir y montar. Skill **`pcb`** (scripts en `hardware/herramientas/`).
  **Modelo 3D para la caja** (20 oct): `modelo3d.py` → `hardware/3d/chispas.step` (Heltec en sus zócalos; dudas de
  medidas en NOTAS: "PCB"). **Caja** (21 oct, sin imprimir): `caja.py` (CadQuery en `~/.venv-cad`) → base y tapa en
  STEP/STL en `hardware/3d/`; decisiones y lo que falta, en NOTAS: "Caja".

- **v1.30b (16 oct, release beta; en la placa 1 por USB):** audio sintetizado por sigma-delta (sin PWM; GPIO7,
  ISR a 16 kHz con el amplificador encendido; pulso casi imperceptible al entrar y salir: se deja); **CW > QSO SIM**
  (`qso_sim.inc`, tercera opción del menú; `tx libre` pasa al pie) con QSB, RUIDO, TORMENTA y QRM; Koch y Duo con
  ajuste TEXTO (grupos, indicativos, QSO POTA; Duo sin probar con dos placas); TX LIBRE > menu > RITMO; batería al
  100 % desde 4,15 V. Detalle en NOTAS: "Audio sintetizado". Auricular izquierdo de la placa 1: T y TN cruzados
  en el jack (arreglado, 16 oct).
- **Después de la 1.30b (18 oct; van en la v1.31b):** decodificador probado con W1AW (20 WPM
  perfecto; la síntesis no le afecta); fin de palabra arreglado con Farnsworth (10 WPM de W1AW salía letra a letra);
  hora de la cabecera que parpadeaba al decodificar (`getLocalTime(&t, 0)` del core). **Decodificador con ruido**
  (18 oct, sin cambios): bien hasta CONTEST MEDIUM de la RSGB, pocos errores en HARD, ilegible en INTENSE; el usuario
  lo da por bueno. Detalle en NOTAS: "DECODIFICAR".
  **QSO SIM** (18 oct): QSO completo y caminos de error en la placa 1 sin palas, con la orden serie nueva
  `m<texto>` (entra como lo manipulado; `serie.py -e "mEA4ILN"`); audio realista. **Ajuste ROL** (18 oct):
  CAZADOR / ACTIVADOR; con ACTIVADOR llamas tú CQ y CHISPAS es el cazador (indicativo parcial, rellamada a los 8 s,
  copia mal tu indicativo, AGN?); probado en la placa 1 con `m`. Detalle en NOTAS: "QSO SIM".
- **AUTOSPOT POTA** (18 oct, **v1.32b**; `autospot.inc`): VER PARQUES > girar marca un parque > pulsar > spot abre la
  ficha (PARQUE, ID editable para /P o radioclub, BANDA, FREC KHZ, COMENTARIO NO/QRP/QRT/TEXTO, TEXTO propio) > enviar (POST sin cuenta a
  `api.pota.app/spot`, API no documentada, la de Ham2K PoLo). Aviso con pitido a los 25 min para renovarlo.
  **Activación en curso** (NVS, sobrevive a apagar): el menú POTA muestra el parque, Inicio dice ACTIVANDO, ficha con
  duración y último spot, botones spot/qrt/volver y CERRAR sin QRT. **Probado en la placa 1 con spots reales** (spot,
  reinicio, qrt; sin conexión también); la web oculta el QRT en minutos. Sin probar: el aviso de los 25 min (lo
  probará el usuario en una activación). Manual, web y README al día. CAT: al ROADMAP. Detalle en NOTAS: "Parques".
- **TIEMPO** (18 oct, **v1.33b**; `tiempo.inc`): pantalla 8 tras SOL (cabecera `WX`), datos de **Open-Meteo**
  (sin clave, por el locator/GPS, ~3 KB; AEMET descartado de momento: clave por usuario y por municipio). AHORA,
  HORAS (cada 3 h hasta 12 h), MAÑANA y AMBIENTE (sale de SOL, que queda con 3 páginas). Probado en la placa 1
  (MANUAL); manual, PDF, web y README al día. Detalle en NOTAS: "Funciones".
- **Velocidad EFECTIVA** (21 oct, **v1.34b**): sustituye al ESPACIADO % en Koch, Duo, Echo y QSO SIM; WPM como
  LCWO (fórmula de su jscwlib, `huecosCW()`), de 5 al wpm (`NO` = sin estirar); cabecera `25/10WPM`; NVS `ef`/`eef`/`sef`.
  Medido en la placa 1 por serie y oído igual que LCWO (usuario, 21 oct); manual, PDF, web y README al día. Detalle en NOTAS: "Funciones".
- **Cuenta atrás del Koch** (21 oct, sin publicar): 3, 2, 1 en grande antes del primer grupo (`CW_CUENTA_ATRAS`);
  probada en la placa 1 por serie; manual y PDF al día.

## Pendiente (detalle en NOTAS: "Pendiente")
0. **CLUSTER** (23 oct, en diseño, sin código): pantalla 10 por telnet, RBN por defecto y URE (`ea4ure.com:7300`);
   nodos medidos, decisiones y fases en NOTAS: "CLUSTER".
1. PCB: recibir y montar (ver "Hardware").
2. Probar con las palas TX LIBRE > RITMO, y la práctica QSO POTA del Koch.
3. **Siguiente:** Duo TEXTO (dos placas) y QSO SIM (los dos roles) con las palas de verdad (sin palas, probado); instalar la beta desde la web (sin probar).
4. TIEMPO: probar WIFI DATOS AUTO (refresco cada 30 min).
5. AUTOSPOT: probar el aviso de los 25 min (y su captura `spot_aviso.png` para el manual) y CERRAR sin QRT en una activación real.
6. Caja (`caja.py`): faltan medidas de batería, GPS y pulsadores PRG/RST (y cuál es cuál), decidir interruptor, y
   probar la ventana impresa sobre la Heltec real (la pantalla del modelo no casa con la zona visible medida).
Pulsaciones fantasma del encoder al decodificar (19 oct): diafonía del cable de audio del transceptor con los del
encoder. Volvieron el 20 oct: filtro de ~1 ms en `leerPulsador()` (PRG y eje), probado en la placa 1, va en la
próxima versión. Si salen giros fantasma o sigue, filtro de CLK/DT o condensadores (NOTAS: "Manejo").
**21 oct:** siguen en el SW, solo en TRX y sin DECOD (solo KEY). Sospecha: RF de la propia emisión rectificada en
GPIO20. **Se prueba con la PCB nueva** (plan en NOTAS: "Manejo").

Cerrado el 16 oct (decisión del usuario; detalle en NOTAS): GPS dormido y cara de tormenta (consumo despreciable,
no se miden ni se esperan), OLED negra de la placa 2 (placa averiada), batería de la placa 2 (se reajusta si se
desvía mucho), ahorro de tokens (hábito; Claude avisa de tareas caras y de cuándo hacer `/clear`) y versión sin
encoder (quitada en la 1.20). Reinicio de la OTA sin avance: probado el 16 oct (ver NOTAS).

## Decisiones que no hay que deshacer
- **SOTA: no programar el cliente** (sus condiciones prohíben software generado por IA sin aprobación previa).
- LoRa con tramas digitales (no audio); ESP-NOW descartado aquí (idea para "Chispas Lite", ver `ROADMAP.md`).
- Sin expansor MCP23017: GPIO directos.
- No cambiar los nombres de los espacios de NVS sin migrar (se pierden los datos de las placas).

## Cómo trabajar
- Responder en español, directo y sin relleno. Separar lo probado en placa de lo razonado.
- **Solo se pregunta si se publica una versión** (18 oct). Las decisiones de diseño las toma Claude (la opción
  razonable, coherente con el estilo y lo ya decidido) y las cuenta en el resumen para que el usuario las corrija.
  Lo que solo puede hacer o ver el usuario (conectar la placa, oír el audio, palas de verdad) se le pide en texto,
  todo junto, después de probar lo posible por serie (`m<texto>`, capturas).
- Compilar antes de dar un cambio por bueno, con **`./compilar.sh`**:
  solo saca errores, avisos de `src/`, RAM, flash y OK/FALLO; la salida completa queda en `.pio/compilar.log`. Cambios con diff claro; no reestructurar sin avisar.
- Antes de afirmar algo sobre RadioLib, WiFiManager o el core de Espressif, comprobar la versión instalada.
- Leer el serie sin reiniciar la placa: **`serie.py`** (busca el CP210x, DTR/RTS a False, hora y log en
  `.pio/serie-*.log`; `-f REGEX` filtra, `-s N` segundos, `-e TEXTO` envía órdenes serie).
- Permisos de Claude Code compartidos en `.claude/settings.json` (viaja por Git): compilar, serie, git, commit, push
  y `publicar.sh` sin confirmación (la pregunta de publicar va antes, con AskUserQuestion). Solo piden confirmación
  `gh release create/edit/delete` sueltos y `git push --force`.
- **Bash ya arranca en la raíz del proyecto: no poner `cd /c/proyectos/CHISPAS-EA4ILN &&` delante** (convierte la
  orden en compuesta, no casa con los permisos y obliga a confirmar). Para otra carpeta, rutas absolutas.
- **Editar archivos con Edit o Write**, no con `sed -i` ni scripts de Python por heredoc (con las barras invertidas,
  las comillas y las tildes se han roto varias veces). La herramienta reduce `\\` a `\` en los heredocs (un `\n`
  acaba como salto de línea real y un `'\0'` como byte nulo). Scripts de ayuda: archivo en el scratchpad con Write.
- **No reescribir el código (`main.cpp`, `parts/*.inc`) con `Get-Content`/`Set-Content` de PowerShell 5.1** (rompe
  las tildes y añade BOM). Desde Bash, `pio` y `python` no están en el PATH:
  `~/.platformio/penv/Scripts/pio.exe` y `~/.platformio/penv/Scripts/python.exe`.
- Skills del proyecto (`.claude/skills/`): **`cerrar-tarea`** al terminar cada tarea (compilar, CLAUDE.md
  resumen + NOTAS detalle, commit, push); **`placa`** para cargar y probar en placa (elige el entorno según la
  placa); **`release`**; **`esquema`**; **`pcb`** (diseño de la PCB en KiCad); **`manual`** (manual de usuario y
  su PDF con `./web/manual.sh`).
