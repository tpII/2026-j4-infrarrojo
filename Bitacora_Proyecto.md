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
- **Ajustes al protocolo tras las pruebas en hardware (25/10):** fragmentación con ID de mensaje, SEQ por destino, broadcast con 3 copias, backoff aleatorio, NAV y bit *More Fragments* (ver sección 3.D).
- **Validación en el caso ideal con 3 nodos colisionando:** 10/10 mensajes entregados con B y C mandándole a A al mismo tiempo.
- **Herramientas de medición en el firmware:** comandos `R`, `M`, `C`, `T` y `S`, más los scripts `mon.py` y `step.py` para registrar los tres puertos en un único log.
- **Plan de Pruebas:** 10 escenarios (E1 a E10), métricas definidas y planilla de registro (sección 4).
- **Registro de flasheo y backups** de los nodos físicos (`registro_flasheo.md`).

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

6. **Pines de alimentación simulada y polaridad (sin jumpers):**
   Los módulos IR se insertan directo en los headers hembra y el orden de las patitas no coincidía con el código inicial.
   - *Decisión:* Se remapeó el emisor a D3 (señal), D4 (VCC), D5 (GND) y el receptor a D8 (señal), D9 (VCC), D10 (GND).

7. **Clasificación NEC vs ONKYO en IRremote 4.x:**
   La biblioteca clasificaba algunos paquetes nuestros como `ONKYO` en vez de `NEC` y los descartábamos.
   - *Decisión:* El filtro de recepción acepta ambos: `proto == NEC || proto == ONKYO`.

8. **Fragmentación robusta (cambio de protocolo):**
   Con el buffer global de reensamblado aparecían mensajes mezclados.
   - *Decisión:* Encabezado de fragmento de 2 bytes con ID de mensaje e índice, un buffer de reensamblado por nodo origen, y SEQ por destino que avanza siempre.

9. **Broadcast confiable "a lo bruto" (cambio de protocolo):**
   Sin ACK no hay retransmisión.
   - *Decisión:* Cada trama broadcast se emite 3 veces con el mismo SEQ y los nodos tienen una cola de recepción de 3 tramas para lo que llega mientras transmiten.

10. **CSMA/CA completo (cambio de protocolo):**
    Las colisiones sincronizadas y la inanición mostraron que faltaban piezas.
    - *Decisión:* Backoff aleatorio tras cada fallo, slot de 200 ms, timeout por paquete de 400 ms, NAV (carrier sense virtual), bit *More Fragments* y cesión del canal al terminar cada mensaje.

---

## 3. Protocolo IREthernet: Explicación y Funcionamiento

El protocolo se inspira fuertemente en IEEE 802.11 (WiFi) y 802.3 (Ethernet), adaptado a un medio infrarrojo usando modulación NEC a 38 kHz.

> **Nota:** esta sección describe el diseño original. Después de las pruebas en hardware del 25/10 el protocolo **tuvo cambios**; están resumidos en la sección 3.D y en las entradas del registro.

### A. Estructura de la Trama
Una trama completa se transporta fragmentada a lo largo de varios "paquetes NEC" (que tienen 2 bytes útiles de payload cada uno):

1. **Preámbulo (PREAMBLE):** `0xAA` / `0x55` — Marca el inicio inequívoco de la trama.
2. **Direccionamiento y Tipo (ADDR/TYPE):** 
   - `DST` (Destino, 4 bits) y `SRC` (Origen, 4 bits).
   - `TYPE` (Datos `0x0`, ACK `0x1`, etc.) y `SEQ` (Número de secuencia para evitar duplicados).
3. **Longitud (LEN/FLAGS):** Longitud de los datos (0-8 bytes) y bits de control. *(Cambio de protocolo: en el diseño original estos bits estaban reservados; ahora el bit 0 es `MORE`, ver 3.D.)*
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

### D. Cambios al protocolo (versión final, 25/10/2026)

Todo lo que sigue se descubrió probando en hardware, después de tener el protocolo "terminado" en papel. Los cambios están marcados en las entradas del registro de desarrollo.

| Aspecto | Primera versión | Versión final |
|---|---|---|
| **Campo FLAGS** | Todos los bits reservados | Bit 0 = `MORE` (More Fragments) |
| **Backoff tras un fallo** | Duplicaba la CW pero reintentaba sin backoff aleatorio | Backoff aleatorio de [0, CW) slots tras cada intento fallido, escuchando el canal |
| **Slot de backoff** | 100 ms | 200 ms (la detección de otro nodo tarda ~170 ms) |
| **Timeout por paquete dentro de una trama** | 1000 ms | 400 ms |
| **Carrier sense** | Solo físico (DIFS) | Físico + virtual (NAV de 1000 ms, o 1400 ms si la trama trae `MORE`) |
| **Cesión del canal** | No existía | `YIELD` de 1000 ms al terminar cada mensaje |
| **Esperas por canal ocupado** | Gastaban intentos de los 5 | Tienen su propio límite (`MAX_DEFER = 10`) |
| **SEQ** | Un solo contador que avanzaba únicamente si había éxito | Un contador por destino, avanza siempre y arranca en un valor aleatorio |
| **Deduplicación** | Un `last_seq[16]` por origen | Por origen, separado para unicast y para broadcast |
| **Fragmentación (capa de aplicación)** | 1 byte de encabezado (`CONT`/`END`), 7 bytes de texto, buffer global | 2 bytes de encabezado `[CONT\|END] [MSG_ID(4b)\|FRAG_IDX(4b)]`, 6 bytes de texto, mensajes de hasta 96 bytes, buffer por origen con descarte a los 30 s |
| **Broadcast** | 1 sola emisión | 3 copias con el mismo SEQ (`BCAST_REPEAT`) |
| **Recepción mientras se transmite** | La trama se perdía | Cola de recepción de 3 tramas |
| **Validación del ACK** | Solo SEQ | SEQ y origen del ACK |

