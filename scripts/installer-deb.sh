#!/bin/sh

yellow=`echo "\033[01;33m"`
green=`echo "\033[01;32m"`
red=`echo "\033[01;31m"`
white=`echo "\033[m"`

KLIPPER_PATH="${HOME}/klipper"
KLIPPY_EXTRA_DIR=$KLIPPER_PATH/klippy/extras
GCODE_SHELL_CMD=$KLIPPY_EXTRA_DIR/gcode_shell_command.py
SHAPER_CONFIG=$KLIPPY_EXTRA_DIR/calibrate_shaper_config.py
CONFIG_FILE="${HOME}/printer_data/config"
PRINTER_DATA_DIR="${HOME}/printer_data"
GUPPY_DIR="${HOME}/guppyscreen"
BACKUP_DIR="${HOME}/guppyscreen-backup-$(date +%Y%m%d-%H%M%S)"
PINNED_RELEASE="v0.2.0-GuppyKE"
WPA_SVC=/lib/systemd/system/wpa_supplicant.service

has_moonraker() {
    echo "Checking for a working Moonraker"
    MRK_KPY_OK=`curl -s localhost:7125/server/info | jq .result.klippy_connected`
    if [ "$MRK_KPY_OK" != "true" ]; then
	printf "${yellow}Moonraker is not properly setup at port 7125. Please fix moonraker ${white}\n"
    fi
}

get_klipper_paths() {
    KLIPPER_PATH=`curl -s localhost:7125/printer/info | jq -r .result.klipper_path`
    if [ "$KLIPPER_PATH" = "null" ]; then
	printf "${green} Falling back to klipper path: $KLIPPER_PATH ${white}\n"
    fi

    printf "${green} Found klipper path: $KLIPPER_PATH ${white}\n"

    KLIPPY_EXTRA_DIR=$KLIPPER_PATH/klippy/extras
    GCODE_SHELL_CMD=$KLIPPY_EXTRA_DIR/gcode_shell_command.py
    SHAPER_CONFIG=$KLIPPY_EXTRA_DIR/calibrate_shaper_config.py


    CONFIG_FILE=`curl -s localhost:7125/printer/info | jq -r .result.config_file`
    if [ "$CONFIG_FILE" = "null" ]; then
	printf "${green} Falling back to config dir: $CONFIG_DIR ${white}\n"
    fi

    CONFIG_DIR=$(dirname "$CONFIG_FILE")
    PRINTER_DATA_DIR=$(dirname "$CONFIG_DIR")
    printf "${green} Found config dir: $CONFIG_DIR ${white}\n"
    printf "${green} Found printer_data dir: $PRINTER_DATA_DIR ${white}\n"

}

backup_existing() {
    printf "${green}Creating pre-install backup at $BACKUP_DIR ${white}\n"
    mkdir -p "$BACKUP_DIR"
    [ -f "$CONFIG_DIR/printer.cfg" ] && cp "$CONFIG_DIR/printer.cfg" "$BACKUP_DIR/printer.cfg"
    [ -f "$WPA_SVC" ] && cp "$WPA_SVC" "$BACKUP_DIR/wpa_supplicant.service"
    [ -f /etc/systemd/system/guppyscreen.service ] && cp /etc/systemd/system/guppyscreen.service "$BACKUP_DIR/guppyscreen.service.bak"
    [ -f /etc/systemd/system/disable_blinking_cursor.service ] && cp /etc/systemd/system/disable_blinking_cursor.service "$BACKUP_DIR/disable_blinking_cursor.service.bak"
    [ -d "$CONFIG_DIR/GuppyScreen" ] && cp -r "$CONFIG_DIR/GuppyScreen" "$BACKUP_DIR/GuppyScreen"
    printf "${green}Backup saved to $BACKUP_DIR ${white}\n"
    printf "${yellow}NOTE: To manually restore printer.cfg: cp $BACKUP_DIR/printer.cfg $CONFIG_DIR/printer.cfg ${white}\n"
}

install_services() {
    sed -i "s|<USER>|$USER|g" ${HOME}/guppyscreen/debian/guppyscreen.service

    sudo cp ${HOME}/guppyscreen/debian/disable_blinking_cursor.service /etc/systemd/system
    sudo cp ${HOME}/guppyscreen/debian/guppyscreen.service /etc/systemd/system
    sudo systemctl enable disable_blinking_cursor.service
    sudo systemctl enable guppyscreen.service
    printf "${green}Configuring guppyscreen services ${white}\n"

    sudo systemctl disable KlipperScreen.service

    # allow users in netdev group to modify wifi
    if grep -q "GROUP=netdev" /lib/systemd/system/wpa_supplicant.service ; then
	printf "${green}WPA Supplicant already grants access to GROUP netdev ${white}\n"
    else
	sudo sed -i 's/\/run\/wpa_supplicant/"DIR=\/run\/wpa_supplicant GROUP=netdev"/g' /lib/systemd/system/wpa_supplicant.service
    fi
}

