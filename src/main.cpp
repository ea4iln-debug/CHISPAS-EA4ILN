#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RadioLib.h>
#include <Preferences.h>

// ============================================================================
// DEFINICIÓN DE PINES NATIVOS Y CONSTANTES (Heltec WiFi LoRa 32 V3 / ESP32-S3)
// ============================================================================
#define OLED_SDA        17  // Pin I2C Data para la pantalla OLED
#define OLED_SCL        18  // Pin I2C Clock para la pantalla OLED
#define OLED_RST        21  // Reset físico de la pantalla OLED
#define VEXT_CTRL       36  // Control MOSFET de alimentación a periféricos (LOW = encendido)

#define BUTTON_PIN      0   // Botón PRG físico integrado en la placa
#define LED_PIN         35  // LED blanco integrado en la Heltec V3 para emisión Morse

#define LORA_NSS        8   // Pin Chip Select del SX1262
#define LORA_DIO1       14  // Pin de Interrupción/DIO1 del SX1262
#define LORA_NRST       12  // Pin Reset del SX1262
#define LORA_BUSY       13  // Pin Busy del SX1262

// Configuración de la Batería LiPo mediante divisor resistivo interno
#define VBAT_ADC_PIN    1   // GPIO 1 (ADC1 Canal 0)
#define ADC_CTRL_PIN    37  // MOSFET de control para lectura de batería (HIGH = encendido)
#define VBAT_MULTIPLIER 5.012f // Multiplicador de escala ajustado para precisión
#define TIEMPO_PULSACION_LARGA 600 // Milisegundos requeridos para detectar pulsación larga (-)

// ============================================================================
// DICCIONARIO MORSE Y TABLA DE CARACTERES KOCH
// ============================================================================
// Secuencia estándar oficial de aprendizaje del Método Koch (40 lecciones)
const char TABLA_KOCH[] = {
  'K', 'M', 'R', 'S', 'U', 'A', 'I', 'O', 'N', 'E', 
  'T', 'L', 'Y', 'P', 'Q', 'W', 'J', 'Z', 'F', 'C', 
  'D', 'X', 'B', 'G', 'V', 'H', '1', '2', '3', '4', 
  '5', '6', '7', '8', '9', '0', '?', '.', '/', ','
};
const uint8_t TOTAL_CARACTERES_KOCH = sizeof(TABLA_KOCH);

// Estructura de mapeo Carácter ASCII -> Código Morse (puntos y rayas)
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

// Función auxiliar para recuperar el patrón Morse de un carácter dado
const char* obtenerCodigoMorse(char c) {
  for (size_t i = 0; i < sizeof(DICCIONARIO_MORSE)/sizeof(PatronMorse); i++) {
    if (DICCIONARIO_MORSE[i].c == c) return DICCIONARIO_MORSE[i].codigo;
  }
  return "";
}

// ============================================================================
// OBJETOS Y VARIABLES GLOBALES DE SISTEMA
// ============================================================================
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST); // Controlador de Pantalla OLED
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY); // Módulo Radio
Preferences prefs; // Manejador de Memoria Flash NVS

uint8_t pantallaActual = 1;         // Pantalla principal activa (1, 2 o 3)
const uint8_t TOTAL_PANTALLAS = 3;  // Total de pantallas navegables en el carrusel base

uint8_t opcionNavegacionP123 = 1;   // 0 = "ant.", 1 = "sig." en la barra inferior de P1, P2 y P3

int contadorPulsaciones = 0;
String estadoLoraStr = "Iniciando...";

// Control de Botón con Anti-rebote (Debounce)
bool ultimoEstadoBoton = HIGH;
unsigned long tiempoInicioPulsacion = 0;
bool procesandoPulsacion = false;

// ============================================================================
// ESTRUCTURA DE ESTADÍSTICAS PERSISTENTES EN MEMORIA NVS (FLASH)
// ============================================================================
struct StatsLeccion {
  uint16_t vecesCompletada; // Intentos/veces finalizadas
  uint8_t maxAcierto;       // Mejor porcentaje registrado
  uint8_t minAcierto;       // Peor porcentaje registrado
};

StatsLeccion statsKoch[40]; // Array estático de rendimiento para las 40 lecciones

// ============================================================================
// VARIABLES DE ESTADO Y AJUSTES DE LAS SUBPANTALLAS (3.1 A 3.5)
// ============================================================================
bool enSubpantallaCW = false;  // Activo cuando estamos en la Subpantalla 3.1
bool enPractica3_2 = false;     // Activo durante la práctica o revisión (3.2 / 3.3 / 3.4)
bool enEstadisticas3_5 = false; // Activo al visualizar historial NVS (3.5)

