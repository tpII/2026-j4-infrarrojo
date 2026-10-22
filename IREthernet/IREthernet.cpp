/**
 * IREthernet.cpp
 * 
 * Implementación del protocolo CSMA/CA sobre IR NEC.
 * Ver IREthernet.h y docs/protocol.md para la especificación.
 * 
 * Proyecto: TdP2 2026 — IREthernet
 */

#include "IREthernet.h"

#define SEND_NEC
#include <IRremote.hpp>

// Instancia global
IREthernetClass IREthernet;

// ============================================================
// Inicialización
// ============================================================

void IREthernetClass::begin(uint8_t myAddr, uint8_t sendPin, uint8_t recvPin) {
    myAddress = myAddr & 0xF;
    _sendPin  = sendPin;
    _recvPin  = recvPin;
    _seq      = 0;
    _cw       = IRETH_CW_MIN;
    _deduplicator.reset();

    IrSender.begin(sendPin, DISABLE_LED_FEEDBACK);
    IrReceiver.begin(recvPin, ENABLE_LED_FEEDBACK);

    // Seed del generador de números aleatorios para el backoff
    // Usamos el pin analógico A0 (ruido de ADC como entropía)
    randomSeed(analogRead(A0) + myAddr * 137);

    Serial.print(F("[IRETH] Iniciado. addr=0x"));
    Serial.print(myAddress, HEX);
    Serial.print(F(" sendPin="));
    Serial.print(sendPin);
    Serial.print(F(" recvPin="));
    Serial.println(recvPin);
}

// ============================================================
// Checksum
// ============================================================

uint8_t IREthernetClass::computeChecksum(const IRFrame& f) {
    uint8_t cs = f.dst ^ f.src ^ f.type ^ f.seq ^ f.len ^ f.flags;
    for (uint8_t i = 0; i < f.len; i++) {
        cs ^= f.payload[i];
    }
    return cs;
}

// ============================================================
// Detección de medio ocupado
// ============================================================

bool IREthernetClass::isMediumBusy(uint16_t listenMs) {
    unsigned long start = millis();
    while (millis() - start < listenMs) {
        if (IrReceiver.decode()) {
            uint32_t raw = IrReceiver.decodedIRData.decodedRawData;
            IrReceiver.resume();
            if (raw != 0) {
                // Cualquier señal IR válida indica que el medio está ocupado
                return true;
            }
        }
    }
    return false; // silencio durante listenMs → canal libre
}

// ============================================================
// Construcción de trama
// ============================================================

void IREthernetClass::buildFrame(IRFrame& f, uint8_t dst, uint8_t type,
                                  uint8_t seq, const uint8_t* data, uint8_t len) {
    f.dst   = dst & 0xF;
    f.src   = myAddress & 0xF;
    f.type  = type & 0xF;
    f.seq   = seq & 0xF;
    f.len   = (len > IRETH_MAX_PAYLOAD) ? IRETH_MAX_PAYLOAD : len;
    f.flags = 0x00;
    f.valid = true;

    for (uint8_t i = 0; i < f.len; i++) {
        f.payload[i] = (data != nullptr) ? data[i] : 0x00;
    }

    f.checksum = computeChecksum(f);
}

// ============================================================
// Envío de trama (serialización NEC)
// ============================================================

