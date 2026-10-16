/**
 * Prueba de audio sintetizado por sigma-delta en el GPIO7 (el del tono), sin cambiar el hardware:
 * ¿suena limpio por el PAM8302A y los auriculares? Si es asi, el simulador de QSO puede tener ruido de
 * banda, chasquidos de estatica (QRN), un pitido de interferencia y QSB, todo a la vez que el CW.
 *
 * Una interrupcion a 16 kHz calcula cada muestra (solo enteros: en una ISR no se usa la FPU) y la escribe en el
 * modulador sigma-delta. Muestra = tono (seno, con flancos de 5 ms) x QSB + ruido filtrado + chasquidos +
 * pitido. El CW repite "CQ CQ DE EA4ILN EA4ILN K" a 18 WPM.
 *
 * Ordenes por serie (115200, terminar con Enter), por ejemplo "r 40":
 *   t N  nivel del tono (0-127)            f N  frecuencia del tono (400-1000 Hz)
 *   r N  nivel del ruido (0-127)           a N  ancho del ruido: 0 = blanco, 1 = ancho, 2 = estrecho (filtro CW)
 *   c N  chasquidos por minuto (0-300)     i N  nivel del pitido de interferencia (0-127)
 *   q N  QSB: 0 no, 1 suave, 2 fuerte      p N  divisor del sigma-delta (0-255; 0 = el mas rapido)
 *   m    cambia a PWM cuadrado (como ahora en Chispas) o vuelve a sigma-delta, para comparar
 *   k    CW o tono continuo                ?    ayuda y valores
 * Se carga con: pio run -e prueba_audio -t upload   (monitor a 115200)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 EA4ILN. Software libre, sin garantia: ver el archivo LICENSE.
 */
#include <Arduino.h>
#include "driver/sigmadelta.h"
#include "hal/sigmadelta_ll.h"
#include "soc/gpio_sd_struct.h"

#define AUDIO_PIN   7
#define AMP_SD_PIN  38
#define KEY_PIN     2
#define CANAL_SD    0
#define CANAL_PWM   0
#define FS          16000  // Muestras por segundo
#define RAMPA       3      // Paso de la envolvente por muestra (256 / 3 = 85 muestras = 5 ms)

// Ajustes (los cambia loop() por serie; la ISR solo los lee)
volatile int32_t nivelTono = 100, nivelRuido = 0, nivelInterf = 0;
volatile uint32_t probChasquido = 0;  // Por muestra, sobre 2^32
volatile uint8_t anchoRuido = 2;
volatile int32_t qsbNivel = 256;      // Lo calcula loop(): 256 = sin atenuar
volatile uint32_t incTono = 0, incInterf = 0;
volatile bool keyAbajo = false;
volatile int32_t svfF = 0, svfQ = 0;  // Filtro de estado variable (Q12) centrado en el tono

int8_t SENO[256];
hw_timer_t* timer = NULL;
bool modoPwm = false;
bool continuo = false;
uint16_t frecTono = 700;
uint8_t qsb = 0;
uint8_t prescaler = 0;
uint16_t chasquidosMin = 0;

// Estado de la ISR
uint32_t faseTono = 0, faseInterf = 0, semilla = 12345;
int32_t envolvente = 0, bajo = 0, banda = 0, lento = 0, ampChasquido = 0;

static inline uint32_t IRAM_ATTR aleatorio() {
  semilla ^= semilla << 13; semilla ^= semilla >> 17; semilla ^= semilla << 5;
  return semilla;
}

