# Hoja de ruta de Chispas

Estado a 12 de octubre de 2026, con el firmware **v1.02b** (beta). Lo marcado ✅ está hecho y probado en placa.

## 1. Pruebas pendientes

Funciones que ya están en el firmware pero falta comprobar en la placa.

| | Prueba | Cómo |
|---|---|---|
| ⬜ | Batería después del decodificador | Usar DECODIFICAR o QSO (TRX) con la batería puesta, salir y ver que el porcentaje se sigue actualizando |
| ⬜ | Xiegu X6200 con KEY | Emisora en llave recta, con el puente R-T de J4. Si salen punto y raya alternados, la emisora está en modo iámbico |
| ⬜ | QSO del TRX con emisora real | Manipular y decodificar en la misma sesión; comprobar que no se descodifica lo propio (sordo 300 ms tras KEY) |
| ⬜ | Keyer iámbico | Apretón de las dos palas en modo A y en modo B, y textos largos |
| ⬜ | QSO LIBRE en los dos sentidos | Necesita palas en la placa 2 (o cambiar las palas de placa) |
| ⬜ | Estabilidad tras muchas horas | Una noche con `[DIAG]` cada minuto en la pantalla POTA: memoria libre, mínimo, mayor bloque y pilas de las tareas |
| ✅ | OTA del binario sin encoder a la v1.02b | Placa 2, 12 oct 2026 |
| ✅ | Dormir y despertar en la placa 1 | 12 oct 2026 (antes solo en la placa 2) |
| ✅ | Consumo en deep sleep | Despreciable, medido en µA (12 oct 2026) |

## 2. Hardware

### En camino
- **Amplificador PAM8302A (U2) y altavoz de 8 Ω (LS1):** en el sitio del piezo (hoja 4 del esquema), con R9 de 10 kΩ.
  Ajustar la ganancia en banco y vigilar los picos en el 3V3 (la OLED de la placa 1 es sensible).
  **SD a GPIO38** (J3-11), para que el firmware lo apague cuando no se usa: el 3V3 sigue encendido en deep sleep y,
  sin SD, el amplificador gastaría unos 4 mA también dormido.
- **GPS NEO-6M:** TX del GPS a GPIO47 (J2-13). Opcional: alimentación conmutada con AO3401A (Q1) y R8 en GPIO3.

### Por comprar
- Divisor de minijack 1 → 2, para seguir oyendo la emisora con el decodificador conectado.

### Opcional
- **BME280** en Wire1 (GPIO41/42): temperatura, presión y humedad.
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
- **BME280** (si se monta): datos en alguna pantalla.

### Ideas decididas
- **POTA sin conexión:** lista de parques en un CSV guardado en la placa (partición de 1,5 MB reservada, LittleFS);
  buscar por referencia y, con GPS, el parque más cercano.
- **Cola de spots POTA:** publicar el spot propio al activar un parque (indicativo, referencia, frecuencia, CW) y, si
  no hay cobertura, guardarlo y enviarlo en cuanto haya WiFi. Antes, revisar las condiciones de la API de POTA para
  publicar.
- **ESP-NOW para Duo y QSO:** las tramas no dependen de la radio; enviarlas por ESP-NOW permitiría practicar con
  placas ESP32 sin LoRa, a corta distancia (50–200 m) y sin router.
- **Audio I2S:** tono de mejor calidad con un DAC o amplificador I2S en lugar de PWM.

### Por decidir
- **SOTA:** las condiciones de su API exigen aprobación previa para software generado con IA.

## 4. Documentación y publicación
- Al montar el amplificador y el GPS: esquema (quitar PENDIENTE), README y capturas.
- Rehacer la captura `status.png` del README (muestra la v1.00b) con el locator genérico.
- Próxima release (v1.03b) cuando haya algo nuevo probado: previsiblemente, amplificador y GPS.

## Orden propuesto
1. **Sin piezas:** batería después del decodificador, prueba de estabilidad de una noche, keyer iámbico y X6200.
2. **Al llegar el amplificador:** montaje, ganancia y control de SD en el firmware.
3. **Al llegar el GPS:** montaje, locator y hora automáticos; release v1.03b.
4. **Después:** POTA sin conexión, cola de spots, ESP-NOW, audio I2S y PCB.

## Hecho
- ✅ Entrenador Koch estilo LCWO con corrección por letra o por total, Echo y TX libre (keyer iámbico A/B).
- ✅ Estadísticas por carácter y por lección (Koch y Echo), y refuerzo de las letras con más errores.
- ✅ Práctica en pareja y QSO libre por LoRa.
- ✅ Tono por buzzer y auriculares con volumen.
- ✅ Salida KEY aislada con tune y memorias (probada con una uSDR).
- ✅ Decodificador CW del audio de la emisora (probado con audio Morse real).
- ✅ Propagación, spots POTA, sol y línea gris.
- ✅ Configuración en el aparato, página de estado y depuración desde el menú.
- ✅ Actualización por WiFi desde las releases de GitHub.
- ✅ Consumo en reposo despreciable.
