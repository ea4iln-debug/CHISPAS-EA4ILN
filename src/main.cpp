/**
 * @file main.cpp
 * @author EA4ILN
 * @brief Chispas: entrenador de CW portatil para la Heltec WiFi LoRa 32 V3 (ESP32-S3). Koch, Echo, keyer
 *        iambico, practica y QSO por LoRa, manipulacion del transceptor (KEY), decodificador CW, propagacion,
 *        spots POTA, sol y linea gris, y actualizacion por OTA desde las releases de GitHub.
 * @version 1.02b - Refuerzo de las letras con mas errores en Koch y Echo
 * @date 2026-10-11
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

// Version: se ve en Inicio, en CONF > STATUS y por serie. La etiqueta de la release de GitHub es "v" + esto
// (la actualizacion OTA las compara). Cambiarla en cada version que se cargue o se publique
#define FW_VERSION "1.02b"

// Depuracion: se activa en CONF > CONFIG > DEPURACION (NVS "depu", un bit por opcion; todo apagado por defecto)
#define DEPU_SERIE 0x01  // Ordenes por serie: 'p' vuelca la OLED (docs/captura_oled.py) y d/i/k/l/a simulan el encoder
#define DEPU_DIAG  0x02  // "[DIAG]" cada minuto: memoria, pilas, NVS y CPU
#define DEPU_DECOD 0x04  // "[DECOD]" cada 2 s con el decodificador en marcha (para ajustar RV2 en banco)
#define DEPU_CW    0x08  // "[CW ...]": retraso de cada transicion del motor CW (medido: 0 ms)

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RadioLib.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include "qrcode.h"  // Componente qrcode de ESP-IDF, incluido en el core (esp_qrcode_generate)
#include <nvs.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <driver/adc.h>  // ADC por DMA (decodificador CW de la pantalla TRX)
#include "certificados_github.h"  // Raices TLS para la actualizacion OTA

volatile uint8_t depuracion = 0;  // Bits DEPU_*; la lee tambien la tarea del motor CW
inline bool depuActiva(uint8_t bit) { return (depuracion & bit) != 0; }

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
#define SPEAKER_PWM_PIN  7  // Tono CW por PWM (J3-18): volumen, auriculares y buzzer (ver esquema, hoja 4)

#define FREC_AUDIO_HZ    850 // Tono CW por defecto; se elige en CONF > CONFIG > TONO (frecTonoHz)
#define TONO_MIN_HZ      400
#define TONO_MAX_HZ      1000
#define TONO_PASO_HZ     50
#define TONO_PRUEBA_MS   300 // Pitido de prueba al cambiar el tono en CONF
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

// Refuerzo de las letras con mas errores (Koch y Echo): 0 = no, 1 = suave (tope x2), 2 = fuerte (tope x3)
#define REFUERZO_NO     0
#define REFUERZO_SUAVE  1
#define REFUERZO_FUERTE 2
float pesoRefuerzo(uint8_t i, uint8_t refuerzo);  // Definida con las estadisticas por caracter

/**
 * Un caracter al azar de la leccion. La letra nueva (la ultima que entra) sale el PESO_LETRA_NUEVA_PCT % de las
 * veces; el resto se reparte entre las demas, por igual o, con refuerzo, segun su peso (mas errores, mas veces)
 */
char caracterKochAleatorio(uint8_t leccion, uint8_t refuerzo = REFUERZO_NO) {
  uint8_t n = caracteresDeLeccion(leccion);
  // Con pocas letras (4 o menos) el reparto uniforme ya le da ese % o mas a la nueva: entra en el sorteo comun
  bool nuevaAparte = n * PESO_LETRA_NUEVA_PCT > 100;
  if (nuevaAparte && random(100) < PESO_LETRA_NUEVA_PCT) return TABLA_KOCH[n - 1];
  uint8_t m = nuevaAparte ? n - 1 : n;  // Candidatas
  if (refuerzo == REFUERZO_NO) return TABLA_KOCH[random(0, m)];

  float pesos[TOTAL_CARACTERES_KOCH];
  float total = 0;
  for (uint8_t i = 0; i < m; i++) {
    pesos[i] = pesoRefuerzo(i, refuerzo);
    total += pesos[i];
  }
  float r = random(0, 100000) / 100000.0f * total;
  for (uint8_t i = 0; i < m; i++) {
    if (r < pesos[i]) return TABLA_KOCH[i];
    r -= pesos[i];
  }
  return TABLA_KOCH[m - 1];
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
#define TRAMA_TIPO_TEXTO_QSO   2  // Una palabra manipulada con las palas (QSO)
#define TRAMA_LONG_CABECERA    13
#define TRAMA_LONG_INDICATIVO  8
#define TRAMA_MAX_CARACTERES   32
#define TRAMA_LONG_MAX         (TRAMA_LONG_CABECERA + TRAMA_MAX_CARACTERES)
#define MORSE_ESPACIO_PALABRA  0x00
#define MORSE_NO_VALIDO        0x01  // Centinela sin elementos: caracter sin codigo Morse

// Datos de la estacion: en NVS (espacio NVS_ESTACION); se cambian en CONF > CONFIG o en el portal (webconf)
#define NVS_ESTACION "estacion"
#define INDICATIVO_POR_DEFECTO "EA4ILN"
#define LOCATOR_POR_DEFECTO    "IN80dk"  // Madrid (generico): cada estacion pone el suyo
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
#define PANTALLA_CW          2  // Entrenador: Koch, Echo y TX libre (keyer)
#define PANTALLA_LORA        3  // Practica Duo / QSO libre
#define PANTALLA_TRX         4  // Conexion con el transceptor: decodificar, operar (KEY), QSO y memorias
#define PANTALLA_PROPAGACION 5
#define PANTALLA_SPOTS       6
#define PANTALLA_SOL         7
#define PANTALLA_CONFIG      8
const uint8_t TOTAL_PANTALLAS = 8;
uint8_t opcionNavegacionP1234 = 1;  // 0 = "ant.", 1 = "sig."

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
StatsLeccion statsEchoLecc[40];  // Practicas del Echo por leccion (% de retos bien); NVS "eLec" (un bloque)

// Estadisticas por caracter: aciertos y fallos de cada caracter de TABLA_KOCH, por separado para el Koch
// (marcando los fallos al corregir) y para el Echo. En NVS, un bloque cada una ("carK" y "carE")
struct StatsCaracter {
  uint16_t aciertos;
  uint16_t fallos;
};
StatsCaracter statsCarKoch[TOTAL_CARACTERES_KOCH];
StatsCaracter statsCarEcho[TOTAL_CARACTERES_KOCH];
// Lecciones del Koch corregidas con el total de fallos (sin saber que letras): solo suman en TOTALES KOCH.
// En NVS "ktcar" y "ktok"
uint32_t kochTotCaracteres = 0, kochTotAciertos = 0;
#define MIN_INTENTOS_LETRA 5     // Intentos minimos para salir en "mas errores"
#define LINEAS_MAS_ERRORES 4

bool enSubpantallaCW = false;
bool enPractica3_2 = false;
bool enEstadisticas3_5 = false;
#define OPCION_BARRA_CW 3
uint8_t opcionPantalla3 = OPCION_BARRA_CW; // 0=KOCH (RX), 1=ECHO (TX), 2=TX LIBRE, 3=BARRA INFERIOR (ant/sig)

// Keyer iambico (pantalla CW > TX LIBRE). La configuracion se guarda en NVS al salir
uint8_t keyerWpm = 20;
bool keyerModoB = false;            // false = modo A
bool keyerPalasInvertidas = false;  // true = punta de la clavija = raya
bool enKeyer = false;               // Subpantalla del keyer abierta
uint8_t opcionKeyer = 0;            // Pantalla: 0 = borrar, 1 = menu
bool keyerEnAjustes = false;        // Lista de ajustes (menu): 0 wpm, 1 modo, 2 palas, 3 salir, 4 volver (pie)
uint8_t opcionAjustesKeyer = 0;
String textoKeyer = "";             // Lo descodificado de la manipulacion
volatile uint32_t keyerUltimoUsoMs = 0;  // Ultima vez que se pulso una pala o sono un elemento (QSO)

// Modo Echo (pantalla CW > ECHO (TX)): Chispas emite letras de su leccion Koch y hay que repetirlas con las
// palas. Como el Koch: ajustes propios (wpm, espaciado y leccion independientes), practica de echoRetos retos
// y resultado. ECHO_ESPERA = ajustes (sin practica)
enum EstadoEcho { ECHO_ESPERA, ECHO_REPRODUCIENDO, ECHO_ESCUCHANDO, ECHO_RESULTADO, ECHO_FIN };
bool enEcho = false;              // Echo abierto (ajustes, practica o estadisticas)
bool echoPracticando = false;
bool enEstadisticasEcho = false;
#define OPCION_ECHO_INICIAR 8
uint8_t opcionEcho = OPCION_ECHO_INICIAR;  // Ajustes: 0 wpm, 1 sp, 2 leccion, 3 letras, 4 ver, 5 retos, 6 refuerzo,
                                           // 7 estad., 8 iniciar, 9 volver
#define N_OPCIONES_ECHO 10
uint8_t opcionPracticaEcho = 0;   // Practica: 0 repetir, 1 salir. Resultado: 0 volver, 1 rep.
uint8_t echoWpm = 20;
uint16_t echoEspaciado = 200;
uint8_t echoLeccion = 4;
uint8_t echoLargo = 1;            // Letras por reto (1-5)
bool echoVerObjetivo = true;      // Ensenar la letra antes de manipularla (para aprender)
uint8_t echoRetos = 20;           // Retos por practica (10-50); cada intento cuenta, tambien al repetir tras un fallo
uint8_t echoRefuerzo = REFUERZO_SUAVE;  // Mas retos con las letras de mas errores (NVS "eref")
EstadoEcho estadoEcho = ECHO_ESPERA;
String echoObjetivo = "";
String echoRespuesta = "";
bool echoAcierto = false;
uint16_t echoAciertos = 0, echoIntentos = 0;
unsigned long echoFinResultadoMs = 0;
uint32_t echoTotalIntentos = 0, echoTotalAciertos = 0;  // Historico (ESTAD. del Echo)
uint16_t echoSesiones = 0;                               // Practicas terminadas
uint8_t paginaStatsEcho = 0;      // 0 mas errores, 1 totales, 2-15 lecciones, 16 borrado (como el Koch)
#define TOTAL_PAGINAS_STATS_ECHO 17

uint8_t wpmCW = 25;
uint16_t espaciadoCW = 200;
uint8_t leccionCW = 4;
uint8_t caracteresCW = 5;
uint8_t gruposCW = 20;
uint8_t refuerzoKoch = REFUERZO_SUAVE;  // Mas veces las letras de mas errores (NVS "kref")
uint8_t opcionSubpantalla = 7;  // Ajustes del Koch: 7 = iniciar

enum EstadoCW { 
  CW_IDLE, 
  CW_GENERAR_GRUPO, 
  CW_EMITIENDO_GRUPO,  // El grupo esta en el motor CW
  CW_PAUSA_ENTRE_GRUPOS, 
  CW_PAUSADO, 
  CW_PANTALLA_3_3_LISTADO,      
  CW_PANTALLA_CORREGIR_ERRORES,  // Correccion "x letra": se marca cada fallo
  CW_PANTALLA_FALLOS_TOTALES,    // Correccion "total": solo el numero de fallos
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
uint8_t opcionFinListado3_3 = 0;  // Ultima pagina del listado: 0 = x letra, 1 = total, 2 = rep.
// Correccion "total": numero de fallos (0..totalCaracteresLeccion). Foco: 0 = numero, 1 = rep., 2 = ok
uint16_t fallosTotales = 0;
uint8_t focoFallosTotales = 0;

// Correccion del Koch: se marcan los caracteres fallados en el listado. Foco: 0..largo-1 = un caracter,
// largo = "rep.", largo+1 = "ok" (largo = textoCompletoLeccion.length(); hasta 50 grupos de 5 con espacios)
#define MAX_TEXTO_LECCION 300
#define CARACTERES_POR_PAGINA_LISTADO 63  // 3 lineas de 21
bool marcasFallo[MAX_TEXTO_LECCION];
uint16_t focoCorregir = 0;
uint8_t paginaCorregir = 0;
uint8_t opcionPractica = 0;  // Practica en marcha: 0 = pausa/seguir, 1 = salir
uint8_t opcionResumen = 0;   // Resultado: 0 = volver, 1 = repetir

uint8_t paginaStats3_5 = 0;
// Paginas de ESTAD. del Koch: 0 = mas errores; 1 = totales; 2-15 = lecciones (40 / 3 por pagina); 16 = borrado
#define PAGINAS_STATS_3_5 14
#define PAG_STATS_PRIMERA_LECCION 2
#define PAG_STATS_BORRADO (PAG_STATS_PRIMERA_LECCION + PAGINAS_STATS_3_5)
#define TOTAL_PAGINAS_STATS (PAG_STATS_BORRADO + 1)
bool confirmandoBorrado = false;

unsigned long tiempoCWProximo = 0;

// Pantalla LoRa: menu (PRACTICA DUO y QSO LIBRE) y sus subpantallas
bool enPantalla4_Sub = false;  
uint8_t opcionPantalla4 = 2;   // 0=PRACTICA DUO, 1=QSO LIBRE, 2=BARRA INFERIOR (ant/sig)
uint8_t subpantalla4Activa = 0;

uint8_t opcionDuo4_1 = 0;       // Duo: 0 enviar, 1 ver (solo con un grupo oculto), 2 menu
// Ajustes ("menu") de Duo (0 wpm, 1 espaciado, 2 leccion, 3 salir, 4 volver) y de QSO (0 wpm, 1 ver, 2 salir,
// 3 volver). Las palas siguen funcionando en los del QSO
bool loraEnAjustes = false;
uint8_t opcionAjustesLora = 0;
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

// QSO por LoRa (subpantalla 2): lo manipulado con las palas se envia palabra a palabra (trama de texto) y lo
// recibido suena cuando hay un silencio de palabra sin manipular; si se manipula mientras suena, se corta y el
// resto queda solo como texto. Usa la cola de recepcion del Duo
#define OPCIONES_QSO 2
uint8_t opcionQSO = 1;            // 0 borrar, 1 menu (wpm, el de TX LIBRE; ver; salir)
bool qsoVerRx = true;             // false: lo recibido sale como "?" mientras suena (NVS "qver")
bool qsoSonando = false;          // Una palabra recibida esta en el motor CW
String qsoPalabraSonando = "";
String qsoTextoRx = "";           // Recibido (lo que ya se muestra)
String qsoTextoTx = "";           // Manipulado
String qsoPalabraTx = "";         // Palabra en curso, aun sin enviar
char qsoIndicativoRx[TRAMA_LONG_INDICATIVO + 1] = "";
#define QSO_TEXTO_MAX 200

// Pantalla TRX (conexion con el transceptor). Menu: DECODIFICAR, OPERAR, QSO y, en el pie, memorias
#define TRX_OPCION_MEMORIAS 3
#define TRX_OPCION_BARRA 4
uint8_t opcionTrx = TRX_OPCION_BARRA;  // 0 decodificar, 1 operar, 2 qso, 3 memorias (pie), 4 barra ant./sig.
enum SubTrx { TRX_MENU, TRX_DECOD, TRX_OPERAR, TRX_QSO, TRX_MEMORIAS, TRX_LISTA_ENVIO, TRX_EDITOR, TRX_AJUSTES };
SubTrx subTrx = TRX_MENU;
SubTrx subTrxOrigen = TRX_MENU;  // A donde vuelven la lista de envio y el editor
SubTrx trxAjustesOrigen = TRX_OPERAR;  // A donde vuelve la lista de ajustes ("menu" de OPERAR o QSO)
uint8_t opcionTrxSub = 0;        // Foco dentro de la subpantalla (OPERAR y QSO: 0 mem, 1 tune, 2 menu)
uint8_t opcionAjustesTrx = 0;    // Ajustes: 0 wpm, 1 tono local, 2 tune (s), 3 SU, 4 salir, 5 volver (pie)
bool decodEnAjustes = false;     // DECODIFICAR > menu: 0 tono, 1 salir, 2 volver (pie)
uint8_t opcionAjustesDecod = 0;
uint8_t trxWpm = 20;             // Keyer y memorias por KEY (NVS "twpm")
uint16_t trxTonoDecodHz = 0;     // Tono del decodificador: 0 = automatico, 400-1000 (NVS "tdec")
String trxTextoRx = "";          // Descodificado de la emisora
String trxTextoTx = "";          // Manipulado y memorias enviadas
char trxSu[TRAMA_LONG_INDICATIVO + 1] = "";  // Indicativo del corresponsal ({SU})
bool trxEnviandoMemoria = false;
// Tune: portadora continua por KEY para ajustar el acoplador o la potencia (OPERAR y QSO). La duracion se
// elige en su menu (NVS "ttune"); mientras dura, el corte de seguridad deja ese tiempo mas un margen
#define TRX_TUNE_MIN_S 1
#define TRX_TUNE_MAX_S 10
#define TRX_TUNE_MARGEN_MS 2000
uint8_t trxTuneSeg = 5;
bool trxSintonizando = false;
bool keyCortadaPorSeguridad = false;
#define KEY_MAX_CONTINUA_MS 10000
// Memorias: 6 de hasta 40 caracteres, con {MI} (indicativo de la estacion) y {SU} (corresponsal).
// En NVS "estacion" (m0..m5); sin guardar, las precargadas
#define TRX_NUM_MEMORIAS 6
#define TRX_MEMORIA_MAX 40
char trxMemorias[TRX_NUM_MEMORIAS][TRX_MEMORIA_MAX + 1];
const char* const TRX_MEMORIAS_PRECARGADAS[TRX_NUM_MEMORIAS] = {
  "CQ CQ CQ DE {MI} {MI} K",
  "{SU} DE {MI} {MI} K",
  "R TNX UR RST 599 599",
  "HW? {SU} DE {MI} K",
  "TNX FER QSO 73 GL",
  "{SU} DE {MI} 73 SK",
};
uint8_t trxMemoriaSel = 0;  // Foco en las listas de memorias (TRX_NUM_MEMORIAS = volver)
// Editor de textos (memorias y {SU}): rueda de caracteres con el encoder, o las palas
int8_t editorDestino = 0;   // 0..5 = memoria; -1 = indicativo del corresponsal; -2 = ID y -3 = locator (CONF)
#define EDITOR_ID_ESTACION -2
#define EDITOR_LOCATOR     -3
bool editorError = false;   // OK con un ID o locator no valido: se avisa y se sigue editando
String editorTexto = "";
uint8_t editorMax = TRX_MEMORIA_MAX;
uint8_t editorSel = 0;      // Posicion en la rueda

int rssiUltimo = 0;

// Depuracion (DEPU_CW): cuanto llega tarde cada transicion del motor CW respecto a lo programado; se
// informa por serie al final de cada grupo. Solo lo usa la tarea del motor CW
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

// ============================================================================
// CONTROL DE AUDIO Y LED MORSE
// ============================================================================
// Salida KEY a la emisora (GPIO2 -> PC817). Solo se habilita en OPERAR y QSO de la pantalla TRX: el resto
// (Koch, Echo, TX libre, LoRa) nunca manipula la emisora
volatile bool keyHabilitada = false;
volatile bool trxTonoLocal = true;           // Buzzer/auriculares mientras sale por KEY (NVS "ttono")
volatile unsigned long keyActivaDesdeMs = 0;  // 0 = KEY en reposo (para el corte de seguridad)
volatile unsigned long keyFinMs = 0;          // Ultima vez que se solto KEY (el decodificador no escucha justo despues)

void conmutarEmisionMorse(bool activar) {
  digitalWrite(LED_PIN, activar ? HIGH : LOW);
  bool key = activar && keyHabilitada;
  digitalWrite(KEY_OUT_PIN, key ? HIGH : LOW);
  if (key) {
    // Nunca por delante de millis() (con "| 1" podia ir 1 ms adelantada y la resta daba la vuelta)
    unsigned long ahora = millis();
    if (keyActivaDesdeMs == 0) keyActivaDesdeMs = (ahora != 0) ? ahora : 1;
  } else if (keyActivaDesdeMs != 0) {
    keyActivaDesdeMs = 0;
    keyFinMs = millis();
  }
  bool tono = activar && (!keyHabilitada || trxTonoLocal);
  ledcWrite(CANAL_PWM_AUDIO, tono ? 128 : 0);
}

uint16_t frecTonoHz = FREC_AUDIO_HZ;  // Tono CW (buzzer y auriculares); se elige en CONF y se guarda en NVS

// Pitido de prueba al elegir el tono en CONF (alli no emite el motor CW). Lo apaga procesarTonoPrueba()
unsigned long tonoPruebaFinMs = 0;  // 0 = sin pitido

void aplicarFrecTono(bool pitidoPrueba) {
  ledcChangeFrequency(CANAL_PWM_AUDIO, frecTonoHz, RES_PWM_AUDIO);
  if (pitidoPrueba) {
    ledcWrite(CANAL_PWM_AUDIO, 128);
    tonoPruebaFinMs = millis() + TONO_PRUEBA_MS;
    if (tonoPruebaFinMs == 0) tonoPruebaFinMs = 1;
  }
}

void procesarTonoPrueba() {
  if (tonoPruebaFinMs != 0 && (long)(millis() - tonoPruebaFinMs) >= 0) {
    ledcWrite(CANAL_PWM_AUDIO, 0);
    tonoPruebaFinMs = 0;
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
  uint16_t portadoraMs;  // Distinto de 0: portadora continua (tune) en vez del texto
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
      if (depuActiva(DEPU_CW)) registrarRetrasoCW(ahora - objetivo);
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
  if (p.portadoraMs != 0) {  // Tune: tareaEmisionCW apaga al volver, tambien si se cancela
    conmutarEmisionMorse(true);
    esperarMotorCW(xTaskGetTickCount() + pdMS_TO_TICKS(p.portadoraMs), p.id);
    return;
  }
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
    if (depuActiva(DEPU_CW)) informarRetrasoCW(p.origen);
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
  p.portadoraMs = 0;
  p.origen = origen;
  if (xQueueSend(colaMotorCW, &p, 0) == pdTRUE) motorCWPedidas = p.id;
}

/**
 * Portadora continua de ms milisegundos (tune), cortable con motorCWDetener(). Vuelve enseguida.
 */
void motorCWPortadora(uint16_t ms, const char* origen) {
  motorCWDetener();

  PeticionCW p;
  p.id = motorCWPedidas + 1;
  p.texto[0] = '\0';
  p.wpm = 1;
  p.espaciado = 100;
  p.portadoraMs = ms;
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
void guardarEchoNVS();                 // Definida en la seccion NVS
void qsoCaracterManipulado(char c);    // Definida con la radio LoRa
void trxCaracterManipulado(char c);    // Definida con la pantalla TRX
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
    // El Echo y la pantalla TRX tienen su propio wpm
    uint8_t wpm = enEcho ? echoWpm : (pantallaActual == PANTALLA_TRX ? trxWpm : keyerWpm);
    TickType_t dit = pdMS_TO_TICKS(1200 / wpm);
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
          // Silencio desde el ultimo elemento: 2 puntos cierran el caracter y 6, la palabra (con 5, una pausa
          // algo larga entre letras partia la palabra; en QSO se enviaba letra a letra)
          TickType_t silencio = ahora - finUltimoElemento;
          if (largo > 0 && silencio >= 2 * dit) {
            patron[min(largo, (uint8_t)8)] = '\0';
            char c = (largo <= 7) ? decodificarPatron(patron) : '*';
            xQueueSend(colaKeyerTexto, &c, 0);
            largo = 0;
            espacioPendiente = true;
          } else if (espacioPendiente && silencio >= 6 * dit) {
            char c = ' ';
            xQueueSend(colaKeyerTexto, &c, 0);
            espacioPendiente = false;
          }
        }
        break;
    }

    if (pDit || pDah || estado != K_ESPERA || siguiente != 0) keyerUltimoUsoMs = millis();

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
  keyerEnAjustes = false;
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
// ESTADISTICAS POR CARACTER (KOCH Y ECHO)
// ============================================================================
#define NVS_ENTRENADOR "chispas"  // Entrenador (Koch, keyer, Echo y estadisticas)

int indiceKoch(char c) {
  for (uint8_t i = 0; i < TOTAL_CARACTERES_KOCH; i++) {
    if (TABLA_KOCH[i] == c) return i;
  }
  return -1;
}

// Olvido gradual: al pasar de STATS_OLVIDO_INTENTOS, aciertos y fallos de esa letra se dividen a la mitad. Asi
// pesa mas lo reciente (en MAS ERRORES y en el refuerzo) y una letra ya dominada deja de reforzarse
#define STATS_OLVIDO_INTENTOS 100

void registrarCaracter(StatsCaracter* tabla, char c, bool acierto) {
  int i = indiceKoch(c);
  if (i < 0) return;
  uint16_t& cuenta = acierto ? tabla[i].aciertos : tabla[i].fallos;
  if (cuenta < UINT16_MAX) cuenta++;
  if ((uint32_t)tabla[i].aciertos + tabla[i].fallos > STATS_OLVIDO_INTENTOS) {
    tabla[i].aciertos = (tabla[i].aciertos + 1) / 2;
    tabla[i].fallos = (tabla[i].fallos + 1) / 2;
  }
}

/**
 * Peso de un caracter en el sorteo con refuerzo: 1 + tope x tasa de fallos, sin pasar del tope (x2 suave, x3
 * fuerte). La tasa es la suma de Koch y Echo, suavizada ((fallos + 1) / (intentos + 2)) para que un fallo suelto
 * no dispare una letra con pocos datos. Con menos de MIN_INTENTOS_LETRA intentos, peso 1 (como sin refuerzo)
 */
float pesoRefuerzo(uint8_t i, uint8_t refuerzo) {
  uint32_t fallos = (uint32_t)statsCarKoch[i].fallos + statsCarEcho[i].fallos;
  uint32_t intentos = fallos + statsCarKoch[i].aciertos + statsCarEcho[i].aciertos;
  if (refuerzo == REFUERZO_NO || intentos < MIN_INTENTOS_LETRA) return 1.0f;
  float tope = (refuerzo == REFUERZO_FUERTE) ? 3.0f : 2.0f;
  float tasa = (fallos + 1.0f) / (intentos + 2.0f);
  return min(tope, 1.0f + tope * tasa);
}

// Hay datos para reforzar en la leccion: alguna de sus letras con MIN_INTENTOS_LETRA intentos o mas
bool hayDatosRefuerzo(uint8_t leccion) {
  uint8_t n = caracteresDeLeccion(leccion);
  for (uint8_t i = 0; i < n; i++) {
    uint32_t intentos = (uint32_t)statsCarKoch[i].aciertos + statsCarKoch[i].fallos +
                        statsCarEcho[i].aciertos + statsCarEcho[i].fallos;
    if (intentos >= MIN_INTENTOS_LETRA) return true;
  }
  return false;
}

// Por serie, al empezar una practica: las letras que salen mas de lo normal y su peso
void informarRefuerzo(const char* origen, uint8_t leccion, uint8_t refuerzo) {
  if (refuerzo == REFUERZO_NO) return;
  Serial.printf("[%s] REFUERZO %s:", origen, refuerzo == REFUERZO_FUERTE ? "FUERTE" : "SUAVE");
  uint8_t n = caracteresDeLeccion(leccion);
  bool alguna = false;
  for (uint8_t i = 0; i < n; i++) {
    float p = pesoRefuerzo(i, refuerzo);
    if (p > 1.05f) {
      Serial.printf(" %c x%.1f", TABLA_KOCH[i], p);
      alguna = true;
    }
  }
  Serial.println(alguna ? "" : " SIN DATOS (sorteo normal)");
}

void guardarStatsKochNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putBytes("carK", statsCarKoch, sizeof(statsCarKoch));
  prefs.end();
}

void guardarTotalesKochNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUInt("ktcar", kochTotCaracteres);
  prefs.putUInt("ktok", kochTotAciertos);
  prefs.end();
}

void guardarStatsEchoNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putBytes("carE", statsCarEcho, sizeof(statsCarEcho));
  prefs.putUInt("eint", echoTotalIntentos);
  prefs.putUInt("eok", echoTotalAciertos);
  prefs.putUShort("eses", echoSesiones);
  prefs.end();
}

// Anota un resultado en las estadisticas de una leccion (veces, % maximo y minimo)
void anotarResultadoLeccion(StatsLeccion& s, float pct) {
  uint8_t aciertoInt = (uint8_t)constrain(pct, 0, 100);
  s.vecesCompletada++;
  if (aciertoInt > s.maxAcierto) s.maxAcierto = aciertoInt;
  if (aciertoInt < s.minAcierto || s.vecesCompletada == 1) s.minAcierto = aciertoInt;
}

// Fin de una practica del Echo: el % de retos bien cuenta para su leccion
void registrarLeccionEchoNVS(uint8_t lecc, float pct) {
  if (lecc < 1 || lecc > 40) return;
  anotarResultadoLeccion(statsEchoLecc[lecc - 1], pct);
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putBytes("eLec", statsEchoLecc, sizeof(statsEchoLecc));
  prefs.end();
}

// ============================================================================
// MODO ECHO: CHISPAS EMITE Y HAY QUE REPETIRLO CON LAS PALAS
// ============================================================================
#define ECHO_PAUSA_ACIERTO_MS 1200
#define ECHO_PAUSA_FALLO_MS   3000

/**
 * Emite un reto. Con repetirMismo vuelve a emitir el actual (tras un fallo o con "repetir").
 * Las letras salen de la leccion del Echo, con la letra nueva al 25 %.
 */
void echoNuevoReto(bool repetirMismo) {
  if (!repetirMismo || echoObjetivo.length() == 0) {
    echoObjetivo = "";
    for (uint8_t i = 0; i < echoLargo; i++) echoObjetivo += caracterKochAleatorio(echoLeccion, echoRefuerzo);
  }
  echoRespuesta = "";
  keyerPausar();
  motorCWEmitir(echoObjetivo.c_str(), echoWpm, echoEspaciado, "ECHO");
  estadoEcho = ECHO_REPRODUCIENDO;
  pantallaPendiente = true;
}

// Abre los ajustes del Echo (la practica empieza con "iniciar")
void entrarEnEcho() {
  motorCWDetener();
  enEcho = true;
  echoPracticando = false;
  enEstadisticasEcho = false;
  opcionEcho = OPCION_ECHO_INICIAR;
  estadoEcho = ECHO_ESPERA;
}

void iniciarPracticaEcho() {
  guardarEchoNVS();
  informarRefuerzo("ECHO", echoLeccion, echoRefuerzo);
  echoPracticando = true;
  opcionPracticaEcho = 0;
  echoAciertos = 0;
  echoIntentos = 0;
  echoObjetivo = "";
  echoNuevoReto(false);
}

// De la practica (o del resultado) a los ajustes
void salirPracticaEcho() {
  keyerPausar();
  motorCWDetener();
  echoPracticando = false;
  estadoEcho = ECHO_ESPERA;
}

void salirDeEcho() {
  salirPracticaEcho();
  enEcho = false;
  editandoValor = false;
  guardarEchoNVS();
}

void echoEvaluar() {
  echoIntentos++;
  echoAcierto = (echoRespuesta == echoObjetivo);
  if (echoAcierto) echoAciertos++;
  echoTotalIntentos++;
  if (echoAcierto) echoTotalAciertos++;
  // Por caracter, posicion a posicion (lo que falte en una respuesta corta cuenta como fallo)
  for (uint8_t i = 0; i < echoObjetivo.length(); i++) {
    registrarCaracter(statsCarEcho, echoObjetivo[i], i < echoRespuesta.length() && echoRespuesta[i] == echoObjetivo[i]);
  }
  guardarStatsEchoNVS();
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
  if (!enEcho || !echoPracticando) return;
  if (estadoEcho == ECHO_REPRODUCIENDO && !motorCWOcupado()) {
    keyerReanudar();  // Turno del usuario
    estadoEcho = ECHO_ESCUCHANDO;
    pantallaPendiente = true;
  } else if (estadoEcho == ECHO_RESULTADO && (long)(millis() - echoFinResultadoMs) >= 0) {
    if (echoIntentos >= echoRetos) {
      // Fin de la practica: resultado como en el Koch
      estadoEcho = ECHO_FIN;
      opcionPracticaEcho = 0;
      echoSesiones++;
      guardarStatsEchoNVS();
      registrarLeccionEchoNVS(echoLeccion, echoIntentos > 0 ? echoAciertos * 100.0f / echoIntentos : 0);
      Serial.printf("[ECHO] FIN: %u/%u\n", echoAciertos, echoIntentos);
      pantallaPendiente = true;
    } else {
      echoNuevoReto(!echoAcierto);  // Tras un fallo se repite el mismo
    }
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
    if (pantallaActual == PANTALLA_LORA && enPantalla4_Sub && subpantalla4Activa == 2) {
      qsoCaracterManipulado(c);
      continue;
    }
    if (pantallaActual == PANTALLA_TRX) {
      trxCaracterManipulado(c);
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
extern uint8_t apagadoAutoMin;  // Minutos del apagado automatico; definida con las opciones de CONF
extern uint8_t aceleracionGiro;  // Aceleracion del encoder; definida con las opciones de CONF

void cargarDatosNVS() {
  prefs.begin(NVS_ENTRENADOR, false);  // Lectura y escritura: en el primer arranque crea el espacio (sin errores)
  wpmCW = prefs.getUChar("wpm", 25);
  espaciadoCW = prefs.getUShort("sp", 200);
  leccionCW = prefs.getUChar("leccion", 4);
  caracteresCW = prefs.getUChar("caract", 5);
  gruposCW = prefs.getUChar("grupos", 20);
  keyerWpm = constrain(prefs.getUChar("kwpm", 20), 10, 40);  // KEYER_WPM_MIN/MAX
  keyerModoB = prefs.getBool("kmodob", false);
  keyerPalasInvertidas = prefs.getBool("kinv", false);
  qsoVerRx = prefs.getBool("qver", true);
  trxWpm = constrain(prefs.getUChar("twpm", 20), KEYER_WPM_MIN, KEYER_WPM_MAX);
  trxTonoLocal = prefs.getBool("ttono", true);
  trxTuneSeg = constrain(prefs.getUChar("ttune", 5), TRX_TUNE_MIN_S, TRX_TUNE_MAX_S);
  trxTonoDecodHz = prefs.getUShort("tdec", 0);
  if (trxTonoDecodHz != 0) trxTonoDecodHz = constrain(trxTonoDecodHz, TONO_MIN_HZ, TONO_MAX_HZ);
  echoWpm = constrain(prefs.getUChar("ewpm", 20), KEYER_WPM_MIN, KEYER_WPM_MAX);
  echoEspaciado = constrain(prefs.getUShort("esp", 200), 100, 200);
  echoLeccion = constrain(prefs.getUChar("elecc", 4), 1, 40);
  echoLargo = constrain(prefs.getUChar("elargo", 1), 1, 5);
  echoVerObjetivo = prefs.getBool("ever", true);
  echoRetos = constrain(prefs.getUChar("eretos", 20), 10, 50);
  echoRefuerzo = min(prefs.getUChar("eref", REFUERZO_SUAVE), (uint8_t)REFUERZO_FUERTE);
  refuerzoKoch = min(prefs.getUChar("kref", REFUERZO_SUAVE), (uint8_t)REFUERZO_FUERTE);
  echoTotalIntentos = prefs.getUInt("eint", 0);
  echoTotalAciertos = prefs.getUInt("eok", 0);
  echoSesiones = prefs.getUShort("eses", 0);
  apagadoAutoMin = prefs.getUChar("apag", 30);  // ajustarApagado(0) lo lleva a un valor de la lista si no lo es
  aceleracionGiro = prefs.getUChar("acel", 5);   // ACELERACION_MAX
  if (aceleracionGiro == 1 || aceleracionGiro > 5) aceleracionGiro = 5;
  frecTonoHz = constrain(prefs.getUShort("tono", FREC_AUDIO_HZ), TONO_MIN_HZ, TONO_MAX_HZ);
  depuracion = prefs.getUChar("depu", 0) & (DEPU_SERIE | DEPU_DIAG | DEPU_DECOD | DEPU_CW);

  for (int i = 0; i < 40; i++) {
    String kComp = "c_" + String(i);
    String kMax = "mx_" + String(i);
    String kMin = "mn_" + String(i);

    statsKoch[i].vecesCompletada = prefs.getUShort(kComp.c_str(), 0);
    statsKoch[i].maxAcierto = prefs.getUChar(kMax.c_str(), 0);
    statsKoch[i].minAcierto = prefs.getUChar(kMin.c_str(), 100);
  }
  memset(statsCarKoch, 0, sizeof(statsCarKoch));
  memset(statsCarEcho, 0, sizeof(statsCarEcho));
  if (prefs.isKey("carK") && prefs.getBytesLength("carK") == sizeof(statsCarKoch)) {
    prefs.getBytes("carK", statsCarKoch, sizeof(statsCarKoch));
  }
  if (prefs.isKey("carE") && prefs.getBytesLength("carE") == sizeof(statsCarEcho)) {
    prefs.getBytes("carE", statsCarEcho, sizeof(statsCarEcho));
  }
  kochTotCaracteres = prefs.getUInt("ktcar", 0);
  kochTotAciertos = prefs.getUInt("ktok", 0);
  for (int i = 0; i < 40; i++) statsEchoLecc[i] = { 0, 0, 100 };
  if (prefs.isKey("eLec") && prefs.getBytesLength("eLec") == sizeof(statsEchoLecc)) {
    prefs.getBytes("eLec", statsEchoLecc, sizeof(statsEchoLecc));
  }
  prefs.end();
}

void guardarUltimaConfiguracionNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("kref", refuerzoKoch);
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

void guardarAceleracionNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("acel", aceleracionGiro);
  prefs.end();
}

void guardarTonoNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUShort("tono", frecTonoHz);
  prefs.end();
}

void guardarDepuracionNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("depu", depuracion);
  prefs.end();
}

void guardarKeyerNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("kwpm", keyerWpm);
  prefs.putBool("kmodob", keyerModoB);
  prefs.putBool("kinv", keyerPalasInvertidas);
  prefs.putBool("qver", qsoVerRx);
  prefs.end();
}

void guardarTrxNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("twpm", trxWpm);
  prefs.putBool("ttono", trxTonoLocal);
  prefs.putUChar("ttune", trxTuneSeg);
  prefs.putUShort("tdec", trxTonoDecodHz);
  prefs.end();
}

void guardarEchoNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("ewpm", echoWpm);
  prefs.putUShort("esp", echoEspaciado);
  prefs.putUChar("elecc", echoLeccion);
  prefs.putUChar("elargo", echoLargo);
  prefs.putBool("ever", echoVerObjetivo);
  prefs.putUChar("eretos", echoRetos);
  prefs.putUChar("eref", echoRefuerzo);
  prefs.end();
}

void registrarResultadoLeccionNVS(uint8_t lecc, float pct) {
  if (lecc < 1 || lecc > 40) return;
  uint8_t idx = lecc - 1;
  anotarResultadoLeccion(statsKoch[idx], pct);

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

// Borra solo las estadisticas del Koch (lecciones y por caracter); las del Echo, la configuracion del
// entrenador y los datos de la estacion se conservan
void borrarEstadisticasKochNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  for (int i = 0; i < 40; i++) {
    prefs.remove(("c_" + String(i)).c_str());
    prefs.remove(("mx_" + String(i)).c_str());
    prefs.remove(("mn_" + String(i)).c_str());
  }
  prefs.remove("carK");
  prefs.remove("ktcar");
  prefs.remove("ktok");
  prefs.end();
  memset(statsCarKoch, 0, sizeof(statsCarKoch));
  kochTotCaracteres = kochTotAciertos = 0;

  for (int i = 0; i < 40; i++) {
    statsKoch[i].vecesCompletada = 0;
    statsKoch[i].maxAcierto = 0;
    statsKoch[i].minAcierto = 100;
  }
}

// Borra solo las estadisticas del Echo (por caracter y totales)
void borrarEstadisticasEchoNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.remove("carE");
  prefs.remove("eint");
  prefs.remove("eok");
  prefs.remove("eses");
  prefs.remove("eLec");
  prefs.end();
  memset(statsCarEcho, 0, sizeof(statsCarEcho));
  for (int i = 0; i < 40; i++) statsEchoLecc[i] = { 0, 0, 100 };
  echoTotalIntentos = echoTotalAciertos = 0;
  echoSesiones = 0;
}

void cargarEstacionNVS() {
  prefs.begin(NVS_ESTACION, false);  // Como en cargarDatosNVS(): crea el espacio en el primer arranque
  // Con isKey: getString() de una clave que aun no existe imprime un error por serie
  strlcpy(indicativoEstacion, prefs.isKey("call") ? prefs.getString("call").c_str() : INDICATIVO_POR_DEFECTO,
          sizeof(indicativoEstacion));
  strlcpy(locatorEstacion, prefs.isKey("loc") ? prefs.getString("loc").c_str() : LOCATOR_POR_DEFECTO,
          sizeof(locatorEstacion));
  horaUtcCabecera = prefs.getBool("utc", false);
  // Memorias de la pantalla TRX: sin clave guardada, la precargada (una memoria borrada se guarda vacia)
  for (uint8_t i = 0; i < TRX_NUM_MEMORIAS; i++) {
    String clave = "m" + String(i);
    String m = prefs.isKey(clave.c_str()) ? prefs.getString(clave.c_str(), "") : String(TRX_MEMORIAS_PRECARGADAS[i]);
    strlcpy(trxMemorias[i], m.c_str(), sizeof(trxMemorias[i]));
  }
  prefs.end();
}

