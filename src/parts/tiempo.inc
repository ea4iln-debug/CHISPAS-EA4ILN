// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: pantalla TIEMPO: tiempo actual y prevision (Open-Meteo) y ambiente (BME280).
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// PANTALLA TIEMPO: OPEN-METEO (SIN CLAVE) Y AMBIENTE
// ============================================================================
// Una peticion de unos 3 KB por la posicion del locator (con GPS, el suyo): tiempo actual, horas de hoy y de
// manana y dos dias. Las horas vienen en la hora local del sitio (timezone=auto). Open-Meteo: gratis para uso no
// comercial, sin clave, datos CC BY 4.0 (se cita en el menu). AEMET se descarto de momento: pide clave por
// usuario, va por municipio (no por coordenadas) y son cuatro peticiones de decenas de KB.
#define URL_TIEMPO "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f" \
  "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m,pressure_msl" \
  "&hourly=temperature_2m,precipitation_probability,weather_code" \
  "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,precipitation_sum," \
  "wind_speed_10m_max,wind_direction_10m_dominant&forecast_days=2&timezone=auto"
#define REFRESCO_TIEMPO_MS (30UL * 60 * 1000)  // En AUTO (el modelo cambia cada hora como mucho)
#define TIEMPO_N_HORAS     4                    // Prevision cada TIEMPO_PASO_HORAS hasta 12 h
#define TIEMPO_PASO_HORAS  3

struct HoraPrevision {
  char hora[3];     // "21" ("" = sin dato)
  int16_t codigo;   // WMO
  float temp;
  int16_t lluvia;   // Probabilidad en %, -1 sin dato
};

struct DiaPrevision {
  char fecha[6];    // "19/10"
  int16_t codigo;
  float tMax, tMin;
  int16_t lluvia;   // Probabilidad maxima en %, -1 sin dato
  float lluviaMm;
  float viento;     // Viento medio maximo (no la racha), km/h
  int16_t dirViento;
};

struct DatosTiempo {
  bool validos;
  unsigned long intentoMs;  // 0 = nunca
  unsigned long okMs;
  int16_t codigo;
  float temp, viento, presion;
  int16_t humedad, dirViento;
  HoraPrevision horas[TIEMPO_N_HORAS];
  DiaPrevision dias[2];     // Hoy y manana
};
DatosTiempo meteo = {};

// Codigos WMO del tiempo: texto largo (AHORA y MANANA, 21 caracteres como mucho) y corto (HORAS, 6)
struct TextoWmo { uint8_t codigo; const char* largo; const char* corto; };
const TextoWmo TEXTOS_WMO[] = {
  { 0, "DESPEJADO", "DESPEJ" },          { 1, "POCO NUBOSO", "POCNUB" },
  { 2, "NUBOSO", "NUBOSO" },             { 3, "CUBIERTO", "CUBIER" },
  { 45, "NIEBLA", "NIEBLA" },            { 48, "NIEBLA CON ESCARCHA", "NIEBLA" },
  { 51, "LLOVIZNA DEBIL", "LLOVIZ" },    { 53, "LLOVIZNA", "LLOVIZ" },
  { 55, "LLOVIZNA DENSA", "LLOVIZ" },    { 56, "LLOVIZNA HELADA", "HELADA" },
  { 57, "LLOVIZNA HELADA", "HELADA" },   { 61, "LLUVIA DEBIL", "LLUVIA" },
  { 63, "LLUVIA", "LLUVIA" },            { 65, "LLUVIA FUERTE", "LLUVIA" },
  { 66, "LLUVIA HELADA", "HELADA" },     { 67, "LLUVIA HELADA", "HELADA" },
  { 71, "NIEVE DEBIL", "NIEVE" },        { 73, "NIEVE", "NIEVE" },
  { 75, "NIEVE FUERTE", "NIEVE" },       { 77, "CINARRA", "NIEVE" },
  { 80, "CHUBASCOS DEBILES", "CHUBAS" }, { 81, "CHUBASCOS", "CHUBAS" },
  { 82, "CHUBASCOS FUERTES", "CHUBAS" }, { 85, "CHUBASCOS DE NIEVE", "NIEVE" },
  { 86, "CHUBASCOS DE NIEVE", "NIEVE" }, { 95, "TORMENTA", "TORMEN" },
  { 96, "TORMENTA CON GRANIZO", "GRANIZ" }, { 99, "TORMENTA CON GRANIZO", "GRANIZ" },
};

