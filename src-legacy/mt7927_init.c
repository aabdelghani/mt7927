// SPDX-License-Identifier: GPL-2.0
/*
 * MT7927 WiFi 7 Linux Driver - CBInfra Initialization
 *
 * Implements the MT7927-specific CBTOP remap and chip initialization
 * sequence. This is the critical missing piece that was blocking the
 * original driver — MT7927 has a CBInfra bus fabric that must be
 * configured before any DMA or MCU access.
 *
 * Based on patches 01-07 from jetm/mediatek-mt7927-dkms.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include "mt7927.h"

/* ── Register helpers ────────────────────────────────────────────── */

static u32 mt7927_reg_read(struct mt7927_dev *dev, u32 offset)
{
    return ioread32(dev->bar2 + offset);
}

static void mt7927_reg_write(struct mt7927_dev *dev, u32 offset, u32 val)
{
    iowrite32(val, dev->bar2 + offset);
}

static void mt7927_reg_set(struct mt7927_dev *dev, u32 offset, u32 bits)
{
    u32 val = mt7927_reg_read(dev, offset);
    mt7927_reg_write(dev, offset, val | bits);
}

/*
 * Access CBInfra registers via BAR0 indirect mapping.
 * CBInfra addresses (0x74xxxxxx) are mapped through BAR0.
 */
static u32 mt7927_cbtop_read(struct mt7927_dev *dev, u32 addr)
{
    return ioread32(dev->bar0 + (addr & 0x00ffffff));
}

static void mt7927_cbtop_write(struct mt7927_dev *dev, u32 addr, u32 val)
{
    iowrite32(val, dev->bar0 + (addr & 0x00ffffff));
    /* Read-back to ensure write is pushed through PCIe */
    ioread32(dev->bar0 + (addr & 0x00ffffff));
}

/* ── CBTOP Remap (patch-03) ──────────────────────────────────────── */
/*
 * Configure PCIe address mapping for WiFi and Bluetooth subsystems.
 * This is required before any WFDMA register access on MT7927.
 *
 * Without this, all DMA register writes are silently ignored,
 * which is why the original driver's DMA init never worked.
 */
static int mt7927_cbtop_remap(struct mt7927_dev *dev)
{
    dev_info(&dev->pdev->dev, "Configuring CBTOP remap...\n");

    /* Enable CONNINFRA wakeup (required before CBInfra register access) */
    mt7927_cbtop_write(dev, MT_CBTOP_WAKEPU_TOP, 0x1);
    wmb();
    usleep_range(1000, 2000);

    /* Set CBTOP PCIe address remap for WF (WiFi) subsystem */
    mt7927_cbtop_write(dev, MT_CBTOP_REMAP_WF_L,
                       MT_CBTOP_REMAP_WF_VAL & 0xffff);
    mt7927_cbtop_write(dev, MT_CBTOP_REMAP_WF_H,
                       MT_CBTOP_REMAP_WF_VAL >> 16);

    /* Set CBTOP PCIe address remap for BT (Bluetooth) subsystem */
    mt7927_cbtop_write(dev, MT_CBTOP_REMAP_BT_L,
                       MT_CBTOP_REMAP_BT_VAL & 0xffff);
    mt7927_cbtop_write(dev, MT_CBTOP_REMAP_BT_H,
                       MT_CBTOP_REMAP_BT_VAL >> 16);

    wmb();
    dev_info(&dev->pdev->dev, "CBTOP remap complete\n");
    return 0;
}

/* ── Chip Init (patch-03) ────────────────────────────────────────── */
/*
 * Perform CBInfra initialization sequence:
 * 1. EMI sleep protect enable
 * 2. WF subsystem reset via CBInfra RGU
 * 3. MCU ownership acquisition
 * 4. Poll ROMCODE_INDEX for MCU idle (0x1D1E)
 * 5. MCIF remap for host DMA
 * 6. Disable PCIe sleep
 * 7. Clear CONNINFRA wakeup
 */
