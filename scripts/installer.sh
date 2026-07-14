#!/bin/sh

yellow=`echo "\033[01;33m"`
green=`echo "\033[01;32m"`
red=`echo "\033[01;31m"`
white=`echo "\033[m"`

BACKUP_DIR=/usr/data/guppyify-backup
K1_GUPPY_DIR=/usr/data/guppyscreen
FT2FONT_PATH=/usr/lib/python3.8/site-packages/matplotlib/ft2font.cpython-38-mipsel-linux-gnu.so
ASSET_NAME="guppyscreen"

# Static MIPS curl binary, used where the on-device wget can't complete a TLS
# handshake with github.com's release-asset redirects (works fine against
# raw.githubusercontent.com, fails with "TLS error from peer (alert code 80)").
# Vendored + pinned to a commit SHA (not a branch) so this never depends on a
# third-party repo's unpinned main branch, or on this branch's own history.
CURL_BOOTSTRAP_URL="https://raw.githubusercontent.com/coreflake1/guppyscreen/d5a502942bde7ad04f45d8ce64f1259adf49d9bf/scripts/vendor/curl-mipsel"

# Download with retries; only succeeds if the destination ends up non-empty.
# A silent wget failure (bad network, flaky DNS) used to leave a 0-byte file
# that the next step would try to extract, crash on, and then get papered
# over by an unconditional "[OK]" — leaving the installed thing broken while
# the installer claimed success. Defined this early (before any of its call
# sites, including Moonraker's own bootstrap) so every download in the script
# can share the same protection instead of only the ones added later.
#
# Some on-device wget builds can't complete a TLS handshake with github.com's
# release-asset redirects (works fine for raw.githubusercontent.com, fails
# with "TLS error from peer (alert code 80)") — the same issue the
# "bootstrap for ssl support" step below works around for the GuppyScreen
# asset itself. Fall back to that same static curl binary here too.
download_file() {   # $1 = url, $2 = dest path, $3 = retries (default 3)
    _url="$1"; _dest="$2"; _tries="${3:-3}"
    _n=0
    while [ "$_n" -lt "$_tries" ]; do
        # User-Agent is required by the GitHub API (a request without one gets a
        # flat 403) and harmless for the other download targets this helper hits.
        wget -q --no-check-certificate --header="User-Agent: OpenKE-Installer" "$_url" -O "$_dest"
        [ -s "$_dest" ] && return 0
        _n=$((_n + 1))
        printf "${yellow}  Download failed (attempt ${_n}/${_tries}), retrying...${white}\n"
        sleep 2
    done

    printf "${yellow}  wget could not fetch it — trying the SSL-capable curl fallback...${white}\n"
    if [ ! -x /tmp/curl ]; then
        wget -q --no-check-certificate "$CURL_BOOTSTRAP_URL" -O /tmp/curl
        chmod +x /tmp/curl
    fi
    [ -x /tmp/curl ] && /tmp/curl -s -L -H "User-Agent: OpenKE-Installer" "$_url" -o "$_dest"
    [ -s "$_dest" ] && return 0
    return 1
}

# Poll Moonraker's own API until it responds or we give up. Starting a service
# and declaring success without checking it actually came up is exactly the
# failure mode this whole helper section exists to avoid.
wait_for_moonraker() {   # $1 = max tries (default 10), 1s apart
    _mr_tries="${1:-10}"
    _mr_n=0
    while [ "$_mr_n" -lt "$_mr_tries" ]; do
        if curl -s localhost:7125/printer/info 2>/dev/null | grep -q '"result"'; then
            return 0
        fi
        _mr_n=$((_mr_n + 1))
        sleep 1
    done
    return 1
}

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

    # Remove the factory-reset service installed by us
    rm -f /etc/init.d/S58factoryreset
    printf "${green}Removed S58factoryreset${white}\n"

    # Restore S12boot_display from backup
    if [ -f "$BACKUP_DIR/S12boot_display" ]; then
        cp "$BACKUP_DIR/S12boot_display" /etc/init.d/S12boot_display
        printf "${green}Restored S12boot_display${white}\n"
    else
        printf "${yellow}No S12boot_display backup found. Restore manually if needed.${white}\n"
    fi

    # Deliberately NOT restoring stock S50dropbear here: it has a startup race
    # with display-server (which S99start_app, restored below, brings back),
    # so reverting it would reintroduce flaky SSH-at-boot right when someone
    # uninstalling might most want reliable remote access. The fix has no
    # GuppyScreen-specific behavior, so it's safe to leave in place.
    printf "${green}Keeping the SSH init-script fix (S50dropbear) — it's a general boot-reliability fix, not GuppyScreen-specific.${white}\n"

    # Restore S99start_app from backup
    if [ -f "$BACKUP_DIR/S99start_app" ] && [ ! -f /etc/init.d/S99start_app ]; then
        cp "$BACKUP_DIR/S99start_app" /etc/init.d/S99start_app
        printf "${green}Restored S99start_app${white}\n"
    fi

    # Re-enable stock webrtc if install disabled it (renamed, not deleted).
    if [ -f /etc/init.d/DISABLED_S97webrtc ] && [ ! -f /etc/init.d/S97webrtc ]; then
        mv /etc/init.d/DISABLED_S97webrtc /etc/init.d/S97webrtc
        /etc/init.d/S97webrtc start >/dev/null 2>&1 || true
        printf "${green}Restored stock webrtc (S97webrtc)${white}\n"
    fi

    # Remove respawn symlinks added for supervise-daemon
    rm -f /lib/libeinfo.so.1 /lib/librc.so.1
    printf "${green}Removed respawn symlinks${white}\n"

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

    # Remove the buzzer songs file (lives at the config-dir top level, so it
    # isn't covered by the GuppyScreen dir removal above).
    if [ -f "$K1_CONFIG_DIR_U/songs.conf" ]; then
        rm -f "$K1_CONFIG_DIR_U/songs.conf"
        printf "${green}Removed songs.conf${white}\n"
    fi

    # Remove static-IP state (also lives at the config-dir top level, written
    # by static_ip.py rather than shipped as a file - never covered by the
    # GuppyScreen dir removal above).
    if [ -f "$K1_CONFIG_DIR_U/static_ip.json" ] || [ -f "$K1_CONFIG_DIR_U/static_ip_dhcp_snapshot.json" ]; then
        rm -f "$K1_CONFIG_DIR_U/static_ip.json" "$K1_CONFIG_DIR_U/static_ip_dhcp_snapshot.json"
        printf "${green}Removed static IP config${white}\n"
    fi

    # Remove Klipper symlinks
    rm -f "$KLIPPY_EXTRA_DIR_U/guppy_module_loader.py"
    rm -f "$KLIPPY_EXTRA_DIR_U/guppy_config_helper.py"
    rm -f "$KLIPPY_EXTRA_DIR_U/tmcstatus.py"
    printf "${green}Removed Klipper symlinks${white}\n"

    # Print-quality mods: Adaptive Print Setup/Skew/ATC cfgs lived in GuppyScreen/ (removed above).
    # Leave the Klipper modules in place — removing them would break a printer.cfg
    # that still has saved [autotune_tmc ...] / [axis_twist_compensation] sections.
    if [ -f "$BACKUP_DIR/probe.py.bak" ]; then
        printf "${yellow}NOTE: Axis Twist edited probe.py. To fully revert:${white}\n"
        printf "${yellow}      cp $BACKUP_DIR/probe.py.bak $KLIPPY_EXTRA_DIR_U/probe.py${white}\n"
        printf "${yellow}      and remove autotune_tmc.py / axis_twist_compensation.py from $KLIPPY_EXTRA_DIR_U${white}\n"
        printf "${yellow}      plus any [autotune_tmc]/[axis_twist_compensation] sections from printer.cfg.${white}\n"
    fi
    if [ -f "$BACKUP_DIR/bed_mesh.py.bak" ]; then
        printf "${yellow}NOTE: Adaptive Print Setup edited bed_mesh.py (adds BED_MESH_CALIBRATE ADAPTIVE=1). To fully revert:${white}\n"
        printf "${yellow}      cp $BACKUP_DIR/bed_mesh.py.bak $KLIPPY_EXTRA_DIR_U/bed_mesh.py${white}\n"
    fi
    if [ -f "$BACKUP_DIR/KAMP_Settings.cfg.bak" ]; then
        printf "${yellow}NOTE: an old KAMP install was migrated/removed. Your pre-migration settings are at:${white}\n"
        printf "${yellow}      $BACKUP_DIR/KAMP_Settings.cfg.bak (and KAMP.bak/ for the macro files)${white}\n"
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

    # Moonraker / Nginx / Mainsail — intentionally left in place.
    # They are general Klipper infrastructure; removing them would break web access and Mainsail.
    # Print a note so the user knows what is still installed.
    if [ -d "/usr/data/moonraker" ] || [ -d "/usr/data/nginx" ] || [ -d "/usr/data/mainsail" ]; then
        printf "${yellow}NOTE: Moonraker, Nginx and Mainsail were left in place — they are general Klipper${white}\n"
        printf "${yellow}      infrastructure and are not GuppyScreen-specific. To remove them manually:${white}\n"
        [ -f /etc/init.d/S56moonraker_service ] && printf "${yellow}        /etc/init.d/S56moonraker_service stop && rm -rf /usr/data/moonraker${white}\n"
        [ -f /etc/init.d/S50nginx ]             && printf "${yellow}        /etc/init.d/S50nginx stop && rm -rf /usr/data/nginx /usr/data/mainsail${white}\n"
        printf "${yellow}      Compat wrappers (supervisorctl, systemctl, sudo) in /usr/bin/ are also left in${white}\n"
        printf "${yellow}      place — Moonraker requires them.${white}\n"
    fi

    # Re-enable Monitor / display-server if install renamed them to .disable.
    # Guard: only restore when the .disable exists and the original isn't already back.
    for bin in Monitor display-server; do
        if [ -f "/usr/bin/$bin.disable" ] && [ ! -f "/usr/bin/$bin" ]; then
            mv "/usr/bin/$bin.disable" "/usr/bin/$bin"
            printf "${green}Restored /usr/bin/$bin${white}\n"
        fi
    done

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