**Parámetros finales:**

| Parámetro | Valor | Parámetro | Valor |
|---|---|---|---|
| `SIFS` | 120 ms | `PKT_TIMEOUT` | 400 ms |
| `DIFS` | 200 ms | `NAV` / `NAV_MORE` | 1000 / 1400 ms |
| `SLOT_TIME` | 200 ms | `YIELD` | 1000 ms |
| `CW_MIN` / `CW_MAX` | 2 / 16 | `BCAST_REPEAT` | 3 |
| `MAX_RETRY` | 5 | `RX_QUEUE_LEN` | 3 |
| `MAX_DEFER` | 10 | `INTER_PKT` | 100 ms |
| `ACK_TIMEOUT` | 2000 ms | | |

---

## 4. Plan de Pruebas (resumen)

El protocolo se diseñó e implementó primero; el plan de pruebas se armó después, a partir de lo que vimos en la validación del 25/10. El documento completo está en `plan_pruebas.md`.

**Métricas por corrida:** PDR (tasa de entrega de mensajes), éxito por trama `p`, reintentos por trama, pérdida de ACK (`rxDup / rxData`), goodput, latencia media, integridad y equidad entre emisores. **Condición de éxito:** el PDR puede bajar, pero la integridad tiene que ser 100 %: un mensaje llega exacto o se descarta, nunca mezclado ni duplicado.

| Escenario | Qué mide |
|---|---|
| E1 — Alcance | PDR y `p` según la distancia, por sentido de enlace (incluye la hipótesis de que la ESP32 con 3,3 V alcanza menos que el Uno con 5 V) |
| E2 — Ángulo | Directividad del emisor y del receptor |
| E3 — Reflexión | PDR por superficie sin línea de vista (pared, espejo, aluminio, madera, cartón, tela negra, vidrio) |
| E4 — Obstrucción y pérdidas forzadas | Obstrucción estática e intermitente, corte a mitad de mensaje |
| E5 — Luz e IR ajeno | Distintas luces y control remoto de TV apuntando a los receptores |
| E6 — Nodo oculto | Colisiones cuando A y C no se escuchan entre sí |
| E7 — Carga y contención | 1, 2 y 3 emisores; mensajes de 1, 16 y 48 bytes; inanición |
| E8 — Broadcast | Si alcanzan las 3 copias |
| E9 — Fallas de nodo | Reset del emisor/receptor a mitad de mensaje, nodo apagado |
| E10 — Larga duración | Cuelgues y estabilidad del PDR (~15 min) |

---

## 5. Próximos Pasos (Roadmap)
- [x] Pruebas empíricas con los 3 nodos operando y colisionando simultáneamente *(hecho en el caso ideal el 25/10: B y C → A, 10/10 mensajes; falta hacerlo fuera del caso ideal, ver E6 y E7)*.
- [x] Terminar de definir las variables a las cuales queremos realizarle mediciones *(ver sección 4)*.
- [ ] Ejecutar los escenarios E1 a E10 con 3 repeticiones por punto. Orden: E1, después E4 y E5, después E6 y E7, y al final E2, E3, E8, E9 y E10.
- [ ] Utilizar la tasa de error/bits como eje principal de las pruebas, medir estas variables y realizar ajustes en base a los resultados. *(En el plan quedó expresada como PDR y éxito por trama `p`; falta decidir si se mide también algo a nivel de bits.)*
- [ ] Evaluar según los resultados de E6 si hace falta RTS/CTS y si hay que cambiar `IRETH_BCAST_REPEAT` (requiere reflashear).
- [ ] Corregir la documentación: tabla de direcciones y velocidad en `protocol.md`, y la mención a CRC-16 del Informe de Avance (ver 30/10).
- [x] ~~Crear una interfaz web alojada en el ESP32 para monitorear las colisiones y el rendimiento de la red en tiempo real.~~ *Descartada (ver 25/09): se reemplazó por `mon.py`, que junta los tres puertos serie en un único log con timestamps.*


------------------------------------------------------------------------------------------------------------------------------------------------------------
## Registro de Desarrollo y Dificultades

