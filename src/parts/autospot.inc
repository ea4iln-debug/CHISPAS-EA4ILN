// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: autospot en POTA (publicar el spot propio), activacion en curso y aviso para renovarlo.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// AUTOSPOT POTA
// ============================================================================
// POTA > VER PARQUES > pulsar > spot abre la ficha con el parque marcado: PARQUE, ID (el de la estacion; editable
// para anadir /P o poner el de un radioclub), BANDA, FREC KHZ, COMENTARIO (NO, QRP, QRT o TEXTO) y TEXTO (el
// comentario propio, con el editor; se guarda). "enviar" publica el spot con el WiFi como ACTUALIZAR (en MANUAL lo
// enciende y al acabar lo apaga); sin conexion se dice en la ficha y se reintenta a mano. La API no esta documentada
// para terceros: es la que usan Ham2K PoLo u OpenHamClock (POST sin cuenta a api.pota.app/spot; la frecuencia en
// kHz, como texto). Las normas de POTA permiten el autospot; aqui solo sale a mano y con SPOT_ESPERA_MS como minimo
// entre dos.
// ACTIVACION EN CURSO: el primer spot bueno sin QRT la abre (parque, ID, inicio, ultimo spot y cuantos; en la NVS,
// sobrevive a dormir o apagar). Mientras dura, el menu de POTA muestra el parque en lugar de VER PARQUES, Inicio dice
// "ACTIVANDO ES-xxxx" y la ficha cambia: arriba el parque, la duracion y lo que hace del ultimo spot; BANDA, FREC KHZ,
// COMENTARIO y TEXTO para renovarlo (o un QSY); SPOTS, INICIO e ID de consulta; CERRAR (sin QRT, pulsando dos veces);
// y en el pie spot, qrt (envia QRT y la cierra) y volver. Un spot con COMENTARIO QRT tambien la cierra.
// El spot desaparece de pota.app unos 30 min despues del ultimo: a los SPOT_AVISO_S, un pitido y un aviso para
// renovarlo (spot = a la ficha con el foco en "spot"). Los tiempos van por la hora (GPS o NTP) si la hay, para que
// valgan tras reiniciar; si no, por millis() de este arranque.
#define URL_SPOT_POTA  "https://api.pota.app/spot"
#define SPOT_FUENTE    "CHISPAS"
#define SPOT_ESPERA_MS (30UL * 1000)
#define SPOT_AVISO_S   (25L * 60)
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

// Ficha sin activacion: 0 parque, 1 ID, 2 banda, 3 frecuencia, 4 comentario, 5 texto; en el pie enviar y volver
#define N_OPCIONES_SPOT 6
const char* const NOMBRES_OPCIONES_SPOT[N_OPCIONES_SPOT] = { "PARQUE", "ID", "BANDA", "FREC KHZ", "COMENTARIO",
                                                             "TEXTO" };
#define FOCO_SPOT_ENVIAR (N_OPCIONES_SPOT)
#define FOCO_SPOT_VOLVER (N_OPCIONES_SPOT + 1)

// Ficha con la activacion en curso (3 filas a la vista bajo la linea de estado): 0 banda, 1 frecuencia, 2 comentario,
// 3 texto, 4 spots, 5 inicio, 6 ID, 7 cerrar; en el pie spot, qrt y volver
#define N_OPCIONES_ACT 8
const char* const NOMBRES_OPCIONES_ACT[N_OPCIONES_ACT] = { "BANDA", "FREC KHZ", "COMENTARIO", "TEXTO", "SPOTS",
                                                           "INICIO", "ID", "CERRAR" };
#define FOCO_ACT_CERRAR 7
#define FOCO_ACT_SPOT   (N_OPCIONES_ACT)
#define FOCO_ACT_QRT    (N_OPCIONES_ACT + 1)
#define FOCO_ACT_VOLVER (N_OPCIONES_ACT + 2)

uint8_t spotBanda = BANDA_SPOT_DEFECTO;   // Banda y frecuencia: NVS "spband" y "spfrec" (las de la ultima activacion)
uint16_t spotFrecKhz = 14060;
uint8_t spotComentario = SPOT_COM_NO;     // No se guarda: un QRT olvidado cerraria la siguiente activacion
char spotParque[12] = "";                 // "ES-0072" (con activacion, NVS "actpq")
uint8_t spotParqueIdx = 0;                // En parquesCercanos
char spotId[SPOT_ID_MAX + 1] = "";        // Vacio = aun no se ha abierto la ficha: se pone el de la estacion
char spotTexto[SPOT_TEXTO_MAX + 1] = "";  // Comentario propio
uint8_t focoSpot = 3;
bool spotNvsLeido = false;
bool spotEnviarQrt = false;               // El envio pedido es el "qrt" del pie (comentario QRT y cierra)
bool confirmandoCerrar = false;           // CERRAR pulsado una vez
unsigned long spotUltimoMs = 0;           // Ultimo spot bueno en este arranque (para SPOT_ESPERA_MS)

