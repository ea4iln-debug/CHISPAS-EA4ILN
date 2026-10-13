// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: GPS NEO-6M (hora, locator y altitud).
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// GPS NEO-6M EN UART1 (OPCIONAL)
// ============================================================================
// TX del GPS a GPIO47 (J2-13), su RX desde GPIO39 (J3-10), 9600 bd, siempre alimentado (sin transistor). Se duerme
// con UBX-RXM-PMREQ (backup, sin limite) y se despierta con una rafaga de 0xFF por su RX (probado con prueba_gps).
// Duerme casi siempre: se despierta al arrancar o despertar Chispas (tope GPS_TOPE_ARRANQUE_MS sin fijo), cada
// GPS_INTERVALO_MS (tope GPS_TOPE_PERIODICO_MS) y mientras se ve STATUS > GPS. Cada vez que se despierta ("ventana")
// pone la hora UTC en el reloj del sistema, toma GPS_MUESTRAS_ALT altitudes para la media y, al cerrar la ventana,
// el locator (si esta en AUTO) y la altitud de referencia (QNH del BME280 e historial de presion a nivel del mar).
// Sin modulo (no llega NMEA valido al arrancar) queda AUSENTE y todo funciona como sin GPS.
#define GPS_RX_PIN            47      // Llega el TX del GPS (NMEA)
#define GPS_TX_PIN            39      // Va al RX del GPS (UBX y rafaga para despertarlo)
#define GPS_BAUD              9600
#define GPS_DETECCION_MS      6000UL  // Al arrancar, sin NMEA valido en este tiempo: no hay GPS
#define GPS_TOPE_ARRANQUE_MS  (5UL * 60 * 1000)
#define GPS_TOPE_PERIODICO_MS 60000UL
#define GPS_INTERVALO_MS      (10UL * 60 * 1000)  // Entre ventanas (como el historial de presion)
#define GPS_SIN_DATOS_MS      3000UL  // Despierto y sin NMEA: se repite la rafaga de 0xFF
#define GPS_MUESTRAS_ALT      30      // Altitudes por ventana (una por segundo con fijo 3D)
#define GPS_MUESTRAS_ALT_MIN  5       // Menos, al llegar al tope: la ventana no cambia la altitud
#define GPS_ALT_CAMBIO_M      30.0f   // La referencia solo cambia si la media se aparta mas (ruido del GPS: +-15 m)
#define GPS_REINTENTOS_DORMIR 3       // Si sigue mandando NMEA tras PMREQ, se repite

enum EstadoGps { GPS_DETECTANDO, GPS_AUSENTE, GPS_DORMIDO, GPS_BUSCANDO, GPS_CON_FIJO };
EstadoGps estadoGps = GPS_DETECTANDO;

// Ultimo dato bueno (se sigue usando con el GPS dormido)
bool gpsHayPosicion = false;
float gpsLat = 0, gpsLon = 0, gpsAltitud = 0, gpsHdop = 0;
uint8_t gpsSatelites = 0;
unsigned long gpsUltimoFijoMs = 0;
float gpsAltRef = NAN;      // Altitud de referencia (media de una ventana); NAN sin ella

// Ventana en curso
unsigned long gpsDespiertoMs = 0, gpsTopeMs = 0, gpsUltimoNmeaMs = 0, gpsUltimaRafagaMs = 0;
unsigned long gpsDormidoMs = 0, gpsFinVentanaMs = 0;
float gpsSumaAlt = 0;
uint8_t gpsNAlt = 0;
bool gpsVentanaHecha = false, gpsHoraVentana = false;
uint8_t gpsReintentosDormir = 0;

bool gpsDetectado() {
  return estadoGps != GPS_AUSENTE && estadoGps != GPS_DETECTANDO;
}

bool gpsDespierto() {
  return estadoGps == GPS_BUSCANDO || estadoGps == GPS_CON_FIJO;
}

// UBX con checksum Fletcher de 8 bits
void gpsEnviarUbx(uint8_t clase, uint8_t id, const uint8_t* datos, uint16_t largo) {
  uint8_t cab[6] = { 0xB5, 0x62, clase, id, (uint8_t)(largo & 0xFF), (uint8_t)(largo >> 8) };
  uint8_t ckA = 0, ckB = 0;
  for (int i = 2; i < 6; i++) { ckA += cab[i]; ckB += ckA; }
  for (uint16_t i = 0; i < largo; i++) { ckA += datos[i]; ckB += ckA; }
  Serial1.write(cab, 6);
  if (largo) Serial1.write(datos, largo);
  Serial1.write(ckA);
  Serial1.write(ckB);
}

