// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: lectura de la bateria.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// LECTURA DE BATERIA (4.20V / 100%)
// ============================================================================
enum EstadoAlimentacion { SOLO_USB, BAT_DESCARGA };

struct BateriaInfo {
  float voltaje;
  int porcentaje;
  EstadoAlimentacion estado;
};

// Curva de descarga tipica de una celda Li-ion en reposo (tension -> %), interpolada.
// Con USB conectado el cargador sube la tension y el porcentaje sale alto.
int porcentajeLiIon(float v) {
  static const float tension[] = { 3.30, 3.60, 3.68, 3.72, 3.75, 3.78, 3.81, 3.86, 3.92, 4.00, 4.10, 4.20 };
  static const int   pct[]     = {    0,    5,   10,   20,   30,   40,   50,   60,   70,   80,   90,  100 };
  const int n = sizeof(tension) / sizeof(tension[0]);
  if (v <= tension[0]) return 0;
  if (v >= tension[n - 1]) return 100;
  for (int i = 1; i < n; i++) {
    if (v <= tension[i]) {
      return pct[i - 1] + (int)lround((v - tension[i - 1]) / (tension[i] - tension[i - 1]) * (pct[i] - pct[i - 1]));
    }
  }
  return 100;
}

BateriaInfo leerBateria() {
  pinMode(ADC_CTRL_PIN, OUTPUT);
  digitalWrite(ADC_CTRL_PIN, HIGH);
  delay(10);

  // Al pin llegan como mucho 4.2 V / 4.985 = 0.84 V: 2.5 dB (rango hasta unos 1.25 V) es mas preciso que 11 dB
  analogReadResolution(12);
  analogSetPinAttenuation(VBAT_ADC_PIN, ADC_2_5db);

  // Rafaga de lecturas seguidas (unos 18 ms): de ella salen la media y la dispersion
  uint32_t sumaMv = 0;
  uint64_t sumaCuadrados = 0;
  for (int i = 0; i < VBAT_LECTURAS; i++) {
    uint32_t mv = analogReadMilliVolts(VBAT_ADC_PIN);
    sumaMv += mv;
    sumaCuadrados += (uint64_t)mv * mv;
  }
  float mvPromedio = (float)sumaMv / VBAT_LECTURAS;
  float varianza = (float)sumaCuadrados / VBAT_LECTURAS - mvPromedio * mvPromedio;
  float dispersion = sqrtf(max(varianza, 0.0f)) * VBAT_MULTIPLIER / 1000.0f;  // En V de bateria

  digitalWrite(ADC_CTRL_PIN, LOW);

  float voltaje = (mvPromedio * VBAT_MULTIPLIER) / 1000.0;

  // Suavizado como Meshtastic: cada lectura se promedia al 50 % con la anterior, para que el
  // porcentaje no baile. La primera lectura (o la primera tras volver la bateria) arranca el filtro.
  static float voltajeFiltrado = 0;

  BateriaInfo bat;
  if (voltaje < VBAT_SIN_BATERIA || dispersion > VBAT_DISPERSION_SIN_BATERIA) {
    voltajeFiltrado = 0;
    bat.voltaje = 0.0;
    bat.porcentaje = 0;
    bat.estado = SOLO_USB;
  } else {
    voltajeFiltrado = (voltajeFiltrado == 0) ? voltaje : voltajeFiltrado + (voltaje - voltajeFiltrado) * 0.5;
    bat.voltaje = voltajeFiltrado;
    bat.porcentaje = porcentajeLiIon(voltajeFiltrado);
    bat.estado = BAT_DESCARGA;
  }
  return bat;
}

// leerBateria() bloquea unos 30 ms: se lee cada 30 s fuera de la emision CW y el
// dibujo usa este valor en cache
#define INTERVALO_BATERIA_MS 30000
BateriaInfo bateriaActual = {0.0, 0, SOLO_USB};
bool pantallaPendiente = false;  // Redibujar en cuanto no suene CW

