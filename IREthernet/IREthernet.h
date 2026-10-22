/**
 * IREthernet.h
 * 
 * Protocolo de acceso al medio IR tipo Ethernet (CSMA/CA).
 * Implementado sobre protocolo NEC usando la librería IRremote.
 * 
 * Ver docs/protocol.md para la especificación completa.
 * 
 * Uso básico:
 *   IREthernet.begin(MY_ADDR, SEND_PIN, RECV_PIN);
 *
 *   // Enviar (en loop o cuando sea necesario)
 *   uint8_t data[] = {0x01, 0x02};
 *   IREthernet.send(0x3, data, 2);
 *
 *   // Recibir (en loop)
 *   IRFrame frame;
 *   if (IREthernet.receive(frame)) {
 *       // procesar frame.payload[0..frame.len-1]
 *   }
 * 
 * Proyecto: TdP2 2026 — IREthernet
 */

#pragma once

#include <Arduino.h>

// ============================================================
// Constantes del protocolo
// ============================================================

/** Bytes de preamble (addr=0xAA, cmd=0x55) marcan inicio de trama IREthernet */
#define IRETH_PREAMBLE_ADDR  0xAA
#define IRETH_PREAMBLE_CMD   0x55

/** Byte final del frame en el campo cmd del último paquete */
#define IRETH_END_CMD        0xEE

/** Tipos de frame (4 bits, van en TYPE[3:0]) */
#define IRETH_TYPE_DATA    0x0   ///< Trama de datos (espera ACK)
#define IRETH_TYPE_ACK     0x1   ///< Confirmación de recepción
#define IRETH_TYPE_NAK     0x2   ///< Error en recepción
#define IRETH_TYPE_BEACON  0x3   ///< Anuncio de presencia (futuro)

/** Dirección especial de broadcast (todos los nodos) */
#define IRETH_ADDR_BROADCAST  0xF

/** Payload máximo en bytes */
#define IRETH_MAX_PAYLOAD  8

// ============================================================
// Parámetros CSMA/CA
// ============================================================

/** SIFS: tiempo mínimo entre trama y ACK (ms) */
#define IRETH_SIFS_MS         120

/** DIFS: período de escucha antes de transmitir (ms) */
#define IRETH_DIFS_MS         200

/** Slot time: unidad de backoff (ms) */
#define IRETH_SLOT_MS         100

/** Contention Window mínima (slots) */
#define IRETH_CW_MIN          2

/** Contention Window máxima (slots) */
#define IRETH_CW_MAX          16

/** Reintentos máximos antes de declarar error */
#define IRETH_MAX_RETRY       5

/** Timeout de espera de ACK tras transmitir (ms) */
#define IRETH_ACK_TIMEOUT_MS  2000

/** Gap entre paquetes NEC dentro de una trama (ms) */
#define IRETH_INTER_PKT_MS    100

// ============================================================
// Estructura de trama
// ============================================================

/**
 * Representación en memoria de una trama IREthernet.
 * Se construye y parsea a partir de secuencias de paquetes NEC.
 */
struct IRFrame {
    uint8_t dst;                        ///< Dirección destino (4 bits)
    uint8_t src;                        ///< Dirección origen (4 bits)
    uint8_t type;                       ///< Tipo de frame (4 bits)
    uint8_t seq;                        ///< Número de secuencia (4 bits)
    uint8_t len;                        ///< Longitud del payload (0–IRETH_MAX_PAYLOAD)
    uint8_t flags;                      ///< Flags de control (reservado)
    uint8_t payload[IRETH_MAX_PAYLOAD]; ///< Bytes de payload
    uint8_t checksum;                   ///< XOR checksum calculado
    bool    valid;                      ///< true si el frame fue parseado sin errores
};

// ============================================================
// Clase principal
// ============================================================

class IREthernetClass {
public:
    // ----------------------------------------------------------
    // Inicialización
    // ----------------------------------------------------------

    /**
     * Inicializa la librería.
     * @param myAddr   Dirección de este nodo (0x1 – 0xE; 0xF = broadcast)
     * @param sendPin  Pin conectado al emisor IR
     * @param recvPin  Pin conectado al receptor IR
     */
    void begin(uint8_t myAddr, uint8_t sendPin, uint8_t recvPin);

    // ----------------------------------------------------------
    // API de alto nivel
    // ----------------------------------------------------------

