// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: propagacion (hamqsl), spots POTA y procesarWifi.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

String valorEtiquetaXml(const String& xml, const char* etiqueta) {
  String apertura = String("<") + etiqueta + ">";
  String cierre = String("</") + etiqueta + ">";
  int ini = xml.indexOf(apertura);
  if (ini < 0) return "";
  ini += apertura.length();
  int fin = xml.indexOf(cierre, ini);
  if (fin < 0) return "";
  String valor = xml.substring(ini, fin);
  valor.trim();
  return valor;
}

void actualizarPropagacion() {
  propagacion.intentoMs = millis();
  String xml;
  if (!descargarUrl(URL_PROPAGACION, xml)) return;

  propagacion.sfi = valorEtiquetaXml(xml, "solarflux").toInt();
  propagacion.aIndex = valorEtiquetaXml(xml, "aindex").toInt();
  propagacion.kIndex = valorEtiquetaXml(xml, "kindex").toInt();
  propagacion.manchas = valorEtiquetaXml(xml, "sunspots").toInt();
  strlcpy(propagacion.rayosX, valorEtiquetaXml(xml, "xray").c_str(), sizeof(propagacion.rayosX));
  strlcpy(propagacion.actualizado, valorEtiquetaXml(xml, "updated").c_str(), sizeof(propagacion.actualizado));
  for (uint8_t b = 0; b < 4; b++) {
    for (uint8_t t = 0; t < 2; t++) {
      // <band name="80m-40m" time="day">Fair</band>
      String clave = String("name=\"") + NOMBRES_BANDAS_XML[b] + "\" time=\"" + (t == 0 ? "day" : "night") + "\">";
      String valor = "";
      int ini = xml.indexOf(clave);
      if (ini >= 0) {
        ini += clave.length();
        int fin = xml.indexOf('<', ini);
        if (fin > ini) valor = xml.substring(ini, fin);
      }
      valor.trim();
      strlcpy(propagacion.bandas[b][t], valor.length() ? valor.c_str() : "?", sizeof(propagacion.bandas[b][t]));
    }
  }
  propagacion.validos = true;
  Serial.printf("[WIFI] PROPAGACION: SFI %d A %d K %d\n", propagacion.sfi, propagacion.aIndex, propagacion.kIndex);
}

int compararSpotsRecientePrimero(const void* a, const void* b) {
  return strcmp(((const SpotPota*)b)->tiempo, ((const SpotPota*)a)->tiempo);
}

void actualizarSpotsPota() {
  spotsPotaIntentoMs = millis();
  String json;
  if (!descargarUrl(URL_POTA, json)) return;

  JsonDocument filtro;
  filtro[0]["activator"] = true;
  filtro[0]["frequency"] = true;
  filtro[0]["mode"] = true;
  filtro[0]["reference"] = true;
  filtro[0]["spotTime"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, DeserializationOption::Filter(filtro));
  if (err) {
    Serial.printf("[WIFI] POTA: JSON NO VALIDO (%s)\n", err.c_str());
    return;
  }

  numSpotsPota = 0;
  for (JsonObject s : doc.as<JsonArray>()) {
    if (numSpotsPota >= MAX_SPOTS_POTA) break;
    if (strcmp(s["mode"] | "", "CW") != 0) continue;

    SpotPota& sp = spotsPota[numSpotsPota++];
    strlcpy(sp.indicativo, s["activator"] | "", sizeof(sp.indicativo));
    strlcpy(sp.frecuencia, s["frequency"] | "", sizeof(sp.frecuencia));
    strlcpy(sp.referencia, s["reference"] | "", sizeof(sp.referencia));
    strlcpy(sp.tiempo, s["spotTime"] | "", sizeof(sp.tiempo));

    // "7044.0" -> "7044"
    size_t n = strlen(sp.frecuencia);
    if (n > 2 && strcmp(&sp.frecuencia[n - 2], ".0") == 0) sp.frecuencia[n - 2] = '\0';
  }
  qsort(spotsPota, numSpotsPota, sizeof(SpotPota), compararSpotsRecientePrimero);
  spotsPotaValidos = true;
  paginaSpots = 0;
  Serial.printf("[WIFI] POTA: %u SPOTS CW\n", numSpotsPota);
}

bool tocaDescargarParques();  // Definidas con los parques POTA
void descargarParques();

/**
 * Opcion "actualizar": con WiFi descarga ya; sin WiFi intenta conectar.
 */
void pedirActualizacionWifi() {
  if (estadoWifi == WIFI_CONECTADO) forzarActualizacion = true;
  else if (estadoWifi == WIFI_APAGADO) encenderWifi();
}

/**
 * Enciende o apaga el WiFi al entrar o salir de sus pantallas, avanza la conexion o el
 * portal y descarga los datos cuando tocan. La descarga bloquea 1-3 s, pero en estas
 * pantallas no suena CW ni hay recepcion LoRa.
 */
