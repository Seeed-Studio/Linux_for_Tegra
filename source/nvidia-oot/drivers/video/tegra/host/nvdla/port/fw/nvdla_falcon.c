// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2023, NVIDIA Corporation. All rights reserved.
 */

#include "nvdla_falcon.h"
#include "../../port/nvdla_host_wrapper.h"

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/pci_ids.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/iommu.h>

enum falcon_memory {
	FALCON_MEMORY_IMEM,
	FALCON_MEMORY_DATA,
};

static void falcon_writel(struct falcon *falcon, u32 value, u32 offset)
{
	writel(value, falcon->regs + offset);
}

static int falcon_wait_idle(struct falcon *falcon)
{
	u32 value;

	return readl_poll_timeout(falcon->regs + FALCON_IDLESTATE, value,
				  (value == 0), 10, 100000);
}

static int falcon_dma_wait_idle(struct falcon *falcon)
{
	u32 value;

	return readl_poll_timeout(falcon->regs + FALCON_DMATRFCMD, value,
				  (value & FALCON_DMATRFCMD_IDLE), 10, 100000);
}

static int falcon_copy_chunk(struct falcon *falcon,
			     phys_addr_t base,
			     unsigned long offset,
			     enum falcon_memory target)
{
	u32 cmd = FALCON_DMATRFCMD_SIZE_256B;

	if (target == FALCON_MEMORY_IMEM)
		cmd |= FALCON_DMATRFCMD_IMEM;

	falcon_writel(falcon, offset, FALCON_DMATRFMOFFS);
	falcon_writel(falcon, base, FALCON_DMATRFFBOFFS);
	falcon_writel(falcon, cmd, FALCON_DMATRFCMD);

	return falcon_dma_wait_idle(falcon);
}

static void falcon_copy_firmware_image(struct falcon *falcon,
				       const struct firmware *firmware)
{
	u32 *virt = falcon->firmware.virt;
	size_t i;

	/* copy the whole thing taking into account endianness */
	for (i = 0; i < firmware->size / sizeof(u32); i++)
		virt[i] = le32_to_cpu(((__le32 *)firmware->data)[i]);
}

static int falcon_parse_firmware_image(struct falcon *falcon)
{
	struct falcon_fw_bin_header_v1 *bin = (void *)falcon->firmware.virt;
	struct falcon_fw_os_header_v1 *os;

	/* endian problems would show up right here */
	if (bin->magic != PCI_VENDOR_ID_NVIDIA && bin->magic != 0x10fe) {
		dev_err(falcon->dev, "incorrect firmware magic\n");
		return -EINVAL;
	}

	/* currently only version 1 is supported */
	if (bin->version != 1) {
		dev_err(falcon->dev, "unsupported firmware version\n");
		return -EINVAL;
	}

	/* check that the firmware size is consistent */
	if (bin->size > falcon->firmware.size) {
		dev_err(falcon->dev, "firmware image size inconsistency\n");
		return -EINVAL;
	}

	os = falcon->firmware.virt + bin->os_header_offset;

	falcon->firmware.bin_data.size = bin->os_size;
	falcon->firmware.bin_data.offset = bin->os_data_offset;
	falcon->firmware.code.offset = os->code_offset;
	falcon->firmware.code.size = os->code_size;
	falcon->firmware.data.offset = os->data_offset;
	falcon->firmware.data.size = os->data_size;

	return 0;
}

static int falcon_read_firmware(struct falcon *falcon, const char *name)
{
	int err, retry_count=10;

retry_request:
	/* request_firmware prints error if it fails */
	err = request_firmware_direct(&falcon->firmware.firmware, name, falcon->dev);
	if (err == -EINTR && retry_count) {
		retry_count--;
		goto retry_request;
	}

	if (err < 0)
		return err;

	falcon->firmware.size = falcon->firmware.firmware->size;

	return 0;
}

static int falcon_load_firmware(struct falcon *falcon)
{
	const struct firmware *firmware = falcon->firmware.firmware;
	int err;

	/* copy firmware image into local area. this also ensures endianness */
	falcon_copy_firmware_image(falcon, firmware);

	/* parse the image data */
	err = falcon_parse_firmware_image(falcon);
	if (err < 0) {
		dev_err(falcon->dev, "failed to parse firmware image\n");
		return err;
	}

	release_firmware(firmware);
	falcon->firmware.firmware = NULL;

	return 0;
}

static int falcon_init(struct falcon *falcon)
{
	falcon->firmware.virt = NULL;

	return 0;
}

/**
 * falcon_exit() - Release resources used by falcon
 *
 * @falcon: Pointer to falcon structure
 *
 * This function releases firmware resources for falcon
 */
void falcon_exit(struct falcon *falcon)
{
	if (falcon->firmware.firmware)
		release_firmware(falcon->firmware.firmware);
}

