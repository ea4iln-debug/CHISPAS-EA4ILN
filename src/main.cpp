/**
 * @file main.cpp
 * @author Chispas EA4ILN Project
 * @brief Firmware para Heltec WiFi LoRa 32 V3 (ESP32-S3) - Aprendizaje CW y LoRa Duo.
 * @version 0.7t - La memoria de la estacion pasa a llamarse "estacion" (indicativo, locator y hora UTC empiezan de cero)
 * @date 2026-10-09
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 EA4ILN
 *
 * Este programa es software libre: puedes redistribuirlo y/o modificarlo bajo los terminos de la
 * GNU General Public License publicada por la Free Software Foundation, version 3 o (a tu eleccion)
 * cualquier version posterior. Se distribuye con la esperanza de que sea util, pero SIN NINGUNA
 * GARANTIA, ni siquiera la implicita de COMERCIABILIDAD o IDONEIDAD PARA UN FIN DETERMINADO.
 * Consulta el archivo LICENSE o <https://www.gnu.org/licenses/>.
 */

#define FW_VERSION "0.7t"  // Se muestra en la pantalla 1 y por serie al arrancar: cambiarla en cada version

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RadioLib.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include "qrcode.h"  // Componente qrcode de ESP-IDF, incluido en el core (esp_qrcode_generate)
#include <nvs.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

// ============================================================================
// DEFINICION DE PINES NATIVOS (Heltec WiFi LoRa 32 V3 / ESP32-S3)
// ============================================================================
#define OLED_SDA        17  // Bus I2C - Datos SSD1306
#define OLED_SCL        18  // Bus I2C - Reloj SSD1306
#define OLED_RST        21  // Reset del OLED
#define VEXT_CTRL       36  // Control de alimentacion VExt (Low = ON)

#define BUTTON_PIN      0   // Boton PRG integrado
#define LED_PIN         35  // LED blanco integrado en placa

#define LORA_NSS        8   // Bus SPI - Select del SX1262
#define LORA_DIO1       14  // Pin de interrupcion Hardware SX1262
#define LORA_NRST       12  // Reset HW del transceptor SX1262
#define LORA_BUSY       13  // Estado ocupado del SX1262

// Parametros de radio LoRa (las dos placas deben usar los mismos)
// 868.1 MHz cae entero en la sub-banda 868.0-868.6 (25 mW, 1 % de ciclo de trabajo).
// SF7 / 250 kHz: unos 32 ms de aire por trama de 18 bytes (SF9 / 125 kHz: unos 226 ms).
#define LORA_FREC_MHZ     868.1
#define LORA_BW_KHZ       250.0
#define LORA_SF           7
#define LORA_CR           7      // 4/7
#define LORA_SYNC_WORD    0x12   // Red privada
#define LORA_POTENCIA_DBM 10
#define LORA_PREAMBULO    8
// La V3 alimenta el TCXO por DIO3 a 1.8 V (Meshtastic heltec_v3: SX126X_DIO3_TCXO_VOLTAGE 1.8);
// el valor por defecto de RadioLib es 1.6 V. DIO2 como RF switch ya lo activa RadioLib en begin().
#define LORA_TCXO_V       1.8
// Escucha del canal (CAD) antes de transmitir
#define LORA_CAD_INTENTOS      3
#define LORA_ESPERA_OCUPADO_MS 100  // Una trama de 45 bytes con SF7/250 kHz dura unos 60 ms

#define KEYER_PADDLE_DIT 4  // Entrada manipulador yambico (Punto), punta del jack J1
#define KEYER_PADDLE_DAH 5  // Entrada manipulador yambico (Raya), anillo del jack J1
#define KEY_OUT_PIN      2  // Salida KEY a la emisora por el PC817: ALTO = manipulando. En BAJO desde el arranque
// Encoder KY-040. 19/20 son USB D-/D+ del S3, pero probados en placa como entradas: funcionan
#define ENC_CLK_PIN     19  // Pull-up de 10 kOhm en el modulo
#define ENC_DT_PIN      20  // Pull-up de 10 kOhm en el modulo
#define ENC_SW_PIN      48  // Pulsador del eje, sin pull-up en el modulo: INPUT_PULLUP
#define SPEAKER_PWM_PIN  7  // Salida PWM provisional (GPIO libre del header). El GPIO25 no existe en el S3

#define FREC_AUDIO_HZ    600// Tono Morse local (600 Hz)
#define CANAL_PWM_AUDIO  0  // Canal PWM del ESP32-S3
#define RES_PWM_AUDIO    8  // Resolucion de 8 bits para el PWM

#define VBAT_ADC_PIN    1   // Pin ADC para lectura del divisor de voltaje de la bateria
#define ADC_CTRL_PIN    37  // Habilitacion del divisor de tension de bateria
// Sin bateria y con USB, en estas placas el cargador deja una media de unos 4,17 V (como una bateria
// llena) pero oscila: medido con pruebas/prueba_bateria.cpp, dispersion de 0,25 V sin bateria frente
// a 0,002 V con bateria (rafaga de lecturas seguidas). Por eso se mira la dispersion, no la tension.
// El umbral de 2,6 V (de Meshtastic, que anota unos 2,2 V sin bateria) se deja por si acaso.
#define VBAT_SIN_BATERIA            2.6f
#define VBAT_DISPERSION_SIN_BATERIA 0.08f  // V: por encima, no hay bateria
#define VBAT_LECTURAS               200    // Lecturas seguidas por medida (unos 18 ms)
#define VBAT_MULTIPLIER 4.9f   // Divisor 390k/100k de la V3. Calibrado con polimetro: 4,10 V reales / 836,7 mV en el pin (2,5 dB)
#define TIEMPO_PULSACION_LARGA 600 // Umbral en ms para detectar pulsacion larga del boton PRG
#define TIEMPO_ANTIRREBOTE_MS  30  // Pulsaciones mas cortas se ignoran (rebotes)
#define TIEMPO_PULSACION_DORMIR 2500  // Solo sin encoder: PRG tan largo = dormir (la larga normal = ejecutar)

// ============================================================================
// DICCIONARIO MORSE Y TABLA KOCH
// ============================================================================
// Orden Koch de LCWO (igual que lcwoKochChars del Morserino-32): la leccion N usa los
// N+1 primeros caracteres, asi que la leccion 1 es K M y la 40 los 41 caracteres
const char TABLA_KOCH[] = {
  'K', 'M', 'U', 'R', 'E', 'S', 'N', 'A', 'P', 'T',
  'L', 'W', 'I', '.', 'J', 'Z', '=', 'F', 'O', 'Y',
  ',', 'V', 'G', '5', '/', 'Q', '9', '2', 'H', '3',
  '8', 'B', '?', '4', '7', 'C', '1', 'D', '6', '0',
  'X'
};
const uint8_t TOTAL_CARACTERES_KOCH = sizeof(TABLA_KOCH);

// Numero de caracteres de la tabla que entran en una leccion (1..40)
uint8_t caracteresDeLeccion(uint8_t leccion) {
  return constrain(leccion + 1, 2, TOTAL_CARACTERES_KOCH);
}

// La letra nueva de la leccion (la ultima que entra) sale al menos este % de las veces.
// Con reparto uniforme saldria 1 de cada N: en la leccion 40, un 2,4 %
#define PESO_LETRA_NUEVA_PCT 25

char caracterKochAleatorio(uint8_t leccion) {
  uint8_t n = caracteresDeLeccion(leccion);
  // Con pocas letras (4 o menos) el reparto uniforme ya le da ese % o mas
  if (n * PESO_LETRA_NUEVA_PCT <= 100) return TABLA_KOCH[random(0, n)];
  if (random(100) < PESO_LETRA_NUEVA_PCT) return TABLA_KOCH[n - 1];
  return TABLA_KOCH[random(0, n - 1)];  // El resto, repartido entre las demas
}

struct PatronMorse {
  char c;
  const char* codigo;
};

