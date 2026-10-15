---
name: placa
description: Cargar el firmware de Chispas en una placa por USB y comprobarlo - identificar la placa (1 placa perforada o 2 protoboard, por la MAC) y su entorno, pio upload, leer el arranque y los mensajes con serie.py, capturar la OLED. Usar para probar cambios en el hardware real (GPS, BME280, audio, KEY, etc.).
---

# Probar en placa

## 1. Qué placa hay conectada
- Ver los puertos: `~/.platformio/penv/Scripts/pio.exe device list`. El de la Heltec es el CP210x (VID 10C4, PID EA60;
  COM16 en el equipo 1 y COM23 en el 2, pero puede cambiar). Si no hay ninguno, pedir al usuario que la conecte.
- Las dos placas tienen el mismo adaptador; se distinguen por la MAC del ESP32, que `pio run -t upload` imprime
  (`MAC: ...`). Tabla en `docs/NOTAS.md`, sección "Estado actual" (MAC de cada placa). Si la tabla no la tiene aún,
  **preguntar al usuario qué placa es** antes de cargar y, tras cargar, apuntar la MAC.
- Entornos: las dos placas llevan encoder → `heltec_wifi_lora_32_V3`. Nombres desde el 15 oct 2026: **placa 1 =
  placa perforada** (MAC `3c:0f:02:ee:08:c0`), **placa 2 = protoboard** (MAC `3c:0f:02:ee:50:74`, OLED con defecto).
  `heltec_sin_encoder` solo existe hasta la v1.10 (Heltec desnuda): **el entorno equivocado deja la placa sin manejo**.

## 2. Cargar
- Cerrar antes cualquier `serie.py` o monitor abierto (el puerto no se comparte).
- `~/.platformio/penv/Scripts/pio.exe run -e <entorno> -t upload` (compila si hace falta). Si falla al conectar:
  pedir que mantenga PRG y pulse RST, o que pruebe otro cable.
- La carga reinicia la placa; la NVS (ajustes, estadísticas) se conserva.

## 3. Comprobar
- Arranque y mensajes: `~/.platformio/penv/Scripts/python.exe serie.py -s 10` (sin reiniciar la placa; log en
  `.pio/serie-*.log`). Debe salir `[INIT] Chispas EA4ILN v<FW_VERSION>`.
- Filtros útiles: `-f "DIAG|DECOD|CW"` (activar antes en CONF > CONFIG > DEPURACION), `-f "^\$G"` (NMEA del GPS).
- Órdenes serie (con ORDENES SERIE en SI en DEPURACION): `-e p` vuelca la OLED; `d`, `i`, `k`, `l`, `a` simulan el
  encoder. Captura de pantalla: `~/.platformio/penv/Scripts/python.exe docs/captura_oled.py --salida <png>` y
  mirar el PNG.
- Lo que no se ve por serie (audio, LED, pantalla real, palas, KEY), pedírselo al usuario con una pregunta concreta
  ("¿suena el tono al pulsar DIT?").

## 4. Informar
Separar lo **probado en placa** (con qué placa y versión) de lo **razonado**. Si se ha cambiado un ajuste de la
placa para la prueba (depuración, locator), recordarlo o dejarlo como estaba.
