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
  - Migrado: equipo 3 (13 oct 2026). La copia de trabajo estaba en OneDrive, pero en
    `Documentos\PlatformIO\Projects\Chispas-EA4ilN` (no en `mrmorse`, que aquí no existe); limpia y al día. Clon en
    `C:\proyectos` con "Equipo listo", los dos entornos compilados (RAM 16,8 %, flash 36,8 %) y la placa en COM16.
    Memoria de Claude Code copiada a `c--proyectos-CHISPAS-EA4ILN/memory`, junto con `usuario-aprendiendo-cw` de la
    ruta aún más antigua `...-Projects-Mr--Morse-EA4ilN`. Este equipo tenía también espressif32 7.1.3 en
    `platforms/espressif32` y la 6.13.0 en `platforms/espressif32@6.13.0`: `comprobar_equipo.sh` ahora mira las dos.
    `gh` instalado el 13 oct 2026 (v2.102.0, winget) con sesión de ea4iln-debug (HTTPS, llavero de Windows); hay que
    reabrir VS Code para que sus terminales lo encuentren en el PATH. Settings Sync activado (cuenta ea4iln-debug).
  - Migrado: equipo 2 (13 oct 2026). No tenía OneDrive: el clon de trabajo estaba en
    `Documents\CHISPAS-EA4ILN\CHISPAS-EA4ILN` (limpio y al día); `Documents\mrmorse` no es git (solo un proyecto
    KiCad vacío del 5 oct). Clon nuevo en `C:\proyectos` con "Equipo listo" y los dos entornos compilados (RAM 16,8 %,
    flash 36,8 %). Memoria de Claude Code copiada de la ruta antigua a `c--proyectos-CHISPAS-EA4ILN/memory`.
    Settings Sync activado (cuenta GitHub; primera sincronización el 13 oct 2026 a las 14:31; Settings, Keyboard
    Shortcuts, Snippets, Extensions y Profiles, sin UI State, igual que el equipo 1). El driver CP210x está instalado.
  - **Pasos en cada equipo que falte:** 1) en la carpeta de OneDrive, `git status` y `git log origin/main..` para ver
    si hay algo sin subir; si lo hay, commit y `git push`. 2) `git clone https://github.com/ea4iln-debug/CHISPAS-EA4ILN.git
    C:\proyectos\CHISPAS-EA4ILN`. 3) Abrir esa carpeta en VS Code (PlatformIO descarga las librerías) y ejecutar
    `./comprobar_equipo.sh --compilar` hasta que diga "Equipo listo". 4) Memoria de Claude Code: no viaja sola
    (está en `~/.claude/projects/<ruta>/memory`, por ruta de carpeta); mirar si hay algo en la de la ruta antigua
    (`c--Users-<usuario>-OneDrive-Documentos-mrmorse-Mr--Morse-EA4ILN/memory`) y pasar lo útil a NOTAS o CLAUDE.md
    (en el equipo 1 estaba vacía). Lo mismo con `.claude/settings.local.json` de la carpeta antigua: lo que valga
    para todos, a `.claude/settings.json`. 5) `gh`: ya está en los equipos 1 y 3; en el 2 comprobar la sesión. Si falta,
    `winget install GitHub.cli` y `gh auth login` si se van a publicar releases desde él. 6) Settings Sync de VS Code
    con la cuenta ea4iln-debug y "Merge". 7) Trabajar ya solo ahí; anotar aquí el equipo migrado.
  - `comprobar_equipo.sh` (13 oct 2026): carpeta fuera de OneDrive, remoto y usuario de git, al día con
    `origin/main`, copia antigua sin nada pendiente, PlatformIO con espressif32 6.13.0, framework y esptool, pyserial,
    `gh` con sesión, Edge o Chrome, placa CP210x; con `--compilar`, además `./compilar.sh`. OK / AVISO / FALTA; el
    código de salida es el número de FALTA. Probado en el equipo 1: listo (sin placa conectada).
  - **Finales de línea** (13 oct 2026): `.gitattributes` con `* text=auto` (además de `*.sh eol=lf`), para que el
    repositorio guarde LF sea cual sea el `core.autocrlf` de cada equipo (en el equipo 1 es `true`, del instalador de
    Git). `git add --renormalize .` no cambió nada: ya estaba todo en LF.
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
  **Primera release completa con `publicar.sh`: v1.07b, 14 oct 2026** (los tres pasos sin problemas). v1.08b y v1.09b, 14 oct 2026: igual. **v1.10 (primera final, no prerelease), 15 oct 2026**, con `publicar.sh` sin
  problemas: WIFI DATOS MANUAL/AUTO, reinicio de la OTA sin avance; **última con binario `sin-encoder`** (desde la
  1.11 no se compila ni se publica). https://github.com/ea4iln-debug/CHISPAS-EA4ILN/releases/tag/v1.10
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
  volver a poner el locator de la placa y apagar la depuración. 14 oct 2026 (placa 1, v1.07b + parques): capturas
  nuevas de CONFIG (tres páginas: ID–HORA, RELOJ–SALVAPANTALLAS, APAGADO–DEPURACION; FABRICA queda fuera), PONER
  HORA, STATUS > GPS (sin fijo, en interior: así no salen coordenadas), menú POTA y VER PARQUES (desde IN80dk). Con
  GPS el locator va en AUTO: para las capturas se pasa a MANUAL con IN80dk y luego a AUTO (hasta el siguiente fijo
  muestra "GPS IN80dk"). Las órdenes serie se pierden a veces si van seguidas: dejar ~0,7 s entre teclas.

