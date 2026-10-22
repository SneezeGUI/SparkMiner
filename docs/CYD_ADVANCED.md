# CYD ST7789: local advanced statistics and direct SMTP

This opt-in profile extends SparkMiner with two display pages of local mining counters, a browser dashboard, local settings, resistive touch, and SMTP reports sent by the ESP32 itself. No permanently running computer, relay or monitoring service is required.

## Hardware and build

Validated on one ESP32-2432S028 dual-USB (USB-C and Micro-USB) board with ST7789, classic ESP32-D0WD-V3 revision 3.1, 4MB flash and 240MHz CPU. Board labels alone do not identify the display controller. ILI9341, S3 and other pin layouts need their own configuration and are not validated by this contribution.

```
python -m pip install platformio==6.2.0
python -m platformio run --project-conf platformio.mail-check.ini --environment cyd-mail-check
```

The separate configuration pins the platform, framework, compiler and resolved library versions. Default upstream environments remain unchanged. The custom partition table has app0 at 0x10000 and app1 at 0x1f0000, each 0x1e0000 bytes; NVS starts at 0x9000. Back up each device independently before changing its firmware or partition table. The tested app image is 1,415,008 bytes, SHA256 `6cf3e968e5f54dd607e5921036c4e181b2c23383948be9fa278b4d39a85035c6`. This identifies the tested local build, not a promise of identical timestamps/hashes after rebuilding.

ST7789 uses MOSI13/SCLK14/CS15/DC2/BL21, software reset (RST=-1), SPI mode 3 at 27MHz and native 240x320 dimensions. XPT2046 touch uses isolated software SPI on CS33/IRQ36/MOSI32/MISO39/SCLK25. First touch wakes a sleeping display; subsequent taps change the statistics page.

## Statistics and settings

Session and lifetime counters include hashes, submitted/accepted/rejected shares, uptime and accepted/(accepted+rejected) success percentage. The two displays and `/api/advanced-stats` use coherent snapshots. The browser dashboard is served at `/`; settings are at `/settings`. External fee, market and network statistics are omitted from this local display profile.

Settings include network/pools/worker, display and touch, difficulty suggestions, timezone and SMTP. Password visibility applies only to newly entered values. Stored passwords are never returned to the browser or settings export. Empty password fields preserve stored values; explicit clear controls remove them. Export/import excludes secrets. POST controls check token, Host and Origin. This remains a local trusted-LAN interface, not an authenticated Internet service. The HTTP settings page and stored NVS credentials must not be described as encrypted credential storage.

## Direct SMTP reports

Gmail is preset to smtp.gmail.com:465. Configure username, app password, sender and a single recipient locally, save, then click Testmail. Gmail requires an eligible account with two-step verification and an app password for this AUTH LOGIN implementation. Automatic sending starts disabled. A separate connection probe checks TLS and SMTP greeting without authentication or a message.

The daily report includes session/lifetime counters and, after a confirmed prior report, their delta. Scheduling uses the existing fixed timezone offset, with no automatic daylight-saving conversion. Enabling after the scheduled hour can trigger today's catch-up report. Warning conditions are pool/WiFi outage for five minutes, low hashrate for five minutes, and rejection percentage over a ten-minute window with at least 20 replies. An offline or powered-off miner cannot immediately email; WiFi recovery is required.

Failures use bounded retries, persisted attempt state and cooldowns. An uncertain final SMTP acknowledgment suppresses automatic duplication of that message. Server acceptance does not guarantee inbox delivery. A narrow power-loss window can still prevent exactly-once delivery.

TLS verifies certificate chain, hostname and validity. The checked-in Mozilla/Certifi trust material and licenses are in src/mail/. Gmail uses standard mbedTLS CA path building with current GTS roots, because the old compact certificate-bundle callback rejected its cross-signed server chain. Other hosts use the compact bundle. No insecure verification fallback is provided. Supported: implicit TLS/465 and AUTH LOGIN. OAuth, STARTTLS/587, multiple recipients and attachments are not implemented. Trust anchors require maintenance.

## SHA sharing and mining

The MIT-licensed classic ESP32 assembly kernel is attributed in src/mining/LICENSE.nerdminer-modded. Core 0 is reserved for network/UI in this opt-in fast profile; core 1 owns the nonce space. Startup known-answer tests run twice, and every hardware candidate is checked by the production software SHA implementation.

The mail profile selects the conservative timing variant. All SDK SHA algorithms share TEXT memory, so locking only the SHA256 engine is insufficient: the guard locks shared SHA memory around at most 1024 hashes and waits for idle before use. Locks are released before yielding or networking. SMTP briefly pauses mining to release SHA resources, closes TLS before resuming, and preserves the current nonce search unless the pool replaced the job. Failed self-tests or candidate verification use software SHA fallback.

## Validation and limits

- Pinned ESP32 cross-build, app size/header/checksum and app readback verification passed.
- Offline SMTP parsing, rejection/authentication errors, lost final acknowledgment, injection checks, retry/reboot policy, warnings and exclusive mining gate passed.
- Shared SHA bounded-lock tests and independent software SHA vectors passed.
- Production settings handlers and browser password/export/import/test-action tests passed; statistics accounting and HTTP cache stress tests passed.
- On one physical board, a session ran for roughly 7h13m, averaging 873kH/s, with 2758 accepted and 24 rejected shares (99.14% success). No reboot occurred within that observed session. Continuous network/serial history for the whole night was not recorded.
- After the TLS correction, the device passed certificate/greeting checks and resumed around 870kH/s; no panic, brownout or watchdog fault appeared in the three-minute boot check. The owner subsequently confirmed receipt of a real Gmail test message. No credentials or personal raw logs are included.
- A separate unresolved persistence issue was observed: after a controlled restart, lifetime totals were below previously observed live values by about 379 seconds and 46 accepted shares. The exact cause is not established; this contribution does not claim that restart persistence issue is fixed. Treat this as an experimental profile pending review.
- New-device factory installation, other boards, Windows compilation, multi-day automatic SMTP delivery and DST handling have not been validated.

Offline tests (Python with ziglang==0.13.0):

```
python tests/advanced/run_native_tests.py
python tests/mail/run_tests.py
```

Browser tests require Node.js, Playwright and an installed browser. Set CHROME_EXECUTABLE to its path, or install Playwright Chromium for the mail browser test. Run the C++ tests first to generate the settings fixture:

```
node tests/mail/test_mail_browser.cjs
node tests/advanced/dashboard_test.cjs
```

No physical flashing or real SMTP message is performed by these offline tests. See tests/mail/README.md for details. Preserve upstream GPLv3 and the bundled MIT/MPL/Apache notices.
