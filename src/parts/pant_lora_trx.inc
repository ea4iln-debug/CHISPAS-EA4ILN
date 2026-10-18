// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: dibujo de las pantallas LoRa y TRX.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

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

// Texto en dos lineas de 21, partido por los espacios (una palabra mas larga que la linea se corta); lo que no
// quepa en las dos no se ve
void dibujarDosLineasPorPalabras(const String& t, int16_t y, uint8_t alto) {
  const unsigned ANCHO = 21;
  String l1 = t, l2 = "";
  if (t.length() > ANCHO) {
    int corte = t.lastIndexOf(' ', ANCHO);  // El espacio en la posicion 21 tambien vale
    if (corte <= 0) corte = ANCHO;
    l1 = t.substring(0, corte);
    l2 = t.substring(t.charAt(corte) == ' ' ? corte + 1 : corte);
    if (l2.length() > ANCHO) l2 = l2.substring(0, ANCHO);
  }
  display.setCursor(0, y);
  display.print(l1);
  if (l2.length() > 0) {
    display.setCursor(0, y + alto);
    display.print(l2);
  }
}

// Ajustes ("menu") de PRACTICA DUO y de QSO LIBRE
#define N_AJUSTES_DUO 5
const char* const NOMBRES_AJUSTES_DUO[N_AJUSTES_DUO] = { "WPM", "ESPACIADO", "LECCION", "TEXTO", "SALIR" };
#define N_AJUSTES_QSO_LORA 3
const char* const NOMBRES_AJUSTES_QSO_LORA[N_AJUSTES_QSO_LORA] = { "WPM", "VER LO RECIBIDO", "SALIR" };

String valorAjusteDuo(uint8_t i) {
  switch (i) {
    case 0: return String(wpmCW);
    case 1: return String(espaciadoCW) + "%";
    case 2: return String(leccionCW);
    case 3: return String(nombreTexto(textoDuo));
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

    // La cabecera ya da el wpm y el espaciado: aqui, la leccion y las letras por grupo, o el texto (se cambian
    // en "menu"), y el RSSI de la ultima trama recibida (0 = aun no se ha recibido nada)
    display.setCursor(0, 11);
    if (textoDuo == TEXTO_GRUPOS) display.print("LECCION " + String(leccionCW) + " x" + String(caracteresCW));
    else display.print(textoDuo == TEXTO_QSO ? F("QSO POTA") : F("INDICATIVOS"));
    if (rssiUltimo != 0) {
      String txtRssi = String(rssiUltimo) + "dBm";
      display.setCursor(128 - txtRssi.length() * 6, 11);
      display.print(txtRssi);
    }

    // TX y RX, de hasta dos lineas cada uno (una frase de QSO POTA no cabe en una). Lo recibido puede ser
    // una frase aunque aqui se envien grupos: depende del ajuste de la otra placa
    String tx = grupoDuoTransmitido.length() > 0 ? "[" + grupoDuoTransmitido + "]" : String("--");
    dibujarDosLineasPorPalabras("TX -> " + tx, 21, 8);

    String rx;
    if (grupoDuoRecibido.length() == 0) rx = "ESCUCHANDO...";
    else if (grupoDuoRevelado) rx = "[" + grupoDuoRecibido + "]";
    else rx = "LED (OCULTO)";
    dibujarDosLineasPorPalabras("RX <- " + rx, 37, 8);

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
                  editorDestino == EDITOR_SPOT ? String("ID ACTIVADOR") :
                  editorDestino == EDITOR_SPOT_TEXTO ? String("COMENTARIO") :
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

