# Mr. Morse EA4ILN — contexto del proyecto

## Qué es
Dispositivo portátil de CW para radioaficionados (indicativo EA4ILN) sobre una **Heltec WiFi LoRa 32 V3**
(ESP32-S3FN8, SX1262 a 868 MHz, OLED 0,96", sin PSRAM, 8 MB de flash). Núcleo actual: entrenador Koch
estilo LCWO y práctica en pareja por LoRa. A medio plazo: keyer iámbico, decodificador desde transceptor,
salida key, GPS/locator, POTA/SOTA, propagación y línea gris.

## Estado actual (8 oct 2026)
- Firmware: `src/main.cpp` **v0.4c**. Arduino + PlatformIO, env `heltec_wifi_lora_32_V3`,
  `platform = espressif32 @ 6.13.0` (core Arduino 2.0.17), librerías RadioLib ^6.6, Adafruit GFX y SSD1306.
  **Un solo `.cpp` en `src/`** (si hay dos, falla el enlazado).
- Desarrollo con **dos Heltec V3 desnudas**: la salida CW va por el LED (GPIO35) y los menús se manejan
  con el botón PRG (GPIO0), pulsación corta/larga.
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
- Las pruebas son con LED, pantalla y monitor serie: no hay audio conectado (no proponer pruebas "a oído").
- La versión está en `#define FW_VERSION` (al principio de `main.cpp`): se ve en la pantalla 1 y por serie.
  Subirla en cada cambio que se cargue en placa.
- Radio (v0.4c, `#define LORA_*`): 868,1 MHz, SF7, 250 kHz, CR 4/7, sync 0x12, 10 dBm, TCXO 1,8 V (revisado:
  DIO2 RF switch lo activa RadioLib 6.6 en `begin()`; SPI 9/11/10 correcto). RSSI a 2 m: unos -40/-45 dBm
  (con SF9/125 kHz y 868,0 eran -31 dBm; sin comparar en las mismas condiciones).

## Pendiente conocido (por prioridad; orden confirmado por el usuario)
1. Koch: estadísticas por carácter (hoy la corrección solo cuenta errores) y modo Echo con el keyer.
   Hecho en v0.3i: tabla en orden LCWO (41 caracteres, con `=`), lección N = N+1 caracteres.
2. Trama LoRa v2 hecha en v0.4a (formato en el comentario de `main.cpp`). Falta: indicativo configurable por
   placa (hoy constante `INDICATIVO`), reproducir a los wpm del emisor y pantalla para textos largos.
3. Un paquete que llega durante `radio.transmit()` (bloqueante) se pierde al borrar el flag tras TX.
4. A futuro: motor CW en una tarea FreeRTOS, audio I2S, keyer iámbico, decodificador (Goertzel).

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