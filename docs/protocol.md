# Protocolo IREthernet — Especificación

## Resumen

**IREthernet** es un protocolo de capa de enlace de datos diseñado para comunicación  
multi-nodo sobre canal infrarrojo. Usa el protocolo NEC como capa física (modulación  
de portadora a 38 kHz) y define una trama propia transportada como secuencia de  
múltiples paquetes NEC consecutivos.

El protocolo implementa **CSMA/CA** (Carrier Sense Multiple Access with Collision Avoidance),  
el mismo mecanismo que usa IEEE 802.11 (WiFi), adaptado a las características del canal IR.

---

## Capa física: NEC sobre IR

### Paquete NEC estándar

Cada paquete NEC transmite 32 bits en ~67 ms:

```
Bit layout NEC:
┌────────────────┬────────────────┬─────────────┬─────────────────┐
│ Address (8b)   │ ~Address (8b)  │ Command (8b)│ ~Command (8b)   │
└────────────────┴────────────────┴─────────────┴─────────────────┘
                                                    67 ms total
```

IREthernet usa los campos **Address** y **Command** para transportar 2 bytes útiles por paquete.  
Los campos complementados (`~Address`, `~Command`) son manejados automáticamente por la  
librería `IRremote`.

### Timing

| Evento | Duración |
|--------|----------|
| Un paquete NEC completo | ~67 ms |
| Gap entre paquetes NEC dentro de una trama | 100 ms |
| Trama de 5 paquetes (4 bytes payload) | ~835 ms |
| Trama ACK (3 paquetes, 0 bytes payload) | ~501 ms |

---

## Formato de trama IREthernet

Una trama se compone de N paquetes NEC consecutivos. Cada paquete transporta 2 bytes:  
`addr` (byte alto) y `cmd` (byte bajo).

```
Paquete 0: PREAMBLE
  addr = 0xAA
  cmd  = 0x55
  → Marca inicio de trama IREthernet

Paquete 1: ADDR/TYPE
  addr = (DST[3:0] << 4) | SRC[3:0]
  cmd  = (TYPE[3:0] << 4) | SEQ[3:0]
  → Direcciones origen/destino + tipo de frame + número de secuencia

Paquete 2: LEN/FLAGS
  addr = LEN       (longitud del payload, 0–8 bytes)
  cmd  = FLAGS     (bits de control, reservado para futuro uso)

Paquetes 3..N: PAYLOAD (2 bytes por paquete)
  addr = payload[2*i]
  cmd  = payload[2*i + 1]   (o 0x00 si el payload tiene longitud impar)

Paquete N+1: CHECKSUM
  addr = XOR checksum de todos los bytes de la trama
  cmd  = 0xEE   (marcador de fin de trama)
```

### Ejemplo: DATA frame de Nodo 0x1 a Nodo 0x3, payload = [0x80, 0x01]

```
Pkt 0: addr=0xAA, cmd=0x55   ← PREAMBLE
Pkt 1: addr=0x31, cmd=0x02   ← DST=0x3, SRC=0x1, TYPE=DATA(0x0), SEQ=2
Pkt 2: addr=0x02, cmd=0x00   ← LEN=2, FLAGS=0
Pkt 3: addr=0x80, cmd=0x01   ← payload[0]=0x80, payload[1]=0x01
Pkt 4: addr=0xAA^0x31^0x02^0x02^0x00^0x80^0x01 = XOR, cmd=0xEE  ← CHECKSUM/END
```

---

## Campos de la trama

### Direcciones (4 bits)

| Valor | Nodo |
|-------|------|
| `0x1` | Nodo A (Arduino Uno/Nano) |
| `0x2` | Nodo B (Arduino Uno/Nano) |
| `0x3` | Nodo C (ESP32 Wemos D1 R32) |
| `0xF` | Broadcast (todos los nodos) |
| `0x0` | Reservado |
| `0x4`–`0xE` | Disponibles para expansión |

### Tipos de frame (4 bits)

| Valor | Tipo | Descripción |
|-------|------|-------------|
| `0x0` | `DATA` | Trama de datos, espera ACK |
| `0x1` | `ACK` | Confirmación de recepción |
| `0x2` | `NAK` | Error en recepción (checksum inválido) |
| `0x3` | `BEACON` | Anuncio de presencia (futuro) |

### Número de secuencia (4 bits)

Rango 0–15, cicla automáticamente. En Stop-and-Wait (PoC actual) solo se usa para  
que el ACK referencie el frame correcto. El receptor descarta frames con el mismo  
SEQ que el último recibido (deduplicación básica).

### Checksum

XOR de todos los bytes de la trama excepto el propio checksum:

```
checksum = dst ^ src ^ type ^ seq ^ len ^ flags ^ payload[0] ^ ... ^ payload[n-1]
```

---

## Protocolo de acceso al medio: CSMA/CA

### Parámetros

