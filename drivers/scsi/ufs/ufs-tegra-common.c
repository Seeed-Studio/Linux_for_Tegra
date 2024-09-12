// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2015-2024 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.

#include <nvidia/conftest.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/version.h>
#include <soc/tegra/fuse.h>
#include <linux/reset.h>
#include <soc/tegra/pmc.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/jiffies.h>
#include <soc/tegra/fuse-helper.h>

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/iommu.h>
#include <linux/pinctrl/consumer.h>

#include <linux/debugfs.h>

#if defined(NV_UFS_UFSHCD_H_PRESENT)
#include <drivers-private/scsi/ufs/ufshcd-pltfrm.h>
#include <ufs/ufshcd.h>
#include <ufs/unipro.h>
#include <ufs/ufshci.h>
#else
#include <drivers-private/scsi/ufs/ufshcd-pltfrm.h>
#include <drivers-private/scsi/ufs/ufshcd.h>
#include <drivers-private/scsi/ufs/unipro.h>
#include <drivers-private/scsi/ufs/ufshci.h>
#endif

#include "ufs-tegra.h"
#include "ufs-provision.h"

static void ufs_tegra_mphy_startup_sequence(struct ufs_tegra_host *ufs_tegra);

#ifdef CONFIG_DEBUG_FS
static void ufs_tegra_init_debugfs(struct ufs_hba *hba)
{
	struct dentry *device_root;
	struct ufs_tegra_host *ufs_tegra = hba->priv;

	device_root = debugfs_create_dir(dev_name(hba->dev), NULL);

	if (ufs_tegra->enable_ufs_provisioning)
		debugfs_provision_init(hba, device_root);
}
#endif

static void ufs_tegra_set_clk_div(struct ufs_hba *hba)
{
	struct ufs_tegra_host *ufs_tegra = hba->priv;
	u32 hclk_val;

	if (tegra_sku_info.platform == TEGRA_PLATFORM_VSP)
		return;

	if (ufs_tegra->soc->chip_id == TEGRA264)
		hclk_val = UFS_VNDR_HCLKDIV_1US_TICK_T264;
	else
		hclk_val = UFS_VNDR_HCLKDIV_1US_TICK;

	if (tegra_sku_info.platform == TEGRA_PLATFORM_SYSTEM_FPGA)
		ufshcd_writel(hba, UFS_VNDR_HCLKDIV_1US_TICK_FPGA, REG_UFS_VNDR_HCLKDIV);
	else
		ufshcd_writel(hba, hclk_val, REG_UFS_VNDR_HCLKDIV);
}

static void ufs_tegra_ufs_mmio_axi(struct ufs_hba *hba)
{
	u32 mask = GENMASK(15, 13);

	if (tegra_sku_info.platform == TEGRA_PLATFORM_VSP)
		return;

	ufshcd_rmwl(hba, mask, VS_BURSTMBLCONFIG, VS_BURSTMBLREGISTER);

}

static int ufs_tegra_host_clk_get(struct device *dev,
		const char *name, struct clk **clk_out)
{
	struct clk *clk;
	int err = 0;

	if (tegra_sku_info.platform == TEGRA_PLATFORM_VSP)
		return 0;

	clk = devm_clk_get(dev, name);
	if (IS_ERR(clk)) {
		err = PTR_ERR(clk);
		if (err != -EPROBE_DEFER) {
			dev_err(dev, "%s: failed to get %s err %d",
				__func__, name, err);
		}
	} else {
		*clk_out = clk;
	}

	return err;
}

static int ufs_tegra_host_clk_enable(struct device *dev,
		const char *name, struct clk *clk)
{
	int err = 0;

	err = clk_prepare_enable(clk);
	if (err)
		dev_err(dev, "%s: %s enable failed %d\n", __func__, name, err);

	return err;
}

/**
 * ufs_tegra_mphy_receiver_calibration
 * @ufs_tegra: ufs_tegra_host controller instance
 *
 * Implements MPhy Receiver Calibration Sequence
 *
 * Returns -ETIMEDOUT if receiver calibration fails
 * and returns zero on success.
 */
static int ufs_tegra_mphy_receiver_calibration(struct ufs_tegra_host *ufs_tegra,
					       void __iomem *mphy_base)
{
	int err = 0;
	struct device *dev = ufs_tegra->hba->dev;
	u32 mphy_rx_vendor2_reg;
	u32 mphy_rx_vendor2;
	unsigned int timeout;

	if (tegra_sku_info.platform == TEGRA_PLATFORM_VDK)
		return 0;
	if (tegra_sku_info.platform == TEGRA_PLATFORM_SYSTEM_FPGA)
		return 0;

	if (tegra_sku_info.platform == TEGRA_PLATFORM_VSP)
		return 0;

	if (ufs_tegra->enable_mphy_rx_calib)
		return 0;

        if (ufs_tegra->soc->chip_id >= TEGRA234) {
                mphy_rx_vendor2_reg = MPHY_RX_APB_VENDOR2_0_T234;
        } else {
                mphy_rx_vendor2_reg = MPHY_RX_APB_VENDOR2_0;
        }

        if (ufs_tegra->soc->chip_id >= TEGRA234) {
                /* Set RX lane calibration */
                mphy_update(ufs_tegra->mphy_l0_base,
                    MPHY_RX_APB_VENDOR2_0_RX_CAL_EN, mphy_rx_vendor2_reg);

                if (ufs_tegra->x2config == true) {
                        dev_err(dev, "%s:x2config is true so invoking mphy_update\n",
                                __func__);
                        mphy_update(ufs_tegra->mphy_l1_base,
                                MPHY_RX_APB_VENDOR2_0_RX_CAL_EN,
                                mphy_rx_vendor2_reg);
                }

                /* TODO: GO bit has to be read back after updating it */
                mphy_update(ufs_tegra->mphy_l0_base, MPHY_GO_BIT,
                        mphy_rx_vendor2_reg);

                if (ufs_tegra->x2config == true) {
                        /* TODO: GO bit has to be read back after updating it */
                        mphy_update(ufs_tegra->mphy_l1_base,
                                MPHY_GO_BIT, mphy_rx_vendor2_reg);
                }

/* Fuse register offset to know if chip is RDL part or not */
#define TEGRA_FUSE_OPT_LOT_CODE_0_0	0x108U
#define NON_RDL_STRUCTURE	0x90570c8
#define NON_RDL_LEAD		0x83c1002

               if (ufs_tegra->x2config == true) {
                        /* Wait till lane calibration is done */
                        timeout = 100U; /* Number of iterations */
                        while (timeout != 0U) {
                                mphy_rx_vendor2 = mphy_readl(ufs_tegra->mphy_l1_base,
                                        mphy_rx_vendor2_reg);

                                if ((mphy_rx_vendor2 & MPHY_RX_APB_VENDOR2_0_RX_CAL_DONE) != 0U) {
                                        dev_err(dev, "%s: MPhy Receiver Calibration passed\n", __func__);
                                        break;
                                } else {
                                        udelay(1);
                                }
                                timeout--;
                        }
                        if (timeout == 0U) {
                                dev_err(dev, "%s: MPhy Receiver Calibration failed\n", __func__);
                                err = -ETIMEDOUT;
                                goto fail;
                        }

                        /* Clear RX lane calibration */
                        mphy_clear_bits(ufs_tegra->mphy_l1_base,
                                MPHY_RX_APB_VENDOR2_0_RX_CAL_EN,
                                mphy_rx_vendor2_reg);
                        mphy_update(ufs_tegra->mphy_l1_base,
                                MPHY_GO_BIT, mphy_rx_vendor2_reg);

                        /* Wait till lane calibration is done */
                        timeout = 100U; /* Number of iterations */
                        while (timeout != 0U) {
                                mphy_rx_vendor2 = mphy_readl(ufs_tegra->mphy_l1_base,
                                        mphy_rx_vendor2_reg);

                                if ((mphy_rx_vendor2 & MPHY_RX_APB_VENDOR2_0_RX_CAL_DONE) == 0U) {
                                        dev_err(dev, "%s: MPhy Receiver Calibration passed\n", __func__);
                                        break;
                                } else {
                                        udelay(1);
                                }
                                timeout--;
                        }
                        if (timeout == 0U) {
                               dev_err(dev, "%s: MPhy Receiver Calibration failed\n", __func__);
                                err = -ETIMEDOUT;
                                goto fail;
                        }
                }
                timeout = 100U; /* Number of iterations */
                while (timeout != 0U) {
                        mphy_rx_vendor2 = mphy_readl(ufs_tegra->mphy_l0_base,
                                mphy_rx_vendor2_reg);

                        if ((mphy_rx_vendor2 & MPHY_RX_APB_VENDOR2_0_RX_CAL_DONE) != 0U) {
                                dev_err(dev, "%s: MPhy Receiver Calibration passed\n", __func__);
                                break;
                        } else {
                                udelay(1);
                        }
                        timeout--;
                }
                if (timeout == 0U) {
                        dev_err(dev, "%s: MPhy Receiver Calibration failed\n", __func__);
                        err = -ETIMEDOUT;
                        goto fail;
                }

                /* Clear RX lane calibration */
                mphy_clear_bits(ufs_tegra->mphy_l0_base,
                                MPHY_RX_APB_VENDOR2_0_RX_CAL_EN,
                                mphy_rx_vendor2_reg);
                mphy_update(ufs_tegra->mphy_l0_base,
                                MPHY_GO_BIT, mphy_rx_vendor2_reg);
                timeout = 100U; /* Number of iterations */
                while (timeout != 0U) {
                        mphy_rx_vendor2 = mphy_readl(ufs_tegra->mphy_l0_base,
                                mphy_rx_vendor2_reg);

                        if ((mphy_rx_vendor2 & MPHY_RX_APB_VENDOR2_0_RX_CAL_DONE) == 0U) {
                                dev_dbg(dev, "%s: MPhy Receiver Calibration passed\n", __func__);
                                break;
                        } else {
				udelay(1);
                        }
                        timeout--;
                }
                if (timeout == 0U) {
                        dev_err(dev, "%s: MPhy Receiver Calibration failed\n", __func__);
                        err = -ETIMEDOUT;
                }
        } else {
                if (ufs_tegra->x2config == true) {
                        dev_dbg(dev, "%s:x2config is true so invoking mphy_update\n",
                                __func__);
                        mphy_update(ufs_tegra->mphy_l1_base,
                                MPHY_RX_APB_VENDOR2_0_RX_CAL_EN,
                                mphy_rx_vendor2_reg);
                }

                mphy_update(ufs_tegra->mphy_l0_base,
                    MPHY_RX_APB_VENDOR2_0_RX_CAL_EN, mphy_rx_vendor2_reg);

                if (ufs_tegra->x2config == true) {
                        /* TODO: GO bit has to be read back after updating it */
                        mphy_update(ufs_tegra->mphy_l1_base,
                                MPHY_GO_BIT, mphy_rx_vendor2_reg);
                }
                /* TODO: GO bit has to be read back after updating it */
                mphy_update(ufs_tegra->mphy_l0_base, MPHY_GO_BIT,
                        mphy_rx_vendor2_reg);
                timeout = 10U; /* Number of iterations */
                while (timeout != 0U) {
			udelay(1000);

                        mphy_rx_vendor2 = mphy_readl(ufs_tegra->mphy_l0_base,
                                mphy_rx_vendor2_reg);

                        if ((mphy_rx_vendor2 & MPHY_RX_APB_VENDOR2_0_RX_CAL_EN) == 0U) {
                                dev_dbg(dev, "%s: MPhy Receiver Calibration passed\n", __func__);
                                break;
                        }
                        timeout--;
                }
                if (timeout == 0U) {
                        dev_err(dev, "%s: MPhy Receiver Calibration failed\n", __func__);
                        err = -ETIMEDOUT;
                }
        }

fail:
        return err;
}

