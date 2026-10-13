// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: carga y guardado en NVS.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// MEMORIA PERSISTENTE NVS
// ============================================================================
extern uint8_t apagadoAutoMin;  // Minutos del apagado automatico; definida con las opciones de CONF
extern uint8_t aceleracionGiro;  // Aceleracion del encoder; definida con las opciones de CONF
extern uint8_t caraEsperaIdx;   // Tiempo del salvapantallas (indice); definida con las opciones de CONF
extern const uint8_t N_OPCIONES_CARA;

void cargarDatosNVS() {
  prefs.begin(NVS_ENTRENADOR, false);  // Lectura y escritura: en el primer arranque crea el espacio (sin errores)
  wpmCW = prefs.getUChar("wpm", 25);
  espaciadoCW = prefs.getUShort("sp", 200);
  leccionCW = prefs.getUChar("leccion", 4);
  caracteresCW = prefs.getUChar("caract", 5);
  gruposCW = prefs.getUChar("grupos", 20);
  keyerWpm = constrain(prefs.getUChar("kwpm", 20), 10, 40);  // KEYER_WPM_MIN/MAX
  keyerModoB = prefs.getBool("kmodob", false);
  keyerPalasInvertidas = prefs.getBool("kinv", false);
  qsoVerRx = prefs.getBool("qver", true);
  trxWpm = constrain(prefs.getUChar("twpm", 20), KEYER_WPM_MIN, KEYER_WPM_MAX);
  trxTonoLocal = prefs.getBool("ttono", true);
  trxTuneSeg = constrain(prefs.getUChar("ttune", 5), TRX_TUNE_MIN_S, TRX_TUNE_MAX_S);
  trxTonoDecodHz = prefs.getUShort("tdec", 0);
  if (trxTonoDecodHz != 0) trxTonoDecodHz = constrain(trxTonoDecodHz, TONO_MIN_HZ, TONO_MAX_HZ);
  echoWpm = constrain(prefs.getUChar("ewpm", 20), KEYER_WPM_MIN, KEYER_WPM_MAX);
  echoEspaciado = constrain(prefs.getUShort("esp", 200), 100, 200);
  echoLeccion = constrain(prefs.getUChar("elecc", 4), 1, 40);
  echoLargo = constrain(prefs.getUChar("elargo", 1), 1, 5);
  echoVerObjetivo = prefs.getBool("ever", true);
  echoRetos = constrain(prefs.getUChar("eretos", 20), 10, 50);
  echoRefuerzo = min(prefs.getUChar("eref", REFUERZO_SUAVE), (uint8_t)REFUERZO_FUERTE);
  refuerzoKoch = min(prefs.getUChar("kref", REFUERZO_SUAVE), (uint8_t)REFUERZO_FUERTE);
  echoTotalIntentos = prefs.getUInt("eint", 0);
  echoTotalAciertos = prefs.getUInt("eok", 0);
  echoSesiones = prefs.getUShort("eses", 0);
  apagadoAutoMin = prefs.getUChar("apag", 30);  // ajustarApagado(0) lo lleva a un valor de la lista si no lo es
  caraEsperaIdx = min(prefs.getUChar("cara", 1), (uint8_t)(N_OPCIONES_CARA - 1));
  aceleracionGiro = prefs.getUChar("acel", 5);   // ACELERACION_MAX
  if (aceleracionGiro == 1 || aceleracionGiro > 5) aceleracionGiro = 5;
  frecTonoHz = constrain(prefs.getUShort("tono", FREC_AUDIO_HZ), TONO_MIN_HZ, TONO_MAX_HZ);
  depuracion = prefs.getUChar("depu", 0) & (DEPU_SERIE | DEPU_DIAG | DEPU_DECOD | DEPU_CW);

  for (int i = 0; i < 40; i++) {
    String kComp = "c_" + String(i);
    String kMax = "mx_" + String(i);
    String kMin = "mn_" + String(i);

    statsKoch[i].vecesCompletada = prefs.getUShort(kComp.c_str(), 0);
    statsKoch[i].maxAcierto = prefs.getUChar(kMax.c_str(), 0);
    statsKoch[i].minAcierto = prefs.getUChar(kMin.c_str(), 100);
  }
  memset(statsCarKoch, 0, sizeof(statsCarKoch));
  memset(statsCarEcho, 0, sizeof(statsCarEcho));
  if (prefs.isKey("carK") && prefs.getBytesLength("carK") == sizeof(statsCarKoch)) {
    prefs.getBytes("carK", statsCarKoch, sizeof(statsCarKoch));
  }
  if (prefs.isKey("carE") && prefs.getBytesLength("carE") == sizeof(statsCarEcho)) {
    prefs.getBytes("carE", statsCarEcho, sizeof(statsCarEcho));
  }
  kochTotCaracteres = prefs.getUInt("ktcar", 0);
  kochTotAciertos = prefs.getUInt("ktok", 0);
  for (int i = 0; i < 40; i++) statsEchoLecc[i] = { 0, 0, 100 };
  if (prefs.isKey("eLec") && prefs.getBytesLength("eLec") == sizeof(statsEchoLecc)) {
    prefs.getBytes("eLec", statsEchoLecc, sizeof(statsEchoLecc));
  }
  prefs.end();
}

void guardarUltimaConfiguracionNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("kref", refuerzoKoch);
  prefs.putUChar("wpm", wpmCW);
  prefs.putUShort("sp", espaciadoCW);
  prefs.putUChar("leccion", leccionCW);
  prefs.putUChar("caract", caracteresCW);
  prefs.putUChar("grupos", gruposCW);
  prefs.end();
}

void guardarApagadoNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("apag", apagadoAutoMin);
  prefs.end();
}

void guardarSalvapantallasNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("cara", caraEsperaIdx);
  prefs.end();
}

void guardarAceleracionNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("acel", aceleracionGiro);
  prefs.end();
}

void guardarTonoNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUShort("tono", frecTonoHz);
  prefs.end();
}

void guardarDepuracionNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("depu", depuracion);
  prefs.end();
}

void guardarKeyerNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("kwpm", keyerWpm);
  prefs.putBool("kmodob", keyerModoB);
  prefs.putBool("kinv", keyerPalasInvertidas);
  prefs.putBool("qver", qsoVerRx);
  prefs.end();
}

void guardarTrxNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("twpm", trxWpm);
  prefs.putBool("ttono", trxTonoLocal);
  prefs.putUChar("ttune", trxTuneSeg);
  prefs.putUShort("tdec", trxTonoDecodHz);
  prefs.end();
}

void guardarEchoNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUChar("ewpm", echoWpm);
  prefs.putUShort("esp", echoEspaciado);
  prefs.putUChar("elecc", echoLeccion);
  prefs.putUChar("elargo", echoLargo);
  prefs.putBool("ever", echoVerObjetivo);
  prefs.putUChar("eretos", echoRetos);
  prefs.putUChar("eref", echoRefuerzo);
  prefs.end();
}

void registrarResultadoLeccionNVS(uint8_t lecc, float pct) {
  if (lecc < 1 || lecc > 40) return;
  uint8_t idx = lecc - 1;
  anotarResultadoLeccion(statsKoch[idx], pct);

  prefs.begin(NVS_ENTRENADOR, false);
  String kComp = "c_" + String(idx);
  String kMax = "mx_" + String(idx);
  String kMin = "mn_" + String(idx);

  prefs.putUShort(kComp.c_str(), statsKoch[idx].vecesCompletada);
  prefs.putUChar(kMax.c_str(), statsKoch[idx].maxAcierto);
  prefs.putUChar(kMin.c_str(), statsKoch[idx].minAcierto);

  prefs.putUChar("leccion", leccionCW);
  prefs.putUChar("wpm", wpmCW);
  prefs.putUShort("sp", espaciadoCW);
  prefs.putUChar("caract", caracteresCW);
  prefs.putUChar("grupos", gruposCW);

  prefs.end();
}

