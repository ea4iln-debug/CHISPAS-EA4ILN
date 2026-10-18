// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: pantalla SOL: sol y linea gris.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// PANTALLA SOL: SOL Y LINEA GRIS (CALCULO LOCAL, SIN WIFI)
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

// Salida, puesta y ventanas de linea gris del dia de "instante", en hora local
void dibujarDiaSol(time_t instante, float lat, float lon, int difLocal) {
  struct tm utc;
  gmtime_r(&instante, &utc);
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

  // Fecha del dia (local), para distinguir HOY de MANANA
  time_t t = instante + difLocal * 60;
  struct tm f;
  gmtime_r(&t, &f);
  char linea[40];
  snprintf(linea, sizeof(linea), "%02d/%02d/%04d HORA LOCAL", f.tm_mday, f.tm_mon + 1, f.tm_year + 1900);
  display.setCursor(0, 42);
  display.print(linea);
}

const char* const NOMBRES_PAGINAS_SOL[3] = { "HOY", "AHORA", "MA\xA4" "ANA" };  // 0xA4 = enye en CP437
#define PAGINAS_SOL 3  // La pagina AMBIENTE (BME280) esta en TIEMPO

// SOL: "actualizar" despierta el GPS (si lo hay) y, si la hora no viene de el, la sincroniza por NTP (enciende el
// WiFi si hace falta; en MANUAL se apaga al llegar la hora o a los WIFI_ESPERA_NTP_MS)
void actualizarHoraSol() {
  if (gpsDetectado()) {
    despertarGps(GPS_TOPE_PERIODICO_MS, "SOL");
    if (fuenteHoraActual() == HORA_GPS) return;
  }
  wifiPorPeticion = true;
  if (estadoWifi == WIFI_CONECTADO) {
    ntpRecibido = false;
    esperaNtpDesdeMs = millis();
    iniciarNtp();
  } else if (estadoWifi == WIFI_APAGADO || estadoWifi == WIFI_SIN_CONEXION) {
    encenderWifi();
  }
  pantallaPendiente = true;
}

void renderizarPantallaSol() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  float lat, lon;
  struct tm local;
  bool conHora = horaLocalValida(local);
  bool locatorOk = locatorALatLon(locatorEstacion, lat, lon);

  // Linea de estado: el WiFi si se esta usando; si no, la hora o el locator
  String estado = textoEstadoDatosWifi();
  if (estado.length() == 0) {
    if (!locatorOk) estado = "LOCATOR NO VALIDO";
    else if (!conHora && estadoWifi == WIFI_CONECTADO) estado = "SINCRONIZANDO HORA...";
    else if (!conHora) estado = "SIN HORA";
    else estado = "LOCATOR " + String(locatorEstacion);
  }

  if (avisoWifiVisible()) {
    dibujarAvisoWifi(" SOL Y LINEA GRIS ");
    return;
  }
  if (subDatos == DATOS_PORTAL) {
    dibujarVistaPortal(focoPortalDatos);
    return;
  }
  if (subDatos == DATOS_MENU) {
    dibujarMenuDatos(" SOL Y LINEA GRIS ", opcionPantallaSol, "< VER DATOS >", "< ACTUALIZAR >", estado);
    return;
  }

  // Paginas
  if (!locatorOk) {
    dibujarSinDatos("LOCATOR NO VALIDO");
    display.setCursor(0, 32);
    display.print(F("CAMBIALO EN CONFIG"));
  } else if (!conHora) {
    dibujarSinDatos(estado);
    if (estadoWifi != WIFI_CONECTADO && estadoWifi != WIFI_CONECTANDO) {
      display.setCursor(0, 32);
      display.print(wifiAuto ? F("PONLA EN WEBCONF") : F("ACTUALIZAR O WEBCONF"));
    }
  } else {
    time_t ahora = time(nullptr);
    struct tm utc;
    gmtime_r(&ahora, &utc);
    int difLocal = (local.tm_hour * 60 + local.tm_min) - (utc.tm_hour * 60 + utc.tm_min);
    if (difLocal > 720) difLocal -= 1440;
    if (difLocal < -720) difLocal += 1440;

    if (paginaSol == 1) {
      // AHORA: altura del sol, linea gris, hora y locator
      DatosSol d = calcularSol(utc, lat, lon);
      bool enGris = d.elevacion >= GRIS_ELEV_MIN && d.elevacion <= GRIS_ELEV_MAX;
      char linea[24];
      snprintf(linea, sizeof(linea), "SOL %+.1f GRADOS", d.elevacion);
      display.setCursor(0, 12);
      display.print(linea);
      display.setCursor(0, 22);
      display.print(enGris ? F("EN LINEA GRIS") : F("FUERA DE LINEA GRIS"));
      snprintf(linea, sizeof(linea), "UTC %02d:%02d LOC %02d:%02d", utc.tm_hour, utc.tm_min, local.tm_hour, local.tm_min);
      display.setCursor(0, 32);
      display.print(linea);
      display.setCursor(0, 42);
      display.print(F("LOCATOR ")); display.print(locatorEstacion);
    } else {
      // HOY o MANANA (el mismo instante, un dia despues)
      dibujarDiaSol(ahora + (paginaSol == 2 ? 86400 : 0), lat, lon, difLocal);
    }
  }
  dibujarPiePaginas(paginaSol, PAGINAS_SOL, NOMBRES_PAGINAS_SOL[paginaSol]);
}

