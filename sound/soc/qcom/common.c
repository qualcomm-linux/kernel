// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018, Linaro Limited.
// Copyright (c) 2018, The Linux Foundation. All rights reserved.

#include <dt-bindings/sound/qcom,q6afe.h>
#include <dt-bindings/sound/qcom,qaif.h>
#include <linux/module.h>
#include <sound/jack.h>
#include <linux/input-event-codes.h>
#include "common.h"

#define NAME_SIZE	32

static const struct snd_soc_dapm_widget qcom_jack_snd_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_SPK("DP0 Jack", NULL),
	SND_SOC_DAPM_SPK("DP1 Jack", NULL),
	SND_SOC_DAPM_SPK("DP2 Jack", NULL),
	SND_SOC_DAPM_SPK("DP3 Jack", NULL),
	SND_SOC_DAPM_SPK("DP4 Jack", NULL),
	SND_SOC_DAPM_SPK("DP5 Jack", NULL),
	SND_SOC_DAPM_SPK("DP6 Jack", NULL),
	SND_SOC_DAPM_SPK("DP7 Jack", NULL),
};

/**
 * asoc_qcom_of_xlate_dai_name - Resolve a sound-dai phandle argument to a
 *                               DAI name by searching the DAI driver array.
 * @dai_drv:  Array of DAI drivers registered by the component.
 * @num_dai:  Number of entries in @dai_drv.
 * @args:     Phandle arguments from the sound-dai property; args[0] is the
 *            DAI ID.
 * @dai_name: Output pointer set to the matched DAI name on success.
 *
 * Returns 0 on success, -EINVAL if args_count != 1 or no match is found.
 */
