# xiao_ph

nRF Connect SDK firmware for the **Seeed XIAO nRF54LM20A** on the custom
`ph_sensor` PCB. It reads the Atlas Scientific pH OEM over isolated I2C and
relays samples to Golioth with **Pouch over BLE GATT**.

## Hardware (from the KiCad PCB)

| Role | Net / connector | XIAO pin |
|------|-----------------|----------|
| Atlas pH OEM SDA (via ADuM1250) | `SDA_H` | **D4 / P1.3** (TWIM22) |
| Atlas pH OEM SCL | `SCL_H` | **D5 / P1.7** (TWIM22) |
| DS18B20 1-Wire (optional, J2) | `TEMP_DQ` | **D0 / P1.0** |
| pH probe | J1 SMA | — |
| Battery | J3 | XIAO VBAT |
| Host I2C header | J4 | same SDA/SCL |

Atlas **INT is not wired** to the nRF (`INT_ISO` on J5 only). The driver polls
register `0x07`. I2C is forced to **100 kHz** (Atlas maximum).

## What it sends

Once per gateway sync (default 60 s, setting `SAMPLE_INTERVAL_S`):

Stream path **`.s/ph`**

```json
{"ph":7.123,"mv":12.40,"temp_c":22.50,"cal":2}
```

Stream path **`.s/battery`**

```json
{"v":3.912,"ma":12.3,"temp_c":24.1,"vbus":true,"chg":"idle"}
```

`cal` is the OEM confirmation bitmask (bit0 low, bit1 mid, bit2 high).
`chg` is `idle`, `trickle`, `cc`, `cv`, or `complete`.

## Golioth settings

| Key | Type | Meaning |
|-----|------|---------|
| `SAMPLE_INTERVAL_S` | int | Seconds between syncs (5–3600) |
| `TEMP_OVERRIDE_C` | float | Compensation °C if J2 has no DS18B20 |
| `LED` | bool | Atlas OEM LED |
| `CAL_CMD` | int | `0` idle, `1` clear, `2` low(4.0), `3` mid(7.0), `4` high(10.0) |

Put the probe in the matching buffer, set `CAL_CMD`, wait one sync, then set it
back to `0`.

Import [`golioth-pipeline.yaml`](golioth-pipeline.yaml) so JSON uplinks land in
LightDB Stream.

## Workspace + build

This repo is the west manifest (Pouch **v0.2.0** → NCS v3.4-branch). Do **not**
run `west init` in `C:\Users\Brian` if that directory already has a `.west`.
Initialize **inside this folder**:

```cmd
cd C:\Users\Brian\xiao_ph
west init -l .
west update
west zephyr-export
pip install -r modules\lib\pouch\requirements.txt
```

If `west boards` does not list `xiao_nrf54lm20a`, add Seeed's board root:

```cmd
west build -b xiao_nrf54lm20a/nrf54lm20a/cpuapp --sysbuild . -- -DBOARD_ROOT=C:/Users/Brian/platform-seeedboards/zephyr
```

From an nRF Connect toolchain shell:

```cmd
build.bat
flash.bat
```

Serial console (UART20 / USB CDC): `ph info`, `ph read`, `ph cal mid`, `i2c scan i2c22`.

## Credentials

Pouch uses a per-device **X.509 cert + key** (DER), not a PSK. Flash the
app first so LittleFS is mounted at `/lfs1`, close any serial terminal on
the CDC port, then upload:

```cmd
set SERIAL_PORT=COM11
set CERT_FILE=path\to\crt.der
set KEY_FILE=path\to\key.der
provision.bat
```

After a successful upload, reset the board. Confirm with `fs ls /lfs1/credentials`.

Reboot. A nearby [Pouch gateway](https://github.com/golioth/pouch-gateway)
must be running or the node only advertises.
