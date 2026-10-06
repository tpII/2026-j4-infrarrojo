# Bitácora de Proyecto: IREthernet (2026)

Esta bitácora documenta el desarrollo, los avances, las decisiones técnicas y el funcionamiento del protocolo **IREthernet**, una red de acceso al medio compartido sobre canal infrarrojo.

---

## 1. Avances y Qué se Desarrolló

El proyecto de este año es una evolución del anterior (*HMI sobre Infrarrojo - 2025*), pasando de una arquitectura maestro-esclavo a una red de **3 nodos pares** donde cualquiera puede iniciar la comunicación.

**Hitos alcanzados:**
- **Diseño del protocolo IREthernet:** Definición de una trama propia y un mecanismo de acceso al medio (CSMA/CA).
- **Librería `IREthernet`:** Implementación en C++ compatible tanto con microcontroladores AVR (Arduino Uno/Nano) como ESP32 (Wemos D1 R32).
- **Firmware de los Nodos:** Desarrollo de los *sketches* genéricos para los 3 nodos.
- **Validación de Funcionalidades:** Se comprobó el ensamblado/desensamblado de tramas, el filtrado MAC, el envío Unicast/Broadcast, y los reintentos automáticos ante pérdida de paquetes.

---

## 2. Decisiones Técnicas y Resolución de Problemas

Durante el desarrollo, se tomaron varias decisiones importantes para sortear limitaciones de hardware y software:

1. **Gestión de Librería IRremote (Colisión del Linker):**
   Al usar la versión 4.x de `IRremote` (que es *header-only*), el compilador arrojaba errores de múltiples definiciones. 
   - *Decisión:* Se expuso una API pura de C++ en `IREthernet.h` y se encapsuló la inclusión de `IRremote.hpp` únicamente dentro de `IREthernet.cpp`.

2. **Limitación de Hardware (Timer2):**
   Inicialmente se intentó usar el pin D11 para la emisión IR, pero el microcontrolador ATmega328P rutea el hardware PWM (Timer2) a un pin específico.
   - *Decisión:* Se reasignó físicamente la señal de emisión al pin nativo **D3**.

3. **Timings y Saturación Óptica:**
   El receptor (tipo VS1838B) tiene un Control Automático de Ganancia (AGC) que se satura con ráfagas continuas de infrarrojo.
   - *Decisión:* Se aumentó el espacio entre paquetes (*Inter-Packet Gap*) a 100 ms y el tiempo SIFS a 120 ms, permitiendo que el receptor se estabilice antes de recibir el ACK.

4. **Transición Half-Duplex y "Eco" de Transmisión:**
   Tras enviar un paquete, el microcontrolador quedaba capturando el "eco" de su propio emisor.
   - *Decisión:* Se implementó un reinicio explícito del receptor (`IrReceiver.resume()`) junto a un pequeño delay de 20 ms de disipación óptica tras cada transmisión.

5. **Deduplicación por ACK Perdido:**
   Se observó que, ante la pérdida del ACK en el aire, el emisor reenviaba el paquete y el receptor lo concatenaba doblemente (duplicado).
   - *Decisión:* Se implementó un módulo de deduplicación que lleva registro de los números de secuencia (`seq`) por nodo origen, descartando automáticamente los frames repetidos.

---

## 3. Protocolo IREthernet: Explicación y Funcionamiento

El protocolo se inspira fuertemente en IEEE 802.11 (WiFi) y 802.3 (Ethernet), adaptado a un medio infrarrojo usando modulación NEC a 38 kHz.

### A. Estructura de la Trama
Una trama completa se transporta fragmentada a lo largo de varios "paquetes NEC" (que tienen 2 bytes útiles de payload cada uno):

1. **Preámbulo (PREAMBLE):** `0xAA` / `0x55` — Marca el inicio inequívoco de la trama.
2. **Direccionamiento y Tipo (ADDR/TYPE):** 
   - `DST` (Destino, 4 bits) y `SRC` (Origen, 4 bits).
   - `TYPE` (Datos `0x0`, ACK `0x1`, etc.) y `SEQ` (Número de secuencia para evitar duplicados).
3. **Longitud (LEN/FLAGS):** Longitud de los datos (0-8 bytes) y bits reservados para futuro uso.
4. **Carga Útil (PAYLOAD):** 2 bytes enviados por cada paquete NEC adicional.
5. **Comprobación (CHECKSUM):** XOR de todos los bytes, finalizando con el marcador `0xEE`.

### B. Acceso al Medio (CSMA/CA)
Para evitar que dos nodos hablen a la vez, se utiliza **CSMA/CA** (Carrier Sense Multiple Access con Collision Avoidance):

- **Carrier Sense:** Antes de transmitir, un nodo escucha el canal por un tiempo llamado `DIFS` (200 ms).
- **Backoff:** Si el canal está ocupado (detecta pulsos IR de otro nodo), el nodo espera un tiempo aleatorio. Si falla nuevamente, este tiempo de espera se duplica exponencialmente (Contention Window).
- **Collision Avoidance:** Como el nodo no puede escuchar mientras emite (no hay detección de colisión mid-TX), se apoya en el recibo de un **ACK**.
- **Stop-and-Wait ARQ:** Tras emitir una trama, el nodo espera el ACK. Si no llega en 2000 ms, asume una colisión o pérdida y retransmite automáticamente (hasta 5 intentos).

### C. Diferencias clave con Ethernet tradicional
A diferencia de Ethernet por cable que usa **CSMA/CD** (Detección de Colisiones), IREthernet usa **CSMA/CA** debido a que no es posible medir eléctricamente el canal durante la propia emisión infrarroja. Además, implementa retransmisión nativa que Ethernet no posee por defecto (best-effort).

---

## 4. Próximos Pasos (Roadmap)
- [ ] Pruebas empíricas con los 3 nodos operando y colisionando simultáneamente.
- [ ] Terminar de definir las variables a las cuales queremos realizarle mediciones.
- [ ] Utilizar la tasa de error/bits como eje principal de las pruebas, medir estas variables y realizar ajustes en base a los resultados.
- [ ] Crear una interfaz web alojada en el ESP32 para monitorear las colisiones y el rendimiento de la red en tiempo real.
