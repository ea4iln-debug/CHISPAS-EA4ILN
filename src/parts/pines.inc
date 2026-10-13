// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: pines y constantes de hardware.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// DEFINICION DE PINES NATIVOS (Heltec WiFi LoRa 32 V3 / ESP32-S3)
// ============================================================================
#define OLED_SDA        17  // Bus I2C - Datos SSD1306
#define OLED_SCL        18  // Bus I2C - Reloj SSD1306
#define OLED_RST        21  // Reset del OLED
#define VEXT_CTRL       36  // Control de alimentacion VExt (Low = ON)

#define BUTTON_PIN      0   // Boton PRG integrado
#define LED_PIN         35  // LED blanco integrado en placa

#define LORA_NSS        8   // Bus SPI - Select del SX1262
#define LORA_DIO1       14  // Pin de interrupcion Hardware SX1262
#define LORA_NRST       12  // Reset HW del transceptor SX1262
#define LORA_BUSY       13  // Estado ocupado del SX1262

// Parametros de radio LoRa (las dos placas deben usar los mismos)
// 868.1 MHz cae entero en la sub-banda 868.0-868.6 (25 mW, 1 % de ciclo de trabajo).
// SF7 / 250 kHz: unos 32 ms de aire por trama de 18 bytes (SF9 / 125 kHz: unos 226 ms).
#define LORA_FREC_MHZ     868.1
#define LORA_BW_KHZ       250.0
#define LORA_SF           7
#define LORA_CR           7      // 4/7
#define LORA_SYNC_WORD    0x12   // Red privada
#define LORA_POTENCIA_DBM 10
#define LORA_PREAMBULO    8
// La V3 alimenta el TCXO por DIO3 a 1.8 V (Meshtastic heltec_v3: SX126X_DIO3_TCXO_VOLTAGE 1.8);
// el valor por defecto de RadioLib es 1.6 V. DIO2 como RF switch ya lo activa RadioLib en begin().
#define LORA_TCXO_V       1.8
// Escucha del canal (CAD) antes de transmitir
#define LORA_CAD_INTENTOS      3
#define LORA_ESPERA_OCUPADO_MS 100  // Una trama de 45 bytes con SF7/250 kHz dura unos 60 ms

#define KEYER_PADDLE_DIT 4  // Entrada manipulador yambico (Punto), punta del jack J1
#define KEYER_PADDLE_DAH 5  // Entrada manipulador yambico (Raya), anillo del jack J1
#define KEY_OUT_PIN      2  // Salida KEY a la emisora por el PC817: ALTO = manipulando. En BAJO desde el arranque
// Encoder KY-040. 19/20 son USB D-/D+ del S3, pero probados en placa como entradas: funcionan
#define ENC_CLK_PIN     19  // Pull-up de 10 kOhm en el modulo
#define ENC_DT_PIN      20  // Pull-up de 10 kOhm en el modulo
#define ENC_SW_PIN      48  // Pulsador del eje, sin pull-up en el modulo: INPUT_PULLUP
#define SPEAKER_PWM_PIN  7  // Tono CW por PWM (J3-18): volumen, auriculares y buzzer (ver esquema, hoja 4)
#define AMP_SD_PIN      38  // SD del amplificador PAM8302A (J3-11): ALTO = encendido. Solo en CW, LoRa y TRX

#define FREC_AUDIO_HZ    850 // Tono CW por defecto; se elige en CONF > CONFIG > TONO (frecTonoHz)
#define TONO_MIN_HZ      400
#define TONO_MAX_HZ      1000
#define TONO_PASO_HZ     50
#define TONO_PRUEBA_MS   300 // Pitido de prueba al cambiar el tono en CONF
#define CANAL_PWM_AUDIO  0  // Canal PWM del ESP32-S3
#define RES_PWM_AUDIO    8  // Resolucion de 8 bits para el PWM

#define VBAT_ADC_PIN    1   // Pin ADC para lectura del divisor de voltaje de la bateria
#define ADC_CTRL_PIN    37  // Habilitacion del divisor de tension de bateria
// Sin bateria y con USB, en estas placas el cargador deja una media de unos 4,17 V (como una bateria
// llena) pero oscila: medido con pruebas/prueba_bateria.cpp, dispersion de 0,25 V sin bateria frente
// a 0,002 V con bateria (rafaga de lecturas seguidas). Por eso se mira la dispersion, no la tension.
// El umbral de 2,6 V (de Meshtastic, que anota unos 2,2 V sin bateria) se deja por si acaso.
#define VBAT_SIN_BATERIA            2.6f
#define VBAT_DISPERSION_SIN_BATERIA 0.08f  // V: por encima, no hay bateria
#define VBAT_LECTURAS               200    // Lecturas seguidas por medida (unos 18 ms)
#define VBAT_MULTIPLIER 4.9f   // Divisor 390k/100k de la V3. Calibrado con polimetro: 4,10 V reales / 836,7 mV en el pin (2,5 dB)
#define TIEMPO_PULSACION_LARGA 600 // Umbral en ms para detectar pulsacion larga del boton PRG
#define TIEMPO_ANTIRREBOTE_MS  30  // Pulsaciones mas cortas se ignoran (rebotes)
#define TIEMPO_PULSACION_DORMIR 2500  // Solo sin encoder: PRG tan largo = dormir (la larga normal = ejecutar)

