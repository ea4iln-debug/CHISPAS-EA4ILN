# Chispas EA4ILN — notas detalladas

> Referencia completa: historial de pruebas, medidas, decisiones y detalles de cada función. El resumen que se
> carga en cada sesión está en `CLAUDE.md`; leer aquí solo la sección del tema que se vaya a tocar.
> Al cambiar algo, actualizar la sección correspondiente de este archivo (y `CLAUDE.md` si afecta al resumen).

## Qué es
**Chispas** ("chispas" era el apodo del radiotelegrafista de a bordo): dispositivo portátil de CW
para radioaficionados (indicativo EA4ILN) sobre una **Heltec WiFi LoRa 32 V3** (ESP32-S3FN8, SX1262 a 868 MHz,
OLED 0,96", sin PSRAM, 8 MB de flash). Entrenador Koch estilo LCWO, Echo, keyer iámbico, práctica y QSO por LoRa,
conexión con el transceptor (KEY, tune, memorias, decodificador), propagación, spots POTA, sol y línea gris,
configuración en el aparato y actualización OTA desde las releases de GitHub. Pendiente: amplificador del tono y GPS (ver `ROADMAP.md`).

## Repositorio
- GitHub: `https://github.com/ea4iln-debug/CHISPAS-EA4ILN` (GitHub redirige la URL anterior del repositorio).
  `README.md` para GitHub.
- **Licencias (decididas 10 oct 2026):** software (firmware, pruebas, `include/`, scripts) con **GPL-3.0-or-later**
  (`LICENSE`, texto oficial de gnu.org; cabecera SPDX en cada archivo de código); hardware y documentación (esquema,
  lista de componentes, diseño, README) con **CC BY-NC-SA 4.0** (`LICENSE-HARDWARE.md`). `include/wm_strings_chispas.h`
  conserva el aviso MIT de WiFiManager (SPDX `MIT AND GPL-3.0-or-later`). Archivos nuevos: añadir la cabecera SPDX.
  Los `.bin` de las releases van con `docs/LICENCIAS_TERCEROS.txt` (avisos de las librerías; regenerarlo si cambian).
- **Carpeta de trabajo (13 oct 2026):** el repositorio estaba en OneDrive (`Documentos\mrmorse\Mr.-Morse-EA4ILN`), con
  lo que se sincronizaba dos veces (OneDrive y Git) y OneDrive copiaba también `.git` y `.pio`: riesgo de corromper
  `.git` y cientos de MB inútiles. Ahora, en cada uno de los tres equipos, clon en `C:\proyectos\CHISPAS-EA4ILN` y
  sincronización solo con Git. Lo que no viaja y no debe viajar: `.pio/` (lo regenera PlatformIO), `.vscode/` generado,
  el puerto COM, la memoria y los permisos locales de Claude Code. Ajustes y extensiones de VS Code: Settings Sync
  (activado en el equipo 1 el 13 oct 2026, cuenta GitHub `ea4iln-debug`: Settings, Keyboard Shortcuts, Snippets,
  Extensions y Profiles, sin UI State). En los otros equipos, activarlo con la misma cuenta y elegir "Merge". No hace
  falta `.code-workspace`: Git lleva `platformio.ini` y `.vscode/extensions.json`. Ajustes propios de un equipo (rutas,
  puerto) en `platformio.ini` o en los ajustes de la carpeta, no en los de usuario (Settings Sync los copiaría).
  - Migrado: equipo 1 (13 oct 2026; clon compilado, mismos RAM y flash).
  - **Pasos en cada equipo que falte:** 1) en la carpeta de OneDrive, `git status` y `git log origin/main..` para ver
    si hay algo sin subir; si lo hay, commit y `git push`. 2) `git clone https://github.com/ea4iln-debug/CHISPAS-EA4ILN.git
    C:\proyectos\CHISPAS-EA4ILN`. 3) Abrir esa carpeta en VS Code (PlatformIO descarga las librerías) y compilar con
    `./compilar.sh`. 4) Trabajar ya solo ahí; anotar aquí el equipo migrado.
  - Cuando estén los tres: borrar la carpeta de OneDrive (desde un equipo basta; OneDrive la borra en todos).

## Releases y OTA
- **Versión:** `#define FW_VERSION` (y `@version` de la cabecera de `main.cpp`). Formato `X.YYb` (b = beta; sin letra,
  final): `compararVersiones()` ordena 1.00b < 1.01b < 1.01. **Preguntar siempre al usuario antes de subirla** y
  antes de publicar una release (las releases llegan a las placas por OTA).
- **Primera release: v1.00b (beta), 11 oct 2026**, publicada como prerelease: https://github.com/ea4iln-debug/CHISPAS-EA4ILN/releases/tag/v1.00b
  (con `gh`, instalado en el segundo equipo: `C:\Program Files\GitHub CLI\gh.exe`, sesión de ea4iln-debug).
  **v1.01b (beta)**, 11 oct 2026, también prerelease; en el primer equipo no hay `gh`: se creó con la API REST y la
  credencial de git (script temporal, fuera del repositorio). **v1.02b (beta)**, 11 oct 2026, prerelease (con `gh`
  desde el segundo equipo): refuerzo de letras con errores y decodificador comprobado con audio real. **v1.03b (beta)**,
  12 oct 2026, prerelease (API REST desde el primer equipo): sensor BME280. Capturas del README rehechas ese día con
  `nav.py` (script temporal: órdenes serie, capturas y escritura en el editor de rueda), locator IN80dk durante las
  capturas y restaurado después. Etiqueta
  de git y de GitHub: `v` + `FW_VERSION`. Para probar el OTA sin publicar otra versión: compilar temporalmente con
  `FW_VERSION "0.99"`, cargar, y ACTUALIZAR FW instala la última release.
- **Binarios de cada release** (nombres exactos; el OTA busca el suyo por nombre):
  `chispas-<ver>.bin` (app, placa con encoder), `chispas-<ver>-sin-encoder.bin` (app, sin encoder) y, para grabar por
  USB en `0x0`, `chispas-<ver>-usb.bin` y `chispas-<ver>-sin-encoder-usb.bin` (imagen completa: `esptool.py
  --chip esp32s3 merge_bin -o ... --flash_mode dio --flash_size 8MB 0x0 bootloader.bin 0x8000 partitions.bin
  0xe000 boot_app0.bin 0x10000 firmware.bin`; `boot_app0.bin` está en
  `framework-arduinoespressif32/tools/partitions/`). Más `docs/LICENCIAS_TERCEROS.txt`.
- **Antes de publicar:** compilar los dos entornos, probar en placa y pedir al usuario
  que confirme. Publicar con `gh release create v<ver> ... --prerelease` (beta) y las notas en castellano.
- **`publicar.sh` (13 oct 2026)** automatiza lo anterior en tres pasos. Sin argumentos: comprueba que no hay cambios
  sin commit, que HEAD es `origin/main`, que no existe la etiqueta y que `@version` coincide con `FW_VERSION`;
  compila con `./compilar.sh`, copia los `firmware.bin`, une los `-usb` con el `esptool.py` de PlatformIO (v4.11) y
  `boot_app0.bin` del framework, copia `LICENCIAS_TERCEROS.txt`, guarda el commit en `dist/<ver>/commit` y escribe
  `notas.md` con los commits desde la última etiqueta (para reescribirlas en castellano). `--subir`: comprueba que
  HEAD sigue siendo ese commit y crea la release como borrador (`--draft`, `--target` el commit; prerelease si la
  versión acaba en letra). Un borrador no aparece en la API sin autenticar, así que el OTA no lo ve. `--publicar`:
  `gh release edit --draft=false` (GitHub crea entonces la etiqueta) y `git fetch --tags`. Probado el paso 1 con la
  v1.06b (sin las comprobaciones de git): mismos nombres; `-usb` = firmware + 64 KB. `dist/` está en `.gitignore`.
- **`gh` en el equipo 1:** instalado el 13 oct 2026 con `winget install GitHub.cli` (v2.102.0) y
  sesión iniciada (`gh auth login`, cuenta ea4iln-debug, HTTPS, token en el llavero de Windows). Desde entonces ya no hace falta la vía de la API REST.
- **OTA** (v1.00b; **probada de punta a punta el 11 oct 2026**: una compilación de prueba "0.99" en la placa 1 encontró la release v1.00b, la descargó e instaló, y arrancó con la v1.00b conservando la NVS): CONF > CONFIG >
  ACTUALIZAR FW (`CONF_OTA`, `confEntrarOta()`, `procesarOta()`, `buscarActualizacion()`, `instalarActualizacion()`):
  enciende el WiFi, consulta `api.github.com/repos/.../releases?per_page=1` (incluye prereleases), compara, y con
  "instalar" descarga con `httpUpdate` (sigue la redirección a `release-assets.githubusercontent.com`) y reinicia.
  TLS validado con `include/certificados_github.h` (Sectigo E46, USERTrust ECC/RSA, ISRG X1, DigiCert G2; exportados
  del almacén de Windows; si GitHub cambia de autoridad y ninguna vale, falla sin instalar). No instala con batería
  < 30 % sin USB. Aviso: con el WiFi ya encendido en PROP o POTA, comprueba una vez al día y lo enseña en la línea de
  estado (`FW NUEVO: v1.01b`) y en la lista de CONFIG. Particiones `default_8MB.csv` (app0/app1 de 3,2 MB).
