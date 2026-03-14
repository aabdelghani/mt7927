/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MT7927 WiFi 7 Linux Driver - Hardware Definitions
 *
 * Based on discoveries from jetm/mediatek-mt7927-dkms patches and
 * the mt76/mt7925 kernel driver.
 *
 * MT7927 = combo module (WiFi 7 + BT 5.4, Filogic 380)
 *   ├─ BT side:   internally MT6639, connects via USB
 *   └─ WiFi side: architecturally MT7925, connects via PCIe
 *
 * Key differences from MT7925:
 *   - CBInfra bus fabric requires CBTOP remap before any DMA/MCU access
 *   - DMA RX rings at 4/6/7 instead of 0/1/2
 *   - Different prefetch configuration
 *   - MT6639 firmware files (not MT7925)
 *   - DBDC must be explicitly enabled (5GHz won't work otherwise)
 *   - CLR_OWN reinitializes WFDMA, destroying ring config
 *   - ASPM must be disabled (causes severe throughput degradation)
 */

#ifndef __MT7927_H
#define __MT7927_H

#include <linux/types.h>

/* ── PCI Device IDs ──────────────────────────────────────────────── */
#define MT7927_VENDOR_ID        0x14c3
#define MT7927_DEVICE_ID        0x7927
#define MT7927_DEVICE_ID_ALT    0x6639  /* Foxconn/Azurewave modules */

/* ── Firmware paths (MT6639-based, NOT MT7925!) ──────────────────── */
#define MT7927_FW_RAM           "mediatek/mt7927/WIFI_RAM_CODE_MT6639_2_1.bin"
#define MT7927_FW_PATCH         "mediatek/mt7927/WIFI_MT6639_PATCH_MCU_2_1_hdr.bin"

/* ── BAR2 Control Registers ─────────────────────────────────────── */
#define MT_FW_STATUS            0x0200
#define MT_DMA_ENABLE           0x0204
#define MT_WPDMA_GLO_CFG        0x0208
#define MT_WPDMA_RST_IDX        0x020c

/* TX ring registers (base + N*0x10) */
#define MT_WPDMA_TX_RING_BASE   0x0300
#define MT_WPDMA_TX_RING(n)     (MT_WPDMA_TX_RING_BASE + (n) * 0x10)

/* RX ring registers (base + N*0x10) */
#define MT_WPDMA_RX_RING_BASE   0x0500
#define MT_WPDMA_RX_RING(n)     (MT_WPDMA_RX_RING_BASE + (n) * 0x10)

/* Ring register offsets within each ring block */
#define MT_RING_BASE            0x00
#define MT_RING_CNT             0x04
#define MT_RING_CIDX            0x08
#define MT_RING_DIDX            0x0c

/* MCU command/event */
#define MT_MCU_CMD              0x0790
#define MT_MCU_CMD_CLEAR_FW_OWN BIT(0)

/* Scratch registers (safe to write) */
#define MT_SCRATCH0             0x0020
#define MT_SCRATCH1             0x0024

/* ── CBInfra / CBTOP Registers (MT7927-specific) ────────────────── */
#define MT_CBTOP_WAKEPU_TOP     0x74000100
#define MT_CBTOP_REMAP_WF_L     0x74030000
#define MT_CBTOP_REMAP_WF_H     0x74030004
#define MT_CBTOP_REMAP_BT_L     0x74030008
#define MT_CBTOP_REMAP_BT_H     0x7403000c

/* CBTOP remap values */
#define MT_CBTOP_REMAP_WF_VAL   0x74037001
#define MT_CBTOP_REMAP_BT_VAL   0x70007000

/* CBInfra RGU (Reset Generation Unit) */
#define MT_CBINFRA_RGU_WF       0x74070010

/* MCU ownership */
#define MT_CBINFRA_MCU_OWN_SET  0x74060404

/* ROMCODE poll target */
#define MT_ROMCODE_INDEX        0x74060010
#define MT_ROMCODE_IDLE         0x1D1E

/* MCIF remap for host DMA */
#define MT_MCIF_REMAP           0x74060800
#define MT_MCIF_REMAP_VAL       0x18051803

/* PCIe sleep disable */
#define MT_PCIE_SLEEP_CTRL      0x74060408

/* ── WFDMA Extended Config (MT7927 DMA differences) ────────────── */
#define MT_WFDMA0_GLO_CFG_EXT1  0x0230

/* GLO_CFG bits specific to MT7927 */
#define MT_WFDMA_GLO_CFG_ADDR_EXT_EN        BIT(26)
#define MT_WFDMA_GLO_CFG_CSR_LBK_RX_Q_SEL   BIT(20)

/* Packed prefetch registers */
#define MT_WFDMA_PREFETCH_CTRL  0x0210
#define MT_WFDMA_PREFETCH_CFG0  0x0214
#define MT_WFDMA_PREFETCH_CFG1  0x0218
#define MT_WFDMA_PREFETCH_CFG2  0x021c
#define MT_WFDMA_PREFETCH_CFG3  0x0220

/* MT7927 prefetch values (from patch-04) */
#define MT7927_PREFETCH_CFG0    0x660077
#define MT7927_PREFETCH_CFG1    0x1100
#define MT7927_PREFETCH_CFG2    0x30004f
#define MT7927_PREFETCH_CFG3    0x542200

/* ── MT7927 DMA Ring Assignment (different from MT7925!) ────────── */
/*
 * MT7925 RX rings: MCU=0, Data=2, Aux=1
 * MT7927 RX rings: MCU=6, Data=4, Aux=7
 */
#define MT7927_RX_RING_MCU      6
#define MT7927_RX_RING_DATA     4
#define MT7927_RX_RING_AUX      7   /* management frames */

/* TX rings (same as MT7925) */
#define MT7927_TX_RING_DATA     0
#define MT7927_TX_RING_MCU_CMD  15
#define MT7927_TX_RING_FW_DL    16  /* firmware download */

/* Ring sizes */
#define MT7927_TX_RING_SIZE     256
#define MT7927_RX_RING_SIZE     512
#define MT7927_MCU_RING_SIZE    32
#define MT7927_FW_DL_RING_SIZE  128

/* ── IRQ Map (MT7927-specific bit positions) ────────────────────── */
/*
 * MT7925 uses BIT(0-2) for RX rings 0-2
 * MT7927 uses BIT(12,14,15) for RX rings 4,6,7
 */
#define MT7927_INT_RX_DATA      BIT(12) /* ring 4 */
#define MT7927_INT_RX_MCU       BIT(14) /* ring 6 */
#define MT7927_INT_RX_AUX       BIT(15) /* ring 7 */
#define MT7927_INT_MCU_CMD      BIT(4)  /* MCU command */
#define MT7927_INT_RX_ALL       (MT7927_INT_RX_DATA | MT7927_INT_RX_MCU | \
                                 MT7927_INT_RX_AUX)