void guardarMemoriaNVS(uint8_t i) {
  prefs.begin(NVS_ESTACION, false);
  prefs.putString(("m" + String(i)).c_str(), trxMemorias[i]);
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
// DIBUJO EN LA OLED: CABECERA, PIES Y PIEZAS COMUNES (cabecera en mayusculas, pies en minusculas)
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

  } else if ((pantallaActual == PANTALLA_CW && (enPractica3_2 || (enEcho && echoPracticando))) ||
             (pantallaActual == PANTALLA_LORA && subpantalla4Activa == 1)) {
    bool deEcho = pantallaActual == PANTALLA_CW && enEcho;
    display.setCursor(0, 0);
    display.print(deEcho ? echoWpm : wpmCW);
    display.print(F("WPM SP:"));
    display.print(deEcho ? echoEspaciado : espaciadoCW);
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
      case PANTALLA_TRX:         nombre = "TRX";   break;
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

// Opcion de un menu principal ("< KOCH (RX) >") centrada; el ">" del foco va a su izquierda, fuera del centrado
void dibujarOpcionCentrada(bool foco, const char* texto, int16_t y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(texto, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (128 - w) / 2;
  if (foco) {
    display.setCursor(x - 12, y);
    display.print('>');
  }
  display.setCursor(x, y);
  display.print(texto);
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

// ----------------------------------------------------------------------------
// Piezas comunes de los menus (estilo de CONF)
// ----------------------------------------------------------------------------
// Titulo de un menu, en negativo y centrado (CW, LoRa, TRX, PROP, POTA, SOL, CONF)
void dibujarTituloMenu(const char* titulo) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(titulo, 0, 0, &x1, &y1, &w, &h);
  int x = (128 - w) / 2;
  display.fillRect(x - 2, 11, w + 4, h + 4, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  display.setCursor(x, 13);
  display.print(titulo);
  display.setTextColor(SSD1306_WHITE);
}

// Pie de las paginas que se pasan girando: "1/2 INDICES  >volver" (pulsar vuelve)
void dibujarPiePaginas(uint8_t pagina, uint8_t total, const String& nombre) {
  dibujarPieCentrado(String(pagina + 1) + "/" + String(total) + " " + nombre + "  >volver");
}

// Pie con dos opciones: "> iniciar   > volver"
void dibujarPieDos(bool foco1, const char* op1, bool foco2, const char* op2) {
  dibujarPieCentrado((foco1 ? "> " : "  ") + String(op1) + "   " + (foco2 ? "> " : "  ") + String(op2));
}

/**
 * Lista de opciones como CONF > CONFIG: 4 a la vista, nombre a la izquierda, valor a la derecha y barra de
 * desplazamiento. foco >= n = el foco esta en el pie (la lista se queda al final). editables: bit i = la
 * opcion i se edita girando (su valor sale en video inverso mientras se edita). El pie lo pone quien llama
 */
void dibujarListaOpciones(const char* const* nombres, uint8_t n, uint8_t foco, String (*valor)(uint8_t),
                          uint32_t editables) {
  uint8_t primera = 0;
  if (n > 4) primera = foco < n ? max(0, (int)foco - 3) : n - 4;
  for (uint8_t k = 0; k < 4 && primera + k < n; k++) {
    uint8_t i = primera + k;
    int16_t y = 12 + k * 10;
    display.setCursor(0, y);
    display.print(foco == i ? '>' : ' ');
    display.print(nombres[i]);
    // El valor acaba en x = 123 para dejar sitio a la barra de desplazamiento
    String v = valor(i);
    display.setCursor(124 - v.length() * 6, y);
    if (editables & (1UL << i)) imprimirValor(foco == i, v);
    else display.print(v);
  }
  if (n > 4) {
    // Barra de desplazamiento: 2 px a la derecha, con el tramo de lista que se ve
    const int16_t yBarra = 12, altoBarra = 38;
    int16_t altoTramo = altoBarra * 4 / n;
    int16_t yTramo = yBarra + (altoBarra - altoTramo) * primera / (n - 4);
    display.drawFastVLine(127, yBarra, altoBarra, SSD1306_WHITE);
    display.fillRect(125, yTramo, 3, altoTramo, SSD1306_WHITE);
  }
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

  // En el centro, opcional: por ejemplo "memorias" en el menu TRX
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

/**
 * Arma una trama v2 del tipo indicado (grupo de Duo o palabra de QSO) y la transmite.
 * Devuelve false si el texto no tiene ningun caracter Morse valido (no se transmite nada).
 */
bool transmitirTextoLoRa(uint8_t tipo, uint8_t wpm, const String& texto) {
  uint8_t trama[TRAMA_LONG_MAX];
  trama[0] = 'M';
  trama[1] = 'M';
  trama[2] = (TRAMA_VERSION << 4) | tipo;
  trama[4] = wpm;
  memset(&trama[5], 0, TRAMA_LONG_INDICATIVO);
  memcpy(&trama[5], indicativoEstacion, min(strlen(indicativoEstacion), (size_t)TRAMA_LONG_INDICATIVO));

  size_t longitud = TRAMA_LONG_CABECERA;
  for (size_t i = 0; i < texto.length() && longitud < TRAMA_LONG_MAX; i++) {
    uint8_t b = codificarCaracterMorse(texto.charAt(i));
    if (b != MORSE_NO_VALIDO) trama[longitud++] = b;
  }
  if (longitud == TRAMA_LONG_CABECERA) return false;  // Nada que enviar

  prepararCanalParaTransmitir();
  trama[3] = seqTx++;

  Serial.printf("[LORA TX] tipo %u seq %u, %u bytes: ", tipo, trama[3], (unsigned)longitud);
  Serial.println(texto);

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
  return true;
}

void transmitirGrupoDuoLoRa(String grupo) {
  grupoDuoTransmitido = grupo;
  if (!transmitirTextoLoRa(TRAMA_TIPO_GRUPO_DUO, wpmCW, grupo)) return;
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
  if (subpantalla4Activa != 1 || emitiendoMorseDuo || colaRxNum == 0) return;  // En QSO la vacia procesarQSO()

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

// ============================================================================
// QSO POR LORA (PALAS)
// ============================================================================
void loraEscuchar();  // Definida mas abajo

bool enQSO() {
  return pantallaActual == PANTALLA_LORA && enPantalla4_Sub && subpantalla4Activa == 2;
}

// Anade una palabra al texto (con espacio delante) y recorta por el principio si se pasa del maximo
void qsoAnadir(String& texto, const String& palabra) {
  if (palabra.length() == 0) return;
  if (texto.length() > 0 && !texto.endsWith(" ")) texto += ' ';
  texto += palabra;
  if (texto.length() > QSO_TEXTO_MAX) texto.remove(0, texto.length() - QSO_TEXTO_MAX);
}

void qsoEnviarPalabra() {
  if (qsoPalabraTx.length() == 0) return;
  transmitirTextoLoRa(TRAMA_TIPO_TEXTO_QSO, keyerWpm, qsoPalabraTx);
  qsoPalabraTx = "";
}

// Un caracter descodificado por el keyer en el QSO: al llegar el espacio entre palabras se envia la palabra
void qsoCaracterManipulado(char c) {
  if (c == ' ') {
    qsoEnviarPalabra();
    if (qsoTextoTx.length() > 0 && !qsoTextoTx.endsWith(" ")) qsoTextoTx += ' ';
  } else {
    qsoPalabraTx += c;
    qsoTextoTx += c;
    if (qsoPalabraTx.length() >= TRAMA_MAX_CARACTERES) qsoEnviarPalabra();  // No cabe mas en una trama
  }
  if (qsoTextoTx.length() > QSO_TEXTO_MAX) qsoTextoTx.remove(0, qsoTextoTx.length() - QSO_TEXTO_MAX);
  pantallaPendiente = true;
}

void entrarEnQSO() {
  enPantalla4_Sub = true;
  subpantalla4Activa = 2;
  modoRadioRxActivo = true;
  opcionQSO = 1;
  loraEnAjustes = false;
  qsoSonando = false;
  qsoPalabraSonando = qsoTextoRx = qsoTextoTx = qsoPalabraTx = "";
  qsoIndicativoRx[0] = '\0';
  colaRxIni = colaRxNum = 0;
  loraEscuchar();  // La radio duerme fuera de Duo y QSO
  keyerReanudar();  // Las palas funcionan con cualquier foco
}

/**
 * Reproduce lo recibido en el QSO: una palabra cada vez, cuando se lleva un silencio de palabra sin
 * manipular (el keyer queda en pausa mientras suena). Si se toca una pala mientras suena, se corta y lo
 * que quedaba pasa al texto sin sonar.
 */
void procesarQSO() {
  if (!enQSO()) return;

  if (qsoSonando) {
    bool palaPulsada = digitalRead(KEYER_PADDLE_DIT) == LOW || digitalRead(KEYER_PADDLE_DAH) == LOW;
    if (motorCWOcupado() && !palaPulsada) return;
    if (motorCWOcupado()) {
      motorCWDetener();  // Break-in
      if (!qsoVerRx) qsoAnadir(qsoTextoRx, qsoPalabraSonando);
      while (colaRxNum > 0) {
        qsoAnadir(qsoTextoRx, String(colaRxDuo[colaRxIni]));
        colaRxIni = (colaRxIni + 1) % COLA_RX_TAM;
        colaRxNum--;
      }
      finEmisionDuo = millis();
    } else {
      if (!qsoVerRx) qsoAnadir(qsoTextoRx, qsoPalabraSonando);  // Con VER NO se ve al acabar de sonar
      finEmisionDuo = motorCWFinMs;
    }
    qsoSonando = false;
    qsoPalabraSonando = "";
    keyerReanudar();
    pantallaPendiente = true;
    return;
  }

  if (colaRxNum == 0) return;
  // Silencio de palabra (7 puntos) sin manipular y espacio entre palabras recibidas
  if (millis() - keyerUltimoUsoMs < 8400UL / keyerWpm) return;
  if (millis() - finEmisionDuo < 8400UL / wpmEmisionDuo) return;

  qsoPalabraSonando = String(colaRxDuo[colaRxIni]);
  wpmEmisionDuo = colaRxWpm[colaRxIni];
  colaRxIni = (colaRxIni + 1) % COLA_RX_TAM;
  colaRxNum--;
  if (qsoVerRx) qsoAnadir(qsoTextoRx, qsoPalabraSonando);

  keyerPausar();
  qsoSonando = true;
  actualizarPantalla();  // Antes de que suene, como en el Duo
  pantallaPendiente = false;
  motorCWEmitir(qsoPalabraSonando.c_str(), wpmEmisionDuo, 100, "QSO");
}

// ============================================================================
// DECODIFICADOR CW DEL AUDIO DE LA EMISORA (GPIO6, ADC1)
// ============================================================================
// Una tarea en el nucleo 0 lee el ADC por DMA a 8 kHz (sin cargar loop()) y, cada bloque de 96 muestras
// (12 ms), mide con Goertzel la energia del tono. Umbral adaptativo (ruido de fondo y pico) con histeresis;
// la duracion de las marcas y los silencios, comparada con un punto que se adapta a la velocidad, da puntos,
// rayas, fin de caracter y espacio. Tono fijo (400-1000 Hz) o automatico: busca el mas fuerte de 400 a
// 1000 Hz en pasos de 50. Mientras sale por KEY (y 300 ms despues) no escucha, para no descodificar el tono
// de la propia emisora. Sin probar con senal real (el circuito de entrada aun no esta montado).
#define DECOD_CANAL_ADC   5     // GPIO6 = ADC1, canal 5
#define DECOD_FS          8000  // Hz
#define DECOD_N           96    // Muestras por bloque (12 ms, ancho de banda ~80 Hz)
#define DECOD_BLOQUE_MS   12
#define DECOD_NUM_BINS    13    // 400..1000 Hz cada 50 (modo automatico)
#define DECOD_MIN_SENAL   1500.0f   // Magnitud minima para considerar que hay tono (amplitud ~30 cuentas)
#define DECOD_ESCALA_MAX  96000.0f  // Magnitud a fondo de escala (amplitud ~2000 cuentas): barra al 100 %
#define DECOD_SORDO_TRAS_KEY_MS 300
#define DECOD_PILA        4096

TaskHandle_t tareaDecod = NULL;
QueueHandle_t colaDecod = NULL;         // Caracteres descodificados -> loop()
volatile bool decodActivo = false;      // Lo escribe loop()
volatile bool decodEnUso = false;       // Lo escribe la tarea: esta leyendo el ADC
volatile uint8_t decodNivelPct = 0;     // Barra de nivel (escala logaritmica)
volatile uint8_t decodUmbralPct = 0;    // Marca del umbral en la barra
volatile uint16_t decodFrecHz = 700;    // Tono que se esta usando
volatile uint8_t decodWpm = 0;          // Velocidad estimada (0 = aun nada)
volatile bool decodSaturado = false;    // La entrada llega a los topes del ADC: bajar RV2
volatile bool decodTono = false;        // Hay tono ahora

float goertzel(const float* x, float coef) {
  float s1 = 0, s2 = 0;
  for (int i = 0; i < DECOD_N; i++) {
    float s = x[i] + coef * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  float p = s1 * s1 + s2 * s2 - coef * s1 * s2;
  return p > 0 ? sqrtf(p) : 0;
}

float coefGoertzel(float f) {
  return 2.0f * cosf(2.0f * PI * f / DECOD_FS);
}

uint8_t nivelEnBarra(float m) {
  if (m <= DECOD_MIN_SENAL) return 0;
  float pct = 100.0f * logf(m / DECOD_MIN_SENAL) / logf(DECOD_ESCALA_MAX / DECOD_MIN_SENAL);
  return (uint8_t)constrain(pct, 0.0f, 100.0f);
}

void decodEnviarCaracter(char c) {
  xQueueSend(colaDecod, &c, 0);
}

void tareaDecodFn(void* parametro) {
  static float muestras[DECOD_N];
  static uint8_t crudo[DECOD_N * SOC_ADC_DIGI_RESULT_BYTES];
  int n = 0;
  float energiaBin[DECOD_NUM_BINS] = {0};
  uint8_t binActual = 6;  // 700 Hz
  float ruido = DECOD_MIN_SENAL, pico = DECOD_MIN_SENAL;
  bool tono = false;
  uint32_t marcaMs = 0, silencioMs = 0;
  float puntoMs = 60;  // 20 wpm para empezar
  char patron[9];
  uint8_t largo = 0;
  bool espacioPendiente = false;
  bool saturado = false;

  for (;;) {
    if (!decodActivo) {
      decodEnUso = false;
      n = 0;
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    decodEnUso = true;

    uint32_t leidos = 0;
    esp_err_t err = adc_digi_read_bytes(crudo, sizeof(crudo), &leidos, 50);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) continue;  // Sin datos (o se ha desbordado: se sigue)

    for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= leidos; i += SOC_ADC_DIGI_RESULT_BYTES) {
      adc_digi_output_data_t* d = (adc_digi_output_data_t*)&crudo[i];
      if (d->type2.unit != 0 || d->type2.channel != DECOD_CANAL_ADC) continue;
      uint16_t v = d->type2.data;
      if (v < 40 || v > 4055) saturado = true;
      muestras[n++] = v;
      if (n < DECOD_N) continue;
      n = 0;

      // Bloque completo: se quita la continua (polarizacion a 1,65 V)
      float media = 0;
      for (int k = 0; k < DECOD_N; k++) media += muestras[k];
      media /= DECOD_N;
      for (int k = 0; k < DECOD_N; k++) muestras[k] -= media;

      float m;
      if (trxTonoDecodHz == 0) {
        // Automatico: energia de cada tono, suavizada; se cambia al mas fuerte si destaca de verdad
        uint8_t mejor = binActual;
        for (uint8_t b = 0; b < DECOD_NUM_BINS; b++) {
          float e = goertzel(muestras, coefGoertzel(400 + 50 * b));
          energiaBin[b] = 0.9f * energiaBin[b] + 0.1f * e;
          if (energiaBin[b] > energiaBin[mejor]) mejor = b;
        }
        if (mejor != binActual && energiaBin[mejor] > 1.5f * energiaBin[binActual] &&
            energiaBin[mejor] > DECOD_MIN_SENAL) binActual = mejor;
        decodFrecHz = 400 + 50 * binActual;
      } else {
        decodFrecHz = trxTonoDecodHz;
      }
      m = goertzel(muestras, coefGoertzel(decodFrecHz));

      // Umbral adaptativo: ruido (lento, solo sin tono) y pico (sube rapido, baja despacio)
      if (!tono) ruido = 0.95f * ruido + 0.05f * m;
      if (m > pico) pico = 0.5f * pico + 0.5f * m;
      else pico = 0.995f * pico;
      if (pico < ruido * 2) pico = ruido * 2;
      float alto = ruido + 0.55f * (pico - ruido);
      float bajo = ruido + 0.40f * (pico - ruido);
      bool hayTono = tono ? (m > bajo) : (m > alto && m > DECOD_MIN_SENAL && pico > 3 * ruido);

      // Sordo mientras se emite por KEY y justo despues
      bool sordo = keyActivaDesdeMs != 0 || millis() - keyFinMs < DECOD_SORDO_TRAS_KEY_MS;
      if (sordo) hayTono = false;

      decodNivelPct = nivelEnBarra(m);
      decodUmbralPct = nivelEnBarra(alto);
      decodSaturado = saturado;
      saturado = false;
      decodTono = hayTono;

      if (hayTono) {
        if (!tono) marcaMs = 0;
        marcaMs += DECOD_BLOQUE_MS;
      } else {
        if (tono) {
          // Fin de una marca: punto o raya segun el punto estimado; las muy cortas son ruido
          if (marcaMs >= max(13.0f, puntoMs * 0.3f)) {
            bool raya = marcaMs > 2 * puntoMs;
            if (largo < 8) patron[largo] = raya ? '-' : '.';
            if (largo < 8) largo++;
            float estimado = raya ? marcaMs / 3.0f : marcaMs;
            puntoMs = constrain(0.7f * puntoMs + 0.3f * estimado, 24.0f, 240.0f);  // 50 a 5 wpm
            decodWpm = (uint8_t)(1200 / puntoMs);
            espacioPendiente = false;
          }
          silencioMs = 0;
        }
        silencioMs += DECOD_BLOQUE_MS;
        if (largo > 0 && silencioMs >= 2 * puntoMs) {
          patron[min(largo, (uint8_t)8)] = '\0';
          decodEnviarCaracter(largo <= 7 ? decodificarPatron(patron) : '*');
          largo = 0;
          espacioPendiente = true;
        } else if (espacioPendiente && silencioMs >= 5 * puntoMs) {
          decodEnviarCaracter(' ');
          espacioPendiente = false;
        }
      }
      tono = hayTono;
    }
  }
}

void iniciarDecodificador() {
  colaDecod = xQueueCreate(64, sizeof(char));
  xTaskCreatePinnedToCore(tareaDecodFn, "decod", DECOD_PILA, NULL, 2, &tareaDecod, 0);
}

// Pone en marcha el ADC por DMA en GPIO6 y despierta la tarea. Mientras, la bateria no se lee (mismo ADC1)
void decodEmpezar() {
  if (decodActivo) return;
  adc_digi_init_config_t ini = {};
  ini.max_store_buf_size = DECOD_N * SOC_ADC_DIGI_RESULT_BYTES * 4;
  ini.conv_num_each_intr = DECOD_N * SOC_ADC_DIGI_RESULT_BYTES;
  ini.adc1_chan_mask = BIT(DECOD_CANAL_ADC);
  ini.adc2_chan_mask = 0;
  esp_err_t err = adc_digi_initialize(&ini);
  if (err != ESP_OK) {
    Serial.printf("[DECOD] ERROR AL INICIAR EL ADC: %d\n", err);
    return;
  }
  adc_digi_pattern_config_t patron = {};
  patron.atten = ADC_ATTEN_DB_12;
  patron.channel = DECOD_CANAL_ADC;
  patron.unit = 0;  // ADC1
  patron.bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
  adc_digi_configuration_t cfg = {};
  cfg.conv_limit_en = false;
  cfg.conv_limit_num = 250;
  cfg.pattern_num = 1;
  cfg.adc_pattern = &patron;
  cfg.sample_freq_hz = DECOD_FS;
  cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
  err = adc_digi_controller_configure(&cfg);
  if (err == ESP_OK) err = adc_digi_start();
  if (err != ESP_OK) {
    Serial.printf("[DECOD] ERROR AL CONFIGURAR EL ADC: %d\n", err);
    adc_digi_deinitialize();
    return;
  }
  decodWpm = 0;
  xQueueReset(colaDecod);
  decodActivo = true;
  xTaskNotifyGive(tareaDecod);
  Serial.println(F("[DECOD] EN MARCHA (GPIO6)"));
}

void decodParar() {
  if (!decodActivo) return;
  decodActivo = false;
  unsigned long inicio = millis();
  while (decodEnUso && millis() - inicio < 200) vTaskDelay(1);  // La lectura caduca a los 50 ms
  adc_digi_stop();
  adc_digi_deinitialize();
  decodNivelPct = 0;
  decodTono = false;
  Serial.println(F("[DECOD] PARADO"));
}

// ============================================================================
// PANTALLA TRX: OPERAR (KEY), QSO, MEMORIAS Y EDITOR
// ============================================================================
bool palaPulsada() {
  return digitalRead(KEYER_PADDLE_DIT) == LOW || digitalRead(KEYER_PADDLE_DAH) == LOW;
}

// Sustituye {MI} y {SU} (si no hay corresponsal, {SU} se queda en nada). Cabe en el motor CW (64)
String trxExpandir(const char* memoria) {
  String t = memoria;
  t.replace("{MI}", indicativoEstacion);
  t.replace("{SU}", trxSu);
  t.trim();
  if (t.length() > MOTOR_CW_TEXTO_MAX) t = t.substring(0, MOTOR_CW_TEXTO_MAX);
  return t;
}

/**
 * Deja un texto con lo que se puede emitir: mayusculas, caracteres con codigo Morse, espacios y las
 * variables {MI} y {SU} (si se permiten). Sirve para el portal y para lo que llega de las palas.
 */
String trxLimpiarTexto(String t, bool conVariables, uint8_t maximo) {
  t.toUpperCase();
  String r = "";
  for (unsigned int i = 0; i < t.length() && r.length() < maximo; i++) {
    if (conVariables && (t.startsWith("{MI}", i) || t.startsWith("{SU}", i))) {
      if (r.length() + 4 > maximo) break;
      r += t.substring(i, i + 4);
      i += 3;
    } else if (t[i] == ' ') {
      if (r.length() > 0 && !r.endsWith(" ")) r += ' ';
    } else if (*obtenerCodigoMorse(t[i]) != '\0') {
      r += t[i];
    }
  }
  r.trim();
  return r;
}

// OPERAR, QSO o sus ajustes: las palas manipulan la emisora
bool trxManipulando() {
  return subTrx == TRX_OPERAR || subTrx == TRX_QSO || subTrx == TRX_AJUSTES;
}

// Habilita la salida KEY (OPERAR y QSO) y pone el keyer en marcha
void trxHabilitarKey() {
  keyCortadaPorSeguridad = false;
  keyHabilitada = true;
  keyerReanudar();
}

void trxDeshabilitarKey() {
  motorCWDetener();
  keyerPausar();
  keyHabilitada = false;
  digitalWrite(KEY_OUT_PIN, LOW);
  keyActivaDesdeMs = 0;
  trxEnviandoMemoria = false;
  trxSintonizando = false;
}

// Tune: portadora de trxTuneSeg segundos por KEY. Pulsar otra vez o tocar una pala la corta
void trxTune() {
  if (trxSintonizando) {
    motorCWDetener();  // procesarTrx() lo da por acabado y reanuda el keyer
    return;
  }
  if (!keyHabilitada) return;  // Tras un corte de seguridad, no
  keyerPausar();
  trxEnviandoMemoria = false;
  trxSintonizando = true;
  motorCWPortadora(trxTuneSeg * 1000, "TUNE");
  Serial.printf("[TRX] TUNE %u s\n", trxTuneSeg);
}

void trxEntrarSub(SubTrx sub) {
  subTrx = sub;
  editandoValor = false;
  switch (sub) {
    case TRX_DECOD:
      opcionTrxSub = 1;  // menu
      decodEnAjustes = false;
      trxTextoRx = "";
      decodEmpezar();
      break;
    case TRX_OPERAR:
      opcionTrxSub = 0;  // mem
      trxTextoTx = "";
      trxHabilitarKey();
      break;
    case TRX_QSO:
      opcionTrxSub = 0;  // mem
      trxTextoRx = trxTextoTx = "";
      decodEmpezar();
      trxHabilitarKey();
      break;
    case TRX_MEMORIAS:
      trxMemoriaSel = 0;
      break;
    default:
      break;
  }
}

// De una subpantalla al menu TRX: todo apagado y ajustes guardados
void trxSalirAlMenu() {
  trxDeshabilitarKey();
  decodParar();
  guardarTrxNVS();
  subTrx = TRX_MENU;
  editandoValor = false;
}

// Lista de memorias para enviar (desde OPERAR o QSO): la emision por KEY sigue habilitada, el keyer en pausa
void trxAbrirListaEnvio() {
  subTrxOrigen = subTrx;
  subTrx = TRX_LISTA_ENVIO;
  trxMemoriaSel = 0;
  keyerPausar();
}

void trxVolverDeLista() {
  subTrx = subTrxOrigen;
  if (!trxEnviandoMemoria) keyerReanudar();
}

void trxEnviarMemoria(uint8_t i) {
  String texto = trxExpandir(trxMemorias[i]);
  if (texto.length() == 0) return;
  subTrx = subTrxOrigen;
  keyerPausar();
  qsoAnadir(trxTextoTx, texto);
  trxSintonizando = false;
  trxEnviandoMemoria = true;
  motorCWEmitir(texto.c_str(), trxWpm, 100, "MEM");
  Serial.printf("[TRX] MEMORIA %u: %s\n", i + 1, texto.c_str());
}

// Editor: memoria (0..5) o indicativo del corresponsal (-1). Desde OPERAR/QSO la KEY se deshabilita mientras
void trxAbrirEditor(int8_t destino) {
  subTrxOrigen = subTrx;
  if (keyHabilitada) {
    motorCWDetener();
    trxEnviandoMemoria = false;
    trxSintonizando = false;
    keyHabilitada = false;
  }
  editorDestino = destino;
  editorTexto = destino < 0 ? String(trxSu) : String(trxMemorias[destino]);
  editorMax = destino < 0 ? TRAMA_LONG_INDICATIVO : TRX_MEMORIA_MAX;
  editorSel = 0;
  subTrx = TRX_EDITOR;
  keyerReanudar();  // Tambien se escribe con las palas (sin KEY)
}

void trxCerrarEditor(bool guardar) {
  if (guardar) {
    if (editorDestino < 0) {
      strlcpy(trxSu, trxLimpiarTexto(editorTexto, false, TRAMA_LONG_INDICATIVO).c_str(), sizeof(trxSu));
    } else {
      strlcpy(trxMemorias[editorDestino], trxLimpiarTexto(editorTexto, true, TRX_MEMORIA_MAX).c_str(),
              sizeof(trxMemorias[editorDestino]));
      guardarMemoriaNVS(editorDestino);
    }
  }
  subTrx = subTrxOrigen;
  if (trxManipulando()) trxHabilitarKey();
  else keyerPausar();
}

// Rueda de caracteres del editor: A-Z, 0-9, signos y acciones
const char* const EDITOR_ESPECIALES[] = { ".", ",", "?", "/", "=", "ESP", "{MI}", "{SU}", "<-", "CLR", "OK", "ESC" };
#define EDITOR_NUM_ESPECIALES (sizeof(EDITOR_ESPECIALES) / sizeof(EDITOR_ESPECIALES[0]))
#define EDITOR_TAM_RUEDA (36 + EDITOR_NUM_ESPECIALES)

String editorElemento(uint8_t i) {
  if (i < 26) return String((char)('A' + i));
  if (i < 36) return String((char)('0' + i - 26));
  return String(EDITOR_ESPECIALES[i - 36]);
}

void editorAnadir(const String& s) {
  if (editorTexto.length() + s.length() <= editorMax) editorTexto += s;
}

void confCerrarEditor(bool guardar);  // Definida con la pantalla CONF

// El editor sirve a TRX (memorias y SU) y a CONF (ID y locator de la estacion)
void editorCerrar(bool guardar) {
  if (editorDestino <= EDITOR_ID_ESTACION) confCerrarEditor(guardar);
  else trxCerrarEditor(guardar);
}

void editorAplicar() {
  String e = editorElemento(editorSel);
  editorError = false;
  if (e == "OK") { editorCerrar(true); return; }
  if (e == "ESC") { editorCerrar(false); return; }
  if (e == "CLR") { editorTexto = ""; return; }
  // ID y locator: solo letras y cifras (y "/" en el ID); sin espacios ni signos
  if (editorDestino <= EDITOR_ID_ESTACION && e.length() == 1 && !isalnum(e[0]) &&
      !(e == "/" && editorDestino == EDITOR_ID_ESTACION)) return;
  if (editorDestino <= EDITOR_ID_ESTACION && e == "ESP") return;
  if (e == "<-") {
    if (editorTexto.endsWith("{MI}") || editorTexto.endsWith("{SU}")) editorTexto.remove(editorTexto.length() - 4);
    else if (editorTexto.length() > 0) editorTexto.remove(editorTexto.length() - 1);
    return;
  }
  if (e == "ESP") { if (editorTexto.length() > 0 && !editorTexto.endsWith(" ")) editorAnadir(" "); return; }
  if (e.startsWith("{") && editorDestino < 0) return;  // El indicativo no lleva variables
  editorAnadir(e);
}

// Tono del decodificador: AUTO (0) y luego 400..1000 Hz de 50 en 50
void ajustarTonoDecod(int8_t giro) {
  int v = trxTonoDecodHz;
  if (giro > 0) v = (v == 0) ? TONO_MIN_HZ : v + TONO_PASO_HZ;
  else v = (v <= TONO_MIN_HZ) ? 0 : v - TONO_PASO_HZ;
#ifdef SIN_ENCODER
  if (v > TONO_MAX_HZ) v = 0;  // Sin encoder: da la vuelta
#else
  if (v > TONO_MAX_HZ) v = TONO_MAX_HZ;
#endif
  trxTonoDecodHz = v;
}

// Lo descodificado por el keyer en la pantalla TRX: al editor, o a lo manipulado en OPERAR/QSO
void trxCaracterManipulado(char c) {
  if (subTrx == TRX_EDITOR) {
    if (c == ' ') { if (editorTexto.length() > 0 && !editorTexto.endsWith(" ")) editorAnadir(" "); }
    else if (c != '*') editorAnadir(String(c));
  } else if (trxManipulando()) {
    if (c == ' ') { if (trxTextoTx.length() > 0 && !trxTextoTx.endsWith(" ")) trxTextoTx += ' '; }
    else trxTextoTx += c;
    if (trxTextoTx.length() > QSO_TEXTO_MAX) trxTextoTx.remove(0, trxTextoTx.length() - QSO_TEXTO_MAX);
  }
  pantallaPendiente = true;
}

/**
 * Cada vuelta de loop() en la pantalla TRX: lo descodificado, la barra de nivel, el fin (o el corte con
 * las palas) de una memoria y el corte de seguridad de KEY.
 */
void procesarTrx() {
  // Corte de seguridad: KEY activa mas de 10 s seguidos (pala atascada, fallo...). Se lee una sola vez:
  // el keyer y el motor CW (mas prioridad) la cambian en cada elemento y, leida dos veces, podia pasar
  // a 0 entre las dos lecturas (millis() - 0 = siempre mas de 10 s: cortaba al manipular)
  // Con el tune, el limite es su duracion mas un margen (la portadora la apaga el motor CW a su hora)
  unsigned long keyDesde = keyActivaDesdeMs;
  long limiteKey = trxSintonizando ? (long)trxTuneSeg * 1000 + TRX_TUNE_MARGEN_MS : (long)KEY_MAX_CONTINUA_MS;
  if (keyDesde != 0 && (long)(millis() - keyDesde) > limiteKey) {  // Con signo, por si acaso
    motorCWDetener();
    keyerPausar();
    keyHabilitada = false;
    digitalWrite(KEY_OUT_PIN, LOW);
    keyActivaDesdeMs = 0;
    trxEnviandoMemoria = false;
    trxSintonizando = false;
    keyCortadaPorSeguridad = true;
    Serial.printf("[TRX] KEY CORTADA: MAS DE %ld MS ACTIVA (desde %lu, ahora %lu)\n", limiteKey, keyDesde, millis());
    pantallaPendiente = true;
  }

  if (pantallaActual != PANTALLA_TRX) return;

  char c;
  while (colaDecod != NULL && xQueueReceive(colaDecod, &c, 0) == pdTRUE) {
    Serial.print(c);
    if (c == ' ') { if (trxTextoRx.length() > 0 && !trxTextoRx.endsWith(" ")) trxTextoRx += ' '; }
    else trxTextoRx += c;
    if (trxTextoRx.length() > QSO_TEXTO_MAX) trxTextoRx.remove(0, trxTextoRx.length() - QSO_TEXTO_MAX);
    pantallaPendiente = true;
  }

  // La barra de nivel se mueve sola: se redibuja 5 veces por segundo
  static unsigned long ultimoRefresco = 0;
  if ((subTrx == TRX_DECOD || subTrx == TRX_QSO) && millis() - ultimoRefresco >= 200) {
    ultimoRefresco = millis();
    pantallaPendiente = true;
  }

  // Para ajustar RV2 en banco
  static unsigned long ultimoDiag = 0;
  if (depuActiva(DEPU_DECOD) && decodActivo && millis() - ultimoDiag >= 2000) {
    ultimoDiag = millis();
    Serial.printf("[DECOD] %u Hz, nivel %u%%, umbral %u%%, %u wpm%s\n", decodFrecHz, decodNivelPct, decodUmbralPct,
                  decodWpm, decodSaturado ? ", SATURA" : "");
  }

  if (trxEnviandoMemoria || trxSintonizando) {
    if (motorCWOcupado() && palaPulsada()) motorCWDetener();  // Break-in: las palas cortan la memoria o el tune
    if (!motorCWOcupado()) {
      if (trxSintonizando) Serial.println(F("[TRX] TUNE FIN"));
      trxEnviandoMemoria = false;
      trxSintonizando = false;
      pantallaPendiente = true;
      if (trxManipulando()) keyerReanudar();
    }
  }
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
    } else if (!duplicada && tipo == TRAMA_TIPO_TEXTO_QSO && enQSO() && texto.length() > 0) {
      uint8_t wpmRx = trama[4];
      if (wpmRx < WPM_MIN_DUO || wpmRx > WPM_MAX_DUO) wpmRx = keyerWpm;
      strlcpy(qsoIndicativoRx, indicativo, sizeof(qsoIndicativoRx));
      encolarGrupoRxDuo(texto.c_str(), wpmRx);
    }
  }

  radio.startReceive();
}

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

/**
 * Sale de las subpantallas LoRa (Duo / QSO): desactiva la recepcion, corta el tono que pudiera estar sonando
 * y limpia el estado, para que un paquete que llegue despues no suene encima de otras pantallas
 */
void salirModoLoRa() {
  if (subpantalla4Activa == 2) {
    qsoEnviarPalabra();  // Lo que quedara a medias
    keyerPausar();
    guardarKeyerNVS();   // wpm y VER del QSO
    qsoSonando = false;
  }
  enPantalla4_Sub = false;
  subpantalla4Activa = 0;
  loraEnAjustes = false;
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
// El WiFi solo se enciende en PROP y POTA, en SOL sin hora, para el portal (webconf) y para ACTUALIZAR FW, y se
// apaga al salir. Si no hay red guardada o no conecta, PROP, POTA y SOL lo dicen y ofrecen abrir el portal
// (WiFiManager), donde se elige la red desde el movil; queda guardada en la NVS del WiFi.
// SOTA no se incluye: sus condiciones de uso exigen aprobacion previa para software generado por IA.
#define WIFI_TIMEOUT_CONEXION_MS 15000
#define PORTAL_TIMEOUT_S         300
#define PORTAL_CLAVE             "chispas73"  // Clave del punto de acceso de configuracion (minimo 8)
#define ZONA_HORARIA             "CET-1CEST,M3.5.0,M10.5.0/3"  // Peninsula
#define URL_PROPAGACION          "https://www.hamqsl.com/solarxml.php"
#define URL_POTA                 "https://api.pota.app/spot/activator"
// hamqsl.com (N0NBH) pide expresamente no consultar el XML mas de una vez por hora ("please only select to update
// every hour", hamqsl.com/solar.html): es lo que tarda en cambiar el flujo solar; el resto, cada 3 horas
#define REFRESCO_PROPAGACION_MS  (60UL * 60 * 1000)
#define REFRESCO_POTA_MS         (2UL * 60 * 1000)   // POTA no publica limites: se consulta con moderacion
#define MAX_SPOTS_POTA           40
#define SPOTS_POR_PAGINA         4

enum EstadoWifi { WIFI_APAGADO, WIFI_CONECTANDO, WIFI_PORTAL, WIFI_CONECTADO, WIFI_SIN_CONEXION };
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

// PROP, POTA y SOL: menu (VER DATOS y ACTUALIZAR; sin conexion, ABRIR WEBCONF y REINTENTAR), paginas que se
// pasan girando y vista del portal. Solo se ve una de las tres a la vez: comparten el estado
#define OPCION_BARRA_DATOS 2
#define OPCION_BARRA_PROP  OPCION_BARRA_DATOS
#define OPCION_BARRA_SPOTS OPCION_BARRA_DATOS
uint8_t opcionPantallaProp = OPCION_BARRA_PROP;    // Menu: 0 = ver datos, 1 = actualizar, 2 = barra ant./sig.
uint8_t opcionPantallaSpots = OPCION_BARRA_SPOTS;
uint8_t opcionPantallaSol = OPCION_BARRA_DATOS;
enum SubDatos { DATOS_MENU, DATOS_PAGINAS, DATOS_PORTAL };
SubDatos subDatos = DATOS_MENU;
uint8_t focoPortalDatos = 0;
uint8_t paginaPropagacion = 0;              // 0 = indices, 1 = condiciones por banda
uint8_t paginaSol = 0;                      // 0 = hoy, 1 = ahora, 2 = manana
void dibujarVistaPortal(uint8_t foco);      // Definidas con la pantalla CONF
bool procesarVistaPortal(uint8_t& foco, int8_t giro, bool pulsa, bool atras);

// CONF: menu con CONFIG (opciones) y STATUS (informacion, en paginas)
#define OPCION_BARRA_CONFIG 2
uint8_t opcionPantallaConfig = OPCION_BARRA_CONFIG;  // Menu: 0 = CONFIG, 1 = STATUS, 2 = barra ant./sig.
enum SubConf { CONF_MENU, CONF_CONFIG, CONF_STATUS, CONF_PORTAL, CONF_EDITOR, CONF_OTA, CONF_DEPURACION };
SubConf subConf = CONF_MENU;
// Lista de CONFIG (4 a la vista; "volver" en el pie)
enum OpcionConf { OC_ID, OC_LOCATOR, OC_ACELERACION, OC_APAGADO, OC_TONO, OC_WEBCONF, OC_ACTUALIZAR, OC_DEPURACION,
                  OC_FABRICA, OC_VOLVER };
#define N_OPCIONES_CONF 10
uint8_t focoConf = 0;
// Lista de DEPURACION: una opcion SI/NO por bit DEPU_* y "volver" en el pie (foco N_OPCIONES_DEPU)
#define N_OPCIONES_DEPU 4
const uint8_t BITS_DEPU[N_OPCIONES_DEPU] = { DEPU_SERIE, DEPU_DIAG, DEPU_DECOD, DEPU_CW };
const char* const NOMBRES_OPCIONES_DEPU[N_OPCIONES_DEPU] = { "ORDENES SERIE", "DIAG (1 MIN)", "DECOD (2 S)",
                                                             "RETRASO CW" };
uint8_t focoDepu = 0;
uint8_t focoPortalConf = 0;  // Vista del portal: 0 = ver qr, 1 = volver
uint8_t paginaStatus = 0;
#define N_PAGINAS_STATUS 5

// Apagado automatico: minutos sin tocar nada antes de dormir (0 = nunca). Se elige en CONF y se guarda en NVS
const uint8_t OPCIONES_APAGADO_MIN[] = { 0, 5, 10, 15, 30, 60, 120 };
const uint8_t N_OPCIONES_APAGADO = sizeof(OPCIONES_APAGADO_MIN);
uint8_t apagadoAutoMin = 30;

// Aceleracion del encoder al girar rapido: 0 = no; 2-5 = el valor avanza x2..x5 (NVS "acel")
#define ACELERACION_MAX 5
uint8_t aceleracionGiro = ACELERACION_MAX;

String textoAceleracion() {
  return aceleracionGiro == 0 ? String("NO") : "x" + String(aceleracionGiro);
}

// 0, 2, 3, 4, 5 (el 1 seria igual que no acelerar)
void ajustarAceleracion(int8_t paso) {
  int v = aceleracionGiro + paso;
  if (aceleracionGiro == 0 && paso > 0) v = 2;
  else if (v < 2) v = 0;
#ifdef SIN_ENCODER
  if (v > ACELERACION_MAX) v = 0;  // Sin encoder: da la vuelta
#else
  if (v > ACELERACION_MAX) v = ACELERACION_MAX;
#endif
  aceleracionGiro = v;
}

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

// Memorias de la pantalla TRX (la primera explica las variables)
WiFiManagerParameter paramMemoria1("m0", "Memoria 1 ({MI} = tu indicativo, {SU} = el del corresponsal)", "", TRX_MEMORIA_MAX);
WiFiManagerParameter paramMemoria2("m1", "Memoria 2", "", TRX_MEMORIA_MAX);
WiFiManagerParameter paramMemoria3("m2", "Memoria 3", "", TRX_MEMORIA_MAX);
WiFiManagerParameter paramMemoria4("m3", "Memoria 4", "", TRX_MEMORIA_MAX);
WiFiManagerParameter paramMemoria5("m4", "Memoria 5", "", TRX_MEMORIA_MAX);
WiFiManagerParameter paramMemoria6("m5", "Memoria 6", "", TRX_MEMORIA_MAX);
WiFiManagerParameter* const paramMemorias[TRX_NUM_MEMORIAS] = {
  &paramMemoria1, &paramMemoria2, &paramMemoria3, &paramMemoria4, &paramMemoria5, &paramMemoria6
};

bool locatorALatLon(const char* loc, float& lat, float& lon);  // Definida con la pantalla SOL

// Indicativo de 3 a 8 caracteres (letras, cifras y "/"). Si vale, pasa a ser el de la estacion (sin guardar)
bool aplicarIndicativo(String call) {
  call.trim();
  call.toUpperCase();
  bool valido = call.length() >= 3 && call.length() <= TRAMA_LONG_INDICATIVO;
  for (size_t i = 0; i < call.length(); i++) {
    if (!isalnum(call[i]) && call[i] != '/') valido = false;
  }
  if (valido) strlcpy(indicativoEstacion, call.c_str(), sizeof(indicativoEstacion));
  return valido;
}

// Locator de 4 o 6 caracteres: "in80DK" -> "IN80dk". Si vale, pasa a ser el de la estacion (sin guardar)
bool aplicarLocator(String loc) {
  loc.trim();
  for (size_t i = 0; i < loc.length(); i++) loc[i] = (i < 2) ? toupper(loc[i]) : tolower(loc[i]);
  float lat, lon;
  bool valido = locatorALatLon(loc.c_str(), lat, lon);
  if (valido) strlcpy(locatorEstacion, loc.c_str(), sizeof(locatorEstacion));
  return valido;
}

void guardarParametrosPortal() {
  if (!aplicarIndicativo(paramIndicativo.getValue())) {
    Serial.printf("[PORTAL] INDICATIVO NO VALIDO: %s\n", paramIndicativo.getValue());
  }
  if (!aplicarLocator(paramLocator.getValue())) {
    Serial.printf("[PORTAL] LOCATOR NO VALIDO: %s\n", paramLocator.getValue());
  }

  horaUtcCabecera = strlen(paramHoraUtc.getValue()) > 0;
  paramHoraUtc.marcada = horaUtcCabecera;
  paramHoraUtc.setValue("1", 2);  // Para que vuelva a enviar "1" si se marca otra vez

  guardarEstacionNVS();
  Serial.printf("[PORTAL] ESTACION: %s %s, hora %s\n", indicativoEstacion, locatorEstacion,
                horaUtcCabecera ? "UTC" : "local");

  // Memorias: solo lo que se puede emitir (mayusculas, Morse, espacios, {MI} y {SU})
  for (uint8_t i = 0; i < TRX_NUM_MEMORIAS; i++) {
    String m = trxLimpiarTexto(paramMemorias[i]->getValue(), true, TRX_MEMORIA_MAX);
    if (m != trxMemorias[i]) {
      strlcpy(trxMemorias[i], m.c_str(), sizeof(trxMemorias[i]));
      guardarMemoriaNVS(i);
      Serial.printf("[PORTAL] MEMORIA %u: %s\n", i + 1, trxMemorias[i]);
    }
  }

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
    for (uint8_t i = 0; i < TRX_NUM_MEMORIAS; i++) wm.addParameter(paramMemorias[i]);
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
  for (uint8_t i = 0; i < TRX_NUM_MEMORIAS; i++) paramMemorias[i]->setValue(trxMemorias[i], TRX_MEMORIA_MAX);

  cpuSegunWifi(true);
  WiFi.mode(WIFI_STA);
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.startConfigPortal(nombrePortal, PORTAL_CLAVE);
  estadoWifi = WIFI_PORTAL;
  Serial.printf("[WIFI] PORTAL ABIERTO: %s\n", nombrePortal);
  pantallaPendiente = true;
}

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
  // PROP, POTA y SOL vuelven a su menu, con el foco en "ABRIR WEBCONF"
  subDatos = DATOS_MENU;
  opcionPantallaProp = opcionPantallaSpots = opcionPantallaSol = 0;
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

// ============================================================================
// ACTUALIZACION DEL FIRMWARE (OTA) DESDE LAS RELEASES DE GITHUB
// ============================================================================
// CONF > CONFIG > ACTUALIZAR FW: consulta la ultima release (tambien las beta), compara su etiqueta ("v1.01b")
// con FW_VERSION y, si es mas nueva y el usuario confirma, descarga el binario de esta placa y lo instala en la
// otra particion de aplicacion (default_8MB.csv: app0/app1). El TLS se valida con los certificados raiz de
// include/certificados_github.h. Ademas, con el WiFi ya encendido en PROP o POTA, se mira una vez al dia si hay
// version nueva y se avisa en su linea de estado.
#define OTA_URL_RELEASES "https://api.github.com/repos/ea4iln-debug/CHISPAS-EA4ILN/releases?per_page=1"
#define OTA_COMPROBACION_MS (24UL * 60 * 60 * 1000)
#define OTA_BATERIA_MIN_PCT 30  // Con menos bateria (y sin USB) no se instala: un corte a medias obliga a cargar por USB

enum EstadoOta { OTA_REPOSO, OTA_CONECTANDO, OTA_BUSCANDO, OTA_AL_DIA, OTA_HAY_NUEVA, OTA_DESCARGANDO, OTA_ERROR, OTA_HECHO };
EstadoOta estadoOta = OTA_REPOSO;
String otaVersionNueva = "";   // Etiqueta de la ultima release, sin la "v"
String otaUrlBinario = "";     // Binario de esta placa en esa release ("" si no lo trae)
String otaError = "";          // Motivo del fallo, corto (cabe en una linea)
uint8_t otaProgreso = 0;       // % descargado
bool otaComprobada = false;
unsigned long otaComprobadaMs = 0;
uint8_t focoOta = 0;           // Con version nueva: 0 instalar, 1 volver

/**
 * Compara versiones "1.00b", "1.01b", "1.10": numeros separados por puntos y, al final, una letra opcional
 * (beta). A igual numero, la version sin letra es mas nueva. Devuelve >0 si a es mas nueva que b
 */
int compararVersiones(const String& a, const String& b) {
  auto trocear = [](const String& v, long num[3], bool& beta) {
    uint8_t n = 0;
    num[0] = num[1] = num[2] = 0;
    beta = false;
    for (size_t i = 0; i < v.length(); i++) {
      char c = v[i];
      if (isdigit(c) && n < 3) num[n] = num[n] * 10 + (c - '0');
      else if (c == '.') n++;
      else if (isalpha(c)) beta = true;
    }
  };
  long na[3], nb[3];
  bool betaA, betaB;
  trocear(a, na, betaA);
  trocear(b, nb, betaB);
  for (uint8_t i = 0; i < 3; i++) {
    if (na[i] != nb[i]) return na[i] > nb[i] ? 1 : -1;
  }
  if (betaA != betaB) return betaA ? -1 : 1;
  return 0;
}

bool hayActualizacion() {
  return otaUrlBinario.length() > 0 && compararVersiones(otaVersionNueva, FW_VERSION) > 0;
}

/**
 * Consulta la ultima release en la API de GitHub (con el WiFi conectado). Deja la version y la URL del binario
 * de esta placa: "chispas-<version>.bin" o, sin encoder, "chispas-<version>-sin-encoder.bin". Bloquea 1-3 s
 */
bool buscarActualizacion() {
  WiFiClientSecure cliente;
  cliente.setCACert(CERTIFICADOS_GITHUB);
  HTTPClient http;
  http.setTimeout(8000);
  String cuerpo;
  int codigo = -1;
  if (http.begin(cliente, OTA_URL_RELEASES)) {
    http.setUserAgent("Chispas-EA4ILN/" FW_VERSION);  // La API de GitHub lo exige
    http.addHeader("Accept", "application/vnd.github+json");
    codigo = http.GET();
    if (codigo == HTTP_CODE_OK) cuerpo = http.getString();
    http.end();
  }
  otaComprobada = true;
  otaComprobadaMs = millis();
  if (codigo != HTTP_CODE_OK) {
    otaError = codigo < 0 ? "SIN CONEXION" : "GITHUB: HTTP " + String(codigo);
    Serial.printf("[OTA] RELEASES -> %d\n", codigo);
    return false;
  }

  // Solo la etiqueta y los binarios: el resto de la respuesta (notas, autores...) se descarta al leer
  JsonDocument filtro;
  filtro[0]["tag_name"] = true;
  filtro[0]["assets"][0]["name"] = true;
  filtro[0]["assets"][0]["browser_download_url"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, cuerpo, DeserializationOption::Filter(filtro))) {
    otaError = "RESPUESTA NO VALIDA";
    return false;
  }
  if (doc[0].isNull()) {
    otaError = "NO HAY RELEASES";
    return false;
  }
  String etiqueta = doc[0]["tag_name"] | "";
  if (etiqueta.startsWith("v") || etiqueta.startsWith("V")) etiqueta.remove(0, 1);
  otaVersionNueva = etiqueta;
  otaUrlBinario = "";
  // Nombre exacto: la release lleva tambien las imagenes completas para grabar por USB ("...-usb.bin")
#ifdef SIN_ENCODER
  String binario = "chispas-" + etiqueta + "-sin-encoder.bin";
#else
  String binario = "chispas-" + etiqueta + ".bin";
#endif
  for (JsonObject a : doc[0]["assets"].as<JsonArray>()) {
    if (binario.equalsIgnoreCase(a["name"] | "")) otaUrlBinario = a["browser_download_url"] | "";
  }
  Serial.printf("[OTA] ULTIMA RELEASE v%s (instalada v%s)%s\n", otaVersionNueva.c_str(), FW_VERSION,
                otaUrlBinario.length() ? "" : ", SIN BINARIO PARA ESTA PLACA");
  return true;
}

void actualizarPantalla();  // Definida con el resto del dibujo

/**
 * Descarga e instala el binario (sigue la redireccion de GitHub a su servidor de descargas). La pantalla
 * enseña el progreso; al terminar, reinicia. Si falla, la version instalada sigue intacta
 */
void instalarActualizacion() {
  if (bateriaActual.estado != SOLO_USB && bateriaActual.porcentaje < OTA_BATERIA_MIN_PCT) {
    estadoOta = OTA_ERROR;
    otaError = "BATERIA BAJA (<" + String(OTA_BATERIA_MIN_PCT) + "%)";
    return;
  }
  estadoOta = OTA_DESCARGANDO;
  otaProgreso = 0;
  actualizarPantalla();
  motorCWDetener();
  keyerPausar();
  Serial.printf("[OTA] DESCARGANDO %s\n", otaUrlBinario.c_str());

  WiFiClientSecure cliente;
  cliente.setCACert(CERTIFICADOS_GITHUB);
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.onProgress([](int hecho, int total) {
    uint8_t pct = total > 0 ? (uint8_t)((int64_t)hecho * 100 / total) : 0;
    if (pct != otaProgreso) {
      otaProgreso = pct;
      actualizarPantalla();
    }
  });
  t_httpUpdate_return r = httpUpdate.update(cliente, otaUrlBinario);
  if (r == HTTP_UPDATE_OK) {
    estadoOta = OTA_HECHO;
    actualizarPantalla();
    Serial.printf("[OTA] INSTALADA v%s: REINICIANDO\n", otaVersionNueva.c_str());
    delay(2000);
    ESP.restart();
  }
  estadoOta = OTA_ERROR;
  otaError = "ERROR " + String(httpUpdate.getLastError());
  Serial.printf("[OTA] FALLO: %s\n", httpUpdate.getLastErrorString().c_str());
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
  // sincronizarla por NTP). Si no conecta, su menu ofrece ABRIR WEBCONF y REINTENTAR. En CONF y SOL el WiFi
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
        // En PROP, POTA o SOL se vuelve a su menu sin conexion (ABRIR WEBCONF y REINTENTAR)
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
      // Aviso de version nueva: una vez al dia, aprovechando el WiFi de PROP o POTA
      bool enDatos = pantallaActual == PANTALLA_PROPAGACION || pantallaActual == PANTALLA_SPOTS;
      if (enDatos && !actualizandoWifi && (!otaComprobada || millis() - otaComprobadaMs > OTA_COMPROBACION_MS)) {
        buscarActualizacion();
        pantallaPendiente = true;
      }
      break;
    }

    default:
      break;
  }
}

/**
 * Vista del portal: con el portal abierto, sus datos (red, clave y web); conectando, el aviso. Devuelve
 * false en los demas estados (no dibuja nada)
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
    default:
      return false;
  }
}

// ============================================================================
// DIBUJO DE LAS PANTALLAS
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

void dibujarTituloCentrado(const String& titulo, int16_t y = 11) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(titulo, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, y);
  display.print(titulo);
}

// Porcentaje con un decimal ("82.5%"); "--" sin datos
String textoPorcentaje(uint32_t aciertos, uint32_t total) {
  if (total == 0) return String("--");
  return String(aciertos * 100.0f / total, 1) + "%";
}

/**
 * Pagina "MAS ERRORES" de ESTAD. (Koch o Echo): los caracteres con peor % de acierto, con al menos
 * MIN_INTENTOS_LETRA intentos. A igual %, primero el que tiene mas intentos
 */
void dibujarMasErrores(const StatsCaracter* tabla) {
  dibujarTituloCentrado(F("- MAS ERRORES -"));

  int8_t lista[LINEAS_MAS_ERRORES];
  uint8_t n = 0;
  bool usado[TOTAL_CARACTERES_KOCH] = {false};
  for (uint8_t linea = 0; linea < LINEAS_MAS_ERRORES; linea++) {
    int8_t peor = -1;
    uint32_t peorPct = 0, peorTotal = 0;
    for (uint8_t i = 0; i < TOTAL_CARACTERES_KOCH; i++) {
      uint32_t total = (uint32_t)tabla[i].aciertos + tabla[i].fallos;
      if (usado[i] || total < MIN_INTENTOS_LETRA) continue;
      uint32_t pct = tabla[i].aciertos * 1000UL / total;
      if (peor < 0 || pct < peorPct || (pct == peorPct && total > peorTotal)) {
        peor = i; peorPct = pct; peorTotal = total;
      }
    }
    if (peor < 0) break;
    usado[peor] = true;
    lista[n++] = peor;
  }

  if (n == 0) {
    display.setCursor(0, 24);
    display.print(F("AUN SIN DATOS"));
    display.setCursor(0, 34);
    display.print(F("(min. " ));
    display.print(MIN_INTENTOS_LETRA);
    display.print(F(" por letra)"));
    return;
  }
  char linea[40];  // 21 caracteres a la vista; holgura para el compilador
  for (uint8_t k = 0; k < n; k++) {
    const StatsCaracter& s = tabla[lista[k]];
    uint32_t total = (uint32_t)s.aciertos + s.fallos;
    snprintf(linea, sizeof(linea), "%c %3lu%%  %u/%lu", TABLA_KOCH[lista[k]],
             (unsigned long)(s.aciertos * 100UL / total), s.aciertos, (unsigned long)total);
    display.setCursor(0, 21 + k * 8);
    display.print(linea);
  }
}

// Suma de aciertos y de intentos de todos los caracteres de una tabla
void sumarStatsCaracter(const StatsCaracter* tabla, uint32_t& aciertos, uint32_t& total) {
  aciertos = total = 0;
  for (uint8_t i = 0; i < TOTAL_CARACTERES_KOCH; i++) {
    aciertos += tabla[i].aciertos;
    total += (uint32_t)tabla[i].aciertos + tabla[i].fallos;
  }
}

void dibujarTotalesKoch() {
  dibujarTituloCentrado(F("- TOTALES KOCH -"));
  uint32_t lecciones = 0;
  for (uint8_t i = 0; i < 40; i++) lecciones += statsKoch[i].vecesCompletada;
  uint32_t aciertos, total;
  sumarStatsCaracter(statsCarKoch, aciertos, total);
  aciertos += kochTotAciertos;  // Lecciones corregidas con el total de fallos
  total += kochTotCaracteres;
  display.setCursor(0, 22); display.print(F("LECCIONES:  ")); display.print(lecciones);
  display.setCursor(0, 32); display.print(F("CARACTERES: ")); display.print(total);
  display.setCursor(0, 42); display.print(F("ACIERTO:    ")); display.print(textoPorcentaje(aciertos, total));
}

void dibujarTotalesEcho() {
  dibujarTituloCentrado(F("- TOTALES ECHO -"));
  display.setCursor(0, 22); display.print(F("PRACTICAS: ")); display.print(echoSesiones);
  display.setCursor(0, 32); display.print(F("RETOS: ")); display.print(echoTotalIntentos);
  display.print(F(" BIEN: ")); display.print(echoTotalAciertos);
  display.setCursor(0, 42); display.print(F("ACIERTO:   ")); display.print(textoPorcentaje(echoTotalAciertos, echoTotalIntentos));
}

// Pagina de lecciones de ESTAD. (Koch o Echo): 3 lecciones por pagina (pagina 0 = L01-L03), con las veces
// completadas y una barra con el % maximo. La linea "L01:99x[========]100%" ocupa 21 caracteres = 126 px
void dibujarPaginaLecciones(const StatsLeccion* tabla, uint8_t pagina) {
  uint8_t leccInicio = pagina * 3 + 1;
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t lNum = leccInicio + i;
    if (lNum > 40) break;
    const StatsLeccion& s = tabla[lNum - 1];

    display.setCursor(0, 12 + (i * 13));
    display.print(F("L"));
    if (lNum < 10) display.print(F("0"));
    display.print(lNum);
    display.print(F(":"));

    // Veces completadas con 2 cifras (a partir de 99 se muestra 99)
    uint16_t veces = min(s.vecesCompletada, (uint16_t)99);
    if (veces < 10) display.print(F(" "));
    display.print(veces);
    display.print(F("x["));

    uint8_t bloques = (s.maxAcierto * 8) / 100;
    for (uint8_t b = 0; b < 8; b++) display.print(b < bloques ? "=" : ".");
    display.print(F("]"));

    if (s.maxAcierto < 100) display.print(F(" "));
    if (s.maxAcierto < 10) display.print(F(" "));
    display.print(s.maxAcierto);
    display.print(F("%"));
  }
}