static void ufs_tegra_mphy_tx_calibration_enable(struct ufs_tegra_host *ufs_tegra)
{
	struct device *dev = ufs_tegra->hba->dev;

	if ((tegra_sku_info.platform == TEGRA_PLATFORM_VDK) ||
		(tegra_sku_info.platform == TEGRA_PLATFORM_SYSTEM_FPGA) ||
		(tegra_sku_info.platform == TEGRA_PLATFORM_VSP) ||
		(ufs_tegra->soc->chip_id != TEGRA264))
		return;

	/* Enable TX Calibration */
	mphy_update(ufs_tegra->mphy_l0_base,
		MPHY_TX_APB_VENDOR2_0_TX_CAL_EN, MPHY_TX_APB_TX_VENDOR2_0_T264);
	if (ufs_tegra->x2config == true) {
		dev_err(dev, "%s:x2config is true so invoking mphy_update\n",
				__func__);
		mphy_update(ufs_tegra->mphy_l1_base,
			MPHY_TX_APB_VENDOR2_0_TX_CAL_EN,
			MPHY_TX_APB_TX_VENDOR2_0_T264);
	}

	mphy_update(ufs_tegra->mphy_l0_base, MPHY_GO_BIT,
			MPHY_TX_APB_TX_VENDOR0_0_T234);

	if (ufs_tegra->x2config == true) {
		mphy_update(ufs_tegra->mphy_l1_base,
			MPHY_GO_BIT, MPHY_TX_APB_TX_VENDOR0_0_T234);
	}
}

static int ufs_tegra_mphy_tx_calibration_status(struct ufs_tegra_host *ufs_tegra,
	void __iomem *mphy_base)
{
	int err = 0;
	u32 mphy_tx_vendor2;
	unsigned int timeout;
	struct device *dev = ufs_tegra->hba->dev;

	/* Wait till lane calibration is done */
	timeout = 100U; /* Number of iterations */
	while (timeout != 0U) {
		mphy_tx_vendor2 = mphy_readl(mphy_base,
				MPHY_TX_APB_TX_VENDOR2_0_T264);

		if ((mphy_tx_vendor2 & MPHY_TX_APB_VENDOR2_0_TX_CAL_DONE) != 0U) {
			dev_err(dev, "%s: MPhy TX Calibration done\n", __func__);

			/* Clear TX lane calibration */
			mphy_tx_vendor2 &=~(MPHY_TX_APB_VENDOR2_0_TX_CAL_EN);
			mphy_writel(mphy_base, mphy_tx_vendor2, MPHY_TX_APB_TX_VENDOR2_0_T264);
			break;
		} else {
			udelay(1);
		}
		timeout--;
	}
	if (timeout == 0U) {
		dev_err(dev, "%s: MPhy TX Calibration failed\n", __func__);
		err = -ETIMEDOUT;
		goto fail;
	}

	mphy_update(mphy_base, MPHY_GO_BIT,
		MPHY_TX_APB_TX_VENDOR0_0_T234);

	/* Wait till lane calibration clear is done */
	timeout = 100U; /* Number of iterations */
	while (timeout != 0U) {
		mphy_tx_vendor2 = mphy_readl(mphy_base,
			MPHY_TX_APB_TX_VENDOR2_0_T264);
		if ((mphy_tx_vendor2 & MPHY_TX_APB_VENDOR2_0_TX_CAL_DONE) == 0U) {
			dev_err(dev,
				"%s: MPhy TX Calibration clear completed\n",
				__func__);
				break;
		} else {
			udelay(1);
		}
		timeout--;
	}
	if (timeout == 0U) {
		dev_err(dev, "%s: MPhy TX Calibration clear failed\n", __func__);
		err = -ETIMEDOUT;
	}
fail:
	return err;
}

static int ufs_tegra_mphy_check_tx_calibration_done_status(struct ufs_tegra_host *ufs_tegra)
{
	int err = 0;
	struct device *dev = ufs_tegra->hba->dev;

	if ((tegra_sku_info.platform == TEGRA_PLATFORM_VDK) ||
		(tegra_sku_info.platform == TEGRA_PLATFORM_SYSTEM_FPGA) ||
		(tegra_sku_info.platform == TEGRA_PLATFORM_VSP) ||
		(ufs_tegra->soc->chip_id != TEGRA264))
		return 0;

	if (ufs_tegra->x2config == true) {
		err = ufs_tegra_mphy_tx_calibration_status(ufs_tegra,
				ufs_tegra->mphy_l1_base);
		if (err ) {
			dev_err(dev, "%s: MPhy1 TX Calibration status check failed \n", __func__);
			goto fail;
		}
	}

	err = ufs_tegra_mphy_tx_calibration_status(ufs_tegra,
			ufs_tegra->mphy_l0_base);
	if (err ) {
		dev_err(dev, "%s: MPhy0 TX Calibration status check failed \n", __func__);
		goto fail;
	}
	dev_err(dev, "%s: MPhy TX Calibration completed\n", __func__);

fail:
	return err;
}

static void ufs_tegra_mphy_war(struct ufs_tegra_host *ufs_tegra)
{
	if ((ufs_tegra->soc->chip_id == TEGRA234) && (ufs_tegra->x2config)) {
		reset_control_assert(ufs_tegra->mphy_l1_rx_rst);
		udelay(50);
		reset_control_deassert(ufs_tegra->mphy_l1_rx_rst);
		udelay(2);

		mphy_update(ufs_tegra->mphy_l1_base,
				MPHY_ENABLE_RX_MPHY2UPHY_IF_OVR_CTRL,
					MPHY_RX_APB_VENDOR3_0_T234);
		mphy_update(ufs_tegra->mphy_l1_base, MPHY_GO_BIT,
				MPHY_RX_APB_VENDOR2_0_T234);
		udelay(5);
	}
}

static void ufs_tegra_disable_mphylane_clks(struct ufs_tegra_host *host)
{
	if (!host->is_lane_clks_enabled)
		return;

	clk_disable_unprepare(host->mphy_core_pll_fixed);
	clk_disable_unprepare(host->mphy_l0_tx_symb);
	clk_disable_unprepare(host->mphy_tx_1mhz_ref);
	clk_disable_unprepare(host->mphy_l0_rx_ana);
	clk_disable_unprepare(host->mphy_l0_rx_symb);
	clk_disable_unprepare(host->mphy_l0_tx_ls_3xbit);
	clk_disable_unprepare(host->mphy_l0_rx_ls_bit);

	if (host->x2config)
		clk_disable_unprepare(host->mphy_l1_rx_ana);
	if (host->soc->chip_id == TEGRA264)
		clk_disable_unprepare(host->mphy_l0_uphy_tx_fifo);

	host->is_lane_clks_enabled = false;
}

static int ufs_tegra_enable_t234_mphy_clocks(struct ufs_tegra_host *host)
{
	int err;
	struct device *dev = host->hba->dev;

	err = clk_set_rate(host->mphy_tx_hs_symb_div, MPHY_TX_HS_BIT_DIV_CLK);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(dev,
				"%s: mphy_tx_hs_symb_div set rate failed %d\n",
				__func__, err);
		goto out;
	}

	if (host->soc->chip_id != TEGRA264) {
		err = clk_set_parent(host->mphy_tx_hs_mux_symb_div, host->mphy_tx_hs_symb_div);
		if (err) {
			if (err != -EPROBE_DEFER)
				dev_err(dev,
				 "%s mphy_tx_hs_mux_symb_div set parent failed %d\n",
				 __func__, err);
			goto out;
		}
	}

	err = ufs_tegra_host_clk_enable(dev, "mphy_tx_hs_symb_div",
			host->mphy_tx_hs_symb_div);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(dev,
			 "%s mphy_tx_hs_symb_div clock enable failed %d\n",
			 __func__, err);
		goto out;
	}

	err = clk_set_rate(host->mphy_rx_hs_symb_div, MPHY_RX_HS_BIT_DIV_CLK);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(dev,
				"%s: mphy_rx_hs_symb_div set rate failed %d\n",
				__func__, err);
		goto disable_mphy_tx_hs_symb_div;
	}

	if (host->soc->chip_id != TEGRA264) {
		err = clk_set_parent(host->mphy_rx_hs_mux_symb_div, host->mphy_rx_hs_symb_div);
		if (err) {
			 dev_err(dev,
				"%s: mphy_rx_hs_symb_div set parent failed %d\n",
				__func__, err);
			goto disable_mphy_tx_hs_symb_div;
		}
	}

	err = ufs_tegra_host_clk_enable(dev, "mphy_rx_hs_symb_div", host->mphy_rx_hs_symb_div);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(dev,
				"%s: mphy_rx_hs_symb_div clock enable failed %d\n",
				__func__, err);
		goto disable_mphy_tx_hs_symb_div;
	}

	if (host->soc->chip_id != TEGRA264) {
		err = ufs_tegra_host_clk_enable(dev, "mphy_l0_tx_2x_symb", host->mphy_l0_tx_2x_symb);
		if (err) {
			if (err != -EPROBE_DEFER)
				dev_err(dev,
					"%s: mphy_l0_tx_2x_symb clock enable failed %d\n",
					__func__, err);
			goto disable_mphy_rx_hs_symb_div;
		}
	}
	goto out;

disable_mphy_rx_hs_symb_div:
	clk_disable_unprepare(host->mphy_rx_hs_symb_div);
disable_mphy_tx_hs_symb_div:
	clk_disable_unprepare(host->mphy_tx_hs_symb_div);
out:
	return err;
}

static int ufs_tegra_enable_mphylane_clks(struct ufs_tegra_host *host)
{
	int err = 0;
	struct device *dev = host->hba->dev;

	if (host->is_lane_clks_enabled)
		return 0;

	if (host->soc->chip_id == TEGRA264)
		err = ufs_tegra_host_clk_enable(dev,
			"mphy_l0_uphy_tx_fifo",
			host->mphy_l0_uphy_tx_fifo);
	else
		err = clk_prepare_enable(host->pllrefe_clk);
	if (err)
		goto out;

	err = ufs_tegra_host_clk_enable(dev, "mphy_core_pll_fixed",
		host->mphy_core_pll_fixed);
	if (err)
		goto disable_mphy_clk;

	err = ufs_tegra_host_clk_enable(dev, "mphy_l0_tx_symb",
		host->mphy_l0_tx_symb);
	if (err)
		goto disable_l0_tx_symb;

	err = ufs_tegra_host_clk_enable(dev, "mphy_tx_1mhz_ref",
		host->mphy_tx_1mhz_ref);
	if (err)
		goto disable_tx_1mhz_ref;

	err = ufs_tegra_host_clk_enable(dev, "mphy_l0_rx_ana",
		host->mphy_l0_rx_ana);
	if (err)
		goto disable_l0_rx_ana;

	err = ufs_tegra_host_clk_enable(dev, "mphy_l0_rx_symb",
		host->mphy_l0_rx_symb);
	if (err)
		goto disable_l0_rx_symb;

	err = ufs_tegra_host_clk_enable(dev, "mphy_l0_tx_ls_3xbit",
		host->mphy_l0_tx_ls_3xbit);
	if (err)
		goto disable_l0_tx_ls_3xbit;

	err = ufs_tegra_host_clk_enable(dev, "mphy_l0_rx_ls_bit",
		host->mphy_l0_rx_ls_bit);
	if (err)
		goto disable_l0_rx_ls_bit;

	if (host->x2config) {
		err = ufs_tegra_host_clk_enable(dev, "mphy_l1_rx_ana",
			host->mphy_l1_rx_ana);
		if (err)
			goto disable_l1_rx_ana;
	}

	if (host->soc->chip_id >= TEGRA234) {
		err = ufs_tegra_enable_t234_mphy_clocks(host);
		if (err)
			goto disable_t234_clocks;
	}

	host->is_lane_clks_enabled = true;
	goto out;

disable_t234_clocks:
	if (host->x2config)
		clk_disable_unprepare(host->mphy_l1_rx_ana);
disable_l1_rx_ana:
	clk_disable_unprepare(host->mphy_l0_rx_ls_bit);
disable_l0_rx_ls_bit:
	clk_disable_unprepare(host->mphy_l0_tx_ls_3xbit);
disable_l0_tx_ls_3xbit:
	clk_disable_unprepare(host->mphy_l0_rx_symb);
disable_l0_rx_symb:
	clk_disable_unprepare(host->mphy_l0_rx_ana);
disable_l0_rx_ana:
	clk_disable_unprepare(host->mphy_tx_1mhz_ref);
disable_tx_1mhz_ref:
	clk_disable_unprepare(host->mphy_l0_tx_symb);
disable_l0_tx_symb:
	clk_disable_unprepare(host->mphy_core_pll_fixed);
disable_mphy_clk:
	if (host->soc->chip_id == TEGRA264)
		clk_disable_unprepare(host->mphy_l0_uphy_tx_fifo);
	else
		clk_disable_unprepare(host->pllrefe_clk);
out:
	return err;
}