printf "${green}=== Installing GuppyScreen OpenKE === ${white}\n"
printf "${green}Backups of modified files will be saved to: $BACKUP_DIR ${white}\n"
printf "${yellow}Your calibration values (Z-offset, bed mesh, input shaper, skew) are stored in\n"
printf "${yellow}printer.cfg's SAVE_CONFIG block and will NOT be changed by this installer.${white}\n\n"

# check ld.so version
if [ ! -f /lib/ld-2.29.so ]; then
    printf "${red}ld.so is not the expected version. This installer requires Ender-3 V3 KE firmware 1.3.x.x.\n"
    printf "${red}Check your firmware version in the printer menu before re-running.${white}\n"
    exit 1
fi

## ============================================================================
## PREREQUISITE: Moonraker (required for GuppyScreen)
## ============================================================================
echo "Checking for Moonraker"
MOONRAKER_INSTALL_DIR=/usr/data/moonraker
MOONRAKER_INIT=/etc/init.d/S56moonraker_service

if [ ! -d "$MOONRAKER_INSTALL_DIR" ] || [ ! -f "$MOONRAKER_INIT" ]; then
    printf "${yellow}[MISSING] Moonraker not found${white}\n"
    printf "Moonraker is required for GuppyScreen to connect to Klipper.\n"
    printf "Install Moonraker now? [Y/n]: "
    read confirm_moonraker
    echo
    if [ "$confirm_moonraker" = "n" ] || [ "$confirm_moonraker" = "N" ]; then
        printf "${red}Moonraker is required. Please install it and re-run this script.${white}\n"
        exit 1
    fi

    printf "${green}  Downloading Moonraker...${white}\n"
    if ! download_file "https://raw.githubusercontent.com/coreflake1/guppyscreen/670d99bea9a801bef7eb863e984146ef4c6b942c/scripts/vendor/moonraker.tar.gz" /tmp/moonraker.tar.gz; then
        printf "${red}  [FAIL] Could not download Moonraker — check your network and re-run the installer.${white}\n"
        exit 1
    fi
    printf "${green}  Installing Moonraker...${white}\n"
    if ! tar xf /tmp/moonraker.tar.gz -C /usr/data/; then
        printf "${red}  [FAIL] Moonraker archive was corrupt/incomplete — check your network and re-run the installer.${white}\n"
        rm -f /tmp/moonraker.tar.gz
        exit 1
    fi
    rm -f /tmp/moonraker.tar.gz

    printf "${green}  Installing compat wrappers (sudo/systemctl/supervisorctl)...${white}\n"
    if [ ! -f /usr/bin/supervisorctl ]; then
        cat > /usr/bin/supervisorctl << 'SUPERVISORCTL_EOF'
#!/bin/sh
# supervisorctl shim - by destinal
# provides just enough for Moonraker to list/start/stop services via init scripts
if [ -t 1 ]; then
  GREEN='\033[32m'
  RED='\033[31m'
  ENDCOLOR='\033[0m'
fi
get_services() {
  moonraker_pid="$(cat /var/run/moonraker.pid)"
  if [ -f /var/run/moonraker.pid ] && [ -d /proc/"$moonraker_pid" ] ; then
    services=$(ls -1 /etc/init.d/S*|sed 's/.*\/S..//;s/_service$//')
    echo $services
  else
    echo "Error: Invalid or missing PID file /var/run/moonraker.pid" >&2
    exit 1
  fi
}
get_pid_file() {
  service="$1"
  [ $service = "klipper" ] && service="klippy"
  echo "/var/run/$service.pid"
}
is_running() {
  pid_file="$(get_pid_file "$1")"
  if [ -f "$pid_file" ] && [ -d "/proc/$(cat $pid_file)" ]; then return 0; fi
  if pidof "$1" >/dev/null 2>&1; then return 0; fi
  return 1
}
print_process_status() {
  if is_running "$service"; then
    printf "%-33s${GREEN}RUNNING${ENDCOLOR}\n" "$service"
  else
    printf "%-33s${RED}STOPPED${ENDCOLOR}\n" "$service"
  fi
}
get_script_path() {
  ls -1 /etc/init.d/S[0-9][0-9]${1}_service /etc/init.d/S[0-9][0-9]${1}* 2>/dev/null | head -1
}
stop()    { script="$(get_script_path $1)"; [ -f "$script" ] && "$script" stop; }
start()   { script="$(get_script_path $1)"; [ -f "$script" ] && "$script" start; }
restart() { script="$(get_script_path $1)"; [ -f "$script" ] && "$script" restart; }
main() {
  action="$1"; shift
  case "$action" in
    status)
      if [ "$#" -lt 1 ]; then
        for service in $(get_services); do print_process_status $service; done
      else
        for service in "$@"; do print_process_status $service; done
      fi ;;
    start)   start "$1" ;;
    stop)    stop "$1" ;;
    restart) restart "$1" ;;
    *) echo "Usage: $0 {status|start|stop|restart}"; exit 1 ;;
  esac
}
main "$@"
SUPERVISORCTL_EOF
        chmod +x /usr/bin/supervisorctl
    fi

    if [ ! -f /usr/bin/systemctl ]; then
        cat > /usr/bin/systemctl << 'SYSTEMCTL_EOF'
#!/bin/sh
if [ "$1" = "reboot" ]; then
  /sbin/reboot
elif [ "$1" = "poweroff" ]; then
  /sbin/poweroff
fi
SYSTEMCTL_EOF
        chmod +x /usr/bin/systemctl
    fi

    if [ ! -f /usr/bin/sudo ]; then
        printf '#!/bin/sh\nexec $*\n' > /usr/bin/sudo
        chmod +x /usr/bin/sudo
    fi

    printf "${green}  Installing Moonraker service...${white}\n"
    cat > /etc/init.d/S56moonraker_service << 'MOONRAKER_SVC_EOF'
#!/bin/sh
#
# Moonraker Service
#
USER_DATA=/usr/data
PROG=$USER_DATA/moonraker/moonraker-env/bin/python
PY_SCRIPT=$USER_DATA/moonraker/moonraker/moonraker/moonraker.py
MOONRAKER_TMP_DIR=$USER_DATA/moonraker/tmp
PRINTER_DATA_DIR=$USER_DATA/printer_data
PRINTER_CONFIG_DIR=$PRINTER_DATA_DIR/config
PRINTER_LOGS_DIR=$PRINTER_DATA_DIR/logs
PID_FILE=/var/run/moonraker.pid
start() {
        # This device's init environment inherits an 0077 umask, which silently made
        # every file/dir Moonraker creates owner-only (600/700) - including a live
        # Mainsail self-update's zip extraction, leaving /usr/data/mainsail
        # unreadable to nginx's www-data worker (403 Forbidden) every time a user
        # updates Mainsail through its own UI, not just on first install.
        umask 022
        [ -d $PRINTER_DATA_DIR ] || mkdir -p $PRINTER_DATA_DIR
        [ -d $PRINTER_CONFIG_DIR ] || mkdir -p $PRINTER_CONFIG_DIR
        [ -d $PRINTER_LOGS_DIR ] || mkdir -p $PRINTER_LOGS_DIR
        rm -rf $MOONRAKER_TMP_DIR && mkdir -p $MOONRAKER_TMP_DIR
        export TMPDIR=$MOONRAKER_TMP_DIR
        HOME=/root start-stop-daemon -S -q -b -m -p $PID_FILE \
          --exec $PROG -- $PY_SCRIPT -d $PRINTER_DATA_DIR
}
stop()    { start-stop-daemon -K -q -p $PID_FILE; }
restart() { stop; sleep 1; start; }
case "$1" in
  start)   start ;;
  stop)    stop ;;
  restart|reload) restart ;;
  *) echo "Usage: $0 {start|stop|restart}"; exit 1 ;;
