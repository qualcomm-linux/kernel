/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2018, The Linux Foundation. All rights reserved.

#ifndef __QCOM_SND_COMMON_H__
#define __QCOM_SND_COMMON_H__

#include <sound/soc.h>

struct qcom_snd_tdm_slot_cfg {
	unsigned int tx_mask;
	unsigned int rx_mask;
	unsigned int slots;
	unsigned int slot_width;
};

int qcom_snd_parse_of(struct snd_soc_card *card);
int qcom_snd_get_dai_tdm_slots(struct snd_soc_pcm_runtime *rtd,
			       struct qcom_snd_tdm_slot_cfg *cpu_cfg,
			       struct qcom_snd_tdm_slot_cfg *codec_cfg);
int qcom_snd_apply_dai_tdm_slots_cfg(struct snd_soc_pcm_runtime *rtd,
				     const struct qcom_snd_tdm_slot_cfg *cpu_cfg,
				     const struct qcom_snd_tdm_slot_cfg *codec_cfg);
int qcom_snd_apply_dai_tdm_slots(struct snd_soc_pcm_runtime *rtd);
int qcom_snd_wcd_jack_setup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_soc_jack *jack, bool *jack_setup);
int qcom_snd_dp_jack_setup(struct snd_soc_pcm_runtime *rtd,
			   struct snd_soc_jack *dp_jack, int id);

int asoc_qcom_of_xlate_dai_name(const struct snd_soc_dai_driver *dai_drv,
				int num_dai,
				const struct of_phandle_args *args,
				const char **dai_name);
#endif