static int mt7927_chip_init(struct mt7927_dev *dev)
{
    u32 val;
    int i;

    dev_info(&dev->pdev->dev, "Starting chip initialization...\n");

    /* Step 1: CBTOP remap must happen first */
    mt7927_cbtop_remap(dev);

    /* Step 2: WF subsystem reset via CBInfra RGU */
    dev_info(&dev->pdev->dev, "  Resetting WF subsystem...\n");
    mt7927_cbtop_write(dev, MT_CBINFRA_RGU_WF, 0x1);
    wmb();
    msleep(10);
    mt7927_cbtop_write(dev, MT_CBINFRA_RGU_WF, 0x0);
    wmb();
    msleep(50);

    /* Step 3: MCU ownership acquisition */
    dev_info(&dev->pdev->dev, "  Acquiring MCU ownership...\n");
    mt7927_cbtop_write(dev, MT_CBINFRA_MCU_OWN_SET, 0x1);
    wmb();
    msleep(10);

    /* Step 4: Poll ROMCODE_INDEX for MCU idle (0x1D1E) */
    dev_info(&dev->pdev->dev, "  Waiting for ROMCODE idle...\n");
    for (i = 0; i < 200; i++) {
        val = mt7927_cbtop_read(dev, MT_ROMCODE_INDEX);
        if (val == MT_ROMCODE_IDLE) {
            dev_info(&dev->pdev->dev, "  ROMCODE idle (0x%04x) after %dms\n",
                     val, i * 10);
            break;
        }
        if (i % 20 == 0)
            dev_info(&dev->pdev->dev, "  ROMCODE: 0x%08x (waiting...)\n", val);
        msleep(10);
    }

    if (val != MT_ROMCODE_IDLE) {
        dev_warn(&dev->pdev->dev, "  ROMCODE did not reach idle (0x%08x)\n", val);
        /* Continue anyway — firmware load may still work */
    }

    /* Step 5: MCIF remap for host DMA */
    dev_info(&dev->pdev->dev, "  Setting MCIF remap...\n");
    mt7927_cbtop_write(dev, MT_MCIF_REMAP, MT_MCIF_REMAP_VAL);
    wmb();

    /* Step 6: Disable PCIe sleep */
    dev_info(&dev->pdev->dev, "  Disabling PCIe sleep...\n");
    mt7927_cbtop_write(dev, MT_PCIE_SLEEP_CTRL, 0x0);
    wmb();

    /* Step 7: Clear CONNINFRA wakeup */
    mt7927_cbtop_write(dev, MT_CBTOP_WAKEPU_TOP, 0x0);
    wmb();

    dev_info(&dev->pdev->dev, "Chip initialization complete\n");
    return 0;
}

/* ── DMA Init (patch-04) ─────────────────────────────────────────── */
/*
 * Initialize DMA rings with MT7927-specific layout.
 *
 * Critical difference from MT7925:
 *   MT7925: RX MCU=ring0, RX data=ring2, RX aux=ring1
 *   MT7927: RX MCU=ring6, RX data=ring4, RX aux=ring7
 *
 * Also configures MT7927-specific prefetch registers.
 */