static int ufs_tegra_init_mphy_lane_clks(struct ufs_tegra_host *host)
{
	int err = 0;
	struct device *dev = host->hba->dev;


	if (host->soc->chip_id != TEGRA264) {
		err = ufs_tegra_host_clk_get(dev,
				"pllrefe_vcoout", &host->pllrefe_clk);
		if (err)
			goto out;
	} else {
		err = ufs_tegra_host_clk_get(dev, "mphy_l0_uphy_tx_fifo",
			&host->mphy_l0_uphy_tx_fifo);
		if (err)
			goto out;
	}

	err = ufs_tegra_host_clk_get(dev,
			"mphy_core_pll_fixed", &host->mphy_core_pll_fixed);
	if (err)
		goto out;

	err = ufs_tegra_host_clk_get(dev,
			"mphy_l0_tx_symb", &host->mphy_l0_tx_symb);
	if (err)
		goto out;

	err = ufs_tegra_host_clk_get(dev, "mphy_tx_1mhz_ref",
		&host->mphy_tx_1mhz_ref);
	if (err)
		goto out;

	err = ufs_tegra_host_clk_get(dev, "mphy_l0_rx_ana",
		&host->mphy_l0_rx_ana);
	if (err)
		goto out;

	err = ufs_tegra_host_clk_get(dev, "mphy_l0_rx_symb",
		&host->mphy_l0_rx_symb);
	if (err)
		goto out;

	err = ufs_tegra_host_clk_get(dev, "mphy_l0_tx_ls_3xbit",
		&host->mphy_l0_tx_ls_3xbit);
	if (err)
		goto out;

	err = ufs_tegra_host_clk_get(dev, "mphy_l0_rx_ls_bit",
		&host->mphy_l0_rx_ls_bit);
	if (err)
		goto out;

	if (host->soc->chip_id != TEGRA264) {
		err = ufs_tegra_host_clk_get(dev, "mphy_force_ls_mode",
			&host->mphy_force_ls_mode);
		if (err)
			goto out;
	}

	err = ufs_tegra_host_clk_get(dev, "mphy_l0_tx_hs_symb_div",
		&host->mphy_tx_hs_symb_div);
	if (err)
		goto out;

	if (host->soc->chip_id != TEGRA264) {
		err = ufs_tegra_host_clk_get(dev, "mphy_l0_tx_mux_symb_div",
			&host->mphy_tx_hs_mux_symb_div);
		if (err)
			goto out;
	}

	err = ufs_tegra_host_clk_get(dev, "mphy_l0_rx_hs_symb_div",
		&host->mphy_rx_hs_symb_div);
	if (err)
		goto out;

	if (host->soc->chip_id != TEGRA264) {
		err = ufs_tegra_host_clk_get(dev, "mphy_l0_rx_mux_symb_div",
			&host->mphy_rx_hs_mux_symb_div);
		if (err)
			goto out;
	}

	if (host->soc->chip_id != TEGRA264) {
		err = ufs_tegra_host_clk_get(dev, "mphy_l0_tx_2x_symb",
			&host->mphy_l0_tx_2x_symb);
		if (err)
			goto out;
	}

	if (host->x2config) {
		err = ufs_tegra_host_clk_get(dev, "mphy_l1_rx_ana",
			&host->mphy_l1_rx_ana);
		if (err)
			goto out;
	}

out:
	return err;
}

static int ufs_tegra_enable_ufs_uphy_pll3(struct ufs_tegra_host *ufs_tegra,
			bool is_rate_b)
{
	int err = 0;
	struct device *dev = ufs_tegra->hba->dev;

	if (!ufs_tegra->configure_uphy_pll3)
		return 0;

	err = ufs_tegra_host_clk_enable(dev, "uphy_pll3",
		ufs_tegra->ufs_uphy_pll3);
	if (err)
		return err;

	if (is_rate_b) {
		if (ufs_tegra->ufs_uphy_pll3)
			err = clk_set_rate(ufs_tegra->ufs_uphy_pll3,
				UFS_CLK_UPHY_PLL3_RATEB);
	} else {
		if (ufs_tegra->ufs_uphy_pll3)
			err = clk_set_rate(ufs_tegra->ufs_uphy_pll3,
					UFS_CLK_UPHY_PLL3_RATEA);
	}
	if (err)
		dev_err(dev, "%s: failed to set ufs_uphy_pll3 freq err %d",
				__func__, err);
	return err;
}

static int ufs_tegra_init_uphy_pll3(struct ufs_tegra_host *ufs_tegra)
{
	int err = 0;
	struct device *dev = ufs_tegra->hba->dev;

	if (!ufs_tegra->configure_uphy_pll3)
		return 0;

	err = ufs_tegra_host_clk_get(dev,
		"uphy_pll3", &ufs_tegra->ufs_uphy_pll3);
	return err;
}

static int ufs_tegra_init_ufs_clks(struct ufs_tegra_host *ufs_tegra)
{
	int err = 0;
	struct device *dev = ufs_tegra->hba->dev;

	err = ufs_tegra_host_clk_get(dev,
		"ufshc", &ufs_tegra->ufshc_clk);
	if (err)
		goto out;

	if (tegra_sku_info.platform == TEGRA_PLATFORM_SYSTEM_FPGA)
		goto out;

	if (ufs_tegra->soc->chip_id != TEGRA264) {
		err = ufs_tegra_host_clk_get(dev,
			"pll_p", &ufs_tegra->ufshc_parent);
	} else {
		err = ufs_tegra_host_clk_get(dev,
			"pllrefe_vcoout", &ufs_tegra->ufshc_parent);
		if (err)
			goto out;
		err = ufs_tegra_host_clk_get(dev,
			"ufshc_div", &ufs_tegra->ufshc_clk_div);
	}
	if (err)
		goto out;
	if (ufs_tegra->soc->chip_id != TEGRA264) {
		err = ufs_tegra_host_clk_get(dev,
			"clk_m", &ufs_tegra->ufsdev_parent);
		if (err)
			goto out;
	}

	err = ufs_tegra_host_clk_get(dev,
		"ufsdev_ref", &ufs_tegra->ufsdev_ref_clk);
	if (err)
		goto out;

	err = ufs_tegra_host_clk_get(dev,
		"osc", &ufs_tegra->ufsdev_osc);

out:
	return err;
}

static int ufs_tegra_enable_ufs_clks(struct ufs_tegra_host *ufs_tegra)
{
	struct device *dev = ufs_tegra->hba->dev;
	int err = 0;

	if (tegra_sku_info.platform == TEGRA_PLATFORM_SYSTEM_FPGA) {
		err = ufs_tegra_host_clk_enable(dev, "ufshc",
			ufs_tegra->ufshc_clk);
		goto out;
	}

	if (ufs_tegra->soc->chip_id == TEGRA264) {
		/* TEGRA264_CLK_UFSHC_CG_SYS_DIV is parent
		 * for ufs
		 */
		err = clk_set_parent(ufs_tegra->ufshc_clk,
			ufs_tegra->ufshc_clk_div);
	} else {
		err = clk_set_parent(ufs_tegra->ufshc_clk,
			ufs_tegra->ufshc_parent);
	}
	if (err) {
		pr_err("Function clk_set_parent failed\n");
		goto out;
	}
	if (ufs_tegra->soc->chip_id != TEGRA264) {
		err = clk_set_rate(ufs_tegra->ufshc_clk, UFSHC_CLK_FREQ);
	} else {
		/* In T264, set frequency for ufs parent and enable
		 * ufs clock
		 */
		err = clk_set_rate(ufs_tegra->ufshc_clk_div, UFSHC_CLK_FREQ_T264);
	}
	if (err) {
		pr_err("Function clk_set_rate failed\n");
		goto out;
	}

	err = ufs_tegra_host_clk_enable(dev, "ufshc",
		ufs_tegra->ufshc_clk);

	if (err) {
		pr_err("ufshc clock enable failed %d\n", err);
		goto out;
	}

	/* clk_m is the parent for ufsdev_ref
	 * Frequency is 19.2 MHz.
	 */
	err = ufs_tegra_host_clk_enable(dev, "ufsdev_ref",
		ufs_tegra->ufsdev_ref_clk);
	if (err)
		goto disable_ufshc;

	if ((ufs_tegra->soc->chip_id >= TEGRA234) &&
			(ufs_tegra->enable_38mhz_clk)) {
		err = clk_set_parent(ufs_tegra->ufsdev_ref_clk,
				ufs_tegra->ufsdev_osc);

		if (err) {
			pr_err("Function clk_set_parent failed\n");
			goto out;
		}
	}

	ufs_tegra->hba->clk_gating.state = CLKS_ON;

	return err;

disable_ufshc:
	clk_disable_unprepare(ufs_tegra->ufshc_clk);
out:
	return err;
}

static void ufs_tegra_disable_ufs_clks(struct ufs_tegra_host *ufs_tegra)
{
	if (ufs_tegra->hba->clk_gating.state == CLKS_OFF)
		return;

	clk_disable_unprepare(ufs_tegra->ufshc_clk);

	if (tegra_sku_info.platform == TEGRA_PLATFORM_SYSTEM_FPGA)
		goto end;

	clk_disable_unprepare(ufs_tegra->ufsdev_ref_clk);

end:
	ufs_tegra->hba->clk_gating.state = CLKS_OFF;
}

static int ufs_tegra_ufs_reset_init(struct ufs_tegra_host *ufs_tegra)
{
	struct device *dev = ufs_tegra->hba->dev;
	int ret = 0;

	if (tegra_sku_info.platform == TEGRA_PLATFORM_VSP)
		return 0;

	ufs_tegra->ufs_rst = devm_reset_control_get(dev, "ufs-rst");
	if (IS_ERR(ufs_tegra->ufs_rst)) {
		ret = PTR_ERR(ufs_tegra->ufs_rst);
		dev_err(dev,
			"Reset control for ufs-rst not found: %d\n", ret);
	}
	ufs_tegra->ufs_axi_m_rst = devm_reset_control_get(dev, "ufs-axi-m-rst");
	if (IS_ERR(ufs_tegra->ufs_axi_m_rst)) {
		ret = PTR_ERR(ufs_tegra->ufs_axi_m_rst);
		dev_err(dev,
			"Reset control for ufs-axi-m-rst not found: %d\n", ret);
	}
	ufs_tegra->ufshc_lp_rst = devm_reset_control_get(dev, "ufshc-lp-rst");
	if (IS_ERR(ufs_tegra->ufshc_lp_rst)) {
		ret = PTR_ERR(ufs_tegra->ufshc_lp_rst);
		dev_err(dev,
			"Reset control for ufshc-lp-rst not found: %d\n", ret);
	}
	return ret;
}

