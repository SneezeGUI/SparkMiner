## [Unreleased]

### Added
- **Heltec WiFi LoRa 32 V3 support** - ESP32-S3 with 128x64 OLED, LoRa SX1262

# Changelog

All notable changes to SparkMiner will be documented in this file.

## [v2.9.6-pre] - 2026-09-19

### Fixed
- **ESP32-S3 zero-shares bug** (#36, issues #28/#10/#5): core 1 now mines the first SHA in software (same proven path as core 0) instead of the broken hardware midstate restore. Root cause: the S3 SHA_H digest-state registers expect big-endian state words (espressif/esp-idf#12440) and the old code seeded little-endian — reported hashrate was real hash speed but no share ever validated. S3 now shows an honest ~50-55 KH/s with shares landing at the pool; a faster correct HW path is tracked in #28.
- **WiFi reconnect after AP reboot / AUTH_EXPIRE** (#37, issues #31/#29): the watchdog re-issues `WiFi.begin()` with the NVS-stored credentials instead of `WiFi.reconnect()`. README note for the macOS captive-portal probe.
- **Config portal: SSIDs with spaces** (#45): scan-list autofill read the HTML-escaped visible text (spaces became U+00A0); now reads `data-ssid`.
- **Block-height and fee stats silently dead** (#44): mempool.space 301-redirects its HTTP endpoints to HTTPS and HTTPClient does not follow redirects; endpoints now use HTTPS via the existing TLS paths.

### Added
- **ESP32-C3/S2 hardware SHA mining path** (#39, issue #34): legitimate START→CONTINUE full double-hash with no midstate injection, software-verify gate on every submission, and a one-shot boot self-test that falls back to the software miner if the register sequence disagrees with the chip.
- **`esp32-devkit-oled` board target** (#38, issue #26): generic ESP32 DevKit + SSD1306 128x64 OLED on SDA=21/SCL=22. Both OLED envs added to the CI matrix.
- **CI artifact upload** — every PR now publishes test-build factory binaries; tags build all targets and publish a GitHub Release automatically.

### Changed
- README hashrate tables now report honest, pool-verified numbers.

## [v2.9.3] - 2026-01-22

### Added
- **Heltec Wireless Paper v1.1 support** - ESP32-S3 with 2.13" E-Ink display (~278 KH/s)
- New build environments: `wireless-paper` and `wireless-paper-headless`
- E-Ink display driver with partial refresh for display durability

### Fixed
- Typo in platformio.ini comments ("monocrhome" → "monochrome")

## [v2.9.2] - 2026-01-14

### Added
- **Stratum Proxy Stats Integration**: Pool hashrate, worker hashrate, difficulty adjustment via custom stats API
- `workerHashrate` field in `live_stats_t` and `display_data_t` structs
- Pool total hashrate display in bottom info panel
- Difficulty retarget progress and expected change % display

### Changed
- Stats grid layout: "Pool HR" renamed to "Workers" showing user's combined miner hashrate
- Bottom panel now shows pool total hashrate
- Extended stats API response parsing for pool and difficulty stats

### Fixed
- Worker visibility on pools - `mining.submit` now uses full `wallet.workername` from authorization

## [v2.9.1] - 2026-01-09

### Fixed
- Display settings (brightness, rotation, invert) now apply immediately after WiFi portal save
- WiFi portal dropdown values not syncing to form on selection
- SSID auto-fill when clicking scanned network names
- ArduinoJson memory optimization for network hashrate API

### Added
- Timezone support with configurable UTC offset (-12 to +14)
- Network hashrate and difficulty stats from mempool.space
- HTTPS stats proxy with SSL bumping support

## [v2.9.0] - 2026-01-08

### Added
- U8g2 OLED display support for SSD1306 displays (9cbec10)
- Display abstraction layer and ESP32-C3 single-core support (c24634e)
- LED status driver and LILYGO T-Display support (50513d9)

### Changed
- Updated README with ESP32-C3 and OLED board support (4081c9b)

## [v2.8.0] - 2025-01-07

### Added
- Persistent mining statistics (NVS storage)
- SD card stats backup (/stats.json)
- Color-coded status indicators (temp, WiFi, ping)
- Unified devtool.py for build/flash/monitor
- Friendly firmware naming (cyd-1usb, cyd-2usb, etc.)
- Cloudflare Worker for HTTPS stats API (optional)

### Changed
- Firmware filenames now use friendly board names
- Display shows lifetime totals instead of session-only stats
- devtool.py replaces flash.py and build_release.py

### Fixed
- Struct alignment padding causing stats checksum failures
- WiFi portal not opening when SD has stats but no config
- COM port handling after device reset

### Removed
- flash.py, flash.bat (use devtool.py instead)

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