## Estado actual (12 oct 2026)
- **MAC de cada placa** (las dos tienen el mismo CP2102; `pio run -t upload` imprime `MAC: ...`; la skill `placa`
  la usa para elegir el entorno). **Nombres cambiados el 15 oct 2026** (todo lo anterior a esa fecha en estas notas
  usa los viejos, al revés): **placa 1 = placa perforada**, `3c:0f:02:ee:08:c0` (la antigua "placa 2", desnuda y
  sin encoder hasta el 15 oct); **placa 2 = protoboard**, `3c:0f:02:ee:50:74` (la antigua "placa 1", con la OLED
  defectuosa). Las dos con encoder, `heltec_wifi_lora_32_V3`. Por OTA cada placa baja el `.bin` de su entorno (el `-sin-encoder` solo
  si lleva `SIN_ENCODER`), así que la placa 2 ya seguirá con el de encoder.
- Firmware: `src/main.cpp` **v1.09b** (el reset de fábrica borra también `/parques.bin`; release del 14 oct 2026,
  probado antes por USB en la placa 2; instalada por OTA en las dos placas el 14 oct; en la placa 1 la primera
  descarga se colgó en el 8 % y la segunda fue bien, ver "Pendiente"). v1.08b (parques POTA cercanos, POTA > VER PARQUES; release del 14 oct 2026 con
  `publicar.sh`, sin cambios de código respecto a lo probado en la placa 1; instalada por OTA en las dos placas el
  14 oct; después la placa 2 lleva por USB el borrado de los parques en el reset de fábrica, sin subir versión). v1.07b (GPS,
  CONFIG > HORA UTC/LOCAL y RELOJ; release del 14 oct 2026, instalada por OTA en la placa 2). v1.06b (opción SALVAPANTALLAS, CONFIG por bloques y expresiones de radioaficionado;
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
  desde el 14 oct 2026 con la lista de parques POTA en LittleFS) sin usar. `[DIAG]` por serie cada minuto (v0.7m): heap libre ~317 KB con el WiFi
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
- **WIFI DATOS: MANUAL / AUTO** (15 oct 2026, sin versión nueva; probado en la placa 2, en la placa perforada).
  Motivo: con la placa perforada el usuario notó que el encoder "se saltaba pasos, como si la placa se congelara
  unos segundos". Por serie coincidía con `[WIFI] POTA: N SPOTS CW`: `descargarUrl()` (HTTPS) bloquea el `loop()`
  ~3 s; el encoder va por interrupción y no pierde pasos, pero se aplican de golpe al acabar. En POTA pasaba cada
  2 min y además el WiFi se encendía solo con pasar por PROP o POTA. Descartado (por ahora) mover las descargas a
  otra tarea: datos compartidos con la pantalla, salir a mitad, LittleFS de los parques.
  Ahora, CONF > CONFIG > **WIFI DATOS** (`wifiAuto`, NVS `chispas`/`wifiauto`, por defecto `false` = MANUAL):
  - **MANUAL:** entrar en PROP, POTA o SOL no enciende nada. ACTUALIZAR (y REINTENTAR) llama a
    `pedirActualizacionWifi()` (`forzarActualizacion` + `wifiPorPeticion`); al conectar se descarga, y en POTA
    también la lista de parques si toca y la comprobación diaria de la OTA; luego, si la hora no viene del GPS ni
    del NTP (o en SOL siempre), se espera al NTP hasta `WIFI_ESPERA_NTP_MS` (8 s; `ntpRecibido` lo pone
    `horaSincronizadaNtp()`), y `terminarPeticionWifi()` apaga el WiFi. Desde el portal de PROP/POTA/SOL también
    cuenta como petición. Cambiar de pantalla la olvida.
  - **AUTO:** como antes (encender al entrar, refresco de POTA 2 min y de PROP 60 min, apagar al salir).
  - **Aviso a pantalla completa** (`avisoWifiVisible()` / `dibujarAvisoWifi()`): título del menú y
    `CONECTANDO WIFI...` (tras ACTUALIZAR), `DESCARGANDO...`, `BAJANDO PARQUES...`, `BUSCANDO FW NUEVO...` (en los dos
    modos; `empezarDescarga()` lo dibuja antes de bloquear) o `SINCRONIZANDO HORA...` (SOL). Mientras conecta o
    espera la hora, `> cancelar` en el pie: atrás o pulsar → `terminarPeticionWifi()`.
  - Páginas sin datos en MANUAL: `ACTUALIZAR EN EL MENU` (SOL sin hora: `ACTUALIZAR O WEBCONF`). Pie de VER SPOTS
    con la antigüedad (`AHORA`, `12MIN`, `3H`; `spotsPotaOkMs`) en lugar de "SPOTS".
  **Probado (placa 2, 15 oct 2026):** pasar por PROP/POTA sin encender el WiFi; POTA > ACTUALIZAR 9 s en total
  (conectar 3 s, 9 spots, comprobación de la OTA 4 s, apagado); PROP > ACTUALIZAR 6 s; cancelar mientras conecta;
  AUTO conecta al entrar en POTA. Avisos y pie de antigüedad vistos por el usuario. SOL > ACTUALIZAR en MANUAL,
  correcto (probado por el usuario). RAM 17,1 %, flash 38,6 %. (Probado en la placa perforada: "placa 2" con los
  nombres de entonces, "placa 1" con los nuevos.)