static void ufs_tegra_ufs_deassert_reset(struct ufs_tegra_host *ufs_tegra)
{
	reset_control_deassert(ufs_tegra->ufs_rst);
	reset_control_deassert(ufs_tegra->ufs_axi_m_rst);
	reset_control_deassert(ufs_tegra->ufshc_lp_rst);
}

static int ufs_tegra_mphy_reset_init(struct ufs_tegra_host *ufs_tegra)
{
	struct device *dev = ufs_tegra->hba->dev;
	int ret = 0;

	ufs_tegra->mphy_l0_rx_rst =
				devm_reset_control_get(dev, "mphy-l0-rx-rst");
	if (IS_ERR(ufs_tegra->mphy_l0_rx_rst)) {
		ret = PTR_ERR(ufs_tegra->mphy_l0_rx_rst);
		dev_err(dev,
			"Reset control for mphy-l0-rx-rst not found: %d\n",
									ret);
	}

	ufs_tegra->mphy_l0_tx_rst =
				devm_reset_control_get(dev, "mphy-l0-tx-rst");
	if (IS_ERR(ufs_tegra->mphy_l0_tx_rst)) {
		ret = PTR_ERR(ufs_tegra->mphy_l0_tx_rst);
		dev_err(dev,
			"Reset control for mphy-l0-tx-rst not found: %d\n",
									ret);
	}

	ufs_tegra->mphy_clk_ctl_rst =
				devm_reset_control_get(dev, "mphy-clk-ctl-rst");
	if (IS_ERR(ufs_tegra->mphy_clk_ctl_rst)) {
		ret = PTR_ERR(ufs_tegra->mphy_clk_ctl_rst);
		dev_err(dev,
			"Reset control for mphy-clk-ctl-rst not found: %d\n",
									ret);
	}

	if (ufs_tegra->x2config) {
		ufs_tegra->mphy_l1_rx_rst =
				devm_reset_control_get(dev, "mphy-l1-rx-rst");
		if (IS_ERR(ufs_tegra->mphy_l1_rx_rst)) {
			ret = PTR_ERR(ufs_tegra->mphy_l1_rx_rst);
			dev_err(dev,
			"Reset control for mphy-l1-rx-rst not found: %d\n",
									ret);
		}

		ufs_tegra->mphy_l1_tx_rst =
				devm_reset_control_get(dev, "mphy-l1-tx-rst");
		if (IS_ERR(ufs_tegra->mphy_l1_tx_rst)) {
			ret = PTR_ERR(ufs_tegra->mphy_l1_tx_rst);
			dev_err(dev,
			"Reset control for mphy_l1_tx_rst not found: %d\n",
									ret);
		}
	}

	return ret;
}

static void ufs_tegra_mphy_assert_reset(struct ufs_tegra_host *ufs_tegra)
{
	reset_control_assert(ufs_tegra->mphy_l0_rx_rst);
	reset_control_assert(ufs_tegra->mphy_l0_tx_rst);
	reset_control_assert(ufs_tegra->mphy_clk_ctl_rst);
	if (ufs_tegra->x2config) {
		reset_control_assert(ufs_tegra->mphy_l1_rx_rst);
		reset_control_assert(ufs_tegra->mphy_l1_tx_rst);
	}
}

static void ufs_tegra_mphy_deassert_reset(struct ufs_tegra_host *ufs_tegra)
{
	reset_control_deassert(ufs_tegra->mphy_l0_rx_rst);
	reset_control_deassert(ufs_tegra->mphy_l0_tx_rst);
	reset_control_deassert(ufs_tegra->mphy_clk_ctl_rst);
	if (ufs_tegra->x2config) {
		reset_control_deassert(ufs_tegra->mphy_l1_rx_rst);
		reset_control_deassert(ufs_tegra->mphy_l1_tx_rst);
	}
}

static void ufs_tegra_pwr_change_clk_boost(struct ufs_tegra_host *ufs_tegra)
{
	u32 reg_vendor_0;

	if (ufs_tegra->soc->chip_id >= TEGRA234)
		reg_vendor_0 = MPHY_RX_APB_VENDOR2_0_T234;
	else
		reg_vendor_0 = MPHY_RX_APB_VENDOR2_0;

	mphy_writel(ufs_tegra->mphy_l0_base, MPHY_PWR_CHANGE_CLK_BOOST,
			MPHY_RX_APB_VENDOR49_0_T234);
	mphy_update(ufs_tegra->mphy_l0_base, MPHY_GO_BIT, reg_vendor_0);

	if (ufs_tegra->x2config) {
		mphy_writel(ufs_tegra->mphy_l1_base, MPHY_PWR_CHANGE_CLK_BOOST,
				MPHY_RX_APB_VENDOR49_0_T234);
		mphy_update(ufs_tegra->mphy_l1_base, MPHY_GO_BIT, reg_vendor_0);
	}
	udelay(20);
}

static void ufs_tegra_mphy_rx_sync_capability(struct ufs_tegra_host *ufs_tegra)
{
	u32 val_88_8b = 0;
	u32 val_94_97 = 0;
	u32 val_8c_8f = 0;
	u32 val_98_9b = 0;
	u32 vendor2_reg, vendor3_reg;

	if (ufs_tegra->soc->chip_id >= TEGRA234) {
		vendor2_reg = MPHY_RX_APB_VENDOR2_0_T234;
		vendor3_reg = MPHY_RX_APB_VENDOR3_0_T234;
	} else {
		vendor2_reg = MPHY_RX_APB_VENDOR2_0;
		vendor3_reg = MPHY_RX_APB_VENDOR3_0;
	}

	/* MPHY RX sync lengths capability changes */

	/*Update HS_G1 Sync Length MPHY_RX_APB_CAPABILITY_88_8B_0*/
	val_88_8b = mphy_readl(ufs_tegra->mphy_l0_base,
			MPHY_RX_APB_CAPABILITY_88_8B_0);
	val_88_8b &= ~RX_HS_G1_SYNC_LENGTH_CAPABILITY(~0);
	val_88_8b |= RX_HS_G1_SYNC_LENGTH_CAPABILITY(RX_HS_SYNC_LENGTH);

	/*Update HS_G2&G3 Sync Length MPHY_RX_APB_CAPABILITY_94_97_0*/
	val_94_97 = mphy_readl(ufs_tegra->mphy_l0_base,
			MPHY_RX_APB_CAPABILITY_94_97_0);
	val_94_97 &= ~RX_HS_G2_SYNC_LENGTH_CAPABILITY(~0);
	val_94_97 |= RX_HS_G2_SYNC_LENGTH_CAPABILITY(RX_HS_SYNC_LENGTH);
	val_94_97 &= ~RX_HS_G3_SYNC_LENGTH_CAPABILITY(~0);
	val_94_97 |= RX_HS_G3_SYNC_LENGTH_CAPABILITY(RX_HS_SYNC_LENGTH);

	/* MPHY RX TActivate_capability changes */

	/* Update MPHY_RX_APB_CAPABILITY_8C_8F_0 */
	val_8c_8f = mphy_readl(ufs_tegra->mphy_l0_base,
			MPHY_RX_APB_CAPABILITY_8C_8F_0);
	val_8c_8f &= ~RX_MIN_ACTIVATETIME_CAP_ARG(~0);
	val_8c_8f |= RX_MIN_ACTIVATETIME_CAP_ARG(RX_MIN_ACTIVATETIME);

	/* Update MPHY_RX_APB_CAPABILITY_98_9B_0 */
	val_98_9b = mphy_readl(ufs_tegra->mphy_l0_base,
			MPHY_RX_APB_CAPABILITY_98_9B_0);
	val_98_9b &= ~RX_ADVANCED_FINE_GRANULARITY(~0);
	val_98_9b &= ~RX_ADVANCED_GRANULARITY(~0);
	val_98_9b &= ~RX_ADVANCED_MIN_ACTIVATETIME(~0);
	val_98_9b |= RX_ADVANCED_MIN_ACTIVATETIME(RX_ADVANCED_MIN_AT);

	mphy_writel(ufs_tegra->mphy_l0_base, val_88_8b,
			MPHY_RX_APB_CAPABILITY_88_8B_0);
	mphy_writel(ufs_tegra->mphy_l0_base, val_94_97,
			MPHY_RX_APB_CAPABILITY_94_97_0);
	mphy_writel(ufs_tegra->mphy_l0_base, val_8c_8f,
			MPHY_RX_APB_CAPABILITY_8C_8F_0);
	mphy_writel(ufs_tegra->mphy_l0_base, val_98_9b,
			MPHY_RX_APB_CAPABILITY_98_9B_0);
	mphy_update(ufs_tegra->mphy_l0_base,
			MPHY_ENABLE_RX_MPHY2UPHY_IF_OVR_CTRL,
				vendor3_reg);
	mphy_update(ufs_tegra->mphy_l0_base,
				MPHY_GO_BIT, vendor2_reg);

	if (ufs_tegra->x2config) {
		mphy_writel(ufs_tegra->mphy_l1_base, val_88_8b,
			MPHY_RX_APB_CAPABILITY_88_8B_0);
		mphy_writel(ufs_tegra->mphy_l1_base, val_94_97,
			MPHY_RX_APB_CAPABILITY_94_97_0);
		mphy_writel(ufs_tegra->mphy_l1_base, val_8c_8f,
			MPHY_RX_APB_CAPABILITY_8C_8F_0);
		mphy_writel(ufs_tegra->mphy_l1_base, val_98_9b,
			MPHY_RX_APB_CAPABILITY_98_9B_0);
		mphy_update(ufs_tegra->mphy_l1_base,
				MPHY_ENABLE_RX_MPHY2UPHY_IF_OVR_CTRL,
					vendor3_reg);
		/* set gobit */
		mphy_update(ufs_tegra->mphy_l1_base,
				MPHY_GO_BIT, vendor2_reg);
	}
}

static void ufs_tegra_mphy_rx_advgran(struct ufs_tegra_host *ufs_tegra)
{
	u32 val = 0, reg_vendor_2;

	if (ufs_tegra->soc->chip_id >= TEGRA234)
		reg_vendor_2 = MPHY_RX_APB_VENDOR2_0_T234;
	else
		reg_vendor_2 = MPHY_RX_APB_VENDOR2_0;

	val = mphy_readl(ufs_tegra->mphy_l0_base, MPHY_RX_APB_CAPABILITY_98_9B_0);
	val &= ~RX_ADVANCED_GRANULARITY(~0);
	val |= RX_ADVANCED_GRANULARITY(0x1);

	val &= ~RX_ADVANCED_MIN_ACTIVATETIME(~0);
	val |= RX_ADVANCED_MIN_ACTIVATETIME(0x8);

	mphy_writel(ufs_tegra->mphy_l0_base, val,
					MPHY_RX_APB_CAPABILITY_98_9B_0);
	mphy_update(ufs_tegra->mphy_l0_base, MPHY_GO_BIT,
			reg_vendor_2);

	if (ufs_tegra->x2config) {
		val = mphy_readl(ufs_tegra->mphy_l1_base,
				MPHY_RX_APB_CAPABILITY_98_9B_0);
		val &= ~RX_ADVANCED_GRANULARITY(~0);
		val |= RX_ADVANCED_GRANULARITY(0x1);

		val &= ~RX_ADVANCED_MIN_ACTIVATETIME(~0);
		val |= RX_ADVANCED_MIN_ACTIVATETIME(0x8);

		mphy_writel(ufs_tegra->mphy_l1_base, val,
					MPHY_RX_APB_CAPABILITY_98_9B_0);
		mphy_update(ufs_tegra->mphy_l1_base, MPHY_GO_BIT,
			reg_vendor_2);
	}
}

static void ufs_tegra_ufs_aux_ref_clk_enable(struct ufs_tegra_host *ufs_tegra)
{
	ufs_aux_update(ufs_tegra->ufs_aux_base, UFSHC_DEV_CLK_EN,
						UFSHC_AUX_UFSHC_DEV_CTRL_0);
}

