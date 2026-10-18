// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: objetos y variables globales.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// OBJETOS Y VARIABLES GLOBALES
// ============================================================================
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);
Preferences prefs;

// Interrupcion por hardware para recepcion LoRa
volatile bool flagPaqueteRecibido = false;

void IRAM_ATTR cbInterrupcionLora() {
  flagPaqueteRecibido = true;
}

uint8_t pantallaActual = 1;         
// Pantallas principales, en el orden en que se recorren con ant. / sig.
#define PANTALLA_INICIO      1
#define PANTALLA_CW          2  // Entrenador: Koch, Echo y TX libre (keyer)
#define PANTALLA_LORA        3  // Practica Duo / QSO libre
#define PANTALLA_TRX         4  // Conexion con el transceptor: decodificar, operar (KEY), QSO y memorias
#define PANTALLA_PROPAGACION 5
#define PANTALLA_SPOTS       6
#define PANTALLA_SOL         7
#define PANTALLA_CONFIG      8
const uint8_t TOTAL_PANTALLAS = 8;
uint8_t opcionNavegacionP1234 = 1;  // 0 = "ant.", 1 = "sig."

String estadoLoraStr = "INICIANDO...";
bool modoRadioRxActivo = false;

bool editandoValor = false;  // Con el encoder: el valor con el foco se cambia girando (sale en video inverso)
bool caraActiva = false;     // Se esta viendo la cara del robot (salvapantallas; ver caraPermitida())
float ultimoPctKoch = -1;    // Ultimo resultado de leccion Koch (-1 = ninguno); la cara lo celebra
unsigned long ultimoPctKochMs = 0;

struct StatsLeccion {
  uint16_t vecesCompletada;
  uint8_t maxAcierto;
  uint8_t minAcierto;
};
StatsLeccion statsKoch[40];
StatsLeccion statsEchoLecc[40];  // Practicas del Echo por leccion (% de retos bien); NVS "eLec" (un bloque)

// Estadisticas por caracter: aciertos y fallos de cada caracter de TABLA_KOCH, por separado para el Koch
// (marcando los fallos al corregir) y para el Echo. En NVS, un bloque cada una ("carK" y "carE")
struct StatsCaracter {
  uint16_t aciertos;
  uint16_t fallos;
};
StatsCaracter statsCarKoch[TOTAL_CARACTERES_KOCH];
StatsCaracter statsCarEcho[TOTAL_CARACTERES_KOCH];
// Lecciones del Koch corregidas con el total de fallos (sin saber que letras): solo suman en TOTALES KOCH.
// En NVS "ktcar" y "ktok"
uint32_t kochTotCaracteres = 0, kochTotAciertos = 0;
#define MIN_INTENTOS_LETRA 5     // Intentos minimos para salir en "mas errores"
#define LINEAS_MAS_ERRORES 4

bool enSubpantallaCW = false;
bool enPractica3_2 = false;
bool enEstadisticas3_5 = false;
#define OPCION_LIBRE_CW 3
#define OPCION_BARRA_CW 4
uint8_t opcionPantalla3 = OPCION_BARRA_CW; // 0=KOCH (RX), 1=ECHO (TX), 2=QSO SIM, 3=TX LIBRE (centro del pie),
                                           // 4=BARRA INFERIOR (ant/sig)

// Keyer iambico (pantalla CW > TX LIBRE). La configuracion se guarda en NVS al salir
uint8_t keyerWpm = 20;
bool keyerModoB = false;            // false = modo A
bool keyerPalasInvertidas = false;  // true = punta de la clavija = raya
bool enKeyer = false;               // Subpantalla del keyer abierta
uint8_t opcionKeyer = 0;            // Pantalla: 0 = borrar, 1 = menu
bool keyerEnAjustes = false;        // Lista de ajustes (menu): 0 wpm, 1 modo, 2 palas, 3 ritmo, 4 salir, 5 volver (pie)
uint8_t opcionAjustesKeyer = 0;
bool keyerEnRitmo = false;          // Paginas de RITMO (desde el menu): 0 espacios, 1 resumen, 2 consejo
uint8_t paginaRitmo = 0;
String textoKeyer = "";             // Lo descodificado de la manipulacion
volatile uint32_t keyerUltimoUsoMs = 0;  // Ultima vez que se pulso una pala o sono un elemento (QSO)