esac
exit $?
MOONRAKER_SVC_EOF
    chmod +x /etc/init.d/S56moonraker_service

    # Create minimal moonraker.conf if not already present
    MK_FRESH_CONF=/usr/data/printer_data/config/moonraker.conf
    mkdir -p /usr/data/printer_data/config
    if [ ! -f "$MK_FRESH_CONF" ]; then
        printf "${green}  Creating moonraker.conf...${white}\n"
        cat > "$MK_FRESH_CONF" << 'MOONRAKER_CONF_EOF'
[server]
host: 0.0.0.0
port: 7125
klippy_uds_address: /tmp/klippy_uds
max_upload_size: 2048

[file_manager]
queue_gcode_uploads: False

[database]

[data_store]
temperature_store_size: 600
gcode_store_size: 1000

[machine]
provider: supervisord_cli
validate_service: False
validate_config: False

[authorization]
force_logins: False
cors_domains:
  *.lan
  *.local
  *://localhost
  *://localhost:*
  *://my.mainsail.xyz

trusted_clients:
  10.0.0.0/8
  127.0.0.0/8
  169.254.0.0/16
  172.16.0.0/12
  192.168.0.0/16
  FE80::/10
  ::1/128

[octoprint_compat]

[history]

[update_manager]
enable_auto_refresh: True
refresh_interval: 24
enable_system_updates: False
MOONRAKER_CONF_EOF
    fi

    printf "${green}  Starting Moonraker...${white}\n"
    /etc/init.d/S56moonraker_service start
    if wait_for_moonraker; then
        printf "${green}  [OK] Moonraker installed and started${white}\n"
    else
        printf "${red}  [FAIL] Moonraker was installed but never responded on port 7125.${white}\n"
        printf "${red}  Check /usr/data/printer_data/logs/moonraker.log and re-run the installer if needed.${white}\n"
        exit 1
    fi
else
    # Detected via the init/binary being present, which says nothing about
    # whether the service is actually up right now (e.g. a crashed service
    # after a bad shutdown, or a leftover Helper-Script config Moonraker
    # refuses to start on) - same check the fresh-install branch above already
    # does before declaring success, applied here too instead of trusting
    # file presence alone.
    if wait_for_moonraker; then
        printf "${green}  [OK] Moonraker detected${white}\n"
    else
        printf "${yellow}  Moonraker is installed but not responding — attempting to restart...${white}\n"
        /etc/init.d/S56moonraker_service restart >/dev/null 2>&1 || true
        if wait_for_moonraker; then
            printf "${green}  [OK] Moonraker restarted and responding${white}\n"
        else
            printf "${red}  [FAIL] Moonraker is installed but not responding on port 7125.${white}\n"
            printf "${red}  Check /usr/data/printer_data/logs/moonraker.log, then re-run the installer.${white}\n"
            exit 1
        fi
    fi
fi

# Patch an already-installed Moonraker service that predates the umask fix above.
# The heredoc only writes S56moonraker_service on a *fresh* install (the branch above) -
# re-running the installer on an existing install never touches this file, so anyone
# who installed before this fix shipped would stay vulnerable to the same silent
# 403-on-self-update bug forever unless we check for it here too.
if [ -f "$MOONRAKER_INIT" ] && ! grep -q "umask 022" "$MOONRAKER_INIT"; then
    printf "${green}  Patching Moonraker service (adds a sane umask, fixes files it${white}\n"
    printf "${green}  creates - e.g. a Mainsail self-update - coming out unreadable)${white}\n"
    sed -i '/^start() {/a\        umask 022' "$MOONRAKER_INIT"
    /etc/init.d/S56moonraker_service restart >/dev/null 2>&1 || true
fi

## ============================================================================
## OPTIONAL: Mainsail + Nginx (browser UI — not required for GuppyScreen)
## ============================================================================

NGINX_INIT=/etc/init.d/S50nginx
MAINSAIL_IDX=/usr/data/mainsail/index.html

_nginx_ok=1; _mainsail_ok=1
[ ! -f "$NGINX_INIT" ] && _nginx_ok=0
[ ! -f "$MAINSAIL_IDX" ] && _mainsail_ok=0

if [ "$_nginx_ok" -eq 0 ] || [ "$_mainsail_ok" -eq 0 ]; then
    _missing_web=""
    [ "$_nginx_ok" -eq 0 ] && _missing_web="Nginx"
    [ "$_mainsail_ok" -eq 0 ] && _missing_web="${_missing_web:+${_missing_web} + }Mainsail"
    printf "${yellow}[MISSING] ${_missing_web} not found${white}\n"
    printf "Mainsail is a browser UI for your printer (not required for GuppyScreen).\n"
    printf "Install Mainsail + Nginx now? [Y/n]: "
    read confirm_mainsail
    echo
    if [ "$confirm_mainsail" != "n" ] && [ "$confirm_mainsail" != "N" ]; then
        if [ "$_nginx_ok" -eq 0 ]; then
            printf "${green}  Downloading Nginx...${white}\n"
            if ! download_file "https://raw.githubusercontent.com/coreflake1/guppyscreen/f9c23043bf19ebb091dd6fcec71d28d3459883ec/scripts/vendor/nginx.tar.gz" /tmp/nginx.tar.gz; then
                printf "${red}  [FAIL] Could not download Nginx — check your network and re-run the installer.${white}\n"
                _nginx_ok=2
            fi
        fi
        if [ "$_nginx_ok" -eq 0 ]; then
            printf "${green}  Installing Nginx...${white}\n"
            tar xf /tmp/nginx.tar.gz -C /usr/data/
            rm -f /tmp/nginx.tar.gz
            # Same idea as Mainsail's index.html check below — verify the
            # extraction actually produced the binary the init script depends
            # on, rather than assuming a `tar xf` on a corrupt/truncated
            # archive would have been caught elsewhere.
            if [ ! -f /usr/data/nginx/sbin/nginx ]; then
                printf "${red}  [FAIL] Nginx extraction failed (no sbin/nginx) — re-run the installer to retry.${white}\n"
                _nginx_ok=2
            fi
        fi
        if [ "$_nginx_ok" -eq 0 ]; then
            cat > /etc/init.d/S50nginx << 'NGINX_INIT_EOF'
#!/bin/sh
#
# Nginx Service
#
NGINX="/usr/data/nginx/sbin/nginx"
PIDFILE="/var/run/nginx.pid"
NGINX_ARGS="-c /usr/data/nginx/nginx/nginx.conf"
case "$1" in
  start)
    echo "Starting nginx..."
    mkdir -p /var/log/nginx /var/tmp/nginx
    start-stop-daemon -S -p "$PIDFILE" --exec "$NGINX" -- $NGINX_ARGS ;;
  stop)
    echo "Stopping nginx..."
    start-stop-daemon -K -x "$NGINX" -p "$PIDFILE" -o ;;
  reload|force-reload)
    echo "Reloading nginx..."
    "$NGINX" -s reload ;;
  restart)
    "$0" stop; sleep 1; "$0" start ;;
  *) echo "Usage: $0 {start|stop|restart|reload|force-reload}"; exit 1 ;;
esac
NGINX_INIT_EOF
            chmod +x /etc/init.d/S50nginx

            cat > /usr/data/nginx/nginx/nginx.conf << 'NGINX_CONF_EOF'