**19/09/2026 - Colisiones en la compilación y reasignación de pines**
Empezamos a portar la librería `IRremote` (versión 4.x). Al ser *header-only*, el compilador tira errores de múltiple definición en el linker. Encapsulamos `IRremote.hpp` dentro de `IREthernet.cpp` y expusimos solo la API pura en C++.
Tuvimos que mover el emisor IR en los Arduino al pin D3 nativo, porque el hardware PWM del Timer2 está ruteado ahí obligatoriamente y no funcionaba en el D11 como queríamos.

**20/09/2026 - El emisor no emitía y los módulos estaban "al revés"**
Seguimos con el emisor. Para ver si emitía algo usamos la cámara del celular y la de la Mac, y el LED no destellaba nunca. Eso lo terminamos de entender con lo del Timer2 de ayer: el 38 kHz sale por hardware en D3 y en D11 no había nada.
Pero había un segundo problema: queríamos meter los módulos directo en los headers hembra del Arduino, sin protoboard ni jumpers, usando los pines vecinos a la señal como VCC y GND (`OUTPUT HIGH` / `OUTPUT LOW`). El receptor no prendía su LED y el emisor tampoco transmitía. Resulta que el orden de las patitas de los módulos chinos no era el que habíamos supuesto:
- Receptor (`Y`, `R`, `G`): puesto en D2-D3-D4, la `R` (VCC) caía en D3 y la `G` (GND) en D4, y nosotros teníamos D3 en LOW y D4 en HIGH. Polaridad invertida.
- Emisor (`S`, medio, `-`): puesto en D3-D4-D5, el pin del medio (VCC) caía en D4 y el `-` (GND) en D5.

Lo arreglamos remapeando el `setup()`: emisor en D3 (señal), D4 (VCC), D5 (GND) y receptor en D8 (señal), D9 (VCC), D10 (GND). Nota para la próxima: mirar el serigrafiado de los módulos *antes* de escribir el código de los pines.

**21/09/2026 - El receptor "veía" la trama pero la capa IREthernet la tiraba**
El LED del sensor se encendía cuando transmitíamos, pero en el monitor serie salía `[IRETH] Timeout esperando addr/type pkt`. Pusimos logs crudos de depuración y vimos esto:
```text
[DEBUG RX] proto=7 raw=0xAA5555AA addr=0xAA cmd=0x55  <-- Preámbulo (NEC = 7)
[DEBUG RX] proto=8 raw=0xFF00DE21 addr=0x21 cmd=0x00  <-- Payload (ONKYO = 8)
```
El primer paquete lo clasificaba como NEC y el segundo como ONKYO. Entendimos que NEC usa dirección de 8 bits más su complemento, y cuando mandamos campos arbitrarios como `(dst << 4) | src`, `IRremote` 4.x lo mete en el enum de ONKYO. Nuestro código filtraba solo `proto == NEC`, así que el segundo paquete se descartaba y la trama moría por timeout. Solución: aceptar los dos, `proto == NEC || proto == ONKYO`. Perdimos bastante tiempo mirando los timings cuando el problema era un `if`.

**22/09/2026 - Saturación óptica y el "Eco" fantasma**
Pruebas iniciales de transmisión fallidas. El receptor VS1838B se satura con las ráfagas IR. Tuvimos que agregar un gap artificial de 100 ms entre paquetes NEC y subir el SIFS a 120 ms para darle tiempo al AGC a estabilizarse.
Además, nos dimos cuenta de que cada nodo se estaba escuchando a sí mismo (half-duplex). El ESP32 leía su propio eco de transmisión. Solución rústica pero funcional: clavar un `IrReceiver.stop()` antes de emitir, y un `resume()` con un delay de 20 ms post-transmisión para limpiar la basura óptica.

**24/09/2026 - El ACK que nunca llegaba (aunque el receptor sí contestaba)**
Con lo anterior pareció que ya andaba, pero el emisor seguía mostrando `Sin ACK (timeout)` aunque el receptor claramente recibía y mandaba el ACK. Y en los reintentos siguientes el receptor quedaba sordo.
Después de un rato de mirar el código de `IRremote` entendimos lo que pasaba: en el ATmega328P el Timer2 se usa tanto para generar el PWM al transmitir como para muestrear al recibir. Al terminar de transmitir llamábamos a `IrReceiver.restartAfterSend()`, pero eso dejaba el buffer con el último pulso de nuestro propio LED, y como nadie llamaba a `resume()`, la interrupción de captura quedaba esperando que el programa leyera ese resultado viejo. Por eso nunca capturaba el preámbulo del ACK.
Lo resolvimos en `sendFrame()` con esta secuencia: `delay(20)`, `IrReceiver.start()` e `IrReceiver.resume()`. Quedó como 22/09 más lo que faltaba: no alcanzaba con limpiar el eco, había que rearmar el receptor explícitamente.

**25/09/2026 - Descarte del monitoreo WiFi (Error de viabilidad)**
Fracaso en la implementación de la red secundaria out-of-band por WiFi que habíamos propuesto para medir colisiones. Los Arduinos obviamente no tienen WiFi, y al intentar usar la baliza física en el ESP32, manejar el stack de red de Expressif y el periférico RMT del infrarrojo al mismo tiempo rompía los timings críticos de microsegundos. Descartamos el módulo de sincronización WiFi por incompatibilidad de hardware. Ahora todos los nodos se conectan por USB a una misma PC para correr benchmarks por consola serie.

