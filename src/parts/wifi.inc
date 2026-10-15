// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: WiFi, NTP, portal y QR.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// WIFI: HORA NTP, PROPAGACION (hamqsl.com) Y SPOTS POTA
// ============================================================================
// CONF > CONFIG > WIFI DATOS: en MANUAL (por defecto, desde el 15 oct 2026) el WiFi de PROP, POTA y SOL solo se
// enciende con ACTUALIZAR, descarga (o sincroniza la hora) y se apaga; en AUTO, como antes: se enciende al entrar en
// PROP y POTA (y en SOL sin hora), descarga cuando toca y se apaga al salir. Ademas, el portal (webconf) y
// ACTUALIZAR FW. Las descargas bloquean unos segundos: se avisa en pantalla. Si no hay red guardada o no conecta, PROP, POTA y SOL lo dicen y ofrecen abrir el portal
// (WiFiManager), donde se elige la red desde el movil; queda guardada en la NVS del WiFi.
// SOTA no se incluye: sus condiciones de uso exigen aprobacion previa para software generado por IA.
#define WIFI_TIMEOUT_CONEXION_MS 15000
#define PORTAL_TIMEOUT_S         300
#define PORTAL_CLAVE             "chispas73"  // Clave del punto de acceso de configuracion (minimo 8)
#define ZONA_HORARIA             "CET-1CEST,M3.5.0,M10.5.0/3"  // Peninsula
#define URL_PROPAGACION          "https://www.hamqsl.com/solarxml.php"
#define URL_POTA                 "https://api.pota.app/spot/activator"
// hamqsl.com (N0NBH) pide expresamente no consultar el XML mas de una vez por hora ("please only select to update
// every hour", hamqsl.com/solar.html): es lo que tarda en cambiar el flujo solar; el resto, cada 3 horas
#define REFRESCO_PROPAGACION_MS  (60UL * 60 * 1000)
#define REFRESCO_POTA_MS         (2UL * 60 * 1000)   // POTA no publica limites: se consulta con moderacion
#define MAX_SPOTS_POTA           40
#define SPOTS_POR_PAGINA         4

enum EstadoWifi { WIFI_APAGADO, WIFI_CONECTANDO, WIFI_PORTAL, WIFI_CONECTADO, WIFI_SIN_CONEXION };
EstadoWifi estadoWifi = WIFI_APAGADO;
unsigned long inicioConexionWifi = 0;
WiFiManager wm;
char nombrePortal[16] = "";
bool actualizandoWifi = false;     // Se esta descargando (bloquea): la pantalla muestra avisoDescarga
const char* avisoDescarga = "";
bool forzarActualizacion = false;  // Pedido con la opcion "actualizar"
bool wifiAuto = false;             // CONFIG > WIFI DATOS (NVS "wifiauto"): false = MANUAL
bool wifiPorPeticion = false;      // ACTUALIZAR en curso (conectar, descargar, hora): al acabar, en MANUAL se apaga
unsigned long esperaNtpDesdeMs = 0;
#define WIFI_ESPERA_NTP_MS 8000    // Tras conectar, lo que se espera a la hora del NTP antes de apagar (MANUAL)

struct DatosPropagacion {
  bool validos;
  unsigned long intentoMs;  // 0 = nunca
  int sfi, aIndex, kIndex, manchas;
  char rayosX[8];
  char actualizado[24];
  char bandas[4][2][6];  // [80-40, 30-20, 17-15, 12-10][dia, noche]: "Good", "Fair", "Poor"
};
const char* const NOMBRES_BANDAS_XML[4] = { "80m-40m", "30m-20m", "17m-15m", "12m-10m" };
const char* const NOMBRES_BANDAS_PANTALLA[4] = { "80-40m", "30-20m", "17-15m", "12-10m" };
DatosPropagacion propagacion = {};

