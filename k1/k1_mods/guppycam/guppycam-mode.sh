#!/bin/sh
# guppycam-mode.sh {install|uninstall|enable|disable|status}
#
# Makes guppycam the boot camera in place of cam_app -- WITHOUT a runtime hot-swap.
# The choice is made at boot: the udev launcher (auto_uvc.sh) starts guppycam on a
# PRISTINE Helix codec, and cam_app simply never runs. cam_app is NEVER deleted, so
# reverting to stock is just a boot-time toggle.
#
#   install   - save stock /usr/bin/auto_uvc.sh -> auto_uvc.stock, install dispatcher,
#               arm the guppycam binary. (Does NOT enable yet.)
#   enable    - turn guppycam mode ON for the next boot (creates ENABLED flag).
#   disable   - turn guppycam mode OFF for the next boot (cam_app comes back).
#   uninstall - disable + restore the stock auto_uvc.sh.
#   status    - show current wiring.
# A reboot applies enable/disable. Nothing is hot-swapped on a running system.
DIR=/usr/data/guppycam
STOCK=/usr/bin/auto_uvc.sh
STOCK_BAK=/usr/bin/auto_uvc.stock
FLAG=$DIR/ENABLED

is_installed() { [ -f "$STOCK_BAK" ] && grep -q "guppycam dispatcher" "$STOCK" 2>/dev/null; }

install() {
  [ -x "$DIR/guppycam" ] || { [ -f "$DIR/guppycam.OFF" ] && mv "$DIR/guppycam.OFF" "$DIR/guppycam"; }
  [ -x "$DIR/guppycam" ] || { echo "ERROR: $DIR/guppycam missing"; return 1; }
  chmod +x "$DIR/guppycam-boot.sh" "$DIR/guppycam-delayed-start.sh" "$DIR/guppycam-register.sh" "$DIR/guppycam-freezelog.sh" 2>/dev/null
  if is_installed; then echo "dispatcher already installed"; return 0; fi
  cp "$STOCK" "$STOCK_BAK" || { echo "ERROR: backup failed"; return 1; }
  cp "$DIR/auto_uvc.dispatch.sh" "$STOCK" && chmod +x "$STOCK"
  echo "installed dispatcher (stock saved to $STOCK_BAK)"
}

uninstall() {
  disable
  if [ -f "$STOCK_BAK" ]; then cp "$STOCK_BAK" "$STOCK" && chmod +x "$STOCK" && rm -f "$STOCK_BAK"; echo "restored stock auto_uvc.sh"; fi
}

enable()  { is_installed || { echo "not installed - run: $0 install"; return 1; }; : > "$FLAG"; rm -f "$DIR/.boot-attempt"; echo "guppycam mode ENABLED (effective next boot)"; }
disable() { rm -f "$FLAG" "$DIR/.boot-attempt"; echo "guppycam mode DISABLED (stock cam_app next boot)"; }

status() {
  echo "  installed : $(is_installed && echo yes || echo no)   enabled(next boot): $([ -f "$FLAG" ] && echo YES || echo no)"
  echo "  binary    : $([ -x "$DIR/guppycam" ] && echo armed || echo "OFF/missing")   boot-attempt marker: $([ -f "$DIR/.boot-attempt" ] && echo PRESENT || echo clear)"
  echo "  running   : cam_app=[$(pidof cam_app)] guppycam=[$(pidof guppycam)] mjpg=[$(pidof mjpg_streamer)]"
}

case "$1" in
  install) install ;;
  uninstall) uninstall ;;
  enable) enable ;;
  disable) disable ;;
  status) status ;;
  *) echo "usage: $0 {install|uninstall|enable|disable|status}" ;;
esac