// Ultima pagina de ESTAD.: borrar las estadisticas de ese apartado (pide confirmacion)
void dibujarPaginaBorrado(const __FlashStringHelper* que) {
  dibujarTituloCentrado(F("- MEMORIA NVS -"), 16);
  display.setCursor(0, 32);
  if (confirmandoBorrado) {
    // La fuente por defecto no es UTF-8: el signo de apertura es el 0xA8 de CP437
    display.print(F(" "));
    display.write(0xA8);
    display.print(F("CONFIRMAR BORRADO?"));
  } else {
    display.print(F(" BORRAR ESTAD. "));
    display.print(que);
  }
  dibujarPieCentrado(confirmandoBorrado ? "> confirmar" : "> borrar");
}

// Ajustes de TX LIBRE (lista): 0 wpm (se edita girando), 1 modo, 2 palas, 3 salir; pie: 4 volver
#define N_AJUSTES_KEYER 4
const char* const NOMBRES_AJUSTES_KEYER[N_AJUSTES_KEYER] = { "WPM", "MODO", "PALAS", "SALIR" };

String valorAjusteKeyer(uint8_t i) {
  switch (i) {
    case 0: return String(keyerWpm);
    case 1: return keyerModoB ? String("B") : String("A");
    case 2: return keyerPalasInvertidas ? String("INV") : String("NORM");
    default: return String("");
  }
}