struct SpotPota {
  char indicativo[12];
  char frecuencia[10];
  char referencia[12];
  char tiempo[20];  // "2026-10-08T12:03:57" (UTC); sirve para ordenar
};
SpotPota spotsPota[MAX_SPOTS_POTA];
uint8_t numSpotsPota = 0;
bool spotsPotaValidos = false;
unsigned long spotsPotaIntentoMs = 0;  // 0 = nunca
unsigned long spotsPotaOkMs = 0;       // Ultima descarga buena (en las paginas sale su antiguedad)
uint8_t paginaSpots = 0;

// PROP, POTA y SOL: menu (VER DATOS y ACTUALIZAR; sin conexion, ABRIR WEBCONF y REINTENTAR), paginas que se
// pasan girando y vista del portal. Solo se ve una de las tres a la vez: comparten el estado
#define OPCION_BARRA_DATOS 2
#define OPCION_BARRA_PROP  OPCION_BARRA_DATOS
#define OPCION_BARRA_SPOTS 3                      // POTA: VER SPOTS, VER PARQUES y ACTUALIZAR
uint8_t opcionPantallaProp = OPCION_BARRA_PROP;    // Menu: 0 = ver datos, 1 = actualizar, 2 = barra ant./sig.
uint8_t opcionPantallaSpots = OPCION_BARRA_SPOTS;
uint8_t opcionPantallaSol = OPCION_BARRA_DATOS;
enum SubDatos { DATOS_MENU, DATOS_PAGINAS, DATOS_PORTAL, DATOS_PARQUES };  // PARQUES: solo en POTA
SubDatos subDatos = DATOS_MENU;
uint8_t focoPortalDatos = 0;
uint8_t paginaPropagacion = 0;              // 0 = indices, 1 = condiciones por banda
uint8_t paginaSol = 0;                      // 0 = hoy, 1 = ahora, 2 = manana
void dibujarVistaPortal(uint8_t foco);      // Definidas con la pantalla CONF
bool procesarVistaPortal(uint8_t& foco, int8_t giro, bool pulsa, bool atras);

// CONF: menu con CONFIG (opciones) y STATUS (informacion, en paginas)
#define OPCION_BARRA_CONFIG 2
uint8_t opcionPantallaConfig = OPCION_BARRA_CONFIG;  // Menu: 0 = CONFIG, 1 = STATUS, 2 = barra ant./sig.
enum SubConf { CONF_MENU, CONF_CONFIG, CONF_STATUS, CONF_PORTAL, CONF_EDITOR, CONF_OTA, CONF_DEPURACION, CONF_RELOJ };
SubConf subConf = CONF_MENU;
// Lista de CONFIG (4 a la vista; "volver" en el pie)
// Por bloques: estacion (ID, LOCATOR, ALTITUD, HORA, RELOJ), sonido y manejo (TONO, ACELERACION), pantalla y
// energia (SALVAPANTALLAS, APAGADO, WIFI DATOS) y sistema (WEBCONF, ACTUALIZAR FW, DEPURACION, FABRICA)
enum OpcionConf { OC_ID, OC_LOCATOR, OC_ALTITUD, OC_HORA, OC_RELOJ, OC_TONO, OC_ACELERACION, OC_SALVAPANTALLAS,
                  OC_APAGADO, OC_WIFI_DATOS, OC_WEBCONF, OC_ACTUALIZAR, OC_DEPURACION, OC_FABRICA, OC_VOLVER };
#define N_OPCIONES_CONF 15
uint8_t focoConf = 0;
// ALTITUD (con el BME280): al pulsar se edita partiendo de la altitud actual; al pulsar otra vez se calcula el QNH
#define ALTITUD_MIN_M -400
#define ALTITUD_MAX_M 8848
int16_t altitudEditada = 0;
// LOCATOR con GPS: al pulsar se elige AUTO (GPS) o MANUAL girando; al pulsar otra vez, MANUAL abre el editor
bool locatorEditAuto = true;
// RELOJ: poner la hora a mano (UTC o local, como la cabecera), campo a campo: hora, minutos, dia, mes y ano
#define RELOJ_N_CAMPOS 5
const int16_t RELOJ_MIN[RELOJ_N_CAMPOS] = { 0, 0, 1, 1, 2024 };
const int16_t RELOJ_MAX[RELOJ_N_CAMPOS] = { 23, 59, 31, 12, 2099 };
int16_t relojCampos[RELOJ_N_CAMPOS];
uint8_t relojCampo = 0;
// Lista de DEPURACION: una opcion SI/NO por bit DEPU_* y "volver" en el pie (foco N_OPCIONES_DEPU)
#define N_OPCIONES_DEPU 4
const uint8_t BITS_DEPU[N_OPCIONES_DEPU] = { DEPU_SERIE, DEPU_DIAG, DEPU_DECOD, DEPU_CW };
const char* const NOMBRES_OPCIONES_DEPU[N_OPCIONES_DEPU] = { "ORDENES SERIE", "DIAG (1 MIN)", "DECOD (2 S)",
                                                             "RETRASO CW" };
