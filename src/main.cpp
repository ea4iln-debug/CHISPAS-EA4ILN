/**
 * @file main.cpp
 * @author Chispas EA4ILN Project
 * @brief Firmware para Heltec WiFi LoRa 32 V3 (ESP32-S3) - Aprendizaje CW y LoRa Duo.
 * @version 0.9e - TUNE en OPERAR y QSO de TRX: portadora por KEY de 1-10 s
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

#define FW_VERSION "0.9e"  // Se muestra en la pantalla 1 y por serie al arrancar: cambiarla en cada version

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
#include <driver/adc.h>  // ADC por DMA (decodificador CW de la pantalla TRX)

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

#define FREC_AUDIO_HZ    850// Tono Morse local por defecto (850 Hz); se elige en CONF -> tono (frecTonoHz)
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
#define TRAMA_TIPO_TEXTO_QSO   2  // Una palabra manipulada con las palas (QSO)
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
#define PANTALLA_LORA        3  // Practica Duo / QSO libre
#define PANTALLA_TRX         4  // Conexion con el transceptor: decodificar, operar (KEY), QSO y memorias
#define PANTALLA_PROPAGACION 5
#define PANTALLA_SPOTS       6
#define PANTALLA_SOL         7
#define PANTALLA_CONFIG      8
const uint8_t TOTAL_PANTALLAS = 8;
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

// Estadisticas por caracter: aciertos y fallos de cada caracter de TABLA_KOCH, por separado para el Koch
// (marcando los fallos al corregir) y para el Echo. En NVS, un bloque cada una ("carK" y "carE")
struct StatsCaracter {
  uint16_t aciertos;
  uint16_t fallos;
};
StatsCaracter statsCarKoch[TOTAL_CARACTERES_KOCH];
StatsCaracter statsCarEcho[TOTAL_CARACTERES_KOCH];
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
uint8_t opcionKeyer = 0;            // 0 = wpm, 1 = modo, 2 = palas, 3 = borrar, 4 = volver
String textoKeyer = "";             // Lo descodificado de la manipulacion
volatile uint32_t keyerUltimoUsoMs = 0;  // Ultima vez que se pulso una pala o sono un elemento (QSO)

// Modo Echo (pantalla CW > ECHO (TX)): Chispas emite letras de su leccion Koch y hay que repetirlas con las
// palas. Como el Koch: ajustes propios (wpm, espaciado y leccion independientes), practica de echoRetos retos
// y resultado. ECHO_ESPERA = ajustes (sin practica)
enum EstadoEcho { ECHO_ESPERA, ECHO_REPRODUCIENDO, ECHO_ESCUCHANDO, ECHO_RESULTADO, ECHO_FIN };
bool enEcho = false;              // Echo abierto (ajustes, practica o estadisticas)
bool echoPracticando = false;
bool enEstadisticasEcho = false;
#define OPCION_ECHO_INICIAR 7
uint8_t opcionEcho = OPCION_ECHO_INICIAR;  // Ajustes: 0 wpm, 1 sp, 2 leccion, 3 letras, 4 ver, 5 retos, 6 estad., 7 iniciar, 8 volver
#define N_OPCIONES_ECHO 9
uint8_t opcionPracticaEcho = 0;   // Practica: 0 repetir, 1 salir. Resultado: 0 volver, 1 rep.
uint8_t echoWpm = 20;
uint16_t echoEspaciado = 200;
uint8_t echoLeccion = 4;
uint8_t echoLargo = 1;            // Letras por reto (1-5)
bool echoVerObjetivo = true;      // Ensenar la letra antes de manipularla (para aprender)
uint8_t echoRetos = 20;           // Retos por practica (10-50); cada intento cuenta, tambien al repetir tras un fallo
EstadoEcho estadoEcho = ECHO_ESPERA;
String echoObjetivo = "";
String echoRespuesta = "";
bool echoAcierto = false;
uint16_t echoAciertos = 0, echoIntentos = 0;
unsigned long echoFinResultadoMs = 0;
uint32_t echoTotalIntentos = 0, echoTotalAciertos = 0;  // Historico (ESTAD. del Echo)
uint16_t echoSesiones = 0;                               // Practicas terminadas
uint8_t paginaStatsEcho = 0;      // 0 mas errores, 1 totales, 2 borrado
#define TOTAL_PAGINAS_STATS_ECHO 3

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

// Variables de estado para Pantalla 4 (Transceptor LoRa)
bool enPantalla4_Sub = false;  
uint8_t opcionPantalla4 = 2;   // 0=PRACTICA DUO, 1=QSO LIBRE, 2=BARRA INFERIOR (ant/sig)
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

// QSO por LoRa (subpantalla 2): lo manipulado con las palas se envia palabra a palabra (trama de texto) y lo
// recibido suena cuando hay un silencio de palabra sin manipular; si se manipula mientras suena, se corta y el
// resto queda solo como texto. Usa la cola de recepcion del Duo
#define OPCIONES_QSO 3
uint8_t opcionQSO = 2;            // 0 wpm (el de TX LIBRE), 1 ver, 2 salir
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
enum SubTrx { TRX_MENU, TRX_DECOD, TRX_OPERAR, TRX_QSO, TRX_MEMORIAS, TRX_LISTA_ENVIO, TRX_EDITOR };
SubTrx subTrx = TRX_MENU;
SubTrx subTrxOrigen = TRX_MENU;  // A donde vuelven la lista de envio y el editor
uint8_t opcionTrxSub = 0;        // Foco dentro de la subpantalla
uint8_t trxWpm = 20;             // Keyer y memorias por KEY (NVS "twpm")
uint16_t trxTonoDecodHz = 0;     // Tono del decodificador: 0 = automatico, 400-1000 (NVS "tdec")
String trxTextoRx = "";          // Descodificado de la emisora
String trxTextoTx = "";          // Manipulado y memorias enviadas
char trxSu[TRAMA_LONG_INDICATIVO + 1] = "";  // Indicativo del corresponsal ({SU})
bool trxEnviandoMemoria = false;
// Tune: portadora continua por KEY para ajustar el acoplador o la potencia (OPERAR y QSO). La duracion se
// elige en OPERAR (NVS "ttune"); mientras dura, el corte de seguridad deja ese tiempo mas un margen
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
int8_t editorDestino = 0;   // 0..5 = memoria; -1 = indicativo del corresponsal
String editorTexto = "";
uint8_t editorMax = TRX_MEMORIA_MAX;
uint8_t editorSel = 0;      // Posicion en la rueda

int rssiUltimo = 0;
float snrUltimo = 0.0;

// ============================================================================
// DEPURACION: MEDIDA DE LA TEMPORIZACION CW
// ============================================================================
// Mide cuanto llega tarde cada transicion (encender/apagar) respecto a lo programado.
// Ese retraso alarga el elemento o la pausa anterior. Se informa por serie al final
// de cada grupo. Poner a 0 para quitarlo del firmware. Solo lo usa la tarea del motor CW.
#define DEBUG_CW 0

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

void registrarCaracter(StatsCaracter* tabla, char c, bool acierto) {
  int i = indiceKoch(c);
  if (i < 0) return;
  uint16_t& cuenta = acierto ? tabla[i].aciertos : tabla[i].fallos;
  if (cuenta < UINT16_MAX) cuenta++;
}

void guardarStatsKochNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putBytes("carK", statsCarKoch, sizeof(statsCarKoch));
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
    for (uint8_t i = 0; i < echoLargo; i++) echoObjetivo += caracterKochAleatorio(echoLeccion);
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
  echoTotalIntentos = prefs.getUInt("eint", 0);
  echoTotalAciertos = prefs.getUInt("eok", 0);
  echoSesiones = prefs.getUShort("eses", 0);
  apagadoAutoMin = prefs.getUChar("apag", 30);  // ajustarApagado(0) lo lleva a un valor de la lista si no lo es
  frecTonoHz = constrain(prefs.getUShort("tono", FREC_AUDIO_HZ), TONO_MIN_HZ, TONO_MAX_HZ);

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
  bool hayBloqueAntiguo = prefs.isKey("car");  // v0.8f: Koch y Echo juntos; se descarta (se empieza de cero)
  prefs.end();
  if (hayBloqueAntiguo) {
    prefs.begin(NVS_ENTRENADOR, false);
    prefs.remove("car");
    prefs.end();
  }
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

void guardarTonoNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUShort("tono", frecTonoHz);
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
  prefs.end();
  memset(statsCarKoch, 0, sizeof(statsCarKoch));

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
  prefs.end();
  memset(statsCarEcho, 0, sizeof(statsCarEcho));
  echoTotalIntentos = echoTotalAciertos = 0;
  echoSesiones = 0;
}

void cargarEstacionNVS() {
  prefs.begin(NVS_ESTACION, true);
  strlcpy(indicativoEstacion, prefs.getString("call", INDICATIVO_POR_DEFECTO).c_str(), sizeof(indicativoEstacion));
  strlcpy(locatorEstacion, prefs.getString("loc", LOCATOR_POR_DEFECTO).c_str(), sizeof(locatorEstacion));
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
  opcionQSO = 2;
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
      opcionTrxSub = 2;  // salir
      trxTextoRx = "";
      decodEmpezar();
      break;
    case TRX_OPERAR:
      opcionTrxSub = 3;  // mem
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
  if (subTrx == TRX_OPERAR || subTrx == TRX_QSO) trxHabilitarKey();
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

void editorAplicar() {
  String e = editorElemento(editorSel);
  if (e == "OK") { trxCerrarEditor(true); return; }
  if (e == "ESC") { trxCerrarEditor(false); return; }
  if (e == "CLR") { editorTexto = ""; return; }
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
  } else if (subTrx == TRX_OPERAR || subTrx == TRX_QSO) {
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

  // Diagnostico por serie para ajustar RV2 en banco
  static unsigned long ultimoDiag = 0;
  if (decodActivo && millis() - ultimoDiag >= 2000) {
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
      if (subTrx == TRX_OPERAR || subTrx == TRX_QSO) keyerReanudar();
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
  if (subpantalla4Activa == 2) {
    qsoEnviarPalabra();  // Lo que quedara a medias
    keyerPausar();
    guardarKeyerNVS();   // wpm y VER del QSO
    qsoSonando = false;
  }
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
// hamqsl.com (N0NBH) pide expresamente no consultar el XML mas de una vez por hora ("please only select to update
// every hour", hamqsl.com/solar.html): es lo que tarda en cambiar el flujo solar; el resto, cada 3 horas
#define REFRESCO_PROPAGACION_MS  (60UL * 60 * 1000)
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

#define OPCION_BARRA_CONFIG 4
uint8_t opcionPantallaConfig = OPCION_BARRA_CONFIG;  // 0 = webconf (portal), 1 = reset de fabrica, 2 = apagado, 3 = tono, 4 = barra

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
  char linea[24];
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

  } else if (enEcho && enEstadisticasEcho) {
    display.setTextSize(1);
    if (paginaStatsEcho == 0) dibujarMasErrores(statsCarEcho);
    else if (paginaStatsEcho == 1) dibujarTotalesEcho();
    else dibujarPaginaBorrado(F("ECHO"));
    if (paginaStatsEcho < TOTAL_PAGINAS_STATS_ECHO - 1) {
      dibujarPieCentrado("pag " + String(paginaStatsEcho + 1) + "/" + String(TOTAL_PAGINAS_STATS_ECHO) + "   > volver");
    }

  } else if (enEcho && !echoPracticando) {
    // Ajustes del Echo, con la disposicion de los del Koch
    display.setTextSize(1);

    display.setCursor(0, 12);
    display.print(opcionEcho == 0 ? ">" : " ");
    display.print(F("WPM: ")); imprimirValor(opcionEcho == 0, String(echoWpm));

    display.setCursor(64, 12);
    display.print(opcionEcho == 1 ? ">" : " ");
    display.print(F("SP: ")); imprimirValor(opcionEcho == 1, String(echoEspaciado) + "%");

    display.setCursor(0, 22);
    display.print(opcionEcho == 2 ? ">" : " ");
    display.print(F("LECCION: ")); imprimirValor(opcionEcho == 2, String(echoLeccion));

    display.setCursor(0, 32);
    display.print(opcionEcho == 3 ? ">" : " ");
    display.print(F("LETRAS: ")); imprimirValor(opcionEcho == 3, String(echoLargo));

    display.setCursor(82, 32);
    display.print(opcionEcho == 4 ? ">" : " ");
    display.print(echoVerObjetivo ? F("VER SI") : F("VER NO"));

    display.setCursor(0, 42);
    display.print(opcionEcho == 5 ? ">" : " ");
    display.print(F("RETOS: ")); imprimirValor(opcionEcho == 5, String(echoRetos));

    display.setCursor(82, 42);
    display.print(opcionEcho == 6 ? ">" : " ");
    display.print(F("ESTAD."));

    dibujarPieCentrado((opcionEcho == 7 ? "> " : "  ") + String("iniciar  ") +
                       (opcionEcho == 8 ? "> " : "  ") + String("volver"));

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

    const char* opciones[] = { "< KOCH (RX) >", "< ECHO (TX) >", "< TX LIBRE >" };
    for (uint8_t i = 0; i < 3; i++) dibujarOpcionCentrada(opcionPantalla3 == i, opciones[i], 24 + i * 9);

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

    if (paginaStats3_5 < PAG_STATS_PRIMERA_LECCION) {
      if (paginaStats3_5 == 0) dibujarMasErrores(statsCarKoch);
      else dibujarTotalesKoch();
      dibujarPieCentrado("pag " + String(paginaStats3_5 + 1) + "/" + String(TOTAL_PAGINAS_STATS) + "   > volver");

    } else if (paginaStats3_5 < PAG_STATS_BORRADO) {
      uint8_t leccInicio = ((paginaStats3_5 - PAG_STATS_PRIMERA_LECCION) * 3) + 1;

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

      dibujarPieCentrado("pag " + String(paginaStats3_5 + 1) + "/" + String(TOTAL_PAGINAS_STATS) + "   > volver");

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
        dibujarPieCentrado("pag " + String(paginaListado3_3 + 1) + "/" + String(paginas) + "   > mas");
      } else {
        String txtPie = (opcionFinListado3_3 == 0 ? "> " : "  ") + String("corregir  ") + 
                        (opcionFinListado3_3 == 1 ? "> " : "  ") + String("rep.");
        dibujarPieCentrado(txtPie);
      }

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

    dibujarOpcionCentrada(opcionPantalla4 == 0, "< PRACTICA DUO >", 28);
    dibujarOpcionCentrada(opcionPantalla4 == 1, "< QSO LIBRE >", 38);

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

    // Pie a mano (21 caracteres) para poder editar el wpm en video inverso: ">20WPM >VER SI >salir"
    display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
    display.setCursor(1, 55);
    display.print(opcionQSO == 0 ? ">" : " ");
    imprimirValor(opcionQSO == 0, String(keyerWpm) + "WPM");
    display.print(opcionQSO == 1 ? " >" : "  ");
    display.print(qsoVerRx ? F("VER SI") : F("VER NO"));
    display.print(opcionQSO == 2 ? " >" : "  ");
    display.print(F("salir"));
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
  String titulo = editorDestino < 0 ? String("INDICATIVO SU") : "MEMORIA " + String(editorDestino + 1);
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

// Pie de OPERAR y QSO: ">mem >SU >tune >salir" (21 caracteres); foco 0..3. "tune" en negativo mientras emite
void dibujarPieTrx(uint8_t foco) {
  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  const char* opciones[] = { "mem", "SU", "tune", "salir" };
  display.setCursor(1, 55);
  for (uint8_t i = 0; i < 4; i++) {
    if (i > 0) display.print(' ');
    display.print(foco == i ? '>' : ' ');
    if (i == 2 && trxSintonizando) display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.print(opciones[i]);
    display.setTextColor(SSD1306_WHITE);
  }
}

void renderizarPantallaTrx() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  switch (subTrx) {
    case TRX_MENU: {
      // Como CW y LoRa: titulo en negativo, tres opciones y "memorias" en el centro del pie
      const char* titulo = " TRANSCEPTOR ";
      int16_t x1, y1;
      uint16_t w, h;
      display.getTextBounds(titulo, 0, 0, &x1, &y1, &w, &h);
      int x = (128 - w) / 2;
      display.fillRect(x - 2, 11, w + 4, h + 4, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      display.setCursor(x, 13);
      display.print(titulo);
      display.setTextColor(SSD1306_WHITE);
      const char* opciones[] = { "< DECODIFICAR >", "< OPERAR >", "< QSO >" };
      for (uint8_t i = 0; i < 3; i++) dibujarOpcionCentrada(opcionTrx == i, opciones[i], 24 + i * 9);
      dibujarBarraAvanzarRetroceder(opcionTrx == TRX_OPCION_BARRA,
                                    opcionTrx == TRX_OPCION_MEMORIAS ? ">memorias" : " memorias");
      break;
    }

    case TRX_DECOD: {
      dibujarEstadoDecod(11);
      dibujarUltimasLineas(trxTextoRx, 21, 3, 9);
      // Pie: ">AUTO >borrar >salir" (el tono se edita girando: AUTO, 400..1000 Hz)
      display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
      display.setCursor(1, 55);
      display.print(opcionTrxSub == 0 ? ">" : " ");
      imprimirValor(opcionTrxSub == 0, trxTonoDecodHz == 0 ? String("AUTO") : String(trxTonoDecodHz) + "Hz");
      display.print(opcionTrxSub == 1 ? " >" : "  ");
      display.print(F("borrar"));
      display.print(opcionTrxSub == 2 ? " >" : "  ");
      display.print(F("salir"));
      break;
    }

    case TRX_OPERAR: {
      display.setCursor(0, 11);
      display.print(opcionTrxSub == 0 ? ">" : " ");
      imprimirValor(opcionTrxSub == 0, String(trxWpm) + "WPM");
      display.print(opcionTrxSub == 1 ? " >" : "  ");
      display.print(trxTonoLocal ? F("TONO SI") : F("TONO NO"));
      dibujarUltimasLineas(trxTextoTx + "_", 21, 2, 9);
      display.setCursor(0, 41);
      if (keyCortadaPorSeguridad) {
        display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
        display.print(F("KEY CORTADA"));
        display.setTextColor(SSD1306_WHITE);
      } else {
        display.print(F("SU: "));
        display.print(trxSu[0] ? trxSu : "--");
      }
      // Duracion del tune, a la derecha (">TUNE 10s" cabe detras de "SU: " y 8 caracteres)
      display.setCursor(74, 41);
      display.print(opcionTrxSub == 2 ? ">" : " ");
      display.print(F("TUNE "));
      imprimirValor(opcionTrxSub == 2, String(trxTuneSeg) + "s");
      dibujarPieTrx(opcionTrxSub >= 3 ? opcionTrxSub - 3 : 255);
      break;
    }

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
  } else if (opcionPantallaConfig == 2 || opcionPantallaConfig == 3) {
    // Apagado automatico y tono: pulsar para editar y girar
    display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
    bool esApagado = (opcionPantallaConfig == 2);
    String txt = esApagado ? "> apagado: " : "> tono: ";
    String valor = esApagado ? textoApagado() : String(frecTonoHz) + " Hz";
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(txt + valor, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((128 - w) / 2, 55);
    display.print(txt);
    imprimirValor(true, valor);
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

// "ok" de la correccion: guarda el % de la leccion y los aciertos y fallos de cada caracter marcado
void enviarResultadoKoch() {
  float pct = 0.0;
  if (totalCaracteresLeccion > 0) pct = ((float)(totalCaracteresLeccion - numeroErrores) / (float)totalCaracteresLeccion) * 100.0;
  registrarResultadoLeccionNVS(leccionCW, pct);

  Serial.print(F("[KOCH] FALLOS:"));
  for (uint16_t i = 0; i < textoCompletoLeccion.length(); i++) {
    char c = textoCompletoLeccion[i];
    if (c == ' ') continue;
    registrarCaracter(statsCarKoch, c, !marcasFallo[i]);
    if (marcasFallo[i]) { Serial.print(' '); Serial.print(c); }
  }
  Serial.printf(" (%u/%u)\n", numeroErrores, totalCaracteresLeccion);
  guardarStatsKochNVS();

  ultimoPctKoch = pct;
  ultimoPctKochMs = millis();
  estadoCW = CW_PANTALLA_RESUMEN_FINAL;
  opcionResumen = 0;
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
EventoBoton eventoSerie = BOTON_NINGUNO;  // PROVISIONAL: evento simulado por serie (ver procesarCapturaSerie)

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
  else if (pantalla == PANTALLA_TRX) { opcionTrx = TRX_OPCION_BARRA; subTrx = TRX_MENU; }
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
  EventoBoton ev = (eventoSerie != BOTON_NINGUNO) ? eventoSerie : leerEventoBoton();  // PROVISIONAL: serie
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
        // Foco: [sig.] -> [PRACTICA DUO] -> [QSO LIBRE] -> [ant.] -> [sig.] ...
        moverFocoAnillo(opcionPantalla4, 2, giro);
      } else if (pulsa) {
        if (opcionPantalla4 == 0) {
          enPantalla4_Sub = true; subpantalla4Activa = 1;
          modoRadioRxActivo = true; opcionDuo4_1 = 3;
          loraEscuchar();  // La radio duerme fuera de Duo y QSO
        } else if (opcionPantalla4 == 1) {
          entrarEnQSO();
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
      // QSO. Foco: 0 wpm, 1 ver, 2 salir. Las palas funcionan con cualquier foco
      if (atras || ejeLarga) {
        salirModoLoRa();
      } else if (giro != 0) {
        if (editandoValor) ajustarValor(keyerWpm, giro, KEYER_WPM_MIN, KEYER_WPM_MAX);
        else opcionQSO = (opcionQSO + OPCIONES_QSO + giro) % OPCIONES_QSO;
      } else if (pulsa) {
        switch (opcionQSO) {
          case 0: editandoValor = !editandoValor; break;
          case 1: qsoVerRx = !qsoVerRx; break;
          case 2: salirModoLoRa(); break;
        }
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
        // Foco: 0 tono (AUTO o 400-1000 Hz), 1 borrar, 2 salir
        if (atras || ejeLarga) trxSalirAlMenu();
        else if (giro != 0) {
          if (editandoValor) ajustarTonoDecod(giro);
          else opcionTrxSub = (opcionTrxSub + 3 + giro) % 3;
        } else if (pulsa) {
          if (opcionTrxSub == 0) editandoValor = !editandoValor;
          else if (opcionTrxSub == 1) trxTextoRx = "";
          else trxSalirAlMenu();
        }
        break;

      case TRX_OPERAR:
        // Foco: 0 wpm, 1 tono local, 2 duracion del tune, 3 mem, 4 SU, 5 tune, 6 salir. Las palas manipulan
        // la emisora con cualquier foco
        if (atras || ejeLarga) trxSalirAlMenu();
        else if (giro != 0) {
          if (editandoValor && opcionTrxSub == 0) ajustarValor(trxWpm, giro, KEYER_WPM_MIN, KEYER_WPM_MAX);
          else if (editandoValor) ajustarValor(trxTuneSeg, giro, TRX_TUNE_MIN_S, TRX_TUNE_MAX_S);
          else opcionTrxSub = (opcionTrxSub + 7 + giro) % 7;
        } else if (pulsa) {
          switch (opcionTrxSub) {
            case 0: editandoValor = !editandoValor; break;
            case 1: trxTonoLocal = !trxTonoLocal; break;
            case 2: editandoValor = !editandoValor; break;
            case 3: trxAbrirListaEnvio(); break;
            case 4: trxAbrirEditor(-1); break;
            case 5: trxTune(); break;
            case 6: trxSalirAlMenu(); break;
          }
        }
        break;

      case TRX_QSO:
        // Foco: 0 mem, 1 SU, 2 tune, 3 salir. El wpm, el tono local y la duracion del tune se eligen en OPERAR
        if (atras || ejeLarga) trxSalirAlMenu();
        else if (giro != 0) opcionTrxSub = (opcionTrxSub + 4 + giro) % 4;
        else if (pulsa) {
          if (opcionTrxSub == 0) trxAbrirListaEnvio();
          else if (opcionTrxSub == 1) trxAbrirEditor(-1);
          else if (opcionTrxSub == 2) trxTune();
          else trxSalirAlMenu();
        }
        break;

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

  } else if (pantallaActual == PANTALLA_PROPAGACION || pantallaActual == PANTALLA_SPOTS ||
             pantallaActual == PANTALLA_CONFIG) {
    // P4 y P5: [sig.] -> [mas >] -> [act.] -> [ant.]
    // P7: [sig.] -> [webconf] -> [fabrica] -> [apagado] -> [tono] -> [ant.]
    bool esProp = (pantallaActual == PANTALLA_PROPAGACION);
    bool esConfig = (pantallaActual == PANTALLA_CONFIG);
    uint8_t& opcion = esProp ? opcionPantallaProp : (esConfig ? opcionPantallaConfig : opcionPantallaSpots);
    uint8_t barra = esProp ? OPCION_BARRA_PROP : (esConfig ? OPCION_BARRA_CONFIG : OPCION_BARRA_SPOTS);
    if (atras) {
      entrarEnPantalla(PANTALLA_INICIO);
    } else if (esConfig && opcion == 2 && editandoValor && giro != 0) {
      ajustarApagado(giro);
      guardarApagadoNVS();
    } else if (esConfig && opcion == 3 && editandoValor && giro != 0) {
      ajustarValor(frecTonoHz, giro * TONO_PASO_HZ, TONO_MIN_HZ, TONO_MAX_HZ);
      aplicarFrecTono(true);  // Pitido de prueba con el tono nuevo
      guardarTonoNVS();
    } else if (esConfig && (opcion == 2 || opcion == 3) && pulsa) {
      editandoValor = !editandoValor;  // Pulsar, girar y pulsar
      if (opcion == 3 && editandoValor) aplicarFrecTono(true);  // Al empezar a editar, se oye el tono actual
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
      // Ajustes. Foco: 0 wpm, 1 sp, 2 leccion, 3 letras, 4 ver, 5 retos, 6 estad., 7 iniciar, 8 volver
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
          case 6:
            enEstadisticasEcho = true;  // Al salir se vuelve aqui
            paginaStatsEcho = 0;
            confirmandoBorrado = false;
            break;
          case 7: iniciarPracticaEcho(); break;
          case 8: salirDeEcho(); break;
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
          // Derecha: pagina siguiente y, en la ultima, alterna entre "corregir" y "rep."
          if (hayMasPaginas) paginaListado3_3++;
          else opcionFinListado3_3 = (opcionFinListado3_3 + 1) % 2;
        } else if (giro < 0) {
          if (!hayMasPaginas && opcionFinListado3_3 == 1) opcionFinListado3_3 = 0;
          else if (paginaListado3_3 > 0) paginaListado3_3--;
        } else if (pulsa) {
          if (hayMasPaginas) paginaListado3_3++;
          else if (opcionFinListado3_3 == 0) {
            estadoCW = CW_PANTALLA_CORREGIR_ERRORES;  // Las marcas se conservan si se vuelve al listado
            focoCorregir = 0;
            paginaCorregir = 0;
          }
          else iniciarPracticaCW();
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
            enviarResultadoKoch();
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
        // Foco: [sig.] -> [KOCH (RX)] -> [ECHO (TX)] -> [TX LIBRE] -> [ant.] -> [sig.] ...
        moverFocoAnillo(opcionPantalla3, OPCION_BARRA_CW, giro);
      } else if (pulsa) {
        if (opcionPantalla3 == 0) {
          enSubpantallaCW = true;
          opcionSubpantalla = 5;
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

// PROVISIONAL: captura de la pantalla por serie. La orden 'p' vuelca el buffer de la OLED (1024 bytes,
// formato del SSD1306: byte = x + (y / 8) * 128, bit = y % 8) en hexadecimal entre dos marcas.
// En el PC, docs/captura_oled.py lo convierte en PNG. Es la copia en RAM: no ve si el panel esta negro
// o atenuado. Para quitarla, borrar esta funcion, su llamada en loop() y el script.
// Tambien simula el encoder, para hacer capturas de todas las pantallas desde el PC: 'd' derecha,
// 'i' izquierda, 'k' pulsar, 'l' pulsacion larga del eje y 'a' atras (PRG corto); quitar con eventoSerie
void procesarCapturaSerie() {
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
  procesarCapturaSerie();  // PROVISIONAL
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
  procesarBateriaYPantalla();
  procesarDiagnostico();
  procesarBrillo();
  procesarApagadoAuto();

  // Ahorro: 1 ms de descanso por vuelta. Sin el, loop() tenia un nucleo al 100 %; con el, la CPU espera
  // en reposo. Las entradas siguen respondiendo (encoder por interrupcion; pulsadores cada ~1 ms) y el
  // CW no se ve afectado (motor y keyer van en sus propias tareas)
  vTaskDelay(1);
}