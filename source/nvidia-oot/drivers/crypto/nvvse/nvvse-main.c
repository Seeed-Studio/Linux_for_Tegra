// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 */

#include <nvidia/conftest.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <soc/tegra/virt/hv-ivc.h>
#include <linux/iommu.h>
#include <linux/completion.h>
#include <linux/version.h>
#include <linux/dma-buf.h>
#include "nvvse-linux-common.h"
#include "nvvse-linux.h"
#include "core_common_hal.h"
#include "soc_plugin_hal.h"

#define NVVSE_MAX_BUFFER_SIZE			0x1000000
#define NVVSE_MAX_SUPPORTED_BUFLEN		((1U << 24) - 1U)
#define NVVSE_MAX_IVC_Q_PRIORITY				2U

#define TEGRA_IVC_ID_OFFSET				0U
#define TEGRA_SE_ENGINE_DOMAIN_OFFSET			1U
#define TEGRA_SE_PORT_OFFSET				2U
#define TEGRA_SE_ENGINE_NUMBER_OFFSET			3U
#define TEGRA_VIRTUALIZED_INSTANCE_NUMBER		4U
#define TEGRA_IVC_PRIORITY_OFFSET			5U
#define TEGRA_MAX_BUFFER_SIZE				6U
#define TEGRA_CHANNEL_GROUPID_OFFSET			7U
#define TEGRA_GCM_SUPPORTED_FLAG_OFFSET			9U
#define TEGRA_GCM_DEC_BUFFER_SIZE			10U
#define TEGRA_GCM_DEC_MEMPOOL_ID			11U
#define TEGRA_GCM_DEC_MEMPOOL_SIZE			12U
#define TEGRA_IVCCFG_ARRAY_LEN				13U

#define NVVSE_SHA_HASH_BUF_SIZE				1024U
#define RESULT_COMPARE_BUF_SIZE				4U

nvvse_context s_nvvse_ctx;
static nvvse_priv g_nvvse_priv;
static se_info g_se_engine_info[MAX_NUM_IVC];
static NvVseGlobalSHAParams g_sha_params[MAX_NUM_IVC];
static NvVseGlobalHMACParams g_hmac_params[MAX_NUM_IVC];

static struct crypto_dev_to_ivc_map g_crypto_to_ivc_map[MAX_NUM_IVC];
static struct nvvse_mem_serv_ctx g_mem_serv_ctx[MAX_NUM_IVC];
static struct nvvse_node_dma g_node_dma[MAX_NUM_IVC];

enum nvvse_sha_buf_idx {
	SHA_SRC_BUF_IDX,
	SHA_HASH_BUF_IDX,
	HMAC_SHA_COMP_BUF_IDX
};

static int32_t nvvse_get_dma_mapped_membuf_count(uint32_t node_id,
	uint32_t *mapped_membuf_count)
{
	if (node_id >= MAX_NUM_IVC) {
		NVVSE_ERR("%s node_id is invalid\n", __func__);
		return -EINVAL;
	}
	if (!mapped_membuf_count) {
		NVVSE_ERR("%s mapped_membuf_count is null\n", __func__);
		return -EINVAL;
	}
	*mapped_membuf_count = g_node_dma[node_id].mapped_membuf_count;
	return 0;
}

static int32_t nvvse_set_dma_mapped_membuf_count(uint32_t node_id,
	uint32_t mapped_membuf_count)
{
	if (node_id >= MAX_NUM_IVC) {
		NVVSE_ERR("%s node_id is invalid\n", __func__);
		return -EINVAL;
	}
	g_node_dma[node_id].mapped_membuf_count = mapped_membuf_count;
	return 0;
}

static int32_t nvvse_get_dma_membuf_ctx(uint32_t node_id, uint32_t index,
	struct nvvse_membuf_ctx **membuf_ctx)
{
	if (node_id >= MAX_NUM_IVC) {
		NVVSE_ERR("%s node_id is invalid\n", __func__);
		return -EINVAL;
	}
	if (index >= MAX_ZERO_COPY_BUFS) {
		NVVSE_ERR("%s index is invalid\n", __func__);
		return -EINVAL;
	}
	if (!membuf_ctx) {
		NVVSE_ERR("%s membuf_ctx is null\n", __func__);
		return -EINVAL;
	}
	*membuf_ctx = &g_node_dma[node_id].membuf_ctx[index];
	return 0;
}

NvBoolVar get_g_terminate(void)
{
	return NvBoolFalse;
}

nvvse_priv *get_nvvse_context(void)
{
	return &g_nvvse_priv;
}

struct crypto_dev_to_ivc_map *nvvse_get_crypto_to_ivc_map(void)
{
	return &g_crypto_to_ivc_map[0];
}