install_guppy_goodies() {
    printf "${green}Setting up Guppy Macros ${white}\n"
    if [ ! -f $GCODE_SHELL_CMD ]; then
	printf "${green}Installing gcode_shell_command.py for klippy ${white}\n"
	cp $GUPPY_DIR/k1_mods/gcode_shell_command.py $GCODE_SHELL_CMD
    fi

    cp $GUPPY_DIR/k1_mods/calibrate_shaper_config.py $SHAPER_CONFIG

    mkdir -p $CONFIG_DIR/GuppyScreen/scripts
    sed -i "s|<CONFIG_DIR>|$CONFIG_DIR|g; s|<KLIPPER_PATH>|$KLIPPER_PATH|g" $GUPPY_DIR/debian/guppy_cmd.cfg
    cp $GUPPY_DIR/debian/*.cfg $CONFIG_DIR/GuppyScreen
    cp $GUPPY_DIR/scripts/*.py $CONFIG_DIR/GuppyScreen/scripts

    if grep -q "include GuppyScreen" $CONFIG_DIR/printer.cfg ; then
	echo "printer.cfg already includes GuppyScreen cfgs"
    else
	printf "${green}Including guppyscreen cfgs in printer.cfg ${white}\n"
	sed -i '1s;^;\[include GuppyScreen/*\.cfg\]\n;' $CONFIG_DIR/printer.cfg
    fi

    sed -i "s|<GUPPY_DIR>|$GUPPY_DIR|g; s|<PRINTER_DATA_DIR>|$PRINTER_DATA_DIR|g" $GUPPY_DIR/debian/guppyconfig.json

    cp $GUPPY_DIR/debian/guppyconfig.json $GUPPY_DIR
    mkdir $GUPPY_DIR/thumbnails
}

restart_services() {
    printf "${green}Restarting Guppy Screen services ${white}\n"
    sudo service disable_blinking_cursor restart
    service guppyscreen restart
}

uninstall_guppy() {
    printf "${green}Uninstalling Guppy Screen ${white}\n"

    # Stop and remove services
    sudo systemctl stop guppyscreen.service 2>/dev/null
    sudo systemctl disable guppyscreen.service 2>/dev/null
    sudo systemctl disable disable_blinking_cursor.service 2>/dev/null
    sudo rm -f /etc/systemd/system/guppyscreen.service
    sudo rm -f /etc/systemd/system/disable_blinking_cursor.service
    sudo systemctl daemon-reload

    # Re-enable KlipperScreen if it exists
    sudo systemctl enable KlipperScreen.service 2>/dev/null && sudo systemctl start KlipperScreen.service 2>/dev/null

    # Restore wpa_supplicant.service from most recent backup if available
    LATEST_BACKUP=$(ls -dt "${HOME}"/guppyscreen-backup-* 2>/dev/null | head -1)
    if [ -n "$LATEST_BACKUP" ] && [ -f "$LATEST_BACKUP/wpa_supplicant.service" ]; then
        sudo cp "$LATEST_BACKUP/wpa_supplicant.service" "$WPA_SVC"
        printf "${green}Restored wpa_supplicant.service from backup ${white}\n"
    fi

    # Remove GuppyScreen include from printer.cfg
    K1_CONFIG_FILE=$(curl -s localhost:7125/printer/info 2>/dev/null | jq -r .result.config_file 2>/dev/null)
    if [ -n "$K1_CONFIG_FILE" ] && [ -f "$K1_CONFIG_FILE" ]; then
        sed -i '/\[include GuppyScreen/d' "$K1_CONFIG_FILE"
        printf "${green}Removed GuppyScreen include from printer.cfg ${white}\n"
    else
        printf "${yellow}Could not auto-remove GuppyScreen from printer.cfg. Remove manually: [include GuppyScreen/*.cfg] ${white}\n"
    fi

    # Remove GuppyScreen config directory
    if [ -n "$K1_CONFIG_FILE" ]; then
        CONFIG_DIR=$(dirname "$K1_CONFIG_FILE")
        if [ -d "$CONFIG_DIR/GuppyScreen" ]; then
            rm -rf "$CONFIG_DIR/GuppyScreen"
            printf "${green}Removed $CONFIG_DIR/GuppyScreen ${white}\n"
        fi
    fi

    printf "${yellow}NOTE: Klipper extras (gcode_shell_command.py, calibrate_shaper_config.py) were NOT removed\n(they may be used by other tools). Remove manually if needed. ${white}\n"

    printf "${yellow}Do you want to remove ~/guppyscreen? (y/n): ${white}"
    read confirm
    if [ "$confirm" = "y" -o "$confirm" = "Y" ]; then
        rm -rf "${HOME}/guppyscreen"
        printf "${green}Removed ~/guppyscreen ${white}\n"
    fi
    printf "${green}GuppyScreen uninstalled. Restart Klipper to apply printer.cfg changes. ${white}\n"
}


ARCH=`uname -m`
echo "Found arch $ARCH"

if [ "$ARCH" = "aarch64" ]; then
    if [ "$1" = "uninstall" ]; then
        uninstall_guppy
        exit 0
    fi

    printf "${green}Installing Guppy Screen (coreflake1 fork - ke-advanced-3d-bedmesh) ${white}\n"

    ASSET_URL="https://github.com/coreflake1/guppyscreen/releases/download/${PINNED_RELEASE}/guppyscreen-arm.tar.gz"
    if [ "$1" = "nightly" ]; then
        printf "${yellow}Installing nightly build ${white}\n"
        ASSET_URL="https://github.com/coreflake1/guppyscreen/releases/download/nightly/guppyscreen-arm.tar.gz"
    fi

    curl -s -L $ASSET_URL -o /tmp/guppyscreen.tar.gz
    tar xf /tmp/guppyscreen.tar.gz -C ${HOME}

    has_moonraker
    get_klipper_paths
    backup_existing
    install_services
    install_guppy_goodies
    restart_services

    printf "${green}Successfully installed Guppy Screen ${white}\n"
else
    printf "${red}Terminating... Your OS Platform has not been tested with Guppy Screen ${white}\n"
    exit 1
fi