uint8_t opcionPantalla3 = 0;    // Selector en Pantalla 3: 0 = < continuar >, 1 = < estadisticas >

// Ajustes por defecto de entrenamiento CW
uint8_t wpmCW = 25;             // Velocidad en Palabras Por Minuto (15 a 30)
uint16_t espaciadoCW = 200;     // Espaciado Farnsworth en % (100% a 200%)
uint8_t leccionCW = 4;          // Lección activa seleccionada (1 a 40)
uint8_t caracteresCW = 5;       // Tamaño del bloque/grupo de caracteres (1 a 5)
uint8_t gruposCW = 20;          // Total de grupos por lección (10 a 50)

uint8_t opcionSubpantalla = 5;  // Cursor por defecto en 3.1 (5 = "iniciar")

// Máquina de estados para ejecución y corrección de ejercicios
enum EstadoCW { 
  CW_IDLE, 
  CW_GENERAR_GRUPO, 
  CW_EMITIENDO_ELEMENTO, 
  CW_PAUSA_ENTRE_GRUPOS, 
  CW_PAUSADO, 
  CW_PANTALLA_3_3_LISTADO,      // Muestra grupos emitidos para cotejar
  CW_PANTALLA_CORREGIR_ERRORES, // Pantalla 3.4a: Recogida manual de fallos
  CW_PANTALLA_RESUMEN_FINAL     // Pantalla 3.4b: Porcentaje final de acierto
};
EstadoCW estadoCW = CW_IDLE;

String grupoActualStr = "";        // Cadena del grupo actual en transmisión
String textoCompletoLeccion = ""; // Registro de todo el texto enviado en la sesión
uint8_t grupoActualNum = 0;
bool mostrarTextoGrupo = false;

// Variables de paginación y entrada de usuario
uint16_t totalCaracteresLeccion = 0;
uint16_t numeroErrores = 0;
uint8_t paginaListado3_3 = 0;    
uint8_t opcionFinListado3_3 = 0; // 0 = corregir, 1 = rep.
uint8_t opcionPantallaCorregir = 0; // 0 = ERRORES, 1 = ENVIAR, 2 = REP.

// Variables de la Subpantalla 3.5 (Estadísticas NVS)
uint8_t paginaStats3_5 = 0;     // Navegación por páginas (3 lecciones por página)
bool confirmandoBorrado = false;

// Variables de temporización no bloqueante para el LED Morse
unsigned long tiempoCWProximo = 0;
int idxCaracter = 0;
int idxElemento = 0;
const char* codigoElementoActual = "";
bool ledEncendido = false;

