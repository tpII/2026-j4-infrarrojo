# Bitácora de Desarrollo y Resolución de Problemas — IREthernet 2026

> Documento técnico de registro de desarrollo, problemas de hardware/software enfrentados, soluciones aplicadas y validación empírica del protocolo IREthernet.

---

## Índice
1. [Resumen del Proyecto y Objetivo](#1-resumen-del-proyecto-y-objetivo)
2. [Problemas Enfrentados y Soluciones Técnicas](#2-problemas-enfrentados-y-soluciones-técnicas)
   - [2.1 Linker: Colisión de Símbolos (`multiple definition of ...`)](#21-linker-colisión-de-símbolos-multiple-definition-of-)
   - [2.2 Capa Física: El Mito del Pin D11 y el Hardware Timer2](#22-capa-física-el-mito-del-pin-d11-y-el-hardware-timer2)
   - [2.3 Alimentación Simulada Directa (Sin Jumpers) y Polaridad](#23-alimentación-simulada-directa-sin-jumpers-y-polaridad)
   - [2.4 Clasificación de Protocolo en IRremote 4.x: NEC vs ONKYO](#24-clasificación-de-protocolo-en-irremote-4x-nec-vs-onkyo)
   - [2.5 Timings de Capa de Enlace: Inter-Packet Gap, SIFS y AGC Recovery](#25-timings-de-capa-de-enlace-inter-packet-gap-sifs-y-agc-recovery)
   - [2.6 Transición Half-Duplex TX ➔ RX y Limpieza de Buffers](#26-transición-half-duplex-tx--rx-y-limpieza-de-buffers)
3. [Features y Comportamientos Validados en los Logs](#3-features-y-comportamientos-validados-en-los-logs)
4. [Análisis Forense de los Logs Reales Obtenidos](#4-análisis-forense-de-los-logs-reales-obtenidos)

---

## 1. Resumen del Proyecto y Objetivo

El objetivo principal es implementar un protocolo de capa de enlace y control de acceso al medio compartido (MAC) inspirado en Ethernet (**CSMA/CA** + **Stop-and-Wait ARQ**) sobre un canal infrarrojo inalámbrico broadcast, permitiendo que múltiples nodos idénticos (Arduino Uno/Nano y ESP32) se comuniquen de forma peer-to-peer sin requerir una topología maestro-esclavo.

---

## 2. Problemas Enfrentados y Soluciones Técnicas

### 2.1 Linker: Colisión de Símbolos (`multiple definition of ...`)

#### El Problema
Al compilar con `arduino-cli`, el compilador arrojó cientos de errores de linkeo (`ld returned 1 exit status` con `multiple definition of FeedbackLEDControl`, `IrReceiver`, `IrSender`, etc.).

#### Causa Raíz
A diferencia de librerías clásicas de C++, la versión 4.x de `IRremote` adoptó una arquitectura *"header-only / inline implementation"* en `IRremote.hpp`. Al incluir `#include <IRremote.hpp>` dentro del encabezado `IREthernet.h`, tanto `node_A.ino` como `IREthernet.cpp` compilaron una copia independiente de las funciones y variables globales de IRremote. En la fase de enlazado (`ld`), el linker encontró los mismos símbolos generados en dos objetos (`.o`) distintos.

#### Solución
- Se eliminó `#include <IRremote.hpp>` de `IREthernet.h`. El header de IREthernet solo expone la API pública con tipos estándar de C++ (`uint8_t`, `bool`, etc.).
- Se incluyó `IRremote.hpp` **únicamente** dentro de `IREthernet.cpp` (una sola unidad de traducción).

---

### 2.2 Capa Física: El Mito del Pin D11 y el Hardware Timer2

#### El Problema
El emisor IR no emitía fotones (verificado inicialmente con la cámara del teléfono y de la Mac: el LED no destellaba).

#### Causa Raíz
El microcontrolador **ATmega328P** (Arduino Uno/Nano) no genera la portadora de modulación de 38 kHz por software (bit-banging), ya que consumiría todo el procesamiento de la CPU. En su lugar, utiliza el temporizador periférico **Timer2** por hardware.  
En la arquitectura del ATmega328P, la salida comparadora `OC2B` del Timer2 está físicamente conectada por silicio al **Pin Digital 3 (PD3)**. Aunque por software se intentaba invocar `IrSender.begin(11)`, la biblioteca `IRremote` para AVR compila obligatoriamente el PWM por hardware ruteado al pin **D3**. En el pin 11 nunca se generaba la modulación de 38 kHz.

#### Solución
Se reasignó la señal del emisor al pin nativo **D3**.

---

### 2.3 Alimentación Simulada Directa (Sin Jumpers) y Polaridad

#### El Problema
El objetivo pedagógico era insertar los módulos directamente en los headers hembra del Arduino sin protoboard ni cables puente (jumpers). Inicialmente el receptor no encendía su LED testigo y el emisor no transmitía.

#### Causa Raíz
Los pines adyacentes a la señal fueron configurados como pines de alimentación simulada (`OUTPUT HIGH` para VCC y `OUTPUT LOW` para GND). Sin embargo, el orden físico de las patitas de los módulos del fabricante chino no coincidía con el código original:
- **Módulo Receptor:** Pines impresos `Y`, `R`, `G` (Yellow = Señal, Red = VCC, Ground = GND).  
  Al clavarlo en D2-D3-D4, la patita `R` (VCC) quedó en D3 y `G` (GND) en D4. El software configuraba D3 como LOW y D4 como HIGH (polaridad inversa).
- **Módulo Emisor:** Pines `S`, `[Medio]`, `-`.  
  Al clavarlo en D3-D4-D5, la patita `Medio` (VCC) quedó en D4 y `-` (GND) en D5.

#### Solución
Se remapearon los pines y la lógica de polarización en el `setup()`:
```cpp
// Emisor en pines D3, D4, D5:
pinMode(3, OUTPUT); // D3: Señal TX (S)
pinMode(4, OUTPUT); digitalWrite(4, HIGH); // D4: VCC (Medio)
pinMode(5, OUTPUT); digitalWrite(5, LOW);  // D5: GND (-)

// Receptor en pines D8, D9, D10:
pinMode(8, INPUT);  // D8: Señal RX (Y)
pinMode(9, OUTPUT); digitalWrite(9, HIGH); // D9: VCC (R)
pinMode(10, OUTPUT); digitalWrite(10, LOW); // D10: GND (G)
```

---

### 2.4 Clasificación de Protocolo en IRremote 4.x: NEC vs ONKYO

#### El Problema
El receptor detectaba la presencia óptica del paquete (`LED indicador del sensor encendía`), pero la capa IREthernet descartaba la trama con timeout: `[IRETH] Timeout esperando addr/type pkt`.

#### Causa Raíz
Al inspeccionar los paquetes con logs crudos de depuración, se observó:
```text
[DEBUG RX] proto=7 raw=0xAA5555AA addr=0xAA cmd=0x55  <-- Preamble (clasificado como NEC = 7)
[DEBUG RX] proto=8 raw=0xFF00DE21 addr=0x21 cmd=0x00  <-- Payload (clasificado como ONKYO = 8)
```
El protocolo NEC estándar utiliza una dirección de 8 bits y su complemento a 8 bits. Cuando transmitimos campos binarios arbitrarios como `(dst << 4) | src` donde la dirección no es estrictamente complementaria a la norma de electrónica de consumo tradicional, la biblioteca `IRremote` 4.x clasifica el paquete internamente bajo el enum `ONKYO` (protocolo 8) en lugar de `NEC` (protocolo 7).  
El código de recepción filtraba con `if (proto == NEC)`, descartando el segundo paquete.

#### Solución
Se amplió el criterio de aceptación del filtro de demodulación para permitir variantes compatibles de modulación por distancia de pulso (NEC y ONKYO):
```cpp
if (raw != 0 && (proto == NEC || proto == ONKYO)) { ... }
```

---

### 2.5 Timings de Capa de Enlace: Inter-Packet Gap, SIFS y AGC Recovery

#### El Problema
Al transmitir tramas compuestas por múltiples paquetes NEC consecutivos, el receptor decodificaba el preámbulo pero perdía los paquetes subsiguientes por timeout o error de checksum.

#### Causa Raíz
1. **Control Automático de Ganancia (AGC) del Receptor:**  
   Los receptores IR integrados (tipo VS1838B) tienen un circuito de ganancia automática interno. Ráfagas infrarrojas prolongadas sin tiempo de reposo saturan el transistor óptico. Un paquete NEC tarda aproximadamente 67 ms en emitirse. Con un gap de solo 50 ms entre paquetes, el AGC no se estabilizaba para el paquete siguiente.
2. **SIFS (Short Inter-Frame Space) insuficiente:**  
   El SIFS inicial era de 50 ms. El emisor todavía estaba apagando la portadora del último paquete cuando el receptor ya estaba disparando el ACK, generando colisión temporal con el propio canal.

#### Solución
- Se incrementó el **Inter-Packet Gap (`IRETH_INTER_PKT_MS`)** a **100 ms**.
- Se incrementó el **SIFS (`IRETH_SIFS_MS`)** a **120 ms**.
- Se ajustó el **ACK Timeout (`IRETH_ACK_TIMEOUT_MS`)** a **2000 ms** para dar margen completo al ciclo de ida y vuelta.

---

### 2.6 Transición Half-Duplex TX ➔ RX y Limpieza de Buffers

#### El Problema
El emisor transmitía correctamente y el receptor recibía y contestaba el ACK, pero el emisor nunca registraba la llegada del ACK (`Sin ACK (timeout)`). Además, en reintentos posteriores el receptor quedaba "sordo".

#### Causa Raíz
En el microcontrolador ATmega328P, el Timer2 conmuta entre generar PWM (en emisión) y muestrear pulsos (en recepción).  
Al finalizar la transmisión de un frame, la biblioteca ejecutaba `IrReceiver.restartAfterSend()`. Sin embargo, esto dejaba el buffer de decodificación sucio con el último pulso remanente emitido por el propio LED del nodo. Al no invocarse explícitamente `IrReceiver.resume()`, la interrupción de captura quedaba bloqueada esperando que la CPU leyera el resultado anterior, imposibilitando la captura del preámbulo entrante del ACK.

#### Solución
En `IREthernet.cpp`, dentro de `sendFrame()`, se agregó la secuencia explícita de estabilización y reinicio de buffer:
```cpp
delay(20);             // Disipación de remanente óptico
IrReceiver.start();    // Habilita interrupción de Timer
IrReceiver.resume();   // Limpia buffer de recepción y rearma máquina de estados
```

---

### 2.7 Fragmentación de Mensajes y el Problema del ACK Perdido (Deduplicación)

#### El Problema
Al implementar la fragmentación de strings largos (mensajes de más de 8 bytes partidos en múltiples tramas de 7 bytes) y mover físicamente la placa durante la transmisión, el mensaje reconstruido en el receptor contenía fragmentos repetidos.  
Por ejemplo, enviando `"Hola fondo norte"` (16 bytes), el receptor reconstruyó `"Hola fondo norndo norte"` (23 bytes), duplicando el fragmento `"ndo nor"`.

#### Causa Raíz (Anatomía del ACK Perdido en Stop-and-Wait ARQ)
1. El emisor transmitió el Fragmento 1 (`seq=4`, `"ndo nor"`).
2. El receptor lo recibió correctamente, lo agregó a su buffer de ensamblado y emitió el `ACK seq=4`.
3. Al mover la placa, el haz óptico del ACK se desalineó y nunca llegó al emisor.
4. El emisor, tras agotar el timeout de ACK, interpretó que el paquete no había llegado y retransmitió el Fragmento 1 (`seq=4`).
5. El receptor recibió el reintento. Como no contaba con un filtro de números de secuencia pasados, trató la retransmisión como si fuese un paquete nuevo, concatenando `"ndo nor"` por segunda vez en el buffer.

#### Solución: Módulo `FrameDeduplicator`
Se implementó un deduplicador formal por emisor (`src`) de 16 estados (`last_seq[16]`):
- Al recibir una trama DATA, el receptor **siempre reenvía el ACK** hacia el emisor para destrabarlo.
- Si `isDuplicate(src, seq)` es verdadero (la secuencia recibida es igual a la última procesada de ese nodo), la capa de enlace **descarta la trama (`return false`)** sin entregarla a la aplicación.
- Si la secuencia es nueva, se registra con `updateSequence(src, seq)` y se entrega a la capa superior.

---

## 3. Features y Comportamientos Validados en los Logs

A través de las pruebas en consola serie, se validaron formalmente las siguientes propiedades del stack:

| Capa / Mecanismo | Comportamiento Comprobado |
|---|---|
| **Estructuración de Trama (Framing)** | Ensamblado y desensamblado de tramas con preámbulo de sincronización `0xAA55`, direccionamiento, longitud, carga útil y delimitador de fin de trama `0xEE`. |
| **Control de Integridad (FCS / Checksum)** | Cálculo y validación de Checksum XOR de extremo a extremo. Los paquetes corruptos son descartados inmediatamente sin procesar. |
| **Filtrado MAC (Address Filtering)** | Los nodos rechazan silenciosamente tramas dirigidas a otras direcciones unicast (ej: el Nodo 2 ignorando tramas dirigidas a `0x3`). |
| **Soporte Unicast y Broadcast** | Difusión dirigida a un nodo específico (con requerimiento de ACK) y difusión masiva a `0xF` (sin ACK, procesada por todos los nodos). |
| **Stop-and-Wait ARQ** | El transmisor retiene el flujo de la aplicación hasta recibir la confirmación positiva (ACK) referenciada con el mismo número de secuencia (`seq`). |
| **Retransmisión por Pérdida (Retries)** | Ante la ausencia de ACK (por interferencia o pérdida de paquete), el emisor retransmite automáticamente hasta `MAX_RETRY=5` intentos. |
| **Deduplicación de Secuencia** | Descarte de tramas duplicadas ante pérdida de ACK en el canal de retorno, evitando corrupción del flujo de datos superior. |
| **Fragmentación y Reensamblado (MTU)** | Segmentación de strings largos en fragmentos de hasta 7 bytes con flags `CONT` / `END`, reconstrucción en recepción y volcado HEX + ASCII. |
| **Detección de Nodo Inalcanzable** | Tras agotar los reintentos hacia un nodo apagado, el stack aborta la transacción y notifica el error a la capa de aplicación. |

---

## 4. Análisis Forense de los Logs Reales Obtenidos

A continuación se analizan los eventos exactos capturados en la sesión de pruebas:

### Caso A: Transmisión Unicast Exitosa (Nodo 1 ➔ Nodo 2)
```text
[APP] TX → dst=0x2 contador=0
[IRETH] TX: dst=0x2 seq=0 len=2 intento=1
[DEBUG TX] Preamble detectado mientras esperaba ACK
[IRETH] ACK recibido. TX exitoso
```
* **Explicación:** El Nodo 1 envía secuencia 0 al Nodo 2. El Nodo 2 recibe la trama en tiempo y forma, responde el ACK tras el SIFS. El Nodo 1 detecta el preámbulo de retorno, valida que el ACK corresponde a `seq=0` y confirma la entrega en el primer intento.

---

### Caso B: Recuperación Automática por Pérdida de Trama (Stop-and-Wait ARQ)
```text
[APP] TX → dst=0x2 contador=3
[IRETH] TX: dst=0x2 seq=3 len=2 intento=1
[IRETH] Sin ACK (timeout). Reintento 1
[IRETH] TX: dst=0x2 seq=3 len=2 intento=2
[DEBUG TX] Preamble detectado mientras esperaba ACK
[IRETH] ACK recibido. TX exitoso
```
Simultáneamente en el receptor (Nodo 2):
```text
[DEBUG RX] proto=7 raw=0xAA5555AA addr=0xAA cmd=0x55
[IRETH] Timeout esperando checksum pkt
[DEBUG RX] proto=7 raw=0xAA5555AA addr=0xAA cmd=0x55
[IRETH] Frame recibido: src=0x1 dst=0x2 type=0 seq=3 len=2
[IRETH] Enviando ACK seq=3 a dst=0x1
[APP] RX ← src=0x1 seq=3 len=2 cmd=0x3 data=0x3
```
* **Explicación:** En el intento 1, un pulso del paquete final de checksum no llegó al receptor (`Timeout esperando checksum pkt`). El receptor descartó la trama dañada y **no emitió ACK**. El transmisor agotó el timeout de espera y disparó el **intento 2**. El intento 2 entró limpio, el receptor envió el ACK y la transmisión concluyó con éxito transparente para la aplicación.

---

### Caso C: Filtrado de Direcciones y Nodo Inalcanzable (Nodo 1 ➔ Nodo 3)
```text
[APP] TX → dst=0x3 contador=4
[IRETH] TX: dst=0x3 seq=4 len=2 intento=1
[IRETH] Sin ACK (timeout). Reintento 1
... (reintentos 2 al 5) ...
[IRETH] ERROR: Max reintentos alcanzados. TX fallido
```
Simultáneamente en el Nodo 2 (oyente del canal):
```text
[DEBUG RX] proto=7 raw=0xAA5555AA addr=0xAA cmd=0x55
[IRETH] Frame para otro nodo (dst=0x3), ignorando
```
* **Explicación:** Como el medio es un éter inalámbrico compartido, el Nodo 2 capturó todas las transmisiones del Nodo 1 hacia el Nodo 3. Al inspeccionar el encabezado de capa 2 y comprobar que la dirección destino (`0x3`) no coincide con la propia (`0x2`), descartó el paquete sin responder ACK. Como el Nodo 3 no estaba conectado, el transmisor reintentó 5 veces y abortó correctamente.
