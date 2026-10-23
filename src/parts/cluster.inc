// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: pantalla CLUSTER: spots por telnet (RBN, URE y otros) y QUIEN ME OYE (RBN).
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// PANTALLA CLUSTER (fase 1)
// ============================================================================
// Mientras se esta en CLUSTER (y hay indicativo propio), el WiFi va encendido y Chispas entra por telnet en el
// nodo elegido en AJUSTES > NODO con el indicativo de CONF: lineas de texto "DX de LLAMANTE: FREC INDICATIVO
// comentario HHMMZ", iguales en todos los nodos. Los filtros (MODO, BANDA) se aplican aqui y nunca en el
// servidor: los de DXSpider o CC Cluster se guardan en la cuenta del usuario y cambiarian lo que ve en su
// programa de log. QUIEN ME OYE usa siempre el RBN (si el nodo es otro, una segunda conexion): los receptores que
// han copiado el indicativo propio, con la distancia exacta desde el GPS o el locator al del receptor (lista de
// receptores con su locator, bajada de reversebeacon.net una vez por semana a LittleFS). Al salir de la pantalla
// se cierran las conexiones y procesarWifi() apaga el WiFi. Detalle y nodos medidos en NOTAS: "CLUSTER".
#define CLUSTER_MAX_SPOTS     30
#define CLUSTER_MAX_OYENTES   20
#define CLUSTER_LINEA_MAX     160
#define CLUSTER_SPOTS_PAGINA  4
#define CLUSTER_CADUCA_RBN_MS (10UL * 60 * 1000)  // El RBN repite el spot mientras la estacion sigue llamando CQ
#define CLUSTER_CADUCA_MS     (30UL * 60 * 1000)  // Spots de personas
#define OYENTE_CADUCA_MS      (30UL * 60 * 1000)
#define CLUSTER_LOGIN_MAX_MS  20000                // Sin entrar en este tiempo, se cierra y se reintenta
#define CLUSTER_REDIBUJO_MS   1000                 // El RBN manda unos 5 spots/s: como mucho un redibujo por segundo
#define CLUSTER_RX_HASH       8                    // Receptores distintos que se recuerdan por spot

#define URL_RECEPTORES          "https://www.reversebeacon.net/cont_includes/status.php?t=skt"
#define FICHERO_RECEPTORES      "/rbn.bin"
#define FICHERO_RECEPTORES_TMP  "/rbn.tmp"
#define RECEPTORES_MAGICO       0x52424E31UL  // "RBN1"
#define RECEPTORES_REFRESCO_S   (7UL * 24 * 3600)
#define RECEPTORES_REINTENTO_MS (60UL * 60 * 1000)
#define RECEPTORES_MINIMO       50
#define RECEPTORES_MAXIMO       1000

struct NodoCluster { const char* nombre; const char* host; uint16_t puerto; };
const NodoCluster NODOS_CLUSTER[] = {
  { "RBN", "telnet.reversebeacon.net", 7000 },  // CW y RTTY de los receptores automaticos (dB y WPM)
  { "URE", "ea4ure.com", 7300 },                 // EA4URE-2/3/5, Madrid (DXSpider)
  { "VE7CC", "dxc.ve7cc.net", 23 },              // CC Cluster
  { "WA9PIE", "dxc.wa9pie.net", 8000 },          // DXSpider
  { "PROPIO", nullptr, 0 },                      // clusterPropio ("host:puerto", desde WEBCONF)
};
#define N_NODOS_CLUSTER (sizeof(NODOS_CLUSTER) / sizeof(NODOS_CLUSTER[0]))
#define NODO_RBN    0
#define NODO_PROPIO 4

// Bandas de HF y 6 m, con el segmento de CW (para MODO CW cuando el spot no dice el modo)
struct BandaCluster { const char* nombre; uint16_t minKhz, maxKhz, cwMinKhz, cwMaxKhz; };
const BandaCluster BANDAS_CLUSTER[] = {
  { "160M", 1800, 2000, 1800, 1840 },    { "80M", 3500, 4000, 3500, 3570 },
  { "60M", 5250, 5450, 5351, 5354 },     { "40M", 7000, 7300, 7000, 7040 },
  { "30M", 10100, 10150, 10100, 10130 }, { "20M", 14000, 14350, 14000, 14070 },
  { "17M", 18068, 18168, 18068, 18095 }, { "15M", 21000, 21450, 21000, 21070 },
  { "12M", 24890, 24990, 24890, 24915 }, { "10M", 28000, 29700, 28000, 28070 },
  { "6M", 50000, 54000, 50000, 50100 },
};
#define N_BANDAS_CLUSTER (sizeof(BANDAS_CLUSTER) / sizeof(BANDAS_CLUSTER[0]))

struct SpotDx {
  char call[12];
  char spotter[12];       // El que mejor lo oye (RBN: mas dB)
  char coment[31];
  char hora[6];           // "1123Z" del ultimo spot
  char modo[6];           // RBN: "CW", "RTTY"...; "" si no lo dice
  float khz;
  int8_t db;              // -128 = sin dato
  uint8_t wpm;            // 0 = sin dato
  uint8_t receptores;     // Distintos que lo han spotteado
  bool rbn;
  unsigned long ultimoMs;
  uint16_t hashRx[CLUSTER_RX_HASH];
};

struct Oyente {
  char rx[12];
  char loc[7];            // "" = receptor sin locator conocido
  float km;               // -1 = no se sabe
  int8_t db;
  uint8_t wpm;
  float khz;
  char hora[6];
  unsigned long ms;
};

enum EstadoConexion : uint8_t { CX_CERRADA, CX_LOGIN, CX_DENTRO };
struct ConexionCluster {
  WiFiClient cliente;
  char host[41];
  uint16_t puerto;
  const char* nombre;
  bool rbn;
  bool usada;
  EstadoConexion estado;
  char linea[CLUSTER_LINEA_MAX];
  uint8_t largo;
  unsigned long intentoMs, loginDesdeMs;
  uint8_t fallos;
  bool loginEnviado, shdxEnviado;
};
ConexionCluster cxNodo = {}, cxRbn = {};

enum VistaCluster { VC_MENU, VC_SPOTS, VC_FICHA, VC_OYEN, VC_OYEN_FICHA, VC_AJUSTES, VC_PORTAL };
VistaCluster vistaCluster = VC_MENU;
#define CL_OPCION_AJUSTES 2
#define CL_OPCION_BARRA   3
uint8_t opcionPantallaCluster = CL_OPCION_BARRA;  // 0 ver spots, 1 quien me oye, 2 ajustes (pie), 3 barra
#define N_AJUSTES_CLUSTER 4
const char* const NOMBRES_AJUSTES_CLUSTER[N_AJUSTES_CLUSTER] = { "NODO", "MODO", "BANDA", "ESCUCHA EN TRX" };
uint8_t focoAjustesCluster = 0;
uint8_t focoFichaCluster = 0;     // Ficha de un spot: 0 "a SU", 1 volver
String avisoCluster = "";         // Sustituye al pie de la ficha hasta tocar algo