**02/10/2026 - Pérdida de paquetes en Broadcast**
Implementamos el CSMA/CA con reintentos para Unicast (hasta 5 intentos si no llega el ACK en 2000 ms). Sin embargo, la función de Broadcast (dirección `0xF`) es un desastre. La consola siempre dice "Broadcast enviado OK", pero los nodos no reciben nada. 
Análisis del error: El emisor no espera ACK en broadcast, por lo que asume éxito automático. Si un nodo receptor justo está en medio de un `isMediumBusy()` censando el canal, o transmitiendo (con el receptor apagado), se pierde el frame entero y no hay retransmisión que lo salve. A esto se suma que los emisores IR son muy direccionales; dependemos de los rebotes en el techo, y si el ángulo es malo, el paquete muere en el aire.

**05/10/2026 - Mensajes con pedazos repetidos (ACK perdido)**
Probando strings largos y moviendo la placa mientras transmitía, el receptor armó `"Hola fondo norndo norte"` (23 bytes) en lugar de `"Hola fondo norte"` (16). Se había duplicado el fragmento `"ndo nor"`.
Lo seguimos paso a paso en el log: el emisor mandó el fragmento 1 (`seq=4`), el receptor lo recibió bien, lo agregó al buffer y contestó el `ACK seq=4`. Al mover la placa el haz del ACK se desalineó y no llegó. El emisor esperó el timeout, pensó que se había perdido el paquete y lo reenvió. El receptor no tenía memoria de las secuencias anteriores y lo trató como un fragmento nuevo, entonces lo concatenó otra vez.
Hicimos un `FrameDeduplicator` con un `last_seq[16]`, uno por nodo origen. La regla: ante cualquier DATA el receptor **siempre reenvía el ACK** (para destrabar al emisor), pero si el `seq` es igual al último de ese origen la capa de enlace lo descarta y no se lo pasa a la aplicación.

**08/10/2026 - Bug crítico: "Fragmentos Huérfanos"**
Estuvimos probando enviar cadenas de texto largas que se fragmentan en payloads de 7 bytes. Apareció un error grave de lógica en la capa de aplicación: usamos un único buffer global `rxMsgBuffer` que solo se limpia al recibir el flag `APP_FRAG_END`. 
Si un emisor se queda sin reintentos a mitad de un mensaje, o si se pierden los ACKs y el emisor aborta, el receptor nunca recibe el `END`. El buffer queda abierto con fragmentos basura (huérfanos). Cuando llega un mensaje nuevo de cualquier nodo, se concatena al final de la basura anterior, generando textos corruptos ilegibles. Temporalmente está sin resolver; hay que implementar IDs de mensaje en el header o purgar el buffer por timeout.

**11/10/2026 - Estado de la Prueba de Concepto**
Se entregó el Informe de Avance. El sistema valida la integridad de tramas con CRC-16 y corre bajo un esquema básico. Queda pendiente medir empíricamente la Tasa de Error de Paquete frente a distintas cargas e implementar las correcciones del buffer de fragmentos.

**15/10/2026 - Backup de los nodos antes de tocar el firmware**
Antes de empezar a cambiar cosas decidimos sacar un backup de lo que tenían cargado las placas. Los tres nodos tenían el firmware del commit `f4df7e8` ("proteccion duplicados, pinout nodo 3"), y a cada placa la identificamos leyendo el banner que imprime al arrancar: A es un Uno en COM10 (dirección `0x1`), B es un Uno en COM5 (`0x2`) y C es la ESP32 Wemos D1 R32 en COM13 (`0x3`).
- **Arduino Uno (A y B):** usamos `avrdude` (el que viene con el Arduino IDE) por bootloader y leímos la flash completa (32 KiB, con el Optiboot incluido) dos veces por placa. Las dos lecturas coincidieron, así que anotamos los sha256 en el registro. La EEPROM no se pudo leer: Optiboot devuelve el contenido de la flash en su lugar. Igual no es un riesgo porque flashear por bootloader no la toca.
- **ESP32 (C): falló.** `esptool` no logró poner la placa en modo descarga (`Wrong boot mode detected (0x13)`). Probamos la secuencia de reset por defecto y otra con tiempos más largos. El reset por RTS (EN) funciona, pero el circuito de auto-programación no baja el GPIO0. Parece ser un problema conocido de los clones de la D1 R32. Lo que sí funciona es hacerlo a mano: unir **IO0 con GND** con un cable y apretar **RESET**; y después de flashear sacar el cable y volver a apretar RESET.