    /**
     * Envía datos a un nodo destino usando CSMA/CA con reintentos.
     * Bloquea hasta recibir ACK, agotar reintentos, o broadcast sin ACK.
     * @param dst      Dirección destino (0x1–0xE o IRETH_ADDR_BROADCAST)
     * @param data     Puntero al buffer de datos a enviar
     * @param len      Longitud del buffer (máx IRETH_MAX_PAYLOAD)
     * @return true si se envió correctamente (ACK recibido o broadcast)
     */
    bool send(uint8_t dst, uint8_t* data, uint8_t len);

    /**
     * Intenta recibir una trama (no bloqueante si no hay preamble).
     * Si detecta un preamble, bloquea hasta recibir la trama completa.
     * Envía ACK automáticamente si el frame es DATA dirigido a este nodo.
     * @param frame    Estructura a llenar con la trama recibida
     * @return true si se recibió una trama válida para este nodo (o broadcast)
     */
    bool receive(IRFrame& frame);

    // ----------------------------------------------------------
    // API de bajo nivel (también útil para debugging)
    // ----------------------------------------------------------

    /**
     * Envía un ACK hacia dst referenciando el número de secuencia seq.
     * Se llama automáticamente desde receive() para DATA frames.
     */
    void sendAck(uint8_t dst, uint8_t seq);

    /**
     * Escucha el canal durante listenMs milisegundos.
     * @return true si detectó actividad IR (canal ocupado)
     */
    bool isMediumBusy(uint16_t listenMs);

    // ----------------------------------------------------------
    // Estado público
    // ----------------------------------------------------------

    uint8_t myAddress;  ///< Dirección asignada a este nodo

// ============================================================
// Módulo de Deduplicación (Stop-and-Wait ARQ)
// ============================================================

#define IRETH_MAX_NODES 16

class FrameDeduplicator {
public:
    uint8_t last_seq[IRETH_MAX_NODES];
    bool    has_received[IRETH_MAX_NODES];

    FrameDeduplicator() {
        reset();
    }

    void reset() {
        for (int i = 0; i < IRETH_MAX_NODES; i++) {
            last_seq[i] = 0xFF; // Valor inválido
            has_received[i] = false;
        }
    }

    bool isDuplicate(uint8_t src, uint8_t seq) {
        if (src >= IRETH_MAX_NODES) return false;
        return (has_received[src] && last_seq[src] == (seq & 0x0F));
    }

    void updateSequence(uint8_t src, uint8_t seq) {
        if (src < IRETH_MAX_NODES) {
            last_seq[src] = seq & 0x0F;
            has_received[src] = true;
        }
    }
};

private:
    uint8_t _sendPin;
    uint8_t _recvPin;
    uint8_t _seq;   ///< Contador de secuencia (cicla 0–15)
    uint8_t _cw;    ///< Contention Window actual
    FrameDeduplicator _deduplicator;

    // -- Construcción y envío de tramas --

    /**
     * Construye una IRFrame en memoria.
     */
    void buildFrame(IRFrame& f, uint8_t dst, uint8_t type,
                    uint8_t seq, const uint8_t* data, uint8_t len);

    /**
     * Serializa y envía una IRFrame como secuencia de paquetes NEC.
     * Para el emisor: detiene el receptor antes de enviar y lo reinicia después.
     */
    bool sendFrame(const IRFrame& frame);

    // -- Recepción --

    /**
     * Recibe un único paquete NEC y extrae addr y cmd.
     * @param addr      Salida: byte address del paquete NEC
     * @param cmd       Salida: byte command del paquete NEC
     * @param timeoutMs Tiempo máximo de espera en ms (0 = no bloquear)
     * @return true si se recibió un paquete válido
     */
    bool recvNECPacket(uint8_t& addr, uint8_t& cmd, uint16_t timeoutMs);

    /**
     * Recibe el cuerpo de una trama DESPUÉS de que el preamble ya fue detectado.
     * @param frame      Estructura a llenar
     * @param timeoutMs  Timeout para cada paquete interno del frame
     * @return true si el frame es válido (checksum OK)
     */
    bool receiveFrameBody(IRFrame& frame, uint16_t timeoutMs);

    /**
     * Espera un ACK de un nodo específico con el número de secuencia indicado.
     * @return true si llegó el ACK esperado dentro del timeout
     */
    bool waitForAck(uint8_t seq, uint16_t timeoutMs);

    // -- Checksum --
    uint8_t computeChecksum(const IRFrame& f);
};

/** Instancia global (estilo Arduino, similar a Serial, Wire, etc.) */
extern IREthernetClass IREthernet;
