// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: tono (audio sintetizado por sigma-delta), LED y amplificador.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// CONTROL DE AUDIO Y LED MORSE
// ============================================================================
// Salida KEY a la emisora (GPIO2 -> PC817). Solo se habilita en OPERAR y QSO de la pantalla TRX: el resto
// (Koch, Echo, TX libre, LoRa) nunca manipula la emisora
volatile bool keyHabilitada = false;
volatile bool trxTonoLocal = true;           // Buzzer/auriculares mientras sale por KEY (NVS "ttono")
volatile unsigned long keyActivaDesdeMs = 0;  // 0 = KEY en reposo (para el corte de seguridad)
volatile unsigned long keyFinMs = 0;          // Ultima vez que se solto KEY (el decodificador no escucha justo despues)

// ----------------------------------------------------------------------------
// Audio sintetizado (16 oct 2026; antes, PWM cuadrado de un canal). Una interrupcion a 16 kHz (timer 0) calcula
// cada muestra y la escribe en el modulador sigma-delta del GPIO7 (mismo pin y mismo hardware que el PWM).
// Muestra = tono (cuadrada suavizada, con flancos de 5 ms: sin clics) x QSB + ruido de banda + chasquidos +
// pitido de interferencia; lo que no es el tono solo lo pone el simulador de QSO. Solo enteros: en una ISR no se
// usa la FPU. Coste medido en pruebas/prueba_audio.cpp: ~5 % de la CPU a 80 MHz, mas la entrada y salida.
// Solo funciona con el amplificador encendido; al apagarlo, el nivel medio baja despacio a 0 V (sin golpe en
// los auriculares) y se para la interrupcion. Escribir en flash (NVS, OTA) la retrasa: corte breve del audio
// ----------------------------------------------------------------------------
volatile bool audioTecla = false;      // Tono sonando (conmutarEmisionMorse y el pitido de prueba)
volatile int32_t audioQsb = 256;       // Volumen del tono: 256 = el maximo (el QSB del simulador lo baja)
volatile int32_t audioRuido = 0;       // Nivel del ruido de banda (0-127; 30 = el elegido de oido)
volatile uint32_t audioProbChasquido = 0;  // Probabilidad de chasquido por muestra, sobre 2^32 (0 = sin)
volatile int32_t audioInterf = 0;      // Nivel del pitido de interferencia (0-127; 10 = el elegido de oido)
volatile uint32_t audioIncTono = 0, audioIncInterf = 0;  // Paso de fase por muestra (2^32 = una vuelta)
volatile int32_t audioSvfF = 0;        // Filtro del ruido (estado variable, Q12) centrado en el tono
#define AUDIO_SVF_Q (4096 / 3)         // Q = 3: unos 250 Hz de ancho ("suena a radio")
volatile bool audioActivo = false;     // false: el nivel medio baja a 0 V y luego se para la interrupcion

int8_t audioSeno[256];
int8_t audioCuadrada[256];             // Fundamental + 3.er + 5.o armonicos (pico 127): preferida al seno
hw_timer_t* audioTimer = NULL;
bool audioTimerEnMarcha = false;

// Estado de la interrupcion
uint32_t audioFaseTono = 0, audioFaseInterf = 0, audioSemilla = 12345;
int32_t audioEnvolvente = 0, audioBajo = 0, audioBanda = 0, audioChasquido = 0;
volatile int32_t audioMedio = -AUDIO_MEDIO_PASOS;  // Nivel medio: de -AUDIO_MEDIO_PASOS (0 V) a 0 (la mitad)

static inline uint32_t IRAM_ATTR audioAleatorio() {
  audioSemilla ^= audioSemilla << 13;
  audioSemilla ^= audioSemilla >> 17;
  audioSemilla ^= audioSemilla << 5;
  return audioSemilla;
}