**20/10/2026 - Cambios al protocolo: fragmentación con ID y broadcast con copias**
Nos pusimos con el bug de los fragmentos huérfanos del 08/10 y con el de broadcast del 02/10. Al analizarlos mejor vimos que había más causas que las que habíamos anotado:
- **Huérfanos:** el encabezado de aplicación tenía un solo byte (`CONT`/`END`), sin índice ni ID de mensaje, así que el receptor no tenía forma de saber que un mensaje había quedado incompleto. Además de que el emisor abortara a mitad de mensaje, encontramos otros dos escenarios: (1) un **falso duplicado por SEQ reutilizado**: `_seq` avanzaba solo si había éxito, entonces si llegaba la trama pero se perdían todos sus ACK, el mensaje siguiente salía con el mismo SEQ, el receptor lo tomaba por retransmisión, le mandaba ACK y lo descartaba, y se perdía el primer fragmento del mensaje nuevo sin que nadie se enterara; (2) la pérdida de un fragmento de broadcast, que no tiene ACK ni retransmisión.
- **Broadcast:** además de la falta de ACK, vimos que un nodo ocupado dentro de su propio `send()` perdía las tramas ajenas: en el carrier sense se consumía el preámbulo solo para concluir "canal ocupado", y mientras esperaba su ACK recibía la trama completa pero la descartaba por no ser el ACK esperado.

*Cambios al protocolo:*
- Encabezado de fragmento de 2 bytes: `[CONT|END] [MSG_ID(4b) | FRAG_IDX(4b)]`, quedan 6 bytes de texto por trama (antes eran 7) y mensajes de hasta 96 bytes.
- Un buffer de reensamblado por nodo origen. Si llega un fragmento fuera de secuencia, empieza otro mensaje o pasan 30 s sin fragmentos, el mensaje a medias se descarta con aviso.
- SEQ por destino, que avanza siempre (haya éxito o no) y arranca en un valor aleatorio (así un reinicio del nodo tampoco genera falsos duplicados). `waitForAck` ahora también chequea el origen del ACK.
- Cada trama broadcast se emite 3 veces (`IRETH_BCAST_REPEAT`) con el mismo SEQ, y el receptor descarta las copias con un deduplicador propio para broadcast.
- Cola de recepción de 3 tramas para lo que llega mientras el nodo está transmitiendo. Si la cola se llena, no se manda ACK y el emisor reintenta.

Todavía no lo probamos en las placas, solo está en el código.

**22/10/2026 - Se perdieron los backups del 15/10**
Fuimos a buscar los archivos del backup de los Uno y no estaban. Los habíamos guardado en una carpeta temporal (`%TEMP%`) y Windows la limpió hoy. Lo único que quedó son los hashes sha256 que habíamos anotado en el registro. Moraleja: nada importante en `%TEMP%`. Decidimos repetir el backup guardándolo **dentro del repo**, y mover las herramientas auxiliares a otra carpeta (`C:\Users\estan\irb-tools`).

**25/10/2026 - Jornada de pruebas en hardware (mañana): flasheo y herramientas**
Antes de probar tuvimos que dejar todo preparado.
- **Backup repetido de los Uno** (ahora en `backups/2026-10-25/`, con un MANIFEST y el comando para restaurar). Leímos dos veces cada placa y los hashes coincidieron con los del 15/10, o sea que las placas no habían cambiado. Primero confirmamos cuál era cuál por el banner: COM10 es el nodo A y COM5 el nodo B, y los números de serie USB coinciden con los de la otra vez.
- **ESP32 sin backup binario**, por decisión del equipo: si hace falta volver atrás se recupera desde git (`git checkout f4df7e8 -- IREthernet node_A node_B node_C`).
- **Compilación:** el repo tiene una ruta larga y con caracteres no ASCII, y `avr-gcc` no la maneja. Terminamos copiando las fuentes a `irb-tools/src` antes de compilar con `arduino-cli` (core `arduino:avr` 1.8.8 para los Uno y `esp32:esp32` 3.3.12 para la ESP32, IRremote 4.7.1).
- **Flasheo de la ESP32** (FQBN `esp32:esp32:d1_uno32`, COM13, 921600 baud, con IO0 puenteado a GND): `esptool` escribió y verificó el hash de cada segmento (bootloader en `0x1000`, tabla de particiones en `0x8000`, `boot_app0.bin` en `0xE000` y la aplicación en `0x10000`).
- **Problema:** después de flashear la placa arrancó normal, pero el banner (`IREthernet — Nodo 0x3 (ESP32)`) es **idéntico** al de la versión anterior, así que no había forma de saber si se había cargado el firmware nuevo. Lo comprobamos mandándole por serie `M HOLA` y `B`, que solo la versión nueva soporta. Salió `msg=0`, `len=6` (4 caracteres más el header de 2 bytes) y las 3 copias del broadcast. Ahí confirmamos que era la versión nueva.
- Armamos `mon.py` (monitor de los tres puertos que escribe un único log con timestamps y recibe comandos desde un archivo) y `step.py` (manda comandos y espera un patrón en el log), así no hay que tener tres terminales abiertas.

**25/10/2026 - Sesión 1: primera corrida con los 3 nodos (tarde)**
Con los tres nodos con la versión nueva (fragmentos con ID, SEQ por destino, broadcast ×3, cola de RX) hicimos la primera tanda. Lo que vimos: el enlace C→A es el más débil (ya sospechábamos por el LED de la ESP32, que va a 3,3 V). Y aparecieron las colisiones: con B y C mandándole un string a A al mismo tiempo, chocaron **en los 5 intentos**, separados por menos de 70 ms, y los dos abortaron. Log: `sesion1_pre_fix_backoff.log`.

