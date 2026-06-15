#!/bin/sh

yellow=`echo "\033[01;33m"`
green=`echo "\033[01;32m"`
red=`echo "\033[01;31m"`
white=`echo "\033[m"`

BACKUP_DIR=/usr/data/guppyify-backup
K1_GUPPY_DIR=/usr/data/guppyscreen
FT2FONT_PATH=/usr/lib/python3.8/site-packages/matplotlib/ft2font.cpython-38-mipsel-linux-gnu.so
ASSET_NAME="guppyscreen"

uninstall_guppy() {
    printf "${green}=== Uninstalling GuppyScreen ===${white}\n"

    # Get klipper/config paths (same fallback logic as installer)
    KLIPPER_PATH_U=$(curl localhost:7125/printer/info 2>/dev/null | jq -r .result.klipper_path 2>/dev/null)
    if [ -z "$KLIPPER_PATH_U" ] || [ "$KLIPPER_PATH_U" = "null" ]; then
        KLIPPER_PATH_U=/usr/share/klipper
    fi
    KLIPPY_EXTRA_DIR_U=$KLIPPER_PATH_U/klippy/extras

    K1_CONFIG_FILE_U=$(curl localhost:7125/printer/info 2>/dev/null | jq -r .result.config_file 2>/dev/null)
    if [ -z "$K1_CONFIG_FILE_U" ] || [ "$K1_CONFIG_FILE_U" = "null" ]; then
        K1_CONFIG_DIR_U=/usr/data/printer_data/config
    else
        K1_CONFIG_DIR_U=$(dirname "$K1_CONFIG_FILE_U")
    fi

    # Stop and remove GuppyScreen
    printf "${green}Stopping GuppyScreen...${white}\n"
    [ -f /etc/init.d/S99guppyscreen ] && /etc/init.d/S99guppyscreen stop 2>/dev/null
    killall -q guppyscreen 2>/dev/null
    rm -f /etc/init.d/S99guppyscreen

    # Restore S12boot_display from backup
    if [ -f "$BACKUP_DIR/S12boot_display" ]; then
        cp "$BACKUP_DIR/S12boot_display" /etc/init.d/S12boot_display
        printf "${green}Restored S12boot_display${white}\n"
    else
        printf "${yellow}No S12boot_display backup found. Restore manually if needed.${white}\n"
    fi

    # Restore S99start_app from backup
    if [ -f "$BACKUP_DIR/S99start_app" ] && [ ! -f /etc/init.d/S99start_app ]; then
        cp "$BACKUP_DIR/S99start_app" /etc/init.d/S99start_app
        printf "${green}Restored S99start_app${white}\n"
    fi

    # Remove [include GuppyScreen] from printer.cfg
    if [ -f "$K1_CONFIG_DIR_U/printer.cfg" ]; then
        sed -i '/\[include GuppyScreen/d' "$K1_CONFIG_DIR_U/printer.cfg"
        printf "${green}Removed GuppyScreen include from printer.cfg${white}\n"
    else
        printf "${yellow}printer.cfg not found at $K1_CONFIG_DIR_U. Remove [include GuppyScreen/*.cfg] manually.${white}\n"
    fi

    # Remove GuppyScreen config directory
    if [ -d "$K1_CONFIG_DIR_U/GuppyScreen" ]; then
        rm -rf "$K1_CONFIG_DIR_U/GuppyScreen"
        printf "${green}Removed GuppyScreen config directory${white}\n"
    fi

    # Remove Klipper symlinks
    rm -f "$KLIPPY_EXTRA_DIR_U/guppy_module_loader.py"
    rm -f "$KLIPPY_EXTRA_DIR_U/guppy_config_helper.py"
    rm -f "$KLIPPY_EXTRA_DIR_U/tmcstatus.py"
    printf "${green}Removed Klipper symlinks${white}\n"

    # Print-quality mods: KAMP/Skew/ATC cfgs lived in GuppyScreen/ (removed above).
    # Leave the Klipper modules in place — removing them would break a printer.cfg
    # that still has saved [autotune_tmc ...] / [axis_twist_compensation] sections.
    if [ -f "$BACKUP_DIR/probe.py.bak" ]; then
        printf "${yellow}NOTE: Axis Twist edited probe.py. To fully revert:${white}\n"
        printf "${yellow}      cp $BACKUP_DIR/probe.py.bak $KLIPPY_EXTRA_DIR_U/probe.py${white}\n"
        printf "${yellow}      and remove autotune_tmc.py / axis_twist_compensation.py from $KLIPPY_EXTRA_DIR_U${white}\n"
        printf "${yellow}      plus any [autotune_tmc]/[axis_twist_compensation] sections from printer.cfg.${white}\n"
    fi

    # Optionally remove /usr/data/guppyscreen
    printf "${yellow}Remove $K1_GUPPY_DIR (binary + themes + configs)? (y/n): ${white}"
    read confirm_rm
    echo
    if [ "$confirm_rm" = "y" ] || [ "$confirm_rm" = "Y" ]; then
        rm -rf "$K1_GUPPY_DIR"
        printf "${green}Removed $K1_GUPPY_DIR${white}\n"
    fi

    printf "${yellow}NOTE: calibrate_shaper_config.py and gcode_shell_command.py NOT removed (may be shared with Klipper).${white}\n"
    printf "${yellow}NOTE: If Monitor/display-server were renamed to .disable, restore manually:${white}\n"
    printf "${yellow}      mv /usr/bin/Monitor.disable /usr/bin/Monitor${white}\n"
    printf "${yellow}      mv /usr/bin/display-server.disable /usr/bin/display-server${white}\n"
    printf "${yellow}NOTE: Backup files remain at $BACKUP_DIR (ft2font.so, printer.cfg.bak, etc.)${white}\n"
    printf "${green}GuppyScreen uninstalled. Reboot printer to restore display services.${white}\n"
}