// Nombre de una pagina de ESTAD. (Koch o Echo) para el pie: "ERRORES", "TOTALES", "L01-03"... "L40", "BORRAR"
String nombrePaginaEstad(uint8_t pagina) {
  if (pagina == 0) return "ERRORES";
  if (pagina == 1) return "TOTALES";
  uint8_t primera = (pagina - PAG_STATS_PRIMERA_LECCION) * 3 + 1;
  if (primera > 40) return "BORRAR";
  char t[10];
  if (primera + 2 <= 40) snprintf(t, sizeof(t), "L%02u-%02u", primera, primera + 2);
  else snprintf(t, sizeof(t), "L%02u", primera);
  return String(t);
}

// REFUERZO: NO, SUAVE o FUERTE; "S/D" (sin datos) si ninguna letra de la leccion tiene intentos suficientes
String textoRefuerzo(uint8_t refuerzo, uint8_t leccion) {
  if (refuerzo == REFUERZO_NO) return String("NO");
  String t = (refuerzo == REFUERZO_FUERTE) ? "FUERTE" : "SUAVE";
  return hayDatosRefuerzo(leccion) ? t : t + " S/D";
}

// NO -> SUAVE -> FUERTE -> NO (se cambia pulsando)
uint8_t siguienteRefuerzo(uint8_t refuerzo) {
  return (refuerzo + 1) % (REFUERZO_FUERTE + 1);
}

// Ajustes del Koch (lista): 0-4 se editan girando, 5 refuerzo (pulsar), 6 abre ESTAD.; pie: 7 iniciar, 8 volver
#define N_AJUSTES_KOCH 7
const char* const NOMBRES_AJUSTES_KOCH[N_AJUSTES_KOCH] = {
  "WPM", "ESPACIADO", "LECCION", "CARACTERES", "GRUPOS", "REFUERZO", "ESTADISTICAS"
};

String valorAjusteKoch(uint8_t i) {
  switch (i) {
    case 0: return String(wpmCW);
    case 1: return String(espaciadoCW) + "%";
    case 2: return String(leccionCW);
    case 3: return String(caracteresCW);
    case 4: return String(gruposCW);
    case 5: return textoRefuerzo(refuerzoKoch, leccionCW);
    default: return String("");
  }
}

// Ajustes del Echo (lista): 0-3 y 5 se editan girando, 4 cambia VER, 6 refuerzo (pulsar), 7 abre ESTAD.;
// pie: 8 iniciar, 9 volver
#define N_AJUSTES_ECHO 8
const char* const NOMBRES_AJUSTES_ECHO[N_AJUSTES_ECHO] = {
  "WPM", "ESPACIADO", "LECCION", "LETRAS", "VER LETRA", "RETOS", "REFUERZO", "ESTADISTICAS"
};

String valorAjusteEcho(uint8_t i) {
  switch (i) {
    case 0: return String(echoWpm);
    case 1: return String(echoEspaciado) + "%";
    case 2: return String(echoLeccion);
    case 3: return String(echoLargo);
    case 4: return echoVerObjetivo ? String("SI") : String("NO");
    case 5: return String(echoRetos);
    case 6: return textoRefuerzo(echoRefuerzo, echoLeccion);
    default: return String("");
  }
}