static int mt7927_dma_ring_alloc(struct mt7927_dev *dev, int ring_idx,
                                  int size, bool is_tx)
{
    struct mt76_desc *desc;
    dma_addr_t dma;
    u32 base;

    desc = dma_alloc_coherent(&dev->pdev->dev,
                              size * sizeof(struct mt76_desc),
                              &dma, GFP_KERNEL);
    if (!desc)
        return -ENOMEM;

    memset(desc, 0, size * sizeof(struct mt76_desc));

    if (is_tx) {
        dev->tx_ring[ring_idx].desc = desc;
        dev->tx_ring[ring_idx].dma = dma;
        dev->tx_ring[ring_idx].size = size;
        base = MT_WPDMA_TX_RING(ring_idx);
    } else {
        dev->rx_ring[ring_idx].desc = desc;
        dev->rx_ring[ring_idx].dma = dma;
        dev->rx_ring[ring_idx].size = size;
        base = MT_WPDMA_RX_RING(ring_idx);
    }

    /* Program ring base address, count, and reset indices */
    mt7927_reg_write(dev, base + MT_RING_BASE, lower_32_bits(dma));
    mt7927_reg_write(dev, base + MT_RING_BASE + 4, upper_32_bits(dma));
    mt7927_reg_write(dev, base + MT_RING_CNT, size);
    mt7927_reg_write(dev, base + MT_RING_CIDX, 0);
    mt7927_reg_write(dev, base + MT_RING_DIDX, 0);
    wmb();

    dev_info(&dev->pdev->dev, "  %s ring %d: %d entries @ 0x%llx\n",
             is_tx ? "TX" : "RX", ring_idx, size, (u64)dma);

    return 0;
}

static int mt7927_dma_init(struct mt7927_dev *dev)
{
    int ret;

    dev_info(&dev->pdev->dev, "Initializing DMA (MT7927 layout)...\n");

    /* Disable DMA before configuration */
    mt7927_reg_write(dev, MT_WPDMA_GLO_CFG, 0x0);
    wmb();
    msleep(5);

    /* Reset all ring indices */
    mt7927_reg_write(dev, MT_WPDMA_RST_IDX, 0xFFFFFFFF);
    wmb();
    msleep(5);
    mt7927_reg_write(dev, MT_WPDMA_RST_IDX, 0x0);
    wmb();
    msleep(5);

    /* Configure MT7927-specific prefetch (patch-04 values) */
    mt7927_reg_write(dev, MT_WFDMA_PREFETCH_CFG0, MT7927_PREFETCH_CFG0);
    mt7927_reg_write(dev, MT_WFDMA_PREFETCH_CFG1, MT7927_PREFETCH_CFG1);
    mt7927_reg_write(dev, MT_WFDMA_PREFETCH_CFG2, MT7927_PREFETCH_CFG2);
    mt7927_reg_write(dev, MT_WFDMA_PREFETCH_CFG3, MT7927_PREFETCH_CFG3);
    wmb();

    /* TX ring 0 — data */
    ret = mt7927_dma_ring_alloc(dev, MT7927_TX_RING_DATA,
                                MT7927_TX_RING_SIZE, true);
    if (ret) return ret;

    /* TX ring 15 — MCU commands */
    ret = mt7927_dma_ring_alloc(dev, MT7927_TX_RING_MCU_CMD,
                                MT7927_MCU_RING_SIZE, true);
    if (ret) return ret;

    /* TX ring 16 — firmware download */
    ret = mt7927_dma_ring_alloc(dev, MT7927_TX_RING_FW_DL,
                                MT7927_FW_DL_RING_SIZE, true);
    if (ret) return ret;

    /* RX ring 4 — data (MT7927-specific!) */
    ret = mt7927_dma_ring_alloc(dev, MT7927_RX_RING_DATA,
                                MT7927_RX_RING_SIZE, false);
    if (ret) return ret;

    /* RX ring 6 — MCU events (MT7927-specific!) */
    ret = mt7927_dma_ring_alloc(dev, MT7927_RX_RING_MCU,
                                MT7927_MCU_RING_SIZE, false);
    if (ret) return ret;

    /* RX ring 7 — management frames (MT7927-specific!) */
    ret = mt7927_dma_ring_alloc(dev, MT7927_RX_RING_AUX,
                                MT7927_MCU_RING_SIZE, false);
    if (ret) return ret;

    /* Set MT7927-specific GLO_CFG bits (patch-10) */
    mt7927_reg_set(dev, MT_WFDMA0_GLO_CFG_EXT1,
                   MT_WFDMA_GLO_CFG_ADDR_EXT_EN |
                   MT_WFDMA_GLO_CFG_CSR_LBK_RX_Q_SEL);

    /* Enable DMA TX and RX */
    mt7927_reg_write(dev, MT_WPDMA_GLO_CFG, 0x5);  /* TX_EN | RX_EN */
    wmb();
    msleep(5);

    dev_info(&dev->pdev->dev, "DMA initialized\n");
    return 0;
}