static int nvvse_validate_membuf_common(struct nvvse_dev_membuf_ctx *ctx)
{
	struct nvvse_dev *se_dev = NULL;
	int err = 0;

	if (!ctx) {
		NVVSE_ERR("%s ctx is null\n", __func__);
		err = -EINVAL;
		goto exit;
	}
	if (ctx->node_id >= MAX_NUM_IVC) {
		NVVSE_ERR("%s node_id is invalid\n", __func__);
		err = -ENODEV;
		goto exit;
	}
	se_dev = g_crypto_to_ivc_map[ctx->node_id].se_dev;
	if (ctx->fd < 0) {
		dev_err(se_dev->dev, "%s fd is invalid\n", __func__);
		err = -EINVAL;
		goto exit;
	}
exit:
	return err;
}

int nvvse_map_membuf(struct nvvse_dev_membuf_ctx *ctx)
{
	struct nvvse_membuf_ctx *membuf_ctx = NULL;
	MemoryParams_t *mem_params = NULL;
	struct MemoryAccess mem_access;
	uint32_t i;
	int err = 0;
	MemServStatus_t mem_serv_status;
	uint32_t mapped_membuf_count;

	err = nvvse_validate_membuf_common(ctx);
	if (err != 0)
		return err;

	err = nvvse_get_dma_mapped_membuf_count(ctx->node_id,
			&mapped_membuf_count);

	if (err != 0)
		return err;

	if (mapped_membuf_count >= MAX_ZERO_COPY_BUFS) {
		NVVSE_ERR("%s no free membuf_ctx\n", __func__);
		return -ENOMEM;
	}

	for (i = 0U; i < MAX_ZERO_COPY_BUFS; i++) {
		err = nvvse_get_dma_membuf_ctx(ctx->node_id, i, &membuf_ctx);
		if (err != 0)
			return err;
		if (membuf_ctx->fd == -1)
			break;
	}

	if (i == MAX_ZERO_COPY_BUFS) {
		NVVSE_ERR("%s no free membuf_ctx\n", __func__);
		return -ENOMEM;
	}

	mem_params = &g_mem_serv_ctx[ctx->node_id].se_mem_params[i];
	mem_access.fd = ctx->fd;

	mem_serv_status = mem_serv_hal_map(&mem_access, mem_params);
	if (mem_serv_status != MEM_SERV_SUCCESS) {
		NVVSE_ERR("%s mem_serv_hal_map failed\n", __func__);
		return -EFAULT;
	}
	membuf_ctx->dmabuf = mem_access.dmabuf;
	membuf_ctx->attach = mem_access.attach;
	membuf_ctx->fd = ctx->fd;
	ctx->iova = mem_access.addr;

	err = nvvse_set_dma_mapped_membuf_count(ctx->node_id, mapped_membuf_count + 1U);
	if (err != 0) {
		NVVSE_ERR("%s nvvse_set_dma_mapped_membuf_count failed\n", __func__);
		return err;
	}
	return 0;
}

void nvvse_unmap_all_membufs(uint32_t node_id)
{
	struct nvvse_membuf_ctx *membuf_ctx = NULL;
	MemoryParams_t *mem_params = NULL;
	struct MemoryAccess mem_access;
	uint32_t i;
	MemServStatus_t mem_serv_status;
	int err = 0;

	if (node_id >= MAX_NUM_IVC) {
		NVVSE_ERR("%s node_id is invalid\n", __func__);
		return;
	}

	for (i = 0U; i < MAX_ZERO_COPY_BUFS; i++) {
		err = nvvse_get_dma_membuf_ctx(node_id, i, &membuf_ctx);
		if (err != 0)
			return;

		if (membuf_ctx->fd == -1)
			continue;

		mem_params = &g_mem_serv_ctx[node_id].se_mem_params[i];
		mem_access.dmabuf = membuf_ctx->dmabuf;
		mem_access.attach = membuf_ctx->attach;

		mem_serv_status = mem_serv_hal_unmap(&mem_access, mem_params);
		if (mem_serv_status != MEM_SERV_SUCCESS) {
			NVVSE_ERR("%s mem_serv_hal_unmap failed\n", __func__);
			return;
		}
		membuf_ctx->fd = -1;
	}
	(void)nvvse_set_dma_mapped_membuf_count(node_id, 0U);
}