// Modo Echo (pantalla CW > ECHO (TX)): Chispas emite letras de su leccion Koch y hay que repetirlas con las
// palas. Como el Koch: ajustes propios (wpm, espaciado y leccion independientes), practica de echoRetos retos
// y resultado. ECHO_ESPERA = ajustes (sin practica)
enum EstadoEcho { ECHO_ESPERA, ECHO_REPRODUCIENDO, ECHO_ESCUCHANDO, ECHO_RESULTADO, ECHO_FIN };
bool enEcho = false;              // Echo abierto (ajustes, practica o estadisticas)
bool echoPracticando = false;
bool enEstadisticasEcho = false;
#define OPCION_ECHO_INICIAR 8
uint8_t opcionEcho = OPCION_ECHO_INICIAR;  // Ajustes: 0 wpm, 1 sp, 2 leccion, 3 letras, 4 ver, 5 retos, 6 refuerzo,
                                           // 7 estad., 8 iniciar, 9 volver
#define N_OPCIONES_ECHO 10
uint8_t opcionPracticaEcho = 0;   // Practica: 0 repetir, 1 salir. Resultado: 0 volver, 1 rep.
uint8_t echoWpm = 20;
uint16_t echoEspaciado = 200;
uint8_t echoLeccion = 4;
uint8_t echoLargo = 1;            // Letras por reto (1-5)
bool echoVerObjetivo = true;      // Ensenar la letra antes de manipularla (para aprender)
uint8_t echoRetos = 20;           // Retos por practica (10-50); cada intento cuenta, tambien al repetir tras un fallo
uint8_t echoRefuerzo = REFUERZO_SUAVE;  // Mas retos con las letras de mas errores (NVS "eref")
EstadoEcho estadoEcho = ECHO_ESPERA;
String echoObjetivo = "";
String echoRespuesta = "";
bool echoAcierto = false;
uint16_t echoAciertos = 0, echoIntentos = 0;
unsigned long echoFinResultadoMs = 0;
uint32_t echoTotalIntentos = 0, echoTotalAciertos = 0;  // Historico (ESTAD. del Echo)
uint16_t echoSesiones = 0;                               // Practicas terminadas
uint8_t paginaStatsEcho = 0;      // 0 mas errores, 1 totales, 2-15 lecciones, 16 borrado (como el Koch)
#define TOTAL_PAGINAS_STATS_ECHO 17

// Simulador de QSO POTA (pantalla CW > qso sim): Chispas hace de activador y tu de cazador con las palas, o al reves.
// SIM_AJUSTES = lista de ajustes (sin QSO en marcha)
enum EstadoSim { SIM_AJUSTES, SIM_EMITIENDO, SIM_ESCUCHANDO, SIM_FIN };
enum FaseSim { FASE_LLAMADA, FASE_INFORME, FASE_DESPEDIDA };
bool enSim = false;               // Simulador abierto (ajustes, QSO o resultado)
EstadoSim estadoSim = SIM_AJUSTES;
FaseSim faseSim = FASE_LLAMADA;
#define OPCION_SIM_INICIAR 9
#define N_OPCIONES_SIM 11
uint8_t opcionSim = OPCION_SIM_INICIAR;  // Ajustes: 0 rol, 1 wpm de Chispas, 2 espaciado, 3 wpm palas, 4 ver,
                                         // 5 qsb, 6 ruido, 7 tormenta, 8 qrm, 9 iniciar, 10 volver
// Tu rol en el QSO: cazador (Chispas es el activador) o activador (Chispas es el cazador). NVS "srol"
#define SIM_ROL_CAZADOR   0
#define SIM_ROL_ACTIVADOR 1
uint8_t simRol = SIM_ROL_CAZADOR;
// Nivel de QSB, ruido, tormenta y QRM del simulador
#define QSB_NO     0
#define QSB_SUAVE  1  // El elegido de oido en pruebas/prueba_audio.cpp
#define QSB_FUERTE 2  // Mas o menos el doble
uint8_t simQsb = QSB_NO;          // Desvanecimiento del activador (NVS "sqsb")
uint8_t simRuido = QSB_NO;        // Ruido de banda (NVS "srui")
uint8_t simTormenta = QSB_NO;     // Chasquidos de estatica (NVS "stor")
uint8_t simQrm = QSB_NO;          // Pitido de interferencia (NVS "sqrm")
uint8_t opcionPracticaSim = 0;    // QSO: 0 repetir, 1 salir. Resultado: 0 volver, 1 otro
uint8_t simWpm = 18;              // Velocidad de Chispas (activador o cazador) (NVS "swpm"); la de las palas es keyerWpm
uint16_t simEspaciado = 150;      // NVS "ssp"
bool simVerTexto = true;          // Ver lo que emite el activador mientras suena (NVS "sver")