if [ "$1" = "uninstall" ]; then
    uninstall_guppy
    exit 0
fi

if [ "$1" = "zbolt" ]; then
    ASSET_NAME="$ASSET_NAME-zbolt"
fi

ARCH=`uname -m`
if [ "$ARCH" = "mips" ] && [ -f /sys/class/graphics/fb0/virtual_size ]; then
    res=`cat /sys/class/graphics/fb0/virtual_size`
    x=${res%,*}
    y=${res#*,}

    printf "${green}Found screen virtual size $x, $y ${white}\n"
    if [ $y -lt 800 ] && [ $x -lt 800 ]; then
	printf "${green}Using smallscreen version ${white}\n"
	ASSET_NAME="guppyscreen-smallscreen"
    fi

else
    printf "${red}Unable to find compatible platform/screen size (found platform: $ARCH) ${white}\n"
    exit 1
fi

printf "${green}=== Installing Guppy Screen === ${white}\n"

# check ld.so version
if [ ! -f /lib/ld-2.29.so ]; then
    printf "${red}ld.so is not the expected version. Make sure you're running 1.3.x.y firmware versions ${white}\n"
    exit 1
fi

echo "Checking for a working Moonraker"
MRK_KPY_OK=`curl localhost:7125/server/info 2> /dev/null | jq .result.klippy_connected`
if [ "$MRK_KPY_OK" != "true" ]; then
    printf "${yellow}Moonraker is not properly setup at port 7125. Continue anyways? (y/n) ${white}\n"
    read confirm
    echo

    if [ "$confirm" = "y" -o "$confirm" = "Y" ]; then
	    echo "Continuing to install GuppyScreen"
    else
        echo "Please fix Moonraker and restart this script."
        exit 1
    fi
fi

KLIPPER_PATH=`curl localhost:7125/printer/info 2> /dev/null | jq -r .result.klipper_path`
if [ -z "$KLIPPER_PATH" -o x"$KLIPPER_PATH" == x"null" ]; then
    KLIPPER_PATH=/usr/share/klipper
    printf "${green} Falling back to klipper path: $KLIPPER_PATH ${white}\n"
fi

printf "${green} Found klipper path: $KLIPPER_PATH ${white}\n"

KLIPPY_EXTRA_DIR=$KLIPPER_PATH/klippy/extras
GCODE_SHELL_CMD=$KLIPPY_EXTRA_DIR/gcode_shell_command.py
SHAPER_CONFIG=$KLIPPY_EXTRA_DIR/calibrate_shaper_config.py

K1_CONFIG_FILE=`curl localhost:7125/printer/info 2> /dev/null | jq -r .result.config_file`
if [ -z "$K1_CONFIG_FILE" -o x"$K1_CONFIG_FILE" == x"null" ]; then
    K1_CONFIG_DIR=/usr/data/printer_data/config
    printf "${green} Falling back to config dir: $K1_CONFIG_DIR ${white}\n"
else
    K1_CONFIG_DIR=$(dirname "$K1_CONFIG_FILE")
    printf "${green} Found config dir: $K1_CONFIG_DIR ${white}\n"
fi

# kill pip cache to free up overlayfs
rm -rf /root/.cache

## bootstrap for ssl support
wget -q --no-check-certificate https://raw.githubusercontent.com/ballaswag/k1-discovery/main/bin/curl -O /tmp/curl
chmod +x /tmp/curl

PINNED_RELEASE="v0.5.5-GuppyKE"
ASSET_URL="https://github.com/coreflake1/guppyscreen/releases/download/${PINNED_RELEASE}/$ASSET_NAME.tar.gz"

printf "${green} Downloading asset: $ASSET_NAME.tar.gz ${white}\n"

# download/extract latest guppyscreen
/tmp/curl -s -L $ASSET_URL -o /tmp/guppyscreen.tar.gz
tar xf /tmp/guppyscreen.tar.gz -C /usr/data/

# substitute paths in guppyconfig.json
if [ -f "$K1_GUPPY_DIR/debian/guppyconfig.json" ]; then
    PRINTER_DATA_DIR=$(dirname "$K1_CONFIG_DIR")
    sed -i "s|<GUPPY_DIR>|$K1_GUPPY_DIR|g; s|<PRINTER_DATA_DIR>|$PRINTER_DATA_DIR|g" "$K1_GUPPY_DIR/debian/guppyconfig.json"
    cp "$K1_GUPPY_DIR/debian/guppyconfig.json" "$K1_GUPPY_DIR/guppyconfig.json"
fi
mkdir -p "$K1_GUPPY_DIR/thumbnails"

if [ ! -f "$K1_GUPPY_DIR/guppyscreen" ]; then
    printf "${red}Did not find guppyscreen in $K1_GUPPY_DIR. GuppyScreen must be extracted in $K1_GUPPY_DIR ${white}\n"
    exit 1
fi

#### let's see if guppyscreen starts before doing anything more
printf "${green} Test starting Guppy Screen ${white}\n"
[ -f /etc/init.d/S99guppyscreen ] && /etc/init.d/S99guppyscreen stop &> /dev/null
killall -q guppyscreen
$K1_GUPPY_DIR/guppyscreen &> /dev/null &

## allow guppy to live a little
sleep 1

ps auxw | grep guppyscreen | grep -v sh | grep -v grep

if [ $? -eq 0 ]; then
    printf "${green} Guppy Screen started sucessfully, continuing with installation ${white}\n"
    killall -q guppyscreen
else
    printf "${red} Guppy Screen FAILED to start, aborting ${white}\n"
    exit 1
fi

printf "${green}Setting up Guppy Macros ${white}\n"
if [ ! -f $GCODE_SHELL_CMD ]; then
    printf "${green}Installing gcode_shell_command.py for klippy ${white}\n"
    cp $K1_GUPPY_DIR/k1_mods/gcode_shell_command.py $GCODE_SHELL_CMD
fi

mkdir -p $K1_CONFIG_DIR/GuppyScreen/scripts
cp $K1_GUPPY_DIR/scripts/*.cfg $K1_CONFIG_DIR/GuppyScreen
cp $K1_GUPPY_DIR/scripts/*.py $K1_CONFIG_DIR/GuppyScreen/scripts

## backup printer.cfg before any modification
mkdir -p $BACKUP_DIR
if [ ! -f "$BACKUP_DIR/printer.cfg.bak" ]; then
    cp "$K1_CONFIG_DIR/printer.cfg" "$BACKUP_DIR/printer.cfg.bak" 2>/dev/null || true
    printf "${green}Backed up printer.cfg to $BACKUP_DIR/printer.cfg.bak ${white}\n"
fi

## include guppyscreen *.cfg in printer.cfg
if grep -q "include GuppyScreen" $K1_CONFIG_DIR/printer.cfg ; then
    echo "printer.cfg already includes GuppyScreen cfgs"
elif grep -q "include gcode_macro" $K1_CONFIG_DIR/printer.cfg ; then
    printf "${green}Including guppyscreen cfgs in printer.cfg (after gcode_macro include) ${white}\n"
    sed -i '/\[include gcode_macro\.cfg\]/a \[include GuppyScreen/*\.cfg\]' $K1_CONFIG_DIR/printer.cfg
else
    printf "${green}Including guppyscreen cfgs in printer.cfg (prepended, no gcode_macro include found) ${white}\n"
    sed -i '1s;^;\[include GuppyScreen/*\.cfg\]\n;' $K1_CONFIG_DIR/printer.cfg
fi

## symlink usb
K1_GCODE_DIR=$(dirname "$K1_CONFIG_DIR")/gcodes
ln -sf /tmp/udisk $K1_GCODE_DIR/usb

if [ ! -f "$BACKUP_DIR/S50dropbear" ]; then
    printf "${green} Backing up original K1 files ${white}\n"
    mkdir -p $BACKUP_DIR

    [ -f /etc/init.d/S12boot_display ] && mv /etc/init.d/S12boot_display $BACKUP_DIR
    cp /etc/init.d/S50dropbear $BACKUP_DIR
    [ -f /etc/init.d/S99start_app ] && cp /etc/init.d/S99start_app $BACKUP_DIR
fi

if [ ! -f $BACKUP_DIR/ft2font.cpython-38-mipsel-linux-gnu.so ]; then
    # backup ft2font
    mv /usr/lib/python3.8/site-packages/matplotlib/ft2font.cpython-38-mipsel-linux-gnu.so $BACKUP_DIR
fi

## dropbear early to ensure ssh is started with display-server
cp $K1_GUPPY_DIR/k1_mods/S50dropbear /etc/init.d/S50dropbear

printf "${white}=== Do you want to disable all Creality services (revertible) with GuppyScreen installation? ===\n"
printf "${green}  Pros: Frees up system resources on your K1 for critical services such as Klipper (Recommended)\n"
printf "${white}  Cons: Disabling all Creality services breaks Creality Cloud/Creality Slicer.\n\n"
printf "Disable all Creality Services? (y/n): "

read confirm_decreality
echo

if [ "$confirm_decreality" = "y" -o "$confirm_decreality" = "Y" ]; then
    printf "${green}Disabling Creality services ${white}\n"
    [ -f /etc/init.d/S99start_app ] && rm /etc/init.d/S99start_app
else
    # disables only display-server and Monitor (guard: may not exist on all firmwares)
    [ -f /usr/bin/Monitor ] && mv /usr/bin/Monitor /usr/bin/Monitor.disable
    [ -f /usr/bin/display-server ] && mv /usr/bin/display-server /usr/bin/display-server.disable
fi

printf "${green}Setting up GuppyScreen ${white}\n"
cp $K1_GUPPY_DIR/k1_mods/S99guppyscreen /etc/init.d/S99guppyscreen

# backup existing calibrate_shaper_config.py before unconditional overwrite
[ -f "$SHAPER_CONFIG" ] && [ ! -f "$BACKUP_DIR/calibrate_shaper_config.py.bak" ] && cp "$SHAPER_CONFIG" "$BACKUP_DIR/calibrate_shaper_config.py.bak"
cp $K1_GUPPY_DIR/k1_mods/calibrate_shaper_config.py $SHAPER_CONFIG

ln -sf $K1_GUPPY_DIR/k1_mods/guppy_module_loader.py $KLIPPY_EXTRA_DIR/guppy_module_loader.py
ln -sf $K1_GUPPY_DIR/k1_mods/guppy_config_helper.py $KLIPPY_EXTRA_DIR/guppy_config_helper.py
ln -sf $K1_GUPPY_DIR/k1_mods/tmcstatus.py $KLIPPY_EXTRA_DIR/tmcstatus.py


if [ ! -d "/usr/lib/python3.8/site-packages/matplotlib-2.2.3-py3.8.egg-info" ]; then
    echo "Not replacing mathplotlib ft2font module. PSD graphs might not work"
else
    printf "${green}Replacing mathplotlib ft2font module for plotting PSD graphs ${white}\n"
    cp $K1_GUPPY_DIR/k1_mods/ft2font.cpython-38-mipsel-linux-gnu.so $FT2FONT_PATH
fi

ln -sf $K1_GUPPY_DIR/k1_mods/respawn/libeinfo.so.1 /lib/libeinfo.so.1
ln -sf $K1_GUPPY_DIR/k1_mods/respawn/librc.so.1 /lib/librc.so.1


## ============================================================================
## OpenKE optional features — install all / skip all / choose each:
##   print-quality mods (KAMP, Axis Twist Compensation, TMC Autotune, Skew),
##   Creality Nebula camera (persistent image tuning + H.264 stream),
##   Pause/Resume layer-shift fix (PAUSE y_park 222 -> 220).
## KAMP/Skew/ATC/camera config install via the existing [include GuppyScreen/*.cfg]
## glob (no printer.cfg section edits); only Axis Twist touches Klipper core
## (probe.py), via an idempotent, backed-up patch. The layer-shift fix sed-edits
## gcode_macro.cfg (backed up first, guarded on the 222 line existing).
## ============================================================================
MODS_DIR=$K1_GUPPY_DIR/k1_mods/klipper_mods
GUPPY_CFG_DIR=$K1_CONFIG_DIR/GuppyScreen

# Safety net: a fresh timestamped printer.cfg backup on EVERY run. Your calibration
# values (z-offset, mesh, input shaper, skew, axis-twist, TMC autotune) live in
# printer.cfg's SAVE_CONFIG block / variables.cfg, which this installer never rewrites —
# so a reinstall does not un-calibrate you. This backup is just belt-and-suspenders.
cp "$K1_CONFIG_DIR/printer.cfg" "$BACKUP_DIR/printer.cfg.$(date +%Y%m%d-%H%M%S).bak" 2>/dev/null || true

# Echo the cfg files Klipper actually loads: printer.cfg + its [include] globs,
# two levels deep (nested includes resolve relative to the including file's dir).
# This deliberately ignores *.bak / printer-*.cfg backups sitting in the config dir.
active_cfgs() {
    _cfg="$K1_CONFIG_DIR/printer.cfg"
    [ -f "$_cfg" ] || return 0
    echo "$_cfg"
    _inc1=$(grep -E "^[[:space:]]*\[include " "$_cfg" 2>/dev/null \
            | sed -e 's/^[[:space:]]*\[include[[:space:]][[:space:]]*//' -e 's/[[:space:]]*\][[:space:]]*$//')
    for _p1 in $_inc1; do
        for _f1 in "$K1_CONFIG_DIR"/$_p1; do
            [ -f "$_f1" ] || continue
            echo "$_f1"
            _d1=$(dirname "$_f1")
            _inc2=$(grep -E "^[[:space:]]*\[include " "$_f1" 2>/dev/null \
                    | sed -e 's/^[[:space:]]*\[include[[:space:]][[:space:]]*//' -e 's/[[:space:]]*\][[:space:]]*$//')
            for _p2 in $_inc2; do
                for _f2 in "$_d1"/$_p2; do [ -f "$_f2" ] && echo "$_f2"; done
            done
        done
    done
}

