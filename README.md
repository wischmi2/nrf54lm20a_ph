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

## Stream paths

Once per gateway sync the node writes two LightDB Stream JSON objects.
Pouch path `.s/` is Golioth stream.

Between syncs the radio is **off** for `SAMPLE_INTERVAL_S` (default 300 s /
5 min). It then advertises with `sync_req` set for
`CONFIG_EXAMPLE_ADV_WINDOW_S` (default 45 s). If no gateway connects, it
sleeps again. A connect that never reaches a Pouch session (bond mismatch)
retries advertise up to `CONFIG_EXAMPLE_ADV_RETRIES` times (default 2)
before sleeping. Atlas stays in hibernate (`0x06` = 0), the XIAO RGB is off,
and TWIM22 is suspended. The hardware floor is Atlas hibernate plus the
always-on ADuM1250 — there is no load switch on this PCB.

Measure wait current on **battery only** with USB unplugged. USB VBUS and
the SAMD11 CDC bridge keep the board awake for COM11.

### `.s/ph`

Atlas Scientific pH OEM on isolated I2C (D4/D5). One conversion per sync.

```json
{"ph":7.123,"mv":12.40,"temp_c":22.50,"cal":7}
```

| Field | Type | Meaning |
|-------|------|---------|
| `ph` | float | Compensated pH (3 decimals) |
| `mv` | float | Probe millivolts |
| `temp_c` | float | Compensation temperature used for that sample (°C). DS18B20 on J2 if present, else `TEMP_OVERRIDE_C`, else 25.00 |
| `cal` | int | OEM confirm bitmask (`0x0D`): bit0 low (pH 4), bit1 mid (pH 7), bit2 high (pH 10). `0` none, `2` mid only, `3` two-point, `7` three-point |

If the OEM is missing or the conversion fails:

```json
{"error":"oem_missing","temp_c":25.00}
{"error":"read","err":-5,"temp_c":25.00}
```

### `.s/battery`

nPM1300 on the XIAO (pack on J3). Same sample instant as `.s/ph`.

```json
{"v":3.912,"ma":12.3,"temp_c":24.1,"die_c":32.4,"vbus":true,"chg":"idle","chg_stat":2,"err":0,"on_bat_s":0}
```

| Field | Type | Meaning |
|-------|------|---------|
| `v` | float | Pack voltage (V) |
| `ma` | float | Pack current (mA). Sign follows the Zephyr gauge channel |
| `temp_c` | float | Pack NTC temperature (°C) |
| `die_c` | float | nPM1300 die temperature (°C) |
| `vbus` | bool | USB / VBUS present |
| `on_bat_s` | int | Seconds since USB was unplugged (`0` if VBUS is present). Written to `/lfs1/bat_run.txt` so a dead pack still reports the last run on the next boot |
| `chg` | string | Charge stage: `idle`, `trickle`, `cc`, `cv`, `complete`, `paused` (die too hot), `recharge`, `supplement` |
| `chg_stat` | int | Raw `BCHGCHARGESTATUS` byte. Bits: 1 complete, 2 trickle, 3 CC, 4 CV, 5 recharge, 6 die-temp pause, 7 supplement |
| `err` | int | Latched charger error. `0` is healthy |

If the nPM1300 cannot be read:

```json
{"error":"npm1300"}
```

## Serial console (COM11, 115200)

USB CDC on the XIAO. Prompt is `uart:~$`. Tab completion works.

### pH OEM (`ph`)

These talk to the Atlas chip at **0x65** on **i2c22** (isolated D4/D5).

| Command | What it does |
|---------|----------------|
| `ph info` | Device type, firmware, cal confirm bits |
| `ph read` | Wake, one conversion, print pH / mV / temp / cal, hibernate |
| `ph temp <c>` | Write temperature compensation (`0x0E`–`0x11`), °C × 100 |
| `ph led on` / `ph led off` | LED control (`0x05`) |
| `ph cal clear` | Delete all cal (`0x0C` = 1) |
| `ph cal mid` | Mid point, pH 7.00 (`0x0C` = 3). **Do this first.** |
| `ph cal low` | Low point, pH 4.00 (`0x0C` = 2) |
| `ph cal high` | High point, pH 10.00 (`0x0C` = 4) |
| `ph log on` / `ph log off` | Local 5 s printk of pH + battery (not cloud) |

