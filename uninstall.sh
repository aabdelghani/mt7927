#!/usr/bin/env bash
# Uninstall mt7927 driver and restore stock mt7925e
# Usage: sudo ./uninstall.sh
set -euo pipefail

KVER="$(uname -r)"

info() { echo -e "\e[1;32m==>\e[0m \e[1m$*\e[0m"; }

[[ $EUID -eq 0 ]] || { echo "Run as root: sudo ./uninstall.sh"; exit 1; }

info "Unloading mt7927..."
rmmod mt7927 mt7925_common mt7921e mt7921_common mt792x_lib \
    mt76_connac_lib mt76 btusb btmtk 2>/dev/null || true

info "Removing modules and config..."
rm -rf "/lib/modules/${KVER}/updates/mt7927"
rm -f /etc/modprobe.d/mt7927.conf
depmod -a

info "Loading stock drivers..."
modprobe mt7925e 2>/dev/null || true
modprobe btusb 2>/dev/null || true
sleep 2

if lsmod | grep -q mt7925e; then
    info "Stock mt7925e restored"
else
    info "Stock drivers will load on next reboot"
fi

info "Uninstall complete"
