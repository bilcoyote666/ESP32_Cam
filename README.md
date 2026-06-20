# 📸 CámaraESP32 — OV5640 + MicroSD + Bluetooth

Firmware para **Freenove ESP32-S3-WROOM CAM (N16R8)** con cámara **OV5640 5MP**.
Captura fotos de alta resolución, las guarda en MicroSD y las transfiere vía **BLE 5.0** (iPhone/Mac) o **BT Classic SPP** (PC/Android).

---

## 🛒 Hardware Necesario

| Componente | Modelo | Enlace |
|---|---|---|
| **Placa** | Freenove ESP32-S3-WROOM CAM (N16R8) | Amazon/AliExpress |
| **Cámara** | OV5640 5MP módulo DVP 24-pin | AliExpress |
| **MicroSD** | SanDisk 32GB+ Class 10 UHS-I | Cualquier tienda |
| **Botón** | Pulsador momentáneo 6mm | Incluido en placa (GPIO0/BOOT) |

---

## ⚡ Conexiones de Hardware

### Cámara OV5640 → ESP32-S3 (interfaz DVP)
La Freenove ESP32-S3-WROOM tiene conector FPC de 24 pines para la cámara.
Conecta el módulo OV5640 directamente al conector de la placa.

> **IMPORTANTE:** Verifica que el conector del OV5640 sea compatible con la ranura de tu placa.
> El pinout está definido en `main/config.h`.

### MicroSD
- **Modo SDMMC 4-bit** (máxima velocidad ~20 MB/s)
- La ranura MicroSD está integrada en la placa Freenove
- Solo insertar la tarjeta formateada en **FAT32**

### Botón de disparo
- Usa el **botón BOOT** (GPIO0) ya incluido en la placa
- **Pulsación corta**: captura 1 foto
- **Pulsación larga (3s)**: modo ráfaga (3 fotos)

---

## 🔧 Setup del Entorno de Desarrollo

### Prerrequisitos

```bash
# 1. Instalar ESP-IDF v5.x
# https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/

# En macOS:
brew install cmake ninja dfu-util
git clone --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf
./install.sh esp32s3
. ./export.sh

# 2. Verificar instalación
idf.py --version  # debe ser >= 5.0
```

### Clonar dependencias (esp-who para face detection)

```bash
# En el directorio del proyecto:
mkdir -p components
git clone --recursive https://github.com/espressif/esp-who.git components/esp-who
git clone --recursive https://github.com/espressif/esp32-camera.git components/esp32-camera
```

---

## 🚀 Compilar y Flashear

```bash
# Navegar al proyecto
cd /Users/oscariglesias/Documents/Arduino/Camara

# Seleccionar target
idf.py set-target esp32s3

# Configurar (opcional — ajustar PSRAM, BT, etc.)
idf.py menuconfig

# Compilar
idf.py build

# Flashear (ajusta el puerto serie)
idf.py -p /dev/cu.usbserial-* flash monitor
```

> **Nota:** Para ver el puerto serie: `ls /dev/cu.usb*`

---

## 📱 Conectar desde el Móvil / PC

### iPhone / iPad / Mac (BLE)
1. Abre **nRF Connect** (App Store) o **LightBlue**
2. Busca el dispositivo **"CamaraESP32"**
3. Conéctate
4. Escribe en la característica **CONTROL**:
   - `0x01` → Lista de fotos (devuelve JSON en FILE_LIST)
   - `0x05` → Disparar foto ahora
5. Para descargar una foto:
   - Escribe el nombre en **FILE_REQUEST** (ej: `FOTO_20240101_120000_001.jpg`)
   - Escribe `0x02` en **CONTROL**
   - Lee los chunks en **FILE_DATA** hasta recibir `[0xFF, 0xFF]` (EOF)

### Android / PC Windows (BT Classic SPP)
1. Empareja el dispositivo **"CamaraESP32"** en la configuración BT
2. Conecta con cualquier app de terminal serie (ej: Serial Bluetooth Terminal)
3. Comandos disponibles:
   ```
   LIST         → JSON con todas las fotos
   GET:FOTO_xxx.jpg  → Descarga el archivo JPEG
   DEL:FOTO_xxx.jpg  → Borra el archivo
   CAPTURE      → Disparar foto
   STATUS       → Estado del sistema (SD libre, etc.)
   ```

### Script Python (PC/Mac) para descargar fotos automáticamente