const PatronMorse DICCIONARIO_MORSE[] = {
  {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},   {'E', "."},
  {'F', "..-."},  {'G', "--."},   {'H', "...."},  {'I', ".."},    {'J', ".---"},
  {'K', "-.-"},   {'L', ".-.."},  {'M', "--"},    {'N', "-."},    {'O', "---"},
  {'P', ".--."},  {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
  {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},  {'Y', "-.--"},
  {'Z', "--.."},  {'1', ".----"}, {'2', "..---"}, {'3', "...--"}, {'4', "....-"},
  {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
  {'0', "-----"}, {'?', "..--.."},{'.', ".-.-.-"},{'/', "-..-."}, {',', "--..--"},
  {'=', "-...-"}
};

const char* obtenerCodigoMorse(char c) {
  for (size_t i = 0; i < sizeof(DICCIONARIO_MORSE)/sizeof(PatronMorse); i++) {
    if (DICCIONARIO_MORSE[i].c == c) return DICCIONARIO_MORSE[i].codigo;
  }
  return "";
}

// ============================================================================
// TRAMA DIGITAL LORA v2 (LONGITUD VARIABLE)
// ============================================================================
// Byte 0-1  : 'M' 'M'
// Byte 2    : version (4 bits altos) | tipo (4 bits bajos)
// Byte 3    : seq, numero de trama (para descartar duplicados)
// Byte 4    : wpm del emisor
// Byte 5-12 : indicativo, 8 caracteres rellenos con 0
// Byte 13.. : de 1 a TRAMA_MAX_CARACTERES bytes, uno por caracter Morse.
//   Cada byte es un 1 centinela seguido de los elementos, del primero al ultimo
//   (punto = 0, raya = 1). Ej.: K (-.-) = 0b1101 = 0x0D. Caben hasta 7 elementos.
//   0x00 = espacio entre palabras.
#define TRAMA_VERSION          2
#define TRAMA_TIPO_GRUPO_DUO   1
#define TRAMA_LONG_CABECERA    13
#define TRAMA_LONG_INDICATIVO  8
#define TRAMA_MAX_CARACTERES   32
#define TRAMA_LONG_MAX         (TRAMA_LONG_CABECERA + TRAMA_MAX_CARACTERES)
#define MORSE_ESPACIO_PALABRA  0x00
#define MORSE_NO_VALIDO        0x01  // Centinela sin elementos: caracter sin codigo Morse

// Datos de la estacion: se guardan en NVS (espacio NVS_ESTACION) y se cambian desde el portal WiFi
#define NVS_ESTACION "estacion"
#define INDICATIVO_POR_DEFECTO "EA4ILN"
#define LOCATOR_POR_DEFECTO    "IN80dk"  // Madrid (generico): cada estacion pone el suyo en webconf
char indicativoEstacion[TRAMA_LONG_INDICATIVO + 1] = INDICATIVO_POR_DEFECTO;
char locatorEstacion[7] = LOCATOR_POR_DEFECTO;
bool horaUtcCabecera = false;  // Hora de la cabecera en UTC ("12:05Z") en vez de local; se elige en webconf
uint8_t seqTx = 0;                   // Se inicializa al azar en setup()

uint8_t codificarCaracterMorse(char c) {
  if (c == ' ') return MORSE_ESPACIO_PALABRA;
  const char* codigo = obtenerCodigoMorse(c);
  size_t n = strlen(codigo);
  if (n == 0 || n > 7) return MORSE_NO_VALIDO;

  uint8_t b = 1;
  for (size_t i = 0; i < n; i++) b = (b << 1) | (codigo[i] == '-' ? 1 : 0);
  return b;
}

// Devuelve el caracter de un byte de la trama: ' ' para el espacio y '*' si el
// patron no esta en el diccionario
char decodificarCaracterMorse(uint8_t b) {
  if (b == MORSE_ESPACIO_PALABRA) return ' ';

  int8_t centinela = 7;
  while (centinela > 0 && !((b >> centinela) & 1)) centinela--;
  if (centinela == 0) return '*';

  char patron[8];
  uint8_t n = 0;
  for (int8_t bit = centinela - 1; bit >= 0; bit--) patron[n++] = ((b >> bit) & 1) ? '-' : '.';
  patron[n] = '\0';

  for (size_t i = 0; i < sizeof(DICCIONARIO_MORSE)/sizeof(PatronMorse); i++) {
    if (strcmp(DICCIONARIO_MORSE[i].codigo, patron) == 0) return DICCIONARIO_MORSE[i].c;
  }
  return '*';
}

// ============================================================================
// OBJETOS Y VARIABLES GLOBALES
// ============================================================================
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);
Preferences prefs;

// Interrupcion por hardware para recepcion LoRa
volatile bool flagPaqueteRecibido = false;

void IRAM_ATTR cbInterrupcionLora() {
  flagPaqueteRecibido = true;
}

uint8_t pantallaActual = 1;         
// Pantallas principales, en el orden en que se recorren con ant. / sig.
#define PANTALLA_INICIO      1
#define PANTALLA_CW          2  // Entrenador Koch
#define PANTALLA_LORA        3  // Duo / QSO
#define PANTALLA_PROPAGACION 4
#define PANTALLA_SPOTS       5
#define PANTALLA_SOL         6
#define PANTALLA_CONFIG      7
const uint8_t TOTAL_PANTALLAS = 7;
uint8_t opcionNavegacionP1234 = 1;  // 0 = "ant.", 1 = "sig."

int contadorPulsaciones = 0;
String estadoLoraStr = "INICIANDO...";
bool modoRadioRxActivo = false;

bool editandoValor = false;  // Con el encoder: el valor con el foco se cambia girando (sale en video inverso)
bool caraActiva = false;     // Se esta viendo la cara del robot (salvapantallas de Inicio)
float ultimoPctKoch = -1;    // Ultimo resultado de leccion Koch (-1 = ninguno); la cara lo celebra
unsigned long ultimoPctKochMs = 0;

struct StatsLeccion {
  uint16_t vecesCompletada;
  uint8_t maxAcierto;
  uint8_t minAcierto;
};
StatsLeccion statsKoch[40];

bool enSubpantallaCW = false;
bool enPractica3_2 = false;
bool enEstadisticas3_5 = false;
#define OPCION_BARRA_CW 3
uint8_t opcionPantalla3 = OPCION_BARRA_CW; // 0=KOCH, 1=KEYER, 2=ECHO, 3=BARRA INFERIOR (ant/sig)

// Keyer iambico (pantalla CW > KEYER). La configuracion se guarda en NVS al salir
uint8_t keyerWpm = 20;
bool keyerModoB = false;            // false = modo A
bool keyerPalasInvertidas = false;  // true = punta de la clavija = raya
bool enKeyer = false;               // Subpantalla del keyer abierta
uint8_t opcionKeyer = 0;            // 0 = wpm, 1 = modo, 2 = palas, 3 = borrar, 4 = volver
String textoKeyer = "";             // Lo descodificado de la manipulacion

// Modo Echo (pantalla CW > ECHO): Chispas emite letras de la leccion Koch y hay que repetirlas con las palas
enum EstadoEcho { ECHO_ESPERA, ECHO_REPRODUCIENDO, ECHO_ESCUCHANDO, ECHO_RESULTADO };
bool enEcho = false;
uint8_t opcionEcho = 3;          // 0 = wpm, 1 = largo, 2 = ver, 3 = iniciar/repetir, 4 = volver
uint8_t echoLargo = 1;           // Letras por reto (1-5)
bool echoVerObjetivo = true;     // Ensenar la letra antes de manipularla (para aprender)
EstadoEcho estadoEcho = ECHO_REPRODUCIENDO;
String echoObjetivo = "";
String echoRespuesta = "";
bool echoAcierto = false;
uint16_t echoAciertos = 0, echoIntentos = 0;
unsigned long echoFinResultadoMs = 0;

uint8_t wpmCW = 25;
uint16_t espaciadoCW = 200;
uint8_t leccionCW = 4;
uint8_t caracteresCW = 5;
uint8_t gruposCW = 20;
uint8_t opcionSubpantalla = 5;

enum EstadoCW { 
  CW_IDLE, 
  CW_GENERAR_GRUPO, 
  CW_EMITIENDO_GRUPO,  // El grupo esta en el motor CW
  CW_PAUSA_ENTRE_GRUPOS, 
  CW_PAUSADO, 
  CW_PANTALLA_3_3_LISTADO,      
  CW_PANTALLA_CORREGIR_ERRORES, 
  CW_PANTALLA_RESUMEN_FINAL     
};
EstadoCW estadoCW = CW_IDLE;

String grupoActualStr = "";
String textoCompletoLeccion = ""; 
uint8_t grupoActualNum = 0;
bool mostrarTextoGrupo = false;

uint16_t totalCaracteresLeccion = 0;
uint16_t numeroErrores = 0;
uint8_t paginaListado3_3 = 0;    
uint8_t opcionFinListado3_3 = 0; 
uint8_t opcionPantallaCorregir = 0; 
uint8_t opcionPractica = 0;  // Practica en marcha: 0 = pausa/seguir, 1 = salir
uint8_t opcionResumen = 0;   // Resultado: 0 = volver, 1 = repetir

uint8_t paginaStats3_5 = 0;
#define PAGINAS_STATS_3_5 14    // 40 lecciones / 3 por pagina = 14 paginas; la 15 (indice 14) es la de borrado
bool confirmandoBorrado = false;

unsigned long tiempoCWProximo = 0;

// Variables de estado para Pantalla 4 (Transceptor LoRa)
bool enPantalla4_Sub = false;  
uint8_t opcionPantalla4 = 2;   // 0=CW DUO, 1=CW QSO, 2=BARRA INFERIOR (ant/sig)
uint8_t subpantalla4Activa = 0;

uint8_t opcionDuo4_1 = 3;      
String grupoDuoTransmitido = "";
String grupoDuoRecibido = "";
bool grupoDuoRevelado = false;

bool hayGrupoDuoPorRevelar() {
  return grupoDuoRecibido.length() > 0 && !grupoDuoRevelado;
}


bool emitiendoMorseDuo = false;  // Hay un grupo de Duo en el motor CW
unsigned long finEmisionDuo = 0;  // Instante en que termino la ultima emision (para el espacio entre grupos)

// Cola circular de grupos recibidos por LoRa pendientes de reproducir
#define COLA_RX_TAM 8
char colaRxDuo[COLA_RX_TAM][TRAMA_MAX_CARACTERES + 1];
uint8_t colaRxWpm[COLA_RX_TAM];
uint8_t wpmEmisionDuo = 20;  // Velocidad de lo que esta sonando en Duo (local o del emisor)
#define WPM_MIN_DUO 5
#define WPM_MAX_DUO 60
uint8_t colaRxIni = 0;
uint8_t colaRxNum = 0;

uint8_t modoKeyer4_2 = 0;
int rssiUltimo = 0;
float snrUltimo = 0.0;

// ============================================================================
// DEPURACION: MEDIDA DE LA TEMPORIZACION CW
// ============================================================================
// Mide cuanto llega tarde cada transicion (encender/apagar) respecto a lo programado.
// Ese retraso alarga el elemento o la pausa anterior. Se informa por serie al final
// de cada grupo. Poner a 0 para quitarlo del firmware. Solo lo usa la tarea del motor CW.
#define DEBUG_CW 1

#if DEBUG_CW
uint32_t medidaCWNum = 0;
uint32_t medidaCWSuma = 0;
uint32_t medidaCWMax = 0;

void registrarRetrasoCW(uint32_t retraso) {
  medidaCWNum++;
  medidaCWSuma += retraso;
  if (retraso > medidaCWMax) medidaCWMax = retraso;
}

void informarRetrasoCW(const char* origen) {
  if (medidaCWNum == 0) return;
  Serial.printf("[CW %s] %u transiciones, retraso medio %.1f ms, max %u ms\n",
                origen, medidaCWNum, (float)medidaCWSuma / medidaCWNum, medidaCWMax);
  medidaCWNum = 0;
  medidaCWSuma = 0;
  medidaCWMax = 0;
}
#endif

// ============================================================================
// CONTROL DE AUDIO Y LED MORSE
// ============================================================================
void conmutarEmisionMorse(bool activar) {
  digitalWrite(LED_PIN, activar ? HIGH : LOW);
  if (activar) {
    ledcWrite(CANAL_PWM_AUDIO, 128); 
  } else {
    ledcWrite(CANAL_PWM_AUDIO, 0);
  }
}

// ============================================================================
// MOTOR CW EN UNA TAREA FREERTOS
// ============================================================================
// El LED y el tono los maneja una tarea con mas prioridad que loop() y en su mismo nucleo:
// la temporizacion ya no depende de lo que bloquee loop() (OLED, LoRa, WiFi, bateria).
// loop() pide un texto con motorCWEmitir(), lo corta con motorCWDetener() y mira si ha
// terminado con motorCWOcupado(). Solo la tarea llama a conmutarEmisionMorse().
#define MOTOR_CW_TEXTO_MAX 64
#define MOTOR_CW_PRIORIDAD 3     // loop() corre con prioridad 1
#define MOTOR_CW_NUCLEO    1     // El de loop() (CONFIG_ARDUINO_RUNNING_CORE); el WiFi va en el 0
#define MOTOR_CW_PILA      4096

struct PeticionCW {
  uint32_t id;
  char texto[MOTOR_CW_TEXTO_MAX + 1];
  uint8_t wpm;
  uint16_t espaciado;  // % sobre el espacio entre caracteres y entre palabras
  const char* origen;  // Para la depuracion por serie
};

TaskHandle_t tareaMotorCW = NULL;
QueueHandle_t colaMotorCW = NULL;
// Cada peticion lleva un id creciente: la tarea esta ocupada mientras no haya terminado la ultima
volatile uint32_t motorCWPedidas = 0;     // Lo escribe loop()
volatile uint32_t motorCWCancelada = 0;   // Lo escribe loop(): las peticiones con id <= este se cortan
volatile uint32_t motorCWTerminadas = 0;  // Lo escribe la tarea
volatile unsigned long motorCWFinMs = 0;  // millis() al acabar la ultima emision

bool motorCWOcupado() {
  return motorCWTerminadas != motorCWPedidas;
}

/**
 * Espera hasta el tick objetivo. Devuelve false si la peticion se ha cancelado: motorCWDetener()
 * despierta la tarea con una notificacion, asi que no hay que esperar a que acabe la pausa.
 */
bool esperarMotorCW(TickType_t objetivo, uint32_t id) {
  for (;;) {
    if (id <= motorCWCancelada) return false;
    TickType_t ahora = xTaskGetTickCount();
    if ((int32_t)(objetivo - ahora) <= 0) {
#if DEBUG_CW
      registrarRetrasoCW(ahora - objetivo);
#endif
      return true;
    }
    ulTaskNotifyTake(pdTRUE, objetivo - ahora);
  }
}

/**
 * Emite el texto con plazos absolutos (cada uno se suma al anterior, sin acumular retrasos).
 * Los espacios marcan el espacio entre palabras; los caracteres sin codigo Morse se saltan.
 */
void emitirTextoMotorCW(const PeticionCW& p) {
  uint32_t tDit = 1200 / p.wpm;
  uint32_t tCharGap = (tDit * 3 * p.espaciado) / 100;
  uint32_t tWordGap = (tDit * 7 * p.espaciado) / 100;

  TickType_t objetivo = xTaskGetTickCount();
  bool primero = true;
  bool hayEspacio = false;
  for (const char* c = p.texto; *c != '\0'; c++) {
    if (*c == ' ') { hayEspacio = true; continue; }
    const char* codigo = obtenerCodigoMorse(*c);
    if (*codigo == '\0') continue;

    if (!primero) {
      objetivo += pdMS_TO_TICKS(hayEspacio ? tWordGap : tCharGap);
      if (!esperarMotorCW(objetivo, p.id)) return;
    }
    primero = false;
    hayEspacio = false;

    for (const char* e = codigo; *e != '\0'; e++) {
      if (e != codigo) {
        objetivo += pdMS_TO_TICKS(tDit);
        if (!esperarMotorCW(objetivo, p.id)) return;
      }
      conmutarEmisionMorse(true);
      objetivo += pdMS_TO_TICKS(*e == '-' ? tDit * 3 : tDit);
      if (!esperarMotorCW(objetivo, p.id)) return;
      conmutarEmisionMorse(false);
    }
  }
}

void tareaEmisionCW(void* parametro) {
  PeticionCW p;
  for (;;) {
    xQueueReceive(colaMotorCW, &p, portMAX_DELAY);
    emitirTextoMotorCW(p);
    conmutarEmisionMorse(false);  // Por si se ha cortado con el tono encendido
    motorCWFinMs = millis();
#if DEBUG_CW
    informarRetrasoCW(p.origen);
#endif
    motorCWTerminadas = p.id;
  }
}

void iniciarMotorCW() {
  colaMotorCW = xQueueCreate(1, sizeof(PeticionCW));
  xTaskCreatePinnedToCore(tareaEmisionCW, "motorCW", MOTOR_CW_PILA, NULL, MOTOR_CW_PRIORIDAD,
                          &tareaMotorCW, MOTOR_CW_NUCLEO);
}

/**
 * Corta lo que este sonando y espera a que la tarea apague LED y tono.
 */
void motorCWDetener() {
  if (!motorCWOcupado()) return;
  motorCWCancelada = motorCWPedidas;
  xTaskNotifyGive(tareaMotorCW);
  unsigned long inicio = millis();
  while (motorCWOcupado() && millis() - inicio < 50) vTaskDelay(1);
}

/**
 * Emite un texto (corta antes lo que estuviera sonando). Vuelve enseguida.
 */
void motorCWEmitir(const char* texto, uint8_t wpm, uint16_t espaciado, const char* origen) {
  motorCWDetener();

  PeticionCW p;
  p.id = motorCWPedidas + 1;
  strncpy(p.texto, texto, MOTOR_CW_TEXTO_MAX);
  p.texto[MOTOR_CW_TEXTO_MAX] = '\0';
  p.wpm = max(wpm, (uint8_t)1);
  p.espaciado = espaciado;
  p.origen = origen;
  if (xQueueSend(colaMotorCW, &p, 0) == pdTRUE) motorCWPedidas = p.id;
}

// ============================================================================
// KEYER IAMBICO EN UNA TAREA FREERTOS
// ============================================================================
// Lee las palas cada 1 ms y genera puntos y rayas con la misma prioridad que el motor CW
// (no suenan a la vez: el keyer solo esta activo en su pantalla). Como sabe que elementos
// ha generado, descodifica lo manipulado y lo pasa a loop() por una cola.
// Memoria de la pala contraria: modo A, solo si se pulsa en la pausa entre elementos;
// modo B, tambien durante el elemento (al soltar un apreton sale un elemento mas).
#define KEYER_WPM_MIN 10
#define KEYER_WPM_MAX 40
#define KEYER_PILA    3072

void guardarKeyerNVS();                // Definida en la seccion NVS
extern bool pantallaPendiente;         // Definida con la bateria
extern unsigned long ultimaActividadMs;  // Definida con la cara (ultimo giro, pulsacion o manipulacion)

TaskHandle_t tareaKeyer = NULL;
QueueHandle_t colaKeyerTexto = NULL;  // Caracteres descodificados, de la tarea a loop()
volatile bool keyerActivo = false;

char decodificarPatron(const char* patron) {
  for (size_t i = 0; i < sizeof(DICCIONARIO_MORSE) / sizeof(PatronMorse); i++) {
    if (strcmp(DICCIONARIO_MORSE[i].codigo, patron) == 0) return DICCIONARIO_MORSE[i].c;
  }
  return '*';
}

void leerPalas(bool& dit, bool& dah) {
  bool punta = digitalRead(KEYER_PADDLE_DIT) == LOW;
  bool anillo = digitalRead(KEYER_PADDLE_DAH) == LOW;
  dit = keyerPalasInvertidas ? anillo : punta;
  dah = keyerPalasInvertidas ? punta : anillo;
}

void tareaKeyerFn(void* parametro) {
  enum { K_ESPERA, K_ELEMENTO, K_PAUSA } estado = K_ESPERA;
  char elemento = 0;  // Ultimo elemento generado ('.' o '-')
  bool memoriaDit = false, memoriaDah = false;
  char patron[9];
  uint8_t largo = 0;              // Elementos del caracter en curso (8 = demasiados: error)
  bool espacioPendiente = false;  // Tras un caracter: falta ver si hay espacio entre palabras
  TickType_t finFase = 0, finUltimoElemento = 0;

  for (;;) {
    if (!keyerActivo) {
      conmutarEmisionMorse(false);
      estado = K_ESPERA;
      largo = 0;
      espacioPendiente = false;
      memoriaDit = memoriaDah = false;
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Hasta que se abra la pantalla del keyer
      continue;
    }

    vTaskDelay(1);
    TickType_t ahora = xTaskGetTickCount();
    TickType_t dit = pdMS_TO_TICKS(1200 / keyerWpm);
    bool pDit, pDah;
    leerPalas(pDit, pDah);
    char siguiente = 0;

    switch (estado) {
      case K_ELEMENTO:
        if (keyerModoB) {
          if (elemento == '.' && pDah) memoriaDah = true;
          if (elemento == '-' && pDit) memoriaDit = true;
        }
        if ((int32_t)(ahora - finFase) >= 0) {
          conmutarEmisionMorse(false);
          finUltimoElemento = ahora;
          finFase = ahora + dit;
          estado = K_PAUSA;
        }
        break;

      case K_PAUSA:
        if (elemento == '.' && pDah) memoriaDah = true;
        if (elemento == '-' && pDit) memoriaDit = true;
        if ((int32_t)(ahora - finFase) >= 0) {
          // Se alterna si hay memoria o la pala contraria esta pulsada; si no, se repite la misma
          if (elemento == '.') siguiente = (memoriaDah || pDah) ? '-' : (pDit ? '.' : 0);
          else siguiente = (memoriaDit || pDit) ? '.' : (pDah ? '-' : 0);
          memoriaDit = memoriaDah = false;
          if (siguiente == 0) estado = K_ESPERA;
        }
        break;

      case K_ESPERA:
        if (pDit || pDah) {
          siguiente = (pDit && pDah) ? (elemento == '.' ? '-' : '.') : (pDit ? '.' : '-');
        } else {
          // Silencio desde el ultimo elemento: 2 puntos cierran el caracter y 5, la palabra
          TickType_t silencio = ahora - finUltimoElemento;
          if (largo > 0 && silencio >= 2 * dit) {
            patron[min(largo, (uint8_t)8)] = '\0';
            char c = (largo <= 7) ? decodificarPatron(patron) : '*';
            xQueueSend(colaKeyerTexto, &c, 0);
            largo = 0;
            espacioPendiente = true;
          } else if (espacioPendiente && silencio >= 5 * dit) {
            char c = ' ';
            xQueueSend(colaKeyerTexto, &c, 0);
            espacioPendiente = false;
          }
        }
        break;
    }

    if (siguiente != 0) {
      elemento = siguiente;
      if (largo < 8) patron[largo] = siguiente;
      if (largo < 8) largo++;
      espacioPendiente = false;
      conmutarEmisionMorse(true);
      finFase = ahora + (siguiente == '.' ? dit : 3 * dit);
      estado = K_ELEMENTO;
    }
  }
}

void iniciarKeyer() {
  colaKeyerTexto = xQueueCreate(64, sizeof(char));
  xTaskCreatePinnedToCore(tareaKeyerFn, "keyer", KEYER_PILA, NULL, MOTOR_CW_PRIORIDAD,
                          &tareaKeyer, MOTOR_CW_NUCLEO);
}

void entrarEnKeyer() {
  motorCWDetener();
  enKeyer = true;
  opcionKeyer = 0;
  textoKeyer = "";
  xQueueReset(colaKeyerTexto);
  keyerActivo = true;
  xTaskNotifyGive(tareaKeyer);
}

void salirDeKeyer() {
  keyerActivo = false;  // La tarea apaga LED y tono en menos de 1 ms
  enKeyer = false;
  editandoValor = false;
  guardarKeyerNVS();
}

// Para y reanuda el keyer sin salir de la pantalla (en Echo, mientras Chispas emite).
// Al pararlo, la espera deja que la tarea apague el LED antes de que lo use el motor CW
void keyerPausar() {
  keyerActivo = false;
  vTaskDelay(3);
}

void keyerReanudar() {
  xQueueReset(colaKeyerTexto);
  keyerActivo = true;
  xTaskNotifyGive(tareaKeyer);
}

// ============================================================================
// MODO ECHO: CHISPAS EMITE Y HAY QUE REPETIRLO CON LAS PALAS
// ============================================================================
#define ECHO_PAUSA_ACIERTO_MS 1200
#define ECHO_PAUSA_FALLO_MS   3000

/**
 * Emite un reto. Con repetirMismo vuelve a emitir el actual (tras un fallo o con "repetir").
 * Las letras salen de la leccion Koch, con la letra nueva al 25 %.
 */
void echoNuevoReto(bool repetirMismo) {
  if (!repetirMismo || echoObjetivo.length() == 0) {
    echoObjetivo = "";
    for (uint8_t i = 0; i < echoLargo; i++) echoObjetivo += caracterKochAleatorio(leccionCW);
  }
  echoRespuesta = "";
  keyerPausar();
  motorCWEmitir(echoObjetivo.c_str(), keyerWpm, espaciadoCW, "ECHO");
  estadoEcho = ECHO_REPRODUCIENDO;
  pantallaPendiente = true;
}

void entrarEnEcho() {
  motorCWDetener();
  enEcho = true;
  opcionEcho = 3;
  echoAciertos = 0;
  echoIntentos = 0;
  echoObjetivo = "";
  echoRespuesta = "";
  estadoEcho = ECHO_ESPERA;  // No emite hasta pulsar "iniciar" (da tiempo a ajustar wpm, largo y ver)
}

void salirDeEcho() {
  keyerPausar();
  motorCWDetener();
  enEcho = false;
  editandoValor = false;
  guardarKeyerNVS();
}

void echoEvaluar() {
  echoIntentos++;
  echoAcierto = (echoRespuesta == echoObjetivo);
  if (echoAcierto) echoAciertos++;
  Serial.printf("\n[ECHO] %s -> %s: %s (%u/%u)\n", echoObjetivo.c_str(), echoRespuesta.c_str(),
                echoAcierto ? "BIEN" : "MAL", echoAciertos, echoIntentos);
  keyerPausar();
  estadoEcho = ECHO_RESULTADO;
  echoFinResultadoMs = millis() + (echoAcierto ? ECHO_PAUSA_ACIERTO_MS : ECHO_PAUSA_FALLO_MS);
  pantallaPendiente = true;
}

// Un caracter descodificado del keyer durante el Echo
void echoRecibirCaracter(char c) {
  if (estadoEcho != ECHO_ESCUCHANDO) return;
  if (c == ' ') {
    // Silencio largo con algo manipulado: se da por terminada la respuesta aunque sea corta
    if (echoRespuesta.length() > 0) echoEvaluar();
    return;
  }
  echoRespuesta += c;
  pantallaPendiente = true;
  if (echoRespuesta.length() >= echoObjetivo.length()) echoEvaluar();
}

void procesarEcho() {
  if (!enEcho) return;
  if (estadoEcho == ECHO_REPRODUCIENDO && !motorCWOcupado()) {
    keyerReanudar();  // Turno del usuario
    estadoEcho = ECHO_ESCUCHANDO;
    pantallaPendiente = true;
  } else if (estadoEcho == ECHO_RESULTADO && (long)(millis() - echoFinResultadoMs) >= 0) {
    echoNuevoReto(!echoAcierto);  // Tras un fallo se repite el mismo
  }
}

// Pasa a la pantalla lo que el keyer ha descodificado (y por serie, para depurar)
void procesarTextoKeyer() {
  char c;
  while (xQueueReceive(colaKeyerTexto, &c, 0) == pdTRUE) {
    ultimaActividadMs = millis();  // Manipular cuenta como uso (brillo y apagado automatico)
    Serial.print(c);
    if (enEcho) {
      echoRecibirCaracter(c);
      continue;
    }
    textoKeyer += c;
    if (textoKeyer.length() > 200) textoKeyer.remove(0, textoKeyer.length() - 200);
    pantallaPendiente = true;
  }
}

// ============================================================================
// MEMORIA PERSISTENTE NVS
// ============================================================================
// Entrenador (Koch, keyer, Echo y estadisticas)
#define NVS_ENTRENADOR "chispas"

extern uint8_t apagadoAutoMin;  // Minutos del apagado automatico; definida con las opciones de CONF

void cargarDatosNVS() {
  prefs.begin(NVS_ENTRENADOR, true);
  wpmCW = prefs.getUChar("wpm", 25);
  espaciadoCW = prefs.getUShort("sp", 200);
  leccionCW = prefs.getUChar("leccion", 4);
  caracteresCW = prefs.getUChar("caract", 5);
  gruposCW = prefs.getUChar("grupos", 20);
  keyerWpm = constrain(prefs.getUChar("kwpm", 20), 10, 40);  // KEYER_WPM_MIN/MAX
  keyerModoB = prefs.getBool("kmodob", false);
  keyerPalasInvertidas = prefs.getBool("kinv", false);
  echoLargo = constrain(prefs.getUChar("elargo", 1), 1, 5);
  echoVerObjetivo = prefs.getBool("ever", true);
  apagadoAutoMin = prefs.getUChar("apag", 30);  // ajustarApagado(0) lo lleva a un valor de la lista si no lo es

  for (int i = 0; i < 40; i++) {
    String kComp = "c_" + String(i);
    String kMax = "mx_" + String(i);
    String kMin = "mn_" + String(i);

    statsKoch[i].vecesCompletada = prefs.getUShort(kComp.c_str(), 0);
    statsKoch[i].maxAcierto = prefs.getUChar(kMax.c_str(), 0);
    statsKoch[i].minAcierto = prefs.getUChar(kMin.c_str(), 100);
  }
  prefs.end();
}

void guardarUltimaConfiguracionNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("wpm", wpmCW);
  prefs.putUShort("sp", espaciadoCW);
  prefs.putUChar("leccion", leccionCW);
  prefs.putUChar("caract", caracteresCW);
  prefs.putUChar("grupos", gruposCW);
  prefs.end();
}

void guardarApagadoNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("apag", apagadoAutoMin);
  prefs.end();
}

void guardarKeyerNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("kwpm", keyerWpm);
  prefs.putBool("kmodob", keyerModoB);
  prefs.putBool("kinv", keyerPalasInvertidas);
  prefs.putUChar("elargo", echoLargo);
  prefs.putBool("ever", echoVerObjetivo);
  prefs.end();
}

void registrarResultadoLeccionNVS(uint8_t lecc, float pct) {
  if (lecc < 1 || lecc > 40) return;
  uint8_t idx = lecc - 1;
  uint8_t aciertoInt = (uint8_t)constrain(pct, 0, 100);

  statsKoch[idx].vecesCompletada++;
  if (aciertoInt > statsKoch[idx].maxAcierto) statsKoch[idx].maxAcierto = aciertoInt;
  if (aciertoInt < statsKoch[idx].minAcierto || statsKoch[idx].vecesCompletada == 1) {
    statsKoch[idx].minAcierto = aciertoInt;
  }

  prefs.begin(NVS_ENTRENADOR, false);
  String kComp = "c_" + String(idx);
  String kMax = "mx_" + String(idx);
  String kMin = "mn_" + String(idx);

  prefs.putUShort(kComp.c_str(), statsKoch[idx].vecesCompletada);
  prefs.putUChar(kMax.c_str(), statsKoch[idx].maxAcierto);
  prefs.putUChar(kMin.c_str(), statsKoch[idx].minAcierto);

  prefs.putUChar("leccion", leccionCW);
  prefs.putUChar("wpm", wpmCW);
  prefs.putUShort("sp", espaciadoCW);
  prefs.putUChar("caract", caracteresCW);
  prefs.putUChar("grupos", gruposCW);

  prefs.end();
}

// Borra solo las estadisticas Koch; la configuracion del entrenador y los datos de la estacion se conservan
void borrarTodasLasEstadisticasNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  for (int i = 0; i < 40; i++) {
    prefs.remove(("c_" + String(i)).c_str());
    prefs.remove(("mx_" + String(i)).c_str());
    prefs.remove(("mn_" + String(i)).c_str());
  }
  prefs.end();

  for (int i = 0; i < 40; i++) {
    statsKoch[i].vecesCompletada = 0;
    statsKoch[i].maxAcierto = 0;
    statsKoch[i].minAcierto = 100;
  }
}

void cargarEstacionNVS() {
  prefs.begin(NVS_ESTACION, true);
  strlcpy(indicativoEstacion, prefs.getString("call", INDICATIVO_POR_DEFECTO).c_str(), sizeof(indicativoEstacion));
  strlcpy(locatorEstacion, prefs.getString("loc", LOCATOR_POR_DEFECTO).c_str(), sizeof(locatorEstacion));
  horaUtcCabecera = prefs.getBool("utc", false);
  prefs.end();
}

void guardarEstacionNVS() {
  prefs.begin(NVS_ESTACION, false);
  prefs.putString("call", indicativoEstacion);
  prefs.putString("loc", locatorEstacion);
  prefs.putBool("utc", horaUtcCabecera);
  prefs.end();
}

// ============================================================================
// LECTURA DE BATERIA (4.20V / 100%)
// ============================================================================
enum EstadoAlimentacion { SOLO_USB, BAT_DESCARGA };

struct BateriaInfo {
  float voltaje;
  int porcentaje;
  EstadoAlimentacion estado;
};

// Curva de descarga tipica de una celda Li-ion en reposo (tension -> %), interpolada.
// Con USB conectado el cargador sube la tension y el porcentaje sale alto.
int porcentajeLiIon(float v) {
  static const float tension[] = { 3.30, 3.60, 3.68, 3.72, 3.75, 3.78, 3.81, 3.86, 3.92, 4.00, 4.10, 4.20 };
  static const int   pct[]     = {    0,    5,   10,   20,   30,   40,   50,   60,   70,   80,   90,  100 };
  const int n = sizeof(tension) / sizeof(tension[0]);
  if (v <= tension[0]) return 0;
  if (v >= tension[n - 1]) return 100;
  for (int i = 1; i < n; i++) {
    if (v <= tension[i]) {
      return pct[i - 1] + (int)lround((v - tension[i - 1]) / (tension[i] - tension[i - 1]) * (pct[i] - pct[i - 1]));
    }
  }
  return 100;
}

BateriaInfo leerBateria() {
  pinMode(ADC_CTRL_PIN, OUTPUT);
  digitalWrite(ADC_CTRL_PIN, HIGH);
  delay(10);

  // Al pin llegan como mucho 4.2 V / 4.9 = 0.86 V: 2.5 dB (rango hasta unos 1.25 V) es mas preciso que 11 dB
  analogReadResolution(12);
  analogSetPinAttenuation(VBAT_ADC_PIN, ADC_2_5db);

  // Rafaga de lecturas seguidas (unos 18 ms): de ella salen la media y la dispersion
  uint32_t sumaMv = 0;
  uint64_t sumaCuadrados = 0;
  for (int i = 0; i < VBAT_LECTURAS; i++) {
    uint32_t mv = analogReadMilliVolts(VBAT_ADC_PIN);
    sumaMv += mv;
    sumaCuadrados += (uint64_t)mv * mv;
  }
  float mvPromedio = (float)sumaMv / VBAT_LECTURAS;
  float varianza = (float)sumaCuadrados / VBAT_LECTURAS - mvPromedio * mvPromedio;
  float dispersion = sqrtf(max(varianza, 0.0f)) * VBAT_MULTIPLIER / 1000.0f;  // En V de bateria

  digitalWrite(ADC_CTRL_PIN, LOW);

  float voltaje = (mvPromedio * VBAT_MULTIPLIER) / 1000.0;

  // Suavizado como Meshtastic: cada lectura se promedia al 50 % con la anterior, para que el
  // porcentaje no baile. La primera lectura (o la primera tras volver la bateria) arranca el filtro.
  static float voltajeFiltrado = 0;

  BateriaInfo bat;
  if (voltaje < VBAT_SIN_BATERIA || dispersion > VBAT_DISPERSION_SIN_BATERIA) {
    voltajeFiltrado = 0;
    bat.voltaje = 0.0;
    bat.porcentaje = 0;
    bat.estado = SOLO_USB;
  } else {
    voltajeFiltrado = (voltajeFiltrado == 0) ? voltaje : voltajeFiltrado + (voltaje - voltajeFiltrado) * 0.5;
    bat.voltaje = voltajeFiltrado;
    bat.porcentaje = porcentajeLiIon(voltajeFiltrado);
    bat.estado = BAT_DESCARGA;
  }
  return bat;
}