worker_processes  1;
events { worker_connections 1024; }
http {
    include       mime.types;
    default_type  application/octet-stream;
    sendfile        on;
    keepalive_timeout  65;
    proxy_connect_timeout 1600;
    proxy_send_timeout 1600;
    proxy_read_timeout 1600;
    send_timeout 1600;

    map $http_upgrade $connection_upgrade {
        default upgrade;
        '' close;
    }
    upstream apiserver {
        ip_hash;
        server 127.0.0.1:7125;
    }
    upstream mjpgstreamer1 { ip_hash; server 127.0.0.1:8080; keepalive 8; }
    upstream mjpgstreamer2 { ip_hash; server 127.0.0.1:8081; keepalive 8; }
    upstream mjpgstreamer3 { ip_hash; server 127.0.0.1:8082; keepalive 8; }
    upstream mjpgstreamer4 { ip_hash; server 127.0.0.1:8083; keepalive 8; }

    server {
        listen 4409 default_server;
        root /usr/data/mainsail;
        index index.html;
        server_name _;
        client_max_body_size 0;
        proxy_request_buffering off;
        access_log off;
        error_log off;
        gzip on;
        gzip_vary on;
        gzip_proxied any;
        gzip_comp_level 4;
        gzip_types text/plain text/css text/xml text/javascript application/javascript application/json application/xml;

        location / { try_files $uri $uri/ /index.html; }
        location = /index.html { add_header Cache-Control "no-store, no-cache, must-revalidate"; }

        location /websocket {
            proxy_pass http://apiserver/websocket;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Connection $connection_upgrade;
            proxy_set_header Host $http_host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
            proxy_read_timeout 86400;
        }
        location ~ ^/(printer|api|access|machine|server)/ {
            proxy_pass http://apiserver$request_uri;
            proxy_http_version 1.1;
            proxy_set_header Upgrade $http_upgrade;
            proxy_set_header Host $http_host;
            proxy_set_header X-Real-IP $remote_addr;
            proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
            proxy_set_header X-Scheme $scheme;
        }
        location /webcam/ {
            postpone_output 0;
            proxy_buffering off;
            proxy_ignore_headers X-Accel-Buffering;
            access_log off; error_log off;
            proxy_http_version 1.1;
            proxy_set_header Connection "";
            proxy_pass http://mjpgstreamer1/;
        }
        location /webcam2/ {
            postpone_output 0; proxy_buffering off;
            proxy_ignore_headers X-Accel-Buffering;
            access_log off; error_log off;
            proxy_http_version 1.1;
            proxy_set_header Connection "";
            proxy_pass http://mjpgstreamer2/;
        }
        location /webcam3/ {
            postpone_output 0; proxy_buffering off;
            proxy_ignore_headers X-Accel-Buffering;
            access_log off; error_log off;
            proxy_http_version 1.1;
            proxy_set_header Connection "";
            proxy_pass http://mjpgstreamer3/;
        }
        location /webcam4/ {
            postpone_output 0; proxy_buffering off;
            proxy_ignore_headers X-Accel-Buffering;
            access_log off; error_log off;
            proxy_http_version 1.1;
            proxy_set_header Connection "";
            proxy_pass http://mjpgstreamer4/;
        }
    }
}
NGINX_CONF_EOF
        fi

        if [ "$_mainsail_ok" -eq 0 ]; then
            printf "${green}  Downloading Mainsail...${white}\n"
            mkdir -p /usr/data/mainsail
            if ! download_file "https://github.com/mainsail-crew/mainsail/releases/latest/download/mainsail.zip" /tmp/mainsail.zip; then
                printf "${red}  [FAIL] Could not download Mainsail — check your network and re-run the installer.${white}\n"
                _mainsail_ok=2
            else
                printf "${green}  Installing Mainsail...${white}\n"
                python3 -c "import zipfile; zipfile.ZipFile('/tmp/mainsail.zip').extractall('/usr/data/mainsail/')"
                rm -f /tmp/mainsail.zip
                # zipfile.extractall() falls back to the umask-masked default
                # mode when the archive carries no Unix permission bits — on
                # a restrictive-umask root shell that leaves everything
                # owner-only, which nginx's www-data worker can't read
                # (browser sees 403 Forbidden even though extraction "worked").
                chmod -R a+rX /usr/data/mainsail
                if [ ! -f /usr/data/mainsail/index.html ]; then
                    printf "${red}  [FAIL] Mainsail extraction failed (no index.html) — re-run the installer to retry.${white}\n"
                    _mainsail_ok=2
                fi
            fi
        fi

        if [ "$_nginx_ok" -ne 2 ]; then
            printf "${green}  Starting Nginx...${white}\n"
            /etc/init.d/S50nginx start
        fi
        if [ "$_nginx_ok" -ne 2 ] && [ "$_mainsail_ok" -ne 2 ]; then
            _ip=$(ip route get 1 2>/dev/null | awk 'NR==1{for(i=1;i<=NF;i++) if ($i=="src") {print $(i+1); exit}}')
            printf "${green}  [OK] Mainsail at http://${_ip}:4409/${white}\n"
        else
            printf "${yellow}  Mainsail/Nginx setup did not complete — re-run the installer to retry.${white}\n"
        fi
    else
        printf "${green}  Skipping Mainsail + Nginx${white}\n"
    fi
else
    printf "${green}  [OK] Nginx + Mainsail detected${white}\n"
fi

# Register Mainsail with Moonraker's update_manager so it actually shows up in
# Mainsail's own "Machine Update" panel. moonraker.conf already has a bare
# [update_manager] section (enables the feature), but that alone registers
# nothing — Moonraker/Klipper self-detect automatically (confirmed live via
# /machine/update/status, no config needed for those), but Mainsail is a
# static frontend, not a git repo Moonraker can introspect on its own, so it
# needs this explicit entry or it just never appears as updatable.
MK_CONF=/usr/data/printer_data/config/moonraker.conf
if [ -f /usr/data/mainsail/index.html ] && [ -f "$MK_CONF" ]; then
    if grep -q "^\[update_manager mainsail\]" "$MK_CONF"; then
        : # already registered
    else
        cp "$MK_CONF" "$BACKUP_DIR/moonraker.conf.bak-$(date +%Y%m%d-%H%M%S)" 2>/dev/null || true
        printf '\n[update_manager mainsail]\ntype: web\nrepo: mainsail-crew/mainsail\npath: /usr/data/mainsail\n' >> "$MK_CONF"
        printf "${green}  Registered Mainsail with Moonraker's update manager${white}\n"
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

# Resolve the latest published stable release dynamically instead of a hardcoded
# pin. GitHub's /releases/latest API skips prereleases/drafts, so the nightly
# builds from plain pushes to main don't count — only an actual tagged release
# does. This removes the "bump PINNED_RELEASE by hand at release time" step
# entirely: forgetting it once already meant real users silently kept pulling a
# stale, unpatched asset after a fix had already shipped in the repo.
#
# Testers can opt into a specific tag (e.g. a nightly prerelease) by exporting
# PINNED_RELEASE before running this script:
#   PINNED_RELEASE=nightly-ke-next bash installer.sh
# which skips this lookup entirely - default (unset) behavior is unchanged.
if [ -z "$PINNED_RELEASE" ]; then
    if download_file "https://api.github.com/repos/coreflake1/guppyscreen/releases/latest" /tmp/latest_release.json; then
        PINNED_RELEASE=$(jq -r '.tag_name // empty' /tmp/latest_release.json 2>/dev/null)
        rm -f /tmp/latest_release.json
    fi
    if [ -z "$PINNED_RELEASE" ]; then
        printf "${red}Could not determine the latest OpenKE release (network issue?). Check your connection and re-run the installer.${white}\n"
        exit 1
    fi
fi
printf "${green} Using OpenKE release: $PINNED_RELEASE ${white}\n"
ASSET_URL="https://github.com/coreflake1/guppyscreen/releases/download/${PINNED_RELEASE}/$ASSET_NAME.tar.gz"

printf "${green} Downloading asset: $ASSET_NAME.tar.gz ${white}\n"

# download/extract latest guppyscreen — this is the actual app binary the rest
# of the installer depends on. A silent failure here (0-byte download, corrupt
# extract) used to leave an upgrade's OLD binary in place while the later
# "does it exist"/"does it start" checks below still passed against that
# stale binary, so the installer would report full success having changed
# nothing. Same failure mode the Moonraker download fix above addresses.
if ! download_file "$ASSET_URL" /tmp/guppyscreen.tar.gz; then
    printf "${red}Could not download the GuppyScreen release asset. Check your connection and re-run the installer.${white}\n"
    exit 1
fi
if ! tar xf /tmp/guppyscreen.tar.gz -C /usr/data/; then
    printf "${red}Failed to extract the GuppyScreen release asset (corrupt download?). Re-run the installer.${white}\n"
    exit 1
fi

