// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: sensor ambiental BME280.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// SENSOR AMBIENTAL BME280 (I2C EN WIRE1)
// ============================================================================
// BME280 (o BMP280, sin humedad) en Wire1: SDA GPIO41 (J3-8), SCL GPIO42 (J3-7), direccion 0x76 o 0x77. Controlador
// propio con las formulas de compensacion en coma flotante de la hoja de datos de Bosch (BME280, apartado 8.1).
// Modo forzado: mide cuando se le pide y vuelve a dormir solo (~0,1 uA)
#define BME_SDA               41
#define BME_SCL               42
#define BME_LECTURA_MS        60000UL                // Una lectura por minuto...
#define BME_LECTURA_VISTA_MS  2000UL                 // ...y cada 2 s con la pagina AMBIENTE a la vista
#define BME_HIST_INTERVALO_MS (10UL * 60 * 1000)     // Historial de presion: una muestra cada 10 min...
#define BME_HIST_N            19                     // ...durante 3 h (18 intervalos)
#define BME_TEND_MIN_MIN      60                     // Minutos de historial antes de dar la tendencia
#define BME_TEND_CAMBIO_HPA   1.0f                   // Cambio en 3 h para BAJANDO / SUBIENDO
#define BME_TORMENTA_HPA      3.0f                   // Bajada en 3 h para el aviso de tormenta
#define QNH_ESTANDAR_HPA      1013.25f

uint8_t bmeDireccion = 0;    // 0 = no hay sensor
bool bmeConHumedad = false;  // BME280 (id 0x60) si; BMP280 (0x56-0x58) no
struct CalibracionBME {
  uint16_t T1; int16_t T2, T3;
  uint16_t P1; int16_t P2, P3, P4, P5, P6, P7, P8, P9;
  uint8_t H1, H3; int16_t H2, H4, H5; int8_t H6;
} bmeCal;
float bmeTempC = NAN, bmeHumedad = NAN, bmePresionHpa = NAN;  // NAN = sin lectura valida
unsigned long bmeUltimaLecturaMs = 0;
float bmeHist[BME_HIST_N];  // Presion (hPa), de la muestra mas antigua a la mas reciente
uint8_t bmeHistN = 0;
bool bmeHistNivelMar = false;  // Muestras reducidas a nivel del mar con la altitud del GPS (sin GPS, presion local)
unsigned long bmeUltimaMuestraMs = 0;
float qnhHpa = QNH_ESTANDAR_HPA;  // Presion a nivel del mar para la altitud (NVS "qnh" en estacion)
bool qnhCalibrado = false;
bool avisoTormenta = false;       // Presion bajando deprisa: se ve en la cabecera ("TORM!")