// Ajustes (NVS "cnodo", "cmodo", "cband" en el espacio del entrenador)
uint8_t clusterNodo = NODO_RBN;
uint8_t clusterModo = 0;          // 0 TODOS, 1 CW
uint8_t clusterBanda = 0;         // 0 TODAS, 1.. = BANDAS_CLUSTER[clusterBanda - 1]
bool clusterNvsLeido = false;
uint8_t nodoAplicado = 255, modoAplicado = 255, bandaAplicada = 255;

SpotDx spotsDx[CLUSTER_MAX_SPOTS];
uint8_t numSpotsDx = 0;
SpotDx vistaSpots[CLUSTER_MAX_SPOTS];  // Copia ordenada que se ve; se congela al bajar por la lista
uint8_t numVistaSpots = 0;
uint8_t selSpot = 0;
SpotDx fichaSpot;
Oyente oyentes[CLUSTER_MAX_OYENTES];
uint8_t numOyentes = 0;
uint8_t selOyente = 0;
Oyente fichaOyente;
bool clusterCambio = false;        // Llego algo nuevo: se redibuja (como mucho cada CLUSTER_REDIBUJO_MS)

// Lista de receptores del RBN (cabecera como la de los parques)
struct CabeceraReceptores { uint32_t magico, fecha, cantidad; };
struct RegistroReceptor { char call[13]; char loc[7]; };
static_assert(sizeof(RegistroReceptor) == 20, "registro de receptor de 20 bytes");
uint32_t receptoresEnLista = 0, receptoresFecha = 0;
unsigned long receptoresIntentoMs = 0;
bool receptoresLeidos = false;

bool exigirIndicativo();                                           // Definida con la pantalla CONF
void moverFocoAnillo(uint8_t& opcion, uint8_t barra, int8_t paso);  // Definidas con la entrada
void navegarDesdeBarra();
void entrarEnPantalla(uint8_t pantalla, bool haciaAtras);

// ----------------------------------------------------------------------------
// Ajustes
// ----------------------------------------------------------------------------
void leerClusterNVS() {
  if (clusterNvsLeido) return;
  clusterNvsLeido = true;
  prefs.begin(NVS_ENTRENADOR, true);
  clusterNodo = min(prefs.getUChar("cnodo", NODO_RBN), (uint8_t)(N_NODOS_CLUSTER - 1));
  clusterModo = min(prefs.getUChar("cmodo", 0), (uint8_t)1);
  clusterBanda = min(prefs.getUChar("cband", 0), (uint8_t)N_BANDAS_CLUSTER);
  clusterEscuchaTrx = prefs.getBool("ctrx", true);
  prefs.end();
}

void guardarClusterNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("cnodo", clusterNodo);
  prefs.putUChar("cmodo", clusterModo);
  prefs.putUChar("cband", clusterBanda);
  prefs.putBool("ctrx", clusterEscuchaTrx);
  prefs.end();
}

String valorAjusteCluster(uint8_t i) {
  switch (i) {
    case 0: return String(NODOS_CLUSTER[clusterNodo].nombre);
    case 1: return clusterModo == 0 ? String("TODOS") : String("CW");
    case 2: return clusterBanda == 0 ? String("TODAS") : String(BANDAS_CLUSTER[clusterBanda - 1].nombre);
    case 3: return clusterEscuchaTrx ? String("SI") : String("NO");
  }
  return String();
}

void ajustarOpcionCluster(int8_t giro) {
  if (focoAjustesCluster == 0) clusterNodo = (clusterNodo + N_NODOS_CLUSTER + giro) % N_NODOS_CLUSTER;
  else if (focoAjustesCluster == 1) clusterModo ^= 1;
  else if (focoAjustesCluster == 2) clusterBanda = (clusterBanda + N_BANDAS_CLUSTER + 1 + giro) % (N_BANDAS_CLUSTER + 1);
  else clusterEscuchaTrx = !clusterEscuchaTrx;
}

// Host y puerto de un nodo; PROPIO, de clusterPropio ("host:puerto"). false si no hay
bool datosNodo(uint8_t nodo, char* host, size_t tam, uint16_t& puerto) {
  if (nodo != NODO_PROPIO) {
    strlcpy(host, NODOS_CLUSTER[nodo].host, tam);
    puerto = NODOS_CLUSTER[nodo].puerto;
    return true;
  }
  const char* dos = strrchr(clusterPropio, ':');
  if (!dos || dos == clusterPropio) return false;
  int p = atoi(dos + 1);
  if (p <= 0 || p > 65535) return false;
  size_t n = min((size_t)(dos - clusterPropio), tam - 1);
  memcpy(host, clusterPropio, n);
  host[n] = '\0';
  puerto = p;
  return true;
}

// ----------------------------------------------------------------------------
// Utilidades
// ----------------------------------------------------------------------------
// "AA4PA-#" -> "AA4PA" (el RBN marca asi a sus receptores)
void quitarMarcaRbn(char* s) {
  size_t n = strlen(s);
  if (n >= 2 && s[n - 2] == '-' && s[n - 1] == '#') s[n - 2] = '\0';
}

void recortar(char* s) {
  size_t n = strlen(s);
  while (n > 0 && s[n - 1] == ' ') s[--n] = '\0';
  size_t i = 0;
  while (s[i] == ' ') i++;
  if (i > 0) memmove(s, s + i, strlen(s + i) + 1);
}

// Palabra entera, sin distinguir mayusculas ("cw" en "TNX CW 599")
bool contienePalabra(const char* texto, const char* palabra) {
  size_t n = strlen(palabra);
  for (const char* p = texto; *p; p++) {
    if ((p == texto || !isalnum((uint8_t)p[-1])) && strncasecmp(p, palabra, n) == 0 && !isalnum((uint8_t)p[n])) {
      return true;
    }
  }
  return false;
}

int8_t bandaDeFrecuencia(float khz) {
  for (uint8_t i = 0; i < N_BANDAS_CLUSTER; i++) {
    if (khz >= BANDAS_CLUSTER[i].minKhz && khz <= BANDAS_CLUSTER[i].maxKhz) return i;
  }
  return -1;
}

// CW si el spot lo dice (RBN o comentario); si dice otro modo, no; si no dice nada, por el segmento de la banda
bool spotEsCw(const SpotDx& s) {
  if (s.modo[0]) return strcmp(s.modo, "CW") == 0;
  if (contienePalabra(s.coment, "CW")) return true;
  static const char* const OTROS[] = { "FT8", "FT4", "SSB", "USB", "LSB", "RTTY", "PSK", "PSK31", "FM", "AM",
                                       "JT65", "JS8", "SSTV", "DIGI" };
  for (const char* o : OTROS) {
    if (contienePalabra(s.coment, o)) return false;
  }
  int8_t b = bandaDeFrecuencia(s.khz);
  return b >= 0 && s.khz >= BANDAS_CLUSTER[b].cwMinKhz && s.khz < BANDAS_CLUSTER[b].cwMaxKhz;
}

