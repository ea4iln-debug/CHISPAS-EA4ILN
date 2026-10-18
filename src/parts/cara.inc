// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: cara del robot (salvapantallas).
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// CARA DEL ROBOT (SALVAPANTALLAS)
// ============================================================================
// Diseno propio, dibujado con primitivas (sin imagenes): dos ojos y una boca que cambian
// de forma con la expresion. Aparece tras el tiempo elegido en CONF > CONFIG > SALVAPANTALLAS (por defecto 15 s;
// OPCIONES_CARA_S, caraEsperaIdx) sin tocar nada en cualquier pantalla salvo CW, LoRa
// y TRX, y se alterna con esa pantalla. Cualquier giro o pulsacion la quita sin hacer nada mas.
#define CARA_TIEMPO_MS        45000UL  // La cara se queda este tiempo...
#define CARA_INICIO_MS         8000UL  // ...y vuelve la pantalla de antes este tiempo, alternando
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
                     EXP_NERVIOSA, EXP_SORPRESA, EXP_CANSADA, EXP_DORMIDA,
                     // De radioaficionado: antena con ondas (llamando CQ), auriculares (escuchando), bocadillo
                     // con CQ/73/88, guino en Morse con los ojos, y preocupada por el aviso de tormenta (fondo)
                     EXP_ANTENA, EXP_AURICULARES, EXP_BOCADILLO, EXP_MORSE, EXP_TORMENTA };
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
const char* textoBocadillo = "CQ";  // Lo que dice el bocadillo
const char* textoMorseCara = "73";  // Lo que parpadea en Morse
#define MORSE_CARA_DIT_MS   160      // Punto del guino en Morse (una raya, tres)
#define MORSE_CARA_PAUSA_MS 500      // Antes de empezar y al terminar, con los ojos abiertos

bool momentoLlegado(unsigned long t) {
  return (long)(millis() - t) >= 0;
}

bool bateriaBaja() {
  return bateriaActual.estado == BAT_DESCARGA && bateriaActual.porcentaje < 20;
}

bool leccionRecienAprobada() {
  return ultimoPctKoch >= 90 && millis() - ultimoPctKochMs < CARA_FELIZ_RECIENTE_MS;
}

// Expresion a la que vuelve la cara cuando no esta haciendo ningun gesto: preocupada con aviso de tormenta
// (BME280), cansada con la bateria baja
ExpresionCara expresionDeFondo() {
  if (avisoTormenta) return EXP_TORMENTA;
  return bateriaBaja() ? EXP_CANSADA : EXP_NORMAL;
}

// Buena propagacion: el ultimo flujo solar de hamqsl (SFI) es alto
bool caraBuenaPropagacion() {
  return propagacion.validos && propagacion.sfi >= 150;
}

// Linea gris en el locator de la estacion (sol entre GRIS_ELEV_MIN y GRIS_ELEV_MAX); sin hora o sin locator, no
bool caraEnLineaGris() {
  struct tm local;
  float lat, lon;
  if (!horaLocalValida(local) || !locatorALatLon(locatorEstacion, lat, lon)) return false;
  time_t ahora = time(nullptr);
  struct tm utc;
  gmtime_r(&ahora, &utc);
  DatosSol d = calcularSol(utc, lat, lon);
  return d.elevacion >= GRIS_ELEV_MIN && d.elevacion <= GRIS_ELEV_MAX;
}

// Guino en Morse: si en el instante t (ms desde el inicio) los ojos estan cerrados; enviados = caracteres ya
// empezados (para escribirlos debajo). Devuelve tambien, en total, lo que dura el mensaje entero
bool ojosCerradosMorse(const char* texto, long t, uint8_t& enviados, unsigned long& total) {
  bool cerrado = false;
  enviados = 0;
  long pos = MORSE_CARA_PAUSA_MS;
  for (uint8_t i = 0; texto[i]; i++) {
    if (t >= pos) enviados = i + 1;
    for (const char* e = obtenerCodigoMorse(texto[i]); *e; e++) {
      long d = (*e == '-' ? 3 : 1) * MORSE_CARA_DIT_MS;
      if (t >= pos && t < pos + d) cerrado = true;
      pos += d + MORSE_CARA_DIT_MS;
    }
    pos += 2 * MORSE_CARA_DIT_MS;
  }
  total = pos + MORSE_CARA_PAUSA_MS;
  return cerrado;
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
    case EXP_TORMENTA: ojosObjetivo.alto = OJO_ALTO - 8; break;
    case EXP_AURICULARES:
      // Concentrado, mirando a un lado (poco: los ojos no deben tocar los auriculares)
      ojosObjetivo.alto = OJO_ALTO - 8;
      ojosObjetivo.dx = random(2) ? 6 : -6;
      ojosObjetivo.dy = 0;
      break;
    case EXP_BOCADILLO:
      // Mira hacia la izquierda para dejar sitio al bocadillo; en linea gris siempre dice CQ
      ojosObjetivo.dx = -10;
      ojosObjetivo.dy = 0;
      if (caraEnLineaGris()) {
        textoBocadillo = "CQ";
      } else {
        static const char* const TEXTOS[] = { "CQ", "73", "88" };
        textoBocadillo = TEXTOS[random(3)];
      }
      break;
    case EXP_MORSE: {
      static const char* const TEXTOS[] = { "73", "K", "R" };
      textoMorseCara = TEXTOS[random(3)];
      ojosObjetivo.dx = 0;
      ojosObjetivo.dy = 0;
      uint8_t enviados;
      unsigned long total;
      ojosCerradosMorse(textoMorseCara, 0, enviados, total);
      duracionExpresionMs = total;  // Lo que dure el mensaje
      break;
    }
    default: break;
  }
}