bool bmeLeer(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire1.beginTransmission(bmeDireccion);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom((int)bmeDireccion, (int)n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire1.read();
  return true;
}

bool bmeEscribir(uint8_t reg, uint8_t valor) {
  Wire1.beginTransmission(bmeDireccion);
  Wire1.write(reg);
  Wire1.write(valor);
  return Wire1.endTransmission() == 0;
}

bool bmeLeerCalibracion() {
  uint8_t b[26];
  if (!bmeLeer(0x88, b, 26)) return false;
  auto u16 = [&](uint8_t i) { return (uint16_t)(b[i] | (b[i + 1] << 8)); };
  bmeCal.T1 = u16(0);           bmeCal.T2 = (int16_t)u16(2);  bmeCal.T3 = (int16_t)u16(4);
  bmeCal.P1 = u16(6);           bmeCal.P2 = (int16_t)u16(8);  bmeCal.P3 = (int16_t)u16(10);
  bmeCal.P4 = (int16_t)u16(12); bmeCal.P5 = (int16_t)u16(14); bmeCal.P6 = (int16_t)u16(16);
  bmeCal.P7 = (int16_t)u16(18); bmeCal.P8 = (int16_t)u16(20); bmeCal.P9 = (int16_t)u16(22);
  bmeCal.H1 = b[25];
  if (!bmeConHumedad) return true;
  uint8_t h[7];
  if (!bmeLeer(0xE1, h, 7)) return false;
  bmeCal.H2 = (int16_t)(h[0] | (h[1] << 8));
  bmeCal.H3 = h[2];
  bmeCal.H4 = (int16_t)(((int8_t)h[3] << 4) | (h[4] & 0x0F));
  bmeCal.H5 = (int16_t)(((int8_t)h[5] << 4) | (h[4] >> 4));
  bmeCal.H6 = (int8_t)h[6];
  return true;
}

/**
 * Una medida en modo forzado (temperatura x1, presion x4, humedad x1: hasta ~16 ms) y su compensacion.
 * Devuelve false si el sensor no contesta (las lecturas quedan en NAN)
 */
bool bmeMedir() {
  if (bmeDireccion == 0) return false;
  bool ok = bmeEscribir(0xF4, (1 << 5) | (3 << 2) | 1);
  uint8_t estado = 0x08;
  unsigned long inicio = millis();
  while (ok && (estado & 0x08) && millis() - inicio < 50) {  // Bit 3 = midiendo
    delay(4);
    ok = bmeLeer(0xF3, &estado, 1);
  }
  uint8_t b[8];
  if (ok) ok = bmeLeer(0xF7, b, bmeConHumedad ? 8 : 6);
  if (!ok) {
    if (!isnan(bmePresionHpa)) Serial.println(F("[BME] ERROR DE LECTURA"));
    bmeTempC = bmeHumedad = bmePresionHpa = NAN;
    return false;
  }
  int32_t adcP = ((int32_t)b[0] << 12) | ((int32_t)b[1] << 4) | (b[2] >> 4);
  int32_t adcT = ((int32_t)b[3] << 12) | ((int32_t)b[4] << 4) | (b[5] >> 4);

  // Temperatura
  double v1 = (adcT / 16384.0 - bmeCal.T1 / 1024.0) * bmeCal.T2;
  double v2 = (adcT / 131072.0 - bmeCal.T1 / 8192.0) * (adcT / 131072.0 - bmeCal.T1 / 8192.0) * bmeCal.T3;
  double tFino = v1 + v2;
  bmeTempC = tFino / 5120.0;

  // Presion (Pa)
  v1 = tFino / 2.0 - 64000.0;
  v2 = v1 * v1 * bmeCal.P6 / 32768.0;
  v2 = v2 + v1 * bmeCal.P5 * 2.0;
  v2 = v2 / 4.0 + bmeCal.P4 * 65536.0;
  v1 = (bmeCal.P3 * v1 * v1 / 524288.0 + bmeCal.P2 * v1) / 524288.0;
  v1 = (1.0 + v1 / 32768.0) * bmeCal.P1;
  if (v1 == 0) {
    bmePresionHpa = NAN;
  } else {
    double p = 1048576.0 - adcP;
    p = (p - v2 / 4096.0) * 6250.0 / v1;
    v1 = bmeCal.P9 * p * p / 2147483648.0;
    v2 = p * bmeCal.P8 / 32768.0;
    bmePresionHpa = (p + (v1 + v2 + bmeCal.P7) / 16.0) / 100.0;
  }

  // Humedad relativa (%)
  if (bmeConHumedad) {
    int32_t adcH = ((int32_t)b[6] << 8) | b[7];
    double h = tFino - 76800.0;
    h = (adcH - (bmeCal.H4 * 64.0 + bmeCal.H5 / 16384.0 * h)) *
        (bmeCal.H2 / 65536.0 * (1.0 + bmeCal.H6 / 67108864.0 * h * (1.0 + bmeCal.H3 / 67108864.0 * h)));
    h = h * (1.0 - bmeCal.H1 * h / 524288.0);
    bmeHumedad = constrain(h, 0.0, 100.0);
  }
  return true;
}

// Busca el sensor en 0x76 y 0x77, lo identifica y hace la primera medida
void iniciarBME() {
  Wire1.begin(BME_SDA, BME_SCL, 100000);
  const uint8_t direcciones[] = { 0x76, 0x77 };
  for (uint8_t d : direcciones) {
    Wire1.beginTransmission(d);
    if (Wire1.endTransmission() != 0) continue;
    bmeDireccion = d;
    uint8_t id = 0;
    if (!bmeLeer(0xD0, &id, 1) || (id != 0x60 && (id < 0x56 || id > 0x58))) {
      Serial.printf("[BME] CHIP DESCONOCIDO (ID 0x%02X) EN 0x%02X\n", id, d);
      bmeDireccion = 0;
      continue;
    }
    bmeConHumedad = (id == 0x60);
    bmeEscribir(0xE0, 0xB6);  // Reset; luego espera a que copie la calibracion (bit 0 de 0xF3)
    delay(5);
    uint8_t estado = 1;
    for (uint8_t i = 0; i < 10 && (estado & 0x01); i++) {
      delay(2);
      bmeLeer(0xF3, &estado, 1);
    }
    if (!bmeLeerCalibracion()) {
      Serial.println(F("[BME] ERROR AL LEER LA CALIBRACION"));
      bmeDireccion = 0;
      continue;
    }
    if (bmeConHumedad) bmeEscribir(0xF2, 0x01);  // Humedad x1 (vale al escribir 0xF4)
    bmeEscribir(0xF5, 0x00);                     // Sin filtro IIR
    Serial.printf("[BME] %s EN 0x%02X\n", bmeConHumedad ? "BME280" : "BMP280 (SIN HUMEDAD)", d);
    if (bmeMedir()) {
      Serial.printf("[BME] T %.1f C  HR %.0f %%  P %.2f HPA\n", bmeTempC, bmeConHumedad ? bmeHumedad : 0.0f,
                    bmePresionHpa);
    }
    bmeUltimaLecturaMs = millis();
    return;
  }
  Serial.println(F("[BME] NO DETECTADO (SDA GPIO41, SCL GPIO42)"));
  Wire1.end();
}

// Altitud barometrica (m) con el QNH guardado; NAN sin lectura
float altitudBarometrica() {
  if (isnan(bmePresionHpa)) return NAN;
  return 44330.0f * (1.0f - powf(bmePresionHpa / qnhHpa, 0.1903f));
}

// QNH que hace que la altitud actual sea la indicada (calibracion con una altitud conocida)
float qnhParaAltitud(float altitudM) {
  return bmePresionHpa / powf(1.0f - altitudM / 44330.0f, 5.255f);
}

// Cambio de presion de las ultimas 3 h (escalado a 3 h si hay menos); NAN con menos de BME_TEND_MIN_MIN de historial
float bmeTendencia3h() {
  if (bmeHistN < 2) return NAN;
  uint16_t minutos = (bmeHistN - 1) * (BME_HIST_INTERVALO_MS / 60000UL);
  if (minutos < BME_TEND_MIN_MIN) return NAN;
  return (bmeHist[bmeHistN - 1] - bmeHist[0]) * 180.0f / minutos;
}

// Macro {WX} de las memorias: "WX 18C" ("WX MINUS 3C" bajo cero: el guion no tiene codigo Morse); vacio sin sensor
String textoWx() {
  if (isnan(bmeTempC)) return String("");
  long t = lroundf(bmeTempC);
  return t < 0 ? "WX MINUS " + String(-t) + "C" : "WX " + String(t) + "C";
}

