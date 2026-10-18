// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: autospot en POTA (publicar el spot propio) y aviso para renovarlo.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// AUTOSPOT POTA
// ============================================================================
// POTA > VER PARQUES > pulsar abre la ficha con el parque marcado: PARQUE, ID (el de la estacion; editable para
// anadir /P o poner el de un radioclub), BANDA, FREC KHZ, COMENTARIO (NO, QRP, QRT o TEXTO) y TEXTO (el comentario
// propio, con el editor; se guarda). "enviar" publica el spot con
// el WiFi como ACTUALIZAR (en MANUAL lo enciende y al acabar lo apaga); sin conexion se dice en la ficha y se
// reintenta a mano. La API no esta documentada para terceros: es la que usan Ham2K PoLo u OpenHamClock (POST sin
// cuenta a api.pota.app/spot; la frecuencia en kHz, como texto). Las normas de POTA permiten el autospot; aqui solo
// sale a mano y con SPOT_ESPERA_MS como minimo entre dos.
// El spot desaparece de pota.app unos 30 min despues del ultimo (o con QRT en el comentario): a los SPOT_AVISO_MS
// de uno bueno sin QRT, un pitido y un aviso para renovarlo (spot = a la ficha con el foco en "enviar").
#define URL_SPOT_POTA  "https://api.pota.app/spot"
#define SPOT_FUENTE    "CHISPAS"
#define SPOT_ESPERA_MS (30UL * 1000)
#define SPOT_AVISO_MS  (25UL * 60 * 1000)
#define SPOT_ID_MAX    12  // "EA8/EA4ILN/P"
#define SPOT_TEXTO_MAX 30  // Comentario propio (NVS "sptexto")

// Bandas de la ficha: limites en kHz (region 1) y una frecuencia CW habitual, la que se pone al cambiar de banda
struct BandaSpot { const char* nombre; uint16_t minKhz, maxKhz, tipicaKhz; };
const BandaSpot BANDAS_SPOT[] = {
  { "160M", 1810, 2000, 1830 },    { "80M", 3500, 3800, 3530 },     { "40M", 7000, 7200, 7030 },
  { "30M", 10100, 10150, 10110 },  { "20M", 14000, 14350, 14060 },  { "17M", 18068, 18168, 18080 },
  { "15M", 21000, 21450, 21060 },  { "12M", 24890, 24990, 24910 },  { "10M", 28000, 29700, 28060 },
  { "6M", 50000, 52000, 50090 }
};
#define N_BANDAS_SPOT (sizeof(BANDAS_SPOT) / sizeof(BANDAS_SPOT[0]))
#define BANDA_SPOT_DEFECTO 4  // 20M

enum ComentarioSpot { SPOT_COM_NO, SPOT_COM_QRP, SPOT_COM_QRT, SPOT_COM_TEXTO };
const char* const TEXTOS_COMENTARIO_SPOT[] = { "", "QRP", "QRT" };  // TEXTO: spotTexto

// Ficha: 0 parque, 1 ID, 2 banda, 3 frecuencia, 4 comentario, 5 texto; en el pie 6 enviar y 7 volver
#define N_OPCIONES_SPOT 6
const char* const NOMBRES_OPCIONES_SPOT[N_OPCIONES_SPOT] = { "PARQUE", "ID", "BANDA", "FREC KHZ", "COMENTARIO",
                                                             "TEXTO" };
#define FOCO_SPOT_ENVIAR (N_OPCIONES_SPOT)
#define FOCO_SPOT_VOLVER (N_OPCIONES_SPOT + 1)