uint8_t wpmCW = 25;
uint16_t espaciadoCW = 200;
uint8_t leccionCW = 4;
uint8_t caracteresCW = 5;
uint8_t gruposCW = 20;
uint8_t refuerzoKoch = REFUERZO_SUAVE;  // Mas veces las letras de mas errores (NVS "kref")
// Texto del Koch (NVS "ktexto"): grupos de la leccion, indicativos al azar o frases de QSO POTA
#define TEXTO_GRUPOS      0
#define TEXTO_INDICATIVOS 1
#define TEXTO_QSO         2
uint8_t textoKoch = TEXTO_GRUPOS;
uint8_t textoDuo = TEXTO_GRUPOS;  // Lo mismo para la PRACTICA DUO de LoRa, aparte (NVS "dtexto")
// Topes de una practica: un indicativo ocupa hasta 8 caracteres ("EA4ABC/P") y una frase hasta 30
// ("CQ POTA DE EA4ABC/P EA4ABC/P K"); con el espacio, 30 x 9 y 9 x 31 caben en MAX_TEXTO_LECCION (300)
#define MAX_INDICATIVOS_PRACTICA 30
#define MAX_FRASES_PRACTICA      9
uint8_t gruposPractica = 20;    // Grupos (o indicativos o frases) de la practica en curso: con texto largo, menos
uint8_t opcionSubpantalla = 8;  // Ajustes del Koch: 8 = iniciar

enum EstadoCW { 
  CW_IDLE, 
  CW_GENERAR_GRUPO, 
  CW_EMITIENDO_GRUPO,  // El grupo esta en el motor CW
  CW_PAUSA_ENTRE_GRUPOS, 
  CW_PAUSADO, 
  CW_PANTALLA_3_3_LISTADO,      
  CW_PANTALLA_CORREGIR_ERRORES,  // Correccion "x letra": se marca cada fallo
  CW_PANTALLA_FALLOS_TOTALES,    // Correccion "total": solo el numero de fallos
  CW_PANTALLA_RESUMEN_FINAL
};
EstadoCW estadoCW = CW_IDLE;

String grupoActualStr = "";
String textoCompletoLeccion = ""; 
uint8_t grupoActualNum = 0;
bool mostrarTextoGrupo = false;

uint16_t totalCaracteresLeccion = 0;
uint16_t numeroErrores = 0;
uint8_t paginaListado3_3 = 0;
uint8_t opcionFinListado3_3 = 0;  // Ultima pagina del listado: 0 = x letra, 1 = total, 2 = rep.
// Correccion "total": numero de fallos (0..totalCaracteresLeccion). Foco: 0 = numero, 1 = rep., 2 = ok
uint16_t fallosTotales = 0;
uint8_t focoFallosTotales = 0;

// Correccion del Koch: se marcan los caracteres fallados en el listado. Foco: 0..largo-1 = un caracter,
// largo = "rep.", largo+1 = "ok" (largo = textoCompletoLeccion.length(); hasta 50 grupos de 5 con espacios)
#define MAX_TEXTO_LECCION 300
#define CARACTERES_POR_PAGINA_LISTADO 63  // 3 lineas de 21
bool marcasFallo[MAX_TEXTO_LECCION];
uint16_t focoCorregir = 0;
uint8_t paginaCorregir = 0;
uint8_t opcionPractica = 0;  // Practica en marcha: 0 = pausa/seguir, 1 = salir
uint8_t opcionResumen = 0;   // Resultado: 0 = volver, 1 = repetir