- **Parques POTA cercanos** (14 oct 2026, sin versión nueva; `src/parts/parques.inc`; **probado en la placa 1**).
  Menú de POTA (título ` POTA `) con tres opciones: `< VER SPOTS >`, `< VER PARQUES >` y `< ACTUALIZAR >` (sin
  conexión, la primera y la última pasan a ABRIR WEBCONF y REINTENTAR; VER PARQUES no cambia: la lista está en la
  flash). La línea de estado del menú no cabe y se quita solo en POTA (`dibujarMenuDatos(..., opMedia)`; foco 3 =
  barra, `OPCION_BARRA_SPOTS`). **Datos:** `api.pota.app/program/parks/ES` (2497 parques activos, JSON de ~795 KB;
  API pública sin clave y sin condiciones publicadas, igual que la de spots: se consulta lo mínimo). Se descarga sola
  al conectar en POTA, después de los spots, si no hay lista o tiene más de 30 días (si falla, reintento a la hora);
  ACTUALIZAR solo refresca los spots. Se lee parque a parque del flujo (`useHTTP10`, `find("[")`, `deserializeJson`
  con filtro y `findUntil(",", "]")`) y se guarda en **LittleFS** (partición `spiffs` de 1,5 MB, montada la primera
  vez que hace falta) en `/parques.bin`: cabecera (mágico `PAR2`, fecha, cantidad) y registros de 32 bytes
  (número, lat/lon × 1e5, nombre de 21 caracteres en ASCII y abreviado: `Natura 2000` → `N2000`, `National Park` →
  `NP`...); unos 80 KB. Se escribe en `/parques.tmp` y solo sustituye a la lista si llegan 1000 parques o más.
  **Búsqueda** (sin conexión): los 20 más cercanos al último fijo del GPS o, sin él, al centro del locator
  (equirectangular; rumbo en 8 puntos, O = oeste). **Páginas:** 2 parques por página, `ES-1078      4.5km E` y el
  nombre debajo; pie `1/10 IM68mu  >volver` (o `GPS`). Durante la descarga, `DESCARGANDO PARQUES` y la cuenta.
  Probado: descarga en 8–17 s con WiFi de casa, 2497 parques; 20 cercanos desde el locator IM68mu (el más cercano
  a 4,5 km); ACTUALIZAR sigue bajando spots. **Sin probar:** búsqueda desde un fijo del GPS (dentro de casa no lo
  tenía), el refresco a los 30 días y la descarga fallida. Flash +49 KB (38,5 %), RAM +0,7 KB.
  **Reset de fábrica** (14 oct 2026, después de la v1.08b): `resetFabrica()` monta LittleFS y borra también
  `/parques.bin` y `/parques.tmp` (antes los dejaba). Probado en la placa 2 por USB: tras el reset, sin lista; al
  configurar el WiFi y entrar en POTA la vuelve a bajar (2497 parques en 8 s).
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
  cambiar de altitud (subir a un monte) la presión baja y puede dar un falso aviso; con el GPS, el historial va a nivel del mar (ver GPS).
  Página **AMBIENTE** (4/4 de SOL, solo con sensor, `paginasSol()`; no necesita hora ni locator). **CONFIG > ALTITUD**
  (`OC_ALTITUD`): muestra la altitud ("?" sin calibrar); pulsar, girar hasta la altitud real y pulsar calcula el QNH
  (`qnhParaAltitud()`, NVS `qnh` en `estacion`; sin él, 1013,25). STATUS página 6 **SENSOR**. Macro **{WX}**
  (`textoWx()`): "WX 18C", "WX MINUS 3C" bajo cero (el guion no está en la tabla Morse), vacío sin sensor; en la
  rueda del editor, en el portal y en `trxLimpiarTexto()`. Por serie cada 10 min: `[BME] T ... P ... TEND ...`.
  La temperatura sale alta por el calor de la placa (en la primera lectura, 25,8 °C dentro de casa).
- **GPS NEO-6M** (13 oct 2026, sin versión nueva; `src/parts/gps.inc`; **probado en la placa 1 el 13 oct 2026**):
  UART1 a 9600 bd (47 recibe su TX, 39 va a su RX), siempre alimentado. Estados `GPS_DETECTANDO` → `AUSENTE` (sin
  NMEA con checksum bueno en 6 s: `Serial1.end()` y todo como sin GPS) o `BUSCANDO` / `CON_FIJO` / `DORMIDO`.
  Duerme con `UBX-RXM-PMREQ` (backup) y se despierta con 16 × 0xFF; si sigue mandando NMEA 1,5 s después del PMREQ,
  lo repite (hasta 3). **Ventanas:** al arrancar o despertar Chispas (tope 5 min), cada 10 min desde el final de la
  anterior (tope 60 s), mientras se ve STATUS > GPS y con ACTUALIZAR de SOL. En cada ventana: hora con la primera RMC
  `A`; 30 altitudes (solo fijos con ≥ 5 satélites y HDOP < 3); al cerrarla (30 muestras o tope), el locator si está
  en AUTO (NVS solo si cambia) y, con ≥ 5 muestras, la **altitud de referencia** `gpsAltRef`, que solo cambia si la
  media se aparta más de 30 m (el NEO-6M varía ±15 m, casi 2 hPa: daría falsos avisos de tormenta; razonado) y el
  **QNH** recalibrado con ella (NVS solo cuando cambia la referencia). El **historial de presión** guarda la presión
  reducida a nivel del mar con `gpsAltRef` (`bmeHistNivelMar`); al pasar de un tipo de muestra al otro, empieza de
  nuevo. Antes del deep sleep (`dormirGpsAntesDeSueno()`, dentro del 1,5 s del mensaje): PMREQ comprobando que se
  calla, y GPIO39 a GPIO en ALTO **sin** `Serial1.end()` (un flanco lo despertaría) y retenido con `gpio_hold_en`;
  se libera en `iniciarGps()`. Buffer de RX de 1 KB (las descargas de PROP y POTA bloquean el loop 1–3 s).
  **CONFIG > LOCATOR** con GPS: pulsar, girar entre `AUTO (GPS)` y `MANUAL` y pulsar (MANUAL abre el editor); en la
  lista sale `GPS IM68mu` en AUTO (NVS `locgps` en `estacion`, por defecto sí). **STATUS > GPS** (página 7/7): estado y
  satélites, lat/lon, altitud y HDOP, locator calculado y antigüedad del último fijo; sin módulo, "GPS NO DETECTADO".
  Probado: detección en menos de 1 s desde el arranque; dentro de casa, sin fijo en la ventana de arranque y a dormir
  a los 5 min (bien); junto a la ventana, el usuario vio CON FIJO en STATUS > GPS, RELOJ en GPS, LOCATOR correcto y
  QNH CALIBRADO en STATUS > SENSOR. **Falta:** medir su consumo dormido en µA y comprobar que no se despierta durante
  el deep sleep de Chispas.
