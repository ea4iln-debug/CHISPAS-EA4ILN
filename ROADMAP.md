# Hoja de ruta de Chispas

Estado a 12 de octubre de 2026, con el firmware **v1.03b** (beta). Lo marcado ✅ está hecho y probado en placa.

## 1. Pruebas pendientes

Funciones que ya están en el firmware pero falta comprobar en la placa.

| | Prueba | Cómo |
|---|---|---|
| ⬜ | QSO LIBRE en los dos sentidos | Necesita palas en la placa 2 (o cambiar las palas de placa) |
| ✅ | Estabilidad tras muchas horas | Placa 2, noche del 12 oct 2026: 8 h en POTA (233 consultas HTTPS); heap libre ±150 B, mínimo estable tras la 1.ª hora, mayor bloque y pilas sin cambios |
| ✅ | OTA del binario sin encoder a la v1.02b y a la v1.03b | Placa 2, 12 oct 2026 |
| ✅ | Batería después del decodificador | Placa 2, 12 oct 2026: la tensión se sigue leyendo bien tras DECODIFICAR y QSO |
| ✅ | QSO del TRX con emisora real | 12 oct 2026: manipular y decodificar en la misma sesión, sin descodificar lo propio |
| ✅ | Keyer iámbico | 12 oct 2026: apretón en modo A y B, y textos largos |
| ✅ | Xiegu X6200 con KEY | 12 oct 2026, en llave recta con el puente R-T de J4: TUNE, palas y memorias |
| ✅ | Dormir y despertar en la placa 1 | 12 oct 2026 (antes solo en la placa 2) |
| ✅ | Consumo en deep sleep | Despreciable, medido en µA (12 oct 2026) |

## 2. Hardware

### En camino
- **Amplificador PAM8302A (U2) y altavoz de 8 Ω (LS1)** (llegan el 12 oct): en el sitio del piezo (hoja 4 del esquema), con R9 de 10 kΩ.
  Ajustar la ganancia en banco y vigilar los picos en el 3V3 (la OLED de la placa 1 es sensible).
  **SD a GPIO38** (J3-11), para que el firmware lo apague cuando no se usa: el 3V3 sigue encendido en deep sleep y,
  sin SD, el amplificador gastaría unos 4 mA también dormido.
- **GPS NEO-6M** (llega el 13 oct): TX del GPS a GPIO47 (J2-13). Opcional: alimentación conmutada con AO3401A (Q1) y R8 en GPIO3.

### Por comprar
- Divisor de minijack 1 → 2, para seguir oyendo la emisora con el decodificador conectado.

### Opcional
- **Trafo de aislamiento 600:600**, solo si aparece zumbido por lazo de masa con la emisora.
- **C3 (10–100 nF) de GPIO6 a GND**, solo si el decodificador da ruido o letras falsas (por ejemplo, al transmitir
  con potencia): filtra la RF y el ruido de alta frecuencia y estabiliza la lectura del ADC.
- **Detección de USB y de carga:** divisor 100k/150k del pin 5V (J2-2) a GPIO33. Antes, comprobar que el pin 5V da
  0 V solo con batería.

### Más adelante
- **PCB propia**, empezando desde cero.

## 3. Desarrollo del firmware

### Con el hardware nuevo
- **Amplificador:** SD en BAJO fuera de CW, LoRa y TRX y durante el sueño (retenido con `gpio_hold`), y en ALTO
  cuando hace falta el tono.
- **GPS:** locator y hora automáticos (UART por la matriz GPIO, NMEA a 9600 bd); el locator alimenta SOL y la hora
  sustituye al NTP cuando no hay WiFi.
- **BME280, lo que queda** (lo básico está hecho en la v1.03b):
  - Comprobar la tendencia y el aviso de tormenta con datos de varias horas.
  - Con el GPS: calibrar la altitud con la del GPS y reiniciar el historial de presión al cambiar de altitud
    (subiendo a un monte la presión baja y puede dar un falso aviso de tormenta).
  - Por decidir: mantener el historial de presión con Chispas dormido (despertar cada 15 min).

### Ideas decididas
- **POTA sin conexión:** lista de parques en un CSV guardado en la placa (partición de 1,5 MB reservada, LittleFS);
  buscar por referencia y, con GPS, el parque más cercano.
- **Cola de spots POTA:** publicar el spot propio al activar un parque (indicativo, referencia, frecuencia, CW) y, si
  no hay cobertura, guardarlo y enviarlo en cuanto haya WiFi. Antes, revisar las condiciones de la API de POTA para
  publicar.
- **Batería en `[DIAG]`:** añadir la tensión y el % a la línea de diagnóstico (p. ej. `| BAT 4.12V 90%`) para ver
  en los registros largos la curva de carga y descarga, y medir la autonomía con un registro en vez de a mano.

### Por decidir
- **Batería llena = 100 %:** tras cargar, la LiPo se relaja a ~4,10–4,15 V y la curva marca ~89–90 % con el LED de
  carga apagado (carga terminada). Propuesta: contar 4,15 V o más como 100 %.
- **SOTA:** las condiciones de su API exigen aprobación previa para software generado con IA.

### A estudiar (no prioritario)
- **Fork de bajo coste con ESP-NOW** (fuera del proyecto principal): una unidad con un ESP32 cualquiera, sin LoRa,
  que haga Duo y QSO por ESP-NOW a corta distancia (50–200 m) y sin router; las tramas no dependen de la radio.
- **Audio I2S:** tono senoidal con rampas (sin clics) y volumen por software con un módulo DAC + amplificador I2S
  (p. ej. MAX98357A, 3 GPIO). Sustituiría al PAM8302A y obligaría a rehacer la salida de auriculares.
- **GPS ATGM336H** en lugar del NEO-6M: más pequeño, más barato y de menor consumo (GPS + BeiDou), con la misma
  conexión (UART a 9600 bd, NMEA). Por ahora se sigue con el NEO-6M.

## 4. Documentación y publicación
- Al montar el amplificador y el GPS: esquema (quitar PENDIENTE), README y capturas.
- Próxima release (v1.04b) cuando haya algo nuevo probado: previsiblemente, amplificador y GPS.

## Orden propuesto
1. **Sin piezas:** QSO LIBRE en los dos sentidos; tendencia del BME280 con varias horas de datos (placa 1).
2. **Al llegar el amplificador:** montaje, ganancia y control de SD en el firmware.
3. **Al llegar el GPS:** montaje, locator y hora automáticos; release v1.04b.
4. **Después:** POTA sin conexión, cola de spots y PCB.

## Hecho
- ✅ Entrenador Koch estilo LCWO con corrección por letra o por total, Echo y TX libre (keyer iámbico A/B).
- ✅ Estadísticas por carácter y por lección (Koch y Echo), y refuerzo de las letras con más errores.
- ✅ Práctica en pareja y QSO libre por LoRa.
- ✅ Tono por buzzer y auriculares con volumen.
- ✅ Salida KEY aislada con tune y memorias (probada con una uSDR).
- ✅ Decodificador CW del audio de la emisora (probado con audio Morse real).
- ✅ Propagación, spots POTA, sol y línea gris.
- ✅ Configuración en el aparato, página de estado y depuración desde el menú.
- ✅ Sensor ambiental BME280 (v1.03b): página AMBIENTE en SOL, tendencia y aviso de tormenta, altitud barométrica
  calibrable y macro {WX}.
- ✅ Actualización por WiFi desde las releases de GitHub.
- ✅ Consumo en reposo despreciable.
