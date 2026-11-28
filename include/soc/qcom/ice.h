/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023, Linaro Limited
 */

#ifndef __QCOM_ICE_H__
#define __QCOM_ICE_H__

#include <linux/blk-crypto.h>
#include <linux/types.h>

struct qcom_ice;
struct device;

enum qcom_ice_cipher {
	QCOM_ICE_CIPHER_AES_128_XTS = 0,
	QCOM_ICE_CIPHER_AES_128_CBC = 1,
	QCOM_ICE_CIPHER_AES_256_XTS = 3,
	QCOM_ICE_CIPHER_AES_256_CBC = 4,
};

/**
 * struct qcom_ice_tz_ops - Qcom ICE TZ operations
 * @drv_name:                    ICE TZ backend driver name.
 * @dev:                         ICE TZ backend device.
 * @ice_invalidate_key:          Invalidate keyslot callback.
 * @ice_set_key:                 Program keyslot callback.
 * @ice_has_wrapped_key_support: Wrapped-key support callback.
 * @ice_derive_sw_secret:        Derive software secret callback.
 * @ice_generate_key:            Generate wrapped key callback.
 * @ice_prepare_key:             Prepare wrapped key callback.
 * @ice_import_key:              Import wrapped key callback.
 */
struct qcom_ice_tz_ops {
	const char *drv_name;
	struct device *dev;
	int (*ice_invalidate_key)(struct device *dev, u32 index);
	int (*ice_set_key)(struct device *dev, u32 index, const u8 *key,
			   u32 key_size, enum qcom_ice_cipher cipher,
			   u32 data_unit_size);
	bool (*ice_has_wrapped_key_support)(struct device *dev);
	int (*ice_derive_sw_secret)(struct device *dev, const u8 *eph_key,
				    size_t eph_key_size, u8 *sw_secret,
				    size_t sw_secret_size);
	int (*ice_generate_key)(struct device *dev, u8 *lt_key,
				size_t lt_key_size);
	int (*ice_prepare_key)(struct device *dev, const u8 *lt_key,
			       size_t lt_key_size, u8 *eph_key,
			       size_t eph_key_size);
	int (*ice_import_key)(struct device *dev, const u8 *raw_key,
			      size_t raw_key_size, u8 *lt_key,
			      size_t lt_key_size);
};

int qcom_ice_enable(struct qcom_ice *ice);
int qcom_ice_resume(struct qcom_ice *ice);
int qcom_ice_suspend(struct qcom_ice *ice);
int qcom_ice_program_key(struct qcom_ice *ice, unsigned int slot,
			 const struct blk_crypto_key *blk_key);
int qcom_ice_evict_key(struct qcom_ice *ice, int slot);
enum blk_crypto_key_type qcom_ice_get_supported_key_type(struct qcom_ice *ice);
int qcom_ice_derive_sw_secret(struct qcom_ice *ice,
			      const u8 *eph_key, size_t eph_key_size,
			      u8 sw_secret[BLK_CRYPTO_SW_SECRET_SIZE]);
int qcom_ice_generate_key(struct qcom_ice *ice,
			  u8 lt_key[BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE]);
int qcom_ice_prepare_key(struct qcom_ice *ice,
			 const u8 *lt_key, size_t lt_key_size,
			 u8 eph_key[BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE]);
int qcom_ice_import_key(struct qcom_ice *ice,
			const u8 *raw_key, size_t raw_key_size,
			u8 lt_key[BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE]);
struct qcom_ice *devm_of_qcom_ice_get(struct device *dev);
int qcom_ice_scale_clk(struct qcom_ice *ice, unsigned long target_freq,
		       bool round_ceil);

#ifdef CONFIG_QCOM_INLINE_CRYPTO_ENGINE
int qcom_ice_tz_ops_register(struct qcom_ice_tz_ops *ops);
void qcom_ice_tz_ops_unregister(struct qcom_ice_tz_ops *ops);
#else
static inline int qcom_ice_tz_ops_register(struct qcom_ice_tz_ops *__unused)
{
	return 0;
}

static inline
void qcom_ice_tz_ops_unregister(struct qcom_ice_tz_ops *__unused)
{
}
#endif /* CONFIG_QCOM_INLINE_CRYPTO_ENGINE */

#endif /* __QCOM_ICE_H__ */