- **Hora: fuente y CONFIG > HORA / RELOJ** (13 oct 2026, **probado en la placa 1**). `fuenteHora` (`RTC_DATA_ATTR`,
  sigue tras el deep sleep): `SIN HORA` < `MANUAL` < `WIFI` < `GPS`. `ponerHora()` solo deja cambiar la hora a una
  fuente igual o más exacta que la actual, salvo la manual (CONFIG > RELOJ o la hora del webconf), que se aplica
  siempre y luego la sustituyen el WiFi o el GPS. El NTP avisa con `sntp_set_time_sync_notification_cb`
  (→ `WIFI`); con hora del GPS se para (`sntp_stop()`) y `iniciarNtp()` no lo arranca. **HORA** (`OC_HORA`):
  `UTC` / `LOCAL` en la cabecera (la misma `horaUtcCabecera` que la casilla del webconf). **RELOJ** (`OC_RELOJ`):
  muestra la fuente; pulsar abre **PONER HORA** (`CONF_RELOJ`): `14:05  13/10/2026`, girar cambia el campo en vídeo
  inverso (da la vuelta), pulsar pasa al siguiente y en el año la pone; larga, ya; PRG, sale sin cambiarla. Se escribe en
  UTC o local según HORA; sin hora parte de las 12:00 del día de compilación (`__DATE__`). Probado con órdenes serie:
  hora manual (cabecera 23:44, RELOJ `MANUAL`), HORA UTC (`21:44Z`) y vuelta a LOCAL; el paso a `GPS` lo vio el usuario.
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
  (12 oct 2026): bocadillo, antena y Morse; auriculares vista el 13 oct 2026; tormenta sin ver aún. Con la cara a la vista, `actualizarPantalla()` no dibuja.
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
  **Diagnóstico del 14 oct 2026: defecto de hardware de la Heltec de la placa 1.** Método: 20 resets por RTS del
  CP210x (pulso de 50 ms, uno cada 6–7 s; script de un solo uso, sin guardar) y el usuario cuenta las pantallas negras.
  Placa 1 montada: firmware normal ~2–4/20; cortar Vext 1 s al arrancar (SDA/SCL como entradas, RST en BAJO) 3/20;
  OLED iniciada después de `radio.begin()` 4/20; reset de 1 s 7/20 (la pista de "mantener RST un poco más" no se
  confirmó con RTS); repetir al final de `setup()` lo de la orden `v` (Vext 500 ms, reset, `begin`) **11/20**: encendía
  bien y se apagaba en el segundo encendido; igual sin GPS 9/20; poner a cero la GDDRAM antes de `begin` 6/20.
  **Placa 2: 0/20 con el firmware normal y 0/20 con el doble encendido.** Placa 1 desnuda (fuera de la protoboard,
  solo USB) con el doble encendido: **4/20**. Conclusión: cada encendido de la OLED de esa placa tiene bastante
  probabilidad de dejar el panel negro (posible bomba de carga o condensadores justos); el montaje lo empeora (carga en
  el 3,3 V) pero no es la causa, y no depende del firmware. Todos los cambios de prueba se descartaron (código como
  estaba). Para la placa perforada: usar la Heltec de la placa 2 o una nueva.
- **Dormir** (v0.7n, probado en la placa 2: tres ciclos; y en la placa 1 el 12 oct 2026): PRG largo, o `apagadoAutoMin` sin tocar nada (v0.7o: se
  elige en CONF → `apagado`: nunca, 5, 10, 15, 30, 60 o 120 min; por defecto 30; NVS `apag`), salvo en CW, LoRa y
  TRX (y sus subpantallas) o con el portal abierto → `dormirChispas()`: aviso 1,5 s, OLED,
  WiFi, LoRa y LED apagados; KEY en BAJO, Vext apagada y divisor de batería desactivado, retenidos con `gpio_hold_en`
  + `gpio_deep_sleep_hold_en` (se liberan al principio de `setup()`); despierta con ext0 en GPIO0 a nivel bajo y, con encoder, con ext1 (`ESP_EXT1_WAKEUP_ANY_LOW`) en GPIO20, el eje,
  con su pull-up del RTC (13 oct 2026; el pin se devuelve a GPIO con `rtc_gpio_deinit` en `setup()`). El serie dice
  "(despierta con PRG)" o "(despierta con el encoder)".
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
  imagen, que dependen de él. Código y datos, idénticos. **Probado en la placa 1 el 13 oct 2026** (carga por USB desde
  el equipo 3): NVS conservada, las 8 pantallas recorridas con órdenes serie, WiFi y NTP en PROP, sin reinicios; el
  usuario confirmó encoder, tono por el altavoz y LED. Placa 2 el mismo día (USB, `heltec_sin_encoder`): arranque en
  modo sin encoder, las 8 pantallas, WiFi, NTP y 20 spots POTA, sin reinicios; el usuario confirmó el manejo con PRG. Con el salvapantallas activo (aunque se vea la pantalla de
  antes), la primera orden serie solo lo quita: no es un evento perdido.