# substitute paths in guppyconfig.json
if [ -f "$K1_GUPPY_DIR/debian/guppyconfig.json" ]; then
    PRINTER_DATA_DIR=$(dirname "$K1_CONFIG_DIR")
    sed -i "s|<GUPPY_DIR>|$K1_GUPPY_DIR|g; s|<PRINTER_DATA_DIR>|$PRINTER_DATA_DIR|g" "$K1_GUPPY_DIR/debian/guppyconfig.json"
    if [ -s "$K1_GUPPY_DIR/guppyconfig.json" ] && jq empty "$K1_GUPPY_DIR/guppyconfig.json" >/dev/null 2>&1; then
        # Existing, valid config: merge the packaged template underneath it instead
        # of overwriting. New default keys from this release still appear, but
        # everything you already set (rotation, sensors, touch calibration,
        # toggles, ...) is kept as-is rather than reset on every reinstall.
        mkdir -p "$BACKUP_DIR" 2>/dev/null
        OLD_GUPPYCONFIG="$BACKUP_DIR/guppyconfig.json.$(date +%Y%m%d-%H%M%S).bak"
        cp "$K1_GUPPY_DIR/guppyconfig.json" "$OLD_GUPPYCONFIG" 2>/dev/null || true

        # Informational only: flag settings that differ from the recommended
        # default so you know they were kept, not silently changed either way.
        _dp=$(jq -r '.default_printer // "default_printer"' "$OLD_GUPPYCONFIG" 2>/dev/null)
        warn_if_default_differs() {   # $1=jq path  $2=recommended value  $3=label
            # Note: deliberately not using jq's `//` here — it treats `false`
            # the same as missing/null, which would silently swallow warnings
            # for exactly the boolean settings this function exists to check.
            _raw=$(jq "$1" "$OLD_GUPPYCONFIG" 2>/dev/null)
            if [ "$_raw" != "null" ] && [ -n "$_raw" ]; then
                _val=$(printf '%s' "$_raw" | sed -e 's/^"//' -e 's/"$//')
                if [ "$_val" != "$2" ]; then
                    printf "${yellow}  Note: %s is currently '%s' (recommended default: '%s') — keeping your value.${white}\n" "$3" "$_val" "$2"
                fi
            fi
        }
        warn_if_default_differs '.invert_y_direction' 'true' 'invert_y_direction'
        warn_if_default_differs '.invert_z_direction' 'true' 'invert_z_direction'
        warn_if_default_differs '.prompt_emergency_stop' 'true' 'prompt_emergency_stop'
        warn_if_default_differs ".printers[\"$_dp\"].log_level" 'info' 'log_level'

        if jq -s '.[0] * .[1]' "$K1_GUPPY_DIR/debian/guppyconfig.json" "$K1_GUPPY_DIR/guppyconfig.json" > "$K1_GUPPY_DIR/guppyconfig.json.new" 2>/dev/null; then
            mv "$K1_GUPPY_DIR/guppyconfig.json.new" "$K1_GUPPY_DIR/guppyconfig.json"
        else
            rm -f "$K1_GUPPY_DIR/guppyconfig.json.new"
            printf "${yellow}  Could not merge guppyconfig.json (jq failed) — keeping your existing file untouched.${white}\n"
        fi
    else
        cp "$K1_GUPPY_DIR/debian/guppyconfig.json" "$K1_GUPPY_DIR/guppyconfig.json"
    fi
fi
mkdir -p "$K1_GUPPY_DIR/thumbnails"

if [ ! -f "$K1_GUPPY_DIR/guppyscreen" ]; then
    printf "${red}Did not find guppyscreen in $K1_GUPPY_DIR. GuppyScreen must be extracted in $K1_GUPPY_DIR ${white}\n"
    exit 1
fi

#### verify the binary runs on this firmware before touching anything else
printf "${green}Verifying GuppyScreen binary is compatible with this firmware...${white}\n"
[ -f /etc/init.d/S99guppyscreen ] && /etc/init.d/S99guppyscreen stop &> /dev/null
killall -q guppyscreen
$K1_GUPPY_DIR/guppyscreen &> /dev/null &

## allow guppy to live a little
sleep 1

ps auxw | grep guppyscreen | grep -v sh | grep -v grep

if [ $? -eq 0 ]; then
    printf "${green}  [OK] Binary verified, continuing with installation${white}\n"
    killall -q guppyscreen
else
    printf "${red}GuppyScreen binary failed to start — aborting before any system changes.${white}\n"
    printf "${red}This usually means a firmware mismatch. Check you are on firmware 1.3.x.x.${white}\n"
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

## Seed the buzzer songs file only if absent, so user-added songs (PLAY_TUNE)
## survive reinstalls and updates.
if [ ! -f "$K1_CONFIG_DIR/songs.conf" ]; then
    cp $K1_GUPPY_DIR/scripts/songs.conf $K1_CONFIG_DIR/songs.conf
    printf "${green}Installed default buzzer songs to $K1_CONFIG_DIR/songs.conf ${white}\n"
else
    printf "${green}Keeping existing $K1_CONFIG_DIR/songs.conf (your songs are preserved) ${white}\n"
fi

## backup printer.cfg before any modification
mkdir -p $BACKUP_DIR
if [ ! -f "$BACKUP_DIR/printer.cfg.bak" ]; then
    cp "$K1_CONFIG_DIR/printer.cfg" "$BACKUP_DIR/printer.cfg.bak" 2>/dev/null || true
    printf "${green}Backed up printer.cfg to $BACKUP_DIR/printer.cfg.bak${white}\n"
fi
printf "${green}  Note: SAVE_CONFIG block (Z-offset, mesh, shaper, skew) is untouched.${white}\n"

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

## Replace dropbear init script so SSH starts reliably alongside GuppyScreen
## (stock S50dropbear has a race with display-server on boot; our version fixes it)
printf "${green}Updating SSH init script (ensures SSH is available at every boot)${white}\n"
cp $K1_GUPPY_DIR/k1_mods/S50dropbear /etc/init.d/S50dropbear

if [ ! -f /etc/init.d/S99start_app ]; then
    printf "${green}Creality services are already disabled — leaving them that way.${white}\n"
    confirm_decreality=y
else
    printf "${white}=== Do you want to disable all Creality services (revertible) with GuppyScreen installation? ===\n"
    printf "${green}  Pros: Frees up system resources on your K1 for critical services such as Klipper (Recommended)\n"
    printf "${white}  Cons: Disabling all Creality services breaks Creality Cloud/Creality Slicer.\n\n"
    printf "Disable all Creality Services? (y/n): "

    read confirm_decreality
    echo
fi

if [ "$confirm_decreality" = "y" -o "$confirm_decreality" = "Y" ]; then
    printf "${green}Disabling Creality services ${white}\n"
    [ -f /etc/init.d/S99start_app ] && rm /etc/init.d/S99start_app

    # webrtc only exists to signal Creality's cloud app (JWT-based WebSocket
    # signaling) — with no cloud account tied to this install it just retries
    # forever, burning real WiFi airtime even at idle. Rename (not delete) so
    # uninstall can restore it.
    if [ -f /etc/init.d/S97webrtc ]; then
        /etc/init.d/S97webrtc stop >/dev/null 2>&1 || true
        mv /etc/init.d/S97webrtc /etc/init.d/DISABLED_S97webrtc
    fi
else
    # disables only display-server and Monitor (guard: may not exist on all firmwares)
    [ -f /usr/bin/Monitor ] && mv /usr/bin/Monitor /usr/bin/Monitor.disable
    [ -f /usr/bin/display-server ] && mv /usr/bin/display-server /usr/bin/display-server.disable
fi

printf "${green}Setting up GuppyScreen ${white}\n"
cp $K1_GUPPY_DIR/k1_mods/S99guppyscreen /etc/init.d/S99guppyscreen

## Install USB emergency factory-reset service
printf "${green}Installing factory reset service (USB emergency reset)${white}\n"
cp $K1_GUPPY_DIR/k1_mods/S58factoryreset /etc/init.d/S58factoryreset
chmod 777 /etc/init.d/S58factoryreset

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
##   print-quality mods (adaptive mesh + purge + park, Axis Twist Compensation, TMC Autotune, Skew),
##   Creality Nebula camera (persistent image tuning),
##   E-Steps Calibration (guided CALIBRATE_ESTEPS / CALIBRATE_ESTEPS_APPLY macros),
##   Pause/Resume layer-shift fix (PAUSE y_park 222 -> 220).
## Adaptive print setup/Skew/ATC/camera config install via the existing [include GuppyScreen/*.cfg]
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

