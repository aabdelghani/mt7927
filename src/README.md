# MT7927 Driver Source

Standalone MT7927 WiFi 7 Linux driver with proper CBInfra initialization.

## Key Files
```
mt7927.h        - Hardware register definitions, DMA ring layout, IRQ map
mt7927_init.c   - PCI probe, CBTOP remap, chip init, DMA setup, firmware load
Kbuild          - Kernel build configuration
```

## What this driver fixes vs the original tests/04_risky_ops/ drivers

1. **Correct firmware** — Uses MT6639-based firmware (`mt7927/`), not MT7925 firmware
2. **CBTOP remap** — Configures the CBInfra bus fabric before DMA access (this was the blocker)
3. **Correct DMA ring layout** — RX rings at 4/6/7 instead of 0/1/2
4. **MT7927 prefetch config** — Hardware-specific prefetch register values
5. **ASPM disabled** — Prevents throughput degradation from PCIe power management
6. **Registers as "mt7927"** — Shows as MT7927 in `lspci -k`, not mt7925e

## Build
```bash
make driver      # from project root
# or
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

## Status
- CBInfra init sequence implemented (patches 01-07 from jetm/mediatek-mt7927-dkms)
- DMA ring allocation with correct MT7927 layout
- Firmware loading via DMA ring 16
- TODO: mac80211 integration, DBDC enable, full MCU command interface