- **Orden y contenido** (ver tabla en `CLAUDE.md`): `pines`, `morse`, `globales`, `audio`, `motor_cw`, `keyer`,
  `estadisticas`, `echo`, `bme280`, `memoria_nvs`, `bateria`, `oled_comun`, `lora`, `decodificador`, `trx`, `wifi`,
  `ota`, `datos_web`, `gps` (13 oct 2026), `parques` (14 oct 2026), `pant_inicio_cw`, `pant_lora_trx`, `pant_prop_pota`, `sol`, `conf`, `estados_cw`, `cara`,
  `entrada`, `eventos`. Rarezas heredadas del orden original: `procesarRecepcionLoRa`, `loraEscuchar` y
  `salirModoLoRa` están al final de `trx.inc`; `actualizarPantalla()` (el reparto del dibujo) está al final de
  `conf.inc`; el dibujo de cada pantalla está en `pant_*.inc`, lejos de su lógica.
- **Siguiente fase posible** (no hecha): agrupar por pantalla con prototipos, o los módulos `.h`/`.cpp` de
  `ROADMAP.md`. Ahí el `.bin` ya cambiaría y habría que probar en placa.
- `git log` de los `.inc` empieza en el corte; la historia anterior está en `git log -- src/main.cpp`.

## Pendiente
1. **Pruebas a mano** (11 oct 2026): hechas en la placa 1 (listas y menús, aceleración, corrección total del Koch,
   Echo por lección) y en la placa 2 (OTA a la v1.01b; menús, listas, edición con vuelta, páginas y práctica sin
   encoder). X6200 con KEY probada el 12 oct 2026. OLED negra de la placa 1: **defecto de hardware de esa Heltec** (14 oct 2026:
   falla también desnuda y la placa 2 no falla con ningún firmware; ver "OLED negra tras reset corto").
2. **Montaje** (orden de la hoja 7 del esquema; hecho: raíles, encoder, palas, tono con amplificador, altavoz y jack,
   salida KEY con PC817 y minijack, BME280, GPS); GPS montado y probado el 13 oct 2026 con `prueba_gps` (ver la tabla de pines: GPIO39 y 47) e integrado en el firmware el mismo día (ver Funciones); BME280 montado y probado (v1.03b). Entrada del decodificador en GPIO6:
   **montada y probada el 11 oct 2026** (ver DECODIFICAR): comprobado con audio Morse real, letras y separación de palabras bien; la batería se sigue leyendo bien después de usar DECODIFICAR o QSO (probado en la placa 2 el 12 oct 2026). Falta comprar: divisor minijack 1→2. El buzzer magnético de 16 Ω no se usa.
3. **Ideas aparcadas** (para repensar más adelante):
   - **Estabilidad comprobada** (placa 2, v1.03b, noche del 12 oct 2026: 8 h en POTA, 233 consultas HTTPS a pota.app, una
     con HTTP -11, tiempo de lectura, que se recuperó sola): heap libre 248,8–249,1 KB, mínimo 165,5 → 162,7 KB en la
     primera hora y luego fijo, mayor bloque 180 212 B fijo (sin fragmentación), pila libre loop 2568, motorCW 3292,
     keyer 2304 sin cambios. Pendiente solo la pila del motor CW tras Duo (2016 bytes libres de 4096, medida antigua). Deep sleep medido por el usuario el 11 oct 2026: **0,00 A** (por debajo de la
     resolución del aparato) y después en µA (12 oct 2026): **despreciable**.
   - **Detectar USB y carga: aparcado (14 oct 2026).** La V3 no lo permite por software; "cargando" tampoco se puede
     leer (la salida del cargador solo va a su LED). Probado en la placa 1 con un divisor del pin 5V (J2-2) a GPIO33
     (J2-12): el pin 5V **no queda a 0 V con batería sola**: 1,3 V en vacío (polímetro) y unos 11 kΩ de impedancia
     (origen desconocido sin el esquema de la V3). Con 47k arriba / 68k abajo: **2,92 V con USB y 0,7 V sin USB** en
     el punto medio. Con una lectura digital de prueba (sin commit) `USB` aparecía y desaparecía bien en STATUS, pero
     se aparcó: 0,7 V está a 0,12 V del BAJO garantizado (0,825 V), el divisor podría gastar ~10 µA de batería sin USB
     (sin medir en deep sleep), la placa 2 no lo tiene y la ganancia es poca (rayo en la cabecera, no apagarse con USB).
     Si se retoma: **GPIO3 con ADC** y umbral por software (~1,8 V), que además despierta del deep sleep (pero gasta
     la única reserva con ADC y RTC); medir antes el deep sleep con el divisor. La reserva R11/R12 de la placa
     perforada queda con 47k/68k. (Nota del montaje: en la protoboard de la placa 1 el carril de masa está partido
     en el centro.)
4. QSO LIBRE: probar en los dos sentidos cuando la placa 2 tenga palas. A estudiar, no prioritarios: audio I2S y GPS ATGM336H en lugar del NEO-6M (ver `ROADMAP.md`).
5. **OTA: timeout de descarga sin avance** — hecho el 15 oct 2026 (compila; **sin probar**: necesita una release
   nueva). `instalarActualizacion()` arranca un `esp_timer` de 1 s (`vigilarOta()`); `onProgress` apunta
   `otaUltimoAvanceMs` y, si pasan `OTA_SIN_AVANCE_MS` (30 s) sin avance, `esp_restart()`: la partición a medias no
   se activa y arranca la versión de antes (se descartó `cliente.stop()` desde otra tarea: no es seguro con TLS).
   Antecedente: 14 oct 2026: la placa 1 se quedó parada en
   el 8 % al actualizar a la v1.09b (la placa 2, bien con la misma release); con RST arrancó en la v1.08b y al
   repetir actualizó. Causa razonada (core 2.0.17): si la conexión se corta, `Update.writeStream()` reintenta
   `readBytes()` hasta 300 veces y cada lectura espera el timeout del cliente (8 s de `HTTPUpdate`), así que
   puede tardar muchos minutos en dar error y PRG no responde (bucle bloqueante). Idea: vigilar el avance (p. ej.
   un temporizador que mire `otaProgreso`) y, si pasan 20–30 s sin avanzar, cortar con `cliente.stop()` para que
   salga "ERROR"; o bajar el timeout del cliente y de `HTTPUpdate`. Comprobar en el core qué corta antes.
