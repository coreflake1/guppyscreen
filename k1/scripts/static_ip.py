#!/usr/bin/env python3

# OpenKE — per-SSID static IP for wlan0.
#
# Design (see memory/project_static_ip_design.md for the full spec): this is
# strictly ADDITIVE to the stock DHCP flow, never a replacement of it. The
# stock /usr/bin/wifi_up.sh always runs `udhcpc -i wlan0 ...` first and gets a
# real lease — that proves association + basic connectivity are healthy.
# Only *after* that succeeds does anything here run, and only if the
# currently-associated SSID has an entry in CONFIG_PATH. If it doesn't,
# apply_boot_hook() is a no-op and stock DHCP behavior is unchanged, byte for
# byte. That "no entry -> zero new code runs" property is the core safety
# guarantee and must not be relaxed.
#
# This script does not touch wifi_up.sh itself (that wiring is a later stage
# — see the staged rollout plan in the design doc). It is runnable standalone
# over SSH for manual testing, and is also the backend the future GUI panel
# and gcode macros will shell out to.

import argparse
import ipaddress
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time

CONFIG_PATH = "/usr/data/printer_data/config/static_ip.json"
DHCP_SNAPSHOT_PATH = "/usr/data/printer_data/config/static_ip_dhcp_snapshot.json"
RESOLV_CONF = "/etc/resolv.conf"
RESOLV_CONF_BACKUP = "/etc/resolv.conf.dhcp-backup"
IFACE = "wlan0"
FRESH_LEASE_TIMEOUT_S = 10
DAEMON_WATCHDOG_WAIT_S = 5


def log(msg):
    print(msg, file=sys.stderr)


def run(cmd, dry_run, check=True):
    log(f"+ {' '.join(cmd)}")
    if dry_run:
        return subprocess.CompletedProcess(cmd, 0, stdout="", stderr="")
    return subprocess.run(cmd, check=check, capture_output=True, text=True)


# ---------------------------------------------------------------------------
# JSON files (per-SSID static IP entries + per-SSID last-known-good DHCP state)
# ---------------------------------------------------------------------------

def load_json(path):
    if not os.path.exists(path):
        return {}
    with open(path) as f:
        return json.load(f)


