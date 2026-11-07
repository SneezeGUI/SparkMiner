# Local Stats API and Settings Page

SparkMiner can serve a small HTTP API with AxeOS / ESP-Miner compatible endpoints
(issue #12). HashWatcher, Home Assistant and other BitAxe tools can poll the miner
over the LAN. It is **opt-in**: while disabled no task is created and no socket is
opened, so there is no hashrate cost.

Uses the synchronous `WebServer` and `ESPmDNS` that ship with the Arduino core
(already pulled in by WiFiManager). No new library dependencies.

## Enable it

- **Config portal:** set "Local Stats API" to *Enabled* and save, or
- **Build default:** add `-DSPARKMINER_API_DEFAULT=1` to the build flags (only used while
  the setting has never been written to NVS).

The miner announces itself via mDNS as `http://sparkminer-xxxx.local/` (`xxxx` = last two MAC bytes).

## Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | Minimal status page |
| GET | `/api/system/info` | AxeOS-compatible system info (JSON, `hashRate` in GH/s) |
| GET | `/api/system/statistics` | AxeOS-compatible history, 120 samples x 30 s (`?columns=a,b,c`) |
| GET | `/api/sparkminer` | Compact native stats (`hashrate` in H/s), handy for Home Assistant |
| GET | `/api/log` | Recent pool/stratum events as plain text (no wallet, no passwords) |
| GET | `/settings` | Settings page (HTTP Basic auth) |
| POST | `/settings` | Save settings and restart |
| POST | `/settings/reset` | Factory reset |
| POST | `/api/system/restart` | Restart the miner |

The wallet address is masked in API responses unless "Wallet in API" is set to *Full address*.
Fields that a single ESP32 board cannot provide (power, voltage, fan, ...) are reported as `0`;
`temp` is the internal chip temperature.

## Settings page

`http://<ip>/settings`, user `admin`, default password = the setup hotspot password.
The password can be changed on the page (minimum 8 characters).

It edits WiFi, wallet, primary/backup pool, display options, live stats and API access.
Saving restarts the miner. If a new WiFi network cannot be joined, the miner returns to the
previous credentials on the next boot. The pool field accepts `host`, `host:port` and
`stratum+tcp://host:port`. POST requests with a foreign `Origin` header are rejected.

## Home Assistant

The **Bitaxe** custom integration reads `/api/system/info` and works with SparkMiner, but it
truncates `hashRate` (GH/s) to an integer, so a few hundred KH/s show up as `0`.
Use `/api/sparkminer` for a correct value, for example:

```yaml
rest:
  - resource: http://<miner-ip>/api/sparkminer
    scan_interval: 30
    sensor:
      - name: "SparkMiner Hashrate"
        unique_id: sparkminer_hashrate_khs
        unit_of_measurement: "kH/s"
        state_class: measurement
        value_template: "{{ (value_json.hashrate / 1000) | round(2) }}"
        json_attributes:
          - hashrate_1h
          - shares_accepted
          - shares_rejected
          - pool_connected
```

## Event log

`GET /api/log` returns the last 64 pool/stratum events (connect, handshake, difficulty,
share submits and the pool's answers). It is kept in RAM only and helps to diagnose a pool
that rejects the miner when no serial console is attached.