// leerBateria() bloquea unos 30 ms: se lee cada 30 s fuera de la emision CW y el
// dibujo usa este valor en cache
#define INTERVALO_BATERIA_MS 30000
BateriaInfo bateriaActual = {0.0, 0, SOLO_USB};
bool pantallaPendiente = false;  // Redibujar en cuanto no suene CW

// ============================================================================
// DIBUJO Y RENDERIZADO OLED (BARRA SUPERIOR: MAYÚSCULAS | INFERIOR: MINÚSCULAS)
// ============================================================================
bool horaLocalValida(struct tm& t) {
  return getLocalTime(&t, 0) && t.tm_year > 120;  // Sin sincronizar el reloj empieza en 1970
}

void dibujarEncabezado() {
  BateriaInfo bat = bateriaActual;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  String strBat = (bat.estado == SOLO_USB) ? "PWR" : String(bat.porcentaje) + "%";

  if (pantallaActual == PANTALLA_CW && enEstadisticas3_5) {
    uint8_t superadas = 0;
    for (int i = 0; i < 40; i++) {
      if (statsKoch[i].maxAcierto >= 90) superadas++;
    }
    uint8_t pctProgreso = (superadas * 100) / 40;

    display.setCursor(0, 0);
    display.print(F("["));
    uint8_t bloques = superadas / 5;
    for (uint8_t b = 0; b < 8; b++) {
      display.print(b < bloques ? "=" : ".");
    }
    display.print(F("] "));
    display.print(pctProgreso);
    display.print(F("%"));

    String txtRight = "BAT:" + strBat;
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(txtRight, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(128 - w, 0);
    display.print(txtRight);

  } else if ((pantallaActual == PANTALLA_CW && enPractica3_2) || (pantallaActual == PANTALLA_LORA && subpantalla4Activa == 1)) {
    display.setCursor(0, 0);
    display.print(wpmCW);
    display.print(F("WPM SP:"));
    display.print(espaciadoCW);
    display.print(F("%"));

    String txtRight = "BAT:" + strBat;
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(txtRight, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(128 - w, 0);
    display.print(txtRight);

  } else {
    // Hora local o UTC ("12:05Z", segun webconf), en cuanto se haya sincronizado por NTP
    // (el reloj interno la mantiene sin WiFi)
    struct tm local;
    if (horaLocalValida(local)) {
      char txtHora[8];
      if (horaUtcCabecera) {
        time_t ahora = time(nullptr);
        struct tm utc;
        gmtime_r(&ahora, &utc);
        snprintf(txtHora, sizeof(txtHora), "%02d:%02dZ", utc.tm_hour, utc.tm_min);
      } else {
        snprintf(txtHora, sizeof(txtHora), "%02d:%02d", local.tm_hour, local.tm_min);
      }
      display.setCursor(0, 0);
      display.print(txtHora);
    }

    String txtRight = "BAT:" + strBat;
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(txtRight, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(128 - w, 0);
    display.print(txtRight);

    // Nombre de la pantalla, centrado. Maximo 5 caracteres: entre el reloj y "BAT:100%"
    const char* nombre = "";
    switch (pantallaActual) {
      case PANTALLA_CW:          nombre = "CW";    break;
      case PANTALLA_LORA:        nombre = "LORA";  break;
      case PANTALLA_PROPAGACION: nombre = "PROP."; break;
      case PANTALLA_SPOTS:       nombre = "POTA";  break;
      case PANTALLA_SOL:         nombre = "SOL";   break;
      case PANTALLA_CONFIG:      nombre = "CONF."; break;
    }
    display.getTextBounds(nombre, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((128 - w) / 2, 0);
    display.print(nombre);
  }

  display.drawLine(0, 9, 128, 9, SSD1306_WHITE);
}

// Valor editable: en video inverso mientras se cambia girando el encoder
void imprimirValor(bool foco, const String& valor) {
  if (foco && editandoValor) display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  display.print(valor);
  display.setTextColor(SSD1306_WHITE);
}

void dibujarPieCentrado(const String& txt) {
  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 55);
  display.print(txt);
}

void dibujarBarraAvanzarRetroceder(bool enfocada, const String& centro = "") {
  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 55);
  if (enfocada && opcionNavegacionP1234 == 0) {
    display.print(F("> ant."));
  } else {
    display.print(F("  ant."));
  }

  String txtSig = (enfocada && opcionNavegacionP1234 == 1) ? "> sig." : "  sig.";
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(txtSig, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(128 - w, 55);
  display.print(txtSig);

  // En el centro: indicador de pagina ("1/2") o "« opc »" si las opciones solo salen al girar
  if (centro.length() > 0) {
    display.getTextBounds(centro, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((128 - w) / 2, 55);
    display.print(centro);
  }
}

// ============================================================================
// TRANSMISION Y RECEPCION LORA POR INTERRUPCION
// ============================================================================
void iniciarEmisionLocalDuo(String cadena, uint8_t wpm) {
  cadena.trim();
  if (cadena.length() == 0) return;

  wpmEmisionDuo = wpm;
  motorCWEmitir(cadena.c_str(), wpm, espaciadoCW, "DUO");
  emitiendoMorseDuo = true;
}

/**
 * Cuando el motor CW acaba el grupo de Duo, guarda el instante (para el espacio hasta el siguiente).
 */
void procesarFinEmisionDuo() {
  if (!emitiendoMorseDuo || motorCWOcupado()) return;
  emitiendoMorseDuo = false;
  finEmisionDuo = motorCWFinMs;
  // No se reinicia la recepcion: la radio ya vuelve a ella tras cada TX y RX, y
  // startReceive() aqui podria cortar una trama que estuviera llegando
}

void procesarRecepcionLoRa();  // Definida mas abajo

/**
 * Antes de transmitir: procesa una trama que haya llegado y aun no se haya leido (transmit()
 * machacaria el buffer) y escucha el canal (CAD). Si la otra placa esta emitiendo, se pone en
 * recepcion hasta que llegue su trama y vuelve a probar.
 */
void prepararCanalParaTransmitir() {
  if (flagPaqueteRecibido) procesarRecepcionLoRa();

  for (uint8_t intento = 0; intento < LORA_CAD_INTENTOS; intento++) {
    int estadoCad = radio.scanChannel();
    flagPaqueteRecibido = false;  // El fin del CAD tambien activa DIO1
    if (estadoCad != RADIOLIB_LORA_DETECTED) return;  // Libre (o error de CAD): se transmite

    Serial.println(F("[LORA TX] CANAL OCUPADO, ESPERANDO TRAMA"));
    radio.startReceive();
    unsigned long inicio = millis();
    while (!flagPaqueteRecibido && millis() - inicio < LORA_ESPERA_OCUPADO_MS) yield();
    if (flagPaqueteRecibido) procesarRecepcionLoRa();
  }
  Serial.println(F("[LORA TX] CANAL SIGUE OCUPADO, SE TRANSMITE IGUAL"));
}

void transmitirGrupoDuoLoRa(String grupo) {
  grupoDuoTransmitido = grupo;
  prepararCanalParaTransmitir();

  uint8_t trama[TRAMA_LONG_MAX];
  trama[0] = 'M';
  trama[1] = 'M';
  trama[2] = (TRAMA_VERSION << 4) | TRAMA_TIPO_GRUPO_DUO;
  trama[3] = seqTx++;
  trama[4] = wpmCW;
  memset(&trama[5], 0, TRAMA_LONG_INDICATIVO);
  memcpy(&trama[5], indicativoEstacion, min(strlen(indicativoEstacion), (size_t)TRAMA_LONG_INDICATIVO));

  size_t longitud = TRAMA_LONG_CABECERA;
  for (size_t i = 0; i < grupo.length() && longitud < TRAMA_LONG_MAX; i++) {
    uint8_t b = codificarCaracterMorse(grupo.charAt(i));
    if (b != MORSE_NO_VALIDO) trama[longitud++] = b;
  }
  if (longitud == TRAMA_LONG_CABECERA) return;  // Nada que enviar

  Serial.printf("[LORA TX] seq %u, %u bytes: ", trama[3], (unsigned)longitud);
  Serial.println(grupo);

  int estadoTx = radio.transmit(trama, longitud);
  
  if (estadoTx == RADIOLIB_ERR_NONE) {
    Serial.println(F("[LORA TX] PAQUETE ENVIADO CON EXITO!"));
  } else {
    Serial.print(F("[LORA TX] ERROR RADIOLIB: "));
    Serial.println(estadoTx);
  }

  radio.startReceive();

  // En el SX1262 el mismo pin DIO1 señala "TX terminado" y "paquete recibido",
  // asi que la ISR se dispara al acabar de transmitir. Se descarta ese aviso falso
  // (esto sustituye al antiguo filtro de "eco" por comparacion de texto).
  flagPaqueteRecibido = false;

  iniciarEmisionLocalDuo(grupo, wpmCW);
}

/**
 * Guarda un grupo recibido y los wpm a los que hay que reproducirlo.
 * Si la cola esta llena se descarta el grupo nuevo.
 */
void encolarGrupoRxDuo(const char* grupo, uint8_t wpm) {
  if (colaRxNum >= COLA_RX_TAM) {
    Serial.println(F("[LORA RX] COLA LLENA, GRUPO DESCARTADO"));
    return;
  }
  uint8_t pos = (colaRxIni + colaRxNum) % COLA_RX_TAM;
  strncpy(colaRxDuo[pos], grupo, TRAMA_MAX_CARACTERES);
  colaRxDuo[pos][TRAMA_MAX_CARACTERES] = '\0';
  colaRxWpm[pos] = wpm;
  colaRxNum++;
}

void actualizarPantalla();  // Definida mas abajo

/**
 * Si no suena nada y hay grupos en cola, reproduce el siguiente tras el espacio entre palabras.
 */
void procesarColaRxDuo() {
  if (emitiendoMorseDuo || colaRxNum == 0) return;

  uint32_t tDit = 1200 / wpmEmisionDuo;  // Pausa a la velocidad del grupo que acaba de sonar
  uint32_t tWordGap = (tDit * 7 * espaciadoCW) / 100;
  if (millis() - finEmisionDuo < tWordGap) return;

  grupoDuoRecibido = String(colaRxDuo[colaRxIni]);
  grupoDuoRevelado = false;
  uint8_t wpm = colaRxWpm[colaRxIni];
  colaRxIni = (colaRxIni + 1) % COLA_RX_TAM;
  colaRxNum--;

  // Se redibuja antes de empezar, para que el grupo nuevo aparezca a la vez que suena
  actualizarPantalla();
  pantallaPendiente = false;
  iniciarEmisionLocalDuo(grupoDuoRecibido, wpm);
}

void procesarRecepcionLoRa() {
  if (!flagPaqueteRecibido) return;
  flagPaqueteRecibido = false;

  // Fuera del modo LoRa se descarta cualquier aviso pendiente
  if (!modoRadioRxActivo) return;

  // Cabecera y al menos un caracter, sin pasar del maximo
  size_t longitud = radio.getPacketLength();
  if (longitud <= TRAMA_LONG_CABECERA || longitud > TRAMA_LONG_MAX) {
    Serial.printf("[LORA RX] LONGITUD NO VALIDA: %u\n", (unsigned)longitud);
    radio.startReceive();
    return;
  }

  uint8_t trama[TRAMA_LONG_MAX];
  int state = radio.readData(trama, longitud);

  if (state == RADIOLIB_ERR_NONE && trama[0] == 'M' && trama[1] == 'M' &&
      (trama[2] >> 4) == TRAMA_VERSION) {
    rssiUltimo = radio.getRSSI();
    snrUltimo = radio.getSNR();
    pantallaPendiente = true;

    uint8_t tipo = trama[2] & 0x0F;
    uint8_t seq = trama[3];
    char indicativo[TRAMA_LONG_INDICATIVO + 1];
    memcpy(indicativo, &trama[5], TRAMA_LONG_INDICATIVO);
    indicativo[TRAMA_LONG_INDICATIVO] = '\0';

    // Duplicado: mismo emisor y mismo seq que la trama anterior
    static bool haySeqRx = false;
    static uint8_t ultimoSeqRx = 0;
    static char ultimoIndicativoRx[TRAMA_LONG_INDICATIVO + 1] = "";
    bool duplicada = haySeqRx && seq == ultimoSeqRx && strcmp(indicativo, ultimoIndicativoRx) == 0;
    haySeqRx = true;
    ultimoSeqRx = seq;
    strcpy(ultimoIndicativoRx, indicativo);

    String texto = "";
    for (size_t i = TRAMA_LONG_CABECERA; i < longitud; i++) texto += decodificarCaracterMorse(trama[i]);
    texto.trim();

    Serial.printf("[LORA RX] %s seq %u wpm %u%s: ", indicativo, seq, trama[4], duplicada ? " (DUPLICADA)" : "");
    Serial.println(texto);

    if (!duplicada && tipo == TRAMA_TIPO_GRUPO_DUO && enPantalla4_Sub && subpantalla4Activa == 1 &&
        texto.length() > 0) {
      // Se reproduce a los wpm del emisor; si el valor no es razonable, a los locales
      uint8_t wpmRx = trama[4];
      if (wpmRx < WPM_MIN_DUO || wpmRx > WPM_MAX_DUO) wpmRx = wpmCW;
      encolarGrupoRxDuo(texto.c_str(), wpmRx);
    }
  }

  radio.startReceive();
}

/**
 * Sale de las subpantallas LoRa (Duo / QSO): desactiva la recepcion, corta el tono
 * que pudiera estar sonando y limpia el estado, para que un paquete que llegue despues
 * no suene encima del entrenador ni de otras pantallas.
 */
/**
 * Despierta la radio (duerme fuera de Duo y QSO) y la pone a escuchar. Con el SX1262 dormido, BUSY
 * esta en alto y startReceive() se queda esperando: hay que despertarlo antes bajando NSS, que es lo
 * que hace standby(..., true). Sin esto, la placa que solo escuchaba no recibia (probado).
 */
void loraEscuchar() {
  int estado = radio.standby(RADIOLIB_SX126X_STANDBY_RC, true);
  if (estado == RADIOLIB_ERR_NONE) estado = radio.startReceive();
  if (estado != RADIOLIB_ERR_NONE) Serial.printf("[LORA] ERROR AL DESPERTAR LA RADIO: %d\n", estado);
}

void salirModoLoRa() {
  enPantalla4_Sub = false;
  subpantalla4Activa = 0;
  editandoValor = false;
  modoRadioRxActivo = false;
  radio.sleep();  // Ahorro: fuera de Duo y QSO la radio duerme (sleep templado: conserva la configuracion)
  flagPaqueteRecibido = false;

  emitiendoMorseDuo = false;
  motorCWDetener();

  colaRxIni = 0;
  colaRxNum = 0;
  grupoDuoRecibido = "";
  grupoDuoTransmitido = "";
  grupoDuoRevelado = false;
}

// ============================================================================
// WIFI: HORA NTP, PROPAGACION (hamqsl.com) Y SPOTS POTA
// ============================================================================
// El WiFi solo se enciende en las pantallas 5 y 6 y se apaga al salir de ellas.
// Si no hay red guardada o no conecta, la placa abre un punto de acceso para
// configurar la red desde el movil (WiFiManager). La red queda guardada en NVS.
// SOTA no se incluye: sus condiciones de uso exigen aprobacion previa para software generado por IA.
#define WIFI_TIMEOUT_CONEXION_MS 15000
#define PORTAL_TIMEOUT_S         300
#define PORTAL_CLAVE             "chispas73"  // Clave del punto de acceso de configuracion (minimo 8)
#define ZONA_HORARIA             "CET-1CEST,M3.5.0,M10.5.0/3"  // Peninsula
#define URL_PROPAGACION          "https://www.hamqsl.com/solarxml.php"
#define URL_POTA                 "https://api.pota.app/spot/activator"
#define REFRESCO_PROPAGACION_MS  (30UL * 60 * 1000)  // hamqsl actualiza cada pocas horas
#define REFRESCO_POTA_MS         (2UL * 60 * 1000)   // POTA no publica limites: se consulta con moderacion
#define MAX_SPOTS_POTA           40
#define SPOTS_POR_PAGINA         4

enum EstadoWifi { WIFI_APAGADO, WIFI_CONECTANDO, WIFI_PORTAL, WIFI_CONECTADO, WIFI_SIN_CONEXION };
uint8_t opcionSinConexion = 0;  // Sin conexion en PROP, POTA o SOL: 0 = "abrir webconf", 1 = barra ant./sig.
EstadoWifi estadoWifi = WIFI_APAGADO;
unsigned long inicioConexionWifi = 0;
WiFiManager wm;
char nombrePortal[16] = "";
bool actualizandoWifi = false;     // Se esta descargando: la pantalla muestra "ACTUALIZANDO..."
bool forzarActualizacion = false;  // Pedido con la opcion "actualizar"

struct DatosPropagacion {
  bool validos;
  unsigned long intentoMs;  // 0 = nunca
  int sfi, aIndex, kIndex, manchas;
  char rayosX[8];
  char actualizado[24];
  char bandas[4][2][6];  // [80-40, 30-20, 17-15, 12-10][dia, noche]: "Good", "Fair", "Poor"
};
const char* const NOMBRES_BANDAS_XML[4] = { "80m-40m", "30m-20m", "17m-15m", "12m-10m" };
const char* const NOMBRES_BANDAS_PANTALLA[4] = { "80-40m", "30-20m", "17-15m", "12-10m" };
DatosPropagacion propagacion = {};

struct SpotPota {
  char indicativo[12];
  char frecuencia[10];
  char referencia[12];
  char tiempo[20];  // "2026-10-08T12:03:57" (UTC); sirve para ordenar
};
SpotPota spotsPota[MAX_SPOTS_POTA];
uint8_t numSpotsPota = 0;
bool spotsPotaValidos = false;
unsigned long spotsPotaIntentoMs = 0;  // 0 = nunca
uint8_t paginaSpots = 0;

#define OPCION_BARRA_PROP 2
#define OPCION_BARRA_SPOTS 2
uint8_t opcionPantallaProp = OPCION_BARRA_PROP;  // 0 = mas (pagina), 1 = actualizar, 2 = barra ant./sig.
uint8_t opcionPantallaSpots = OPCION_BARRA_SPOTS;  // 0 = mas (pagina), 1 = actualizar, 2 = barra ant./sig.
uint8_t paginaPropagacion = 0;              // 0 = indices, 1 = condiciones por banda

#define OPCION_BARRA_CONFIG 3
uint8_t opcionPantallaConfig = OPCION_BARRA_CONFIG;  // 0 = webconf (portal), 1 = reset de fabrica, 2 = apagado, 3 = barra

// Apagado automatico: minutos sin tocar nada antes de dormir (0 = nunca). Se elige en CONF y se guarda en NVS
const uint8_t OPCIONES_APAGADO_MIN[] = { 0, 5, 10, 15, 30, 60, 120 };
const uint8_t N_OPCIONES_APAGADO = sizeof(OPCIONES_APAGADO_MIN);
uint8_t apagadoAutoMin = 30;

String textoApagado() {
  return apagadoAutoMin == 0 ? String("nunca") : String(apagadoAutoMin) + " min";
}

// Paso al valor siguiente o anterior de OPCIONES_APAGADO_MIN, sin dar la vuelta
void ajustarApagado(int8_t paso) {
  int i = 0;
  while (i < N_OPCIONES_APAGADO - 1 && OPCIONES_APAGADO_MIN[i] != apagadoAutoMin) i++;
#ifdef SIN_ENCODER
  i = (i + N_OPCIONES_APAGADO + paso) % N_OPCIONES_APAGADO;  // Sin encoder: da la vuelta
#else
  i = constrain(i + paso, 0, N_OPCIONES_APAGADO - 1);
#endif
  apagadoAutoMin = OPCIONES_APAGADO_MIN[i];
}
bool confirmandoFabrica = false;

// Pagina "Setup" del portal: datos de la estacion y hora manual (para campo sin internet)
WiFiManagerParameter paramIndicativo("call", "Indicativo", INDICATIVO_POR_DEFECTO, TRAMA_LONG_INDICATIVO);
WiFiManagerParameter paramLocator("loc", "Locator (4 o 6 caracteres)", LOCATOR_POR_DEFECTO, 6);
// Hora manual en formato espanol. Dentro del campo, como pista, la hora que tiene Chispas al abrir el portal
class ParametroHora : public WiFiManagerParameter {
 public:
  char pista[48] = "placeholder='HH:MM DD-MM-AAAA'";
  ParametroHora() : WiFiManagerParameter("hora", "Hora local, HH:MM DD-MM-AAAA (vacío = no cambiarla)", "", 16) {}
  const char* getCustomHTML() const override { return pista; }
  void actualizarPista() {
    struct tm t;
    if (getLocalTime(&t, 0) && t.tm_year > 120) {
      snprintf(pista, sizeof(pista), "placeholder='%02d:%02d %02d-%02d-%04d'",
               t.tm_hour, t.tm_min, t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    }
  }
};
ParametroHora paramHora;

// Casilla del portal. WiFiManager no tiene casillas: es un campo cuyo HTML dice type="checkbox".
// Marcada envia "1" y sin marcar no envia nada (valor vacio). "checked" depende del estado guardado
class ParametroCasilla : public WiFiManagerParameter {
 public:
  bool marcada = false;
  ParametroCasilla(const char* id, const char* etiqueta)
    : WiFiManagerParameter(id, etiqueta, "1", 2, "type=\"checkbox\"", WFM_LABEL_AFTER) {}
  const char* getCustomHTML() const override {
    return marcada ? "type=\"checkbox\" checked" : "type=\"checkbox\"";
  }
};
ParametroCasilla paramHoraUtc("utc", "Hora UTC en la cabecera (12:05Z) en vez de la local");

bool locatorALatLon(const char* loc, float& lat, float& lon);  // Definida en la pantalla 7

void guardarParametrosPortal() {
  String call = paramIndicativo.getValue();
  call.trim();
  call.toUpperCase();
  bool callValido = call.length() >= 3 && call.length() <= TRAMA_LONG_INDICATIVO;
  for (size_t i = 0; i < call.length(); i++) {
    if (!isalnum(call[i]) && call[i] != '/') callValido = false;
  }
  if (callValido) strlcpy(indicativoEstacion, call.c_str(), sizeof(indicativoEstacion));
  else Serial.printf("[PORTAL] INDICATIVO NO VALIDO: %s\n", call.c_str());

  // "in80DK" -> "IN80dk"
  String loc = paramLocator.getValue();
  loc.trim();
  for (size_t i = 0; i < loc.length(); i++) loc[i] = (i < 2) ? toupper(loc[i]) : tolower(loc[i]);
  float lat, lon;
  if (locatorALatLon(loc.c_str(), lat, lon)) strlcpy(locatorEstacion, loc.c_str(), sizeof(locatorEstacion));
  else Serial.printf("[PORTAL] LOCATOR NO VALIDO: %s\n", loc.c_str());

  horaUtcCabecera = strlen(paramHoraUtc.getValue()) > 0;
  paramHoraUtc.marcada = horaUtcCabecera;
  paramHoraUtc.setValue("1", 2);  // Para que vuelva a enviar "1" si se marca otra vez

  guardarEstacionNVS();
  Serial.printf("[PORTAL] ESTACION: %s %s, hora %s\n", indicativoEstacion, locatorEstacion,
                horaUtcCabecera ? "UTC" : "local");

  // "14:05 10-10-2026" (tambien vale con barras: "14:05 10/10/2026")
  String hora = paramHora.getValue();
  hora.trim();
  hora.replace('/', '-');
  if (hora.length() > 0) {
    int a, me, d, h, mi;
    if (sscanf(hora.c_str(), "%d:%d %d-%d-%d", &h, &mi, &d, &me, &a) == 5 &&
        a >= 2024 && me >= 1 && me <= 12 && d >= 1 && d <= 31 && h >= 0 && h <= 23 && mi >= 0 && mi <= 59) {
      struct tm t = {};
      t.tm_year = a - 1900; t.tm_mon = me - 1; t.tm_mday = d;
      t.tm_hour = h; t.tm_min = mi; t.tm_isdst = -1;  // Hora local: mktime aplica CET/CEST
      struct timeval tv = { mktime(&t), 0 };
      settimeofday(&tv, nullptr);
      Serial.printf("[PORTAL] HORA MANUAL: %s\n", hora.c_str());
    } else {
      Serial.printf("[PORTAL] HORA NO VALIDA: %s\n", hora.c_str());
    }
  }
  pantallaPendiente = true;
}

// ----------------------------------------------------------------------------
// QR DEL PORTAL: al escanearlo, el movil se une a la red de Chispas (y suele abrir el portal solo)
// ----------------------------------------------------------------------------
// Con el portal abierto, la pantalla muestra los datos en texto (red, clave e IP). En CONF, la
// opcion "ver qr" muestra a pantalla completa el QR de WiFi: al escanearlo, el movil se une a la red
// de Chispas (la pagina del portal se abre sola o no segun el movil). Cualquier giro o pulsacion
// vuelve a los datos.
#define QR_MAX_MODULOS 41  // Hasta la version 6 del QR (sobra: el de WiFi ocupa una version 3, 29x29)

enum VistaPortal { VISTA_DATOS, VISTA_QR_RED };

struct CodigoQr {
  uint8_t modulos[(QR_MAX_MODULOS * QR_MAX_MODULOS + 7) / 8];  // Un bit por modulo (1 = negro)
  uint8_t tam;                                                 // Modulos por lado (0 = no hay QR)
};
CodigoQr qrRed = {{0}, 0};
CodigoQr* qrDestino = nullptr;  // Donde guarda guardarQrGenerado() (el callback no admite contexto)

VistaPortal vistaPortal = VISTA_DATOS;

// Llamada por esp_qrcode_generate() con el QR ya codificado: se copia a *qrDestino
void guardarQrGenerado(esp_qrcode_handle_t qr) {
  int n = esp_qrcode_get_size(qr);
  if (qrDestino == nullptr || n > QR_MAX_MODULOS) return;
  memset(qrDestino->modulos, 0, sizeof(qrDestino->modulos));
  for (int y = 0; y < n; y++) {
    for (int x = 0; x < n; x++) {
      if (esp_qrcode_get_module(qr, x, y)) qrDestino->modulos[(y * n + x) / 8] |= 1 << ((y * n + x) % 8);
    }
  }
  qrDestino->tam = n;
}

void generarQr(CodigoQr& destino, const char* texto) {
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = guardarQrGenerado;
  cfg.max_qrcode_version = 6;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;  // Modulos mas grandes, mejor para una pantalla pequena
  destino.tam = 0;
  qrDestino = &destino;
  if (esp_qrcode_generate(&cfg, texto) != ESP_OK) Serial.printf("[PORTAL] NO SE PUDO GENERAR EL QR: %s\n", texto);
  else Serial.printf("[PORTAL] QR %ux%u: %s\n", destino.tam, destino.tam, texto);
  qrDestino = nullptr;
}

/**
 * QR estandar de WiFi ("WIFI:T:WPA;S:<red>;P:<clave>;;"), que entienden las camaras de Android e iOS.
 * La red y la clave no llevan ; , : ni comillas, asi que no hace falta escaparlas.
 */
void generarQrPortal() {
  char texto[64];
  snprintf(texto, sizeof(texto), "WIFI:T:WPA;S:%s;P:%s;;", nombrePortal, PORTAL_CLAVE);
  generarQr(qrRed, texto);
}

bool viendoQrPortal() {
  return estadoWifi == WIFI_PORTAL && vistaPortal == VISTA_QR_RED && qrRed.tam > 0;
}

// QR a la izquierda (blanco con modulos negros y margen de 1 modulo); devuelve el ancho ocupado
int dibujarQr(const CodigoQr& qr) {
  int escala = ((qr.tam + 2) * 2 <= 64) ? 2 : 1;
  int lado = (qr.tam + 2) * escala;
  int y0 = (64 - lado) / 2;
  display.fillRect(0, y0, lado, lado, SSD1306_WHITE);
  for (int y = 0; y < qr.tam; y++) {
    for (int x = 0; x < qr.tam; x++) {
      int i = y * qr.tam + x;
      if (qr.modulos[i / 8] & (1 << (i % 8))) {
        display.fillRect((x + 1) * escala, y0 + (y + 1) * escala, escala, escala, SSD1306_BLACK);
      }
    }
  }
  return lado;
}

// QR de la red a la izquierda y explicacion a la derecha
void dibujarPantallaQr() {
  display.clearDisplay();
  int xt = dibujarQr(qrRed) + 4;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(xt, 2);  display.print(F("WEBCONF"));
  display.drawLine(xt, 11, 127, 11, SSD1306_WHITE);
  display.setCursor(xt, 16); display.print(F("ESCANEA"));
  display.setCursor(xt, 26); display.print(F("PARA UNIR"));
  display.setCursor(xt, 36); display.print(F("EL MOVIL"));
  display.setCursor(xt, 55); display.print(F("> DATOS"));
  display.display();
}

void cpuSegunWifi(bool wifiEncendido);  // Definida justo debajo

void abrirPortalWifi() {
  if (nombrePortal[0] == '\0') {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(nombrePortal, sizeof(nombrePortal), "Chispas-%02X%02X", mac[4], mac[5]);
  }
  if (qrRed.tam == 0) generarQrPortal();
  vistaPortal = VISTA_DATOS;  // Por defecto, los datos; el QR es opcion de CONF
  static bool parametrosAnadidos = false;
  if (!parametrosAnadidos) {
    wm.addParameter(&paramIndicativo);
    wm.addParameter(&paramLocator);
    wm.addParameter(&paramHora);
    wm.addParameter(&paramHoraUtc);
    const char* menu[] = { "wifi", "param", "exit" };  // "param" = pagina "Setup"
    wm.setMenu(menu, 3);
    wm.setSaveParamsCallback(guardarParametrosPortal);
    parametrosAnadidos = true;
  }
  paramIndicativo.setValue(indicativoEstacion, TRAMA_LONG_INDICATIVO);
  paramLocator.setValue(locatorEstacion, 6);
  paramHora.setValue("", 16);
  paramHora.actualizarPista();
  paramHoraUtc.setValue("1", 2);
  paramHoraUtc.marcada = horaUtcCabecera;

  cpuSegunWifi(true);
  WiFi.mode(WIFI_STA);
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.startConfigPortal(nombrePortal, PORTAL_CLAVE);
  estadoWifi = WIFI_PORTAL;
  Serial.printf("[WIFI] PORTAL ABIERTO: %s\n", nombrePortal);
  pantallaPendiente = true;
}

/**
 * Sin red guardada o si no conecta: no se abre el portal solo; la pantalla lo explica y ofrece
 * "abrir webconf" (el usuario decide).
 */
// Ahorro: la CPU va a 80 MHz con el WiFi apagado y a 240 MHz con el encendido (HTTPS y portal).
// El motor CW, el keyer (ticks del systimer), el tono, el I2C y el puerto serie (APB a 80 MHz) no
// dependen de esta frecuencia.
#define CPU_MHZ_REPOSO 80
#define CPU_MHZ_WIFI   240

void cpuSegunWifi(bool wifiEncendido) {
  uint32_t mhz = wifiEncendido ? CPU_MHZ_WIFI : CPU_MHZ_REPOSO;
  if (getCpuFrequencyMhz() != mhz) setCpuFrequencyMhz(mhz);
}

void quedarseSinConexion() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  cpuSegunWifi(false);
  estadoWifi = WIFI_SIN_CONEXION;
  opcionSinConexion = 0;  // Foco en "abrir webconf"
  pantallaPendiente = true;
}

void encenderWifi() {
  cpuSegunWifi(true);
  WiFi.mode(WIFI_STA);
  if (!wm.getWiFiIsSaved()) {
    Serial.println(F("[WIFI] NO HAY RED GUARDADA"));
    quedarseSinConexion();
    return;
  }
  WiFi.begin();  // Red guardada
  estadoWifi = WIFI_CONECTANDO;
  inicioConexionWifi = millis();
  Serial.println(F("[WIFI] CONECTANDO..."));
  pantallaPendiente = true;
}

void apagarWifi() {
  if (estadoWifi == WIFI_PORTAL) wm.stopConfigPortal();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  cpuSegunWifi(false);
  estadoWifi = WIFI_APAGADO;
  Serial.println(F("[WIFI] APAGADO"));
}

void wifiConectado() {
  estadoWifi = WIFI_CONECTADO;
  Serial.print(F("[WIFI] CONECTADO, IP "));
  Serial.println(WiFi.localIP());
  configTzTime(ZONA_HORARIA, "pool.ntp.org", "time.cloudflare.com");  // Sincroniza en segundo plano
  pantallaPendiente = true;
}

bool descargarUrl(const char* url, String& cuerpo) {
  WiFiClientSecure cliente;
  cliente.setInsecure();  // Datos publicos de solo lectura: no se valida el certificado
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(cliente, url)) return false;
  http.setUserAgent("Chispas-EA4ILN/" FW_VERSION);
  int codigo = http.GET();
  if (codigo == HTTP_CODE_OK) cuerpo = http.getString();
  else Serial.printf("[WIFI] %s -> HTTP %d\n", url, codigo);
  http.end();
  return codigo == HTTP_CODE_OK;
}

String valorEtiquetaXml(const String& xml, const char* etiqueta) {
  String apertura = String("<") + etiqueta + ">";
  String cierre = String("</") + etiqueta + ">";
  int ini = xml.indexOf(apertura);
  if (ini < 0) return "";
  ini += apertura.length();
  int fin = xml.indexOf(cierre, ini);
  if (fin < 0) return "";
  String valor = xml.substring(ini, fin);
  valor.trim();
  return valor;
}

void actualizarPropagacion() {
  propagacion.intentoMs = millis();
  String xml;
  if (!descargarUrl(URL_PROPAGACION, xml)) return;

  propagacion.sfi = valorEtiquetaXml(xml, "solarflux").toInt();
  propagacion.aIndex = valorEtiquetaXml(xml, "aindex").toInt();
  propagacion.kIndex = valorEtiquetaXml(xml, "kindex").toInt();
  propagacion.manchas = valorEtiquetaXml(xml, "sunspots").toInt();
  strlcpy(propagacion.rayosX, valorEtiquetaXml(xml, "xray").c_str(), sizeof(propagacion.rayosX));
  strlcpy(propagacion.actualizado, valorEtiquetaXml(xml, "updated").c_str(), sizeof(propagacion.actualizado));
  for (uint8_t b = 0; b < 4; b++) {
    for (uint8_t t = 0; t < 2; t++) {
      // <band name="80m-40m" time="day">Fair</band>
      String clave = String("name=\"") + NOMBRES_BANDAS_XML[b] + "\" time=\"" + (t == 0 ? "day" : "night") + "\">";
      String valor = "";
      int ini = xml.indexOf(clave);
      if (ini >= 0) {
        ini += clave.length();
        int fin = xml.indexOf('<', ini);
        if (fin > ini) valor = xml.substring(ini, fin);
      }
      valor.trim();
      strlcpy(propagacion.bandas[b][t], valor.length() ? valor.c_str() : "?", sizeof(propagacion.bandas[b][t]));
    }
  }
  propagacion.validos = true;
  Serial.printf("[WIFI] PROPAGACION: SFI %d A %d K %d\n", propagacion.sfi, propagacion.aIndex, propagacion.kIndex);
}

int compararSpotsRecientePrimero(const void* a, const void* b) {
  return strcmp(((const SpotPota*)b)->tiempo, ((const SpotPota*)a)->tiempo);
}

void actualizarSpotsPota() {
  spotsPotaIntentoMs = millis();
  String json;
  if (!descargarUrl(URL_POTA, json)) return;

  JsonDocument filtro;
  filtro[0]["activator"] = true;
  filtro[0]["frequency"] = true;
  filtro[0]["mode"] = true;
  filtro[0]["reference"] = true;
  filtro[0]["spotTime"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, DeserializationOption::Filter(filtro));
  if (err) {
    Serial.printf("[WIFI] POTA: JSON NO VALIDO (%s)\n", err.c_str());
    return;
  }

  numSpotsPota = 0;
  for (JsonObject s : doc.as<JsonArray>()) {
    if (numSpotsPota >= MAX_SPOTS_POTA) break;
    if (strcmp(s["mode"] | "", "CW") != 0) continue;

    SpotPota& sp = spotsPota[numSpotsPota++];
    strlcpy(sp.indicativo, s["activator"] | "", sizeof(sp.indicativo));
    strlcpy(sp.frecuencia, s["frequency"] | "", sizeof(sp.frecuencia));
    strlcpy(sp.referencia, s["reference"] | "", sizeof(sp.referencia));
    strlcpy(sp.tiempo, s["spotTime"] | "", sizeof(sp.tiempo));

    // "7044.0" -> "7044"
    size_t n = strlen(sp.frecuencia);
    if (n > 2 && strcmp(&sp.frecuencia[n - 2], ".0") == 0) sp.frecuencia[n - 2] = '\0';
  }
  qsort(spotsPota, numSpotsPota, sizeof(SpotPota), compararSpotsRecientePrimero);
  spotsPotaValidos = true;
  paginaSpots = 0;
  Serial.printf("[WIFI] POTA: %u SPOTS CW\n", numSpotsPota);
}

/**
 * Opcion "actualizar": con WiFi descarga ya; sin WiFi intenta conectar.
 */
void pedirActualizacionWifi() {
  if (estadoWifi == WIFI_CONECTADO) forzarActualizacion = true;
  else if (estadoWifi == WIFI_APAGADO) encenderWifi();
}

/**
 * Enciende o apaga el WiFi al entrar o salir de sus pantallas, avanza la conexion o el
 * portal y descarga los datos cuando tocan. La descarga bloquea 1-3 s, pero en estas
 * pantallas no suena CW ni hay recepcion LoRa.
 */
void procesarWifi() {
  static uint8_t pantallaAnterior = 0;
  // PROP y POTA encienden el WiFi al entrar; SOL tambien, pero solo si aun no hay hora (para
  // sincronizarla por NTP). Si no conecta, muestran el aviso con "abrir webconf". En CONF y SOL el WiFi
  // solo sigue encendido si el portal esta abierto: si se llega con el WiFi conectado (desde PROP o
  // POTA) se apaga, porque no se usa (eran unos 40 mA de mas, medido).
  if (pantallaActual != pantallaAnterior) {
    struct tm hora;
    bool solSinHora = pantallaActual == PANTALLA_SOL && !horaLocalValida(hora);
    bool enDatos = (pantallaActual == PANTALLA_PROPAGACION || pantallaActual == PANTALLA_SPOTS || solSinHora);
    bool conPortal = estadoWifi == WIFI_PORTAL &&
                     (pantallaActual == PANTALLA_CONFIG || pantallaActual == PANTALLA_SOL);
    bool usaWifi = enDatos || conPortal;
    if (enDatos && (estadoWifi == WIFI_APAGADO || estadoWifi == WIFI_SIN_CONEXION)) encenderWifi();
    if (!usaWifi && estadoWifi != WIFI_APAGADO) apagarWifi();
    pantallaAnterior = pantallaActual;
  }

  switch (estadoWifi) {
    case WIFI_CONECTANDO:
      if (WiFi.status() == WL_CONNECTED) {
        wifiConectado();
      } else if (millis() - inicioConexionWifi > WIFI_TIMEOUT_CONEXION_MS) {
        Serial.println(F("[WIFI] NO CONECTA"));
        quedarseSinConexion();
      }
      break;

    case WIFI_PORTAL:
      if (wm.process()) {
        wifiConectado();
      } else if (!wm.getConfigPortalActive()) {
        Serial.println(F("[WIFI] PORTAL CERRADO POR TIEMPO"));
        apagarWifi();
        // En PROP, POTA o SOL se vuelve al aviso de "sin conexion" (con "abrir webconf")
        if (pantallaActual == PANTALLA_PROPAGACION || pantallaActual == PANTALLA_SPOTS ||
            pantallaActual == PANTALLA_SOL) {
          quedarseSinConexion();
        }
        pantallaPendiente = true;
      }
      break;

    case WIFI_CONECTADO: {
      if (WiFi.status() != WL_CONNECTED) {
        estadoWifi = WIFI_CONECTANDO;
        inicioConexionWifi = millis();
        pantallaPendiente = true;
        break;
      }
      bool tocaPropagacion = pantallaActual == PANTALLA_PROPAGACION &&
          (forzarActualizacion || propagacion.intentoMs == 0 ||
           millis() - propagacion.intentoMs > REFRESCO_PROPAGACION_MS);
      bool tocaPota = pantallaActual == PANTALLA_SPOTS &&
          (forzarActualizacion || spotsPotaIntentoMs == 0 ||
           millis() - spotsPotaIntentoMs > REFRESCO_POTA_MS);
      if (tocaPropagacion || tocaPota) {
        forzarActualizacion = false;
        actualizandoWifi = true;
        actualizarPantalla();
        if (tocaPropagacion) actualizarPropagacion();
        if (tocaPota) actualizarSpotsPota();
        actualizandoWifi = false;
        pantallaPendiente = true;
      }
      break;
    }

    default:
      break;
  }
}

/**
 * Si no hay conexion dibuja el estado del WiFi en el cuerpo de la pantalla y devuelve true.
 */
bool dibujarEstadoWifi() {
  switch (estadoWifi) {
    case WIFI_CONECTANDO:
      display.setCursor(0, 22);
      display.print(F("CONECTANDO WIFI..."));
      return true;
    case WIFI_PORTAL:
      display.setCursor(0, 12);
      display.print(F("CONFIGURA EL WIFI:"));
      display.setCursor(0, 22);
      display.print(F("RED   ")); display.print(nombrePortal);
      display.setCursor(0, 32);
      display.print(F("CLAVE " PORTAL_CLAVE));
      display.setCursor(0, 42);
      display.print(F("WEB   192.168.4.1"));
      return true;
    case WIFI_APAGADO:
      // Solo se ve un instante al entrar en PROP, POTA o SOL, justo antes de empezar a conectar
      display.setCursor(0, 22);
      display.print(F("CONECTANDO WIFI..."));
      return true;
    case WIFI_SIN_CONEXION:
      // El pie lo pone dibujarPieSinConexion() en cada pantalla
      display.setCursor(0, 12);
      display.print(F("SIN CONEXION WIFI"));
      display.setCursor(0, 24);
      display.print(F("LA RED NO ESTA O NO"));
      display.setCursor(0, 33);
      display.print(F("HAY NINGUNA GUARDADA."));
      display.setCursor(0, 42);
      display.print(F("CONFIGURALA:"));
      return true;
    default:
      return false;
  }
}

// Pie de PROP, POTA y SOL sin conexion: "abrir webconf" o, al girar, la barra ant./sig.
void dibujarPieSinConexion() {
  if (opcionSinConexion == 1) dibujarBarraAvanzarRetroceder(true, String((char)0xAE) + " opc " + String((char)0xAF));
  else dibujarPieCentrado("> abrir webconf");
}

// ============================================================================
// RENDERIZADO PANTALLAS (1 A 6)
// ============================================================================
void renderizarPantallaInicio() {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextColor(SSD1306_WHITE);

  // Marco redondeado entre la cabecera (linea en y=9) y la barra inferior (y=52)
  display.drawRoundRect(4, 12, 120, 38, 4, SSD1306_WHITE);

  // Nombre del aparato, grande (tamano 2: 12x16 px por caracter)
  const char* titulo = "CHISPAS";  // Apodo del radiotelegrafista de a bordo (en ingles, "Sparks")
  display.setTextSize(2);
  display.getTextBounds(titulo, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 18);
  display.print(titulo);
  display.setTextSize(1);

  // Indicativo y version, en pequeno
  String linea = "de " + String(indicativoEstacion) + "  v" + FW_VERSION;
  display.getTextBounds(linea.c_str(), 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 38);
  display.print(linea);

  dibujarBarraAvanzarRetroceder(true);
}

void renderizarPantallaCW() {
  display.setTextColor(SSD1306_WHITE);

  if (enKeyer) {
    display.setTextSize(1);

    // Opciones: ">20WPM >MODO A >NORM" (20 caracteres)
    display.setCursor(0, 12);
    display.print(opcionKeyer == 0 ? ">" : " ");
    imprimirValor(opcionKeyer == 0, String(keyerWpm) + "WPM");
    display.print(opcionKeyer == 1 ? " >" : "  ");
    display.print(keyerModoB ? F("MODO B") : F("MODO A"));
    display.print(opcionKeyer == 2 ? " >" : "  ");
    display.print(keyerPalasInvertidas ? F("INV") : F("NORM"));

    // Texto descodificado: 3 lineas de 21 caracteres. Al llenarse se quitan lineas enteras
    // por arriba, para que el texto no se desplace caracter a caracter
    String t = textoKeyer + "_";
    const int ANCHO = 21;
    if (t.length() > 3 * ANCHO) {
      int sobran = t.length() - 3 * ANCHO;
      t = t.substring(((sobran + ANCHO - 1) / ANCHO) * ANCHO);
    }
    for (int linea = 0; linea < 3 && linea * ANCHO < (int)t.length(); linea++) {
      display.setCursor(0, 23 + linea * 9);
      display.print(t.substring(linea * ANCHO, min((int)t.length(), (linea + 1) * ANCHO)));
    }

    dibujarPieCentrado((opcionKeyer == 3 ? "> " : "  ") + String("borrar   ") +
                       (opcionKeyer == 4 ? "> " : "  ") + String("volver"));

  } else if (enEcho) {
    display.setTextSize(1);

    // Opciones: ">15WPM >1LET >VER SI" (20 caracteres)
    display.setCursor(0, 12);
    display.print(opcionEcho == 0 ? ">" : " ");
    imprimirValor(opcionEcho == 0, String(keyerWpm) + "WPM");
    display.print(opcionEcho == 1 ? " >" : "  ");
    imprimirValor(opcionEcho == 1, String(echoLargo) + "LET");
    display.print(opcionEcho == 2 ? " >" : "  ");
    display.print(echoVerObjetivo ? F("VER SI") : F("VER NO"));

    // El reto en grande (con "VER NO", "?" hasta el resultado)
    bool mostrar = echoVerObjetivo || estadoEcho == ECHO_RESULTADO;
    String reto = "";
    for (uint8_t i = 0; i < echoObjetivo.length(); i++) reto += mostrar ? echoObjetivo.charAt(i) : '?';
    int16_t x1, y1;
    uint16_t w, h;
    display.setTextSize(2);
    display.getTextBounds(reto, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((128 - w) / 2, 23);
    display.print(reto);
    display.setTextSize(1);

    // Aciertos / intentos, a la derecha
    String marcador = String(echoAciertos) + "/" + String(echoIntentos);
    display.getTextBounds(marcador, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(128 - w, 27);
    display.print(marcador);

    // Estado
    display.setCursor(0, 42);
    if (estadoEcho == ECHO_ESPERA) {
      display.print(F("PULSA INICIAR"));
    } else if (estadoEcho == ECHO_REPRODUCIENDO) {
      display.print(F("ESCUCHA..."));
    } else if (estadoEcho == ECHO_ESCUCHANDO) {
      display.print(F("TU TURNO: ")); display.print(echoRespuesta); display.print('_');
    } else if (echoAcierto) {
      display.print(F("BIEN!"));
    } else {
      // Lo manipulado y, con una sola letra, el codigo correcto: "MAL: R  K -.-"
      display.print(F("MAL: ")); display.print(echoRespuesta);
      if (echoObjetivo.length() == 1) {
        display.print(F("  ")); display.print(echoObjetivo); display.print(' ');
        display.print(obtenerCodigoMorse(echoObjetivo.charAt(0)));
      }
    }

    dibujarPieCentrado((opcionEcho == 3 ? "> " : "  ") + String(estadoEcho == ECHO_ESPERA ? "iniciar   " : "repetir   ") +
                       (opcionEcho == 4 ? "> " : "  ") + String("volver"));

  } else if (!enSubpantallaCW && !enPractica3_2 && !enEstadisticas3_5) {
    // Mismo estilo que la pantalla LoRa: titulo en negativo y tres opciones
    display.setTextSize(1);

    const char* titulo = " ENTRENADOR CW ";
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(titulo, 0, 0, &x1, &y1, &w, &h);
    int x = (128 - w) / 2;
    int y = 13;
    display.fillRect(x - 2, y - 2, w + 4, h + 4, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor(x, y);
    display.print(titulo);
    display.setTextColor(SSD1306_WHITE);

    const char* opciones[] = { "< KOCH >", "< KEYER >", "< ECHO >" };
    for (uint8_t i = 0; i < 3; i++) {
      display.setCursor(15, 24 + i * 9);
      display.print(opcionPantalla3 == i ? "> " : "  ");
      display.print(opciones[i]);
    }

    dibujarBarraAvanzarRetroceder(opcionPantalla3 == OPCION_BARRA_CW);

  } else if (enSubpantallaCW && !enPractica3_2 && !enEstadisticas3_5) {
    display.setTextSize(1);

    display.setCursor(0, 12);
    display.print(opcionSubpantalla == 0 ? ">" : " ");
    display.print(F("WPM: ")); imprimirValor(opcionSubpantalla == 0, String(wpmCW));

    display.setCursor(64, 12);
    display.print(opcionSubpantalla == 1 ? ">" : " ");
    display.print(F("SP: ")); imprimirValor(opcionSubpantalla == 1, String(espaciadoCW) + "%");

    display.setCursor(0, 22);
    display.print(opcionSubpantalla == 2 ? ">" : " ");
    display.print(F("LECCION: ")); imprimirValor(opcionSubpantalla == 2, String(leccionCW));

    display.setCursor(0, 32);
    display.print(opcionSubpantalla == 3 ? ">" : " ");
    display.print(F("CARACTERES: ")); imprimirValor(opcionSubpantalla == 3, String(caracteresCW));

    display.setCursor(0, 42);
    display.print(opcionSubpantalla == 4 ? ">" : " ");
    display.print(F("GRUPOS: ")); imprimirValor(opcionSubpantalla == 4, String(gruposCW));

    // Estadisticas de las lecciones: ultima opcion, en el hueco a la derecha de GRUPOS
    display.setCursor(82, 42);
    display.print(opcionSubpantalla == 7 ? ">" : " ");
    display.print(F("ESTAD."));

    String txtPie3_1 = (opcionSubpantalla == 5 ? "> " : "  ") + String("iniciar  ") + 
                       (opcionSubpantalla == 6 ? "> " : "  ") + String("volver");
    dibujarPieCentrado(txtPie3_1);

  } else if (enEstadisticas3_5) {
    display.setTextSize(1);

    if (paginaStats3_5 < PAGINAS_STATS_3_5) {
      uint8_t leccInicio = (paginaStats3_5 * 3) + 1;

      for (uint8_t i = 0; i < 3; i++) {
        uint8_t lNum = leccInicio + i;
        if (lNum > 40) break;
        uint8_t idx = lNum - 1;

        int yPos = 12 + (i * 13);
        display.setCursor(0, yPos);
        display.print(F("L"));
        if (lNum < 10) display.print(F("0"));
        display.print(lNum);
        display.print(F(":"));

        // Alineacion limpia de veces completadas (2 cifras; a partir de 99 se muestra 99)
        // La linea completa "L01:99x[========]100%" ocupa 21 caracteres = 126 px
        uint16_t veces = min(statsKoch[idx].vecesCompletada, (uint16_t)99);
        if (veces < 10) display.print(F(" "));
        display.print(veces);
        display.print(F("x["));

        // DIBUJO DE BARRA VISUAL OPCION 4 (8 BLOQUES)
        uint8_t maxPct = statsKoch[idx].maxAcierto;
        uint8_t bloques = (maxPct * 8) / 100;
        for (uint8_t b = 0; b < 8; b++) {
          display.print(b < bloques ? "=" : ".");
        }
        display.print(F("]"));

        // Alineacion fija del porcentaje final sin saltos de linea
        if (maxPct < 100) display.print(F(" "));
        if (maxPct < 10) display.print(F(" "));
        display.print(maxPct);
        display.print(F("%"));
      }

      dibujarPieCentrado("pag " + String(paginaStats3_5 + 1) + "/" + String(PAGINAS_STATS_3_5 + 1) + "   > volver");

    } else {
      display.setCursor(10, 16);
      display.println(F("- MEMORIA NVS -"));

      display.setCursor(0, 32);
      if (confirmandoBorrado) {
        // La fuente por defecto no es UTF-8: el signo de apertura es el 0xA8 de CP437
        display.print(F(" "));
        display.write(0xA8);
        display.println(F("CONFIRMAR BORRADO?"));
      } else {
        display.println(F(" BORRAR ESTADISTICAS"));  // Solo estadisticas: el resto, en P8 (fabrica)
      }

      dibujarPieCentrado(confirmandoBorrado ? "> confirmar" : "> borrar");
    }

  } else if (enPractica3_2) {
    display.setTextSize(1);

    if (estadoCW == CW_PANTALLA_3_3_LISTADO) {
      display.setCursor(0, 11);
      display.print(F("- GRUPOS EMITIDOS -"));

      int charPorPagina = 63; 
      int inicioChar = paginaListado3_3 * charPorPagina;
      int totalChars = textoCompletoLeccion.length();

      String bloquePagina = textoCompletoLeccion.substring(inicioChar, min(inicioChar + charPorPagina, totalChars));

      display.setCursor(0, 22);
      if (bloquePagina.length() > 0) display.println(bloquePagina.substring(0, min((int)bloquePagina.length(), 21)));
      if (bloquePagina.length() > 21) display.println(bloquePagina.substring(21, min((int)bloquePagina.length(), 42)));
      if (bloquePagina.length() > 42) display.println(bloquePagina.substring(42, min((int)bloquePagina.length(), 63)));

      bool hayMasPaginas = (inicioChar + charPorPagina) < totalChars;

      if (hayMasPaginas) {
        int paginas = (totalChars + charPorPagina - 1) / charPorPagina;
        dibujarPieCentrado("pag " + String(paginaListado3_3 + 1) + "/" + String(paginas) + "   > mas");
      } else {
        String txtPie = (opcionFinListado3_3 == 0 ? "> " : "  ") + String("corregir  ") + 
                        (opcionFinListado3_3 == 1 ? "> " : "  ") + String("rep.");
        dibujarPieCentrado(txtPie);
      }

    } else if (estadoCW == CW_PANTALLA_CORREGIR_ERRORES) {
      display.setCursor(10, 12);
      display.println(F("- REVISAR LECCION -"));

      display.setCursor(0, 26);
      display.print(opcionPantallaCorregir == 0 ? "> " : "  ");
      display.print(F("ERRORES: "));
      imprimirValor(opcionPantallaCorregir == 0, String(numeroErrores));
      display.print(F(" / "));
      display.print(totalCaracteresLeccion);

      display.setCursor(0, 42);
      display.print(opcionPantallaCorregir == 1 ? "> " : "  ");
      display.print(F("ENVIAR  "));
      display.print(opcionPantallaCorregir == 2 ? "> " : "  ");
      display.print(F("REP."));


    } else if (estadoCW == CW_PANTALLA_RESUMEN_FINAL) {
      float pctAciertos = 0.0;
      if (totalCaracteresLeccion > 0) {
        pctAciertos = ((float)(totalCaracteresLeccion - numeroErrores) / (float)totalCaracteresLeccion) * 100.0;
        if (pctAciertos < 0) pctAciertos = 0;
      }

      display.setCursor(10, 12);
      display.println(F("- RESULTADO KOCH -"));

      display.setCursor(0, 26);
      display.print(F("ACIERTOS: "));
      display.print(pctAciertos, 1);
      display.println(F("%"));

      display.setCursor(0, 41);
      display.print(F("REPETIR SI <90%")); // Criterio ajustado: estrictamente menor a 90%

      dibujarPieCentrado((opcionResumen == 0 ? "> " : "  ") + String("volver  ") +
                         (opcionResumen == 1 ? "> " : "  ") + String("rep."));

    } else {
      display.setCursor(0, 12);
      display.print(F("GRUPO: "));
      display.print(grupoActualNum);
      display.print(F(" / "));
      display.print(gruposCW);

      if (mostrarTextoGrupo) {
        display.setTextSize(2);
        int16_t x1, y1;
        uint16_t w, h;
        display.getTextBounds(grupoActualStr, 0, 0, &x1, &y1, &w, &h);
        display.setCursor((128 - w) / 2, 26);
        display.print(grupoActualStr);
        display.setTextSize(1);
      } else {
        display.setCursor(0, 28);
        if (estadoCW == CW_PAUSADO) {
          display.print(F("    [ PAUSADO ]"));
        } else {
          display.print(F("  TX LED (OCULTO)..."));
        }
      }

      dibujarPieCentrado((opcionPractica == 0 ? "> " : "  ") + String(estadoCW == CW_PAUSADO ? "seguir  " : "pausa  ") +
                         (opcionPractica == 1 ? "> " : "  ") + String("salir"));
    }
  }
}

void renderizarPantallaLora() {
  display.setTextColor(SSD1306_WHITE);

  if (!enPantalla4_Sub) {
    display.setTextSize(1);
    
    const char* titulo = " TRANSCEPTOR LORA ";
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(titulo, 0, 0, &x1, &y1, &w, &h);
    
    int x = (128 - w) / 2;
    int y = 14;

    display.fillRect(x - 2, y - 2, w + 4, h + 4, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor(x, y);
    display.print(titulo);

    display.setTextColor(SSD1306_WHITE);

    display.setCursor(5, 28);
    display.print(opcionPantalla4 == 0 ? "> " : "  ");
    display.print(F("< CW DUO (LORA) >"));

    display.setCursor(5, 38);
    display.print(opcionPantalla4 == 1 ? "> " : "  ");
    display.print(F("< CW QSO (KEYER) >"));

    dibujarBarraAvanzarRetroceder(opcionPantalla4 == 2);

  } else if (subpantalla4Activa == 1) {
    display.setTextSize(1);

    display.setCursor(0, 11);
    display.print(opcionDuo4_1 == 0 ? ">" : " ");
    display.print(F("WPM:")); imprimirValor(opcionDuo4_1 == 0, String(wpmCW));

    display.setCursor(52, 11);
    display.print(opcionDuo4_1 == 1 ? ">" : " ");
    display.print(F("SP:")); imprimirValor(opcionDuo4_1 == 1, String(espaciadoCW) + "%");

    display.setCursor(0, 21);
    display.print(opcionDuo4_1 == 2 ? ">" : " ");
    display.print(F("LECC:")); imprimirValor(opcionDuo4_1 == 2, String(leccionCW));

    if (grupoDuoTransmitido.length() > 0) {
      display.setCursor(52, 21);
      display.print(F("TX:[")); display.print(grupoDuoTransmitido); display.print(F("]"));
    }

    display.setCursor(0, 32);
    if (grupoDuoRecibido.length() > 0) {
      if (grupoDuoRevelado) {
        display.print(F("RX <- [")); display.print(grupoDuoRecibido); display.print(F("]"));
      } else {
        display.print(F("RX <- LED (OCULTO)"));
      }
    } else {
      display.print(F("RX <- ESCUCHANDO..."));
    }

    // RSSI de la ultima trama recibida (0 = aun no se ha recibido nada)
    if (rssiUltimo != 0) {
      String txtRssi = String(rssiUltimo) + "dBm";
      int16_t x1, y1;
      uint16_t w, h;
      display.getTextBounds(txtRssi.c_str(), 0, 0, &x1, &y1, &w, &h);
      display.setCursor(128 - w, 42);
      display.print(txtRssi);
    }

    // "ver" solo aparece si hay un grupo recibido sin revelar (con el, 20 de los 21 caracteres)
    bool conVer = hayGrupoDuoPorRevelar();
    const char* sep = conVer ? " " : "  ";
    String txtPie4_1 = (opcionDuo4_1 == 3 ? ">" : " ") + String("enviar") + sep;
    if (conVer) txtPie4_1 += (opcionDuo4_1 == 4 ? ">" : " ") + String("ver") + sep;
    txtPie4_1 += (opcionDuo4_1 == 5 ? ">" : " ") + String("volver");
    dibujarPieCentrado(txtPie4_1);

  } else if (subpantalla4Activa == 2) {
    display.setTextSize(1);
    display.setCursor(0, 12);
    display.print(F("KEYER: "));
    if (modoKeyer4_2 == 0) display.print(F("AUTO-PRACTICA"));
    else if (modoKeyer4_2 == 1) display.print(F("QSO REAL-TIME"));
    else display.print(F("MODO ECO"));

    display.setCursor(0, 26);
    display.print(F("PINS: DIT=G4 DAH=G5"));

    display.setCursor(0, 38);
    display.print(F("RSSI: ")); display.print(rssiUltimo); display.print(F("DBM"));

    dibujarPieCentrado("girar: modo  > volver");
  }
}

void renderizarPantallaPropagacion() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (!dibujarEstadoWifi()) {
    if (actualizandoWifi) {
      display.setCursor(0, 22);
      display.print(F("ACTUALIZANDO..."));
    } else if (!propagacion.validos) {
      display.setCursor(0, 22);
      display.print(F("SIN DATOS"));
    } else if (paginaPropagacion == 1) {
      // Prevision de hamqsl por banda: D = dia, N = noche
      for (uint8_t b = 0; b < 4; b++) {
        char linea[24];
        snprintf(linea, sizeof(linea), "%s D:%-4s N:%s", NOMBRES_BANDAS_PANTALLA[b],
                 propagacion.bandas[b][0], propagacion.bandas[b][1]);
        display.setCursor(0, 12 + b * 10);
        display.print(linea);
      }
    } else {
      display.setCursor(0, 12);
      display.print(F("SFI ")); display.print(propagacion.sfi);
      display.print(F("  SSN ")); display.print(propagacion.manchas);

      display.setCursor(0, 22);
      display.print(F("A ")); display.print(propagacion.aIndex);
      display.print(F("  K ")); display.print(propagacion.kIndex);
      display.print(F("  XR ")); display.print(propagacion.rayosX);

      // "08 Oct 2026 1205 GMT" -> se muestra la hora del dato
      String act = String(propagacion.actualizado);
      display.setCursor(0, 32);
      display.print(F("HAMQSL ")); display.print(act.length() > 8 ? act.substring(act.length() - 8) : act);
    }

    struct tm local;
    if (paginaPropagacion == 0 && horaLocalValida(local)) {
      time_t ahora = time(nullptr);
      struct tm utc;
      gmtime_r(&ahora, &utc);
      char txtHora[24];
      snprintf(txtHora, sizeof(txtHora), "UTC %02d:%02d LOC %02d:%02d", utc.tm_hour, utc.tm_min, local.tm_hour, local.tm_min);
      display.setCursor(0, 42);
      display.print(txtHora);
    }
  }

  if (estadoWifi == WIFI_SIN_CONEXION) {
    dibujarPieSinConexion();
    return;
  }
  String txtPagina = String(paginaPropagacion + 1) + "/2";
  if (opcionPantallaProp == OPCION_BARRA_PROP) {
    dibujarBarraAvanzarRetroceder(true, txtPagina);
  } else {
    String pie = (opcionPantallaProp == 0 ? "> " : "  ") + String("mas > ") + txtPagina +
                 (opcionPantallaProp == 1 ? "  > " : "    ") + String("act.");
    dibujarPieCentrado(pie);
  }
}

void renderizarPantallaSpots() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  uint8_t paginas = max(1, ((int)numSpotsPota + SPOTS_POR_PAGINA - 1) / SPOTS_POR_PAGINA);

  if (!dibujarEstadoWifi()) {
    if (actualizandoWifi) {
      display.setCursor(0, 22);
      display.print(F("ACTUALIZANDO..."));
    } else if (!spotsPotaValidos) {
      display.setCursor(0, 22);
      display.print(F("SIN DATOS"));
    } else if (numSpotsPota == 0) {
      display.setCursor(0, 22);
      display.print(F("NO HAY SPOTS CW"));
    } else {
      // Indicativo, frecuencia (kHz) y referencia en 21 caracteres. La referencia va entera y
      // pegada a la derecha, con la frecuencia delante. Si no cabe todo, la frecuencia pierde
      // el decimal ("14045.5" -> "14045") y, si aun asi no cabe, se recorta el indicativo.
      const int ANCHO_LINEA = 21;
      display.setTextWrap(false);
      for (uint8_t i = 0; i < SPOTS_POR_PAGINA; i++) {
        uint8_t idx = paginaSpots * SPOTS_POR_PAGINA + i;
        if (idx >= numSpotsPota) break;
        const SpotPota& sp = spotsPota[idx];
        char frec[sizeof(sp.frecuencia)];
        strlcpy(frec, sp.frecuencia, sizeof(frec));
        int largoRef = strlen(sp.referencia);
        if ((int)(strlen(sp.indicativo) + strlen(frec) + largoRef) + 2 > ANCHO_LINEA) {
          char* punto = strchr(frec, '.');
          if (punto) *punto = '\0';
        }
        int anchoIndicativo = max(1, ANCHO_LINEA - (int)strlen(frec) - largoRef - 2);
        char linea[40];
        snprintf(linea, sizeof(linea), "%-*.*s %s %s", anchoIndicativo, anchoIndicativo,
                 sp.indicativo, frec, sp.referencia);
        display.setCursor(0, 12 + i * 10);
        display.print(linea);
      }
      display.setTextWrap(true);
    }
  }

  if (estadoWifi == WIFI_SIN_CONEXION) {
    dibujarPieSinConexion();
    return;
  }
  String txtPagina = String(paginaSpots + 1) + "/" + String(paginas);
  if (opcionPantallaSpots == OPCION_BARRA_SPOTS) {
    dibujarBarraAvanzarRetroceder(true, txtPagina);
  } else {
    // Maximo 21 caracteres: "> mas > 10/10  > act."
    String pie = (opcionPantallaSpots == 0 ? "> " : "  ") + String("mas > ") + txtPagina +
                 (opcionPantallaSpots == 1 ? "  > " : "    ") + String("act.");
    dibujarPieCentrado(pie);
  }
}

// ============================================================================
// PANTALLA 7: SOL Y LINEA GRIS (CALCULO LOCAL, SIN WIFI)
// ============================================================================
// Ecuaciones solares de NOAA (precision de un par de minutos). Ventana de linea gris:
// sol entre GRIS_ELEV_MIN y GRIS_ELEV_MAX grados de altura (unos 30-40 min antes y
// despues de la salida y la puesta en la Peninsula). Necesita la hora sincronizada por NTP.
#define GRIS_ELEV_MIN  -6.0
#define GRIS_ELEV_MAX  6.0

// Centro de la cuadricula Maidenhead (4 o 6 caracteres)
bool locatorALatLon(const char* loc, float& lat, float& lon) {
  size_t n = strlen(loc);
  if (n != 4 && n != 6) return false;
  char c0 = toupper(loc[0]), c1 = toupper(loc[1]);
  if (c0 < 'A' || c0 > 'R' || c1 < 'A' || c1 > 'R' || !isdigit(loc[2]) || !isdigit(loc[3])) return false;
  lon = (c0 - 'A') * 20.0 - 180.0 + (loc[2] - '0') * 2.0;
  lat = (c1 - 'A') * 10.0 - 90.0 + (loc[3] - '0') * 1.0;
  if (n == 6) {
    char c4 = toupper(loc[4]), c5 = toupper(loc[5]);
    if (c4 < 'A' || c4 > 'X' || c5 < 'A' || c5 > 'X') return false;
    lon += (c4 - 'A') * (2.0 / 24) + (1.0 / 24);
    lat += (c5 - 'A') * (1.0 / 24) + (0.5 / 24);
  } else {
    lon += 1.0;
    lat += 0.5;
  }
  return true;
}

struct DatosSol {
  float elevacion;      // Grados sobre el horizonte, ahora
  float ecuacionTiempo; // Minutos
  float declinacion;    // Radianes
};

DatosSol calcularSol(const struct tm& utc, float lat, float lon) {
  float horaUtc = utc.tm_hour + utc.tm_min / 60.0 + utc.tm_sec / 3600.0;
  float g = 2 * PI / 365.0 * (utc.tm_yday + (horaUtc - 12) / 24.0);  // tm_yday empieza en 0
  DatosSol d;
  d.ecuacionTiempo = 229.18 * (0.000075 + 0.001868 * cos(g) - 0.032077 * sin(g)
                               - 0.014615 * cos(2 * g) - 0.040849 * sin(2 * g));
  d.declinacion = 0.006918 - 0.399912 * cos(g) + 0.070257 * sin(g) - 0.006758 * cos(2 * g)
                  + 0.000907 * sin(2 * g) - 0.002697 * cos(3 * g) + 0.00148 * sin(3 * g);
  float tiempoSolar = horaUtc * 60 + d.ecuacionTiempo + 4 * lon;  // Minutos
  float anguloHorario = radians(tiempoSolar / 4 - 180);
  float phi = radians(lat);
  float cosZenit = sin(phi) * sin(d.declinacion) + cos(phi) * cos(d.declinacion) * cos(anguloHorario);
  d.elevacion = 90 - degrees(acos(constrain(cosZenit, -1.0f, 1.0f)));
  return d;
}

// Minuto UTC del dia en que el sol pasa por la altura indicada (subiendo o bajando); -1 si no ocurre
int minutoUtcAltura(const DatosSol& d, float lat, float lon, float altura, bool subiendo) {
  float phi = radians(lat);
  float x = (cos(radians(90 - altura)) - sin(phi) * sin(d.declinacion)) / (cos(phi) * cos(d.declinacion));
  if (x < -1 || x > 1) return -1;  // Sol de medianoche o noche polar
  float ha = degrees(acos(x));
  float minuto = 720 - 4 * (lon + (subiendo ? ha : -ha)) - d.ecuacionTiempo;
  return ((int)lround(minuto) % 1440 + 1440) % 1440;
}

// "HH:MM" en hora local a partir del minuto UTC y la diferencia local-UTC en minutos
String formatearMinuto(int minutoUtc, int difLocal) {
  if (minutoUtc < 0) return "--:--";
  int m = ((minutoUtc + difLocal) % 1440 + 1440) % 1440;
  char txt[6];
  snprintf(txt, sizeof(txt), "%02d:%02d", m / 60, m % 60);
  return String(txt);
}

void renderizarPantallaSol() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  float lat, lon;
  struct tm local;
  bool conHora = horaLocalValida(local);
  if (estadoWifi == WIFI_PORTAL || estadoWifi == WIFI_CONECTANDO || estadoWifi == WIFI_SIN_CONEXION ||
      (!conHora && estadoWifi == WIFI_APAGADO)) {
    // Sin hora, SOL enciende el WiFi para sincronizarla (APAGADO solo se ve un instante al entrar)
    dibujarEstadoWifi();
  } else if (!locatorALatLon(locatorEstacion, lat, lon)) {
    display.setCursor(0, 22);
    display.print(F("LOCATOR NO VALIDO"));
    display.setCursor(0, 32);
    display.print(F("CAMBIALO EN WEBCONF"));
  } else if (!conHora && estadoWifi == WIFI_CONECTADO) {
    display.setCursor(0, 22);
    display.print(F("SINCRONIZANDO HORA..."));  // NTP tarda un momento tras conectar
  } else if (!conHora) {
    display.setCursor(0, 12);
    display.print(F("SIN HORA."));
    display.setCursor(0, 22);
    display.print(F("PONLA EN WEBCONF"));
    display.setCursor(0, 32);
    display.print(F("(PANTALLA CONF.)"));
  } else {
    time_t ahora = time(nullptr);
    struct tm utc;
    gmtime_r(&ahora, &utc);
    int difLocal = (local.tm_hour * 60 + local.tm_min) - (utc.tm_hour * 60 + utc.tm_min);
    if (difLocal > 720) difLocal -= 1440;
    if (difLocal < -720) difLocal += 1440;

    DatosSol d = calcularSol(utc, lat, lon);
    // -0.833 grados: salida y puesta oficiales (refraccion y radio del disco)
    String sale = formatearMinuto(minutoUtcAltura(d, lat, lon, -0.833, true), difLocal);
    String pone = formatearMinuto(minutoUtcAltura(d, lat, lon, -0.833, false), difLocal);

    display.setCursor(0, 12);
    display.print("SALE " + sale + " PONE " + pone);

    display.setCursor(0, 22);
    display.print("GRIS AM " + formatearMinuto(minutoUtcAltura(d, lat, lon, GRIS_ELEV_MIN, true), difLocal) +
                  "-" + formatearMinuto(minutoUtcAltura(d, lat, lon, GRIS_ELEV_MAX, true), difLocal));
    display.setCursor(0, 32);
    display.print("GRIS PM " + formatearMinuto(minutoUtcAltura(d, lat, lon, GRIS_ELEV_MAX, false), difLocal) +
                  "-" + formatearMinuto(minutoUtcAltura(d, lat, lon, GRIS_ELEV_MIN, false), difLocal));

    char linea[24];
    bool enGris = d.elevacion >= GRIS_ELEV_MIN && d.elevacion <= GRIS_ELEV_MAX;
    snprintf(linea, sizeof(linea), "%s SOL %+.1f%s", locatorEstacion, d.elevacion, enGris ? " GRIS" : "");
    display.setCursor(0, 42);
    display.print(linea);
  }

  if (estadoWifi == WIFI_SIN_CONEXION) dibujarPieSinConexion();
  else dibujarBarraAvanzarRetroceder(true);
}

// ============================================================================
// PANTALLA 8: CONFIGURACION
// ============================================================================
/**
 * Reset de fabrica: borra estadisticas, configuracion del entrenador, datos de la estacion
 * y la red WiFi guardada, y reinicia.
 */
void resetFabrica() {
  Serial.println(F("[CONFIG] RESET DE FABRICA"));
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.clear();
  prefs.end();
  prefs.begin(NVS_ESTACION, false);
  prefs.clear();
  prefs.end();
  wm.resetSettings();  // Red WiFi guardada

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 28);
  display.print(F("RESET DE FABRICA..."));
  display.display();
  delay(1500);
  ESP.restart();
}

void renderizarPantallaConfig() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (estadoWifi == WIFI_PORTAL || estadoWifi == WIFI_CONECTANDO) {
    dibujarEstadoWifi();
  } else {
    char linea[24];
    snprintf(linea, sizeof(linea), "%-9s %s", indicativoEstacion, locatorEstacion);
    display.setCursor(0, 12);
    display.print(linea);

    display.setCursor(0, 22);
    display.print(F("LORA ")); display.print(estadoLoraStr);

    display.setCursor(0, 32);
    if (bateriaActual.estado == SOLO_USB) {
      display.print(F("BAT  SIN BATERIA"));
    } else {
      snprintf(linea, sizeof(linea), "BAT  %.2fV  %d%%", bateriaActual.voltaje, bateriaActual.porcentaje);
      display.print(linea);
    }

    display.setCursor(0, 42);
    if (confirmandoFabrica) display.print(F("OTRA VEZ: BORRA TODO"));
    else display.print(F("FW   v" FW_VERSION));
  }

  if (opcionPantallaConfig == OPCION_BARRA_CONFIG) {
    // "« opc »" con los caracteres 0xAE y 0xAF de CP437
    dibujarBarraAvanzarRetroceder(true, String((char)0xAE) + " opc " + String((char)0xAF));
  } else if (opcionPantallaConfig == 2) {
    // Apagado automatico: pulsar para editar y girar
    display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
    String txt = "> apagado: ";
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(txt + textoApagado(), 0, 0, &x1, &y1, &w, &h);
    display.setCursor((128 - w) / 2, 55);
    display.print(txt);
    imprimirValor(true, textoApagado());
  } else {
    String pie = (opcionPantallaConfig == 0 ? "> " : "  ") + String(estadoWifi == WIFI_PORTAL ? "ver qr  " : "webconf  ") +
                 (opcionPantallaConfig == 1 ? "> " : "  ") + String("fabrica");
    dibujarPieCentrado(pie);
  }
}

void actualizarPantalla() {
  if (caraActiva) return;  // La cara se dibuja sola (procesarCara)
  if (viendoQrPortal()) {  // Portal abierto: QR a pantalla completa
    dibujarPantallaQr();
    return;
  }
  display.clearDisplay();
  dibujarEncabezado();

  switch (pantallaActual) {
    case PANTALLA_INICIO: renderizarPantallaInicio(); break;
    case PANTALLA_CW: renderizarPantallaCW(); break;
    case PANTALLA_LORA: renderizarPantallaLora(); break;
    case PANTALLA_PROPAGACION: renderizarPantallaPropagacion(); break;
    case PANTALLA_SPOTS: renderizarPantallaSpots(); break;
    case PANTALLA_SOL: renderizarPantallaSol(); break;
    case PANTALLA_CONFIG: renderizarPantallaConfig(); break;
  }

  display.display();
}

// ============================================================================
// MAQUINA DE ESTADOS CW Y BOTON PRG
// ============================================================================
void iniciarPracticaCW() {
  enPractica3_2 = true;
  grupoActualNum = 0;
  textoCompletoLeccion = "";
  numeroErrores = 0;
  paginaListado3_3 = 0;
  opcionFinListado3_3 = 0;
  opcionPantallaCorregir = 0;
  opcionPractica = 0;
  totalCaracteresLeccion = gruposCW * caracteresCW;

  guardarUltimaConfiguracionNVS();

  estadoCW = CW_GENERAR_GRUPO;
  mostrarTextoGrupo = false;
  tiempoCWProximo = millis();
}

void detenerPracticaCW() {
  motorCWDetener();
  enPractica3_2 = false;
  estadoCW = CW_IDLE;
  actualizarPantalla();
}

void generarGrupoKoch() {
  grupoActualStr = "";
  for (int i = 0; i < caracteresCW; i++) {
    grupoActualStr += caracterKochAleatorio(leccionCW);
  }

  if (textoCompletoLeccion.length() > 0) textoCompletoLeccion += " ";
  textoCompletoLeccion += grupoActualStr;
}

void procesarMaquinaEstadosCW() {
  if (!enPractica3_2 || estadoCW == CW_PAUSADO || estadoCW == CW_IDLE || 
      estadoCW == CW_PANTALLA_3_3_LISTADO || estadoCW == CW_PANTALLA_CORREGIR_ERRORES || 
      estadoCW == CW_PANTALLA_RESUMEN_FINAL) return;

  if (millis() < tiempoCWProximo) return;

  switch (estadoCW) {
    case CW_GENERAR_GRUPO:
      if (grupoActualNum >= gruposCW) {
        estadoCW = CW_PANTALLA_3_3_LISTADO;
        paginaListado3_3 = 0;
        opcionFinListado3_3 = 0;
        actualizarPantalla();
        return;
      }

      grupoActualNum++;
      generarGrupoKoch();
      mostrarTextoGrupo = false;
      estadoCW = CW_EMITIENDO_GRUPO;
      actualizarPantalla();
      motorCWEmitir(grupoActualStr.c_str(), wpmCW, espaciadoCW, "KOCH");
      break;

    case CW_EMITIENDO_GRUPO:
      if (motorCWOcupado()) return;
      mostrarTextoGrupo = true;
      actualizarPantalla();
      estadoCW = CW_PAUSA_ENTRE_GRUPOS;
      tiempoCWProximo = motorCWFinMs + 2000;
      break;

    case CW_PAUSA_ENTRE_GRUPOS:
      estadoCW = CW_GENERAR_GRUPO;
      break;

    default:
      break;
  }
}

// ============================================================================
// CARA DEL ROBOT (SALVAPANTALLAS DE INICIO)
// ============================================================================
// Diseno propio, dibujado con primitivas (sin imagenes): dos ojos y una boca que cambian
// de forma con la expresion. Aparece tras CARA_ESPERA_MS sin tocar nada en Inicio y se
// alterna con la pantalla de Inicio. Cualquier giro o pulsacion la quita sin hacer nada mas.
#define CARA_ESPERA_MS        15000UL  // Sin tocar nada en Inicio: aparece la cara
#define CARA_TIEMPO_MS        45000UL  // La cara se queda este tiempo...
#define CARA_INICIO_MS         8000UL  // ...y vuelve Inicio este tiempo, alternando
#define CARA_DORMIR_MS       300000UL  // Sin tocar nada 5 min: se duerme y deja de alternar
#define CARA_FOTOGRAMA_MS          66  // Unos 15 fotogramas/s (cada uno tarda unos 25 ms por I2C)
#define CARA_FOTOGRAMA_DORMIDA_MS 250
#define CARA_FELIZ_RECIENTE_MS (15UL * 60 * 1000)  // Leccion >= 90 % hace menos de 15 min: contenta

// Geometria (pantalla de 128x64): ojos arriba y boca debajo
#define OJO_ANCHO      30
#define OJO_ALTO       26
#define OJO_RADIO       7
#define OJO_SEPARACION 46  // Entre los centros de los dos ojos
#define OJOS_Y         24  // Centro vertical de los ojos
#define BOCA_Y         50  // Centro vertical de la boca
#define MIRADA_MAX_X   14
#define MIRADA_MAX_Y    4

enum ExpresionCara { EXP_NORMAL, EXP_FELIZ, EXP_RISA, EXP_APRETADOS, EXP_GUINO, EXP_PICARA,
                     EXP_NERVIOSA, EXP_SORPRESA, EXP_CANSADA, EXP_DORMIDA };
enum FormaOjo { OJO_ABIERTO, OJO_ARCO, OJO_CHEVRON, OJO_PARPADO, OJO_CAIDO };
enum FormaBoca { BOCA_SONRISA, BOCA_ABIERTA, BOCA_GATO, BOCA_O, BOCA_RECTA, BOCA_ONDA };

struct EstadoOjos { float dx, dy, ancho, alto; };  // Mirada (desplazamiento) y tamano

unsigned long ultimaActividadMs = 0;  // Ultimo giro o pulsacion
unsigned long cambioCaraMs = 0;       // Cuando aparecio o se quito la cara
EstadoOjos ojos = {0, 0, OJO_ANCHO, OJO_ALTO};
EstadoOjos ojosObjetivo = {0, 0, OJO_ANCHO, OJO_ALTO};  // Los ojos se acercan a esto poco a poco
ExpresionCara expresionCara = EXP_NORMAL;
bool guinoIzquierdo = false;            // Ojo que guina
unsigned long inicioExpresionMs = 0;
unsigned long duracionExpresionMs = 0;  // 0 = expresion de fondo, sin fin
unsigned long inicioParpadeoMs = 0;     // 0 = no esta parpadeando
unsigned long proximoParpadeoMs = 0;
unsigned long proximaMiradaMs = 0;
unsigned long proximoGestoMs = 0;

bool momentoLlegado(unsigned long t) {
  return (long)(millis() - t) >= 0;
}

bool bateriaBaja() {
  return bateriaActual.estado == BAT_DESCARGA && bateriaActual.porcentaje < 20;
}

bool leccionRecienAprobada() {
  return ultimoPctKoch >= 90 && millis() - ultimoPctKochMs < CARA_FELIZ_RECIENTE_MS;
}

// Expresion a la que vuelve la cara cuando no esta haciendo ningun gesto
ExpresionCara expresionDeFondo() {
  return bateriaBaja() ? EXP_CANSADA : EXP_NORMAL;
}

void ponerExpresion(ExpresionCara e, unsigned long duracionMs) {
  expresionCara = e;
  inicioExpresionMs = millis();
  duracionExpresionMs = duracionMs;
  ojosObjetivo.ancho = OJO_ANCHO;
  ojosObjetivo.alto = OJO_ALTO;
  switch (e) {
    case EXP_SORPRESA: ojosObjetivo.ancho = OJO_ANCHO + 4; ojosObjetivo.alto = OJO_ALTO + 6; break;
    case EXP_NERVIOSA: ojosObjetivo.alto = 12; break;
    case EXP_CANSADA:  ojosObjetivo.alto = OJO_ALTO - 4; break;
    case EXP_DORMIDA:  ojosObjetivo.alto = 3; ojosObjetivo.dx = 0; ojosObjetivo.dy = 3; break;
    case EXP_GUINO:    guinoIzquierdo = random(2); break;
    default: break;
  }
}

/**
 * Gesto al azar. Tras aprobar una leccion salen mas a menudo los alegres.
 */
void hacerGestoAleatorio() {
  // Pesos: contenta, risa, apretados, guino, picara, nerviosa, sorpresa
  static const uint8_t PESOS_NORMAL[]  = { 18, 12, 10, 18, 16, 12, 14 };
  static const uint8_t PESOS_ALEGRE[]  = { 30, 25, 10, 15, 10,  2,  8 };
  static const ExpresionCara GESTOS[]  = { EXP_FELIZ, EXP_RISA, EXP_APRETADOS, EXP_GUINO,
                                           EXP_PICARA, EXP_NERVIOSA, EXP_SORPRESA };
  static const uint16_t DURACION_MS[]  = { 2000, 1800, 1500, 700, 2500, 2000, 1200 };
  const uint8_t* pesos = leccionRecienAprobada() ? PESOS_ALEGRE : PESOS_NORMAL;

  int total = 0;
  for (uint8_t i = 0; i < 7; i++) total += pesos[i];
  int r = random(total);
  for (uint8_t i = 0; i < 7; i++) {
    if (r < pesos[i]) { ponerExpresion(GESTOS[i], DURACION_MS[i]); return; }
    r -= pesos[i];
  }
}

void activarCara() {
  unsigned long ahora = millis();
  caraActiva = true;
  cambioCaraMs = ahora;
  ojos = {0, 0, OJO_ANCHO, 2};  // Aparece abriendo los ojos
  ojosObjetivo = {0, 0, OJO_ANCHO, OJO_ALTO};
  if (leccionRecienAprobada()) ponerExpresion(EXP_RISA, 2500);
  else ponerExpresion(expresionDeFondo(), 0);
  inicioParpadeoMs = 0;
  proximoParpadeoMs = ahora + random(2000, 5000);
  proximaMiradaMs = ahora + random(800, 2000);
  proximoGestoMs = ahora + random(6000, 14000);
}

void desactivarCara() {
  caraActiva = false;
  cambioCaraMs = millis();
  actualizarPantalla();
}

// Arco grueso de circunferencia, de "desde" a "hasta" grados (0 = derecha, 90 = abajo)
void dibujarArco(int cx, int cy, int r, int grosor, int desde, int hasta) {
  for (int a = desde; a <= hasta; a += 4) {
    float rad = a * DEG_TO_RAD;
    float c = cosf(rad), s = sinf(rad);
    for (int g = 0; g < grosor; g++) {
      display.drawPixel(cx + (int)roundf((r - g) * c), cy + (int)roundf((r - g) * s), SSD1306_WHITE);
    }
  }
}

// Linea gruesa: la misma linea repetida hacia abajo
void dibujarLineaGruesa(int x0, int y0, int x1, int y1, int grosor) {
  for (int g = 0; g < grosor; g++) display.drawLine(x0, y0 + g, x1, y1 + g, SSD1306_WHITE);
}

void dibujarOjo(int cx, int cy, int ancho, int alto, FormaOjo forma, bool izquierdo) {
  ancho = max(ancho, 4);
  alto = max(alto, 2);
  int x = cx - ancho / 2;
  int y = cy - alto / 2;

  switch (forma) {
    case OJO_ARCO:
      // "^": ojo cerrado sonriendo
      dibujarArco(cx, cy + 6, ancho / 2 - 2, 4, 200, 340);
      break;
    case OJO_CHEVRON: {
      // ">" el izquierdo y "<" el derecho: ojos apretados que miran al centro
      int punta = izquierdo ? cx + ancho / 4 : cx - ancho / 4;
      int lado = izquierdo ? cx - ancho / 4 : cx + ancho / 4;
      dibujarLineaGruesa(lado, cy - 9, punta, cy - 1, 4);
      dibujarLineaGruesa(punta, cy - 1, lado, cy + 7, 4);
      break;
    }
    default:
      display.fillRoundRect(x, y, ancho, alto, min(OJO_RADIO, min(ancho, alto) / 2), SSD1306_WHITE);
      if (forma == OJO_PARPADO && alto > 8) {
        // Parpado recto a media altura: mirada picara
        display.fillRect(x - 1, y - 1, ancho + 2, alto / 2, SSD1306_BLACK);
      } else if (forma == OJO_CAIDO && alto > 8) {
        // Parpado caido hacia el lado de fuera: cansancio
        int xFuera = izquierdo ? x - 1 : x + ancho;
        int xDentro = izquierdo ? x + ancho : x - 1;
        display.fillTriangle(xFuera, y + alto / 2, xFuera, y - 1, xDentro, y - 1, SSD1306_BLACK);
        display.fillRect(x - 1, y - 1, ancho + 2, alto / 5, SSD1306_BLACK);
      }
      break;
  }
}

void dibujarBoca(int cx, int cy, FormaBoca forma) {
  switch (forma) {
    case BOCA_SONRISA:
      dibujarArco(cx, cy - 6, 8, 2, 25, 155);
      break;
    case BOCA_ABIERTA:
      // Media luna rellena hacia abajo (risa)
      for (int i = 0; i <= 9; i++) {
        int w = (int)sqrtf(81.0f - i * i);
        display.drawFastHLine(cx - w, cy - 4 + i, 2 * w + 1, SSD1306_WHITE);
      }
      break;
    case BOCA_GATO:
      // "w"
      dibujarArco(cx - 4, cy - 3, 4, 2, 0, 180);
      dibujarArco(cx + 4, cy - 3, 4, 2, 0, 180);
      break;
    case BOCA_O:
      display.drawCircle(cx, cy, 4, SSD1306_WHITE);
      display.drawCircle(cx, cy, 3, SSD1306_WHITE);
      break;
    case BOCA_RECTA:
      display.fillRect(cx - 6, cy, 13, 2, SSD1306_WHITE);
      break;
    case BOCA_ONDA:
      dibujarLineaGruesa(cx - 9, cy, cx - 5, cy - 3, 2);
      dibujarLineaGruesa(cx - 5, cy - 3, cx - 1, cy, 2);
      dibujarLineaGruesa(cx - 1, cy, cx + 3, cy - 3, 2);
      dibujarLineaGruesa(cx + 3, cy - 3, cx + 7, cy, 2);
      break;
  }
}

// Gota de sudor (expresion nerviosa), junto al ojo derecho
void dibujarGota(int x, int y) {
  display.fillCircle(x, y + 5, 3, SSD1306_WHITE);
  display.fillTriangle(x - 3, y + 5, x + 3, y + 5, x, y - 2, SSD1306_WHITE);
}

void dibujarCara() {
  display.clearDisplay();

  FormaOjo ojoIzq = OJO_ABIERTO, ojoDer = OJO_ABIERTO;
  FormaBoca boca = BOCA_SONRISA;
  switch (expresionCara) {
    case EXP_FELIZ:     ojoIzq = ojoDer = OJO_ARCO; break;
    case EXP_RISA:      ojoIzq = ojoDer = OJO_ARCO; boca = BOCA_ABIERTA; break;
    case EXP_APRETADOS: ojoIzq = ojoDer = OJO_CHEVRON; boca = BOCA_ABIERTA; break;
    case EXP_GUINO:     (guinoIzquierdo ? ojoIzq : ojoDer) = OJO_ARCO; break;
    case EXP_PICARA:    ojoIzq = ojoDer = OJO_PARPADO; boca = BOCA_GATO; break;
    case EXP_NERVIOSA:  boca = BOCA_ONDA; break;
    case EXP_SORPRESA:  boca = BOCA_O; break;
    case EXP_CANSADA:   ojoIzq = ojoDer = OJO_CAIDO; boca = BOCA_RECTA; break;
    case EXP_DORMIDA:   boca = BOCA_O; break;
    default: break;
  }

  // Parpadeo (solo con los ojos abiertos): el alto baja hasta casi cerrar y vuelve en 240 ms
  float alto = ojos.alto;
  if (inicioParpadeoMs != 0) {
    unsigned long t = millis() - inicioParpadeoMs;
    if (t >= 240) inicioParpadeoMs = 0;
    else alto = max(2.0f, alto * fabsf((float)t - 120.0f) / 120.0f);
  }

  int cx = 64 + (int)ojos.dx;
  int dy = (int)ojos.dy;
  int xIzq = cx - OJO_SEPARACION / 2;
  int xDer = cx + OJO_SEPARACION / 2;
  dibujarOjo(xIzq, OJOS_Y + dy, (int)ojos.ancho, ojoIzq == OJO_ARCO || ojoIzq == OJO_CHEVRON ? (int)ojos.alto : (int)alto, ojoIzq, true);
  dibujarOjo(xDer, OJOS_Y + dy, (int)ojos.ancho, ojoDer == OJO_ARCO || ojoDer == OJO_CHEVRON ? (int)ojos.alto : (int)alto, ojoDer, false);

  if (expresionCara == EXP_DORMIDA) {
    // Boca pequena y "z" y "Z" que suben despacio
    display.drawCircle(cx, BOCA_Y + dy, 2, SSD1306_WHITE);
    uint8_t paso = (millis() / CARA_FOTOGRAMA_DORMIDA_MS) % 10;
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(100, 22 - paso);
    display.print('z');
    display.setTextSize(2);
    display.setCursor(110, 12 - paso);
    display.print('Z');
    display.setTextSize(1);
  } else {
    dibujarBoca(cx, BOCA_Y + dy, boca);
  }
  if (expresionCara == EXP_NERVIOSA) dibujarGota(xDer + OJO_ANCHO / 2 + 4, OJOS_Y + dy - 12);

  display.display();
}

/**
 * En Inicio: saca la cara tras un rato sin tocar nada, la alterna con la pantalla de Inicio
 * y la anima (parpadeos, miradas y gestos al azar).
 */
void procesarCara() {
  if (pantallaActual != PANTALLA_INICIO) return;
  unsigned long ahora = millis();
  bool dormir = ahora - ultimaActividadMs >= CARA_DORMIR_MS;

  if (!caraActiva) {
    if (ahora - ultimaActividadMs < CARA_ESPERA_MS || ahora - cambioCaraMs < CARA_INICIO_MS) return;
    activarCara();
  } else if (!dormir && ahora - cambioCaraMs >= CARA_TIEMPO_MS) {
    desactivarCara();  // Un rato de Inicio y vuelve
    return;
  }

  static unsigned long ultimoFotogramaMs = 0;
  unsigned long intervalo = (expresionCara == EXP_DORMIDA) ? CARA_FOTOGRAMA_DORMIDA_MS : CARA_FOTOGRAMA_MS;
  if (ahora - ultimoFotogramaMs < intervalo) return;
  ultimoFotogramaMs = ahora;

  if (dormir) {
    if (expresionCara != EXP_DORMIDA) ponerExpresion(EXP_DORMIDA, 0);  // procesarBrillo() la atenua
  } else {
    // Fin de un gesto, o cambio de la expresion de fondo (bateria)
    bool gestoAcabado = duracionExpresionMs != 0 && ahora - inicioExpresionMs >= duracionExpresionMs;
    if (gestoAcabado || (duracionExpresionMs == 0 && expresionCara != expresionDeFondo())) {
      ponerExpresion(expresionDeFondo(), 0);
    }
    if (momentoLlegado(proximoParpadeoMs)) {
      if (duracionExpresionMs == 0) inicioParpadeoMs = ahora;  // No parpadea en mitad de un gesto
      proximoParpadeoMs = ahora + random(2500, 6000);
    }
    if (momentoLlegado(proximaMiradaMs)) {
      if (random(100) < 35) {
        ojosObjetivo.dx = 0;
        ojosObjetivo.dy = 0;
      } else {
        ojosObjetivo.dx = random(-MIRADA_MAX_X, MIRADA_MAX_X + 1);
        ojosObjetivo.dy = random(-MIRADA_MAX_Y, MIRADA_MAX_Y + 1);
      }
      proximaMiradaMs = ahora + random(1000, 3500);
    }
    if (momentoLlegado(proximoGestoMs)) {
      hacerGestoAleatorio();
      proximoGestoMs = ahora + random(6000, 15000);
    }
  }

  // Movimiento suave: cada fotograma recorre un tercio de lo que falta
  const float k = 0.35f;
  ojos.dx += (ojosObjetivo.dx - ojos.dx) * k;
  ojos.dy += (ojosObjetivo.dy - ojos.dy) * k;
  ojos.ancho += (ojosObjetivo.ancho - ojos.ancho) * k;
  ojos.alto += (ojosObjetivo.alto - ojos.alto) * k;

  dibujarCara();
}

// ============================================================================
// ENTRADA DE USUARIO: EVENTOS DE BOTON
// ============================================================================
// Toda la logica de menus trabaja con eventos, no con el hardware. Hay dos fuentes:
// - Boton PRG: corta / larga, como siempre.
// - Encoder: giro a derecha / izquierda (mueve el foco o cambia el valor que se edita),
//   pulsacion corta del eje (= corta del PRG) y larga del eje (= larga del PRG, salvo
//   donde esta solo mueve el foco: ahi no hace nada, o sale de la edicion).
enum EventoBoton { BOTON_NINGUNO, BOTON_CORTA, BOTON_LARGA, ENC_DERECHA, ENC_IZQUIERDA, ENC_PULSA, ENC_LARGA };

struct Pulsador {
  uint8_t pin;
  bool ultimoEstado;
  unsigned long inicio;
  bool pulsando;
  unsigned long ultimaDuracion;  // De la ultima pulsacion (para la muy larga del modo sin encoder)
};
Pulsador pulsadorPrg = { BUTTON_PIN, HIGH, 0, false, 0 };
Pulsador pulsadorEje = { ENC_SW_PIN, HIGH, 0, false, 0 };

/**
 * Lee un pulsador sin bloquear y da el evento al SOLTARLO: 1 = corta (mas de
 * TIEMPO_ANTIRREBOTE_MS), 2 = larga (>= TIEMPO_PULSACION_LARGA), 0 = nada.
 * Los rebotes dan pulsaciones de menos de TIEMPO_ANTIRREBOTE_MS y se ignoran
 * (el pulsador del encoder rebota al soltar: probado en placa).
 */
uint8_t leerPulsador(Pulsador& p) {
  bool estadoActual = digitalRead(p.pin);
  uint8_t ev = 0;

  if (p.ultimoEstado == HIGH && estadoActual == LOW) {
    p.inicio = millis();
    p.pulsando = true;
  }

  if (p.ultimoEstado == LOW && estadoActual == HIGH && p.pulsando) {
    unsigned long duracion = millis() - p.inicio;
    p.pulsando = false;
    p.ultimaDuracion = duracion;

    if (duracion >= TIEMPO_PULSACION_LARGA) ev = 2;
    else if (duracion > TIEMPO_ANTIRREBOTE_MS) ev = 1;
  }

  p.ultimoEstado = estadoActual;
  return ev;
}

// Encoder por interrupcion: un cambio en CLK o DT avanza un decodificador de cuadratura por tabla.
// El KY-040 da 4 transiciones por paso y reposa con CLK = DT = 1. Los rebotes van y vuelven
// en la tabla y no suman pasos. Sentido horario = positivo (probado en placa).
static_assert(ENC_CLK_PIN < 32 && ENC_DT_PIN < 32, "La ISR lee CLK y DT de GPIO_IN_REG (GPIO 0-31)");
DRAM_ATTR const int8_t TABLA_CUADRATURA[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
volatile uint8_t encEstado = 3;
volatile int8_t encAcumulado = 0;
volatile int16_t encPasos = 0;  // Pasos pendientes de procesar (+ horario, - antihorario)
portMUX_TYPE muxEncoder = portMUX_INITIALIZER_UNLOCKED;

// Solo registros y datos en RAM: puede ejecutarse mientras se escribe la flash (NVS)
void IRAM_ATTR isrEncoder() {
  uint32_t entradas = REG_READ(GPIO_IN_REG);
  uint8_t nuevo = (((entradas >> ENC_CLK_PIN) & 1) << 1) | ((entradas >> ENC_DT_PIN) & 1);
  portENTER_CRITICAL_ISR(&muxEncoder);
  if (nuevo != encEstado) {
    encAcumulado += TABLA_CUADRATURA[(encEstado << 2) | nuevo];
    encEstado = nuevo;
    if (nuevo == 3) {
      if (encAcumulado >= 2) encPasos++;
      else if (encAcumulado <= -2) encPasos--;
      encAcumulado = 0;
    }
  }
  portEXIT_CRITICAL_ISR(&muxEncoder);
}

void iniciarEncoder() {
  pinMode(ENC_CLK_PIN, INPUT_PULLUP);
  pinMode(ENC_DT_PIN, INPUT_PULLUP);
  pinMode(ENC_SW_PIN, INPUT_PULLUP);
  encEstado = (digitalRead(ENC_CLK_PIN) << 1) | digitalRead(ENC_DT_PIN);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK_PIN), isrEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT_PIN), isrEncoder, CHANGE);
}