uint8_t spotBanda = BANDA_SPOT_DEFECTO;   // Banda y frecuencia: NVS "spband" y "spfrec" (las de la ultima activacion)
uint16_t spotFrecKhz = 14060;
uint8_t spotComentario = SPOT_COM_NO;     // No se guarda: un QRT olvidado cerraria la siguiente activacion
char spotParque[12] = "";                 // "ES-0072"
uint8_t spotParqueIdx = 0;                // En parquesCercanos
char spotId[SPOT_ID_MAX + 1] = "";        // Vacio = aun no se ha abierto la ficha: se pone el de la estacion
char spotTexto[SPOT_TEXTO_MAX + 1] = "";  // Comentario propio
uint8_t focoSpot = 3;
bool spotNvsLeido = false;
unsigned long spotUltimoMs = 0;           // Ultimo spot bueno (0 = ninguno)
unsigned long spotAvisoDesdeMs = 0;       // Cuenta del aviso para renovarlo (0 = no hay que avisar)
bool avisoRespot = false;                 // Aviso a la vista (en cualquier pantalla, ver avisoRespotPermitido)
uint8_t focoAvisoRespot = 0;              // 0 spot, 1 ahora no

void desactivarCara();  // Definida con el salvapantallas
template <typename T>
void ajustarValor(T& valor, int paso, int minimo, int maximo);  // Definida con la entrada

void leerSpotNVS() {
  if (spotNvsLeido) return;
  spotNvsLeido = true;
  prefs.begin(NVS_ENTRENADOR, true);
  spotBanda = min(prefs.getUChar("spband", BANDA_SPOT_DEFECTO), (uint8_t)(N_BANDAS_SPOT - 1));
  spotFrecKhz = prefs.getUShort("spfrec", BANDAS_SPOT[spotBanda].tipicaKhz);
  if (prefs.isKey("sptexto")) prefs.getString("sptexto", spotTexto, sizeof(spotTexto));  // Sin la clave, el core avisa
  prefs.end();
  const BandaSpot& b = BANDAS_SPOT[spotBanda];
  if (spotFrecKhz < b.minKhz || spotFrecKhz > b.maxKhz) spotFrecKhz = b.tipicaKhz;
}

void guardarSpotNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("spband", spotBanda);
  prefs.putUShort("spfrec", spotFrecKhz);
  prefs.end();
}

void elegirParqueSpot(uint8_t idx) {
  if (idx >= numParquesCercanos) return;
  spotParqueIdx = idx;
  snprintf(spotParque, sizeof(spotParque), "ES-%04u", parquesCercanos[idx].numero);
}

// VER PARQUES > pulsar: la ficha con ese parque, el foco en la frecuencia (lo que mas cambia)
void abrirAutospot(uint8_t idxParque) {
  leerSpotNVS();
  elegirParqueSpot(idxParque);
  if (spotId[0] == '\0') strlcpy(spotId, indicativoEstacion, sizeof(spotId));
  focoSpot = 3;
  editandoValor = false;
  resultadoSpot = "";
  subDatos = DATOS_AUTOSPOT;
}

// ID del activador (EDITOR_SPOT) o comentario propio (EDITOR_SPOT_TEXTO)
void autospotAbrirEditor(int8_t destino) {
  bool esTexto = destino == EDITOR_SPOT_TEXTO;
  editorDestino = destino;
  editorTexto = String(esTexto ? spotTexto : spotId);
  editorMax = esTexto ? SPOT_TEXTO_MAX : SPOT_ID_MAX;
  editorSel = 0;
  editorError = false;
  subDatos = DATOS_EDITOR_SPOT;
}

// OK con el ID: como el de CONF (3 o mas letras y cifras, y "/"); si no vale, se avisa y se sigue editando.
// OK con el texto: se guarda en la NVS y, si no esta vacio, el comentario pasa a TEXTO
void autospotCerrarEditor(bool guardar) {
  if (guardar && editorDestino == EDITOR_SPOT_TEXTO) {
    String texto = editorTexto;
    texto.trim();
    strlcpy(spotTexto, texto.c_str(), sizeof(spotTexto));
    prefs.begin(NVS_ENTRENADOR, false);
    prefs.putString("sptexto", spotTexto);
    prefs.end();
    if (spotTexto[0]) spotComentario = SPOT_COM_TEXTO;
  } else if (guardar) {
    String id = editorTexto;
    id.trim();
    id.toUpperCase();
    bool valido = id.length() >= 3 && id.length() <= SPOT_ID_MAX;
    for (size_t i = 0; i < id.length(); i++) {
      if (!isalnum(id[i]) && id[i] != '/') valido = false;
    }
    if (!valido) {
      editorError = true;
      return;
    }
    strlcpy(spotId, id.c_str(), sizeof(spotId));
  }
  editorError = false;
  subDatos = DATOS_AUTOSPOT;
}

