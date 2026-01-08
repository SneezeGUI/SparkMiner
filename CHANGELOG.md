# Changelog

All notable changes to SparkMiner will be documented in this file.

## [v2.7.0] - 2025-01-06

### Fixed
- **Critical memory leak** in live_stats.cpp - changed `DynamicJsonDocument` to `StaticJsonDocument<2048>` to prevent heap fragmentation
- **WiFi client memory leak** - replaced `new/delete WiFiClientSecure` with stack-allocated clients
- **Stratum heap fragmentation** - converted `String` members in `stratum_job_t` to fixed char arrays
- **Headless build failure** - added conditional SD card support for boards without SD slots

### Changed
- Stratum job parsing now uses `strncpy` to fixed-size buffers instead of Arduino `String`
- Enhanced heap monitoring with Min/MaxAlloc tracking
- Updated documentation with accurate hashrate measurements (~715-725 KH/s for CYD)

### Added
- Comprehensive board compatibility documentation (`docs/Compatible-Boards-Research.md`)
- Support for 7 ESP32/ESP32-S3 board variants

### Improved
- Memory stability - heap now stays stable at ~168KB during operation
- Share acceptance rate (tested at 70/70 during development)

### Supported Boards
| Board | Environment | Status |
|-------|-------------|--------|
| ESP32-2432S028R (CYD 1-USB) | `esp32-2432s028` | Tested |
| ESP32-2432S028R (CYD 2-USB) | `esp32-2432s028-2usb` | Tested |
| ESP32-2432S028R ST7789 | `esp32-2432s028-st7789` | Supported |
| Freenove FNK0104 ESP32-S3 | `esp32-s3-2432s028` | Supported |
| ESP32-S3 DevKitC-1 | `esp32-s3-devkit` | Supported |
| ESP32 Headless | `esp32-headless` | Supported |
| Lolin S3 Mini | `esp32-s3-mini` | Supported |

---

## [v2.6.0] - Previous Release

- Initial pipelined assembly SHA-256 implementation
- Dual-core mining architecture
- WiFi portal configuration
- SD card config file support