bool pasaFiltros(const SpotDx& s) {
  if (clusterBanda != 0 && bandaDeFrecuencia(s.khz) != clusterBanda - 1) return false;
  return clusterModo == 0 || spotEsCw(s);
}

uint16_t hashIndicativo(const char* s) {
  uint16_t h = 5381;
  while (*s) h = h * 33 + (uint8_t)*s++;
  return h ? h : 1;
}

// Es el indicativo propio, tambien con /P, EA8/... ("EA4ILN/P", "EA8/EA4ILN")
bool esMiIndicativo(const char* c) {
  size_t n = strlen(indicativoEstacion);
  if (n < 3) return false;
  const char* p = strstr(c, indicativoEstacion);
  if (!p) return false;
  bool inicioOk = (p == c || p[-1] == '/');
  bool finOk = (p[n] == '\0' || p[n] == '/');
  return inicioOk && finOk;
}

float distanciaKm(float lat1, float lon1, float lat2, float lon2) {
  const float RAD = PI / 180.0f;
  float dLat = (lat2 - lat1) * RAD, dLon = (lon2 - lon1) * RAD;
  float a = sinf(dLat / 2) * sinf(dLat / 2) + cosf(lat1 * RAD) * cosf(lat2 * RAD) * sinf(dLon / 2) * sinf(dLon / 2);
  return 6371.0f * 2 * atan2f(sqrtf(a), sqrtf(1 - a));
}

// Posicion propia: el ultimo fijo del GPS o, sin el, el centro del locator
bool posicionPropia(float& lat, float& lon) {
  if (gpsHayPosicion) { lat = gpsLat; lon = gpsLon; return true; }
  return locatorALatLon(locatorEstacion, lat, lon);
}

// Minutos desde "HHMMZ" (UTC); -1 si no se sabe la hora o el texto no vale
int minutosDesdeHoraZ(const char* hz) {
  struct tm t;
  if (strlen(hz) != 5 || hz[4] != 'Z' || !horaLocalValida(t)) return -1;
  int m = ((hz[0] - '0') * 10 + (hz[1] - '0')) * 60 + (hz[2] - '0') * 10 + (hz[3] - '0');
  time_t ahora = time(nullptr);
  struct tm utc;
  gmtime_r(&ahora, &utc);
  return (utc.tm_hour * 60 + utc.tm_min - m + 1440) % 1440;
}

// ----------------------------------------------------------------------------
// Lista de receptores del RBN (locators)
// ----------------------------------------------------------------------------
void leerCabeceraReceptores() {
  receptoresLeidos = true;
  receptoresEnLista = receptoresFecha = 0;
  if (!montarParques()) return;
  File f = LittleFS.open(FICHERO_RECEPTORES, "r");
  if (!f) return;
  CabeceraReceptores cab;
  if (f.read((uint8_t*)&cab, sizeof(cab)) == sizeof(cab) && cab.magico == RECEPTORES_MAGICO &&
      f.size() == sizeof(cab) + cab.cantidad * sizeof(RegistroReceptor)) {
    receptoresEnLista = cab.cantidad;
    receptoresFecha = cab.fecha;
  }
  f.close();
}

bool tocaBajarReceptores() {
  if (!receptoresLeidos) leerCabeceraReceptores();
  if (receptoresIntentoMs != 0 && millis() - receptoresIntentoMs < RECEPTORES_REINTENTO_MS) return false;
  if (receptoresEnLista == 0) return true;
  struct tm t;
  if (!horaLocalValida(t)) return false;
  return receptoresFecha == 0 || (uint32_t)time(nullptr) - receptoresFecha > RECEPTORES_REFRESCO_S;
}

/**
 * Baja la pagina de estado de los receptores (HTML de unos 300 KB) y guarda indicativo y locator de cada uno.
 * Se lee del flujo sin guardar la pagina: en cada fila, el indicativo va en el enlace "...&c=K7CO&t=de", luego
 * la celda de las bandas y despues "<td>DN40BN</td>". Bloquea unos segundos (aviso en pantalla)
 */
void bajarReceptores() {
  if (!montarParques()) { receptoresIntentoMs = millis(); return; }
  Serial.println(F("[DXC] BAJANDO LA LISTA DE RECEPTORES DEL RBN"));
  unsigned long inicio = millis();
  WiFiClientSecure cliente;
  cliente.setInsecure();  // Datos publicos de solo lectura
  HTTPClient http;
  http.setTimeout(15000);
  http.useHTTP10(true);   // Sin "chunked"
  uint32_t n = 0;
  bool ok = false;
  if (http.begin(cliente, URL_RECEPTORES)) {
    http.setUserAgent("Chispas-EA4ILN/" FW_VERSION);
    int codigo = http.GET();
    if (codigo == HTTP_CODE_OK) {
      File f = LittleFS.open(FICHERO_RECEPTORES_TMP, "w");
      if (f) {
        CabeceraReceptores cab = { 0, 0, 0 };
        f.write((const uint8_t*)&cab, sizeof(cab));
        WiFiClient& flujo = http.getStream();
        flujo.setTimeout(8);  // Segundos
        while (n < RECEPTORES_MAXIMO && flujo.find("&c=")) {
          String call = flujo.readStringUntil('&');
          if (!flujo.find("</td>") || !flujo.find("</td>") || !flujo.find("<td>")) break;
          String loc = flujo.readStringUntil('<');
          call.trim();
          loc.trim();
          float la, lo;
          if (call.length() < 3 || call.length() > 12 || !locatorALatLon(loc.c_str(), la, lo)) continue;
          RegistroReceptor r = {};
          strlcpy(r.call, call.c_str(), sizeof(r.call));
          strlcpy(r.loc, loc.c_str(), sizeof(r.loc));
          if (f.write((const uint8_t*)&r, sizeof(r)) != sizeof(r)) break;
          n++;
        }
        ok = n >= RECEPTORES_MINIMO;
        if (ok) {
          struct tm t;
          cab = { RECEPTORES_MAGICO, horaLocalValida(t) ? (uint32_t)time(nullptr) : 0, n };
          f.seek(0);
          f.write((const uint8_t*)&cab, sizeof(cab));
        }
        f.close();
      }
    } else {
      Serial.printf("[DXC] %s -> HTTP %d\n", URL_RECEPTORES, codigo);
    }
    http.end();
  }
  if (ok) {
    LittleFS.remove(FICHERO_RECEPTORES);
    ok = LittleFS.rename(FICHERO_RECEPTORES_TMP, FICHERO_RECEPTORES);
  } else {
    LittleFS.remove(FICHERO_RECEPTORES_TMP);
  }
  Serial.printf("[DXC] RECEPTORES: %s, %u EN %lu S\n", ok ? "LISTA GUARDADA" : "DESCARGA FALLIDA", n,
                (millis() - inicio) / 1000);
  receptoresIntentoMs = ok ? 0 : millis();
  leerCabeceraReceptores();
}

