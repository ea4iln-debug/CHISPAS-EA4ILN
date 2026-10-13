// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: cabecera, pies y piezas comunes de la OLED.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

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
    if (avisoTormenta) nombre = "TORM!";  // Presion bajando deprisa (BME280): el aviso sustituye al nombre
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

