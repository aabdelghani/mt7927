#!/usr/bin/env bash
# Try the MT7927 driver temporarily (offline-safe, auto-rollback)
#
# Builds and loads the full mt76 stack renamed as mt7927.
# Auto-rollback on error, timeout, or Ctrl+C.
#
# Usage: sudo ./try.sh
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KVER="$(uname -r)"
TIMEOUT=60

info()  { echo -e "\e[1;32m==>\e[0m \e[1m$*\e[0m"; }
warn()  { echo -e "\e[1;33m==>\e[0m $*"; }
error() { echo -e "\e[1;31m==>\e[0m $*" >&2; }

rollback() {
    echo ""
    info "Rolling back to mt7925e..."
    rmmod mt7927 mt7925_common mt7921e mt7921_common mt792x_lib \
        mt76_connac_lib mt76 btusb btmtk 2>/dev/null || true
    sleep 1
    modprobe mt7925e 2>/dev/null
    modprobe btusb 2>/dev/null
    sleep 2
    if lsmod | grep -q mt7925e; then
        info "mt7925e restored — WiFi should be back"
    else
        error "Failed to restore — reboot to recover"
    fi
}

cleanup() {
    if lsmod | grep -q "^mt7927 " && ! lsmod | grep -q mt7925e; then
        warn "Script interrupted — auto-rolling back"
        rollback
    fi
}
trap cleanup EXIT

[[ $EUID -eq 0 ]] || { error "Run as root: sudo ./try.sh"; exit 1; }

echo "============================================"
echo "  MT7927 Driver Test (full mt76 stack)"
echo "  WiFi WILL drop during this test"
echo "  Auto-rollback on error or Ctrl+C"
echo "============================================"
echo ""

# ── Build ───────────────────────────────────────────────────────────
info "Building..."
make -C "${SCRIPT_DIR}" driver 2>&1
[[ -f "${SCRIPT_DIR}/src/mt76/mt7925/mt7927.ko" ]] || { error "Build failed"; exit 1; }

# Verify kernel match
mod_ver="$(modinfo -F vermagic "${SCRIPT_DIR}/src/mt76/mt7925/mt7927.ko" 2>/dev/null)"
[[ "$mod_ver" == *"${KVER}"* ]] || { error "Kernel mismatch: ${mod_ver} vs ${KVER}"; exit 1; }

# Check firmware
[[ -f /lib/firmware/mediatek/mt7927/WIFI_RAM_CODE_MT6639_2_1.bin ]] || \
    { error "Firmware not installed at /lib/firmware/mediatek/mt7927/"; exit 1; }

# Check PCI device
lspci -nn | grep -q "14c3:7927" || { error "MT7927 PCI device not found"; exit 1; }

info "Ready"
echo "  PCI: $(lspci -nn | grep 14c3:7927)"
echo ""

read -r -t 30 -p "WiFi will drop. Continue? [y/N] " answer || answer="n"
echo ""
[[ "$answer" =~ ^[Yy]$ ]] || { info "Aborted."; trap - EXIT; exit 0; }

dmesg_before=$(dmesg | wc -l)

# ── Unload old stack ────────────────────────────────────────────────
info "Unloading mt7925e (WiFi drops NOW)..."
modprobe -r mt7925e mt7925_common mt7921e mt7921_common \
    mt792x_lib mt76_connac_lib mt76 btusb btmtk 2>/dev/null || true
sleep 1

if lsmod | grep -q mt7925e; then
    error "Could not unload mt7925e"
    exit 1
fi

# ── Load new stack ──────────────────────────────────────────────────
info "Loading mt7927 stack..."

insmod "${SCRIPT_DIR}/src/mt76/mt76.ko" || { error "mt76.ko failed"; exit 1; }
insmod "${SCRIPT_DIR}/src/mt76/mt76-connac-lib.ko" || { error "mt76-connac-lib.ko failed"; exit 1; }
insmod "${SCRIPT_DIR}/src/mt76/mt792x-lib.ko" || { error "mt792x-lib.ko failed"; exit 1; }
insmod "${SCRIPT_DIR}/src/mt76/mt7921/mt7921-common.ko" || true
insmod "${SCRIPT_DIR}/src/mt76/mt7921/mt7921e.ko" || true
insmod "${SCRIPT_DIR}/src/mt76/mt7925/mt7925-common.ko" || { error "mt7925-common.ko failed"; exit 1; }
insmod "${SCRIPT_DIR}/src/mt76/mt7925/mt7927.ko" || { error "mt7927.ko failed"; exit 1; }
insmod "${SCRIPT_DIR}/src/bluetooth/btmtk.ko" 2>/dev/null || true
insmod "${SCRIPT_DIR}/src/bluetooth/btusb.ko" 2>/dev/null || true

sleep 3
info "mt7927 loaded"

# ── Results ─────────────────────────────────────────────────────────
echo ""
info "=== Kernel log ==="
dmesg | tail -n +"${dmesg_before}" | grep -iE "mt7927|mt7925|mt76|14c3|mediatek|firmware" | head -30
echo ""

info "=== PCI driver ==="
lspci -ks "$(lspci -nn | grep 14c3:7927 | awk '{print $1}')" 2>/dev/null
echo ""

info "=== WiFi interface ==="
ip link show 2>/dev/null | grep -E "wl" || echo "  (none yet — may take a few seconds)"
echo ""

info "=== Modules ==="
lsmod | grep -E "mt7927|mt7925|mt792x|mt76" || echo "  (none)"
echo ""

# ── Decide ──────────────────────────────────────────────────────────
echo "============================================"
echo "  [k] Keep mt7927 (run ./rollback.sh later)"
echo "  [r] Rollback to mt7925e now"
echo "  ${TIMEOUT}s timeout → auto rollback"
echo "============================================"

read -r -t "${TIMEOUT}" -p "[k/R] " choice || choice="r"
echo ""

if [[ "$choice" =~ ^[Kk]$ ]]; then
    info "Keeping mt7927"
    trap - EXIT
else
    rollback
    trap - EXIT
fi