void gpsRafagaDespertar() {
  for (uint8_t i = 0; i < 16; i++) Serial1.write(0xFF);
  gpsUltimaRafagaMs = millis();
}

// UBX-RXM-PMREQ: duracion 0 (sin limite) y flags bit 1 = backup
void gpsPedirDormir() {
  const uint8_t datos[8] = { 0, 0, 0, 0, 0x02, 0, 0, 0 };
  gpsEnviarUbx(0x02, 0x41, datos, sizeof(datos));
}

void gpsEmpezarVentana(unsigned long topeMs) {
  gpsDespiertoMs = gpsUltimoNmeaMs = millis();
  gpsTopeMs = topeMs;
  gpsSumaAlt = 0;
  gpsNAlt = 0;
  gpsVentanaHecha = gpsHoraVentana = false;
}

void despertarGps(unsigned long topeMs, const char* motivo) {
  if (estadoGps != GPS_DORMIDO) return;
  gpsRafagaDespertar();
  gpsEmpezarVentana(topeMs);
  estadoGps = GPS_BUSCANDO;
  pantallaPendiente = true;
  Serial.printf("[GPS] DESPIERTA (%s)\n", motivo);
}

void dormirGps() {
  gpsPedirDormir();
  estadoGps = GPS_DORMIDO;
  gpsDormidoMs = millis();
  gpsReintentosDormir = 0;
  pantallaPendiente = true;
  Serial.println(F("[GPS] DUERME"));
}

// Al arrancar o despertar Chispas: el GPS sigue alimentado y puede estar dormido de la sesion anterior
void iniciarGps() {
  gpio_hold_dis((gpio_num_t)GPS_TX_PIN);  // Retenido en ALTO durante el deep sleep (reposo de la UART)
  Serial1.setRxBufferSize(1024);  // La descarga de PROP o POTA bloquea el loop 1-3 s (~1 KB de NMEA por segundo)
  Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  gpsRafagaDespertar();
  gpsEmpezarVentana(GPS_TOPE_ARRANQUE_MS);
  estadoGps = GPS_DETECTANDO;
}

// Grados decimales de "ddmm.mmmm" (lat) o "dddmm.mmmm" (lon) y su hemisferio
float gpsGrados(const char* valor, char hemisferio) {
  double v = atof(valor);
  double g = (int)(v / 100) + fmod(v, 100.0) / 60.0;
  return (hemisferio == 'S' || hemisferio == 'W') ? -g : g;
}

// Locator de 6 caracteres ("IN80dk") de una posicion
void latLonALocator(float lat, float lon, char* loc) {
  double x = constrain(lon + 180.0, 0.0, 359.9999), y = constrain(lat + 90.0, 0.0, 179.9999);
  loc[0] = 'A' + (int)(x / 20);
  loc[1] = 'A' + (int)(y / 10);
  loc[2] = '0' + (int)(fmod(x, 20.0) / 2);
  loc[3] = '0' + (int)fmod(y, 10.0);
  loc[4] = 'a' + (int)(fmod(x, 2.0) * 12);
  loc[5] = 'a' + (int)(fmod(y, 1.0) * 24);
  loc[6] = '\0';
}

// Hora y fecha de una RMC valida al reloj del sistema (una vez por ventana; si ya era del GPS, solo si se aparta
// mas de 1 s)
void gpsPonerHora(const char* hora, const char* fecha) {
  if (strlen(hora) < 6 || strlen(fecha) != 6) return;
  auto dos = [](const char* p) { return (p[0] - '0') * 10 + (p[1] - '0'); };
  time_t t = epochUtc(2000 + dos(fecha + 4), dos(fecha + 2), dos(fecha), dos(hora), dos(hora + 2), dos(hora + 4));
  gpsHoraVentana = true;
  FuenteHora antes = fuenteHoraActual();
  if (antes == HORA_GPS && labs((long)(time(nullptr) - t)) <= 1) return;
  ponerHora(t, HORA_GPS);
  Serial.printf("[GPS] HORA UTC 20%.2s-%.2s-%.2s %.2s:%.2s:%.2s (ANTES: %s)\n", fecha + 4, fecha + 2, fecha, hora,
                hora + 2, hora + 4, NOMBRES_FUENTE_HORA[antes]);
}

// Campo n (0 = tipo de frase) de una frase NMEA
void gpsCampo(const char* l, int n, char* dest, size_t tam) {
  size_t i = 0;
  while (n > 0 && *l) { if (*l == ',') n--; l++; }
  while (*l && *l != ',' && *l != '*' && i < tam - 1) dest[i++] = *l++;
  dest[i] = '\0';
}

