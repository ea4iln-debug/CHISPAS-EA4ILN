// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: pantalla CONF, STATUS, OTA y actualizarPantalla().
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// PANTALLA CONF: CONFIGURACION, STATUS Y ACTUALIZACION
// ============================================================================
/**
 * Reset de fabrica: borra estadisticas, configuracion del entrenador, datos de la estacion
 * la red WiFi guardada y la lista de parques POTA, y reinicia.
 */
void resetFabrica() {
  Serial.println(F("[CONFIG] RESET DE FABRICA"));
  if (montarParques()) {  // Lista de parques en LittleFS: se vuelve a bajar al entrar en POTA con WiFi
    LittleFS.remove(FICHERO_PARQUES);
    LittleFS.remove(FICHERO_PARQUES_TMP);
  }
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.clear();
  prefs.end();
  prefs.begin(NVS_ESTACION, false);
  prefs.clear();
  prefs.end();
  wm.resetSettings();  // Red WiFi guardada

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 28);
  display.print(F("RESET DE FABRICA..."));
  display.display();
  delay(1500);
  ESP.restart();
}

// SSID de la red guardada, leido de la NVS del WiFi sin encender la radio (esp_wifi_get_config necesita el
// WiFi iniciado). El bloque "sta.ssid" lleva la longitud delante (4 bytes en el IDF 4.4; 1 en otras
// versiones). Vacio si no hay red guardada
String ssidGuardada() {
  // API de NVS de ESP-IDF directamente: sin red guardada no imprime errores (Preferences si)
  nvs_handle_t h;
  if (nvs_open("nvs.net80211", NVS_READONLY, &h) != ESP_OK) return String();
  uint8_t buf[40] = {0};
  size_t n = sizeof(buf);
  esp_err_t err = nvs_get_blob(h, "sta.ssid", buf, &n);
  nvs_close(h);
  if (err != ESP_OK || n == 0) return String();
  uint8_t cab = (n >= 36) ? 4 : 1;
  uint32_t largo = (cab == 4) ? (buf[0] | (buf[1] << 8) | (buf[2] << 16) | ((uint32_t)buf[3] << 24)) : buf[0];
  if (largo > 32 || largo > n - cab) return String();
  char ssid[33];
  memcpy(ssid, buf + cab, largo);
  ssid[largo] = '\0';
  return String(ssid);
}

// Motivo del ultimo arranque, en 11 caracteres como mucho
String textoMotivoReinicio() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "ENCENDIDO";
    case ESP_RST_EXT:       return "PIN RST";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "ERROR";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "DESPERTAR";
    case ESP_RST_BROWNOUT:  return "BAJA TENS.";
    default:                return "OTRO (" + String((int)esp_reset_reason()) + ")";
  }
}

