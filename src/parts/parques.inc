// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: parques POTA cercanos (lista guardada en la flash y busqueda sin conexion).
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// PARQUES POTA CERCANOS
// ============================================================================
// La lista de parques de Espana (unos 2500) se descarga de pota.app con el WiFi de la pantalla POTA si no la hay o
// tiene mas de PARQUES_REFRESCO_S, y se guarda en LittleFS (particion "spiffs" de 1,5 MB) como registros fijos de
// 32 bytes (unos 80 KB). La API es publica, sin clave y sin condiciones publicadas: se consulta lo menos posible.
// VER PARQUES busca sin conexion los PARQUES_CERCANOS mas cercanos al ultimo fijo del GPS o, sin el, al centro
// del locator.
#define URL_PARQUES          "https://api.pota.app/program/parks/ES"
#define FICHERO_PARQUES      "/parques.bin"
#define FICHERO_PARQUES_TMP  "/parques.tmp"
#define PARQUES_MAGICO       0x50415232UL  // "PAR2": cambia si cambia el formato del registro
#define PARQUES_REFRESCO_S   (30UL * 24 * 3600)
#define PARQUES_REINTENTO_MS (60UL * 60 * 1000)  // Tras una descarga fallida
#define PARQUES_MINIMO       1000              // Menos parques: la descarga se da por mala
#define PARQUES_CERCANOS     20
#define PARQUES_POR_PAGINA   2

struct CabeceraParques {
  uint32_t magico;
  uint32_t fecha;     // Epoch de la descarga; 0 si no habia hora
  uint32_t cantidad;
};

struct __attribute__((packed)) RegistroParque {
  uint16_t numero;    // ES-0072 -> 72
  int32_t lat, lon;   // Grados x 1e5
  char nombre[22];    // Ya abreviado y en ASCII: los 21 caracteres de la linea
};
static_assert(sizeof(RegistroParque) == 32, "registro de parque de 32 bytes");

struct ParqueCercano {
  uint16_t numero;
  float km;
  uint16_t rumbo;     // Grados desde el norte
  char nombre[22];
};
ParqueCercano parquesCercanos[PARQUES_CERCANOS];
uint8_t numParquesCercanos = 0;
uint8_t parqueSel = 0;                   // Parque marcado en VER PARQUES
bool menuParques = false;                // Pulsar sobre un parque: el pie pasa a spot / volver
uint8_t focoMenuParques = 0;             // 0 spot, 1 volver
bool parquesFsMontado = false;
bool parquesDesdeGps = false;            // Origen de la ultima busqueda (si no, el locator)
uint32_t parquesEnLista = 0, parquesFecha = 0;  // De la cabecera; 0 = no hay lista
unsigned long parquesIntentoMs = 0;      // Ultima descarga fallida; 0 = ninguna
uint16_t parquesDescargados = 0;         // Progreso, para la pantalla
bool descargandoParques = false;

void dibujarSinDatos(const String& aviso, bool sinDatos = false);  // Definida con PROP y POTA
bool buscarParquesCercanos();               // Definida mas abajo

// Monta LittleFS la primera vez que hace falta (la primera de todas formatea la particion, 1-2 s)
bool montarParques() {
  if (!parquesFsMontado) {
    parquesFsMontado = LittleFS.begin(true, "/littlefs", 4, "spiffs");
    if (!parquesFsMontado) Serial.println(F("[PARQUES] NO SE PUEDE MONTAR LITTLEFS"));
  }
  return parquesFsMontado;
}

// Lee la cabecera de la lista guardada (parquesEnLista = 0 si no hay o no vale)
void leerCabeceraParques() {
  parquesEnLista = 0;
  parquesFecha = 0;
  if (!montarParques()) return;
  File f = LittleFS.open(FICHERO_PARQUES, "r");
  if (!f) return;
  CabeceraParques cab;
  if (f.read((uint8_t*)&cab, sizeof(cab)) == sizeof(cab) && cab.magico == PARQUES_MAGICO &&
      f.size() == sizeof(cab) + cab.cantidad * sizeof(RegistroParque)) {
    parquesEnLista = cab.cantidad;
    parquesFecha = cab.fecha;
  }
  f.close();
}