// Borra solo las estadisticas del Koch (lecciones y por caracter); las del Echo, la configuracion del
// entrenador y los datos de la estacion se conservan
void borrarEstadisticasKochNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  for (int i = 0; i < 40; i++) {
    prefs.remove(("c_" + String(i)).c_str());
    prefs.remove(("mx_" + String(i)).c_str());
    prefs.remove(("mn_" + String(i)).c_str());
  }
  prefs.remove("carK");
  prefs.remove("ktcar");
  prefs.remove("ktok");
  prefs.end();
  memset(statsCarKoch, 0, sizeof(statsCarKoch));
  kochTotCaracteres = kochTotAciertos = 0;

  for (int i = 0; i < 40; i++) {
    statsKoch[i].vecesCompletada = 0;
    statsKoch[i].maxAcierto = 0;
    statsKoch[i].minAcierto = 100;
  }
}

// Borra solo las estadisticas del Echo (por caracter y totales)
void borrarEstadisticasEchoNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.remove("carE");
  prefs.remove("eint");
  prefs.remove("eok");
  prefs.remove("eses");
  prefs.remove("eLec");
  prefs.end();
  memset(statsCarEcho, 0, sizeof(statsCarEcho));
  for (int i = 0; i < 40; i++) statsEchoLecc[i] = { 0, 0, 100 };
  echoTotalIntentos = echoTotalAciertos = 0;
  echoSesiones = 0;
}

void cargarEstacionNVS() {
  prefs.begin(NVS_ESTACION, false);  // Como en cargarDatosNVS(): crea el espacio en el primer arranque
  // Con isKey: getString() de una clave que aun no existe imprime un error por serie
  strlcpy(indicativoEstacion, prefs.isKey("call") ? prefs.getString("call").c_str() : INDICATIVO_POR_DEFECTO,
          sizeof(indicativoEstacion));
  strlcpy(locatorEstacion, prefs.isKey("loc") ? prefs.getString("loc").c_str() : LOCATOR_POR_DEFECTO,
          sizeof(locatorEstacion));
  horaUtcCabecera = prefs.getBool("utc", false);
  qnhHpa = prefs.getFloat("qnh", 0);  // Sin calibrar: atmosfera estandar
  qnhCalibrado = qnhHpa > 850 && qnhHpa < 1100;
  if (!qnhCalibrado) qnhHpa = QNH_ESTANDAR_HPA;
  // Memorias de la pantalla TRX: sin clave guardada, la precargada (una memoria borrada se guarda vacia)
  for (uint8_t i = 0; i < TRX_NUM_MEMORIAS; i++) {
    String clave = "m" + String(i);
    String m = prefs.isKey(clave.c_str()) ? prefs.getString(clave.c_str(), "") : String(TRX_MEMORIAS_PRECARGADAS[i]);
    strlcpy(trxMemorias[i], m.c_str(), sizeof(trxMemorias[i]));
  }
  prefs.end();
}

void guardarQnhNVS() {
  prefs.begin(NVS_ESTACION, false);
  prefs.putFloat("qnh", qnhHpa);
  prefs.end();
}

void guardarMemoriaNVS(uint8_t i) {
  prefs.begin(NVS_ESTACION, false);
  prefs.putString(("m" + String(i)).c_str(), trxMemorias[i]);
  prefs.end();
}

void guardarEstacionNVS() {
  prefs.begin(NVS_ESTACION, false);
  prefs.putString("call", indicativoEstacion);
  prefs.putString("loc", locatorEstacion);
  prefs.putBool("utc", horaUtcCabecera);
  prefs.end();
}