/* ── Firmware Loading ────────────────────────────────────────────── */
static int mt7927_load_firmware(struct mt7927_dev *dev)
{
    const struct firmware *fw_patch = NULL, *fw_ram = NULL;
    struct mt76_desc *desc;
    u32 val;
    int ret, i;

    dev_info(&dev->pdev->dev, "Loading firmware...\n");

    /* Load patch firmware (ROM patch) */
    ret = request_firmware(&fw_patch, MT7927_FW_PATCH, &dev->pdev->dev);
    if (ret) {
        dev_err(&dev->pdev->dev, "Failed to load patch: %s (ret=%d)\n",
                MT7927_FW_PATCH, ret);
        dev_err(&dev->pdev->dev,
                "Install firmware from ASUS driver package or\n"
                "  use download-driver.sh from mediatek-mt7927-dkms\n");
        return ret;
    }
    dev_info(&dev->pdev->dev, "  Patch firmware: %zu bytes\n", fw_patch->size);

    /* Load RAM firmware */
    ret = request_firmware(&fw_ram, MT7927_FW_RAM, &dev->pdev->dev);
    if (ret) {
        dev_err(&dev->pdev->dev, "Failed to load RAM fw: %s (ret=%d)\n",
                MT7927_FW_RAM, ret);
        release_firmware(fw_patch);
        return ret;
    }
    dev_info(&dev->pdev->dev, "  RAM firmware: %zu bytes\n", fw_ram->size);

    /*
     * Transfer patch firmware via DMA ring 16 (firmware download queue).
     *
     * The sequence is:
     * 1. Copy firmware to DMA-coherent buffer
     * 2. Set up descriptor on TX ring 16
     * 3. Bump CPU index to trigger DMA
     * 4. Poll FW_STATUS for firmware acknowledgment
     */

    /* Allocate DMA buffer for patch */
    dev->fw_size = ALIGN(fw_patch->size, 4);
    dev->fw_buf = dma_alloc_coherent(&dev->pdev->dev, dev->fw_size,
                                     &dev->fw_dma, GFP_KERNEL);
    if (!dev->fw_buf) {
        ret = -ENOMEM;
        goto out;
    }

    memcpy(dev->fw_buf, fw_patch->data, fw_patch->size);

    /* Set up descriptor on FW download ring (ring 16) */
    if (dev->tx_ring[MT7927_TX_RING_FW_DL].desc) {
        desc = &dev->tx_ring[MT7927_TX_RING_FW_DL].desc[0];
        desc->buf0 = cpu_to_le32(lower_32_bits(dev->fw_dma));
        desc->buf1 = cpu_to_le32(upper_32_bits(dev->fw_dma));
        desc->ctrl = cpu_to_le32((dev->fw_size & MT_DMA_CTL_SD_LEN0) |
                                 MT_DMA_CTL_LAST_SEC0);
        desc->info = 0;
        wmb();

        /* Bump CPU index to trigger DMA transfer */
        mt7927_reg_write(dev, MT_WPDMA_TX_RING(MT7927_TX_RING_FW_DL) +
                         MT_RING_CIDX, 1);
        wmb();

        dev_info(&dev->pdev->dev, "  Patch DMA transfer triggered\n");
    }

    /* Poll for firmware status change */
    for (i = 0; i < 200; i++) {
        val = mt7927_reg_read(dev, MT_FW_STATUS);
        if (i % 20 == 0)
            dev_info(&dev->pdev->dev, "  FW_STATUS: 0x%08x\n", val);

        /* Any change from initial state means firmware is processing */
        if (val != 0xffff10f1 && val != 0x00000000) {
            dev_info(&dev->pdev->dev,
                     "  Firmware status changed to 0x%08x!\n", val);
            break;
        }
        msleep(10);
    }

    /*
     * TODO: After patch loads, repeat for RAM firmware on same ring.
     * Then send MCU init command and wait for full initialization.
     * Once firmware is running, proceed to mac80211 registration.
     */

out:
    release_firmware(fw_ram);
    release_firmware(fw_patch);
    return ret;
}

