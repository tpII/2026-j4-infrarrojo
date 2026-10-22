# Setup Arduino IDE en macOS — IREthernet 2026

## Índice

1. [Instalar Arduino IDE 2](#1-instalar-arduino-ide-2)
2. [Driver USB-Serial (CH340 / CP2102)](#2-driver-usb-serial)
3. [Agregar soporte para ESP32](#3-agregar-soporte-para-esp32)
4. [Instalar librerías necesarias](#4-instalar-librerías-necesarias)
5. [Conectar los 3 MCUs via USB Hub](#5-conectar-los-3-mcus-via-usb-hub)
6. [Abrir 3 instancias del IDE](#6-abrir-3-instancias-del-ide)
7. [Cargar la librería IREthernet](#7-cargar-la-librería-irethernet)
8. [Compilar y cargar cada sketch](#8-compilar-y-cargar-cada-sketch)
9. [Troubleshooting frecuente en macOS](#9-troubleshooting-frecuente-en-macos)

---

## 1. Instalar Arduino IDE 2

1. Ir a [arduino.cc/en/software](https://www.arduino.cc/en/software)
2. Descargar **Arduino IDE 2.x** para macOS (`.dmg`)
3. Abrir el `.dmg` y arrastrar `Arduino IDE.app` a la carpeta `Aplicaciones`
4. Primera vez que lo abrís: click derecho → **Abrir** (para bypasear Gatekeeper)

---

## 2. Driver USB-Serial

Los Arduinos clones (Uno, Nano baratos) y el Wemos D1 R32 suelen usar el chip **CH340**  
como conversor USB-Serial. macOS no trae este driver incluido.

### Cómo saber qué chip tiene tu placa

Con la placa conectada, correr en Terminal:

```bash
ls /dev/cu.*
```

- Si aparece `/dev/cu.usbserial-XXXX` → tiene **CH340** (necesitás driver)
- Si aparece `/dev/cu.usbmodem-XXXX` → tiene **ATmega16U2** (no necesita driver extra)
- Si aparece `/dev/cu.SLAB_USBtoUART` → tiene **CP2102** (ver abajo)

### Instalar driver CH340

**Opción A — Homebrew (recomendado):**

```bash
brew install --cask wch-ch34x-usb-serial-driver
```

Luego reiniciar la Mac. Después del reinicio aparecerá una notificación de Privacidad  
→ ir a **Configuración del Sistema → Privacidad y seguridad → permitir extensión de sistema de WCH**.

**Opción B — Manual:**

Descargar de [github.com/WCHSoftGroup/ch34xser_macos](https://github.com/WCHSoftGroup/ch34xser_macos/releases)  
e instalar el `.pkg`. Luego ir a **Configuración del Sistema → Privacidad y seguridad** y habilitarlo.

> [!WARNING]
> En macOS 14 (Sonoma) y 15 (Sequoia) los drivers de kernel extensions están muy restringidos.  
> Si después de instalar y reiniciar el puerto no aparece, puede ser necesario arrancar en  
> **Modo Recuperación** y ejecutar: `systemextensionsctl developer on`.

### Driver CP2102 (Silicon Labs)

Si tu placa tiene CP2102 (aparece como `SLAB_USBtoUART`):

```bash
brew install --cask silicon-labs-vcp-driver
```

O descargar de [silabs.com/developers/usb-to-uart-bridge-vcp-drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers).

---

## 3. Agregar soporte para ESP32

1. Abrir Arduino IDE
2. Ir a **Arduino IDE → Preferences** (o ⌘,)
3. En el campo **"Additional boards manager URLs"**, agregar esta URL:

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

4. Ir a **Tools → Board → Boards Manager**
5. Buscar `esp32` y instalar **"esp32 by Espressif Systems"** (versión ≥ 3.0)
6. Seleccionar la board: **Tools → Board → esp32 → "WEMOS D1 R32"**  
   (si no aparece "WEMOS D1 R32", usar "ESP32 Dev Module")

> [!NOTE]
> La primera instalación descarga ~200 MB. Esperar a que termine antes de seguir.

---

## 4. Instalar librerías necesarias

Ir a **Tools → Manage Libraries** y buscar/instalar:

| Librería | Versión mínima | Autor | Usada en |
|----------|---------------|-------|----------|
| `IRremote` | 4.3.0 | Armin Joachimsmeyer | Todos los nodos |

> [!IMPORTANT]
> Instalar IRremote versión **4.x**, no la versión 2.x o 3.x. La API cambió significativamente.

---

## 5. Conectar los 3 MCUs via USB Hub

Con el hub USB conectado a la Mac, conectar los 3 dispositivos uno por vez e identificar  
qué puerto le corresponde a cada uno.

### Identificar los puertos

**Paso 1:** Con ningún Arduino conectado:
```bash
ls /dev/cu.*
```

**Paso 2:** Conectar el **Nodo A** y correr de nuevo:
```bash
ls /dev/cu.*
```
El puerto nuevo que apareció es el del Nodo A. Anotarlo. Ejemplo: `/dev/cu.usbserial-1410`

**Paso 3:** Repetir para el **Nodo B** y el **Nodo C (ESP32)**.

### Ejemplo de resultado típico

```
/dev/cu.usbserial-1410    ← Nodo A
/dev/cu.usbserial-1420    ← Nodo B
/dev/cu.usbserial-1430    ← Nodo C (o /dev/cu.SLAB_USBtoUART3)
```

> [!TIP]
> Los puertos pueden cambiar de nombre si desconectás y volvés a conectar, o si usás  
> otro puerto del hub. Para evitar confusiones, conectar siempre en el mismo orden  
> y al mismo puerto físico del hub.

---

## 6. Abrir 3 instancias del IDE

Arduino IDE 2 permite abrir múltiples ventanas. Procedimiento:

1. Abrir el sketch del **Nodo A** (`node_A/node_A.ino`) → va a abrir la ventana principal
2. **File → New Window** (o ⌘⇧N) → abrir el sketch del **Nodo B** (`node_B/node_B.ino`)
3. **File → New Window** → abrir el sketch del **Nodo C** (`node_C/node_C.ino`)

En cada ventana, seleccionar:
- La **board** correcta (Arduino Uno o WEMOS D1 R32)
- El **puerto** correcto identificado en el paso anterior

Así tenés los 3 Serial Monitors abiertos simultáneamente a 115200 baud y podés ver  
el log de cada nodo en tiempo real.

---

## 7. Cargar la librería IREthernet

La librería `IREthernet` (carpeta `IREthernet/`) no está en el Library Manager; hay que  
instalarla manualmente.

### Opción A — Copiar a la carpeta de librerías (recomendado)

```bash
# La ruta por defecto de las librerías en macOS es ~/Documents/Arduino/libraries/
cp -r /Users/pujasku/facultad/5A/TDP2/tests/2026-J1-IREthernet/IREthernet \
      ~/Documents/Arduino/libraries/IREthernet
```

Reiniciar Arduino IDE. La librería aparecerá en **Sketch → Include Library → IREthernet**.

### Opción B — Symlink (más cómodo para desarrollo)

```bash
ln -s /Users/pujasku/facultad/5A/TDP2/tests/2026-J1-IREthernet/IREthernet \
      ~/Documents/Arduino/libraries/IREthernet
```

Con symlink, los cambios en el archivo fuente original se reflejan inmediatamente  
sin necesidad de copiar de nuevo.

---

## 8. Compilar y cargar cada sketch

### Orden de carga

Podés cargar los sketches en cualquier orden. Cada nodo esperará comandos por el Serial Monitor una vez iniciado.

### Para cada sketch

1. Seleccionar la board correcta en **Tools → Board**
2. Seleccionar el puerto correcto en **Tools → Port**
3. Click en **Upload** (→) o ⌘U
4. Esperar a que diga **"Done uploading"**
5. Abrir Serial Monitor con **Tools → Serial Monitor** (⌘⇧M) y seleccionar **115200 baud**

### Verificar que funciona

En el Serial Monitor de cada nodo deberías ver al arrancar:

**Nodo A (Arduino):**
```
============================
  IREthernet — Nodo 0x1     
============================
[IRETH] Iniciado. addr=0x1 sendPin=3 recvPin=8
```

**Nodo B (Arduino):**
```
============================
  IREthernet — Nodo 0x2     
============================
[IRETH] Iniciado. addr=0x2 sendPin=3 recvPin=8
```

**Nodo C (ESP32):**
```
====================================
  IREthernet — Nodo 0x3 (ESP32)     
====================================
[IRETH] Iniciado. addr=0x3 sendPin=17 recvPin=16
```

---

## 9. Troubleshooting frecuente en macOS

### Puerto no aparece en Tools → Port

- El driver CH340 no está instalado o no fue aprobado en Privacidad y Seguridad
- Probar otro cable USB (muchos cables son solo de carga, sin datos)
- Probar otro puerto del hub
- En Terminal: `ls /dev/cu.*` y verificar si el puerto aparece allí aunque no en el IDE

### "avrdude: stk500v2_getsync: timeout"

- El Arduino Nano barato puede necesitar seleccionar **Tools → Processor → ATmega328P (Old Bootloader)**

### ESP32: "Failed to connect to ESP32: Timed out waiting for packet"

- Antes de hacer click en Upload, mantener presionado el botón **BOOT** del ESP32
- Soltarlo cuando aparezca "Connecting..." en la barra de progreso
- El Wemos D1 R32 suele necesitar esto

### La librería IREthernet no compila en ESP32

- Verificar que la versión de IRremote instalada sea ≥ 4.3.0
- En `IREthernet.cpp`, la línea `analogRead(A0)` para el seed del random:  
  en ESP32, `A0` puede no estar definido → si hay error, cambiar a `analogRead(36)` o `esp_random()`

### El Serial Monitor muestra basura o caracteres extraños

- Verificar que la velocidad sea **115200 baud** en ambos extremos (Serial Monitor y código)

### Múltiples Serial Monitors en paralelo

- Arduino IDE 2 permite un Serial Monitor por ventana → abrir una ventana por nodo
- Si querés ver los 3 en la misma pantalla, usar `screen` en Terminal:

```bash
# Ventana 1 (Sensor):
screen /dev/cu.usbserial-1410 115200

# Ventana 2 (Actuador):
screen /dev/cu.usbserial-1420 115200

# Para salir de screen: Ctrl+A, luego K, luego Y
```
