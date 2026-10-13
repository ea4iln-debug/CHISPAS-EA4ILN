// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: actualizacion OTA desde GitHub.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// ACTUALIZACION DEL FIRMWARE (OTA) DESDE LAS RELEASES DE GITHUB
// ============================================================================
// CONF > CONFIG > ACTUALIZAR FW: consulta la ultima release (tambien las beta), compara su etiqueta ("v1.01b")
// con FW_VERSION y, si es mas nueva y el usuario confirma, descarga el binario de esta placa y lo instala en la
// otra particion de aplicacion (default_8MB.csv: app0/app1). El TLS se valida con los certificados raiz de
// include/certificados_github.h. Ademas, con el WiFi ya encendido en PROP o POTA, se mira una vez al dia si hay
// version nueva y se avisa en su linea de estado.
#define OTA_URL_RELEASES "https://api.github.com/repos/ea4iln-debug/CHISPAS-EA4ILN/releases?per_page=1"
#define OTA_COMPROBACION_MS (24UL * 60 * 60 * 1000)
#define OTA_BATERIA_MIN_PCT 30  // Con menos bateria (y sin USB) no se instala: un corte a medias obliga a cargar por USB

enum EstadoOta { OTA_REPOSO, OTA_CONECTANDO, OTA_BUSCANDO, OTA_AL_DIA, OTA_HAY_NUEVA, OTA_DESCARGANDO, OTA_ERROR, OTA_HECHO };
EstadoOta estadoOta = OTA_REPOSO;
String otaVersionNueva = "";   // Etiqueta de la ultima release, sin la "v"
String otaUrlBinario = "";     // Binario de esta placa en esa release ("" si no lo trae)
String otaError = "";          // Motivo del fallo, corto (cabe en una linea)
uint8_t otaProgreso = 0;       // % descargado
bool otaComprobada = false;
unsigned long otaComprobadaMs = 0;
uint8_t focoOta = 0;           // Con version nueva: 0 instalar, 1 volver

/**
 * Compara versiones "1.00b", "1.01b", "1.10": numeros separados por puntos y, al final, una letra opcional
 * (beta). A igual numero, la version sin letra es mas nueva. Devuelve >0 si a es mas nueva que b
 */
int compararVersiones(const String& a, const String& b) {
  auto trocear = [](const String& v, long num[3], bool& beta) {
    uint8_t n = 0;
    num[0] = num[1] = num[2] = 0;
    beta = false;
    for (size_t i = 0; i < v.length(); i++) {
      char c = v[i];
      if (isdigit(c) && n < 3) num[n] = num[n] * 10 + (c - '0');
      else if (c == '.') n++;
      else if (isalpha(c)) beta = true;
    }
  };
  long na[3], nb[3];
  bool betaA, betaB;
  trocear(a, na, betaA);
  trocear(b, nb, betaB);
  for (uint8_t i = 0; i < 3; i++) {
    if (na[i] != nb[i]) return na[i] > nb[i] ? 1 : -1;
  }
  if (betaA != betaB) return betaA ? -1 : 1;
  return 0;
}

bool hayActualizacion() {
  return otaUrlBinario.length() > 0 && compararVersiones(otaVersionNueva, FW_VERSION) > 0;
}

/**
 * Consulta la ultima release en la API de GitHub (con el WiFi conectado). Deja la version y la URL del binario
 * de esta placa: "chispas-<version>.bin" o, sin encoder, "chispas-<version>-sin-encoder.bin". Bloquea 1-3 s
 */