static void ufs_tegra_ufs_aux_ref_clk_disable(struct ufs_tegra_host *ufs_tegra)
{
	ufs_aux_clear_bits(ufs_tegra->ufs_aux_base, UFSHC_DEV_CLK_EN,
						UFSHC_AUX_UFSHC_DEV_CTRL_0);
}

static void ufs_tegra_aux_reset_enable(struct ufs_tegra_host *ufs_tegra)
{
	ufs_aux_clear_bits(ufs_tegra->ufs_aux_base,
					UFSHC_DEV_RESET,
					UFSHC_AUX_UFSHC_DEV_CTRL_0);
}

static void ufs_tegra_ufs_aux_prog(struct ufs_tegra_host *ufs_tegra)
{

	/*
	 * Release the reset to UFS device on pin ufs_rst_n
	 */

	if (ufs_tegra->ufshc_state != UFSHC_SUSPEND)
		ufs_aux_update(ufs_tegra->ufs_aux_base, UFSHC_DEV_RESET,
						UFSHC_AUX_UFSHC_DEV_CTRL_0);


	if (ufs_tegra->ufshc_state == UFSHC_SUSPEND) {
		/*
		 * Disable reference clock to Device
		 */
		ufs_tegra_ufs_aux_ref_clk_disable(ufs_tegra);

	} else {
		/*
		 * Enable reference clock to Device
		 */
		ufs_tegra_ufs_aux_ref_clk_enable(ufs_tegra);
	}
}

static void ufs_tegra_context_save(struct ufs_tegra_host *ufs_tegra)
{
	u32 reg_len = 0;
	u32 len = 0;
	u32 *mphy_context_save = ufs_tegra->mphy_context;

	reg_len = ARRAY_SIZE(mphy_rx_apb);
	/*
	 * Save mphy_rx_apb lane0 and lane1 context
	 */
	ufs_save_regs(ufs_tegra->mphy_l0_base, mphy_context_save,
							mphy_rx_apb, reg_len);
	len += reg_len;

	if (ufs_tegra->x2config) {
		ufs_save_regs(ufs_tegra->mphy_l1_base, mphy_context_save + len,
							mphy_rx_apb, reg_len);
		len += reg_len;
	}

	reg_len = ARRAY_SIZE(mphy_tx_apb);
	/*
	 * Save mphy_tx_apb lane0 and lane1 context
	 */
	ufs_save_regs(ufs_tegra->mphy_l0_base, mphy_context_save + len,
							mphy_tx_apb, reg_len);
	len += reg_len;
	if (ufs_tegra->x2config)
		ufs_save_regs(ufs_tegra->mphy_l1_base,
			mphy_context_save + len, mphy_tx_apb, reg_len);
}

static void ufs_tegra_context_restore(struct ufs_tegra_host *ufs_tegra)
{
	u32 reg_len = 0;
	u32 len = 0;
	u32 *mphy_context_restore = ufs_tegra->mphy_context;
	u32 reg_vendor_0, reg_vendor_2;

	if (ufs_tegra->soc->chip_id >= TEGRA234) {
		reg_vendor_0 = MPHY_TX_APB_TX_VENDOR0_0_T234;
		reg_vendor_2 = MPHY_RX_APB_VENDOR2_0_T234;
	} else {
		reg_vendor_0 = MPHY_TX_APB_TX_VENDOR0_0;
		reg_vendor_2 = MPHY_RX_APB_VENDOR2_0;
	}

	reg_len = ARRAY_SIZE(mphy_rx_apb);
	/*
	 * Restore mphy_rx_apb lane0 and lane1 context
	 */
	ufs_restore_regs(ufs_tegra->mphy_l0_base, mphy_context_restore,
							mphy_rx_apb, reg_len);
	mphy_update(ufs_tegra->mphy_l0_base, MPHY_GO_BIT, reg_vendor_2);

	len += reg_len;
	if (ufs_tegra->x2config) {
		ufs_restore_regs(ufs_tegra->mphy_l1_base,
			mphy_context_restore + len, mphy_rx_apb, reg_len);
		mphy_update(ufs_tegra->mphy_l1_base, MPHY_GO_BIT,
				reg_vendor_2);
		len += reg_len;
	}

	reg_len = ARRAY_SIZE(mphy_tx_apb);
	/*
	 * Restore mphy_tx_apb lane0 and lane1 context
	 */
	ufs_restore_regs(ufs_tegra->mphy_l0_base, mphy_context_restore + len,
							mphy_tx_apb, reg_len);
	mphy_writel(ufs_tegra->mphy_l0_base, MPHY_GO_BIT, reg_vendor_0);

	len += reg_len;
	if (ufs_tegra->x2config) {
		ufs_restore_regs(ufs_tegra->mphy_l1_base,
			mphy_context_restore + len, mphy_tx_apb, reg_len);
		mphy_writel(ufs_tegra->mphy_l1_base, MPHY_GO_BIT, reg_vendor_0);
	}
}

#if defined(NV_UFS_HBA_VARIANT_OPS_SUSPEND_HAS_STATUS_ARG)
static int ufs_tegra_suspend(struct ufs_hba *hba, enum ufs_pm_op pm_op,
		enum ufs_notify_change_status status)
#else
static int ufs_tegra_suspend(struct ufs_hba *hba, enum ufs_pm_op pm_op)
#endif
{
	struct ufs_tegra_host *ufs_tegra = hba->priv;
	struct device *dev = hba->dev;
	u32 val;
	int ret = 0;
	int timeout = 500;
	bool is_ufs_lp_pwr_gated = false;

	if (pm_op != UFS_SYSTEM_PM)
		return 0;

	ufs_tegra->ufshc_state = UFSHC_SUSPEND;

	if (ufs_tegra->soc->chip_id < TEGRA234) {
		/*
		 * Enable DPD for UFS
		 */
		if (ufs_tegra->ufs_pinctrl &&
				!IS_ERR_OR_NULL(ufs_tegra->dpd_enable)) {
			ret = pinctrl_select_state(ufs_tegra->ufs_pinctrl,
						   ufs_tegra->dpd_enable);
			if (ret)
				dev_err(dev, "pinctrl power down fail %d\n",
						ret);
		}

		do {
			udelay(100);
			val = ufs_aux_readl(ufs_tegra->ufs_aux_base,
					UFSHC_AUX_UFSHC_STATUS_0);
			if (val & UFSHC_HIBERNATE_STATUS) {
				is_ufs_lp_pwr_gated = true;
				break;
			}
			timeout--;
		} while (timeout > 0);

		if (timeout <= 0) {
			dev_err(dev, "UFSHC_AUX_UFSHC_STATUS_0 = %x\n", val);
			return -ETIMEDOUT;
		}

		if (is_ufs_lp_pwr_gated) {
			/*
			 * Save all armphy_rx_apb and armphy_tx_apb registers
			 * T234 does not require context save
			 */
			ufs_tegra_context_save(ufs_tegra);
			reset_control_assert(ufs_tegra->ufshc_lp_rst);
		}
	} else {
		/*
		 * For T234, during sc7 entry, the link is set to off state
		 * so that during sc7 exit link startup happens (According to IAS)
		 */
		ufshcd_set_link_off(hba);
	}

	/* Clocks are not present on VDK */
	if (tegra_sku_info.platform == TEGRA_PLATFORM_VDK)
		goto end;

	if (tegra_sku_info.platform == TEGRA_PLATFORM_VSP)
		goto end;

	/*
	 * Disable ufs, mphy tx/rx lane clocks if they are on
	 * and assert the reset
	 */

	/* MPHY is not present on FPGA */
	if (tegra_sku_info.platform == TEGRA_PLATFORM_SYSTEM_FPGA)
		goto disable_ufs_clks;

	ufs_tegra_disable_mphylane_clks(ufs_tegra);
	ufs_tegra_mphy_assert_reset(ufs_tegra);

disable_ufs_clks:
	ufs_tegra_disable_ufs_clks(ufs_tegra);
	goto end;

	reset_control_assert(ufs_tegra->ufs_axi_m_rst);
end:
	return ret;
}


static int ufs_tegra_resume(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	struct ufs_tegra_host *ufs_tegra = hba->priv;
	struct device *dev = hba->dev;
	int ret = 0;

	if (pm_op != UFS_SYSTEM_PM)
		return 0;

	ufs_tegra->ufshc_state = UFSHC_RESUME;

	ret = ufs_tegra_enable_ufs_clks(ufs_tegra);
	if (ret)
		return ret;

	if (tegra_sku_info.platform == TEGRA_PLATFORM_SYSTEM_FPGA)
		goto deassert_ufs_clk;

	ret = ufs_tegra_enable_mphylane_clks(ufs_tegra);
	if (ret)
		goto out_disable_ufs_clks;
	ufs_tegra_mphy_deassert_reset(ufs_tegra);

deassert_ufs_clk:
	ufs_tegra_ufs_deassert_reset(ufs_tegra);
	ufs_tegra_ufs_aux_prog(ufs_tegra);

	if (ufs_tegra->soc->chip_id < TEGRA234) {
		if (ufs_tegra->ufs_pinctrl &&
			!IS_ERR_OR_NULL(ufs_tegra->dpd_disable)) {
			ret = pinctrl_select_state(ufs_tegra->ufs_pinctrl,
					   ufs_tegra->dpd_disable);
			if (ret) {
				dev_err(dev, "pinctrl power up fail %d\n", ret);
				goto out_disable_mphylane_clks;
			}
		}
		/*
		 * T234 does not require context restore
		 */
		ufs_tegra_context_restore(ufs_tegra);
	} else {
		if (dev_iommu_fwspec_get(dev) != NULL) {
			writel(UFS_AUX_ADDR_VIRT_CTRL_EN,
				ufs_tegra->ufs_virtualization_base +
				UFS_AUX_ADDR_VIRT_CTRL_0);
			writel(ufs_tegra->streamid,
				ufs_tegra->ufs_virtualization_base +
				UFS_AUX_ADDR_VIRT_REG_0);
		}
	}

	if (tegra_sku_info.platform == TEGRA_PLATFORM_SYSTEM_FPGA)
		goto end;

	ufs_tegra_set_clk_div(hba);

	ret = ufs_tegra_mphy_receiver_calibration(ufs_tegra,
			ufs_tegra->mphy_l0_base);
	if (ret < 0)
		goto out_disable_mphylane_clks;

end:
	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return ret;

out_disable_mphylane_clks:
	ufs_tegra_disable_mphylane_clks(ufs_tegra);
out_disable_ufs_clks:
	ufs_tegra_disable_ufs_clks(ufs_tegra);

	return ret;
}

static void ufs_tegra_print_power_mode_config(struct ufs_hba *hba,
			struct ufs_pa_layer_attr *configured_params)
{
	u32 rx_gear;
	u32 tx_gear;
	const char *freq_series = "";

	rx_gear = configured_params->gear_rx;
	tx_gear = configured_params->gear_tx;

	if (configured_params->hs_rate) {
		if (configured_params->hs_rate == PA_HS_MODE_A)
			freq_series = "RATE_A";
		else if (configured_params->hs_rate == PA_HS_MODE_B)
			freq_series = "RATE_B";
		dev_info(hba->dev,
			"HS Mode RX_Gear:gear_%u TX_Gear:gear_%u %s series\n",
				rx_gear, tx_gear, freq_series);
	} else {
		dev_info(hba->dev,
			"PWM Mode RX_Gear:gear_%u TX_Gear:gear_%u\n",
				rx_gear, tx_gear);
	}
}

static void ufs_tegra_scramble_enable(struct ufs_hba *hba)
{
	u32 pa_val;

	ufshcd_dme_get(hba, UIC_ARG_MIB(PA_PEERSCRAMBLING), &pa_val);
	if (pa_val & SCREN) {
		ufshcd_dme_get(hba, UIC_ARG_MIB(PA_SCRAMBLING), &pa_val);
		pa_val |= SCREN;
		ufshcd_dme_set(hba, UIC_ARG_MIB(PA_SCRAMBLING), pa_val);
	}
}