void procesarWifi() {
  static uint8_t pantallaAnterior = 0;
  // PROP y POTA encienden el WiFi al entrar; SOL tambien, pero solo si aun no hay hora (para
  // sincronizarla por NTP). Si no conecta, su menu ofrece ABRIR WEBCONF y REINTENTAR. En CONF y SOL el WiFi
  // solo sigue encendido si el portal esta abierto: si se llega con el WiFi conectado (desde PROP o
  // POTA) se apaga, porque no se usa (eran unos 40 mA de mas, medido).
  if (pantallaActual != pantallaAnterior) {
    struct tm hora;
    bool solSinHora = pantallaActual == PANTALLA_SOL && !horaLocalValida(hora);
    bool enDatos = (pantallaActual == PANTALLA_PROPAGACION || pantallaActual == PANTALLA_SPOTS || solSinHora);
    bool conPortal = estadoWifi == WIFI_PORTAL &&
                     (pantallaActual == PANTALLA_CONFIG || pantallaActual == PANTALLA_SOL);
    bool usaWifi = enDatos || conPortal;
    if (enDatos && (estadoWifi == WIFI_APAGADO || estadoWifi == WIFI_SIN_CONEXION)) encenderWifi();
    if (!usaWifi && estadoWifi != WIFI_APAGADO) apagarWifi();
    pantallaAnterior = pantallaActual;
  }

  switch (estadoWifi) {
    case WIFI_CONECTANDO:
      if (WiFi.status() == WL_CONNECTED) {
        wifiConectado();
      } else if (millis() - inicioConexionWifi > WIFI_TIMEOUT_CONEXION_MS) {
        Serial.println(F("[WIFI] NO CONECTA"));
        quedarseSinConexion();
      }
      break;

    case WIFI_PORTAL:
      if (wm.process()) {
        wifiConectado();
      } else if (!wm.getConfigPortalActive()) {
        Serial.println(F("[WIFI] PORTAL CERRADO POR TIEMPO"));
        apagarWifi();
        // En PROP, POTA o SOL se vuelve a su menu sin conexion (ABRIR WEBCONF y REINTENTAR)
        if (pantallaActual == PANTALLA_PROPAGACION || pantallaActual == PANTALLA_SPOTS ||
            pantallaActual == PANTALLA_SOL) {
          quedarseSinConexion();
        }
        pantallaPendiente = true;
      }
      break;

    case WIFI_CONECTADO: {
      if (WiFi.status() != WL_CONNECTED) {
        estadoWifi = WIFI_CONECTANDO;
        inicioConexionWifi = millis();
        pantallaPendiente = true;
        break;
      }
      bool tocaPropagacion = pantallaActual == PANTALLA_PROPAGACION &&
          (forzarActualizacion || propagacion.intentoMs == 0 ||
           millis() - propagacion.intentoMs > REFRESCO_PROPAGACION_MS);
      bool tocaPota = pantallaActual == PANTALLA_SPOTS &&
          (forzarActualizacion || spotsPotaIntentoMs == 0 ||
           millis() - spotsPotaIntentoMs > REFRESCO_POTA_MS);
      if (tocaPropagacion || tocaPota) {
        forzarActualizacion = false;
        actualizandoWifi = true;
        actualizarPantalla();
        if (tocaPropagacion) actualizarPropagacion();
        if (tocaPota) actualizarSpotsPota();
        actualizandoWifi = false;
        pantallaPendiente = true;
      } else if (pantallaActual == PANTALLA_SPOTS && tocaDescargarParques()) {
        // Lista de parques: si no la hay o tiene mas de un mes (despues de los spots, ya con la hora del NTP)
        descargarParques();
        pantallaPendiente = true;
      }
      // Aviso de version nueva: una vez al dia, aprovechando el WiFi de PROP o POTA
      bool enDatos = pantallaActual == PANTALLA_PROPAGACION || pantallaActual == PANTALLA_SPOTS;
      if (enDatos && !actualizandoWifi && (!otaComprobada || millis() - otaComprobadaMs > OTA_COMPROBACION_MS)) {
        buscarActualizacion();
        pantallaPendiente = true;
      }
      break;
    }

    default:
      break;
  }
}

/**
 * Vista del portal: con el portal abierto, sus datos (red, clave y web); conectando, el aviso. Devuelve
 * false en los demas estados (no dibuja nada)
 */
bool dibujarEstadoWifi() {
  switch (estadoWifi) {
    case WIFI_CONECTANDO:
      display.setCursor(0, 22);
      display.print(F("CONECTANDO WIFI..."));
      return true;
    case WIFI_PORTAL:
      display.setCursor(0, 12);
      display.print(F("CONFIGURA EL WIFI:"));
      display.setCursor(0, 22);
      display.print(F("RED   ")); display.print(nombrePortal);
      display.setCursor(0, 32);
      display.print(F("CLAVE " PORTAL_CLAVE));
      display.setCursor(0, 42);
      display.print(F("WEB   192.168.4.1"));
      return true;
    default:
      return false;
  }
}