void renderizarPantallaCW() {
  display.setTextColor(SSD1306_WHITE);

  if (enKeyer && keyerEnAjustes) {
    // Ajustes de TX LIBRE (lista como CONF > CONFIG). Las palas siguen manipulando
    display.setTextSize(1);
    dibujarListaOpciones(NOMBRES_AJUSTES_KEYER, N_AJUSTES_KEYER, opcionAjustesKeyer, valorAjusteKeyer, 0x1);
    dibujarPieCentrado(opcionAjustesKeyer == N_AJUSTES_KEYER ? "> volver" : "  volver");

  } else if (enKeyer) {
    display.setTextSize(1);

    // Ajustes a la vista (se cambian en "menu"): "20WPM  MODO A  NORM"
    display.setCursor(0, 12);
    display.print(String(keyerWpm) + "WPM  " + (keyerModoB ? "MODO B" : "MODO A") + "  " +
                  (keyerPalasInvertidas ? "INV" : "NORM"));

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

    dibujarPieDos(opcionKeyer == 0, "borrar", opcionKeyer == 1, "menu");

  } else if (enEcho && enEstadisticasEcho) {
    display.setTextSize(1);
    if (paginaStatsEcho == 0) dibujarMasErrores(statsCarEcho);
    else if (paginaStatsEcho == 1) dibujarTotalesEcho();
    else if (paginaStatsEcho < TOTAL_PAGINAS_STATS_ECHO - 1) dibujarPaginaLecciones(statsEchoLecc, paginaStatsEcho - 2);
    else dibujarPaginaBorrado(F("ECHO"));
    if (paginaStatsEcho < TOTAL_PAGINAS_STATS_ECHO - 1) {
      dibujarPiePaginas(paginaStatsEcho, TOTAL_PAGINAS_STATS_ECHO, nombrePaginaEstad(paginaStatsEcho));
    }

  } else if (enEcho && !echoPracticando) {
    // Ajustes del Echo, con la disposicion de los del Koch
    display.setTextSize(1);
    dibujarListaOpciones(NOMBRES_AJUSTES_ECHO, N_AJUSTES_ECHO, opcionEcho, valorAjusteEcho, 0x2F);  // 0-3 y 5
    dibujarPieDos(opcionEcho == N_AJUSTES_ECHO, "iniciar", opcionEcho == N_AJUSTES_ECHO + 1, "volver");

  } else if (enEcho && estadoEcho == ECHO_FIN) {
    display.setTextSize(1);
    dibujarTituloCentrado(F("- RESULTADO ECHO -"), 12);
    display.setCursor(0, 26);
    display.print(F("ACIERTOS: ")); display.print(textoPorcentaje(echoAciertos, echoIntentos));
    display.setCursor(0, 38);
    display.print(F("BIEN: ")); display.print(echoAciertos);
    display.print(F(" DE ")); display.print(echoIntentos);
    dibujarPieCentrado((opcionPracticaEcho == 0 ? "> " : "  ") + String("volver  ") +
                       (opcionPracticaEcho == 1 ? "> " : "  ") + String("rep."));

  } else if (enEcho) {
    // Practica: reto n de echoRetos y aciertos
    display.setTextSize(1);
    uint16_t numReto = min((uint16_t)(echoIntentos + (estadoEcho == ECHO_RESULTADO ? 0 : 1)), (uint16_t)echoRetos);
    display.setCursor(0, 12);
    display.print(F("RETO ")); display.print(numReto); display.print('/'); display.print(echoRetos);

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

    // Aciertos, a la derecha de "RETO n/N"
    String marcador = "BIEN " + String(echoAciertos);
    display.getTextBounds(marcador, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(128 - w, 12);
    display.print(marcador);

    // Estado
    display.setCursor(0, 42);
    if (estadoEcho == ECHO_REPRODUCIENDO) {
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

    // "repetir" vuelve a emitir el reto y no cuenta como intento
    dibujarPieCentrado((opcionPracticaEcho == 0 ? "> " : "  ") + String("repetir   ") +
                       (opcionPracticaEcho == 1 ? "> " : "  ") + String("salir"));

  } else if (!enSubpantallaCW && !enPractica3_2 && !enEstadisticas3_5) {
    // Mismo estilo que la pantalla LoRa: titulo en negativo y tres opciones
    display.setTextSize(1);
    dibujarTituloMenu(" ENTRENADOR CW ");

    const char* opciones[] = { "< KOCH (RX) >", "< ECHO (TX) >", "< TX LIBRE >" };
    for (uint8_t i = 0; i < 3; i++) dibujarOpcionCentrada(opcionPantalla3 == i, opciones[i], 24 + i * 9);

    dibujarBarraAvanzarRetroceder(opcionPantalla3 == OPCION_BARRA_CW);

  } else if (enSubpantallaCW && !enPractica3_2 && !enEstadisticas3_5) {
    // Ajustes del Koch: lista como CONF > CONFIG; "iniciar" y "volver" en el pie
    display.setTextSize(1);
    dibujarListaOpciones(NOMBRES_AJUSTES_KOCH, N_AJUSTES_KOCH, opcionSubpantalla, valorAjusteKoch, 0x1F);
    dibujarPieDos(opcionSubpantalla == N_AJUSTES_KOCH, "iniciar", opcionSubpantalla == N_AJUSTES_KOCH + 1, "volver");

  } else if (enEstadisticas3_5) {
    display.setTextSize(1);

    if (paginaStats3_5 < PAG_STATS_PRIMERA_LECCION) {
      if (paginaStats3_5 == 0) dibujarMasErrores(statsCarKoch);
      else dibujarTotalesKoch();
      dibujarPiePaginas(paginaStats3_5, TOTAL_PAGINAS_STATS, nombrePaginaEstad(paginaStats3_5));

    } else if (paginaStats3_5 < PAG_STATS_BORRADO) {
      dibujarPaginaLecciones(statsKoch, paginaStats3_5 - PAG_STATS_PRIMERA_LECCION);
      dibujarPiePaginas(paginaStats3_5, TOTAL_PAGINAS_STATS, nombrePaginaEstad(paginaStats3_5));

    } else {
      dibujarPaginaBorrado(F("KOCH"));  // Solo estadisticas del Koch: el resto, en CONF (fabrica)
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
        dibujarPieCentrado(String(paginaListado3_3 + 1) + "/" + String(paginas) + " GRUPOS   > mas");
      } else {
        // Corregir marcando cada fallo (x letra) o solo con el numero de fallos (total)
        String txtPie = (opcionFinListado3_3 == 0 ? ">" : " ") + String("x letra ") +
                        (opcionFinListado3_3 == 1 ? ">" : " ") + String("total ") +
                        (opcionFinListado3_3 == 2 ? ">" : " ") + String("rep.");
        dibujarPieCentrado(txtPie);
      }

    } else if (estadoCW == CW_PANTALLA_FALLOS_TOTALES) {
      dibujarTituloCentrado(F("- FALLOS TOTALES -"));
      display.setCursor(0, 25);
      display.print(focoFallosTotales == 0 ? ">" : " ");
      display.print(F("FALLOS: "));
      imprimirValor(focoFallosTotales == 0, String(fallosTotales));
      display.print(F(" / "));
      display.print(totalCaracteresLeccion);
      display.setCursor(0, 38);
      display.print(F(" ACIERTO: "));
      display.print(textoPorcentaje(totalCaracteresLeccion - fallosTotales, totalCaracteresLeccion));
      dibujarPieCentrado((focoFallosTotales == 1 ? "> " : "  ") + String("rep.   ") +
                         (focoFallosTotales == 2 ? "> " : "  ") + String("ok"));

    } else if (estadoCW == CW_PANTALLA_CORREGIR_ERRORES) {
      // Listado con los fallos marcados en video inverso y el cursor subrayado. Lineas cada 9 px
      // para que el subrayado quepa entre ellas
      uint16_t largo = textoCompletoLeccion.length();
      String titulo = "FALLOS: " + String(numeroErrores) + "/" + String(totalCaracteresLeccion);
      int16_t x1, y1;
      uint16_t w, h;
      display.getTextBounds(titulo, 0, 0, &x1, &y1, &w, &h);
      display.setCursor((128 - w) / 2, 11);
      display.print(titulo);

      for (uint8_t k = 0; k < CARACTERES_POR_PAGINA_LISTADO; k++) {
        uint16_t i = paginaCorregir * CARACTERES_POR_PAGINA_LISTADO + k;
        if (i >= largo) break;
        int16_t x = (k % 21) * 6;
        int16_t y = 21 + (k / 21) * 9;
        display.setCursor(x, y);
        if (marcasFallo[i]) display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.print(textoCompletoLeccion[i]);
        display.setTextColor(SSD1306_WHITE);
        if (i == focoCorregir) display.drawFastHLine(x, y + 8, 5, SSD1306_WHITE);
      }

      dibujarPieCentrado((focoCorregir == largo ? "> " : "  ") + String("rep.   ") +
                         (focoCorregir == largo + 1 ? "> " : "  ") + String("ok"));

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

/**
 * Dibuja las ultimas lineas de un texto (21 caracteres por linea). Al llenarse se quitan lineas enteras por
 * arriba, para que el texto no se desplace caracter a caracter
 */
void dibujarUltimasLineas(String t, int16_t y, uint8_t lineas, uint8_t alto) {
  const int ANCHO = 21;
  if (t.length() > (unsigned)(lineas * ANCHO)) {
    int sobran = t.length() - lineas * ANCHO;
    t = t.substring(((sobran + ANCHO - 1) / ANCHO) * ANCHO);
  }
  for (int linea = 0; linea < lineas && linea * ANCHO < (int)t.length(); linea++) {
    display.setCursor(0, y + linea * alto);
    display.print(t.substring(linea * ANCHO, min((int)t.length(), (linea + 1) * ANCHO)));
  }
}

// Ajustes ("menu") de PRACTICA DUO y de QSO LIBRE
#define N_AJUSTES_DUO 4
const char* const NOMBRES_AJUSTES_DUO[N_AJUSTES_DUO] = { "WPM", "ESPACIADO", "LECCION", "SALIR" };
#define N_AJUSTES_QSO_LORA 3
const char* const NOMBRES_AJUSTES_QSO_LORA[N_AJUSTES_QSO_LORA] = { "WPM", "VER LO RECIBIDO", "SALIR" };

String valorAjusteDuo(uint8_t i) {
  switch (i) {
    case 0: return String(wpmCW);
    case 1: return String(espaciadoCW) + "%";
    case 2: return String(leccionCW);
    default: return String("");
  }
}

String valorAjusteQsoLora(uint8_t i) {
  switch (i) {
    case 0: return String(keyerWpm);
    case 1: return qsoVerRx ? String("SI") : String("NO");
    default: return String("");
  }
}

void renderizarPantallaLora() {
  display.setTextColor(SSD1306_WHITE);

  if (enPantalla4_Sub && loraEnAjustes) {
    display.setTextSize(1);
    bool esDuo = (subpantalla4Activa == 1);
    uint8_t n = esDuo ? N_AJUSTES_DUO : N_AJUSTES_QSO_LORA;
    if (esDuo) dibujarListaOpciones(NOMBRES_AJUSTES_DUO, n, opcionAjustesLora, valorAjusteDuo, 0x7);
    else dibujarListaOpciones(NOMBRES_AJUSTES_QSO_LORA, n, opcionAjustesLora, valorAjusteQsoLora, 0x1);
    dibujarPieCentrado(opcionAjustesLora == n ? "> volver" : "  volver");

  } else if (!enPantalla4_Sub) {
    display.setTextSize(1);
    dibujarTituloMenu(" TRANSCEPTOR LORA ");
    // Mismas posiciones que el menu de CONF
    dibujarOpcionCentrada(opcionPantalla4 == 0, "< PRACTICA DUO >", 27);
    dibujarOpcionCentrada(opcionPantalla4 == 1, "< QSO LIBRE >", 37);

    dibujarBarraAvanzarRetroceder(opcionPantalla4 == 2);

  } else if (subpantalla4Activa == 1) {
    display.setTextSize(1);

    // La cabecera ya da el wpm y el espaciado: aqui, la leccion y las letras por grupo (se cambian en "menu")
    display.setCursor(0, 11);
    display.print("LECCION " + String(leccionCW) + "  " + String(caracteresCW) + " LETRAS");  // 20 como mucho

    display.setCursor(0, 21);
    display.print(F("TX -> "));
    if (grupoDuoTransmitido.length() > 0) {
      display.print(F("[")); display.print(grupoDuoTransmitido); display.print(F("]"));
    } else {
      display.print(F("--"));
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

    // Pie: "> enviar   ver   menu" (21 caracteres); "ver" solo si hay un grupo recibido sin revelar
    String txtPie4_1 = (opcionDuo4_1 == 0 ? "> " : "  ") + String("enviar ");
    if (hayGrupoDuoPorRevelar()) txtPie4_1 += (opcionDuo4_1 == 1 ? "> " : "  ") + String("ver ");
    txtPie4_1 += (opcionDuo4_1 == 2 ? "> " : "  ") + String("menu");
    dibujarPieCentrado(txtPie4_1);

  } else if (subpantalla4Activa == 2) {
    // QSO: recibido arriba (2 lineas), separador con el indicativo y el RSSI, manipulado abajo (2 lineas)
    display.setTextSize(1);

    String rx = qsoTextoRx;
    if (qsoSonando && !qsoVerRx) {
      String oculto = "";
      for (uint16_t i = 0; i < qsoPalabraSonando.length(); i++) oculto += '?';
      qsoAnadir(rx, oculto);
    }
    dibujarUltimasLineas(rx, 11, 2, 8);

    display.drawFastHLine(0, 31, 128, SSD1306_WHITE);
    String etiqueta = qsoIndicativoRx[0] ? String(qsoIndicativoRx) : String("QSO LORA");
    if (qsoIndicativoRx[0] && rssiUltimo != 0) etiqueta += " " + String(rssiUltimo) + "dBm";
    etiqueta = " " + etiqueta + " ";
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(etiqueta, 0, 0, &x1, &y1, &w, &h);
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);  // El fondo negro tapa la linea
    display.setCursor((128 - w) / 2, 28);
    display.print(etiqueta);

    dibujarUltimasLineas(qsoTextoTx + "_", 37, 2, 8);

    dibujarPieDos(opcionQSO == 0, "borrar", opcionQSO == 1, "menu");
  }
}

// Estado del decodificador en una linea: "18WPM 700Hz" y la barra de nivel con la marca del umbral a la derecha
// (o "SATURA" si la entrada llega a los topes del ADC: bajar RV2). Con fondo negro, para ir sobre una linea
void dibujarEstadoDecod(int16_t y) {
  String txt = (decodWpm > 0 ? String(decodWpm) : String("--")) + "WPM " + String(decodFrecHz) + "Hz";
  if (trxTonoDecodHz == 0) txt += "A";  // Automatico
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
  display.setCursor(0, y);
  display.print(txt + " ");
  const int16_t xb = 86, ancho = 42;
  display.fillRect(xb - 2, y - 1, ancho + 2, 9, SSD1306_BLACK);
  if (decodSaturado) {
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor(xb + 3, y);
    display.print(F("SATURA"));
  } else {
    display.drawRect(xb, y, ancho, 7, SSD1306_WHITE);
    display.fillRect(xb + 1, y + 1, (ancho - 2) * decodNivelPct / 100, 5, SSD1306_WHITE);
    int16_t xu = xb + 1 + (ancho - 2) * decodUmbralPct / 100;
    display.drawFastVLine(xu, y - 1, 9, decodTono ? SSD1306_BLACK : SSD1306_WHITE);
  }
  display.setTextColor(SSD1306_WHITE);
}

// Lista de memorias (4 a la vista) y "volver": para editar (MEMORIAS) o para enviar (con {MI}/{SU} sustituidos)
void dibujarListaMemorias(bool paraEnviar) {
  dibujarTituloCentrado(paraEnviar ? F("- ENVIAR MEMORIA -") : F("- MEMORIAS -"));
  uint8_t primera = trxMemoriaSel < TRX_NUM_MEMORIAS ? max(0, (int)trxMemoriaSel - 3) : TRX_NUM_MEMORIAS - 4;
  for (uint8_t k = 0; k < 4; k++) {
    uint8_t i = primera + k;
    String t = paraEnviar ? trxExpandir(trxMemorias[i]) : String(trxMemorias[i]);
    if (t.length() == 0) t = "(vacia)";
    display.setCursor(0, 20 + k * 8);
    display.print(trxMemoriaSel == i ? '>' : ' ');
    display.print(i + 1);
    display.print(' ');
    display.print(t.substring(0, 18));
  }
  dibujarPieCentrado(trxMemoriaSel == TRX_NUM_MEMORIAS ? "> volver" : "  volver");
}

// Editor: titulo, texto con cursor y, en el pie, la rueda de caracteres (el elegido en video inverso)
void dibujarEditor() {
  String titulo = editorDestino == EDITOR_ID_ESTACION ? String("TU ID") :
                  editorDestino == EDITOR_LOCATOR ? String("LOCATOR") :
                  editorDestino < 0 ? String("INDICATIVO SU") : "MEMORIA " + String(editorDestino + 1);
  if (editorError) titulo += " NO VALIDO";
  display.setCursor(0, 11);
  display.print(titulo);
  String cuenta = String(editorTexto.length()) + "/" + String(editorMax);
  display.setCursor(128 - cuenta.length() * 6, 11);
  display.print(cuenta);
  dibujarUltimasLineas(editorTexto + "_", 21, 3, 9);

  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  String piezas[5];
  int ancho = 0;
  for (int8_t k = -2; k <= 2; k++) {
    piezas[k + 2] = editorElemento((editorSel + EDITOR_TAM_RUEDA + k) % EDITOR_TAM_RUEDA);
    ancho += piezas[k + 2].length() * 6 + (k < 2 ? 6 : 0);
  }
  display.setCursor((128 - ancho) / 2, 55);
  for (uint8_t k = 0; k < 5; k++) {
    if (k == 2) display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.print(piezas[k]);
    display.setTextColor(SSD1306_WHITE);
    if (k < 4) display.print(' ');
  }
}

// Pie de OPERAR y QSO: "> mem   > tune   > menu" (centrado); foco 0..2. "tune" en negativo mientras emite
void dibujarPieTrx(uint8_t foco) {
  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  const char* opciones[] = { "mem", "tune", "menu" };
  display.setCursor(1, 55);  // "> mem    tune    menu": 21 caracteres
  for (uint8_t i = 0; i < 3; i++) {
    if (i > 0) display.print(F("  "));
    display.print(foco == i ? F("> ") : F("  "));
    if (i == 1 && trxSintonizando) display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.print(opciones[i]);
    display.setTextColor(SSD1306_WHITE);
  }
}

// Ajustes de DECODIFICAR ("menu"): 0 tono (se edita girando), 1 salir
#define N_AJUSTES_DECOD 2
const char* const NOMBRES_AJUSTES_DECOD[N_AJUSTES_DECOD] = { "TONO", "SALIR" };

String valorAjusteDecod(uint8_t i) {
  if (i != 0) return String("");
  return trxTonoDecodHz == 0 ? String("AUTO") : String(trxTonoDecodHz) + " Hz";
}

// Ajustes de OPERAR y QSO ("menu"): 0 wpm y 2 tune se editan girando, 1 tono local, 3 SU (editor), 4 salir
#define N_AJUSTES_TRX 5
const char* const NOMBRES_AJUSTES_TRX[N_AJUSTES_TRX] = { "WPM", "TONO LOCAL", "TUNE", "SU", "SALIR" };

String valorAjusteTrx(uint8_t i) {
  switch (i) {
    case 0: return String(trxWpm);
    case 1: return trxTonoLocal ? String("SI") : String("NO");
    case 2: return String(trxTuneSeg) + " s";
    case 3: return trxSu[0] ? String(trxSu) : String("--");
    default: return String("");
  }
}

void renderizarPantallaTrx() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  switch (subTrx) {
    case TRX_MENU: {
      // Como CW y LoRa: titulo en negativo, tres opciones y "memorias" en el centro del pie
      dibujarTituloMenu(" TRANSCEPTOR ");
      const char* opciones[] = { "< DECODIFICAR >", "< OPERAR >", "< QSO >" };
      for (uint8_t i = 0; i < 3; i++) dibujarOpcionCentrada(opcionTrx == i, opciones[i], 24 + i * 9);
      dibujarBarraAvanzarRetroceder(opcionTrx == TRX_OPCION_BARRA,
                                    opcionTrx == TRX_OPCION_MEMORIAS ? ">memorias" : " memorias");
      break;
    }

    case TRX_DECOD:
      if (decodEnAjustes) {
        // TONO: AUTO o 400..1000 Hz (se edita girando)
        dibujarListaOpciones(NOMBRES_AJUSTES_DECOD, N_AJUSTES_DECOD, opcionAjustesDecod, valorAjusteDecod, 0x1);
        dibujarPieCentrado(opcionAjustesDecod == N_AJUSTES_DECOD ? "> volver" : "  volver");
      } else {
        dibujarEstadoDecod(11);
        dibujarUltimasLineas(trxTextoRx, 21, 3, 9);
        dibujarPieDos(opcionTrxSub == 0, "borrar", opcionTrxSub == 1, "menu");
      }
      break;

    case TRX_OPERAR: {
      // Ajustes a la vista (se cambian en "menu"): "20WPM TONO SI TUNE 5s" (21 caracteres; con 10 s,
      // "TUNE10s" para que no pase de una linea)
      display.setCursor(0, 11);
      display.print(String(trxWpm) + "WPM " + (trxTonoLocal ? "TONO SI" : "TONO NO") +
                    (trxTuneSeg < 10 ? " TUNE " : " TUNE") + String(trxTuneSeg) + "s");
      dibujarUltimasLineas(trxTextoTx + "_", 21, 2, 9);
      display.setCursor(0, 41);
      if (keyCortadaPorSeguridad) {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.print(F("KEY CORTADA (>10 s)"));
        display.setTextColor(SSD1306_WHITE);
      } else {
        display.print(F("SU: "));
        display.print(trxSu[0] ? trxSu : "--");
      }
      dibujarPieTrx(opcionTrxSub);
      break;
    }

    case TRX_AJUSTES:
      dibujarListaOpciones(NOMBRES_AJUSTES_TRX, N_AJUSTES_TRX, opcionAjustesTrx, valorAjusteTrx, 0x5);  // 0 y 2
      dibujarPieCentrado(opcionAjustesTrx == N_AJUSTES_TRX ? "> volver" : "  volver");
      break;

    case TRX_QSO: {
      // Recibido arriba, estado del decodificador en el separador, manipulado abajo
      dibujarUltimasLineas(trxTextoRx, 11, 2, 8);
      display.drawFastHLine(0, 31, 128, SSD1306_WHITE);
      if (keyCortadaPorSeguridad) {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.setCursor(10, 28);
        display.print(F(" KEY CORTADA >10s "));
        display.setTextColor(SSD1306_WHITE);
      } else {
        dibujarEstadoDecod(28);
      }
      dibujarUltimasLineas(trxTextoTx + "_", 37, 2, 8);
      dibujarPieTrx(opcionTrxSub);
      break;
    }

    case TRX_MEMORIAS:    dibujarListaMemorias(false); break;
    case TRX_LISTA_ENVIO: dibujarListaMemorias(true);  break;
    case TRX_EDITOR:      dibujarEditor();             break;
  }
}

// ----------------------------------------------------------------------------
// PROP, POTA y SOL: menu (como CONF), paginas que se pasan girando y vista del portal
// ----------------------------------------------------------------------------
// Estado del WiFi para la linea de estado del menu; "" si no hay nada que decir. Con una version nueva del
// firmware en GitHub, se avisa aqui (se instala en CONF > CONFIG > ACTUALIZAR FW)
String textoEstadoDatosWifi() {
  if (actualizandoWifi) return "ACTUALIZANDO...";
  if (hayActualizacion() && pantallaActual != PANTALLA_SOL) return "FW NUEVO: v" + otaVersionNueva;
  switch (estadoWifi) {
    case WIFI_CONECTANDO:   return "CONECTANDO WIFI...";
    case WIFI_PORTAL:       return "PORTAL ABIERTO";
    case WIFI_SIN_CONEXION: return "SIN CONEXION WIFI";
    default:                return "";
  }
}

/**
 * Menu de PROP, POTA o SOL: titulo, dos opciones (sin conexion: ABRIR WEBCONF y REINTENTAR), una linea de
 * estado y la barra ant./sig. Foco: 0 y 1 = opciones, 2 = barra
 */
void dibujarMenuDatos(const char* titulo, uint8_t opcion, const char* op0, const char* op1, const String& estado) {
  dibujarTituloMenu(titulo);
  bool sinConexion = (estadoWifi == WIFI_SIN_CONEXION);
  dibujarOpcionCentrada(opcion == 0, sinConexion ? "< ABRIR WEBCONF >" : op0, 25);
  dibujarOpcionCentrada(opcion == 1, sinConexion ? "< REINTENTAR >" : op1, 34);
  if (estado.length() > 0) dibujarTituloCentrado(estado, 43);
  dibujarBarraAvanzarRetroceder(opcion == OPCION_BARRA_DATOS);
}

// Cuerpo de una pagina sin datos: el estado del WiFi o el aviso que toque
void dibujarSinDatos(const String& aviso) {
  display.setCursor(0, 22);
  display.print(aviso);
}

const char* const NOMBRES_PAGINAS_PROP[2] = { "INDICES", "BANDAS" };

void renderizarPantallaPropagacion() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  String estado = textoEstadoDatosWifi();
  if (estado.length() == 0) {
    if (!propagacion.validos) {
      estado = "SIN DATOS";
    } else {
      // "08 Oct 2026 1205 GMT" -> "HAMQSL 1205 GMT"
      String act = String(propagacion.actualizado);
      estado = "HAMQSL " + (act.length() > 8 ? act.substring(act.length() - 8) : act);
    }
  }

  if (subDatos == DATOS_PORTAL) {
    dibujarVistaPortal(focoPortalDatos);
    return;
  }
  if (subDatos == DATOS_MENU) {
    dibujarMenuDatos(" PROPAGACION ", opcionPantallaProp, "< VER DATOS >", "< ACTUALIZAR >", estado);
    return;
  }

  // Paginas
  if (actualizandoWifi || !propagacion.validos) {
    dibujarSinDatos(estado);
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

    display.setCursor(0, 32);
    display.print(estado);  // "HAMQSL 1205 GMT": hora del dato

    struct tm local;
    if (horaLocalValida(local)) {
      time_t ahora = time(nullptr);
      struct tm utc;
      gmtime_r(&ahora, &utc);
      char txtHora[24];
      snprintf(txtHora, sizeof(txtHora), "UTC %02d:%02d LOC %02d:%02d", utc.tm_hour, utc.tm_min, local.tm_hour, local.tm_min);
      display.setCursor(0, 42);
      display.print(txtHora);
    }
  }
  dibujarPiePaginas(paginaPropagacion, 2, NOMBRES_PAGINAS_PROP[paginaPropagacion]);
}

uint8_t paginasSpots() {
  return max(1, ((int)numSpotsPota + SPOTS_POR_PAGINA - 1) / SPOTS_POR_PAGINA);
}

void renderizarPantallaSpots() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  uint8_t paginas = paginasSpots();
  if (paginaSpots >= paginas) paginaSpots = 0;  // Han llegado menos spots

  String estado = textoEstadoDatosWifi();
  if (estado.length() == 0) {
    if (!spotsPotaValidos) estado = "SIN DATOS";
    else if (numSpotsPota == 0) estado = "NO HAY SPOTS CW";
    else estado = String(numSpotsPota) + " SPOTS CW";
  }

  if (subDatos == DATOS_PORTAL) {
    dibujarVistaPortal(focoPortalDatos);
    return;
  }
  if (subDatos == DATOS_MENU) {
    dibujarMenuDatos(" SPOTS POTA ", opcionPantallaSpots, "< VER SPOTS >", "< ACTUALIZAR >", estado);
    return;
  }

  // Paginas
  if (actualizandoWifi || !spotsPotaValidos || numSpotsPota == 0) {
    dibujarSinDatos(estado);
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
  dibujarPiePaginas(paginaSpots, paginas, "SPOTS");
}

// ============================================================================
// PANTALLA SOL: SOL Y LINEA GRIS (CALCULO LOCAL, SIN WIFI)
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

// Salida, puesta y ventanas de linea gris del dia de "instante", en hora local
void dibujarDiaSol(time_t instante, float lat, float lon, int difLocal) {
  struct tm utc;
  gmtime_r(&instante, &utc);
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

  // Fecha del dia (local), para distinguir HOY de MANANA
  time_t t = instante + difLocal * 60;
  struct tm f;
  gmtime_r(&t, &f);
  char linea[40];
  snprintf(linea, sizeof(linea), "%02d/%02d/%04d HORA LOCAL", f.tm_mday, f.tm_mon + 1, f.tm_year + 1900);
  display.setCursor(0, 42);
  display.print(linea);
}

const char* const NOMBRES_PAGINAS_SOL[3] = { "HOY", "AHORA", "MA\xA4" "ANA" };  // 0xA4 = enye en CP437

// SOL: "actualizar" sincroniza la hora por NTP (enciende el WiFi si hace falta)
void actualizarHoraSol() {
  if (estadoWifi == WIFI_CONECTADO) configTzTime(ZONA_HORARIA, "pool.ntp.org", "time.cloudflare.com");
  else if (estadoWifi == WIFI_APAGADO) encenderWifi();
}

void renderizarPantallaSol() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  float lat, lon;
  struct tm local;
  bool conHora = horaLocalValida(local);
  bool locatorOk = locatorALatLon(locatorEstacion, lat, lon);

  // Linea de estado: el WiFi si se esta usando; si no, la hora o el locator
  String estado = textoEstadoDatosWifi();
  if (estado.length() == 0) {
    if (!locatorOk) estado = "LOCATOR NO VALIDO";
    else if (!conHora && estadoWifi == WIFI_CONECTADO) estado = "SINCRONIZANDO HORA...";
    else if (!conHora) estado = "SIN HORA";
    else estado = "LOCATOR " + String(locatorEstacion);
  }

  if (subDatos == DATOS_PORTAL) {
    dibujarVistaPortal(focoPortalDatos);
    return;
  }
  if (subDatos == DATOS_MENU) {
    dibujarMenuDatos(" SOL Y LINEA GRIS ", opcionPantallaSol, "< VER DATOS >", "< ACTUALIZAR >", estado);
    return;
  }

  // Paginas
  if (!locatorOk) {
    dibujarSinDatos("LOCATOR NO VALIDO");
    display.setCursor(0, 32);
    display.print(F("CAMBIALO EN CONFIG"));
  } else if (!conHora) {
    dibujarSinDatos(estado);
    if (estadoWifi != WIFI_CONECTADO && estadoWifi != WIFI_CONECTANDO) {
      display.setCursor(0, 32);
      display.print(F("PONLA EN WEBCONF"));
    }
  } else {
    time_t ahora = time(nullptr);
    struct tm utc;
    gmtime_r(&ahora, &utc);
    int difLocal = (local.tm_hour * 60 + local.tm_min) - (utc.tm_hour * 60 + utc.tm_min);
    if (difLocal > 720) difLocal -= 1440;
    if (difLocal < -720) difLocal += 1440;

    if (paginaSol == 1) {
      // AHORA: altura del sol, linea gris, hora y locator
      DatosSol d = calcularSol(utc, lat, lon);
      bool enGris = d.elevacion >= GRIS_ELEV_MIN && d.elevacion <= GRIS_ELEV_MAX;
      char linea[24];
      snprintf(linea, sizeof(linea), "SOL %+.1f GRADOS", d.elevacion);
      display.setCursor(0, 12);
      display.print(linea);
      display.setCursor(0, 22);
      display.print(enGris ? F("EN LINEA GRIS") : F("FUERA DE LINEA GRIS"));
      snprintf(linea, sizeof(linea), "UTC %02d:%02d LOC %02d:%02d", utc.tm_hour, utc.tm_min, local.tm_hour, local.tm_min);
      display.setCursor(0, 32);
      display.print(linea);
      display.setCursor(0, 42);
      display.print(F("LOCATOR ")); display.print(locatorEstacion);
    } else {
      // HOY o MANANA (el mismo instante, un dia despues)
      dibujarDiaSol(ahora + (paginaSol == 2 ? 86400 : 0), lat, lon, difLocal);
    }
  }
  dibujarPiePaginas(paginaSol, 3, NOMBRES_PAGINAS_SOL[paginaSol]);
}

// ============================================================================
// PANTALLA CONF: CONFIGURACION, STATUS Y ACTUALIZACION
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

// SSID de la red guardada, leido de la NVS del WiFi sin encender la radio (esp_wifi_get_config necesita el
// WiFi iniciado). El bloque "sta.ssid" lleva la longitud delante (4 bytes en el IDF 4.4; 1 en otras
// versiones). Vacio si no hay red guardada
String ssidGuardada() {
  // API de NVS de ESP-IDF directamente: sin red guardada no imprime errores (Preferences si)
  nvs_handle_t h;
  if (nvs_open("nvs.net80211", NVS_READONLY, &h) != ESP_OK) return String();
  uint8_t buf[40] = {0};
  size_t n = sizeof(buf);
  esp_err_t err = nvs_get_blob(h, "sta.ssid", buf, &n);
  nvs_close(h);
  if (err != ESP_OK || n == 0) return String();
  uint8_t cab = (n >= 36) ? 4 : 1;
  uint32_t largo = (cab == 4) ? (buf[0] | (buf[1] << 8) | (buf[2] << 16) | ((uint32_t)buf[3] << 24)) : buf[0];
  if (largo > 32 || largo > n - cab) return String();
  char ssid[33];
  memcpy(ssid, buf + cab, largo);
  ssid[largo] = '\0';
  return String(ssid);
}

// Motivo del ultimo arranque, en 11 caracteres como mucho
String textoMotivoReinicio() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "ENCENDIDO";
    case ESP_RST_EXT:       return "PIN RST";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "ERROR";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "DESPERTAR";
    case ESP_RST_BROWNOUT:  return "BAJA TENS.";
    default:                return "OTRO (" + String((int)esp_reset_reason()) + ")";
  }
}

// Tiempo desde el arranque: "03:15:07" o "2d 03:15:07"
String textoTiempoEncendido() {
  uint32_t s = millis() / 1000;
  char t[16];
  if (s >= 86400) snprintf(t, sizeof(t), "%lud %02lu:%02lu:%02lu", (unsigned long)(s / 86400),
                           (unsigned long)(s / 3600 % 24), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
  else snprintf(t, sizeof(t), "%02lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
                (unsigned long)(s % 60));
  return String(t);
}

String textoEstadoWifi() {
  switch (estadoWifi) {
    case WIFI_CONECTANDO:   return "CONECTANDO";
    case WIFI_PORTAL:       return "PORTAL ABIERTO";
    case WIFI_CONECTADO:    return "CONECTADO";
    case WIFI_SIN_CONEXION: return "SIN CONEXION";
    default:                return "APAGADO";
  }
}

const char* const NOMBRES_PAGINAS_STATUS[N_PAGINAS_STATUS] = { "ESTACION", "SISTEMA", "WIFI", "MEMORIA", "ARRANQUE" };

// STATUS: cuatro lineas por pagina (21 caracteres) y, en el pie, la pagina y "volver"
void dibujarPaginaStatus() {
  char l[4][40];  // 21 caracteres a la vista; holgura para el compilador
  for (uint8_t i = 0; i < 4; i++) l[i][0] = '\0';
  switch (paginaStatus) {
    case 0: {  // Estacion
      snprintf(l[0], sizeof(l[0]), "ID      %s", indicativoEstacion);
      snprintf(l[1], sizeof(l[1]), "LOCATOR %s", locatorEstacion);
      struct tm t;
      if (!horaLocalValida(t)) {
        snprintf(l[2], sizeof(l[2]), "HORA    SIN HORA");
      } else {
        if (horaUtcCabecera) {
          time_t ahora = time(nullptr);
          gmtime_r(&ahora, &t);
        }
        snprintf(l[2], sizeof(l[2]), "HORA %02d:%02d%s %02d/%02d/%02d", t.tm_hour, t.tm_min, horaUtcCabecera ? "Z" : "",
                 t.tm_mday, t.tm_mon + 1, t.tm_year % 100);
      }
      snprintf(l[3], sizeof(l[3]), "FW      v%s", FW_VERSION);
      break;
    }
    case 1:  // Sistema
      if (bateriaActual.estado == SOLO_USB) snprintf(l[0], sizeof(l[0]), "BAT  SIN BATERIA");
      else snprintf(l[0], sizeof(l[0]), "BAT  %.2fV  %d%%", bateriaActual.voltaje, bateriaActual.porcentaje);
      snprintf(l[1], sizeof(l[1]), "LORA %s", estadoLoraStr.c_str());
      snprintf(l[2], sizeof(l[2]), "APAGADO %s", textoApagado().c_str());
      snprintf(l[3], sizeof(l[3]), "TONO %uHz  ACEL %s", frecTonoHz, textoAceleracion().c_str());
      break;
    case 2: {  // WiFi
      String red = ssidGuardada();
      if (red.length() == 0) red = "(ninguna)";
      snprintf(l[0], sizeof(l[0]), "RED  %.16s", red.c_str());
      snprintf(l[1], sizeof(l[1]), "WIFI %s", textoEstadoWifi().c_str());
      if (estadoWifi == WIFI_CONECTADO) snprintf(l[2], sizeof(l[2]), "IP   %s", WiFi.localIP().toString().c_str());
      else if (estadoWifi == WIFI_PORTAL) snprintf(l[2], sizeof(l[2]), "IP   192.168.4.1");
      else snprintf(l[2], sizeof(l[2]), "IP   --");
      uint8_t mac[6];
      esp_read_mac(mac, ESP_MAC_WIFI_STA);  // La misma MAC que da el nombre del portal
      snprintf(l[3], sizeof(l[3]), "AP   Chispas-%02X%02X", mac[4], mac[5]);
      break;
    }
    case 3: {  // Memoria
      nvs_stats_t nvs;
      bool hayNvs = nvs_get_stats(NULL, &nvs) == ESP_OK;
      snprintf(l[0], sizeof(l[0]), "HEAP LIBRE %u KB", ESP.getFreeHeap() / 1024);
      snprintf(l[1], sizeof(l[1]), "HEAP MINIMO %u KB", ESP.getMinFreeHeap() / 1024);
      snprintf(l[2], sizeof(l[2]), "MAYOR BLOQUE %u KB", ESP.getMaxAllocHeap() / 1024);
      snprintf(l[3], sizeof(l[3]), "NVS %u/%u", hayNvs ? nvs.used_entries : 0, hayNvs ? nvs.total_entries : 0);
      break;
    }
    default:  // Arranque
      snprintf(l[0], sizeof(l[0]), "ENCENDIDO %s", textoTiempoEncendido().c_str());
      snprintf(l[1], sizeof(l[1]), "REINICIO  %s", textoMotivoReinicio().c_str());
      snprintf(l[2], sizeof(l[2]), "CPU       %u MHz", getCpuFrequencyMhz());
      break;
  }
  for (uint8_t i = 0; i < 4; i++) {
    display.setCursor(0, 12 + i * 10);
    display.print(l[i]);
  }
  dibujarPiePaginas(paginaStatus, N_PAGINAS_STATUS, NOMBRES_PAGINAS_STATUS[paginaStatus]);
}

// Valor de cada opcion de CONFIG (a la derecha de la linea)
String valorOpcionConf(uint8_t i) {
  switch (i) {
    case OC_ID:          return String(indicativoEstacion);
    case OC_LOCATOR:     return String(locatorEstacion);
    case OC_ACELERACION: return textoAceleracion();
    case OC_APAGADO:     return textoApagado();
    case OC_TONO:        return String(frecTonoHz) + " Hz";
    case OC_WEBCONF:     return estadoWifi == WIFI_PORTAL ? String("ABIERTO") : String("");
    case OC_ACTUALIZAR:  return hayActualizacion() ? "v" + otaVersionNueva : String("");
    case OC_DEPURACION:  return depuracion != 0 ? String("SI") : String("NO");
    case OC_FABRICA:    return confirmandoFabrica ? String("OTRA VEZ") : String("");
    default:             return String("");
  }
}

const char* const NOMBRES_OPCIONES_CONF[N_OPCIONES_CONF - 1] = {
  "ID", "LOCATOR", "ACELERACION", "APAGADO", "TONO", "WEBCONF", "ACTUALIZAR FW", "DEPURACION", "FABRICA"
};

// CONFIG: lista de opciones (4 a la vista, con el valor a la derecha) y "volver" en el pie
void dibujarListaConf() {
  dibujarListaOpciones(NOMBRES_OPCIONES_CONF, N_OPCIONES_CONF - 1, focoConf, valorOpcionConf,
                       (1UL << OC_ACELERACION) | (1UL << OC_APAGADO) | (1UL << OC_TONO));
  dibujarPieCentrado(focoConf == OC_VOLVER ? "> volver" : "  volver");
}

String valorOpcionDepu(uint8_t i) {
  return depuActiva(BITS_DEPU[i]) ? String("SI") : String("NO");
}

// DEPURACION: salidas por serie para pruebas en banco (pulsar cambia SI/NO; se guarda en la NVS)
void dibujarListaDepuracion() {
  dibujarListaOpciones(NOMBRES_OPCIONES_DEPU, N_OPCIONES_DEPU, focoDepu, valorOpcionDepu, 0);
  dibujarPieCentrado(focoDepu == N_OPCIONES_DEPU ? "> volver" : "  volver");
}

// Datos del portal (red, clave, web) con "ver qr" y "volver": CONF > WEBCONF y, sin conexion, PROP, POTA y SOL
void dibujarVistaPortal(uint8_t foco) {
  if (estadoWifi == WIFI_PORTAL || estadoWifi == WIFI_CONECTANDO) {
    dibujarEstadoWifi();
  } else {
    display.setCursor(0, 22);
    display.print(F("PORTAL CERRADO"));
    display.setCursor(0, 32);
    display.print(F("WIFI ")); display.print(textoEstadoWifi());
  }
  if (estadoWifi == WIFI_PORTAL) dibujarPieDos(foco == 0, "ver qr", foco == 1, "volver");
  else dibujarPieCentrado("> volver");
}

// Eventos de la vista del portal. Devuelve true cuando hay que salir de ella
bool procesarVistaPortal(uint8_t& foco, int8_t giro, bool pulsa, bool atras) {
  if (atras) return true;
  if (giro != 0 && estadoWifi == WIFI_PORTAL) foco = (foco + 1) % 2;
  else if (pulsa) {
    if (foco == 0 && estadoWifi == WIFI_PORTAL) vistaPortal = VISTA_QR_RED;
    else return true;
  }
  return false;
}

void dibujarPantallaOta();  // Definida justo debajo, con la logica de la actualizacion

void renderizarPantallaConfig() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  switch (subConf) {
    case CONF_MENU:
      dibujarTituloMenu(" CONFIGURACION ");
      dibujarOpcionCentrada(opcionPantallaConfig == 0, "< CONFIG >", 27);
      dibujarOpcionCentrada(opcionPantallaConfig == 1, "< STATUS >", 37);
      dibujarBarraAvanzarRetroceder(opcionPantallaConfig == OPCION_BARRA_CONFIG);
      break;

    case CONF_CONFIG:
      dibujarListaConf();
      break;

    case CONF_STATUS:
      dibujarPaginaStatus();
      break;

    case CONF_PORTAL:
      dibujarVistaPortal(focoPortalConf);
      break;

    case CONF_EDITOR:
      dibujarEditor();
      break;

    case CONF_OTA:
      dibujarPantallaOta();
      break;

    case CONF_DEPURACION:
      dibujarListaDepuracion();
      break;
  }
}

