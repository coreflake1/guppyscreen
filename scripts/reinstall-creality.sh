#!/bin/sh
# Uninstall OpenKE and restore the stock Creality UI.
# Delegates to the authoritative uninstall logic in installer.sh.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
exec "$SCRIPT_DIR/installer.sh" uninstall
