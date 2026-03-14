#!/usr/bin/env bash
# Rollback from mt7927 to stock mt7925e
# Usage: sudo ./rollback.sh
set -uo pipefail
[[ $EUID -eq 0 ]] || { echo "Run as root: sudo ./rollback.sh"; exit 1; }

echo "Rolling back to mt7925e..."
rmmod mt7927 mt7925_common mt7921e mt7921_common mt792x_lib \
    mt76_connac_lib mt76 btusb btmtk 2>/dev/null || true
sleep 1
modprobe mt7925e 2>/dev/null
modprobe btusb 2>/dev/null
sleep 2

if lsmod | grep -q mt7925e; then
    echo "mt7925e restored"
    ip -o link show 2>/dev/null | grep -oP 'wlp\S+'
else
    echo "Failed — reboot to recover"
fi