6. **Batería de la placa 1 (perforada)** (15 oct 2026): con el cargador ya parado marca 90 % (unos 4,10 V en la
   curva). Razonado, sin medir: tras cortar la carga la celda baja a 4,10–4,15 V, y el factor 4,9 se calibró en un
   solo punto con la Heltec que entonces era la placa 1 (ahora la protoboard, placa 2); la perforada lleva la otra
   Heltec, y un 1 % de tolerancia del divisor son ~0,04 V (4–5 puntos). Prueba: comparar `BAT x.xxV` de STATUS >
   SISTEMA con el polímetro en el mismo momento. Si difiere, factor por placa según la MAC.
   Sin USB, después de la carga, marca **4,08 V** (lo esperado en reposo: 4,15–4,18 V). Puede leer bajo (factor ~1,5–2 %
   corto) o la celda quedarse ahí (corte pronto del cargador o celda gastada): hace falta el polímetro. Si da ~4,16 V,
   el factor de esta placa sería 4,9 × 4,16 / 4,08 ≈ 5,0.

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
| 19 / 20 / 48 | Encoder KY-040 CLK / SW / DT. **Montado y probado** (19/20 son USB D−/D+ del S3 pero funcionan como entradas). El 13 oct 2026 se cambiaron SW (antes 48) y DT (antes 20): SW en un pin RTC despierta del deep sleep (ext1, pull-up del RTC; PRG sigue en ext0) y GPIO3, el único libre con ADC y RTC, queda de reserva. La ISR lee DT de `GPIO_IN1_REG`. En la placa perforada, P1 va en el orden del KY-040 (CLK, DT, SW, +, GND; cable recto) desde el 15 oct 2026; la placa 1 se montó antes con CLK, SW, DT (cable con DT y SW cruzados). **Probado en la placa 1** (13 oct 2026): giro con aceleración, pulsación corta y larga, y despertar con el eje y con PRG |
| 47 / 39 | GPS NEO-6M (GY-GPS6MV2, firmware 7.03, hw 00040007) en UART1 a 9600 bd: 47 recibe su TX (NMEA), 39 va a su RX (UBX). VCC a 3V3, siempre alimentado. **Montado y probado el 13 oct 2026** en la placa 1 con `pruebas/prueba_gps.cpp` (`pio run -e prueba_gps -t upload`; órdenes serie y PRG para dormir/despertar, estado en la OLED): `UBX-RXM-PMREQ` (backup, sin límite) lo duerme (0 bytes) y una ráfaga de 0xFF por su RX lo despierta; con fijo previo vuelve a 6 satélites en segundos. Consumo con medidor USB (resolución 0,01 A, toda la placa, sin batería): 0,13 A buscando, 0,09–0,13 A con fijo (6 satélites), 0,07 A con el GPS dormido (= placa sin GPS): el GPS gasta ~60 mA buscando, 20–60 mA con fijo y, dormido, menos de lo que resuelve el medidor. **Falta medirlo dormido en µA/mA** (polímetro en serie con su VCC) antes de fiarse en deep sleep. Enable con transistor (AO3401A en GPIO3) **descartado el 13 oct 2026** |
| 3 (libre) | El único libre con ADC1 y RTC: de reserva |
| 41 / 42 | BME280: I2C 0x76 en Wire1, SDA 41 (J3-8), SCL 42 (J3-7). **Montado y probado** (12 oct 2026, v1.03b) |

Posición física (comprobada con fotos de la placa; pin 1 = GND, pad cuadrado, el más cercano al USB):
- J3: 18 GPIO7 tono · 17 GPIO6 decod. · 16 GPIO5 DAH · 15 GPIO4 DIT · 14 GPIO3 (libre) · 13 GPIO2 KEY · 12 GPIO1 (VBAT, no)
  · 11 GPIO38 SD del amplificador · 10 GPIO39 TX al GPS · 9 GPIO40 (libre) · 8/7 GPIO41/42 · 6/5 GPIO45/46 (no) · 4 GPIO37 (no) · 3/2 **3V3** · 1 **GND**.
- J2: 18/17 GPIO19/20 encoder CLK/SW · 16 GPIO21 (OLED_RST) · 15 GPIO26 (flash, no) · 14 GPIO48 encoder DT · 13 GPIO47 GPS RX
  · 12/11 GPIO33/34 · 10 GPIO35 (LED) · 9 GPIO36 (Vext) · 8 GPIO0 (PRG) · 7 RST · 6/5 GPIO43/44 (UART0) · 4/3 Ve · 2 5V · 1 GND.
- `Ve` es Vext (conmutada por GPIO36, alimenta la OLED): los módulos van a 3V3 de J3. `5V` solo tiene tensión con USB.
- GPIO17/18 (I2C de la OLED) no salen a los conectores, pero hay dos **pads de prueba "17" y "18" por detrás**
  (mismo bus; 0x76 no choca con la OLED 0x3C): solo si faltan pines, soldando hilo fino.
- GPIO3 es pin de arranque (JTAG) solo con un eFuse grabado (de fábrica no).
- Pines libres: 3, 33, 34 y 40. No usar: 0, 1, 8–14, 17/18/21 (OLED), 26–32, 35, 36, 37, 43/44, 45/46 (arranque).

