# ⚡ SparkMiner con API HTTP + Dashboard TFT + Túnel SuperDMZ

![SparkMiner Dashboard](https://raw.githubusercontent.com/BOLANEGRA/SparkMiner/main/images/dashboard.png)
> Modificación del firmware original de **SparkMiner** que añade una **API HTTP compatible con AxeOS/ESP-Miner**, un **dashboard web estilo TFT** y **acceso remoto vía túnel SuperDMZ**.

**Autor original**: [SneezeGUI](https://github.com/SneezeGUI/SparkMiner)  
**Modificaciones**: [BOLANEGRA](https://github.com/BOLANEGRA)  
**Licencia**: GPL v3

---

## ✨ Características añadidas

### 🌐 API HTTP compatible con AxeOS/ESP-Miner
- **Endpoints**:
  - `GET /api/system/info` → Información general del minero
  - `GET /api/system/statistics` → Estadísticas detalladas
- **Compatible con**: HashWatcher, Home Assistant, AxeOS Dashboard, y cualquier cliente HTTP.
- **Formato JSON** idéntico al de ESP-Miner.

### 🎨 Dashboard web estilo TFT
- Réplica visual de la pantalla física de SparkMiner.
- **Diseño oscuro** con acentos naranjas apagados.
- **Responsive** (funciona en móvil, tablet y PC).
- **Auto-refresh** cada 5 segundos.
- **URL local**: `http://<IP_DEL_ESP32>/`

### 🎛️ Activación desde el portal
- Nuevo desplegable **"Stats API Server"** (`Enabled`/`Disabled`) en el portal de configuración.
- **Por defecto: Disabled** (no consume hashrate si no se activa).
- **Coste cero de hashrate** cuando está desactivado.

### 🔒 Túnel SuperDMZ para acceso remoto
- **Acceso HTTPS** desde cualquier lugar, sin abrir puertos en el router.
- **URL pública**: `https://tu-nombre.dmzgate.com`
- **Autenticación**: usuario + contraseña.
- **Sin necesidad de PC encendida** ni Raspberry Pi.

---

## 📊 Captura del Dashboard

![SparkMiner Dashboard](https://raw.githubusercontent.com/BOLANEGRA/SparkMiner/main/images/dashboard.png)

El dashboard muestra:
- **HashRate** actual en tiempo real.
- **Shares** aceptadas/rechazadas.
- **Best Difficulty**.
- **Uptime**, **Templates**, **Blocks**, **Sessions**.
- **Pool**, **Diff**, **IP**, **Ping**.
- **LEDs de estado**: SDC, WiFi, POOL.

---

## 🚀 Instalación paso a paso

### 📦 Requisitos

- **Hardware**: Wemos D1 R32 (o cualquier ESP32 compatible con SparkMiner).
- **Software**:
  - Python 3.10+
  - PlatformIO Core (`pip install platformio`)
  - Git
- **Cuenta SuperDMZ** (gratis): https://superdmz.com

### Paso 1: Clonar el repositorio

```bash
git clone https://github.com/BOLANEGRA/SparkMiner.git
cd SparkMiner
```

### Paso 2: Instalar PlatformIO

```bash
pip install platformio
```

Verifica:
```bash
pio --version
```

### Paso 3: Crear tu cuenta SuperDMZ

1. Ve a https://superdmz.com
2. Crea una cuenta gratuita.
3. Crea un **nuevo túnel**:
   - **Nombre**: `tu-nombre`
   - **Servidor**: el más cercano (ej: São Paulo)
   - **Protocolo**: HTTP/HTTPS
   - **Puerto local**: `80`
   - **Control de acceso**: Autenticado
   - **Usuario y contraseña**: a tu elección
4. Copia el **token de 48 caracteres** que te dará SuperDMZ.

### Paso 4: Editar el token en el código

Abre `src/config/wifi_manager.cpp` con un editor de texto.

**Busca** (`Ctrl+F`):

```cpp
const char* SUPERDMZ_TOKEN = "TU_TOKEN_SUPERDMZ_AQUI";
```

**Reemplaza** `TU_TOKEN_SUPERDMZ_AQUI` por tu token real:

```cpp
const char* SUPERDMZ_TOKEN = "a1b2c3d4e5f6...";
```

⚠️ **Importante**: el token debe tener exactamente 48 caracteres.

### Paso 5: Compilar el firmware

```bash
python devtool.py build -b esp32-headless
```

Espera a ver `[SUCCESS] Build completed!`

### Paso 6: Flashear el firmware

Conecta tu Wemos D1 R32 por USB.

```bash
python devtool.py flash -b esp32-headless
```

Selecciona el puerto cuando pregunte.

### Paso 7: Configurar el minero

El ESP32 arrancará en **modo portal de configuración**.

1. **Conéctate al WiFi** `SparkMiner_XXXX` (password: `minebitcoin`).
2. **Abre** `http://192.168.4.1`
3. **Rellena el formulario**:
   - **WiFi**: tu red doméstica
   - **BTC Wallet**: tu dirección de Bitcoin
   - **Worker Name**: `SparkMiner`
   - **Primary Pool URL**: `solo.ckpool.org`
   - **Primary Pool Port**: `3333`
   - **Primary Pool Password**: `x`
   - **Stats API Server**: **`Enabled`** ← **IMPORTANTE**
4. **Pulsa Save**.

### Paso 8: Verificar el funcionamiento

Abre el monitor serie:

```bash
python devtool.py monitor -b esp32-headless
```

Deberías ver algo como:

```
[WIFI] Connected! IP: 192.168.101.30
[API] Server started on port 80 (http://192.168.101.30/)
[TUNNEL] SuperDMZ iniciado correctamente
[SuperDMZ:ready] ONLINE: https://tu-nombre.dmzgate.com
[STRATUM] Authorized as 1...SparkMiner
[STATS] Hashrate: 715000 H/s
```

### Paso 9: Acceder al dashboard

**Local** (desde tu red WiFi):

```
http://192.168.101.30/
```

**Remoto** (desde cualquier lugar):

```
https://tu-nombre.dmzgate.com
```

---

## 📡 Endpoints de la API

### `GET /api/system/info`

Devuelve información general del minero:

```json
{
  "deviceModel": "ESP32-Headless",
  "firmwareVersion": "dev",
  "hostname": "SparkMiner",
  "hashRate": 715506.03,
  "hashRateString": "715.51 kH/s",
  "sharesAccepted": 0,
  "sharesRejected": 0,
  "bestDifficulty": 0.0042,
  "blocksFound": 0,
  "uptimeSeconds": 123,
  "sessionCount": 1,
  "pool": "solo.ckpool.org",
  "poolPort": 3333,
  "wallet": "bc1q...",
  "worker": "SparkMiner"
}
```

### `GET /api/system/statistics`

Devuelve estadísticas detalladas:

```json
{
  "sessionHashes": 64273341,
  "sessionShares": 0,
  "sessionAccepted": 0,
  "sessionRejected": 0,
  "sessionBlocks": 0,
  "sessionBestDifficulty": 0.013403822,
  "sessionTemplates": 3,
  "avgLatencyMs": 279,
  "lifetimeHashes": 143250791,
  "lifetimeShares": 0,
  "lifetimeAccepted": 0,
  "lifetimeRejected": 0,
  "lifetimeBlocks": 0,
  "lifetimeUptimeSeconds": 300,
  "bestDifficultyEver": 0.014719055
}
```

### `GET /`

Dashboard HTML estilo TFT.

---

## 🔧 Solución de problemas

### Error: `PlatformIO not found`

```bash
pip install platformio
```

Si `pio` no funciona, usa `python -m platformio`.

### Error: `Failed to connect to ESP32`

- Comprueba el cable USB.
- Baja la velocidad de flasheo:
  ```bash
  python -m esptool --chip esp32 --port COM7 --baud 115200 write-flash -z 0x0 firmware\v2.9.5-10-g8036985-dirty\esp32-headless_factory.bin
  ```
- Para chips antiguos (ESP32-D0WDQ6), usa `--baud 57600`.

### El dashboard no carga

- Verifica que `Stats API Server` está en `Enabled` en el portal.
- Comprueba la IP del ESP32 en el monitor serie.

### El túnel SuperDMZ no conecta

- Verifica que el token está bien copiado (48 caracteres).
- Comprueba que el túnel está activo en el panel de SuperDMZ.
- Reinicia el ESP32.

### Error: `HTTP 400` en el túnel

El token está mal o falta. Verifica en `wifi_manager.cpp`.

---

## 📊 Compatibilidad

| Chip | Funciona | Notas |
| :--- | :--- | :--- |
| **ESP32-D0WD-V3** (2019+) | ✅ Sí | Compatible, ~715 kH/s |
| **ESP32-D0WDQ6** (2016) | ⚠️ Parcial | Flashear a 57600 baud. Puede tener inestabilidad |

---

## 🙏 Agradecimientos

- **[SneezeGUI](https://github.com/SneezeGUI/SparkMiner)** por crear SparkMiner, el firmware base.
- **SuperDMZ** por el servicio de túnel.
- **Comunidad ESP32** por las librerías utilizadas (WiFiManager, ArduinoJson, etc.).

---

## 📄 Licencia

GPL v3 (heredada del proyecto original).

---

## 🔗 Enlaces

- **SparkMiner original**: https://github.com/SneezeGUI/SparkMiner
- **SuperDMZ**: https://superdmz.com
- **AxeOS / ESP-Miner** (referencia): https://github.com/skot/ESP-Miner
- **WiFiManager**: https://github.com/tzapu/WiFiManager