**25/10/2026 - Sesión 2: faltaba el backoff después de fallar**
Revisando el código de `send()` encontramos el problema: después de un intento sin ACK duplicaba la CW pero **reintentaba sin ningún backoff aleatorio**. Como cada intento dura lo mismo (la trama más 2 s de timeout de ACK), dos nodos que chocaban una vez quedaban sincronizados para siempre. Ese defecto venía desde la versión original y no lo habíamos visto porque nunca habíamos probado dos emisores juntos.
*Cambio al protocolo:* backoff aleatorio de [0, CW) slots tras cada intento fallido, sin dejar de escuchar el canal (`listenFor()`).
Con slots de 100 ms todavía chocaban cuando los dos nodos elegían slots contiguos. Midiendo el log vimos que un nodo tarda unos 170 ms en darse cuenta de que otro empezó a transmitir (100 ms de pausa inicial en `sendFrame` más 67 ms del paquete NEC del preámbulo), o sea que el slot tenía que ser mayor a eso. Lo pasamos a **200 ms**. Log: `sesion2_fix_backoff.log`.

**25/10/2026 - Sesión 3: el timeout de 1 segundo y el ACK sin protección**
Con el backoff arreglado igual seguíamos perdiendo tramas que deberían haber llegado. Miramos los logs y había tres cosas:
1. El timeout por paquete era de **1000 ms**. A perdió un paquete de una trama de B y se quedó esperando. Mientras tanto C empezó a transmitir, y A tomó el preámbulo de C como si fuera el final de la trama de B (`END marker invalido: 0x55`). Se perdieron **las dos tramas**.
2. El ACK no estaba protegido. Un tercer nodo que escuchaba un DATA ajeno solo esperaba DIFS más un backoff que podía salir 0, y transmitía justo encima del ACK del destino.
3. Las esperas por canal ocupado gastaban intentos, así que un nodo podía abortar sin haber transmitido 5 veces.

*Cambios al protocolo:* `IRETH_PKT_TIMEOUT_MS = 400` (los paquetes llegan cada ~170 ms, así que una trama rota se abandona enseguida) y un **NAV** (carrier sense virtual, como en 802.11): después de escuchar un DATA unicast dirigido a otro nodo, el canal se considera ocupado 1000 ms, que es lo que dura el ACK. Log: `sesion3_fix_nav.log`.

**25/10/2026 - Sesión 4: el NAV nos generó inanición**
En esta tanda agregamos el resto de lo de la sesión 3 (backoff aleatorio después del NAV o del canal ocupado, y que las esperas no cuenten como intentos, con su propio límite `IRETH_MAX_DEFER = 10`). También agregamos al firmware los comandos `R` (reset de contadores), `T <d> <n>` (enviar `n` mensajes) y `S` (estadísticas), para no contar todo a mano en el log.
Seguía habiendo un choque: cuando el nodo que esperaba terminaba su NAV, el emisor ya salía con el fragmento siguiente del mismo mensaje, y el DIFS del que esperaba terminaba **10 ms antes** de poder detectar ese preámbulo. Chocaban. Para arreglarlo subimos el NAV a 1400 ms. Funcionó para eso, pero creó otro problema: el NAV largo también protegía el **mensaje siguiente** de un emisor con tráfico continuo, y el otro nodo nunca conseguía el canal. Con 5 mensajes por nodo, B abortó uno por `canal ocupado demasiado tiempo`. A descartó correctamente los 12 bytes que había recibido de ese mensaje incompleto. Log: `sesion4_starvation.log`.

**25/10/2026 - Sesión 5: se perdió una corrida entera (la ESP32 en modo descarga)**
Flasheamos la versión siguiente y largamos la corrida, pero la ESP32 no respondía nada. Después de un rato nos dimos cuenta: la placa había quedado en **modo descarga** después de flashear. Esto nos pasó dos veces ese día, y en una de ellas se perdió la corrida completa (`sesion5_C_en_modo_descarga.log`). Reglas que anotamos para la próxima:
1. Si `esptool` dice `Wrong boot mode detected (0x13)`, el cable IO0–GND no hace contacto: revisarlo y repetir.
2. Flashear.
3. **Sacar el cable primero y recién después** apretar RESET.
4. Antes de abrir el monitor, resetear la placa por RTS y **verificar que salga el banner**.

**25/10/2026 - Sesión 6: More Fragments y cesión del canal (funciona)**
La solución de la inanición la sacamos de cómo lo hace 802.11:
- **Bit `IRETH_FLAG_MORE`** en el campo FLAGS (que hasta ahora estaba reservado). La aplicación lo activa en todos los fragmentos de un mensaje salvo el último. Quien escucha un DATA con ese bit aplica un NAV de 1400 ms (`IRETH_NAV_MORE_MS`), suficiente para que el fragmento siguiente se escuche antes de que termine su DIFS. Sin el bit, el NAV vuelve a ser de 1000 ms.
- **Cesión del canal:** al terminar un mensaje (trama sin el bit), el emisor espera `IRETH_YIELD_MS = 1000` antes de su próxima trama, y así el nodo que estaba esperando toma el canal.