# True if a config section is ACTIVELY defined in a loaded file OUTSIDE our managed
# GuppyScreen/ dir. Anchored match so commented-out headers (e.g. the KE's stock
# "#[filament_switch_sensor ...]") do NOT count, and only loaded files are checked
# (not the *.bak / printer-*.cfg backups). Prevents adding a duplicate section.
section_defined_elsewhere() {   # $1 = literal header, e.g. "[axis_twist_compensation]"
    _pat=$(printf '%s' "$1" | sed 's/[][]/\\&/g')   # escape [ and ] for ERE
    _lst="/tmp/.ke_active.$$"
    active_cfgs | sort -u | grep -v "/GuppyScreen/" > "$_lst"
    _ret=1
    while IFS= read -r _f; do
        [ -f "$_f" ] || continue
        if grep -qE "^[[:space:]]*$_pat" "$_f" 2>/dev/null; then _ret=0; break; fi
    done < "$_lst"
    rm -f "$_lst"
    return $_ret
}

# Back up an existing klippy/extras file once before we overwrite it — e.g. a TMC
# Autotune or axis_twist module a user already installed via the Creality Helper
# Script or by hand — so their original is recoverable from the backup dir.
backup_extra_once() {   # $1 = filename in KLIPPY_EXTRA_DIR
    if [ -f "$KLIPPY_EXTRA_DIR/$1" ] && [ ! -f "$BACKUP_DIR/$1.bak" ]; then
        cp "$KLIPPY_EXTRA_DIR/$1" "$BACKUP_DIR/$1.bak"
        printf "${yellow}  Backed up existing $1 -> $BACKUP_DIR/$1.bak${white}\n"
    fi
    return 0
}

