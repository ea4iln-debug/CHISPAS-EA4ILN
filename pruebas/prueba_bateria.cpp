/**
 * Prueba de la lectura de bateria: ¿se puede saber si hay bateria cuando hay USB?
 * Con USB y sin bateria el cargador deja unos 4,2 V en el nodo de la bateria (medido: 4,19 V),
 * igual que con una bateria llena. Esta prueba busca otra diferencia: si sin bateria el
 * cargador oscila, la tension se movera mucho mas que con bateria.
 *
 * Cada 3 s imprime dos medidas (en V de bateria, con el factor calibrado 4,9):
 *   LENTA: 500 lecturas en unos 2 s (oscilaciones de milisegundos a segundos)
 *   RAPIDA: 500 lecturas seguidas, sin pausa (oscilaciones de microsegundos)
 * Se carga con: pio run -e prueba_bateria -t upload   (monitor a 115200)
 */
#include <Arduino.h>

#define VBAT_ADC_PIN    1
#define ADC_CTRL_PIN    37    // En estas placas el divisor se activa en ALTO
#define VBAT_MULTIPLIER 4.9f
#define NUM_LECTURAS    500

uint16_t lecturas[NUM_LECTURAS];

void resumir(const char* nombre, unsigned long duracionUs) {
  uint32_t suma = 0;
  uint16_t minimo = 0xFFFF, maximo = 0;
  for (int i = 0; i < NUM_LECTURAS; i++) {
    suma += lecturas[i];
    minimo = min(minimo, lecturas[i]);
    maximo = max(maximo, lecturas[i]);
  }
  float media = (float)suma / NUM_LECTURAS;
  float var = 0;
  for (int i = 0; i < NUM_LECTURAS; i++) var += (lecturas[i] - media) * (lecturas[i] - media);
  float desv = sqrtf(var / NUM_LECTURAS);

  // Cruces de la media con histeresis de 2 desviaciones: cuenta oscilaciones claras, no ruido
  int cruces = 0;
  int lado = 0;
  for (int i = 0; i < NUM_LECTURAS; i++) {
    if (lecturas[i] > media + 2 * desv && lado <= 0) { if (lado < 0) cruces++; lado = 1; }
    else if (lecturas[i] < media - 2 * desv && lado >= 0) { if (lado > 0) cruces++; lado = -1; }
  }

  const float k = VBAT_MULTIPLIER / 1000.0f;
  Serial.printf("%-6s %5lu ms | media %.3f V | min %.3f | max %.3f | pico-pico %.3f V | desv %.4f V | cruces %d\n",
                nombre, duracionUs / 1000, media * k, minimo * k, maximo * k, (maximo - minimo) * k, desv * k, cruces);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println(F("\n=== PRUEBA DE LECTURA DE BATERIA ==="));
  pinMode(ADC_CTRL_PIN, OUTPUT);
  digitalWrite(ADC_CTRL_PIN, HIGH);
  analogReadResolution(12);
  analogSetPinAttenuation(VBAT_ADC_PIN, ADC_2_5db);
  delay(50);
}

void loop() {
  unsigned long t0 = micros();
  for (int i = 0; i < NUM_LECTURAS; i++) {
    lecturas[i] = analogReadMilliVolts(VBAT_ADC_PIN);
    delayMicroseconds(4000);
  }
  resumir("LENTA", micros() - t0);

  t0 = micros();
  for (int i = 0; i < NUM_LECTURAS; i++) lecturas[i] = analogReadMilliVolts(VBAT_ADC_PIN);
  resumir("RAPIDA", micros() - t0);

  delay(1000);
}