// Locator de un receptor ("" si no esta en la lista)
void locatorReceptor(const char* rx, char* loc, size_t tam) {
  loc[0] = '\0';
  if (!receptoresLeidos) leerCabeceraReceptores();
  if (receptoresEnLista == 0) return;
  File f = LittleFS.open(FICHERO_RECEPTORES, "r");
  if (!f) return;
  f.seek(sizeof(CabeceraReceptores));
  RegistroReceptor r;
  while (f.read((uint8_t*)&r, sizeof(r)) == sizeof(r)) {
    if (strcasecmp(r.call, rx) == 0) { strlcpy(loc, r.loc, tam); break; }
  }
  f.close();
}

// ----------------------------------------------------------------------------
// Spots y oyentes
// ----------------------------------------------------------------------------
void anadirSpot(const SpotDx& s) {
  if (!pasaFiltros(s)) return;
  uint16_t h = hashIndicativo(s.spotter);
  for (uint8_t i = 0; i < numSpotsDx; i++) {
    SpotDx& e = spotsDx[i];
    if (strcmp(e.call, s.call) != 0 || fabsf(e.khz - s.khz) >= 1.0f) continue;
    // El mismo indicativo en la misma frecuencia: un solo spot, con el mejor receptor y cuantos lo oyen
    if (s.db > e.db || !e.rbn) {
      strlcpy(e.spotter, s.spotter, sizeof(e.spotter));
      e.db = s.db;
      e.wpm = s.wpm;
      e.khz = s.khz;
      strlcpy(e.coment, s.coment, sizeof(e.coment));
    }
    strlcpy(e.hora, s.hora, sizeof(e.hora));
    e.ultimoMs = max(e.ultimoMs, s.ultimoMs);
    bool visto = false;
    uint8_t libre = CLUSTER_RX_HASH;
    for (uint8_t k = 0; k < CLUSTER_RX_HASH; k++) {
      if (e.hashRx[k] == h) visto = true;
      else if (e.hashRx[k] == 0 && libre == CLUSTER_RX_HASH) libre = k;
    }
    if (!visto) {
      if (libre < CLUSTER_RX_HASH) e.hashRx[libre] = h;
      if (e.receptores < 255) e.receptores++;
    }
    clusterCambio = true;
    return;
  }
  uint8_t i = numSpotsDx;
  if (numSpotsDx < CLUSTER_MAX_SPOTS) {
    numSpotsDx++;
  } else {
    i = 0;  // Lleno: sustituye al mas viejo
    for (uint8_t k = 1; k < CLUSTER_MAX_SPOTS; k++) {
      if ((long)(spotsDx[k].ultimoMs - spotsDx[i].ultimoMs) < 0) i = k;
    }
  }
  spotsDx[i] = s;
  memset(spotsDx[i].hashRx, 0, sizeof(spotsDx[i].hashRx));
  spotsDx[i].hashRx[0] = h;
  spotsDx[i].receptores = 1;
  clusterCambio = true;
}

void anadirOyente(const SpotDx& s) {
  uint8_t i = 0;
  while (i < numOyentes && strcmp(oyentes[i].rx, s.spotter) != 0) i++;
  if (i == numOyentes) {
    if (numOyentes < CLUSTER_MAX_OYENTES) {
      numOyentes++;
    } else {
      i = 0;
      for (uint8_t k = 1; k < CLUSTER_MAX_OYENTES; k++) {
        if ((long)(oyentes[k].ms - oyentes[i].ms) < 0) i = k;
      }
    }
    strlcpy(oyentes[i].rx, s.spotter, sizeof(oyentes[i].rx));
    oyentes[i].loc[0] = '\0';
  }
  Oyente& o = oyentes[i];
  if (o.loc[0] == '\0') {
    // Sin locator (o aun sin la lista de receptores, que se baja en CLUSTER): se vuelve a buscar en cada spot
    locatorReceptor(o.rx, o.loc, sizeof(o.loc));
    float la1, lo1, la2, lo2;
    o.km = (o.loc[0] && posicionPropia(la1, lo1) && locatorALatLon(o.loc, la2, lo2)) ? distanciaKm(la1, lo1, la2, lo2)
                                                                                      : -1;
  }
  o.db = s.db;
  o.wpm = s.wpm;
  o.khz = s.khz;
  strlcpy(o.hora, s.hora, sizeof(o.hora));
  o.ms = millis();
  clusterCambio = true;
  Serial.printf("[DXC] TE OYE %s (%s, %.0f KM): %d DB, %u WPM, %.1f KHZ\n", o.rx, o.loc[0] ? o.loc : "?", o.km, o.db,
                o.wpm, o.khz);
}

void caducarCluster() {
  static unsigned long ultima = 0;
  if (millis() - ultima < 5000) return;
  ultima = millis();
  for (uint8_t i = 0; i < numSpotsDx;) {
    unsigned long caduca = spotsDx[i].rbn ? CLUSTER_CADUCA_RBN_MS : CLUSTER_CADUCA_MS;
    if (millis() - spotsDx[i].ultimoMs > caduca) { spotsDx[i] = spotsDx[--numSpotsDx]; clusterCambio = true; }
    else i++;
  }
  for (uint8_t i = 0; i < numOyentes;) {
    if (millis() - oyentes[i].ms > OYENTE_CADUCA_MS) { oyentes[i] = oyentes[--numOyentes]; clusterCambio = true; }
    else i++;
  }
}

// RBN: "CW    33 dB  22 WPM  CQ" -> modo, dB y WPM
void leerExtrasRbn(SpotDx& s) {
  char modo[6] = "";
  sscanf(s.coment, "%5s", modo);
  if (isalpha((uint8_t)modo[0])) strlcpy(s.modo, modo, sizeof(s.modo));
  const char* p = strstr(s.coment, " dB");
  if (p) {
    const char* q = p;
    while (q > s.coment && (isdigit((uint8_t)q[-1]) || q[-1] == '-')) q--;
    s.db = constrain(atoi(q), -99, 99);
  }
  p = strstr(s.coment, " WPM");
  if (p) {
    const char* q = p;
    while (q > s.coment && isdigit((uint8_t)q[-1])) q--;
    s.wpm = min(atoi(q), 99);
  }
}

// "DX de AA4PA-#:   7028.00  W8XW           CW    33 dB  22 WPM  CQ      1123Z"
bool leerLineaDx(const char* l, SpotDx& s, bool rbn) {
  memset(&s, 0, sizeof(s));
  s.db = -128;
  s.rbn = rbn;
  const char* p = l + 6;
  const char* dos = strchr(p, ':');
  if (!dos) return false;
  size_t n = min((size_t)(dos - p), sizeof(s.spotter) - 1);
  memcpy(s.spotter, p, n);
  recortar(s.spotter);
  quitarMarcaRbn(s.spotter);
  char* fin;
  s.khz = strtof(dos + 1, &fin);
  if (fin == dos + 1 || s.khz < 1000) return false;
  p = fin;
  while (*p == ' ') p++;
  n = 0;
  while (p[n] && p[n] != ' ') n++;
  if (n == 0) return false;
  memcpy(s.call, p, min(n, sizeof(s.call) - 1));
  p += n;
  char resto[CLUSTER_LINEA_MAX];
  strlcpy(resto, p, sizeof(resto));
  recortar(resto);
  // La hora es la ultima palabra, si tiene la forma "1123Z"
  char* ultima = strrchr(resto, ' ');
  char* hz = ultima ? ultima + 1 : resto;
  if (strlen(hz) == 5 && hz[4] == 'Z' && isdigit((uint8_t)hz[0]) && isdigit((uint8_t)hz[3])) {
    strlcpy(s.hora, hz, sizeof(s.hora));
    *hz = '\0';
    recortar(resto);
  }
  strlcpy(s.coment, resto, sizeof(s.coment));
  if (rbn) leerExtrasRbn(s);
  s.ultimoMs = millis();
  return true;
}

