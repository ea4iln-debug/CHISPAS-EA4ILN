// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: diccionario Morse, tabla Koch y caracteres de la trama LoRa v2.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

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
bool locatorAuto = true;       // Con GPS, el locator sale de su posicion (CONFIG > LOCATOR: AUTO o MANUAL)
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

