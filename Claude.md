# Mr. Morse EA4ILN — contexto del proyecto

## Qué es
Dispositivo portátil de CW para radioaficionados (indicativo EA4ILN) sobre una **Heltec WiFi LoRa 32 V3**
(ESP32-S3FN8, SX1262 a 868 MHz, OLED 0,96", sin PSRAM, 8 MB de flash). Núcleo actual: entrenador Koch
estilo LCWO y práctica en pareja por LoRa. A medio plazo: keyer iámbico, decodificador desde transceptor,
salida key, GPS/locator, POTA/SOTA, propagación y línea gris.

## Estado actual (8 oct 2026)
- Firmware: `src/main.cpp` **v0.3d**. Arduino + PlatformIO, env `heltec_wifi_lora_32_V3`,
  librerías RadioLib ^6.6, Adafruit GFX y SSD1306. **Un solo `.cpp` en `src/`** (si hay dos, falla el enlazado).
- Desarrollo con **dos Heltec V3 desnudas**: la salida CW va por el LED (GPIO35) y los menús se manejan
  con el botón PRG (GPIO0), pulsación corta/larga.
- **No migrar a `pins.h` ni al hardware nuevo hasta que el firmware sea estable y se monte el circuito.**
- La entrada está abstraída en `leerEventoBoton()` (BOTON_CORTA / BOTON_LARGA): al montar el rotary y el
  pulsador de función solo habrá que cambiar esa función, no los menús.
- `SPEAKER_PWM_PIN` está en GPIO7 de forma provisional (el GPIO25 del código antiguo no existe en el S3).
- Revisión hecha: v0.3b -> v0.3c (temporizador entre caracteres, semilla con `esp_random()`, flag LoRa tras TX,
  `salirModoLoRa()`) -> v0.3d (botón por eventos, sin `delay`).

## Pendiente conocido (por prioridad; orden confirmado por el usuario)
1. Un paquete LoRa que llega mientras suena otro se pierde (falta cola).
2. Estadísticas 3.5: la línea mide 22 caracteres (132 px > 128) y el `%` salta de línea; la lección 40 no
   aparece (`paginaStats3_5 < 13` solo cubre 39).
3. `"¿CONFIRMAR BORRADO?"` muestra basura (la fuente por defecto no es UTF-8).
4. Al ir de la pantalla 3 a la 2 queda `opcionNavegacionP1234 = 0` (entra con "ant." seleccionado).
5. Koch: la lección 1 genera un solo carácter (usar `leccionCW + 1`); verificar el orden de la tabla frente
   a LCWO / Morserino; estadísticas por carácter y modo Echo con el keyer.
6. `dibujarEncabezado()` lee la batería (~25 ms bloqueantes) en cada redibujado y hay refresco cada 3 s:
   puede alargar un elemento CW. Cachear la batería y redibujar solo cuando cambie algo.
7. Trama LoRa: `sizeof` = 16 bytes (el comentario dice 14), máximo 5 caracteres. Migrar al formato variable.
8. `radio.begin()`: comprobar `setDio2AsRfSwitch(true)` y el voltaje del TCXO para la V3 si el alcance es pobre.
   Verificar también que el SPI usa los pines 9/10/11 (board `heltec_wifi_lora_32_V3`).
9. Fijar la versión de `platform = espressif32 @ ...` en `platformio.ini`: el core Arduino 3.x elimina
   `ledcSetup()` / `ledcAttachPin()`.
10. A futuro: motor CW en una tarea FreeRTOS, audio I2S, keyer iámbico, decodificador (Goertzel).

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