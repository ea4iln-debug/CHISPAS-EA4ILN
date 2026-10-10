/**
 * Prueba de pines del prototipo: encoder KY-040.
 * Imprime por serie cada cambio de nivel y decodifica el giro en dos juegos de pines:
 *   A: CLK GPIO19, DT GPIO20, SW GPIO48 (mapa decidido; 19/20 son USB D-/D+ del S3)
 *   B: CLK GPIO38, DT GPIO39, SW GPIO40 (plan B)
 * Un juego sin nada conectado debe leer 1 fijo (pull-up interno) y no cambiar nunca.
 * Se carga con: pio run -e prueba_pines -t upload   (y monitor a 115200)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 EA4ILN. Software libre, sin garantia: ver el archivo LICENSE.
 */
#include <Arduino.h>

struct Pin {
  uint8_t gpio;
  const char* nombre;
  bool nivel;
  uint32_t cambios;
};

Pin pines[] = {
  {19, "A CLK (19)", true, 0}, {20, "A DT  (20)", true, 0}, {48, "A SW  (48)", true, 0},
  {38, "B CLK (38)", true, 0}, {39, "B DT  (39)", true, 0}, {40, "B SW  (40)", true, 0},
};
const uint8_t NUM_PINES = sizeof(pines) / sizeof(pines[0]);

// Decodificador de cuadratura por tabla: indice = (estado anterior << 2) | estado nuevo
const int8_t TABLA_CUADRATURA[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };

struct Encoder {
  const char* nombre;
  uint8_t idxClk, idxDt;
  uint8_t estado;
  int8_t acumulado;   // Transiciones desde el ultimo reposo (detent)
  int32_t pasos;
  uint32_t invalidas;  // Saltos de dos estados de golpe (rebote o lectura perdida)
};

Encoder encoders[] = {
  {"A (19/20)", 0, 1, 0, 0, 0, 0},
  {"B (38/39)", 3, 4, 0, 0, 0, 0},
};

uint8_t leerEstado(const Encoder& e) {
  return (pines[e.idxClk].nivel << 1) | pines[e.idxDt].nivel;
}

void imprimirNiveles(const char* titulo) {
  Serial.printf("%s:", titulo);
  for (uint8_t i = 0; i < NUM_PINES; i++) Serial.printf("  %s=%d", pines[i].nombre, pines[i].nivel);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println(F("\n=== PRUEBA DE PINES DEL ENCODER ==="));
  Serial.println(F("Gira despacio en los dos sentidos y pulsa el eje. Con el KY-040 en reposo, CLK y DT suelen leer 1."));

  for (uint8_t i = 0; i < NUM_PINES; i++) {
    pinMode(pines[i].gpio, INPUT_PULLUP);
  }
  delay(10);
  for (uint8_t i = 0; i < NUM_PINES; i++) pines[i].nivel = digitalRead(pines[i].gpio);
  for (Encoder& e : encoders) e.estado = leerEstado(e);
  imprimirNiveles("[INICIO]");
}

void loop() {
  // Lectura continua: el encoder a mano no pasa de unos cientos de transiciones por segundo
  for (uint8_t i = 0; i < NUM_PINES; i++) {
    bool n = digitalRead(pines[i].gpio);
    if (n != pines[i].nivel) {
      pines[i].nivel = n;
      pines[i].cambios++;
      Serial.printf("%8lu ms  %s -> %d\n", millis(), pines[i].nombre, n);
    }
  }

  for (Encoder& e : encoders) {
    uint8_t nuevo = leerEstado(e);
    if (nuevo == e.estado) continue;
    int8_t d = TABLA_CUADRATURA[(e.estado << 2) | nuevo];
    if (d == 0) e.invalidas++;
    e.acumulado += d;
    e.estado = nuevo;
    // El KY-040 da 4 transiciones por paso y reposa con CLK = DT = 1 (estado 3)
    if (nuevo == 3) {
      if (e.acumulado >= 2) { e.pasos++; Serial.printf("          >>> Encoder %s: PASO +1 (horario?)   total %ld\n", e.nombre, (long)e.pasos); }
      else if (e.acumulado <= -2) { e.pasos--; Serial.printf("          <<< Encoder %s: PASO -1 (antihorario?) total %ld\n", e.nombre, (long)e.pasos); }
      e.acumulado = 0;
    }
  }

  // Resumen cada 10 s
  static uint32_t ultimoResumen = 0;
  if (millis() - ultimoResumen >= 10000) {
    ultimoResumen = millis();
    imprimirNiveles("[RESUMEN] niveles");
    Serial.print(F("[RESUMEN] cambios:"));
    for (uint8_t i = 0; i < NUM_PINES; i++) Serial.printf("  %s=%lu", pines[i].nombre, (unsigned long)pines[i].cambios);
    Serial.println();
    for (Encoder& e : encoders) {
      Serial.printf("[RESUMEN] encoder %s: pasos %ld, transiciones invalidas %lu\n", e.nombre, (long)e.pasos, (unsigned long)e.invalidas);
    }
  }
}
