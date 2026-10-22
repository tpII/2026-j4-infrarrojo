/**
 * node_A.ino — Nodo 0x1 (Arduino o ESP32, según lo que tengas)
 *
 * Nodo genérico de la red IREthernet.
 * Todos los nodos son funcionalmente idénticos: emiten y reciben.
 * La única diferencia entre ellos es MY_ADDR.
 *
 * Pines (Arduino Uno/Nano):
 *   D3  → señal TX IR (emisor)
 *   D4  → VCC simulado módulo TX  (OUTPUT HIGH)
 *   D5  → GND simulado módulo TX  (OUTPUT LOW)
 *   D8  → señal RX IR (receptor)
 *   D9  → VCC simulado módulo RX  (OUTPUT HIGH)
 *   D10 → GND simulado módulo RX  (OUTPUT LOW)
 *
 * Pines (ESP32 Wemos D1 R32) — si lo usás como nodo A:
 *   IO17 → señal TX IR (DAT)
 *   IO25 → VCC simulado módulo TX (OUTPUT HIGH)
 *   IO26 → GND simulado módulo TX (OUTPUT LOW)
 *   IO16 → señal RX IR (OUT)
 *   IO27 → VCC simulado módulo RX (OUTPUT HIGH)
 *   IO14 → GND simulado módulo RX (OUTPUT LOW)
 *
 * Comandos Serial Monitor (115200 baud):
 *   B           → broadcast a todos los nodos
 *   2           → enviar frame a nodo 0x2
 *   3           → enviar frame a nodo 0x3
 *   1           → enviar frame a nodo 0x1 (loopback, para test)
 *   A           → enviar ráfaga: broadcast + unicast a 0x2 y 0x3
 *                 (fuerza colisiones si los otros nodos también transmiten)
 *   ?           → mostrar menú
 *
 * Proyecto: TdP2 2026 — IREthernet
 */

#include <IREthernet.h>

// ============================================================
// SELECCIONAR PLATAFORMA: descomentar la correcta
// ============================================================

// -- Arduino Uno / Nano --
#define IR_SEND_PIN   3    // S (Señal TX por hardware)
#define TX_VCC_PIN    4    // Medio (VCC - HIGH)
#define TX_GND_PIN    5    // - (GND - LOW)

#define IR_RECV_PIN   8    // Y (Señal RX)
#define RX_VCC_PIN    9    // R (VCC - HIGH)
#define RX_GND_PIN    10   // G (GND - LOW)

#define SETUP_SIMULATED_POWER() \
    do { \
        pinMode(TX_VCC_PIN, OUTPUT); digitalWrite(TX_VCC_PIN, HIGH); \
        pinMode(TX_GND_PIN, OUTPUT); digitalWrite(TX_GND_PIN, LOW);  \
        pinMode(RX_VCC_PIN, OUTPUT); digitalWrite(RX_VCC_PIN, HIGH); \
        pinMode(RX_GND_PIN, OUTPUT); digitalWrite(RX_GND_PIN, LOW);  \
    } while(0)

// -- ESP32 Wemos D1 R32 --
// (descomentar y comentar el bloque Arduino si usás ESP32 acá)
// #define IR_RECV_PIN   16
// #define IR_SEND_PIN   17
// #define SETUP_SIMULATED_POWER()   // noop: VCC/GND van a pines físicos del board

// ============================================================
// Dirección de ESTE nodo — cambiar por 0x2 o 0x3 en los otros sketches
// ============================================================
#define MY_ADDR  0x1

// ============================================================
// Payload de aplicación (PoC de protocolo — sin lógica de negocio)
// ============================================================

// ============================================================
// Tipos de mensaje de la capa de aplicación (van en payload[0])
// ============================================================
#define APP_PING       0x01  ///< "estoy vivo"
#define APP_PONG       0x02  ///< respuesta a PING
#define APP_DATA       0x03  ///< datos genéricos (payload[1] = contador)
#define APP_FRAG_CONT  0x40  ///< fragmento de string (continúa)
#define APP_FRAG_END   0x41  ///< último fragmento de string (fin)

// ============================================================
// Estado de aplicación
// ============================================================
static uint8_t txCounter = 0;          ///< contador de transmisiones simples
static String  customMessage = "";     ///< string custom a enviar si está seteado

// Buffer de reensamblado en recepción
#define MAX_MSG_BUF 256
static char    rxMsgBuffer[MAX_MSG_BUF];
static uint16_t rxMsgLen = 0;

// Buffer de lectura por Serial
static String  serialLineBuf = "";

// ============================================================
// Helpers de visualización y menú
// ============================================================