void IRAM_ATTR isrAudio() {
  // Nivel medio: sube y baja despacio (sin golpe); el tono no empieza hasta llegar a la mitad
  if (audioActivo) { if (audioMedio < 0) audioMedio++; }
  else if (audioMedio > -AUDIO_MEDIO_PASOS) audioMedio--;

  // Tono con flancos suaves y QSB
  if (audioTecla && audioMedio == 0) { audioEnvolvente += AUDIO_RAMPA; if (audioEnvolvente > 256) audioEnvolvente = 256; }
  else { audioEnvolvente -= AUDIO_RAMPA; if (audioEnvolvente < 0) audioEnvolvente = 0; }
  int32_t s = 0;
  if (audioEnvolvente > 0) {
    audioFaseTono += audioIncTono;
    s = (int32_t)audioCuadrada[audioFaseTono >> 24] * audioEnvolvente * audioQsb >> 16;
  }

  // Ruido: blanco por un filtro de estado variable en el tono (como el filtro de CW de una emisora)
  if (audioRuido > 0) {
    int32_t blanco = (int32_t)(audioAleatorio() & 0xFF) - 128;
    int32_t alto = blanco * 4 - audioBajo - (AUDIO_SVF_Q * audioBanda >> 12);
    audioBanda += audioSvfF * alto >> 12;
    audioBajo += audioSvfF * audioBanda >> 12;
    s += audioBanda / 4 * audioRuido / 127;
  }

  // Chasquidos de estatica: rafagas de ruido blanco que se apagan en unos 10 ms
  if (audioProbChasquido && audioAleatorio() < audioProbChasquido) audioChasquido = 60 + (audioAleatorio() & 0x3F);
  if (audioChasquido > 0) {
    s += (int32_t)((audioAleatorio() & 0xFF) - 128) * audioChasquido / 128;
    audioChasquido -= (audioChasquido >> 6) + 1;
  }

  // Pitido de interferencia (su deriva lenta la pone el simulador)
  if (audioInterf > 0) {
    audioFaseInterf += audioIncInterf;
    s += (int32_t)audioSeno[audioFaseInterf >> 24] * audioInterf / 127;
  }

  s += audioMedio * 128 / AUDIO_MEDIO_PASOS;
  if (s > 127) s = 127;
  if (s < -128) s = -128;
  sigmadelta_ll_set_duty(&SIGMADELTA, (sigmadelta_channel_t)CANAL_SD_AUDIO, (int8_t)s);
}

uint32_t audioIncremento(float hz) { return (uint32_t)(hz * 4294967296.0f / AUDIO_FS); }

// Chasquidos por minuto -> probabilidad por muestra
void audioPonerChasquidos(uint16_t porMinuto) {
  audioProbChasquido = (uint32_t)(4294967296.0 * porMinuto / 60.0 / AUDIO_FS);
}

// Tablas, modulador (a 0 V) y la interrupcion, parada hasta que se encienda el amplificador
void iniciarAudio() {
  float suma[256], pico = 0;
  for (int i = 0; i < 256; i++) {
    float x = 2 * PI * i / 256;
    audioSeno[i] = (int8_t)lroundf(127 * sinf(x));
    suma[i] = sinf(x) + sinf(3 * x) / 3 + sinf(5 * x) / 5;
    pico = max(pico, fabsf(suma[i]));
  }
  for (int i = 0; i < 256; i++) audioCuadrada[i] = (int8_t)lroundf(127 * suma[i] / pico);

  sigmadelta_config_t cfg = {};
  cfg.channel = (sigmadelta_channel_t)CANAL_SD_AUDIO;
  cfg.sigmadelta_prescale = DIVISOR_SD_AUDIO;  // Con 0 (el mas rapido) hay zumbido y clics leves; con 10, no
  cfg.sigmadelta_duty = -128;                  // 0 V
  cfg.sigmadelta_gpio = SPEAKER_PWM_PIN;
  sigmadelta_config(&cfg);

  audioTimer = timerBegin(0, 80, true);  // 1 MHz (APB a 80 MHz, tambien con la CPU a 80)
  timerAttachInterrupt(audioTimer, isrAudio, false);
  timerAlarmWrite(audioTimer, 1000000 / AUDIO_FS, true);
}

void arrancarAudio() {
  audioActivo = true;
  if (!audioTimerEnMarcha) {
    audioTimerEnMarcha = true;
    timerAlarmEnable(audioTimer);
  }
}