/**
 * Devuelve el siguiente evento de entrada: primero el PRG, luego el pulsador del eje y
 * luego un paso de giro (si se acumulan varios, se entregan de uno en uno).
 */
EventoBoton leerEventoBoton() {
  uint8_t prg = leerPulsador(pulsadorPrg);
#ifdef SIN_ENCODER
  // Placa sin encoder (entorno heltec_sin_encoder): el PRG hace de encoder. Corta = girar a la
  // derecha (mover el foco), larga = pulsar el eje (ejecutar), muy larga = PRG largo (dormir).
  // No hay atras: todas las pantallas tienen volver o ant./sig.
  if (prg == 1) return ENC_DERECHA;
  if (prg == 2) return (pulsadorPrg.ultimaDuracion >= TIEMPO_PULSACION_DORMIR) ? BOTON_LARGA : ENC_PULSA;
  return BOTON_NINGUNO;
#endif
  if (prg == 1) return BOTON_CORTA;
  if (prg == 2) return BOTON_LARGA;

  uint8_t eje = leerPulsador(pulsadorEje);
  if (eje == 1) return ENC_PULSA;
  if (eje == 2) return ENC_LARGA;

  int16_t pasos;
  portENTER_CRITICAL(&muxEncoder);
  pasos = encPasos;
  if (pasos > 0) encPasos--;
  else if (pasos < 0) encPasos++;
  portEXIT_CRITICAL(&muxEncoder);
  if (pasos > 0) return ENC_DERECHA;
  if (pasos < 0) return ENC_IZQUIERDA;
  return BOTON_NINGUNO;
}