int nvvse_unmap_membuf(struct nvvse_dev_membuf_ctx *ctx)
{
	struct MemoryAccess mem_access;
	MemoryParams_t *mem_params = NULL;
	struct nvvse_membuf_ctx *membuf_ctx = NULL;
	uint32_t i;
	int err = 0;
	MemServStatus_t mem_serv_status;
	uint32_t mapped_membuf_count;

	err = nvvse_validate_membuf_common(ctx);
	if (err != 0)
		return err;

	err = nvvse_get_dma_mapped_membuf_count(ctx->node_id,
			&mapped_membuf_count);
	if (err != 0)
		return err;

	if (mapped_membuf_count == 0U) {
		NVVSE_ERR("%s no mapped membuf to free\n", __func__);
		return -EINVAL;
	}

	for (i = 0U; i < MAX_ZERO_COPY_BUFS; i++) {
		err = nvvse_get_dma_membuf_ctx(ctx->node_id, i, &membuf_ctx);
		if (err != 0)
			return err;

		if (membuf_ctx->fd == ctx->fd)
			break;
	}

	if (i == MAX_ZERO_COPY_BUFS) {
		NVVSE_ERR("%s fd not found\n", __func__);
		return -EINVAL;
	}

	mem_params = &g_mem_serv_ctx[ctx->node_id].se_mem_params[i];
	mem_access.dmabuf = membuf_ctx->dmabuf;
	mem_access.attach = membuf_ctx->attach;

	mem_serv_status = mem_serv_hal_unmap(&mem_access, mem_params);
	if (mem_serv_status != MEM_SERV_SUCCESS) {
		NVVSE_ERR("%s mem_serv_hal_unmap failed\n", __func__);
		return -EFAULT;
	}
	membuf_ctx->fd = -1;

	err = nvvse_set_dma_mapped_membuf_count(ctx->node_id,
			(mapped_membuf_count - 1U));
	if (err != 0)
		return -EINVAL;
	return 0;
}

static int nvvse_validate_ivc_node_id(uint32_t se_comm_id, uint32_t instance_id,
	uint32_t se_engine_domain, uint32_t se_engine_domain_instanceId,
	unsigned int se_port)
{
	uint32_t cnt;

	for (cnt = 0; cnt < MAX_NUM_IVC; cnt++) {
		if (g_crypto_to_ivc_map[cnt].node_in_use != true)
			break;

		if (g_crypto_to_ivc_map[cnt].se_comm_id == se_comm_id) {
			NVVSE_ERR("%s: ivc id %u is already used\n", __func__, se_comm_id);
			return -EINVAL;
		}

		if ((g_crypto_to_ivc_map[cnt].se_port == se_port)
				&& (g_crypto_to_ivc_map[cnt].virtualized_instanceId == instance_id)
				&& (g_crypto_to_ivc_map[cnt].se_engine_domain == se_engine_domain)
				&& (g_crypto_to_ivc_map[cnt].se_engine_domain_instanceId ==
					se_engine_domain_instanceId)) {
			NVVSE_ERR("%s: instance id %u is already used for engine id %d\n",
					__func__, instance_id, se_port);
			return -EINVAL;
		}
	}

	return 0;
}

static int nvvse_allocate_se_dma_bufs(struct nvvse_mem_serv_ctx *mem_serv_ctx,
		struct device *se_dev,
		struct crypto_dev_to_ivc_map *ivc_map)
{
	int32_t err = -ENOMEM;
	uint32_t buf_sizes[MAX_SE_DMA_BUFS] = {0U};
	uint32_t i;
	struct MemServDeviceAttributes mem_serv_dev_attr;

	if (!mem_serv_ctx) {
		NVVSE_ERR("%s mem_serv_ctx is null\n", __func__);
		err = -EINVAL;
		goto exit;
	}

	if (!se_dev) {
		NVVSE_ERR("%s se_dev is null\n", __func__);
		err = -EINVAL;
		goto exit;
	}

	if (!ivc_map) {
		NVVSE_ERR("%s ivc_map is null\n", __func__);
		err = -EINVAL;
		goto exit;
	}

	if (ivc_map->se_port == NVVSE_PORT_SHA) {
		/*
		 * For SHA algs, the worst case requirement for SHAKE128/SHAKE256:
		 * 1. plaintext buffer(requires up to max limit specified in DT)
		 * 2. digest buffer(support a maximum digest size of 1024 bytes for SHAKE)
		 * 3. match code/comp buffer(requires 4 bytes)
		 */
		buf_sizes[SHA_SRC_BUF_IDX] = ivc_map->max_buffer_size;
		buf_sizes[SHA_HASH_BUF_IDX] = NVVSE_SHA_HASH_BUF_SIZE;
		buf_sizes[HMAC_SHA_COMP_BUF_IDX] = RESULT_COMPARE_BUF_SIZE;
	} else {
		err = 0;
		goto exit;
	}