const char* textoWmo(int16_t codigo, bool largo) {
  for (const TextoWmo& t : TEXTOS_WMO) {
    if (t.codigo == codigo) return largo ? t.largo : t.corto;
  }
  return "?";
}

// Rumbo de donde viene el viento, en 8 puntos (O = oeste)
const char* rumboViento(int16_t grados) {
  static const char* const RUMBOS[8] = { "N", "NE", "E", "SE", "S", "SO", "O", "NO" };
  if (grados < 0) return "-";
  return RUMBOS[((grados + 22) / 45) % 8];
}

// Para procesarWifi: en AUTO, la primera vez y cada REFRESCO_TIEMPO_MS
bool tocaActualizarTiempo() {
  return wifiAuto && (meteo.intentoMs == 0 || millis() - meteo.intentoMs > REFRESCO_TIEMPO_MS);
}

void actualizarTiempo() {
  meteo.intentoMs = millis();
  float lat, lon;
  if (!locatorALatLon(locatorEstacion, lat, lon)) {
    Serial.println(F("[WIFI] TIEMPO: LOCATOR NO VALIDO"));
    return;
  }
  char url[440];
  snprintf(url, sizeof(url), URL_TIEMPO, lat, lon);
  String json;
  if (!descargarUrl(url, json)) return;

  JsonDocument filtro;
  filtro["current"] = true;
  filtro["hourly"] = true;
  filtro["daily"] = true;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, DeserializationOption::Filter(filtro));
  json = String();  // Libera el texto antes de copiar los datos
  if (err) {
    Serial.printf("[WIFI] TIEMPO: JSON NO VALIDO (%s)\n", err.c_str());
    return;
  }

  JsonObject c = doc["current"];
  const char* ahora = c["time"] | "";  // "2026-10-18T20:45", hora local del sitio
  if (strlen(ahora) < 16) {
    Serial.println(F("[WIFI] TIEMPO: SIN DATOS ACTUALES"));
    return;
  }
  meteo.codigo = c["weather_code"] | -1;
  meteo.temp = c["temperature_2m"] | NAN;
  meteo.humedad = c["relative_humidity_2m"] | -1;
  meteo.viento = c["wind_speed_10m"] | NAN;
  meteo.dirViento = c["wind_direction_10m"] | -1;
  meteo.presion = c["pressure_msl"] | NAN;

  // Horas: la de ahora ("2026-10-18T20") y luego cada TIEMPO_PASO_HORAS
  JsonObject h = doc["hourly"];
  JsonArray tiempos = h["time"];
  int i0 = -1;
  for (size_t i = 0; i < tiempos.size(); i++) {
    if (strncmp(tiempos[i] | "", ahora, 13) == 0) { i0 = i; break; }
  }
  for (uint8_t k = 0; k < TIEMPO_N_HORAS; k++) {
    HoraPrevision& hp = meteo.horas[k];
    int idx = i0 + TIEMPO_PASO_HORAS * (k + 1);
    const char* t = (i0 >= 0 && idx < (int)tiempos.size()) ? (tiempos[idx] | "") : "";
    if (strlen(t) < 16) { hp.hora[0] = '\0'; continue; }
    strlcpy(hp.hora, t + 11, sizeof(hp.hora));
    hp.codigo = h["weather_code"][idx] | -1;
    hp.temp = h["temperature_2m"][idx] | NAN;
    hp.lluvia = h["precipitation_probability"][idx] | -1;
  }

  JsonObject d = doc["daily"];
  for (uint8_t j = 0; j < 2; j++) {
    DiaPrevision& dp = meteo.dias[j];
    const char* f = d["time"][j] | "";  // "2026-10-19"
    if (strlen(f) == 10) snprintf(dp.fecha, sizeof(dp.fecha), "%.2s/%.2s", f + 8, f + 5);
    else dp.fecha[0] = '\0';
    dp.codigo = d["weather_code"][j] | -1;
    dp.tMax = d["temperature_2m_max"][j] | NAN;
    dp.tMin = d["temperature_2m_min"][j] | NAN;
    dp.lluvia = d["precipitation_probability_max"][j] | -1;
    dp.lluviaMm = d["precipitation_sum"][j] | NAN;
    dp.viento = d["wind_speed_10m_max"][j] | NAN;
    dp.dirViento = d["wind_direction_10m_dominant"][j] | -1;
  }
  meteo.validos = true;
  meteo.okMs = millis();
  Serial.printf("[WIFI] TIEMPO: %s %.1f C, %s (%.3f, %.3f)\n", ahora + 11, meteo.temp,
                textoWmo(meteo.codigo, true), lat, lon);
}

