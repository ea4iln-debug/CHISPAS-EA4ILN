/**
 * @file main.cpp
 * @author Mr. Morse EA4ILN Project
 * @brief Firmware para Heltec WiFi LoRa 32 V3 (ESP32-S3) - Aprendizaje CW y LoRa Duo.
 * @version 0.5c - Indicativo y locator en NVS; pantalla 8 de configuracion (portal, reset de fabrica); hora manual en el portal
 * @date 2026-10-08
 */

#define FW_VERSION "0.5c"  // Se muestra en la pantalla 1 y por serie al arrancar: cambiarla en cada version

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

#define KEYER_PADDLE_DIT 2  // Entrada manipulador yambico (Punto)
#define KEYER_PADDLE_DAH 3  // Entrada manipulador yambico (Raya)
#define SPEAKER_PWM_PIN  7  // Salida PWM provisional (GPIO libre del header). El GPIO25 no existe en el S3

#define FREC_AUDIO_HZ    600// Tono Morse local (600 Hz)
#define CANAL_PWM_AUDIO  0  // Canal PWM del ESP32-S3
#define RES_PWM_AUDIO    8  // Resolucion de 8 bits para el PWM

#define VBAT_ADC_PIN    1   // Pin ADC para lectura del divisor de voltaje de la bateria
#define ADC_CTRL_PIN    37  // Habilitacion del divisor de tension de bateria
#define VBAT_MULTIPLIER 5.112f // Factor de calibracion del divisor R1/R2 de Heltec V3
#define TIEMPO_PULSACION_LARGA 600 // Umbral en ms para detectar pulsacion larga del boton PRG
#define TIEMPO_ANTIRREBOTE_MS  30  // Pulsaciones mas cortas se ignoran (rebotes)

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

// Datos de la estacion: se guardan en NVS (espacio "mr_estacion") y se cambian desde el portal WiFi
#define INDICATIVO_POR_DEFECTO "EA4ILN"
#define LOCATOR_POR_DEFECTO    "IM68mu"
char indicativoEstacion[TRAMA_LONG_INDICATIVO + 1] = INDICATIVO_POR_DEFECTO;
char locatorEstacion[7] = LOCATOR_POR_DEFECTO;
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
const uint8_t TOTAL_PANTALLAS = 8;
uint8_t opcionNavegacionP1234 = 1;  // 0 = "ant.", 1 = "sig."

int contadorPulsaciones = 0;
String estadoLoraStr = "INICIANDO...";
bool modoRadioRxActivo = false;

bool ultimoEstadoBoton = HIGH;
unsigned long tiempoInicioPulsacion = 0;
bool procesandoPulsacion = false;

struct StatsLeccion {
  uint16_t vecesCompletada;
  uint8_t maxAcierto;
  uint8_t minAcierto;
};
StatsLeccion statsKoch[40];

bool enSubpantallaCW = false;
bool enPractica3_2 = false;
bool enEstadisticas3_5 = false;
uint8_t opcionPantalla3 = 2; // 0=CONTINUAR, 1=ESTADISTICAS, 2=BARRA INFERIOR (ant/sig)

uint8_t wpmCW = 25;
uint16_t espaciadoCW = 200;
uint8_t leccionCW = 4;
uint8_t caracteresCW = 5;
uint8_t gruposCW = 20;
uint8_t opcionSubpantalla = 5;

