# Red Auxiliar de Medición — Roadmap

> [!NOTE]
> Esta funcionalidad está **planteada pero no implementada** en la iteración actual.  
> Se documenta acá para tener una base de diseño para la próxima etapa.

---

## Motivación

El protocolo IREthernet sobre IR tiene limitaciones inherentes:
- Velocidad baja (~600 ms por trama de datos)
- Half-duplex (no se puede emitir y recibir al mismo tiempo)
- Sensible a interferencias lumínicas

Para **mediciones de rendimiento del protocolo** (latencia, throughput, tasa de error, colisiones)  
necesitamos un canal de comunicación alternativo que sea:
- **Independiente** del canal IR (no interfiere con las pruebas)
- **Confiable** (bajo error para no distorsionar las mediciones)
- **Bidireccional** (los 3 nodos pueden reportar métricas)

---

## Opciones evaluadas

### Opción 1 — I2C Bus (recomendada para PoC)

```
Nodo C (ESP32 Master) ←→ Nodo A (Arduino Slave 0x10) ←→ Nodo B (Arduino Slave 0x20)
       SDA/SCL                  SDA/SCL                      SDA/SCL
       GPIO21/GPIO22            A4/A5                        A4/A5
```

**Pros:**
- Requiere solo 2 cables entre los 3 nodos (bus compartido)
- Librería `Wire.h` nativa en Arduino y ESP32
- Hasta 400 kbps (Fast Mode) → suficiente para telemetría
- Fácil de implementar

**Contras:**
- Distancias cortas (< 1 m ideal, < 5 m con resistencias adecuadas)
- El ESP32 debe ser master (no puede ser slave con `Wire.h` estándar fácilmente)

**Parámetros sugeridos:**
- Velocidad: 100 kbps (Standard Mode) para mayor tolerancia
- Pull-ups: 4.7 kΩ a 3.3V (compatible con el ESP32)
- Dirección Nodo A: `0x10`
- Dirección Nodo B: `0x20`

### Opción 2 — UART punto a punto (más simple, menos escalable)

```
Nodo C (ESP32) Serial2 ←──── Nodo A (Arduino) Serial
Nodo C (ESP32) Serial1 ←──── Nodo B (Arduino) Serial
```

**Pros:**
- Sin librería adicional
- Muy simple de implementar
- Alta velocidad (hasta 115200 baud en la práctica)

**Contras:**
- Requiere 2 cables por nodo (4 cables en total: 2 TX + 2 RX)
- No es un bus: necesita 2 puertos UART separados en el ESP32
- No escala bien si se agregan más nodos

### Opción 3 — WiFi (solo ESP32 como concentrador)

Los Arduinos no tienen WiFi nativo, por lo que esta opción requiere un módulo WiFi adicional en cada Arduino (ESP8266 o similar), lo que complica demasiado el hardware para el PoC.

**Descartada para esta etapa.**

---

## Métricas a recolectar

| Métrica | Descripción | Nodo que reporta |
|---------|-------------|------------------|
| `tx_count` | Número de tramas transmitidas | Todos |
| `tx_success` | Transmisiones con ACK recibido | Todos |
| `tx_retry` | Número de reintentos | Todos |
| `tx_fail` | Transmisiones que fallaron (max retry) | Todos |
| `rx_count` | Tramas recibidas | Todos |
| `rx_crc_error` | Tramas con checksum inválido | Todos |
| `collision_detected` | Veces que se detectó medio ocupado al intentar transmitir | Todos |
| `backoff_total_ms` | Tiempo total acumulado en backoff | Todos |
| `ack_timeout_ms` | Tiempo promedio de espera de ACK | Todos |

---

## Formato de telemetría propuesto (JSON via I2C/UART)

```json
{
  "node": 2,
  "uptime_ms": 12345,
  "tx_count": 10,
  "tx_success": 8,
  "tx_retry": 3,
  "tx_fail": 2,
  "rx_count": 5,
  "rx_crc_error": 0,
  "collision_detected": 2,
  "backoff_total_ms": 450
}
```

---

## Plan de implementación (futura)

1. **Agregar contadores** en la librería `IREthernet` (struct `IRStats`).
2. **Implementar I2C Slave** en los Arduinos para reportar `IRStats` al ser consultados.
3. **Implementar I2C Master** en el ESP32 para polear a los 3 nodos cada N segundos.
4. **Mostrar las métricas** en la interfaz web del ESP32 (gráficos de latencia, colisiones, etc.).

---

## Conexiones físicas (I2C)

```
        Nodo C (ESP32 Wemos D1)     Nodo A (Arduino Uno)  Nodo B (Arduino Uno)
        ┌──────────────────┐        ┌─────────────┐      ┌───────────────┐
        │ GPIO21 (SDA) ────┼────────┤ A4 (SDA)    │      │ A4 (SDA)      │
        │ GPIO22 (SCL) ────┼────────┤ A5 (SCL)    │      │ A5 (SCL)      │
        │ 3.3V ────────────┼──┬─────┤ (pull-ups)  │      │ (pull-ups)    │
        │ GND  ────────────┼──┴─────┤ GND         ├──────┤ GND           │
        └──────────────────┘  │     └─────────────┘      └───────────────┘
                            4.7kΩ
                            pull-ups al bus SDA/SCL
```

> [!IMPORTANT]
> Los pull-ups deben conectarse **una sola vez** al bus, no en cada nodo.  
> Usar 4.7 kΩ a 3.3V (rail del ESP32) para compatibilidad con todos los niveles lógicos.