bool IREthernetClass::sendFrame(const IRFrame& frame) {
    // Detener receptor para evitar autorecepción durante el envío
    IrReceiver.stop();
    delay(IRETH_INTER_PKT_MS);

    // Paquete 0: PREAMBLE (0xAA / 0x55)
    IrSender.sendNEC(IRETH_PREAMBLE_ADDR, IRETH_PREAMBLE_CMD, 0);
    delay(IRETH_INTER_PKT_MS);

    // Paquete 1: DST|SRC (addr), TYPE|SEQ (cmd)
    uint8_t addr1 = (uint8_t)((frame.dst << 4) | (frame.src & 0x0F));
    uint8_t cmd1  = (uint8_t)((frame.type << 4) | (frame.seq & 0x0F));
    IrSender.sendNEC(addr1, cmd1, 0);
    delay(IRETH_INTER_PKT_MS);

    // Paquete 2: LEN (addr), FLAGS (cmd)
    IrSender.sendNEC(frame.len, frame.flags, 0);
    delay(IRETH_INTER_PKT_MS);

    // Paquetes de payload (2 bytes por paquete NEC)
    uint8_t i = 0;
    while (i < frame.len) {
        uint8_t b0 = frame.payload[i++];
        uint8_t b1 = (i < frame.len) ? frame.payload[i++] : 0x00;
        IrSender.sendNEC(b0, b1, 0);
        delay(IRETH_INTER_PKT_MS);
    }

    // Último paquete: CHECKSUM (addr), END=0xEE (cmd)
    IrSender.sendNEC(frame.checksum, IRETH_END_CMD, 0);

    // Reiniciar y limpiar el receptor para escuchar inmediatamente
    delay(20);
    IrReceiver.start();
    IrReceiver.resume();

    return true;
}

// ============================================================
// Recepción de un paquete NEC
// ============================================================

bool IREthernetClass::recvNECPacket(uint8_t& addr, uint8_t& cmd, uint16_t timeoutMs) {
    unsigned long start = millis();
    do {
        if (IrReceiver.decode()) {
            uint32_t raw      = IrReceiver.decodedIRData.decodedRawData;
            decode_type_t proto = IrReceiver.decodedIRData.protocol;

            if (raw != 0 && (proto == NEC || proto == ONKYO)) {
                addr = (uint8_t)(IrReceiver.decodedIRData.address & 0xFF);
                cmd  = (uint8_t)(IrReceiver.decodedIRData.command  & 0xFF);
                IrReceiver.resume();
                return true;
            }
            IrReceiver.resume();
        }
    } while (millis() - start < timeoutMs);

    return false;
}

// ============================================================
// Recepción del cuerpo de trama (después del preamble)
// ============================================================

bool IREthernetClass::receiveFrameBody(IRFrame& frame, uint16_t timeoutMs) {
    uint8_t addr, cmd;

    // Paquete 1: DST|SRC, TYPE|SEQ
    if (!recvNECPacket(addr, cmd, timeoutMs)) {
        Serial.println(F("[IRETH] Timeout esperando addr/type pkt"));
        return false;
    }
    frame.dst  = (addr >> 4) & 0x0F;
    frame.src  = addr & 0x0F;
    frame.type = (cmd >> 4) & 0x0F;
    frame.seq  = cmd & 0x0F;

    // Paquete 2: LEN, FLAGS
    if (!recvNECPacket(addr, cmd, timeoutMs)) {
        Serial.println(F("[IRETH] Timeout esperando len/flags pkt"));
        return false;
    }
    frame.len   = addr;
    frame.flags = cmd;

    // Sanity check de longitud
    if (frame.len > IRETH_MAX_PAYLOAD) {
        Serial.print(F("[IRETH] LEN invalido: "));
        Serial.println(frame.len);
        frame.valid = false;
        return false;
    }

    // Paquetes de payload (2 bytes por paquete)
    uint8_t i = 0;
    while (i < frame.len) {
        if (!recvNECPacket(addr, cmd, timeoutMs)) {
            Serial.println(F("[IRETH] Timeout en payload pkt"));
            return false;
        }
        frame.payload[i++] = addr;
        if (i < frame.len) {
            frame.payload[i++] = cmd;
        }
    }

    // Último paquete: CHECKSUM, END (debe ser 0xEE)
    if (!recvNECPacket(addr, cmd, timeoutMs)) {
        Serial.println(F("[IRETH] Timeout esperando checksum pkt"));
        return false;
    }
    if (cmd != IRETH_END_CMD) {
        Serial.print(F("[IRETH] END marker invalido: 0x"));
        Serial.println(cmd, HEX);
        frame.valid = false;
        return false;
    }

    uint8_t receivedCs = addr;
    frame.checksum = computeChecksum(frame);

    if (receivedCs != frame.checksum) {
        Serial.print(F("[IRETH] Checksum error! recibido=0x"));
        Serial.print(receivedCs, HEX);
        Serial.print(F(" calculado=0x"));
        Serial.println(frame.checksum, HEX);
        frame.valid = false;
        return false;
    }

    frame.valid = true;
    return true;
}

