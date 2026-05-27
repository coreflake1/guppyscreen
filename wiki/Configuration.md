# Configuration

## Runtime configuration: `guppyconfig.json`

Runtime configuration lives in `guppyconfig.json`. The shipped template is `debian/guppyconfig.json`;
on the printer it is installed to `/usr/data/guppyscreen/guppyconfig.json` with the `<GUPPY_DIR>` and
`<PRINTER_DATA_DIR>` placeholders substituted by the installer.

### Top-level fields

| Field | Meaning |
|---|---|
| `default_printer` | Key of the active entry in `printers` |
| `display_rotate` | Framebuffer rotation. **`2` (180°) is the KE default** — the screen is mounted upside-down (see below) |
| `log_path` | Log file path (resolves to `/usr/data/printer_data/logs/guppyscreen.log` on device) |
| `thumbnail_path` | Directory for cached gcode thumbnails |
| `wpa_supplicant` | Path to the wpa_supplicant control socket (used for WiFi management) |
| `guppy_init_script` | Command used to (re)start the service, e.g. `service guppyscreen` |
| `printers` | Map of named printer configurations (see below) |

### Per-printer fields

| Field | Meaning |
|---|---|
| `moonraker_host` / `moonraker_port` | Moonraker address (default `127.0.0.1:7125`) |
| `moonraker_api_key` | API key string, or `false` if not required |
| `display_sleep_sec` | Screen blank timeout in seconds |
| `monitored_sensors` | Sensors shown on the dashboard. Each has `id`, `display_name`, `controllable`, `color` |
| `default_macros` | Macro name → gcode mappings (e.g. `cooldown`, `load_filament`, `unload_filament`) |
| `fans` | Fans to surface in the fan panel |
| `log_level` | spdlog level (e.g. `debug`, `info`) |

### Default monitored sensors

The shipped template monitors Extruder, Bed, and **MCU Temp** (`temperature_sensor mcu_temp`). This
default applies to **fresh installs only** — an existing on-device `guppyconfig.json` must be edited
by hand to change which sensors are shown.

### Display rotation (KE-specific)

The KE screen is physically mounted upside-down. The kernel display driver rotates the framebuffer
180° to compensate, so GuppyScreen must *pre-rotate* 180° for the two rotations to cancel out:

- `display_rotate: 0` → screen appears upside-down
- `display_rotate: 1` → 90° off (wrong axis)
- `display_rotate: 2` → **correct** (KE default)

## Build-time options (environment variables)

These are read by the `Makefile` at build time, not at runtime:

| Variable | Effect |
|---|---|
| `CROSS_COMPILE` | Toolchain prefix (e.g. `mipsel-linux-`). **Unset** = simulator/x86 build |
| `GUPPY_SMALL_SCREEN` | Enables the 480×272 layout and reduces the default font to `montserrat_10` |
| `GUPPY_ROTATE` | Compiles in rotation support |
| `GUPPY_THEME` | `material` (default), `zbolt`, or a colour theme name (e.g. `blue`) |
| `GUPPYSCREEN_VERSION` | Version string shown in the System panel |
| `EVDEV_CALIBRATE` | Enables touch calibration |
| `SIMULATION` / (no `CROSS_COMPILE`) | Builds the SDL simulator with the `SIMULATOR` define and links `-lSDL2` |

> **Font note:** With `GUPPY_SMALL_SCREEN`, `LV_FONT_DEFAULT` is `montserrat_10` (set in `lv_conf.h`).
> Most widgets inherit this; a few panels set explicit fonts. This is the value in the current source —
> some older notes still reference `montserrat_12`.

## Themes

Colour themes are JSON files in `themes/` (`blue`, `green`, `pink`, `purple`, `red`, `yellow`). Icon
sets are compiled in via `GUPPY_THEME` (`material` is the default; `zbolt` selects the Z-Bolt icons).