`ph cal` immediately reprints confirm bits; the OEM often needs longer than
50 ms to update `0x0D`. Trust the next `ph read` / pouch `cal` value.

Always start 2- or 3-point cal at mid. Sending mid again clears low and high.

### Raw I2C (any Atlas register)

Atlas needs a **STOP** after the register-pointer write, then a separate read.
Zephyr `i2c read` / `i2c read_byte` use a repeated START and usually return
`0xFF` on this chip. Use a pointer write + `direct_read`:

```
i2c scan i2c22
i2c write i2c22 0x65 0x00
i2c direct_read i2c22 0x65 2
```

That example sets the pointer to `0x00` and reads type + firmware.

```
i2c write_byte i2c22 0x65 0x05 0x00
```

Writes LED off (`0x05` = 0). Multi-byte values are MSB first.

```
i2c recover i2c22
```

If the bus is stuck after a bad transfer.

### Other hardware on this board

| Command | Hardware |
|---------|----------|
| `bat` | nPM1300: V, I, pack NTC, die °C, VBUS, charge status/error, `on_bat_s` |
| `w1 search w1` | DS18B20 on D0 (J2), if fitted |
| `sensor get ds18b20` | One-shot DS18B20 temperature |
| `fs ls /lfs1` | LittleFS (device cert lives in `/lfs1/credentials`) |

## Atlas pH OEM registers

Default address **0x65**. All bytes. Multi-byte fields are **MSB first**.
From the [pH OEM datasheet](https://files.atlas-scientific.com/pH_OEM_datasheet.pdf)
(v4.3). This firmware also reads probe mV at `0x1A`–`0x1D` (not in that TOC).

Kept across power loss: I2C address, calibration.
**Not** kept: LED, active/hibernate, interrupt, new-reading flag, temperature
compensation.

| Addr | Name | R/W | Encoding / values |
|------|------|-----|-------------------|
| `0x00` | Device type | R | `1` = pH |
| `0x01` | Firmware version | R | e.g. `2` |
| `0x02` | I2C address lock | R/W | Unlock: write `0x55` then `0xAA` back-to-back. Any other write locks. `0` = unlocked, `1` = locked |
| `0x03` | I2C address | R/W | Default `0x65`. Range `0x01`–`0x7F`. Takes effect after unlock + write |
| `0x04` | Interrupt control | R/W | Pin 7 (INT). **Not wired on this PCB.** `0` disabled, `2` high on new reading (manual reset), `4` low on new reading (manual reset), `8` invert on new reading (auto reset) |
| `0x05` | LED | R/W | `1` blink each reading (default), `0` off |
| `0x06` | Active / hibernate | R/W | `1` wake (reading every ~420 ms), `0` hibernate. Must wake to convert |
| `0x07` | New reading available | R/W | `1` = new sample. Master must write `0` to clear. Driver polls this |
| `0x08`–`0x0B` | Calibration value | R/W | Signed long, **pH × 1000**. Load this, then `0x0C` |
| `0x0C` | Calibration request | R/W | `0` idle (returns here after STOP). `1` clear all, `2` low (4.00), `3` mid (7.00), `4` high (10.00). Cal runs on the I2C STOP |
| `0x0D` | Calibration confirm | R | Bit0 low, bit1 mid, bit2 high. `0` none, `2` mid only, `3` two-point, `7` three-point |
| `0x0E`–`0x11` | Temperature compensation | R/W | Unsigned long, **°C × 100**. Default 25.00 °C |
| `0x12`–`0x15` | Temperature confirm | R | °C × 100 actually used for the last pH sample |
| `0x16`–`0x19` | pH reading | R | Signed long, **pH × 1000** |
| `0x1A`–`0x1D` | Probe mV | R | Signed long, **mV × 1000**. Used by `ph read` / `.s/ph` |

Wake (`0x06` = 1) → wait for `0x07` = 1 → read `0x16`… → write `0x07` = 0 →
hibernate. That is what `ph read` does.

## Golioth settings

| Key | Type | Meaning |
|-----|------|---------|
| `SAMPLE_INTERVAL_S` | int | Radio-off seconds between advertise windows (5–3600). Firmware default is 300 (5 min) |
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
must be running during the advertise window or the node goes back to sleep.

COM11 after a successful sync should show `pouch: sleeping 300 s (radio off)`,
then ~5 minutes later `pouch: waking` and `sync_req=1`. There are no
`pouch: waiting` lines during the radio-off window.