# Copy a vendored .cfg into GuppyScreen/ ONLY if none of the [sections] it defines
# already exist elsewhere in the config. Prevents a duplicate-section crash when a
# stock or Helper-Script config already owns one of those sections.
install_cfg_guarded() {   # $1 = src path, $2 = dest filename, $3 = label
    _src="$1"; _dest="$2"; _label="$3"
    [ -f "$_src" ] || return 0
    _secfile="/tmp/.ke_secs.$$"
    grep -oE '^\[[^]]+\]' "$_src" 2>/dev/null | sort -u > "$_secfile"
    _conflict=""
    while IFS= read -r _sec; do
        [ -z "$_sec" ] && continue
        if section_defined_elsewhere "$_sec"; then _conflict="$_sec"; break; fi
    done < "$_secfile"
    rm -f "$_secfile"
    if [ -n "$_conflict" ]; then
        printf "${yellow}  Skipping %s — your config already defines %s.${white}\n" "$_label" "$_conflict"
    else
        cp "$_src" "$GUPPY_CFG_DIR/$_dest"
        printf "${green}  Installed %s${white}\n" "$_label"
    fi
}

printf "${white}=== OpenKE optional features ===\n"
printf "${green}  Print-quality mods (KAMP, Axis Twist Compensation, TMC Autotune, Skew Correction),\n"
printf "${green}  the Creality Nebula camera (persistent image tuning + H.264 stream), Creality macros\n"
printf "${green}  (M600, Save Z-Offset, useful macros, Exclude Object), and the layer-shift fix.${white}\n\n"
printf "${white}  [Y] install all     [n] skip all     [o] choose each one${white}\n"
printf "Choice (Y/n/o): "
read feat_mode
echo
case "$feat_mode" in
    n|N)  FEAT=none ;;
    o|O)  FEAT=choose ;;
    *)    FEAT=all ;;