// sh/dx de DXSpider y CC Cluster: "  7034.0 EA3AVV/P    23-Oct-2026 1122Z SOTA  EA3/GI-063               <EA5K>"
bool leerLineaShDx(const char* l, SpotDx& s) {
  memset(&s, 0, sizeof(s));
  s.db = -128;
  char fecha[12], hz[6];
  if (sscanf(l, "%f %11s %11s %5s", &s.khz, s.call, fecha, hz) != 4) return false;
  if (s.khz < 1000 || strlen(hz) != 5 || hz[4] != 'Z' || strchr(fecha, '-') == nullptr) return false;
  strlcpy(s.hora, hz, sizeof(s.hora));
  const char* p = strstr(l, hz);
  if (!p) return false;
  p += 5;
  const char* menor = strrchr(p, '<');
  const char* mayor = menor ? strchr(menor, '>') : nullptr;
  size_t n = menor ? (size_t)(menor - p) : strlen(p);
  char resto[CLUSTER_LINEA_MAX];
  n = min(n, sizeof(resto) - 1);
  memcpy(resto, p, n);
  resto[n] = '\0';
  recortar(resto);
  strlcpy(s.coment, resto, sizeof(s.coment));
  if (menor && mayor && mayor > menor + 1) {
    n = min((size_t)(mayor - menor - 1), sizeof(s.spotter) - 1);
    memcpy(s.spotter, menor + 1, n);
  }
  int edad = minutosDesdeHoraZ(s.hora);
  if (edad < 0) edad = 0;
  if ((unsigned long)edad * 60000UL >= CLUSTER_CADUCA_MS) return false;
  s.ultimoMs = millis() - (unsigned long)edad * 60000UL;
  return true;
}

// ----------------------------------------------------------------------------
// Conexiones telnet
// ----------------------------------------------------------------------------
void cerrarConexion(ConexionCluster& cx) {
  if (cx.estado != CX_CERRADA) {
    cx.cliente.stop();
    Serial.printf("[DXC] %s: DESCONECTADO\n", cx.nombre);
  }
  cx.estado = CX_CERRADA;
  cx.largo = 0;
}

void cerrarCluster() {
  cerrarConexion(cxNodo);
  cerrarConexion(cxRbn);
  cxNodo.usada = cxRbn.usada = false;
  nodoAplicado = 255;
}

// Prepara las conexiones segun el nodo: la del nodo y, si no es el RBN, la del RBN para QUIEN ME OYE
void configurarConexiones() {
  cerrarConexion(cxNodo);
  cerrarConexion(cxRbn);
  cxNodo.usada = datosNodo(clusterNodo, cxNodo.host, sizeof(cxNodo.host), cxNodo.puerto);
  cxNodo.nombre = NODOS_CLUSTER[clusterNodo].nombre;
  cxNodo.rbn = (clusterNodo == NODO_RBN);
  cxNodo.fallos = 0;
  cxNodo.intentoMs = 0;
  cxRbn.usada = !cxNodo.rbn;
  datosNodo(NODO_RBN, cxRbn.host, sizeof(cxRbn.host), cxRbn.puerto);
  cxRbn.nombre = "RBN";
  cxRbn.rbn = true;
  cxRbn.fallos = 0;
  cxRbn.intentoMs = 0;
}

void conectar(ConexionCluster& cx) {
  static const uint16_t ESPERA_S[] = { 0, 5, 15, 30, 60 };
  unsigned long espera = ESPERA_S[min(cx.fallos, (uint8_t)4)] * 1000UL;
  if (cx.intentoMs != 0 && millis() - cx.intentoMs < espera) return;
  cx.intentoMs = millis();
  Serial.printf("[DXC] %s: CONECTANDO A %s:%u\n", cx.nombre, cx.host, cx.puerto);
  pantallaPendiente = true;
  // connect() bloquea el loop() hasta que contesta: en TRX, menos espera (el keyer y el motor CW van en sus tareas)
  if (!cx.cliente.connect(cx.host, cx.puerto, pantallaActual == PANTALLA_TRX ? 2000 : 4000)) {
    if (cx.fallos < 255) cx.fallos++;
    Serial.printf("[DXC] %s: NO CONECTA (%u)\n", cx.nombre, cx.fallos);
    return;
  }
  cx.cliente.setNoDelay(true);
  cx.estado = CX_LOGIN;
  cx.largo = 0;
  cx.loginEnviado = cx.shdxEnviado = false;
  cx.loginDesdeMs = millis();
}

// Ya dentro (prompt o primer spot): en los nodos que no son el RBN se piden los ultimos spots
void dentroDelNodo(ConexionCluster& cx) {
  if (cx.estado == CX_DENTRO) return;
  cx.estado = CX_DENTRO;
  cx.fallos = 0;
  Serial.printf("[DXC] %s: DENTRO COMO %s\n", cx.nombre, indicativoEstacion);
  if (!cx.rbn && &cx == &cxNodo && !cx.shdxEnviado) {
    cx.cliente.print("sh/dx 20\r\n");
    cx.shdxEnviado = true;
  }
  pantallaPendiente = true;
}

// Prompt sin salto de linea: el de entrada ("login:", "Please enter your call:") o el del nodo ("... >")
void comprobarPrompt(ConexionCluster& cx) {
  char baja[CLUSTER_LINEA_MAX];
  size_t n = cx.largo;
  for (size_t i = 0; i < n; i++) baja[i] = tolower((uint8_t)cx.linea[i]);
  baja[n] = '\0';
  while (n > 0 && baja[n - 1] == ' ') baja[--n] = '\0';
  if (!cx.loginEnviado && (strstr(baja, "login:") || strstr(baja, "call:") || strstr(baja, "callsign"))) {
    cx.cliente.print(indicativoEstacion);
    cx.cliente.print("\r\n");
    cx.loginEnviado = true;
    cx.largo = 0;
    Serial.printf("[DXC] %s: LOGIN ENVIADO\n", cx.nombre);
  } else if (cx.loginEnviado && n > 0 && baja[n - 1] == '>') {
    dentroDelNodo(cx);
    cx.largo = 0;
  }
}