// Temperatura redondeada a grados (entero: -0.4 sale 0, no "-0")
int gradosEnteros(float t) {
  return (int)lroundf(t);
}

// AHORA: estado del cielo, temperatura y humedad, presion y viento, y el resumen de hoy
void dibujarTiempoAhora() {
  char linea[28];
  display.setCursor(0, 12);
  display.print(textoWmo(meteo.codigo, true));
  if (meteo.humedad >= 0) snprintf(linea, sizeof(linea), "T %.1fC  HR %d%%", meteo.temp, meteo.humedad);
  else snprintf(linea, sizeof(linea), "T %.1fC", meteo.temp);
  display.setCursor(0, 22);
  display.print(linea);
  snprintf(linea, sizeof(linea), "P %.0f HPA %s %.0fKM/H", meteo.presion, rumboViento(meteo.dirViento),
           meteo.viento);
  display.setCursor(0, 32);
  display.print(linea);
  const DiaPrevision& hoy = meteo.dias[0];
  snprintf(linea, sizeof(linea), "HOY %d/%dC LLUVIA %d%%", gradosEnteros(hoy.tMin), gradosEnteros(hoy.tMax),
           max((int)hoy.lluvia, 0));
  display.setCursor(0, 42);
  display.print(linea);
}

// HORAS: una fila por hora, "21H CHUBAS  12C  40%"
void dibujarTiempoHoras() {
  for (uint8_t k = 0; k < TIEMPO_N_HORAS; k++) {
    const HoraPrevision& hp = meteo.horas[k];
    if (hp.hora[0] == '\0') continue;
    char linea[28];
    if (hp.lluvia >= 0) {
      snprintf(linea, sizeof(linea), "%sH %-6s %3dC %3d%%", hp.hora, textoWmo(hp.codigo, false),
               gradosEnteros(hp.temp), hp.lluvia);
    } else {
      snprintf(linea, sizeof(linea), "%sH %-6s %3dC", hp.hora, textoWmo(hp.codigo, false), gradosEnteros(hp.temp));
    }
    display.setCursor(0, 12 + k * 10);
    display.print(linea);
  }
}

// MANANA: estado del cielo, fecha y temperaturas, lluvia y viento
void dibujarTiempoDia(const DiaPrevision& dp) {
  char linea[28];
  display.setCursor(0, 12);
  display.print(textoWmo(dp.codigo, true));
  snprintf(linea, sizeof(linea), "%s MAX %dC MIN %dC", dp.fecha, gradosEnteros(dp.tMax), gradosEnteros(dp.tMin));
  display.setCursor(0, 22);
  display.print(linea);
  if (dp.lluvia >= 0) snprintf(linea, sizeof(linea), "LLUVIA %d%% %.1fMM", dp.lluvia, dp.lluviaMm);
  else snprintf(linea, sizeof(linea), "LLUVIA %.1fMM", dp.lluviaMm);
  display.setCursor(0, 32);
  display.print(linea);
  snprintf(linea, sizeof(linea), "VIENTO %s %.0fKM/H", rumboViento(dp.dirViento), dp.viento);
  display.setCursor(0, 42);
  display.print(linea);
}