void printMenu() {
    Serial.println(F(""));
    Serial.println(F("=== IREthernet Nodo 0x1 ==="));
    Serial.println(F("  B       → broadcast"));
    Serial.println(F("  2       → unicast a 0x2"));
    Serial.println(F("  3       → unicast a 0x3"));
    Serial.println(F("  A       → ráfaga (fuerza colisiones)"));
    Serial.println(F("  M <txt> → setear mensaje (ej: M HOLA MUNDO)"));
    Serial.println(F("  C       → borrar mensaje (vuelve a contador)"));
    Serial.println(F("  ?       → este menú"));
    if (customMessage.length() > 0) {
        Serial.print(F("  [Mensaje activo]: \""));
        Serial.print(customMessage);
        Serial.println(F("\""));
    } else {
        Serial.println(F("  [Mensaje activo]: ninguno (modo contador)"));
    }
    Serial.println(F("==========================="));
}

/**
 * Envía un string largo fragmentándolo en tramas de hasta 8 bytes
 * (1 byte de control APP_FRAG + hasta 7 bytes de texto por trama).
 */
bool sendStringFragmented(uint8_t dst, const String& text) {
    uint8_t totalLen = text.length();
    uint8_t offset = 0;
    uint8_t fragIndex = 0;

    Serial.print(F("[APP-TX] Iniciando envío de string ("));
    Serial.print(totalLen);
    Serial.print(F(" bytes) a dst=0x"));
    Serial.println(dst, HEX);

    while (offset < totalLen) {
        uint8_t chunk = totalLen - offset;
        if (chunk > 7) chunk = 7; // máx 7 bytes útiles por trama de 8 bytes

        bool isLast = (offset + chunk >= totalLen);
        uint8_t payload[8];
        payload[0] = isLast ? APP_FRAG_END : APP_FRAG_CONT;

        for (uint8_t i = 0; i < chunk; i++) {
            payload[1 + i] = (uint8_t)text.charAt(offset + i);
        }

        Serial.print(F("[APP-TX] Fragmento "));
        Serial.print(fragIndex);
        Serial.print(F(" ("));
        Serial.print(chunk);
        Serial.print(F(" bytes) ["));
        Serial.print(isLast ? F("END") : F("CONT"));
        Serial.print(F("]: \""));
        for (uint8_t i = 0; i < chunk; i++) {
            Serial.print((char)payload[1 + i]);
        }
        Serial.println(F("\""));

        bool ok = IREthernet.send(dst, payload, 1 + chunk);
        if (!ok) {
            Serial.print(F("[APP-TX] !! Falló envío en fragmento "));
            Serial.println(fragIndex);
            return false;
        }

        offset += chunk;
        fragIndex++;
        if (!isLast) {
            delay(50); // breve pausa entre fragmentos
        }
    }

    Serial.println(F("[APP-TX] ✅ String completo enviado y confirmado!"));
    return true;
}

/**
 * Envía el mensaje actual (custom string o contador de 1 byte).
 */
bool sendMessage(uint8_t dst) {
    if (customMessage.length() > 0) {
        return sendStringFragmented(dst, customMessage);
    }

    uint8_t payload[2] = { APP_DATA, txCounter++ };
    Serial.print(F("[APP] TX → dst=0x"));
    Serial.print(dst, HEX);
    Serial.print(F(" contador="));
    Serial.println(payload[1]);

    bool ok = IREthernet.send(dst, payload, 2);
    if (!ok) {
        Serial.println(F("[APP] !! TX fallida (sin ACK o max reintentos)"));
    }
    return ok;
}

/**
 * Responde con PONG a un PING recibido.
 */
void handlePing(uint8_t srcAddr) {
    Serial.print(F("[APP] PING de 0x"));
    Serial.println(srcAddr, HEX);

    uint8_t payload[1] = { APP_PONG };
    delay(50);
    IREthernet.send(srcAddr, payload, 1);
}

/**
 * Procesa un frame recibido y realiza el reensamblado si es fragmento.
 */
