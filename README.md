# SparkMiner v2.8.0

**High-performance dual-core Bitcoin solo miner for ESP32 & ESP32-S3**

<img src="images/1767589853452.jpg" alt="SparkMiner Display" width="575">

SparkMiner is optimized firmware for ESP32-based boards with displays, delivering **~1+ MH/s** (pool-reported) using hardware-accelerated SHA-256 and pipelined assembly mining. Supports both ESP32 "Cheap Yellow Display" (CYD) boards and ESP32-S3 variants.

> **Solo Mining Disclaimer:** Solo mining on an ESP32 is a lottery. The odds of finding a block are astronomically low (~1 in 10^20 per hash at current difficulty). This project is for education, fun, and supporting network decentralization - not profit.

---

## Quick Start

### Option 1: Launcher + SD Card (Recommended for CYD Boards)

The easiest way to install and manage SparkMiner on CYD boards (1-USB or 2-USB variants):

**Step 1: Flash the Launcher (one-time)**
1. Go to [Bruce Launcher Web Flasher](https://bmorcelli.github.io/Launcher/webflasher.html)
2. Connect your CYD board via USB
3. Select your board type and click **Install**
4. The Launcher provides a boot menu for multiple firmwares

**Step 2: Prepare SD Card**
1. Format a microSD card as **FAT32**
2. Download `cyd-2usb_firmware.bin` (or your board variant) from [Releases](https://github.com/SneezeGUI/SparkMiner/releases)
3. Copy the `.bin` file to the SD card root
4. Create a `config.json` file (see Configuration section)
5. Insert SD card into CYD

**Step 3: Boot SparkMiner**
1. Power on the CYD - the Launcher menu appears
2. Select SparkMiner firmware from the SD card
3. SparkMiner loads your config and starts mining!

**Why use the Launcher?**
- Easy firmware updates - just replace the `.bin` on SD card
- Switch between multiple firmwares
- No need to re-flash via USB for updates
- Config persists on SD card

### Option 2: Direct USB Flashing

1. Download the latest `*_factory.bin` firmware from [Releases](https://github.com/SneezeGUI/SparkMiner/releases)
2. Flash using [ESP Web Flasher](https://esp.huhn.me/) or esptool:
   ```bash
   esptool.py --chip esp32 --port COM3 write_flash 0x0 cyd-2usb_factory.bin
   ```
3. Power on the board - it will create a WiFi access point
4. Connect to `SparkMiner-XXXX` WiFi and configure via the web portal

### Option 3: Build from Source

```bash
# Clone repository
git clone https://github.com/SneezeGUI/SparkMiner.git
cd SparkMiner

# Create virtual environment and install dependencies
python -m venv .venv
.venv\Scripts\activate  # Windows
# source .venv/bin/activate  # Linux/Mac
pip install platformio

# Use the interactive devtool (recommended)
devtool.bat          # Windows - interactive menu
python devtool.py    # Cross-platform

# Or build a specific board directly
python devtool.py build -b cyd-2usb
python devtool.py flash -b cyd-2usb
python devtool.py monitor

# All-in-one: build, flash, and monitor
python devtool.py all -b cyd-2usb
```

---

## Firmware Types

Understanding the difference between the firmware files:

- **`*_firmware.bin`**: The application only. Use this for **Launcher/SD card updates** or OTA updates. It does not include the bootloader.
- **`*_factory.bin`**: The complete image (Bootloader + Partition Table + App). Use this for **direct USB flashing** (Option 2) to a blank board or to restore a board.

---

## Upgrading

To upgrade from an older version:

1. **Via SD Card (Launcher):** Replace the `*_firmware.bin` file on your SD card with the new version (e.g., `cyd-2usb_firmware.bin`).
2. **Via USB:** Flash the new `*_factory.bin` using the interactive `devtool.py` or esptool.

> **Note:** NVS stats are persistent across standard reboots, but a full flash *might* clear NVS depending on your method. The SD card backup (`/stats.json`) ensures your lifetime totals can be restored.

---

## Hardware

### Supported Boards

| Board | Environment | Chip | Display | Hashrate | Notes |
|-------|-------------|------|---------|----------|-------|
| **ESP32-2432S028R** | `esp32-2432s028` | ESP32-WROOM-32 | 2.8" ILI9341 | ~715 KH/s | Standard CYD (1-USB), most common |
| **ESP32-2432S028R 2-USB** | `esp32-2432s028-2usb` | ESP32-WROOM-32 | 2.8" ILI9341 | ~715 KH/s | CYD with dual USB (Type-C + Micro) |
| **ESP32-2432S028R ST7789** | `esp32-2432s028-st7789` | ESP32-WROOM-32E | 2.8" ST7789 | ~715 KH/s | Alternative display driver variant |
| **Freenove FNK0104** | `esp32-s3-2432s028` | ESP32-S3 | 2.8" IPS | ~280-400 KH/s | S3 with 8MB Flash, 8MB PSRAM, SD_MMC |
| **LILYGO T-Display S3** | `lilygo-t-display-s3` | ESP32-S3 | 1.9" ST7789 | ~280-400 KH/s | 170x320, 8-bit parallel, 16MB Flash |
| **LILYGO T-Display V1** | `lilygo-t-display-v1` | ESP32-WROOM-32 | 1.14" ST7789 | ~715 KH/s | 135x240, SPI, compact |
| **ESP32-S3 DevKitC-1** | `esp32-s3-devkit` | ESP32-S3 | None | ~280-400 KH/s | Headless with PSRAM, USB-OTG |
| **ESP32 Headless** | `esp32-headless` | ESP32-WROOM-32 | None | ~715 KH/s | Any generic ESP32 dev board |
| **ESP32 Headless + LED** | `esp32-headless-led` | ESP32-WROOM-32 | RGB LED | ~715 KH/s | Headless with NeoPixel status LED |
| **Lolin S3 Mini** | `esp32-s3-mini` | ESP32-S3FH4R2 | RGB LED | ~280-400 KH/s | Compact with WS2812B LED status |

### Board Support Status

SparkMiner is optimized for CYD (Cheap Yellow Display) boards but now supports LILYGO T-Display boards too.

| Board | Status | Notes |
|-------|--------|-------|
| **ESP32-2432S028R 2.8" (CYD)** | ✅ Full | Primary target, 3 variants supported |
| **LILYGO T-Display S3** | ✅ Full | 170x320 ST7789 (8-bit parallel) |
| **LILYGO T-Display V1** | ✅ Full | 135x240 ST7789 (SPI) |
| **ESP32-WROOM-32 / DevKit** | ✅ Full | Use `esp32-headless` or `esp32-headless-led` |
| **ESP32-S3 DevKit** | ✅ Full | Use `esp32-s3-devkit` (headless) |
| **Wemos Lolin S3 Mini** | ✅ Full | Use `esp32-s3-mini` (RGB LED status) |
| **Weact S3 Mini** | ⚠️ Partial | May work with `esp32-s3-mini` |
| **Weact ESP32-D0WD-V3** | ⚠️ Partial | May work with `esp32-headless` |
| **LILYGO T-Dongle S3** | ⚠️ Partial | May work with `esp32-s3-mini` (LED only) |
| **Wemos Lolin S2 Mini** | ❌ None | ESP32-S2 single-core not supported |
| **LILYGO T-Display S3 AMOLED** | ❌ None | AMOLED not supported |
| **LILYGO T-QT Pro** | ❌ None | GC9107 display not supported |
| **LILYGO T-HMI** | ❌ None | ST7789 480x320 not configured |
| **ESP32-CAM** | ❌ None | No camera/display support |
| **M5-StampS3** | ❌ None | Not configured |
| **M5Stick-C / C-Plus** | ❌ None | Not configured (library conflicts) |
| **ESP32-C3 (all variants)** | ❌ None | Single-core RISC-V, no dual-core mining |
| **ESP32-S3/C3 0.42" OLED** | ❌ None | OLED (U8g2) not yet supported |
| **Waveshare ESP32-S3-GEEK** | ❌ None | LCD not configured |

**Legend:** ✅ Full support | ⚠️ May work (untested) | ❌ Not supported

> **Note:** SparkMiner focuses on maximum hashrate for CYD boards (~715 KH/s) using pipelined assembly SHA-256. For broader board support with lower hashrates, consider [NerdMiner](https://github.com/BitMaker-hub/NerdMiner_v2).

### Where to Buy

- **AliExpress:** Search "ESP32-2432S028" for CYD boards (~$4-16 USD)
- **Amazon:** Search "CYD ESP32 2.8 inch" or "Freenove ESP32-S3" (~$15-25 USD)
- **Freenove Store:** [FNK0104 ESP32-S3 Display](https://store.freenove.com/) (~$20 USD)

### Hardware Features

- **CPU:** Dual-core Xtensa LX6 @ 240MHz (or LX7 for S3)
- **Display:** 2.8" 320x240 TFT (ILI9341/ST7789)
- **Storage:** MicroSD card slot
- **Connectivity:** WiFi 802.11 b/g/n
- **RGB LED:** Status indicator
- **Button:** Boot button for interaction

---

## Configuration

SparkMiner can be configured in three ways (in order of priority):

### 1. SD Card Configuration (Recommended)

Create a `config.json` file on a FAT32-formatted microSD card:

```json
{
  "ssid": "YourWiFiName",
  "wifi_password": "YourWiFiPassword",
  "pool_url": "public-pool.io",
  "pool_port": 21496,
  "wallet": "bc1qYourBitcoinAddressHere",
  "worker_name": "SparkMiner-1",
  "pool_password": "x",
  "brightness": 100
}
```

#### Configuration Options

| Field | Required | Default | Description |
|-------|----------|---------|-------------|
| `ssid` | Yes | - | Your WiFi network name |
| `wifi_password` | Yes | - | Your WiFi password |
| `pool_url` | Yes | `public-pool.io` | Mining pool hostname |
| `pool_port` | Yes | `21496` | Mining pool port |
| `wallet` | Yes | - | Your Bitcoin address (receives payouts) |
| `worker_name` | No | `SparkMiner` | Identifier shown on pool dashboard |
| `pool_password` | No | `x` | Pool password (usually `x`) |
| `brightness` | No | `100` | Display brightness (0-100) |
| `rotation` | No | `1` | Screen rotation (0-3) |
| `invert_colors` | No | `false` | Invert display colors |
| `backup_pool_url` | No | - | Failover pool hostname |
| `backup_pool_port` | No | - | Failover pool port |
| `backup_wallet` | No | - | Wallet for backup pool |

### 2. WiFi Access Point Portal

If no SD card config is found, SparkMiner creates a WiFi access point:

1. **Connect** to WiFi network: `SparkMiner-XXXX` (password shown on display)
2. **Open browser** to `http://192.168.4.1`
3. You will see the **new dark-themed portal** with full configuration options:
    - Primary & Backup Pool settings
    - Display brightness, rotation, and color inversion
    - Target difficulty
4. **Configure** your settings, click **Save**, and the device will reboot and connect.

### 3. NVS (Non-Volatile Storage)

Configuration is automatically saved to flash memory after first successful setup. To reset:
- Long-press BOOT button (1.5s) during operation for 3-second countdown reset, OR
- Hold BOOT button for 5 seconds at power-on, OR
- Reflash the firmware

---

## Persistent Mining Stats

SparkMiner automatically saves mining statistics to ensure your lifetime totals are preserved across reboots and power cycles.

- **NVS Persistence:** Stats are saved to the device's non-volatile storage.
  - **Triggers:** First share found, 5 minutes after boot, and hourly thereafter.
  - **Data:** Lifetime hashes, shares (accepted/rejected), best difficulty, and blocks found.
- **SD Card Backup:** If an SD card is present, stats are also backed up to `/stats.json` for disaster recovery. This survives firmware updates and factory resets.
- **Reset:** A factory reset (long-press BOOT) will clear NVS stats. Delete `/stats.json` from the SD card to fully reset.

---

## HTTPS Stats Proxy (Advanced)

SparkMiner displays live Bitcoin price and network stats. These APIs use HTTPS, which is memory-intensive for the ESP32 and can cause stability issues or mining interruptions.

To fix this, SparkMiner supports an HTTP-to-HTTPS proxy that offloads SSL/TLS to an external server.

### Why use a proxy?
- **Stability:** Offloads heavy SSL/TLS encryption to an external server
- **Memory:** Saves ~30KB of RAM on the ESP32 for mining
- **Performance:** Prevents mining interruptions during stats updates

### Option 1: Self-Hosted Proxy (Recommended)

Run the proxy on any server, Raspberry Pi, or always-on computer on your network:

```bash
# Using Node.js (save scripts/cloudflare_stats_proxy.js as proxy.js)
npm install -g wrangler
wrangler dev proxy.js --port 8080

# Or use any HTTP-to-HTTPS proxy like:
# - nginx with proxy_pass
# - Caddy with reverse_proxy
# - mitmproxy
```

Configure SparkMiner with your local proxy:

```json
{
  "ssid": "YourWiFi",
  "password": "YourPassword",
  "wallet": "bc1q...",
  "stats_proxy_url": "http://192.168.1.100:8080"
}
```

### Option 2: Cloudflare Worker (Requires Custom Domain)

The `workers.dev` domain requires HTTPS, so you need a custom domain with HTTP enabled:

1. Deploy the worker from [`scripts/cloudflare_stats_proxy.js`](scripts/cloudflare_stats_proxy.js)
2. Add a custom domain to your worker in Cloudflare dashboard
3. In **SSL/TLS > Edge Certificates**, disable "Always Use HTTPS"
4. Configure SparkMiner:

```json
{
  "stats_proxy_url": "http://stats.yourdomain.com"
}
```

### Configuration Options

| Field | Description |
|-------|-------------|
| `stats_proxy_url` | HTTP proxy URL (e.g., `http://192.168.1.100:8080`) |
| `enable_https_stats` | Set to `true` to fetch HTTPS directly (uses more memory, less stable) |

---

## Pool Configuration

### Recommended Pools

| Pool | URL | Port | Fee | Notes |
|------|-----|------|-----|-------|
| **Public Pool** | `public-pool.io` | `21496` | 0% | Recommended, solo mining |
| **FindMyBlock EU** | `eu.findmyblock.xyz` | `3335` | 0% | Solo mining, EU server |
| **CKPool Solo** | `solo.ckpool.org` | `3333` | 0.5% | Solo mining |
| **Braiins Pool** | `stratum.braiins.com` | `3333` | 2% | Pooled mining |

### Bitcoin Address Formats

SparkMiner supports all standard Bitcoin address formats:

- **Bech32 (bc1q...)** - Native SegWit, lowest fees (recommended)
- **Bech32m (bc1p...)** - Taproot addresses
- **P2SH (3...)** - SegWit-compatible
- **Legacy (1...)** - Original format

> **Important:** Use YOUR OWN wallet address. Never use an exchange deposit address for mining.

---

## Button Controls

The BOOT button (closest to USB-C) provides these actions:

| Action | Function | Notes |
|--------|----------|-------|
| **Single click** | Cycle screens | Mining → Stats → Clock |
| **Double click** | Cycle rotation (0°→90°→180°→270°) | Rotation saved to NVS |
| **Triple click** | Toggle color inversion | Saved to NVS |
| **Long press (1.5s)** | Factory reset | 3-second countdown, release to cancel |
| **Hold at boot (5s)** | Factory reset | Alternative if UI is unresponsive |

> **Note:** Buttons remain responsive during mining thanks to a dedicated FreeRTOS task.

---

## Display Orientation

You can change the screen rotation by double-clicking the BOOT button or setting `"rotation"` in `config.json`.

| Rotation | Orientation | USB Position |
|----------|-------------|--------------|
| 0 | Portrait | Right side |
| 1 | Landscape | Bottom (default) |
| 2 | Portrait | Left side |
| 3 | Landscape | Top |

*Note: Portrait mode has a bottom status bar.*

---

## Display Screens

SparkMiner has 3 display screens. Press BOOT to cycle:

### Screen 1: Mining Status (Default)

```
┌─────────────────────────────────┐
│ SparkMiner v2.6     45C  [●][●] │
├─────────────────────────────────┤
│  687.25 KH/s          Shares    │
│                        12/12    │
│ Best     Hashes    Uptime       │
│ 0.0673   47.5M     2h 15m       │
│ Ping     32-bit    Blocks       │
│ 326ms    3         0            │
│                                 │
│ Pool: public-pool.io    12miners│
│ Diff: 0.0032            You: 1  │
│ IP: 192.168.1.109    Ping: 326ms│
└─────────────────────────────────┘
```

### Screen 2: Network Stats

Shows BTC price, block height, network hashrate, fees, and your contribution.

### Screen 3: Clock

Large time display with mining summary at bottom.

### Status Indicators

The display features color-coded indicators for quick health monitoring:

| Indicator | Green | Yellow | Red |
|-----------|-------|--------|-----|
| **Temperature** | <50°C | 50-70°C | >70°C |
| **WiFi Signal** | >-60dBm | -60 to -75dBm | <-75dBm |
| **Pool Latency** | <100ms | 100-300ms | >300ms |

---

## Performance

### Expected Hashrates

| Board | Device Display | Pool Reported | Power | Notes |
|-------|---------------|---------------|-------|-------|
| **ESP32-2432S028 (CYD)** | ~715-725 KH/s | ~715-725 KH/s | ~0.5W | Pipelined assembly v2 |
| **ESP32-S3 (Freenove)** | ~280 KH/s | ~400 KH/s | ~0.4W | Midstate caching v3 |
| **ESP32 Headless** | ~750 KH/s | ~750 KH/s | ~0.3W | No display overhead |

> **Note:** Pool-reported hashrate is typically higher than device display due to share submission timing and pool difficulty adjustments.

### Architecture

SparkMiner uses both ESP32 cores efficiently:

- **Core 1 (High Priority, 19):** Pipelined hardware SHA-256 mining using direct register access and assembly optimization
- **Core 0 (Low Priority, 1):** WiFi, Stratum protocol, display updates, and software SHA-256 backup mining

**v2.8.0 Features & Architecture:**
- **Persistent Stats:** Lifetime mining history preserved via NVS and SD card backups.
- **Enhanced Stability:** Struct alignment fixes and robust error handling.
- **Optimized Core Usage:**
  - Core 1: Pipelined assembly SHA-256 (v2) with unrolled loops.
  - Core 0: Network stack, Stratum, and UI management.

---

## Troubleshooting

### WiFi Issues

| Problem | Solution |
|---------|----------|
| Won't connect to WiFi | Check SSID/password, ensure 2.4GHz network (not 5GHz) |
| Keeps disconnecting | Move closer to router, check for interference |
| AP mode not appearing | Hold BOOT 5s at power-on, or long-press during operation |

### Display Issues

| Problem | Solution |
|---------|----------|
| White/blank screen | Try `esp32-2432s028-st7789` environment |
| Inverted colors | Triple-click to toggle, or set `invert_colors` in config.json |
| Flickering | Reduce SPI frequency in platformio.ini |

### Mining Issues

| Problem | Solution |
|---------|----------|
| 0 H/s hashrate | Check pool connection, verify wallet address |
| Shares rejected | Check wallet address format, pool may be down |
| High reject rate | Network latency issue, try different pool |
| "SHA-PIPE WARNING" | Normal during HTTPS requests, doesn't affect mining |

### Serial Debug

Connect via USB and monitor at 115200 baud:
```bash
pio device monitor
# or
screen /dev/ttyUSB0 115200
```

---

## Building from Source

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- Python 3.8+
- Git

### DevTool (Recommended)

SparkMiner includes a unified development tool that supports all boards with an interactive menu:

```bash
# Interactive menu - select board, build, flash, monitor
devtool.bat              # Windows
python devtool.py        # Cross-platform

# List all supported boards
python devtool.py --help

# Build specific board
python devtool.py build -b cyd-2usb
python devtool.py build -b freenove-s3

# Flash to specific port
python devtool.py flash -b cyd-2usb -p COM5

# Monitor serial output
python devtool.py monitor -p COM5

# All-in-one: build, flash, and monitor
python devtool.py all -b cyd-2usb -p COM5

# Build release firmware for all boards
python devtool.py release

# Flash custom firmware file (opens file browser)
python devtool.py        # Select [F] from menu
```

**ESP32-S3 Note:** The Freenove ESP32-S3 requires manual bootloader mode entry:
1. Hold **BOOT** button
2. Press and release **RESET** button
3. Release **BOOT** button
4. The display will be blank - this is normal in download mode

### Manual PlatformIO Commands

```bash
# List available environments
pio run --list-targets

# Build specific environment
pio run -e esp32-2432s028-2usb

# Build and upload
pio run -e esp32-2432s028-2usb -t upload

# Clean build
pio run -e esp32-2432s028-2usb -t clean

# Monitor serial output
pio device monitor
```

### Manual Flashing with esptool

If you need to flash manually without PlatformIO:

```bash
# ESP32 (CYD boards) - factory bin at 0x0
esptool.py --chip esp32 --port COM3 --baud 921600 \
    write_flash -z --flash-mode dio --flash-freq 40m \
    0x0 cyd-2usb_factory.bin

# ESP32-S3 (Freenove) - factory bin at 0x0
esptool.py --chip esp32s3 --port COM5 --baud 921600 \
    write_flash -z --flash-mode dio --flash-freq 80m \
    0x0 freenove-s3_factory.bin
```

### Project Structure

```
SparkMiner/
├── src/
│   ├── main.cpp              # Entry point
│   ├── config/               # WiFi & NVS configuration
│   ├── display/              # TFT display driver
│   ├── mining/               # SHA-256 implementations
│   │   ├── miner.cpp         # Mining coordinator
│   │   ├── sha256_hw.cpp     # Hardware SHA (registers)
│   │   └── sha256_pipelined.h # Pipelined assembly
│   ├── stats/                # Live stats & monitoring
│   └── stratum/              # Stratum v1 protocol
├── include/
│   └── board_config.h        # Hardware definitions
├── devtool.py                # Unified build/flash/monitor tool
├── devtool.bat               # Windows launcher
├── devtool.toml              # Board & project configuration
├── platformio.ini            # PlatformIO build settings
└── README.md
```

---

## FAQ

**Q: Will I actually mine a Bitcoin block?**

A: Extremely unlikely. At ~700 KH/s vs network ~500 EH/s, your odds per block are about 1 in 10^15. It's like winning the lottery multiple times. But someone has to mine blocks, and it could theoretically be you!

**Q: How much electricity does it use?**

A: About 0.5W, or ~4.4 kWh per year (~$0.50-1.00/year in electricity).

**Q: Can I mine other cryptocurrencies?**

A: No, SparkMiner only supports Bitcoin (SHA-256d). Other coins use different algorithms.

**Q: Why is my hashrate lower than expected?**

A: Display updates, WiFi activity, and live stats fetching briefly reduce hashrate. The EMA-smoothed display shows average performance.

**Q: Do I need an SD card?**

A: No, you can configure via the WiFi portal. SD card is just more convenient for headless setup.

**Q: Can I use this with a mining pool that pays regularly?**

A: Yes, but solo pools like Public Pool only pay if YOU find a block. For regular payouts, use a traditional pool, but the amounts will be negligible.

---

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit changes (`git commit -m 'Add amazing feature'`)
4. Push to branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

---

## Credits

- **Sneeze** - SparkMiner development
- **bmorcelli** - [Launcher](https://github.com/bmorcelli/Launcher) & bootloader magic
- **BitsyMiner** - Pipelined SHA-256 assembly inspiration
- **NerdMiner** - Stratum protocol reference
- **ESP32 Community** - Hardware documentation

---

## License

MIT License - see [LICENSE](LICENSE) file for details.

---

## Support

- **Issues:** [GitHub Issues](https://github.com/SneezeGUI/SparkMiner/issues)
- **Discussions:** [GitHub Discussions](https://github.com/SneezeGUI/SparkMiner/discussions)

If you find a block, consider donating to support development:
`bc1qkg83n8lek6cwk4mpad9hrvvun7q0u7nlafws9p`