// Hay que descargar si no hay lista o, con hora, si es vieja o no se sabe de cuando es. Tras un fallo, se espera
bool tocaDescargarParques() {
  static bool cabeceraLeida = false;
  if (!cabeceraLeida) { leerCabeceraParques(); cabeceraLeida = true; }
  if (parquesIntentoMs != 0 && millis() - parquesIntentoMs < PARQUES_REINTENTO_MS) return false;
  if (parquesEnLista == 0) return true;
  struct tm t;
  if (!horaLocalValida(t)) return false;
  time_t ahora = time(nullptr);
  return parquesFecha == 0 || (uint32_t)ahora - parquesFecha > PARQUES_REFRESCO_S;
}

// UTF-8 -> ASCII para la fuente 5x7: vocales con tilde o dieresis sin ella, n por la enie, c por la c cedilla;
// el resto de caracteres no ASCII se quitan
void nombreParqueAscii(const char* origen, char* destino, size_t tam) {
  // Segundo byte tras 0xC3 (U+00C0..U+00FF)
  static const char MAPA_C3[] = "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYPs" "aaaaaaaceeeeiiii" "dnooooo/ouuuuypy";
  size_t n = 0;
  for (const uint8_t* p = (const uint8_t*)origen; *p && n + 1 < tam; p++) {
    if (*p < 0x80) destino[n++] = *p;
    else if (*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) destino[n++] = MAPA_C3[*++p - 0x80];
    else if ((*p & 0xC0) == 0xC0) { while ((p[1] & 0xC0) == 0x80) p++; }  // Otro caracter: se salta entero
  }
  destino[n] = '\0';
}

// Abrevia los finales mas comunes de los nombres de POTA ("Natura 2000" en mas de la mitad)
void abreviarNombreParque(char* nombre) {
  static const char* const LARGOS[] = { " National Recreation Trail", " National Historic Trail",
    " National Historic Site", " World Heritage Site", " Natura 2000", " Wetland Reserve", " Nature Park",
    " Nature Reserve", " National Park", " Biosphere Reserve", " Natural Monument", " State Trail",
    " Regional Park", " Natural Park", " Protected Landscape" };
  static const char* const CORTOS[] = { " NRT", " NHT", " NHS", " WHS", " N2000", " WR", " NatP", " NatR", " NP",
    " BR", " NM", " ST", " RP", " NatP", " PL" };
  for (size_t i = 0; i < sizeof(LARGOS) / sizeof(LARGOS[0]); i++) {
    char* p = strstr(nombre, LARGOS[i]);
    if (p) {
      size_t largo = strlen(LARGOS[i]);
      memmove(p + strlen(CORTOS[i]), p + largo, strlen(p + largo) + 1);
      memcpy(p, CORTOS[i], strlen(CORTOS[i]));
    }
  }
}

/**
 * Descarga la lista de parques y la guarda. El JSON (unos 800 KB) no cabe en RAM: se lee parque a parque del
 * flujo HTTP. Se escribe en un fichero temporal que solo sustituye a la lista si la descarga sale bien.
 * Bloquea unos segundos (en POTA no suena CW ni hay LoRa); la pantalla muestra el progreso
 */
