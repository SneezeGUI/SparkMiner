# SparkMiner

SparkMiner is a high-performance dual-core Bitcoin mining firmware for the ESP32-2432S028 (Cheap Yellow Display) board. It combines the best features of BitsyMiner and NerdMiner to deliver maximum hashrate using hardware acceleration.

## Features

- **Dual-Core Mining:** Utilizes both ESP32 cores for maximum throughput.
- **Hardware Acceleration:** Uses the ESP32's SHA-256 hardware accelerator via direct register access.
- **Pipelined Assembly:** Core 1 runs a highly optimized pipelined assembly mining loop.
- **Visual Interface:** Beautiful UI on the 2.8" TFT display showing hashrate, difficulty, and network stats.
- **Stratum V1 Support:** Compatible with standard pools (Public-Pool, CKPool, etc.).
- **Live Monitoring:** Real-time tracking of:
  - Hashrate (Instant & Average)
  - Pool Latency (Ping)
  - Device Temperature
  - Network Difficulty & Block Height

## Hardware Requirements

- **Board:** ESP32-2432S028R (CYD - Cheap Yellow Display)
- **USB Cables:** 2x USB cables (if using the 2-USB version for power stability)

## Architecture

- **Core 0:** Runs WiFi, Stratum protocol, Display updates, and a secondary Software SHA-256 miner.
- **Core 1:** Dedicated to the high-performance Pipelined Hardware SHA-256 miner.

## Configuration

Configuration is loaded from `config.json` on the SD card (if present) or via the WiFi Access Point portal on first boot.

## Build Instructions

This project is built using PlatformIO.

1. Install Visual Studio Code and the PlatformIO extension.
2. Clone this repository.
3. Open the project folder in VS Code.
4. Select the environment `esp32-2432s028` (or `2usb` variant).
5. Build and Upload:
   ```bash
   pio run -t upload
   ```

## License

MIT License - see LICENSE file for details.