// Activacion en curso (NVS "act", "actpq", "actid", "actini", "actult", "actn"). Los epoch son UTC; 0 = sin hora
bool actActiva = false;
uint32_t actInicio = 0, actUltimo = 0;
unsigned long actInicioMs = 0, actUltimoMs = 0;  // Respaldo sin hora (solo este arranque; 0 = no se sabe)
uint8_t actSpots = 0;
bool avisadoUltimoSpot = false;           // Ya salio (o se descarto) el aviso del ultimo spot
bool avisoRespot = false;                 // Aviso a la vista (en cualquier pantalla, ver avisoRespotPermitido)
uint8_t focoAvisoRespot = 0;              // 0 spot, 1 ahora no

void desactivarCara();  // Definida con el salvapantallas
template <typename T>
void ajustarValor(T& valor, int paso, int minimo, int maximo);  // Definida con la entrada

uint32_t epochAhora() {
  struct tm t;
  return horaLocalValida(t) ? (uint32_t)time(nullptr) : 0;
}

// Segundos desde un momento: por la hora si se sabe; si no, por millis() de este arranque; -1 = no se sabe
long segundosDesde(uint32_t epoch, unsigned long ms) {
  uint32_t ahora = epochAhora();
  if (epoch != 0 && ahora >= epoch) return (long)(ahora - epoch);
  if (ms != 0) return (long)((millis() - ms) / 1000);
  return -1;
}

void leerSpotNVS() {
  if (spotNvsLeido) return;
  spotNvsLeido = true;
  prefs.begin(NVS_ENTRENADOR, true);
  spotBanda = min(prefs.getUChar("spband", BANDA_SPOT_DEFECTO), (uint8_t)(N_BANDAS_SPOT - 1));
  spotFrecKhz = prefs.getUShort("spfrec", BANDAS_SPOT[spotBanda].tipicaKhz);
  if (prefs.isKey("sptexto")) prefs.getString("sptexto", spotTexto, sizeof(spotTexto));  // Sin la clave, el core avisa
  actActiva = prefs.getBool("act", false);
  if (actActiva) {
    if (prefs.isKey("actpq")) prefs.getString("actpq", spotParque, sizeof(spotParque));
    if (prefs.isKey("actid")) prefs.getString("actid", spotId, sizeof(spotId));
    actInicio = prefs.getUInt("actini", 0);
    actUltimo = prefs.getUInt("actult", 0);
    actSpots = prefs.getUChar("actn", 0);
    if (spotParque[0] == '\0' || spotId[0] == '\0') actActiva = false;
  }
  prefs.end();
  const BandaSpot& b = BANDAS_SPOT[spotBanda];
  if (spotFrecKhz < b.minKhz || spotFrecKhz > b.maxKhz) spotFrecKhz = b.tipicaKhz;
  if (actActiva) Serial.printf("[SPOT] ACTIVACION EN CURSO: %s %s, %u SPOTS\n", spotId, spotParque, actSpots);
}

void guardarSpotNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("spband", spotBanda);
  prefs.putUShort("spfrec", spotFrecKhz);
  prefs.end();
}

void guardarActivacionNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putBool("act", actActiva);
  if (actActiva) {
    prefs.putString("actpq", spotParque);
    prefs.putString("actid", spotId);
    prefs.putUInt("actini", actInicio);
    prefs.putUInt("actult", actUltimo);
    prefs.putUChar("actn", actSpots);
  }
  prefs.end();
}

void cerrarActivacion() {
  actActiva = false;
  avisoRespot = false;
  guardarActivacionNVS();
  Serial.printf("[SPOT] ACTIVACION CERRADA: %s, %u SPOTS\n", spotParque, actSpots);
}

void elegirParqueSpot(uint8_t idx) {
  if (idx >= numParquesCercanos) return;
  spotParqueIdx = idx;
  snprintf(spotParque, sizeof(spotParque), "ES-%04u", parquesCercanos[idx].numero);
}

// VER PARQUES > pulsar > spot: la ficha con ese parque, el foco en la frecuencia (lo que mas cambia)
void abrirAutospot(uint8_t idxParque) {
  leerSpotNVS();
  elegirParqueSpot(idxParque);
  if (spotId[0] == '\0') strlcpy(spotId, indicativoEstacion, sizeof(spotId));
  focoSpot = 3;
  editandoValor = false;
  confirmandoCerrar = false;
  resultadoSpot = "";
  subDatos = DATOS_AUTOSPOT;
}

