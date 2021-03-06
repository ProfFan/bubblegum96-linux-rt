﻿/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2009 Actions Semi Inc.
 */

#ifndef __S900_SNDRV_H__
#define __S900_SNDRV_H__
#include <sound/pcm.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <linux/atomic.h>
//#include <mach/hdmac-owl.h>
#include <linux/dmaengine.h>
#include <linux/pinctrl/consumer.h>

#define PMU_NOT_USED	-1


#define S900_AIF_I2S 0
#define S900_AIF_HDMI 0
static int error_switch = 1;
static int debug_switch = 1;

#define SND_DEBUG
#ifdef SND_DEBUG
#define snd_err(fmt, args...) \
	if (error_switch) \
		printk(KERN_ERR"[SNDRV]:[%s] "fmt"\n", __func__, ##args)

#define snd_dbg(fmt, args...) \
	if (debug_switch) \
		printk(KERN_DEBUG"[SNDRV]:[%s] "fmt"\n", __func__, ##args)
#endif

enum {
	O_MODE_I2S,
	O_MODE_HDMI,
	O_MODE_SPDIF
};

enum {
	SPEAKER_ON = 0,
	HEADSET_MIC = 1,
	HEADSET_NO_MIC = 2,
};



typedef struct {
	short sample_rate;	/* 真实采样率除以1000 */
	char index[2];		/* 对应硬件寄存器的索引值 */
} fs_t;

typedef struct {
	unsigned int earphone_gpios;
	unsigned int speaker_gpios;
	unsigned int earphone_output_mode;
	unsigned int mic_num;
	unsigned int mic0_gain[2];
	unsigned int speaker_gain[2];
	unsigned int earphone_gain[2];
	unsigned int speaker_volume;
	unsigned int earphone_volume;
	unsigned int earphone_detect_mode;
} audio_hw_cfg_t;

//extern audio_hw_cfg_t audio_hw_cfg;

struct s900_pcm_priv {
	int output_mode;
	struct pinctrl *pc;
	struct pinctrl_state *ps;
};

enum{
I2S_SPDIF_NUM = 0,
GPIO_MFP_NUM,
HDMI_NUM,
MAX_RES_NUM
};

void set_dai_reg_base(int num);
u32 snd_dai_readl(u32 reg);
void snd_dai_writel(u32 val, u32 reg);

void snd_dai_i2s_clk_disable(void);
void snd_dai_i2s_clk_enable(void);
void __iomem* get_dai_reg_base(int num);


extern void hdmihw_write_reg(u32 val, const u16 idx);
extern int hdmihw_read_reg(const u16 idx);

extern void earphone_detect_cancel(void);
extern void earphone_detect_work(void);

#endif /* ifndef __S900_SNDRV_H__ */