esac

# want <label> -> 0 (install) / 1 (skip), per the chosen mode.
want() {
    case "$FEAT" in
        all)    return 0 ;;
        none)   return 1 ;;
        choose) printf "  Install %s? (y/N): " "$1"; read _a; { [ "$_a" = y ] || [ "$_a" = Y ]; } ;;
    esac
}

if [ "$FEAT" = "none" ]; then
    printf "${yellow}Skipping all optional features.${white}\n"
elif [ ! -d "$MODS_DIR" ]; then
    printf "${yellow}Mods dir $MODS_DIR not in this release; skipping optional features.${white}\n"
else
    # --- KAMP ---
    if [ -d "$MODS_DIR/kamp" ] && want "KAMP (adaptive mesh + purge)"; then
        if section_defined_elsewhere "[gcode_macro _KAMP_Settings]"; then
            printf "${yellow}  KAMP already set up elsewhere — leaving it untouched.${white}\n"
        else
            printf "${green}  Installing KAMP${white}\n"
            cp -r "$MODS_DIR/kamp/KAMP" "$GUPPY_CFG_DIR/"
            cp "$MODS_DIR/kamp/KAMP_Settings.cfg" "$GUPPY_CFG_DIR/KAMP_Settings.cfg"
        fi
    fi

    # --- Axis Twist Compensation: module + cfg + idempotent probe.py patch ---
    if [ -d "$MODS_DIR/axis_twist_compensation" ] && want "Axis Twist Compensation (left/right first-layer fix)"; then
        printf "${green}  Installing Axis Twist Compensation${white}\n"
        backup_extra_once axis_twist_compensation.py
        cp "$MODS_DIR/axis_twist_compensation/axis_twist_compensation.py" "$KLIPPY_EXTRA_DIR/"
        if section_defined_elsewhere "[axis_twist_compensation]"; then
            printf "${yellow}  [axis_twist_compensation] already in your config — keeping yours.${white}\n"
        else
            cp "$MODS_DIR/axis_twist_compensation/axis_twist_compensation.cfg" "$GUPPY_CFG_DIR/axis_twist_compensation.cfg"
        fi
        [ -f "$KLIPPY_EXTRA_DIR/probe.py" ] && [ ! -f "$BACKUP_DIR/probe.py.bak" ] && cp "$KLIPPY_EXTRA_DIR/probe.py" "$BACKUP_DIR/probe.py.bak"
        python3 "$MODS_DIR/axis_twist_compensation/patch_probe.py" "$KLIPPY_EXTRA_DIR/probe.py" || \
            printf "${yellow}  Axis Twist: probe.py not auto-patched (see above); patch by hand if your firmware differs (wiki).${white}\n"
    fi

    # --- TMC Autotune: modules into klippy/extras (sections written on-screen) ---
    if [ -d "$MODS_DIR/tmc_autotune" ] && want "TMC Autotune (quieter, cooler steppers)"; then
        printf "${green}  Installing TMC Autotune modules${white}\n"
        backup_extra_once autotune_tmc.py
        backup_extra_once motor_constants.py
        backup_extra_once motor_database.cfg
        cp "$MODS_DIR/tmc_autotune/autotune_tmc.py"    "$KLIPPY_EXTRA_DIR/"
        cp "$MODS_DIR/tmc_autotune/motor_constants.py" "$KLIPPY_EXTRA_DIR/"
        cp "$MODS_DIR/tmc_autotune/motor_database.cfg" "$KLIPPY_EXTRA_DIR/"
    fi

    # --- Skew Correction: bare [skew_correction] via the include dir ---
    if want "Skew Correction (square up parts)"; then
        if section_defined_elsewhere "[skew_correction]"; then
            printf "${yellow}  [skew_correction] already in your config — not adding a duplicate.${white}\n"
        else
            printf "${green}  Enabling Skew Correction${white}\n"
            printf '[skew_correction]\n' > "$GUPPY_CFG_DIR/skew_correction.cfg"
        fi
    fi

    # --- Creality Nebula camera: persistent image tuning + H.264 stream ---
    if want "Creality Nebula camera (persistent image tuning + H.264 stream)"; then
        if [ -f "$MODS_DIR/nebula_camera/nebula_camera.cfg" ]; then
            if section_defined_elsewhere "[gcode_macro APPLY_CAM_SETTINGS]"; then
                printf "${yellow}  Camera persist macros already present — leaving yours.${white}\n"
            else
                printf "${green}  Installing Nebula camera persist macros${white}\n"
                cp "$MODS_DIR/nebula_camera/nebula_camera.cfg" "$GUPPY_CFG_DIR/nebula_camera.cfg"
            fi
        fi
        if [ -f "$K1_GUPPY_DIR/k1_mods/h264cam/install.sh" ]; then
            printf "${green}  Installing H.264 camera stream (go2rtc)${white}\n"
            sh "$K1_GUPPY_DIR/k1_mods/h264cam/install.sh"
        fi
    fi

    # --- Pause/Resume layer-shift fix (PAUSE y_park 222 -> 220) ---
    if want "Pause/Resume layer-shift fix (y_park 222->220)"; then
        GM="$K1_CONFIG_DIR/gcode_macro.cfg"
        if [ -f "$GM" ] && grep -q "{% set y_park = 222 %}" "$GM"; then
            [ ! -f "$BACKUP_DIR/gcode_macro.cfg.bak-ypark" ] && cp "$GM" "$BACKUP_DIR/gcode_macro.cfg.bak-ypark"
            sed -i 's/{% set y_park = 222 %}/{% set y_park = 220 %}/' "$GM"
            printf "${green}  Applied layer-shift fix (PAUSE y_park 222 -> 220)${white}\n"
        else
            printf "${yellow}  Layer-shift fix: y_park=222 not found in gcode_macro.cfg (already fixed or different config); skipped.${white}\n"
        fi
    fi

    # --- Creality / extra macros: M600, Save Z-Offset, useful macros, Exclude Object ---
    if [ -d "$MODS_DIR/creality_macros" ] && want "Creality macros (M600, Save Z-Offset, useful macros, Exclude Object)"; then
        CM="$MODS_DIR/creality_macros"
        install_cfg_guarded "$CM/useful-macros.cfg"  "useful-macros.cfg"  "useful macros (backup/restore, PID, bed-level, warmup)"
        install_cfg_guarded "$CM/save-zoffset.cfg"   "save-zoffset.cfg"   "Save Z-Offset (persists z-offset across reboots)"
        install_cfg_guarded "$CM/M600-support.cfg"   "M600-support.cfg"   "M600 filament-change support"
        install_cfg_guarded "$CM/exclude_object.cfg" "exclude_object.cfg" "Exclude Object"
        # moonraker: object processing (needed by Exclude Object + KAMP). Add only if safe.
        MK_CONF="$K1_CONFIG_DIR/moonraker.conf"
        if [ -f "$MK_CONF" ]; then
            if grep -q "enable_object_processing" "$MK_CONF"; then
                : # already set
            elif grep -q "^\[file_manager\]" "$MK_CONF"; then
                printf "${yellow}  moonraker.conf has [file_manager] but no enable_object_processing — add 'enable_object_processing: True' under it for Exclude Object/KAMP.${white}\n"
            else
                cp "$MK_CONF" "$BACKUP_DIR/moonraker.conf.bak-$(date +%Y%m%d-%H%M%S)" 2>/dev/null || true
                printf '\n[file_manager]\nenable_object_processing: True\n' >> "$MK_CONF"
                printf "${green}  Enabled object processing in moonraker.conf${white}\n"
            fi
        fi
    fi

    printf "${green}Optional features done.${white}\n"
    printf "${yellow}  One-time setup still needed where applicable (see the OpenKE wiki):\n"
    printf "${yellow}    Axis Twist: BED_MESH_CLEAR; AXIS_TWIST_COMPENSATION_CALIBRATE SAMPLE_COUNT=5; SAVE_CONFIG\n"
    printf "${yellow}    KAMP:       add ADAPTIVE_BED_MESH_CALIBRATE / SMART_PARK / LINE_PURGE to slicer start g-code\n"
    printf "${yellow}    TMC Autotune & Skew: configure on-screen (Tune tab).${white}\n"