int asoc_qcom_of_xlate_dai_name(const struct snd_soc_dai_driver *dai_drv,
				int num_dai,
				const struct of_phandle_args *args,
				const char **dai_name)
{
	int id, i;

	if (args->args_count != 1)
		return -EINVAL;

	id = args->args[0];

	for (i = 0; i < num_dai; i++) {
		if (dai_drv[i].id == id) {
			*dai_name = dai_drv[i].name;
			return 0;
		}
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(asoc_qcom_of_xlate_dai_name);

static struct device_node *qcom_snd_get_link_node(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_card *card = rtd->card;
	struct device_node *np;
	struct device_node *cpu_np;
	struct of_phandle_args args;
	int ret;

	if (!card->dev || !card->dev->of_node)
		return NULL;

	for_each_available_child_of_node(card->dev->of_node, np) {
		cpu_np = of_get_child_by_name(np, "cpu");
		if (!cpu_np)
			continue;

		ret = of_parse_phandle_with_args(cpu_np, "sound-dai", "#sound-dai-cells", 0, &args);
		of_node_put(cpu_np);
		if (ret)
			continue;

		if (args.np == rtd->dai_link->cpus[0].of_node &&
		    args.args_count == 1 && args.args[0] == cpu_dai->id) {
			of_node_put(args.np);
			return np;
		}

		of_node_put(args.np);
	}

	return NULL;
}

static int qcom_snd_parse_tdm_slot(struct device_node *np,
				   struct qcom_snd_tdm_slot_cfg *cfg)
{
	memset(cfg, 0, sizeof(*cfg));

	return snd_soc_of_parse_tdm_slot(np, &cfg->tx_mask, &cfg->rx_mask,
					 &cfg->slots, &cfg->slot_width);
}

static int qcom_snd_normalize_tdm_slots(struct qcom_snd_tdm_slot_cfg *cpu_cfg,
					struct qcom_snd_tdm_slot_cfg *codec_cfg)
{
	unsigned int slots;
	unsigned int slot_width;

	if (cpu_cfg->slots && codec_cfg->slots && cpu_cfg->slots != codec_cfg->slots)
		return -EINVAL;

	if (cpu_cfg->slot_width && codec_cfg->slot_width &&
	    cpu_cfg->slot_width != codec_cfg->slot_width)
		return -EINVAL;

	slots = cpu_cfg->slots ?: codec_cfg->slots;
	if (!slots)
		return 0;

	slot_width = cpu_cfg->slot_width ?: codec_cfg->slot_width;
	if (!slot_width)
		return -EINVAL;

	cpu_cfg->slots = slots;
	codec_cfg->slots = slots;
	cpu_cfg->slot_width = slot_width;
	codec_cfg->slot_width = slot_width;

	return 0;
}

static int qcom_snd_parse_dai_tdm_slots(struct snd_soc_pcm_runtime *rtd,
					struct qcom_snd_tdm_slot_cfg *cpu_cfg,
					struct qcom_snd_tdm_slot_cfg *codec_cfg)
{
	struct device_node *link_np;
	struct device_node *cpu_np = NULL;
	struct device_node *codec_np = NULL;
	int ret;

	link_np = qcom_snd_get_link_node(rtd);
	if (!link_np)
		return -EINVAL;

	cpu_np = of_get_child_by_name(link_np, "cpu");
	codec_np = of_get_child_by_name(link_np, "codec");
	if (!cpu_np || !codec_np) {
		ret = -EINVAL;
		goto out;
	}

	ret = qcom_snd_parse_tdm_slot(cpu_np, cpu_cfg);
	if (ret)
		goto out;

	ret = qcom_snd_parse_tdm_slot(codec_np, codec_cfg);
out:
	of_node_put(codec_np);
	of_node_put(cpu_np);
	of_node_put(link_np);

	return ret;
}

int qcom_snd_get_dai_tdm_slots(struct snd_soc_pcm_runtime *rtd,
			       struct qcom_snd_tdm_slot_cfg *cpu_cfg,
			       struct qcom_snd_tdm_slot_cfg *codec_cfg)
{
	int ret;

	ret = qcom_snd_parse_dai_tdm_slots(rtd, cpu_cfg, codec_cfg);
	if (ret)
		return ret;

	return qcom_snd_normalize_tdm_slots(cpu_cfg, codec_cfg);
}
EXPORT_SYMBOL_GPL(qcom_snd_get_dai_tdm_slots);

int qcom_snd_apply_dai_tdm_slots_cfg(struct snd_soc_pcm_runtime *rtd,
				     const struct qcom_snd_tdm_slot_cfg *cpu_cfg,
				     const struct qcom_snd_tdm_slot_cfg *codec_cfg)
{
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai;
	int i;
	int ret;

	if (!cpu_cfg->slots)
		return 0;

	ret = snd_soc_dai_set_tdm_slot(cpu_dai, cpu_cfg->tx_mask, cpu_cfg->rx_mask,
				       cpu_cfg->slots, cpu_cfg->slot_width);
	if (ret)
		return ret;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		ret = snd_soc_dai_set_tdm_slot(codec_dai,
					       codec_cfg->tx_mask,
					       codec_cfg->rx_mask,
					       codec_cfg->slots,
					       codec_cfg->slot_width);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_snd_apply_dai_tdm_slots_cfg);

int qcom_snd_apply_dai_tdm_slots(struct snd_soc_pcm_runtime *rtd)
{
	struct qcom_snd_tdm_slot_cfg cpu_cfg;
	struct qcom_snd_tdm_slot_cfg codec_cfg;
	int ret;

	ret = qcom_snd_get_dai_tdm_slots(rtd, &cpu_cfg, &codec_cfg);
	if (ret)
		return ret == -EINVAL ? 0 : ret;

	return qcom_snd_apply_dai_tdm_slots_cfg(rtd, &cpu_cfg, &codec_cfg);
}
EXPORT_SYMBOL_GPL(qcom_snd_apply_dai_tdm_slots);

int qcom_snd_parse_of(struct snd_soc_card *card)
{
	struct device_node *np;
	struct device_node *codec = NULL;
	struct device_node *platform = NULL;
	struct device_node *cpu = NULL;
	struct device *dev = card->dev;
	struct snd_soc_dai_link *link;
	struct of_phandle_args args;
	struct snd_soc_dai_link_component *dlc;
	int ret, num_links;

	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret == 0 && !card->name)
		/* Deprecated, only for compatibility with old device trees */
		ret = snd_soc_of_parse_card_name(card, "qcom,model");
	if (ret) {
		dev_err(dev, "Error parsing card name: %d\n", ret);
		return ret;
	}

	if (of_property_present(dev->of_node, "widgets")) {
		ret = snd_soc_of_parse_audio_simple_widgets(card, "widgets");
		if (ret)
			return ret;
	}

	/* DAPM routes */
	if (of_property_present(dev->of_node, "audio-routing")) {
		ret = snd_soc_of_parse_audio_routing(card, "audio-routing");
		if (ret)
			return ret;
	}
	/* Deprecated, only for compatibility with old device trees */
	if (of_property_present(dev->of_node, "qcom,audio-routing")) {
		ret = snd_soc_of_parse_audio_routing(card, "qcom,audio-routing");
		if (ret)
			return ret;
	}

	ret = snd_soc_of_parse_pin_switches(card, "pin-switches");
	if (ret)
		return ret;

	ret = snd_soc_of_parse_aux_devs(card, "aux-devs");
	if (ret)
		return ret;

	/* Populate links */
	num_links = of_get_available_child_count(dev->of_node);

	/* Allocate the DAI link array */
	card->dai_link = devm_kcalloc(dev, num_links, sizeof(*link), GFP_KERNEL);
	if (!card->dai_link)
		return -ENOMEM;

	card->num_links = num_links;
	link = card->dai_link;

	for_each_available_child_of_node(dev->of_node, np) {
		dlc = devm_kcalloc(dev, 2, sizeof(*dlc), GFP_KERNEL);
		if (!dlc) {
			ret = -ENOMEM;
			goto err_put_np;
		}

		link->cpus	= &dlc[0];
		link->platforms	= &dlc[1];

		link->num_cpus		= 1;
		link->num_platforms	= 1;

		ret = of_property_read_string(np, "link-name", &link->name);
		if (ret) {
			dev_err(card->dev, "error getting codec dai_link name\n");
			goto err_put_np;
		}

		cpu = of_get_child_by_name(np, "cpu");
		platform = of_get_child_by_name(np, "platform");
		codec = of_get_child_by_name(np, "codec");

		if (!cpu) {
			dev_err(dev, "%s: Can't find cpu DT node\n", link->name);
			ret = -EINVAL;
			goto err;
		}

		ret = snd_soc_of_get_dlc(cpu, &args, link->cpus, 0);
		if (ret) {
			dev_err_probe(card->dev, ret,
				      "%s: error getting cpu dai name\n", link->name);
			goto err;
		}

		link->id = args.args[0];

		if (platform) {
			link->platforms->of_node = of_parse_phandle(platform,
					"sound-dai",
					0);
			if (!link->platforms->of_node) {
				dev_err(card->dev, "%s: platform dai not found\n", link->name);
				ret = -EINVAL;
				goto err;
			}
		} else {
			link->platforms->of_node = link->cpus->of_node;
		}

		if (codec) {
			ret = snd_soc_of_get_dai_link_codecs(dev, codec, link);
			if (ret < 0) {
				dev_err_probe(card->dev, ret,
					      "%s: codec dai not found\n", link->name);
				goto err;
			}

			if (platform) {
				/* DPCM backend */
				link->no_pcm = 1;
				link->ignore_pmdown_time = 1;
			}
		} else {
			/* DPCM frontend */
			link->codecs	 = &snd_soc_dummy_dlc;
			link->num_codecs = 1;
			link->dynamic = 1;
		}

		if (platform || !codec) {
			/* DPCM */
			link->ignore_suspend = 1;
			link->nonatomic = 1;
		}

		link->stream_name = link->name;
		link++;

		of_node_put(cpu);
		of_node_put(codec);
		of_node_put(platform);
	}

	if (!card->dapm_widgets) {
		card->dapm_widgets = qcom_jack_snd_widgets;
		card->num_dapm_widgets = ARRAY_SIZE(qcom_jack_snd_widgets);
	}

	return 0;
err:
	of_node_put(cpu);
	of_node_put(codec);
	of_node_put(platform);
err_put_np:
	of_node_put(np);
	return ret;
}
EXPORT_SYMBOL_GPL(qcom_snd_parse_of);

static struct snd_soc_jack_pin qcom_headset_jack_pins[] = {
	/* Headset */
	{
		.pin = "Mic Jack",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Headphone Jack",
		.mask = SND_JACK_HEADPHONE,
	},
};

int qcom_snd_wcd_jack_setup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_soc_jack *jack, bool *jack_setup)
{
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_card *card = rtd->card;
	int rval, i;

	if (!*jack_setup) {
		rval = snd_soc_card_jack_new_pins(card, "Headset Jack",
					     SND_JACK_HEADSET | SND_JACK_LINEOUT |
					     SND_JACK_MECHANICAL |
					     SND_JACK_BTN_0 | SND_JACK_BTN_1 |
					     SND_JACK_BTN_2 | SND_JACK_BTN_3 |
					     SND_JACK_BTN_4 | SND_JACK_BTN_5,
					     jack, qcom_headset_jack_pins,
					     ARRAY_SIZE(qcom_headset_jack_pins));

		if (rval < 0) {
			dev_err(card->dev, "Unable to add Headphone Jack\n");
			return rval;
		}

		snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_MEDIA);
		snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
		snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
		snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);
		*jack_setup = true;
	}

	switch (cpu_dai->id) {
	case QAIF_CDC_DMA_RX0:
	case TX_CODEC_DMA_TX_0:
	case TX_CODEC_DMA_TX_1:
	case TX_CODEC_DMA_TX_2:
	case TX_CODEC_DMA_TX_3:
		for_each_rtd_codec_dais(rtd, i, codec_dai) {
			rval = snd_soc_component_set_jack(codec_dai->component,
							  jack, NULL);
			if (rval != 0 && rval != -ENOTSUPP) {
				dev_warn(card->dev, "Failed to set jack: %d\n", rval);
				return rval;
			}
		}

		break;
	default:
		break;
	}


	return 0;
}
EXPORT_SYMBOL_GPL(qcom_snd_wcd_jack_setup);

int qcom_snd_dp_jack_setup(struct snd_soc_pcm_runtime *rtd,
			   struct snd_soc_jack *dp_jack, int dp_pcm_id)
{
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
	struct snd_soc_card *card = rtd->card;
	char jack_name[NAME_SIZE];
	int rval, i;

	snprintf(jack_name, sizeof(jack_name), "DP%d Jack", dp_pcm_id);
	rval = snd_soc_card_jack_new(card, jack_name, SND_JACK_AVOUT, dp_jack);
	if (rval)
		return rval;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		rval = snd_soc_component_set_jack(codec_dai->component, dp_jack, NULL);
		if (rval != 0 && rval != -ENOTSUPP) {
			dev_warn(card->dev, "Failed to set jack: %d\n", rval);
			return rval;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(qcom_snd_dp_jack_setup);

MODULE_DESCRIPTION("ASoC Qualcomm helper functions");
MODULE_LICENSE("GPL");
