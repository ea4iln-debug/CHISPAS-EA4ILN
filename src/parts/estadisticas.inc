// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: estadisticas por caracter (Koch y Echo).
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// ESTADISTICAS POR CARACTER (KOCH Y ECHO)
// ============================================================================
#define NVS_ENTRENADOR "chispas"  // Entrenador (Koch, keyer, Echo y estadisticas)

int indiceKoch(char c) {
  for (uint8_t i = 0; i < TOTAL_CARACTERES_KOCH; i++) {
    if (TABLA_KOCH[i] == c) return i;
  }
  return -1;
}

// Olvido gradual: al pasar de STATS_OLVIDO_INTENTOS, aciertos y fallos de esa letra se dividen a la mitad. Asi
// pesa mas lo reciente (en MAS ERRORES y en el refuerzo) y una letra ya dominada deja de reforzarse
#define STATS_OLVIDO_INTENTOS 100

void registrarCaracter(StatsCaracter* tabla, char c, bool acierto) {
  int i = indiceKoch(c);
  if (i < 0) return;
  uint16_t& cuenta = acierto ? tabla[i].aciertos : tabla[i].fallos;
  if (cuenta < UINT16_MAX) cuenta++;
  if ((uint32_t)tabla[i].aciertos + tabla[i].fallos > STATS_OLVIDO_INTENTOS) {
    tabla[i].aciertos = (tabla[i].aciertos + 1) / 2;
    tabla[i].fallos = (tabla[i].fallos + 1) / 2;
  }
}

/**
 * Peso de un caracter en el sorteo con refuerzo: 1 + tope x tasa de fallos, sin pasar del tope (x2 suave, x3
 * fuerte). La tasa es la suma de Koch y Echo, suavizada ((fallos + 1) / (intentos + 2)) para que un fallo suelto
 * no dispare una letra con pocos datos. Con menos de MIN_INTENTOS_LETRA intentos, peso 1 (como sin refuerzo)
 */
float pesoRefuerzo(uint8_t i, uint8_t refuerzo) {
  uint32_t fallos = (uint32_t)statsCarKoch[i].fallos + statsCarEcho[i].fallos;
  uint32_t intentos = fallos + statsCarKoch[i].aciertos + statsCarEcho[i].aciertos;
  if (refuerzo == REFUERZO_NO || intentos < MIN_INTENTOS_LETRA) return 1.0f;
  float tope = (refuerzo == REFUERZO_FUERTE) ? 3.0f : 2.0f;
  float tasa = (fallos + 1.0f) / (intentos + 2.0f);
  return min(tope, 1.0f + tope * tasa);
}

// Hay datos para reforzar en la leccion: alguna de sus letras con MIN_INTENTOS_LETRA intentos o mas
bool hayDatosRefuerzo(uint8_t leccion) {
  uint8_t n = caracteresDeLeccion(leccion);
  for (uint8_t i = 0; i < n; i++) {
    uint32_t intentos = (uint32_t)statsCarKoch[i].aciertos + statsCarKoch[i].fallos +
                        statsCarEcho[i].aciertos + statsCarEcho[i].fallos;
    if (intentos >= MIN_INTENTOS_LETRA) return true;
  }
  return false;
}

// Por serie, al empezar una practica: las letras que salen mas de lo normal y su peso
void informarRefuerzo(const char* origen, uint8_t leccion, uint8_t refuerzo) {
  if (refuerzo == REFUERZO_NO) return;
  Serial.printf("[%s] REFUERZO %s:", origen, refuerzo == REFUERZO_FUERTE ? "FUERTE" : "SUAVE");
  uint8_t n = caracteresDeLeccion(leccion);
  bool alguna = false;
  for (uint8_t i = 0; i < n; i++) {
    float p = pesoRefuerzo(i, refuerzo);
    if (p > 1.05f) {
      Serial.printf(" %c x%.1f", TABLA_KOCH[i], p);
      alguna = true;
    }
  }
  Serial.println(alguna ? "" : " SIN DATOS (sorteo normal)");
}

void guardarStatsKochNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putBytes("carK", statsCarKoch, sizeof(statsCarKoch));
  prefs.end();
}

void guardarTotalesKochNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putUInt("ktcar", kochTotCaracteres);
  prefs.putUInt("ktok", kochTotAciertos);
  prefs.end();
}

void guardarStatsEchoNVS() {
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putBytes("carE", statsCarEcho, sizeof(statsCarEcho));
  prefs.putUInt("eint", echoTotalIntentos);
  prefs.putUInt("eok", echoTotalAciertos);
  prefs.putUShort("eses", echoSesiones);
  prefs.end();
}

// Anota un resultado en las estadisticas de una leccion (veces, % maximo y minimo)
void anotarResultadoLeccion(StatsLeccion& s, float pct) {
  uint8_t aciertoInt = (uint8_t)constrain(pct, 0, 100);
  s.vecesCompletada++;
  if (aciertoInt > s.maxAcierto) s.maxAcierto = aciertoInt;
  if (aciertoInt < s.minAcierto || s.vecesCompletada == 1) s.minAcierto = aciertoInt;
}

// Fin de una practica del Echo: el % de retos bien cuenta para su leccion
void registrarLeccionEchoNVS(uint8_t lecc, float pct) {
  if (lecc < 1 || lecc > 40) return;
  anotarResultadoLeccion(statsEchoLecc[lecc - 1], pct);
  prefs.begin(NVS_ENTRENADOR, false);
  prefs.putBytes("eLec", statsEchoLecc, sizeof(statsEchoLecc));
  prefs.end();
}