// Menu de POTA (opcion del parque) o aviso de renovar: la ficha de la activacion en curso
void abrirActivacion(uint8_t foco) {
  leerSpotNVS();
  focoSpot = foco;
  editandoValor = false;
  confirmandoCerrar = false;
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

// Valor que se esta editando en la ficha (parque, banda o frecuencia)
void ajustarOpcionSpot(int8_t giro) {
  uint8_t opcion = actActiva ? focoSpot + 2 : focoSpot;  // Con activacion no hay parque ni ID: banda es la 0
  switch (opcion) {
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

// "enviar"/"spot" (qrt = false) o "qrt": se comprueba lo que se puede aqui y se pide el envio, que hace
// procesarWifi() al haber conexion
void pedirEnvioSpot(bool qrt) {
  if (spotParque[0] == '\0') { resultadoSpot = "SIN PARQUE"; return; }
  if (spotUltimoMs != 0 && millis() - spotUltimoMs < SPOT_ESPERA_MS) {
    resultadoSpot = "ESPERA " + String((SPOT_ESPERA_MS - (millis() - spotUltimoMs)) / 1000 + 1) + " S";
    return;
  }
  guardarSpotNVS();
  spotEnviarQrt = qrt;
  resultadoSpot = "";
  spotPendiente = true;
  wifiPorPeticion = true;
  if (estadoWifi == WIFI_APAGADO || estadoWifi == WIFI_SIN_CONEXION) encenderWifi();
  pantallaPendiente = true;
}

// "1727Z" de un epoch; "?" sin hora
String horaZ(uint32_t epoch) {
  if (epoch == 0) return String("?");
  time_t t = epoch;
  struct tm utc;
  gmtime_r(&t, &utc);
  char txt[8];
  snprintf(txt, sizeof(txt), "%02d%02dZ", utc.tm_hour, utc.tm_min);
  return String(txt);
}

/**
 * Publica el spot (lo llama procesarWifi() con el WiFi conectado; bloquea 1-3 s con el aviso en pantalla).
 * El resultado queda en resultadoSpot para el pie de la ficha. Uno bueno abre o renueva la activacion; con QRT la
 * cierra. El certificado no se valida, como en las descargas: el spot es publico y no lleva ninguna clave
 */
void enviarSpotPota() {
  bool qrt = spotEnviarQrt || spotComentario == SPOT_COM_QRT;
  JsonDocument doc;
  doc["activator"] = spotId;
  // El que lo publica: el ID de la estacion (con el de un radioclub en la ficha, se sabe quien lo puso)
  doc["spotter"] = strlen(indicativoEstacion) >= 3 ? indicativoEstacion : spotId;
  doc["frequency"] = String(spotFrecKhz);
  doc["reference"] = spotParque;
  doc["mode"] = "CW";
  doc["source"] = SPOT_FUENTE;
  if (spotEnviarQrt) doc["comments"] = "QRT";
  else doc["comments"] = spotComentario == SPOT_COM_TEXTO ? spotTexto : TEXTOS_COMENTARIO_SPOT[spotComentario];
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
    uint32_t ahora = epochAhora();
    String hora = ahora ? " " + horaZ(ahora) : String("");
    if (qrt) {
      if (actActiva) cerrarActivacion();
      focoSpot = FOCO_SPOT_VOLVER;  // La ficha vuelve a la de sin activacion
      resultadoSpot = "QRT ENVIADO" + hora;
    } else {
      if (!actActiva) {
        actActiva = true;
        actInicio = ahora;
        actInicioMs = millis() | 1;
        actSpots = 0;
        focoSpot = FOCO_ACT_SPOT;  // La ficha pasa a la de la activacion
        Serial.printf("[SPOT] ACTIVACION ABIERTA: %s %s\n", spotId, spotParque);
      }
      actSpots = min(actSpots + 1, 255);
      actUltimo = ahora;
      actUltimoMs = millis() | 1;
      avisadoUltimoSpot = false;
      guardarActivacionNVS();
      resultadoSpot = "SPOT ENVIADO" + hora;
    }
  } else if (codigo > 0) {
    resultadoSpot = "ERROR: HTTP " + String(codigo);
  } else {
    resultadoSpot = "SIN RESPUESTA DE POTA";
  }
  spotEnviarQrt = false;
}

String textoComentarioSpot() {
  if (spotComentario == SPOT_COM_NO) return String("NO");
  if (spotComentario == SPOT_COM_TEXTO) return String("TEXTO");
  return String(TEXTOS_COMENTARIO_SPOT[spotComentario]);
}

// Lo que cabe a la derecha de "TEXTO": 14 caracteres (con ".." si sigue)
String textoPropioSpot() {
  String t = spotTexto[0] ? String(spotTexto) : String("-");
  return t.length() > 14 ? t.substring(0, 12) + ".." : t;
}

String valorOpcionSpot(uint8_t i) {
  switch (i) {
    case 0: return spotParque[0] ? String(spotParque) : String("-");
    case 1: return String(spotId);
    case 2: return String(BANDAS_SPOT[spotBanda].nombre);
    case 3: return String(spotFrecKhz);
    case 4: return textoComentarioSpot();
    case 5: return textoPropioSpot();
    default: return String("");
  }
}

String valorOpcionAct(uint8_t i) {
  switch (i) {
    case 0: return String(BANDAS_SPOT[spotBanda].nombre);
    case 1: return String(spotFrecKhz);
    case 2: return textoComentarioSpot();
    case 3: return textoPropioSpot();
    case 4: return String(actSpots);
    case 5: return horaZ(actInicio);
    case 6: return String(spotId);
    case 7: return String("SIN QRT");
    default: return String("");
  }
}

// "1:05" (horas y minutos); "-:--" si no se sabe
String textoDuracion(long s) {
  if (s < 0) return String("-:--");
  char txt[8];
  snprintf(txt, sizeof(txt), "%ld:%02ld", min(s / 3600, 9L), (s / 60) % 60);
  return String(txt);
}

/**
 * Ficha. Sin activacion: la lista y, en el pie, enviar/volver. Con activacion: linea de estado en negativo
 * ("ES-2218 0:42 ULT 12M": parque, duracion y minutos desde el ultimo spot), 3 filas y spot/qrt/volver.
 * En los dos casos, lo que paso con el ultimo envio ocupa el pie hasta tocar algo
 */
void dibujarFichaSpot() {
  if (!actActiva) {
    dibujarListaOpciones(NOMBRES_OPCIONES_SPOT, N_OPCIONES_SPOT, focoSpot, valorOpcionSpot, 0xD);  // 0, 2 y 3
    if (resultadoSpot.length() > 0) dibujarPieCentrado(resultadoSpot);
    else dibujarPieDos(focoSpot == FOCO_SPOT_ENVIAR, "enviar", focoSpot == FOCO_SPOT_VOLVER, "volver");
    return;
  }
  long ult = segundosDesde(actUltimo, actUltimoMs);
  String estado = String(spotParque) + " " + textoDuracion(segundosDesde(actInicio, actInicioMs)) + " ULT " +
                  (ult < 0 ? String("?") : String(ult / 60) + "M");
  display.fillRect(0, 11, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  dibujarTituloCentrado(estado, 12);
  display.setTextColor(SSD1306_WHITE);
  dibujarListaOpciones(NOMBRES_OPCIONES_ACT, N_OPCIONES_ACT, focoSpot, valorOpcionAct, 0x3, 3, 22);  // 0 y 1
  if (resultadoSpot.length() > 0) {
    dibujarPieCentrado(resultadoSpot);
  } else {
    dibujarPieCentrado(String(focoSpot == FOCO_ACT_SPOT ? "> " : "  ") + "spot " +
                       (focoSpot == FOCO_ACT_QRT ? "> " : "  ") + "qrt " +
                       (focoSpot == FOCO_ACT_VOLVER ? "> " : "  ") + "volver");
  }
}

// Donde puede salir el aviso de renovar: no al practicar (CW, LoRa), ni editando un texto, ni con el portal, la
// OTA o una descarga en marcha. Si no puede, espera
bool avisoRespotPermitido() {
  if (actualizandoWifi) return false;
  if (pantallaActual == PANTALLA_CW || pantallaActual == PANTALLA_LORA) return false;
  if (pantallaActual == PANTALLA_TRX && subTrx == TRX_EDITOR) return false;
  if (pantallaActual == PANTALLA_CONFIG && subConf != CONF_MENU && subConf != CONF_CONFIG) return false;
  bool pantallaDatos = pantallaActual == PANTALLA_PROPAGACION || pantallaActual == PANTALLA_SPOTS ||
                       pantallaActual == PANTALLA_SOL || pantallaActual == PANTALLA_TIEMPO;
  if (pantallaDatos && (subDatos == DATOS_PORTAL || subDatos == DATOS_EDITOR_SPOT)) return false;  // ID o texto
  return true;
}

void procesarAutospot() {
  leerSpotNVS();  // La primera vez: la activacion en curso, si la habia antes de dormir o apagar
  if (!actActiva) return;
  // Con la ficha a la vista, se redibuja cada minuto (duracion y ultimo spot)
  static long minutoVisto = -1;
  long minuto = segundosDesde(actUltimo, actUltimoMs) / 60;
  if (pantallaActual == PANTALLA_SPOTS && subDatos == DATOS_AUTOSPOT && minuto != minutoVisto) {
    minutoVisto = minuto;
    pantallaPendiente = true;
  }
  if (avisoRespot || avisadoUltimoSpot) return;
  long s = segundosDesde(actUltimo, actUltimoMs);
  if (s < SPOT_AVISO_S || !avisoRespotPermitido()) return;
  avisadoUltimoSpot = true;
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