| Parámetro | Valor | Descripción |
|-----------|-------|-------------|
| `SIFS` | 120 ms | Short Inter-Frame Space: tiempo mínimo entre trama y su ACK |
| `DIFS` | 200 ms | Distributed IFS: período de escucha antes de transmitir |
| `SLOT_TIME` | 100 ms | Unidad mínima de backoff |
| `CW_MIN` | 2 | Contention Window inicial (slots) |
| `CW_MAX` | 16 | Contention Window máxima (slots) |
| `MAX_RETRY` | 5 | Reintentos antes de declarar fallo |
| `ACK_TIMEOUT` | 2000 ms | Tiempo máximo de espera del ACK |
| `INTER_PKT` | 100 ms | Gap entre paquetes NEC dentro de una trama |

### Máquina de estados del transmisor

```
                      ┌─────────────────────────┐
                      │         IDLE             │
                      │  (nodo no transmite)      │
                      └──────────┬──────────────-┘
                                 │ quiero transmitir
                                 ▼
                      ┌─────────────────────────┐
                      │   CARRIER SENSE          │
                      │  Escuchar por DIFS       │
                      │  (200 ms)                │
                      └──────────┬───────────────┘
                    libre        │        ocupado
               ┌─────────────────┘         │
               ▼                           ▼
  ┌────────────────────┐      ┌────────────────────────┐
  │   TRANSMIT         │      │   BACKOFF               │
  │   Enviar trama     │      │   Esperar random        │
  └─────────┬──────────┘      │   [0..CW] * SLOT_TIME  │
            │                 │   CW se duplica         │
            │                 └────────────┬───────────-┘
            ▼                              │ backoff expiró
  ┌────────────────────┐                   │
  │   WAIT ACK         │◄──────────────────┘
  │   (ACK_TIMEOUT)    │     (re-intentar carrier sense)
  └─────────┬──────────┘
     OK      │       NO ACK
  ┌──────────┘         │
  ▼                    ▼
ÉXITO             retry++ / CW*=2
                  retry >= MAX_RETRY?
                       │ sí
                       ▼
                    ERROR
```

### Detección de colisión (implícita por ausencia de ACK)

El canal IR no permite detección de colisión durante la transmisión (no se puede  
escuchar el propio canal mientras se emite). La colisión se infiere cuando el  
transmisor **no recibe ACK** dentro del timeout. El backoff exponencial reduce la  
probabilidad de colisiones repetidas.

### Flujo de recepción y ACK

```
Receptor detecta PREAMBLE (0xAA/0x55)
    → bloquea para recibir el resto de la trama
    → verifica checksum
    → si OK y DST == myAddr o BROADCAST:
        si TYPE == DATA y DST != BROADCAST:
            esperar SIFS (50 ms)
            enviar ACK frame
        entregar payload a la aplicación
    → si checksum inválido:
        enviar NAK (opcional, el transmisor reintenta por timeout de todas formas)
```

---

## Consideraciones de implementación

### Por qué Stop-and-Wait para el PoC

En la implementación actual se usa **Stop-and-Wait**: el transmisor envía un frame  
y espera el ACK antes de enviar el siguiente. Esto simplifica enormemente la lógica  
de la máquina de estados y es suficiente para demostrar CSMA/CA con 3 nodos.

Una implementación más avanzada podría usar **Sliding Window** (como en TCP) para  
mejorar el throughput, pero dado el bajo bitrate del canal IR no es prioritario.

### Conflicto Timer en Arduino

En Arduino Uno/Nano (ATmega328P), `IRremote` usa **Timer2** tanto para generar  
la portadora de 38 kHz (TX) como para temporizar la recepción (RX). Para evitar  
conflictos, antes de enviar se llama a `IrReceiver.stop()` y después del envío  
a `IrReceiver.restartAfterSend()`.

### Autorecepción

Un nodo puede "verse a sí mismo" si el receptor IR está encendido mientras transmite.  
El uso de `IrReceiver.stop()` antes de cada transmisión evita este problema.

### Broadcast y ACK

Los frames con DST=0xF (broadcast) **no generan ACK**. Si múltiples nodos  
respondieran simultáneamente al mismo broadcast, las respuestas colisionarían.

---

## Comparación con IEEE 802.3 (Ethernet)

| Característica | Ethernet 802.3 | IREthernet |
|----------------|----------------|------------|
| Acceso al medio | CSMA/CD | CSMA/CA |
| Detección colisión | Eléctrica (mid-TX) | Implícita (ACK timeout) |
| Capa física | Cable coaxial / par trenzado | IR 38 kHz (NEC) |
| Velocidad | 10 Mbps (original) | ~2 bytes / 600 ms ≈ ~3 bps útiles |
| Direcciones | 48 bits (MAC) | 4 bits (hasta 15 nodos) |
| Checksum | CRC-32 | XOR simple |
| Retransmisión | No (best-effort) | Sí (hasta MAX_RETRY=5) |
| Longitud máxima frame | 1500 bytes payload | 8 bytes payload |
