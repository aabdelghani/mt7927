#!/usr/bin/env bash
# Install MT7927 driver (full mt76 stack, renamed to mt7927)
# Usage: sudo ./install.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KVER="$(uname -r)"

info() { echo -e "\e[1;32m==>\e[0m \e[1m$*\e[0m"; }
error() { echo -e "\e[1;31m==>\e[0m $*" >&2; exit 1; }

[[ $EUID -eq 0 ]] || error "Run as root: sudo ./install.sh"
[[ -d "/lib/modules/${KVER}/build" ]] || error "Missing kernel headers: apt install linux-headers-${KVER}"

info "Building mt7927 driver..."
make -C "${SCRIPT_DIR}" clean 2>/dev/null || true
make -C "${SCRIPT_DIR}" driver 2>&1

# Verify the renamed module exists
[[ -f "${SCRIPT_DIR}/src/mt76/mt7925/mt7927.ko" ]] || error "Build failed — mt7927.ko not found"

info "Installing..."
make -C "${SCRIPT_DIR}" install 2>&1

info "Loading mt7927..."
modprobe -r mt7925e mt7925_common mt7921e mt7921_common \
    mt792x_lib mt76_connac_lib mt76 btusb btmtk 2>/dev/null || true
sleep 1

# Load the new stack
insmod "/lib/modules/${KVER}/updates/mt7927/mt76.ko" 2>/dev/null || modprobe mt76
insmod "/lib/modules/${KVER}/updates/mt7927/mt76-connac-lib.ko" 2>/dev/null || true
insmod "/lib/modules/${KVER}/updates/mt7927/mt792x-lib.ko" 2>/dev/null || true
insmod "/lib/modules/${KVER}/updates/mt7927/mt7925-common.ko" 2>/dev/null || true
insmod "/lib/modules/${KVER}/updates/mt7927/mt7927.ko" 2>/dev/null || true
insmod "/lib/modules/${KVER}/updates/mt7927/btusb.ko" 2>/dev/null || modprobe btusb
sleep 2

if lsmod | grep -q mt7927; then
    info "mt7927 loaded!"
    lspci -ks "$(lspci -nn | grep 14c3:7927 | awk '{print $1}')" 2>/dev/null
    echo ""
    iface="$(ip -o link show 2>/dev/null | grep -oP 'wlp\S+' | head -1)"
    [[ -n "$iface" ]] && info "WiFi interface: ${iface}"
else
    info "Module installed but not loaded — reboot to activate"
fi

info "Done. Uninstall with: sudo ./uninstall.sh"
