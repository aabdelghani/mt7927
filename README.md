# MT7927 WiFi 7 Linux Driver Project

## 🎉 Major Breakthrough: MT7927 = MT7925 + 320MHz

We've discovered that the MT7927 is architecturally identical to the MT7925 (which has full Linux support since kernel 6.7) except for 320MHz channel width capability. This means we can adapt the existing mt7925 driver rather than writing one from scratch!

## Current Status: PATCHED - CBInfra Init Implemented

**Working**: CBTOP remap, DMA ring setup (correct MT7927 layout), firmware loading via DMA
**Registers as**: `mt7927` (visible in `lspci -k`, not mt7925e)
**Next Step**: mac80211 integration for full WiFi functionality

### What was fixed
The original driver was stuck because MT7927 has a **CBInfra bus fabric** that must be
initialized before any DMA or MCU access. Without CBTOP remap, all WFDMA register writes
are silently ignored. This knowledge comes from the
[jetm/mediatek-mt7927-dkms](https://github.com/jetm/mediatek-mt7927-dkms) 18-patch series.

Key fixes applied:
1. **CBTOP remap** — CBInfra bus fabric initialization (the main blocker)
2. **Correct firmware** — MT6639-based firmware, not MT7925
3. **DMA ring layout** — RX rings at 4/6/7 (not 0/1/2 like MT7925)
4. **Prefetch config** — MT7927-specific register values
5. **ASPM disabled** — Prevents PCIe power management throughput issues
6. **DBDC awareness** — 5GHz requires explicit dual-band enable

## Quick Start

### Prerequisites
```bash
# Check kernel version (need 6.7+ for mt7925 base)
uname -r  # Should show 6.7 or higher

# Verify MT7927 device is present
lspci -nn | grep 14c3:7927  # Should show your device
```

### Install Firmware
```bash
# MT7927 needs MT6639-based firmware (NOT MT7925 firmware!)
# Get the ASUS driver package which contains the correct firmware:
cd /tmp
git clone https://github.com/jetm/mediatek-mt7927-dkms.git
cd mediatek-mt7927-dkms
./download-driver.sh .
python3 extract_firmware.py DRV_WiFi_MTK_MT7925_MT7927*.zip /tmp/mt7927_fw

# Install firmware
sudo mkdir -p /lib/firmware/mediatek/mt7927
sudo cp /tmp/mt7927_fw/WIFI_*.bin /lib/firmware/mediatek/mt7927/
sudo update-initramfs -u
```

### Build and Load Driver
```bash
# Clone and build
git clone https://github.com/aabdelghani/mt7927.git
cd mt7927
make driver

# Unload any existing mt76 drivers first
sudo modprobe -r mt7925e mt7921e mt76 2>/dev/null

# Load the driver
sudo insmod src/mt7927.ko

# Check status — should show "Kernel driver in use: mt7927"
lspci -k | grep -A 3 "14c3:7927"
sudo dmesg | tail -30
```

## Technical Details

### Hardware Information
- **Chip**: MediaTek MT7927 WiFi 7 (802.11be)
- **PCI ID**: 14c3:7927 (vendor: MediaTek, device: MT7927)
- **Architecture**: Same as MT7925 except supports 320MHz channels
- **Current State**: FW_STATUS: 0xffff10f1 (waiting for DMA firmware transfer)

### Key Discoveries
1. **MT6639 firmware is required** - MT7925 firmware does NOT work for MT7927
2. **CBInfra bus fabric** - CBTOP remap is mandatory before any DMA access
3. **DMA rings are different** - RX at rings 4/6/7, not 0/1/2
4. **CLR_OWN destroys DMA config** - Must be handled carefully
5. **DBDC must be explicit** - Without it, only 2.4GHz works
6. **ASPM must be disabled** - Causes severe throughput issues

### What's Working
- PCI enumeration and BAR mapping
- CBTOP remap and CBInfra initialization
- DMA ring allocation with correct MT7927 layout
- Firmware loading via DMA ring 16
- Driver registers as "mt7927" (not mt7925e)
- Chip is stable and responsive

### What's Not Working Yet
- Full MCU command interface (needs mt76 infrastructure)
- mac80211 integration (WiFi network interface)
- DBDC dual-band enable command
- Power management wake path

### Root Cause (now understood)
The original blocker was the **CBInfra bus fabric**. MT7927 has a CBTOP
(Combo Bus Top) layer between PCIe and the WFDMA engine. Without the
remap sequence, all DMA register writes are silently dropped. The jetm
DKMS project solved this with an 18-patch series against the in-kernel
mt7925e driver.

## Project Structure
```
mt7927/
├── README.md                        # This file
├── Makefile                         # Build system (make driver / make tests)
├── src/
│   ├── mt7927.h                    # Hardware definitions, register map, DMA layout
│   ├── mt7927_init.c               # CBTOP remap, chip init, DMA, firmware load
│   └── Kbuild                      # Kernel build config
├── tests/                           # Original test infrastructure (23 tests)
│   ├── 01_safe_basic/              # PCI enumeration, BAR mapping, chip ID
│   ├── 02_safe_discovery/          # Config space analysis
│   ├── 03_careful_write/           # Memory activation attempts
│   ├── 04_risky_ops/               # Legacy drivers (before CBInfra fix)
│   └── tools/                      # Exploration tools
└── docs/
    └── TEST_RESULTS_SUMMARY.md     # Test results from discovery phase
```

## Development Roadmap

### Phase 1: Get It Working (CURRENT)
- [x] Bind driver to device
- [x] Load firmware files
- [ ] **Implement DMA transfer** ← Current focus
- [ ] Activate chip memory
- [ ] Create network interface

### Phase 2: Make It Good
- [ ] Port full mt7925 functionality
- [ ] Add 320MHz channel support
- [ ] Integrate with mac80211
- [ ] Implement WiFi 7 features

### Phase 3: Make It Official
- [ ] Clean up code for upstream
- [ ] Submit to linux-wireless
- [ ] Get merged into mainline kernel

## How to Contribute

### Immediate Needs
1. **DMA Implementation** - Study mt7925 source in `drivers/net/wireless/mediatek/mt76/mt7925/`
2. **Testing** - Try the driver on your MT7927 hardware
3. **Documentation** - Improve this README with your findings

### Code References

#### Key Source Files to Study (in your kernel source)
```bash
# Main mt7925 driver files (your reference implementation)
drivers/net/wireless/mediatek/mt76/mt7925/
├── pci.c         # PCI probe and initialization sequence
├── mcu.c         # MCU communication and firmware loading
├── init.c        # Hardware initialization
└── dma.c         # DMA setup and transfer

# Shared mt76 infrastructure
drivers/net/wireless/mediatek/mt76/
├── dma.c         # Generic DMA implementation
├── mt76_connac_mcu.c  # MCU interface for Connac chips
└── util.c        # Utility functions
```

#### Online References
- **mt7925 on GitHub**: [Linux kernel source](https://github.com/torvalds/linux/tree/master/drivers/net/wireless/mediatek/mt76/mt7925)
- **mt76 framework**: [OpenWrt repository](https://github.com/openwrt/mt76)
- **Our working code**: `tests/04_risky_ops/mt7927_init.c`

## Troubleshooting

### Driver Won't Load
```bash
# Check for conflicts
lsmod | grep mt79
sudo rmmod mt7921e mt7925e  # Remove any conflicting drivers

# Check kernel messages
sudo dmesg | grep -E "mt7927|0a:00"
```

### Chip in Error State
```bash
# If chip shows 0xffffffff, reset via PCI
echo 1 | sudo tee /sys/bus/pci/devices/0000:0a:00.0/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan
```

### Firmware Not Found
```bash
# Verify firmware is installed
ls -la /lib/firmware/mediatek/mt7925/
# Should show WIFI_MT7925_PATCH_MCU_1_1_hdr.bin and WIFI_RAM_CODE_MT7925_1_1.bin
```

## Test Results Summary

### Successful Tests ✅
- **Hardware Detection**: PCI enumeration works perfectly
- **Driver Binding**: Custom driver claims device successfully  
- **Firmware Compatibility**: MT7925 firmware loads without errors
- **Register Access**: All BAR2 control registers accessible
- **Chip Stability**: No crashes or lockups during testing

### Pending Implementation 🚧
- **DMA Transfer**: Firmware not reaching chip memory
- **Memory Activation**: Main memory at 0x000000 still shows 0x00000000
- **Network Interface**: Requires successful initialization first

## FAQ

**Q: Why not just use the mt7925e driver?**
A: The mt7925e driver refuses to bind to MT7927's PCI ID (14c3:7927). The [jetm/mediatek-mt7927-dkms](https://github.com/jetm/mediatek-mt7927-dkms) project patches mt7925e via DKMS and is the recommended working solution. This standalone driver is for development and learning.

**Q: Why does this driver show as "mt7927" instead of "mt7925e"?**
A: This is a standalone driver that registers with PCI as "mt7927". When loaded, `lspci -k` shows `Kernel driver in use: mt7927`.

**Q: What firmware do I need?**
A: MT6639-based firmware from the ASUS driver package (NOT MT7925 firmware). The files are `WIFI_RAM_CODE_MT6639_2_1.bin` and `WIFI_MT6639_PATCH_MCU_2_1_hdr.bin`, installed to `/lib/firmware/mediatek/mt7927/`.

**Q: Is this safe to test?**
A: Yes. The worst case is the driver doesn't fully initialize. The chip remains stable.

**Q: What was the main blocker?**
A: The CBInfra bus fabric. MT7927 has a CBTOP layer that must be configured before DMA works. Without it, all DMA register writes are silently ignored.

**Q: Will this support full WiFi 7 320MHz channels?**
A: The register definitions include 320MHz support. Full implementation requires mac80211 integration.

## License
GPL v2 - Intended for upstream Linux kernel submission

## Contact & Support
- **GitHub Issues**: Report bugs and discuss development
- **Linux Wireless**: [Mailing list](http://vger.kernel.org/vger-lists.html#linux-wireless) for upstream discussion

---

**Status as of 2025-08-18**: Driver successfully binds and loads firmware. Implementing DMA transfer to complete initialization. This is no longer a reverse engineering project - we're adapting proven MT7925 code to support MT7927's PCI ID and eventually its 320MHz capability.