uint8_t focoDepu = 0;
uint8_t focoPortalConf = 0;  // Vista del portal: 0 = ver qr, 1 = volver
uint8_t paginaStatus = 0;
#define N_PAGINAS_STATUS 7
#define PAGINA_STATUS_GPS 6

// Apagado automatico: minutos sin tocar nada antes de dormir (0 = nunca). Se elige en CONF y se guarda en NVS
const uint8_t OPCIONES_APAGADO_MIN[] = { 0, 5, 10, 15, 30, 60, 120 };
const uint8_t N_OPCIONES_APAGADO = sizeof(OPCIONES_APAGADO_MIN);
uint8_t apagadoAutoMin = 30;

// Salvapantallas (cara): segundos sin tocar nada antes de que salga (0 = nunca). Se elige en CONF (NVS "cara")
const uint16_t OPCIONES_CARA_S[] = { 0, 15, 30, 60, 120, 300 };
const uint8_t N_OPCIONES_CARA = sizeof(OPCIONES_CARA_S) / sizeof(OPCIONES_CARA_S[0]);
uint8_t caraEsperaIdx = 1;  // 15 s

String textoSalvapantallas() {
  uint16_t s = OPCIONES_CARA_S[caraEsperaIdx];
  if (s == 0) return String("NO");
  return s < 60 ? String(s) + "s" : String(s / 60) + " min";
}

// Aceleracion del encoder al girar rapido: 0 = no; 2-5 = el valor avanza x2..x5 (NVS "acel")
#define ACELERACION_MAX 5
uint8_t aceleracionGiro = ACELERACION_MAX;

String textoAceleracion() {
  return aceleracionGiro == 0 ? String("NO") : "x" + String(aceleracionGiro);
}

// 0, 2, 3, 4, 5 (el 1 seria igual que no acelerar)
void ajustarAceleracion(int8_t paso) {
  int v = aceleracionGiro + paso;
  if (aceleracionGiro == 0 && paso > 0) v = 2;
  else if (v < 2) v = 0;
  if (v > ACELERACION_MAX) v = ACELERACION_MAX;
  aceleracionGiro = v;
}

String textoApagado() {
  return apagadoAutoMin == 0 ? String("nunca") : String(apagadoAutoMin) + " min";
}

// Paso al valor siguiente o anterior de OPCIONES_APAGADO_MIN, sin dar la vuelta
void ajustarApagado(int8_t paso) {
  int i = 0;
  while (i < N_OPCIONES_APAGADO - 1 && OPCIONES_APAGADO_MIN[i] != apagadoAutoMin) i++;
  i = constrain(i + paso, 0, N_OPCIONES_APAGADO - 1);
  apagadoAutoMin = OPCIONES_APAGADO_MIN[i];
}
bool confirmandoFabrica = false;

