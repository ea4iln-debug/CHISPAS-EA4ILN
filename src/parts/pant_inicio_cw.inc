// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: dibujo de Inicio y de la pantalla CW.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

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

// Ajustes de TX LIBRE (lista): 0 wpm (se edita girando), 1 modo, 2 palas, 3 ritmo (abre sus paginas), 4 salir;
// pie: 5 volver
#define N_AJUSTES_KEYER 5
const char* const NOMBRES_AJUSTES_KEYER[N_AJUSTES_KEYER] = { "WPM", "MODO", "PALAS", "RITMO", "SALIR" };

// ----------------------------------------------------------------------------
// RITMO DE TX LIBRE: tres paginas con lo medido por la tarea del keyer (ritmoKeyer) desde que se entro o se
// borro. Los espacios, en puntos (ideal 3 entre letras y 7 entre palabras)
// ----------------------------------------------------------------------------
#define PAGINAS_RITMO 3
#define RITMO_MIN_LETRAS 5   // Espacios entre letras necesarios para opinar

// Media en decimas de punto (0 si no hay datos)
uint32_t mediaRitmo10(uint32_t suma, uint32_t n) {
  return n ? (suma + n / 2) / n : 0;
}

// Desviacion tipica de los espacios entre letras, en decimas de punto
uint32_t dispersionLetras10() {
  const RitmoKeyer& r = ritmoKeyer;
  if (r.letras < 2) return 0;
  float m = (float)r.sumaLetra10 / r.letras;
  float v = (float)r.sumaLetra10Cuad / r.letras - m * m;
  return (uint32_t)lroundf(sqrtf(max(v, 0.0f)));
}

// "LETRAS   2.6/3  BIEN": media, ideal y juicio (CORTO, BIEN, LARGO); "--" sin datos
void lineaEspacioRitmo(int16_t y, const char* nombre, uint32_t n, uint32_t media10, uint8_t ideal,
                       uint32_t corto10, uint32_t largo10) {
  char linea[22];
  if (n == 0) {
    snprintf(linea, sizeof(linea), "%-8s --/%u", nombre, ideal);
  } else {
    const char* juicio = media10 < corto10 ? "CORTO" : (media10 > largo10 ? "LARGO" : "BIEN");
    snprintf(linea, sizeof(linea), "%-8s %lu.%lu/%u %s", nombre, (unsigned long)(media10 / 10),
             (unsigned long)(media10 % 10), ideal, juicio);
  }
  display.setCursor(0, y);
  display.print(linea);
}

// Consejo segun lo que mas falle (en orden de importancia): tres lineas de hasta 21 caracteres
void consejoRitmo(const char*& l1, const char*& l2, const char*& l3) {
  const RitmoKeyer& r = ritmoKeyer;
  uint32_t letra10 = mediaRitmo10(r.sumaLetra10, r.letras);
  l3 = "";
  if (r.letras < RITMO_MIN_LETRAS) {
    l1 = "AUN HAY POCOS DATOS:"; l2 = "MANIPULA UN RATO Y"; l3 = "VUELVE AQUI";
  } else if (r.desconocidos * 10 > r.caracteres) {
    l1 = "MUCHOS NO LEIDOS (*):"; l2 = "REPASA ESAS LETRAS"; l3 = "MAS DESPACIO";
  } else if (letra10 < 25) {
    l1 = "LETRAS MUY JUNTAS:"; l2 = "DEJA 3 PUNTOS ENTRE"; l3 = "UNA LETRA Y OTRA";
  } else if (letra10 > 40) {
    l1 = "LETRAS SEPARADAS:"; l2 = "A 6 PUNTOS SE PARTE"; l3 = "LA PALABRA";
  } else if (r.titubeos * 10 > r.caracteres) {
    l1 = "TITUBEAS DENTRO DE"; l2 = "LAS LETRAS: PIENSA"; l3 = "LA LETRA ENTERA";
  } else if (r.palabras > 0 && mediaRitmo10(r.sumaPalabra10, r.palabras) > 95) {
    l1 = "ESPACIO ENTRE"; l2 = "PALABRAS LARGO:"; l3 = "UNOS 7 PUNTOS";
  } else if (dispersionLetras10() > 8) {
    l1 = "RITMO IRREGULAR:"; l2 = "BUSCA ESPACIOS"; l3 = "SIEMPRE IGUALES";
  } else {
    l1 = "BUEN RITMO!"; l2 = "PRUEBA A SUBIR"; l3 = "LOS WPM";
  }
}