void ajustarOpcionSpot(int8_t giro) {
  switch (focoSpot) {
    case 0:
      if (numParquesCercanos > 0) elegirParqueSpot((spotParqueIdx + numParquesCercanos + giro) % numParquesCercanos);
      break;
    case 2:
      spotBanda = (spotBanda + N_BANDAS_SPOT + giro) % N_BANDAS_SPOT;
      spotFrecKhz = BANDAS_SPOT[spotBanda].tipicaKhz;
      break;
    case 3:
      ajustarValor(spotFrecKhz, giro, BANDAS_SPOT[spotBanda].minKhz, BANDAS_SPOT[spotBanda].maxKhz);
      break;
  }
}

// "enviar": se comprueba lo que se puede aqui y se pide el envio, que hace procesarWifi() al haber conexion
void pedirEnvioSpot() {
  if (spotParque[0] == '\0') { resultadoSpot = "SIN PARQUE"; return; }
  if (spotUltimoMs != 0 && millis() - spotUltimoMs < SPOT_ESPERA_MS) {
    resultadoSpot = "ESPERA " + String((SPOT_ESPERA_MS - (millis() - spotUltimoMs)) / 1000 + 1) + " S";
    return;
  }
  guardarSpotNVS();
  resultadoSpot = "";
  spotPendiente = true;
  wifiPorPeticion = true;
  if (estadoWifi == WIFI_APAGADO || estadoWifi == WIFI_SIN_CONEXION) encenderWifi();
  pantallaPendiente = true;
}

/**
 * Publica el spot (lo llama procesarWifi() con el WiFi conectado; bloquea 1-3 s con el aviso en pantalla).
 * El resultado queda en resultadoSpot para el pie de la ficha. El certificado no se valida, como en las descargas:
 * el spot es publico y no lleva ninguna clave
 */
void enviarSpotPota() {
  JsonDocument doc;
  doc["activator"] = spotId;
  // El que lo publica: el ID de la estacion (con el de un radioclub en la ficha, se sabe quien lo puso)
  doc["spotter"] = strlen(indicativoEstacion) >= 3 ? indicativoEstacion : spotId;
  doc["frequency"] = String(spotFrecKhz);
  doc["reference"] = spotParque;
  doc["mode"] = "CW";
  doc["source"] = SPOT_FUENTE;
  doc["comments"] = spotComentario == SPOT_COM_TEXTO ? spotTexto : TEXTOS_COMENTARIO_SPOT[spotComentario];
  String cuerpo;
  serializeJson(doc, cuerpo);

  WiFiClientSecure cliente;
  cliente.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  int codigo = -1;
  String respuesta;
  if (http.begin(cliente, URL_SPOT_POTA)) {
    http.setUserAgent("Chispas-EA4ILN/" FW_VERSION);
    http.addHeader("Content-Type", "application/json");
    codigo = http.POST(cuerpo);
    if (codigo > 0) respuesta = http.getString();
    http.end();
  }
  Serial.printf("[SPOT] %s -> HTTP %d %s\n", cuerpo.c_str(), codigo, respuesta.substring(0, 120).c_str());

  if (codigo == HTTP_CODE_OK) {
    spotUltimoMs = millis();
    bool qrt = spotComentario == SPOT_COM_QRT;
    spotAvisoDesdeMs = qrt ? 0 : millis();  // Con QRT se acaba la activacion: ya no se avisa
    if (spotAvisoDesdeMs == 0 && !qrt) spotAvisoDesdeMs = 1;
    String hora = "";
    time_t ahora = time(nullptr);
    struct tm local;
    if (horaLocalValida(local)) {
      struct tm utc;
      gmtime_r(&ahora, &utc);
      char txt[8];
      snprintf(txt, sizeof(txt), " %02d%02dZ", utc.tm_hour, utc.tm_min);
      hora = txt;
    }
    resultadoSpot = (qrt ? String("QRT ENVIADO") : String("SPOT ENVIADO")) + hora;
  } else if (codigo > 0) {
    resultadoSpot = "ERROR: HTTP " + String(codigo);
  } else {
    resultadoSpot = "SIN RESPUESTA DE POTA";
  }
}

