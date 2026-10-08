/**
 * @file main.cpp
 * @author Mr. Morse EA4ILN Project
 * @brief Firmware para Heltec WiFi LoRa 32 V3 (ESP32-S3) - Aprendizaje CW y LoRa Duo.
 * @version 0.3f - Arreglos de pantalla: estadisticas 3.5 (ancho de linea y leccion 40), signo de apertura, foco al volver a P2
 * @date 2026-10-08
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RadioLib.h>
#include <Preferences.h>

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
const char TABLA_KOCH[] = {
  'K', 'M', 'R', 'S', 'U', 'A', 'I', 'O', 'N', 'E', 
  'T', 'L', 'Y', 'P', 'Q', 'W', 'J', 'Z', 'F', 'C', 
  'D', 'X', 'B', 'G', 'V', 'H', '1', '2', '3', '4', 
  '5', '6', '7', '8', '9', '0', '?', '.', '/', ','
};
const uint8_t TOTAL_CARACTERES_KOCH = sizeof(TABLA_KOCH);

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
  {'0', "-----"}, {'?', "..--.."},{'.', ".-.-.-"},{'/', "-..-."}, {',', "--..--"}
};

const char* obtenerCodigoMorse(char c) {
  for (size_t i = 0; i < sizeof(DICCIONARIO_MORSE)/sizeof(PatronMorse); i++) {
    if (DICCIONARIO_MORSE[i].c == c) return DICCIONARIO_MORSE[i].codigo;
  }
  return "";
}

// ============================================================================
// ESTRUCTURA DE PAQUETE DIGITAL LORA (14 BYTES)
// ============================================================================
struct __attribute__((packed)) TramaMrMorse {
  uint8_t  magicHeader[2]; // "MM"
  uint8_t  tipoMensaje;    // 0x01: GRUPO_KOCH
  char     callsign[6];    // Indicativo (ej. "EA4ILN")
  uint8_t  wpm;            // Velocidad de la trama enviada
  uint8_t  longitudTexto;  // Numero de caracteres validos en payload
  char     payload[5];     // Cadena del grupo transmitido
};

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
const uint8_t TOTAL_PANTALLAS = 4;
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
char colaRxDuo[COLA_RX_TAM][6];
uint8_t colaRxIni = 0;
uint8_t colaRxNum = 0;

uint8_t modoKeyer4_2 = 0;
int rssiUltimo = 0;
float snrUltimo = 0.0;

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

void borrarTodasLasEstadisticasNVS() {
  prefs.begin("mr_morse", false);
  prefs.clear();
  prefs.end();

  wpmCW = 25; espaciadoCW = 200; leccionCW = 4; caracteresCW = 5; gruposCW = 20;
  for (int i = 0; i < 40; i++) {
    statsKoch[i].vecesCompletada = 0;
    statsKoch[i].maxAcierto = 0;
    statsKoch[i].minAcierto = 100;
  }
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

// ============================================================================
// DIBUJO Y RENDERIZADO OLED (BARRA SUPERIOR: MAYÚSCULAS | INFERIOR: MINÚSCULAS)
// ============================================================================
void dibujarEncabezado() {
  BateriaInfo bat = leerBateria();
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
void iniciarEmisionLocalDuo(String cadena) {
  cadena.trim();
  if (cadena.length() == 0) return;

  conmutarEmisionMorse(false);
  cadenaMorseAEmitirDuo = cadena;
  idxCharDuo = 0;
  idxElemDuo = 0;
  codigoElemDuo = obtenerCodigoMorse(cadenaMorseAEmitirDuo.charAt(0));

  if (strlen(codigoElemDuo) == 0) return;

  emitiendoMorseDuo = true;
  estadoTonoDuo = false; 
  tiempoProximoDuo = millis();
}

void procesarEmisionMorseDuoNoBloqueante() {
  if (!emitiendoMorseDuo) return;
  if (millis() < tiempoProximoDuo) return;

  uint32_t tDit = 1200 / wpmCW;  
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
      if (idxCharDuo < (int)cadenaMorseAEmitirDuo.length()) {
        codigoElemDuo = obtenerCodigoMorse(cadenaMorseAEmitirDuo.charAt(idxCharDuo));
        idxElemDuo = 0;
        tiempoProximoDuo = millis() + tCharGap;
      } else {
        emitiendoMorseDuo = false;
        conmutarEmisionMorse(false);
        finEmisionDuo = millis();

        if (modoRadioRxActivo) {
          radio.startReceive();
        }
      }
    }
  }
}

void transmitirGrupoDuoLoRa(String grupo) {
  grupoDuoTransmitido = grupo; 

  TramaMrMorse trama;
  trama.magicHeader[0] = 'M';
  trama.magicHeader[1] = 'M';
  trama.tipoMensaje = 0x01;
  memset(trama.callsign, 0, 6);
  strncpy(trama.callsign, "EA4ILN", 6);
  trama.wpm = wpmCW;
  trama.longitudTexto = min((int)grupo.length(), 5);
  memset(trama.payload, 0, 5);
  memcpy(trama.payload, grupo.c_str(), trama.longitudTexto);

  Serial.print(F("[LORA TX] ENVIANDO POR AIRE: "));
  Serial.println(grupo);

  int estadoTx = radio.transmit((uint8_t*)&trama, sizeof(TramaMrMorse));
  
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

  iniciarEmisionLocalDuo(grupo);
}

/**
 * Guarda un grupo recibido en la cola. Si esta llena se descarta el grupo nuevo.
 */
