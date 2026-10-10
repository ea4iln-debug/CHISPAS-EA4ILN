# Chispas EA4ILN

**Entrenador de telegrafía (CW) portátil para radioaficionados, basado en la Heltec WiFi LoRa 32 V3.**

![Plataforma](https://img.shields.io/badge/plataforma-ESP32--S3-blue)
![Placa](https://img.shields.io/badge/placa-Heltec%20WiFi%20LoRa%2032%20V3-orange)
![Framework](https://img.shields.io/badge/framework-Arduino%20%2B%20PlatformIO-teal)
![Versión](https://img.shields.io/badge/firmware-v0.7k-green)
![Estado](https://img.shields.io/badge/estado-en%20desarrollo-yellow)
![Licencia del software](https://img.shields.io/badge/software-GPL--3.0-blue)
![Licencia del hardware](https://img.shields.io/badge/hardware%20y%20docs-CC%20BY--NC--SA%204.0-lightgrey)

> *"Chispas"* era el apodo que se daba al radiotelegrafista de a bordo.

Chispas reúne en un aparato de bolsillo lo necesario para aprender y practicar CW: un entrenador por el método Koch,
un keyer iámbico, un modo de repetición (Echo), práctica en pareja por radio LoRa y utilidades de estación, como la
propagación, los spots POTA y la línea gris. Todo se maneja con un encoder rotativo y una pantalla OLED.

---

## Índice

- [Funciones](#funciones)
- [Hardware](#hardware)
- [Conexiones](#conexiones)
- [Compilar y cargar el firmware](#compilar-y-cargar-el-firmware)
- [Manejo](#manejo)
- [Pantallas](#pantallas)
- [Configuración por WiFi (webconf)](#configuración-por-wifi-webconf)
- [Estructura del repositorio](#estructura-del-repositorio)
- [Detalles técnicos](#detalles-técnicos)
- [Hoja de ruta](#hoja-de-ruta)
- [Avisos](#avisos)
- [Créditos y referencias](#créditos-y-referencias)
- [Licencia](#licencia)
- [Autor](#autor)

---

## Funciones

| Función | Descripción | Estado |
|---|---|---|
| **Entrenador Koch** | Orden de caracteres de LCWO (lecciones 1–40), grupos aleatorios configurables (wpm, espaciado, caracteres y grupos). La letra nueva de cada lección sale el 25 % de las veces. Corrección de errores y estadísticas por lección. | ✅ |
| **Keyer iámbico** | Modos A y B, 10–40 wpm, palas normales o invertidas. Descodifica en pantalla lo que se manipula. | ✅ |
| **Modo Echo** | Chispas emite letras de la lección Koch y hay que repetirlas con las palas. Indica el acierto o, si fallas, lo manipulado y el código correcto. | ✅ |
| **Duo por LoRa** | Práctica en pareja entre dos Chispas: uno envía grupos y el otro los recibe y los copia. Tramas digitales a 868 MHz, sin audio. | ✅ |
| **Propagación** | Índices solares y condiciones por banda de [hamqsl.com](https://www.hamqsl.com/). | ✅ |
| **Spots POTA** | Activaciones en CW publicadas en [pota.app](https://pota.app/). | ✅ |
| **Sol y línea gris** | Salida y puesta del sol, ventanas de línea gris y altura actual del sol para el locator de la estación. Funciona sin conexión. | ✅ |
| **Batería** | Tensión y porcentaje. Detecta cuando no hay batería conectada. | ✅ |
| **Cara de Chispas** | Salvapantallas animado con expresiones y gestos. | ✅ |
| **Configuración por WiFi** | Portal web en castellano, con QR para unirse desde el móvil. | ✅ |
| **Tono por buzzer y auriculares** | Tono CW con volumen y silencio automático del buzzer al conectar auriculares. Firmware listo. | 🚧 montaje |
| **Salida KEY** | Manipulación del transceptor, aislada con optoacoplador. | 🚧 |
| **Decodificador CW** | Descodificación del audio del transceptor. | 🚧 |
| **GPS** | Posición y locator automáticos. | 🚧 |

---

## Hardware

### Placa principal
- **[Heltec WiFi LoRa 32 V3](https://heltec.org/project/wifi-lora-32-v3/)**: ESP32-S3FN8 (8 MB de flash, sin PSRAM), radio
  LoRa SX1262 a 868 MHz, pantalla OLED de 0,96" (128×64, SSD1306) y cargador de batería LiPo integrado.

### Componentes del prototipo
El prototipo actual se monta en protoboard, sin PCB. Componentes principales:

- Encoder rotativo **KY-040** con pulsador.
- Manipulador de **palas** con clavija estéreo de 3,5 mm.
- **Buzzer piezoeléctrico pasivo** (no activo ni magnético), potenciómetro de volumen de 10 kΩ logarítmico y jack de
  auriculares con conmutación.
- Optoacoplador **PC817** para la salida KEY.
- Módulo **GPS** NEO-6M (en camino).
- Batería **LiPo** de 3,7 V con conector JST de 1,25 mm. Comprueba la polaridad antes de conectarla.

El esquema completo, con la lista de componentes y el orden de montaje, está en
[`docs/esquema_protoboard.pdf`](docs/esquema_protoboard.pdf).

---

## Conexiones

| GPIO | Conector | Uso | Estado |
|---|---|---|---|
| 2 | J3-13 | Salida KEY (PC817, 330 Ω; se recomienda un pull-down de 10 kΩ) | 🚧 |
| 4 | J3-15 | Pala de punto (DIT), pull-up interno | ✅ |
| 5 | J3-16 | Pala de raya (DAH), pull-up interno | ✅ |
| 6 | J3-17 | Entrada de audio del decodificador (ADC1) | 🚧 |
| 7 | J3-18 | Tono CW por PWM (buzzer y auriculares) | 🚧 |
| 19 / 20 | J2-18 / J2-17 | Encoder CLK / DT | ✅ |
| 48 | J2-14 | Pulsador del encoder | ✅ |
| 47 | J2-13 | RX desde el TX del GPS | 🚧 |
| 3 | J3-14 | Enable del GPS (opcional) | 🚧 |
| 41 / 42 | J3-8 / J3-7 | I2C para un sensor BME280 (opcional) | 🚧 |

Notas:
- El pin 1 de cada conector es el de pad cuadrado, el más cercano al USB, y es GND. La alimentación de 3,3 V está en J3-2/3.
- Todos los módulos van a **3,3 V**. Los GPIO del ESP32-S3 no admiten 5 V.
- GPIO19 y GPIO20 son las líneas USB D−/D+ del S3, pero funcionan como entradas porque la V3 programa a través de un
  conversor CP2102 (probado en placa).

---

## Compilar y cargar el firmware

### Requisitos
- [Visual Studio Code](https://code.visualstudio.com/) con la extensión [PlatformIO IDE](https://platformio.org/), o
  la línea de órdenes de PlatformIO (`pio`).
- Cable USB-C con datos.

Las dependencias se instalan solas al compilar. Están fijadas en [`platformio.ini`](platformio.ini):

| Componente | Versión |
|---|---|
| Plataforma `espressif32` (core Arduino 2.0.17) | 6.13.0 |
| [RadioLib](https://github.com/jgromes/RadioLib) | ^6.6 |
| [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) y [SSD1306](https://github.com/adafruit/Adafruit_SSD1306) | ^1.11 / ^2.5 |
| [WiFiManager](https://github.com/tzapu/WiFiManager) | ^2.0.17 |
| [ArduinoJson](https://arduinojson.org/) | ^7.2 |

### Pasos
```bash
git clone https://github.com/ea4iln-debug/CHISPAS-EA4ILN.git
cd CHISPAS-EA4ILN
pio run                                  # compila el firmware
pio run -t upload --upload-port COMx     # lo carga en la placa (en Linux/macOS, /dev/ttyUSB0 o similar)
pio device monitor                       # monitor serie a 115200 baudios
```

### Programas de prueba
En [`pruebas/`](pruebas/) hay programas independientes para comprobar el hardware sin tocar el firmware:

| Entorno | Para qué |
|---|---|
| `pio run -e prueba_pines -t upload` | Comprueba los pines del encoder e imprime por serie cada cambio y el sentido de giro |
| `pio run -e prueba_bateria -t upload` | Mide la lectura de batería (media y dispersión) con y sin batería conectada |

Después de usarlos, vuelve a cargar el firmware con `pio run -t upload`.

---

## Manejo

Todo se hace con el **encoder**:

| Acción | Efecto |
|---|---|
| **Girar** | Mueve el foco por las opciones de la pantalla, incluidas las flechas `ant.` y `sig.` para cambiar de pantalla |
| **Pulsar** | Ejecuta la opción con el foco |
| **Pulsar sobre un valor** | Entra en edición (el valor sale en vídeo inverso): girar lo cambia y pulsar otra vez lo confirma |
| **Pulsación larga** | Atajo en algunas pantallas, como salir de la práctica o repetir |
| **Botón PRG** (corto) | *Atrás*: sale de la edición o del submenú y, en las pantallas principales, vuelve a Inicio |

Al cambiar de pantalla, el foco se queda en la flecha que has usado, así que basta con pulsar varias veces seguidas
para recorrer las pantallas en un sentido.

---

## Pantallas

| N.º | Pantalla | Contenido |
|---|---|---|
| 1 | **Inicio** | Nombre, indicativo y versión. Tras unos segundos sin uso aparece la cara de Chispas. |
| 2 | **CW** | Entrenador: `KOCH` (configuración, práctica y estadísticas), `KEYER` y `ECHO` |
| 3 | **LoRa** | `CW DUO` (práctica en pareja) y `CW QSO` (en desarrollo) |
| 4 | **PROP.** | Propagación: índices solares y condiciones por banda |
| 5 | **POTA** | Spots de activaciones en CW |
| 6 | **SOL** | Salida y puesta del sol y línea gris |
| 7 | **CONF.** | Estación, radio LoRa, batería y versión; acceso a webconf y reset de fábrica |

La cabecera muestra la hora (local o UTC, a elegir), el nombre de la pantalla y la batería (`PWR` si no hay batería).

---

## Configuración por WiFi (webconf)

Chispas solo enciende el WiFi cuando lo necesita: en PROP. y POTA, y en SOL si aún no tiene hora. La red, el
indicativo, el locator y la hora se configuran desde el móvil:

1. Ve a **CONF.**, pon el foco en **webconf** y pulsa. Chispas crea la red `Chispas-XXXX` (clave `chispas73`) y
   muestra los datos en pantalla.
2. Únete a esa red. Con la opción **ver qr** aparece un QR que el móvil escanea para unirse directamente.
3. Si la página no se abre sola, entra en **http://192.168.4.1**.
4. En **Red WiFi** eliges tu red y en **Estación y hora** pones el indicativo, el locator, la hora (formato
   `HH:MM DD-MM-AAAA`) y si quieres la hora UTC en la cabecera.

Si en PROP., POTA o SOL no hay red guardada o no conecta, Chispas no abre el portal por su cuenta: muestra un aviso
con la opción **abrir webconf**.

> La clave del portal está definida en `PORTAL_CLAVE`, en `src/main.cpp`. Cámbiala si vas a usar Chispas en un lugar público.

---

## Estructura del repositorio

```
├── src/main.cpp                    Firmware completo (un solo archivo .cpp)
├── include/wm_strings_chispas.h    Textos del portal WiFiManager en castellano
├── pruebas/                        Programas de prueba del hardware (entornos aparte)
├── docs/
│   ├── esquema_protoboard.pdf      Esquema de conexiones del prototipo (6 hojas)
│   └── gen_esquema.py              Generador del esquema (HTML/SVG → PDF)
├── platformio.ini                  Configuración de PlatformIO y dependencias
├── LICENSE                         Licencia del software (GPL-3.0)
├── LICENSE-HARDWARE.md             Licencia del hardware y la documentación (CC BY-NC-SA 4.0)
└── CLAUDE.md                       Notas de desarrollo: estado, decisiones y pendientes
```

---

## Detalles técnicos

- **Temporización CW en tareas FreeRTOS.** La emisión (motor CW) y el keyer corren en tareas de mayor prioridad que
  el bucle principal, con plazos absolutos sobre ticks de 1 ms. La pantalla, la radio o el WiFi no alteran el ritmo
  del Morse (retraso medido: 0 ms).
- **Encoder por interrupción**, con decodificador de cuadratura por tabla que filtra los rebotes.
- **Duo por LoRa con tramas digitales**, no audio: un byte por carácter (bit centinela; punto = 0, raya = 1), cabecera
  con número de secuencia, wpm e indicativo. Antes de transmitir se escucha el canal (CAD).
- **Radio:** 868,1 MHz, SF7, 250 kHz, CR 4/7, 10 dBm.
- **Detección de batería ausente:** con USB y sin batería, el cargador de la V3 deja una tensión media similar a la de
  una batería llena, pero oscila. Chispas mide la dispersión de una ráfaga de lecturas para distinguir los dos casos.
- **QR de WiFi** generado con el componente `qrcode` de ESP-IDF incluido en el core, sin librerías adicionales.
- **Cálculo solar** con las ecuaciones de la NOAA a partir del locator Maidenhead.
- **Persistencia** en NVS: configuración del entrenador, keyer y Echo, estadísticas y datos de la estación.

---

## Hoja de ruta

- [ ] Tono por buzzer piezo y auriculares con volumen (firmware listo; pendiente de montaje)
- [ ] Salida KEY aislada para manipular el transceptor
- [ ] Decodificador CW desde el audio del transceptor
- [ ] GPS: locator y hora automáticos
- [ ] Estadísticas por carácter (letras más falladas en Koch y Echo)
- [ ] QSO por LoRa con el keyer y textos largos en Duo
- [ ] Medida y optimización del consumo; modo de bajo consumo
- [ ] PCB propia

---

## Avisos

- **LoRa a 868 MHz:** Chispas transmite en la banda ISM de 868 MHz (sub-banda 868,0–868,6 MHz, 25 mW, ciclo de
  trabajo del 1 %). Respeta la normativa de tu país.
- **Salida KEY:** manipular un transceptor de radioaficionado requiere la licencia correspondiente.
- **SOTA:** no hay cliente de SOTA. Las condiciones de su API exigen aprobación previa para software de terceros
  como este.
- **APIs externas:** los datos de propagación y de POTA vienen de servicios públicos no oficiales. Chispas los consulta
  con moderación (cada 30 y 2 minutos).

---

## Créditos y referencias

- Método Koch y orden de caracteres de [LCWO.net](https://lcwo.net/).
- [Morserino-32](https://www.morserino.info/), como referencia de diseño.
- Datos de propagación: [hamqsl.com](https://www.hamqsl.com/) (N0NBH). Spots: [pota.app](https://pota.app/).
- Librerías: [RadioLib](https://github.com/jgromes/RadioLib), [Adafruit GFX y SSD1306](https://github.com/adafruit),
  [WiFiManager](https://github.com/tzapu/WiFiManager) (MIT; la traducción de `include/wm_strings_chispas.h` parte de
  su archivo en español) y [ArduinoJson](https://arduinojson.org/).

---

## Licencia

Chispas tiene dos licencias, según el tipo de contenido:

| Parte | Licencia | Archivo |
|---|---|---|
| **Software**: firmware (`src/`), programas de prueba (`pruebas/`), `include/` y scripts (`docs/gen_esquema.py`) | [GNU GPL v3.0 o posterior](https://www.gnu.org/licenses/gpl-3.0.html) | [`LICENSE`](LICENSE) |
| **Hardware y documentación**: esquema, lista de componentes, diseño del montaje, README e imágenes | [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/deed.es) | [`LICENSE-HARDWARE.md`](LICENSE-HARDWARE.md) |

- **Software (GPL-3.0):** es software libre. Puedes usarlo, estudiarlo, modificarlo y redistribuirlo; si distribuyes
  el firmware o una versión modificada, debes hacerlo con la misma licencia y entregar el código fuente.
- **Hardware y documentación (CC BY-NC-SA 4.0):** puedes compartirlos y adaptarlos citando al autor, **sin fines
  comerciales** y con la misma licencia. Para un uso comercial, contacta con el autor.

`include/wm_strings_chispas.h` deriva de WiFiManager y conserva su aviso de licencia MIT. Las librerías externas
mantienen sus propias licencias.

---

## Autor

**EA4ILN**. Proyecto personal de radioafición. 73!