/**
 * Mueve el foco por el anillo de una pantalla: posicion 0 = "sig.", 1..barra = opciones
 * 0..barra-1 y barra+1 = "ant.". Con paso +1 es el ciclo de siempre de la pulsacion larga.
 */
void moverFocoAnillo(uint8_t& opcion, uint8_t barra, int8_t paso) {
  uint8_t n = barra + 2;
  uint8_t pos = (opcion == barra) ? (opcionNavegacionP1234 == 1 ? 0 : barra + 1) : opcion + 1;
  pos = (pos + n + paso) % n;
  if (pos == 0) { opcion = barra; opcionNavegacionP1234 = 1; }
  else if (pos == barra + 1) { opcion = barra; opcionNavegacionP1234 = 0; }
  else opcion = pos - 1;
}

// Cambia un valor con el encoder, sin dar la vuelta al llegar a los limites
template <typename T>
void ajustarValor(T& valor, int paso, int minimo, int maximo) {
#ifdef SIN_ENCODER
  // Sin encoder solo se avanza en un sentido: al pasar del maximo se vuelve al minimo
  int v = (int)valor + paso;
  valor = (T)(v > maximo ? minimo : (v < minimo ? maximo : v));
#else
  valor = (T)constrain((int)valor + paso, minimo, maximo);
#endif
}