String valorOpcionSpot(uint8_t i) {
  switch (i) {
    case 0: return spotParque[0] ? String(spotParque) : String("-");
    case 1: return String(spotId);
    case 2: return String(BANDAS_SPOT[spotBanda].nombre);
    case 3: return String(spotFrecKhz);
    case 4:
      if (spotComentario == SPOT_COM_NO) return String("NO");
      if (spotComentario == SPOT_COM_TEXTO) return String("TEXTO");
      return String(TEXTOS_COMENTARIO_SPOT[spotComentario]);
    case 5: {
      // Lo que cabe a la derecha de "TEXTO": 14 caracteres (con ".." si sigue)
      String t = spotTexto[0] ? String(spotTexto) : String("-");
      return t.length() > 14 ? t.substring(0, 12) + ".." : t;
    }
    default: return String("");
  }
}

// Ficha: lista de opciones y, en el pie, enviar/volver o lo que paso con el ultimo envio (hasta tocar algo)
void dibujarFichaSpot() {
  dibujarListaOpciones(NOMBRES_OPCIONES_SPOT, N_OPCIONES_SPOT, focoSpot, valorOpcionSpot, 0xD);  // 0, 2 y 3
  if (resultadoSpot.length() > 0) dibujarPieCentrado(resultadoSpot);
  else dibujarPieDos(focoSpot == FOCO_SPOT_ENVIAR, "enviar", focoSpot == FOCO_SPOT_VOLVER, "volver");
}

// Donde puede salir el aviso de renovar: no al practicar (CW, LoRa), ni editando un texto, ni con el portal, la
// OTA o una descarga en marcha. Si no puede, espera
bool avisoRespotPermitido() {
  if (actualizandoWifi) return false;
  if (pantallaActual == PANTALLA_CW || pantallaActual == PANTALLA_LORA) return false;
  if (pantallaActual == PANTALLA_TRX && subTrx == TRX_EDITOR) return false;
  if (pantallaActual == PANTALLA_CONFIG && subConf != CONF_MENU && subConf != CONF_CONFIG) return false;
  bool pantallaDatos = pantallaActual == PANTALLA_PROPAGACION || pantallaActual == PANTALLA_SPOTS ||
                       pantallaActual == PANTALLA_SOL;
  if (pantallaDatos && (subDatos == DATOS_PORTAL || subDatos == DATOS_EDITOR_SPOT)) return false;  // ID o texto
  return true;
}

void procesarAutospot() {
  if (spotAvisoDesdeMs == 0 || avisoRespot) return;
  if (millis() - spotAvisoDesdeMs < SPOT_AVISO_MS || !avisoRespotPermitido()) return;
  spotAvisoDesdeMs = 0;
  avisoRespot = true;
  focoAvisoRespot = 0;
  if (caraActiva) desactivarCara();
  if (!motorCWOcupado()) aplicarFrecTono(true);  // Pitido corto (por el altavoz, nunca por KEY)
  pantallaPendiente = true;
  Serial.println(F("[SPOT] AVISO: RENOVAR EL SPOT"));
}

void dibujarAvisoRespot() {
  dibujarTituloMenu(" POTA ");
  dibujarTituloCentrado(String("RENOVAR EL SPOT?"), 27);
  dibujarTituloCentrado(String(spotParque) + " " + String(spotFrecKhz) + " KHZ", 38);
  dibujarPieDos(focoAvisoRespot == 0, "spot", focoAvisoRespot == 1, "ahora no");
}