def save_json(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp_path = path + ".tmp"
    with open(tmp_path, "w") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")
    os.replace(tmp_path, path)  # atomic - never leaves a half-written file


def load_config():
    return load_json(CONFIG_PATH)


def save_config(config):
    save_json(CONFIG_PATH, config)


def snapshot_dhcp_state(ssid, dry_run):
    """Capture wlan0's current (real DHCP-obtained) IP/netmask/gateway/DNS for
    ssid, but only the FIRST time - called right before the first-ever
    apply_static() for this SSID, while it's still on a genuine DHCP lease.
    This is what makes revert_to_dhcp() deterministic: it restores exactly
    this snapshot instead of depending on a fresh DHCP negotiation succeeding
    (a live outage on 2026-07-11 showed that dependency is not safe - see
    memory/project_static_ip_design.md for the incident)."""
    snapshots = load_json(DHCP_SNAPSHOT_PATH)
    if ssid in snapshots:
        return  # don't clobber a real DHCP snapshot with an already-static state
    state = current_live_state()
    if not state["ip"]:
        log(f"warning: no live IP to snapshot for SSID '{ssid}' - revert won't have a fallback for it")
        return
    if not dry_run:
        snapshots[ssid] = state
        save_json(DHCP_SNAPSHOT_PATH, snapshots)
    log(f"Snapshotted DHCP state for '{ssid}': {state}")


# ---------------------------------------------------------------------------
# Validation (mirrors the GUI's Save-button gating rules from the design doc)
# ---------------------------------------------------------------------------

def validate_entry(ip, netmask, gateway):
    """Raises ValueError with a user-facing message if the entry is unsafe to apply."""
    try:
        network = ipaddress.IPv4Network(f"{ip}/{netmask}", strict=False)
        addr = ipaddress.IPv4Address(ip)
        gw = ipaddress.IPv4Address(gateway)
    except ValueError as e:
        raise ValueError(f"Invalid IPv4 address/netmask: {e}")

    if addr not in network:
        raise ValueError(f"{ip} is not within the {netmask} subnet")
    if gw not in network:
        raise ValueError(f"Gateway {gateway} is not within the {netmask} subnet")
    if addr == gw:
        raise ValueError(f"{ip} is the gateway address")
    if addr == network.network_address:
        raise ValueError(f"{ip} is the network address")
    if addr == network.broadcast_address:
        raise ValueError(f"{ip} is the broadcast address")


# ---------------------------------------------------------------------------
# Live interface state (wpa_supplicant / wlan0)
# ---------------------------------------------------------------------------

def current_ssid():
    result = subprocess.run(["wpa_cli", "-i", IFACE, "status"],
                             capture_output=True, text=True, check=False)
    for line in result.stdout.splitlines():
        if line.startswith("ssid="):
            return line.split("=", 1)[1]
    return None


def current_live_state():
    """Best-effort read of wlan0's live IP/netmask/gateway/DNS - used both by the
    boot hook (nothing to decide from this) and to prefill the GUI's DHCP-mode
    read-only fields with whatever the active lease actually is right now."""
    state = {"ip": None, "netmask": None, "gateway": None, "dns": []}

    ifcfg = subprocess.run(["ifconfig", IFACE], capture_output=True, text=True, check=False)
    m = re.search(r"inet addr:(\S+)", ifcfg.stdout) or re.search(r"inet (\S+)", ifcfg.stdout)
    if m:
        state["ip"] = m.group(1)
    m = re.search(r"Mask:(\S+)", ifcfg.stdout)
    if m:
        state["netmask"] = m.group(1)

    if shutil.which("ip"):
        route = subprocess.run(["ip", "route", "show", "default"],
                                capture_output=True, text=True, check=False)
        m = re.search(r"default via (\S+)", route.stdout)
    else:
        route = subprocess.run(["route", "-n"], capture_output=True, text=True, check=False)
        m = re.search(r"^0\.0\.0\.0\s+(\S+)", route.stdout, re.MULTILINE)
    if m:
        state["gateway"] = m.group(1)

    if os.path.exists(RESOLV_CONF):
        with open(RESOLV_CONF) as f:
            state["dns"] = re.findall(r"^nameserver\s+(\S+)", f.read(), re.MULTILINE)

    return state


def kill_udhcpc(dry_run):
    """killall by process name - confirmed live that only one udhcpc runs on
    this device (for wlan0; eth0 is unused and has no udhcpc of its own), so
    an exact-name kill is safe and simpler than a cmdline pattern match.
    pkill isn't present on this firmware's busybox; killall is (real psmisc)."""
    run(["killall", "udhcpc"], dry_run, check=False)


def configure_interface(ip, netmask, gateway, dns, dry_run):
    """The actual ifconfig/route/resolv.conf writes - shared by apply_static()
    and revert_to_dhcp()'s snapshot restore, since both are "make wlan0 look
    like this" and neither should depend on anything else succeeding first."""
    run(["ifconfig", IFACE, ip, "netmask", netmask, "up"], dry_run)

    if shutil.which("ip"):
        run(["ip", "route", "del", "default"], dry_run, check=False)
        run(["ip", "route", "add", "default", "via", gateway, "dev", IFACE], dry_run)
    else:
        run(["route", "del", "default"], dry_run, check=False)
        run(["route", "add", "default", "gw", gateway], dry_run)

    if not dry_run:
        if os.path.exists(RESOLV_CONF) and not os.path.exists(RESOLV_CONF_BACKUP):
            shutil.copy2(RESOLV_CONF, RESOLV_CONF_BACKUP)  # one-time, non-destructive
        with open(RESOLV_CONF, "w") as f:
            for server in dns:
                f.write(f"nameserver {server}\n")
    else:
        log(f"+ write {RESOLV_CONF}: " + ", ".join(f"nameserver {d}" for d in dns))


def dedupe_resolv_conf(dry_run):
    """Called after a persistent daemon confirms its own bind. Confirmed live
    2026-07-11: this device's stock udhcpc 'bound' event script APPENDS its
    own nameserver line to resolv.conf rather than truncating it first, so our
    own phase-1 configure_interface() write plus the daemon's own write left a
    duplicate (same IP, different trailing comment, so a naive exact-line dedupe
    wouldn't have caught it). Harmless functionally (redundant DNS entry), but
    worth cleaning up. Keeps the LAST occurrence of each nameserver IP (the
    daemon's own, freshest entry) and drops earlier duplicates; non-nameserver
    lines (like 'search lan') are left untouched."""
    if dry_run or not os.path.exists(RESOLV_CONF):
        return
    with open(RESOLV_CONF) as f:
        lines = f.readlines()

    def ns_ip(line):
        m = re.match(r"nameserver\s+(\S+)", line.strip())
        return m.group(1) if m else None

    last_index_for_ip = {}
    for i, line in enumerate(lines):
        ip = ns_ip(line)
        if ip:
            last_index_for_ip[ip] = i

    deduped = [line for i, line in enumerate(lines)
               if not ns_ip(line) or last_index_for_ip[ns_ip(line)] == i]

    if deduped != lines:
        with open(RESOLV_CONF, "w") as f:
            f.writelines(deduped)
        log("Deduplicated resolv.conf (kept freshest entry per nameserver IP)")


def try_fresh_lease(request_ip, dry_run):
    """Best-effort, bounded attempt at a real DHCP lease - one-shot (-n -q, no
    daemon left behind) and time-boxed by subprocess's own timeout so this can
    never hang the caller. request_ip asks for the same address as before
    (usually granted, since it's the same MAC's prior lease) but a lease for
    ANY address still counts as success - the caller decides what to do with
    the result. This never runs a persistent renewing udhcpc: the live outage
    on 2026-07-11 was caused by a detached background udhcpc left running with
    no supervision after the SSH session that started it went away, plus
    busybox udhcpc's own startup behavior of zeroing the interface immediately
    (its 'deconfig' event) before a lease is confirmed. A one-shot, bounded,
    foreground attempt avoids both: nothing is left running past this call,
    and the interface never ends the call worse off than when it's re-asserted
    by the caller afterward."""
    hostname = socket.gethostname()
    cmd = ["udhcpc", "-i", IFACE, "-n", "-q", "-x", f"hostname:{hostname}"]
    if request_ip:
        cmd += ["-r", request_ip]
    log(f"+ {' '.join(cmd)}  (bounded to {FRESH_LEASE_TIMEOUT_S}s)")
    if dry_run:
        return True
    try:
        result = subprocess.run(cmd, timeout=FRESH_LEASE_TIMEOUT_S, capture_output=True, text=True)
        return result.returncode == 0
    except subprocess.TimeoutExpired:
        log("Fresh lease attempt timed out")
        return False


def start_persistent_daemon(request_ip, dry_run):
    """Starts the same kind of persistent, renewing udhcpc that runs at every
    boot (stock wifi_up.sh) - detached so it outlives this process. Only ever
    called after try_fresh_lease() has already proven, moments earlier, that
    the DHCP server is actually reachable and responsive - request_ip asks for
    that same address, so this negotiation should bind almost immediately
    rather than a blind DISCOVER."""
    hostname = socket.gethostname()
    cmd = ["udhcpc", "-i", IFACE, "-x", f"hostname:{hostname}"]
    if request_ip:
        cmd += ["-r", request_ip]
    log(f"+ {' '.join(cmd)} &  (persistent, renewing)")
    if not dry_run:
        subprocess.Popen(cmd, start_new_session=True)


def apply_static(ip, netmask, gateway, dns, dry_run):
    kill_udhcpc(dry_run)
    configure_interface(ip, netmask, gateway, dns, dry_run)


def revert_to_dhcp(ssid, dry_run):
    """The GUI's 'DHCP' button + the boot hook's no-entry case both end up here
    conceptually - drop any static override and get back onto real DHCP.

    Three phases, each a strict safety improvement on a plain "just start
    udhcpc" approach (which is what caused the live outage on 2026-07-11 - a
    detached daemon with no supervision, stuck at 0.0.0.0 when its negotiation
    stalled):
      1. If a last-known-good snapshot exists for ssid, restore it immediately
         via configure_interface() - deterministic, no network round-trip.
      2. A bounded, one-shot, foreground lease attempt (try_fresh_lease) -
         proves the DHCP server is reachable right now without ever risking
         an unbounded hang.
      3. Only if step 2 succeeded: start a REAL persistent renewing udhcpc
         (start_persistent_daemon) requesting the same address. A short
         watchdog wait then re-checks the interface; if the daemon somehow
         didn't bind, it's killed and the snapshot is re-asserted - so the
         worst case is "back to the last-known-good values", never "stuck
         unconfigured".
    If step 2 fails and there's a snapshot, falls back to the snapshot values
    (no live daemon - matches stock behavior again after the next reboot). If
    there's no snapshot at all (ssid never had static IP applied) and step 2
    fails, there's no fallback net - same as plain stock behavior would be."""
    kill_udhcpc(dry_run)

    snapshot = load_json(DHCP_SNAPSHOT_PATH).get(ssid) if ssid else None
    if snapshot:
        configure_interface(snapshot["ip"], snapshot["netmask"], snapshot["gateway"],
                             snapshot.get("dns", []), dry_run)
        log(f"Restored last-known DHCP state for '{ssid}': {snapshot['ip']}/{snapshot['netmask']}")

    request_ip = snapshot["ip"] if snapshot else None
    got_fresh_lease = try_fresh_lease(request_ip, dry_run)

    if not got_fresh_lease:
        if snapshot:
            configure_interface(snapshot["ip"], snapshot["netmask"], snapshot["gateway"],
                                 snapshot.get("dns", []), dry_run)  # guarantee - can't safely start a daemon
            log("Fresh lease attempt failed - staying on last-known values, no live daemon (a reboot restores one)")
            return True
        log("Fresh lease attempt failed and no snapshot to fall back to")
        return False

    start_persistent_daemon(request_ip, dry_run)

    if dry_run:
        return True

    time.sleep(DAEMON_WATCHDOG_WAIT_S)
    if current_live_state()["ip"]:
        log("Persistent DHCP daemon confirmed bound")
        dedupe_resolv_conf(dry_run)
        return True

    log("Persistent daemon watchdog: no IP after wait - falling back")
    kill_udhcpc(dry_run)
    if snapshot:
        configure_interface(snapshot["ip"], snapshot["netmask"], snapshot["gateway"],
                             snapshot.get("dns", []), dry_run)
        return True
    return False


# ---------------------------------------------------------------------------
# Subcommands
# ---------------------------------------------------------------------------

def cmd_status(args):
    print(json.dumps({
        "ssid": current_ssid(),
        "live": current_live_state(),
        "configured": load_config(),
        "dhcp_snapshots": load_json(DHCP_SNAPSHOT_PATH),
    }, indent=2))


def cmd_get(args):
    entry = load_config().get(args.ssid)
    print(json.dumps(entry, indent=2) if entry else "null")


def cmd_set(args):
    validate_entry(args.ip, args.netmask, args.gateway)
    dns = [d for d in (args.dns1, args.dns2) if d]

    config = load_config()
    config[args.ssid] = {
        "ip": args.ip,
        "netmask": args.netmask,
        "gateway": args.gateway,
        "dns": dns,
    }
    save_config(config)
    log(f"Saved static IP entry for SSID '{args.ssid}'")

    if not args.no_apply and current_ssid() == args.ssid:
        snapshot_dhcp_state(args.ssid, args.dry_run)
        apply_static(args.ip, args.netmask, args.gateway, dns, args.dry_run)
        log(f"Applied live: {args.ip}/{args.netmask} via {args.gateway}")
    else:
        log("Not currently associated to this SSID - saved for next time it connects, nothing applied live")


def cmd_remove(args):
    config = load_config()
    was_active_ssid = current_ssid() == args.ssid
    if args.ssid in config:
        del config[args.ssid]
        save_config(config)
        log(f"Removed static IP entry for SSID '{args.ssid}'")
    else:
        log(f"No static IP entry existed for SSID '{args.ssid}'")

    if not args.no_apply and was_active_ssid:
        revert_to_dhcp(args.ssid, args.dry_run)
        log("Reverted to DHCP")


def cmd_apply_boot_hook(args):
    """Intended to be called from wifi_up.sh, after its own udhcpc call has
    already gotten a real lease. Silently does nothing if there's no entry for
    the currently-associated SSID - see the module docstring."""
    ssid = current_ssid()
    if ssid is None:
        log("No associated SSID - nothing to do")
        return
    entry = load_config().get(ssid)
    if entry is None:
        log(f"SSID '{ssid}' has no static IP entry - leaving stock DHCP as-is")
        return
    snapshot_dhcp_state(ssid, args.dry_run)
    apply_static(entry["ip"], entry["netmask"], entry["gateway"], entry.get("dns", []), args.dry_run)
    log(f"Applied static IP for SSID '{ssid}': {entry['ip']}/{entry['netmask']} via {entry['gateway']}")


def cmd_revert(args):
    ssid = args.ssid or current_ssid()
    revert_to_dhcp(ssid, args.dry_run)
    log("Reverted to DHCP")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true",
                         help="print the commands that would run instead of running them")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("status", help="print current SSID, live IP state, and saved config").set_defaults(func=cmd_status)

    p = sub.add_parser("get", help="print the saved static IP entry for an SSID, if any")
    p.add_argument("ssid")
    p.set_defaults(func=cmd_get)

    p = sub.add_parser("set", help="save a static IP entry for an SSID and apply it live if currently connected to it")
    p.add_argument("ssid")
    p.add_argument("ip")
    p.add_argument("netmask")
    p.add_argument("gateway")
    p.add_argument("dns1")
    p.add_argument("dns2", nargs="?", default=None)
    p.add_argument("--no-apply", action="store_true", help="write config only, skip the live apply step")
    p.set_defaults(func=cmd_set)

    p = sub.add_parser("remove", help="delete an SSID's static IP entry and revert to DHCP live if currently connected to it")
    p.add_argument("ssid")
    p.add_argument("--no-apply", action="store_true", help="edit config only, skip the live revert step")
    p.set_defaults(func=cmd_remove)

    sub.add_parser("apply-boot-hook", help="called from wifi_up.sh after its own udhcpc call").set_defaults(func=cmd_apply_boot_hook)

    p = sub.add_parser("revert", help="drop any static override and restore the last-known DHCP state for the current SSID")
    p.add_argument("--ssid", default=None, help="defaults to the currently-associated SSID")
    p.set_defaults(func=cmd_revert)

    args = parser.parse_args()
    try:
        args.func(args)
    except ValueError as e:
        log(f"error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
