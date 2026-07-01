#!/bin/bash

RELEASES_DIR=./releases/guppyscreen
rm -rf $RELEASES_DIR
mkdir -p $RELEASES_DIR

ASSET_NAME=$1

"$CROSS_COMPILE"strip ./build/bin/guppyscreen
cp ./build/bin/guppyscreen $RELEASES_DIR/guppyscreen
if [ -f ./build/bin/guppybeep ]; then
    "$CROSS_COMPILE"strip ./build/bin/guppybeep
    cp ./build/bin/guppybeep $RELEASES_DIR/guppybeep
fi
cp -r ./k1/k1_mods $RELEASES_DIR
cp -r ./k1/scripts $RELEASES_DIR
cp -r ./themes $RELEASES_DIR
cp ./scripts/installer-deb.sh $RELEASES_DIR
cp ./scripts/installer.sh $RELEASES_DIR
cp ./scripts/update.sh $RELEASES_DIR
if [ -f ./custom_upgrade.sh ]; then
    cp ./custom_upgrade.sh $RELEASES_DIR
fi
# debian/ must land before kd_graphic_mode: the binary is placed inside it
# (see debian/disable_blinking_cursor.service, which execs debian/kd_graphic_mode).
# Copying it to $RELEASES_DIR/debian directly (old behavior) collided with this
# cp -r, which cp then silently refused (won't overwrite a dir with a file) —
# every release ended up shipping that stray binary instead of the debian/ dir.
cp -r ./debian $RELEASES_DIR
if [ -f ./build/bin/kd_graphic_mode ]; then
    "$CROSS_COMPILE"strip ./build/bin/kd_graphic_mode
    cp ./build/bin/kd_graphic_mode $RELEASES_DIR/debian/kd_graphic_mode
fi


echo "{\"version\": \"$GUPPYSCREEN_VERSION\", \"theme\": \"$GUPPY_THEME\", \"asset_name\": \"$ASSET_NAME.tar.gz\"}" > $RELEASES_DIR/.version
tar czf $ASSET_NAME.tar.gz -C releases .