- **Depuración** (desde la v1.01b, en el aparato: CONF > CONFIG > DEPURACION; antes eran `#define DEPURACION_*` y
  había que recompilar): lista `CONF_DEPURACION` con ORDENES SERIE (`p` vuelca la OLED para
  `docs/captura_oled.py`; `d`, `i`, `k`, `l`, `a` simulan el encoder), DIAG (1 MIN) (`[DIAG]`), DECOD (2 S)
  (`[DECOD]`) y RETRASO CW (`[CW ...]`); pulsar cambia SI/NO. Variable `depuracion` (bits `DEPU_*`,
  `depuActiva()`), NVS `depu` en `chispas`, todo apagado por defecto; sobrevive al reinicio (el script de capturas
  reinicia la placa) y el reset de fábrica lo apaga. En CONFIG la opción muestra SI si hay alguna activa. Para rehacer
  las capturas del README: ORDENES SERIE en SI, locator genérico IN80dk (en CONFIG) y el script de capturas; luego
  volver a poner el locator de la placa y apagar la depuración.

## Estado actual (12 oct 2026)
- **MAC de cada placa** (las dos tienen el mismo CP2102; `pio run -t upload` imprime `MAC: ...`; la skill `placa`
  la usa para elegir el entorno): placa 1 (encoder, `heltec_wifi_lora_32_V3`): *por apuntar*; placa 2 (sin
  encoder, `heltec_sin_encoder`): *por apuntar*.