Resultado: B y C mandándole 5 strings cada uno a A **al mismo tiempo: 10/10 mensajes entregados**, alternados B/C/B/C, con un solo reintento por nodo. Log: `sesion6_fix_morefrag.log`.
Corrimos también toda la regresión con la versión final (los tres nodos en triángulo, muy cerca y mirándose, o sea el **caso ideal**; sirve para depurar el protocolo, no para caracterizar el canal):

| Prueba | Mensajes | Entregados | Tramas emitidas | Reintentos | Tiempo | Goodput |
|---|---|---|---|---|---|---|
| Unicast contador A→B | 10 × 1 B | 10/10 | 14 | 4 | 41,4 s | 0,24 B/s |
| Unicast string A→C | 5 × 16 B | 5/5 | 15 | 0 | 39,7 s | 2,02 B/s |
| Broadcast string desde A | 5 × 16 B | 5/5 en B y en C | 45 (15 × 3 copias) | — | 73,0 s | 1,10 B/s |
| Ráfaga `A` (broadcast + unicast a B y a C) | 4 recepciones | 4/4 | — | — | 29,9 s | — |
| Contención: B→A y C→A simultáneos | 5 + 5 strings (18 y 20 B) | **10/10** | 16 + 21 | 1 + 1 | 79–89 s | 1,13 B/s por nodo |

Cosas que notamos: en broadcast B recibió 44 de las 45 copias y C las 45, o sea que la repetición absorbió la pérdida. En contención los turnos se alternaron estrictamente. El goodput del modo contador es bajísimo porque cada trama dura más de 1 s aunque lleve un solo byte útil (el costo fijo domina). Y `deferrals` no da cero aunque no haya contención porque también cuenta la espera por cesión propia del canal (1 por mensaje); nos confundió un rato.
La versión final quedó cargada en los tres nodos (iteración 5): A 24642 B (76 %), B 24648 B (76 %) y C 312954 B (23 %). En los Uno `avrdude` verificó la flash después de cada escritura y en la ESP32 `esptool` verificó el hash de cada segmento. Los hashes están en `registro_flasheo.md`.

**27/10/2026 - Definimos cómo medir: herramientas y métricas**
Ahora que el protocolo anda en el caso ideal, toca caracterizarlo de verdad, fuera de ese caso. Primero revisamos qué tenemos en el firmware: todos los nodos aceptan por serie (115200 baud) `R` (poner en cero los contadores, **en todos los nodos antes de cada corrida**), `M <texto>` / `C` (cargar un string de hasta 96 bytes o volver al modo contador de 1 byte), `T <d> <n>` (enviar `n` mensajes a `1`, `2`, `3` o `B`; `X` aborta) y `S` (imprimir los contadores `[STATS]` y `[APPSTATS]`, que se ejecuta en los receptores al terminar).
Contadores: `txFrames`, `txRetries`, `txOk`/`txFail`, `deferrals`, `rxData`, `rxDup`, `rxErrors`, `rxOthers`, `msgsOk`/`msgsDropped` y `dataRx`. Con esos definimos las métricas: PDR (`ok / n` en el emisor, o `msgsOk / n` en cada receptor si es broadcast), éxito por trama `p = txOk / txFrames`, reintentos por trama, pérdida de ACK (`rxDup / rxData`, porque en unicast un duplicado significa un ACK perdido), goodput, latencia media (`ms / ok`), integridad y equidad (`ok_B / ok_C` o índice de Jain).
Lo más importante que acordamos es la **condición de éxito**: el PDR puede bajar, pero la integridad tiene que ser 100 %. Un mensaje se entrega exacto o se descarta; nunca mezclado ni duplicado. Es para asegurarnos de que no vuelva el problema de los mensajes mezclados del 08/10.
Duda que nos quedó: el roadmap original decía usar la tasa de error de bits como eje principal. Con este protocolo lo que medimos es entrega de tramas y mensajes, porque el checksum descarta la trama entera si algo falla. Quedó en PDR y `p`.