// ============================================================================
// GESTIÓN DE MEMORIA PERSISTENTE (NVS / FLASH)
// ============================================================================
// Carga todos los parámetros y métricas guardadas al arrancar el microcontrolador
void cargarDatosNVS() {
  prefs.begin("mr_morse", true); // Abre en modo solo lectura
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

// Guarda la configuración activa de la lección
void guardarUltimaConfiguracionNVS() {
  prefs.begin("mr_morse", false); // Abre en modo lectura/escritura
  prefs.putUChar("wpm", wpmCW);
  prefs.putUShort("sp", espaciadoCW);
  prefs.putUChar("leccion", leccionCW);
  prefs.putUChar("caract", caracteresCW);
  prefs.putUChar("grupos", gruposCW);
  prefs.end();
}

// Registra la precisión conseguida en la memoria Flash
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

// Reinicia de fábrica todos los datos de usuario en la NVS
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
// LECTURA ANALÓGICA DE BATERÍA
// ============================================================================
enum EstadoAlimentacion { SOLO_USB, BAT_DESCARGA };

struct BateriaInfo {
  float voltaje;
  int porcentaje;
  EstadoAlimentacion estado;
};

BateriaInfo leerBateria() {
  pinMode(ADC_CTRL_PIN, OUTPUT);
  digitalWrite(ADC_CTRL_PIN, HIGH); // Habilita el divisor de tensión por hardware
  delay(10);

  analogReadResolution(12);
  analogSetPinAttenuation(VBAT_ADC_PIN, ADC_11db);

  uint32_t sumaMv = 0;
  for (int i = 0; i < 15; i++) {
    sumaMv += analogReadMilliVolts(VBAT_ADC_PIN);
    delay(1);
  }
  float mvPromedio = sumaMv / 15.0;

  digitalWrite(ADC_CTRL_PIN, LOW); // Apaga el divisor para reducir consumo

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
// FUNCIONES DE DIBUJO Y RENDERIZADO OLED
// ============================================================================
// Dibuja la barra superior constante con la batería y parámetros clave
void dibujarEncabezado() {
  BateriaInfo bat = leerBateria();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  String strBat = (bat.estado == SOLO_USB) ? "PWR" : String(bat.porcentaje) + "%";

  if (pantallaActual == 3 && enEstadisticas3_5) {
    // Progreso global de lecciones aprobadas (>= 90% aciertos)
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

  } else if (pantallaActual == 3 && enPractica3_2) {
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
    String txtRight = "BAT: " + strBat;
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(txtRight, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(128 - w, 0);
    display.print(txtRight);
  }

  display.drawLine(0, 9, 128, 9, SSD1306_WHITE);
}

// Dibuja una línea de texto centrada en el pie de pantalla
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

// Dibuja la barra de navegación "ant." y "sig." alineada a los bordes para Pantallas 1, 2 y 3
void dibujarBarraAvanzarRetroceder() {
  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Izquierda: "ant." (con cursor si está seleccionado)
  display.setCursor(0, 55);
  if (opcionNavegacionP123 == 0) display.print(F("> "));
  else display.print(F("  "));
  display.print(F("ant."));

  // Derecha: "sig." (con cursor si está seleccionado)
  String txtSig = (opcionNavegacionP123 == 1 ? "> " : "  ") + String("sig.");
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(txtSig, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(128 - w, 55);
  display.print(txtSig);
}

// ============================================================================
// RENDERIZADO ESPECÍFICO DE PANTALLAS
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

  dibujarBarraAvanzarRetroceder();
}

void renderizarPantalla2() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 16);
  display.print(F("LoRa: "));
  display.println(estadoLoraStr);

  display.setCursor(0, 32);
  display.print(F("Eventos Btn: "));
  display.print(contadorPulsaciones);

  dibujarBarraAvanzarRetroceder();
}

void renderizarPantalla3() {
  display.setTextColor(SSD1306_WHITE);

  if (!enSubpantallaCW && !enPractica3_2 && !enEstadisticas3_5) {
    // PANTALLA 3 PRINCIPAL (MODIFICADA: Sin "Metodo Koch")
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

    // Selector entre Continuar y Estadísticas en el cuerpo central
    display.setCursor(15, 28);
    display.print(opcionPantalla3 == 0 ? "> " : "  ");
    display.print(F("< continuar >"));

    display.setCursor(15, 38);
    display.print(opcionPantalla3 == 1 ? "> " : "  ");
    display.print(F("< estadisticas >"));

    dibujarBarraAvanzarRetroceder();

  } else if (enSubpantallaCW && !enPractica3_2) {
    // SUBPANTALLA DE CONFIGURACIÓN 3.1
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
                       (opcionSubpantalla == 6 ? "> " : "  ") + String("atras");
    dibujarPieCentrado(txtPie3_1);

  } else if (enEstadisticas3_5) {
    // SUBPANTALLA DE HISTORIAL Y PROGRESO 3.5
    display.setTextSize(1);

    if (paginaStats3_5 < 13) {
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
        display.print(statsKoch[idx].vecesCompletada);
        display.print(F("x MAX:"));
        display.print(statsKoch[idx].maxAcierto);
        display.print(F("% MIN:"));
        if (statsKoch[idx].vecesCompletada == 0) display.print(F("0%"));
        else { display.print(statsKoch[idx].minAcierto); display.print(F("%")); }
      }

      dibujarPieCentrado(". : mas   - : atras");

    } else {
      display.setCursor(10, 16);
      display.println(F("- MEMORIA NVS -"));

      display.setCursor(0, 32);
      if (confirmandoBorrado) {
        display.println(F(" ¿CONFIRMAR BORRADO?"));
      } else {
        display.println(F("  > Borrar Todo <"));
      }

      dibujarPieCentrado(". : borrar   - : atras");
    }

  } else if (enPractica3_2) {
    display.setTextSize(1);

    if (estadoCW == CW_PANTALLA_3_3_LISTADO) {
      // SUBPANTALLA 3.3: LISTADO DE VERIFICACIÓN
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
      // PANTALLA 3.4a: CORRECCIÓN MANUAL DE ERRORES
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
      // PANTALLA 3.4b: RESUMEN FINAL
      float pctAciertos = 0.0;
      if (totalCaracteresLeccion > 0) {
        pctAciertos = ((float)(totalCaracteresLeccion - numeroErrores) / (float)totalCaracteresLeccion) * 100.0;
        if (pctAciertos < 0) pctAciertos = 0;
      }

      display.setCursor(10, 12);
      display.println(F("- RESULTADO KOCH -"));

      display.setCursor(0, 26);
      display.print(F("Aciertos: "));
      display.print(pctAciertos, 1);
      display.println(F("%"));

      display.setCursor(0, 41);
      display.print(F("Repetir si <=90%"));

      dibujarPieCentrado(". : volver   - : rep.");

    } else {
      // EMISIÓN SILENCIOSA EN TIEMPO REAL POR LED (3.2)
      display.setCursor(0, 12);
      display.print(F("Grupo: "));
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
          display.print(F("  TX LED (Oculto)..."));
        }
      }

      dibujarPieCentrado(". : pausa   - : salir");
    }
  }
}