static int falcon_boot(struct falcon *falcon)
{
	unsigned long offset;
	u32 value;
	int err;

	if (!falcon->firmware.virt)
		return -EINVAL;

	err = readl_poll_timeout(falcon->regs + FALCON_DMACTL, value,
				 (value & (FALCON_DMACTL_IMEM_SCRUBBING |
					   FALCON_DMACTL_DMEM_SCRUBBING)) == 0,
				 10, 10000);
	if (err < 0)
		return err;

	falcon_writel(falcon, 0, FALCON_DMACTL);

	/* setup the address of the binary data so Falcon can access it later */
	falcon_writel(falcon, (falcon->firmware.iova +
			       falcon->firmware.bin_data.offset) >> 8,
		      FALCON_DMATRFBASE);

	/* copy the data segment into Falcon internal memory */
	for (offset = 0; offset < falcon->firmware.data.size; offset += 256)
		falcon_copy_chunk(falcon,
				  falcon->firmware.data.offset + offset,
				  offset, FALCON_MEMORY_DATA);

	/* copy the code segment into Falcon internal memory */
	for (offset = 0; offset < falcon->firmware.code.size; offset += 256)
		falcon_copy_chunk(falcon, falcon->firmware.code.offset + offset,
				  offset, FALCON_MEMORY_IMEM);

	/* enable interface */
	falcon_writel(falcon, FALCON_ITFEN_MTHDEN |
			      FALCON_ITFEN_CTXEN,
		      FALCON_ITFEN);

	/* boot falcon */
	falcon_writel(falcon, 0x00000000, FALCON_BOOTVEC);
	falcon_writel(falcon, FALCON_CPUCTL_STARTCPU, FALCON_CPUCTL);

	err = falcon_wait_idle(falcon);
	if (err < 0) {
		dev_err(falcon->dev, "Falcon boot failed due to timeout\n");
		return err;
	}

	return 0;
}

/* NVDLA Falcon API implementations */

/**
 * ISR for falcon
 */
static irqreturn_t nvdla_flcn_isr(int irq, void *dev_id)
{
    struct platform_device *pdev = dev_id;
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

    if (pdata->flcn_isr)
        pdata->flcn_isr(pdev);

    return IRQ_HANDLED;
}

/**
 * Initialize the interrupt for DLA falcon
 */
int nvdla_flcn_intr_init(struct platform_device *pdev)
{
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
    int ret = 0;

    pdata->irq = platform_get_irq(pdev, 0);
    if (pdata->irq < 0) {
        dev_err(&pdev->dev, "failed to get IRQ\n");
        return -ENXIO;
    }

    ret = devm_request_irq(&pdev->dev, pdata->irq, nvdla_flcn_isr, 0,
                         dev_name(&pdev->dev), pdev);
    if (ret) {
        dev_err(&pdev->dev, "failed to request irq. err %d\n", ret);
        return ret;
    }

    /* keep irq disabled */
    disable_irq(pdata->irq);

    return 0;
}

/**
 * Reload firmware for DLA falcon
 */
int nvdla_flcn_reload_fw(struct platform_device *pdev)
{
    /* TODO: Used by debugfs */
    return -EOPNOTSUPP;
}

/**
 * Initialize DLA falcon
 */
static int nvdla_flcn_init(struct platform_device *pdev,
                         struct nvhost_device_data *pdata)
{
    struct falcon *falcon;

    falcon = devm_kzalloc(&pdev->dev, sizeof(*falcon), GFP_KERNEL);
    if (!falcon)
        return -ENOMEM;

    falcon->dev = &pdev->dev;
    falcon->regs = pdata->aperture[0];

    falcon_init(falcon);

    pdata->falcon_data = falcon;

    return 0;
}

/**
 * Prepare DLA falcon for power off
 */
int nvdla_flcn_prepare_poweroff(struct platform_device *pdev)
{
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);

    if (pdata->flcn_isr)
        disable_irq(pdata->irq);

    return 0;
}

/**
 * Load firmware for DLA falcon
 */
static int nvdla_flcn_load_firmware(struct platform_device *pdev,
                                  struct falcon *falcon,
                                  char *firmware_name)
{
    dma_addr_t iova;
    size_t size;
    void *virt;
    int err;

    if (falcon->firmware.virt)
        return 0;

    err = falcon_read_firmware(falcon, firmware_name);
    if (err < 0)
        return err;

    size = falcon->firmware.size;
    virt = dma_alloc_coherent(&pdev->dev, size, &iova, GFP_KERNEL);
    if (!virt)
        return -ENOMEM;

    falcon->firmware.virt = virt;
    falcon->firmware.iova = iova;

    err = falcon_load_firmware(falcon);
    if (err < 0)
        goto cleanup;

    return 0;

cleanup:
    dma_free_coherent(&pdev->dev, size, virt, iova);

    return err;
}

/**
 * Finalize DLA falcon power on
 */
int nvdla_flcn_finalize_poweron(struct platform_device *pdev)
{
    struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
#ifdef CONFIG_IOMMU_API
    struct iommu_fwspec *spec = dev_iommu_fwspec_get(&pdev->dev);
#endif
    struct falcon *falcon;
    int err;
    u32 value;

    if (!pdata->falcon_data) {
        err = nvdla_flcn_init(pdev, pdata);
        if (err < 0)
            return -ENOMEM;
    }

    falcon = pdata->falcon_data;

    err = nvdla_flcn_load_firmware(pdev, falcon, pdata->firmware_name);
    if (err < 0)
        return err;

#ifdef CONFIG_IOMMU_API
    if (spec) {
        host1x_writel(pdev, pdata->transcfg_addr, pdata->transcfg_val);

        if (spec->num_ids > 0) {
            value = spec->ids[0] & 0xffff;
            host1x_writel(pdev, THI_STREAMID0, value);
            host1x_writel(pdev, THI_STREAMID1, value);
        }
    }
#endif

    err = falcon_boot(falcon);
    if (err < 0)
        return err;

    err = falcon_wait_idle(falcon);
    if (err < 0) {
        dev_err(&pdev->dev, "falcon boot timed out\n");
        return err;
    }

    if (pdata->flcn_isr)
        enable_irq(pdata->irq);

    return 0;
}