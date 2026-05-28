## Highlights
- Promoted `ke-gui-fixes` into `main` as the new release baseline.
- Added advanced 3D bed mesh visualization and redesigned bed mesh panel behavior.
- Added KE small-screen (480x272) focused UI/font/layout improvements.
- Improved panel visibility flow based on print state and refined tuner/LED panel ordering.

## Fixes
- Fixed wake/sleep display reliability issues (blank/white screen scenarios).
- Set safer defaults for KE orientation/display and small-screen font sizing.
- Applied small-screen readout/layout corrections and default MCU temp sensor handling.
- Improved installer robustness: uninstall mode, backup behavior, silent include handling, and missing-file guards.

## Breaking Changes
- None known.

## Upgrade Notes
- Recommended upgrade path is now from `main` at tag `v0.1.1-ke-gui-fixes`.
- Existing users of bedmesh branches can switch to `main` after this release.
- If you have local configuration edits, back up your config files before upgrade.
- For KE users, review orientation and display settings after first boot to confirm expected behavior.

## Included Commits
- 75cb487 Merge pull request #1 from coreflake1/ke-gui-fixes
- 9169a81 docs: record the 2026-05-26 GUI/functional pass
- 925211a fix: small-screen readout/layout tweaks; default MCU temp sensor
- d234497 feat: redesign bed mesh panel — table default, 3D fullscreen
- 57b165f feat: gate panels behind print state; swap LED and Fine Tune
- 88131d2 ui: 480x272 layout + global font pass for KE small screen
- a9b543a fix: disable fbdev_blank on sleep — Ingenic X2000 DSI unrecoverable
- a042f6e fix: invalidate screen on wake to prevent white screen after sleep
- 14d3585 fix: reduce LV_FONT_DEFAULT to montserrat_12 on GUPPY_SMALL_SCREEN
- 8bed38d fix: set display_rotate=2 default for KE upside-down screen mount
- 3ef8b4b docs: fill in confirmed on-device facts from SSH inspection
- 09c334c docs: add dev-notes with hardware, installer, and build discoveries
- 89e13f7 installer.sh: add uninstall mode, backup printer.cfg before modify, fix silent include, guard KE-missing files
- ae4a270 installer.sh: pin to coreflake1 v0.1.0-ke-bedmesh, add guppyconfig.json setup
- 78d8556 installer/README: pre-install safety pass
- 4c0f335 README: update build instructions and install/uninstall for coreflake1 fork
- b3d1d87 scripts: adapt installer-deb.sh and release.sh for coreflake1 fork
- 62dc656 README: document full fork lineage and probielodan changes vs ballaswag
- d64b1a8 Add build instructions for ke-advanced-3d-bedmesh branch
- 1447299 Add advanced 3D bed mesh visualization with interactive controls