/* ── Disable ASPM (patch-14) ─────────────────────────────────────── */
/*
 * MT7927's CONNINFRA power domain and WFDMA register access are
 * unreliable with PCIe L1 active. Causes throughput to drop from
 * 1+ Gbps to ~200 Mbps.
 */
static void mt7927_disable_aspm(struct pci_dev *pdev)
{
    u16 lnkctl;

    pcie_capability_read_word(pdev, PCI_EXP_LNKCTL, &lnkctl);
    if (lnkctl & (PCI_EXP_LNKCTL_ASPM_L0S | PCI_EXP_LNKCTL_ASPM_L1)) {
        lnkctl &= ~(PCI_EXP_LNKCTL_ASPM_L0S | PCI_EXP_LNKCTL_ASPM_L1);
        pcie_capability_write_word(pdev, PCI_EXP_LNKCTL, lnkctl);
        dev_info(&pdev->dev, "ASPM disabled for MT7927\n");
    }
}

/* ── PCI Probe ───────────────────────────────────────────────────── */
static int mt7927_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct mt7927_dev *dev;
    int ret;
    u32 val;

    dev_info(&pdev->dev, "MT7927 WiFi 7 device found (PCI ID: %04x:%04x)\n",
             pdev->vendor, pdev->device);

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->pdev = pdev;
    pci_set_drvdata(pdev, dev);

    /* Enable PCI device */
    ret = pci_enable_device(pdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable PCI device\n");
        goto err_free;
    }

    pci_set_master(pdev);

    /* Disable ASPM before any register access (patch-14) */
    mt7927_disable_aspm(pdev);

    /* Set DMA mask */
    ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
    if (ret) {
        dev_err(&pdev->dev, "Failed to set DMA mask\n");
        goto err_disable;
    }

    /* Request PCI regions */
    ret = pci_request_regions(pdev, "mt7927");
    if (ret) {
        dev_err(&pdev->dev, "Failed to request PCI regions\n");
        goto err_disable;
    }

    /* Map BARs */
    dev->bar0 = pci_iomap(pdev, 0, 0);  /* 2MB main memory */
    dev->bar2 = pci_iomap(pdev, 2, 0);  /* 32KB control registers */

    if (!dev->bar0 || !dev->bar2) {
        dev_err(&pdev->dev, "Failed to map BARs\n");
        ret = -ENOMEM;
        goto err_release;
    }

    /* Verify chip is accessible */
    val = ioread32(dev->bar2);
    if (val == 0xffffffff) {
        dev_err(&pdev->dev, "Chip in error state (0xffffffff)\n");
        ret = -EIO;
        goto err_unmap;
    }
    dev_info(&pdev->dev, "Chip status: 0x%08x\n", val);

    val = mt7927_reg_read(dev, MT_FW_STATUS);
    dev_info(&pdev->dev, "Initial FW_STATUS: 0x%08x\n", val);

    /*
     * MT7927-specific initialization sequence:
     * 1. CBTOP remap + chip init (CBInfra fabric setup)
     * 2. DMA ring allocation with MT7927 ring layout
     * 3. Firmware load via DMA ring 16
     *
     * This was the missing piece in the original driver — without
     * CBTOP remap, all WFDMA register writes are silently ignored.
     */

    /* Step 1: CBInfra initialization */
    ret = mt7927_chip_init(dev);
    if (ret) {
        dev_err(&pdev->dev, "Chip init failed (ret=%d)\n", ret);
        goto err_unmap;
    }

    /* Check FW_STATUS after chip init */
    val = mt7927_reg_read(dev, MT_FW_STATUS);
    dev_info(&pdev->dev, "FW_STATUS after chip init: 0x%08x\n", val);

    /* Step 2: DMA initialization with correct ring layout */
    ret = mt7927_dma_init(dev);
    if (ret) {
        dev_err(&pdev->dev, "DMA init failed (ret=%d)\n", ret);
        goto err_unmap;
    }

    /* Step 3: Load firmware */
    ret = mt7927_load_firmware(dev);
    if (ret) {
        dev_warn(&pdev->dev,
                 "Firmware load incomplete (ret=%d) — device claimed but not functional\n",
                 ret);
        /* Don't fail probe — keep device claimed for debugging */
        ret = 0;
    }

    /* Report final state */
    val = mt7927_reg_read(dev, MT_FW_STATUS);
    dev_info(&pdev->dev, "Final FW_STATUS: 0x%08x\n", val);

    val = ioread32(dev->bar0);
    dev_info(&pdev->dev, "BAR0[0] memory: 0x%08x%s\n", val,
             val ? " (ACTIVATED!)" : " (inactive)");

    dev_info(&pdev->dev, "MT7927 driver bound successfully\n");
    return 0;