void IRAM_ATTR muestra() {
  // Tono con flancos suaves (sin clics) y QSB
  if (keyAbajo) { envolvente += RAMPA; if (envolvente > 256) envolvente = 256; }
  else          { envolvente -= RAMPA; if (envolvente < 0) envolvente = 0; }
  faseTono += incTono;
  int32_t s = (int32_t)SENO[faseTono >> 24] * envolvente / 256 * nivelTono / 127 * qsbNivel / 256;

  // Ruido: blanco; o paso alto suave (ancho); o filtro de estado variable en el tono (como un filtro de CW)
  int32_t blanco = (int32_t)(aleatorio() & 0xFF) - 128;
  int32_t ruido;
  if (anchoRuido == 0) {
    ruido = blanco;
  } else if (anchoRuido == 1) {
    bajo += (blanco - bajo) / 2;           // Quita lo mas agudo
    lento += (bajo - lento) / 32;          // Quita los graves
    ruido = (bajo - lento) * 3 / 2;
  } else {
    int32_t alto = blanco * 4 - bajo - (svfQ * banda >> 12);
    banda += svfF * alto >> 12;
    bajo += svfF * banda >> 12;
    ruido = banda / 4;
  }
  s += ruido * nivelRuido / 127;

  // Chasquidos de estatica: rafagas de ruido blanco que se apagan en unos 10 ms
  if (probChasquido && aleatorio() < probChasquido) ampChasquido = 60 + (aleatorio() & 0x3F);
  if (ampChasquido > 0) {
    s += (int32_t)((aleatorio() & 0xFF) - 128) * ampChasquido / 128;
    ampChasquido -= (ampChasquido >> 6) + 1;
  }

  // Pitido de interferencia (deriva despacio: lo mueve loop())
  if (nivelInterf) {
    faseInterf += incInterf;
    s += (int32_t)SENO[faseInterf >> 24] * nivelInterf / 127;
  }

  if (s > 127) s = 127;
  if (s < -128) s = -128;
  sigmadelta_ll_set_duty(&SIGMADELTA, (sigmadelta_channel_t)CANAL_SD, (int8_t)s);
}

void IRAM_ATTR isrMuestra() {
  if (!modoPwm) muestra();
}

uint32_t incremento(float hz) { return (uint32_t)(hz * 4294967296.0f / FS); }

void ponerTono(uint16_t hz) {
  frecTono = constrain(hz, 400, 1000);
  incTono = incremento(frecTono);
  float f = 2.0f * sinf(PI * frecTono / FS);
  svfF = (int32_t)(f * 4096);
  svfQ = 4096 / 3;  // Q = 3: unos 250 Hz de ancho
  if (modoPwm) ledcChangeFrequency(CANAL_PWM, frecTono, 8);
}

void ponerModo(bool pwm) {
  modoPwm = pwm;
  if (pwm) {
    ledcSetup(CANAL_PWM, frecTono, 8);
    ledcAttachPin(AUDIO_PIN, CANAL_PWM);
  } else {
    ledcDetachPin(AUDIO_PIN);
    sigmadelta_config_t cfg = {};
    cfg.channel = (sigmadelta_channel_t)CANAL_SD;
    cfg.sigmadelta_prescale = prescaler;
    cfg.sigmadelta_duty = 0;
    cfg.sigmadelta_gpio = AUDIO_PIN;
    sigmadelta_config(&cfg);
  }
}

// En PWM no hay mezcla: solo el tono cuadrado, como ahora (para comparar el timbre)
void teclaPwm(bool abajo) {
  if (modoPwm) ledcWrite(CANAL_PWM, abajo ? 128 * qsbNivel / 256 : 0);
}

// ---------------------------------------------------------------------------------------------------------
// CW: "CQ CQ DE EA4ILN EA4ILN K" a 18 WPM y 2 s de pausa
const char* TEXTO = "CQ CQ DE EA4ILN EA4ILN K";
struct Morse { char c; const char* codigo; };
const Morse TABLA[] = {
  {'A', ".-"}, {'C', "-.-."}, {'D', "-.."}, {'E', "."}, {'I', ".."}, {'K', "-.-"}, {'L', ".-.."}, {'N', "-."},
  {'Q', "--.-"}, {'4', "....-"}
};
const char* codigoDe(char c) {
  for (const Morse& m : TABLA) if (m.c == c) return m.codigo;
  return "";
}

#define MAX_TRAMOS 200
uint16_t tramos[MAX_TRAMOS];  // Duraciones alternas: encendido, apagado, encendido...
uint8_t nTramos = 0, tramo = 0;
unsigned long inicioTramo = 0;

void prepararTexto(uint8_t wpm) {
  uint16_t dit = 1200 / wpm;
  nTramos = 0;
  for (const char* p = TEXTO; *p; p++) {
    if (*p == ' ') { if (nTramos) tramos[nTramos - 1] += 4 * dit; continue; }  // 3 + 4 = 7
    for (const char* e = codigoDe(*p); *e; e++) {
      tramos[nTramos++] = (*e == '-') ? 3 * dit : dit;
      tramos[nTramos++] = dit;
    }
    tramos[nTramos - 1] += 2 * dit;  // 1 + 2 = 3 entre letras
  }
  tramos[nTramos - 1] = 2000;
}

