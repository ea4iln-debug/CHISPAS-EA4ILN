# Mr. Morse EA4ILN — contexto del proyecto

## Qué es
Dispositivo portátil de CW para radioaficionados (indicativo EA4ILN) sobre una **Heltec WiFi LoRa 32 V3**
(ESP32-S3FN8, SX1262 a 868 MHz, OLED 0,96", sin PSRAM, 8 MB de flash). Núcleo actual: entrenador Koch
estilo LCWO y práctica en pareja por LoRa. A medio plazo: keyer iámbico, decodificador desde transceptor,
salida key, GPS/locator, POTA/SOTA, propagación y línea gris.

## Estado actual (8 oct 2026)
- Firmware: `src/main.cpp` **v0.5f** (compila; sin cargar en placa). Arduino + PlatformIO, env `heltec_wifi_lora_32_V3`,
  `platform = espressif32 @ 6.13.0` (core Arduino 2.0.17), librerías RadioLib ^6.6, Adafruit GFX y SSD1306, WiFiManager 2.0.17 y ArduinoJson 7.
  **Un solo `.cpp` en `src/`** (si hay dos, falla el enlazado).
- Desarrollo con **dos Heltec V3 desnudas**: la salida CW va por el LED (GPIO35) y los menús se manejan
  con el botón PRG (GPIO0), pulsación corta/larga.
- Pantallas (v0.5d, constantes `PANTALLA_*`): 1 Inicio, 2 CW (Koch), 3 LoRa (Duo/QSO), 4 Propagación, 5 POTA,
  6 Sol, 7 Configuración (indicativo, locator, LoRa, batería, FW, portal, reset de fábrica). La antigua pantalla
  de debug se eliminó. Cabecera con nombre corto de pantalla; `- : opc` en la barra si hay opciones.
- NVS: `mr_morse` (entrenador y estadísticas), `mr_estacion` (indicativo, locator); WiFi en el NVS del ESP32.
  Borrar estadísticas solo borra estadísticas; el reset de fábrica lo borra todo.
- **No migrar a `pins.h` ni al hardware nuevo hasta que el firmware sea estable y se monte el circuito.**
- La entrada está abstraída en `leerEventoBoton()` (BOTON_CORTA / BOTON_LARGA): al montar el rotary y el
  pulsador de función solo habrá que cambiar esa función, no los menús.
- `SPEAKER_PWM_PIN` está en GPIO7 de forma provisional (el GPIO25 del código antiguo no existe en el S3).
- Revisión hecha: v0.3b -> v0.3c (temporizador entre caracteres, semilla con `esp_random()`, flag LoRa tras TX,
  `salirModoLoRa()`) -> v0.3d (botón por eventos, sin `delay`).
- 8 oct 2026, probado en placa: v0.3e cola de recepción LoRa (8 grupos) -> v0.3f estadísticas 3.5 (L40 y ancho),
  `¿` con `write(0xA8)`, foco al volver a P2 -> v0.3g batería en caché cada 30 s fuera de la emisión CW y
  redibujado solo por cambios (sin refresco fijo) -> v0.3h RSSI en Duo y medida de temporización CW por serie
  (`DEBUG_CW`; resultado: retraso máx. 0 ms). RSSI medido: -31 dBm a 2 m.
- **v0.5e cargada en las dos placas** (8 oct 2026).
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
  enciende en esas pantallas. Red por portal WiFiManager (`MrMorse-XXXX`, clave `PORTAL_CLAVE`, 192.168.4.1),
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

## Pendiente conocido (por prioridad; orden confirmado por el usuario)
1. Aplazado hasta tener el encoder (decisión del usuario, 8 oct 2026):
   - Koch: estadísticas por carácter (marcar fallos en la corrección) y modo Echo (necesita keyer).
   - Indicativo configurable por placa (hoy constante `INDICATIVO` = "EA4ILN" en las dos; con dos placas
     no afecta al descarte de duplicados).
2. Pantalla Duo para textos largos (cuando haya frases: QSO con keyer).
3. A futuro: motor CW en una tarea FreeRTOS, audio I2S, keyer iámbico, decodificador (Goertzel).

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

- **Expansor MCP23017: decidido en principio quitarlo** y usar GPIO directos (rotary en 38/39/40, función en 41,
  detección de auriculares en 42, enables de GPS y amplificador en 19/20). Antes de fijarlo, probar cada pin en una
  placa desnuda. Evitar 45 y 46 con pull-up externo (pines de arranque). Los cambios de esquema que eso implica
  quedan para cuando se retome el hardware.

## Decisiones abiertas (para cuando se retome el hardware)
- Detector de auriculares: añadir 10 kΩ en serie con el pin (el contacto TN lleva señal del DAC cuando no hay jack).
- Auriculares directos del PCM5102A con 220 Ω frente a amplificador TPA6132A2 (decidir tras probar en banco).

## Hardware y KiCad: APLAZADO
**No trabajar en KiCad, `pins.h`, `pcb_spec.md` ni `gen_kicad.py` por ahora.** El usuario retomará esas decisiones
más adelante. Mientras tanto, el desarrollo es solo de firmware sobre las placas desnudas.
(Referencia, sin usar ahora: `pins.h`, `pcb_spec.md` y `kicad/gen_kicad.py` describen una PCB portadora rev. A.2;
la fuente de verdad del circuito era `gen_kicad.py`.)

## Cómo trabajar
- Responder en español, directo y sin relleno.
- Compilar con `pio run` antes de dar un cambio por bueno. No fiarse de comprobaciones de sintaxis simuladas.
- Hacer los cambios sobre copia o con diff claro; no reestructurar sin avisar. No sobrescribir el original.
- Antes de afirmar algo sobre RadioLib o el core de Espressif, comprobar la versión instalada.
- Separar lo que se ha probado en la placa de lo que solo se ha razonado.