static int ufs_tegra_pwr_change_notify(struct ufs_hba *hba,
		enum ufs_notify_change_status status,
		struct ufs_pa_layer_attr *dev_max_params,
		struct ufs_pa_layer_attr *dev_req_params)
{
	struct ufs_tegra_host *ufs_tegra = hba->priv;
	u32 vs_save_config;
	int ret = 0;
	u32 pa_reg_check;

	if (!dev_req_params) {
		pr_err("%s: incoming dev_req_params is NULL\n", __func__);
		ret = -EINVAL;
		goto out;
	}

	switch (status) {
	case PRE_CHANGE:
		/* Update VS_DebugSaveConfigTime Tref */
		ufshcd_dme_get(hba, UIC_ARG_MIB(VS_DEBUGSAVECONFIGTIME),
			&vs_save_config);
		/* Update VS_DebugSaveConfigTime st_sct */
		vs_save_config &= ~SET_ST_SCT(~0);
		vs_save_config |= SET_ST_SCT(VS_DEBUGSAVECONFIGTIME_ST_SCT);
		/* Update VS_DebugSaveConfigTime Tref */
		vs_save_config &= ~SET_TREF(~0);
		vs_save_config |= SET_TREF(VS_DEBUGSAVECONFIGTIME_TREF);

		ufshcd_dme_set(hba, UIC_ARG_MIB(VS_DEBUGSAVECONFIGTIME),
				vs_save_config);

		memcpy(dev_req_params, dev_max_params,
			sizeof(struct ufs_pa_layer_attr));

		if ((ufs_tegra->enable_hs_mode) && (dev_max_params->hs_rate)) {
			if (ufs_tegra->max_hs_gear) {
				if (dev_max_params->gear_rx >
						ufs_tegra->max_hs_gear)
					dev_req_params->gear_rx =
						ufs_tegra->max_hs_gear;
				if (dev_max_params->gear_tx >
						ufs_tegra->max_hs_gear)
					dev_req_params->gear_tx =
						ufs_tegra->max_hs_gear;
			} else {
				dev_req_params->gear_rx = UFS_HS_G1;
				dev_req_params->gear_tx = UFS_HS_G1;
			}
			if (ufs_tegra->mask_fast_auto_mode) {
				dev_req_params->pwr_rx = FAST_MODE;
				dev_req_params->pwr_tx = FAST_MODE;
			} else {
				dev_req_params->pwr_rx = FASTAUTO_MODE;
				dev_req_params->pwr_tx = FASTAUTO_MODE;
			}
			if (ufs_tegra->mask_hs_mode_b) {
				dev_req_params->hs_rate = PA_HS_MODE_A;
				ufs_tegra_enable_ufs_uphy_pll3(ufs_tegra,
								false);
			} else {
				ufs_tegra_enable_ufs_uphy_pll3(ufs_tegra, true);
			}
			if (ufs_tegra->enable_scramble)
				ufs_tegra_scramble_enable(hba);

			/*
			 * Clock boost during power change
			 * is required as per T234 IAS document
			 */
			if (ufs_tegra->soc->chip_id == TEGRA234) {
				ufs_tegra_pwr_change_clk_boost(ufs_tegra);
				ufshcd_dme_configure_adapt(hba, dev_req_params->gear_rx,
						PA_INITIAL_ADAPT);
			}
		} else {
			if (ufs_tegra->max_pwm_gear) {
				ufshcd_dme_get(hba,
					UIC_ARG_MIB(PA_MAXRXPWMGEAR),
					&dev_req_params->gear_rx);
				ufshcd_dme_peer_get(hba,
					UIC_ARG_MIB(PA_MAXRXPWMGEAR),
					&dev_req_params->gear_tx);
				if (dev_req_params->gear_rx >
						ufs_tegra->max_pwm_gear)
					dev_req_params->gear_rx =
						ufs_tegra->max_pwm_gear;
				if (dev_req_params->gear_tx >
						ufs_tegra->max_pwm_gear)
					dev_req_params->gear_tx =
						ufs_tegra->max_pwm_gear;
			} else {
				dev_req_params->gear_rx = UFS_PWM_G1;
				dev_req_params->gear_tx = UFS_PWM_G1;
			}
			dev_req_params->pwr_rx = SLOWAUTO_MODE;
			dev_req_params->pwr_tx = SLOWAUTO_MODE;
			dev_req_params->hs_rate = 0;
		}
		memcpy(&hba->max_pwr_info.info, dev_req_params,
			sizeof(struct ufs_pa_layer_attr));
		break;
	case POST_CHANGE:
		ufs_tegra_print_power_mode_config(hba, dev_req_params);
		ufshcd_dme_get(hba, UIC_ARG_MIB(PA_SCRAMBLING), &pa_reg_check);
		if (pa_reg_check & SCREN)
			dev_info(hba->dev, "ufs scrambling feature enabled\n");
		break;
	default:
		break;
	}
out:
	return ret;
}

static int ufs_tegra_hce_enable_notify(struct ufs_hba *hba,
		enum ufs_notify_change_status status)
{
	struct ufs_tegra_host *ufs_tegra = hba->priv;
	struct device *dev = ufs_tegra->hba->dev;
	int err = 0;

	switch (status) {
	case PRE_CHANGE:
		if (ufs_tegra->soc->chip_id != TEGRA264) {
			err = ufs_tegra_host_clk_enable(dev,
				"mphy_force_ls_mode",
				ufs_tegra->mphy_force_ls_mode);
			if (err)
				return err;
		}
		udelay(500);
		ufs_aux_clear_bits(ufs_tegra->ufs_aux_base,
				UFSHC_DEV_RESET,
				UFSHC_AUX_UFSHC_DEV_CTRL_0);
		break;
	case POST_CHANGE:
		ufs_aux_clear_bits(ufs_tegra->ufs_aux_base,
					UFSHC_CG_SYS_CLK_OVR_ON,
					UFSHC_AUX_UFSHC_SW_EN_CLK_SLCG_0);
		ufs_tegra_ufs_aux_prog(ufs_tegra);
		ufs_tegra_set_clk_div(hba);
		if (ufs_tegra->soc->chip_id != TEGRA264)
			clk_disable_unprepare(ufs_tegra->mphy_force_ls_mode);
		if (ufs_tegra->soc->chip_id >= TEGRA234)
			ufs_tegra_ufs_mmio_axi(hba);
		break;
	default:
		break;
	}
	return err;
}

static void ufs_tegra_unipro_post_linkup(struct ufs_hba *hba)
{
	struct ufs_tegra_host *ufs_tegra = hba->priv;

	/* set cport connection status = 1 */
	ufshcd_dme_set(hba, UIC_ARG_MIB(T_CONNECTIONSTATE), 0x1);

	/* MPHY TX sync length changes to MAX */
	ufshcd_dme_set(hba, UIC_ARG_MIB(PA_TxHsG1SyncLength), 0x4f);
	ufshcd_dme_set(hba, UIC_ARG_MIB(PA_TxHsG2SyncLength), 0x4f);
	ufshcd_dme_set(hba, UIC_ARG_MIB(PA_TxHsG3SyncLength), 0x4f);

	/* Local Timer Value Changes */
	ufshcd_dme_set(hba, UIC_ARG_MIB(DME_FC0PROTECTIONTIMEOUTVAL), 0x1fff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(DME_TC0REPLAYTIMEOUTVAL), 0xffff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(DME_AFC0REQTIMEOUTVAL), 0x7fff);

	/* PEER TIMER values changes - PA_PWRModeUserData */
	ufshcd_dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA0), 0x1fff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA1), 0xffff);
	ufshcd_dme_set(hba, UIC_ARG_MIB(PA_PWRMODEUSERDATA2), 0x7fff);

	/* After link start configuration request from Host controller,
	 * burst closure delay needs to be configured.
	 */
	ufshcd_dme_set(hba, UIC_ARG_MIB(VS_TXBURSTCLOSUREDELAY),
					ufs_tegra->vs_burst);

}

static void ufs_tegra_unipro_pre_linkup(struct ufs_hba *hba)
{
	struct ufs_tegra_host *ufs_tegra = hba->priv;
	/* Unipro LCC disable */
	ufshcd_dme_set(hba, UIC_ARG_MIB(PA_Local_TX_LCC_Enable), 0x0);
	/* Before link start configuration request from Host controller,
	 * burst closure delay needs to be configured to 0[7:0]
	 */
	ufshcd_dme_get(hba, UIC_ARG_MIB(VS_TXBURSTCLOSUREDELAY),
			&ufs_tegra->vs_burst);
	ufshcd_dme_set(hba, UIC_ARG_MIB(VS_TXBURSTCLOSUREDELAY), 0x0);
}

static int ufs_tegra_link_startup_notify(struct ufs_hba *hba,
			enum ufs_notify_change_status status)
{
	struct ufs_tegra_host *ufs_tegra = hba->priv;
	int err = 0;

	switch (status) {
	case PRE_CHANGE:
		if (tegra_sku_info.platform == TEGRA_PLATFORM_SYSTEM_FPGA) {
			ufs_tegra_mphy_startup_sequence(ufs_tegra);
		} else {
			ufs_tegra_mphy_rx_sync_capability(ufs_tegra);
			ufs_tegra_unipro_pre_linkup(hba);
			/* Enable TX link calibration */
			ufs_tegra_mphy_tx_calibration_enable(ufs_tegra);
		}
		break;
	case POST_CHANGE:
		/* Check TX link calibration status */
		err = ufs_tegra_mphy_check_tx_calibration_done_status(ufs_tegra);
		if (err)
			return err;
		/*POST_CHANGE case is called on success of link start-up*/
		dev_info(hba->dev, "dme-link-startup Successful\n");
		ufs_tegra_unipro_post_linkup(hba);

		err = ufs_tegra_mphy_receiver_calibration(ufs_tegra,
				ufs_tegra->mphy_l0_base);
		if (err)
			return err;
		ufs_tegra_mphy_war(ufs_tegra);
		break;
	default:
		break;
	}

	return err;
}

static int ufs_tegra_config_soc_data(struct ufs_tegra_host *ufs_tegra)
{
	struct device *dev = ufs_tegra->hba->dev;
	struct device_node *np = dev->of_node;

	ufs_tegra->enable_mphy_rx_calib =
		of_property_read_bool(np, "nvidia,enable-rx-calib");

	ufs_tegra->x2config =
		of_property_read_bool(np, "nvidia,enable-x2-config");

	ufs_tegra->enable_hs_mode =
		of_property_read_bool(np, "nvidia,enable-hs-mode");

	ufs_tegra->enable_38mhz_clk =
		of_property_read_bool(np, "nvidia,enable-38mhz-clk");

	ufs_tegra->mask_fast_auto_mode =
		of_property_read_bool(np, "nvidia,mask-fast-auto-mode");

	ufs_tegra->mask_hs_mode_b =
		of_property_read_bool(np, "nvidia,mask-hs-mode-b");

	ufs_tegra->enable_ufs_provisioning =
		of_property_read_bool(np, "nvidia,enable-ufs-provisioning");

	ufs_tegra->configure_uphy_pll3 =
		of_property_read_bool(np, "nvidia,configure-uphy-pll3");


	of_property_read_u32(np, "nvidia,max-hs-gear", &ufs_tegra->max_hs_gear);
	of_property_read_u32(np, "nvidia,max-pwm-gear",
					&ufs_tegra->max_pwm_gear);

	ufs_tegra->enable_scramble =
		of_property_read_bool(np, "nvidia,enable-scramble");

	if (ufs_tegra->soc->chip_id >= TEGRA234) {
#if defined(NV_UFSHCD_QUIRKS_ENUM_HAS_UFSHCD_QUIRK_BROKEN_64BIT_ADDRESS) /* Linux 6.0 */
		ufs_tegra->hba->quirks |= UFSHCD_QUIRK_BROKEN_64BIT_ADDRESS;
#else
		dev_err(dev, "UFSHCD_QUIRK_BROKEN_64BIT_ADDRESS not supported!\n");
		return -ENOTSUPP;
#endif
	}

	return 0;
}