void descargarParques() {
  if (!montarParques()) { parquesIntentoMs = millis(); return; }
  Serial.println(F("[PARQUES] DESCARGANDO LISTA"));
  unsigned long inicio = millis();
  WiFiClientSecure cliente;
  cliente.setInsecure();  // Datos publicos de solo lectura, como los spots
  HTTPClient http;
  http.setTimeout(15000);
  http.useHTTP10(true);   // Sin "chunked": el flujo es el JSON tal cual
  bool ok = false;
  uint32_t n = 0;
  if (http.begin(cliente, URL_PARQUES)) {
    http.setUserAgent("Chispas-EA4ILN/" FW_VERSION);
    int codigo = http.GET();
    if (codigo == HTTP_CODE_OK) {
      File f = LittleFS.open(FICHERO_PARQUES_TMP, "w");
      if (f) {
        CabeceraParques cab = { 0, 0, 0 };  // Se escribe de verdad al terminar
        f.write((const uint8_t*)&cab, sizeof(cab));
        JsonDocument filtro;
        filtro["reference"] = true;
        filtro["name"] = true;
        filtro["latitude"] = true;
        filtro["longitude"] = true;
        WiFiClient& flujo = http.getStream();
        descargandoParques = true;
        parquesDescargados = 0;
        actualizarPantalla();
        if (flujo.find("[")) {
          do {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, flujo, DeserializationOption::Filter(filtro));
            if (err) { Serial.printf("[PARQUES] JSON NO VALIDO (%s) EN EL %u\n", err.c_str(), n); break; }
            const char* ref = doc["reference"] | "";
            if (strncmp(ref, "ES-", 3) != 0 || doc["latitude"].isNull() || doc["longitude"].isNull()) continue;
            RegistroParque r = {};
            r.numero = atoi(ref + 3);
            r.lat = lroundf(doc["latitude"].as<float>() * 1e5f);
            r.lon = lroundf(doc["longitude"].as<float>() * 1e5f);
            char nombre[112];
            nombreParqueAscii(doc["name"] | "", nombre, sizeof(nombre));
            abreviarNombreParque(nombre);
            strlcpy(r.nombre, nombre, sizeof(r.nombre));
            if (f.write((const uint8_t*)&r, sizeof(r)) != sizeof(r)) { Serial.println(F("[PARQUES] FLASH LLENA")); break; }
            n++;
            if (n % 250 == 0) { parquesDescargados = n; actualizarPantalla(); }
          } while (flujo.findUntil(",", "]"));
        }
        descargandoParques = false;
        ok = n >= PARQUES_MINIMO;
        if (ok) {
          time_t ahora = time(nullptr);
          struct tm t;
          cab = { PARQUES_MAGICO, horaLocalValida(t) ? (uint32_t)ahora : 0, n };
          f.seek(0);
          f.write((const uint8_t*)&cab, sizeof(cab));
        }
        f.close();
      }
    } else {
      Serial.printf("[PARQUES] %s -> HTTP %d\n", URL_PARQUES, codigo);
    }
    http.end();
  }
  if (ok) {
    LittleFS.remove(FICHERO_PARQUES);
    ok = LittleFS.rename(FICHERO_PARQUES_TMP, FICHERO_PARQUES);
  } else {
    LittleFS.remove(FICHERO_PARQUES_TMP);
  }
  Serial.printf("[PARQUES] %s: %u PARQUES EN %lu S\n", ok ? "LISTA GUARDADA" : "DESCARGA FALLIDA", n,
                (millis() - inicio) / 1000);
  parquesIntentoMs = ok ? 0 : millis();
  leerCabeceraParques();
  // Si se estan viendo los parques (p. ej. "SIN LISTA"), se buscan ya en la lista nueva
  if (pantallaActual == PANTALLA_SPOTS && subDatos == DATOS_PARQUES) buscarParquesCercanos();
}

/**
 * Busca los PARQUES_CERCANOS mas cercanos al ultimo fijo del GPS o, sin el, al centro del locator. Recorre la
 * lista de la flash (unas decimas de segundo). Devuelve false si no hay lista o posicion
 */