	mem_serv_dev_attr.dev = se_dev;
	for (i = 0; i < MAX_SE_DMA_BUFS; i++) {
		err = mem_serv_hal_mem_context_init(&mem_serv_ctx->se_mem_params[i].mem_context,
				&mem_serv_dev_attr);
		if (err) {
			dev_err(se_dev, "Failed to initialize mem_serv_hal\n");
			goto exit;
		}

		if (buf_sizes[i] == 0U)
			continue;

		mem_serv_ctx->se_mem_params[i].size = buf_sizes[i];
		err = mem_serv_hal_alloc(&mem_serv_ctx->se_mem_params[i]);
		if (err) {
			dev_err(se_dev, "Failed to allocate memory\n");
			goto exit;
		}
	}

	err = 0;

exit:
	return err;
}

static const struct of_device_id nvvse_of_match[] = {
	{ .compatible = "nvvse-plug-arch"},
	{},
};
MODULE_DEVICE_TABLE(of, nvvse_of_match);

static int nvvse_probe(struct platform_device *pdev)
{
	struct nvvse_dev *se_dev = NULL;
	struct crypto_dev_to_ivc_map *crypto_dev = NULL;
	se_info *se_engine_info = NULL;
	NvVseGlobalSHAParams *sha_params = NULL;
	NvVseGlobalHMACParams *hmac_params = NULL;
	struct device_node *np;
	int err = 0;
	uint32_t se_comm_id;
	uint32_t se_port;
	uint32_t se_engine_domain;
	uint32_t se_engine_domain_instanceId;
	uint32_t instance_id;
	static uint32_t s_node_id;
	uint32_t ivc_cnt, cnt;
	bool has_zero_copy_prop;
	struct SeJobCompletionDeviceAttributes se_job_completion_dev_attr;
	struct CommsDeviceAttribute comms_dev_attr;
	Soc_Plugin_Hal_Status soc_plugin_hal_status;
	se_job_completion_device_handle se_job_completion_dev_hdl;
	se_job_completion_wait_handle se_job_completion_wait_hdl;
	soc_plugin_init_param_os soc_plugin_init_param;

	dev_info(&pdev->dev, "probe start\n");
	has_zero_copy_prop = of_property_read_bool(pdev->dev.of_node, "#zero-copy");

	se_dev = devm_kzalloc(&pdev->dev, sizeof(struct nvvse_dev), GFP_KERNEL);
	if (!se_dev) {
		NVVSE_ERR("%s devm_kzalloc failed\n", __func__);
		err = -ENOMEM;
		goto exit;
	}

	soc_plugin_init_param.nvdt_node = pdev->dev.of_node;
	soc_plugin_hal_status = soc_plugin_hal_init(soc_plugin_init_param);
	if (soc_plugin_hal_status != SOC_PLUGIN_HAL_SUCCESS) {
		NVVSE_ERR("%s soc_plugin_hal_init failed\n", __func__);
		err = -EINVAL;
		goto exit;
	}

	se_job_completion_dev_attr.dev = &pdev->dev;
	err = se_job_completion_init(
			(SeJobCompletionDeviceAttributesOS *)&se_job_completion_dev_attr,
			&se_job_completion_dev_hdl);
	if (err != SE_JOB_COMPLETION_STATUS_SUCCESS) {
		dev_err(&pdev->dev, "se_job_completion_init failed with err: %d\n", err);
		err = -ENODEV;
		goto exit;
	}

	err = se_job_completion_waiter_allocate(se_job_completion_dev_hdl,
			&se_job_completion_wait_hdl);
	if (err != SE_JOB_COMPLETION_STATUS_SUCCESS) {
		dev_err(&pdev->dev,
			"se_job_completion_waiter_allocate failed with err: %d\n", err);
		err = -ENODEV;
		goto exit;
	}

	np = pdev->dev.of_node;
	se_dev->crypto_to_ivc_map = g_crypto_to_ivc_map;
	se_dev->dev = &pdev->dev;
	err = of_property_read_u32(np, "se_engine_domain", &se_engine_domain);
	if (err) {
		dev_err(&pdev->dev, "se_engine_domain property not present\n");
		err = -ENODEV;
		goto exit;
	}
	err = of_property_read_u32(np, "se_port", &se_port);
	if (err) {
		dev_err(&pdev->dev, "se_port property not present\n");
		err = -ENODEV;
		goto exit;
	}

	if (se_port != NVVSE_PORT_SHA) {
		dev_err(&pdev->dev, "se_port other than SHA is not supported\n");
		err = -EINVAL;
		goto exit;
	}

	err = of_property_read_u32(np, "se_engine_domain_instanceId",
			&se_engine_domain_instanceId);
	if (err) {
		dev_err(&pdev->dev, "se_engine_domain_instanceId property not present\n");
		err = -ENODEV;
		goto exit;
	}

	/* read ivccfg from dts */
	err = of_property_read_u32_index(np, "nvidia,ivccfg_cnt", 0, &ivc_cnt);
	if (err) {
		dev_err(se_dev->dev, "Error: failed to read ivc_cnt. err %u\n", err);
		err = -ENODEV;
		goto exit;
	}
	if (ivc_cnt > MAX_NUM_IVC) {
		dev_err(se_dev->dev, "%s Error: Unsupported IVC queue count %u\n", __func__,
				ivc_cnt);
		err = -EINVAL;
		goto exit;
	}
	if (s_node_id > (MAX_NUM_IVC - ivc_cnt)) {
		dev_err(se_dev->dev,
			"%s Error: IVC queue count exceeds maximum supported value of %u\n",
			__func__, MAX_NUM_IVC);
		err = -EINVAL;
		goto exit;
	}

	if (g_nvvse_priv.se_engine_info == NULL) {
		g_nvvse_priv.se_engine_info = g_se_engine_info;
		s_nvvse_ctx.hnvvse = &g_nvvse_priv;

		err = nvvse_dev_create_info_node();
		if (err) {
			dev_err(se_dev->dev, "%s failed to create info node\n", __func__);
			goto exit;
		}
	}

	for (cnt = 0; cnt < ivc_cnt; cnt++) {
		err = of_property_read_u32_index(np, "nvidia,ivccfg",
				(cnt * TEGRA_IVCCFG_ARRAY_LEN
						 + TEGRA_VIRTUALIZED_INSTANCE_NUMBER),
				&instance_id);
		if (err) {
			dev_err(se_dev->dev, "%s Error: failed to read instance id. err %d\n",
			__func__,  err);
			err = -ENODEV;
			goto exit;
		}
		crypto_dev = &g_crypto_to_ivc_map[s_node_id];
		se_engine_info = &g_se_engine_info[s_node_id];
		sha_params = &g_sha_params[s_node_id];
		hmac_params = &g_hmac_params[s_node_id];

		se_engine_info->sha_params = sha_params;
		se_engine_info->hmac_params = hmac_params;

		err = of_property_read_u32_index(np, "nvidia,ivccfg",
				(cnt * TEGRA_IVCCFG_ARRAY_LEN + TEGRA_IVC_ID_OFFSET),
				&se_comm_id);
		if (err) {
			dev_err(se_dev->dev, "Error: failed to read se_comm_id. err %d\n", err);
			err = -ENODEV;
			goto exit;
		}
		err = nvvse_validate_ivc_node_id(se_comm_id, instance_id,
			se_engine_domain, se_engine_domain_instanceId, se_port);
		if (err) {
			err = -ENODEV;
			goto exit;
		}
		crypto_dev->se_comm_id = se_comm_id;
		crypto_dev->node_id = s_node_id;
		crypto_dev->virtualized_instanceId = instance_id;
		crypto_dev->se_dev = se_dev;
		crypto_dev->node_in_use = true;

		err = of_property_read_u32_index(np, "nvidia,ivccfg",
				(cnt * TEGRA_IVCCFG_ARRAY_LEN + TEGRA_SE_PORT_OFFSET),
				&crypto_dev->se_port);
		if (err) {
			dev_err(se_dev->dev, "Error: failed to read se_port. err %d\n", err);
			err = -ENODEV;
			goto exit;
		}
		if (se_port != crypto_dev->se_port) {
			dev_err(se_dev->dev, "Error: se engine mismatch for se_comm_id %u\n",
			crypto_dev->se_comm_id);
			err = -ENODEV;
			goto exit;
		}
		err = of_property_read_u32_index(np, "nvidia,ivccfg",
				(cnt * TEGRA_IVCCFG_ARRAY_LEN + TEGRA_SE_ENGINE_DOMAIN_OFFSET),
				&crypto_dev->se_engine_domain);
		if (err) {
			dev_err(se_dev->dev, "Error: invalid queue se_engine_domain. err %d\n",
				err);
			err = -ENODEV;
			goto exit;
		}
		err = of_property_read_u32_index(np, "nvidia,ivccfg",
				(cnt * TEGRA_IVCCFG_ARRAY_LEN + TEGRA_SE_ENGINE_NUMBER_OFFSET),
				&crypto_dev->se_engine_domain_instanceId);
		if (err) {
			dev_err(se_dev->dev,
			"Error: invalid queue se_engine_domain_instanceId. err %d\n", err);
			err = -ENODEV;
			goto exit;
		}
		err = of_property_read_u32_index(np, "nvidia,ivccfg",
				(cnt * TEGRA_IVCCFG_ARRAY_LEN + TEGRA_IVC_PRIORITY_OFFSET),
				&crypto_dev->priority);
		if (err || crypto_dev->priority > NVVSE_MAX_IVC_Q_PRIORITY) {
			dev_err(se_dev->dev, "Error: invalid queue priority. err %d\n", err);
			err = -ENODEV;
			goto exit;
		}
		err = of_property_read_u32_index(np, "nvidia,ivccfg",
				cnt * TEGRA_IVCCFG_ARRAY_LEN + TEGRA_MAX_BUFFER_SIZE,
				&crypto_dev->max_buffer_size);
		if (err) {
			dev_err(se_dev->dev, "Error: invalid max buffer size. err %d\n", err);
			err = -ENODEV;
			goto exit;
		}
		if (crypto_dev->max_buffer_size >= NVVSE_MAX_BUFFER_SIZE) {
			dev_err(se_dev->dev, "Error: max buffer size must be less than %u\n",
			NVVSE_MAX_BUFFER_SIZE);
			err = -EINVAL;
			goto exit;
		}

		if (has_zero_copy_prop) {
			if (crypto_dev->max_buffer_size > 0U) {
				dev_err(se_dev->dev,
				"Error: max buffer size must be 0 if 0-copy is supported\n");
				err = -ENODEV;
				goto exit;
			}
			crypto_dev->is_zero_copy_node = true;
		} else {
			crypto_dev->is_zero_copy_node = false;
		}
		err = of_property_read_u32_index(np, "nvidia,ivccfg",
				(cnt * TEGRA_IVCCFG_ARRAY_LEN + TEGRA_CHANNEL_GROUPID_OFFSET),
				&crypto_dev->gid);
		if (err) {
			dev_err(se_dev->dev, "Error: invalid channel group id. err %d\n", err);
			err = -ENODEV;
			goto exit;
		}
		err = of_property_read_u32_index(np, "nvidia,ivccfg",
				(cnt * TEGRA_IVCCFG_ARRAY_LEN + TEGRA_GCM_SUPPORTED_FLAG_OFFSET),
				&crypto_dev->gcm_dec_supported);
		if (err || crypto_dev->gcm_dec_supported > GCM_DEC_OP_SUPPORTED) {
			dev_err(se_dev->dev, "Error: invalid gcm decrypt supported flag. err %d\n",
					err);
			err = -ENODEV;
			goto exit;
		}
		err = of_property_read_u32_index(np, "nvidia,ivccfg",
				(cnt * TEGRA_IVCCFG_ARRAY_LEN + TEGRA_GCM_DEC_BUFFER_SIZE),
				&crypto_dev->soc_params[0]);
		if (err || (crypto_dev->gcm_dec_supported != GCM_DEC_OP_SUPPORTED &&
				crypto_dev->soc_params[0] != 0)) {
			dev_err(se_dev->dev,
				"Error: invalid gcm decrypt buffer size. err %d\n", err);
			err = -ENODEV;
			goto exit;
		}
		if (crypto_dev->soc_params[0] >= NVVSE_MAX_BUFFER_SIZE) {
			dev_err(se_dev->dev,
				"Error: gcm decrypt buffer size must be less than %u\n",
				NVVSE_MAX_BUFFER_SIZE);
			err = -EINVAL;
			goto exit;
		}

		dev_info(se_dev->dev, "Virtual SE channel number: %d", se_comm_id);
		comms_dev_attr.comms_id = se_comm_id;
		comms_dev_attr.dev = &pdev->dev;
		err = comms_lib_init_channel(
			(CommsDeviceAttributeOS)&comms_dev_attr, &se_engine_info->comms_handle);
		if (err) {
			dev_err(se_dev->dev, "Failed to initialize comms library channel\n");
			goto exit;
		}

		err = nvvse_allocate_se_dma_bufs(&g_mem_serv_ctx[s_node_id], se_dev->dev,
				crypto_dev);
		if (err) {
			dev_err(se_dev->dev, "%s returned error %d for engine id %d, node id %d\n",
						 __func__, err, se_port, crypto_dev->node_id);
			goto exit;
		}
		se_engine_info->se_comm_id = se_comm_id;
		se_engine_info->se_port = se_port;
		se_engine_info->instanceId = instance_id;
		se_engine_info->se_job_wait_ctxt = se_job_completion_wait_hdl;
		se_engine_info->mapped_buffer_size = crypto_dev->max_buffer_size;

		if (crypto_dev->is_zero_copy_node)
			sha_params->uAllocatedInputBuffersize = NVVSE_MAX_BUFFER_SIZE;
		else
			sha_params->uAllocatedInputBuffersize = se_engine_info->mapped_buffer_size;

		sha_params->input_mem_params =
				g_mem_serv_ctx[s_node_id].se_mem_params[SHA_SRC_BUF_IDX];
		sha_params->digest_mem_params =
				g_mem_serv_ctx[s_node_id].se_mem_params[SHA_HASH_BUF_IDX];
		sha_params->sha_op_in_progress = NvBoolFalse;
		sha_params->sha_op_counter = 0;

		hmac_params->uAllocatedInputBuffersize = se_engine_info->mapped_buffer_size;
		hmac_params->input_mem_params =
				g_mem_serv_ctx[s_node_id].se_mem_params[SHA_SRC_BUF_IDX];
		hmac_params->digest_mem_params =
				g_mem_serv_ctx[s_node_id].se_mem_params[SHA_HASH_BUF_IDX];
		hmac_params->result_mem_params =
				g_mem_serv_ctx[s_node_id].se_mem_params[HMAC_SHA_COMP_BUF_IDX];
		hmac_params->sha_op_in_progress = NvBoolFalse;
		hmac_params->sha_op_counter = 0;

		soc_plugin_hal_status = soc_plugin_hal_init_sha_param(
				&se_engine_info->sha_init_param);
		if (soc_plugin_hal_status != SOC_PLUGIN_HAL_SUCCESS) {
			NVVSE_ERR("Failed to initialize SHA init param, se comm id %u\n",
					se_comm_id);
			err = -EINVAL;
			goto exit;
		}

		soc_plugin_hal_status = soc_plugin_hal_get_sha_capabilities(
				se_engine_info->sha_init_param);
		if (soc_plugin_hal_status != SOC_PLUGIN_HAL_SUCCESS) {
			NVVSE_ERR("Failed to get SHA capabilities, se comm id %u\n", se_comm_id);
			err = -EINVAL;
			goto exit;
		}

		soc_plugin_hal_status = soc_plugin_hal_allocate_sha_msg_buffer(
				&se_engine_info->sha_req_buf,
				(size_t *)&se_engine_info->sha_req_buf_len);
		if (soc_plugin_hal_status != SOC_PLUGIN_HAL_SUCCESS) {
			NVVSE_ERR("Failed to allocate SHA req msg buffer, se comm id %u\n",
					se_comm_id);
			err = -EINVAL;
			goto exit;
		}

		soc_plugin_hal_status = soc_plugin_hal_init_hmac_sha_param(
				&se_engine_info->hmac_sha_init_param);
		if (soc_plugin_hal_status != SOC_PLUGIN_HAL_SUCCESS) {
			NVVSE_ERR("Failed to initialize HMAC SHA init param, se comm id %u\n",
					se_comm_id);
			err = -EINVAL;
			goto exit;
		}

		soc_plugin_hal_status = soc_plugin_hal_get_hmac_sha_capabilities(
				se_engine_info->hmac_sha_init_param);
		if (soc_plugin_hal_status != SOC_PLUGIN_HAL_SUCCESS) {
			NVVSE_ERR("Failed to get HMAC SHA capabilities, se comm id %u\n",
					se_comm_id);
			err = -EINVAL;
			goto exit;
		}

		soc_plugin_hal_status = soc_plugin_hal_allocate_hmac_sha_msg_buffer(
				&se_engine_info->hmac_sha_req_buf,
				(size_t *)&se_engine_info->hmac_sha_req_buf_len);
		if (soc_plugin_hal_status != SOC_PLUGIN_HAL_SUCCESS) {
			NVVSE_ERR("Failed to allocate HMAC SHA req msg buffer, se comm id %u\n",
					se_comm_id);
			err = -EINVAL;
			goto exit;
		}

		soc_plugin_hal_status = soc_plugin_hal_allocate_resp_msg_buffer(
				&se_engine_info->resp_buf,
				(size_t *)&se_engine_info->resp_buf_len);
		if (soc_plugin_hal_status != SOC_PLUGIN_HAL_SUCCESS) {
			NVVSE_ERR("Failed to allocate SHA resp msg buffer, se comm id %u\n",
					se_comm_id);
			err = -EINVAL;
			goto exit;
		}

		err = nvvse_dev_create_node(se_comm_id, s_node_id);
		if (err) {
			dev_err(se_dev->dev,
				"%s dev node creation failed for se_comm_id %u, node_id %u\n",
				__func__, se_comm_id, s_node_id);
			goto exit;
		}
		s_node_id++;
	}

	g_nvvse_priv.total_ivc_entries = s_node_id;

	se_dev->se_port = se_port;
	se_dev->se_engine_domain_instanceId = se_engine_domain_instanceId;
	/* Set Engine suspended state to false*/
	atomic_set(&se_dev->se_suspended, 0);
	platform_set_drvdata(pdev, se_dev);
	dev_info(&pdev->dev, "probe success\n");
	return 0;
exit:
	return err;
}


