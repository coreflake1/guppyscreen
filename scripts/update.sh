#!/bin/sh

GUPPY_DIR=$(dirname "$0")
VERSION_FILE=$GUPPY_DIR/.version
CUSTOM_UPGRADE_SCRIPT=$GUPPY_DIR/custom_upgrade.sh

if [ -f $VERSION_FILE ]; then
    CURRENT_VERSION=`cat $VERSION_FILE | jq -r .version`
    THEME=`cat $VERSION_FILE | jq -r .theme`
    # -r so ASSET_NAME is the bare string; the un-rawed value kept its quotes and
    # broke the jq asset filter below.
    ASSET_NAME=`cat $VERSION_FILE | jq -r .asset_name`
fi

CURL=`which curl`
if grep -Fqs "ID=buildroot" /etc/os-release
then
    wget -q --no-check-certificate https://raw.githubusercontent.com/ballaswag/k1-discovery/main/bin/curl -O /tmp/curl
    chmod +x /tmp/curl
    CURL=/tmp/curl
fi

# GuppyKE lives at coreflake1/guppyscreen (was probielodan, upstream of this fork).
$CURL -s https://api.github.com/repos/coreflake1/guppyscreen/releases -o /tmp/guppy-releases.json
# Only consider stable releases: pushes to main publish a rolling "nightly" prerelease
# whose tag doesn't version-compare, so ignore prereleases for the update check.
latest_version=`jq -r '[.[] | select(.prerelease == false)][0].tag_name' /tmp/guppy-releases.json`

if [ -z "$latest_version" ] || [ "$latest_version" = "null" ]; then
    echo "Could not determine latest release; skipping update."
    exit 0
fi

if [ "$(printf '%s\n' "$CURRENT_VERSION" "$latest_version" | sort -V | head -n1)" = "$latest_version" ]; then
    echo "Current version $CURRENT_VERSION is up to date."
    exit 0
else
    # --arg passes the asset name safely (no shell-quote breakage) and we pull the asset
    # from the same stable release chosen above.
    asset_url=`jq -r --arg n "$ASSET_NAME" '[.[] | select(.prerelease == false)][0].assets[] | select(.name == $n).browser_download_url' /tmp/guppy-releases.json`
    echo "Downloading latest version $latest_version, $asset_url"
    $CURL -L "$asset_url" -o /tmp/guppyscreen.tar.gz
    _curl_rc=$?
    # A mid-transfer failure (TLS reset, dropped connection, ...) can still
    # leave a partial file on disk, and this had NO check at all before -
    # straight into tar on whatever showed up. Same class of bug found and
    # fixed in installer.sh's download_file() against a real v1.4.0 report;
    # this script has its own separate download path with the same gap.
    if [ "$_curl_rc" -ne 0 ] || [ ! -s /tmp/guppyscreen.tar.gz ]; then
        echo "Download failed or produced an empty/corrupt file - aborting before touching the existing install."
        exit 1
    fi
fi

## override existing guppyscreen
# A prior corrupt/truncated extraction can leave one of these directories
# mistyped as a plain file on disk, which tar refuses to overwrite ("File
# exists" / "Not a directory") - permanently blocking every future update
# otherwise, even once the download itself succeeds cleanly. Same fix as
# installer.sh's GuppyScreen asset extraction (found on a real user's printer,
# 2026-07-16). These four are entirely packaged content the archive fully
# recreates every run - deliberately NOT touching guppyconfig.json/thumbnails/
# etc, which must survive an update.
rm -rf "$GUPPY_DIR/k1_mods" "$GUPPY_DIR/scripts" "$GUPPY_DIR/themes" "$GUPPY_DIR/debian"
if ! tar xf /tmp/guppyscreen.tar.gz -C $GUPPY_DIR/..; then
    echo "Failed to extract the downloaded update (corrupt download?) - aborting, existing install untouched."
    exit 1
fi

## Re-sync helper scripts (installer.sh does this on a fresh install, but an
## in-place OTA update never re-runs the installer, so new/changed .py/.cfg
## helper scripts were never reaching GuppyScreen/scripts here - only the
## binary itself got updated. Same source, same destination as installer.sh.
K1_CONFIG_DIR=/usr/data/printer_data/config
mkdir -p $K1_CONFIG_DIR/GuppyScreen/scripts
cp $GUPPY_DIR/scripts/*.cfg $K1_CONFIG_DIR/GuppyScreen
cp $GUPPY_DIR/scripts/*.py $K1_CONFIG_DIR/GuppyScreen/scripts

if [ -f $CUSTOM_UPGRADE_SCRIPT ]; then
    echo "Running custom_upgrade.sh for release $latest_version"
    $CUSTOM_UPGRADE_SCRIPT
fi

echo "Updated Guppy Screen to version $latest_version"
if grep -Fqs "ID=buildroot" /etc/os-release
then
    [ -f /etc/init.d/S99guppyscreen ] && /etc/init.d/S99guppyscreen stop &> /dev/null
    killall -q guppyscreen
    /etc/init.d/S99guppyscreen restart &> /dev/null
fi

exit 0