void procesarLineaCluster(ConexionCluster& cx, const char* l) {
  SpotDx s;
  if (strncmp(l, "DX de ", 6) == 0) {
    if (!leerLineaDx(l, s, cx.rbn)) return;
    if (cx.estado != CX_DENTRO) dentroDelNodo(cx);
    if (&cx == &cxNodo && pantallaActual == PANTALLA_CLUSTER) anadirSpot(s);
    if (cx.rbn && esMiIndicativo(s.call)) anadirOyente(s);
  } else if (&cx == &cxNodo && !cx.rbn && isdigit((uint8_t)l[strspn(l, " ")])) {
    if (leerLineaShDx(l, s)) anadirSpot(s);
  } else if (cx.loginEnviado) {
    size_t n = strlen(l);
    while (n > 0 && l[n - 1] == ' ') n--;
    if (n > 0 && l[n - 1] == '>' && strstr(l, " de ")) dentroDelNodo(cx);
  }
}

void leerConexion(ConexionCluster& cx) {
  if (!cx.cliente.connected()) {
    Serial.printf("[DXC] %s: CONEXION CERRADA POR EL NODO\n", cx.nombre);
    cx.cliente.stop();
    cx.estado = CX_CERRADA;
    if (cx.fallos < 255) cx.fallos++;
    cx.intentoMs = millis();
    pantallaPendiente = true;
    return;
  }
  if (cx.estado == CX_LOGIN && millis() - cx.loginDesdeMs > CLUSTER_LOGIN_MAX_MS) {
    Serial.printf("[DXC] %s: NO SE PUDO ENTRAR\n", cx.nombre);
    cerrarConexion(cx);
    if (cx.fallos < 255) cx.fallos++;
    cx.intentoMs = millis();
    return;
  }
  uint8_t buf[256];
  int leidos = 0;
  for (uint8_t vuelta = 0; vuelta < 8 && cx.cliente.available() > 0; vuelta++) {
    int n = cx.cliente.read(buf, sizeof(buf));
    if (n <= 0) break;
    leidos += n;
    for (int i = 0; i < n; i++) {
      char c = (char)buf[i];
      if (c == '\n') {
        cx.linea[cx.largo] = '\0';
        procesarLineaCluster(cx, cx.linea);
        cx.largo = 0;
      } else if ((uint8_t)c >= 0x20 && cx.largo < CLUSTER_LINEA_MAX - 1) {
        cx.linea[cx.largo++] = c;
      }
    }
  }
  if (leidos > 0 && cx.largo > 0) comprobarPrompt(cx);
}

// Copia ordenada (lo ultimo arriba) de la lista para verla
int compararSpotsRecientes(const void* a, const void* b) {
  long d = (long)(((const SpotDx*)b)->ultimoMs - ((const SpotDx*)a)->ultimoMs);
  return d > 0 ? 1 : (d < 0 ? -1 : 0);
}

void refrescarVistaSpots() {
  memcpy(vistaSpots, spotsDx, numSpotsDx * sizeof(SpotDx));
  numVistaSpots = numSpotsDx;
  qsort(vistaSpots, numVistaSpots, sizeof(SpotDx), compararSpotsRecientes);
}

/**
 * Cada vuelta de loop(): conecta, lee y entra en los nodos mientras se esta en CLUSTER con el WiFi conectado e
 * indicativo propio; si no, cierra. Con la lista de receptores vieja o sin ella, la baja antes (bloquea, con aviso).
 * Con ESCUCHA EN TRX, en TRX sigue solo la conexion al RBN, para QUIEN ME OYE (los demas spots no se guardan)
 */
void procesarCluster() {
  caducarCluster();
  bool enCluster = pantallaActual == PANTALLA_CLUSTER;
  bool enTrx = pantallaActual == PANTALLA_TRX && clusterEscuchaTrx;
  bool activo = (enCluster || enTrx) && estadoWifi == WIFI_CONECTADO && indicativoConfigurado();
  if (!activo) {
    if (cxNodo.usada || cxRbn.usada) cerrarCluster();
    return;
  }
  if (!clusterNvsLeido) leerClusterNVS();
  if (enCluster && tocaBajarReceptores()) {
    empezarDescarga("BAJANDO RECEPTORES...");
    bajarReceptores();
    terminarDescarga();
    return;
  }
  // Ajustes cambiados (o primera vez): otro nodo, conexiones nuevas; otro filtro, la lista empieza de nuevo
  if (clusterNodo != nodoAplicado) {
    configurarConexiones();
    numSpotsDx = numVistaSpots = 0;
    selSpot = 0;
    nodoAplicado = clusterNodo;
    modoAplicado = clusterModo;
    bandaAplicada = clusterBanda;
  } else if (clusterModo != modoAplicado || clusterBanda != bandaAplicada) {
    numSpotsDx = numVistaSpots = 0;
    selSpot = 0;
    modoAplicado = clusterModo;
    bandaAplicada = clusterBanda;
    clusterCambio = true;
  }
  ConexionCluster* cxs[2] = { &cxNodo, &cxRbn };
  for (ConexionCluster* cx : cxs) {
    if (!cx->usada) continue;
    if (!enCluster && !cx->rbn) {  // En TRX, el nodo que no es el RBN se cierra; vuelve al entrar en CLUSTER
      cerrarConexion(*cx);
      continue;
    }
    if (cx->estado == CX_CERRADA) conectar(*cx);
    else leerConexion(*cx);
  }
  static unsigned long ultimoRedibujo = 0;
  if (enCluster && clusterCambio && millis() - ultimoRedibujo >= CLUSTER_REDIBUJO_MS) {
    clusterCambio = false;
    ultimoRedibujo = millis();
    if (vistaCluster == VC_SPOTS && selSpot == 0) refrescarVistaSpots();
    if (vistaCluster == VC_MENU || vistaCluster == VC_SPOTS || vistaCluster == VC_OYEN) pantallaPendiente = true;
  }
}

// WiFi sin conexion (desde quedarseSinConexion): vuelta al menu, con el foco en ABRIR WEBCONF
void clusterSinConexion() {
  vistaCluster = VC_MENU;
  opcionPantallaCluster = 0;
}

void entrarEnCluster() {
  leerClusterNVS();
  vistaCluster = VC_MENU;
  opcionPantallaCluster = CL_OPCION_BARRA;
  avisoCluster = "";
}

// ----------------------------------------------------------------------------
// Dibujo
// ----------------------------------------------------------------------------
String textoEstadoConexion(const ConexionCluster& cx) {
  if (!cx.usada) return clusterNodo == NODO_PROPIO ? String("SIN NODO PROPIO") : String("CONECTANDO...");
  String nombre = cx.nombre ? cx.nombre : "";
  switch (cx.estado) {
    case CX_CERRADA: return cx.fallos > 0 ? nombre + ": SIN RESPUESTA" : "CONECTANDO " + nombre;
    case CX_LOGIN:   return "ENTRANDO EN " + nombre;
    default:         return &cx == &cxNodo ? nombre + " " + String(numSpotsDx) + " SPOTS" : nombre + " DENTRO";
  }
}