static void nvvse_shutdown(struct nvvse_dev *se_dev)
{
	uint32_t cnt;

	/* Set engine to suspend state */
	atomic_set(&se_dev->se_suspended, 1);

	for (cnt = 0; cnt < MAX_NUM_IVC; cnt++) {
		if ((g_crypto_to_ivc_map[cnt].se_port == se_dev->se_port) &&
			(g_crypto_to_ivc_map[cnt].se_engine_domain_instanceId ==
			se_dev->se_engine_domain_instanceId)) {
			/* TODO: Wait for active operation completion */
		}
	}
}

static void nvvse_shutdown_wrapper(struct platform_device *pdev)
{
	struct nvvse_dev *se_dev = platform_get_drvdata(pdev);

	nvvse_shutdown(se_dev);
}

static int nvvse_remove(struct platform_device *pdev)
{
	uint32_t i;
	static bool is_info_node_removed;

	if (!is_info_node_removed) {
		nvvse_dev_remove_info_node();
		is_info_node_removed = true;
	}

	for (i = 0; i < MAX_NUM_IVC; i++) {
		if (!g_crypto_to_ivc_map[i].node_in_use)
			continue;

		nvvse_dev_remove_node(g_crypto_to_ivc_map[i].node_id);

		if (g_crypto_to_ivc_map[i].se_dev->dev == &pdev->dev)
			comms_lib_deinit_channel(g_se_engine_info[i].comms_handle);
	}

	return 0;
}