void actualizarPantalla() {
  display.clearDisplay();
  dibujarEncabezado();

  switch (pantallaActual) {
    case 1: renderizarPantalla1(); break;
    case 2: renderizarPantalla2(); break;
    case 3: renderizarPantalla3(); break;
  }

  display.display();
}

// ============================================================================
// MAQUINA DE ESTADOS DE TELEGRAFÍA (EJECUCIÓN MORSE NO BLOQUEANTE)
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
  digitalWrite(LED_PIN, LOW);
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
        digitalWrite(LED_PIN, HIGH);
        ledEncendido = true;

        uint32_t duracionElemento = (elem == '-') ? tDah : tDit;
        tiempoCWProximo = millis() + duracionElemento;
      } else {
        digitalWrite(LED_PIN, LOW);
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
// PROCESAMIENTO INTERACTIVO DEL BOTÓN PRG
// ============================================================================
void procesarBoton() {
  bool estadoActual = digitalRead(BUTTON_PIN);

  if (ultimoEstadoBoton == HIGH && estadoActual == LOW) {
    tiempoInicioPulsacion = millis();
    procesandoPulsacion = true;
    delay(20);
  }

  if (ultimoEstadoBoton == LOW && estadoActual == HIGH && procesandoPulsacion) {
    unsigned long duracion = millis() - tiempoInicioPulsacion;
    procesandoPulsacion = false;

    if (pantallaActual == 3) {
      if (enEstadisticas3_5) {
        if (duracion >= TIEMPO_PULSACION_LARGA) {
          enEstadisticas3_5 = false;
          confirmandoBorrado = false;
        } else if (duracion > 30) {
          if (paginaStats3_5 < 13) {
            paginaStats3_5++;
          } else {
            if (!confirmandoBorrado) {
              confirmandoBorrado = true;
            } else {
              borrarTodasLasEstadisticasNVS();
              confirmandoBorrado = false;
              paginaStats3_5 = 0;
            }
          }
        }

      } else if (enPractica3_2) {
        if (estadoCW == CW_PANTALLA_3_3_LISTADO) {
          int charPorPagina = 63;
          bool hayMasPaginas = ((paginaListado3_3 + 1) * charPorPagina) < textoCompletoLeccion.length();

          if (hayMasPaginas) {
            if (duracion > 30) paginaListado3_3++;
          } else {
            if (duracion >= TIEMPO_PULSACION_LARGA) {
              opcionFinListado3_3 = (opcionFinListado3_3 + 1) % 2;
            } else if (duracion > 30) {
              if (opcionFinListado3_3 == 0) {
                estadoCW = CW_PANTALLA_CORREGIR_ERRORES;
                opcionPantallaCorregir = 0;
              } else {
                iniciarPracticaCW();
              }
            }
          }

        } else if (estadoCW == CW_PANTALLA_CORREGIR_ERRORES) {
          if (duracion >= TIEMPO_PULSACION_LARGA) {
            opcionPantallaCorregir = (opcionPantallaCorregir + 1) % 3;
          } else if (duracion > 30) {
            if (opcionPantallaCorregir == 0) {
              numeroErrores++;
              if (numeroErrores > totalCaracteresLeccion) numeroErrores = 0;
            } else if (opcionPantallaCorregir == 1) {
              float pct = 0.0;
              if (totalCaracteresLeccion > 0) {
                pct = ((float)(totalCaracteresLeccion - numeroErrores) / (float)totalCaracteresLeccion) * 100.0;
              }
              registrarResultadoLeccionNVS(leccionCW, pct);
              estadoCW = CW_PANTALLA_RESUMEN_FINAL;
            } else if (opcionPantallaCorregir == 2) {
              iniciarPracticaCW();
            }
          }

        } else if (estadoCW == CW_PANTALLA_RESUMEN_FINAL) {
          if (duracion >= TIEMPO_PULSACION_LARGA) {
            iniciarPracticaCW();
          } else if (duracion > 30) {
            detenerPracticaCW();
          }

        } else {
          if (duracion >= TIEMPO_PULSACION_LARGA) {
            detenerPracticaCW();
          } else if (duracion > 30) {
            if (estadoCW == CW_PAUSADO) {
              estadoCW = CW_GENERAR_GRUPO;
            } else {
              estadoCW = CW_PAUSADO;
              digitalWrite(LED_PIN, LOW);
              ledEncendido = false;
            }
          }
        }

      } else if (enSubpantallaCW) {
        if (duracion >= TIEMPO_PULSACION_LARGA) {
          opcionSubpantalla = (opcionSubpantalla + 1) % 7;
        } else if (duracion > 30) {
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
        // NAVEGACIÓN DENTRO DE PANTALLA 3 PRINCIPAL
        if (duracion >= TIEMPO_PULSACION_LARGA) {
          // Alterna entre la opción central y la barra inferior ant./sig.
          // 0 = < continuar >, 1 = < estadisticas >, 2 = barra ant./sig.
          if (opcionPantalla3 == 0) opcionPantalla3 = 1;
          else if (opcionPantalla3 == 1) opcionPantalla3 = 2; // Pasa a la barra inferior
          else { opcionPantalla3 = 0; opcionNavegacionP123 = 1; }
        } else if (duracion > 30) {
          if (opcionPantalla3 == 0) {
            enSubpantallaCW = true;
            opcionSubpantalla = 5;
          } else if (opcionPantalla3 == 1) {
            enEstadisticas3_5 = true;
            paginaStats3_5 = 0;
            confirmandoBorrado = false;
          } else if (opcionPantalla3 == 2) {
            if (opcionNavegacionP123 == 0) pantallaActual = 2; // ant. -> P2
            else pantallaActual = 1; // sig. -> P1
            opcionPantalla3 = 0;
          }
        }
      }

    } else {
      // NAVEGACIÓN DENTRO DE PANTALLAS 1 Y 2
      if (duracion >= TIEMPO_PULSACION_LARGA) {
        // Conmuta el cursor de la barra inferior entre "ant." (0) y "sig." (1)
        opcionNavegacionP123 = (opcionNavegacionP123 == 0) ? 1 : 0;
      } else if (duracion > 30) {
        // Ejecuta el cambio de pantalla según la opción seleccionada
        if (opcionNavegacionP123 == 0) {
          pantallaActual = (pantallaActual == 1) ? TOTAL_PANTALLAS : pantallaActual - 1;
        } else {
          pantallaActual = (pantallaActual == TOTAL_PANTALLAS) ? 1 : pantallaActual + 1;
        }
        contadorPulsaciones++;
      }
    }

    actualizarPantalla();
  }

  ultimoEstadoBoton = estadoActual;
}

// ============================================================================
// CONFIGURACIÓN INICIAL Y BUCLE PRINCIPAL
// ============================================================================
void encenderPerifericos() {
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW); // Alimenta pantallas y componentes externos
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  delay(100);
}

void setup() {
  Serial.begin(115200);
  encenderPerifericos();

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Error OLED"));
    for (;;);
  }

  randomSeed(analogRead(0) + millis());

  // Cargar preferencias del usuario desde NVS
  cargarDatosNVS();

  actualizarPantalla();

  // Inicialización de la radio LoRa SX1262 a 868 MHz
  int state = radio.begin(868.0, 125.0, 9, 7, 0x12, 10, 8);
  if (state == RADIOLIB_ERR_NONE) {
    estadoLoraStr = "868MHz OK";
  } else {
    estadoLoraStr = "ERR (" + String(state) + ")";
  }

  actualizarPantalla();
}

void loop() {
  // 1. Escuchar la entrada por botón
  procesarBoton();

  // 2. Ejecutar la temporización no bloqueante del motor CW
  procesarMaquinaEstadosCW();

  // 3. Refrescar datos de entorno cada 3 segundos
  static unsigned long ultimoRefresco = 0;
  if (millis() - ultimoRefresco > 3000) {
    ultimoRefresco = millis();
    actualizarPantalla();
  }
}