String textoEstadoCluster() {
  if (!indicativoConfigurado()) return String("FALTA TU INDICATIVO");
  if (actualizandoWifi) return String("ACTUALIZANDO...");
  switch (estadoWifi) {
    case WIFI_APAGADO:      return String("WIFI APAGADO");
    case WIFI_CONECTANDO:   return String("CONECTANDO WIFI...");
    case WIFI_PORTAL:       return String("PORTAL ABIERTO");
    case WIFI_SIN_CONEXION: return String("SIN CONEXION WIFI");
    default: break;
  }
  if (nodoAplicado == 255) return String("CONECTANDO...");
  if (clusterNodo == NODO_PROPIO && !cxNodo.usada) return String("SIN NODO PROPIO");
  return textoEstadoConexion(cxNodo);
}

// La del RBN, para QUIEN ME OYE (con el RBN como nodo, la misma)
const ConexionCluster& conexionRbn() {
  return cxNodo.rbn ? cxNodo : cxRbn;
}

void dibujarMenuCluster() {
  dibujarTituloMenu(" CLUSTER ");
  bool sinConexion = (estadoWifi == WIFI_SIN_CONEXION);
  uint8_t op = opcionPantallaCluster;
  dibujarOpcionCentrada(op == 0, sinConexion ? "< ABRIR WEBCONF >" : "< VER SPOTS >", 25);
  dibujarOpcionCentrada(op == 1, sinConexion ? "< REINTENTAR >" : "< QUIEN ME OYE? >", 34);
  dibujarTituloCentrado(textoEstadoCluster(), 43);
  dibujarBarraAvanzarRetroceder(op == CL_OPCION_BARRA, op == CL_OPCION_AJUSTES ? ">ajustes" : " ajustes");
}

// "14025.3 W8XW     33dB": frecuencia, indicativo (8) y dB del mejor receptor (RBN) o minutos desde el spot
void dibujarPaginaSpots() {
  if (numVistaSpots == 0) {
    bool dentro = cxNodo.usada && cxNodo.estado == CX_DENTRO;
    dibujarSinDatos(dentro ? String("ESPERANDO SPOTS...") : textoEstadoCluster());
    if (dentro && (clusterModo != 0 || clusterBanda != 0)) {
      display.setCursor(0, 32);
      display.print("FILTRO: " + valorAjusteCluster(1) + " " + valorAjusteCluster(2));
    }
    dibujarPieCentrado(String(NODOS_CLUSTER[clusterNodo].nombre) + "  >volver");
    return;
  }
  if (selSpot >= numVistaSpots) selSpot = numVistaSpots - 1;
  display.setTextWrap(false);
  uint8_t primera = (selSpot / CLUSTER_SPOTS_PAGINA) * CLUSTER_SPOTS_PAGINA;
  for (uint8_t k = 0; k < CLUSTER_SPOTS_PAGINA && primera + k < numVistaSpots; k++) {
    const SpotDx& s = vistaSpots[primera + k];
    char dcha[8];
    if (s.db != -128) snprintf(dcha, sizeof(dcha), "%2ddB", s.db);
    else snprintf(dcha, sizeof(dcha), "%3lum", min((millis() - s.ultimoMs) / 60000UL, 999UL));
    char linea[28];
    snprintf(linea, sizeof(linea), s.khz < 100000 ? "%7.1f %-8.8s %4s" : "%7.0f %-8.8s %4s", s.khz, s.call, dcha);
    display.setCursor(0, 12 + k * 10);
    if (primera + k == selSpot) display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.print(linea);
    display.setTextColor(SSD1306_WHITE);
  }
  display.setTextWrap(true);
  // Lista congelada mientras se baja por ella; al volver arriba, vuelve a ir en vivo
  dibujarPieCentrado(String(selSpot + 1) + "/" + String(numVistaSpots) + " " + NODOS_CLUSTER[clusterNodo].nombre +
                     (selSpot > 0 ? " PAUSA" : ""));
}

void dibujarFichaDx() {
  const SpotDx& s = fichaSpot;
  char linea[40];
  display.setTextWrap(false);
  snprintf(linea, sizeof(linea), "%-11.11s%8.1f", s.call, s.khz);
  display.setCursor(0, 12);
  display.print(linea);
  display.setCursor(0, 22);
  if (s.rbn) {
    snprintf(linea, sizeof(linea), "%s %dDB %uWPM X%u", s.modo, s.db, s.wpm, s.receptores);
    display.print(linea);
  } else {
    display.print(String(s.coment).substring(0, 21));
  }
  snprintf(linea, sizeof(linea), "DE %s %s", s.spotter, s.hora);
  display.setCursor(0, 32);
  display.print(linea);
  display.setCursor(0, 42);
  if (s.rbn) {
    // Lo que va detras de los WPM ("CQ", "TEST"...)
    const char* p = strstr(s.coment, "WPM");
    if (p) {
      p += 3;
      while (*p == ' ') p++;
      display.print(p);
    }
  } else if (strlen(s.coment) > 21) {
    display.print(s.coment + 21);
  }
  display.setTextWrap(true);
  if (avisoCluster.length() > 0) dibujarPieCentrado(avisoCluster);
  else dibujarPieDos(focoFichaCluster == 0, "a SU", focoFichaCluster == 1, "volver");
}

// Indices de los oyentes, el mas lejano primero (sin distancia, al final)
uint8_t ordenOyentes(uint8_t* orden) {
  for (uint8_t i = 0; i < numOyentes; i++) orden[i] = i;
  for (uint8_t i = 1; i < numOyentes; i++) {
    uint8_t v = orden[i];
    uint8_t j = i;
    while (j > 0 && oyentes[orden[j - 1]].km < oyentes[v].km) { orden[j] = orden[j - 1]; j--; }
    orden[j] = v;
  }
  return numOyentes;
}

// "VE6WZ      7210  26dB": receptor, km y dB, el mas lejano arriba
void dibujarPaginaOyentes() {
  const ConexionCluster& cx = conexionRbn();
  if (numOyentes == 0) {
    if (cx.estado == CX_DENTRO) {
      dibujarSinDatos(String("NADIE TE HA OIDO AUN"));
      display.setCursor(0, 32);
      display.print(F("LANZA CQ EN CW"));
    } else {
      bool soloWifi = !indicativoConfigurado() || estadoWifi != WIFI_CONECTADO || nodoAplicado == 255;
      dibujarSinDatos(soloWifi ? textoEstadoCluster() : textoEstadoConexion(cx));
    }
    dibujarPieCentrado("TE OYEN  >volver");
    return;
  }
  uint8_t orden[CLUSTER_MAX_OYENTES];
  ordenOyentes(orden);
  if (selOyente >= numOyentes) selOyente = numOyentes - 1;
  uint8_t primera = (selOyente / CLUSTER_SPOTS_PAGINA) * CLUSTER_SPOTS_PAGINA;
  display.setTextWrap(false);
  for (uint8_t k = 0; k < CLUSTER_SPOTS_PAGINA && primera + k < numOyentes; k++) {
    const Oyente& o = oyentes[orden[primera + k]];
    char km[8];
    if (o.km >= 0) snprintf(km, sizeof(km), "%.0f", o.km);
    else strlcpy(km, "?", sizeof(km));
    char linea[28];
    snprintf(linea, sizeof(linea), "%-9.9s %5s %3ddB", o.rx, km, o.db);
    display.setCursor(0, 12 + k * 10);
    if (primera + k == selOyente) display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.print(linea);
    display.setTextColor(SSD1306_WHITE);
  }
  display.setTextWrap(true);
  dibujarPieCentrado(String(selOyente + 1) + "/" + String(numOyentes) + " TE OYEN KM");
}

