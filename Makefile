# MT7927 WiFi 7 Linux Driver
# Uses the full mt76 stack with MT7927 patches applied

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
KVER := $(shell uname -r)

# Include path for airoha stub (kernel < 6.19)
COMPAT_DIR := $(PWD)/src/compat
NEED_STUB := $(shell test ! -f $(KDIR)/include/linux/soc/airoha/airoha_offload.h && echo yes)

# Default target
all: driver

# Driver modules (mt76 stack + bluetooth)
driver: _install_stub
	$(MAKE) -C $(KDIR) M=$(PWD)/src/bluetooth modules
	$(MAKE) -C $(KDIR) M=$(PWD)/src/mt76 modules

# Install airoha stub if kernel < 6.19
_install_stub:
ifeq ($(NEED_STUB),yes)
	@echo "Installing airoha_offload.h stub (kernel < 6.19)..."
	@sudo mkdir -p $(KDIR)/include/linux/soc/airoha
	@sudo cp $(COMPAT_DIR)/airoha_offload.h $(KDIR)/include/linux/soc/airoha/
endif

# Install firmware + modules
install: driver
	@echo "Installing firmware..."
	sudo mkdir -p /lib/firmware/mediatek/mt7927 /lib/firmware/mediatek/mt6639
	sudo cp firmware/mt7927/*.bin /lib/firmware/mediatek/mt7927/
	sudo cp firmware/mt6639/*.bin /lib/firmware/mediatek/mt6639/
	@echo "Installing modules..."
	sudo mkdir -p /lib/modules/$(KVER)/updates/mt7927
	sudo cp src/bluetooth/btusb.ko src/bluetooth/btmtk.ko /lib/modules/$(KVER)/updates/mt7927/
	sudo cp src/mt76/mt76.ko /lib/modules/$(KVER)/updates/mt7927/
	sudo cp src/mt76/mt76-connac-lib.ko /lib/modules/$(KVER)/updates/mt7927/
	sudo cp src/mt76/mt792x-lib.ko /lib/modules/$(KVER)/updates/mt7927/
	sudo cp src/mt76/mt7921/mt7921-common.ko /lib/modules/$(KVER)/updates/mt7927/
	sudo cp src/mt76/mt7921/mt7921e.ko /lib/modules/$(KVER)/updates/mt7927/
	sudo cp src/mt76/mt7925/mt7925-common.ko /lib/modules/$(KVER)/updates/mt7927/
	sudo cp src/mt76/mt7925/mt7927.ko /lib/modules/$(KVER)/updates/mt7927/
	@echo "Configuring module loading..."
	sudo sh -c 'echo "blacklist mt7925e" > /etc/modprobe.d/mt7927.conf'
	sudo depmod -a
	@echo "Done. Load with: sudo modprobe mt7927"

# Uninstall
uninstall:
	sudo rm -rf /lib/modules/$(KVER)/updates/mt7927
	sudo rm -f /etc/modprobe.d/mt7927.conf
	sudo depmod -a
	@echo "Uninstalled. Reboot to restore stock drivers."

# Clean build artifacts
clean:
	$(MAKE) -C $(KDIR) M=$(PWD)/src/mt76 clean 2>/dev/null || true
	$(MAKE) -C $(KDIR) M=$(PWD)/src/bluetooth clean 2>/dev/null || true

# Test modules (legacy)
tests:
	$(MAKE) -C $(KDIR) M=$(PWD)/tests modules

# Check chip state
check:
	@lspci -nn | grep -i "14c3:7927" || echo "MT7927 not found"
	@lspci -ks $$(lspci -nn | grep "14c3:7927" | awk '{print $$1}') 2>/dev/null || true

help:
	@echo "MT7927 WiFi 7 Linux Driver"
	@echo ""
	@echo "  make            - Build driver modules"
	@echo "  make install    - Install modules + firmware + blacklist mt7925e"
	@echo "  make uninstall  - Remove everything and restore stock drivers"
	@echo "  make clean      - Clean build artifacts"
	@echo "  make check      - Check chip and driver status"
	@echo "  make tests      - Build legacy test modules"
	@echo ""
	@echo "After install, load with:"
	@echo "  sudo modprobe -r mt7925e mt76 2>/dev/null; sudo modprobe mt7927"

.PHONY: all driver _install_stub install uninstall clean tests check help