void encolarGrupoRxDuo(const char* grupo) {
  if (colaRxNum >= COLA_RX_TAM) {
    Serial.println(F("[LORA RX] COLA LLENA, GRUPO DESCARTADO"));
    return;
  }
  uint8_t pos = (colaRxIni + colaRxNum) % COLA_RX_TAM;
  strncpy(colaRxDuo[pos], grupo, 5);
  colaRxDuo[pos][5] = '\0';
  colaRxNum++;
}

/**
 * Si no suena nada y hay grupos en cola, reproduce el siguiente tras el espacio entre palabras.
 */
void procesarColaRxDuo() {
  if (emitiendoMorseDuo || colaRxNum == 0) return;

  uint32_t tDit = 1200 / wpmCW;
  uint32_t tWordGap = (tDit * 7 * espaciadoCW) / 100;
  if (millis() - finEmisionDuo < tWordGap) return;

  grupoDuoRecibido = String(colaRxDuo[colaRxIni]);
  grupoDuoRevelado = false;
  colaRxIni = (colaRxIni + 1) % COLA_RX_TAM;
  colaRxNum--;

  iniciarEmisionLocalDuo(grupoDuoRecibido);
}

void procesarRecepcionLoRa() {
  if (!flagPaqueteRecibido) return;
  flagPaqueteRecibido = false;

  // Fuera del modo LoRa se descarta cualquier aviso pendiente
  if (!modoRadioRxActivo) return;

  // Solo se acepta una trama del tamaño exacto
  if (radio.getPacketLength() != sizeof(TramaMrMorse)) {
    radio.startReceive();
    return;
  }

  TramaMrMorse tramaRx;
  int state = radio.readData((uint8_t*)&tramaRx, sizeof(TramaMrMorse));

  if (state == RADIOLIB_ERR_NONE) {
    if (tramaRx.magicHeader[0] == 'M' && tramaRx.magicHeader[1] == 'M') {
      rssiUltimo = radio.getRSSI();
      snrUltimo = radio.getSNR();

      if (tramaRx.tipoMensaje == 0x01 && enPantalla4_Sub && subpantalla4Activa == 1) {
        char bufferPayload[6] = {0};
        memcpy(bufferPayload, tramaRx.payload, min((int)tramaRx.longitudTexto, 5));
        String grupoRx = String(bufferPayload);
        grupoRx.trim();

        Serial.print(F("[LORA RX INTERRUPCION OK] NUEVO GRUPO REMOTO: "));
        Serial.println(grupoRx);

        if (grupoRx.length() > 0) encolarGrupoRxDuo(grupoRx.c_str());
      }
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
// RENDERIZADO PANTALLAS (1 A 4)
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

  dibujarBarraAvanzarRetroceder(true);
}

void renderizarPantalla2() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 16);
  display.print(F("LORA: "));
  display.println(estadoLoraStr);

  BateriaInfo bat = leerBateria();
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
        display.println(F("  > BORRAR TODO <"));
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

void actualizarPantalla() {
  display.clearDisplay();
  dibujarEncabezado();

  switch (pantallaActual) {
    case 1: renderizarPantalla1(); break;
    case 2: renderizarPantalla2(); break;
    case 3: renderizarPantalla3(); break;
    case 4: renderizarPantalla4(); break;
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
  uint8_t maxIdx = constrain(leccionCW, 1, TOTAL_CARACTERES_KOCH);
  
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
              pantallaActual = 1;
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
              uint8_t maxIdx = constrain(leccionCW, 1, TOTAL_CARACTERES_KOCH);
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
  digitalWrite(VEXT_CTRL, LOW);
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
  Serial.println(F("\n[INIT] Mr. Morse EA4ILN v0.3f..."));
  
  encenderPerifericos();

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("[ERROR] OLED"));
    for (;;);
  }

  randomSeed(esp_random());  // GPIO0 no es ADC en el S3: la semilla anterior era casi fija

  cargarDatosNVS();
  actualizarPantalla();

  Serial.println(F("[LORA] INICIANDO SX1262..."));
  int state = radio.begin(868.0, 125.0, 9, 7, 0x12, 10, 8);
  if (state == RADIOLIB_ERR_NONE) {
    estadoLoraStr = "868MHZ OK";
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

void loop() {
  procesarBoton();
  procesarMaquinaEstadosCW();
  procesarEmisionMorseDuoNoBloqueante();
  procesarRecepcionLoRa();
  procesarColaRxDuo();

  static unsigned long ultimoRefresco = 0;
  if (millis() - ultimoRefresco > 3000) {
    ultimoRefresco = millis();
    actualizarPantalla();
  }
}