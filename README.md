# MT7927 WiFi 7 Linux Driver

Standalone Linux driver for the MediaTek MT7927 (Filogic 380) WiFi 7 chip.

MT7927 is architecturally identical to the MT7925 but with 320MHz channel support. The WiFi side connects via PCIe (`14c3:7927`), while the Bluetooth side is internally MT6639 via USB.

## Status

| Component | Status |
|-----------|--------|
| CBTOP remap / CBInfra init | Done |
| DMA ring setup (MT7927 layout) | Done |
| Firmware loading via DMA | Done |
| ASPM disable | Done |
| Driver identity (`mt7927`) | Done |
| mac80211 / network interface | TODO |
| DBDC dual-band (5GHz) | TODO |
| Power management | TODO |

## Supported Hardware

| Device | BT USB ID | WiFi PCI ID |
|--------|-----------|-------------|
| ASUS ROG Crosshair X870E Hero | 0489:e13a | 14c3:7927 |
| ASUS ROG Strix X870-I | 0489:e13a | 14c3:7927 |
| ASUS ProArt X870E-Creator WiFi | 13d3:3588 | 14c3:6639 |
| ASUS X870E-E | 13d3:3588 | 14c3:7927 |
| Gigabyte X870E Aorus Master X3D | 0489:e10f | 14c3:7927 |
| Gigabyte Z790 AORUS MASTER X | 0489:e10f | 14c3:7927 |
| Lenovo Legion Pro 7 16ARX9 | 0489:e0fa | 14c3:7927 |
| TP-Link Archer TBE550E PCIe | 0489:e116 | 14c3:7927 |
| AMD RZ738 (MediaTek MT7927) | - | 14c3:0738 |

```bash
lspci -nn | grep 14c3        # WiFi (PCIe)
lsusb | grep -iE '0489|13d3' # Bluetooth (USB)
```

## Quick Start

### Prerequisites

```bash
# Kernel 6.7+ required
uname -r

# Verify device is present
lspci -nn | grep 14c3:7927

# Install build dependencies
sudo apt install build-essential linux-headers-$(uname -r) dkms
```

### Install Firmware

MT7927 requires MT6639-based firmware (NOT MT7925 firmware).

```bash
# Download firmware from ASUS driver package
cd /tmp
git clone https://github.com/jetm/mediatek-mt7927-dkms.git
cd mediatek-mt7927-dkms
./download-driver.sh .
python3 extract_firmware.py DRV_WiFi_MTK_MT7925_MT7927*.zip /tmp/mt7927_fw

# Install
sudo mkdir -p /lib/firmware/mediatek/mt7927
sudo cp /tmp/mt7927_fw/WIFI_*.bin /lib/firmware/mediatek/mt7927/
sudo update-initramfs -u
```

### Build and Load

```bash
git clone https://github.com/aabdelghani/mt7927.git
cd mt7927
make driver

# Unload conflicting drivers
sudo modprobe -r mt7925e mt7921e mt76 2>/dev/null

# Load
sudo insmod src/mt7927.ko

# Verify — should show "Kernel driver in use: mt7927"
lspci -k | grep -A 3 "14c3:7927"
sudo dmesg | tail -30
```

### Install (persistent)

```bash
sudo make install
sudo modprobe mt7927
```

## Architecture

```
MT7927 (Filogic 380 combo module)
  ├── WiFi: MT7925 silicon via PCIe (14c3:7927)
  │     └── Uses MT6639 firmware (NOT MT7925 firmware)
  └── BT:   MT6639 via USB (0489:e13a etc.)

PCIe ──► CBTOP remap ──► CBInfra bus fabric ──► WFDMA engine ──► WiFi MAC
              │
              └── Must be initialized FIRST or all DMA writes are silently dropped
```

### Key Differences from MT7925

| Feature | MT7925 | MT7927 |
|---------|--------|--------|
| RX MCU ring | 0 | 6 |
| RX data ring | 2 | 4 |
| RX aux ring | 1 | 7 |
| IRQ RX bits | BIT(0-2) | BIT(12,14,15) |
| CBInfra init | Not needed | Required |
| Firmware | mt7925/ | mt7927/ (MT6639) |
| ASPM | OK | Must disable |
| DBDC | Auto | Must enable explicitly |
| CLR_OWN | Safe | Destroys DMA config |
| 320MHz | No | Yes |