// ----------------------------------------------------------------------------
// AMBIENTE (BME280): ultima pagina de TIEMPO, solo con el sensor
// ----------------------------------------------------------------------------
const char* const NOMBRES_PAGINAS_TIEMPO[4] = { "AHORA", "HORAS", "MA\xA4" "ANA", "AMBIENTE" };  // 0xA4 = enye
#define PAGINA_TIEMPO_AMBIENTE 3  // Solo con el BME280

uint8_t paginasTiempo() {
  return bmeDireccion != 0 ? 4 : 3;
}

/**
 * Lee el BME280 cada minuto (cada 2 s con AMBIENTE a la vista), guarda una muestra de presion cada 10 min y
 * activa o quita el aviso de tormenta (bajada de BME_TORMENTA_HPA en 3 h)
 */
void procesarBME() {
  if (bmeDireccion == 0) return;
  bool aLaVista = pantallaActual == PANTALLA_TIEMPO && subDatos == DATOS_PAGINAS &&
                  paginaTiempo == PAGINA_TIEMPO_AMBIENTE;
  if (millis() - bmeUltimaLecturaMs < (aLaVista ? BME_LECTURA_VISTA_MS : BME_LECTURA_MS)) return;
  bmeUltimaLecturaMs = millis();
  bool ok = bmeMedir();
  if (aLaVista) pantallaPendiente = true;
  if (!ok || (bmeHistN > 0 && millis() - bmeUltimaMuestraMs < BME_HIST_INTERVALO_MS)) return;

  bmeUltimaMuestraMs = millis();
  if (bmeHistN == BME_HIST_N) {
    memmove(bmeHist, bmeHist + 1, (BME_HIST_N - 1) * sizeof(float));
    bmeHistN--;
  }
  // Con la altitud del GPS, la muestra va reducida a nivel del mar: subir a un monte no parece una bajada de
  // presion (3 hPa son unos 25 m). Al pasar de un tipo de muestra al otro, el historial empieza de nuevo
  bool nivelMar = !isnan(gpsAltRef);
  if (nivelMar != bmeHistNivelMar) {
    if (bmeHistN > 0) Serial.println(nivelMar ? F("[BME] HISTORIAL A NIVEL DEL MAR (ALTITUD DEL GPS): EMPIEZA DE NUEVO")
                                              : F("[BME] HISTORIAL SIN ALTITUD: EMPIEZA DE NUEVO"));
    bmeHistN = 0;
    bmeHistNivelMar = nivelMar;
  }
  bmeHist[bmeHistN++] = nivelMar ? qnhParaAltitud(gpsAltRef) : bmePresionHpa;
  float tend = bmeTendencia3h();
  bool aviso = !isnan(tend) && tend <= -BME_TORMENTA_HPA;
  if (aviso != avisoTormenta) {
    avisoTormenta = aviso;
    pantallaPendiente = true;
    Serial.printf(aviso ? "[BME] AVISO DE TORMENTA: %.1f HPA EN 3 H\n" : "[BME] FIN DEL AVISO (%.1f HPA EN 3 H)\n", tend);
  }
  Serial.printf("[BME] T %.1f C  HR %.0f %%  P %.2f HPA  ALT %.0f M  TEND %.1f HPA/3H (%u MUESTRAS)\n", bmeTempC,
                bmeConHumedad ? bmeHumedad : 0.0f, bmePresionHpa, altitudBarometrica(), isnan(tend) ? 0.0f : tend,
                bmeHistN);
}