uint8_t paginaStats3_5 = 0;
// Paginas de ESTAD. del Koch: 0 = mas errores; 1 = totales; 2-15 = lecciones (40 / 3 por pagina); 16 = borrado
#define PAGINAS_STATS_3_5 14
#define PAG_STATS_PRIMERA_LECCION 2
#define PAG_STATS_BORRADO (PAG_STATS_PRIMERA_LECCION + PAGINAS_STATS_3_5)
#define TOTAL_PAGINAS_STATS (PAG_STATS_BORRADO + 1)
bool confirmandoBorrado = false;

unsigned long tiempoCWProximo = 0;

// Pantalla LoRa: menu (PRACTICA DUO y QSO LIBRE) y sus subpantallas
bool enPantalla4_Sub = false;  
uint8_t opcionPantalla4 = 2;   // 0=PRACTICA DUO, 1=QSO LIBRE, 2=BARRA INFERIOR (ant/sig)
uint8_t subpantalla4Activa = 0;

uint8_t opcionDuo4_1 = 0;       // Duo: 0 enviar, 1 ver (solo con un grupo oculto), 2 menu
// Ajustes ("menu") de Duo (0 wpm, 1 espaciado, 2 leccion, 3 texto, 4 salir, 5 volver) y de QSO (0 wpm, 1 ver, 2 salir,
// 3 volver). Las palas siguen funcionando en los del QSO
bool loraEnAjustes = false;
uint8_t opcionAjustesLora = 0;
String grupoDuoTransmitido = "";
String grupoDuoRecibido = "";
bool grupoDuoRevelado = false;

bool hayGrupoDuoPorRevelar() {
  return grupoDuoRecibido.length() > 0 && !grupoDuoRevelado;
}


bool emitiendoMorseDuo = false;  // Hay un grupo de Duo en el motor CW
unsigned long finEmisionDuo = 0;  // Instante en que termino la ultima emision (para el espacio entre grupos)

// Cola circular de grupos recibidos por LoRa pendientes de reproducir
#define COLA_RX_TAM 8
char colaRxDuo[COLA_RX_TAM][TRAMA_MAX_CARACTERES + 1];
uint8_t colaRxWpm[COLA_RX_TAM];
uint8_t wpmEmisionDuo = 20;  // Velocidad de lo que esta sonando en Duo (local o del emisor)
#define WPM_MIN_DUO 5
#define WPM_MAX_DUO 60
uint8_t colaRxIni = 0;
uint8_t colaRxNum = 0;

// QSO por LoRa (subpantalla 2): lo manipulado con las palas se envia palabra a palabra (trama de texto) y lo
// recibido suena cuando hay un silencio de palabra sin manipular; si se manipula mientras suena, se corta y el
// resto queda solo como texto. Usa la cola de recepcion del Duo
#define OPCIONES_QSO 2
uint8_t opcionQSO = 1;            // 0 borrar, 1 menu (wpm, el de TX LIBRE; ver; salir)
bool qsoVerRx = true;             // false: lo recibido sale como "?" mientras suena (NVS "qver")
bool qsoSonando = false;          // Una palabra recibida esta en el motor CW
String qsoPalabraSonando = "";
String qsoTextoRx = "";           // Recibido (lo que ya se muestra)
String qsoTextoTx = "";           // Manipulado
String qsoPalabraTx = "";         // Palabra en curso, aun sin enviar
char qsoIndicativoRx[TRAMA_LONG_INDICATIVO + 1] = "";
#define QSO_TEXTO_MAX 200