void handleFrame(const IRFrame& frame) {
    Serial.print(F("[APP] RX ← src=0x"));
    Serial.print(frame.src, HEX);
    Serial.print(F(" seq="));
    Serial.print(frame.seq);
    Serial.print(F(" len="));
    Serial.print(frame.len);
    Serial.print(F(" HEX: "));
    for (uint8_t i = 0; i < frame.len; i++) {
        if (frame.payload[i] < 0x10) Serial.print('0');
        Serial.print(frame.payload[i], HEX);
        Serial.print(' ');
    }
    Serial.println();

    if (frame.len == 0) return;

    uint8_t type = frame.payload[0];

    // Manejo de fragmentos de texto
    if (type == APP_FRAG_CONT || type == APP_FRAG_END) {
        uint8_t textBytes = frame.len - 1;
        for (uint8_t i = 0; i < textBytes; i++) {
            if (rxMsgLen < MAX_MSG_BUF - 1) {
                rxMsgBuffer[rxMsgLen++] = (char)frame.payload[1 + i];
            }
        }

        if (type == APP_FRAG_END) {
            rxMsgBuffer[rxMsgLen] = '\0'; // terminador nulo

            Serial.println(F("---------------------------------------------"));
            Serial.print(F("📩 [MENSAJE REENSAMBLADO] De nodo 0x"));
            Serial.print(frame.src, HEX);
            Serial.print(F(" ("));
            Serial.print(rxMsgLen);
            Serial.println(F(" bytes):"));
            
            Serial.print(F("   HEX:   "));
            for (uint8_t i = 0; i < rxMsgLen; i++) {
                if ((uint8_t)rxMsgBuffer[i] < 0x10) Serial.print('0');
                Serial.print((uint8_t)rxMsgBuffer[i], HEX);
                Serial.print(' ');
            }
            Serial.println();

            Serial.print(F("   ASCII: \""));
            Serial.print(rxMsgBuffer);
            Serial.println(F("\""));
            Serial.println(F("---------------------------------------------"));

            rxMsgLen = 0; // reset buffer
        } else {
            Serial.print(F("   [Fragmento parcial acumulado: "));
            Serial.print(rxMsgLen);
            Serial.println(F(" bytes]"));
        }
        return;
    }

    // Ping / Pong / Contador simple
    if (type == APP_PING) {
        handlePing(frame.src);
    } else if (type == APP_DATA) {
        Serial.print(F("   [Dato simple] Contador: "));
        if (frame.len > 1) Serial.println(frame.payload[1]);
        else Serial.println(F("N/A"));
    }
}

// ============================================================
// Procesamiento de comandos serie
// ============================================================

void processCommand(const String& cmd) {
    if (cmd.length() == 0) return;

    char firstChar = cmd.charAt(0);

    if (firstChar == 'M' || firstChar == 'm') {
        if (cmd.length() > 2 && cmd.charAt(1) == ' ') {
            customMessage = cmd.substring(2);
        } else if (cmd.length() > 1) {
            customMessage = cmd.substring(1);
        }
        Serial.print(F("[APP] Mensaje seteado: \""));
        Serial.print(customMessage);
        Serial.print(F("\" ("));
        Serial.print(customMessage.length());
        Serial.println(F(" bytes)"));
        return;
    }

    if (firstChar == 'C' || firstChar == 'c') {
        customMessage = "";
        Serial.println(F("[APP] Mensaje borrado. Modo contador activado."));
        return;
    }

    if (firstChar == 'B' || firstChar == 'b') {
        Serial.println(F("[APP] Enviando BROADCAST..."));
        sendMessage(IRETH_ADDR_BROADCAST);
    } else if (firstChar == '2') {
        Serial.println(F("[APP] Enviando a nodo 0x2..."));
        sendMessage(0x2);
    } else if (firstChar == '3') {
        Serial.println(F("[APP] Enviando a nodo 0x3..."));
        sendMessage(0x3);
    } else if (firstChar == '1') {
        Serial.println(F("[APP] Enviando a nodo 0x1 (loopback)..."));
        sendMessage(0x1);
    } else if (firstChar == 'A' || firstChar == 'a') {
        Serial.println(F("[APP] Rafaga: broadcast + 0x2 + 0x3"));
        sendMessage(IRETH_ADDR_BROADCAST);
        sendMessage(0x2);
        sendMessage(0x3);
    } else if (firstChar == '?') {
        printMenu();
    } else {
        Serial.print(F("[APP] Comando desconocido: '"));
        Serial.print(cmd);
        Serial.println(F("'. Usa '?' para ayuda."));
    }
}

// ============================================================
// Setup
// ============================================================

void setup() {
    Serial.begin(115200);
    while (!Serial) {}

    Serial.println(F("============================"));
    Serial.println(F("  IREthernet — Nodo 0x1     "));
    Serial.println(F("============================"));

    SETUP_SIMULATED_POWER();
    IREthernet.begin(MY_ADDR, IR_SEND_PIN, IR_RECV_PIN);

    printMenu();
}

// ============================================================
// Loop
// ============================================================

void loop() {
    // --- Recibir frames ---
    IRFrame frame;
    if (IREthernet.receive(frame)) {
        if (frame.type == IRETH_TYPE_DATA) {
            handleFrame(frame);
        }
    }

    // --- Lectura de comandos por Serial ---
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            serialLineBuf.trim();
            if (serialLineBuf.length() > 0) {
                processCommand(serialLineBuf);
                serialLineBuf = "";
            }
        } else if (c == '\b' || (uint8_t)c == 0x7F) {
            // Manejar tecla Backspace / Delete
            if (serialLineBuf.length() > 0) {
                serialLineBuf.remove(serialLineBuf.length() - 1);
            }
        } else {
            serialLineBuf += c;
        }
    }
}