Esquema de conexiones: `docs/esquema_protoboard.pdf` (7 hojas: vista general, pines, entradas, tono y amplificador,
KEY y alimentación, decodificador, componentes y montaje), generado con `docs/gen_esquema.py` (HTML/SVG; el PDF se
imprime con Edge sin interfaz: `msedge --headless --no-pdf-header-footer --print-to-pdf=... archivo.html`; en el segundo equipo no hay Edge: lo mismo
con `C:\Program Files\Google\Chrome\Application\chrome.exe`). El script recibe la ruta del HTML como argumento. Las hojas
tienen altura fija y ocultan lo que sobra: tras cambios grandes, revisar el PDF (no basta con el HTML en pantalla). Si cambia algún pin, actualizar el script y regenerar el PDF.

## Placa perforada (12 oct 2026; montada y probada el 15 oct 2026)
**15 oct 2026: montada y funcionando.** El usuario hizo las comprobaciones de la página 4 del PDF y pinchó la Heltec
de la placa 2: arrancó bien. Se le cargó por USB la v1.09b del entorno con encoder (`heltec_wifi_lora_32_V3`; los
pines del encoder llevan pull-up interno, así que sin el encoder montado no se dispara nada) y se fueron
conectando los módulos uno a uno, todos bien: encoder (giro, eje, PRG atrás, despertar con el eje en GPIO20),
amplificador + altavoz + RV1 + auriculares (el altavoz calla al meter la clavija), palas (TX LIBRE), BME280
(STATUS > SENSOR), KEY (polímetro en P6 con el tune de TRX > OPERAR y con las palas), decodificador (audio Morse en
J3, TRX > DECODIFICAR) y GPS. Notas del montaje: **sin el jack de auriculares en P3 el amplificador no suena** (A+
recibe la señal por el contacto T–TN del jack; para probar sin jack, puente T–TN). RV1: patas 1-2-3 de izquierda a
derecha con el eje hacia uno y las patas abajo (3 a R1, 2 a C1, 1 a masa).
**Montaje completo (15 oct 2026, más tarde):** con todos los componentes, jack de auriculares incluido, todo funciona:
el circuito de la placa perforada está bien.
**Encoder con pasos en falso (15 oct 2026, placa 1, v1.10):** traza temporal en `isrEncoder` (cada interrupción con
su hora y estado CLK·DT, volcada por serie como `[ENC]`; no está en el repositorio). Primera captura: 164 de 194
recorridos eran `3 >1 >3` (CLK baja y sube, **DT en GPIO48 siempre a 1**): sin DT no hay sentido y no cuenta paso;
casi todo el giro rápido se perdía. El usuario movió los conectores Dupont del encoder y en la segunda captura los
135 recorridos dieron su paso (122 limpios con ±4; 7 con un `SALTO` por rebote, contados con ±2; 3 rebotes de CLK
sueltos antes de un paso bueno). Causa: **mal contacto en el conector de DT**, no el firmware. El firmware estaba
bien: la tabla filtra los rebotes y no se pierden eventos (al girar rápido el `loop()` va unos pocos pasos por detrás,
~50 ms por paso con la traza, y los entrega todos). Si vuelve a pasar: revisar ese conector (o soldar los cables).
**Volvió a fallar** (mismo día): con los cables quietos, rachas de `3 >1 >0 >1 >3` (el pulso de DT acaba antes que el
de CLK) y `3 >1 >3`; a veces también CLK (`3 >2 >3`) y algún detente que se queda en 1 o en 0 en vez de en 3. Prueba
controlada, 10 clics lentos a cada lado: Inicio con el GPS dormido 20/20, STATUS > GPS (despierto) 19/20, TRX >
DECODIFICAR (ADC por DMA) 20/20: **ni el GPS ni el decodificador influyen** (la traza ve los pines en la ISR; un
retraso de software daría `SALTO`, no pulsos de 50–100 ms). Causa probable: contacto intermitente en el camino del
encoder (contactos del KY-040, Dupont, soldaduras de P1/J2 o la masa, que comparten CLK y DT). Sin resolver: queda
revisar soldaduras y continuidad (hoja de revisión hecha, no guardada en el repositorio) o probar el encoder de la placa 2.
**P1 de la placa 1, montada al revés del plano nuevo:** el plano tenía P1 en CLK, SW, DT y la tabla de la página 3
decía "cable recto" (mal); el usuario lo vio y la montó con **DT y SW cruzados en el cable**. Desde el 15 oct el
plano pone P1 en el orden del KY-040 (CLK, DT, SW, +, GND: U9, U10, U11) con cable recto: DT llega por T13–T10 a U10
y SW (GPIO20) va por el cable aislado 15 (S10 → U11). **La placa 1 sigue con el orden viejo**: su cable del encoder
no es recto.