# True if some ACTIVE config file (outside our managed GuppyScreen/ dir) reads
# this section by name via a quoted printer[...] lookup, e.g.
# printer["gcode_macro M600"], OR - for a [gcode_macro NAME] section
# specifically - calls it directly as a bare gcode command elsewhere (e.g.
# some other macro's body containing a plain "SET_GCODE_OFFSET X=0" line).
# The bare-call form is arguably the more dangerous one to miss: a printer[]
# lookup just gates conditional logic, but a macro that calls a command which
# no longer exists fails hard ("Unknown command") the moment it runs, not at
# Klipper startup. Found live on a real printer during on-device verification
# (Helper-Script/timelapse.cfg calls SET_GCODE_OFFSET directly - a completely
# separate file from save-zoffset.cfg, which defines it).
# Checked across EVERY active file, not just whichever one defines the
# section - Klipper merges all [include]d files into one config, so a real
# setup can easily have the section's definition in one file and the macro
# that reads/calls it in a completely different one (this is a normal,
# common layout, not an edge case). Used by replace_conflicting_cfg to decide
# whether a section is safe to comment out.
section_referenced_elsewhere() {   # $1 = bare section name, e.g. "gcode_macro M600"
    _refbare="$1"
    _reflst="/tmp/.ke_active_ref.$$"
    active_cfgs | sort -u | grep -v "/GuppyScreen/" > "$_reflst"
    _refret=1
    while IFS= read -r _rf; do
        [ -f "$_rf" ] || continue
        if grep -qE "[\"']${_refbare}[\"']" "$_rf" 2>/dev/null; then _refret=0; break; fi
    done < "$_reflst"
    case "$_refbare" in
        "gcode_macro "*)
            _refcmd="${_refbare#gcode_macro }"
            if [ "$_refret" -ne 0 ]; then
                while IFS= read -r _rf; do
                    [ -f "$_rf" ] || continue
                    if grep -qE "^[[:space:]]*${_refcmd}([[:space:]]|\$)" "$_rf" 2>/dev/null; then _refret=0; break; fi
                done < "$_reflst"
            fi
            ;;
    esac
    rm -f "$_reflst"
    return $_refret
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

# Klipper allows exactly one [save_variables] section in the whole config. Some
# features (Save Z-Offset, camera tuning) need SAVE_VARIABLE support but must not
# assume they're the one defining it — reuse whatever's already active (ours or
# the user's own/Helper Script's), and only add one if truly none exists anywhere.
# Unlike section_defined_elsewhere, this also checks our own GuppyScreen/ dir, so
# it correctly sees a [save_variables] this same installer run already added.
ensure_save_variables() {
    _found=1
    active_cfgs | sort -u > "/tmp/.ke_svcheck.$$"
    while IFS= read -r _f; do
        [ -f "$_f" ] || continue
        if grep -qE '^\[save_variables\]' "$_f" 2>/dev/null; then _found=0; break; fi
    done < "/tmp/.ke_svcheck.$$"
    rm -f "/tmp/.ke_svcheck.$$"
    [ "$_found" -eq 0 ] && return 0
    cat > "$GUPPY_CFG_DIR/save_variables.cfg" << 'SAVE_VARS_EOF'
[save_variables]
filename: /usr/data/printer_data/config/variables.cfg
SAVE_VARS_EOF
    printf "${green}  Added [save_variables] (none was defined yet; needed by camera macros)${white}\n"
}

# Copy a vendored .cfg into GuppyScreen/ ONLY if none of the [sections] it defines
# already exist elsewhere in the config. Prevents a duplicate-section crash when a
# stock or Helper-Script config already owns one of those sections.
install_cfg_guarded() {   # $1 = src path, $2 = dest filename, $3 = label
    _src="$1"; _dest="$2"; _label="$3"
    [ -f "$_src" ] || return 0
    _secfile="/tmp/.ke_secs.$$"
    grep -oE '^\[[^]]+\]' "$_src" 2>/dev/null | sort -u > "$_secfile"
    # Collect every conflicting section in one pass rather than stopping at the first -
    # a file that owns N conflicting sections (e.g. M600-support.cfg) used to only ever
    # report one per installer run, forcing a comment-one/rerun/comment-next cycle to
    # discover them all instead of seeing the full list up front.
    _conflicts=""
    while IFS= read -r _sec; do
        [ -z "$_sec" ] && continue
        if section_defined_elsewhere "$_sec"; then
            _conflicts="${_conflicts:+$_conflicts, }$_sec"
        fi
    done < "$_secfile"
    rm -f "$_secfile"
    if [ -n "$_conflicts" ]; then
        printf "${yellow}  Skipping %s — your config already defines %s.${white}\n" "$_label" "$_conflicts"
    else
        cp "$_src" "$GUPPY_CFG_DIR/$_dest"
        printf "${green}  Installed %s${white}\n" "$_label"
    fi
}