void dibujarPaginaRitmo() {
  const RitmoKeyer& r = ritmoKeyer;
  char linea[22];
  if (paginaRitmo == 0) {
    // Espacios: entre letras se cierra a los 2 puntos y entre palabras a los 6 (no pueden salir menos)
    lineaEspacioRitmo(12, "LETRAS", r.letras, mediaRitmo10(r.sumaLetra10, r.letras), 3, 25, 40);
    lineaEspacioRitmo(22, "PALABRAS", r.palabras, mediaRitmo10(r.sumaPalabra10, r.palabras), 7, 0, 95);
    uint32_t disp = dispersionLetras10();
    snprintf(linea, sizeof(linea), "REGULAR. +-%lu.%lu", (unsigned long)(disp / 10), (unsigned long)(disp % 10));
    display.setCursor(0, 32);
    display.print(r.letras >= 2 ? linea : "REGULAR. --");
    display.setCursor(0, 42);
    display.print(F("(EN PUNTOS/IDEAL)"));
    dibujarPiePaginas(0, PAGINAS_RITMO, "ESPACIOS");
  } else if (paginaRitmo == 1) {
    snprintf(linea, sizeof(linea), "CARACTERES %10lu", (unsigned long)r.caracteres);
    display.setCursor(0, 12); display.print(linea);
    snprintf(linea, sizeof(linea), "NO LEIDOS (*) %7lu", (unsigned long)r.desconocidos);
    display.setCursor(0, 22); display.print(linea);
    snprintf(linea, sizeof(linea), "TITUBEOS %12lu", (unsigned long)r.titubeos);
    display.setCursor(0, 32); display.print(linea);
    // Velocidad con los espacios de verdad: la del keyer por lo ideal entre lo real
    if (r.msReal > 0) {
      uint32_t wpmReal = (uint32_t)lroundf((float)keyerWpm * r.msIdeal / r.msReal);
      snprintf(linea, sizeof(linea), "VELOCIDAD %2lu DE %2u", (unsigned long)wpmReal, keyerWpm);
    } else {
      snprintf(linea, sizeof(linea), "VELOCIDAD -- DE %2u", keyerWpm);
    }
    display.setCursor(0, 42); display.print(linea);
    dibujarPiePaginas(1, PAGINAS_RITMO, "RESUMEN");
  } else {
    const char *l1, *l2, *l3;
    consejoRitmo(l1, l2, l3);
    dibujarTituloCentrado(l1, 14);
    dibujarTituloCentrado(l2, 26);
    dibujarTituloCentrado(l3, 36);
    dibujarPiePaginas(2, PAGINAS_RITMO, "CONSEJO");
  }
}

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

// Ajustes del Koch (lista): 0-4 se editan girando, 5 texto y 6 refuerzo (pulsar), 7 abre ESTAD.;
// pie: 8 iniciar, 9 volver
#define N_AJUSTES_KOCH 8
const char* const NOMBRES_AJUSTES_KOCH[N_AJUSTES_KOCH] = {
  "WPM", "ESPACIADO", "LECCION", "CARACTERES", "GRUPOS", "TEXTO", "REFUERZO", "ESTADISTICAS"
};

// Nombre del texto del Koch (y del Duo): en la lista y en la practica
const char* nombreTexto(uint8_t texto) {
  switch (texto) {
    case TEXTO_INDICATIVOS: return "INDICAT.";
    case TEXTO_QSO:         return "QSO POTA";
    default:                return "GRUPOS";
  }
}

const char* nombreTextoKoch() { return nombreTexto(textoKoch); }

String valorAjusteKoch(uint8_t i) {
  // Con indicativos o QSO no cuentan la leccion, los caracteres ni el refuerzo: "-"
  bool grupos = textoKoch == TEXTO_GRUPOS;
  switch (i) {
    case 0: return String(wpmCW);
    case 1: return String(espaciadoCW) + "%";
    case 2: return grupos ? String(leccionCW) : String("-");
    case 3: return grupos ? String(caracteresCW) : String("-");
    case 4: return String(gruposCW);
    case 5: return String(nombreTextoKoch());
    case 6: return grupos ? textoRefuerzo(refuerzoKoch, leccionCW) : String("-");
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

  if (enKeyer && keyerEnRitmo) {
    // Paginas de RITMO: se actualizan solas al manipular (las palas siguen funcionando)
    display.setTextSize(1);
    dibujarPaginaRitmo();

  } else if (enKeyer && keyerEnAjustes) {
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
      dibujarTituloCentrado(textoKoch == TEXTO_GRUPOS ? F("- GRUPOS EMITIDOS -") : F("- TEXTO EMITIDO -"));

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
        dibujarPieCentrado(String(paginaListado3_3 + 1) + "/" + String(paginas) + (textoKoch == TEXTO_GRUPOS ? " GRUPOS   > mas" : " TEXTO   > mas"));
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
      if (textoKoch == TEXTO_INDICATIVOS) display.print(F("INDICATIVO: "));
      else if (textoKoch == TEXTO_QSO) display.print(F("FRASE: "));
      else display.print(F("GRUPO: "));
      display.print(grupoActualNum);
      display.print(F(" / "));
      display.print(gruposPractica);

      if (mostrarTextoGrupo && grupoActualStr.length() > 10) {
        // Frase de QSO: no cabe en grande; en pequeno, hasta 2 lineas de 21 caracteres, partida en un espacio
        int corte = grupoActualStr.length();
        if (corte > 21) {
          corte = grupoActualStr.lastIndexOf(' ', 21);
          if (corte <= 0) corte = 21;
        }
        display.setCursor(0, 26);
        display.print(grupoActualStr.substring(0, corte));
        display.setCursor(0, 35);
        String resto = grupoActualStr.substring(corte);
        resto.trim();
        display.print(resto);
      } else if (mostrarTextoGrupo) {
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