// Pagina "Setup" del portal: datos de la estacion y hora manual (para campo sin internet)
WiFiManagerParameter paramIndicativo("call", "Indicativo", INDICATIVO_POR_DEFECTO, TRAMA_LONG_INDICATIVO);
WiFiManagerParameter paramLocator("loc", "Locator (4 o 6 caracteres)", LOCATOR_POR_DEFECTO, 6);
// Hora manual en formato espanol. Dentro del campo, como pista, la hora que tiene Chispas al abrir el portal
class ParametroHora : public WiFiManagerParameter {
 public:
  char pista[48] = "placeholder='HH:MM DD-MM-AAAA'";
  ParametroHora() : WiFiManagerParameter("hora", "Hora local, HH:MM DD-MM-AAAA (vacío = no cambiarla)", "", 16) {}
  const char* getCustomHTML() const override { return pista; }
  void actualizarPista() {
    struct tm t;
    if (getLocalTime(&t, 0) && t.tm_year > 120) {
      snprintf(pista, sizeof(pista), "placeholder='%02d:%02d %02d-%02d-%04d'",
               t.tm_hour, t.tm_min, t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    }
  }
};
ParametroHora paramHora;

// Casilla del portal. WiFiManager no tiene casillas: es un campo cuyo HTML dice type="checkbox".
// Marcada envia "1" y sin marcar no envia nada (valor vacio). "checked" depende del estado guardado
class ParametroCasilla : public WiFiManagerParameter {
 public:
  bool marcada = false;
  ParametroCasilla(const char* id, const char* etiqueta)
    : WiFiManagerParameter(id, etiqueta, "1", 2, "type=\"checkbox\"", WFM_LABEL_AFTER) {}
  const char* getCustomHTML() const override {
    return marcada ? "type=\"checkbox\" checked" : "type=\"checkbox\"";
  }
};
ParametroCasilla paramHoraUtc("utc", "Hora UTC en la cabecera (12:05Z) en vez de la local");

// Memorias de la pantalla TRX (la primera explica las variables)
WiFiManagerParameter paramMemoria1("m0", "Memoria 1 ({MI} = tu indicativo, {SU} = el del corresponsal, {WX} = temperatura)",
                                   "", TRX_MEMORIA_MAX);
WiFiManagerParameter paramMemoria2("m1", "Memoria 2", "", TRX_MEMORIA_MAX);
WiFiManagerParameter paramMemoria3("m2", "Memoria 3", "", TRX_MEMORIA_MAX);
WiFiManagerParameter paramMemoria4("m3", "Memoria 4", "", TRX_MEMORIA_MAX);
WiFiManagerParameter paramMemoria5("m4", "Memoria 5", "", TRX_MEMORIA_MAX);
WiFiManagerParameter paramMemoria6("m5", "Memoria 6", "", TRX_MEMORIA_MAX);
WiFiManagerParameter* const paramMemorias[TRX_NUM_MEMORIAS] = {
  &paramMemoria1, &paramMemoria2, &paramMemoria3, &paramMemoria4, &paramMemoria5, &paramMemoria6
};

bool locatorALatLon(const char* loc, float& lat, float& lon);  // Definida con la pantalla SOL

// Indicativo de 3 a 8 caracteres (letras, cifras y "/"). Si vale, pasa a ser el de la estacion (sin guardar)
bool aplicarIndicativo(String call) {
  call.trim();
  call.toUpperCase();
  bool valido = call.length() >= 3 && call.length() <= TRAMA_LONG_INDICATIVO;
  for (size_t i = 0; i < call.length(); i++) {
    if (!isalnum(call[i]) && call[i] != '/') valido = false;
  }
  if (valido) strlcpy(indicativoEstacion, call.c_str(), sizeof(indicativoEstacion));
  return valido;
}

// Locator de 4 o 6 caracteres: "in80DK" -> "IN80dk". Si vale, pasa a ser el de la estacion (sin guardar)
bool aplicarLocator(String loc) {
  loc.trim();
  for (size_t i = 0; i < loc.length(); i++) loc[i] = (i < 2) ? toupper(loc[i]) : tolower(loc[i]);
  float lat, lon;
  bool valido = locatorALatLon(loc.c_str(), lat, lon);
  if (valido) strlcpy(locatorEstacion, loc.c_str(), sizeof(locatorEstacion));
  return valido;
}

void guardarParametrosPortal() {
  if (!aplicarIndicativo(paramIndicativo.getValue())) {
    Serial.printf("[PORTAL] INDICATIVO NO VALIDO: %s\n", paramIndicativo.getValue());
  }
  if (!aplicarLocator(paramLocator.getValue())) {
    Serial.printf("[PORTAL] LOCATOR NO VALIDO: %s\n", paramLocator.getValue());
  }

  horaUtcCabecera = strlen(paramHoraUtc.getValue()) > 0;
  paramHoraUtc.marcada = horaUtcCabecera;
  paramHoraUtc.setValue("1", 2);  // Para que vuelva a enviar "1" si se marca otra vez

  guardarEstacionNVS();
  Serial.printf("[PORTAL] ESTACION: %s %s, hora %s\n", indicativoEstacion, locatorEstacion,
                horaUtcCabecera ? "UTC" : "local");

  // Memorias: solo lo que se puede emitir (mayusculas, Morse, espacios, {MI}, {SU} y {WX})
  for (uint8_t i = 0; i < TRX_NUM_MEMORIAS; i++) {
    String m = trxLimpiarTexto(paramMemorias[i]->getValue(), true, TRX_MEMORIA_MAX);
    if (m != trxMemorias[i]) {
      strlcpy(trxMemorias[i], m.c_str(), sizeof(trxMemorias[i]));
      guardarMemoriaNVS(i);
      Serial.printf("[PORTAL] MEMORIA %u: %s\n", i + 1, trxMemorias[i]);
    }
  }

  // "14:05 10-10-2026" (tambien vale con barras: "14:05 10/10/2026")
  String hora = paramHora.getValue();
  hora.trim();
  hora.replace('/', '-');
  if (hora.length() > 0) {
    int a, me, d, h, mi;
    if (sscanf(hora.c_str(), "%d:%d %d-%d-%d", &h, &mi, &d, &me, &a) == 5 &&
        a >= 2024 && me >= 1 && me <= 12 && d >= 1 && d <= 31 && h >= 0 && h <= 23 && mi >= 0 && mi <= 59) {
      struct tm t = {};
      t.tm_year = a - 1900; t.tm_mon = me - 1; t.tm_mday = d;
      t.tm_hour = h; t.tm_min = mi; t.tm_isdst = -1;  // Hora local: mktime aplica CET/CEST
      ponerHora(mktime(&t), HORA_MANUAL);
      Serial.printf("[PORTAL] HORA MANUAL: %s\n", hora.c_str());
    } else {
      Serial.printf("[PORTAL] HORA NO VALIDA: %s\n", hora.c_str());
    }
  }
  pantallaPendiente = true;
}

// ----------------------------------------------------------------------------
// QR DEL PORTAL: al escanearlo, el movil se une a la red de Chispas (y suele abrir el portal solo)
// ----------------------------------------------------------------------------
// Con el portal abierto, la pantalla muestra los datos en texto (red, clave e IP). En CONF, la
// opcion "ver qr" muestra a pantalla completa el QR de WiFi: al escanearlo, el movil se une a la red
// de Chispas (la pagina del portal se abre sola o no segun el movil). Cualquier giro o pulsacion
// vuelve a los datos.
#define QR_MAX_MODULOS 41  // Hasta la version 6 del QR (sobra: el de WiFi ocupa una version 3, 29x29)

enum VistaPortal { VISTA_DATOS, VISTA_QR_RED };

struct CodigoQr {
  uint8_t modulos[(QR_MAX_MODULOS * QR_MAX_MODULOS + 7) / 8];  // Un bit por modulo (1 = negro)
  uint8_t tam;                                                 // Modulos por lado (0 = no hay QR)
};
CodigoQr qrRed = {{0}, 0};
CodigoQr* qrDestino = nullptr;  // Donde guarda guardarQrGenerado() (el callback no admite contexto)

VistaPortal vistaPortal = VISTA_DATOS;

// Llamada por esp_qrcode_generate() con el QR ya codificado: se copia a *qrDestino
void guardarQrGenerado(esp_qrcode_handle_t qr) {
  int n = esp_qrcode_get_size(qr);
  if (qrDestino == nullptr || n > QR_MAX_MODULOS) return;
  memset(qrDestino->modulos, 0, sizeof(qrDestino->modulos));
  for (int y = 0; y < n; y++) {
    for (int x = 0; x < n; x++) {
      if (esp_qrcode_get_module(qr, x, y)) qrDestino->modulos[(y * n + x) / 8] |= 1 << ((y * n + x) % 8);
    }
  }
  qrDestino->tam = n;
}

void generarQr(CodigoQr& destino, const char* texto) {
  esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
  cfg.display_func = guardarQrGenerado;
  cfg.max_qrcode_version = 6;
  cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;  // Modulos mas grandes, mejor para una pantalla pequena
  destino.tam = 0;
  qrDestino = &destino;
  if (esp_qrcode_generate(&cfg, texto) != ESP_OK) Serial.printf("[PORTAL] NO SE PUDO GENERAR EL QR: %s\n", texto);
  else Serial.printf("[PORTAL] QR %ux%u: %s\n", destino.tam, destino.tam, texto);
  qrDestino = nullptr;
}

/**
 * QR estandar de WiFi ("WIFI:T:WPA;S:<red>;P:<clave>;;"), que entienden las camaras de Android e iOS.
 * La red y la clave no llevan ; , : ni comillas, asi que no hace falta escaparlas.
 */
void generarQrPortal() {
  char texto[64];
  snprintf(texto, sizeof(texto), "WIFI:T:WPA;S:%s;P:%s;;", nombrePortal, PORTAL_CLAVE);
  generarQr(qrRed, texto);
}

bool viendoQrPortal() {
  return estadoWifi == WIFI_PORTAL && vistaPortal == VISTA_QR_RED && qrRed.tam > 0;
}

// QR a la izquierda (blanco con modulos negros y margen de 1 modulo); devuelve el ancho ocupado
int dibujarQr(const CodigoQr& qr) {
  int escala = ((qr.tam + 2) * 2 <= 64) ? 2 : 1;
  int lado = (qr.tam + 2) * escala;
  int y0 = (64 - lado) / 2;
  display.fillRect(0, y0, lado, lado, SSD1306_WHITE);
  for (int y = 0; y < qr.tam; y++) {
    for (int x = 0; x < qr.tam; x++) {
      int i = y * qr.tam + x;
      if (qr.modulos[i / 8] & (1 << (i % 8))) {
        display.fillRect((x + 1) * escala, y0 + (y + 1) * escala, escala, escala, SSD1306_BLACK);
      }
    }
  }
  return lado;
}

// QR de la red a la izquierda y explicacion a la derecha
void dibujarPantallaQr() {
  display.clearDisplay();
  int xt = dibujarQr(qrRed) + 4;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(xt, 2);  display.print(F("WEBCONF"));
  display.drawLine(xt, 11, 127, 11, SSD1306_WHITE);
  display.setCursor(xt, 16); display.print(F("ESCANEA"));
  display.setCursor(xt, 26); display.print(F("PARA UNIR"));
  display.setCursor(xt, 36); display.print(F("EL MOVIL"));
  display.setCursor(xt, 55); display.print(F("> DATOS"));
  display.display();
}

void cpuSegunWifi(bool wifiEncendido);  // Definida justo debajo

void abrirPortalWifi() {
  if (nombrePortal[0] == '\0') {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(nombrePortal, sizeof(nombrePortal), "Chispas-%02X%02X", mac[4], mac[5]);
  }
  if (qrRed.tam == 0) generarQrPortal();
  vistaPortal = VISTA_DATOS;  // Por defecto, los datos; el QR es opcion de CONF
  static bool parametrosAnadidos = false;
  if (!parametrosAnadidos) {
    wm.addParameter(&paramIndicativo);
    wm.addParameter(&paramLocator);
    wm.addParameter(&paramHora);
    wm.addParameter(&paramHoraUtc);
    for (uint8_t i = 0; i < TRX_NUM_MEMORIAS; i++) wm.addParameter(paramMemorias[i]);
    const char* menu[] = { "wifi", "param", "exit" };  // "param" = pagina "Setup"
    wm.setMenu(menu, 3);
    wm.setSaveParamsCallback(guardarParametrosPortal);
    parametrosAnadidos = true;
  }
  paramIndicativo.setValue(indicativoEstacion, TRAMA_LONG_INDICATIVO);
  paramLocator.setValue(locatorEstacion, 6);
  paramHora.setValue("", 16);
  paramHora.actualizarPista();
  paramHoraUtc.setValue("1", 2);
  paramHoraUtc.marcada = horaUtcCabecera;
  for (uint8_t i = 0; i < TRX_NUM_MEMORIAS; i++) paramMemorias[i]->setValue(trxMemorias[i], TRX_MEMORIA_MAX);

  cpuSegunWifi(true);
  WiFi.mode(WIFI_STA);
  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT_S);
  wm.startConfigPortal(nombrePortal, PORTAL_CLAVE);
  estadoWifi = WIFI_PORTAL;
  Serial.printf("[WIFI] PORTAL ABIERTO: %s\n", nombrePortal);
  pantallaPendiente = true;
}