# Like install_cfg_guarded, but for macro files an OpenKE on-screen feature (M600's
# filament-change UI, the Z-offset panel, the E-Steps wizard, ...) specifically depends
# on by exact macro name - instead of skipping forever on a conflict, offer to safely
# replace it: back up the conflicting file, comment out just the conflicting sections,
# and install our version in their place. Only ever mutates if NOTHING comes back
# "unsafe" (i.e. some macro ANYWHERE in the active config - not just the same file -
# reads printer["<section>"] / printer['<section>'] at runtime, OR (for a gcode_macro)
# calls it directly as a bare command, via section_referenced_elsewhere - a sign of a
# genuinely working, self-contained subsystem rather than a stray duplicate; blindly
# commenting there would leave that macro referencing/calling something that no
# longer exists, even if the reader lives in a completely different [include]d file
# than the definition). Always asks first.
replace_conflicting_cfg() {   # $1=src path, $2=dest filename, $3=label, $4=backup-tag
    _rc_src="$1"; _rc_dest="$2"; _rc_label="$3"; _rc_tag="$4"
    _rc_secs=$(grep -oE '^\[[^]]+\]' "$_rc_src" 2>/dev/null | sort -u)
    _rc_conflict=1
    _rc_oldifs="$IFS"; IFS='
'
    for _rc_sec in $_rc_secs; do
        IFS="$_rc_oldifs"
        section_defined_elsewhere "$_rc_sec" && _rc_conflict=0
        IFS='
'
    done
    IFS="$_rc_oldifs"
    if [ "$_rc_conflict" -ne 0 ]; then
        install_cfg_guarded "$_rc_src" "$_rc_dest" "$_rc_label"
        return 0
    fi

    printf "${yellow}  Skipping %s — one or more of its sections are already present in your config:${white}\n" "$_rc_label"
    _rc_oldifs="$IFS"; IFS='
'
    for _rc_sec in $_rc_secs; do
        IFS="$_rc_oldifs"
        printf "${yellow}    %s${white}\n" "$_rc_sec"
        IFS='
'
    done
    IFS="$_rc_oldifs"
    printf "  Comment out the conflicting sections automatically and install OpenKE's %s? (y/N): " "$_rc_label"
    read _rc_fix
    if [ "$_rc_fix" != "y" ] && [ "$_rc_fix" != "Y" ]; then
        printf "${yellow}  Skipped. Re-run the installer after commenting out the sections above.${white}\n"
        return 0
    fi

    # Two passes: find every conflicting file WITHOUT touching anything, classifying
    # each section as safe-to-comment or not, only mutate afterward and only if
    # nothing came back unsafe.
    _rc_safe="/tmp/.ke_rc_safe.$$"
    _rc_unsafe="/tmp/.ke_rc_unsafe.$$"
    : > "$_rc_safe"
    : > "$_rc_unsafe"
    _rc_oldifs="$IFS"; IFS='
'
    for _rc_sec in $_rc_secs; do
        IFS="$_rc_oldifs"
        _rc_pat=$(printf '%s' "$_rc_sec" | sed 's/[][]/\\&/g')
        _rc_bare=$(printf '%s' "$_rc_sec" | sed 's/^\[//; s/\]$//')
        # Checked once against every active file (not just whichever file
        # defines the section) - a config that splits a macro's definition
        # and its consumer across two separate [include]d files is a normal
        # Klipper layout, and must not be misclassified as safe just because
        # the defining file, in isolation, looks clean.
        if section_referenced_elsewhere "$_rc_bare"; then
            _rc_this_unsafe=1
        else
            _rc_this_unsafe=0
        fi
        _rc_deflst="/tmp/.ke_rc_def.$$"
        active_cfgs | sort -u | grep -v "/GuppyScreen/" > "$_rc_deflst"
        while IFS= read -r _rc_f; do
            [ -f "$_rc_f" ] || continue
            grep -qE "^[[:space:]]*$_rc_pat" "$_rc_f" 2>/dev/null || continue
            if [ "$_rc_this_unsafe" -eq 1 ]; then
                echo "$_rc_sec|$_rc_f" >> "$_rc_unsafe"
            else
                echo "$_rc_sec|$_rc_f" >> "$_rc_safe"
            fi
        done < "$_rc_deflst"
        rm -f "$_rc_deflst"
        IFS='
'
    done
    IFS="$_rc_oldifs"

    if [ -s "$_rc_unsafe" ]; then
        printf "${yellow}  Found sections that are read by a macro somewhere in your active\n"
        printf "${yellow}  config - that looks like a working, self-contained setup rather\n"
        printf "${yellow}  than a stray duplicate, so nothing was changed:${white}\n"
        while IFS='|' read -r _rc_sec _rc_f; do
            printf "${yellow}    %s in %s${white}\n" "$_rc_sec" "$_rc_f"
        done < "$_rc_unsafe"
        printf "${yellow}  Skipping the OpenKE %s install — your existing setup should keep working.${white}\n" "$_rc_label"
        rm -f "$_rc_safe" "$_rc_unsafe"
        return 0
    fi

    # Comment out every safe section for a given file in ONE pass (not one pass per
    # section) - a separate awk invocation per section can't reset `in_sec` once an
    # earlier pass's own target header becomes a comment and stops matching /^\[/,
    # so a later pass bleeds past it to the next real header, stacking extra '#'s
    # onto lines already handled. Harmless (still all correctly disabled) but noisy;
    # a single batched pass per file avoids it.
    _rc_files=$(cut -d'|' -f2 "$_rc_safe" | sort -u)
    _rc_oldifs="$IFS"; IFS='
'
    for _rc_f in $_rc_files; do
        IFS="$_rc_oldifs"
        [ -f "$_rc_f" ] || continue
        cp "$_rc_f" "$BACKUP_DIR/$(basename "$_rc_f").bak-$_rc_tag-$(date +%Y%m%d-%H%M%S)"
        _rc_target=$(awk -F'|' -v f="$_rc_f" '$2==f{print $1}' "$_rc_safe")
        awk -v secs="$_rc_target" '
            BEGIN { n = split(secs, arr, "\n"); for (i=1;i<=n;i++) target[arr[i]] = 1 }
            # Same CRLF-safe match as the section-conflict check (leading whitespace
            # only, no end anchor) rather than requiring a byte-identical line - a
            # stray trailing \r or trailing space made an exact match miss silently.
            { stripped = $0; gsub(/[ \t\r]+$/, "", stripped) }
            /^\[/ { in_sec = (stripped in target) ? 1 : 0 }
            { print (in_sec ? "#" : "") $0 }
        ' "$_rc_f" > "/tmp/.ke_rc.$$" && mv "/tmp/.ke_rc.$$" "$_rc_f"
        printf "${green}  Commented out the conflicting sections in %s${white}\n" "$_rc_f"
        IFS='
'
    done
    IFS="$_rc_oldifs"
    rm -f "$_rc_safe" "$_rc_unsafe"
    install_cfg_guarded "$_rc_src" "$_rc_dest" "$_rc_label"
}

# --- Migration: remove the deprecated H.264 camera stream (go2rtc), if present. ---
# Runs unconditionally on every install/upgrade (not an opt-in). The go2rtc+ffmpeg
# stack is the memory-pressure / OOM driver on the 197 MB box; v1.2.0 drops it and
# reverts to the stock camera. main's h264cam never disabled the stock camera, so
# simply removing it leaves the stock feed intact. (Mirrors the old
# k1_mods/h264cam/install.sh `uninstall`, inlined since that mod is gone now.)
if [ -f /etc/init.d/S96h264cam ] || [ -d /usr/data/h264cam ]; then
    printf "${yellow}Removing the deprecated H.264 camera stream (go2rtc) -> stock camera...${white}\n"
    [ -f /etc/init.d/S96h264cam ] && /etc/init.d/S96h264cam stop 2>/dev/null
    for p in $(pidof go2rtc) $(pidof memfd_h264_dump); do kill "$p" 2>/dev/null; done
    rm -f /etc/init.d/S96h264cam
    rm -rf /usr/data/h264cam
    # Remove the "Nebula H264" Moonraker webcam via python3 — the device's busybox
    # curl rejects -X DELETE, so the old curl-based uninstall left this entry behind.
    python3 - <<'PY' >/dev/null 2>&1
import urllib.request
try:
    urllib.request.urlopen(urllib.request.Request(
        "http://localhost:7125/server/webcams/item?name=Nebula%20H264", method="DELETE"), timeout=5)
except Exception:
    pass
PY
fi

printf "${white}=== OpenKE optional features ===\n"
printf "${green}  Print-quality mods (adaptive mesh + purge + park, Axis Twist Compensation, TMC Autotune, Skew Correction),\n"
printf "${green}  the Creality Nebula camera (persistent image tuning), Creality macros\n"
printf "${green}  (M600, Save Z-Offset, useful macros, Exclude Object), E-Steps Calibration,\n"
printf "${green}  and the layer-shift fix.${white}\n\n"
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
    # --- Adaptive Print Setup (mesh + purge + park) ---
    # Used to vendor KAMP wholesale. Adaptive_Meshing.cfg is now original OpenKE
    # code delegating to Klipper's native BED_MESH_CALIBRATE ADAPTIVE=1 (patched
    # into the KE's Klipper via patch_bed_mesh.py, since its fork predates that
    # upstream merge). Line_Purge.cfg/Smart_Park.cfg are still adapted from KAMP
    # (GPL-3.0, see NOTICE.md) but no longer a package dependency we track/sync -
    # own copies, own settings macro. See docs/VENDORING.md.
    if [ -d "$MODS_DIR/adaptive_print_setup" ] && want "Adaptive mesh + purge + park (formerly KAMP)"; then
        OLD_KAMP_SETTINGS=""
        for _cand in "$K1_CONFIG_DIR/KAMP_Settings.cfg" "$GUPPY_CFG_DIR/KAMP_Settings.cfg"; do
            [ -f "$_cand" ] && OLD_KAMP_SETTINGS="$_cand" && break
        done

        if section_defined_elsewhere "[gcode_macro _OPENKE_ADAPTIVE_SETTINGS]"; then
            printf "${yellow}  Adaptive print setup already configured elsewhere — leaving it untouched.${white}\n"
        else
            printf "${green}  Installing Adaptive Print Setup (mesh + purge + park)${white}\n"

            # Settings.cfg below gets overwritten unconditionally (section_defined_elsewhere
            # deliberately excludes our own GuppyScreen/ dir, so a prior OpenKE install's
            # Settings.cfg never blocks this - that's what lets a re-run pick up a newly
            # added macro like START_PRINT). Snapshot any customized values first so
            # migrate_settings.py can restore them afterward - same mechanism as the KAMP
            # migration below, just pointed at our own previous file instead of KAMP's.
            _prev_settings="$OLD_KAMP_SETTINGS"
            if [ -z "$_prev_settings" ] && [ -f "$GUPPY_CFG_DIR/Settings.cfg" ]; then
                _prev_settings="/tmp/.ke_prev_settings.$$"
                cp "$GUPPY_CFG_DIR/Settings.cfg" "$_prev_settings"
            fi

            cp -r "$MODS_DIR/adaptive_print_setup/modules" "$GUPPY_CFG_DIR/"
            cp "$MODS_DIR/adaptive_print_setup/Settings.cfg" "$GUPPY_CFG_DIR/Settings.cfg"

            if [ -n "$OLD_KAMP_SETTINGS" ]; then
                printf "${green}  Found an existing KAMP install — migrating its settings and removing it${white}\n"
                if [ ! -f "$BACKUP_DIR/KAMP_Settings.cfg.bak" ]; then
                    cp "$OLD_KAMP_SETTINGS" "$BACKUP_DIR/KAMP_Settings.cfg.bak"
                    [ -d "$(dirname "$OLD_KAMP_SETTINGS")/KAMP" ] && \
                        cp -r "$(dirname "$OLD_KAMP_SETTINGS")/KAMP" "$BACKUP_DIR/KAMP.bak"
                fi
                python3 "$MODS_DIR/adaptive_print_setup/migrate_settings.py" \
                    "$OLD_KAMP_SETTINGS" "$GUPPY_CFG_DIR/Settings.cfg"
                rm -rf "$K1_CONFIG_DIR/KAMP" "$K1_CONFIG_DIR/KAMP_Settings.cfg" \
                       "$GUPPY_CFG_DIR/KAMP" "$GUPPY_CFG_DIR/KAMP_Settings.cfg"
                sed -i '/\[include KAMP_Settings\.cfg\]/d' "$K1_CONFIG_DIR/printer.cfg"
            elif [ -n "$_prev_settings" ]; then
                python3 "$MODS_DIR/adaptive_print_setup/migrate_settings.py" \
                    "$_prev_settings" "$GUPPY_CFG_DIR/Settings.cfg"
                rm -f "$_prev_settings"
            fi
        fi

        backup_extra_once bed_mesh.py
        python3 "$MODS_DIR/adaptive_print_setup/patch_bed_mesh.py" "$KLIPPY_EXTRA_DIR/bed_mesh.py" || \
            printf "${yellow}  Adaptive mesh needs bed_mesh.py patched (see above) - patch by hand if your firmware differs (wiki).${white}\n"
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

    # --- Firmware Retraction ---
    if [ -d "$MODS_DIR/firmware_retraction" ] && want "Firmware Retraction (enable G10/G11 retract commands)"; then
        if section_defined_elsewhere "[firmware_retraction]"; then
            printf "${yellow}  [firmware_retraction] already in your config — leaving it untouched.${white}\n"
        else
            printf "${green}  Enabling Firmware Retraction${white}\n"
            cp "$MODS_DIR/firmware_retraction/firmware_retraction.cfg" "$GUPPY_CFG_DIR/firmware_retraction.cfg"
        fi
    fi

    # --- Screws Tilt Adjust: module + cfg (KE's stock Klipper lacks this module) ---
    if [ -d "$MODS_DIR/screws_tilt_adjust" ] && want "Screws Tilt Adjust (SCREWS_TILT_CALCULATE bed levelling)"; then
        printf "${green}  Installing Screws Tilt Adjust${white}\n"
        backup_extra_once screws_tilt_adjust.py
        cp "$MODS_DIR/screws_tilt_adjust/screws_tilt_adjust.py" "$KLIPPY_EXTRA_DIR/"
        if section_defined_elsewhere "[screws_tilt_adjust]"; then
            printf "${yellow}  [screws_tilt_adjust] already in your config — leaving it untouched.${white}\n"
        else
            cp "$MODS_DIR/screws_tilt_adjust/screws_tilt_adjust.cfg" "$GUPPY_CFG_DIR/screws_tilt_adjust.cfg"
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
        # Useful macros (backup/restore/reload-camera/PID/bed-leveling/warmup): same
        # detect-conflict-and-offer-to-replace treatment as the others below. No OpenKE
        # panel depends on these by exact name, but a pre-existing conflicting version
        # would otherwise keep silently pointing at someone else's backup/restore scripts
        # forever - fixing it here for the same future-proofing/maintainability reason as
        # the rest, not because anything currently breaks without it.
        replace_conflicting_cfg "$CM/useful-macros.cfg" "useful-macros.cfg" "useful macros (backup/restore, PID, bed-level, warmup)" "usefulmacros"

        # Save Z-Offset: same detect-conflict-and-offer-to-replace treatment as M600 below,
        # instead of install_cfg_guarded's default skip. A pre-existing Helper Script
        # save-zoffset.cfg is exactly the scenario that leaves our own Z-offset panel
        # silently depending on someone else's file for persistence rather than installing
        # cleanly, like it would on a fresh setup. No settings-migration step needed here
        # (unlike KAMP/adaptive print setup) - the actual saved value lives in
        # variables.cfg, external to this cfg file, and both the old and new file point at
        # the same filename, so the value survives the swap automatically.
        replace_conflicting_cfg "$CM/save-zoffset.cfg" "save-zoffset.cfg" "Save Z-Offset (persists z-offset across reboots)" "zoffset"

        # Same detect-conflict-and-offer-to-replace treatment as Save Z-Offset above -
        # the on-screen OpenKE filament-change UI (load/unload/purge buttons) needs its
        # own M600/idle_timeout/RESUME structure, so a pre-existing conflicting version
        # left in place forever would silently degrade or break that UI.
        replace_conflicting_cfg "$CM/M600-support.cfg" "M600-support.cfg" "M600 filament-change support" "m600"
        install_cfg_guarded "$CM/exclude_object.cfg" "exclude_object.cfg" "Exclude Object"
        # moonraker: object processing (needed by Exclude Object + adaptive mesh). Add only if safe.
        MK_CONF="$K1_CONFIG_DIR/moonraker.conf"
        if [ -f "$MK_CONF" ]; then
            if grep -q "enable_object_processing" "$MK_CONF"; then
                : # already set
            elif grep -q "^\[file_manager\]" "$MK_CONF"; then
                # [file_manager] already exists (true for this installer's own
                # fresh-install template, and for virtually any pre-existing
                # Moonraker/Helper-Script conf) — insert the option under the
                # existing header instead of just printing a note, or Exclude
                # Object/adaptive mesh silently never get their Moonraker-side
                # gcode preprocessing on the single most common path.
                cp "$MK_CONF" "$BACKUP_DIR/moonraker.conf.bak-$(date +%Y%m%d-%H%M%S)" 2>/dev/null || true
                sed -i '/^\[file_manager\]/a enable_object_processing: True' "$MK_CONF"
                printf "${green}  Enabled object processing in moonraker.conf${white}\n"
            else
                cp "$MK_CONF" "$BACKUP_DIR/moonraker.conf.bak-$(date +%Y%m%d-%H%M%S)" 2>/dev/null || true
                printf '\n[file_manager]\nenable_object_processing: True\n' >> "$MK_CONF"
                printf "${green}  Enabled object processing in moonraker.conf${white}\n"
            fi
        fi
    fi

    # --- Creality Nebula camera: image tuning that persists across reboots ---
    # (The H.264 go2rtc stream was removed in v1.2.0 — see the migration step above.)
    # Runs after the Creality-macros block above so ensure_save_variables correctly
    # sees a [save_variables] that block may have just installed (e.g. Save Z-Offset).
    if [ -f "$MODS_DIR/nebula_camera/nebula_camera.cfg" ] && want "Creality Nebula camera (persistent image tuning)"; then
        ensure_save_variables
        install_cfg_guarded "$MODS_DIR/nebula_camera/nebula_camera.cfg" "nebula_camera.cfg" "Creality Nebula camera tuning + persist macros"
    fi

    # --- E-Steps calibration: guided CALIBRATE_ESTEPS / CALIBRATE_ESTEPS_APPLY macros ---
    # Same detect-conflict/offer-to-replace treatment as M600/Save Z-Offset above: the
    # on-screen E-Steps wizard (src/esteps_calibration_panel.cpp) calls these exact macro
    # names, so a same-named pre-existing macro (someone's own e-steps helper, a different
    # guide's version, etc.) left in place forever would silently break or mismatch the
    # wizard rather than ever getting OpenKE's own version.
    if [ -f "$MODS_DIR/esteps_calibration/esteps_calibration.cfg" ] && want "E-Steps Calibration (guided rotation_distance tuning)"; then
        ensure_save_variables
        replace_conflicting_cfg "$MODS_DIR/esteps_calibration/esteps_calibration.cfg" "esteps_calibration.cfg" "E-Steps Calibration macros" "esteps"
    fi

    printf "${green}Optional features done.${white}\n"
    printf "${yellow}  One-time setup still needed where applicable (see the OpenKE wiki):\n"
    printf "${yellow}    Axis Twist: BED_MESH_CLEAR; AXIS_TWIST_COMPENSATION_CALIBRATE SAMPLE_COUNT=5; SAVE_CONFIG\n"
    printf "${yellow}    Adaptive mesh/purge/park: add ADAPTIVE_BED_MESH_CALIBRATE / SMART_PARK / LINE_PURGE to slicer start g-code\n"
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
    _ip=$(ip route get 1 2>/dev/null | awk 'NR==1{for(i=1;i<=NF;i++) if ($i=="src") {print $(i+1); exit}}')
    printf "${green}\n=== GuppyScreen installed successfully ===\n${white}"
    printf "${green}  GuppyScreen is running on the printer screen.\n${white}"
    if [ -f /etc/init.d/S50nginx ] && [ -f /usr/data/mainsail/index.html ]; then
        printf "${green}  Mainsail web UI:  http://${_ip}:4409/\n${white}"
    fi
    printf "${green}  Backups saved to: $BACKUP_DIR\n${white}"
    printf "${yellow}  Next steps and calibration guide: https://github.com/coreflake1/guppyscreen/wiki\n${white}"
else
    printf "${red}GuppyScreen failed to start. Rolling back...${white}\n"
    cp $BACKUP_DIR/S99start_app /etc/init.d/S99start_app
    rm /etc/init.d/S99guppyscreen

    /etc/init.d/S99start_app restart &> /dev/null
    exit 1
fi