// Tiempo desde el arranque: "03:15:07" o "2d 03:15:07"
String textoTiempoEncendido() {
  uint32_t s = millis() / 1000;
  char t[16];
  if (s >= 86400) snprintf(t, sizeof(t), "%lud %02lu:%02lu:%02lu", (unsigned long)(s / 86400),
                           (unsigned long)(s / 3600 % 24), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
  else snprintf(t, sizeof(t), "%02lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
                (unsigned long)(s % 60));
  return String(t);
}

String textoEstadoWifi() {
  switch (estadoWifi) {
    case WIFI_CONECTANDO:   return "CONECTANDO";
    case WIFI_PORTAL:       return "PORTAL ABIERTO";
    case WIFI_CONECTADO:    return "CONECTADO";
    case WIFI_SIN_CONEXION: return "SIN CONEXION";
    default:                return "APAGADO";
  }
}

const char* const NOMBRES_PAGINAS_STATUS[N_PAGINAS_STATUS] = { "ESTACION", "SISTEMA", "WIFI", "MEMORIA", "ARRANQUE",
                                                                "SENSOR", "GPS" };

// STATUS: cuatro lineas por pagina (21 caracteres) y, en el pie, la pagina y "volver"
void dibujarPaginaStatus() {
  char l[4][40];  // 21 caracteres a la vista; holgura para el compilador
  for (uint8_t i = 0; i < 4; i++) l[i][0] = '\0';
  switch (paginaStatus) {
    case 0: {  // Estacion
      snprintf(l[0], sizeof(l[0]), "ID      %s", indicativoEstacion);
      snprintf(l[1], sizeof(l[1]), "LOCATOR %s", locatorEstacion);
      struct tm t;
      if (!horaLocalValida(t)) {
        snprintf(l[2], sizeof(l[2]), "HORA    SIN HORA");
      } else {
        if (horaUtcCabecera) {
          time_t ahora = time(nullptr);
          gmtime_r(&ahora, &t);
        }
        snprintf(l[2], sizeof(l[2]), "HORA %02d:%02d%s %02d/%02d/%02d", t.tm_hour, t.tm_min, horaUtcCabecera ? "Z" : "",
                 t.tm_mday, t.tm_mon + 1, t.tm_year % 100);
      }
      snprintf(l[3], sizeof(l[3]), "FW      v%s", FW_VERSION);
      break;
    }
    case 1:  // Sistema
      if (bateriaActual.estado == SOLO_USB) snprintf(l[0], sizeof(l[0]), "BAT  SIN BATERIA");
      else snprintf(l[0], sizeof(l[0]), "BAT  %.2fV  %d%%", bateriaActual.voltaje, bateriaActual.porcentaje);
      snprintf(l[1], sizeof(l[1]), "LORA %s", estadoLoraStr.c_str());
      snprintf(l[2], sizeof(l[2]), "APAGADO %s", textoApagado().c_str());
      snprintf(l[3], sizeof(l[3]), "TONO %uHz  ACEL %s", frecTonoHz, textoAceleracion().c_str());
      break;
    case 2: {  // WiFi
      String red = ssidGuardada();
      if (red.length() == 0) red = "(ninguna)";
      snprintf(l[0], sizeof(l[0]), "RED  %.16s", red.c_str());
      snprintf(l[1], sizeof(l[1]), "WIFI %s", textoEstadoWifi().c_str());
      if (estadoWifi == WIFI_CONECTADO) snprintf(l[2], sizeof(l[2]), "IP   %s", WiFi.localIP().toString().c_str());
      else if (estadoWifi == WIFI_PORTAL) snprintf(l[2], sizeof(l[2]), "IP   192.168.4.1");
      else snprintf(l[2], sizeof(l[2]), "IP   --");
      uint8_t mac[6];
      esp_read_mac(mac, ESP_MAC_WIFI_STA);  // La misma MAC que da el nombre del portal
      snprintf(l[3], sizeof(l[3]), "AP   Chispas-%02X%02X", mac[4], mac[5]);
      break;
    }
    case 3: {  // Memoria
      nvs_stats_t nvs;
      bool hayNvs = nvs_get_stats(NULL, &nvs) == ESP_OK;
      snprintf(l[0], sizeof(l[0]), "HEAP LIBRE %u KB", ESP.getFreeHeap() / 1024);
      snprintf(l[1], sizeof(l[1]), "HEAP MINIMO %u KB", ESP.getMinFreeHeap() / 1024);
      snprintf(l[2], sizeof(l[2]), "MAYOR BLOQUE %u KB", ESP.getMaxAllocHeap() / 1024);
      snprintf(l[3], sizeof(l[3]), "NVS %u/%u", hayNvs ? nvs.used_entries : 0, hayNvs ? nvs.total_entries : 0);
      break;
    }
    case 5:  // Sensor ambiental
      if (bmeDireccion == 0) {
        snprintf(l[0], sizeof(l[0]), "SENSOR NO DETECTADO");
        snprintf(l[1], sizeof(l[1]), "BME280: SDA GPIO41");
        snprintf(l[2], sizeof(l[2]), "        SCL GPIO42");
        break;
      }
      snprintf(l[0], sizeof(l[0]), "%s EN 0x%02X", bmeConHumedad ? "BME280" : "BMP280", bmeDireccion);
      if (!bmeConHumedad) snprintf(l[1], sizeof(l[1]), "(SIN HUMEDAD)");
      else if (isnan(bmePresionHpa)) snprintf(l[1], sizeof(l[1]), "SIN LECTURA");
      else snprintf(l[1], sizeof(l[1]), "LECTURA OK");
      snprintf(l[2], sizeof(l[2]), "QNH %.1f %s", qnhHpa, qnhCalibrado ? "CALIBRADO" : "ESTANDAR");
      snprintf(l[3], sizeof(l[3]), "HISTORIAL %u MIN", bmeHistN > 0 ? (bmeHistN - 1) * 10 : 0);
      break;
    case PAGINA_STATUS_GPS:
      if (!gpsDetectado()) {
        snprintf(l[0], sizeof(l[0]), estadoGps == GPS_DETECTANDO ? "BUSCANDO EL GPS..." : "GPS NO DETECTADO");
        snprintf(l[1], sizeof(l[1]), "TX DEL GPS A GPIO47");
        snprintf(l[2], sizeof(l[2]), "RX DEL GPS A GPIO39");
        break;
      }
      snprintf(l[0], sizeof(l[0]), "%-11s%2u SAT", estadoGps == GPS_CON_FIJO ? "CON FIJO" :
               estadoGps == GPS_BUSCANDO ? "BUSCANDO" : "DORMIDO", gpsSatelites);
      if (!gpsHayPosicion) {
        snprintf(l[1], sizeof(l[1]), "SIN POSICION");
        break;
      }
      snprintf(l[1], sizeof(l[1]), "%.5f%c %.5f%c", fabsf(gpsLat), gpsLat < 0 ? 'S' : 'N', fabsf(gpsLon),
               gpsLon < 0 ? 'W' : 'E');
      snprintf(l[2], sizeof(l[2]), "ALT %.0fM  HDOP %.1f", gpsAltitud, gpsHdop);
      {
        char loc[7];
        latLonALocator(gpsLat, gpsLon, loc);
        snprintf(l[3], sizeof(l[3]), "%s  %s", loc, textoAntiguedadGps().c_str());
      }
      break;
    case 4:  // Arranque
      snprintf(l[0], sizeof(l[0]), "ENCENDIDO %s", textoTiempoEncendido().c_str());
      snprintf(l[1], sizeof(l[1]), "REINICIO  %s", textoMotivoReinicio().c_str());
      snprintf(l[2], sizeof(l[2]), "CPU       %u MHz", getCpuFrequencyMhz());
      break;
  }
  for (uint8_t i = 0; i < 4; i++) {
    display.setCursor(0, 12 + i * 10);
    display.print(l[i]);
  }
  dibujarPiePaginas(paginaStatus, N_PAGINAS_STATUS, NOMBRES_PAGINAS_STATUS[paginaStatus]);
}

// Valor de cada opcion de CONFIG (a la derecha de la linea)
String valorOpcionConf(uint8_t i) {
  switch (i) {
    case OC_ID:          return String(indicativoEstacion);
    case OC_LOCATOR:
      if (editandoValor && focoConf == OC_LOCATOR) return locatorEditAuto ? String("AUTO (GPS)") : String("MANUAL");
      return gpsDetectado() && locatorAuto ? "GPS " + String(locatorEstacion) : String(locatorEstacion);
    case OC_ALTITUD:
      if (editandoValor && focoConf == OC_ALTITUD) return String(altitudEditada) + " m";
      if (bmeDireccion == 0 || isnan(bmePresionHpa)) return String("--");
      return String(lroundf(altitudBarometrica())) + (qnhCalibrado ? " m" : " m?");
    case OC_HORA:        return horaUtcCabecera ? String("UTC") : String("LOCAL");
    case OC_RELOJ:       return String(NOMBRES_FUENTE_HORA[fuenteHoraActual()]);
    case OC_ACELERACION: return textoAceleracion();
    case OC_SALVAPANTALLAS: return textoSalvapantallas();
    case OC_APAGADO:     return textoApagado();
    case OC_WIFI_DATOS:  return wifiAuto ? String("AUTO") : String("MANUAL");
    case OC_TONO:        return String(frecTonoHz) + " Hz";
    case OC_WEBCONF:     return estadoWifi == WIFI_PORTAL ? String("ABIERTO") : String("");
    case OC_ACTUALIZAR:  return hayActualizacion() ? "v" + otaVersionNueva : String("");
    case OC_DEPURACION:  return depuracion != 0 ? String("SI") : String("NO");
    case OC_FABRICA:    return confirmandoFabrica ? String("OTRA VEZ") : String("");
    default:             return String("");
  }
}

const char* const NOMBRES_OPCIONES_CONF[N_OPCIONES_CONF - 1] = {
  "ID", "LOCATOR", "ALTITUD", "HORA", "RELOJ", "TONO", "ACELERACION", "SALVAPANTALLAS", "APAGADO", "WIFI DATOS", "WEBCONF", "ACTUALIZAR FW",
  "DEPURACION", "FABRICA"
};

// CONFIG: lista de opciones (4 a la vista, con el valor a la derecha) y "volver" en el pie
void dibujarListaConf() {
  dibujarListaOpciones(NOMBRES_OPCIONES_CONF, N_OPCIONES_CONF - 1, focoConf, valorOpcionConf,
                       (1UL << OC_LOCATOR) | (1UL << OC_ALTITUD) | (1UL << OC_HORA) | (1UL << OC_TONO) | (1UL << OC_ACELERACION) |
                       (1UL << OC_SALVAPANTALLAS) | (1UL << OC_APAGADO) | (1UL << OC_WIFI_DATOS));
  dibujarPieCentrado(focoConf == OC_VOLVER ? "> volver" : "  volver");
}

String valorOpcionDepu(uint8_t i) {
  return depuActiva(BITS_DEPU[i]) ? String("SI") : String("NO");
}

// DEPURACION: salidas por serie para pruebas en banco (pulsar cambia SI/NO; se guarda en la NVS)
void dibujarListaDepuracion() {
  dibujarListaOpciones(NOMBRES_OPCIONES_DEPU, N_OPCIONES_DEPU, focoDepu, valorOpcionDepu, 0);
  dibujarPieCentrado(focoDepu == N_OPCIONES_DEPU ? "> volver" : "  volver");
}

// Datos del portal (red, clave, web) con "ver qr" y "volver": CONF > WEBCONF y, sin conexion, PROP, POTA y SOL
void dibujarVistaPortal(uint8_t foco) {
  if (estadoWifi == WIFI_PORTAL || estadoWifi == WIFI_CONECTANDO) {
    dibujarEstadoWifi();
  } else {
    display.setCursor(0, 22);
    display.print(F("PORTAL CERRADO"));
    display.setCursor(0, 32);
    display.print(F("WIFI ")); display.print(textoEstadoWifi());
  }
  if (estadoWifi == WIFI_PORTAL) dibujarPieDos(foco == 0, "ver qr", foco == 1, "volver");
  else dibujarPieCentrado("> volver");
}

// Eventos de la vista del portal. Devuelve true cuando hay que salir de ella
bool procesarVistaPortal(uint8_t& foco, int8_t giro, bool pulsa, bool atras) {
  if (atras) return true;
  if (giro != 0 && estadoWifi == WIFI_PORTAL) foco = (foco + 1) % 2;
  else if (pulsa) {
    if (foco == 0 && estadoWifi == WIFI_PORTAL) vistaPortal = VISTA_QR_RED;
    else return true;
  }
  return false;
}

void dibujarPantallaOta();  // Definida justo debajo, con la logica de la actualizacion

// CONFIG > RELOJ: parte de la hora actual (UTC o local, como la cabecera) o, sin hora, de las 12:00 del dia en que
// se compilo el firmware
void confAbrirReloj() {
  struct tm t;
  if (horaLocalValida(t)) {
    if (horaUtcCabecera) {
      time_t ahora = time(nullptr);
      gmtime_r(&ahora, &t);
    }
    relojCampos[0] = t.tm_hour; relojCampos[1] = t.tm_min;
    relojCampos[2] = t.tm_mday; relojCampos[3] = t.tm_mon + 1; relojCampos[4] = t.tm_year + 1900;
  } else {
    const char* meses = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mes[4] = "";
    int dia = 1, anio = 2026;
    sscanf(__DATE__, "%3s %d %d", mes, &dia, &anio);
    const char* p = strstr(meses, mes);
    relojCampos[0] = 12; relojCampos[1] = 0;
    relojCampos[2] = dia; relojCampos[3] = p ? (p - meses) / 3 + 1 : 1; relojCampos[4] = anio;
  }
  relojCampo = 0;
  subConf = CONF_RELOJ;
}

void confAplicarReloj() {
  int16_t* c = relojCampos;
  const uint8_t diasMes[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  bool bisiesto = (c[4] % 4 == 0 && c[4] % 100 != 0) || c[4] % 400 == 0;
  uint8_t maxDia = diasMes[c[3] - 1] + (c[3] == 2 && bisiesto);
  if (c[2] > maxDia) c[2] = maxDia;
  time_t t;
  if (horaUtcCabecera) {
    t = epochUtc(c[4], c[3], c[2], c[0], c[1], 0);
  } else {
    struct tm tm = {};
    tm.tm_year = c[4] - 1900; tm.tm_mon = c[3] - 1; tm.tm_mday = c[2];
    tm.tm_hour = c[0]; tm.tm_min = c[1]; tm.tm_isdst = -1;  // Hora local: mktime aplica CET/CEST
    t = mktime(&tm);
  }
  ponerHora(t, HORA_MANUAL);
  Serial.printf("[CONFIG] RELOJ MANUAL %02d:%02d%s %02d/%02d/%04d\n", c[0], c[1], horaUtcCabecera ? "Z" : "", c[2],
                c[3], c[4]);
  subConf = CONF_CONFIG;
}

// PONER HORA: "14:05  13/10/2026" con el campo que se cambia en video inverso
void dibujarReloj() {
  dibujarTituloCentrado(F("- PONER HORA -"));
  dibujarTituloCentrado(horaUtcCabecera ? F("UTC") : F("LOCAL"), 22);
  const char* seps[RELOJ_N_CAMPOS] = { ":", "  ", "/", "/", "" };
  display.setCursor(13, 35);
  for (uint8_t i = 0; i < RELOJ_N_CAMPOS; i++) {
    char txt[6];
    snprintf(txt, sizeof(txt), i == 4 ? "%04d" : "%02d", relojCampos[i]);
    if (i == relojCampo) display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.print(txt);
    display.setTextColor(SSD1306_WHITE);
    display.print(seps[i]);
  }
  dibujarPieCentrado(relojCampo < RELOJ_N_CAMPOS - 1 ? "pulsar: siguiente" : "pulsar: poner hora");
}

void renderizarPantallaConfig() {
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  switch (subConf) {
    case CONF_MENU:
      dibujarTituloMenu(" CONFIGURACION ");
      dibujarOpcionCentrada(opcionPantallaConfig == 0, "< CONFIG >", 27);
      dibujarOpcionCentrada(opcionPantallaConfig == 1, "< STATUS >", 37);
      dibujarBarraAvanzarRetroceder(opcionPantallaConfig == OPCION_BARRA_CONFIG);
      break;

    case CONF_CONFIG:
      dibujarListaConf();
      break;

    case CONF_STATUS:
      dibujarPaginaStatus();
      break;

    case CONF_PORTAL:
      dibujarVistaPortal(focoPortalConf);
      break;

    case CONF_EDITOR:
      dibujarEditor();
      break;

    case CONF_OTA:
      dibujarPantallaOta();
      break;

    case CONF_DEPURACION:
      dibujarListaDepuracion();
      break;

    case CONF_RELOJ:
      dibujarReloj();
      break;
  }
}

// Editor de CONF: ID (indicativo de la estacion) o locator, con la rueda de las memorias
void confAbrirEditor(int8_t destino) {
  editorDestino = destino;
  editorTexto = destino == EDITOR_ID_ESTACION ? String(indicativoEstacion) : String(locatorEstacion);
  editorTexto.toUpperCase();
  editorMax = destino == EDITOR_ID_ESTACION ? TRAMA_LONG_INDICATIVO : 6;
  editorSel = 0;
  editorError = false;
  subConf = CONF_EDITOR;
}

// OK: si no es valido se avisa ("NO VALIDO") y se sigue editando. ESC: sale sin cambiar nada
void confCerrarEditor(bool guardar) {
  if (guardar) {
    bool esId = (editorDestino == EDITOR_ID_ESTACION);
    bool valido = esId ? aplicarIndicativo(editorTexto) : aplicarLocator(editorTexto);
    if (!valido) {
      editorError = true;
      return;
    }
    guardarEstacionNVS();
    Serial.printf("[CONFIG] %s: %s\n", esId ? "ID" : "LOCATOR", esId ? indicativoEstacion : locatorEstacion);
  }
  editorError = false;
  subConf = CONF_CONFIG;
}

// ACTUALIZAR FW: enciende el WiFi (si hace falta) y procesarOta() busca la ultima release al conectar
void confEntrarOta() {
  subConf = CONF_OTA;
  focoOta = 0;
  otaError = "";
  if (estadoWifi == WIFI_PORTAL) {
    estadoOta = OTA_ERROR;
    otaError = "CIERRA EL PORTAL";
    return;
  }
  estadoOta = OTA_CONECTANDO;
  if (estadoWifi == WIFI_APAGADO || estadoWifi == WIFI_SIN_CONEXION) encenderWifi();
}

// Al salir se apaga el WiFi: en CONF solo se usa para el portal
void confSalirOta() {
  subConf = CONF_CONFIG;
  estadoOta = OTA_REPOSO;
  if (estadoWifi != WIFI_PORTAL && estadoWifi != WIFI_APAGADO) apagarWifi();
}

void procesarOta() {
  if (pantallaActual != PANTALLA_CONFIG || subConf != CONF_OTA || estadoOta != OTA_CONECTANDO) return;
  if (estadoWifi == WIFI_SIN_CONEXION) {
    estadoOta = OTA_ERROR;
    otaError = "SIN CONEXION WIFI";
    pantallaPendiente = true;
  } else if (estadoWifi == WIFI_CONECTADO) {
    estadoOta = OTA_BUSCANDO;
    actualizarPantalla();
    if (!buscarActualizacion()) estadoOta = OTA_ERROR;
    else if (hayActualizacion()) estadoOta = OTA_HAY_NUEVA;
    else estadoOta = OTA_AL_DIA;
    pantallaPendiente = true;
  }
}

// Pantalla de ACTUALIZAR FW: version instalada, estado y, al descargar, una barra de progreso
void dibujarPantallaOta() {
  dibujarTituloCentrado(F("- ACTUALIZAR FW -"));
  display.setCursor(0, 22);
  display.print(F("INSTALADA  v" FW_VERSION));
  display.setCursor(0, 32);
  switch (estadoOta) {
    case OTA_CONECTANDO: display.print(F("CONECTANDO WIFI...")); break;
    case OTA_BUSCANDO:   display.print(F("BUSCANDO EN GITHUB...")); break;
    case OTA_AL_DIA:
      display.print(F("ULTIMA     v")); display.print(otaVersionNueva);
      display.setCursor(0, 42);
      // Mas nueva pero sin binario para esta placa (con o sin encoder): no se puede instalar
      display.print(compararVersiones(otaVersionNueva, FW_VERSION) > 0 ? F("SIN BINARIO PARA ESTA") : F("YA ESTA AL DIA"));
      break;
    case OTA_HAY_NUEVA:
      display.print(F("NUEVA      v")); display.print(otaVersionNueva);
      break;
    case OTA_DESCARGANDO: {
      display.print(F("INSTALANDO ")); display.print(otaProgreso); display.print('%');
      display.drawRect(0, 43, 128, 7, SSD1306_WHITE);
      display.fillRect(1, 44, 126 * otaProgreso / 100, 5, SSD1306_WHITE);
      break;
    }
    case OTA_HECHO:      display.print(F("INSTALADA: REINICIA")); break;
    default:             display.print(otaError); break;
  }
  if (estadoOta == OTA_HAY_NUEVA) dibujarPieDos(focoOta == 0, "instalar", focoOta == 1, "volver");
  else if (estadoOta == OTA_DESCARGANDO) dibujarPieCentrado(F("NO LA APAGUES"));
  else if (estadoOta != OTA_HECHO) dibujarPieCentrado(F("> volver"));
}

// STATUS se redibuja cada segundo (hora, tiempo encendido, memoria)
void procesarStatus() {
  static unsigned long ultimo = 0;
  if (pantallaActual != PANTALLA_CONFIG || subConf != CONF_STATUS) return;
  if (millis() - ultimo < 1000) return;
  ultimo = millis();
  pantallaPendiente = true;
}

void actualizarPantalla() {
  if (caraActiva) return;  // La cara se dibuja sola (procesarCara)
  if (viendoQrPortal()) {  // Portal abierto: QR a pantalla completa
    dibujarPantallaQr();
    return;
  }
  display.clearDisplay();
  dibujarEncabezado();

  switch (pantallaActual) {
    case PANTALLA_INICIO: renderizarPantallaInicio(); break;
    case PANTALLA_CW: renderizarPantallaCW(); break;
    case PANTALLA_LORA: renderizarPantallaLora(); break;
    case PANTALLA_TRX: renderizarPantallaTrx(); break;
    case PANTALLA_PROPAGACION: renderizarPantallaPropagacion(); break;
    case PANTALLA_SPOTS: renderizarPantallaSpots(); break;
    case PANTALLA_SOL: renderizarPantallaSol(); break;
    case PANTALLA_CONFIG: renderizarPantallaConfig(); break;
  }

  display.display();
}