bool buscarParquesCercanos() {
  numParquesCercanos = 0;
  parqueSel = 0;
  menuParques = false;
  float lat, lon;
  parquesDesdeGps = gpsHayPosicion;
  if (gpsHayPosicion) { lat = gpsLat; lon = gpsLon; }
  else if (!locatorALatLon(locatorEstacion, lat, lon)) return false;
  if (parquesEnLista == 0) leerCabeceraParques();
  if (parquesEnLista == 0) return false;
  File f = LittleFS.open(FICHERO_PARQUES, "r");
  if (!f) return false;
  f.seek(sizeof(CabeceraParques));
  const float RAD = PI / 180.0f;
  float cosLat = cosf(lat * RAD);
  RegistroParque r;
  while (f.read((uint8_t*)&r, sizeof(r)) == sizeof(r)) {
    // Equirectangular: de sobra a estas distancias para ordenar; km y rumbo exactos solo para los elegidos
    float dLat = r.lat * 1e-5f - lat;
    float dLon = (r.lon * 1e-5f - lon) * cosLat;
    float km = sqrtf(dLat * dLat + dLon * dLon) * 111.195f;
    if (numParquesCercanos == PARQUES_CERCANOS && km >= parquesCercanos[PARQUES_CERCANOS - 1].km) continue;
    uint8_t i = numParquesCercanos < PARQUES_CERCANOS ? numParquesCercanos++ : PARQUES_CERCANOS - 1;
    while (i > 0 && parquesCercanos[i - 1].km > km) { parquesCercanos[i] = parquesCercanos[i - 1]; i--; }
    ParqueCercano& p = parquesCercanos[i];
    p.numero = r.numero;
    p.km = km;
    float rumbo = atan2f(dLon, dLat) / RAD;
    p.rumbo = (uint16_t)lroundf(rumbo < 0 ? rumbo + 360 : rumbo) % 360;
    strlcpy(p.nombre, r.nombre, sizeof(p.nombre));
  }
  f.close();
  Serial.printf("[PARQUES] %u CERCANOS DESDE %s (%.4f %.4f)\n", numParquesCercanos,
                parquesDesdeGps ? "GPS" : locatorEstacion, lat, lon);
  return numParquesCercanos > 0;
}

// Rumbo en 8 puntos, en castellano (O = oeste)
const char* rumboCardinal(uint16_t grados) {
  static const char* const PUNTOS[8] = { "N", "NE", "E", "SE", "S", "SO", "O", "NO" };
  return PUNTOS[((grados + 22) % 360) / 45];
}

/**
 * Paginas de VER PARQUES: dos parques por pagina, cada uno en dos lineas ("ES-0072    12km SO" y el nombre).
 * Girar mueve el parque marcado (su primera linea en video inverso); pulsar abre en el pie "spot" (AUTOSPOT con
 * ese parque) y "volver" (al menu).
 * Sin lista o sin posicion, el aviso
 */
void dibujarPaginasParques() {
  if (descargandoParques) {
    dibujarSinDatos("DESCARGANDO PARQUES");
    display.setCursor(0, 32);
    display.print(parquesDescargados);
    return;
  }
  if (numParquesCercanos == 0) {
    if (parquesEnLista == 0) {
      dibujarSinDatos(estadoWifi == WIFI_CONECTADO ? "SIN LISTA DE PARQUES" : "SIN LISTA: HACE FALTA");
      if (estadoWifi != WIFI_CONECTADO) { display.setCursor(0, 32); display.print(F("EL WIFI UNA VEZ")); }
    } else {
      dibujarSinDatos("SIN POSICION:");
      display.setCursor(0, 32);
      display.print(F("GPS O LOCATOR"));
    }
    dibujarPiePaginas(0, 1, "PARQUES");
    return;
  }
  display.setTextWrap(false);
  for (uint8_t i = 0; i < PARQUES_POR_PAGINA; i++) {
    uint8_t idx = (parqueSel / PARQUES_POR_PAGINA) * PARQUES_POR_PAGINA + i;
    if (idx >= numParquesCercanos) break;
    const ParqueCercano& p = parquesCercanos[idx];
    char dist[8];
    if (p.km < 10) snprintf(dist, sizeof(dist), "%.1fkm", p.km);
    else snprintf(dist, sizeof(dist), "%.0fkm", p.km);
    char linea[24];
    snprintf(linea, sizeof(linea), "ES-%04u %10s %-2s", p.numero, dist, rumboCardinal(p.rumbo));
    display.setCursor(0, 12 + i * 20);
    if (idx == parqueSel) display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.print(linea);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 22 + i * 20);
    display.print(p.nombre);
  }
  display.setTextWrap(true);
  // El pie dice de donde se ha medido (GPS o el locator); al pulsar, spot / volver
  if (menuParques) dibujarPieDos(focoMenuParques == 0, "spot", focoMenuParques == 1, "volver");
  else dibujarPieCentrado(String(parqueSel + 1) + "/" + String(numParquesCercanos) + " " +
                          (parquesDesdeGps ? String("GPS") : String(locatorEstacion)) + "  >menu");
}
