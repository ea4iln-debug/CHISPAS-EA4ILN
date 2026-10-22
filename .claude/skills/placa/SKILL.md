---
name: placa
description: Cargar el firmware de Chispas en una placa por USB y comprobarlo - identificar la placa (1 placa perforada o 2 protoboard, por la MAC) y su entorno, pio upload, leer el arranque y los mensajes con serie.py, capturar la OLED. Usar para probar cambios en el hardware real (GPS, BME280, audio, KEY, etc.).
---

# Probar en placa

## 1. Qué placa hay conectada
- Ver los puertos: `~/.platformio/penv/Scripts/pio.exe device list`. El de la Heltec es el CP210x (VID 10C4, PID
  EA60). **El número de COM cambia** (16, 23…): sacarlo siempre de la lista, nunca de memoria. Si no hay ninguno,
  pedir al usuario en una línea que la conecte.
- Las dos placas tienen el mismo adaptador; se distinguen por la MAC del ESP32, que `pio run -t upload` imprime
  (`MAC: ...`). Si la MAC no es ninguna de las de abajo, es una placa nueva: cargar igual (mismo entorno) y apuntar
  su MAC en NOTAS, sin preguntar.
- Entornos: las dos placas llevan encoder → `heltec_wifi_lora_32_V3`. Nombres desde el 15 oct 2026: **placa 1 =
  placa perforada** (MAC `3c:0f:02:ee:08:c0`), **placa 2 = Heltec nueva desnuda** (MAC `44:1b:f6:fa:ab:fc`, desde el 23 oct 2026; la antigua
  placa 2, protoboard con MAC `3c:0f:02:ee:50:74` y OLED averiada, se devolvió).
  `heltec_sin_encoder` existio hasta la v1.10; desde la 1.20 hay un solo entorno.

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
- Las palas se simulan con `-e "m<texto>"` (entra como lo manipulado). Antes de pedir nada al usuario, probar todo
  lo que se pueda por serie y con capturas.
- Lo que no se ve por serie (audio, LED, pantalla real, palas de verdad, KEY), pedírselo al usuario en texto, todo
  junto y concreto ("¿suena el tono al pulsar DIT?"), no con AskUserQuestion: es una comprobación, no una decisión.
- Si la placa no conecta al WiFi o falta algo del entorno, buscar otra forma de probar (datos guardados, orden serie)
  o dejarlo como pendiente en el resumen, en vez de parar a preguntar.

## 4. Informar
Separar lo **probado en placa** (con qué placa y versión) de lo **razonado**. Si se ha cambiado un ajuste de la
placa para la prueba (depuración, locator), recordarlo o dejarlo como estaba.
