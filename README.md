# SparkMiner

**High-performance dual-core Bitcoin solo miner for ESP32**

<img src="images/3CYD-V1-UI.jpg" alt="SparkMiner Display" width="575">

SparkMiner is optimized firmware for the ESP32-2432S028 "Cheap Yellow Display" (CYD) board, delivering ~700 KH/s using hardware-accelerated SHA-256 and a pipelined assembly mining loop.

> **Solo Mining Disclaimer:** Solo mining on an ESP32 is a lottery. The odds of finding a block are astronomically low (~1 in 10^20 per hash at current difficulty). This project is for education, fun, and supporting network decentralization - not profit.

---

## Quick Start

### Option 1: Pre-built Firmware (Easiest)

1. Download the latest firmware from [Releases](https://github.com/SneezeGUI/SparkMiner/releases)
2. Flash using [ESP Web Flasher](https://esp.huhn.me/) or esptool:
   ```bash
   esptool.py --chip esp32 --port COM3 write_flash 0x0 SparkMiner_factory.bin
   ```
3. Power on the board - it will create a WiFi access point
4. Connect to `SparkMiner-XXXX` WiFi and configure via the web portal

### Option 2: Build from Source

```bash
# Clone repository
git clone https://github.com/SneezeGUI/SparkMiner.git
cd SparkMiner

# Install PlatformIO CLI (if not installed)
pip install platformio

# Build and flash (auto-detects USB port)
pio run -e esp32-2432s028-2usb -t upload

# Monitor serial output
pio device monitor
```

---

## Hardware

### Supported Boards

| Board | Environment | Notes |
|-------|-------------|-------|
| ESP32-2432S028R (CYD 2.8") | `esp32-2432s028` | Most common, ILI9341 display |
| ESP32-2432S028R 2-USB | `esp32-2432s028-2usb` | Has 2 USB ports, better power stability |
| ESP32-2432S028R ST7789 | `esp32-2432s028-st7789` | Some boards use ST7789 driver |
| ESP32-S3 DevKit | `esp32-s3-devkit` | Headless, no display |
| ESP32 Generic | `esp32-headless` | Headless, serial output only |

### Where to Buy

- **AliExpress:** Search "ESP32-2432S028" (~$10-15 USD)
- **Amazon:** Search "CYD ESP32 2.8 inch" (~$15-20 USD)

### Hardware Features

- **CPU:** Dual-core Xtensa LX6 @ 240MHz
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
| `backup_pool_url` | No | - | Failover pool hostname |
| `backup_pool_port` | No | - | Failover pool port |
| `backup_wallet` | No | - | Wallet for backup pool |

### 2. WiFi Access Point Portal

If no SD card config is found, SparkMiner creates a WiFi access point:

1. **Connect** to WiFi network: `SparkMiner-XXXX` (password shown on display)
2. **Open browser** to `http://192.168.4.1`
3. **Configure** WiFi credentials and pool settings
4. **Save** and the device will reboot and connect

### 3. NVS (Non-Volatile Storage)

Configuration is automatically saved to flash memory after first successful setup. To reset:
- Hold the BOOT button for 10 seconds during startup, OR
- Reflash the firmware

---

## Pool Configuration

### Recommended Pools

| Pool | URL | Port | Fee | Notes |
|------|-----|------|-----|-------|
| **Public Pool** | `public-pool.io` | `21496` | 0% | Recommended, solo mining |
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

## Display Screens

SparkMiner has 3 display screens. Tap the screen or press BOOT to cycle:

### Screen 1: Mining Status (Default)

```
┌─────────────────────────────────┐
│ SparkMiner V2       45C  [●][●] │
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

---

## Performance

### Expected Hashrates

| Board | Hashrate | Power | Notes |
|-------|----------|-------|-------|
| ESP32-2432S028 (CYD) | ~700 KH/s | ~0.5W | With display |
| ESP32-S3 | ~800 KH/s | ~0.4W | No display overhead |
| ESP32 Headless | ~750 KH/s | ~0.3W | Serial only |

### Architecture

SparkMiner uses both ESP32 cores efficiently:

- **Core 1 (High Priority):** Pipelined hardware SHA-256 mining using direct register access
- **Core 0 (Low Priority):** WiFi, Stratum protocol, display, and software SHA-256 mining

This dual-core approach maximizes hashrate while maintaining responsive UI and network connectivity.

---

## Troubleshooting

### WiFi Issues

| Problem | Solution |
|---------|----------|
| Won't connect to WiFi | Check SSID/password, ensure 2.4GHz network (not 5GHz) |
| Keeps disconnecting | Move closer to router, check for interference |
| AP mode not appearing | Hold BOOT button 10s during startup to reset |

### Display Issues

| Problem | Solution |
|---------|----------|
| White/blank screen | Try `esp32-2432s028-st7789` environment |
| Inverted colors | Display driver mismatch, try alternate environment |
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
- Python 3.7+
- Git

### Build Commands

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
│   │   ├── sha256_pipelined.cpp  # Pipelined assembly
│   │   └── sha256_soft.cpp   # Software SHA (Core 0)
│   ├── stats/                # Live stats & monitoring
│   └── stratum/              # Stratum v1 protocol
├── include/
│   └── board_config.h        # Hardware definitions
├── platformio.ini            # Build configuration
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
