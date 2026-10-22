# Protocolo de Acceso al Medio IR

> Continuación del proyecto *HMI sobre Infrarrojo* (2025-J1).
> Esta iteración implementa un protocolo de **acceso al medio compartido** (CSMA/CA)
> sobre canal infrarrojo, al estilo Ethernet, para una red de 3 nodos.

---

## Qué es esto

El proyecto del año anterior tenía arquitectura maestro-esclavo: el ESP32 iniciaba
siempre, los Arduinos solo respondían. No había acceso al medio compartido real.

Este año el objetivo es:

1. **3 nodos iguales** — cada uno tiene un emisor IR y un receptor IR.
   Cualquier nodo puede iniciar una transmisión en cualquier momento.
2. **Trama propia ("IREthernet frame")** — transportada como secuencia de paquetes NEC.
3. **CSMA/CA** — Carrier Sense Multiple Access / Collision Avoidance:
   escuchar antes de transmitir, backoff exponencial si el canal está ocupado.
4. **ACK de capa de enlace** — el receptor confirma cada frame recibido correctamente.
5. **Retransmisión automática** — hasta `MAX_RETRY=5` intentos con backoff creciente.

---

## Hardware

Los 3 nodos son **funcionalmente idénticos** — cada uno tiene un emisor IR y un
receptor IR, y cualquiera puede iniciar una transmisión en cualquier momento.
La única diferencia es la dirección asignada y la plataforma de hardware.

| Nodo | MCU | Dirección | Sketch |
|------|-----|-----------|--------|
| A | Arduino Uno/Nano | `0x1` | `node_A/node_A.ino` |
| B | Arduino Uno/Nano | `0x2` | `node_B/node_B.ino` |
| C | ESP32 Wemos D1 R32 | `0x3` | `node_C/node_C.ino` |

Cada nodo tiene:
- 1 módulo emisor IR de 3 pines → conectado directo a 3 pines GPIO consecutivos
- 1 módulo receptor IR de 3 pines → conectado directo a 3 pines GPIO consecutivos

Ver [`docs/hardware.md`](docs/hardware.md) para los pines exactos de cada plataforma.

---

## Estructura

```
2026-J1-IREthernet/
├── README.md
├── docs/
│   ├── hardware.md          ← pinouts, conexiones, truco 3 pines
│   ├── protocol.md          ← especificación completa del protocolo
│   ├── arduino_ide_macos.md ← cómo instalar y configurar el entorno
│   └── red_auxiliar.md      ← red de medición auxiliar (roadmap futuro)
├── IREthernet/
│   ├── IREthernet.h         ← librería: API pública
│   └── IREthernet.cpp       ← librería: implementación CSMA/CA
├── node_A/
│   └── node_A.ino           ← Nodo 0x1 (Arduino) — genérico
├── node_B/
│   └── node_B.ino           ← Nodo 0x2 (Arduino) — genérico
└── node_C/
    └── node_C.ino           ← Nodo 0x3 (ESP32) — genérico
```

---

## Cómo usar

### 1. Instalar dependencia

```bash
# IRremote >= 4.3.0 via Arduino IDE Library Manager
```

### 2. Instalar la librería IREthernet

```bash
ln -s $(pwd)/IREthernet ~/Documents/Arduino/libraries/IREthernet
```

### 3. Cargar cada sketch en su MCU

| Sketch | Board en Arduino IDE |
|--------|---------------------|
| `node_A.ino` | Arduino Uno |
| `node_B.ino` | Arduino Uno |
| `node_C.ino` | WEMOS D1 R32 (o ESP32 Dev Module) |

### 4. Abrir 3 Serial Monitors a 115200 baud

Cada nodo acepta comandos:

```
B  → broadcast a todos
1  → unicast a nodo 0x1
2  → unicast a nodo 0x2
3  → unicast a nodo 0x3
A  → ráfaga de 3 frames (genera colisiones)
?  → menú
```

### 5. Qué observar en el log

```
[IRETH] Canal ocupado. Backoff 200 ms (CW=4)   ← CSMA/CA en acción
[IRETH] TX: dst=0x2 seq=3 len=2 intento=1
[IRETH] ACK recibido. TX exitoso
[APP]   RX ← src=0x1 seq=3 len=2 cmd=0x03 data=0x05
```

---

## Documentación adicional
Documentación para trabajar con agentes
| Documento                                                | Contenido                                                                                       |
|----------------------------------------------------------|-------------------------------------------------------------------------------------------------|
| [`docs/log.md`](docs/bitacora.md)                        | **Bitácora interna:** problemas de hardware/software, Timer2, polaridad, AGC y análisis de logs |
| [`docs/hardware.md`](docs/hardware.md)                   | Pinouts por plataforma (Arduino y ESP32), conexión de módulos IR                                |
| [`docs/protocol.md`](docs/protocol.md)                   | Trama, CSMA/CA, parámetros, comparativa con Ethernet                                            |
| [`docs/arduino_ide_macos.md`](docs/arduino_ide_macos.md) | Setup macOS + 3 MCUs simultáneos                                                                |
| [`docs/red_auxiliar.md`](docs/red_auxiliar.md)           | Red I2C de medición (para etapa siguiente)                                                      |

---

## Roadmap

- [x] Diseño del protocolo IREthernet (trama + CSMA/CA)
- [x] Librería `IREthernet` (Arduino + ESP32)
- [x] Firmware 3 nodos genéricos
- [x] Documentación hardware y protocolo
- [ ] Pruebas con los 3 nodos en simultáneo
- [ ] Red auxiliar de medición I2C (ver `docs/red_auxiliar.md`) (puede que lo solucionemos conectando los 3 nodos a una pc sola)
- [ ] Interfaz web en ESP32 para monitoreo de colisiones y métricas