/**
 * Cambia de pantalla principal con las opciones en reposo y el foco en la barra: en "sig."
 * si se avanza (o se va a Inicio) y en "ant." si se retrocede, para poder seguir en el mismo sentido
 */
void entrarEnPantalla(uint8_t pantalla, bool haciaAtras = false) {
  pantallaActual = pantalla;
  opcionNavegacionP1234 = haciaAtras ? 0 : 1;
  if (pantalla == PANTALLA_CW) opcionPantalla3 = OPCION_BARRA_CW;
  else if (pantalla == PANTALLA_LORA) opcionPantalla4 = 2;
  else if (pantalla == PANTALLA_PROPAGACION) opcionPantallaProp = OPCION_BARRA_PROP;
  else if (pantalla == PANTALLA_SPOTS) opcionPantallaSpots = OPCION_BARRA_SPOTS;
  else if (pantalla == PANTALLA_CONFIG) { opcionPantallaConfig = OPCION_BARRA_CONFIG; confirmandoFabrica = false; }
}

// "ant." o "sig." desde la barra inferior
void navegarDesdeBarra() {
  if (opcionNavegacionP1234 == 0) entrarEnPantalla(pantallaActual == 1 ? TOTAL_PANTALLAS : pantallaActual - 1, true);
  else entrarEnPantalla(pantallaActual == TOTAL_PANTALLAS ? 1 : pantallaActual + 1);
  contadorPulsaciones++;
}

