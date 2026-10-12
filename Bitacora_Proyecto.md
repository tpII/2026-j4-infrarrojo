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


------------------------------------------------------------------------------------------------------------------------------------------------------------
## Registro de Desarrollo y Dificultades

**19/09/2026 - Colisiones en la compilación y reasignación de pines**
Empezamos a portar la librería `IRremote` (versión 4.x). Al ser *header-only*, el compilador tira errores de múltiple definición en el linker. Encapsulamos `IRremote.hpp` dentro de `IREthernet.cpp` y expusimos solo la API pura en C++.
Tuvimos que mover el emisor IR en los Arduino al pin D3 nativo, porque el hardware PWM del Timer2 está ruteado ahí obligatoriamente y no funcionaba en el D11 como queríamos.

**22/09/2026 - Saturación óptica y el "Eco" fantasma**
Pruebas iniciales de transmisión fallidas. El receptor VS1838B se satura con las ráfagas IR. Tuvimos que agregar un gap artificial de 100 ms entre paquetes NEC y subir el SIFS a 120 ms para darle tiempo al AGC a estabilizarse.
Además, nos dimos cuenta de que cada nodo se estaba escuchando a sí mismo (half-duplex). El ESP32 leía su propio eco de transmisión. Solución rústica pero funcional: clavar un `IrReceiver.stop()` antes de emitir, y un `resume()` con un delay de 20 ms post-transmisión para limpiar la basura óptica.

**25/09/2026 - Descarte del monitoreo WiFi (Error de viabilidad)**
Fracaso en la implementación de la red secundaria out-of-band por WiFi que habíamos propuesto para medir colisiones. Los Arduinos obviamente no tienen WiFi, y al intentar usar la baliza física en el ESP32, manejar el stack de red de Expressif y el periférico RMT del infrarrojo al mismo tiempo rompía los timings críticos de microsegundos. Descartamos el módulo de sincronización WiFi por incompatibilidad de hardware. Ahora todos los nodos se conectan por USB a una misma PC para correr benchmarks por consola serie.

**02/10/2026 - Pérdida de paquetes en Broadcast**
Implementamos el CSMA/CA con reintentos para Unicast (hasta 5 intentos si no llega el ACK en 2000 ms). Sin embargo, la función de Broadcast (dirección `0xF`) es un desastre. La consola siempre dice "Broadcast enviado OK", pero los nodos no reciben nada. 
Análisis del error: El emisor no espera ACK en broadcast, por lo que asume éxito automático. Si un nodo receptor justo está en medio de un `isMediumBusy()` censando el canal, o transmitiendo (con el receptor apagado), se pierde el frame entero y no hay retransmisión que lo salve. A esto se suma que los emisores IR son muy direccionales; dependemos de los rebotes en el techo, y si el ángulo es malo, el paquete muere en el aire.

**08/10/2026 - Bug crítico: "Fragmentos Huérfanos"**
Estuvimos probando enviar cadenas de texto largas que se fragmentan en payloads de 7 bytes. Apareció un error grave de lógica en la capa de aplicación: usamos un único buffer global `rxMsgBuffer` que solo se limpia al recibir el flag `APP_FRAG_END`. 
Si un emisor se queda sin reintentos a mitad de un mensaje, o si se pierden los ACKs y el emisor aborta, el receptor nunca recibe el `END`. El buffer queda abierto con fragmentos basura (huérfanos). Cuando llega un mensaje nuevo de cualquier nodo, se concatena al final de la basura anterior, generando textos corruptos ilegibles. Temporalmente está sin resolver; hay que implementar IDs de mensaje en el header o purgar el buffer por timeout.

**11/10/2026 - Estado de la Prueba de Concepto**
Se entregó el Informe de Avance. El sistema valida la integridad de tramas con CRC-16 y corre bajo un esquema básico. Queda pendiente medir empíricamente la Tasa de Error de Paquete frente a distintas cargas e implementar las correcciones del buffer de fragmentos.

***
