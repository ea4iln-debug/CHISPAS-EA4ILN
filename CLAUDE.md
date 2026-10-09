# Chispas EA4ILN — contexto del proyecto

## Qué es
**Chispas** (antes "Mr. Morse"; "chispas" era el apodo del radiotelegrafista de a bordo):
dispositivo portátil de CW para radioaficionados (indicativo EA4ILN) sobre una **Heltec WiFi LoRa 32 V3**
(ESP32-S3FN8, SX1262 a 868 MHz, OLED 0,96", sin PSRAM, 8 MB de flash). Núcleo actual: entrenador Koch
estilo LCWO y práctica en pareja por LoRa. A medio plazo: keyer iámbico, decodificador desde transceptor,
salida key, GPS/locator, POTA/SOTA, propagación y línea gris.

## Estado actual (9 oct 2026)
- Firmware: `src/main.cpp` **v0.7b** (10 oct 2026), cargada en la placa del encoder y las palas (COM16); la desnuda sigue en v0.6n. Arduino + PlatformIO, env `heltec_wifi_lora_32_V3`,
  `platform = espressif32 @ 6.13.0` (core Arduino 2.0.17), librerías RadioLib ^6.6, Adafruit GFX y SSD1306, WiFiManager 2.0.17 y ArduinoJson 7.
  **Un solo `.cpp` en `src/`** (si hay dos, falla el enlazado).
- Desarrollo con **dos Heltec V3 desnudas**: la salida CW va por el LED (GPIO35) y los menús se manejan
  con el botón PRG (GPIO0), pulsación corta/larga.
- Pantallas (v0.5d, constantes `PANTALLA_*`): 1 Inicio, 2 CW (Koch), 3 LoRa (Duo/QSO), 4 Propagación, 5 POTA,
  6 Sol, 7 Configuración (indicativo, locator, LoRa, batería, FW, portal, reset de fábrica). La antigua pantalla
  de debug se eliminó. Cabecera con nombre corto de pantalla; `- : opc` en la barra si hay opciones.
- NVS: `mr_morse` (entrenador y estadísticas; el nombre se mantiene para no perder lo guardado al renombrar el aparato), `mr_estacion` (indicativo, locator); WiFi en el NVS del ESP32.
  Borrar estadísticas solo borra estadísticas; el reset de fábrica lo borra todo.
- Hardware en curso: prototipo en protoboard (sección más abajo). La PCB y `pins.h` siguen aplazados.
- La entrada está abstraída en `leerEventoBoton()` (BOTON_CORTA / BOTON_LARGA): al montar el rotary y el
  pulsador de función solo habrá que cambiar esa función, no los menús.
- `SPEAKER_PWM_PIN` en GPIO7, ya fijado en el mapa del prototipo (el GPIO25 del código antiguo no existe en el S3).
- Revisión hecha: v0.3b -> v0.3c (temporizador entre caracteres, semilla con `esp_random()`, flag LoRa tras TX,
  `salirModoLoRa()`) -> v0.3d (botón por eventos, sin `delay`).
- 8 oct 2026, probado en placa: v0.3e cola de recepción LoRa (8 grupos) -> v0.3f estadísticas 3.5 (L40 y ancho),
  `¿` con `write(0xA8)`, foco al volver a P2 -> v0.3g batería en caché cada 30 s fuera de la emisión CW y
  redibujado solo por cambios (sin refresco fijo) -> v0.3h RSSI en Duo y medida de temporización CW por serie
  (`DEBUG_CW`; resultado: retraso máx. 0 ms). RSSI medido: -31 dBm a 2 m.
- Las pruebas son con LED, pantalla y monitor serie: no hay audio conectado (no proponer pruebas "a oído").
- La versión está en `#define FW_VERSION` (al principio de `main.cpp`): se ve en la pantalla 1 y por serie.
  Subirla en cada cambio que se cargue en placa.
- Radio (v0.4c, `#define LORA_*`): 868,1 MHz, SF7, 250 kHz, CR 4/7, sync 0x12, 10 dBm, TCXO 1,8 V (revisado:
  DIO2 RF switch lo activa RadioLib 6.6 en `begin()`; SPI 9/11/10 correcto). RSSI a 2 m: unos -40/-45 dBm
  (con SF9/125 kHz y 868,0 eran -31 dBm; sin comparar en las mismas condiciones).

- OLED negra tras reset corto (resuelto en v0.4e, probado en las dos placas): en cerca de la mitad de los
  reinicios cortos (RST o RTS al flashear) la placa arrancaba bien pero la OLED quedaba negra, aunque respondía
  por I2C, su estado decía "encendida" y el I2C no daba errores. No era alimentación (cortar Vext 0,3 s o 2 s
  no cambiaba nada). Arreglo: tras `display.begin()` se reenvían bomba de carga (`0x8D 0x14`) y encendido (`0xAF`).
  También se imprime por serie el motivo del reinicio y ya no hay `for(;;)` si falla la OLED.

- WiFi (v0.5a, probado): pantallas 5 (propagación, hamqsl.com) y 6 (spots POTA solo CW). El WiFi solo se
  enciende en esas pantallas. Red por portal WiFiManager (`Chispas-XXXX`, clave `PORTAL_CLAVE` = `chispas73`, 192.168.4.1),
  guardada en NVS. Hora NTP (CET/CEST) en la cabecera de todas las pantallas. HTTPS con `setInsecure()`.
  Refresco: propagación 30 min, POTA 2 min (sin límites publicados: consultar con moderación).
- v0.5b (probado): pantalla 5 con segunda página de condiciones por banda (hamqsl, día/noche); pantalla 7
  "Sol" sin WiFi: salida/puesta, ventanas de línea gris (sol entre -6° y +6°) y altura actual, calculadas con
  las ecuaciones de NOAA para el locator de la estación **IM68mu** (`#define LOCATOR`). Necesita hora NTP.
- **SOTA: no programar el cliente.** Sus condiciones (api-db2.sota.org.uk/docs) exigen unirse al grupo
  "API-consumers" y prohíben conectar software generado por IA sin aprobación previa. Lo decide el usuario.

- Batería (v0.5e): en estas placas el divisor se activa con GPIO37 **HIGH** (con LOW el pin lee 0; Meshtastic
  declara LOW para su V3). Atenuación 2,5 dB, factor 4,9 (teórico 390k/100k) y porcentaje con curva Li-ion.
  **v0.5f (9 oct 2026): calibrado con polímetro**, 4,10 V reales y 836,7 mV en el pin, lo que da 4,900: el factor
  teórico es correcto (un solo punto medido). Se quitó `diagnosticoBateria()`. Con USB el cargador sube la tensión: el % solo es fiable con batería sola.

- Motor CW (v0.6a, 9 oct 2026, **probado en placa**: Koch, pausa, Duo con cola, salir de Duo, batería durante CW).
  v0.6b quita el `radio.startReceive()` al acabar cada emisión de Duo (sobraba y podía cortar una trama entrante).
  Detalle: tarea FreeRTOS `motorCW` en el núcleo 1 con prioridad 3
  (`loop()` tiene 1), con plazos absolutos sobre ticks de 1 ms. API para `loop()`: `motorCWEmitir()`, `motorCWDetener()`
  (corta enseguida con una notificación), `motorCWOcupado()` y `motorCWFinMs`. Solo la tarea llama a `conmutarEmisionMorse()`.
  Koch (`CW_EMITIENDO_GRUPO`) y Duo piden el grupo y esperan a que acabe. Se quitó `motorCWActivo()`: batería y
  redibujado ya se hacen aunque suene CW. `DEBUG_CW` mide desde la tarea.

## Dónde lo dejamos (9 oct 2026) y siguiente sesión
- **RECORDAR AL USUARIO al empezar la sesión:** renombrar el repositorio en GitHub (Settings → Repository name,
  p. ej. `Chispas-EA4ILN`). Después: `git remote set-url origin <url nueva>`, quitar este aviso y, si quiere,
  renombrar la carpeta local `Mr.-Morse-EA4ILN` con VS Code cerrado.
- Tarde del 9 oct: v0.6d batería (umbral "sin batería" 2,6 V tomado de Meshtastic, suavizado al 50 %),
  v0.6e/f/g pantalla de inicio y **cambio de nombre a Chispas** (portal `Chispas-XXXX`, clave `chispas73`).
  **Batería sin conectar con USB (v0.6n, probado en las dos placas):** el umbral de 2,6 V no servía: sin batería el
  cargador deja una media de ~4,17 V (CONF decía 4,19 V), pero oscila. Medido con `pio run -e prueba_bateria`
  (ráfaga de 500 lecturas en 44 ms): dispersión 0,25 V y pico a pico 0,86 V sin batería, frente a 0,002 V y 0,03 V
  con batería. `leerBateria()` toma 200 lecturas seguidas y, si la dispersión supera 0,08 V
  (`VBAT_DISPERSION_SIN_BATERIA`), indica "sin batería" (`PWR` en la cabecera). Con batería, 88 %; al quitarla, `PWR`;
  al volver, 88 %.
- Detectar USB y carga: la V3 no lo permite por software (Meshtastic tampoco; solo estima con > 4,20 V, que con
  nuestro factor calibrado casi no ocurre). Propuesta sin decidir: divisor 100k/150k del pin 5V (J2-2) a GPIO33
  (J2-12); comprobar antes que el pin 5V da 0 V solo con batería.
- Aspecto de pantallas: el usuario prefiere mantener el estilo actual (fuente 5×7, sin imitar Meshtastic).
- Hecho hoy: batería calibrada (v0.5f), motor CW en tarea FreeRTOS (v0.6a/b, probado), lista de la compra revisada,
  esquema `docs/esquema_protoboard.pdf` y v0.6c (palas 4/5, KEY en GPIO2 en BAJO). Todo con commit y push.
- GPIO2 medido con polímetro (v0.6n, placa desnuda, 9 oct 2026): 0 V en marcha; picos momentáneos de 0,3–0,5 V al
  arrancar y al pulsar RST (pin al aire hasta `setup()`). Por debajo de los ~1,1 V que necesita el LED del PC817:
  no manipula. El polímetro no ve pulsos de microsegundos. **Recomendado: 10 kΩ de GPIO2 a GND** (pull-down, para
  que esté en BAJO también durante el reset); añadirlo al esquema cuando se monte la salida KEY.
- Siguiente, por el orden de montaje de la hoja 6 del esquema:
  1. Raíles 3V3/GND de la Heltec a la protoboard (comprobar 3,3 V y que no hay cortocircuito).
  2. Programa de prueba de pines por serie (19, 20, 48 y reserva 38–40) con el encoder conectado: decide si el
     encoder se queda en 19/20/48 o pasa a 38/39/40.
  3. Encoder en el firmware: nueva fuente de eventos en `leerEventoBoton()`. Luego desbloquea lo aplazado
     (estadísticas por carácter, indicativo por placa).
  4. Después: palas y keyer iámbico (usa el motor CW), tono por buzzer, KEY, decodificador y GPS cuando llegue.
- Falta comprar: divisor minijack 1→2, 2 pulsadores 6×6 mm (y soldador si no tiene).

- **Encoder (v0.6h, probado en placa):** se queda en 19/20/48 (prueba con `pio run -e prueba_pines`: giros exactos,
  38/39/40 libres fijos a 1; el pulsador del eje rebota al soltar). Lectura por interrupción en CLK/DT con tabla de
  cuadratura (`isrEncoder`, en IRAM, lee `GPIO_IN_REG`); pulsadores PRG y eje con `leerPulsador()`. Eventos:
  `ENC_DERECHA/IZQUIERDA/PULSA/LARGA` además de `BOTON_CORTA/LARGA`. Girar = foco (`moverFocoAnillo()`), pulsar = corta,
  pulsar sobre un valor = editar girando (`editandoValor`, vídeo inverso, sin dar la vuelta), larga del eje = larga
  del PRG salvo donde esta solo mueve el foco. El PRG sigue igual.
- **Manejo (v0.6i–k, probado):** todo se hace con el encoder; la larga del eje es solo atajo. **PRG corto = atrás**
  (sale de la edición, sube un nivel, en pantallas principales va a Inicio). **PRG largo libre**: reservado para dormir
  la placa (deep sleep, despertar con GPIO0; sin probar). Acciones que antes eran pulsación larga pasan a opciones con
  foco (práctica: pausa/seguir y salir; resultado: volver y rep.; Duo: "ver" solo si hay grupo oculto; estadísticas:
  volver). `entrarEnPantalla(p, haciaAtras)`: al cambiar de pantalla el foco queda en la flecha usada (sig./ant.).
  Barra: página "1/2" en el centro en PROP y POTA (opción "mas >"), `« opc »` en CONF. PROP sin "wifi"; en CONF el
  portal se llama **webconf**. POTA: línea de spot en 21 caracteres (referencia entera a la derecha; la frecuencia
  pierde el decimal y luego se recorta el indicativo si no cabe).
- **Cara de Chispas (v0.6l/m, probada; "es la cara de Chispas", según el usuario):** salvapantallas de Inicio,
  diseño propio inspirado en Eilik (no copiarlo). Ojos y boca dibujados con primitivas, sin imágenes (~5 KB de flash).
  Aparece a los 15 s sin tocar nada en Inicio, alterna 45 s de cara y 8 s de Inicio, y a los 5 min se duerme;
  cualquier giro o pulsación la quita sin hacer nada más (constantes `CARA_*`). Gestos al azar cada 6–15 s: contenta,
  risa, `>_<`, guiño, pícara (`ω`), nerviosa (gota), sorpresa. Cansada con batería < 20 %; más alegre si la última
  lección Koch fue ≥ 90 % hace menos de 15 min. Con la cara a la vista, `actualizarPantalla()` no dibuja.
- **Koch:** la letra nueva de la lección sale el 25 % de las veces (`PESO_LETRA_NUEVA_PCT`, `caracterKochAleatorio()`,
  también en Duo); con 4 caracteres o menos, reparto uniforme.
- **Keyer iámbico (v0.7a, probado en placa con palas en GPIO4/5, 10 oct 2026):** pantalla CW > `< KEYER >`. Tarea
  FreeRTOS `keyer` (núcleo 1, prioridad del motor CW) que lee las palas cada 1 ms; solo activa en su pantalla.
  Memoria de la pala contraria: modo A solo en la pausa entre elementos; modo B también durante el elemento.
  Descodifica lo manipulado (silencio de 2 puntos = fin de carácter, 5 = espacio; patrón desconocido = `*`) y lo pasa
  a `loop()` por una cola; también sale por serie. Opciones: wpm 10–40 (editar), MODO A/B, palas NORM/INV, borrar,
  volver; se guardan en `mr_morse` (`kwpm`, `kmodob`, `kinv`) al salir. Salida solo por LED (y tono en GPIO7 cuando
  haya buzzer); KEY (GPIO2) aún no. Probado: letras con una pala cada vez descodificadas bien (S, H, M, O, K).
  El usuario aún no sabe manipular: el apretón iámbico y el texto largo no se han validado con él.
- **Modo Echo (v0.7b, probado en placa: 7/12 aciertos, "está bien" según el usuario):** pantalla CW > `< ECHO >`
  (la pantalla CW pierde el título para que quepan 4 opciones: CONTINUAR, ESTADÍSTICAS, KEYER, ECHO). Chispas emite
  con el motor CW 1–5 letras de la lección Koch (`caracterKochAleatorio`), a los wpm del keyer; mientras emite, el
  keyer está en pausa (`keyerPausar()`/`keyerReanudar()`). Se evalúa al llegar a las letras del reto o con un silencio
  de palabra. Acierto: "BIEN!" y otro reto a 1,2 s. Fallo: "MAL: R  K -.-" y se repite el mismo a 3 s. Opciones: wpm,
  largo (`elargo`), ver la letra antes (`ever`, SÍ por defecto, para aprender), repetir (no cuenta) y volver.
  Marcador de aciertos/intentos. Resultados por serie (`[ECHO] U -> V: MAL (5/8)`).
- Grabar el puerto serie sin reiniciar la placa: pyserial con `dtr = False` y `rts = False` antes de `open()`.
- Ojo al editar con heredocs de Bash: la herramienta reduce las barras invertidas dobles a una, y en un literal de C
  como el del carácter nulo acaba entrando un byte 0 en el archivo. Para esos casos, editar con la herramienta Edit.

## Pendiente conocido (por prioridad; orden confirmado por el usuario)
1. Ideas con el encoder, para repensar más adelante (el usuario las aparca, 9 oct 2026):
   - **Estadísticas por carácter en el Koch** (recomendada): al corregir, recorrer el texto emitido con el encoder
     y marcar los caracteres fallados; guardar aciertos/fallos por letra en NVS y mostrar las más flojas. Más
     adelante, opcional: repetir más las letras flojas (no es como LCWO).
   - Editar indicativo y locator en el aparato, carácter a carácter con el encoder (sin móvil ni webconf).
   - Aceleración del giro: al girar rápido, los valores avanzan de 5 en 5 (p. ej. lección 1–40).
   - El indicativo por placa ya está resuelto: se cambia en webconf y se guarda en `mr_estacion`.
   - Consumo y recursos (propuesto 9 oct 2026, para estudiar más adelante). **Primero medir**: línea por serie cada
     minuto con heap libre, mínimo histórico, pila libre de cada tarea (`uxTaskGetStackHighWaterMark`) y uso de NVS
     (`nvs_get_stats`); consumo con polímetro o medidor USB antes y después de cada cambio. Luego, por orden:
     (1) `loop()` gira sin pausa y tiene el núcleo 1 al 100 %: `delay(1)` por vuelta o bajar la CPU a 80–160 MHz fuera
     del WiFi; (2) la LoRa está en recepción siempre: dormirla fuera de Duo (~5 mA); (3) bajar el brillo de la OLED
     con la cara dormida; (4) deep sleep con PRG largo. Vigilar la fragmentación por el uso de `String` con muchas
     horas encendido. Cifras de v0.6m: flash 1,12 MB de 3,19 MB (app0); RAM estática 51,8 KB de 320 KB; app1 (OTA)
     y spiffs (1,5 MB, para el CSV de POTA) sin usar; la cara ocupa ~40 % del bucle mientras se ve.
2. Pantalla Duo para textos largos (cuando haya frases: QSO con keyer).
3. A futuro: audio I2S, keyer iámbico, decodificador (Goertzel).

Hecho y probado: v0.3i tabla Koch LCWO; v0.4a trama v2; v0.4d Duo a los wpm del emisor (5-60, si no los
locales) y antes de TX se lee la trama pendiente y se escucha el canal (CAD, 3 intentos, espera 100 ms).
Nota: lo que llega *durante* `transmit()` se pierde por ser semidúplex, no por el firmware.

## Decisiones tomadas
- Práctica en pareja con **tramas digitales**, no audio: un byte por carácter con bit centinela
  (punto = 0, raya = 1) y un byte reservado para el espacio entre palabras. Cabecera con magic, versión/tipo,
  `seq`, wpm e indicativo (8 caracteres). Unos 32 caracteres por trama; enviar por frase o línea, no por pulsación.
  Para corta distancia probar SF7 con 250 kHz. Capa de transporte intercambiable (LoRa / ESP-NOW).
- Si el firmware del Morserino-32 sirve de referencia: es open source y su protocolo MOPP envía un paquete por
  palabra. Las V3 no son compatibles con los Morserino oficiales.
- POTA/SOTA: búsqueda de parque offline con CSV guardado en LittleFS; spots online con cola si no hay cobertura.
  Las APIs de POTA son no oficiales; la lectura de SOTA es abierta, pero publicar spots exige credenciales.

- Expansor MCP23017 descartado: GPIO directos (ver prototipo en protoboard).

## Prototipo en protoboard (9 oct 2026)
Montaje sin PCB con la lista de compra "opción A (buzzer)", comprada salvo el GPS (pedido), el adaptador a bornas,
el adaptador de 6,35 mm y los opcionales. Valores de la lista sin probar en banco. Pines decididos por el usuario:

| GPIO | Uso |
|---|---|
| 2 | Salida KEY (PC817, 330 Ω). `KEY_OUT_PIN`, en BAJO al principio de `setup()` (v0.6c) |
| 4 / 5 | Paleta DIT / DAH (pull-up interno; `KEYER_PADDLE_DIT/DAH` desde v0.6c) |
| 6 | Entrada del decodificador (ADC1): acoplo 1 µF, polarización 2×10 kΩ, trimmer, 1 kΩ y 2×1N4148 |
| 7 | Tono CW por PWM: 1 kΩ, potenciómetro de volumen, buzzer piezo pasivo en TN del jack de auriculares (silencio mecánico) y auriculares con 10 µF y 220 Ω en T y R |
| 19 / 20 / 48 | Encoder KY-040 CLK / DT / SW (19/20 son USB D−/D+ del S3: probar en placa desnuda) |
| 47 | RX desde el TX del GPS (UART por la matriz GPIO) |
| 3 (futuro) | Enable del GPS: AO3401A en el positivo, 100 kΩ puerta-3V3, BAJO = encendido |
| BME280 (futuro) | I2C 0x76. Si 17/18 no salen al conector, segundo bus (Wire1) en dos pines libres |

Posición física (fotos del usuario, 9 oct 2026; pin 1 = GND, pad cuadrado, el más cercano al USB):
- J3: 18 GPIO7 tono · 17 GPIO6 decod. · 16 GPIO5 DAH · 15 GPIO4 DIT · 14 GPIO3 GPS en · 13 GPIO2 KEY · 12 GPIO1 (VBAT, no)
  · 11/10/9 GPIO38/39/40 (plan B encoder) · 8/7 GPIO41/42 · 6/5 GPIO45/46 (no) · 4 GPIO37 (no) · 3/2 **3V3** · 1 **GND**.
- J2: 18/17 GPIO19/20 encoder CLK/DT · 16 GPIO21 (OLED_RST) · 15 GPIO26 (flash, no) · 14 GPIO48 encoder SW · 13 GPIO47 GPS RX
  · 12/11 GPIO33/34 · 10 GPIO35 (LED) · 9 GPIO36 (Vext) · 8 GPIO0 (PRG) · 7 RST · 6/5 GPIO43/44 (UART0) · 4/3 Ve · 2 5V · 1 GND.
- `Ve` es Vext (conmutada por GPIO36, alimenta la OLED): los módulos van a 3V3 de J3. `5V` solo tiene tensión con USB.
- GPIO17/18 (I2C de la OLED) no salen a los conectores, pero hay dos **pads de prueba "17" y "18" por detrás**.
  BME280 (futuro): recomendado Wire1 en GPIO41/42 para el prototipo; los pads 17/18 (mismo bus, 0x76 no choca con
  la OLED 0x3C) solo si faltan pines, soldando hilo fino y fijándolo. Sin decidir.
- GPIO3 es pin de arranque (JTAG) solo si hay un eFuse grabado (de fábrica no): el 100 kΩ del enable del GPS no
  debería afectar (razonado, sin probar).

Esquema de conexiones: `docs/esquema_protoboard.pdf` (6 hojas), generado con `docs/gen_esquema.py` (HTML/SVG; el PDF
se imprime con Edge sin interfaz). Si cambia algún pin, actualizar el script y regenerar el PDF.
Pines aún libres según el pinout publicado de la V3 (sin comprobar en la placa): 38–42, 33 y 34.
No usar: 0, 8–14, 17/18/21 (OLED), 35, 36, 37, 43/44, 45/46 (arranque), 26–32.

## KiCad: APLAZADO
**No trabajar en KiCad, `pins.h`, `pcb_spec.md` ni `gen_kicad.py` por ahora.**
(Referencia, sin usar ahora: `pins.h`, `pcb_spec.md` y `kicad/gen_kicad.py` describen una PCB portadora rev. A.2;
la fuente de verdad del circuito era `gen_kicad.py`.)

## Cómo trabajar
- Responder en español, directo y sin relleno.
- Compilar con `pio run` antes de dar un cambio por bueno. No fiarse de comprobaciones de sintaxis simuladas.
- Hacer los cambios sobre copia o con diff claro; no reestructurar sin avisar. No sobrescribir el original.
- Antes de afirmar algo sobre RadioLib o el core de Espressif, comprobar la versión instalada.
- Separar lo que se ha probado en la placa de lo que solo se ha razonado.