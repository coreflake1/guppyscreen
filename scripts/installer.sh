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

PINNED_RELEASE="v0.3.0-GuppyKE"
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