// Pagina AMBIENTE: temperatura, humedad, presion, tendencia (aviso de tormenta) y altitud
void dibujarPaginaAmbiente() {
  if (isnan(bmePresionHpa)) {
    dibujarSinDatos("SIN LECTURA DEL SENSOR");
    return;
  }
  char linea[32];
  if (bmeConHumedad) snprintf(linea, sizeof(linea), "T %.1fC   HR %.0f%%", bmeTempC, bmeHumedad);
  else snprintf(linea, sizeof(linea), "T %.1fC   HR --", bmeTempC);
  display.setCursor(0, 12);
  display.print(linea);
  snprintf(linea, sizeof(linea), "P %.1f HPA", bmePresionHpa);
  display.setCursor(0, 22);
  display.print(linea);

  float tend = bmeTendencia3h();
  if (isnan(tend)) {
    uint16_t minutos = bmeHistN > 0 ? (bmeHistN - 1) * (BME_HIST_INTERVALO_MS / 60000UL) : 0;
    snprintf(linea, sizeof(linea), "3H: FALTAN %u MIN", BME_TEND_MIN_MIN - minutos);
  } else {
    const char* texto = avisoTormenta ? "TORMENTA?" :
                        tend <= -BME_TEND_CAMBIO_HPA ? "BAJANDO" : tend >= BME_TEND_CAMBIO_HPA ? "SUBIENDO" : "ESTABLE";
    snprintf(linea, sizeof(linea), "3H %+.1f HPA %s", tend, texto);
  }
  display.setCursor(0, 32);
  display.print(linea);

  // Sin calibrar, la altitud sale con la atmosfera estandar (se calibra en CONFIG > ALTITUD)
  if (qnhCalibrado) snprintf(linea, sizeof(linea), "ALT %.0fM  QNH %.1f", altitudBarometrica(), qnhHpa);
  else snprintf(linea, sizeof(linea), "ALT %.0fM (QNH STD)", altitudBarometrica());
  display.setCursor(0, 42);
  display.print(linea);
}

// ----------------------------------------------------------------------------
// PANTALLA
// ----------------------------------------------------------------------------
void renderizarPantallaTiempo() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  float lat, lon;
  bool locatorOk = locatorALatLon(locatorEstacion, lat, lon);
  // Linea de estado: el WiFi si se esta usando; si no, la fuente (Open-Meteo, CC BY 4.0) y la edad de los datos
  String estado = textoEstadoDatosWifi();
  if (estado.length() == 0) {
    if (!locatorOk) estado = "LOCATOR NO VALIDO";
    else if (!meteo.validos) estado = "SIN DATOS";
    else estado = "OPEN-METEO " + textoEdadDatos(meteo.okMs);
  }

  if (avisoWifiVisible()) {
    dibujarAvisoWifi(" TIEMPO ");
    return;
  }
  if (subDatos == DATOS_PORTAL) {
    dibujarVistaPortal(focoPortalDatos);
    return;
  }
  if (subDatos == DATOS_MENU) {
    dibujarMenuDatos(" TIEMPO ", opcionPantallaTiempo, "< VER DATOS >", "< ACTUALIZAR >", estado);
    return;
  }

  // Paginas (AMBIENTE no necesita conexion)
  if (paginaTiempo == PAGINA_TIEMPO_AMBIENTE) {
    dibujarPaginaAmbiente();
  } else if (!meteo.validos) {
    dibujarSinDatos(estado, locatorOk);
    if (!locatorOk) {
      display.setCursor(0, 32);
      display.print(F("CAMBIALO EN CONFIG"));
    }
  } else if (paginaTiempo == 0) {
    dibujarTiempoAhora();
  } else if (paginaTiempo == 1) {
    dibujarTiempoHoras();
  } else {
    dibujarTiempoDia(meteo.dias[1]);
  }
  dibujarPiePaginas(paginaTiempo, paginasTiempo(), NOMBRES_PAGINAS_TIEMPO[paginaTiempo]);
}