// Pantalla TRX (conexion con el transceptor). Menu: DECODIFICAR, OPERAR, QSO y, en el pie, memorias
#define TRX_OPCION_MEMORIAS 3
#define TRX_OPCION_BARRA 4
uint8_t opcionTrx = TRX_OPCION_BARRA;  // 0 decodificar, 1 operar, 2 qso, 3 memorias (pie), 4 barra ant./sig.
enum SubTrx { TRX_MENU, TRX_DECOD, TRX_OPERAR, TRX_QSO, TRX_MEMORIAS, TRX_LISTA_ENVIO, TRX_EDITOR, TRX_AJUSTES };
SubTrx subTrx = TRX_MENU;
SubTrx subTrxOrigen = TRX_MENU;  // A donde vuelven la lista de envio y el editor
SubTrx trxAjustesOrigen = TRX_OPERAR;  // A donde vuelve la lista de ajustes ("menu" de OPERAR o QSO)
uint8_t opcionTrxSub = 0;        // Foco dentro de la subpantalla (OPERAR y QSO: 0 mem, 1 tune, 2 menu)
uint8_t opcionAjustesTrx = 0;    // Ajustes: 0 wpm, 1 tono local, 2 tune (s), 3 SU, 4 salir, 5 volver (pie)
bool decodEnAjustes = false;     // DECODIFICAR > menu: 0 tono, 1 salir, 2 volver (pie)
uint8_t opcionAjustesDecod = 0;
uint8_t trxWpm = 20;             // Keyer y memorias por KEY (NVS "twpm")
uint16_t trxTonoDecodHz = 0;     // Tono del decodificador: 0 = automatico, 400-1000 (NVS "tdec")
String trxTextoRx = "";          // Descodificado de la emisora
String trxTextoTx = "";          // Manipulado y memorias enviadas
char trxSu[TRAMA_LONG_INDICATIVO + 1] = "";  // Indicativo del corresponsal ({SU})
bool trxEnviandoMemoria = false;
// Tune: portadora continua por KEY para ajustar el acoplador o la potencia (OPERAR y QSO). La duracion se
// elige en su menu (NVS "ttune"); mientras dura, el corte de seguridad deja ese tiempo mas un margen
#define TRX_TUNE_MIN_S 1
#define TRX_TUNE_MAX_S 10
#define TRX_TUNE_MARGEN_MS 2000
uint8_t trxTuneSeg = 5;
bool trxSintonizando = false;
bool keyCortadaPorSeguridad = false;
#define KEY_MAX_CONTINUA_MS 10000
// Memorias: 6 de hasta 40 caracteres, con {MI} (indicativo de la estacion) y {SU} (corresponsal).
// En NVS "estacion" (m0..m5); sin guardar, las precargadas
#define TRX_NUM_MEMORIAS 6
#define TRX_MEMORIA_MAX 40
char trxMemorias[TRX_NUM_MEMORIAS][TRX_MEMORIA_MAX + 1];
const char* const TRX_MEMORIAS_PRECARGADAS[TRX_NUM_MEMORIAS] = {
  "CQ CQ CQ DE {MI} {MI} K",
  "{SU} DE {MI} {MI} K",
  "R TNX UR RST 599 599",
  "HW? {SU} DE {MI} K",
  "TNX FER QSO 73 GL",
  "{SU} DE {MI} 73 SK",
};
uint8_t trxMemoriaSel = 0;  // Foco en las listas de memorias (TRX_NUM_MEMORIAS = volver)
// Editor de textos (memorias y {SU}): rueda de caracteres con el encoder, o las palas
int8_t editorDestino = 0;   // 0..5 = memoria; -1 = indicativo del corresponsal; -2 = ID y -3 = locator (CONF);
                            // -4 = ID del activador y -5 = comentario (AUTOSPOT)
#define EDITOR_ID_ESTACION -2
#define EDITOR_LOCATOR     -3
#define EDITOR_SPOT        -4  // ID del activador en POTA > AUTOSPOT
#define EDITOR_SPOT_TEXTO  -5  // Comentario propio en POTA > AUTOSPOT
bool editorError = false;   // OK con un ID o locator no valido: se avisa y se sigue editando
String editorTexto = "";
uint8_t editorMax = TRX_MEMORIA_MAX;
uint8_t editorSel = 0;      // Posicion en la rueda

int rssiUltimo = 0;

// Depuracion (DEPU_CW): cuanto llega tarde cada transicion del motor CW respecto a lo programado; se
// informa por serie al final de cada grupo. Solo lo usa la tarea del motor CW
uint32_t medidaCWNum = 0;
uint32_t medidaCWSuma = 0;
uint32_t medidaCWMax = 0;

void registrarRetrasoCW(uint32_t retraso) {
  medidaCWNum++;
  medidaCWSuma += retraso;
  if (retraso > medidaCWMax) medidaCWMax = retraso;
}

void informarRetrasoCW(const char* origen) {
  if (medidaCWNum == 0) return;
  Serial.printf("[CW %s] %u transiciones, retraso medio %.1f ms, max %u ms\n",
                origen, medidaCWNum, (float)medidaCWSuma / medidaCWNum, medidaCWMax);
  medidaCWNum = 0;
  medidaCWSuma = 0;
  medidaCWMax = 0;
}