// Gestos que necesitan la mirada quieta (si no, los ojos chocan con el bocadillo o los auriculares)
bool miradaFija() {
  return expresionCara == EXP_AURICULARES || expresionCara == EXP_BOCADILLO || expresionCara == EXP_MORSE;
}

/**
 * Gesto al azar. Tras aprobar una leccion salen mas a menudo los alegres; en linea gris, los de llamar (antena y
 * bocadillo con CQ); con buena propagacion, los alegres y la antena.
 */
void hacerGestoAleatorio() {
  // Pesos: contenta, risa, apretados, guino, picara, nerviosa, sorpresa, antena, auriculares, bocadillo, Morse
  static const uint8_t PESOS_NORMAL[]      = { 14, 10,  8, 14, 12, 10, 10,  8,  8,  8,  6 };
  static const uint8_t PESOS_ALEGRE[]      = { 26, 22,  8, 12,  8,  2,  6,  6,  4,  8,  4 };
  static const uint8_t PESOS_LINEA_GRIS[]  = { 10,  8,  6, 10,  8,  6, 10, 16,  8, 18,  8 };
  static const uint8_t PESOS_PROPAGACION[] = { 20, 14,  6, 12, 10,  4,  8, 18,  6, 10,  6 };
  static const ExpresionCara GESTOS[]  = { EXP_FELIZ, EXP_RISA, EXP_APRETADOS, EXP_GUINO, EXP_PICARA,
                                           EXP_NERVIOSA, EXP_SORPRESA, EXP_ANTENA, EXP_AURICULARES,
                                           EXP_BOCADILLO, EXP_MORSE };
  static const uint16_t DURACION_MS[]  = { 2000, 1800, 1500, 700, 2500, 2000, 1200, 3000, 3500, 2500, 0 };
  const uint8_t N = sizeof(GESTOS) / sizeof(GESTOS[0]);
  const uint8_t* pesos = PESOS_NORMAL;
  if (leccionRecienAprobada()) pesos = PESOS_ALEGRE;
  else if (caraEnLineaGris()) pesos = PESOS_LINEA_GRIS;
  else if (caraBuenaPropagacion()) pesos = PESOS_PROPAGACION;

  int total = 0;
  for (uint8_t i = 0; i < N; i++) total += pesos[i];
  int r = random(total);
  for (uint8_t i = 0; i < N; i++) {
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

// Antena en la cabeza con ondas que salen hacia los lados (llamando CQ)
void dibujarAntena(int cx, unsigned long t) {
  display.fillRect(cx - 1, 2, 2, 8, SSD1306_WHITE);
  display.fillCircle(cx, 2, 2, SSD1306_WHITE);
  uint8_t ondas = 1 + (t / 250) % 3;  // 1, 2 y 3 ondas, y vuelta a empezar
  for (uint8_t i = 0; i < ondas; i++) {
    int r = 6 + 4 * i;
    dibujarArco(cx, 3, r, 1, 150, 210);  // Hacia la izquierda
    dibujarArco(cx, 3, r, 1, 330, 390);  // Hacia la derecha
  }
}

// Auriculares: diadema por encima de los ojos y dos almohadillas a los lados (fijos, aunque se muevan los ojos)
void dibujarAuriculares(unsigned long t) {
  display.drawRoundRect(17, 1, 94, 34, 10, SSD1306_WHITE);
  display.drawRoundRect(18, 2, 92, 32, 9, SSD1306_WHITE);
  display.fillRect(20, 12, 88, 26, SSD1306_BLACK);  // Solo queda la diadema: arriba y bajando a los lados
  display.fillRoundRect(10, 16, 10, 20, 3, SSD1306_WHITE);
  display.fillRoundRect(108, 16, 10, 20, 3, SSD1306_WHITE);
  // Lo que escucha: un "-.-" que aparece y desaparece junto al auricular derecho
  if ((t / 400) % 2 == 0) {
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(108, 42);
    display.print(F("-.-"));
  }
}

// Bocadillo arriba a la derecha con el texto (CQ, 73 u 88) en grande
void dibujarBocadillo(const char* texto) {
  display.fillRoundRect(94, 0, 34, 20, 5, SSD1306_WHITE);
  display.fillTriangle(100, 18, 106, 18, 96, 26, SSD1306_WHITE);  // Rabito hacia la boca
  display.fillRoundRect(96, 2, 30, 16, 4, SSD1306_BLACK);
  display.fillTriangle(101, 17, 104, 17, 98, 23, SSD1306_BLACK);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(111 - 6 * (int)strlen(texto), 3);
  display.print(texto);
  display.setTextSize(1);
}

// Nubecilla con un rayo que parpadea (aviso de tormenta), arriba a la derecha
void dibujarTormenta(unsigned long t) {
  display.fillCircle(110, 7, 4, SSD1306_WHITE);
  display.fillCircle(117, 5, 5, SSD1306_WHITE);
  display.fillCircle(123, 8, 4, SSD1306_WHITE);
  display.fillRect(110, 8, 14, 4, SSD1306_WHITE);
  if ((t / 500) % 2 == 0) {
    display.drawLine(118, 13, 114, 19, SSD1306_WHITE);
    display.drawLine(114, 19, 119, 19, SSD1306_WHITE);
    display.drawLine(119, 19, 115, 26, SSD1306_WHITE);
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
    case EXP_TORMENTA:  boca = BOCA_ONDA; break;
    case EXP_AURICULARES: boca = BOCA_RECTA; break;
    case EXP_ANTENA:
    case EXP_BOCADILLO:
      // Hablando: la boca se abre y se cierra
      boca = ((millis() - inicioExpresionMs) / 300) % 2 ? BOCA_O : BOCA_ABIERTA;
      break;
    default: break;
  }
  unsigned long tExpr = millis() - inicioExpresionMs;
  if (expresionCara == EXP_AURICULARES) dibujarAuriculares(tExpr);  // Antes que los ojos: borra por dentro

  // Parpadeo (solo con los ojos abiertos): el alto baja hasta casi cerrar y vuelve en 240 ms
  float alto = ojos.alto;
  if (inicioParpadeoMs != 0) {
    unsigned long t = millis() - inicioParpadeoMs;
    if (t >= 240) inicioParpadeoMs = 0;
    else alto = max(2.0f, alto * fabsf((float)t - 120.0f) / 120.0f);
  }
  uint8_t morseEnviados = 0;
  if (expresionCara == EXP_MORSE) {
    unsigned long total;
    if (ojosCerradosMorse(textoMorseCara, (long)tExpr, morseEnviados, total)) alto = 2;
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
  if (expresionCara == EXP_ANTENA) dibujarAntena(64, tExpr);
  if (expresionCara == EXP_BOCADILLO) dibujarBocadillo(textoBocadillo);
  if (expresionCara == EXP_TORMENTA) dibujarTormenta(millis());
  if (expresionCara == EXP_MORSE && morseEnviados > 0) {
    // Lo enviado hasta ahora, abajo a la derecha
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(104, 56);
    for (uint8_t i = 0; i < morseEnviados; i++) display.print(textoMorseCara[i]);
  }

  display.display();
}

// Pantallas donde puede salir la cara: todas salvo las de practicar (CW, LoRa y TRX), y nunca con los datos
// del portal o el QR a la vista ni durante una actualizacion OTA
bool caraPermitida() {
  if (OPCIONES_CARA_S[caraEsperaIdx] == 0) return false;  // SALVAPANTALLAS = NO
  if (avisoRespot) return false;
  if (pantallaActual == PANTALLA_CW || pantallaActual == PANTALLA_LORA || pantallaActual == PANTALLA_TRX) return false;
  if (pantallaActual == PANTALLA_CONFIG && (subConf == CONF_PORTAL || subConf == CONF_OTA)) return false;
  bool pantallaDatos = pantallaActual == PANTALLA_PROPAGACION || pantallaActual == PANTALLA_SPOTS ||
                       pantallaActual == PANTALLA_SOL;
  if (pantallaDatos && subDatos == DATOS_PORTAL) return false;
  return true;
}

/**
 * Salvapantallas: tras un rato sin tocar nada saca la cara, la alterna con la pantalla en la que se estaba
 * y la anima (parpadeos, miradas y gestos al azar)
 */
void procesarCara() {
  if (!caraPermitida()) {
    if (caraActiva) desactivarCara();
    return;
  }
  unsigned long ahora = millis();
  bool dormir = ahora - ultimaActividadMs >= CARA_DORMIR_MS;

  if (!caraActiva) {
    if (ahora - ultimaActividadMs < OPCIONES_CARA_S[caraEsperaIdx] * 1000UL || ahora - cambioCaraMs < CARA_INICIO_MS) {
      return;
    }
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
    if (momentoLlegado(proximaMiradaMs) && miradaFija()) {
      proximaMiradaMs = ahora + random(1000, 3500);
    } else if (momentoLlegado(proximaMiradaMs)) {
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