// Comprueba el checksum ("*XX": XOR de lo que hay entre '$' y '*')
bool gpsNmeaValida(const char* l) {
  const char* ast = strchr(l, '*');
  if (l[0] != '$' || !ast || strlen(ast) < 3) return false;
  uint8_t x = 0;
  for (const char* p = l + 1; p < ast; p++) x ^= (uint8_t)*p;
  return x == (uint8_t)strtol(ast + 1, nullptr, 16);
}

void gpsProcesarLinea(const char* l) {
  if (!gpsNmeaValida(l)) return;
  gpsUltimoNmeaMs = millis();
  if (estadoGps == GPS_DETECTANDO) {
    estadoGps = GPS_BUSCANDO;
    pantallaPendiente = true;
    Serial.println(F("[GPS] DETECTADO"));
  }
  if (!gpsDespierto()) return;
  char c[16];
  if (strncmp(l + 3, "GGA", 3) == 0) {
    gpsCampo(l, 6, c, sizeof(c));
    int calidad = atoi(c);
    gpsCampo(l, 7, c, sizeof(c));
    gpsSatelites = atoi(c);
    if (calidad == 0) {
      if (estadoGps == GPS_CON_FIJO) { estadoGps = GPS_BUSCANDO; pantallaPendiente = true; }
      return;
    }
    char lat[16], ns[2], lon[16], ew[2];
    gpsCampo(l, 2, lat, sizeof(lat)); gpsCampo(l, 3, ns, sizeof(ns));
    gpsCampo(l, 4, lon, sizeof(lon)); gpsCampo(l, 5, ew, sizeof(ew));
    if (!lat[0] || !lon[0]) return;
    gpsLat = gpsGrados(lat, ns[0]);
    gpsLon = gpsGrados(lon, ew[0]);
    gpsCampo(l, 8, c, sizeof(c));
    gpsHdop = atof(c);
    gpsCampo(l, 9, c, sizeof(c));
    gpsAltitud = atof(c);
    gpsUltimoFijoMs = millis();
    if (!gpsHayPosicion || estadoGps != GPS_CON_FIJO) {
      Serial.printf("[GPS] FIJO: %u SAT, HDOP %.1f, %.5f %.5f, %.0f M (%lu S)\n", gpsSatelites, gpsHdop, gpsLat,
                    gpsLon, gpsAltitud, (millis() - gpsDespiertoMs) / 1000);
    }
    gpsHayPosicion = true;
    estadoGps = GPS_CON_FIJO;
    // Para la media de la altitud, solo fijos 3D buenos
    if (!gpsVentanaHecha && gpsNAlt < GPS_MUESTRAS_ALT && gpsSatelites >= 5 && gpsHdop < 3.0f) {
      gpsSumaAlt += gpsAltitud;
      gpsNAlt++;
    }
  } else if (strncmp(l + 3, "RMC", 3) == 0) {
    char estado[2], hora[12], fecha[8];
    gpsCampo(l, 2, estado, sizeof(estado));
    if (estado[0] != 'A' || gpsHoraVentana) return;  // Sin fijo, la hora del GPS puede no ser buena
    gpsCampo(l, 1, hora, sizeof(hora));
    gpsCampo(l, 9, fecha, sizeof(fecha));
    gpsPonerHora(hora, fecha);
  }
}

void leerGps() {
  static char linea[100];
  static uint8_t largo = 0;
  while (Serial1.available()) {
    char b = Serial1.read();
    if (b == '$') largo = 0;  // Tambien descarta lo que no sea NMEA (respuestas UBX)
    if (b == '\r') continue;
    if (b == '\n') {
      linea[largo] = '\0';
      if (largo > 6) gpsProcesarLinea(linea);
      largo = 0;
    } else if (largo < sizeof(linea) - 1) {
      linea[largo++] = b;
    }
  }
}

// Fin de la ventana: locator (en AUTO) y, con bastantes muestras, altitud de referencia y QNH del BME280
void gpsCerrarVentana() {
  gpsVentanaHecha = true;
  gpsFinVentanaMs = millis();
  if (!gpsHayPosicion || millis() - gpsUltimoFijoMs > GPS_TOPE_PERIODICO_MS) {
    Serial.println(F("[GPS] SIN FIJO EN ESTA VENTANA"));
    return;
  }
  if (locatorAuto) {
    char loc[7];
    latLonALocator(gpsLat, gpsLon, loc);
    if (strcmp(loc, locatorEstacion) != 0) {  // En la NVS solo si cambia la cuadricula
      strlcpy(locatorEstacion, loc, sizeof(locatorEstacion));
      guardarEstacionNVS();
      pantallaPendiente = true;
      Serial.printf("[GPS] LOCATOR %s\n", locatorEstacion);
    }
  }
  if (gpsNAlt < GPS_MUESTRAS_ALT_MIN) return;
  float media = gpsSumaAlt / gpsNAlt;
  bool cambia = isnan(gpsAltRef) || fabsf(media - gpsAltRef) > GPS_ALT_CAMBIO_M;
  if (cambia) {
    gpsAltRef = media;
    Serial.printf("[GPS] ALTITUD DE REFERENCIA %.0f M (MEDIA DE %u)\n", gpsAltRef, gpsNAlt);
  }
  if (bmeDireccion != 0 && !isnan(bmePresionHpa)) {
    qnhHpa = qnhParaAltitud(gpsAltRef);
    qnhCalibrado = true;
    if (cambia) guardarQnhNVS();  // Con la misma altitud, el QNH nuevo solo en RAM (se recalibra en cada ventana)
  }
}