// ============================================================
// receive() — punto de entrada en el loop del sketch
// ============================================================

bool IREthernetClass::receive(IRFrame& frame) {
    // Verificar si hay un paquete NEC disponible (no bloquea)
    if (!IrReceiver.decode()) {
        return false;
    }

    uint32_t raw      = IrReceiver.decodedIRData.decodedRawData;
    decode_type_t proto = IrReceiver.decodedIRData.protocol;
    uint8_t  addr     = (uint8_t)(IrReceiver.decodedIRData.address & 0xFF);
    uint8_t  cmd      = (uint8_t)(IrReceiver.decodedIRData.command  & 0xFF);

    Serial.print(F("[DEBUG RX] proto="));
    Serial.print((int)proto);
    Serial.print(F(" raw=0x"));
    Serial.print(raw, HEX);
    Serial.print(F(" addr=0x"));
    Serial.print(addr, HEX);
    Serial.print(F(" cmd=0x"));
    Serial.println(cmd, HEX);

    IrReceiver.resume();

    // Descartar paquetes que no sean NEC/ONKYO válidos o que no sean nuestro preamble
    if (raw == 0 || (proto != NEC && proto != ONKYO)) {
        return false;
    }
    if (addr != IRETH_PREAMBLE_ADDR || cmd != IRETH_PREAMBLE_CMD) {
        return false;
    }

    // Preamble detectado: recibir el resto de la trama (bloqueante, con timeout)
    if (!receiveFrameBody(frame, 1000)) {
        return false;
    }

    // Filtrar frames no dirigidos a este nodo (excepto broadcast)
    if (frame.dst != myAddress && frame.dst != IRETH_ADDR_BROADCAST) {
        // Frame válido pero para otro nodo — lo descartamos silenciosamente
        // (útil para modo promiscuo: podría loguear aquí en vez de descartar)
        Serial.print(F("[IRETH] Frame para otro nodo (dst=0x"));
        Serial.print(frame.dst, HEX);
        Serial.println(F("), ignorando"));
        return false;
    }

    // Log
    Serial.print(F("[IRETH] Frame recibido: src=0x"));
    Serial.print(frame.src, HEX);
    Serial.print(F(" dst=0x"));
    Serial.print(frame.dst, HEX);
    Serial.print(F(" type="));
    Serial.print(frame.type);
    Serial.print(F(" seq="));
    Serial.print(frame.seq);
    Serial.print(F(" len="));
    Serial.println(frame.len);

    // ACK automático para DATA frames dirigidos a este nodo (no broadcast)
    if (frame.type == IRETH_TYPE_DATA && frame.dst != IRETH_ADDR_BROADCAST) {
        sendAck(frame.src, frame.seq);

        // Deduplicación Stop-and-Wait ARQ
        if (_deduplicator.isDuplicate(frame.src, frame.seq)) {
            Serial.print(F("[IRETH] ⚠️ Trama duplicada descartada (src=0x"));
            Serial.print(frame.src, HEX);
            Serial.print(F(" seq="));
            Serial.print(frame.seq);
            Serial.println(F(") - ACK reenviado"));
            return false; // Descartar de la capa de aplicación
        }

        // Trama nueva: registrar secuencia
        _deduplicator.updateSequence(frame.src, frame.seq);
    }

    return true;
}

// ============================================================
// Envío de ACK
// ============================================================

void IREthernetClass::sendAck(uint8_t dst, uint8_t seq) {
    delay(IRETH_SIFS_MS); // esperar SIFS antes de responder

    IRFrame ack;
    buildFrame(ack, dst, IRETH_TYPE_ACK, seq, nullptr, 0);

    Serial.print(F("[IRETH] Enviando ACK seq="));
    Serial.print(seq);
    Serial.print(F(" a dst=0x"));
    Serial.println(dst, HEX);

    sendFrame(ack);
}

