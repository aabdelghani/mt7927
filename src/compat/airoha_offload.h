/* Stub for kernel < 6.19 compatibility - airoha offload not available */
#ifndef _AIROHA_OFFLOAD_H
#define _AIROHA_OFFLOAD_H

#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/skbuff.h>

struct airoha_ppe_dev;
struct airoha_npu;

enum airoha_npu_wlan_set_cmd { __AIROHA_NPU_WLAN_SET_DUMMY };
enum airoha_npu_wlan_get_cmd { __AIROHA_NPU_WLAN_GET_DUMMY };

struct airoha_npu_tx_dma_desc {
    __le32 d[8];
};

struct airoha_npu_rx_dma_desc {
    __le32 d[8];
};

static inline int airoha_npu_wlan_send_msg(struct airoha_npu *npu, int ifindex,
    enum airoha_npu_wlan_set_cmd cmd, void *val, int len, gfp_t gfp)
{ return -EOPNOTSUPP; }

static inline int airoha_npu_wlan_get_msg(struct airoha_npu *npu, int ifindex,
    enum airoha_npu_wlan_get_cmd cmd, void *val, int len, gfp_t gfp)
{ return -EOPNOTSUPP; }

static inline int airoha_npu_wlan_get_irq_status(struct airoha_npu *npu, int index)
{ return 0; }

static inline void airoha_npu_wlan_set_irq_status(struct airoha_npu *npu, int status) {}
static inline void airoha_npu_wlan_disable_irq(struct airoha_npu *npu, int index) {}

static inline bool airoha_ppe_dev_check_skb(struct airoha_ppe_dev *dev,
    struct sk_buff *skb, u32 hash, bool flag)
{ return false; }

#endif /* _AIROHA_OFFLOAD_H */