// Ahorro: la CPU va a 80 MHz con el WiFi apagado y a 240 MHz con el encendido (HTTPS y portal).
// El motor CW, el keyer (ticks del systimer), el tono, el I2C y el puerto serie (APB a 80 MHz) no
// dependen de esta frecuencia.
#define CPU_MHZ_REPOSO 80
#define CPU_MHZ_WIFI   240

void cpuSegunWifi(bool wifiEncendido) {
  uint32_t mhz = wifiEncendido ? CPU_MHZ_WIFI : CPU_MHZ_REPOSO;
  if (getCpuFrequencyMhz() != mhz) setCpuFrequencyMhz(mhz);
}

void quedarseSinConexion() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  cpuSegunWifi(false);
  estadoWifi = WIFI_SIN_CONEXION;
  wifiPorPeticion = false;
  forzarActualizacion = false;
  // PROP, POTA y SOL vuelven a su menu, con el foco en "ABRIR WEBCONF"
  subDatos = DATOS_MENU;
  opcionPantallaProp = opcionPantallaSpots = opcionPantallaSol = 0;
  pantallaPendiente = true;
}

void encenderWifi() {
  cpuSegunWifi(true);
  WiFi.mode(WIFI_STA);
  if (!wm.getWiFiIsSaved()) {
    Serial.println(F("[WIFI] NO HAY RED GUARDADA"));
    quedarseSinConexion();
    return;
  }
  WiFi.begin();  // Red guardada
  estadoWifi = WIFI_CONECTANDO;
  inicioConexionWifi = millis();
  Serial.println(F("[WIFI] CONECTANDO..."));
  pantallaPendiente = true;
}