// ============================================================================
// PROCESAMIENTO INTERACTIVO DE LOS EVENTOS DE ENTRADA
// ============================================================================
// Todo se puede hacer solo con el encoder: girar mueve el foco (o cambia el valor que se
// edita) y pulsar ejecuta. La larga del eje es solo un atajo. El PRG corto es "atras":
// sale de la edicion, sube un nivel y, en las pantallas principales, lleva a Inicio.
// La larga del PRG duerme la placa (deep sleep; se despierta pulsando PRG).
void dormirChispas(const char* motivo);  // Definida antes de setup()

void procesarBoton() {
  EventoBoton ev = leerEventoBoton();
  if (ev == BOTON_NINGUNO) return;
  if (ev == BOTON_LARGA) {
    dormirChispas("PRG LARGO");  // No vuelve: al despertar, la placa arranca de nuevo
    return;
  }

  ultimaActividadMs = millis();
  if (caraActiva) {
    // Con la cara a la vista, el gesto solo la quita: se descartan los pasos de giro que queden
    portENTER_CRITICAL(&muxEncoder);
    encPasos = 0;
    portEXIT_CRITICAL(&muxEncoder);
    desactivarCara();
    return;
  }

  // Con el QR del portal a la vista, cualquier giro o pulsacion (tambien "atras") vuelve a los datos
  if (viendoQrPortal()) {
    vistaPortal = VISTA_DATOS;
    actualizarPantalla();
    return;
  }

  int8_t giro = (ev == ENC_DERECHA) ? 1 : (ev == ENC_IZQUIERDA) ? -1 : 0;
  bool pulsa = (ev == ENC_PULSA);
  bool ejeLarga = (ev == ENC_LARGA);
  bool atras = (ev == BOTON_CORTA);

  // PROP, POTA o SOL sin conexion WiFi: [sig.] -> [abrir webconf] -> [ant.]
  if (estadoWifi == WIFI_SIN_CONEXION && (pantallaActual == PANTALLA_PROPAGACION ||
      pantallaActual == PANTALLA_SPOTS || pantallaActual == PANTALLA_SOL)) {
    if (atras) entrarEnPantalla(PANTALLA_INICIO);
    else if (giro != 0) moverFocoAnillo(opcionSinConexion, 1, giro);
    else if (pulsa) {
      if (opcionSinConexion == 0) abrirPortalWifi();
      else navegarDesdeBarra();
    }
    actualizarPantalla();
    return;
  }

  // Mientras se edita un valor, "atras" y la larga del eje solo salen de la edicion
  if ((atras || ejeLarga) && editandoValor) {
    editandoValor = false;
    actualizarPantalla();
    return;
  }

  if (pantallaActual == PANTALLA_LORA) {
    if (!enPantalla4_Sub) {
      if (atras) {
        entrarEnPantalla(PANTALLA_INICIO);
      } else if (giro != 0) {
        // Foco: [sig.] -> [CW DUO] -> [CW QSO] -> [ant.] -> [sig.] ...
        moverFocoAnillo(opcionPantalla4, 2, giro);
      } else if (pulsa) {
        if (opcionPantalla4 == 0) {
          enPantalla4_Sub = true; subpantalla4Activa = 1;
          modoRadioRxActivo = true; opcionDuo4_1 = 3;
          loraEscuchar();  // La radio duerme fuera de Duo y QSO
        } else if (opcionPantalla4 == 1) {
          enPantalla4_Sub = true; subpantalla4Activa = 2;
          modoRadioRxActivo = true;
          loraEscuchar();
        } else {
          navegarDesdeBarra();
        }
      }

    } else if (subpantalla4Activa == 1) {
      // Duo. Foco: 0 wpm, 1 espaciado, 2 leccion, 3 enviar, 4 ver (solo si hay grupo oculto), 5 volver
      if (atras) {
        salirModoLoRa();
      } else if (giro != 0) {
        if (editandoValor) {
          switch (opcionDuo4_1) {
            case 0: ajustarValor(wpmCW, giro, 15, 30); break;
            case 1: ajustarValor(espaciadoCW, giro * 10, 100, 200); break;
            case 2: ajustarValor(leccionCW, giro, 1, 40); break;
          }
        } else {
          do {
            opcionDuo4_1 = (opcionDuo4_1 + 6 + giro) % 6;
          } while (opcionDuo4_1 == 4 && !hayGrupoDuoPorRevelar());
        }
      } else if (ejeLarga) {
        grupoDuoRevelado = true;  // Atajo de "ver"
        if (opcionDuo4_1 == 4) opcionDuo4_1 = 3;
      } else if (pulsa) {
        switch (opcionDuo4_1) {
          case 0: case 1: case 2:
            editandoValor = !editandoValor;  // Pulsar, girar y pulsar
            break;
          case 3: {
            String nuevoGrupo = "";
            for (int i = 0; i < caracteresCW; i++) nuevoGrupo += caracterKochAleatorio(leccionCW);
            transmitirGrupoDuoLoRa(nuevoGrupo);
            break;
          }
          case 4: grupoDuoRevelado = true; opcionDuo4_1 = 3; break;
          case 5: salirModoLoRa(); break;
        }
      }

    } else if (subpantalla4Activa == 2) {
      // QSO (por hacer): girar cambia el modo, pulsar vuelve
      if (giro != 0) modoKeyer4_2 = (modoKeyer4_2 + 3 + giro) % 3;
      else if (atras || pulsa || ejeLarga) salirModoLoRa();
    }

  } else if (pantallaActual == PANTALLA_PROPAGACION || pantallaActual == PANTALLA_SPOTS ||
             pantallaActual == PANTALLA_CONFIG) {
    // P4 y P5: [sig.] -> [mas >] -> [act.] -> [ant.]
    // P7: [sig.] -> [webconf] -> [fabrica] -> [apagado] -> [ant.]
    bool esProp = (pantallaActual == PANTALLA_PROPAGACION);
    bool esConfig = (pantallaActual == PANTALLA_CONFIG);
    uint8_t& opcion = esProp ? opcionPantallaProp : (esConfig ? opcionPantallaConfig : opcionPantallaSpots);
    uint8_t barra = esProp ? OPCION_BARRA_PROP : (esConfig ? OPCION_BARRA_CONFIG : OPCION_BARRA_SPOTS);
    if (atras) {
      entrarEnPantalla(PANTALLA_INICIO);
    } else if (esConfig && opcion == 2 && editandoValor && giro != 0) {
      ajustarApagado(giro);
      guardarApagadoNVS();
    } else if (esConfig && opcion == 2 && pulsa) {
      editandoValor = !editandoValor;  // Pulsar, girar y pulsar
    } else if (giro != 0) {
      confirmandoFabrica = false;
      moverFocoAnillo(opcion, barra, giro);
    } else if (pulsa) {
      if (opcion == barra) {
        navegarDesdeBarra();
      } else if (esConfig && opcion == 0) {
        if (estadoWifi == WIFI_PORTAL) vistaPortal = VISTA_QR_RED;  // "ver qr": el portal ya esta abierto
        else abrirPortalWifi();
      } else if (esConfig && opcion == 1) {
        if (!confirmandoFabrica) confirmandoFabrica = true;
        else resetFabrica();
      } else if (esProp && opcion == 0) {
        paginaPropagacion = (paginaPropagacion + 1) % 2;
      } else if (esProp && opcion == 1) {
        pedirActualizacionWifi();
      } else if (!esProp && !esConfig && opcion == 0) {
        uint8_t paginas = max(1, ((int)numSpotsPota + SPOTS_POR_PAGINA - 1) / SPOTS_POR_PAGINA);
        paginaSpots = (paginaSpots + 1) % paginas;
      } else if (!esProp && !esConfig && opcion == 1) {
        pedirActualizacionWifi();
      }
    }

  } else if (pantallaActual == PANTALLA_CW) {
    if (enEcho) {
      // Foco: 0 wpm, 1 largo, 2 ver, 3 repetir, 4 volver. Las palas funcionan con cualquier foco
      if (atras || ejeLarga) {
        salirDeEcho();
      } else if (giro != 0) {
        if (editandoValor && opcionEcho == 0) ajustarValor(keyerWpm, giro, KEYER_WPM_MIN, KEYER_WPM_MAX);
        else if (editandoValor && opcionEcho == 1) ajustarValor(echoLargo, giro, 1, 5);
        else opcionEcho = (opcionEcho + 5 + giro) % 5;
      } else if (pulsa) {
        switch (opcionEcho) {
          case 0:
            editandoValor = !editandoValor;
            break;
          case 1:
            editandoValor = !editandoValor;
            if (!editandoValor && estadoEcho != ECHO_ESPERA) echoNuevoReto(false);  // Nuevo largo: reto nuevo
            break;
          case 2: echoVerObjetivo = !echoVerObjetivo; break;
          case 3:
            // "iniciar" la primera vez; despues, "repetir" (volver a oirlo, no cuenta como intento)
            echoNuevoReto(estadoEcho != ECHO_ESPERA);
            break;
          case 4: salirDeEcho(); break;
        }
      }

    } else if (enKeyer) {
      // Foco: 0 wpm, 1 modo, 2 palas, 3 borrar, 4 volver. Las palas funcionan con cualquier foco
      if (atras || ejeLarga) {
        salirDeKeyer();
      } else if (giro != 0) {
        if (editandoValor) ajustarValor(keyerWpm, giro, KEYER_WPM_MIN, KEYER_WPM_MAX);
        else opcionKeyer = (opcionKeyer + 5 + giro) % 5;
      } else if (pulsa) {
        switch (opcionKeyer) {
          case 0: editandoValor = !editandoValor; break;
          case 1: keyerModoB = !keyerModoB; break;
          case 2: keyerPalasInvertidas = !keyerPalasInvertidas; break;
          case 3: textoKeyer = ""; break;
          case 4: salirDeKeyer(); break;
        }
      }

    } else if (enEstadisticas3_5) {
      // Girar pasa pagina (la ultima es la de borrado). Pulsar: volver, o borrar en la ultima
      if (atras || ejeLarga) {
        enEstadisticas3_5 = false; confirmandoBorrado = false;
      } else if (giro != 0) {
        confirmandoBorrado = false;
        paginaStats3_5 = (paginaStats3_5 + PAGINAS_STATS_3_5 + 1 + giro) % (PAGINAS_STATS_3_5 + 1);
      } else if (pulsa) {
        if (paginaStats3_5 < PAGINAS_STATS_3_5) {
          enEstadisticas3_5 = false;
        } else if (!confirmandoBorrado) {
          confirmandoBorrado = true;
        } else {
          borrarTodasLasEstadisticasNVS(); confirmandoBorrado = false; paginaStats3_5 = 0;
        }
      }

    } else if (enPractica3_2) {
      if (estadoCW == CW_PANTALLA_3_3_LISTADO) {
        int charPorPagina = 63;
        bool hayMasPaginas = ((paginaListado3_3 + 1) * charPorPagina) < textoCompletoLeccion.length();
        if (atras) {
          if (paginaListado3_3 > 0) { paginaListado3_3--; opcionFinListado3_3 = 0; }
        } else if (giro > 0) {
          // Derecha: pagina siguiente y, en la ultima, alterna entre "corregir" y "rep."
          if (hayMasPaginas) paginaListado3_3++;
          else opcionFinListado3_3 = (opcionFinListado3_3 + 1) % 2;
        } else if (giro < 0) {
          if (!hayMasPaginas && opcionFinListado3_3 == 1) opcionFinListado3_3 = 0;
          else if (paginaListado3_3 > 0) paginaListado3_3--;
        } else if (pulsa) {
          if (hayMasPaginas) paginaListado3_3++;
          else if (opcionFinListado3_3 == 0) { estadoCW = CW_PANTALLA_CORREGIR_ERRORES; opcionPantallaCorregir = 0; }
          else iniciarPracticaCW();
        }

      } else if (estadoCW == CW_PANTALLA_CORREGIR_ERRORES) {
        if (atras) {
          estadoCW = CW_PANTALLA_3_3_LISTADO;
        } else if (giro != 0) {
          if (editandoValor) ajustarValor(numeroErrores, giro, 0, totalCaracteresLeccion);
          else opcionPantallaCorregir = (opcionPantallaCorregir + 3 + giro) % 3;
        } else if (pulsa) {
          if (opcionPantallaCorregir == 0) {
            editandoValor = !editandoValor;
          } else if (opcionPantallaCorregir == 1) {
            float pct = 0.0;
            if (totalCaracteresLeccion > 0) pct = ((float)(totalCaracteresLeccion - numeroErrores) / (float)totalCaracteresLeccion) * 100.0;
            registrarResultadoLeccionNVS(leccionCW, pct);
            ultimoPctKoch = pct;
            ultimoPctKochMs = millis();
            estadoCW = CW_PANTALLA_RESUMEN_FINAL;
            opcionResumen = 0;
          } else {
            iniciarPracticaCW();
          }
        }

      } else if (estadoCW == CW_PANTALLA_RESUMEN_FINAL) {
        // Foco: 0 volver, 1 repetir. La larga del eje es atajo de repetir
        if (atras) detenerPracticaCW();
        else if (ejeLarga) iniciarPracticaCW();
        else if (giro != 0) opcionResumen = (opcionResumen + 1) % 2;
        else if (pulsa) {
          if (opcionResumen == 0) detenerPracticaCW();
          else iniciarPracticaCW();
        }

      } else {
        // Practica en marcha o en pausa. Foco: 0 pausa/seguir, 1 salir. La larga del eje es atajo de salir
        if (atras || ejeLarga) {
          detenerPracticaCW();
        } else if (giro != 0) {
          opcionPractica = (opcionPractica + 1) % 2;
        } else if (pulsa) {
          if (opcionPractica == 1) detenerPracticaCW();
          else if (estadoCW == CW_PAUSADO) estadoCW = CW_GENERAR_GRUPO;
          else { estadoCW = CW_PAUSADO; motorCWDetener(); }
        }
      }

    } else if (enSubpantallaCW) {
      // Configuracion de KOCH. Foco: 0 wpm, 1 espaciado, 2 leccion, 3 caracteres, 4 grupos,
      // 7 estadisticas, 5 iniciar, 6 volver (en ese orden de recorrido; ESTAD. va junto a GRUPOS)
      static const uint8_t ORDEN_FOCO[] = { 0, 1, 2, 3, 4, 7, 5, 6 };
      const uint8_t N_FOCO = sizeof(ORDEN_FOCO);
      if (atras) {
        enSubpantallaCW = false;
      } else if (giro != 0) {
        if (editandoValor) {
          switch (opcionSubpantalla) {
            case 0: ajustarValor(wpmCW, giro, 15, 30); break;
            case 1: ajustarValor(espaciadoCW, giro * 10, 100, 200); break;
            case 2: ajustarValor(leccionCW, giro, 1, 40); break;
            case 3: ajustarValor(caracteresCW, giro, 1, 5); break;
            case 4: ajustarValor(gruposCW, giro * 10, 10, 50); break;
          }
        } else {
          uint8_t pos = 0;
          while (pos < N_FOCO && ORDEN_FOCO[pos] != opcionSubpantalla) pos++;
          opcionSubpantalla = ORDEN_FOCO[(pos + N_FOCO + giro) % N_FOCO];
        }
      } else if (pulsa) {
        if (opcionSubpantalla <= 4) editandoValor = !editandoValor;
        else if (opcionSubpantalla == 5) iniciarPracticaCW();
        else if (opcionSubpantalla == 7) {
          enEstadisticas3_5 = true;  // Al salir se vuelve aqui
          paginaStats3_5 = 0;
          confirmandoBorrado = false;
        }
        else enSubpantallaCW = false;
      }

    } else {
      if (atras) {
        entrarEnPantalla(PANTALLA_INICIO);
      } else if (giro != 0) {
        // Foco: [sig.] -> [KOCH] -> [KEYER] -> [ECHO] -> [ant.] -> [sig.] ...
        moverFocoAnillo(opcionPantalla3, OPCION_BARRA_CW, giro);
      } else if (pulsa) {
        if (opcionPantalla3 == 0) {
          enSubpantallaCW = true;
          opcionSubpantalla = 5;
        } else if (opcionPantalla3 == 1) {
          entrarEnKeyer();
        } else if (opcionPantalla3 == 2) {
          entrarEnEcho();
        } else {
          navegarDesdeBarra();
        }
      }
    }

  } else {
    // Pantallas sin opciones (Inicio, Sol): el foco solo alterna entre "ant." y "sig."
    if (atras) entrarEnPantalla(PANTALLA_INICIO);
    else if (giro != 0) opcionNavegacionP1234 = (opcionNavegacionP1234 == 0) ? 1 : 0;
    else if (pulsa) navegarDesdeBarra();
  }

  actualizarPantalla();
}

