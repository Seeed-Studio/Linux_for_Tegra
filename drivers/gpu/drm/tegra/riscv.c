// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, NVIDIA Corporation.
 */

#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/iopoll.h>
#include <linux/of.h>

#include "riscv.h"
#include "falcon.h"

#define RISCV_CPUCTL					0x4388
#define RISCV_CPUCTL_STARTCPU_TRUE			(1 << 0)
#define RISCV_BR_RETCODE				0x465c
#define RISCV_BR_RETCODE_RESULT_V(x)			((x) & 0x3)
#define RISCV_BR_RETCODE_RESULT_PASS_V			3
#define RISCV_BCR_CTRL					0x4668
#define RISCV_BCR_CTRL_CORE_SELECT_RISCV		(1 << 4)
#define RISCV_BCR_DMACFG				0x466c
#define RISCV_BCR_DMACFG_TARGET_LOCAL_FB		(0 << 0)
#define RISCV_BCR_DMACFG_LOCK_LOCKED			(1 << 31)
#define RISCV_BCR_DMAADDR_PKCPARAM_LO			0x4670
#define RISCV_BCR_DMAADDR_PKCPARAM_HI			0x4674
#define RISCV_BCR_DMAADDR_FMCCODE_LO			0x4678
#define RISCV_BCR_DMAADDR_FMCCODE_HI			0x467c
#define RISCV_BCR_DMAADDR_FMCDATA_LO			0x4680
#define RISCV_BCR_DMAADDR_FMCDATA_HI			0x4684
#define RISCV_BCR_DMACFG_SEC				0x4694
#define RISCV_BCR_DMACFG_SEC_GSCID(v)			((v) << 16)
#define RISCV_BOOT_VECTOR_LO				0x1780
#define RISCV_BOOT_VECTOR_HI				0x1784

enum riscv_memory {
	RISCV_MEMORY_IMEM,
	RISCV_MEMORY_DATA,
};

static void riscv_writel(struct tegra_drm_riscv *riscv, u32 value, u32 offset)
{
	writel(value, riscv->regs + offset);
}

int tegra_drm_riscv_read_descriptors(struct tegra_drm_riscv *riscv)
{
	struct tegra_drm_riscv_descriptor *bl = &riscv->bl_desc;
	struct tegra_drm_riscv_descriptor *os = &riscv->os_desc;
	const struct device_node *np = riscv->dev->of_node;
	int err;

#define READ_PROP(name, location) \
	err = of_property_read_u32(np, name, location); \
	if (err) { \
		dev_err(riscv->dev, "failed to read " name ": %d", err); \
		return err; \
	}

	READ_PROP("nvidia,bl-manifest-offset", &bl->manifest_offset);
	READ_PROP("nvidia,bl-code-offset", &bl->code_offset);
	READ_PROP("nvidia,bl-data-offset", &bl->data_offset);
	READ_PROP("nvidia,os-manifest-offset", &os->manifest_offset);
	READ_PROP("nvidia,os-code-offset", &os->code_offset);
	READ_PROP("nvidia,os-data-offset", &os->data_offset);
#undef READ_PROP

	return 0;
}

int tegra_drm_riscv_boot_bootrom(struct tegra_drm_riscv *riscv, phys_addr_t image_address,
				 u32 gscid, const struct tegra_drm_riscv_descriptor *desc)
{
	phys_addr_t addr;
	int err;
	u32 val;

	riscv_writel(riscv, RISCV_BCR_CTRL_CORE_SELECT_RISCV, RISCV_BCR_CTRL);

	addr = image_address + desc->manifest_offset;
	riscv_writel(riscv, lower_32_bits(addr >> 8), RISCV_BCR_DMAADDR_PKCPARAM_LO);
	riscv_writel(riscv, upper_32_bits(addr >> 8), RISCV_BCR_DMAADDR_PKCPARAM_HI);

	addr = image_address + desc->code_offset;
	riscv_writel(riscv, lower_32_bits(addr >> 8), RISCV_BCR_DMAADDR_FMCCODE_LO);
	riscv_writel(riscv, upper_32_bits(addr >> 8), RISCV_BCR_DMAADDR_FMCCODE_HI);

	addr = image_address + desc->data_offset;
	riscv_writel(riscv, lower_32_bits(addr >> 8), RISCV_BCR_DMAADDR_FMCDATA_LO);
	riscv_writel(riscv, upper_32_bits(addr >> 8), RISCV_BCR_DMAADDR_FMCDATA_HI);

	riscv_writel(riscv, RISCV_BCR_DMACFG_SEC_GSCID(gscid), RISCV_BCR_DMACFG_SEC);
	riscv_writel(riscv,
		RISCV_BCR_DMACFG_TARGET_LOCAL_FB | RISCV_BCR_DMACFG_LOCK_LOCKED, RISCV_BCR_DMACFG);

	riscv_writel(riscv, RISCV_CPUCTL_STARTCPU_TRUE, RISCV_CPUCTL);

	err = readl_poll_timeout(
		riscv->regs + RISCV_BR_RETCODE, val,
		RISCV_BR_RETCODE_RESULT_V(val) == RISCV_BR_RETCODE_RESULT_PASS_V,
		10, 100000);
	if (err) {
		dev_err(riscv->dev, "error during bootrom execution. BR_RETCODE=%d\n", val);
		return err;
	}

	return 0;
}