- Firmware: `src/main.cpp` **v1.06b** (opción SALVAPANTALLAS, CONFIG por bloques y expresiones de radioaficionado;
  release del 12 oct 2026). v1.05b (salvapantallas en todas las pantallas salvo CW, LoRa y TRX; brillo tenue en
  todas; release del 12 oct 2026). v1.04b (control del amplificador, SD en GPIO38; release
  del 12 oct 2026, instalada por OTA en la placa 1 y probada). v1.03b (sensor BME280; release del 12 oct 2026). v1.02b (refuerzo de las letras con más errores;
  silenciador y fin de palabra del decodificador; release publicada el 11 oct 2026). El usuario decide cuándo se sube la versión y cuándo se publica;
  v1.01b (depuración desde el menú; release publicada el 11 oct 2026). Historial de cambios:
  `git log`. Placa 1 (protoboard con encoder, palas, KEY y batería; entorno normal; BME280, amplificador y altavoz; v1.06b desde el 12 oct 2026, por USB; ORDENES SERIE en SI) y placa 2 (desnuda, sin encoder; entorno `heltec_sin_encoder`; v1.06b
  desde el 12 oct 2026, por OTA; antes la v1.05b, la v1.04b, la v1.03b, la v1.02b y la v1.01b, también por OTA desde la v1.00b: **OTA del binario sin encoder probada**). El puerto COM depende del equipo (COM16 en el primero, COM23 en el segundo desde
  el 11 oct 2026; `platformio.ini` no lo fija y PlatformIO lo detecta); el CP2102 de la V3 necesita el driver CP210x
  de Silicon Labs. Arduino + PlatformIO, env `heltec_wifi_lora_32_V3`, `platform = espressif32 @ 6.13.0` (core
  Arduino 2.0.17), RadioLib ^6.6, Adafruit GFX y SSD1306, WiFiManager 2.0.17, ArduinoJson 7.
  **Un solo `.cpp` en `src/`** (si hay dos, falla el enlazado); el resto, en `src/parts/*.inc` (ver "Organización
  del código"). `default_envs` hace que `pio run` compile solo el
  firmware; programas de prueba aparte en `pruebas/` con sus entornos (`prueba_pines`, `prueba_bateria`).
- Tono: opción TONO en CONFIG (400–1000 Hz en pasos de 50, NVS `tono`, `frecTonoHz`, por defecto 850; pitido de
  prueba de 0,3 s al editarla).
- Pruebas con LED, pantalla, monitor serie y **audio** (altavoz con amplificador PAM8302A desde el 12 oct 2026, y
  auriculares). Ver "Tono" en Funciones.
- Pantallas (`PANTALLA_*`, recorrido con ant./sig.): 1 Inicio, 2 CW, 3 LoRa (Duo/QSO), 4 TRX (transceptor, v0.9a),
  5 PROP (propagación), 6 POTA, 7 Sol, 8 CONF. Cabecera: hora, nombre corto de la pantalla y batería (`PWR` sin batería).
- NVS: `chispas` (`NVS_ENTRENADOR`: Koch, keyer, Echo y estadísticas), `estacion` (`NVS_ESTACION`: indicativo, locator, hora UTC; hasta la v0.7s se llamaba de otra forma y se empezó de cero);
  WiFi en el NVS del ESP32. Borrar estadísticas solo borra estadísticas; el reset de fábrica lo borra todo. El locator por defecto es genérico (IN80dk, Madrid): cada placa
  debe poner el suyo en webconf.
- Recursos (v0.6m): flash 1,12 MB de 3,19 MB (app0); RAM estática 51,8 KB de 320 KB; app1 (OTA) y spiffs (1,5 MB,
  reservada para el CSV de POTA) sin usar. `[DIAG]` por serie cada minuto (v0.7m): heap libre ~317 KB con el WiFi
  apagado y mínimo ~181 KB tras usar WiFi + HTTPS; pila libre: loop ~4,3 KB (tras HTTPS), motorCW 3,3 KB de 4, keyer
  2,3 KB de 3; NVS 168/630 entradas.
- **Consumo** (medido por USB sin batería, resolución 10 mA): v0.7l: 50 mA sin WiFi, 90 mA con WiFi (y el WiFi se
  quedaba encendido en CONF y SOL). **v0.7m: 30 mA sin WiFi, 50–60 mA con WiFi** (~100 h con 3000 mAh, estimado).
  Cambios: CPU a 80 MHz con el WiFi apagado y 240 MHz con el encendido (`cpuSegunWifi()`), `vTaskDelay(1)` al final de
  `loop()`, LoRa dormida fuera de Duo/QSO (`radio.sleep()`; al entrar, `loraEscuchar()`), WiFi apagado en CONF y SOL
  salvo con el portal abierto o SOL esperando la hora. **Ojo (v0.7q):** con el SX1262 dormido, BUSY está en alto y
  `startReceive()` caduca sin despertarlo: hay que despertarlo con `radio.standby(RADIOLIB_SX126X_STANDBY_RC, true)`
  (baja NSS) antes de `startReceive()`; sin eso la placa que solo escuchaba no recibía. Duo probado entre las dos
  placas en los dos sentidos (v0.7q).
- **Brillo** (v0.7o): la OLED pasa al mínimo (`display.dim`, contraste 0) tras 1 min sin uso (`BRILLO_TENUE_MS`) o
  con la cara dormida, en todas las pantallas (desde la v1.05b; antes no en CW, LoRa ni TRX); vuelve con cualquier giro, pulsación o manipulación con
  las palas (`procesarBrillo()`, la única que toca el brillo). Se probó un contraste bajo legible (0x10, v0.8a) y el
  usuario prefirió volver al 0 (v0.8b).

## Manejo (encoder KY-040; probado)
- **Todo se hace con el encoder:** girar mueve el foco (`moverFocoAnillo()`), pulsar ejecuta, y pulsar sobre un
  valor lo edita girando (`editandoValor`, vídeo inverso, sin dar la vuelta). La larga del eje es solo un atajo.
- **PRG corto = atrás** (sale de la edición, sube un nivel; en las pantallas principales va a Inicio).
  **PRG largo = dormir** (deep sleep; despierta pulsando PRG). Ver "Dormir" en Funciones.
- Al cambiar de pantalla el foco queda en la flecha usada (`entrarEnPantalla(p, haciaAtras)`).
- Barra inferior de los menús: `ant.`/`sig.`. Sin ayudas tipo `- : opc`.
- **Estilo común (v0.9h, el de CONF):** las pantallas principales son un **menú** (título en negativo con
  `dibujarTituloMenu()`, opciones centradas `< ... >` y barra `ant.`/`sig.`). Los ajustes son **listas**
  (`dibujarListaOpciones()`: 4 a la vista, nombre a la izquierda, valor a la derecha, barra de desplazamiento; lo
  que se edita girando sale en vídeo inverso) con las acciones en el pie (`volver`, o `iniciar`/`volver` con
  `dibujarPieDos()`). Los datos en **páginas** se pasan girando y pulsar (o atrás) vuelve; pie
  `1/2 INDICES  >volver` (`dibujarPiePaginas()`). Así están todas las pantallas salvo Inicio (portada, se queda
  igual por decisión del usuario). Las de manipular o escuchar (TX LIBRE, OPERAR, QSO, PRÁCTICA DUO, QSO LIBRE y
  DECODIFICAR) tienen un pie corto y **"menu"**, que abre la lista de sus ajustes con SALIR al final y volver en el
  pie.
- Entradas: encoder por interrupción en CLK/DT con tabla de cuadratura (`isrEncoder`, en IRAM, lee `GPIO_IN_REG`);
  PRG y pulsador del eje con `leerPulsador()` (antirrebote: el eje rebota al soltar). Eventos `BOTON_CORTA/LARGA` y
  `ENC_DERECHA/IZQUIERDA/PULSA/LARGA`, todos tratados en `procesarBoton()`.
- **Sin encoder** (v0.7p; con las pantallas de la v0.9h, probado a mano en la placa 2 el 11 oct 2026): entorno `heltec_sin_encoder` (`-DSIN_ENCODER`, mismo `main.cpp`).
  `leerEventoBoton()` traduce el PRG: corta = `ENC_DERECHA` (mover), larga = `ENC_PULSA` (ejecutar), muy larga
  (≥ `TIEMPO_PULSACION_DORMIR`, 2,5 s) = dormir. No hay "atrás" (todas las pantallas tienen volver o ant./sig.).
  Al editar, los valores dan la vuelta (`ajustarValor`, `ajustarApagado`).
- **Aceleración del giro** (v0.9f): la ISR del encoder mide el tiempo entre pasos (`encIntervaloTicks`); si es
  menor de 40 ms (`ENC_RAPIDO_MS`), `ajustarValor()` avanza ×`aceleracionGiro` en los valores con 20 o más pasos de
  mínimo a máximo (`ENC_MIN_PASOS_ACELERAR`: fallos, wpm, lección; no en rangos cortos como 1–5 ni en el tono). Se
  elige en CONF > CONFIG > ACELERACION (v0.9g): NO, x2–x5 (NVS `acel`, por defecto x5). Solo con encoder.
  **Probada a mano en la placa 1 (11 oct 2026):** despacio de 1 en 1, rápido a saltos y con NO siempre de 1 en 1.
- **CONF** (v0.9g, reestructurada; probado por serie con capturas en la placa 1): menú con título
  " CONFIGURACION ", `< CONFIG >` y `< STATUS >` (`subConf`: `CONF_MENU/CONFIG/STATUS/PORTAL/EDITOR`).
  - **CONFIG:** lista con 4 a la vista, valor a la derecha y barra de desplazamiento; "volver" en el pie
    (`OpcionConf`, por bloques desde el 12 oct 2026: ID, LOCATOR, ALTITUD · TONO, ACELERACION · SALVAPANTALLAS,
    APAGADO · WEBCONF, ACTUALIZAR FW, DEPURACION, FABRICA). **SALVAPANTALLAS**: NO, 15 s, 30 s, 1, 2 o 5 min
    (`OPCIONES_CARA_S`, `caraEsperaIdx`, NVS `cara` en `chispas`, por defecto 15 s; NO = la cara no sale). ID y LOCATOR con el editor de las
    memorias (`confAbrirEditor()`; destinos `EDITOR_ID_ESTACION` -2 y `EDITOR_LOCATOR` -3; solo letras y cifras, y
    `/` en el ID); OK valida con `aplicarIndicativo()`/`aplicarLocator()` (las mismas que el portal) y, si no vale,
    "NO VALIDO" y se sigue editando; guarda en `estacion`. WEBCONF abre el portal y una vista con sus datos
    (`CONF_PORTAL`: `ver qr` y `volver`; el portal sigue abierto al volver, y la opción muestra "ABIERTO").
    FABRICA pide confirmación ("OTRA VEZ").
  - **STATUS:** 5 páginas (girar), pulsar o atrás vuelve; se redibuja cada segundo (`procesarStatus()`):
    ESTACION (ID, locator, hora, FW), SISTEMA (batería, LoRa, apagado, tono y aceleración), WIFI (red guardada
    leída de la NVS `nvs.net80211`/`sta.ssid` sin encender el WiFi, `ssidGuardada()`; estado, IP y nombre del
    portal), MEMORIA (heap libre, mínimo, mayor bloque, NVS) y ARRANQUE (tiempo encendido, motivo del reinicio,
    CPU).

## Funciones (probadas en placa salvo que se diga)
- **Pantalla CW** (v0.7d; nombres y orden de la v0.8g): estilo LoRa, título " ENTRENADOR CW " en negativo y
  `< KOCH (RX) >`, `< ECHO (TX) >`, `< TX LIBRE >`, centradas con `dibujarOpcionCentrada()` como las de LoRa (`opcionPantalla3` 0/1/2; no caben 4 opciones con 9 px por línea).
  - **KOCH (RX)**: ajustes en lista desde la v0.9h (WPM, ESPACIADO, LECCION, CARACTERES, GRUPOS, ESTAD.; pie
    `iniciar`/`volver`; `opcionSubpantalla` 0–7, 6 = iniciar por defecto). El Echo, igual (`NOMBRES_AJUSTES_ECHO`).
    ESTAD. (Koch y Echo): pie `3/17 L01-03  >volver` (`nombrePaginaEstad()`). Práctica: pausa/seguir y salir; resultado: volver y
    rep. Al salir de estadísticas se vuelve a la configuración. **Corrección por carácter** (v0.8f, probada): en
    "corregir" se marcan los fallos en el listado (cursor subrayado, `focoCorregir`; pulsar marca/desmarca en vídeo
    inverso, `marcasFallo[]`; anillo caracteres → rep. → ok); `numeroErrores` sale de las marcas. "ok"
    (`enviarResultadoKoch()`) guarda el % de la lección y los aciertos/fallos de cada carácter en `statsCarKoch[]`
    (41, orden de `TABLA_KOCH`; NVS `carK`). **Dos modos de corregir** (v0.9f, probado a mano en la placa 1 el 11 oct 2026): la última página del
    listado ofrece `>x letra >total >rep.`; "x letra" es lo de antes y "total" (`CW_PANTALLA_FALLOS_TOTALES`) solo
    pide el número de fallos (0..caracteres, `fallosTotales`, ya editando al entrar y partiendo de lo marcado en
    x letra; % en vivo; rep. y ok). En modo total se guarda el % de la lección y se suma a `kochTotCaracteres` y
    `kochTotAciertos` (NVS `ktcar`, `ktok`), que entran en TOTALES KOCH; no toca `statsCarKoch` (MÁS ERRORES solo
    ve lo corregido x letra). El borrado del Koch también los borra. Por serie: `[KOCH] FALLOS TOTALES: 12/250`. ESTAD. (v0.8g, 17 páginas): 1 **MÁS ERRORES** (`dibujarMasErrores()`:
    las 4 de peor %, con ≥ `MIN_INTENTOS_LETRA` = 5 intentos), 2 **TOTALES KOCH** (lecciones, caracteres, %),
    3–16 lecciones, 17 borrado (solo lo del Koch). La letra nueva de la lección sale el 25 % de las veces
    (`PESO_LETRA_NUEVA_PCT`, `caracterKochAleatorio()`, también en Duo y Echo); con 4 caracteres o menos, reparto
    uniforme. Tabla Koch de LCWO (v0.3i).
    **REFUERZO** (v1.02b; probado en la placa 1 el 11 oct 2026: la opción cambia NO/SUAVE/FUERTE en Koch y Echo y por
    serie salen los pesos, p. ej. `[KOCH] REFUERZO FUERTE: K x1.4 M x1.4`, con datos del Echo): opción NO / SUAVE / FUERTE en los ajustes
    del Koch (`refuerzoKoch`, NVS `kref`) y del Echo (`echoRefuerzo`, NVS `eref`), por defecto SUAVE. El sorteo
    (`caracterKochAleatorio(leccion, refuerzo)`) mantiene la letra nueva al 25 % y reparte el resto por pesos:
    `pesoRefuerzo()` = 1 + tope × (fallos + 1) / (intentos + 2), con tope ×2 (suave) o ×3 (fuerte), usando la
    **suma de las estadísticas del Koch y del Echo** (la corrección por totales del Koch no da datos por letra; el
    Echo sí). Con menos de 5 intentos, peso 1. Sin datos en la lección, el valor sale `SUAVE S/D` y el sorteo es
    normal. **Olvido gradual** (`registrarCaracter()`): al pasar de 100 intentos una letra, aciertos y fallos se
    dividen a la mitad (también afecta a MÁS ERRORES). Por serie al empezar: `[KOCH] REFUERZO SUAVE: K x1.6 ...`.
    El Duo no usa refuerzo.
  - **ECHO (TX)** (v0.7b; reestructurado en la v0.8g, probado): Chispas emite 1–5 letras de la lección del Echo con
    el motor CW (keyer en pausa mientras emite: `keyerPausar()`/`keyerReanudar()`) y hay que repetirlas con las
    palas; durante el Echo el keyer descodifica con `echoWpm`. Como el Koch: **ajustes** propios (WPM 10–40, SP
    100–200 %, LECCIÓN 1–40, LETRAS 1–5, VER, RETOS 10–50, ESTAD., iniciar, volver; NVS `ewpm`, `esp`, `elecc`,
    `elargo`, `ever`, `eretos`; independientes del Koch y de TX LIBRE). **Por lección** (v0.9g, probado a mano en la placa 1 el 11 oct 2026): al
    acabar una práctica, su % de retos bien cuenta para la lección del Echo (`statsEchoLecc[40]`, veces y % máx./mín.,
    NVS `eLec` en un solo bloque); ESTAD. del Echo con 17 páginas como el Koch (más errores, totales, 14 de
    lecciones con `dibujarPaginaLecciones()`, compartida, y borrado, que también borra `eLec`), **práctica** (`echoPracticando`: `RETO n/N`
    y `BIEN n`; repetir —no cuenta— y salir; se evalúa al llegar a las letras del reto o con un silencio de palabra;
    acierto: "BIEN!" y otro reto a 1,2 s; fallo: "MAL: R  K -.-" y se repite el mismo a 3 s; cada intento cuenta como
    reto) y **resultado** (`ECHO_FIN`: %, volver y rep.). Estadísticas en `statsCarEcho[]` (NVS `carE`) y totales
    (`eint`, `eok`, `eses`); ESTAD. del Echo: MÁS ERRORES, TOTALES ECHO, borrado (solo lo del Echo). Por serie:
    `[ECHO] U -> V: MAL (5/8)` y `[ECHO] FIN: 8/10`. El bloque `car` de la v0.8f (Koch y Echo juntos) se borra al
    arrancar.
  - **TX LIBRE** (antes KEYER; v0.7a): tarea FreeRTOS `keyer` (núcleo 1, prioridad del motor CW) que lee las palas
    (GPIO4/5) cada 1 ms; solo activa en su pantalla o en la práctica del Echo. Memoria de la pala contraria: modo A
    solo en la pausa entre elementos; modo B también durante el elemento. Descodifica lo manipulado (silencio de 2
    puntos = fin de carácter, 6 = espacio (5 hasta la v0.8i: partía palabras); patrón desconocido = `*`) y lo pasa a `loop()` por una cola; también sale
    por serie. Desde la v0.9h: arriba `20WPM  MODO A  NORM`, el texto y el pie `> borrar  menu`; "menu" abre la
    lista (`keyerEnAjustes`): WPM 10–40, MODO A/B, PALAS NORM/INV y SALIR, con volver en el pie; en `chispas`
    (`kwpm`, `kmodob`, `kinv`). Salida por LED y tono en GPIO7; KEY aún no. Apretón iámbico (modos A y B) y texto largo probados a mano el 12 oct 2026.
    Prueba del modo B: mantener la raya, tocar el punto y soltar las dos antes de que acabe la raya → modo A da "T",
    modo B da "N" (salió N). Si se suelta en la pausa, los dos modos añaden el elemento (memoria de pausa).
- **Motor CW** (v0.6a/b): tarea FreeRTOS `motorCW` en el núcleo 1 con prioridad 3 (`loop()` tiene 1), plazos absolutos
  sobre ticks de 1 ms. API: `motorCWEmitir()`, `motorCWDetener()` (corta enseguida con una notificación),
  `motorCWOcupado()`, `motorCWFinMs`. Solo las tareas llaman a `conmutarEmisionMorse()`. `DEBUG_CW` mide el retraso
  desde la tarea (resultado: 0 ms).
- **LoRa** (pantalla 3): `< PRACTICA DUO >` y `< QSO LIBRE >` (nombres de la v0.8i). Tramas digitales v2
  (`transmitirTextoLoRa(tipo, wpm, texto)`; tipo 1 = grupo Duo, tipo 2 = palabra de QSO); antes de transmitir se lee
  la trama pendiente y se escucha el canal (CAD, 3 intentos, espera 100 ms). Lo que llega *durante* `transmit()` se
  pierde por ser semidúplex. Cola de recepción de 8 (`colaRxDuo`, compartida).
  - **PRÁCTICA DUO**: grupos Koch a los wpm del emisor (5–60); RSSI en pantalla. Desde la v0.9h: arriba
    `LECCION 4  5 LETRAS` (wpm y espaciado van en la cabecera), TX y RX, y el pie `> enviar  ver  menu` ("ver"
    solo si hay un grupo oculto; `opcionDuo4_1` 0–2); menu (`loraEnAjustes`): WPM, ESPACIADO, LECCION, SALIR.
  - **QSO LIBRE** (v0.8h, probado en un sentido: placa 1 → placa 2, que no tiene palas): con las palas (keyer activo
    en cualquier foco, wpm de TX LIBRE); cada palabra se envía sola al detectar el espacio entre palabras
    (`qsoCaracterManipulado()`; a los 32 caracteres, en el acto; al salir, lo que quede). Lo recibido suena a los wpm
    del emisor (SP 100 %) cuando se lleva un silencio de 7 puntos sin manipular (`keyerUltimoUsoMs`, lo actualiza la
    tarea del keyer), con el keyer en pausa; tocar una pala mientras suena lo corta (break-in) y lo pendiente pasa a
    texto (`procesarQSO()`). Pantalla: recibido arriba (2 líneas), separador con indicativo y RSSI, manipulado abajo
    (2 líneas); pie `> borrar  menu` (v0.9h; borrar limpia los dos textos, no la palabra que se está enviando);
    menu: WPM, VER LO RECIBIDO y SALIR. VER NO: la palabra sale como `???` mientras suena (NVS `qver`). El keyer
    cierra la palabra a los 6 puntos de silencio (con 5 se enviaba letra a letra).
- **Radio** (v0.4c, `#define LORA_*`): 868,1 MHz, SF7, 250 kHz, CR 4/7, sync 0x12, 10 dBm, TCXO 1,8 V (DIO2 como RF
  switch lo activa RadioLib 6.6 en `begin()`; SPI 9/11/10). RSSI a 2 m: unos -40/-45 dBm.
- **WiFi** (v0.5a/b, v0.7k): se enciende al entrar en PROP y POTA, y en SOL solo si no hay hora (para el NTP).
  Si no hay red guardada o no conecta en 15 s, el portal **no se abre solo**: estado `WIFI_SIN_CONEXION`. Desde la
  v0.9h, PROP, POTA y SOL son un **menú** (`subDatos`: `DATOS_MENU/PAGINAS/PORTAL`, compartido) con `< VER DATOS >`
  (o VER SPOTS) y `< ACTUALIZAR >` y una línea de estado (conectando, hora del dato de hamqsl, nº de spots, locator);
  sin conexión, las opciones pasan a `< ABRIR WEBCONF >` (abre el portal y su vista, la misma de CONF:
  `dibujarVistaPortal()`) y `< REINTENTAR >`, con el foco en la primera (`quedarseSinConexion()`). Páginas: PROP
  INDICES y BANDAS; POTA, 4 spots por página; SOL HOY, AHORA y MAÑANA (salida, puesta y línea gris de mañana; AHORA:
  altura del sol, si está en línea gris, hora y locator). En SOL, ACTUALIZAR vuelve a sincronizar la hora por NTP
  (`actualizarHoraSol()`). Probado por serie con capturas (PROP y POTA con datos reales). Si el portal se cierra
  por tiempo (5 min) en esas pantallas, vuelve el menú sin conexión. Al reentrar se reintenta. Hora NTP (CET/CEST) al conectar.
  HTTPS con `setInsecure()`. PROP: hamqsl.com cada 60 min (su autor pide no consultar el XML más de una vez por hora; desde la v0.9c), dos páginas (índices y condiciones por banda). POTA: spots
  solo CW cada 2 min (sin límites publicados: con moderación); línea de spot en 21 caracteres (referencia entera a la
  derecha; la frecuencia pierde el decimal y luego se recorta el indicativo si no cabe).
- **Webconf** (portal WiFiManager, `Chispas-XXXX`, clave `PORTAL_CLAVE` = `chispas73`, 192.168.4.1; v0.7f–k,
  probado): se abre con **WEBCONF** en CONF > CONFIG o desde el aviso de sin conexión, y muestra los datos (red,
  clave, web). Con el portal abierto, en esa vista está **`ver qr`**: QR de WiFi a pantalla completa
  (`WIFI:T:WPA;S:...;P:...;;`, versión 3, 29×29, a 2 px por módulo), generado con el componente `qrcode` de ESP-IDF
  que trae el core (`esp_qrcode_generate`, sin librerías extra); cualquier giro o pulsación vuelve a los datos. El QR
  une el móvil a la red, pero la página del portal se abre sola o no según el móvil (no depende de Chispas; se probó
  y se descartó un segundo QR con la URL). **Portal en castellano**: `include/wm_strings_chispas.h` (traducción propia
  basada en `wm_strings_es.h` de WiFiManager 2.0.17, que estaba a medias), activada con `build_flags` `-I include` y
  `-DWM_STRINGS_FILE` en `platformio.ini`; menú "Red WiFi", "Estación y hora", "Salir". Si se actualiza WiFiManager,
  comparar con su `wm_strings_en.h`. Página "Estación y hora": indicativo, locator, hora manual en formato
  **HH:MM DD-MM-AAAA** (acepta barras; la pista del campo es la hora actual: `ParametroHora`) y casilla de **hora UTC**
  (`ParametroCasilla`, que sobrescribe `getCustomHTML()` para el "checked"; en `estacion`, `utc`): la cabecera
  muestra `12:05Z` en vez de la hora local.
- **Sol** (pantalla 6): salida/puesta, ventanas de línea gris (sol entre -6° y +6°) y altura actual con las ecuaciones
  de NOAA para el locator de la estación (por defecto IN80dk, Madrid, genérico). Necesita hora: sin ella enciende el WiFi
  (`CONECTANDO WIFI...`, luego `SINCRONIZANDO HORA...`); con hora no usa el WiFi.
- **Sensor ambiental BME280** (v1.03b; **probado en la placa 1 el 12 oct 2026**: BME280 auténtico en 0x76, lecturas,
  AMBIENTE, calibración de altitud, STATUS y {WX}). Opcional: sin sensor, todo sigue igual. Controlador propio
  (`src/parts/bme280.inc`; fórmulas en coma flotante de la hoja de datos de Bosch) en Wire1
  (SDA 41, SCL 42, 100 kHz); `iniciarBME()` busca 0x76/0x77 y distingue BME280 (0x60) de BMP280 (0x56–0x58, sin
  humedad); sin sensor, `Wire1.end()`. Modo forzado (T x1, P x4, H x1; `bmeMedir()` bloquea ~16 ms). `procesarBME()`:
  lectura cada minuto (cada 2 s con AMBIENTE a la vista) y una muestra de presión cada 10 min en `bmeHist[19]` (3 h).
  `bmeTendencia3h()`: cambio escalado a 3 h, desde 60 min de historial; ±1 hPa = BAJANDO/SUBIENDO; ≤ -3 hPa =
  `avisoTormenta` (la cabecera pone "TORM!" en vez del nombre; `[BME] AVISO DE TORMENTA` por serie). Ojo: al
  cambiar de altitud (subir a un monte) la presión baja y puede dar un falso aviso; con el GPS, reiniciar el historial.
  Página **AMBIENTE** (4/4 de SOL, solo con sensor, `paginasSol()`; no necesita hora ni locator). **CONFIG > ALTITUD**
  (`OC_ALTITUD`): muestra la altitud ("?" sin calibrar); pulsar, girar hasta la altitud real y pulsar calcula el QNH
  (`qnhParaAltitud()`, NVS `qnh` en `estacion`; sin él, 1013,25). STATUS página 6 **SENSOR**. Macro **{WX}**
  (`textoWx()`): "WX 18C", "WX MINUS 3C" bajo cero (el guion no está en la tabla Morse), vacío sin sensor; en la
  rueda del editor, en el portal y en `trxLimpiarTexto()`. Por serie cada 10 min: `[BME] T ... P ... TEND ...`.
  La temperatura sale alta por el calor de la placa (en la primera lectura, 25,8 °C dentro de casa).
- **SOTA: no programar el cliente.** Sus condiciones (api-db2.sota.org.uk/docs) exigen unirse al grupo
  "API-consumers" y prohíben conectar software generado por IA sin aprobación previa. Decisión pendiente del autor.
- **Batería**: divisor activado con GPIO37 **HIGH** en estas placas (Meshtastic declara LOW). Atenuación 2,5 dB,
  factor 4,9 calibrado con polímetro (4,10 V reales / 836,7 mV en el pin; un solo punto), curva Li-ion y suavizado al
  50 %. **Sin batería y con USB** el cargador deja ~4,17 V de media pero oscila (medido con `prueba_bateria`:
  dispersión 0,25 V frente a 0,002 V con batería): `leerBateria()` toma 200 lecturas seguidas y, si la dispersión
  supera 0,08 V (`VBAT_DISPERSION_SIN_BATERIA`), indica "sin batería" (`PWR`). Probado en las dos placas. Con USB el
  cargador sube la tensión: el % solo es fiable con batería sola.
- **Cara de Chispas** (v0.6l/m): salvapantallas, diseño propio inspirado en Eilik (no copiarlo). Ojos y boca
  con primitivas (~5 KB de flash). Desde la v1.05b sale en **todas las pantallas salvo CW, LoRa y TRX**
  (`caraPermitida()`; tampoco con el portal o el QR a la vista, `CONF_PORTAL`/`DATOS_PORTAL`, ni en `CONF_OTA`); al
  quitarla se vuelve a la misma pantalla. Aparece tras el tiempo de CONFIG > SALVAPANTALLAS (por defecto 15 s) sin
  tocar nada, alterna 45 s de cara y 8 s de la pantalla,
  y a los 5 min se duerme; cualquier giro o pulsación la quita sin hacer nada más (constantes `CARA_*`). Gestos al azar
  cada 6–15 s: contenta, risa, `>_<`, guiño, pícara (`ω`), nerviosa (gota), sorpresa y, desde el 12 oct 2026, de
  radioaficionado: **antena** con ondas y boca hablando (`EXP_ANTENA`), **auriculares** con `-.-` parpadeando
  (`EXP_AURICULARES`), **bocadillo** CQ/73/88 (`EXP_BOCADILLO`) y **guiño en Morse** 73/K/R con los ojos, escrito
  abajo a la derecha (`EXP_MORSE`, `ojosCerradosMorse()`, punto de 160 ms). Fondo: cansada con batería < 20 %;
  **preocupada con nube y rayo con el aviso de tormenta** del BME280 (`EXP_TORMENTA`, tiene prioridad). Pesos de los
  gestos (`hacerGestoAleatorio()`): más alegre tras una lección Koch ≥ 90 % en los últimos 15 min; en **línea gris**
  (`caraEnLineaGris()`, con hora y locator) más antena y bocadillo, que entonces siempre dice CQ; con **buena
  propagación** (SFI ≥ 150 en el último dato de hamqsl, `caraBuenaPropagacion()`) más alegre y más antena.
  Los gestos de bocadillo, auriculares y Morse fijan la mirada (`miradaFija()`). Probado en la placa 2 con capturas
  (12 oct 2026): bocadillo, antena y Morse; auriculares y tormenta sin ver aún. Con la cara a la vista, `actualizarPantalla()` no dibuja.
  Ocupa ~40 % del bucle mientras se ve.
- **OLED negra tras reset corto** (v0.4e: tras `display.begin()` se reenvían bomba de carga `0x8D 0x14` y encendido
  `0xAF`, en `reafirmarOled()`). **Sigue pasando a veces en la placa 1** (no en la 2; nunca al despertar del deep sleep):
  el controlador contesta por I2C y su estado dice "encendida" (`0x02`), pero el panel está negro. Probado el 10 oct
  2026 (v0.7u–z), todo descartado a petición del usuario, que volvió a la secuencia de la v0.4e (v0.8a):
  reenvío periódico de 8D/AF (no arregla), de AE/8D/AF (arregla a veces y parpadea, y a veces apaga un panel que
  funcionaba), `display.begin()` a mano (igual: una de tres lo apagó), corte de Vext 500 ms al arrancar (falla a
  veces) y con I2C y RST configurados antes del corte (1 fallo de ~10). A mano, la orden serie `v` (cortar Vext
  500 ms + reset + begin) la encendió 5 de 5. Al arrancar la LoRa la OLED parpadea: sospecha de caídas de 3,3 V en la
  placa 1 (alimentada a través del medidor USB); probado sin medidor y con batería (10 oct 2026):
  encendió 8 de 10 veces, así que el medidor no es la causa. Aparcado por decisión del usuario. Las órdenes de diagnóstico por serie de la v0.8a (`s`, `a`, `b`, `v`)
  se quitaron en la v0.8b a petición del usuario (están en el historial de git, commit 2f61d2f, si hacen falta).
- **Dormir** (v0.7n, probado en la placa 2: tres ciclos; y en la placa 1 el 12 oct 2026): PRG largo, o `apagadoAutoMin` sin tocar nada (v0.7o: se
  elige en CONF → `apagado`: nunca, 5, 10, 15, 30, 60 o 120 min; por defecto 30; NVS `apag`), salvo en CW, LoRa y
  TRX (y sus subpantallas) o con el portal abierto → `dormirChispas()`: aviso 1,5 s, OLED,
  WiFi, LoRa y LED apagados; KEY en BAJO, Vext apagada y divisor de batería desactivado, retenidos con `gpio_hold_en`
  + `gpio_deep_sleep_hold_en` (se liberan al principio de `setup()`); despierta con ext0 en GPIO0 a nivel bajo.
  Despertar con PRG **no** entra en modo de carga (`boot:0x8`); arranca de nuevo (`esp_reset_reason` 8).
- **Pantalla TRX** (v0.9a; conexión con el transceptor; nombre en la cabecera "TRX"): menú con título " TRANSCEPTOR ",
  `< DECODIFICAR >`, `< OPERAR >`, `< QSO >` y **memorias** en el centro del pie (`opcionTrx`; `subTrx`).
  **Probado en la placa 1 (10 oct 2026):** menú, memorias, editor, envío de memorias (suenan en el buzzer, luego altavoz) y el corte de
  seguridad de KEY (dos fallos corregidos: lectura doble de `keyActivaDesdeMs` desde `loop()` y marca `millis() | 1`
  1 ms adelantada, que con la resta sin signo daba la vuelta y cortaba al manipular). GPIO2 medido con
  polímetro con el TUNE (11 oct 2026, v0.9e): ~3,3 V activa, 0 V en reposo. PC817 y minijack de KEY montados y
  comprobados con el polímetro en el jack (11 oct 2026: abierto en reposo, conduce con el TUNE). **Probado con la emisora**
  (uSDR, HF QRP SDR, 11 oct 2026): manipula por el **anillo**, no por la punta (con el jack a medias funcionaba y a
  fondo no), así que J4 lleva un **puente R-T** (esquema, hoja 5) y vale para emisoras de punta o de anillo; la
  emisora, en llave recta. **Xiegu X6200: probada el 12 oct 2026** (llave recta, con el puente R-T: TUNE, palas y
  memorias bien; si sale punto-raya alternado, la emisora está en modo iámbico). Decodificador: montado y probado el 11 oct 2026 (ver DECODIFICAR).
  - **Salida KEY:** `conmutarEmisionMorse()` activa GPIO2 solo con `keyHabilitada` (OPERAR y QSO de TRX; nunca en
    Koch, Echo, TX LIBRE ni LoRa). `trxTonoLocal` (NVS `ttono`): altavoz/auriculares mientras sale por KEY. Corte de
    seguridad si KEY sigue activa más de 10 s seguidos (`KEY_MAX_CONTINUA_MS`): "KEY CORTADA (>10 s)" y por serie con
    las marcas de tiempo.
  - **OPERAR:** keyer con wpm propio (`trxWpm`, NVS `twpm`). Desde la v0.9h: arriba los ajustes a la vista
    (`20WPM TONO SI TUNE 5s`), el texto manipulado, la línea de SU y el pie `> mem  tune  menu` (`dibujarPieTrx()`,
    igual en QSO). "menu" abre la lista de ajustes (`TRX_AJUSTES`, vuelve a `trxAjustesOrigen`): WPM, TONO LOCAL,
    TUNE (s), SU (editor) y SALIR, con volver en el pie; las palas siguen manipulando (`trxManipulando()`). Las palas cortan una memoria que se está enviando.
  - **DECODIFICAR:** tarea `decod` (núcleo 0, prioridad 2) que lee GPIO6 (ADC1 canal 5) por DMA (`adc_digi_*` del
    IDF 4.4) a 8 kHz; Goertzel en bloques de 96 muestras (12 ms); tono fijo 400–1000 Hz o AUTO (el más fuerte de
    13, `trxTonoDecodHz`, NVS `tdec`); umbral adaptativo (ruido/pico, histéresis); punto adaptativo (5–50 wpm);
    barra de nivel logarítmica con la marca del umbral y "SATURA" si toca los topes (bajar RV2; sin el
    montaje, con GPIO6 al aire, también sale); pie `> borrar  menu` y menu con TONO (AUTO o Hz) y SALIR
    (v0.9h, `decodEnAjustes`); `[DECOD] ...`
    por serie cada 2 s. Sordo mientras sale por KEY y 300 ms después. Mientras funciona no se lee la batería (mismo
    ADC1). Valores de partida: `DECOD_MIN_SENAL` 1500, `DECOD_ESCALA_MAX` 96000.
    **Montado y probado en la placa 1 (11 oct 2026):** sin nada en J3, GPIO6 a ~1,65 V y barra baja y quieta (ya no
    sale SATURA). RV2 hay que llevarlo casi al máximo (multivuelta): con 0,1–0,5 V~ en la punta de J3 la barra llega
    al 60–90 % sin saturar. **Con Morse limpio** (generador del móvil/PC, texto de la NOAA "OFFSHORE WATERS...
    NATIONAL ... SERVICE WASHINGTON DC") descodifica bien en AUTO y con tono fijo, y los wpm estimados son correctos.
    **Comprobado y funcionando con audio Morse en entorno real** (11 oct 2026, placa 1): descodifica perfectamente.
    **Mejoras del 11 oct 2026 (en la v1.02b), probadas en la placa 1:**
    - **Silenciador:** un tono solo empieza si supera `DECOD_SNR_INICIO` (4×) el ruido de fondo; marcas de menos de
      `DECOD_MARCA_MIN_MS` (24 ms) o del 40 % del punto se descartan; en AUTO solo se cambia de tono si destaca
      `DECOD_AUTO_DESTACA` (2,5×) sobre la media de todos. El ruido se mide como un **suelo** (baja deprisa, sube
      despacio): con una media normal, los bordes de los elementos lo inflaban y el silenciador se comía puntos (se
      probó y descodificaba peor). Resultado: **con solo ruido de banda ya no salen letras** y el Morse limpio va bien.
    - **Fin de palabra adaptativo:** media móvil de la pausa entre letras (`huecoLetraMs`) y palabra a
      `DECOD_PALABRA_X` (1,7×) esa pausa, nunca menos de 5 puntos. Con un vídeo de YouTube de la NOAA llegó a partir y
      juntar palabras ("OF FSH ORE WA TERS"), pero **el usuario confirma (11 oct 2026) que corta las palabras
      perfectamente** con la v1.02b: no hace falta afinar las pausas.
  - **QSO:** decodificado arriba, estado del decodificador en el separador, manipulado abajo; mem, SU, tune, salir.
    **Probado con emisora real el 12 oct 2026:** manipular y decodificar en la misma sesión, sin descodificar lo propio.
  - **TUNE** (v0.9e): portadora continua por KEY de 1–10 s (`trxTuneSeg`, NVS
    `ttune`, por defecto 5) para ajustar acoplador o potencia. La duración se elige en OPERAR (`TUNE 5s`, a la derecha
    de la línea de SU) y vale también en QSO; "tune" en el pie de las dos (`>mem >SU >tune >salir`, `dibujarPieTrx()`,
    en negativo mientras emite). La emite el motor CW (`motorCWPortadora()`, campo `portadoraMs` de `PeticionCW`);
    pulsar otra vez o tocar una pala la corta. Mientras dura, el corte de seguridad deja la duración + 2 s
    (`TRX_TUNE_MARGEN_MS`). Tono local según `TONO SI/NO`. Por serie: `[TRX] TUNE 5 s` y `[TRX] TUNE FIN`.
    **Probado en la placa 1 (11 oct 2026)**, en OPERAR y QSO: GPIO2 a ~3,3 V con el polímetro durante el TUNE y 0 V
    en reposo; 10 s sin "KEY CORTADA"; se corta pulsando otra vez y con las palas. (Un primer "0 V siempre" fue por
    medir en otra patilla.)
  - **Memorias:** 6 de 40 caracteres en NVS `estacion` (`m0`..`m5`), precargadas (CQ, respuesta, RST, HW?, TNX 73,
    73 SK), con `{MI}` (indicativo) y `{SU}` (corresponsal). Se editan con el encoder (rueda en el pie: A–Z, 0–9,
    signos, ESP, {MI}, {SU}, <-, CLR, OK, ESC; PRG = ESC, larga = OK), con las palas (dentro del editor) o en webconf
    (página "Estación, hora y memorias"; `trxLimpiarTexto()` deja solo lo emitible).
- **Salida KEY** (GPIO2): `KEY_OUT_PIN` en BAJO al principio de `setup()` (v0.6c); la usa la pantalla TRX (v0.9a). Medido con polímetro:
  0 V en marcha; picos de 0,3–0,5 V al arrancar y con RST (pin al aire hasta `setup()`), por debajo de los ~1,1 V
  que necesita el LED del PC817. **10 kΩ de GPIO2 a GND** (R10, ya en el esquema y en la lista).
- **Tono** (montado y probado el 10 oct 2026, placa 1): GPIO7 (PWM, `frecTonoHz`) → R1 1 kΩ → RV1 10 kΩ log (pat. 3;
  pat. 1 a GND) → cursor → C1 10 µF (+ al cursor) → R2 220 Ω → J2 T y R3 220 Ω → J2 R; J2 TN → BZ1 + (piezo pasivo
  de 16 mm), BZ1 − y J2 S a GND; RN libre. Auriculares por los dos lados y silencio mecánico del buzzer probados
  (los fallos fueron malos contactos de R2/R3 en la protoboard). **El piezo suena muy bajo** a 3,3 V, también
  directo a GPIO7: no se oye con ruido ambiente. Con R1 de 100 Ω sube algo (R1 era la mayor pérdida), pero se dejó
  en 1 kΩ: se prueba con auriculares. Descartados: transistor con buzzer magnético o altavoz (se pierden RV1 y el
  silencio del jack) y dos GPIO en contrafase (RV1 deja de regular el buzzer). **Pedidos** (10 oct 2026): módulo
  **PAM8302A** (U2) y **altavoz de 8 Ω 0,5–1 W** (LS1), en el sitio del piezo: TN → A+ con R9 10 kΩ a GND, A− y GND
  a GND, VIN a 3V3 (el pin 5V no tiene tensión con batería), salida al altavoz; **SD a GPIO38** (decidido el 12 oct
  2026; J3-11): el firmware lo pondrá en BAJO fuera de CW/LoRa/TRX y al dormir (con `gpio_hold`), porque el 3V3 sigue
  vivo en deep sleep y sin SD el amplificador gastaría ~4 mA también dormido. RV1 sigue de volumen; el PAM8302A tiene mucha ganancia: bajar su
  trimmer si lo trae, o 47 kΩ en serie con A+. Vigilar picos en el 3V3 (la OLED de la placa 1 es sensible).
  **Montados el 12 oct 2026** (placa 1): el altavoz suena bien con RV1 de volumen y calla al meter los auriculares.
  **Firmware v1.04b:** `ponerAmplificador()`/`procesarAmplificador()`: SD en ALTO solo en CW, LoRa y TRX y durante el
  pitido de prueba del TONO (`aplicarFrecTono(true)` lo enciende), en BAJO en el resto; al dormir, BAJO retenido con
  `gpio_hold_en` (se libera en `setup()`, donde arranca en BAJO). `[AMP] ENCENDIDO/APAGADO` por serie. **Control de
  SD probado en la placa 1 el 12 oct 2026 (OTA a la v1.04b).** Esquema (hoja 4): el amplificador es el montaje principal (sin buzzer).

## Organización del código (13 oct 2026)
- **Por qué:** `main.cpp` tenía 7009 líneas y 285 KB; leerlo o buscar en él gastaba muchos tokens. Se partió para
  poder abrir solo el área que toca, sin cambiar el código.
- **Cómo:** se cortó por las cabeceras de sección `// ====` en 27 trozos seguidos, copiados byte a byte (script en
  Python, sin PowerShell) a `src/parts/<area>.inc`. `main.cpp` conserva la cabecera (`@version`, `FW_VERSION`,
  `DEPU_*`), los includes de librerías, `depuActiva()`, la lista de `#include "parts/....inc"` y el final (periféricos,
  dormir, `setup`, `loop` y lo que llaman). Cada `.inc` empieza con SPDX y una línea de qué contiene.
- **Una sola unidad de compilación:** el preprocesador vuelve a juntar el mismo texto en el mismo orden, así que no hay
  prototipos ni `.h` de interfaz; cada parte usa lo de las anteriores y ninguna compila suelta.
- **Por qué `.inc` y `src/parts/`:** con `.inc` IntelliSense no los analiza como cabeceras (con `.h` marcaba errores
  falsos). La subcarpeta evita que un `src/wifi.h` o `src/nvs.h` tape a `<WiFi.h>` o al `<nvs.h>` del IDF (PlatformIO
  pasa `-Isrc` y Windows no distingue mayúsculas).
- **Comprobado:** al juntar las partes sale exactamente el `main.cpp` anterior (`cmp`). Compilados los dos entornos
  antes y después: mismos RAM y flash, y los `.bin` solo difieren en `app_elf_sha256` (0xb0–0xcf, hash del ELF, que
  cambia con la información de depuración: nombres de archivo y líneas) y en la suma y el SHA-256 del final de la
  imagen, que dependen de él. Código y datos, idénticos.
- **Orden y contenido** (ver tabla en `CLAUDE.md`): `pines`, `morse`, `globales`, `audio`, `motor_cw`, `keyer`,
  `estadisticas`, `echo`, `bme280`, `memoria_nvs`, `bateria`, `oled_comun`, `lora`, `decodificador`, `trx`, `wifi`,
  `ota`, `datos_web`, `pant_inicio_cw`, `pant_lora_trx`, `pant_prop_pota`, `sol`, `conf`, `estados_cw`, `cara`,
  `entrada`, `eventos`. Rarezas heredadas del orden original: `procesarRecepcionLoRa`, `loraEscuchar` y
  `salirModoLoRa` están al final de `trx.inc`; `actualizarPantalla()` (el reparto del dibujo) está al final de
  `conf.inc`; el dibujo de cada pantalla está en `pant_*.inc`, lejos de su lógica.
- **Siguiente fase posible** (no hecha): agrupar por pantalla con prototipos, o los módulos `.h`/`.cpp` de
  `ROADMAP.md`. Ahí el `.bin` ya cambiaría y habría que probar en placa.
- `git log` de los `.inc` empieza en el corte; la historia anterior está en `git log -- src/main.cpp`.

## Pendiente
1. **Pruebas a mano** (11 oct 2026): hechas en la placa 1 (listas y menús, aceleración, corrección total del Koch,
   Echo por lección) y en la placa 2 (OTA a la v1.01b; menús, listas, edición con vuelta, páginas y práctica sin
   encoder). X6200 con KEY probada el 12 oct 2026. OLED negra de la placa 1: aparcada (falla ~2 de 10 arranques también con
   batería y sin medidor).
2. **Montaje** (orden de la hoja 7 del esquema; hecho: raíles, encoder, palas, tono con amplificador, altavoz y jack,
   salida KEY con PC817 y minijack, BME280); amplificador y altavoz llegan el 12 oct y GPS el 13 oct 2026 (el enable del GPS queda pendiente: falta el AO3401A; el GPS irá sin él, siempre alimentado); BME280 montado y probado (v1.03b). Entrada del decodificador en GPIO6:
   **montada y probada el 11 oct 2026** (ver DECODIFICAR): comprobado con audio Morse real, letras y separación de palabras bien; la batería se sigue leyendo bien después de usar DECODIFICAR o QSO (probado en la placa 2 el 12 oct 2026). Falta comprar: divisor minijack 1→2. El buzzer magnético de 16 Ω no se usa.
3. **Ideas aparcadas** (para repensar más adelante):
   - **Estabilidad comprobada** (placa 2, v1.03b, noche del 12 oct 2026: 8 h en POTA, 233 consultas HTTPS a pota.app, una
     con HTTP -11, tiempo de lectura, que se recuperó sola): heap libre 248,8–249,1 KB, mínimo 165,5 → 162,7 KB en la
     primera hora y luego fijo, mayor bloque 180 212 B fijo (sin fragmentación), pila libre loop 2568, motorCW 3292,
     keyer 2304 sin cambios. Pendiente solo la pila del motor CW tras Duo (2016 bytes libres de 4096, medida antigua). Deep sleep medido por el usuario el 11 oct 2026: **0,00 A** (por debajo de la
     resolución del aparato) y después en µA (12 oct 2026): **despreciable**.
   - Detectar USB y carga: la V3 no lo permite por software. Propuesta sin decidir: divisor 100k/150k del pin 5V
     (J2-2) a GPIO33 (J2-12); comprobar antes que el pin 5V da 0 V solo con batería.
4. QSO LIBRE: probar en los dos sentidos cuando la placa 2 tenga palas. A estudiar, no prioritarios: audio I2S y GPS ATGM336H en lugar del NEO-6M (ver `ROADMAP.md`).

## Decisiones tomadas
- Aspecto: mantener el estilo actual de pantallas (fuente 5×7, sin imitar Meshtastic).
- Práctica en pareja con **tramas digitales**, no audio: un byte por carácter con bit centinela (punto = 0,
  raya = 1) y un byte reservado para el espacio entre palabras. Cabecera con magic, versión/tipo, `seq`, wpm e
  indicativo (8 caracteres). Unos 32 caracteres por trama; enviar por frase o línea, no por pulsación. Capa de
  transporte intercambiable (LoRa; ESP-NOW descartado en el proyecto principal el 12 oct 2026: idea para un **Chispas Lite**,
  ver `ROADMAP.md`, que también recoge las ideas de Chispas Pro (BLE, carcasa) y Ultimate (palas táctiles)).
- Si el firmware del Morserino-32 sirve de referencia: es open source y su protocolo MOPP envía un paquete por
  palabra. Las V3 no son compatibles con los Morserino oficiales.
- POTA/SOTA: búsqueda de parque offline con CSV guardado en LittleFS; spots online con cola si no hay cobertura.
  Las APIs de POTA son no oficiales; la lectura de SOTA es abierta, pero publicar spots exige credenciales.
- Expansor MCP23017 descartado: GPIO directos.

## Prototipo en protoboard (desde el 9 oct 2026)
Montaje sin PCB con la lista de compra "opción A (buzzer)" (el buzzer se sustituyó por el amplificador PAM8302A y
un altavoz el 12 oct 2026), comprada salvo el GPS (llega el 13 oct 2026), el transistor AO3401A de su enable, el adaptador a bornas,
el adaptador de 6,35 mm y los opcionales. Valores de la lista sin probar en banco.

| GPIO | Uso |
|---|---|
| 2 | Salida KEY (PC817, 330 Ω, 10 kΩ a GND, minijack J4 con puente R-T). `KEY_OUT_PIN`. **Montado y probado con la uSDR y la X6200** |
| 4 / 5 | Pala DIT / DAH (pull-up interno; `KEYER_PADDLE_DIT/DAH`). **Montado y probado** |
| 6 | Entrada del decodificador (ADC1): acoplo 1 µF, polarización 2×10 kΩ, trimmer, 1 kΩ y 2×1N4148 |
| 7 | Tono CW por PWM: 1 kΩ, potenciómetro de volumen, amplificador PAM8302A con altavoz en TN del jack de auriculares (silencio mecánico) y auriculares con 10 µF y 220 Ω en T y R. **Montado y probado** (el buzzer piezo del principio se descartó: sonaba muy bajo) |
| 38 | SD (apagado) del amplificador PAM8302A (`AMP_SD_PIN`). BAJO = apagado. Amplificador montado y control de SD probado (v1.04b) |
| 19 / 20 / 48 | Encoder KY-040 CLK / DT / SW. **Montado y probado** (19/20 son USB D−/D+ del S3 pero funcionan como entradas) |
| 47 | RX desde el TX del GPS (UART por la matriz GPIO) |
| 3 (pendiente) | Enable del GPS: AO3401A en el positivo, 100 kΩ puerta-3V3, BAJO = encendido. **Pendiente** (falta el transistor): el GPS se monta sin él, siempre alimentado. Antes se probará apagarlo por software (ver 39) |
| 39 (a probar) | TX hacia el RX del GPS (UART): órdenes UBX para dormirlo (`UBX-RXM-PMREQ`) y despertarlo; si dormido gasta < ~0,5 mA, sustituye al enable de GPIO3. Alternativas al AO3401A: IRLML6401, Si2301, DMG2305UX o PNP BC327/BC557 (ver `ROADMAP.md`) |
| 41 / 42 | BME280: I2C 0x76 en Wire1, SDA 41 (J3-8), SCL 42 (J3-7). **Montado y probado** (12 oct 2026, v1.03b) |

Posición física (comprobada con fotos de la placa; pin 1 = GND, pad cuadrado, el más cercano al USB):
- J3: 18 GPIO7 tono · 17 GPIO6 decod. · 16 GPIO5 DAH · 15 GPIO4 DIT · 14 GPIO3 GPS en · 13 GPIO2 KEY · 12 GPIO1 (VBAT, no)
  · 11 GPIO38 SD del amplificador · 10/9 GPIO39/40 (libres) · 8/7 GPIO41/42 · 6/5 GPIO45/46 (no) · 4 GPIO37 (no) · 3/2 **3V3** · 1 **GND**.
- J2: 18/17 GPIO19/20 encoder CLK/DT · 16 GPIO21 (OLED_RST) · 15 GPIO26 (flash, no) · 14 GPIO48 encoder SW · 13 GPIO47 GPS RX
  · 12/11 GPIO33/34 · 10 GPIO35 (LED) · 9 GPIO36 (Vext) · 8 GPIO0 (PRG) · 7 RST · 6/5 GPIO43/44 (UART0) · 4/3 Ve · 2 5V · 1 GND.
- `Ve` es Vext (conmutada por GPIO36, alimenta la OLED): los módulos van a 3V3 de J3. `5V` solo tiene tensión con USB.
- GPIO17/18 (I2C de la OLED) no salen a los conectores, pero hay dos **pads de prueba "17" y "18" por detrás**
  (mismo bus; 0x76 no choca con la OLED 0x3C): solo si faltan pines, soldando hilo fino.
- GPIO3 es pin de arranque (JTAG) solo con un eFuse grabado (de fábrica no): el 100 kΩ del enable no debería afectar
  (razonado, sin probar).
- Pines libres: 39–42, 33 y 34. No usar: 0, 1, 8–14, 17/18/21 (OLED), 26–32, 35, 36, 37, 43/44, 45/46 (arranque).

Esquema de conexiones: `docs/esquema_protoboard.pdf` (7 hojas: vista general, pines, entradas, tono y amplificador,
KEY y alimentación, decodificador, componentes y montaje), generado con `docs/gen_esquema.py` (HTML/SVG; el PDF se
imprime con Edge sin interfaz: `msedge --headless --no-pdf-header-footer --print-to-pdf=... archivo.html`; en el segundo equipo no hay Edge: lo mismo
con `C:\Program Files\Google\Chrome\Application\chrome.exe`). El script recibe la ruta del HTML como argumento. Las hojas
tienen altura fija y ocultan lo que sobra: tras cambios grandes, revisar el PDF (no basta con el HTML en pantalla). Si cambia algún pin, actualizar el script y regenerar el PDF.

## Placa perforada (12 oct 2026, sin montar)
Plano para montar el prototipo en una placa de pads a doble cara (pedidas de 7×9 cm, 24×30 agujeros; el plano ocupa
21×26, 53×66 mm): `docs/placa_perforada.pdf` (4 páginas: componentes, soldaduras con puentes y 12 cables numerados,
piezas y conectores, montaje), generado con `docs/gen_placa.py` (`python gen_placa.py salida.html` y luego Chrome
headless a PDF, como el esquema). El script comprueba que cada red queda unida, que nada se pisa y nada va bajo la
Heltec, **con y sin las reservas montadas**. Heltec en zócalos hembra con el USB en el borde; módulos y mandos en la
carcasa con cables Dupont a las tiras P1–P10. Reservas: trafo 600:600 (P10, quitar los dos jumpers), divisor de
detección de USB (R11/R12, cables A y B), enable del GPS (Q1/R8, quitar el cable 8) y C3 sobre las patas de D2.
**Pendiente antes de soldar:** confirmar la orientación de la Heltec (`J3_IZQUIERDA`, supuesta sin comprobar) y el
orden de pines del PAM8302A, el GPS y el BME280.

## PCB
Se empezará desde cero más adelante (decidido el 12 oct 2026): no hay nada de KiCad en el repositorio.

## Cómo trabajar
- Responder en español, directo y sin relleno.
- Compilar antes de dar un cambio por bueno. No fiarse de comprobaciones de sintaxis simuladas.
- `compilar.sh` (13 oct 2026): compila los dos entornos del firmware (o uno: `enc`, `sin`) y saca solo el resultado,
  RAM y flash, los errores con 3 líneas de contexto y los avisos de `src/` e `include/`; de las librerías, solo
  cuántos avisos hay. La salida completa queda en `.pio/compilar.log`. Sale con 1 si algún entorno falla. Busca `pio`
  en el PATH y, si no está (Bash de Claude Code), usa `~/.platformio/penv/Scripts/pio.exe`. Probado con un error
  metido a propósito.
- Extensiones de VS Code (revisadas el 13 oct 2026): PlatformIO IDE (necesaria; trae C/C++), Python y Pylance (para
  los scripts de `docs/`) y Claude Code. No hace falta ninguna más: clangd chocaría con el IntelliSense de PlatformIO,
  los monitores serie sobran (PlatformIO y pyserial 3.5 del Python de PlatformIO), y GitLens o Error Lens no ayudan
  a Claude. pioarduino sigue como no deseada en `.vscode/extensions.json`.
- Hacer los cambios sobre copia o con diff claro; no reestructurar sin avisar. No sobrescribir el original.
- Antes de afirmar algo sobre RadioLib, WiFiManager o el core de Espressif, comprobar la versión instalada.
- Separar lo que se ha probado en la placa de lo que solo se ha razonado.
- Grabar el puerto serie sin reiniciar la placa: pyserial con `dtr = False` y `rts = False` antes de `open()`.
  Desde el 13 oct 2026, `serie.py` en la raíz: busca el CP210x (VID 10C4, PID EA60; con varias placas pide
  `--puerto`), pone la hora a cada línea, guarda todo en `.pio/serie-AAAAMMDD-HHMMSS.log` aunque se filtre, y si la
  placa se reinicia vuelve a abrir el puerto. `-f "DIAG|DECOD"`, `-f "^\$G"` (NMEA), `-s 30`, `-e d` (orden serie).
- Permisos compartidos de Claude Code en `.claude/settings.json` (13 oct 2026): permitidos sin preguntar
  `compilar.sh`, `pio run`, `serie.py`, los scripts de `docs/`, git de lectura y `gh release list/view`; piden
  confirmación `publicar.sh`, `gh release create/edit/delete` y `git push`. Lo personal de cada equipo, en
  `.claude/settings.local.json` (no se sube).
- Skills del proyecto en `.claude/skills/` (13 oct 2026; viajan por Git; en cada sesión solo se carga su
  descripción, y el texto entero cuando hace falta o con `/nombre`): `cerrar-tarea` (compilar, revisar diff,
  NOTAS + CLAUDE.md, commit, push, resumen probado/razonado), `placa` (identificar la placa por la MAC, cargar el
  entorno correcto, `serie.py`, captura de la OLED), `release` (versión y `publicar.sh`; las notas de la release se
  reescriben para el usuario final) y `esquema` (gen_esquema/gen_placa → PDF con Edge o Chrome; probado el 13 oct
  generando la placa en una carpeta temporal: mismo tamaño que el PDF del repositorio). Con ellas salen de
  CLAUDE.md los procedimientos de release y de PDF.
- Ojo con los heredocs de Bash: la herramienta reduce las barras invertidas dobles a una (un `'\0'` acabó como
  byte nulo en el archivo). Para código con barras invertidas, editar con la herramienta Edit o Write.
- No reescribir `main.cpp` con `Get-Content`/`Set-Content` de PowerShell 5.1: lee el UTF-8 sin BOM como ANSI y
  rompe las tildes (y añade BOM). Usar Edit, o Python con `encoding="utf-8"`.
