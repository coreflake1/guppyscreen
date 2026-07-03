# Creality / extra macros — vendored into OpenKE

**Upstream:** [Guilouz/Creality-Helper-Script](https://github.com/Guilouz/Creality-Helper-Script)
(`files/macros/` and `files/moonraker/`)
**License:** GPL-3.0 (see [`LICENSE`](LICENSE)) — compatible with OpenKE.

These are the everyday macros KE users used to install from the Creality Helper Script. Vendored here so
OpenKE can be a one-shot install. Full credit to Guilouz and contributors.

## Files & KE-specific changes

| File | Upstream | Change |
|---|---|---|
| `useful-macros.cfg` | `useful-macros-3v3.cfg` (the KE/3V3 variant) | backup/restore shell commands repointed from `/usr/data/helper-script/files/scripts/useful_macros.sh` (which most OpenKE-only installs don't have — those macros existed on-screen but silently failed) to our own `scripts/klipper_backup_restore.py`; `RELOAD_CAMERA` removed (called into Helper Script's own `auto_uvc.sh`, out of scope for this fix — see project notes) |
| `M600-support.cfg` | `M600-support-ke.cfg` (the KE variant) | bare `[respond]` removed (OpenKE's `guppy_cmd.cfg` already provides it) |
| `save-zoffset.cfg` | `save-zoffset.cfg` | `[respond]` removed; `variables.cfg` repointed from the Helper-Script dir to `printer_data/config/variables.cfg` |
| `exclude_object.cfg` | (Klipper native) | bare `[exclude_object]`; pairs with `enable_object_processing` in moonraker.conf |

## How the installer applies them (safety)

`M600-support.cfg` and `save-zoffset.cfg` **redefine sections that a stock or Helper-Script config may
already own** (`[idle_timeout]`, `[filament_switch_sensor filament_sensor]`, `SET_GCODE_OFFSET`,
`[save_variables]`, `RESUME`). Two such definitions = a duplicate-section error and Klipper won't start.

So the installer copies each file **only if none of the sections it defines already exist** elsewhere in
the config (it greps first and skips the file otherwise, with a message). This means:
- On a clean stock config → they install and work.
- On a config that already has them (e.g. an existing Helper-Script setup) → they're skipped, leaving
  your working setup intact. Use the Helper Script there.

`exclude_object` is added only if absent; `enable_object_processing` is added to moonraker.conf only if
neither it nor a conflicting `[file_manager]` is already present.

To refresh from upstream, see [`docs/VENDORING.md`](../../../../docs/VENDORING.md).