int tegra_drm_riscv_read_firmware(struct tegra_drm_riscv *riscv, const char *name)
{
	int err;

	/* request_firmware prints error if it fails */
	err = request_firmware(&riscv->firmware.firmware, name, riscv->dev);
	if (err < 0)
		return err;

	riscv->firmware.size = riscv->firmware.firmware->size;

	return 0;
}

int tegra_drm_riscv_load_firmware(struct tegra_drm_riscv *riscv)
{
	const struct firmware *firmware = riscv->firmware.firmware;
	u32 *virt = riscv->firmware.virt;
	size_t i;

	/* Copy firmware image into local area taking into account endianness */
	for (i = 0; i < firmware->size / sizeof(u32); i++)
		virt[i] = le32_to_cpu(((__le32 *)firmware->data)[i]);

	release_firmware(firmware);
	riscv->firmware.firmware = NULL;

	return 0;
}

int tegra_drm_riscv_init(struct tegra_drm_riscv *riscv)
{
	riscv->firmware.virt = NULL;

	return 0;
}

void tegra_drm_riscv_exit(struct tegra_drm_riscv *riscv)
{
	if (riscv->firmware.firmware)
		release_firmware(riscv->firmware.firmware);
}

static int tegra_drm_riscv_dma_wait_idle(struct tegra_drm_riscv *riscv)
{
	u32 value;

	return readl_poll_timeout(riscv->regs + FALCON_DMATRFCMD, value,
				  (value & FALCON_DMATRFCMD_IDLE), 10, 100000);
}

static int tegra_drm_riscv_copy_chunk(struct tegra_drm_riscv *riscv,
			     phys_addr_t base,
			     unsigned long offset,
			     enum riscv_memory target)
{
	u32 cmd = FALCON_DMATRFCMD_SIZE_256B;

	if (target == RISCV_MEMORY_IMEM)
		cmd |= FALCON_DMATRFCMD_IMEM;

	/*
	 * Use second DMA context (i.e. the one for firmware). Strictly
	 * speaking, at this point both DMA contexts point to the firmware
	 * stream ID, but this register's value will be reused by the firmware
	 * for later DMA transactions, so we need to use the correct value.
	 */
	cmd |= FALCON_DMATRFCMD_DMACTX(1);

	riscv_writel(riscv, offset, FALCON_DMATRFMOFFS);
	riscv_writel(riscv, base, FALCON_DMATRFFBOFFS);
	riscv_writel(riscv, cmd, FALCON_DMATRFCMD);

	return tegra_drm_riscv_dma_wait_idle(riscv);
}

int tegra_drm_riscv_boot_external(struct tegra_drm_riscv *riscv)
{
	unsigned long offset;
	u32 value;
	int err;

	if (!riscv->firmware.virt)
		return -EINVAL;

	err = readl_poll_timeout(riscv->regs + FALCON_DMACTL, value,
				 (value & (FALCON_DMACTL_IMEM_SCRUBBING |
					   FALCON_DMACTL_DMEM_SCRUBBING)) == 0,
				 10, 10000);
	if (err < 0)
		return err;

	riscv_writel(riscv, 0, FALCON_DMACTL);

	/* setup the address of the binary data so Falcon can access it later */
	riscv_writel(riscv, riscv->firmware.iova >> 8, FALCON_DMATRFBASE);

	/* copy the data segment into riscv internal memory */
	for (offset = 0; offset < riscv->os_desc.data_size; offset += 256) {
		err = tegra_drm_riscv_copy_chunk(riscv,
				riscv->os_desc.data_offset + offset,
				offset, RISCV_MEMORY_DATA);
	}

	/* copy the code segment into riscv internal memory */
	for (offset = 0; offset < riscv->os_desc.code_size; offset += 256) {
		err = tegra_drm_riscv_copy_chunk(riscv,
				riscv->os_desc.code_offset + offset,
				offset, RISCV_MEMORY_IMEM);
	}

	/* setup riscv interrupts */
	riscv_writel(riscv, FALCON_IRQMSET_EXT(0xff) |
			      FALCON_IRQMSET_SWGEN1 |
			      FALCON_IRQMSET_SWGEN0 |
			      FALCON_IRQMSET_EXTERR |
			      FALCON_IRQMSET_HALT |
			      FALCON_IRQMSET_WDTMR,
		      FALCON_IRQMSET);
	riscv_writel(riscv, FALCON_IRQDEST_EXT(0xff) |
			      FALCON_IRQDEST_SWGEN1 |
			      FALCON_IRQDEST_SWGEN0 |
			      FALCON_IRQDEST_EXTERR |
			      FALCON_IRQDEST_HALT,
		      FALCON_IRQDEST);

	/* enable interface */
	riscv_writel(riscv, FALCON_ITFEN_MTHDEN |
			      FALCON_ITFEN_CTXEN,
		      FALCON_ITFEN);

	/* boot riscv */
	riscv_writel(riscv, 0x00000000, RISCV_BOOT_VECTOR_HI);
	riscv_writel(riscv, 0x00100000, RISCV_BOOT_VECTOR_LO);

	return 0;
}
