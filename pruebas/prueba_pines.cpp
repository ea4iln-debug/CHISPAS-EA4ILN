/**
 * Prueba de pines del encoder: CLK GPIO19, SW GPIO20, DT GPIO48.
 * Imprime cada cambio de nivel y, cada 2 s, un resumen acumulado: cambios, pasos y la tension
 * minima y maxima de CLK (ADC2, aproximada y sin el pull-up interno); GPIO48 no tiene ADC.
 * Sin girar, CLK y DT deben leer 1 fijo. Girando, el nivel bajo debe llegar a menos de 0,8 V y
 * el alto a mas de 2,5 V (umbrales del ESP32-S3: 0,25 y 0,75 VDD).
 * Se carga con: pio run -e prueba_pines -t upload   (y serie.py a 115200)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 EA4ILN. Software libre, sin garantia: ver el archivo LICENSE.
 */
#include <Arduino.h>

struct Pin {
  uint8_t gpio;
  const char* nombre;
  bool conAdc;
  bool nivel;
  uint32_t cambios;
  uint32_t minMv;
  uint32_t maxMv;
};

Pin pines[] = {
  {19, "CLK", true, true, 0, 9999, 0},
  {48, "DT", false, true, 0, 9999, 0},
  {20, "SW", false, true, 0, 9999, 0},  // Sin pull-up externo: en modo analogico flota, no se mide
};
const uint8_t NUM_PINES = sizeof(pines) / sizeof(pines[0]);

// Decodificador de cuadratura por tabla: indice = (estado anterior << 2) | estado nuevo
const int8_t TABLA_CUADRATURA[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
uint8_t estado = 3;
int8_t acumulado = 0;
int32_t pasos = 0;
uint32_t invalidas = 0;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println(F("\n=== PRUEBA DE PINES DEL ENCODER ==="));
  for (Pin& p : pines) pinMode(p.gpio, INPUT_PULLUP);
  delay(10);
  for (Pin& p : pines) p.nivel = digitalRead(p.gpio);
  estado = (pines[0].nivel << 1) | pines[1].nivel;
}

// Mide con el pin en modo analogico y lo devuelve a entrada con pull-up
void medirTension(Pin& p) {
  uint32_t mv = analogRead(p.gpio) * 3100UL / 4095;  // Crudo a 11 dB, ~3,1 V a fondo (analogReadMilliVolts satura en ADC2)
  pinMode(p.gpio, INPUT_PULLUP);
  if (mv < p.minMv) p.minMv = mv;
  if (mv > p.maxMv) p.maxMv = mv;
}

void loop() {
  for (Pin& p : pines) {
    bool n = digitalRead(p.gpio);
    if (n != p.nivel) {
      p.nivel = n;
      p.cambios++;
      Serial.printf("%8lu ms  %s -> %d\n", millis(), p.nombre, n);
    }
  }

  uint8_t nuevo = (pines[0].nivel << 1) | pines[1].nivel;
  if (nuevo != estado) {
    int8_t d = TABLA_CUADRATURA[(estado << 2) | nuevo];
    if (d == 0) invalidas++;
    acumulado += d;
    estado = nuevo;
    if (nuevo == 3) {
      if (acumulado >= 2) pasos++;
      else if (acumulado <= -2) pasos--;
      acumulado = 0;
    }
  }

  static uint32_t ultimaMedida = 0;
  if (millis() - ultimaMedida >= 3) {
    ultimaMedida = millis();
    for (Pin& p : pines) {
      if (p.conAdc) medirTension(p);
    }
    delayMicroseconds(300);  // Deja que el pull-up recupere el nodo antes de leer en digital
  }

  static uint32_t ultimoResumen = 0;
  if (millis() - ultimoResumen >= 2000) {
    ultimoResumen = millis();
    Serial.print(F("[RESUMEN]"));
    for (Pin& p : pines) {
      Serial.printf("  %s=%d cambios %lu", p.nombre, p.nivel, (unsigned long)p.cambios);
      if (p.conAdc) Serial.printf(" (%lu-%lu mV)", (unsigned long)p.minMv, (unsigned long)p.maxMv);
    }
    Serial.printf("  pasos %ld invalidas %lu\n", (long)pasos, (unsigned long)invalidas);
  }
}