### Register Map

**BAR0** (2MB main memory):
- `0x000000` — Main memory (inactive until firmware loads)
- `0x080000` — Configuration commands (79 decoded)
- `0x180000` — Status region

**BAR2** (32KB control registers):
- `0x0020-0x0024` — Scratch registers (writable)
- `0x0200` — FW_STATUS (`0xffff10f1` = waiting for firmware)
- `0x0208` — WPDMA_GLO_CFG
- `0x0210-0x0220` — Prefetch configuration
- `0x0300+` — TX ring descriptors
- `0x0500+` — RX ring descriptors

**CBInfra** (via BAR0 indirect):
- `0x74000100` — CONNINFRA wakeup
- `0x74030000` — CBTOP remap WF/BT
- `0x74060010` — ROMCODE_INDEX (poll for `0x1D1E`)
- `0x74060800` — MCIF remap
- `0x74070010` — WF subsystem reset

## Project Structure

```
mt7927/
├── src/
│   ├── mt7927.h          # Register map, DMA layout, IRQ definitions
│   ├── mt7927_init.c     # Probe, CBTOP remap, DMA init, firmware load
│   └── Kbuild
├── tests/                 # Hardware discovery tests (23 modules)
│   ├── 01_safe_basic/     # PCI enum, BAR mapping, chip ID, scratch regs
│   ├── 02_safe_discovery/ # Config space decoding, MT7925 pattern analysis
│   ├── 03_careful_write/  # Memory activation attempts
│   ├── 04_risky_ops/      # Legacy drivers, firmware/DMA experiments
│   └── tools/             # Exploration utilities
├── docs/
│   └── TEST_RESULTS_SUMMARY.md
├── Makefile
└── README.md
```

## Roadmap

### Phase 1: Hardware Init (DONE)
- [x] PCI enumeration and BAR mapping
- [x] CBTOP remap (CBInfra bus fabric)
- [x] Chip init (WF reset, MCU ownership, ROMCODE poll)
- [x] DMA ring allocation (correct MT7927 layout)
- [x] Firmware loading via DMA ring 16
- [x] ASPM disable
- [x] Driver registers as "mt7927"

### Phase 2: WiFi Functionality
- [ ] MCU command interface
- [ ] DBDC enable (5GHz support)
- [ ] mac80211 integration
- [ ] Network interface creation
- [ ] 320MHz channel support

### Phase 3: Production Ready
- [ ] Power management
- [ ] Suspend/resume
- [ ] Error recovery (MAC reset)
- [ ] Upstream submission to linux-wireless

## Troubleshooting

**Driver won't load:**
```bash
lsmod | grep mt79
sudo modprobe -r mt7925e mt7921e mt76  # remove conflicts
sudo insmod src/mt7927.ko
```

**Chip in error state (0xffffffff):**
```bash
echo 1 | sudo tee /sys/bus/pci/devices/0000:XX:00.0/remove
sleep 2
echo 1 | sudo tee /sys/bus/pci/rescan
```

**Firmware not found:**
```bash
ls -la /lib/firmware/mediatek/mt7927/
# Should show:
#   WIFI_RAM_CODE_MT6639_2_1.bin
#   WIFI_MT6639_PATCH_MCU_2_1_hdr.bin
```

**Already have working WiFi via DKMS?**
The [jetm/mediatek-mt7927-dkms](https://github.com/jetm/mediatek-mt7927-dkms) project patches the in-kernel mt7925e driver and is the recommended solution for daily use. This standalone driver is for development.

## References

- [MT7927 WiFi: The Missing Piece](https://jetm.github.io/blog/posts/mt7927-wifi-the-missing-piece/)
- [mt76 tracking issue](https://github.com/openwrt/mt76/issues/927)
- [Kernel mt7925 source](https://github.com/torvalds/linux/tree/master/drivers/net/wireless/mediatek/mt76/mt7925)

## License

GPL-2.0-only