#if defined(CONFIG_PM)
static int nvvse_suspend(struct device *dev)
{
	uint32_t i;

	for (i = 0U; i < MAX_NUM_IVC; i++) {
		if ((g_crypto_to_ivc_map[i].node_in_use)
			&& (g_crypto_to_ivc_map[i].se_dev->dev == dev))
			break;
	}

	/* Keep engine in suspended state */
	if (i >= MAX_NUM_IVC) {
		dev_err(dev, "Failed to find se_dev for dev %s\n", dev->kobj.name);
		return -ENODEV;
	}
	nvvse_shutdown(g_crypto_to_ivc_map[i].se_dev);
	return 0;
}

static int nvvse_resume(struct device *dev)
{
	int i;

	for (i = 0U; i < MAX_NUM_IVC; i++) {
		if ((g_crypto_to_ivc_map[i].node_in_use)
			&& (g_crypto_to_ivc_map[i].se_dev->dev == dev)) {
			break;
		}
	}

	/* Set engine to suspend state to 1 to make it as false */
	if (i >= MAX_NUM_IVC) {
		NVVSE_ERR("%s(): Failed to find se_dev for dev\n", __func__);
		return -ENODEV;
	}
	atomic_set(&g_crypto_to_ivc_map[i].se_dev->se_suspended, 0);

	return 0;
}