**28/10/2026 - Plan de pruebas: escenarios de canal (E1 a E5)**
Escribimos `plan_pruebas.md`. Procedimiento general: medir y anotar distancia, ángulo, altura, superficie, iluminación y hora; `R` en los tres nodos; correr `C` o `M <texto>` y después `T <d> <n>`; al terminar, `S` en los receptores; anotar una fila en la planilla y **repetir 3 veces** cada punto para tener la dispersión. Valores sugeridos: `n = 20` en modo contador (~4 s por mensaje) y `n = 5` con `M Hola fondo norte` (16 bytes, 3 fragmentos) en modo string.
- **E1 Alcance:** A→B de frente a 10, 25, 50, 100, 150, 200 y 300 cm, y seguir de a 50 cm hasta que el PDR baje de 50 %. Repetir los puntos clave con C→A y A→C. Hipótesis a verificar: como la ESP32 maneja el LED con 3,3 V contra 5 V del Uno, su alcance de emisión debería ser menor.
- **E2 Ángulo:** a la mitad del alcance de E1, girar solo el receptor (0°, 15°, 30°, 45°, 60°, 90°) y después solo el emisor.
- **E3 Reflexión:** nodos lado a lado mirando una superficie a 30 y 60 cm, con una barrera opaca en el medio. Superficies: pared blanca, espejo, papel aluminio (liso y arrugado), madera, cartón, tela negra y vidrio. Esperamos que las brillantes funcionen y la tela negra no.
- **E4 Obstrucción y pérdidas forzadas:** las mejores para probar fallos. Estática (hoja de papel, papel calco, acrílico de color, botella con agua, tela fina), intermitente (pasar la mano por el haz cada 1 a 2 s, o un ventilador delante del emisor) y corte a mitad de mensaje (tapar el haz ~5 s después del primer fragmento; esperado: el emisor reintenta, y si agota los intentos el receptor descarta con `msgsDropped` y el mensaje siguiente llega limpio).
- **E5 Luz e IR ajeno:** oscuridad, tubo fluorescente, LED, incandescente/halógena (emite mucho IR) y luz solar directa. Y la más directa: apretar un **control remoto de TV** apuntando a los receptores, porque muchos usan NEC, el mismo protocolo físico que nosotros. Prueba que el checksum y el preámbulo filtran bien.

**29/10/2026 - Plan de pruebas: escenarios de protocolo (E6 a E10) y planilla**
Seguimos con los escenarios que pegan en el protocolo en sí:
- **E6 Nodo oculto:** B en el centro, A y C a los costados con una barrera opaca entre ellos, y los dos mandándole strings a B al mismo tiempo (`T 2 5` en A y en C). Es el caso que CSMA/CA **no** resuelve por carrier sense, porque A y C no se escuchan, y se va a notar en la caída de PDR y el aumento de reintentos. Después repetir sin la barrera para comparar. Sirve para justificar (o no) implementar RTS/CTS a futuro.
- **E7 Carga y contención:** triángulo a 50 cm con 1, 2 y 3 emisores simultáneos y mensajes de 1, 16 y 48 bytes. Hay que verificar que no haya inanición (ningún nodo con PDR cercano a 0).
- **E8 Broadcast:** `T B 10` desde A con un receptor cerca y otro cerca del límite de E1. Con `rxData + rxDup` sobre `txFrames` vemos si las 3 copias alcanzan o si conviene cambiar `IRETH_BCAST_REPEAT` (hay que reflashear).
- **E9 Fallas de nodo:** resetear el emisor o el receptor a mitad de un string, y mandarle un unicast a un nodo apagado (esperado: `txFail` sin bloquear al emisor).
- **E10 Larga duración:** `T 2 200` en modo contador (~15 min) y `T 1 50` desde B y C a la vez con strings. Buscamos que no haya cuelgues ni reinicios espontáneos.

Para registrar armamos una planilla (CSV) con una fila por corrida: fecha, hora, escenario, emisor, receptor, modo, bytes, `n`, distancia, ángulos, superficie/obstáculo, iluminación, `ok`, `ms`, goodput, los contadores del emisor y del receptor, integridad y observaciones. Los escenarios largos conviene correrlos desde la PC con `mon.py`/`step.py`, que dejan todo en un único log.
Orden que decidimos: **E1 primero** (define las distancias de todos los demás), después E4 y E5 (las que mejor prueban fallos), después E6 y E7 (límites del acceso al medio), y al final E2, E3, E8, E9 y E10.

**30/10/2026 - Repasando la documentación: cosas que no cierran**
Nos pusimos a leer todo junto (protocolo, bitácora y plan) para el informe final y encontramos inconsistencias que hay que corregir:
- **CRC-16 vs. XOR:** el Informe de Avance del 11/10 dice que el sistema valida la integridad con CRC-16, pero el protocolo usa un **checksum XOR simple** (en la comparación con Ethernet, que usa CRC-32, está bien puesto). Hay que corregir el informe. Esto además tiene consecuencias: un XOR detecta menos errores que un CRC.
- **Tabla de direcciones de `protocol.md`:** todavía figuran los roles del proyecto del año pasado (`0x1` Monitor ESP32, `0x2` Sensor, `0x3` Actuador). Hoy los nodos son A = `0x1` (Uno), B = `0x2` (Uno) y C = `0x3` (ESP32).
- **Velocidad en la tabla comparativa:** dice "~2 bytes / 600 ms ≈ ~3 bps útiles", pero 2 bytes cada 600 ms son más de 3 bytes por segundo, y lo que medimos en las pruebas del 25/10 fue entre 0,24 y 2 B/s de goodput según el caso. Hay que rehacer la cuenta con los datos medidos.

Estado: el protocolo está validado en el caso ideal y las pruebas están planificadas, pero **todavía no corrimos ningún escenario del plan** (E1 a E10). Eso es lo que sigue.

***


**11/10/2026 - Estado de la Prueba de Concepto**
Se entregó el Informe de Avance. El sistema valida la integridad de tramas con CRC-16 y corre bajo un esquema básico. Queda pendiente medir empíricamente la Tasa de Error de Paquete frente a distintas cargas e implementar las correcciones del buffer de fragmentos.

***