void procesarCW() {
  if (continuo) {
    if (!keyAbajo) { keyAbajo = true; teclaPwm(true); }
    return;
  }
  if (millis() - inicioTramo < tramos[tramo]) return;
  inicioTramo += tramos[tramo];
  tramo = (tramo + 1) % nTramos;
  keyAbajo = (tramo % 2) == 0;
  teclaPwm(keyAbajo);
}

// QSB como en el simulador (dos senos de 5,3 y 2,1 s) y deriva lenta del pitido de interferencia
void procesarLento() {
  static unsigned long ultimo = 0;
  if (millis() - ultimo < 10) return;
  ultimo = millis();
  float t = millis() / 1000.0f;
  if (qsb == 0) {
    qsbNivel = 256;
  } else {
    float f = 0.5f + 0.5f * (0.7f * sinf(2 * PI * t / 5.3f) + 0.3f * sinf(2 * PI * t / 2.1f));
    float prof = (qsb == 2) ? 0.95f : 0.6f;
    qsbNivel = (int32_t)(256 * (1.0f - prof * f));
  }
  incInterf = incremento(frecTono + 180 + 60 * sinf(2 * PI * t / 17.0f));
}

void ayuda() {
  Serial.println(F("\n[AUDIO] t tono, f frec, r ruido, a ancho (0 blanco, 1 ancho, 2 estrecho), c chasquidos/min,"));
  Serial.println(F("        i interferencia, q qsb (0-2), p divisor SD, m PWM/SD, k CW/continuo"));
  Serial.printf("[AUDIO] %s  tono %ld a %u Hz  ruido %ld (ancho %u)  chasquidos %u/min  interf %ld  qsb %u  p %u  %s\n",
                modoPwm ? "PWM" : "SIGMA-DELTA", (long)nivelTono, frecTono, (long)nivelRuido, anchoRuido,
                chasquidosMin, (long)nivelInterf, qsb, prescaler, continuo ? "CONTINUO" : "CW");
}

void procesarSerie() {
  static String linea;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch != '\n' && ch != '\r') { linea += ch; continue; }
    linea.trim();
    if (linea.length() == 0) continue;
    char orden = linea[0];
    long v = linea.substring(1).toInt();
    switch (orden) {
      case 't': nivelTono = constrain(v, 0, 127); break;
      case 'f': ponerTono(v); break;
      case 'r': nivelRuido = constrain(v, 0, 127); break;
      case 'a': anchoRuido = constrain(v, 0, 2); bajo = banda = lento = 0; break;
      case 'c':
        chasquidosMin = constrain(v, 0, 300);
        probChasquido = (uint32_t)(4294967296.0 * chasquidosMin / 60.0 / FS);
        break;
      case 'i': nivelInterf = constrain(v, 0, 127); break;
      case 'q': qsb = constrain(v, 0, 2); break;
      case 'p':
        prescaler = constrain(v, 0, 255);
        if (!modoPwm) sigmadelta_set_prescale((sigmadelta_channel_t)CANAL_SD, prescaler);
        break;
      case 'm': ponerModo(!modoPwm); teclaPwm(keyAbajo); break;
      case 'k': continuo = !continuo; keyAbajo = false; teclaPwm(false); tramo = 0; inicioTramo = millis(); break;
    }
    linea = "";
    ayuda();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(KEY_PIN, OUTPUT);
  digitalWrite(KEY_PIN, LOW);  // La emisora, nunca
  pinMode(AMP_SD_PIN, OUTPUT);
  digitalWrite(AMP_SD_PIN, HIGH);
  for (int i = 0; i < 256; i++) SENO[i] = (int8_t)lroundf(127 * sinf(2 * PI * i / 256));

  ponerTono(frecTono);
  ponerModo(false);
  prepararTexto(18);
  inicioTramo = millis();

  timer = timerBegin(0, 80, true);  // 1 MHz
  timerAttachInterrupt(timer, isrMuestra, true);
  timerAlarmWrite(timer, 1000000 / FS, true);
  timerAlarmEnable(timer);

  delay(500);
  Serial.println(F("\n[AUDIO] Prueba de audio sigma-delta en GPIO7"));
  ayuda();
}

void loop() {
  procesarSerie();
  procesarCW();
  procesarLento();
  delay(1);
}