bool buscarActualizacion() {
  WiFiClientSecure cliente;
  cliente.setCACert(CERTIFICADOS_GITHUB);
  HTTPClient http;
  http.setTimeout(8000);
  String cuerpo;
  int codigo = -1;
  if (http.begin(cliente, OTA_URL_RELEASES)) {
    http.setUserAgent("Chispas-EA4ILN/" FW_VERSION);  // La API de GitHub lo exige
    http.addHeader("Accept", "application/vnd.github+json");
    codigo = http.GET();
    if (codigo == HTTP_CODE_OK) cuerpo = http.getString();
    http.end();
  }
  otaComprobada = true;
  otaComprobadaMs = millis();
  if (codigo != HTTP_CODE_OK) {
    otaError = codigo < 0 ? "SIN CONEXION" : "GITHUB: HTTP " + String(codigo);
    Serial.printf("[OTA] RELEASES -> %d\n", codigo);
    return false;
  }

  // Solo la etiqueta y los binarios: el resto de la respuesta (notas, autores...) se descarta al leer
  JsonDocument filtro;
  filtro[0]["tag_name"] = true;
  filtro[0]["assets"][0]["name"] = true;
  filtro[0]["assets"][0]["browser_download_url"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, cuerpo, DeserializationOption::Filter(filtro))) {
    otaError = "RESPUESTA NO VALIDA";
    return false;
  }
  if (doc[0].isNull()) {
    otaError = "NO HAY RELEASES";
    return false;
  }
  String etiqueta = doc[0]["tag_name"] | "";
  if (etiqueta.startsWith("v") || etiqueta.startsWith("V")) etiqueta.remove(0, 1);
  otaVersionNueva = etiqueta;
  otaUrlBinario = "";
  // Nombre exacto: la release lleva tambien las imagenes completas para grabar por USB ("...-usb.bin")
#ifdef SIN_ENCODER
  String binario = "chispas-" + etiqueta + "-sin-encoder.bin";
#else
  String binario = "chispas-" + etiqueta + ".bin";
#endif
  for (JsonObject a : doc[0]["assets"].as<JsonArray>()) {
    if (binario.equalsIgnoreCase(a["name"] | "")) otaUrlBinario = a["browser_download_url"] | "";
  }
  Serial.printf("[OTA] ULTIMA RELEASE v%s (instalada v%s)%s\n", otaVersionNueva.c_str(), FW_VERSION,
                otaUrlBinario.length() ? "" : ", SIN BINARIO PARA ESTA PLACA");
  return true;
}

void actualizarPantalla();  // Definida con el resto del dibujo

/**
 * Descarga e instala el binario (sigue la redireccion de GitHub a su servidor de descargas). La pantalla
 * enseña el progreso; al terminar, reinicia. Si falla, la version instalada sigue intacta
 */
void instalarActualizacion() {
  if (bateriaActual.estado != SOLO_USB && bateriaActual.porcentaje < OTA_BATERIA_MIN_PCT) {
    estadoOta = OTA_ERROR;
    otaError = "BATERIA BAJA (<" + String(OTA_BATERIA_MIN_PCT) + "%)";
    return;
  }
  estadoOta = OTA_DESCARGANDO;
  otaProgreso = 0;
  actualizarPantalla();
  motorCWDetener();
  keyerPausar();
  Serial.printf("[OTA] DESCARGANDO %s\n", otaUrlBinario.c_str());

  WiFiClientSecure cliente;
  cliente.setCACert(CERTIFICADOS_GITHUB);
  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(false);
  httpUpdate.onProgress([](int hecho, int total) {
    uint8_t pct = total > 0 ? (uint8_t)((int64_t)hecho * 100 / total) : 0;
    if (pct != otaProgreso) {
      otaProgreso = pct;
      actualizarPantalla();
    }
  });
  t_httpUpdate_return r = httpUpdate.update(cliente, otaUrlBinario);
  if (r == HTTP_UPDATE_OK) {
    estadoOta = OTA_HECHO;
    actualizarPantalla();
    Serial.printf("[OTA] INSTALADA v%s: REINICIANDO\n", otaVersionNueva.c_str());
    delay(2000);
    ESP.restart();
  }
  estadoOta = OTA_ERROR;
  otaError = "ERROR " + String(httpUpdate.getLastError());
  Serial.printf("[OTA] FALLO: %s\n", httpUpdate.getLastErrorString().c_str());
}