static const struct dev_pm_ops nvvse_pm_ops = {
	.suspend = nvvse_suspend,
	.resume = nvvse_resume,
};
#endif /* CONFIG_PM */

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void nvvse_remove_wrapper(struct platform_device *pdev)
{
	nvvse_remove(pdev);
}
#else
static int nvvse_remove_wrapper(struct platform_device *pdev)
{
	return nvvse_remove(pdev);
}
#endif

static struct platform_driver nvvse_driver = {
	.probe = nvvse_probe,
	.remove = nvvse_remove_wrapper,
	.shutdown = nvvse_shutdown_wrapper,
	.driver = {
		.name = "nvvse",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(nvvse_of_match),
#if defined(CONFIG_PM)
		.pm = &nvvse_pm_ops,
#endif
	},
};

static int __init nvvse_module_init(void)
{
	uint32_t i, j;
	struct nvvse_membuf_ctx *membuf_ctx;
	int err = 0;

	for (i = 0U; i < MAX_NUM_IVC; i++) {
		g_crypto_to_ivc_map[i].node_in_use = false;
		for (j = 0; j < MAX_ZERO_COPY_BUFS; j++) {
			err = nvvse_get_dma_membuf_ctx(i, j, &membuf_ctx);
			if (err != 0)
				return err;

			membuf_ctx->fd = -1;
		}
	}

	return platform_driver_register(&nvvse_driver);
}

static void __exit nvvse_module_exit(void)
{
	platform_driver_unregister(&nvvse_driver);
}

module_init(nvvse_module_init);
module_exit(nvvse_module_exit);

#if defined(NV_MODULE_IMPORT_NS_CALLS_STRINGIFY)
MODULE_IMPORT_NS(DMA_BUF);
#else
MODULE_IMPORT_NS("DMA_BUF");
#endif

MODULE_AUTHOR("NVIDIA Corporation");
MODULE_DESCRIPTION("Virtual Security Engine driver");
MODULE_LICENSE("GPL");
