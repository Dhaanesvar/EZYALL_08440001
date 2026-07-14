# RAK3112 LoRaWAN Portal Firmware (PlatformIO)

ESP32-S3 + SX1262 firmware with:

- LoRaWAN OTAA/ABP configuration
- Embedded AP + web portal for live config and logs
- Downlink monitor (history + state)
- YAT/serial `getuid` command for LED board identity
- Editable LED board UID, MAC, and IP from the AP web UI

## Project Structure

- `platformio.ini` - PlatformIO environment config
- `src/main.cpp` - Main firmware (web UI, LoRaWAN, serial, API)
- `scripts/pre_upload_guard.py` - Upload guard helper

## Current Defaults

- AP login URL: `http://192.168.4.1`
- AP SSID default: `CRE8IOT_08440001` (can be changed via UID Name)
- Portal password: `2240624`
- Board default IP info (display-only): `13.22.0.213`
- LED board MAC default: `DE:AD:BE:EF:FE:ED`
- Health ping default interval: `2s`

## Build

```bash
~/.platformio/penv/bin/platformio run
```

## Upload

Use this safe command (kills common serial tools first):

```bash
pkill -f "cutecom|yat|minicom|picocom|putty|screen" >/dev/null 2>&1 || true
~/.platformio/penv/bin/platformio run --target upload
```

## Serial Monitor (YAT / terminal)

Baud rate:

- `115200` (default unless changed in config)

Useful command:

- `getuid`

When `getuid` is sent over serial, firmware prints:

- LED board UID
- LED board MAC
- LED board IP

## Web Portal Features

### Device Info panel

Editable fields:

- UID name (also updates AP SSID)
- Device IP (AP IP on board)
- LED board UID
- LED board MAC
- LED board IP

Shown values:

- Device UID
- Device MAC
- Device IP
- Board Default IP (info only)
- LED UID / LED MAC / LED IP
- AP SSID

### LoRaWAN configuration

- OTAA and ABP
- Region, ADR, confirmed uplink, FPort, TX power
- Custom payload
- Health interval scheduler

### Live monitoring

- Live log
- Clear logs
- Downlink state monitor
- Downlink history

## API Endpoints

- `GET /api/config`
- `POST /api/config`
- `GET /api/deviceinfo`
- `POST /api/deviceinfo`
- `GET /api/logs`
- `POST /api/logs/clear`
- `GET /api/downlink`
- `GET /api/dlstate`
- `POST /api/join`
- `POST /api/leave`
- `POST /api/forceuplink`

## Notes

- AP operational default is `192.168.4.1`.
- `13.22.0.213` is kept as board default info value in Device Info.
- YAT output for `getuid` uses saved LED identity values from NVS.