// Desde loop(): cuando el nivel medio ya ha bajado a 0 V, para la interrupcion
void procesarAudio() {
  if (audioTimerEnMarcha && !audioActivo && audioMedio == -AUDIO_MEDIO_PASOS) {
    timerAlarmDisable(audioTimer);
    audioTimerEnMarcha = false;
    sigmadelta_ll_set_duty(&SIGMADELTA, (sigmadelta_channel_t)CANAL_SD_AUDIO, -128);
  }
}

// Al dormir: sin interrupcion y el pin en BAJO (luego se retiene con gpio_hold_en)
void apagarAudioYa() {
  audioActivo = false;
  audioTecla = false;
  if (audioTimer) timerAlarmDisable(audioTimer);
  audioTimerEnMarcha = false;
  pinMode(SPEAKER_PWM_PIN, OUTPUT);  // Quita el modulador del pin
  digitalWrite(SPEAKER_PWM_PIN, LOW);
}

void conmutarEmisionMorse(bool activar) {
  digitalWrite(LED_PIN, activar ? HIGH : LOW);
  bool key = activar && keyHabilitada;
  digitalWrite(KEY_OUT_PIN, key ? HIGH : LOW);
  if (key) {
    // Nunca por delante de millis() (con "| 1" podia ir 1 ms adelantada y la resta daba la vuelta)
    unsigned long ahora = millis();
    if (keyActivaDesdeMs == 0) keyActivaDesdeMs = (ahora != 0) ? ahora : 1;
  } else if (keyActivaDesdeMs != 0) {
    keyActivaDesdeMs = 0;
    keyFinMs = millis();
  }
  audioTecla = activar && (!keyHabilitada || trxTonoLocal);
}

uint16_t frecTonoHz = FREC_AUDIO_HZ;  // Tono CW (buzzer y auriculares); se elige en CONF y se guarda en NVS

// Pitido de prueba al elegir el tono en CONF (alli no emite el motor CW). Lo apaga procesarTonoPrueba()
unsigned long tonoPruebaFinMs = 0;  // 0 = sin pitido

// Amplificador del altavoz (PAM8302A): en reposo gasta ~4 mA, tambien con la placa dormida (va al 3V3), asi que
// solo se enciende donde puede sonar algo: CW, LoRa y TRX, y el pitido de prueba del tono en CONF.
// La sintesis va con el: sin amplificador no suena nada (tampoco por los auriculares)
bool amplificadorEncendido = false;

void ponerAmplificador(bool encendido) {
  if (encendido == amplificadorEncendido) return;
  amplificadorEncendido = encendido;
  digitalWrite(AMP_SD_PIN, encendido ? HIGH : LOW);
  if (encendido) arrancarAudio();
  else audioActivo = false;
  Serial.println(encendido ? F("[AMP] ENCENDIDO") : F("[AMP] APAGADO"));
}

void procesarAmplificador() {
  ponerAmplificador(pantallaActual == PANTALLA_CW || pantallaActual == PANTALLA_LORA ||
                    pantallaActual == PANTALLA_TRX || tonoPruebaFinMs != 0);
  procesarAudio();
}

void aplicarFrecTono(bool pitidoPrueba) {
  audioIncTono = audioIncremento(frecTonoHz);
  audioIncInterf = audioIncremento(frecTonoHz + 180);
  audioSvfF = (int32_t)(2.0f * sinf(PI * frecTonoHz / AUDIO_FS) * 4096);
  if (pitidoPrueba) {
    // Si el amplificador estaba apagado, el tono espera a que el nivel medio suba (AUDIO_MEDIO_MS)
    unsigned long espera = amplificadorEncendido ? 0 : AUDIO_MEDIO_MS;
    ponerAmplificador(true);
    audioTecla = true;
    tonoPruebaFinMs = millis() + espera + TONO_PRUEBA_MS;
    if (tonoPruebaFinMs == 0) tonoPruebaFinMs = 1;
  }
}

void procesarTonoPrueba() {
  if (tonoPruebaFinMs != 0 && (long)(millis() - tonoPruebaFinMs) >= 0) {
    audioTecla = false;
    tonoPruebaFinMs = 0;
  }
}
