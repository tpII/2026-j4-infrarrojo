/**
 * test_led_ir.ino
 * 
 * Test de hardware para verificar el encendido del LED emisor IR.
 * 
 * Conexión física directa sin jumpers:
 *   D3 -> Pata 'S' (Señal / Control)
 *   D4 -> Pata del medio (VCC simulado)
 *   D5 -> Pata '-' (GND simulado)
 */

#define PIN_SIGNAL  3   // S
#define PIN_VCC     4   // Medio
#define PIN_GND     5   // -

void setup() {
    Serial.begin(115200);
    Serial.println("Iniciando test de LED IR...");

    // Alimentación simulada
    pinMode(PIN_VCC, OUTPUT);
    digitalWrite(PIN_VCC, HIGH); // 5V

    pinMode(PIN_GND, OUTPUT);
    digitalWrite(PIN_GND, LOW);  // GND

    // Pin de señal
    pinMode(PIN_SIGNAL, OUTPUT);

    // También prendemos el LED integrado (D13) para saber que el loop corre
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    Serial.println("LED IR: ENCENDIDO");
    digitalWrite(PIN_SIGNAL, HIGH);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000);

    Serial.println("LED IR: APAGADO");
    digitalWrite(PIN_SIGNAL, LOW);
    digitalWrite(LED_BUILTIN, LOW);
    delay(1000);
}