static void ufs_tegra_eq_timeout(struct ufs_tegra_host *ufs_tegra)
{
	mphy_writel(ufs_tegra->mphy_l0_base, MPHY_EQ_TIMEOUT,
			MPHY_RX_APB_VENDOR3B_0_T234);
	mphy_update(ufs_tegra->mphy_l0_base, MPHY_GO_BIT, MPHY_RX_APB_VENDOR2_0_T234);

	if (ufs_tegra->x2config) {
		mphy_writel(ufs_tegra->mphy_l1_base, MPHY_EQ_TIMEOUT,
				MPHY_RX_APB_VENDOR3B_0_T234);
		mphy_update(ufs_tegra->mphy_l1_base, MPHY_GO_BIT, MPHY_RX_APB_VENDOR2_0_T234);
	}
}
static void ufs_tegra_jtag(struct ufs_tegra_host *ufs_tegra, u32 addr, u32 val)
{
	struct ufs_hba *hba = ufs_tegra->hba;

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8192, 0x4), ((addr >> 8) & (0xff)));
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8193, 0x4), (addr & 0xff));
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8194, 0x4), 0x0);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8195, 0x4), 0x2);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8195, 0x4), 0x0);
	udelay(3);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8192, 0x4), ((val >> 8) & (0xff)));
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8193, 0x4), (val & 0xff));
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8194, 0x4), 0x1);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8195, 0x4), 0x2);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8195, 0x4), 0x0);
	udelay(3);
}

static void writeC10RegBank(struct ufs_tegra_host *ufs_tegra, u32 addr, u32 val)
{
	struct ufs_hba *hba = ufs_tegra->hba;

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x0016, 0x0), (addr & 0xff));
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x0017, 0x0), ((addr >> 8) & 0xff));
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x0018, 0x0), (val & 0xff));
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x0019, 0x0), ((val >> 8) & 0xff));
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x001c, 0x0), 0x1);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd0fa, 0x0), 0x1);
	udelay(1);

}

static void waitCfgRdyN_TX(struct ufs_tegra_host *ufs_tegra)
{
	struct ufs_hba *hba = ufs_tegra->hba;
	int cfg_rdy_n = 1;
	u32 read_data = 0;

	while (cfg_rdy_n == 1) {
		ufshcd_dme_get(hba, UIC_ARG_MIB_SEL(0xd08e, 0x0), &read_data);
		cfg_rdy_n = ((read_data >> 16) & 0x1);
		read_data = 0;
	}

}

static void waitCfgRdyN_RX(struct ufs_tegra_host *ufs_tegra)
{
	struct ufs_hba *hba = ufs_tegra->hba;
	int cfg_rdy_n = 1;
	u32 read_data = 0;

	while (cfg_rdy_n == 1) {
		ufshcd_dme_get(hba, UIC_ARG_MIB_SEL(0xd08e, 0x0), &read_data);
		cfg_rdy_n = ((read_data >> 10) & 0x1);
		read_data = 0;
	}
}

static void ufs_tegra_mphy_startup_sequence(struct ufs_tegra_host *ufs_tegra)
{
	struct ufs_hba *hba = ufs_tegra->hba;

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x819f, 0x4U), 0x1U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x819f, 0x4U), 0x0U);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x819e, 0x4U), 0x21U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x819e, 0x4), 0x0U);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x819a, 0x4U), 0x4U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x819a, 0x4U), 0x14U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x819a, 0x4U), 0x4U);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x819c, 0x4U), 0x4U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x819c, 0x4U), 0x14U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x819c, 0x4U), 0x4U);


	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x818c, 0x4U), 0x4U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x818c, 0x4U), 0x14U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x818c, 0x4U), 0x4U);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x818d, 0x4U), 0x4U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x818d, 0x4U), 0x14U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x818d, 0x4U), 0x4U);


	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x818e, 0x4U), 0x4U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x818e, 0x4U), 0x14U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x818e, 0x4U), 0x4U);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x818f, 0x4U), 0x4U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x818f, 0x4U), 0x14U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x818f, 0x4U), 0x4U);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd0c1, 0x0U), 0x1U);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8190, 0x4U), 0x0U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8190, 0x4U), 0x10U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8190, 0x4U), 0x0U);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8195, 0x4U), 0x0U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8195, 0x4U), 0x4U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8195, 0x4U), 0x0U);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8191, 0x4U), 0x31U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8195, 0x4U), 0x1U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8195, 0x4U), 0x0U);

	ufs_tegra_jtag(ufs_tegra, 0x400a, 0x26);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8185, 0x4U), 0x3U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8185, 0x4U), 0x1U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8185, 0x4U), 0x2U);


	ufs_tegra_jtag(ufs_tegra, 0x2002, 0x1);
	ufs_tegra_jtag(ufs_tegra, 0x200a, 0x01bf);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8184, 0x4U), 0x1U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8189, 0x4U), 0x1aU);

	ufs_tegra_jtag(ufs_tegra, 0x3007U, 0x70U);
	ufs_tegra_jtag(ufs_tegra, 0x3107U, 0x70U);
	ufs_tegra_jtag(ufs_tegra, 0x3004U, 0x2040U);
	ufs_tegra_jtag(ufs_tegra, 0x3104U, 0x2040U);


	ufs_tegra_jtag(ufs_tegra, 0x2003U, 0x260U);
	ufs_tegra_jtag(ufs_tegra, 0x2004U, 0x4bU);
	ufs_tegra_jtag(ufs_tegra, 0x3006U, 0x540U);
	ufs_tegra_jtag(ufs_tegra, 0x3005U, 0x1e87U);
	ufs_tegra_jtag(ufs_tegra, 0x3106U, 0x540U);
	ufs_tegra_jtag(ufs_tegra, 0x3105U, 0x1e87U);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8182, 0x4U), 0x1U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8114, 0x0U), 0x1U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x811f, 0x0U), 0x1U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8132, 0x0U), 0x80U);


	waitCfgRdyN_TX(ufs_tegra);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd0fa, 0x0U), 0x1U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8009, 0x4U), 0x1U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8013, 0x4U), 0x2U);

	waitCfgRdyN_RX(ufs_tegra);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd0fa, 0x0U), 0x10U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x008f, 0x4U), 0x2U);
	waitCfgRdyN_RX(ufs_tegra);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd0fa, 0x0U), 0x10U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8009, 0x5U), 0x1U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8013, 0x5U), 0x2U);
	waitCfgRdyN_RX(ufs_tegra);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd0fa, 0x0U), 0x20U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x008f, 0x5U), 0x2U);
	waitCfgRdyN_RX(ufs_tegra);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd0fa, 0x0U), 0x20U);

	writeC10RegBank(ufs_tegra, 0x401c, 0x0004);
	writeC10RegBank(ufs_tegra, 0x411c, 0x0004);
	writeC10RegBank(ufs_tegra, 0x10ae, 0x0001);
	writeC10RegBank(ufs_tegra, 0x10ad, 0x0080);
	writeC10RegBank(ufs_tegra, 0x10af, 0x001a);
	writeC10RegBank(ufs_tegra, 0x10b6, 0x0001);
	writeC10RegBank(ufs_tegra, 0x10AE, 0x0000);
	writeC10RegBank(ufs_tegra, 0x11AE, 0x0001);
	writeC10RegBank(ufs_tegra, 0x11AD, 0x0080);
	writeC10RegBank(ufs_tegra, 0x11AF, 0x001A);
	writeC10RegBank(ufs_tegra, 0x11B6, 0x0001);
	writeC10RegBank(ufs_tegra, 0x11AE, 0x0000);
	writeC10RegBank(ufs_tegra, 0x1002, 0xAD00);
	writeC10RegBank(ufs_tegra, 0x1102, 0xAD00);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd0c1, 0x0U), 0x0U);
	waitCfgRdyN_TX(ufs_tegra);
	mdelay(200);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x002b, 0x0U), 0x0U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x002b, 0x1U), 0x0U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd0fa, 0x0U), 0x2U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd0fa, 0x0U), 0x1U);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8009, 0x0U), 0x0);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x800a, 0x0U), 0x64U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8009, 0x1U), 0x0U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x800a, 0x1U), 0x64U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8011, 0x4U), 0x0U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8012, 0x4U), 0x64U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8011, 0x5U), 0x0U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x8012, 0x5), 0x64U);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd0fa, 0x0U), 0x2U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd0fa, 0x0U), 0x1U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd0fa, 0x0U), 0x20U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd0fa, 0x0U), 0x10U);

	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0x1564, 0x0U), 0x80U);
	ufshcd_dme_set(hba, UIC_ARG_MIB_SEL(0xd086, 0x0U), 0x80U);
}

#if defined(CONFIG_TEGRA_PROD_LEGACY)
static void ufs_tegra_prod_settings(struct ufs_tegra_host *ufs_tegra)
{
	int err;

	if (!ufs_tegra->prod_list)
		return;

	err = tegra_prod_set_by_name(&ufs_tegra->mphy_l0_base, "prod", ufs_tegra->prod_list);
	if (err < 0) {
		dev_info_once(ufs_tegra->hba->dev,
			"Prod config not found for mphy0: %d\n", err);
		return;
	}

	if (ufs_tegra->x2config) {
		err = tegra_prod_set_by_name(&ufs_tegra->mphy_l1_base, "prod", ufs_tegra->prod_list);
		if (err < 0)
			dev_info_once(ufs_tegra->hba->dev,
				"Prod config not found for mphy1: %d\n", err);
	}
	return;
}
#endif

/**
 * ufs_tegra_init - bind phy with controller
 * @hba: host controller instance
 *
 * Binds PHY with controller and powers up UPHY enabling clocks
 * and regulators.
 *
 * Returns -EPROBE_DEFER if binding fails, returns negative error
 * on phy power up failure and returns zero on success.
 */