fi

sync

if [ ! -f $K1_GUPPY_DIR/guppyscreen ]; then
    printf "${red}Installation failed, did not find guppyscreen in $K1_GUPPY_DIR. Make sure to extract the guppyscreen directory in /usr/data. ${white}\n"
    exit 1
fi

## double check dropbear is the correct one
if ! diff $K1_GUPPY_DIR/k1_mods/S50dropbear /etc/init.d/S50dropbear > /dev/null ; then
    printf "${red}Dropbear (SSHD) didn't install properly. ${white}\n"
    exit 1
fi

## request to reboot
printf "Restart Klipper now to pick up the new changes (y/n): "
read confirm
echo

if [ "$confirm" = "y" -o "$confirm" = "Y" ]; then
    echo "Restarting Klipper"
    /etc/init.d/S55klipper_service restart
    printf "${yellow}Note: on the KE the first restart after a config change can shut down with a\n"
    printf "${yellow}'serialqueue ... NoneType' error (a host-MCU reconnect race, harmless). If Klipper\n"
    printf "${yellow}shows 'shutdown', just restart it again (Mainsail -> Restart) or cold-boot.${white}\n"
else
    printf "${red}Some GuppyScreen functionality won't work until Klipper is restarted. ${white}\n"
fi

killall -q Monitor
killall -q display-server
if [ "$confirm_decreality" = "y" -o "$confirm_decreality" = "Y" ]; then
    echo "Killing Creality services"
    killall -q master-server
    killall -q audio-server
    killall -q wifi-server
    killall -q app-server
    killall -q upgrade-server
    killall -q web-server
fi

printf "${green}Starting GuppyScreen ${white}\n"
/etc/init.d/S99guppyscreen restart &> /dev/null

sleep 1

ps auxw | grep guppyscreen | grep -v sh | grep -v grep

if [ $? -eq 0 ]; then
    printf "${green} Successfully installed Guppy Screen. Enjoy! ${white}\n"
else
    printf "${red} Guppy Screen FAILED to install. Rolling back... ${white}\n"
    cp $BACKUP_DIR/S99start_app /etc/init.d/S99start_app
    rm /etc/init.d/S99guppyscreen

    /etc/init.d/S99start_app restart &> /dev/null
    exit 1
fi