/* ── DMA Descriptor Format (mt76 compatible) ────────────────────── */
struct mt76_desc {
    __le32 buf0;
    __le32 ctrl;
    __le32 buf1;
    __le32 info;
} __packed __aligned(4);

/* DMA control bits */
#define MT_DMA_CTL_SD_LEN0      GENMASK(15, 0)
#define MT_DMA_CTL_LAST_SEC0    BIT(16)
#define MT_DMA_CTL_BURST_SIZE   BIT(17)
#define MT_DMA_CTL_DMA_DONE     BIT(31)

/* ── Firmware Header (mt76_connac format) ────────────────────────── */
struct mt7927_fw_header {
    __le32 ilm_len;     /* instruction local memory length */
    __le32 dlm_len;     /* data local memory length */
    __le16 build_ver;
    __le16 fw_ver;
    u8 build_time[16];
    u8 reserved[64];
} __packed;

/* ── Device Structure ────────────────────────────────────────────── */
struct mt7927_dev {
    struct pci_dev *pdev;
    void __iomem *bar0;     /* BAR0: 2MB main memory */
    void __iomem *bar2;     /* BAR2: 32KB control registers */

    /* DMA rings */
    struct {
        struct mt76_desc *desc;
        dma_addr_t dma;
        int size;
    } tx_ring[32], rx_ring[8];

    /* Firmware */
    dma_addr_t fw_dma;
    void *fw_buf;
    size_t fw_size;
};

#endif /* __MT7927_H */