enum EstadoCW { 
  CW_IDLE, 
  CW_GENERAR_GRUPO, 
  CW_EMITIENDO_ELEMENTO, 
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

uint8_t paginaStats3_5 = 0;
#define PAGINAS_STATS_3_5 14    // 40 lecciones / 3 por pagina = 14 paginas; la 15 (indice 14) es la de borrado
bool confirmandoBorrado = false;

unsigned long tiempoCWProximo = 0;
int idxCaracter = 0;
int idxElemento = 0;
const char* codigoElementoActual = "";
bool ledEncendido = false;

// Variables de estado para Pantalla 4 (Transceptor LoRa)
bool enPantalla4_Sub = false;  
uint8_t opcionPantalla4 = 2;   // 0=CW DUO, 1=CW QSO, 2=BARRA INFERIOR (ant/sig)
uint8_t subpantalla4Activa = 0;

uint8_t opcionDuo4_1 = 3;      
String grupoDuoTransmitido = "";
String grupoDuoRecibido = "";
bool grupoDuoRevelado = false;

bool emitiendoMorseDuo = false;
String cadenaMorseAEmitirDuo = "";
int idxCharDuo = 0;
int idxElemDuo = 0;
const char* codigoElemDuo = "";
unsigned long tiempoProximoDuo = 0;
bool estadoTonoDuo = false;
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
// de cada grupo. Poner a 0 para quitarlo del firmware.
#define DEBUG_CW 1

#if DEBUG_CW
uint32_t medidaCWNum = 0;
uint32_t medidaCWSuma = 0;
uint32_t medidaCWMax = 0;

void registrarRetrasoCW(unsigned long previsto) {
  uint32_t retraso = millis() - previsto;
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
// MEMORIA PERSISTENTE NVS
// ============================================================================
void cargarDatosNVS() {
  prefs.begin("mr_morse", true);
  wpmCW = prefs.getUChar("wpm", 25);
  espaciadoCW = prefs.getUShort("sp", 200);
  leccionCW = prefs.getUChar("leccion", 4);
  caracteresCW = prefs.getUChar("caract", 5);
  gruposCW = prefs.getUChar("grupos", 20);

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
  prefs.begin("mr_morse", false);
  prefs.putUChar("wpm", wpmCW);
  prefs.putUShort("sp", espaciadoCW);
  prefs.putUChar("leccion", leccionCW);
  prefs.putUChar("caract", caracteresCW);
  prefs.putUChar("grupos", gruposCW);
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

  prefs.begin("mr_morse", false);
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
  prefs.begin("mr_morse", false);
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
  prefs.begin("mr_estacion", true);
  strlcpy(indicativoEstacion, prefs.getString("call", INDICATIVO_POR_DEFECTO).c_str(), sizeof(indicativoEstacion));
  strlcpy(locatorEstacion, prefs.getString("loc", LOCATOR_POR_DEFECTO).c_str(), sizeof(locatorEstacion));
  prefs.end();
}

void guardarEstacionNVS() {
  prefs.begin("mr_estacion", false);
  prefs.putString("call", indicativoEstacion);
  prefs.putString("loc", locatorEstacion);
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

BateriaInfo leerBateria() {
  pinMode(ADC_CTRL_PIN, OUTPUT);
  digitalWrite(ADC_CTRL_PIN, HIGH);
  delay(10);

  analogReadResolution(12);
  analogSetPinAttenuation(VBAT_ADC_PIN, ADC_11db);

  uint32_t sumaMv = 0;
  for (int i = 0; i < 15; i++) {
    sumaMv += analogReadMilliVolts(VBAT_ADC_PIN);
    delay(1);
  }
  float mvPromedio = sumaMv / 15.0;

  digitalWrite(ADC_CTRL_PIN, LOW);

  float voltaje = (mvPromedio * VBAT_MULTIPLIER) / 1000.0;

  BateriaInfo bat;
  if (voltaje < 1.0) {
    bat.voltaje = 0.0;
    bat.porcentaje = 0;
    bat.estado = SOLO_USB;
  } else {
    bat.voltaje = voltaje;
    int pct = map((int)(voltaje * 100), 330, 420, 0, 100);
    bat.porcentaje = constrain(pct, 0, 100);
    bat.estado = BAT_DESCARGA;
  }
  return bat;
}

// leerBateria() bloquea unos 25 ms: se lee cada 30 s fuera de la emision CW y el
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

  if (pantallaActual == 3 && enEstadisticas3_5) {
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

  } else if ((pantallaActual == 3 && enPractica3_2) || (pantallaActual == 4 && subpantalla4Activa == 1)) {
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
    // Hora local, en cuanto se haya sincronizado por NTP (el reloj interno la mantiene sin WiFi)
    struct tm local;
    if (horaLocalValida(local)) {
      char txtHora[6];
      snprintf(txtHora, sizeof(txtHora), "%02d:%02d", local.tm_hour, local.tm_min);
      display.setCursor(0, 0);
      display.print(txtHora);
    }

    String txtRight = "BAT:" + strBat;
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(txtRight, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(128 - w, 0);
    display.print(txtRight);
  }

  display.drawLine(0, 9, 128, 9, SSD1306_WHITE);
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

void dibujarBarraAvanzarRetroceder(bool enfocada) {
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
}

// ============================================================================
// TRANSMISION Y RECEPCION LORA POR INTERRUPCION
// ============================================================================
/**
 * Busca desde idxCharDuo el siguiente caracter con codigo Morse y lo deja en codigoElemDuo.
 * Salta espacios (y avisa en hayEspacio) y caracteres sin codigo. False si no queda ninguno.
 */
bool buscarCaracterDuo(bool& hayEspacio) {
  hayEspacio = false;
  while (idxCharDuo < (int)cadenaMorseAEmitirDuo.length()) {
    char c = cadenaMorseAEmitirDuo.charAt(idxCharDuo);
    if (c == ' ') {
      hayEspacio = true;
    } else {
      codigoElemDuo = obtenerCodigoMorse(c);
      if (strlen(codigoElemDuo) > 0) {
        idxElemDuo = 0;
        return true;
      }
    }
    idxCharDuo++;
  }
  return false;
}

void iniciarEmisionLocalDuo(String cadena, uint8_t wpm) {
  cadena.trim();
  if (cadena.length() == 0) return;

  conmutarEmisionMorse(false);
  cadenaMorseAEmitirDuo = cadena;
  wpmEmisionDuo = wpm;
  idxCharDuo = 0;
  bool hayEspacio;
  if (!buscarCaracterDuo(hayEspacio)) return;

  emitiendoMorseDuo = true;
  estadoTonoDuo = false; 
  tiempoProximoDuo = millis();
}

void procesarEmisionMorseDuoNoBloqueante() {
  if (!emitiendoMorseDuo) return;
  if (millis() < tiempoProximoDuo) return;
#if DEBUG_CW
  registrarRetrasoCW(tiempoProximoDuo);
#endif

  uint32_t tDit = 1200 / wpmEmisionDuo;
  uint32_t tDah = tDit * 3;
  uint32_t tElementoGap = tDit;
  uint32_t tCharGap = (tDit * 3 * espaciadoCW) / 100;

  if (!estadoTonoDuo) {
    char elem = codigoElemDuo[idxElemDuo];
    conmutarEmisionMorse(true);
    estadoTonoDuo = true;

    uint32_t duracionElemento = (elem == '-') ? tDah : tDit;
    tiempoProximoDuo = millis() + duracionElemento;
  } else {
    conmutarEmisionMorse(false);
    estadoTonoDuo = false;
    idxElemDuo++;

    if (codigoElemDuo[idxElemDuo] != '\0') {
      tiempoProximoDuo = millis() + tElementoGap;
    } else {
      idxCharDuo++;
      bool hayEspacio;
      if (buscarCaracterDuo(hayEspacio)) {
        uint32_t tWordGap = (tDit * 7 * espaciadoCW) / 100;
        tiempoProximoDuo = millis() + (hayEspacio ? tWordGap : tCharGap);
      } else {
        emitiendoMorseDuo = false;
        conmutarEmisionMorse(false);
        finEmisionDuo = millis();
#if DEBUG_CW
        informarRetrasoCW("DUO");
#endif

        if (modoRadioRxActivo) {
          radio.startReceive();
        }
      }
    }
  }
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

  // Se redibuja antes de empezar: iniciarEmisionLocalDuo() toma millis() despues
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
void salirModoLoRa() {
  enPantalla4_Sub = false;
  subpantalla4Activa = 0;
  modoRadioRxActivo = false;
  flagPaqueteRecibido = false;

  emitiendoMorseDuo = false;
  estadoTonoDuo = false;
  conmutarEmisionMorse(false);

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
#define PANTALLA_PROPAGACION     5
#define PANTALLA_SPOTS           6
#define WIFI_TIMEOUT_CONEXION_MS 15000
#define PORTAL_TIMEOUT_S         300
#define PORTAL_CLAVE             "mrmorse73"  // Clave del punto de acceso de configuracion (minimo 8)
#define ZONA_HORARIA             "CET-1CEST,M3.5.0,M10.5.0/3"  // Peninsula
#define URL_PROPAGACION          "https://www.hamqsl.com/solarxml.php"
#define URL_POTA                 "https://api.pota.app/spot/activator"
#define REFRESCO_PROPAGACION_MS  (30UL * 60 * 1000)  // hamqsl actualiza cada pocas horas
#define REFRESCO_POTA_MS         (2UL * 60 * 1000)   // POTA no publica limites: se consulta con moderacion
#define MAX_SPOTS_POTA           40
#define SPOTS_POR_PAGINA         4

enum EstadoWifi { WIFI_APAGADO, WIFI_CONECTANDO, WIFI_PORTAL, WIFI_CONECTADO };
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

#define OPCION_BARRA_P5 3
#define OPCION_BARRA_P6 2
uint8_t opcionPantalla5 = OPCION_BARRA_P5;  // 0 = pagina, 1 = actualizar, 2 = wifi (portal), 3 = barra ant./sig.
uint8_t opcionPantalla6 = OPCION_BARRA_P6;  // 0 = mas (pagina), 1 = actualizar, 2 = barra ant./sig.
uint8_t paginaPropagacion = 0;              // 0 = indices, 1 = condiciones por banda

#define PANTALLA_CONFIG 8
#define OPCION_BARRA_P8 2
uint8_t opcionPantalla8 = OPCION_BARRA_P8;  // 0 = portal, 1 = reset de fabrica, 2 = barra ant./sig.
bool confirmandoFabrica = false;

// Pagina "Setup" del portal: datos de la estacion y hora manual (para campo sin internet)
WiFiManagerParameter paramIndicativo("call", "Indicativo", INDICATIVO_POR_DEFECTO, TRAMA_LONG_INDICATIVO);
WiFiManagerParameter paramLocator("loc", "Locator (4 o 6 caracteres)", LOCATOR_POR_DEFECTO, 6);
WiFiManagerParameter paramHora("hora", "Hora local AAAA-MM-DD HH:MM (vacio = no cambiar)", "", 16);

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

  // "im68MU" -> "IM68mu"
  String loc = paramLocator.getValue();
  loc.trim();
  for (size_t i = 0; i < loc.length(); i++) loc[i] = (i < 2) ? toupper(loc[i]) : tolower(loc[i]);
  float lat, lon;
  if (locatorALatLon(loc.c_str(), lat, lon)) strlcpy(locatorEstacion, loc.c_str(), sizeof(locatorEstacion));
  else Serial.printf("[PORTAL] LOCATOR NO VALIDO: %s\n", loc.c_str());

  guardarEstacionNVS();
  Serial.printf("[PORTAL] ESTACION: %s %s\n", indicativoEstacion, locatorEstacion);

  String hora = paramHora.getValue();
  hora.trim();
  if (hora.length() > 0) {
    int a, me, d, h, mi;
    if (sscanf(hora.c_str(), "%d-%d-%d %d:%d", &a, &me, &d, &h, &mi) == 5 &&
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

void abrirPortalWifi() {
  if (nombrePortal[0] == '\0') {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(nombrePortal, sizeof(nombrePortal), "MrMorse-%02X%02X", mac[4], mac[5]);
  }
  static bool parametrosAnadidos = false;
  if (!parametrosAnadidos) {
    wm.addParameter(&paramIndicativo);
    wm.addParameter(&paramLocator);
    wm.addParameter(&paramHora);
    const char* menu[] = { "wifi", "param", "exit" };  // "param" = pagina "Setup"
    wm.setMenu(menu, 3);
    wm.setSaveParamsCallback(guardarParametrosPortal);
    parametrosAnadidos = true;
  }
  paramIndicativo.setValue(indicativoEstacion, TRAMA_LONG_INDICATIVO);
  paramLocator.setValue(locatorEstacion, 6);
  paramHora.setValue("", 16);

  WiFi.mode(WIFI_STA);
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.startConfigPortal(nombrePortal, PORTAL_CLAVE);
  estadoWifi = WIFI_PORTAL;
  Serial.printf("[WIFI] PORTAL ABIERTO: %s\n", nombrePortal);
  pantallaPendiente = true;
}

void encenderWifi() {
  WiFi.mode(WIFI_STA);
  if (!wm.getWiFiIsSaved()) {
    abrirPortalWifi();
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
  http.setUserAgent("MrMorse-EA4ILN/" FW_VERSION);
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
  // P5 y P6 encienden el WiFi al entrar; en P8 solo se usa si se abre el portal.
  // Al ir a cualquier otra pantalla se apaga.
  if (pantallaActual != pantallaAnterior) {
    bool enDatos = (pantallaActual == PANTALLA_PROPAGACION || pantallaActual == PANTALLA_SPOTS);
    bool usaWifi = enDatos || pantallaActual == PANTALLA_CONFIG;
    if (enDatos && estadoWifi == WIFI_APAGADO) encenderWifi();
    if (!usaWifi && estadoWifi != WIFI_APAGADO) apagarWifi();
    pantallaAnterior = pantallaActual;
  }

  switch (estadoWifi) {
    case WIFI_CONECTANDO:
      if (WiFi.status() == WL_CONNECTED) {
        wifiConectado();
      } else if (millis() - inicioConexionWifi > WIFI_TIMEOUT_CONEXION_MS) {
        Serial.println(F("[WIFI] NO CONECTA"));
        abrirPortalWifi();
      }
      break;

    case WIFI_PORTAL:
      if (wm.process()) {
        wifiConectado();
      } else if (!wm.getConfigPortalActive()) {
        Serial.println(F("[WIFI] PORTAL CERRADO POR TIEMPO"));
        apagarWifi();
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
      display.setCursor(0, 22);
      display.print(F("WIFI APAGADO"));
      return true;
    default:
      return false;
  }
}

// ============================================================================
// RENDERIZADO PANTALLAS (1 A 6)
// ============================================================================
void renderizarPantalla1() {
  const char* titulo = " MR. MORSE EA4ILN ";
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(1);
  display.getTextBounds(titulo, 0, 0, &x1, &y1, &w, &h);

  int x = (128 - w) / 2;
  int y = 26;

  display.fillRect(x - 2, y - 2, w + 4, h + 4, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  display.setCursor(x, y);
  display.print(titulo);

  // Version del firmware cargado, centrada bajo el titulo
  display.setTextColor(SSD1306_WHITE);
  String txtVersion = "FW v" + String(FW_VERSION);
  display.getTextBounds(txtVersion.c_str(), 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 40);
  display.print(txtVersion);

  dibujarBarraAvanzarRetroceder(true);
}

void renderizarPantalla2() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 16);
  display.print(F("LORA: "));
  display.println(estadoLoraStr);

  BateriaInfo bat = bateriaActual;
  display.setCursor(0, 32);
  display.print(F("VBAT: "));
  display.print(bat.voltaje, 2);
  display.print(F("V ("));
  display.print(bat.porcentaje);
  display.print(F("%)"));

  dibujarBarraAvanzarRetroceder(true);
}

void renderizarPantalla3() {
  display.setTextColor(SSD1306_WHITE);

  if (!enSubpantallaCW && !enPractica3_2 && !enEstadisticas3_5) {
    display.setTextSize(1);
    
    const char* titulo = " ENTRENADOR CW ";
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

    display.setCursor(15, 28);
    display.print(opcionPantalla3 == 0 ? "> " : "  ");
    display.print(F("< CONTINUAR >"));

    display.setCursor(15, 38);
    display.print(opcionPantalla3 == 1 ? "> " : "  ");
    display.print(F("< ESTADISTICAS >"));

    dibujarBarraAvanzarRetroceder(opcionPantalla3 == 2);

  } else if (enSubpantallaCW && !enPractica3_2) {
    display.setTextSize(1);

    display.setCursor(0, 12);
    display.print(opcionSubpantalla == 0 ? ">" : " ");
    display.print(F("WPM: ")); display.print(wpmCW);

    display.setCursor(64, 12);
    display.print(opcionSubpantalla == 1 ? ">" : " ");
    display.print(F("SP: ")); display.print(espaciadoCW); display.print(F("%"));

    display.setCursor(0, 22);
    display.print(opcionSubpantalla == 2 ? ">" : " ");
    display.print(F("LECCION: ")); display.print(leccionCW);

    display.setCursor(0, 32);
    display.print(opcionSubpantalla == 3 ? ">" : " ");
    display.print(F("CARACTERES: ")); display.print(caracteresCW);

    display.setCursor(0, 42);
    display.print(opcionSubpantalla == 4 ? ">" : " ");
    display.print(F("GRUPOS: ")); display.print(gruposCW);

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

      dibujarPieCentrado(". : mas   - : volver");

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

      dibujarPieCentrado(". : borrar   - : volver");
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
        dibujarPieCentrado(". : mas...");
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
      display.print(numeroErrores);
      display.print(F(" / "));
      display.print(totalCaracteresLeccion);

      display.setCursor(0, 42);
      display.print(opcionPantallaCorregir == 1 ? "> " : "  ");
      display.print(F("ENVIAR  "));
      display.print(opcionPantallaCorregir == 2 ? "> " : "  ");
      display.print(F("REP."));

      dibujarPieCentrado("- : nav   . : mod");

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

      dibujarPieCentrado(". : volver   - : rep.");

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

      dibujarPieCentrado(". : pausa   - : salir");
    }
  }
}

void renderizarPantalla4() {
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
    display.print(F("WPM:")); display.print(wpmCW);

    display.setCursor(52, 11);
    display.print(opcionDuo4_1 == 1 ? ">" : " ");
    display.print(F("SP:")); display.print(espaciadoCW); display.print(F("%"));

    display.setCursor(0, 21);
    display.print(opcionDuo4_1 == 2 ? ">" : " ");
    display.print(F("LECC:")); display.print(leccionCW);

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
        display.setCursor(0, 42);
        display.print(F(" [-: rev]"));
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

    String txtPie4_1 = (opcionDuo4_1 == 3 ? "> " : "  ") + String("enviar  ") + 
                       (opcionDuo4_1 == 4 ? "> " : "  ") + String("volver");
    dibujarPieCentrado(txtPie4_1);

  } else if (subpantalla4Activa == 2) {
    display.setTextSize(1);
    display.setCursor(0, 12);
    display.print(F("KEYER: "));
    if (modoKeyer4_2 == 0) display.print(F("AUTO-PRACTICA"));
    else if (modoKeyer4_2 == 1) display.print(F("QSO REAL-TIME"));
    else display.print(F("MODO ECO"));

    display.setCursor(0, 26);
    display.print(F("PINS: DIT=G2 DAH=G3"));

    display.setCursor(0, 38);
    display.print(F("RSSI: ")); display.print(rssiUltimo); display.print(F("DBM"));

    dibujarPieCentrado(". : modo   - : volver");
  }
}

void renderizarPantalla5() {
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

  if (opcionPantalla5 == OPCION_BARRA_P5) {
    dibujarBarraAvanzarRetroceder(true);
  } else {
    // Maximo 21 caracteres: "> pag  > act.  > wifi"
    String pie = (opcionPantalla5 == 0 ? "> " : "  ") + String("pag ") +
                 (opcionPantalla5 == 1 ? "> " : "  ") + String("act. ") +
                 (opcionPantalla5 == 2 ? "> " : "  ") + String("wifi");
    dibujarPieCentrado(pie);
  }
}

void renderizarPantalla6() {
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
      // Indicativo, frecuencia (kHz) y referencia; lo que no cabe se recorta
      display.setTextWrap(false);
      for (uint8_t i = 0; i < SPOTS_POR_PAGINA; i++) {
        uint8_t idx = paginaSpots * SPOTS_POR_PAGINA + i;
        if (idx >= numSpotsPota) break;
        char linea[40];
        snprintf(linea, sizeof(linea), "%-7s%6s %s", spotsPota[idx].indicativo,
                 spotsPota[idx].frecuencia, spotsPota[idx].referencia);
        display.setCursor(0, 12 + i * 10);
        display.print(linea);
      }
      display.setTextWrap(true);
    }
  }

  if (opcionPantalla6 == OPCION_BARRA_P6) {
    dibujarBarraAvanzarRetroceder(true);
  } else {
    // Maximo 21 caracteres: "> mas 10/10   > act."
    String pie = (opcionPantalla6 == 0 ? "> " : "  ") + String("mas ") + String(paginaSpots + 1) + "/" + String(paginas) +
                 (opcionPantalla6 == 1 ? "   > " : "     ") + String("act.");
    dibujarPieCentrado(pie);
  }
}

// ============================================================================
// PANTALLA 7: SOL Y LINEA GRIS (CALCULO LOCAL, SIN WIFI)
// ============================================================================
// Ecuaciones solares de NOAA (precision de un par de minutos). Ventana de linea gris:
// sol entre GRIS_ELEV_MIN y GRIS_ELEV_MAX grados de altura (unos 30-40 min antes y
// despues de la salida y la puesta en la Peninsula). Necesita la hora sincronizada por NTP.
#define PANTALLA_SOL   7
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

void renderizarPantalla7() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  float lat, lon;
  struct tm local;
  if (!locatorALatLon(locatorEstacion, lat, lon)) {
    display.setCursor(0, 22);
    display.print(F("LOCATOR NO VALIDO"));
  } else if (!horaLocalValida(local)) {
    display.setCursor(0, 12);
    display.print(F("SIN HORA."));
    display.setCursor(0, 22);
    display.print(F("SINCRONIZALA ENTRANDO"));
    display.setCursor(0, 32);
    display.print(F("EN LA PANTALLA 5"));
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

  dibujarBarraAvanzarRetroceder(true);
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
  prefs.begin("mr_morse", false);
  prefs.clear();
  prefs.end();
  prefs.begin("mr_estacion", false);
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

void renderizarPantalla8() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (estadoWifi == WIFI_PORTAL || estadoWifi == WIFI_CONECTANDO) {
    dibujarEstadoWifi();
  } else {
    display.setCursor(0, 12);
    display.print(F("INDICATIVO ")); display.print(indicativoEstacion);
    display.setCursor(0, 22);
    display.print(F("LOCATOR    ")); display.print(locatorEstacion);
    display.setCursor(0, 32);
    display.print(F("FIRMWARE   v" FW_VERSION));
    if (confirmandoFabrica) {
      display.setCursor(0, 42);
      display.print(F("OTRA VEZ: BORRA TODO"));
    }
  }

  if (opcionPantalla8 == OPCION_BARRA_P8) {
    dibujarBarraAvanzarRetroceder(true);
  } else {
    String pie = (opcionPantalla8 == 0 ? "> " : "  ") + String("portal  ") +
                 (opcionPantalla8 == 1 ? "> " : "  ") + String("fabrica");
    dibujarPieCentrado(pie);
  }
}

void actualizarPantalla() {
  display.clearDisplay();
  dibujarEncabezado();

  switch (pantallaActual) {
    case 1: renderizarPantalla1(); break;
    case 2: renderizarPantalla2(); break;
    case 3: renderizarPantalla3(); break;
    case 4: renderizarPantalla4(); break;
    case PANTALLA_PROPAGACION: renderizarPantalla5(); break;
    case PANTALLA_SPOTS: renderizarPantalla6(); break;
    case PANTALLA_SOL: renderizarPantalla7(); break;
    case PANTALLA_CONFIG: renderizarPantalla8(); break;
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
  totalCaracteresLeccion = gruposCW * caracteresCW;

  guardarUltimaConfiguracionNVS();

  estadoCW = CW_GENERAR_GRUPO;
  mostrarTextoGrupo = false;
  tiempoCWProximo = millis();
}

void detenerPracticaCW() {
  conmutarEmisionMorse(false);
  ledEncendido = false;
  enPractica3_2 = false;
  estadoCW = CW_IDLE;
  actualizarPantalla();
}

void generarGrupoKoch() {
  grupoActualStr = "";
  uint8_t maxIdx = caracteresDeLeccion(leccionCW);
  
  for (int i = 0; i < caracteresCW; i++) {
    char c = TABLA_KOCH[random(0, maxIdx)];
    grupoActualStr += c;
  }

  if (textoCompletoLeccion.length() > 0) textoCompletoLeccion += " ";
  textoCompletoLeccion += grupoActualStr;
}

void procesarMaquinaEstadosCW() {
  if (!enPractica3_2 || estadoCW == CW_PAUSADO || estadoCW == CW_IDLE || 
      estadoCW == CW_PANTALLA_3_3_LISTADO || estadoCW == CW_PANTALLA_CORREGIR_ERRORES || 
      estadoCW == CW_PANTALLA_RESUMEN_FINAL) return;

  if (millis() < tiempoCWProximo) return;

  uint32_t tDit = 1200 / wpmCW;  
  uint32_t tDah = tDit * 3;     
  uint32_t tElementoGap = tDit; 
  uint32_t tCharGap = (tDit * 3 * espaciadoCW) / 100;

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
      
      idxCaracter = 0;
      idxElemento = 0;
      codigoElementoActual = obtenerCodigoMorse(grupoActualStr.charAt(0));
      estadoCW = CW_EMITIENDO_ELEMENTO;
      actualizarPantalla();
      break;

    case CW_EMITIENDO_ELEMENTO:
#if DEBUG_CW
      // El primer elemento del grupo no tiene hora programada (viene de CW_GENERAR_GRUPO)
      if (ledEncendido || idxElemento > 0 || idxCaracter > 0) registrarRetrasoCW(tiempoCWProximo);
#endif
      if (!ledEncendido) {
        char elem = codigoElementoActual[idxElemento];
        conmutarEmisionMorse(true);
        ledEncendido = true;

        uint32_t duracionElemento = (elem == '-') ? tDah : tDit;
        tiempoCWProximo = millis() + duracionElemento;
      } else {
        conmutarEmisionMorse(false);
        ledEncendido = false;
        idxElemento++;

        if (codigoElementoActual[idxElemento] != '\0') {
          tiempoCWProximo = millis() + tElementoGap;
        } else {
          idxCaracter++;
          if (idxCaracter < grupoActualStr.length()) {
            codigoElementoActual = obtenerCodigoMorse(grupoActualStr.charAt(idxCaracter));
            idxElemento = 0;
            tiempoCWProximo = millis() + tCharGap;
          } else {
            mostrarTextoGrupo = true;
#if DEBUG_CW
            informarRetrasoCW("KOCH");
#endif
            actualizarPantalla();
            estadoCW = CW_PAUSA_ENTRE_GRUPOS;
            tiempoCWProximo = millis() + 2000;
          }
        }
      }
      break;

    case CW_PAUSA_ENTRE_GRUPOS:
      estadoCW = CW_GENERAR_GRUPO;
      break;

    default:
      break;
  }
}

// ============================================================================
// ENTRADA DE USUARIO: EVENTOS DE BOTON
// ============================================================================
// Toda la logica de menus trabaja con eventos (corta / larga), no con el hardware.
// Cuando monte el rotary y el pulsador de funcion, solo hay que cambiar leerEventoBoton()
// (o anadir otra fuente de eventos); los menus de procesarBoton() no se tocan.
enum EventoBoton { BOTON_NINGUNO, BOTON_CORTA, BOTON_LARGA };

/**
 * Lee el boton PRG sin bloquear y devuelve un evento cuando se SUELTA el boton.
 * Corta: mas de TIEMPO_ANTIRREBOTE_MS. Larga: >= TIEMPO_PULSACION_LARGA.
 */
EventoBoton leerEventoBoton() {
  bool estadoActual = digitalRead(BUTTON_PIN);
  EventoBoton ev = BOTON_NINGUNO;

  if (ultimoEstadoBoton == HIGH && estadoActual == LOW) {
    tiempoInicioPulsacion = millis();
    procesandoPulsacion = true;
  }

  if (ultimoEstadoBoton == LOW && estadoActual == HIGH && procesandoPulsacion) {
    unsigned long duracion = millis() - tiempoInicioPulsacion;
    procesandoPulsacion = false;

    if (duracion >= TIEMPO_PULSACION_LARGA) ev = BOTON_LARGA;
    else if (duracion > TIEMPO_ANTIRREBOTE_MS) ev = BOTON_CORTA;
  }

  ultimoEstadoBoton = estadoActual;
  return ev;
}

// ============================================================================
// PROCESAMIENTO INTERACTIVO DE LOS EVENTOS DE BOTON
// ============================================================================
void procesarBoton() {
  EventoBoton ev = leerEventoBoton();

  if (ev != BOTON_NINGUNO) {

    if (pantallaActual == 4) {
      if (!enPantalla4_Sub) {
        if (ev == BOTON_LARGA) {
          // CICLO ROTATIVO PANTALLA 4 CON PULSACION LARGA (-):
          // [sig.] -> [CW DUO] -> [CW QSO] -> [ant.] -> [sig.] ...
          if (opcionPantalla4 == 2 && opcionNavegacionP1234 == 1) {
            opcionPantalla4 = 0;
          } else if (opcionPantalla4 == 0) {
            opcionPantalla4 = 1;
          } else if (opcionPantalla4 == 1) {
            opcionPantalla4 = 2;
            opcionNavegacionP1234 = 0;
          } else if (opcionPantalla4 == 2 && opcionNavegacionP1234 == 0) {
            opcionPantalla4 = 2;
            opcionNavegacionP1234 = 1;
          }
        } else if (ev == BOTON_CORTA) {
          // PULSACION CORTA (.): EJECUTAR OPCION SELECCIONADA
          if (opcionPantalla4 == 0) {
            enPantalla4_Sub = true; subpantalla4Activa = 1;
            modoRadioRxActivo = true; opcionDuo4_1 = 3;
          } else if (opcionPantalla4 == 1) {
            enPantalla4_Sub = true; subpantalla4Activa = 2;
            modoRadioRxActivo = true;
          } else if (opcionPantalla4 == 2) {
            if (opcionNavegacionP1234 == 0) {
              pantallaActual = 3;
              opcionPantalla3 = 2;          // Por defecto al entrar a P3: barra inferior
              opcionNavegacionP1234 = 1;    // "sig."
            } else {
              pantallaActual = PANTALLA_PROPAGACION;
              opcionPantalla5 = OPCION_BARRA_P5;
              opcionNavegacionP1234 = 1;
            }
            opcionPantalla4 = 2;
          }
        }
      } else if (subpantalla4Activa == 1) {
        if (ev == BOTON_LARGA) {
          if (grupoDuoRecibido.length() > 0 && !grupoDuoRevelado) {
            grupoDuoRevelado = true;
          } else {
            opcionDuo4_1 = (opcionDuo4_1 + 1) % 5;
          }
        } else if (ev == BOTON_CORTA) {
          switch (opcionDuo4_1) {
            case 0: wpmCW++; if (wpmCW > 30) wpmCW = 15; break;
            case 1: espaciadoCW += 10; if (espaciadoCW > 200) espaciadoCW = 100; break;
            case 2: leccionCW++; if (leccionCW > 40) leccionCW = 1; break;
            case 3: {
              uint8_t maxIdx = caracteresDeLeccion(leccionCW);
              String nuevoGrupo = "";
              for (int i = 0; i < caracteresCW; i++) nuevoGrupo += TABLA_KOCH[random(0, maxIdx)];
              transmitirGrupoDuoLoRa(nuevoGrupo);
              break;
            }
            case 4: salirModoLoRa(); break;
          }
        }
      } else if (subpantalla4Activa == 2) {
        if (ev == BOTON_LARGA) {
          salirModoLoRa();
        } else if (ev == BOTON_CORTA) {
          modoKeyer4_2 = (modoKeyer4_2 + 1) % 3;
        }
      }

    } else if (pantallaActual == PANTALLA_PROPAGACION || pantallaActual == PANTALLA_SPOTS ||
               pantallaActual == PANTALLA_CONFIG) {
      // P5: [sig.] -> [pag] -> [act.] -> [wifi] -> [ant.]    P6: [sig.] -> [mas] -> [act.] -> [ant.]
      // P8: [sig.] -> [portal] -> [fabrica] -> [ant.]
      bool esP5 = (pantallaActual == PANTALLA_PROPAGACION);
      bool esP8 = (pantallaActual == PANTALLA_CONFIG);
      uint8_t& opcion = esP5 ? opcionPantalla5 : (esP8 ? opcionPantalla8 : opcionPantalla6);
      uint8_t barra = esP5 ? OPCION_BARRA_P5 : (esP8 ? OPCION_BARRA_P8 : OPCION_BARRA_P6);
      if (ev == BOTON_LARGA) confirmandoFabrica = false;
      if (ev == BOTON_LARGA) {
        if (opcion == barra && opcionNavegacionP1234 == 1) {
          opcion = 0;
        } else if (opcion < barra - 1) {
          opcion++;
        } else if (opcion == barra - 1) {
          opcion = barra;
          opcionNavegacionP1234 = 0;
        } else {
          opcionNavegacionP1234 = 1;
        }
      } else if (ev == BOTON_CORTA) {
        if (opcion == barra) {
          if (opcionNavegacionP1234 == 0) {
            pantallaActual--;  // P5 -> P4, P6 -> P5, P8 -> P7
          } else {
            pantallaActual = (pantallaActual == TOTAL_PANTALLAS) ? 1 : pantallaActual + 1;
          }
          if (pantallaActual == 4) opcionPantalla4 = 2;
          else if (pantallaActual == PANTALLA_PROPAGACION) opcionPantalla5 = OPCION_BARRA_P5;
          else if (pantallaActual == PANTALLA_SPOTS) opcionPantalla6 = OPCION_BARRA_P6;
          opcionNavegacionP1234 = 1;
        } else if (esP8 && opcion == 0) {
          abrirPortalWifi();
        } else if (esP8 && opcion == 1) {
          if (!confirmandoFabrica) confirmandoFabrica = true;
          else resetFabrica();
        } else if (esP5 && opcion == 0) {
          paginaPropagacion = (paginaPropagacion + 1) % 2;
        } else if (esP5 && opcion == 1) {
          pedirActualizacionWifi();
        } else if (esP5 && opcion == 2) {
          abrirPortalWifi();  // Para cambiar de red
        } else if (!esP5 && !esP8 && opcion == 0) {
          uint8_t paginas = max(1, ((int)numSpotsPota + SPOTS_POR_PAGINA - 1) / SPOTS_POR_PAGINA);
          paginaSpots = (paginaSpots + 1) % paginas;
        } else if (!esP5 && !esP8 && opcion == 1) {
          pedirActualizacionWifi();
        }
      }

    } else if (pantallaActual == 3) {
      if (enEstadisticas3_5) {
        if (ev == BOTON_LARGA) {
          enEstadisticas3_5 = false; confirmandoBorrado = false;
        } else if (ev == BOTON_CORTA) {
          if (paginaStats3_5 < PAGINAS_STATS_3_5) paginaStats3_5++;
          else {
            if (!confirmandoBorrado) confirmandoBorrado = true;
            else { borrarTodasLasEstadisticasNVS(); confirmandoBorrado = false; paginaStats3_5 = 0; }
          }
        }
      } else if (enPractica3_2) {
        if (estadoCW == CW_PANTALLA_3_3_LISTADO) {
          int charPorPagina = 63;
          bool hayMasPaginas = ((paginaListado3_3 + 1) * charPorPagina) < textoCompletoLeccion.length();
          if (hayMasPaginas) { paginaListado3_3++; } 
          else {
            if (ev == BOTON_LARGA) opcionFinListado3_3 = (opcionFinListado3_3 + 1) % 2;
            else if (ev == BOTON_CORTA) {
              if (opcionFinListado3_3 == 0) { estadoCW = CW_PANTALLA_CORREGIR_ERRORES; opcionPantallaCorregir = 0; }
              else iniciarPracticaCW();
            }
          }
        } else if (estadoCW == CW_PANTALLA_CORREGIR_ERRORES) {
          if (ev == BOTON_LARGA) opcionPantallaCorregir = (opcionPantallaCorregir + 1) % 3;
          else if (ev == BOTON_CORTA) {
            if (opcionPantallaCorregir == 0) { numeroErrores++; if (numeroErrores > totalCaracteresLeccion) numeroErrores = 0; }
            else if (opcionPantallaCorregir == 1) {
              float pct = 0.0;
              if (totalCaracteresLeccion > 0) pct = ((float)(totalCaracteresLeccion - numeroErrores) / (float)totalCaracteresLeccion) * 100.0;
              registrarResultadoLeccionNVS(leccionCW, pct);
              estadoCW = CW_PANTALLA_RESUMEN_FINAL;
            } else if (opcionPantallaCorregir == 2) iniciarPracticaCW();
          }
        } else if (estadoCW == CW_PANTALLA_RESUMEN_FINAL) {
          if (ev == BOTON_LARGA) iniciarPracticaCW();
          else if (ev == BOTON_CORTA) detenerPracticaCW();
        } else {
          if (ev == BOTON_LARGA) detenerPracticaCW();
          else if (ev == BOTON_CORTA) {
            if (estadoCW == CW_PAUSADO) estadoCW = CW_GENERAR_GRUPO;
            else { estadoCW = CW_PAUSADO; conmutarEmisionMorse(false); ledEncendido = false; }
          }
        }
      } else if (enSubpantallaCW) {
        if (ev == BOTON_LARGA) opcionSubpantalla = (opcionSubpantalla + 1) % 7;
        else if (ev == BOTON_CORTA) {
          switch (opcionSubpantalla) {
            case 0: wpmCW++; if (wpmCW > 30) wpmCW = 15; break;
            case 1: espaciadoCW += 10; if (espaciadoCW > 200) espaciadoCW = 100; break;
            case 2: leccionCW++; if (leccionCW > 40) leccionCW = 1; break;
            case 3: caracteresCW++; if (caracteresCW > 5) caracteresCW = 1; break;
            case 4: gruposCW += 10; if (gruposCW > 50) gruposCW = 10; break;
            case 5: iniciarPracticaCW(); break;
            case 6: enSubpantallaCW = false; break;
          }
        }
      } else {
        if (ev == BOTON_LARGA) {
          // CICLO ROTATIVO PANTALLA 3 CON PULSACION LARGA (-):
          // [sig.] -> [CONTINUAR] -> [ESTADISTICAS] -> [ant.] -> [sig.] ...
          if (opcionPantalla3 == 2 && opcionNavegacionP1234 == 1) {
            opcionPantalla3 = 0;
          } else if (opcionPantalla3 == 0) {
            opcionPantalla3 = 1;
          } else if (opcionPantalla3 == 1) {
            opcionPantalla3 = 2;
            opcionNavegacionP1234 = 0;
          } else if (opcionPantalla3 == 2 && opcionNavegacionP1234 == 0) {
            opcionPantalla3 = 2;
            opcionNavegacionP1234 = 1;
          }
        } else if (ev == BOTON_CORTA) {
          // PULSACION CORTA (.): EJECUTAR OPCION SELECCIONADA
          if (opcionPantalla3 == 0) {
            enSubpantallaCW = true; 
            opcionSubpantalla = 5;
          } else if (opcionPantalla3 == 1) {
            enEstadisticas3_5 = true; 
            paginaStats3_5 = 0; 
            confirmandoBorrado = false;
          } else if (opcionPantalla3 == 2) {
            if (opcionNavegacionP1234 == 0) {
              pantallaActual = 2; // Retroceder a Pantalla 2
              opcionNavegacionP1234 = 1;
            } else {
              pantallaActual = 4; // Avanzar a Pantalla 4
              opcionPantalla4 = 2;
              opcionNavegacionP1234 = 1;
            }
            opcionPantalla3 = 2; 
          }
        }
      }

    } else {
      if (ev == BOTON_LARGA) opcionNavegacionP1234 = (opcionNavegacionP1234 == 0) ? 1 : 0;
      else if (ev == BOTON_CORTA) {
        if (opcionNavegacionP1234 == 0) pantallaActual = (pantallaActual == 1) ? TOTAL_PANTALLAS : pantallaActual - 1;
        else pantallaActual = (pantallaActual == TOTAL_PANTALLAS) ? 1 : pantallaActual + 1;
        contadorPulsaciones++;
        
        if (pantallaActual == 3) {
          opcionPantalla3 = 2;       // Foco en barra inferior
          opcionNavegacionP1234 = 1; // Apuntando por defecto a "sig."
        } else if (pantallaActual == 4) {
          opcionPantalla4 = 2;
          opcionNavegacionP1234 = 1;
        } else if (pantallaActual == PANTALLA_SPOTS) {
          opcionPantalla6 = OPCION_BARRA_P6;
          opcionNavegacionP1234 = 1;
        } else if (pantallaActual == PANTALLA_CONFIG) {
          opcionPantalla8 = OPCION_BARRA_P8;
          confirmandoFabrica = false;
          opcionNavegacionP1234 = 1;
        } else {
          opcionNavegacionP1234 = 1;
        }
      }
    }

    actualizarPantalla();
  }
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

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n[INIT] Mr. Morse EA4ILN v" FW_VERSION "..."));
  
  encenderPerifericos();

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.printf("[INIT] MOTIVO DEL REINICIO: %d\n", (int)esp_reset_reason());

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
    radio.startReceive();
  } else {
    estadoLoraStr = "ERR (" + String(state) + ")";
    Serial.print(F("[LORA ERROR] CODE: "));
    Serial.println(state);
  }

  actualizarPantalla();
}

/**
 * True mientras suena CW (practica Koch o Duo): en ese tiempo no se hace nada
 * que bloquee (lectura de bateria, redibujado), para no alargar los elementos.
 */
bool motorCWActivo() {
  if (emitiendoMorseDuo) return true;
  return enPractica3_2 && estadoCW != CW_PAUSADO && estadoCW != CW_IDLE &&
         estadoCW != CW_PANTALLA_3_3_LISTADO && estadoCW != CW_PANTALLA_CORREGIR_ERRORES &&
         estadoCW != CW_PANTALLA_RESUMEN_FINAL;
}

/**
 * Lee la bateria cada INTERVALO_BATERIA_MS y redibuja solo si algo ha cambiado.
 * Sustituye al refresco fijo cada 3 s.
 */
void procesarBateriaYPantalla() {
  if (motorCWActivo()) return;

  static unsigned long ultimaLecturaBat = 0;
  if (millis() - ultimaLecturaBat >= INTERVALO_BATERIA_MS) {
    ultimaLecturaBat = millis();
    BateriaInfo nueva = leerBateria();
    bool cambia = nueva.estado != bateriaActual.estado ||
                  nueva.porcentaje != bateriaActual.porcentaje ||
                  (pantallaActual == 2 && fabs(nueva.voltaje - bateriaActual.voltaje) >= 0.01);
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

void loop() {
  procesarBoton();
  procesarMaquinaEstadosCW();
  procesarEmisionMorseDuoNoBloqueante();
  procesarRecepcionLoRa();
  procesarColaRxDuo();
  procesarWifi();
  procesarBateriaYPantalla();
}