void dibujarFichaOyente() {
  const Oyente& o = fichaOyente;
  char linea[32];
  snprintf(linea, sizeof(linea), "%s %s", o.rx, o.loc[0] ? o.loc : "(SIN LOCATOR)");
  display.setCursor(0, 12);
  display.print(linea);
  if (o.km >= 0) snprintf(linea, sizeof(linea), "%.0f KM  %d DB", o.km, o.db);
  else snprintf(linea, sizeof(linea), "? KM  %d DB", o.db);
  display.setCursor(0, 22);
  display.print(linea);
  snprintf(linea, sizeof(linea), "%u WPM  %.1f KHZ", o.wpm, o.khz);
  display.setCursor(0, 32);
  display.print(linea);
  snprintf(linea, sizeof(linea), "%s (HACE %lu MIN)", o.hora, (millis() - o.ms) / 60000UL);
  display.setCursor(0, 42);
  display.print(linea);
  dibujarPieCentrado("> volver");
}

void renderizarPantallaCluster() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  if (actualizandoWifi) {
    dibujarAvisoWifi(" CLUSTER ");
    return;
  }
  switch (vistaCluster) {
    case VC_MENU:       dibujarMenuCluster(); break;
    case VC_SPOTS:      dibujarPaginaSpots(); break;
    case VC_FICHA:      dibujarFichaDx(); break;
    case VC_OYEN:       dibujarPaginaOyentes(); break;
    case VC_OYEN_FICHA: dibujarFichaOyente(); break;
    case VC_PORTAL:     dibujarVistaPortal(focoPortalDatos); break;
    case VC_AJUSTES:
      dibujarListaOpciones(NOMBRES_AJUSTES_CLUSTER, N_AJUSTES_CLUSTER, focoAjustesCluster, valorAjusteCluster, 0xF);
      dibujarPieCentrado(focoAjustesCluster == N_AJUSTES_CLUSTER ? "> volver" : "  volver");
      break;
  }
}

// ----------------------------------------------------------------------------
// Eventos del encoder y de PRG
// ----------------------------------------------------------------------------
// "a SU": el indicativo del spot pasa a ser el del corresponsal ({SU}) y se va al menu TRX
void spotASu() {
  if (strlen(fichaSpot.call) > TRAMA_LONG_INDICATIVO) {
    avisoCluster = "NO CABE EN SU (MAX 8)";
    return;
  }
  strlcpy(trxSu, fichaSpot.call, sizeof(trxSu));
  guardarTrxNVS();
  Serial.printf("[DXC] SU: %s (%.1f KHZ)\n", trxSu, fichaSpot.khz);
  entrarEnPantalla(PANTALLA_TRX, false);
}

void eventoCluster(int8_t giro, bool pulsa, bool atras, bool ejeLarga) {
  switch (vistaCluster) {
    case VC_MENU:
      if (atras) entrarEnPantalla(PANTALLA_INICIO, false);
      else if (giro != 0) moverFocoAnillo(opcionPantallaCluster, CL_OPCION_BARRA, giro);
      else if (pulsa) {
        uint8_t op = opcionPantallaCluster;
        if (op == CL_OPCION_BARRA) {
          navegarDesdeBarra();
        } else if (op == CL_OPCION_AJUSTES) {
          vistaCluster = VC_AJUSTES;
          focoAjustesCluster = 0;
          editandoValor = false;
        } else if (estadoWifi == WIFI_SIN_CONEXION) {
          if (op == 0) {
            abrirPortalWifi();
            vistaCluster = VC_PORTAL;
            focoPortalDatos = 0;
          } else {
            encenderWifi();  // Reintentar
          }
        } else if (exigirIndicativo()) {
          if (op == 0) {
            vistaCluster = VC_SPOTS;
            selSpot = 0;
            refrescarVistaSpots();
          } else {
            vistaCluster = VC_OYEN;
            selOyente = 0;
          }
        }
      }
      break;

    case VC_SPOTS:
      // Girar marca otro spot (bajar congela la lista); pulsar abre su ficha; atras vuelve al menu
      if (atras || ejeLarga) vistaCluster = VC_MENU;
      else if (giro != 0 && numVistaSpots > 0) {
        selSpot = (selSpot + numVistaSpots + giro) % numVistaSpots;
        if (selSpot == 0) refrescarVistaSpots();
      } else if (pulsa) {
        if (numVistaSpots == 0) {
          vistaCluster = VC_MENU;
        } else {
          fichaSpot = vistaSpots[selSpot];
          focoFichaCluster = 0;
          avisoCluster = "";
          vistaCluster = VC_FICHA;
        }
      }
      break;

    case VC_FICHA:
      if (avisoCluster.length() > 0) { avisoCluster = ""; break; }
      if (atras) vistaCluster = VC_SPOTS;
      else if (giro != 0) focoFichaCluster ^= 1;
      else if (pulsa || ejeLarga) {
        if (focoFichaCluster == 0) spotASu();
        else vistaCluster = VC_SPOTS;
      }
      break;

    case VC_OYEN:
      if (atras || ejeLarga) vistaCluster = VC_MENU;
      else if (giro != 0 && numOyentes > 0) selOyente = (selOyente + numOyentes + giro) % numOyentes;
      else if (pulsa) {
        if (numOyentes == 0) {
          vistaCluster = VC_MENU;
        } else {
          uint8_t orden[CLUSTER_MAX_OYENTES];
          ordenOyentes(orden);
          fichaOyente = oyentes[orden[min(selOyente, (uint8_t)(numOyentes - 1))]];
          vistaCluster = VC_OYEN_FICHA;
        }
      }
      break;

    case VC_OYEN_FICHA:
      if (atras || pulsa || ejeLarga) vistaCluster = VC_OYEN;
      break;

    case VC_AJUSTES:
      // Foco: 0 nodo, 1 modo, 2 banda, 3 escucha en TRX (giran al editar), 4 volver. Los cambios los aplica
      // procesarCluster()
      if (atras) { guardarClusterNVS(); vistaCluster = VC_MENU; }
      else if (giro != 0) {
        if (editandoValor) ajustarOpcionCluster(giro);
        else focoAjustesCluster = (focoAjustesCluster + N_AJUSTES_CLUSTER + 1 + giro) % (N_AJUSTES_CLUSTER + 1);
      } else if (pulsa) {
        if (focoAjustesCluster == N_AJUSTES_CLUSTER) { guardarClusterNVS(); vistaCluster = VC_MENU; }
        else editandoValor = !editandoValor;
      }
      break;

    case VC_PORTAL:
      if (procesarVistaPortal(focoPortalDatos, giro, pulsa, atras)) vistaCluster = VC_MENU;
      break;
  }
}