bool gpsVistoEnStatus() {
  return pantallaActual == PANTALLA_CONFIG && subConf == CONF_STATUS && paginaStatus == PAGINA_STATUS_GPS;
}

void procesarGps() {
  if (estadoGps == GPS_AUSENTE) return;
  leerGps();
  unsigned long ahora = millis();
  switch (estadoGps) {
    case GPS_DETECTANDO:
      if (ahora - gpsDespiertoMs >= GPS_DETECCION_MS) {
        estadoGps = GPS_AUSENTE;
        Serial1.end();
        pantallaPendiente = true;
        Serial.println(F("[GPS] NO DETECTADO (TX DEL GPS A GPIO47, RX A GPIO39)"));
      } else if (ahora - gpsUltimaRafagaMs >= GPS_SIN_DATOS_MS) {
        gpsRafagaDespertar();
      }
      break;

    case GPS_DORMIDO:
      // Si sigue hablando, no ha recibido bien la orden de dormir: se repite
      if (gpsReintentosDormir < GPS_REINTENTOS_DORMIR && (long)(gpsUltimoNmeaMs - gpsDormidoMs) > 1500) {
        gpsReintentosDormir++;
        gpsPedirDormir();
        gpsDormidoMs = ahora;
        Serial.printf("[GPS] SIGUE DESPIERTO: PMREQ OTRA VEZ (%u)\n", gpsReintentosDormir);
      } else if (gpsVistoEnStatus()) {
        despertarGps(GPS_TOPE_PERIODICO_MS, "STATUS");
      } else if (ahora - gpsFinVentanaMs >= GPS_INTERVALO_MS) {
        despertarGps(GPS_TOPE_PERIODICO_MS, "CADA 10 MIN");
      }
      break;

    default:  // Despierto
      if (ahora - gpsUltimoNmeaMs >= GPS_SIN_DATOS_MS && ahora - gpsUltimaRafagaMs >= GPS_SIN_DATOS_MS) {
        gpsRafagaDespertar();
      }
      if (!gpsVentanaHecha && (gpsNAlt >= GPS_MUESTRAS_ALT || ahora - gpsDespiertoMs >= gpsTopeMs)) {
        gpsCerrarVentana();
      }
      if (gpsVentanaHecha && !gpsVistoEnStatus()) dormirGps();
      break;
  }
}

// Antes del deep sleep: PMREQ (comprobando que deja de mandar NMEA) y su RX retenido en ALTO, en reposo
void dormirGpsAntesDeSueno() {
  if (estadoGps == GPS_AUSENTE) return;
  if (estadoGps != GPS_DORMIDO) {
    for (uint8_t i = 0; i < GPS_REINTENTOS_DORMIR; i++) {
      gpsPedirDormir();
      Serial1.flush();
      delay(300);
      while (Serial1.available()) Serial1.read();
      unsigned long t = millis();
      bool habla = false;
      while (!habla && millis() - t < 1200) {
        habla = Serial1.available() > 0;
        delay(10);
      }
      if (!habla) break;
      Serial.println(F("[GPS] SIGUE DESPIERTO: PMREQ OTRA VEZ"));
    }
    estadoGps = GPS_DORMIDO;
  }
  // Sin Serial1.end(): el pin pasa de la UART a GPIO en ALTO sin bajar (un flanco despertaria al GPS)
  digitalWrite(GPS_TX_PIN, HIGH);
  pinMode(GPS_TX_PIN, OUTPUT);
  gpio_hold_en((gpio_num_t)GPS_TX_PIN);
}

// "AHORA", "40 S", "12 MIN" o "3 H" desde el ultimo fijo
String textoAntiguedadGps() {
  unsigned long s = (millis() - gpsUltimoFijoMs) / 1000;
  if (s < 5) return String("AHORA");
  if (s < 60) return "HACE " + String(s) + " S";
  if (s < 3600) return "HACE " + String(s / 60) + " MIN";
  return "HACE " + String(s / 3600) + " H";
}