Plano para montar el prototipo en una placa de pads a doble cara (pedidas de 7×9 cm, 24×30 agujeros; el plano ocupa
21×26, 53×66 mm): `docs/placa_perforada.pdf` (4 páginas: componentes, soldaduras con puentes y 12 cables numerados,
piezas y conectores, montaje), generado con `docs/gen_placa.py` (`python gen_placa.py salida.html` y luego Chrome
headless a PDF, como el esquema). El script comprueba que cada red queda unida, que nada se pisa y nada va bajo la
Heltec, **con y sin las reservas montadas**. Heltec en zócalos hembra con el USB en el borde; módulos y mandos en la
carcasa con cables Dupont a las tiras P1–P10. Reservas: trafo 600:600 (P10, quitar los dos jumpers), divisor de
detección de USB (R11/R12, cables A y B), enable del GPS (Q1/R8, quitar el cable 8) y C3 sobre las patas de D2.
Orden de pines de los módulos comprobado con fotos el 14 oct 2026: GPS GY-GPS6MV2 (VCC, RX, TX, GND) y BME280
GYBMEP (VIN, GND, SCL, SDA) coinciden con P9 y P8; el PAM8302A (WCMCU-8302) tiene 5 pines A+, A−, SD, Vin, Gnd y
P5 pasó de 4 pines (VIN, GND, SD, A+) a 5 en ese orden (A17–A21; A− y GND a masa por la placa, cable recto de 5
hilos; tiras macho: 32 pines). La orientación de la Heltec (`J3_IZQUIERDA`) quedó confirmada el 13 oct 2026. La reserva del enable del GPS (Q1/R8) se quitó del plano; el RX del GPS va con el cable 9 desde J3-10 (GPIO39).
14 oct 2026, soldando (zócalos y resistencias ya puestos): U1 estaba dibujado en espejo (pata 1 en E15 con la 2
debajo, en sentido horario visto desde arriba; un DIP va en antihorario). Corregido sin mover zócalos ni resistencias:
punto en **E16** (abajo a la derecha), pata 2 (cátodo) en E15, 3 (emisor) en B15, 4 (colector) en B16. KEY_A va de
E14 (R7) a E16 rodeando por D14–D16; la masa del cátodo sale de E15 por F15–F18 a E18; P6 pasa a ser (E, C) en
A15/A16 (solo cambia qué hilo va a la punta del jack J4).
C2 resultó ser un EPCOS de poliéster 1 µF 100 V de paso 15 mm (18 × 5 × 11 mm), no de 5 mm: va en vertical en
C8 (DEC_CUR, puente B8–C8) y C14 (BIAS, cable aislado 14 hasta D9), pegado por arriba al zócalo de U1.

## PCB
**Empezada el 15 oct 2026** en KiCad 10.0.7, proyecto `hardware/chispas.kicad_pro`. Objetivo: 5 placas en JLCPCB,
SMD montado por ellos (montaje económico, cara de arriba) y lo de agujero pasante soldado por el usuario, pedido en
LCSC con el mismo envío. Forma de trabajar: el usuario dibuja en la interfaz siguiendo pasos numerados; Claude revisa
los archivos con `kicad-cli` (netlist, ERC, DRC, PDF → PNG con pymupdf) y coloca con scripts del Python de KiCad
(`C:/Program Files/KiCad/10.0/bin/python.exe`, con el editor de placas **cerrado**). Al revisar, comparar la netlist
con la del último commit (una vez se borró un cable sin querer: PJ1 pin 5).

**Decisiones (15 oct):** Heltec enchufada en dos zócalos hembra 1×18 (C2905422); GPS y BME280 como módulos; KY-040
por cable; amplificador **PAM8302A integrado** (C113367, MSOP-8) con el circuito de la hoja de datos (10 µF + 1 µF en
VDD, 0,1 µF en serie en IN+ e IN−); jacks, volumen y trimmer soldados en el borde; trafo 600:600 como **dos puentes de
soldadura cerrados** (JP1 punta→DEC_IN, JP2 malla→GND) + 4 agujeros J6 (TJ, SJ, DEC_IN, GND); detección de USB
**no** incluida; tira de 6 pines con GPIO3, 33, 34, 40, 3V3, GND; encoder, GPS y BME280 con **JST-XH** con enganche
(B5B C157991, B4B C144395; el fallo del encoder de la placa 1 era un Dupont); altavoz con JST-PH 2 (C131337);
**10 nF a masa en DIT y DAH** (filtro de RF); 10 nF de DECOD a masa (el C3 opcional de la perforada).
Distribución: **detrás** KEY y decodificador (+ trimmer); **delante** palas, auriculares y volumen; **USB de la
Heltec en el borde izquierdo**, Heltec en horizontal (el usuario: la OLED se lee derecha con el USB a la izquierda).
Placa de 90 × 60 mm, 4 agujeros M3 a 4 mm de las esquinas.

**Piezas LCSC** (catálogo consultado el 15 oct): R 0805 Basic 10k C17414, 1k C17513, 220 C17557, 330 C17630;
C 0805 Basic 100n C49678, 1µ C28323, 10µ C15850, 10n C1710; 1N4148W C81598; PC817 SMD C66405 (Extended);
PAM8302AASCR C113367 (Extended). Para soldar: PJ-307 C2939173 (×4; 1 = S, 2 = R, 3 = RN, 4 = TN, 5 = T, comprobado
en la hoja de datos), potenciómetro ALPS RK097111080J C97444 (horizontal, rosca M7, eje 15 mm, curva log),
trimmer 3296W C118954. Campo de símbolo **`LCSC Part`** (lo lee Fabrication Toolkit); las piezas que suelda el
usuario, con "Excluir de la lista de materiales".

**Archivos:** `hardware/lib/` (librería `chispas`: piezas importadas con `easyeda2kicad` en `~/.venv-kicad`, que hay
que crear en cada equipo; `heltec.kicad_sym` + huella `Heltec_WiFi_LoRa_32_V3`: pads 1–18 = J3, 19–36 = J2, filas a
22,86 mm). `hardware/herramientas/`: `buscar_jlc.py` (catálogo de JLCPCB: Basic/Extended, existencias, precio),
`gen_heltec.py`, `tipos_pines.py` (corrige los tipos de pin de los símbolos importados; repetir tras importar),
`colocar.py` (posición de cada pieza y contorno). Se ignoran `hardware/.history/` (historial de KiCad 10) y backups.

**Estado (15 oct):** esquema completo (ERC 0 errores, 0 avisos); placa con todas las piezas colocadas por
`colocar.py` (DRC: solo conexiones sin rutar y serigrafía). **Siguiente:** el usuario revisa la colocación en 3D
(paso 9); luego rutado (propuesta: Freerouting + revisión de masa, alimentación del PAM y entrada del
decodificador), planos de masa, serigrafía, Gerber/BOM/CPL con Fabrication Toolkit y comprobar rotaciones en la
vista previa de JLCPCB. Pendiente de la caja: agujero para ajustar el trimmer.

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