// Editor de CONF: ID (indicativo de la estacion) o locator, con la rueda de las memorias
void confAbrirEditor(int8_t destino) {
  editorDestino = destino;
  editorTexto = destino == EDITOR_ID_ESTACION ? String(indicativoEstacion) : String(locatorEstacion);
  editorTexto.toUpperCase();
  editorMax = destino == EDITOR_ID_ESTACION ? TRAMA_LONG_INDICATIVO : 6;
  editorSel = 0;
  editorError = false;
  subConf = CONF_EDITOR;
}

// OK: si no es valido se avisa ("NO VALIDO") y se sigue editando. ESC: sale sin cambiar nada
void confCerrarEditor(bool guardar) {
  if (guardar) {
    bool esId = (editorDestino == EDITOR_ID_ESTACION);
    bool valido = esId ? aplicarIndicativo(editorTexto) : aplicarLocator(editorTexto);
    if (!valido) {
      editorError = true;
      return;
    }
    guardarEstacionNVS();
    Serial.printf("[CONFIG] %s: %s\n", esId ? "ID" : "LOCATOR", esId ? indicativoEstacion : locatorEstacion);
  }
  editorError = false;
  subConf = CONF_CONFIG;
}

// ACTUALIZAR FW: enciende el WiFi (si hace falta) y procesarOta() busca la ultima release al conectar
void confEntrarOta() {
  subConf = CONF_OTA;
  focoOta = 0;
  otaError = "";
  if (estadoWifi == WIFI_PORTAL) {
    estadoOta = OTA_ERROR;
    otaError = "CIERRA EL PORTAL";
    return;
  }
  estadoOta = OTA_CONECTANDO;
  if (estadoWifi == WIFI_APAGADO || estadoWifi == WIFI_SIN_CONEXION) encenderWifi();
}

// Al salir se apaga el WiFi: en CONF solo se usa para el portal
void confSalirOta() {
  subConf = CONF_CONFIG;
  estadoOta = OTA_REPOSO;
  if (estadoWifi != WIFI_PORTAL && estadoWifi != WIFI_APAGADO) apagarWifi();
}

void procesarOta() {
  if (pantallaActual != PANTALLA_CONFIG || subConf != CONF_OTA || estadoOta != OTA_CONECTANDO) return;
  if (estadoWifi == WIFI_SIN_CONEXION) {
    estadoOta = OTA_ERROR;
    otaError = "SIN CONEXION WIFI";
    pantallaPendiente = true;
  } else if (estadoWifi == WIFI_CONECTADO) {
    estadoOta = OTA_BUSCANDO;
    actualizarPantalla();
    if (!buscarActualizacion()) estadoOta = OTA_ERROR;
    else if (hayActualizacion()) estadoOta = OTA_HAY_NUEVA;
    else estadoOta = OTA_AL_DIA;
    pantallaPendiente = true;
  }
}

// Pantalla de ACTUALIZAR FW: version instalada, estado y, al descargar, una barra de progreso
void dibujarPantallaOta() {
  dibujarTituloCentrado(F("- ACTUALIZAR FW -"));
  display.setCursor(0, 22);
  display.print(F("INSTALADA  v" FW_VERSION));
  display.setCursor(0, 32);
  switch (estadoOta) {
    case OTA_CONECTANDO: display.print(F("CONECTANDO WIFI...")); break;
    case OTA_BUSCANDO:   display.print(F("BUSCANDO EN GITHUB...")); break;
    case OTA_AL_DIA:
      display.print(F("ULTIMA     v")); display.print(otaVersionNueva);
      display.setCursor(0, 42);
      // Mas nueva pero sin binario para esta placa (con o sin encoder): no se puede instalar
      display.print(compararVersiones(otaVersionNueva, FW_VERSION) > 0 ? F("SIN BINARIO PARA ESTA") : F("YA ESTA AL DIA"));
      break;
    case OTA_HAY_NUEVA:
      display.print(F("NUEVA      v")); display.print(otaVersionNueva);
      break;
    case OTA_DESCARGANDO: {
      display.print(F("INSTALANDO ")); display.print(otaProgreso); display.print('%');
      display.drawRect(0, 43, 128, 7, SSD1306_WHITE);
      display.fillRect(1, 44, 126 * otaProgreso / 100, 5, SSD1306_WHITE);
      break;
    }
    case OTA_HECHO:      display.print(F("INSTALADA: REINICIA")); break;
    default:             display.print(otaError); break;
  }
  if (estadoOta == OTA_HAY_NUEVA) dibujarPieDos(focoOta == 0, "instalar", focoOta == 1, "volver");
  else if (estadoOta == OTA_DESCARGANDO) dibujarPieCentrado(F("NO LA APAGUES"));
  else if (estadoOta != OTA_HECHO) dibujarPieCentrado(F("> volver"));
}

// STATUS se redibuja cada segundo (hora, tiempo encendido, memoria)
void procesarStatus() {
  static unsigned long ultimo = 0;
  if (pantallaActual != PANTALLA_CONFIG || subConf != CONF_STATUS) return;
  if (millis() - ultimo < 1000) return;
  ultimo = millis();
  pantallaPendiente = true;
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
    case PANTALLA_TRX: renderizarPantallaTrx(); break;
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
  memset(marcasFallo, 0, sizeof(marcasFallo));
  opcionPractica = 0;
  totalCaracteresLeccion = gruposCW * caracteresCW;

  guardarUltimaConfiguracionNVS();
  informarRefuerzo("KOCH", leccionCW, refuerzoKoch);

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
    grupoActualStr += caracterKochAleatorio(leccionCW, refuerzoKoch);
  }

  if (textoCompletoLeccion.length() > 0) textoCompletoLeccion += " ";
  textoCompletoLeccion += grupoActualStr;
}

// "ok" de la correccion: guarda el % de la leccion y, segun el modo, los aciertos y fallos de cada caracter
// marcado (x letra) o solo los totales (total: no se sabe que letras fallaron)
void enviarResultadoKoch(bool porLetra) {
  if (!porLetra) numeroErrores = fallosTotales;
  float pct = 0.0;
  if (totalCaracteresLeccion > 0) pct = ((float)(totalCaracteresLeccion - numeroErrores) / (float)totalCaracteresLeccion) * 100.0;
  registrarResultadoLeccionNVS(leccionCW, pct);

  if (porLetra) {
    Serial.print(F("[KOCH] FALLOS:"));
    for (uint16_t i = 0; i < textoCompletoLeccion.length(); i++) {
      char c = textoCompletoLeccion[i];
      if (c == ' ') continue;
      registrarCaracter(statsCarKoch, c, !marcasFallo[i]);
      if (marcasFallo[i]) { Serial.print(' '); Serial.print(c); }
    }
    Serial.printf(" (%u/%u)\n", numeroErrores, totalCaracteresLeccion);
    guardarStatsKochNVS();
  } else {
    kochTotCaracteres += totalCaracteresLeccion;
    kochTotAciertos += totalCaracteresLeccion - numeroErrores;
    Serial.printf("[KOCH] FALLOS TOTALES: %u/%u\n", numeroErrores, totalCaracteresLeccion);
    guardarTotalesKochNVS();
  }

  ultimoPctKoch = pct;
  ultimoPctKochMs = millis();
  estadoCW = CW_PANTALLA_RESUMEN_FINAL;
  opcionResumen = 0;
}

