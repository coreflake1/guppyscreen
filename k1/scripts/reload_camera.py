#!/usr/bin/env python3

# OpenKE — reload the stock Creality camera pipeline (cam_app + mjpg_streamer).
#
# Replaces a dependency on the Creality Helper Script's own camera reload
# (which calls into Helper Script's auto_uvc.sh — most OpenKE-only installs
# don't have it, so RELOAD_CAMERA existed on-screen but silently failed).
#
# Neither cam_app nor mjpg_streamer has a supervisor watching them (both run
# with PPID 1, confirmed live — nothing else respawns them if killed), which
# matches the known behavior that a full reboot is what currently "fixes" a
# wedged camera pipeline. This kills and manually relaunches both with their
# standard arguments (captured live from a running instance), using
# start-stop-daemon the same way this project's other init scripts already
# launch detached background services — no reboot needed.

import os
import subprocess
import time

CAM_APP = ["/usr/bin/cam_app", "-i", "/dev/v4l/by-id/main-video-4", "-t", "0", "-w", "1920", "-h", "1080", "-f", "15", "-c"]

# mjpg_streamer only takes -i/-o at the top level; each plugin's own parameters
# (-t for the input plugin, -w/-p for the output plugin) must be embedded as a
# single quoted string within -i/-o, not passed as separate top-level flags —
# confirmed live via mjpg_streamer's own --help after an initial version that
# flattened /proc/PID/cmdline's argv lost this quoting structure and passed
# them as separate flags, which mjpg_streamer rejected outright ("invalid
# option -- 't'"), leaving the camera pipeline half-reloaded (cam_app restarted
# fine, mjpg_streamer did not start at all).
MJPG_STREAMER = ["/usr/bin/mjpg_streamer", "-i", "input_memfd.so -t 0", "-o",
                 "output_http.so -w /usr/share/mjpg-streamer/www/ -p 8080"]

# mjpg_streamer dlopen()s its input/output plugins at runtime and doesn't
# search /usr/lib/mjpg-streamer on its own — confirmed live: without this, it
# started (start-stop-daemon reported success) but immediately exited with
# "dlopen: input_memfd.so: cannot open shared object file", leaving nothing
# running despite the apparent success. Whatever originally launched it at
# boot must set this; manually relaunching needs the same environment.
MJPG_STREAMER_ENV = dict(os.environ, LD_LIBRARY_PATH="/usr/lib/mjpg-streamer")


def stop(binary_path):
    subprocess.run(["start-stop-daemon", "-K", "-q", "-x", binary_path], check=False)


def start(cmd, env=None):
    subprocess.run(["start-stop-daemon", "-S", "-b", "-q", "--exec", cmd[0], "--", *cmd[1:]], check=True, env=env)


def main():
    stop(MJPG_STREAMER[0])
    stop(CAM_APP[0])
    time.sleep(2)  # let the camera device release before something else opens it
    start(CAM_APP)
    time.sleep(1)  # cam_app needs to be up and writing to the memfd before mjpg_streamer reads it
    start(MJPG_STREAMER, env=MJPG_STREAMER_ENV)
    print("Camera pipeline reloaded")


if __name__ == "__main__":
    main()