static int ufs_tegra_init(struct ufs_hba *hba)
{
	struct ufs_tegra_host *ufs_tegra;
	struct device *dev = hba->dev;
	int err = 0;
	resource_size_t ufs_aux_base_addr, ufs_aux_addr_range, mphy_addr_range;
	resource_size_t mphy_l0_addr_base, mphy_l1_addr_base;
	resource_size_t ufs_virt_base_addr = 0, ufs_virt_addr_range = 0;
	struct iommu_fwspec *fwspec;
	u32 virt_ctrl_en = 0;

	ufs_tegra = devm_kzalloc(dev, sizeof(*ufs_tegra), GFP_KERNEL);
	if (!ufs_tegra) {
		err = -ENOMEM;
		dev_err(dev, "no memory for tegra ufs host\n");
		goto out;
	}

	ufs_tegra->soc = (struct ufs_tegra_soc_data *)of_device_get_match_data(dev);
	if (!ufs_tegra->soc)
		return -EINVAL;

	ufs_tegra->ufshc_state = UFSHC_INIT;
	ufs_tegra->hba = hba;
	hba->priv = (void *)ufs_tegra;

	err = ufs_tegra_config_soc_data(ufs_tegra);
	if (err)
		return err;

	hba->spm_lvl = UFS_PM_LVL_3;
	hba->rpm_lvl = UFS_PM_LVL_1;
	hba->caps |= UFSHCD_CAP_INTR_AGGR;

	if (ufs_tegra->soc->chip_id > TEGRA234) {
		ufs_aux_base_addr = NV_ADDRESS_MAP_T264_UFSHC_AUX_BASE;
		ufs_aux_addr_range = UFS_AUX_ADDR_RANGE_264;
		mphy_l0_addr_base = NV_ADDRESS_MAP_MPHY_L0_BASE_T264;
		mphy_l1_addr_base = NV_ADDRESS_MAP_MPHY_L1_BASE_T264;
		mphy_addr_range = MPHY_ADDR_RANGE_T264;
		ufs_virt_base_addr = NV_ADDRESS_MAP_T264_UFSHC_VIRT_BASE;
		ufs_virt_addr_range = UFS_AUX_ADDR_VIRT_RANGE_264;
		virt_ctrl_en = UFS_AUX_ADDR_VIRT_CTRL_EN |
				UFS_AUX_ADDR_VIRT_PA_VA_CTRL;
	} else if (ufs_tegra->soc->chip_id == TEGRA234) {
		ufs_aux_base_addr = NV_ADDRESS_MAP_T23X_UFSHC_AUX_BASE;
		ufs_aux_addr_range = UFS_AUX_ADDR_RANGE_23X;
		mphy_l0_addr_base = NV_ADDRESS_MAP_MPHY_L0_BASE;
		mphy_l1_addr_base = NV_ADDRESS_MAP_MPHY_L1_BASE;
		mphy_addr_range = MPHY_ADDR_RANGE_T234;
		ufs_virt_base_addr = NV_ADDRESS_MAP_T23X_UFSHC_VIRT_BASE;
		ufs_virt_addr_range = UFS_AUX_ADDR_VIRT_RANGE_23X;
		virt_ctrl_en = UFS_AUX_ADDR_VIRT_CTRL_EN;
	} else {
		ufs_aux_base_addr = NV_ADDRESS_MAP_UFSHC_AUX_BASE;
		ufs_aux_addr_range = UFS_AUX_ADDR_RANGE;
		mphy_l0_addr_base = NV_ADDRESS_MAP_MPHY_L0_BASE;
		mphy_l1_addr_base = NV_ADDRESS_MAP_MPHY_L1_BASE;
		mphy_addr_range = MPHY_ADDR_RANGE;
	}

	ufs_tegra->ufs_aux_base = devm_ioremap(dev,
			ufs_aux_base_addr, ufs_aux_addr_range);
	if (!ufs_tegra->ufs_aux_base) {
		err = -ENOMEM;
		dev_err(dev, "ufs_aux_base ioremap failed\n");
		goto out;
	}

	ufs_tegra->mphy_l0_base = devm_ioremap(dev,
			mphy_l0_addr_base, mphy_addr_range);
	if (!ufs_tegra->mphy_l0_base) {
		err = -ENOMEM;
		dev_err(dev, "mphy_l0_base ioremap failed\n");
		goto out;
	}

	ufs_tegra->mphy_l1_base = devm_ioremap(dev,
			mphy_l1_addr_base, mphy_addr_range);
	if (!ufs_tegra->mphy_l1_base) {
		err = -ENOMEM;
		dev_err(dev, "mphy_l1_base ioremap failed\n");
		goto out;
	}

	if (ufs_tegra->soc->chip_id >= TEGRA234) {
		ufs_tegra->ufs_virtualization_base = devm_ioremap(dev,
				ufs_virt_base_addr,
				ufs_virt_addr_range);
		if (!ufs_tegra->ufs_virtualization_base) {
			err = -ENOMEM;
			dev_err(dev, "UFS Virtualization failed\n");
			goto out;
		}
	}

#if defined(CONFIG_TEGRA_PROD_LEGACY)
	ufs_tegra->prod_list = devm_tegra_prod_get(dev);
	if (IS_ERR(ufs_tegra->prod_list)) {
		dev_dbg(dev, "No Prod list\n");
		ufs_tegra->prod_list = NULL;
	}
#endif

	/*
	 * Clocks are not present on VDK
	 */
	if (tegra_sku_info.platform == TEGRA_PLATFORM_VDK)
		goto aux_init;

	if (tegra_sku_info.platform == TEGRA_PLATFORM_VSP)
		goto out;

	err = ufs_tegra_init_ufs_clks(ufs_tegra);
	if (err)
		goto out_host_free;

	/* MPHY is not present in FPGA
	 * don't program MPHY clocks
	 *
	 */
	if (tegra_sku_info.platform == TEGRA_PLATFORM_SYSTEM_FPGA)
		goto enable_ufs_clk;

	err = ufs_tegra_init_mphy_lane_clks(ufs_tegra);
	if (err) {
		dev_err(dev, "mphy clk init failed. Err %d\n", err);
		goto out_host_free;
	}
	err = ufs_tegra_init_uphy_pll3(ufs_tegra);
	if (err)
		goto out_host_free;
	if (ufs_tegra->soc->chip_id != TEGRA264) {
		err = ufs_tegra_host_clk_enable(dev, "mphy_force_ls_mode",
			ufs_tegra->mphy_force_ls_mode);
		if (err)
			goto out_host_free;
		usleep_range(1000, 2000);
		clk_disable_unprepare(ufs_tegra->mphy_force_ls_mode);
	}
	usleep_range(1000, 2000);

enable_ufs_clk:
	err = ufs_tegra_enable_ufs_clks(ufs_tegra);
	if (err)
		goto out_host_free;

	if (tegra_sku_info.platform == TEGRA_PLATFORM_SYSTEM_FPGA)
		goto ufs_clk_deassert;

	err = ufs_tegra_enable_mphylane_clks(ufs_tegra);
	if (err)
		goto out_disable_ufs_clks;

	err = ufs_tegra_mphy_reset_init(ufs_tegra);
	if (err)
		goto out_disable_mphylane_clks;

	ufs_tegra_mphy_deassert_reset(ufs_tegra);

ufs_clk_deassert:
	err = ufs_tegra_ufs_reset_init(ufs_tegra);
	if (err)
		goto out_disable_mphylane_clks;

	ufs_tegra_ufs_deassert_reset(ufs_tegra);
	if (tegra_sku_info.platform == TEGRA_PLATFORM_SYSTEM_FPGA)
		goto end;

	ufs_tegra_mphy_rx_advgran(ufs_tegra);

aux_init:
	ufs_tegra_ufs_aux_ref_clk_disable(ufs_tegra);
	ufs_tegra_aux_reset_enable(ufs_tegra);
	ufs_tegra_ufs_aux_prog(ufs_tegra);
	ufs_tegra_set_clk_div(hba);
	ufs_tegra_eq_timeout(ufs_tegra);

end:
	if (ufs_tegra->soc->chip_id >= TEGRA234) {
		fwspec = dev_iommu_fwspec_get(dev);
		if (fwspec == NULL) {
			dev_err(dev, "Failed to get MC streamid. Continuing\n");
		} else {
			ufs_tegra->streamid = fwspec->ids[0] & 0xffff;
			writel(virt_ctrl_en,
				ufs_tegra->ufs_virtualization_base +
				UFS_AUX_ADDR_VIRT_CTRL_0);
			writel(ufs_tegra->streamid,
				ufs_tegra->ufs_virtualization_base +
				UFS_AUX_ADDR_VIRT_REG_0);
		}
	}

#if defined(CONFIG_TEGRA_PROD_LEGACY)
	/* Configure prod values */
	ufs_tegra_prod_settings(ufs_tegra);
#endif

#ifdef CONFIG_DEBUG_FS
	ufs_tegra_init_debugfs(hba);
#endif

	return err;

out_disable_mphylane_clks:
	ufs_tegra_disable_mphylane_clks(ufs_tegra);
out_disable_ufs_clks:
	ufs_tegra_disable_ufs_clks(ufs_tegra);
out_host_free:
		hba->priv = NULL;
out:
	return err;
}

static void ufs_tegra_exit(struct ufs_hba *hba)
{
	struct ufs_tegra_host *ufs_tegra = hba->priv;

	ufs_tegra_disable_mphylane_clks(ufs_tegra);

#ifdef CONFIG_DEBUG_FS
	if (ufs_tegra->enable_ufs_provisioning)
		debugfs_provision_exit(hba);
#endif
}

/**
 * struct ufs_hba_tegra_vops - UFS TEGRA specific variant operations
 *
 * The variant operations configure the necessary controller and PHY
 * handshake during initialization.
 */
struct ufs_hba_variant_ops ufs_hba_tegra_vops = {
	.name                   = "ufs-tegra",
	.init                   = ufs_tegra_init,
	.exit                   = ufs_tegra_exit,
	.suspend		= ufs_tegra_suspend,
	.resume			= ufs_tegra_resume,
	.hce_enable_notify      = ufs_tegra_hce_enable_notify,
	.link_startup_notify	= ufs_tegra_link_startup_notify,
	.pwr_change_notify      = ufs_tegra_pwr_change_notify,
};

static int ufs_tegra_probe(struct platform_device *pdev)
{
	int err;
	struct device *dev = &pdev->dev;
	unsigned int value;

	if (tegra_platform_is_silicon()) {
		/* Do not enable ufs on non-rdl part*/
		err = tegra_fuse_readl(TEGRA_FUSE_OPT_LOT_CODE_0_0, &value);
		if (err) {
			dev_err(dev, "%s rdl fuse read failed err: %d val: %#x\n", __func__, err, value);
			goto end;
		}
		if ((value == NON_RDL_STRUCTURE) ||
			(value == NON_RDL_LEAD)) {
			dev_info(dev, "%s This is non-rdl part. No support for UFS %#x\n", __func__, value);
			err = -ENODEV;
			goto end;
		} else {
			dev_dbg(dev, "%s This is rdl part. UFS is supported %#x\n", __func__, value);
		}
	}

	/* Perform generic probe */
	err = ufshcd_pltfrm_init(pdev, &ufs_hba_tegra_vops);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(dev, "ufshcd_pltfrm_init() failed %d\n", err);
	}
end:
	return err;
}

static int ufs_tegra_remove(struct platform_device *pdev)
{
	struct ufs_hba *hba =  platform_get_drvdata(pdev);

	pm_runtime_get_sync(&(pdev)->dev);
	ufshcd_remove(hba);
	return 0;

}

static struct ufs_tegra_soc_data tegra194_soc_data = {
	.chip_id = TEGRA194,
};

static struct ufs_tegra_soc_data tegra234_soc_data = {
	.chip_id = TEGRA234,
};

#if defined(NV_TEGRA264_CHIP_ID_PRESENT) /* Linux v6.5 */
static struct ufs_tegra_soc_data tegra264_soc_data = {
	.chip_id = TEGRA264,
};
#endif

static const struct of_device_id ufs_tegra_of_match[] = {
	{
		.compatible = "tegra194,ufs_variant",
		.data = &tegra194_soc_data,
	}, {
		.compatible = "tegra234,ufs_variant",
		.data = &tegra234_soc_data,
#if defined(NV_TEGRA264_CHIP_ID_PRESENT) /* Linux v6.5 */
	}, {
		.compatible = "tegra264,ufs_variant",
		.data = &tegra264_soc_data,
#endif
	},
	{},
};

static const struct dev_pm_ops ufs_tegra_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ufshcd_system_suspend, ufshcd_system_resume)
	SET_RUNTIME_PM_OPS(ufshcd_runtime_suspend, ufshcd_runtime_resume, NULL)
};

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void ufs_tegra_remove_wrapper(struct platform_device *pdev)
{
	ufs_tegra_remove(pdev);
}
#else
static int ufs_tegra_remove_wrapper(struct platform_device *pdev)
{
	return ufs_tegra_remove(pdev);
}
#endif

static struct platform_driver ufs_tegra_platform = {
	.probe = ufs_tegra_probe,
	.remove = ufs_tegra_remove_wrapper,
	.driver = {
		.name = "ufs_tegra",
		.pm   = &ufs_tegra_pm_ops,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ufs_tegra_of_match),
	},
};

module_platform_driver(ufs_tegra_platform);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Naveen Kumar Arepalli <naveenk@nvidia.com>");
MODULE_AUTHOR("Venkata Jagadish <vjagadish@nvidia.com>");
