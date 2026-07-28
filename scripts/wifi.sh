#!/bin/bash
#=============================================================================
# scripts/wifi.sh — ADB Wi-Fi management for Gemini-S1 (openvela/NuttX)
#=============================================================================

set -euo pipefail

IFACE="wlan0"
CONF_REMOTE="/data/etc/wifi/wapi.conf"
CONF_LOCAL="$(mktemp /tmp/wapi.XXXXXX.conf)"
SCAN_TIMEOUT_SECONDS=15

cleanup() { rm -f "$CONF_LOCAL"; }
trap cleanup EXIT

die() { echo "[ERROR] $*" >&2; exit 1; }
info() { echo "[INFO] $*"; }

check_adb() {
    command -v adb >/dev/null 2>&1 || die "adb not found in PATH."
    adb get-state 2>/dev/null | grep -qx device || \
        die "No ADB device found. Connect Gemini-S1 via USB."
}

run_wapi() {
    timeout "$SCAN_TIMEOUT_SECONDS" adb shell "$*" ||
        die "Wi-Fi command timed out or failed: $*. Check RTL8733BS driver state."
}

start_sta() {
    info "Starting RTL8733BS in STA mode..."
    run_wapi "ifup $IFACE; wapi mode $IFACE 2"
}

scan_output() {
    start_sta
    info "Scanning Wi-Fi networks on $IFACE..."
    timeout "$SCAN_TIMEOUT_SECONDS" adb shell "wapi scan $IFACE" ||
        die "Wi-Fi scan did not finish. The Realtek STA stack may be unavailable."
}

valid_ipv4() {
    local address="$1"
    [[ -n "$address" && "$address" != "0.0.0.0" && \
       "$address" != "255.255.255.255" ]]
}

current_ip() {
    adb shell "ifconfig $IFACE" 2>/dev/null |
        sed -n 's/.*inet addr:\([0-9.]*\).*/\1/p' | head -n1
}

write_config() {
    local ssid="$1"
    local psk="$2"
    local bssid="$3"

    [[ ${#ssid} -gt 0 && ${#ssid} -le 32 ]] ||
        die "SSID must be 1–32 bytes."
    [[ ${#psk} -ge 8 && ${#psk} -le 63 ]] ||
        die "WPA2 password must be 8–63 characters."

    python3 - "$ssid" "$psk" "$bssid" > "$CONF_LOCAL" <<'PY'
import json
import sys
ssid, psk, bssid = sys.argv[1:]
config = {
    "wlan0": {
        "mode": 2,
        "auth": 4,
        "cmode": 8,
        "alg": 3,
        "ssid": ssid,
        "psk": psk,
    }
}
if bssid:
    config["wlan0"]["bssid"] = bssid
json.dump(config, sys.stdout, ensure_ascii=False, indent=2)
sys.stdout.write("\n")
PY
}

wait_for_association() {
    local expected_bssid="$1"
    local attempt ap

    for attempt in $(seq 1 15); do
        ap=$(adb shell "wapi show $IFACE" 2>/dev/null |
             sed -n 's/.*AP:[[:space:]]*\([^[:space:]]*\).*/\1/p' | head -n1)
        if [[ -n "$ap" && "$ap" != "00:00:00:00:00:00" ]] &&
           [[ -z "$expected_bssid" || "$ap" == "$expected_bssid" ]]; then
            printf '%s\n' "$ap"
            return 0
        fi
        sleep 1
    done
    return 1
}

wait_for_ipv4() {
    local attempt address

    for attempt in $(seq 1 3); do
        info "Requesting IP via DHCP (attempt $attempt/3)..."
        adb shell "renew $IFACE" || true
        sleep 2
        address=$(current_ip)
        if valid_ipv4 "$address"; then
            printf '%s\n' "$address"
            return 0
        fi
    done
    return 1
}

configure_dns_and_time() {
    info "Configuring fallback DNS and starting NTP..."
    adb shell "echo 'nameserver 223.5.5.5' > /tmp/resolv.conf; \
               echo 'nameserver 119.29.29.29' >> /tmp/resolv.conf; \
               ntpcstart" || true
}

# ---- scan ------------------------------------------------------------------
do_scan() {
    check_adb
    scan_output
}

# ---- connect ---------------------------------------------------------------
do_connect() {
    local ssid="${1:-}"
    local psk="${2:-}"
    local scan bssid ap address

    [[ -n "$ssid" && -n "$psk" ]] ||
        die "Usage: $0 connect <SSID> <PASSWORD>"
    check_adb

    scan=$(scan_output)
    bssid=$(printf '%s\n' "$scan" |
        awk -v target="$ssid" '
            $1 ~ /^([0-9A-Fa-f][0-9A-Fa-f]:){5}[0-9A-Fa-f][0-9A-Fa-f]$/ {
                name = $NF
                if (name == target) { print $1; exit }
            }')
    [[ -n "$bssid" ]] || die "SSID '$ssid' not found in scan results."
    info "Using BSSID: $bssid"

    write_config "$ssid" "$psk" "$bssid"
    info "Saving Wi-Fi configuration to device..."
    adb shell "mkdir -p /data/etc/wifi" || die "/data is not mounted. Flash the corrected image first."
    adb push "$CONF_LOCAL" "$CONF_REMOTE" >/dev/null || die "adb push failed"

    info "Connecting..."
    run_wapi "wapi disconnect $IFACE; wapi reconnect $IFACE"
    ap=$(wait_for_association "$bssid") ||
        die "Association failed. Check SSID, password, BSSID, and AP security."
    info "Associated with AP: $ap"

    address=$(wait_for_ipv4) || die "DHCP failed; no usable IPv4 address."
    info "Received IP: $address"
    configure_dns_and_time
    do_status
}

# ---- disconnect ------------------------------------------------------------
do_disconnect() {
    check_adb
    info "Disconnecting $IFACE..."
    adb shell "wapi disconnect $IFACE; ifdown $IFACE"
    info "Disconnected"
}

# ---- status ----------------------------------------------------------------
do_status() {
    check_adb
    echo ""
    echo "========================== Wi-Fi Status =========================="
    adb shell "wapi show $IFACE" || true
    adb shell "ifconfig $IFACE" || true
    adb shell "cat /tmp/resolv.conf" || true
    echo "==================================================================="
}

case "${1:-}" in
    scan)       do_scan ;;
    connect)    do_connect "${2:-}" "${3:-}" ;;
    disconnect) do_disconnect ;;
    status)     do_status ;;
    *)
        echo "Usage: $0 {scan|connect <SSID> <PSK>|disconnect|status}"
        exit 1
        ;;
esac