// ============================================================
// Espera de ACK
// ============================================================

bool IREthernetClass::waitForAck(uint8_t seq, uint16_t timeoutMs) {
    unsigned long start = millis();

    while (millis() - start < timeoutMs) {
        // Esperar un preamble IREthernet
        uint8_t addr, cmd;
        if (!recvNECPacket(addr, cmd, timeoutMs - (uint16_t)(millis() - start))) {
            break;
        }
        if (addr != IRETH_PREAMBLE_ADDR || cmd != IRETH_PREAMBLE_CMD) {
            continue; // no es un preamble IREthernet
        }

        Serial.println(F("[DEBUG TX] Preamble detectado mientras esperaba ACK"));

        // Recibir el cuerpo del frame
        IRFrame response;
        if (!receiveFrameBody(response, 1000)) {
            Serial.println(F("[DEBUG TX] Fallo recibir cuerpo de respuesta ACK"));
            continue;
        }

        // Verificar que es el ACK que esperamos
        if (response.type == IRETH_TYPE_ACK &&
            response.dst  == myAddress &&
            response.seq  == (seq & 0x0F)) {
            return true;
        } else {
            Serial.print(F("[DEBUG TX] Frame recibido no coincide: type="));
            Serial.print(response.type);
            Serial.print(F(" dst=0x"));
            Serial.print(response.dst, HEX);
            Serial.print(F(" seq="));
            Serial.println(response.seq);
        }
    }
    return false;
}

// ============================================================
// send() — CSMA/CA con backoff exponencial
// ============================================================

bool IREthernetClass::send(uint8_t dst, uint8_t* data, uint8_t len) {
    _cw = IRETH_CW_MIN;

    for (uint8_t retry = 0; retry < IRETH_MAX_RETRY; retry++) {

        // --- Carrier Sense: escuchar DIFS ms ---
        if (isMediumBusy(IRETH_DIFS_MS)) {
            // Canal ocupado → backoff aleatorio
            uint16_t slots   = (uint16_t)random(0, _cw);
            uint16_t backoff = slots * IRETH_SLOT_MS;

            Serial.print(F("[IRETH] Canal ocupado. Backoff "));
            Serial.print(backoff);
            Serial.print(F(" ms (CW="));
            Serial.print(_cw);
            Serial.println(F(")"));

            delay(backoff);

            // Doblar CW (Binary Exponential Backoff)
            if (_cw < IRETH_CW_MAX) _cw *= 2;
            continue; // volver a escuchar
        }

        // --- Canal libre: construir y enviar trama ---
        IRFrame frame;
        buildFrame(frame, dst, IRETH_TYPE_DATA, _seq & 0x0F, data, len);

        Serial.print(F("[IRETH] TX: dst=0x"));
        Serial.print(dst, HEX);
        Serial.print(F(" seq="));
        Serial.print(_seq & 0x0F);
        Serial.print(F(" len="));
        Serial.print(len);
        Serial.print(F(" intento="));
        Serial.println(retry + 1);

        sendFrame(frame);

        // --- Broadcast: no necesita ACK ---
        if (dst == IRETH_ADDR_BROADCAST) {
            _seq++;
            _cw = IRETH_CW_MIN;
            Serial.println(F("[IRETH] Broadcast enviado OK"));
            return true;
        }

        // --- Esperar ACK ---
        if (waitForAck(_seq & 0x0F, IRETH_ACK_TIMEOUT_MS)) {
            Serial.println(F("[IRETH] ACK recibido. TX exitoso"));
            _seq++;
            _cw = IRETH_CW_MIN;
            return true;
        }

        // --- Sin ACK: posible colisión o frame perdido ---
        Serial.print(F("[IRETH] Sin ACK (timeout). Reintento "));
        Serial.println(retry + 1);
        if (_cw < IRETH_CW_MAX) _cw *= 2;
    }

    Serial.println(F("[IRETH] ERROR: Max reintentos alcanzados. TX fallido"));
    return false;
}
