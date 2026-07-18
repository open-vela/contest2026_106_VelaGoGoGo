#!/bin/bash
#=============================================================================
# scripts/wifi.sh — ADB WiFi management for Gemini-S1 (openvela/NuttX)
#
# Usage:
#   ./scripts/wifi.sh scan                 List available WiFi networks
#   ./scripts/wifi.sh connect <SSID> <PSK>  Connect to a WPA/WPA2 network
#   ./scripts/wifi.sh disconnect            Disconnect from WiFi
#   ./scripts/wifi.sh status                Show current connection status
#
# Prerequisites:
#   - Device connected via USB and `adb devices` shows it
#   - /data/etc/wifi/ must exist on device
#
# NuttX quirks (vs Linux):
#   - ifup/ifdown instead of ifconfig <iface> up/down
#   - renew <iface> instead of dhcpc <iface>
#   - wapi save_config does NOT persist to file — must adb push config
#=============================================================================

set -euo pipefail

IFACE="wlan0"
CONF_REMOTE="/data/etc/wifi/wapi.conf"
CONF_LOCAL="/tmp/wapi_$$.conf"

# ---- helpers ---------------------------------------------------------------
die() { echo "[ERROR] $*" >&2; exit 1; }
info() { echo "[INFO] $*"; }
check_adb() {
    if ! command -v adb &>/dev/null; then
        die "adb not found in PATH. Install Android platform-tools first."
    fi
    local dev_count
    dev_count=$(adb devices 2>/dev/null | grep -v "List of devices" | grep -c "device$" || true)
    if [ "$dev_count" -eq 0 ]; then
        die "No ADB device found. Connect Gemini-S1 via USB and check 'adb devices'."
    fi
}

# ---- scan ------------------------------------------------------------------
do_scan() {
    check_adb
    info "Scanning WiFi networks on $IFACE..."
    adb shell "ifup $IFACE ; wapi scan $IFACE"
}

# ---- connect ---------------------------------------------------------------
#  Steps verified on device:
#   1. Scan to find BSSID for the target SSID
#   2. Write wapi.conf with {mode, auth, cmode, alg, ssid, bssid, psk}
#   3. adb push config → /data/etc/wifi/wapi.conf
#   4. wapi disconnect + wapi reconnect
#   5. Wait for association, then renew (DHCP)
do_connect() {
    local ssid="$1"
    local psk="$2"

    if [ -z "$ssid" ] || [ -z "$psk" ]; then
        die "Usage: $0 connect <SSID> <PASSWORD>"
    fi

    check_adb

    # --- find BSSID for this SSID via scan ---
    info "Scanning for '$ssid'..."
    local bssid
    bssid=$(adb shell "ifup $IFACE ; wapi scan $IFACE" 2>/dev/null \
        | awk -v ssid="$ssid" '$NF == ssid { print $1; exit }')
    if [ -z "$bssid" ]; then
        die "SSID '$ssid' not found in scan results. Check the name and try again."
    fi
    info "Found BSSID: $bssid"

    # --- write config locally ---
    cat > "$CONF_LOCAL" <<EOF
{
    "wlan0": {
        "mode": 2,
        "auth": 4,
        "cmode": 8,
        "alg": 3,
        "ssid": "$ssid",
        "bssid": "$bssid",
        "psk": "$psk"
    }
}
EOF

    # --- push and reconnect ---
    info "Pushing config to device..."
    adb push "$CONF_LOCAL" "$CONF_REMOTE" >/dev/null || die "adb push failed"
    rm -f "$CONF_LOCAL"

    info "Reconnecting..."
    adb shell "wapi disconnect $IFACE ; wapi reconnect $IFACE"

    # --- wait for association ---
    info "Waiting for association..."
    sleep 5

    # --- verify AP association ---
    local ap
    ap=$(adb shell "wapi show $IFACE" 2>/dev/null | awk '/AP:/ { print $NF }')
    if [ "$ap" = "00:00:00:00:00:00" ] || [ -z "$ap" ]; then
        die "Association failed — AP is $ap. Check SSID / password."
    fi
    info "Associated with AP: $ap"

    # --- DHCP ---
    info "Requesting IP via DHCP..."
    adb shell "renew $IFACE"
    sleep 1

    do_status
}

# ---- disconnect ------------------------------------------------------------
do_disconnect() {
    check_adb
    info "Disconnecting $IFACE..."
    adb shell "wapi disconnect $IFACE ; ifdown $IFACE"
    info "Disconnected"
}

# ---- status ----------------------------------------------------------------
do_status() {
    check_adb
    echo ""
    echo "========================== WiFi Status ==========================="
    adb shell "wapi show $IFACE"
    echo ""
    adb shell "ifconfig $IFACE" || true
    echo "=================================================================="
}

# ---- main ------------------------------------------------------------------
case "${1:-}" in
    scan)
        do_scan
        ;;
    connect)
        do_connect "${2:-}" "${3:-}"
        ;;
    disconnect)
        do_disconnect
        ;;
    status)
        do_status
        ;;
    *)
        echo "Usage: $0 {scan|connect <SSID> <PSK>|disconnect|status}"
        echo ""
        echo "  scan                 List available WiFi networks"
        echo "  connect <SSID> <PSK>  Connect to a WPA/WPA2 network"
        echo "  disconnect            Disconnect from WiFi"
        echo "  status                Show current connection status"
        exit 1
        ;;
esac