void procesarMaquinaEstadosCW() {
  if (!enPractica3_2 || estadoCW == CW_PAUSADO || estadoCW == CW_IDLE || 
      estadoCW == CW_PANTALLA_3_3_LISTADO || estadoCW == CW_PANTALLA_CORREGIR_ERRORES ||
      estadoCW == CW_PANTALLA_FALLOS_TOTALES || estadoCW == CW_PANTALLA_RESUMEN_FINAL) return;

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
EventoBoton eventoSerie = BOTON_NINGUNO;  // Evento simulado por serie (ver procesarOrdenesSerie)

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
// Aceleracion: la ISR mide el tiempo entre pasos. Menos de ENC_RAPIDO_MS = giro rapido: ajustarValor()
// avanza aceleracionGiro veces mas en los valores con muchos pasos (no en la cola de eventos: se mide
// en el encoder, asi que un loop() ocupado no lo dispara). aceleracionGiro se elige en CONF > CONFIG
#define ENC_RAPIDO_MS 40
#define ENC_MIN_PASOS_ACELERAR 20  // Valores con menos pasos de minimo a maximo, sin aceleracion
volatile TickType_t encUltimoPasoTick = 0;
volatile TickType_t encIntervaloTicks = portMAX_DELAY;
bool giroRapido = false;  // El paso que se esta procesando llego rapido (lo pone leerEventoBoton)
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
      if (encAcumulado >= 2 || encAcumulado <= -2) {
        if (encAcumulado >= 2) encPasos++;
        else encPasos--;
        TickType_t ahora = xTaskGetTickCountFromISR();
        encIntervaloTicks = ahora - encUltimoPasoTick;
        encUltimoPasoTick = ahora;
      }
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
  TickType_t intervalo;
  portENTER_CRITICAL(&muxEncoder);
  pasos = encPasos;
  if (pasos > 0) encPasos--;
  else if (pasos < 0) encPasos++;
  intervalo = encIntervaloTicks;
  portEXIT_CRITICAL(&muxEncoder);
  giroRapido = (pasos != 0) && intervalo < pdMS_TO_TICKS(ENC_RAPIDO_MS);
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

// Cambia un valor con el encoder, sin dar la vuelta al llegar a los limites. Al girar rapido, los valores
// con muchos pasos (fallos, wpm, leccion...) avanzan de ENC_FACTOR_RAPIDO en ENC_FACTOR_RAPIDO
template <typename T>
void ajustarValor(T& valor, int paso, int minimo, int maximo) {
#ifdef SIN_ENCODER
  // Sin encoder solo se avanza en un sentido: al pasar del maximo se vuelve al minimo
  int v = (int)valor + paso;
  valor = (T)(v > maximo ? minimo : (v < minimo ? maximo : v));
#else
  if (giroRapido && aceleracionGiro > 1 && paso != 0 && (maximo - minimo) / abs(paso) >= ENC_MIN_PASOS_ACELERAR) {
    paso *= aceleracionGiro;
  }
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
  else if (pantalla == PANTALLA_TRX) { opcionTrx = TRX_OPCION_BARRA; subTrx = TRX_MENU; }
  else if (pantalla == PANTALLA_PROPAGACION) { opcionPantallaProp = OPCION_BARRA_PROP; subDatos = DATOS_MENU; }
  else if (pantalla == PANTALLA_SPOTS) { opcionPantallaSpots = OPCION_BARRA_SPOTS; subDatos = DATOS_MENU; }
  else if (pantalla == PANTALLA_SOL) { opcionPantallaSol = OPCION_BARRA_DATOS; subDatos = DATOS_MENU; }
  else if (pantalla == PANTALLA_CONFIG) {
    opcionPantallaConfig = OPCION_BARRA_CONFIG;
    subConf = CONF_MENU;
    confirmandoFabrica = false;
  }
}

// "ant." o "sig." desde la barra inferior
void navegarDesdeBarra() {
  if (opcionNavegacionP1234 == 0) entrarEnPantalla(pantallaActual == 1 ? TOTAL_PANTALLAS : pantallaActual - 1, true);
  else entrarEnPantalla(pantallaActual == TOTAL_PANTALLAS ? 1 : pantallaActual + 1);
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
  EventoBoton ev = (eventoSerie != BOTON_NINGUNO) ? eventoSerie : leerEventoBoton();
  if (eventoSerie != BOTON_NINGUNO) giroRapido = false;
  eventoSerie = BOTON_NINGUNO;
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
        // Foco: [sig.] -> [PRACTICA DUO] -> [QSO LIBRE] -> [ant.] -> [sig.] ...
        moverFocoAnillo(opcionPantalla4, 2, giro);
      } else if (pulsa) {
        if (opcionPantalla4 == 0) {
          enPantalla4_Sub = true; subpantalla4Activa = 1;
          modoRadioRxActivo = true; opcionDuo4_1 = 0; loraEnAjustes = false;
          loraEscuchar();  // La radio duerme fuera de Duo y QSO
        } else if (opcionPantalla4 == 1) {
          entrarEnQSO();
        } else {
          navegarDesdeBarra();
        }
      }

    } else if (loraEnAjustes) {
      // Ajustes ("menu"). Duo: 0 wpm, 1 espaciado, 2 leccion, 3 salir, 4 volver.
      // QSO: 0 wpm, 1 ver, 2 salir, 3 volver (las palas siguen funcionando)
      bool esDuo = (subpantalla4Activa == 1);
      uint8_t n = esDuo ? N_AJUSTES_DUO : N_AJUSTES_QSO_LORA;
      if (atras) {
        loraEnAjustes = false;
      } else if (giro != 0) {
        if (editandoValor) {
          if (!esDuo) ajustarValor(keyerWpm, giro, KEYER_WPM_MIN, KEYER_WPM_MAX);
          else if (opcionAjustesLora == 0) ajustarValor(wpmCW, giro, 15, 30);
          else if (opcionAjustesLora == 1) ajustarValor(espaciadoCW, giro * 10, 100, 200);
          else ajustarValor(leccionCW, giro, 1, 40);
        } else {
          opcionAjustesLora = (opcionAjustesLora + n + 1 + giro) % (n + 1);
        }
      } else if (pulsa) {
        if (opcionAjustesLora == n) loraEnAjustes = false;                       // volver
        else if (opcionAjustesLora == n - 1) salirModoLoRa();                   // salir
        else if (!esDuo && opcionAjustesLora == 1) qsoVerRx = !qsoVerRx;
        else editandoValor = !editandoValor;                                    // Pulsar, girar y pulsar
      }

    } else if (subpantalla4Activa == 1) {
      // Duo. Foco: 0 enviar, 1 ver (solo si hay grupo oculto), 2 menu
      if (atras) {
        salirModoLoRa();
      } else if (giro != 0) {
        do {
          opcionDuo4_1 = (opcionDuo4_1 + 3 + giro) % 3;
        } while (opcionDuo4_1 == 1 && !hayGrupoDuoPorRevelar());
      } else if (ejeLarga) {
        grupoDuoRevelado = true;  // Atajo de "ver"
        if (opcionDuo4_1 == 1) opcionDuo4_1 = 0;
      } else if (pulsa) {
        if (opcionDuo4_1 == 0) {
          String nuevoGrupo = "";
          for (int i = 0; i < caracteresCW; i++) nuevoGrupo += caracterKochAleatorio(leccionCW);
          transmitirGrupoDuoLoRa(nuevoGrupo);
        } else if (opcionDuo4_1 == 1) {
          grupoDuoRevelado = true;
          opcionDuo4_1 = 0;
        } else {
          loraEnAjustes = true;
          opcionAjustesLora = 0;
        }
      }

    } else if (subpantalla4Activa == 2) {
      // QSO. Foco: 0 borrar (los dos textos), 1 menu. Las palas funcionan con cualquier foco
      if (atras || ejeLarga) {
        salirModoLoRa();
      } else if (giro != 0) {
        opcionQSO = (opcionQSO + OPCIONES_QSO + giro) % OPCIONES_QSO;
      } else if (pulsa) {
        if (opcionQSO == 0) qsoTextoRx = qsoTextoTx = "";
        else { loraEnAjustes = true; opcionAjustesLora = 0; }
      }
    }

  } else if (pantallaActual == PANTALLA_TRX) {
    switch (subTrx) {
      case TRX_MENU:
        // Foco: [sig.] -> [DECODIFICAR] -> [OPERAR] -> [QSO] -> [memorias] -> [ant.] -> [sig.] ...
        if (atras) entrarEnPantalla(PANTALLA_INICIO);
        else if (giro != 0) moverFocoAnillo(opcionTrx, TRX_OPCION_BARRA, giro);
        else if (pulsa) {
          if (opcionTrx == 0) trxEntrarSub(TRX_DECOD);
          else if (opcionTrx == 1) trxEntrarSub(TRX_OPERAR);
          else if (opcionTrx == 2) trxEntrarSub(TRX_QSO);
          else if (opcionTrx == TRX_OPCION_MEMORIAS) trxEntrarSub(TRX_MEMORIAS);
          else navegarDesdeBarra();
        }
        break;

      case TRX_DECOD:
        if (decodEnAjustes) {
          // Ajustes. Foco: 0 tono (AUTO o 400-1000 Hz), 1 salir, 2 volver (pie)
          if (atras) decodEnAjustes = false;
          else if (giro != 0) {
            if (editandoValor) ajustarTonoDecod(giro);
            else opcionAjustesDecod = (opcionAjustesDecod + N_AJUSTES_DECOD + 1 + giro) % (N_AJUSTES_DECOD + 1);
          } else if (pulsa) {
            if (opcionAjustesDecod == 0) editandoValor = !editandoValor;
            else if (opcionAjustesDecod == 1) { decodEnAjustes = false; trxSalirAlMenu(); }
            else decodEnAjustes = false;
          }
        } else {
          // Foco: 0 borrar, 1 menu
          if (atras || ejeLarga) trxSalirAlMenu();
          else if (giro != 0) opcionTrxSub = (opcionTrxSub + 1) % 2;
          else if (pulsa) {
            if (opcionTrxSub == 0) trxTextoRx = "";
            else { decodEnAjustes = true; opcionAjustesDecod = 0; }
          }
        }
        break;

      case TRX_OPERAR:
      case TRX_QSO:
        // OPERAR y QSO. Foco: 0 mem, 1 tune, 2 menu (ajustes). Las palas manipulan la emisora con cualquier foco
        if (atras || ejeLarga) trxSalirAlMenu();
        else if (giro != 0) opcionTrxSub = (opcionTrxSub + 3 + giro) % 3;
        else if (pulsa) {
          if (opcionTrxSub == 0) trxAbrirListaEnvio();
          else if (opcionTrxSub == 1) trxTune();
          else {
            trxAjustesOrigen = subTrx;
            subTrx = TRX_AJUSTES;
            opcionAjustesTrx = 0;
          }
        }
        break;

      case TRX_AJUSTES: {
        // Foco: 0 wpm, 1 tono local, 2 tune (s), 3 SU, 4 salir, 5 volver (pie). Las palas siguen manipulando
        const uint8_t N_FOCO = N_AJUSTES_TRX + 1;
        if (atras) subTrx = trxAjustesOrigen;
        else if (giro != 0) {
          if (editandoValor && opcionAjustesTrx == 0) ajustarValor(trxWpm, giro, KEYER_WPM_MIN, KEYER_WPM_MAX);
          else if (editandoValor) ajustarValor(trxTuneSeg, giro, TRX_TUNE_MIN_S, TRX_TUNE_MAX_S);
          else opcionAjustesTrx = (opcionAjustesTrx + N_FOCO + giro) % N_FOCO;
        } else if (pulsa) {
          switch (opcionAjustesTrx) {
            case 0: case 2: editandoValor = !editandoValor; break;
            case 1: trxTonoLocal = !trxTonoLocal; break;
            case 3: trxAbrirEditor(-1); break;  // Vuelve aqui
            case 4: trxSalirAlMenu(); break;
            default: subTrx = trxAjustesOrigen; break;
          }
        }
        break;
      }

      case TRX_MEMORIAS:
      case TRX_LISTA_ENVIO: {
        // Foco: memoria 1..6 o volver. Pulsar: editarla (MEMORIAS) o enviarla por KEY (lista de envio)
        bool paraEnviar = (subTrx == TRX_LISTA_ENVIO);
        bool volver = atras || (pulsa && trxMemoriaSel == TRX_NUM_MEMORIAS);
        if (volver) {
          if (paraEnviar) trxVolverDeLista();
          else subTrx = TRX_MENU;
        } else if (giro != 0) {
          trxMemoriaSel = (trxMemoriaSel + TRX_NUM_MEMORIAS + 1 + giro) % (TRX_NUM_MEMORIAS + 1);
        } else if (pulsa) {
          if (paraEnviar) trxEnviarMemoria(trxMemoriaSel);
          else trxAbrirEditor(trxMemoriaSel);
        }
        break;
      }

      case TRX_EDITOR:
        // Girar recorre la rueda; pulsar aplica (letra, ESP, {MI}, {SU}, <-, CLR, OK, ESC). PRG = ESC, larga = OK
        if (atras) trxCerrarEditor(false);
        else if (ejeLarga) trxCerrarEditor(true);
        else if (giro != 0) editorSel = (editorSel + EDITOR_TAM_RUEDA + giro) % EDITOR_TAM_RUEDA;
        else if (pulsa) editorAplicar();
        break;
    }

  } else if (pantallaActual == PANTALLA_CONFIG) {
    switch (subConf) {
      case CONF_MENU:
        // Foco: [sig.] -> [CONFIG] -> [STATUS] -> [ant.]
        if (atras) entrarEnPantalla(PANTALLA_INICIO);
        else if (giro != 0) moverFocoAnillo(opcionPantallaConfig, OPCION_BARRA_CONFIG, giro);
        else if (pulsa) {
          if (opcionPantallaConfig == 0) { subConf = CONF_CONFIG; focoConf = 0; confirmandoFabrica = false; }
          else if (opcionPantallaConfig == 1) { subConf = CONF_STATUS; paginaStatus = 0; }
          else navegarDesdeBarra();
        }
        break;

      case CONF_CONFIG:
        // Lista: ID, LOCATOR, ACELERACION, APAGADO, TONO, WEBCONF, ACTUALIZAR FW, DEPURACION, FABRICA y volver (pie)
        if (atras) {
          subConf = CONF_MENU;
          confirmandoFabrica = false;
        } else if (giro != 0) {
          if (editandoValor && focoConf == OC_ACELERACION) {
            ajustarAceleracion(giro);
            guardarAceleracionNVS();
          } else if (editandoValor && focoConf == OC_APAGADO) {
            ajustarApagado(giro);
            guardarApagadoNVS();
          } else if (editandoValor && focoConf == OC_TONO) {
            ajustarValor(frecTonoHz, giro * TONO_PASO_HZ, TONO_MIN_HZ, TONO_MAX_HZ);
            aplicarFrecTono(true);  // Pitido de prueba con el tono nuevo
            guardarTonoNVS();
          } else {
            confirmandoFabrica = false;
            focoConf = (focoConf + N_OPCIONES_CONF + giro) % N_OPCIONES_CONF;
          }
        } else if (pulsa) {
          switch (focoConf) {
            case OC_ID:      confAbrirEditor(EDITOR_ID_ESTACION); break;
            case OC_LOCATOR: confAbrirEditor(EDITOR_LOCATOR); break;
            case OC_ACELERACION:
            case OC_APAGADO:
              editandoValor = !editandoValor;  // Pulsar, girar y pulsar
              break;
            case OC_TONO:
              editandoValor = !editandoValor;
              if (editandoValor) aplicarFrecTono(true);  // Al empezar a editar, se oye el tono actual
              break;
            case OC_WEBCONF:
              if (estadoWifi != WIFI_PORTAL) abrirPortalWifi();
              subConf = CONF_PORTAL;
              focoPortalConf = 0;
              break;
            case OC_ACTUALIZAR:
              confEntrarOta();
              break;
            case OC_DEPURACION:
              subConf = CONF_DEPURACION;
              focoDepu = 0;
              break;
            case OC_FABRICA:
              if (!confirmandoFabrica) confirmandoFabrica = true;
              else resetFabrica();
              break;
            default:
              subConf = CONF_MENU;
              break;
          }
        }
        break;

      case CONF_PORTAL:
        // Foco: 0 ver qr, 1 volver (sin portal abierto, solo volver). El portal sigue abierto al volver
        if (procesarVistaPortal(focoPortalConf, giro, pulsa, atras)) subConf = CONF_CONFIG;
        break;

      case CONF_DEPURACION:
        // Foco: una opcion por bit DEPU_* (pulsar cambia SI/NO) y volver (pie)
        if (atras) {
          subConf = CONF_CONFIG;
        } else if (giro != 0) {
          focoDepu = (focoDepu + N_OPCIONES_DEPU + 1 + giro) % (N_OPCIONES_DEPU + 1);
        } else if (pulsa) {
          if (focoDepu < N_OPCIONES_DEPU) {
            depuracion ^= BITS_DEPU[focoDepu];
            guardarDepuracionNVS();
            Serial.printf("[DEPURACION] %s: %s\n", NOMBRES_OPCIONES_DEPU[focoDepu],
                          depuActiva(BITS_DEPU[focoDepu]) ? "SI" : "NO");
          } else {
            subConf = CONF_CONFIG;
          }
        }
        break;

      case CONF_STATUS:
        // Girar pasa pagina; pulsar (o atras) vuelve al menu
        if (atras || pulsa || ejeLarga) subConf = CONF_MENU;
        else if (giro != 0) paginaStatus = (paginaStatus + N_PAGINAS_STATUS + giro) % N_PAGINAS_STATUS;
        break;

      case CONF_OTA:
        // Con version nueva: 0 instalar, 1 volver. Si no, cualquier pulsacion (o atras) vuelve
        if (estadoOta == OTA_DESCARGANDO || estadoOta == OTA_HECHO) break;
        if (estadoOta == OTA_HAY_NUEVA && giro != 0) focoOta = (focoOta + 1) % 2;
        else if (atras || (pulsa && (estadoOta != OTA_HAY_NUEVA || focoOta == 1))) confSalirOta();
        else if (pulsa) instalarActualizacion();
        break;

      case CONF_EDITOR:
        // Como el de las memorias: girar recorre la rueda; pulsar aplica. PRG = ESC, larga = OK
        if (atras) confCerrarEditor(false);
        else if (ejeLarga) confCerrarEditor(true);
        else if (giro != 0) { editorError = false; editorSel = (editorSel + EDITOR_TAM_RUEDA + giro) % EDITOR_TAM_RUEDA; }
        else if (pulsa) editorAplicar();
        break;
    }

  } else if (pantallaActual == PANTALLA_PROPAGACION || pantallaActual == PANTALLA_SPOTS ||
             pantallaActual == PANTALLA_SOL) {
    // Menu: [sig.] -> [VER DATOS] -> [ACTUALIZAR] -> [ant.] (sin conexion: ABRIR WEBCONF y REINTENTAR).
    // Paginas: girar pasa pagina y pulsar (o atras) vuelve al menu
    bool esProp = (pantallaActual == PANTALLA_PROPAGACION);
    bool esSol = (pantallaActual == PANTALLA_SOL);
    uint8_t& opcion = esProp ? opcionPantallaProp : (esSol ? opcionPantallaSol : opcionPantallaSpots);
    uint8_t& pagina = esProp ? paginaPropagacion : (esSol ? paginaSol : paginaSpots);
    uint8_t paginas = esProp ? 2 : (esSol ? 3 : paginasSpots());
    switch (subDatos) {
      case DATOS_MENU:
        if (atras) entrarEnPantalla(PANTALLA_INICIO);
        else if (giro != 0) moverFocoAnillo(opcion, OPCION_BARRA_DATOS, giro);
        else if (pulsa) {
          if (opcion == OPCION_BARRA_DATOS) {
            navegarDesdeBarra();
          } else if (estadoWifi == WIFI_SIN_CONEXION) {
            if (opcion == 0) {
              abrirPortalWifi();
              subDatos = DATOS_PORTAL;
              focoPortalDatos = 0;
            } else {
              encenderWifi();  // Reintentar
            }
          } else if (opcion == 0) {
            subDatos = DATOS_PAGINAS;
            pagina = 0;
          } else if (esSol) {
            actualizarHoraSol();
          } else {
            pedirActualizacionWifi();
          }
        }
        break;

      case DATOS_PAGINAS:
        if (atras || pulsa || ejeLarga) subDatos = DATOS_MENU;
        else if (giro != 0) pagina = (pagina + paginas + giro) % paginas;
        break;

      case DATOS_PORTAL:
        if (procesarVistaPortal(focoPortalDatos, giro, pulsa, atras)) subDatos = DATOS_MENU;
        break;
    }

  } else if (pantallaActual == PANTALLA_CW) {
    if (enEcho && enEstadisticasEcho) {
      // Como ESTAD. del Koch: girar pasa pagina (la ultima es la de borrado); pulsar vuelve o borra
      if (atras || ejeLarga) {
        enEstadisticasEcho = false; confirmandoBorrado = false;
      } else if (giro != 0) {
        confirmandoBorrado = false;
        paginaStatsEcho = (paginaStatsEcho + TOTAL_PAGINAS_STATS_ECHO + giro) % TOTAL_PAGINAS_STATS_ECHO;
      } else if (pulsa) {
        if (paginaStatsEcho != TOTAL_PAGINAS_STATS_ECHO - 1) {
          enEstadisticasEcho = false;
        } else if (!confirmandoBorrado) {
          confirmandoBorrado = true;
        } else {
          borrarEstadisticasEchoNVS(); confirmandoBorrado = false; paginaStatsEcho = 0;
        }
      }

    } else if (enEcho && !echoPracticando) {
      // Ajustes. Foco: 0 wpm, 1 sp, 2 leccion, 3 letras, 4 ver, 5 retos, 6 refuerzo, 7 estad., 8 iniciar, 9 volver
      if (atras) {
        salirDeEcho();
      } else if (giro != 0) {
        if (editandoValor) {
          switch (opcionEcho) {
            case 0: ajustarValor(echoWpm, giro, KEYER_WPM_MIN, KEYER_WPM_MAX); break;
            case 1: ajustarValor(echoEspaciado, giro * 10, 100, 200); break;
            case 2: ajustarValor(echoLeccion, giro, 1, 40); break;
            case 3: ajustarValor(echoLargo, giro, 1, 5); break;
            case 5: ajustarValor(echoRetos, giro * 10, 10, 50); break;
          }
        } else {
          opcionEcho = (opcionEcho + N_OPCIONES_ECHO + giro) % N_OPCIONES_ECHO;
        }
      } else if (pulsa) {
        switch (opcionEcho) {
          case 4: echoVerObjetivo = !echoVerObjetivo; break;
          case 6: echoRefuerzo = siguienteRefuerzo(echoRefuerzo); break;
          case 7:
            enEstadisticasEcho = true;  // Al salir se vuelve aqui
            paginaStatsEcho = 0;
            confirmandoBorrado = false;
            break;
          case 8: iniciarPracticaEcho(); break;
          case 9: salirDeEcho(); break;
          default: editandoValor = !editandoValor; break;  // 0, 1, 2, 3 y 5
        }
      }

    } else if (enEcho && estadoEcho == ECHO_FIN) {
      // Resultado. Foco: 0 volver (a los ajustes), 1 repetir. La larga del eje es atajo de repetir
      if (atras) salirPracticaEcho();
      else if (ejeLarga) iniciarPracticaEcho();
      else if (giro != 0) opcionPracticaEcho = (opcionPracticaEcho + 1) % 2;
      else if (pulsa) {
        if (opcionPracticaEcho == 0) salirPracticaEcho();
        else iniciarPracticaEcho();
      }

    } else if (enEcho) {
      // Practica. Foco: 0 repetir (no cuenta como intento), 1 salir. Las palas funcionan con cualquier foco
      if (atras || ejeLarga) {
        salirPracticaEcho();
      } else if (giro != 0) {
        opcionPracticaEcho = (opcionPracticaEcho + 1) % 2;
      } else if (pulsa) {
        if (opcionPracticaEcho == 1) salirPracticaEcho();
        else if (estadoEcho != ECHO_RESULTADO) echoNuevoReto(true);  // Con el resultado a la vista, sigue solo
      }

    } else if (enKeyer && keyerEnAjustes) {
      // Ajustes de TX LIBRE. Foco: 0 wpm, 1 modo, 2 palas, 3 salir, 4 volver (pie). Las palas siguen manipulando
      const uint8_t N_FOCO = N_AJUSTES_KEYER + 1;
      if (atras) {
        keyerEnAjustes = false;
      } else if (giro != 0) {
        if (editandoValor) ajustarValor(keyerWpm, giro, KEYER_WPM_MIN, KEYER_WPM_MAX);
        else opcionAjustesKeyer = (opcionAjustesKeyer + N_FOCO + giro) % N_FOCO;
      } else if (pulsa) {
        switch (opcionAjustesKeyer) {
          case 0: editandoValor = !editandoValor; break;
          case 1: keyerModoB = !keyerModoB; break;
          case 2: keyerPalasInvertidas = !keyerPalasInvertidas; break;
          case 3: keyerEnAjustes = false; salirDeKeyer(); break;
          default: keyerEnAjustes = false; break;
        }
      }

    } else if (enKeyer) {
      // Foco: 0 borrar, 1 menu (ajustes). Las palas funcionan con cualquier foco
      if (atras || ejeLarga) {
        salirDeKeyer();
      } else if (giro != 0) {
        opcionKeyer = (opcionKeyer + 1) % 2;
      } else if (pulsa) {
        if (opcionKeyer == 0) textoKeyer = "";
        else { keyerEnAjustes = true; opcionAjustesKeyer = 0; }
      }

    } else if (enEstadisticas3_5) {
      // Girar pasa pagina (la ultima es la de borrado). Pulsar: volver, o borrar en la ultima
      if (atras || ejeLarga) {
        enEstadisticas3_5 = false; confirmandoBorrado = false;
      } else if (giro != 0) {
        confirmandoBorrado = false;
        paginaStats3_5 = (paginaStats3_5 + TOTAL_PAGINAS_STATS + giro) % TOTAL_PAGINAS_STATS;
      } else if (pulsa) {
        if (paginaStats3_5 != PAG_STATS_BORRADO) {
          enEstadisticas3_5 = false;
        } else if (!confirmandoBorrado) {
          confirmandoBorrado = true;
        } else {
          borrarEstadisticasKochNVS(); confirmandoBorrado = false; paginaStats3_5 = 0;
        }
      }

    } else if (enPractica3_2) {
      if (estadoCW == CW_PANTALLA_3_3_LISTADO) {
        int charPorPagina = 63;
        bool hayMasPaginas = ((paginaListado3_3 + 1) * charPorPagina) < textoCompletoLeccion.length();
        if (atras) {
          if (paginaListado3_3 > 0) { paginaListado3_3--; opcionFinListado3_3 = 0; }
        } else if (giro > 0) {
          // Derecha: pagina siguiente y, en la ultima, recorre "x letra", "total" y "rep."
          if (hayMasPaginas) paginaListado3_3++;
          else opcionFinListado3_3 = (opcionFinListado3_3 + 1) % 3;
        } else if (giro < 0) {
          if (!hayMasPaginas && opcionFinListado3_3 > 0) opcionFinListado3_3--;
          else if (paginaListado3_3 > 0) paginaListado3_3--;
        } else if (pulsa) {
          if (hayMasPaginas) paginaListado3_3++;
          else if (opcionFinListado3_3 == 0) {
            estadoCW = CW_PANTALLA_CORREGIR_ERRORES;  // Las marcas se conservan si se vuelve al listado
            focoCorregir = 0;
            paginaCorregir = 0;
          } else if (opcionFinListado3_3 == 1) {
            // Empieza en lo marcado en "x letra" (0 si no se ha marcado nada) y ya editando
            estadoCW = CW_PANTALLA_FALLOS_TOTALES;
            fallosTotales = numeroErrores;
            focoFallosTotales = 0;
            editandoValor = true;
          }
          else iniciarPracticaCW();
        }

      } else if (estadoCW == CW_PANTALLA_FALLOS_TOTALES) {
        // Foco: 0 numero de fallos (pulsar lo edita; al girar rapido va de 5 en 5), 1 rep., 2 ok
        if (atras) {
          estadoCW = CW_PANTALLA_3_3_LISTADO;  // Si se estaba editando, "atras" ya ha salido de la edicion arriba
        } else if (giro != 0) {
          if (editandoValor) ajustarValor(fallosTotales, giro, 0, totalCaracteresLeccion);
          else focoFallosTotales = (focoFallosTotales + 3 + giro) % 3;
        } else if (pulsa) {
          if (focoFallosTotales == 0) editandoValor = !editandoValor;
          else if (focoFallosTotales == 1) { editandoValor = false; iniciarPracticaCW(); }
          else { editandoValor = false; enviarResultadoKoch(false); }
        }

      } else if (estadoCW == CW_PANTALLA_CORREGIR_ERRORES) {
        // Foco en anillo: caracteres (sin los espacios) -> rep. -> ok -> primer caracter
        uint16_t largo = textoCompletoLeccion.length();
        if (atras) {
          estadoCW = CW_PANTALLA_3_3_LISTADO;
        } else if (giro != 0) {
          int8_t paso = giro > 0 ? 1 : -1;
          for (int8_t n = abs(giro); n > 0; n--) {
            do {
              focoCorregir = (focoCorregir + largo + 2 + paso) % (largo + 2);
            } while (focoCorregir < largo && textoCompletoLeccion[focoCorregir] == ' ');
          }
          if (focoCorregir < largo) paginaCorregir = focoCorregir / CARACTERES_POR_PAGINA_LISTADO;
        } else if (pulsa) {
          if (focoCorregir < largo) {
            marcasFallo[focoCorregir] = !marcasFallo[focoCorregir];
            numeroErrores += marcasFallo[focoCorregir] ? 1 : -1;
          } else if (focoCorregir == largo) {
            iniciarPracticaCW();
          } else {
            enviarResultadoKoch(true);
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
      // Ajustes del KOCH (lista). Foco: 0 wpm, 1 espaciado, 2 leccion, 3 caracteres, 4 grupos, 5 refuerzo,
      // 6 estadisticas, 7 iniciar, 8 volver
      const uint8_t N_FOCO = N_AJUSTES_KOCH + 2;
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
          opcionSubpantalla = (opcionSubpantalla + N_FOCO + giro) % N_FOCO;
        }
      } else if (pulsa) {
        if (opcionSubpantalla <= 4) editandoValor = !editandoValor;
        else if (opcionSubpantalla == 5) refuerzoKoch = siguienteRefuerzo(refuerzoKoch);
        else if (opcionSubpantalla == 6) {
          enEstadisticas3_5 = true;  // Al salir se vuelve aqui
          paginaStats3_5 = 0;
          confirmandoBorrado = false;
        }
        else if (opcionSubpantalla == 7) iniciarPracticaCW();
        else enSubpantallaCW = false;
      }

    } else {
      if (atras) {
        entrarEnPantalla(PANTALLA_INICIO);
      } else if (giro != 0) {
        // Foco: [sig.] -> [KOCH (RX)] -> [ECHO (TX)] -> [TX LIBRE] -> [ant.] -> [sig.] ...
        moverFocoAnillo(opcionPantalla3, OPCION_BARRA_CW, giro);
      } else if (pulsa) {
        if (opcionPantalla3 == 0) {
          enSubpantallaCW = true;
          opcionSubpantalla = 7;  // iniciar
        } else if (opcionPantalla3 == 1) {
          entrarEnEcho();
        } else if (opcionPantalla3 == 2) {
          entrarEnKeyer();
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

/**
 * Reenvia a la OLED bomba de carga (0x8D 0x14) y encendido (0xAF), una vez, tras display.begin()
 * (arreglo de la v0.4e). Tras algunos reinicios cortos la OLED contesta por I2C y dice estar encendida,
 * pero el panel se queda negro. En la placa 1 aun pasa a veces: ver CLAUDE.md (pruebas de las v0.7u-z).
 */
void reafirmarOled() {
  const uint8_t cmdsOled[][2] = { {0x8D, 0x14}, {0xAF, 0x00} };  // Bomba de carga on, pantalla on
  for (uint8_t i = 0; i < 2; i++) {
    Wire.beginTransmission(0x3C);
    Wire.write((uint8_t)0x00);  // Byte de control: siguen comandos
    Wire.write(cmdsOled[i][0]);
    if (cmdsOled[i][0] == 0x8D) Wire.write(cmdsOled[i][1]);
    uint8_t err = Wire.endTransmission();
    if (err != 0) Serial.printf("[ERROR] OLED CMD 0x%02X: I2C %u\n", cmdsOled[i][0], err);
  }
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
  if (pantallaActual == PANTALLA_CW || pantallaActual == PANTALLA_LORA || pantallaActual == PANTALLA_TRX ||
      estadoWifi == WIFI_PORTAL) return;
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
  iniciarDecodificador();
  iniciarEncoder();

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.printf("[INIT] MOTIVO DEL REINICIO: %d%s\n", (int)esp_reset_reason(),
                esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 ? " (despierta con PRG)" : "");

  // display.begin() no comprueba que la OLED responda (solo falla si no hay memoria).
  // Se comprueba el ACK en I2C tras un pulso de reset y, si no contesta, se corta Vext y se reintenta.
  // Nota (v0.7u-z): en la placa 1, tras algunos reinicios cortos, el panel queda negro aunque el
  // controlador contesta y dice estar encendido. Se probaron reenvios periodicos (parpadeaban), cortar
  // Vext al arrancar (seguia fallando a veces) y otras variantes; se mantiene esta secuencia (la de la
  // v0.4e). Sospecha: caidas de 3,3 V (la OLED parpadea al arrancar la LoRa); sin comprobar con otra
  // alimentacion. Detalles en CLAUDE.md.
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

  reafirmarOled();

  randomSeed(esp_random());  // GPIO0 no es ADC en el S3: la semilla anterior era casi fija
  seqTx = random(256);       // Si la otra placa se reinicia, que no repita el ultimo seq

  cargarDatosNVS();
  ajustarApagado(0);  // Si lo guardado no es un valor de la lista, lo corrige
  aplicarFrecTono(false);
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
  // Con el decodificador en marcha el ADC1 es suyo (DMA): la bateria se deja como estaba
  if (!decodActivo && millis() - ultimaLecturaBat >= INTERVALO_BATERIA_MS) {
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
 * vigilar la fragmentacion), pila libre de cada tarea, uso de la NVS y frecuencia de la CPU
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
 * Ahorro: la OLED pasa al minimo de brillo (display.dim: contraste 0) tras BRILLO_TENUE_MS sin tocar
 * nada (o con la cara dormida) y vuelve al normal con cualquier giro, pulsacion o manipulacion con las
 * palas. En CW y LoRa (y sus subpantallas) no se atenua: son las que se miran mientras se practica.
 */
#define BRILLO_TENUE_MS 60000UL

void procesarBrillo() {
  static bool tenue = false;
  bool pantallaDePractica = (pantallaActual == PANTALLA_CW || pantallaActual == PANTALLA_LORA ||
                             pantallaActual == PANTALLA_TRX);
  bool debeSerTenue = (!pantallaDePractica && millis() - ultimaActividadMs >= BRILLO_TENUE_MS) ||
                      (caraActiva && expresionCara == EXP_DORMIDA);
  if (debeSerTenue != tenue) {
    tenue = debeSerTenue;
    display.dim(tenue);
  }
}

// Ordenes por serie para capturas y pruebas desde el PC. 'p' vuelca el buffer de la OLED (1024 bytes, formato
// del SSD1306: byte = x + (y / 8) * 128, bit = y % 8) en hexadecimal entre dos marcas; docs/captura_oled.py lo
// guarda en PNG (es la copia en RAM: no ve si el panel esta negro o atenuado). 'd' derecha, 'i' izquierda,
// 'k' pulsar, 'l' pulsacion larga del eje y 'a' atras (PRG corto) simulan el encoder
void procesarOrdenesSerie() {
  if (!Serial.available()) return;
  int c = Serial.read();
  switch (c) {
    case 'd': eventoSerie = ENC_DERECHA; return;
    case 'i': eventoSerie = ENC_IZQUIERDA; return;
    case 'k': eventoSerie = ENC_PULSA; return;
    case 'l': eventoSerie = ENC_LARGA; return;
    case 'a': eventoSerie = BOTON_CORTA; return;
  }
  if (c != 'p') return;
  const uint8_t* buf = display.getBuffer();
  Serial.println(F("[CAPTURA] INICIO"));
  char hex[3];
  for (int i = 0; i < 1024; i++) {
    snprintf(hex, sizeof(hex), "%02X", buf[i]);
    Serial.print(hex);
    if ((i & 63) == 63) Serial.println();
  }
  Serial.println(F("[CAPTURA] FIN"));
}

void loop() {
  if (depuActiva(DEPU_SERIE)) procesarOrdenesSerie();
  procesarBoton();
  procesarTonoPrueba();
  procesarTextoKeyer();
  procesarEcho();
  procesarCara();
  procesarMaquinaEstadosCW();
  procesarFinEmisionDuo();
  procesarRecepcionLoRa();
  procesarColaRxDuo();
  procesarQSO();
  procesarTrx();
  procesarWifi();
  procesarOta();
  procesarBateriaYPantalla();
  if (depuActiva(DEPU_DIAG)) procesarDiagnostico();
  procesarStatus();
  procesarBrillo();
  procesarApagadoAuto();

  // Ahorro: 1 ms de descanso por vuelta. Sin el, loop() tenia un nucleo al 100 %; con el, la CPU espera
  // en reposo. Las entradas siguen respondiendo (encoder por interrupcion; pulsadores cada ~1 ms) y el
  // CW no se ve afectado (motor y keyer van en sus propias tareas)
  vTaskDelay(1);
}