err_unmap:
    if (dev->bar2) pci_iounmap(pdev, dev->bar2);
    if (dev->bar0) pci_iounmap(pdev, dev->bar0);
err_release:
    pci_release_regions(pdev);
err_disable:
    pci_disable_device(pdev);
err_free:
    kfree(dev);
    return ret;
}

/* ── PCI Remove ──────────────────────────────────────────────────── */
static void mt7927_remove(struct pci_dev *pdev)
{
    struct mt7927_dev *dev = pci_get_drvdata(pdev);
    int i;

    dev_info(&pdev->dev, "Removing MT7927 device\n");

    /* Free firmware DMA buffer */
    if (dev->fw_buf)
        dma_free_coherent(&pdev->dev, dev->fw_size,
                          dev->fw_buf, dev->fw_dma);

    /* Free TX rings */
    for (i = 0; i < 32; i++) {
        if (dev->tx_ring[i].desc)
            dma_free_coherent(&pdev->dev,
                              dev->tx_ring[i].size * sizeof(struct mt76_desc),
                              dev->tx_ring[i].desc,
                              dev->tx_ring[i].dma);
    }

    /* Free RX rings */
    for (i = 0; i < 8; i++) {
        if (dev->rx_ring[i].desc)
            dma_free_coherent(&pdev->dev,
                              dev->rx_ring[i].size * sizeof(struct mt76_desc),
                              dev->rx_ring[i].desc,
                              dev->rx_ring[i].dma);
    }

    /* Unmap BARs */
    if (dev->bar2) pci_iounmap(pdev, dev->bar2);
    if (dev->bar0) pci_iounmap(pdev, dev->bar0);

    pci_release_regions(pdev);
    pci_disable_device(pdev);
    kfree(dev);
}

/* ── PCI Driver Registration ─────────────────────────────────────── */
static struct pci_device_id mt7927_ids[] = {
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID) },
    { PCI_DEVICE(MT7927_VENDOR_ID, MT7927_DEVICE_ID_ALT) },
    { 0, }
};
MODULE_DEVICE_TABLE(pci, mt7927_ids);

static struct pci_driver mt7927_driver = {
    .name = "mt7927",
    .id_table = mt7927_ids,
    .probe = mt7927_probe,
    .remove = mt7927_remove,
};

module_pci_driver(mt7927_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MT7927 WiFi 7 Driver with CBInfra initialization");
MODULE_AUTHOR("MT7927 Linux Driver Project");
MODULE_FIRMWARE(MT7927_FW_RAM);
MODULE_FIRMWARE(MT7927_FW_PATCH);