void apagarWifi() {
  if (estadoWifi == WIFI_PORTAL) wm.stopConfigPortal();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  cpuSegunWifi(false);
  estadoWifi = WIFI_APAGADO;
  Serial.println(F("[WIFI] APAGADO"));
}

// NTP en segundo plano, salvo que la hora venga del GPS (mas exacto)
void iniciarNtp() {
  if (fuenteHoraActual() == HORA_GPS) return;
  sntp_set_time_sync_notification_cb(horaSincronizadaNtp);
  configTzTime(ZONA_HORARIA, "pool.ntp.org", "time.cloudflare.com");
}

void wifiConectado() {
  estadoWifi = WIFI_CONECTADO;
  Serial.print(F("[WIFI] CONECTADO, IP "));
  Serial.println(WiFi.localIP());
  ntpRecibido = false;
  esperaNtpDesdeMs = millis();
  iniciarNtp();
  pantallaPendiente = true;
}

bool descargarUrl(const char* url, String& cuerpo) {
  WiFiClientSecure cliente;
  cliente.setInsecure();  // Datos publicos de solo lectura: no se valida el certificado
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(cliente, url)) return false;
  http.setUserAgent("Chispas-EA4ILN/" FW_VERSION);
  int codigo = http.GET();
  if (codigo == HTTP_CODE_OK) cuerpo = http.getString();
  else Serial.printf("[WIFI] %s -> HTTP %d\n", url, codigo);
  http.end();
  return codigo == HTTP_CODE_OK;
}