// ============================================================================
// SETUP Y BUCLE PRINCIPAL
// ============================================================================
void encenderPerifericos() {
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);   // Vext encendido
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(KEYER_PADDLE_DIT, INPUT_PULLUP);
  pinMode(KEYER_PADDLE_DAH, INPUT_PULLUP);

  ledcSetup(CANAL_PWM_AUDIO, FREC_AUDIO_HZ, RES_PWM_AUDIO);
  ledcAttachPin(SPEAKER_PWM_PIN, CANAL_PWM_AUDIO);
  ledcWrite(CANAL_PWM_AUDIO, 0);

  delay(100);
}

// ============================================================================
// DORMIR (DEEP SLEEP) Y APAGADO AUTOMATICO
// ============================================================================
// PRG largo, o apagadoAutoMin minutos sin tocar nada (se elige en CONF), pone la placa en deep sleep: OLED, Vext, LoRa, WiFi
// y LED apagados. Se despierta pulsando PRG (GPIO0, pin RTC: wakeup ext0 a nivel bajo) y arranca de
// nuevo como al encenderla (la configuracion esta en NVS).

void dormirChispas(const char* motivo) {
  Serial.printf("[SUENO] %s: a dormir. PRG para despertar\n", motivo);
  motorCWDetener();
  keyerActivo = false;
  if (estadoWifi != WIFI_APAGADO) apagarWifi();
  radio.sleep();

  display.dim(false);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(22, 16); display.print(F("CHISPAS DUERME"));
  display.setCursor(10, 34); display.print(F("PULSA PRG PARA"));
  display.setCursor(10, 44); display.print(F("DESPERTARLO"));
  display.display();
  delay(1500);
  display.ssd1306_command(SSD1306_DISPLAYOFF);

  // Salidas en reposo y retenidas durante el sueno (si no, quedan al aire): KEY en BAJO, Vext
  // apagada (ALTO: OLED sin alimentacion) y divisor de bateria desactivado
  digitalWrite(LED_PIN, LOW);
  ledcWrite(CANAL_PWM_AUDIO, 0);
  digitalWrite(KEY_OUT_PIN, LOW);
  digitalWrite(VEXT_CTRL, HIGH);
  digitalWrite(ADC_CTRL_PIN, LOW);
  gpio_hold_en((gpio_num_t)KEY_OUT_PIN);
  gpio_hold_en((gpio_num_t)VEXT_CTRL);
  gpio_hold_en((gpio_num_t)ADC_CTRL_PIN);
  gpio_deep_sleep_hold_en();

  // Despertar con PRG: GPIO0 a nivel bajo, con su pull-up activo durante el sueno
  rtc_gpio_pullup_en(GPIO_NUM_0);
  rtc_gpio_pulldown_dis(GPIO_NUM_0);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);

  Serial.flush();
  esp_deep_sleep_start();
}

/**
 * Apagado automatico tras apagadoAutoMin minutos sin tocar nada (0 = nunca), salvo en las pantallas CW y
 * LoRa (Koch, keyer, Echo, Duo, QSO: se puede estar usando sin tocar nada) o con el portal abierto.
 */
void procesarApagadoAuto() {
  if (apagadoAutoMin == 0) return;  // "nunca"
  if (millis() - ultimaActividadMs < apagadoAutoMin * 60000UL) return;
  // En CW y LoRa (y sus subpantallas) nunca: son las de practicar. Tampoco con el portal abierto
  if (pantallaActual == PANTALLA_CW || pantallaActual == PANTALLA_LORA || estadoWifi == WIFI_PORTAL) return;
  dormirChispas("SIN USO");
}

void setup() {
  // Al despertar del deep sleep siguen retenidos los pines que se fijaron al dormir: se liberan
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)KEY_OUT_PIN);
  gpio_hold_dis((gpio_num_t)VEXT_CTRL);
  gpio_hold_dis((gpio_num_t)ADC_CTRL_PIN);

  // Lo primero: la salida KEY en BAJO, para que la emisora no transmita mientras arranca la placa
  pinMode(KEY_OUT_PIN, OUTPUT);
  digitalWrite(KEY_OUT_PIN, LOW);

  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n[INIT] Chispas EA4ILN v" FW_VERSION "..."));
  cpuSegunWifi(false);  // Ahorro: 80 MHz mientras el WiFi este apagado
#ifdef SIN_ENCODER
  Serial.println(F("[INIT] MODO SIN ENCODER: PRG corto = mover, largo = ejecutar, muy largo = dormir"));
#endif
  
  encenderPerifericos();
  iniciarMotorCW();
  iniciarKeyer();
  iniciarEncoder();

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.printf("[INIT] MOTIVO DEL REINICIO: %d%s\n", (int)esp_reset_reason(),
                esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 ? " (despierta con PRG)" : "");

  // display.begin() no comprueba que la OLED responda (solo falla si no hay memoria).
  // Se comprueba el ACK en I2C tras un pulso de reset y, si no contesta, se corta Vext y se reintenta.
  Wire.begin(OLED_SDA, OLED_SCL);
  bool oledResponde = false;
  pinMode(OLED_RST, OUTPUT);
  for (uint8_t intento = 1; intento <= 3 && !oledResponde; intento++) {
    digitalWrite(OLED_RST, LOW);   // Mientras RST esta bajo el SSD1306 no contesta
    delay(10);
    digitalWrite(OLED_RST, HIGH);
    delay(10);
    Wire.beginTransmission(0x3C);
    oledResponde = (Wire.endTransmission() == 0);
    if (!oledResponde) {
      Serial.printf("[ERROR] OLED NO RESPONDE (intento %u)\n", intento);
      digitalWrite(VEXT_CTRL, HIGH);  // Vext apagado
      delay(200);
      digitalWrite(VEXT_CTRL, LOW);   // Vext encendido
      delay(200);
    }
  }
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("[ERROR] OLED: SIN MEMORIA PARA EL BUFFER"));
  }

  // Tras display.begin(), cerca de la mitad de los reinicios cortos la OLED quedaba negra:
  // contestaba por I2C y su estado decia "encendida", pero el panel no se iluminaba (bomba
  // de carga sin activar). Reenviar bomba de carga y encendido lo arregla (probado 10/10).
  const uint8_t cmdsOled[][2] = { {0x8D, 0x14}, {0xAF, 0x00} };  // Bomba de carga on, pantalla on
  for (uint8_t i = 0; i < 2; i++) {
    Wire.beginTransmission(0x3C);
    Wire.write((uint8_t)0x00);  // Byte de control: siguen comandos
    Wire.write(cmdsOled[i][0]);
    if (cmdsOled[i][0] == 0x8D) Wire.write(cmdsOled[i][1]);
    uint8_t err = Wire.endTransmission();
    if (err != 0) Serial.printf("[ERROR] OLED CMD 0x%02X: I2C %u\n", cmdsOled[i][0], err);
  }

  randomSeed(esp_random());  // GPIO0 no es ADC en el S3: la semilla anterior era casi fija
  seqTx = random(256);       // Si la otra placa se reinicia, que no repita el ultimo seq

  cargarDatosNVS();
  ajustarApagado(0);  // Si lo guardado no es un valor de la lista, lo corrige
  cargarEstacionNVS();
  setenv("TZ", ZONA_HORARIA, 1);  // Para que la hora manual del portal se interprete como local
  tzset();
  bateriaActual = leerBateria();
  actualizarPantalla();

  Serial.println(F("[LORA] INICIANDO SX1262..."));
  int state = radio.begin(LORA_FREC_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD,
                          LORA_POTENCIA_DBM, LORA_PREAMBULO, LORA_TCXO_V);
  if (state == RADIOLIB_ERR_NONE) {
    estadoLoraStr = String(LORA_FREC_MHZ, 1) + "MHZ SF" + String(LORA_SF);  // Cabe en 21 caracteres con "LORA: "
    Serial.println(F("[LORA] MODULO OK. VINCULANDO ISR EN DIO1..."));
    
    radio.setPacketReceivedAction(cbInterrupcionLora);
    radio.sleep();  // Ahorro: solo escucha en Duo y QSO (al entrar se llama a startReceive)
  } else {
    estadoLoraStr = "ERR (" + String(state) + ")";
    Serial.print(F("[LORA ERROR] CODE: "));
    Serial.println(state);
  }

  actualizarPantalla();
}

/**
 * Lee la bateria cada INTERVALO_BATERIA_MS y redibuja solo si algo ha cambiado.
 * Sustituye al refresco fijo cada 3 s. Con el motor CW en su tarea, puede hacerse
 * aunque suene CW.
 */
void procesarBateriaYPantalla() {
  static unsigned long ultimaLecturaBat = 0;
  if (millis() - ultimaLecturaBat >= INTERVALO_BATERIA_MS) {
    ultimaLecturaBat = millis();
    BateriaInfo nueva = leerBateria();
    bool cambia = nueva.estado != bateriaActual.estado ||
                  nueva.porcentaje != bateriaActual.porcentaje ||
                  (pantallaActual == PANTALLA_CONFIG && fabs(nueva.voltaje - bateriaActual.voltaje) >= 0.01);
    bateriaActual = nueva;
    if (cambia) pantallaPendiente = true;
  }

  // El reloj de la cabecera cambia cada minuto
  static int ultimoMinuto = -1;
  struct tm local;
  if (horaLocalValida(local) && local.tm_min != ultimoMinuto) {
    ultimoMinuto = local.tm_min;
    pantallaPendiente = true;
  }

  if (pantallaPendiente) {
    pantallaPendiente = false;
    actualizarPantalla();
  }
}

/**
 * Diagnostico por serie cada DIAG_INTERVALO_MS: memoria (libre, minimo historico y mayor bloque, para
 * vigilar la fragmentacion), pila libre de cada tarea, uso de la NVS y frecuencia de la CPU.
 */
#define DIAG_INTERVALO_MS 60000UL

void procesarDiagnostico() {
  static unsigned long ultimoMs = 0;
  if (millis() - ultimoMs < DIAG_INTERVALO_MS) return;
  ultimoMs = millis();
  nvs_stats_t nvs;
  bool hayNvs = nvs_get_stats(NULL, &nvs) == ESP_OK;
  Serial.printf("[DIAG] %lu min | heap libre %u, minimo %u, mayor bloque %u | pila libre: loop %u, motorCW %u, keyer %u | "
                "NVS %u/%u entradas | CPU %u MHz\n",
                millis() / 60000, ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
                uxTaskGetStackHighWaterMark(NULL), uxTaskGetStackHighWaterMark(tareaMotorCW),
                uxTaskGetStackHighWaterMark(tareaKeyer), hayNvs ? nvs.used_entries : 0,
                hayNvs ? nvs.total_entries : 0, getCpuFrequencyMhz());
}

/**
 * Ahorro: la OLED pasa al minimo de brillo tras BRILLO_TENUE_MS sin tocar nada (o con la cara dormida)
 * y vuelve al normal con cualquier giro, pulsacion o manipulacion con las palas. En CW y LoRa (y sus
 * subpantallas) no se atenua: son las que se miran mientras se practica.
 */
#define BRILLO_TENUE_MS 60000UL

void procesarBrillo() {
  static bool tenue = false;
  bool pantallaDePractica = (pantallaActual == PANTALLA_CW || pantallaActual == PANTALLA_LORA);
  bool debeSerTenue = (!pantallaDePractica && millis() - ultimaActividadMs >= BRILLO_TENUE_MS) ||
                      (caraActiva && expresionCara == EXP_DORMIDA);
  if (debeSerTenue != tenue) {
    tenue = debeSerTenue;
    display.dim(tenue);
  }
}

void loop() {
  procesarBoton();
  procesarTextoKeyer();
  procesarEcho();
  procesarCara();
  procesarMaquinaEstadosCW();
  procesarFinEmisionDuo();
  procesarRecepcionLoRa();
  procesarColaRxDuo();
  procesarWifi();
  procesarBateriaYPantalla();
  procesarDiagnostico();
  procesarBrillo();
  procesarApagadoAuto();

  // Ahorro: 1 ms de descanso por vuelta. Sin el, loop() tenia un nucleo al 100 %; con el, la CPU espera
  // en reposo. Las entradas siguen respondiendo (encoder por interrupcion; pulsadores cada ~1 ms) y el
  // CW no se ve afectado (motor y keyer van en sus propias tareas)
  vTaskDelay(1);
}