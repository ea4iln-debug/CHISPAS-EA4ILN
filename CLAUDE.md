# Chispas EA4ILN — contexto del proyecto

## Qué es
**Chispas** (antes "Mr. Morse"; "chispas" era el apodo del radiotelegrafista de a bordo): dispositivo portátil de CW
para radioaficionados (indicativo EA4ILN) sobre una **Heltec WiFi LoRa 32 V3** (ESP32-S3FN8, SX1262 a 868 MHz,
OLED 0,96", sin PSRAM, 8 MB de flash). Hoy: entrenador Koch estilo LCWO, keyer iámbico, modo Echo, práctica en pareja
por LoRa, propagación, spots POTA, sol y línea gris. A medio plazo: tono por buzzer, salida KEY, decodificador desde
el transceptor, GPS/locator. El usuario aún está aprendiendo CW (no sabe manipular todavía).

## Recordar al usuario al empezar la sesión
- Renombrar el repositorio en GitHub (Settings → Repository name, p. ej. `Chispas-EA4ILN`; hoy sigue siendo
  `ea4iln-debug/Mr.-Morse-EA4ILN`). Después: `git remote set-url origin <url nueva>`, quitar este aviso y, si quiere,
  renombrar la carpeta local con VS Code cerrado.

## Estado actual (10 oct 2026)
- Firmware: `src/main.cpp` **v0.7k**, cargada en la placa 1 (protoboard con encoder, palas y batería; COM16). La
  placa 2 (desnuda) sigue en v0.6n. Arduino + PlatformIO, env `heltec_wifi_lora_32_V3`, `platform = espressif32 @
  6.13.0` (core Arduino 2.0.17), RadioLib ^6.6, Adafruit GFX y SSD1306, WiFiManager 2.0.17, ArduinoJson 7.
  **Un solo `.cpp` en `src/`** (si hay dos, falla el enlazado). `default_envs` hace que `pio run` compile solo el
  firmware; programas de prueba aparte en `pruebas/` con sus entornos (`prueba_pines`, `prueba_bateria`).
- La versión está en `#define FW_VERSION` (y en `@version` de la cabecera): se ve en la pantalla 1 y por serie.
  Subirla en cada cambio que se cargue en placa.
- Pruebas con LED, pantalla y monitor serie; **aún no hay audio** (no proponer pruebas "a oído" hasta montar el buzzer).
- Pantallas (`PANTALLA_*`, recorrido con ant./sig.): 1 Inicio, 2 CW, 3 LoRa (Duo/QSO), 4 PROP (propagación),
  5 POTA, 6 Sol, 7 CONF. Cabecera: hora, nombre corto de la pantalla y batería (`PWR` sin batería).
- NVS: `mr_morse` (Koch, keyer, Echo y estadísticas; el nombre se mantiene para no perder lo guardado al renombrar el
  aparato), `mr_estacion` (indicativo, locator, hora UTC); WiFi en el NVS del ESP32. Borrar estadísticas solo borra
  estadísticas; el reset de fábrica lo borra todo.
- Recursos (v0.6m): flash 1,12 MB de 3,19 MB (app0); RAM estática 51,8 KB de 320 KB; app1 (OTA) y spiffs (1,5 MB,
  reservada para el CSV de POTA) sin usar.

## Manejo (encoder KY-040; probado)
- **Todo se hace con el encoder:** girar mueve el foco (`moverFocoAnillo()`), pulsar ejecuta, y pulsar sobre un
  valor lo edita girando (`editandoValor`, vídeo inverso, sin dar la vuelta). La larga del eje es solo un atajo.
- **PRG corto = atrás** (sale de la edición, sube un nivel; en las pantallas principales va a Inicio).
  **PRG largo libre**: reservado para dormir la placa (deep sleep, despertar con GPIO0; sin probar).
- Al cambiar de pantalla el foco queda en la flecha usada (`entrarEnPantalla(p, haciaAtras)`).
- Barra inferior: `ant.`/`sig.`; en PROP y POTA, la página ("1/2") en el centro y la opción "mas >"; en CONF,
  `« opc »` (opciones que solo salen al girar). Sin ayudas tipo `- : opc`.
- Entradas: encoder por interrupción en CLK/DT con tabla de cuadratura (`isrEncoder`, en IRAM, lee `GPIO_IN_REG`);
  PRG y pulsador del eje con `leerPulsador()` (antirrebote: el eje rebota al soltar). Eventos `BOTON_CORTA/LARGA` y
  `ENC_DERECHA/IZQUIERDA/PULSA/LARGA`, todos tratados en `procesarBoton()`.

## Funciones (probadas en placa salvo que se diga)
- **Pantalla CW** (v0.7d): estilo LoRa, título " ENTRENADOR CW " en negativo y `< KOCH >`, `< KEYER >`, `< ECHO >`
  (no caben 4 opciones con 9 px por línea).
  - **KOCH**: configuración (wpm, espaciado, lección, caracteres, grupos, `>ESTAD.` como última opción a la derecha de
    GRUPOS, iniciar, volver; foco 7 = estadísticas). Práctica: pausa/seguir y salir; corrección de errores girando;
    resultado: volver y rep. Al salir de estadísticas se vuelve a la configuración. La letra nueva de la lección sale
    el 25 % de las veces (`PESO_LETRA_NUEVA_PCT`, `caracterKochAleatorio()`, también en Duo y Echo); con 4 caracteres
    o menos, reparto uniforme. Tabla Koch de LCWO (v0.3i).
  - **KEYER** (v0.7a): tarea FreeRTOS `keyer` (núcleo 1, prioridad del motor CW) que lee las palas (GPIO4/5) cada
    1 ms; solo activa en su pantalla o en Echo. Memoria de la pala contraria: modo A solo en la pausa entre elementos;
    modo B también durante el elemento. Descodifica lo manipulado (silencio de 2 puntos = fin de carácter, 5 =
    espacio; patrón desconocido = `*`) y lo pasa a `loop()` por una cola; también sale por serie. Opciones: wpm 10–40,
    MODO A/B, palas NORM/INV, borrar, volver; en `mr_morse` (`kwpm`, `kmodob`, `kinv`). Salida solo por LED (y tono en
    GPIO7 cuando haya buzzer); KEY aún no. El apretón iámbico y el texto largo no se han validado (usuario principiante).
  - **ECHO** (v0.7b): Chispas emite 1–5 letras de la lección Koch con el motor CW a los wpm del keyer (keyer en pausa
    mientras emite: `keyerPausar()`/`keyerReanudar()`) y hay que repetirlas con las palas. Se evalúa al llegar a las
    letras del reto o con un silencio de palabra. Acierto: "BIEN!" y otro reto a 1,2 s; fallo: "MAL: R  K -.-" y se
    repite el mismo a 3 s. Opciones: wpm, largo (`elargo`), ver la letra antes (`ever`, SÍ por defecto), repetir (no
    cuenta) y volver. Marcador de aciertos/intentos. Por serie: `[ECHO] U -> V: MAL (5/8)`.
- **Motor CW** (v0.6a/b): tarea FreeRTOS `motorCW` en el núcleo 1 con prioridad 3 (`loop()` tiene 1), plazos absolutos
  sobre ticks de 1 ms. API: `motorCWEmitir()`, `motorCWDetener()` (corta enseguida con una notificación),
  `motorCWOcupado()`, `motorCWFinMs`. Solo las tareas llaman a `conmutarEmisionMorse()`. `DEBUG_CW` mide el retraso
  desde la tarea (resultado: 0 ms).
- **LoRa Duo** (pantalla 3): tramas digitales v2; Duo a los wpm del emisor (5–60); cola de recepción de 8 grupos;
  antes de transmitir se lee la trama pendiente y se escucha el canal (CAD, 3 intentos, espera 100 ms); RSSI en
  pantalla. Lo que llega *durante* `transmit()` se pierde por ser semidúplex. "ver" en el pie solo si hay un grupo
  oculto. QSO (subpantalla 2) es aún un marcador sin funcionalidad.
- **Radio** (v0.4c, `#define LORA_*`): 868,1 MHz, SF7, 250 kHz, CR 4/7, sync 0x12, 10 dBm, TCXO 1,8 V (DIO2 como RF
  switch lo activa RadioLib 6.6 en `begin()`; SPI 9/11/10). RSSI a 2 m: unos -40/-45 dBm.
- **WiFi** (v0.5a/b, v0.7k): se enciende al entrar en PROP y POTA, y en SOL solo si no hay hora (para el NTP).
  Si no hay red guardada o no conecta en 15 s, el portal **no se abre solo**: estado `WIFI_SIN_CONEXION` con aviso fijo
  y opción `> abrir webconf` (foco por defecto; girar lleva a ant./sig.; `opcionSinConexion`); si el portal se cierra
  por tiempo (5 min) en esas pantallas, vuelve el aviso. Al reentrar se reintenta. Hora NTP (CET/CEST) al conectar.
  HTTPS con `setInsecure()`. PROP: hamqsl.com cada 30 min, dos páginas (índices y condiciones por banda). POTA: spots
  solo CW cada 2 min (sin límites publicados: con moderación); línea de spot en 21 caracteres (referencia entera a la
  derecha; la frecuencia pierde el decimal y luego se recorta el indicativo si no cabe).
- **Webconf** (portal WiFiManager, `Chispas-XXXX`, clave `PORTAL_CLAVE` = `chispas73`, 192.168.4.1; v0.7f–k,
  probado): se abre con la opción **webconf** de CONF o desde el aviso de sin conexión, y muestra los datos (red,
  clave, web). Con el portal abierto, en CONF la opción pasa a **`ver qr`**: QR de WiFi a pantalla completa
  (`WIFI:T:WPA;S:...;P:...;;`, versión 3, 29×29, a 2 px por módulo), generado con el componente `qrcode` de ESP-IDF
  que trae el core (`esp_qrcode_generate`, sin librerías extra); cualquier giro o pulsación vuelve a los datos. El QR
  une el móvil a la red, pero la página del portal se abre sola o no según el móvil (no depende de Chispas; se probó
  y se descartó un segundo QR con la URL). **Portal en castellano**: `include/wm_strings_chispas.h` (traducción propia
  basada en `wm_strings_es.h` de WiFiManager 2.0.17, que estaba a medias), activada con `build_flags` `-I include` y
  `-DWM_STRINGS_FILE` en `platformio.ini`; menú "Red WiFi", "Estación y hora", "Salir". Si se actualiza WiFiManager,
  comparar con su `wm_strings_en.h`. Página "Estación y hora": indicativo, locator, hora manual en formato
  **HH:MM DD-MM-AAAA** (acepta barras; la pista del campo es la hora actual: `ParametroHora`) y casilla de **hora UTC**
  (`ParametroCasilla`, que sobrescribe `getCustomHTML()` para el "checked"; en `mr_estacion`, `utc`): la cabecera
  muestra `12:05Z` en vez de la hora local.
- **Sol** (pantalla 6): salida/puesta, ventanas de línea gris (sol entre -6° y +6°) y altura actual con las ecuaciones
  de NOAA para el locator de la estación (por defecto IM68mu). Necesita hora: sin ella enciende el WiFi
  (`CONECTANDO WIFI...`, luego `SINCRONIZANDO HORA...`); con hora no usa el WiFi.
- **SOTA: no programar el cliente.** Sus condiciones (api-db2.sota.org.uk/docs) exigen unirse al grupo
  "API-consumers" y prohíben conectar software generado por IA sin aprobación previa. Lo decide el usuario.
- **Batería**: divisor activado con GPIO37 **HIGH** en estas placas (Meshtastic declara LOW). Atenuación 2,5 dB,
  factor 4,9 calibrado con polímetro (4,10 V reales / 836,7 mV en el pin; un solo punto), curva Li-ion y suavizado al
  50 %. **Sin batería y con USB** el cargador deja ~4,17 V de media pero oscila (medido con `prueba_bateria`:
  dispersión 0,25 V frente a 0,002 V con batería): `leerBateria()` toma 200 lecturas seguidas y, si la dispersión
  supera 0,08 V (`VBAT_DISPERSION_SIN_BATERIA`), indica "sin batería" (`PWR`). Probado en las dos placas. Con USB el
  cargador sube la tensión: el % solo es fiable con batería sola.
- **Cara de Chispas** (v0.6l/m): salvapantallas de Inicio, diseño propio inspirado en Eilik (no copiarlo). Ojos y boca
  con primitivas (~5 KB de flash). Aparece a los 15 s sin tocar nada en Inicio, alterna 45 s de cara y 8 s de Inicio,
  y a los 5 min se duerme; cualquier giro o pulsación la quita sin hacer nada más (constantes `CARA_*`). Gestos al azar
  cada 6–15 s: contenta, risa, `>_<`, guiño, pícara (`ω`), nerviosa (gota), sorpresa. Cansada con batería < 20 %; más
  alegre tras una lección Koch ≥ 90 % en los últimos 15 min. Con la cara a la vista, `actualizarPantalla()` no dibuja.
  Ocupa ~40 % del bucle mientras se ve.
- **OLED negra tras reset corto** (resuelto en v0.4e): tras `display.begin()` se reenvían bomba de carga
  (`0x8D 0x14`) y encendido (`0xAF`). Se imprime por serie el motivo del reinicio; no hay `for(;;)` si falla la OLED.
- **Salida KEY** (GPIO2): `KEY_OUT_PIN` en BAJO al principio de `setup()` (v0.6c); aún no se usa. Medido con polímetro:
  0 V en marcha; picos de 0,3–0,5 V al arrancar y con RST (pin al aire hasta `setup()`), por debajo de los ~1,1 V
  que necesita el LED del PC817. **Recomendado: 10 kΩ de GPIO2 a GND**; añadirlo al esquema al montar la salida KEY.

## Pendiente
1. **Por hacer:** cargar la versión actual en la placa 2 (sigue en v0.6n).
2. **Montaje** (orden de la hoja 6 del esquema; hecho: raíles, encoder, palas): buzzer, auriculares y volumen (tono en
   GPIO7 ya programado); salida KEY con PC817 y pull-down de 10 kΩ; decodificador CW (Goertzel); GPS cuando llegue
   (opcionales: enable del GPS y BME280). Falta comprar: divisor minijack 1→2 y 2 pulsadores 6×6 mm.
3. **Ideas aparcadas por el usuario** (para repensar más adelante):
   - **Estadísticas por carácter** (recomendada): marcar fallos al corregir el Koch y recoger los del Echo; guardar
     aciertos/fallos por letra en NVS y mostrar las más flojas. Opcional: repetir más las letras flojas.
   - Editar indicativo y locator en el aparato con el encoder (sin móvil ni webconf).
   - Aceleración del giro: al girar rápido, los valores avanzan de 5 en 5.
   - **Consumo y recursos. Primero medir**: línea por serie cada minuto con heap libre, mínimo histórico, pila libre
     de cada tarea (`uxTaskGetStackHighWaterMark`) y uso de NVS (`nvs_get_stats`); consumo con polímetro o medidor USB
     antes y después de cada cambio. Luego, por orden: (1) `loop()` gira sin pausa y tiene el núcleo 1 al 100 %:
     `delay(1)` por vuelta o bajar la CPU a 80–160 MHz fuera del WiFi; (2) la LoRa está en recepción siempre: dormirla
     fuera de Duo (~5 mA); (3) bajar el brillo de la OLED con la cara dormida; (4) deep sleep con PRG largo. Vigilar
     la fragmentación por el uso de `String` con muchas horas encendido.
   - Detectar USB y carga: la V3 no lo permite por software. Propuesta sin decidir: divisor 100k/150k del pin 5V
     (J2-2) a GPIO33 (J2-12); comprobar antes que el pin 5V da 0 V solo con batería.
4. Pantalla Duo para textos largos y QSO por LoRa con el keyer. A futuro: audio I2S y OTA.

## Decisiones tomadas
- Aspecto: mantener el estilo actual de pantallas (fuente 5×7, sin imitar Meshtastic).
- Práctica en pareja con **tramas digitales**, no audio: un byte por carácter con bit centinela (punto = 0,
  raya = 1) y un byte reservado para el espacio entre palabras. Cabecera con magic, versión/tipo, `seq`, wpm e
  indicativo (8 caracteres). Unos 32 caracteres por trama; enviar por frase o línea, no por pulsación. Capa de
  transporte intercambiable (LoRa / ESP-NOW).
- Si el firmware del Morserino-32 sirve de referencia: es open source y su protocolo MOPP envía un paquete por
  palabra. Las V3 no son compatibles con los Morserino oficiales.
- POTA/SOTA: búsqueda de parque offline con CSV guardado en LittleFS; spots online con cola si no hay cobertura.
  Las APIs de POTA son no oficiales; la lectura de SOTA es abierta, pero publicar spots exige credenciales.
- Expansor MCP23017 descartado: GPIO directos.

## Prototipo en protoboard (desde el 9 oct 2026)
Montaje sin PCB con la lista de compra "opción A (buzzer)", comprada salvo el GPS (pedido), el adaptador a bornas,
el adaptador de 6,35 mm y los opcionales. Valores de la lista sin probar en banco.

| GPIO | Uso |
|---|---|
| 2 | Salida KEY (PC817, 330 Ω; añadir 10 kΩ a GND). `KEY_OUT_PIN` |
| 4 / 5 | Pala DIT / DAH (pull-up interno; `KEYER_PADDLE_DIT/DAH`). **Montado y probado** |
| 6 | Entrada del decodificador (ADC1): acoplo 1 µF, polarización 2×10 kΩ, trimmer, 1 kΩ y 2×1N4148 |
| 7 | Tono CW por PWM: 1 kΩ, potenciómetro de volumen, buzzer piezo pasivo en TN del jack de auriculares (silencio mecánico) y auriculares con 10 µF y 220 Ω en T y R |
| 19 / 20 / 48 | Encoder KY-040 CLK / DT / SW. **Montado y probado** (19/20 son USB D−/D+ del S3 pero funcionan como entradas) |
| 47 | RX desde el TX del GPS (UART por la matriz GPIO) |
| 3 (futuro) | Enable del GPS: AO3401A en el positivo, 100 kΩ puerta-3V3, BAJO = encendido |
| BME280 (futuro) | I2C 0x76: recomendado Wire1 en GPIO41/42 |

Posición física (fotos del usuario; pin 1 = GND, pad cuadrado, el más cercano al USB):
- J3: 18 GPIO7 tono · 17 GPIO6 decod. · 16 GPIO5 DAH · 15 GPIO4 DIT · 14 GPIO3 GPS en · 13 GPIO2 KEY · 12 GPIO1 (VBAT, no)
  · 11/10/9 GPIO38/39/40 (libres) · 8/7 GPIO41/42 · 6/5 GPIO45/46 (no) · 4 GPIO37 (no) · 3/2 **3V3** · 1 **GND**.
- J2: 18/17 GPIO19/20 encoder CLK/DT · 16 GPIO21 (OLED_RST) · 15 GPIO26 (flash, no) · 14 GPIO48 encoder SW · 13 GPIO47 GPS RX
  · 12/11 GPIO33/34 · 10 GPIO35 (LED) · 9 GPIO36 (Vext) · 8 GPIO0 (PRG) · 7 RST · 6/5 GPIO43/44 (UART0) · 4/3 Ve · 2 5V · 1 GND.
- `Ve` es Vext (conmutada por GPIO36, alimenta la OLED): los módulos van a 3V3 de J3. `5V` solo tiene tensión con USB.
- GPIO17/18 (I2C de la OLED) no salen a los conectores, pero hay dos **pads de prueba "17" y "18" por detrás**
  (mismo bus; 0x76 no choca con la OLED 0x3C): solo si faltan pines, soldando hilo fino.
- GPIO3 es pin de arranque (JTAG) solo con un eFuse grabado (de fábrica no): el 100 kΩ del enable no debería afectar
  (razonado, sin probar).
- Pines libres: 38–42, 33 y 34. No usar: 0, 1, 8–14, 17/18/21 (OLED), 26–32, 35, 36, 37, 43/44, 45/46 (arranque).

Esquema de conexiones: `docs/esquema_protoboard.pdf` (6 hojas), generado con `docs/gen_esquema.py` (HTML/SVG; el PDF
se imprime con Edge sin interfaz). Si cambia algún pin, actualizar el script y regenerar el PDF.

## KiCad: APLAZADO
**No trabajar en KiCad, `pins.h`, `pcb_spec.md` ni `gen_kicad.py` por ahora.** (Referencia, sin usar: describen una
PCB portadora rev. A.2; la fuente de verdad del circuito era `gen_kicad.py`.)

## Cómo trabajar
- Responder en español, directo y sin relleno.
- Compilar con `pio run` antes de dar un cambio por bueno. No fiarse de comprobaciones de sintaxis simuladas.
- Hacer los cambios sobre copia o con diff claro; no reestructurar sin avisar. No sobrescribir el original.
- Antes de afirmar algo sobre RadioLib, WiFiManager o el core de Espressif, comprobar la versión instalada.
- Separar lo que se ha probado en la placa de lo que solo se ha razonado.
- Grabar el puerto serie sin reiniciar la placa: pyserial con `dtr = False` y `rts = False` antes de `open()`.
- Ojo con los heredocs de Bash: la herramienta reduce las barras invertidas dobles a una (un `'\0'` acabó como
  byte nulo en el archivo). Para código con barras invertidas, editar con la herramienta Edit o Write.
