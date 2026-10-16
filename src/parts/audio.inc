// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: tono, LED y amplificador.
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

// Volumen del tono: 128 = el maximo (cuadrada al 50 %); con menos, la fundamental baja (sin(pi * ciclo)).
// Solo lo cambia el QSB del simulador de QSO, mientras emite el activador; se aplica al empezar cada elemento
volatile uint8_t cicloTono = 128;

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
  bool tono = activar && (!keyHabilitada || trxTonoLocal);
  ledcWrite(CANAL_PWM_AUDIO, tono ? cicloTono : 0);
}

uint16_t frecTonoHz = FREC_AUDIO_HZ;  // Tono CW (buzzer y auriculares); se elige en CONF y se guarda en NVS

// Pitido de prueba al elegir el tono en CONF (alli no emite el motor CW). Lo apaga procesarTonoPrueba()
unsigned long tonoPruebaFinMs = 0;  // 0 = sin pitido

// Amplificador del altavoz (PAM8302A): en reposo gasta ~4 mA, tambien con la placa dormida (va al 3V3), asi que
// solo se enciende donde puede sonar algo: CW, LoRa y TRX, y el pitido de prueba del tono en CONF
bool amplificadorEncendido = false;

void ponerAmplificador(bool encendido) {
  if (encendido == amplificadorEncendido) return;
  amplificadorEncendido = encendido;
  digitalWrite(AMP_SD_PIN, encendido ? HIGH : LOW);
  Serial.println(encendido ? F("[AMP] ENCENDIDO") : F("[AMP] APAGADO"));
}

void procesarAmplificador() {
  ponerAmplificador(pantallaActual == PANTALLA_CW || pantallaActual == PANTALLA_LORA ||
                    pantallaActual == PANTALLA_TRX || tonoPruebaFinMs != 0);
}

void aplicarFrecTono(bool pitidoPrueba) {
  ledcChangeFrequency(CANAL_PWM_AUDIO, frecTonoHz, RES_PWM_AUDIO);
  if (pitidoPrueba) {
    ponerAmplificador(true);
    ledcWrite(CANAL_PWM_AUDIO, 128);
    tonoPruebaFinMs = millis() + TONO_PRUEBA_MS;
    if (tonoPruebaFinMs == 0) tonoPruebaFinMs = 1;
  }
}

void procesarTonoPrueba() {
  if (tonoPruebaFinMs != 0 && (long)(millis() - tonoPruebaFinMs) >= 0) {
    ledcWrite(CANAL_PWM_AUDIO, 0);
    tonoPruebaFinMs = 0;
  }
}

