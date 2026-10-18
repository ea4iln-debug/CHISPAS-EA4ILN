// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: menus y paginas de PROP y POTA.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

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
 * estado y la barra ant./sig. Foco: 0 y 1 = opciones, 2 = barra. Con opMedia (POTA: VER PARQUES, que no necesita
 * WiFi) son tres opciones, la media siempre igual, y la linea de estado no cabe: foco 3 = barra
 */
void dibujarMenuDatos(const char* titulo, uint8_t opcion, const char* op0, const char* op1, const String& estado,
                      const char* opMedia = nullptr) {
  dibujarTituloMenu(titulo);
  bool sinConexion = (estadoWifi == WIFI_SIN_CONEXION);
  uint8_t ultima = opMedia ? 2 : 1;
  dibujarOpcionCentrada(opcion == 0, sinConexion ? "< ABRIR WEBCONF >" : op0, 25);
  if (opMedia) dibujarOpcionCentrada(opcion == 1, opMedia, 34);
  dibujarOpcionCentrada(opcion == ultima, sinConexion ? "< REINTENTAR >" : op1, 25 + ultima * 9);
  if (!opMedia && estado.length() > 0) dibujarTituloCentrado(estado, 43);
  dibujarBarraAvanzarRetroceder(opcion == ultima + 1);
}

// Aviso a pantalla completa mientras se conecta, se descarga o se espera la hora (ver avisoWifiVisible)
void dibujarAvisoWifi(const char* titulo) {
  dibujarTituloMenu(titulo);
  const char* texto = actualizandoWifi ? avisoDescarga
                      : (estadoWifi == WIFI_CONECTANDO ? "CONECTANDO WIFI..." : "SINCRONIZANDO HORA...");
  dibujarTituloCentrado(String(texto), 32);
  if (!actualizandoWifi) dibujarPieCentrado("> cancelar");
}

// Cuerpo de una pagina sin datos: el estado del WiFi o el aviso que toque. En MANUAL, sin datos aun, como pedirlos
void dibujarSinDatos(const String& aviso, bool sinDatos) {
  display.setCursor(0, 22);
  display.print(aviso);
  if (sinDatos && !wifiAuto && estadoWifi == WIFI_APAGADO) {
    display.setCursor(0, 32);
    display.print(F("ACTUALIZAR EN EL MENU"));
  }
}

// Antiguedad corta de unos datos para el pie de sus paginas: "AHORA", "12MIN", "3H"
String textoEdadDatos(unsigned long okMs) {
  unsigned long min = (millis() - okMs) / 60000UL;
  if (min < 1) return String("AHORA");
  if (min < 60) return String(min) + "MIN";
  return String(min / 60) + "H";
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

  if (avisoWifiVisible()) {
    dibujarAvisoWifi(" PROPAGACION ");
    return;
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
    dibujarSinDatos(estado, !propagacion.validos);
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

void dibujarPaginasParques();  // Definida con los parques POTA

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

  if (avisoWifiVisible()) {
    dibujarAvisoWifi(" POTA ");
    return;
  }
  if (subDatos == DATOS_PORTAL) {
    dibujarVistaPortal(focoPortalDatos);
    return;
  }
  if (subDatos == DATOS_MENU) {
    // Con una activacion en curso, la opcion del medio es su parque (abre su ficha)
    String medio = actActiva ? "< " + String(spotParque) + " >" : String("< VER PARQUES >");
    dibujarMenuDatos(" POTA ", opcionPantallaSpots, "< VER SPOTS >", "< ACTUALIZAR >", estado, medio.c_str());
    return;
  }
  if (subDatos == DATOS_PARQUES) {
    dibujarPaginasParques();
    return;
  }
  if (subDatos == DATOS_AUTOSPOT) {
    dibujarFichaSpot();
    return;
  }
  if (subDatos == DATOS_EDITOR_SPOT) {
    dibujarEditor();
    return;
  }

  // Paginas
  if (actualizandoWifi || !spotsPotaValidos || numSpotsPota == 0) {
    dibujarSinDatos(estado, !spotsPotaValidos);
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
  // Con datos, su antiguedad (en MANUAL no se refrescan solos)
  dibujarPiePaginas(paginaSpots, paginas, spotsPotaValidos ? textoEdadDatos(spotsPotaOkMs) : String("SPOTS"));
}