```python
# Instalar: pip install bleak asyncio
# Ver scripts/download_photos.py (incluido en este proyecto)
import asyncio
from bleak import BleakClient

DEVICE_NAME = "CamaraESP32"
# ... (ver scripts/download_photos.py para implementación completa)
```

---

## 💡 Indicador LED (GPIO48)

| Patrón | Significado |
|---|---|
| Encendido fijo | Sistema listo |
| Parpadeo lento 1Hz | Detectando caras |
| Flash rápido x1 | Capturando foto |
| Parpadeo rápido 5Hz | Guardando en SD |
| Doble parpadeo cada 2s | Transfiriendo por BLE |
| 3 flashes rápidos + pausa | **Error de MicroSD** |
| LED apagado | **Error de cámara** |

---

## 🔍 Resoluciones Disponibles

Edita `main/config.h` para cambiar la resolución de captura:

```c
// Resolución de captura final (fotos guardadas en SD)
#define CAM_FRAMESIZE_CAPTURE  FRAMESIZE_UXGA     // 1600x1200 (por defecto)
// #define CAM_FRAMESIZE_CAPTURE  FRAMESIZE_QSXGA  // 2560x1920 (5MP, necesita más PSRAM)
// #define CAM_FRAMESIZE_CAPTURE  FRAMESIZE_FHD    // 1920x1080 (Full HD)
// #define CAM_FRAMESIZE_CAPTURE  FRAMESIZE_HD     // 1280x720 (HD)

// Calidad JPEG (10=mejor calidad, 63=menor tamaño)
#define CAM_JPEG_QUALITY_CAPTURE  12  // Máxima calidad (por defecto)
```

---

## 🧠 Face Detection

La detección de caras usa el modelo **MTMN** de **esp-who** (Espressif).

- Corre en **Core 0** a ~5-10 FPS en resolución QVGA (320x240)
- Cuando detecta una cara, **activa el autoenfoque** del OV5640
- La foto final se captura en alta resolución (UXGA o 5MP)
- **Auto-disparo desactivado por defecto** (solo AF automático)

Para activar el auto-disparo cuando se detecta una cara:
```c
// En main.cpp, línea:
static volatile bool s_face_auto_capture = false;
// Cambiar a:
static volatile bool s_face_auto_capture = true;
```

---

## 🐛 Troubleshooting

| Problema | Solución |
|---|---|
| Error de cámara al arrancar | Verifica el conector FPC y los pines en config.h |
| SD no monta | Formatear en FAT32, verificar voltaje (3.3V) |
| BLE no aparece en el móvil | Verifica que NimBLE está en el menuconfig |
| Face detection muy lenta | Asegurar 8MB PSRAM configurada en menuconfig |
| Foto borrosa | Esperar más tiempo al AF (aumentar CAM_AF_TIMEOUT_MS) |
| OOM (out of memory) | Reducir CAM_FRAMESIZE_CAPTURE a FRAMESIZE_UXGA |

---

## 📁 Estructura del Proyecto

```
Camara/
├── CMakeLists.txt          # Build raíz
├── partitions.csv          # Tabla de particiones (16MB flash)
├── sdkconfig.defaults      # Configuración por defecto ESP-IDF
├── idf_component.yml       # Dependencias (esp32-camera, esp-who)
├── README.md               # Este archivo
└── main/
    ├── CMakeLists.txt      # Build del componente principal
    ├── config.h            # ⭐ PINES y configuración central
    ├── main.cpp            # Punto de entrada, FreeRTOS tasks
    ├── camera.h/cpp        # Driver OV5640, modo dual, AF
    ├── face_detect.h/cpp   # Detección de caras (esp-who MTMN)
    ├── sd_storage.h/cpp    # MicroSD SDMMC 4-bit, FAT32
    ├── ble_transfer.h/cpp  # Servidor BLE GATT (NimBLE)
    ├── bt_classic.h/cpp    # BT Classic SPP
    ├── button.h/cpp        # Botón con debounce + pulsación larga
    └── led.h/cpp           # LED de estado con patrones
```

---

## 📊 Rendimiento Esperado

| Métrica | Valor |
|---|---|
| Tiempo de captura + SD | ~1.5-2.5 segundos |
| Velocidad transferencia BLE | ~200-400 KB/s |
| Face detection FPS | 5-10 FPS (QVGA) |
| Resolución foto | 1600x1200 (UXGA) por defecto |
| Tamaño foto JPEG | ~80-200 KB (calidad 12) |
| Capacidad MicroSD 32GB | ~200.000 fotos |

---

## 📄 Licencia

MIT License — Úsalo libremente para proyectos personales y comerciales.
