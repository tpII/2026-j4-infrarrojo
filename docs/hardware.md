# Hardware — IREthernet 2026

## Inventario de componentes

| Componente | Cantidad | Notas |
|------------|----------|-------|
| ESP32 Wemos D1 R32 | 1 | Form factor Arduino Uno |
| Arduino Uno / Nano | 2 | ATmega328P |
| Módulo emisor IR (3 pines) | 3 | LED IR 940 nm, activo en HIGH |
| Módulo receptor IR (3 pines) | 3 | Típico VS1838B o TSOP4838, señal activa en LOW |

---

## Arquitectura de la red

Los **3 nodos son funcionalmente idénticos**: cada uno puede emitir y recibir
frames en cualquier momento. La única diferencia entre ellos es la dirección
asignada (`MY_ADDR`) y la plataforma de hardware (Arduino vs ESP32).

| Nodo | MCU | Dirección | Sketch |
|------|-----|-----------|--------|
| A | Arduino Uno/Nano | `0x1` | `node_A/node_A.ino` |
| B | Arduino Uno/Nano | `0x2` | `node_B/node_B.ino` |
| C | ESP32 Wemos D1 R32 | `0x3` | `node_C/node_C.ino` |

---

## Módulos IR de 3 pines — conexión directa sin jumpers

Los módulos tienen 3 patitas. En lugar de cablear cada módulo a los rieles de
alimentación de la protoboard, se conectan **directamente a 3 pines GPIO
consecutivos** del microcontrolador:

- Un pin configurado `OUTPUT LOW` → actúa como **GND simulado**
- Un pin configurado `OUTPUT HIGH` → actúa como **VCC simulado** (~5V en Arduino, ~3.3V en ESP32)
- El tercer pin → **señal IR** real (TX o RX)

```cpp
// En setup() de cada sketch:
SETUP_SIMULATED_POWER();  // activa los pines de alimentación simulada
```

### Identificar el pinout del módulo

Buscar la serigrafía en el PCB. Si al insertarlo la señal no cae en el GPIO
correcto, **girar el módulo 180°**. Lo importante es que el pin de señal
coincida con el GPIO correspondiente.

---

## Nodos A y B — Arduino Uno / Nano

### Pinout (según `node_A.ino` / `node_B.ino`)

| Pin Arduino | Función | Configuración |
|-------------|---------|---------------|
| D3 | TX IR — Señal emisor | `IrSender` (señal de modulación) |
| D4 | TX IR — VCC simulado | `OUTPUT HIGH` |
| D5 | TX IR — GND simulado | `OUTPUT LOW` |
| D8 | RX IR — Señal receptor | `IrReceiver` |
| D9 | RX IR — VCC simulado | `OUTPUT HIGH` |
| D10 | RX IR — GND simulado | `OUTPUT LOW` |

### Esquema de conexión

```
Emisor IR (bloque D3-D4-D5):
  D3 → Señal (S / DAT)
  D4 → VCC  (OUTPUT HIGH)
  D5 → GND  (OUTPUT LOW)

Receptor IR (bloque D8-D9-D10):
  D8  → Señal (Y / OUT)
  D9  → VCC  (OUTPUT HIGH)
  D10 → GND  (OUTPUT LOW)
```

> [!NOTE]
> Los pines exactos están definidos como `#define` al inicio de cada sketch.
> Si necesitás cambiar algún pin, solo hay que modificar esos defines y
> respetar las restricciones de hardware (ej: pin TX debe soportar PWM/timer
> para la portadora de 38 kHz).

---

## Nodo C — ESP32 Wemos D1 R32

El ESP32 también usa la técnica de alimentación simulada por GPIO. Aunque los
GPIO del ESP32 entregan menos corriente que los del Arduino (~12 mA vs ~40 mA),
en la práctica funciona para los módulos IR usados en este proyecto.

### Pinout (según `node_C.ino`)

| GPIO ESP32 | Función | Configuración |
|------------|---------|---------------|
| IO17 | TX IR — Señal emisor | `IrSender` (RMT) |
| IO25 | TX IR — VCC simulado | `OUTPUT HIGH` |
| IO26 | TX IR — GND simulado | `OUTPUT LOW` |
| IO16 | RX IR — Señal receptor | `IrReceiver` (RMT) |
| IO27 | RX IR — VCC simulado | `OUTPUT HIGH` |
| IO14 | RX IR — GND simulado | `OUTPUT LOW` |

### Esquema de conexión

```
Emisor IR (bloque IO26-IO25-IO17):
  IO26 → GND  (OUTPUT LOW)
  IO25 → VCC  (OUTPUT HIGH)
  IO17 → Señal (DAT)

Receptor IR (bloque IO16-IO27-IO14):
  IO16 → GND  (OUTPUT LOW) (*)
  IO27 → VCC  (OUTPUT HIGH)
  IO14 → Señal (OUT)
```

> [!NOTE]
> (*) En el código, `RX_GND_PIN` es IO14 y `IR_RECV_PIN` es IO16. La
> asignación de qué pata del módulo va a qué GPIO depende de la orientación
> física del módulo al clavarlo. Verificar contra la serigrafía del módulo.

---

## Diagrama de red IR

```
      ┌──────────────────────────────────────┐
      │          Canal IR (broadcast)         │
      │    (todos ven todos los mensajes)     │
      └──────────────────────────────────────┘
              ↑            ↑           ↑
         TX/RX IR      TX/RX IR    TX/RX IR
              │            │           │
    ┌─────────┴──┐  ┌──────┴──┐  ┌────┴────────┐
    │ Nodo A     │  │ Nodo B  │  │ Nodo C      │
    │ Arduino    │  │ Arduino │  │ ESP32       │
    │ addr: 0x1  │  │addr:0x2 │  │ addr: 0x3  │
    └────────────┘  └─────────┘  └─────────────┘
```

---

## Notas sobre el medio IR

- **Alcance efectivo:** 50–100 cm en condiciones normales de interior con alineación directa.
  Con reflexión en superficies blancas puede extenderse, pero la confiabilidad baja.
- **Interferencias:** luz solar directa y lámparas fluorescentes generan ruido en 38 kHz.
  Recomendado hacer pruebas en ambiente controlado o con los módulos apuntados entre sí.
- **Half-duplex:** un nodo no puede emitir y recibir simultáneamente. El protocolo CSMA/CA
  maneja esto con períodos de escucha antes de transmitir.
- **Velocidad:** cada paquete NEC tarda ~67 ms. Una trama IREthernet de 4 bytes de payload
  ocupa ~5 paquetes NEC ≈ 400–600 ms totales (incluyendo gaps entre paquetes).
