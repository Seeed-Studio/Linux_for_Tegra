// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "soc/tegra/camrtc-dbg-messages.h"

#include <linux/debugfs.h>
#if IS_ENABLED(CONFIG_INTERCONNECT)
#include <linux/interconnect.h>
#include <dt-bindings/interconnect/tegra_icc_id.h>
#endif
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/clock.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tegra-camera-rtcpu.h>
#include <soc/tegra/ivc_ext.h>
#include <linux/tegra-ivc-bus.h>
#include <linux/platform/tegra/common.h>
#include <soc/tegra/fuse.h>
#include <linux/tegra-rtcpu-trace.h>

#define CAMRTC_TEST_CAM_DEVICES 5

struct camrtc_test_device {
	/* device handle */
	struct device *dev;
	/* device iova for the memory in context */
	dma_addr_t dev_iova;
};

struct camrtc_test_mem {
	/* access id in memory array */
	u32 index;
	/* occupied memory size */
	size_t used;
	/* total size */
	size_t size;
	/* CPU address */
	void *ptr;
	/* Physical base address, offsets valid for first page only */
	phys_addr_t phys_addr;
	/* base iova for device used for allocation */
	dma_addr_t iova;
	/* device index */
	u32 dev_index;
	/* metadata for all the devices using this memory */
	struct camrtc_test_device devices[CAMRTC_TEST_CAM_DEVICES];
};

struct camrtc_falcon_coverage {
	u8 id;
	bool enabled;
	struct camrtc_test_mem mem;
	struct sg_table sgt;
	u64 falc_iova;
	struct tegra_ivc_channel *ch;
	struct device *mem_dev;
	struct device *falcon_dev;
};

struct camrtc_debug {
	struct tegra_ivc_channel *channel;
	struct mutex mutex;
	struct dentry *root;
	wait_queue_head_t waitq;
	struct {
		u32 completion_timeout;
		u32 mods_case;
		u32 mods_loops;
		u32 mods_dma_channels;
		char *test_case;
		size_t test_case_size;
		u32 test_timeout;
		u32 test_bw;
	} parameters;
	struct camrtc_falcon_coverage vi_falc_coverage;
	struct camrtc_falcon_coverage isp_falc_coverage;
	struct icc_path *icc_path;
	struct camrtc_test_mem mem[CAMRTC_DBG_NUM_MEM_TEST_MEM];
	struct device *mem_devices[CAMRTC_TEST_CAM_DEVICES];
	struct ast_regset {
		struct debugfs_regset32 common, region[8];
	} ast_regsets[2];
};

#define NV(x) "nvidia," #x
#define FALCON_COVERAGE_MEM_SIZE (1024 * 128) /* 128kB */

struct camrtc_dbgfs_rmem {
	/* reserved memory base address */
	phys_addr_t     base_address;
	 /* reserved memory size */
	unsigned long   total_size;
	/* if reserved memory enabled */
	bool enabled;
	/* memory contexts */
	struct camrtc_rmem_ctx {
		phys_addr_t address;
		unsigned long size;
	} mem_ctxs[CAMRTC_DBG_NUM_MEM_TEST_MEM];
};

static struct camrtc_dbgfs_rmem _camdbg_rmem;

/**
 * @brief Initializes the camera debug reserved memory
 *
 * This function initializes the camera debug reserved memory area by dividing it into
 * equal-sized memory contexts. It performs the following operations:
 * - Stores the base address and total size of the reserved memory
 * - Divides the memory into CAMRTC_DBG_NUM_MEM_TEST_MEM equal contexts
 * - Sets up each memory context with its address and size
 * - Uses @ref __builtin_uaddll_overflow to safely compute addresses
 * - Enables the reserved memory flag
 *
 * @param[in] rmem  Pointer to the reserved memory descriptor
 *                  Valid value: non-NULL
 *
 * @retval 0  Operation successful
 */
static int __init camrtc_dbgfs_rmem_init(struct reserved_mem *rmem)
{
	int i;
	phys_addr_t curr_address = rmem->base;
	unsigned long ctx_size = rmem->size/CAMRTC_DBG_NUM_MEM_TEST_MEM;

	_camdbg_rmem.base_address = rmem->base;
	_camdbg_rmem.total_size = rmem->size;

	for (i = 0; i < CAMRTC_DBG_NUM_MEM_TEST_MEM; i++) {
		_camdbg_rmem.mem_ctxs[i].address = curr_address;
		_camdbg_rmem.mem_ctxs[i].size = ctx_size;
		(void)__builtin_uaddll_overflow(curr_address, ctx_size, &curr_address);
	}

	_camdbg_rmem.enabled = true;

	return 0;
}

RESERVEDMEM_OF_DECLARE(tegra_cam_rtcpu,
		"nvidia,camdbg_carveout", camrtc_dbgfs_rmem_init);

/**
 * @brief Gets the camera-rtcpu device from an IVC channel
 *
 * This function retrieves the camera-rtcpu device associated with an IVC channel.
 * It performs the following operations:
 * - Checks if the channel pointer is valid using @ref unlikely()
 * - Verifies both parent and grandparent device pointers exist using @ref BUG_ON()
 * - Returns the grandparent device which is the camera-rtcpu device
 *
 * @param[in] ch  Pointer to the IVC channel
 *                Valid value: non-NULL (NULL check handled internally)
 *
 * @retval (struct device *) Pointer to the camera-rtcpu device
 * @retval NULL if channel is NULL
 */
static struct device *camrtc_get_device(struct tegra_ivc_channel *ch)
{
	if (unlikely(ch == NULL))
		return NULL;

	BUG_ON(ch->dev.parent == NULL);
	BUG_ON(ch->dev.parent->parent == NULL);

	return ch->dev.parent->parent;
}

#define INIT_OPEN_FOPS(_open) { \
	.open = _open, \
	.read = seq_read, \
	.llseek = seq_lseek, \
	.release = single_release \
}

#define DEFINE_SEQ_FOPS(_fops_, _show_) \
static int _fops_ ## _open(struct inode *inode, struct file *file) \
{ \
	return single_open(file, _show_, inode->i_private); \
} \
static const struct file_operations _fops_ = INIT_OPEN_FOPS(_fops_ ## _open)

/**
 * @brief Outputs the camera-rtcpu version information
 *
 * This function retrieves and displays the camera-rtcpu version.
 * It performs the following operations:
 * - Gets the camera-rtcpu device using @ref camrtc_get_device()
 * - Calls @ref tegra_camrtc_print_version() to get the version string
 * - Writes the version string to the sequence file using @ref seq_puts()
 *
 * @param[in] file  Pointer to sequence file for output
 *                  Valid value: non-NULL
 * @param[in] data  Private data pointer
 *                  Valid value: any value
 *
 * @retval 0  Operation successful
 */
static int camrtc_show_version(struct seq_file *file, void *data)
{
	struct tegra_ivc_channel *ch = file->private;
	struct device *rce_dev = camrtc_get_device(ch);
	char version[TEGRA_CAMRTC_VERSION_LEN];

	tegra_camrtc_print_version(rce_dev, version, sizeof(version));

	seq_puts(file, version);
	seq_puts(file, "\n");

	return 0;
}

DEFINE_SEQ_FOPS(camrtc_dbgfs_fops_version, camrtc_show_version);

/**
 * @brief Reboots the camera-rtcpu and shows the result
 *
 * This function reboots the camera-rtcpu and writes the result to the sequence file.
 * It performs the following operations:
 * - Gets the camera-rtcpu device using @ref camrtc_get_device()
 * - Brings rtcpu online using @ref tegra_ivc_channel_runtime_get()
 * - Reboots the rtcpu using @ref tegra_camrtc_reboot()
 * - Outputs "0" to the sequence file on success using @ref seq_puts()
 * - Releases the runtime reference using @ref tegra_ivc_channel_runtime_put()
 *
 * @param[in] file  Pointer to sequence file for output
 *                  Valid value: non-NULL
 * @param[in] data  Private data pointer
 *                  Valid value: any value
 *
 * @retval 0       Reboot successful
 * @retval negative Error code from @ref tegra_ivc_channel_runtime_get() or @ref tegra_camrtc_reboot()
 */
static int camrtc_show_reboot(struct seq_file *file, void *data)
{
	struct tegra_ivc_channel *ch = file->private;
	struct device *rce_dev = camrtc_get_device(ch);
	int ret = 0;

	/* Make rtcpu online */
	ret = tegra_ivc_channel_runtime_get(ch);
	if (ret < 0)
		goto error;

	ret = tegra_camrtc_reboot(rce_dev);
	if (ret)
		goto error;

	seq_puts(file, "0\n");

error:
	tegra_ivc_channel_runtime_put(ch);
	return ret;
}

DEFINE_SEQ_FOPS(camrtc_dbgfs_fops_reboot, camrtc_show_reboot);

/**
 * @brief Notifies waiting threads about IVC activity
 *
 * This function wakes up all threads waiting on the debug wait queue.
 * It performs the following operations:
 * - Retrieves the camera debug data from the IVC channel using @ref tegra_ivc_channel_get_drvdata()
 * - Wakes up all waiting threads using @ref wake_up_all()
 *
 * @param[in] ch  Pointer to the IVC channel
 *                Valid value: non-NULL
 */
static void camrtc_debug_notify(struct tegra_ivc_channel *ch)
{
	struct camrtc_debug *crd = tegra_ivc_channel_get_drvdata(ch);

	wake_up_all(&crd->waitq);
}

/**
 * @brief Forces a reset and restore of the camera-rtcpu
 *
 * This function forces a reset and restore of the camera-rtcpu and shows the result.
 * It performs the following operations:
 * - Gets the camera-rtcpu device using @ref camrtc_get_device()
 * - Brings rtcpu online using @ref tegra_ivc_channel_runtime_get()
 * - Restores the rtcpu using @ref tegra_camrtc_restore()
 * - Outputs "0" to the sequence file on success using @ref seq_puts()
 * - Releases the runtime reference using @ref tegra_ivc_channel_runtime_put()
 *
 * @param[in] file  Pointer to sequence file for output
 *                  Valid value: non-NULL
 * @param[in] data  Private data pointer
 *                  Valid value: any value
 *
 * @retval 0       Restore operation successful
 * @retval negative Error code from @ref tegra_ivc_channel_runtime_get() or @ref tegra_camrtc_restore()
 */
static int camrtc_show_forced_reset_restore(struct seq_file *file, void *data)
{
	struct tegra_ivc_channel *ch = file->private;
	struct device *rce_dev = camrtc_get_device(ch);
	int ret = 0;

	/* Make rtcpu online */
	ret = tegra_ivc_channel_runtime_get(ch);
	if (ret < 0)
		goto error;

	ret = tegra_camrtc_restore(rce_dev);
	if (ret)
		goto error;

	seq_puts(file, "0\n");

error:
	tegra_ivc_channel_runtime_put(ch);
	return ret;
}

DEFINE_SEQ_FOPS(camrtc_dbgfs_fops_forced_reset_restore,
			camrtc_show_forced_reset_restore);

/**
 * @brief Performs a full-frame transaction with the camera-rtcpu debug interface
 *
 * This function sends a request to the camera-rtcpu and waits for a response.
 * It performs the following operations:
 * - Validates input parameters and acquires mutex lock using @ref mutex_lock_interruptible()
 * - Gets runtime reference using @ref tegra_ivc_channel_runtime_get()
 * - Verifies IVC channel is online with @ref tegra_ivc_channel_online_check()
 * - Flushes any stray responses by calling @ref tegra_ivc_read_advance()
 * - Waits for write availability using @ref wait_event_interruptible_timeout()
 * - If the wait times out log the RCE snapshot by calling @ref rtcpu_trace_panic_callback().
 * - Writes request using @ref tegra_ivc_write()
 * - Waits for and reads response using @ref tegra_ivc_read_peek()
 * - If the response is not received within the timeout,
 *   log the RCE snapshot by calling @ref rtcpu_trace_panic_callback().
 * - Verifies response matches request type
 * - Releases resources and lock using @ref tegra_ivc_channel_runtime_put() and @ref mutex_unlock()
 *
 * @param[in] ch         Pointer to the IVC channel
 *                       Valid value: non-NULL
 * @param[in] req        Pointer to the debug request structure
 *                       Valid value: non-NULL
 * @param[in] req_size   Size of the request in bytes
 *                       Valid range: > 0
 * @param[out] resp      Pointer to the debug response structure
 *                       Valid value: non-NULL
 * @param[in] resp_size  Size of the response in bytes
 *                       Valid range: > 0
 * @param[in] timeout    Timeout in milliseconds, 0 for default
 *                       Valid range: >= 0
 *
 * @retval 0          Transaction successful
 * @retval -ENOMEM    Input parameters invalid
 * @retval -EINTR     Interrupted while waiting
 * @retval -ETIMEDOUT Timeout occurred
 * @retval -ECONNRESET IVC channel was reset
 * @retval negative   Other error
 */
static int camrtc_ivc_dbg_full_frame_xact(
	struct tegra_ivc_channel *ch,
	struct camrtc_dbg_request *req,
	size_t req_size,
	struct camrtc_dbg_response *resp,
	size_t resp_size,
	long timeout)
{
	struct camrtc_debug *crd = tegra_ivc_channel_get_drvdata(ch);
	int ret;

	if (req == NULL || resp == NULL)
		return -ENOMEM;

	if (timeout == 0)
		timeout = crd->parameters.completion_timeout;

	timeout = msecs_to_jiffies(timeout);

	ret = mutex_lock_interruptible(&crd->mutex);
	if (ret)
		return ret;

	ret = tegra_ivc_channel_runtime_get(ch);
	if (ret < 0)
		goto unlock;

	if ((!tegra_ivc_channel_online_check(ch))) {
		ret = -ECONNRESET;
		goto out;
	}

	while (tegra_ivc_can_read(&ch->ivc)) {
		tegra_ivc_read_advance(&ch->ivc);
		dev_warn(&ch->dev, "stray response\n");
	}

	timeout = wait_event_interruptible_timeout(crd->waitq,
			tegra_ivc_channel_has_been_reset(ch) ||
			tegra_ivc_can_write(&ch->ivc), timeout);
	if (timeout <= 0) {
		rtcpu_trace_panic_callback(camrtc_get_device(ch));
		ret = timeout ?: -ETIMEDOUT;
		goto out;
	}
	if (tegra_ivc_channel_has_been_reset(ch)) {
		ret = -ECONNRESET;
		goto out;
	}

	ret = tegra_ivc_write(&ch->ivc, NULL, req, req_size);
	if (ret < 0) {
		dev_err(&ch->dev, "IVC write error: %d\n", ret);
		goto out;
	}

	for (;;) {
		timeout = wait_event_interruptible_timeout(crd->waitq,
				tegra_ivc_channel_has_been_reset(ch) ||
				tegra_ivc_can_read(&ch->ivc),
				timeout);
		if (timeout <= 0) {
			rtcpu_trace_panic_callback(camrtc_get_device(ch));
			ret = timeout ?: -ETIMEDOUT;
			break;
		}
		if (tegra_ivc_channel_has_been_reset(ch)) {
			ret = -ECONNRESET;
			break;
		}

		dev_dbg(&ch->dev, "rx msg\n");

		ret = tegra_ivc_read_peek(&ch->ivc, NULL, resp, 0, resp_size);
		if (ret < 0) {
			dev_err(&ch->dev, "IVC read error: %d\n", ret);
			break;
		}

		tegra_ivc_read_advance(&ch->ivc);

		if (resp->resp_type == req->req_type) {
			ret = 0;
			break;
		}

		dev_err(&ch->dev, "unexpected response\n");
	}

out:
	tegra_ivc_channel_runtime_put(ch);
unlock:
	mutex_unlock(&crd->mutex);
	return ret;
}

/**
 * @brief Performs a standard transaction with the camera-rtcpu debug interface
 *
 * This function is a wrapper around @ref camrtc_ivc_dbg_full_frame_xact that uses
 * standard structure sizes for requests and responses.
 * It performs the following operations:
 * - Calls @ref camrtc_ivc_dbg_full_frame_xact with sizeof(*req) and sizeof(*resp)
 *
 * @param[in] ch      Pointer to the IVC channel
 *                    Valid value: non-NULL
 * @param[in] req     Pointer to the debug request structure
 *                    Valid value: non-NULL
 * @param[out] resp   Pointer to the debug response structure
 *                    Valid value: non-NULL
 * @param[in] timeout Timeout in milliseconds, 0 for default
 *                    Valid range: >= 0
 *
 * @retval (int)  Return value from @ref camrtc_ivc_dbg_full_frame_xact
 */
static inline int camrtc_ivc_dbg_xact(
	struct tegra_ivc_channel *ch,
	struct camrtc_dbg_request *req,
	struct camrtc_dbg_response *resp,
	long timeout)
{
	return camrtc_ivc_dbg_full_frame_xact(ch, req, sizeof(*req),
					resp, sizeof(*resp),
					timeout);
}

/**
 * @brief Pings the camera-rtcpu and displays timing information
 *
 * This function sends a ping request to the camera-rtcpu and measures
 * the round-trip time. It performs the following operations:
 * - Gets current time using @ref sched_clock()
 * - Prepares ping request with timestamp
 * - Sends request using @ref camrtc_ivc_dbg_xact()
 * - Calculates round-trip time and offset
 * - Displays formatted timing information using @ref seq_printf()
 * - Displays any response data
 *
 * @param[in] file  Pointer to sequence file for output
 *                  Valid value: non-NULL
 * @param[in] data  Private data pointer
 *                  Valid value: any value
 *
 * @retval 0  Ping successful
 * @retval (int) Error code from @ref camrtc_ivc_dbg_xact()
 */
static int camrtc_show_ping(struct seq_file *file, void *data)
{
	struct tegra_ivc_channel *ch = file->private;
	struct camrtc_dbg_request req = {
		.req_type = CAMRTC_REQ_PING,
	};
	struct camrtc_dbg_response resp;
	u64 sent, recv, tsc, subRet, mulRet;
	int ret;

	sent = sched_clock();
	req.data.ping_data.ts_req = sent;

	ret = camrtc_ivc_dbg_xact(ch, &req, &resp, 0);
	if (ret)
		return ret;

	recv = sched_clock();
	tsc = resp.data.ping_data.ts_resp;
	(void)__builtin_usubll_overflow(recv, sent, &subRet);
	seq_printf(file,
		"roundtrip=%llu.%03llu us "
		"(sent=%llu.%09llu recv=%llu.%09llu)\n",
		subRet / 1000, subRet % 1000,
		sent / 1000000000, sent % 1000000000,
		recv / 1000000000, recv % 1000000000);
	(void)__builtin_umulll_overflow(tsc, 32ULL, &mulRet);
	(void)__builtin_usubll_overflow(mulRet, sent, &subRet);
	seq_printf(file,
		"rtcpu tsc=%llu.%09llu offset=%llu.%09llu\n",
		tsc / (1000000000 / 32), tsc % (1000000000 / 32),
		subRet / 1000000000,
		subRet % 1000000000);
	seq_printf(file, "%.*s\n",
		(int)sizeof(resp.data.ping_data.data),
		(char *)resp.data.ping_data.data);

	return 0;
}

DEFINE_SEQ_FOPS(camrtc_dbgfs_fops_ping, camrtc_show_ping);

/**
 * @brief Pings the camera-rtcpu using shared memory interface and displays timing information
 *
 * This function sends a ping request to the camera-rtcpu using the shared memory interface
 * and measures the round-trip time. It performs the following operations:
 * - Gets the camera-rtcpu device using @ref camrtc_get_device()
 * - Acquires runtime reference using @ref tegra_ivc_channel_runtime_get()
 * - Gets current time using @ref sched_clock()
 * - Sends ping using @ref tegra_camrtc_ping()
 * - Calculates and displays round-trip time using @ref seq_printf()
 * - Releases runtime reference using @ref tegra_ivc_channel_runtime_put()
 *
 * @param[in] file  Pointer to sequence file for output
 *                  Valid value: non-NULL
 * @param[in] data  Private data pointer
 *                  Valid value: any value
 *
 * @retval 0  Ping successful
 * @retval (int)) Error code from @ref tegra_ivc_channel_runtime_get() or @ref tegra_camrtc_ping()
 */
static int camrtc_show_sm_ping(struct seq_file *file, void *data)
{
	struct tegra_ivc_channel *ch = file->private;
	struct device *camrtc = camrtc_get_device(ch);
	u64 sent, recv, subRet;
	int err;

	err = tegra_ivc_channel_runtime_get(ch);
	if (err < 0)
		return err;

	sent = sched_clock();

	err = tegra_camrtc_ping(camrtc, (uint32_t)sent & 0xffffffU, 0);
	if (err < 0)
		goto error;

	recv = sched_clock();
	err = 0;

	(void)__builtin_usubll_overflow(recv, sent, &subRet);
	seq_printf(file,
		"roundtrip=%llu.%03llu us "
		"(sent=%llu.%09llu recv=%llu.%09llu)\n",
		subRet / 1000, subRet % 1000,
		sent / 1000000000, sent % 1000000000,
		recv / 1000000000, recv % 1000000000);

error:
	tegra_ivc_channel_runtime_put(ch);

	return err;
}

DEFINE_SEQ_FOPS(camrtc_dbgfs_fops_sm_ping, camrtc_show_sm_ping);

/**
 * @brief Gets the current log level of the camera-rtcpu
 *
 * This function retrieves the current log level setting from the camera-rtcpu.
 * It performs the following operations:
 * - Prepares a GET_LOGLEVEL request
 * - Sends request using @ref camrtc_ivc_dbg_xact()
 * - Verifies response status is OK
 * - Sets the output parameter to the current log level
 *
 * @param[in] data  Private data pointer (IVC channel)
 *                  Valid value: non-NULL
 * @param[out] val  Pointer to store the retrieved log level
 *                  Valid value: non-NULL
 *
 * @retval 0  Operation successful
 * @retval -EPROTO Response status not OK
 * @retval (int) Error code from @ref camrtc_ivc_dbg_xact()
 */
static int camrtc_dbgfs_show_loglevel(void *data, u64 *val)
{
	struct tegra_ivc_channel *ch = data;
	struct camrtc_dbg_request req = {
		.req_type = CAMRTC_REQ_GET_LOGLEVEL,
	};
	struct camrtc_dbg_response resp;
	int ret;

	ret = camrtc_ivc_dbg_xact(ch, &req, &resp, 0);
	if (ret)
		return ret;

	if (resp.status != CAMRTC_STATUS_OK)
		return -EPROTO;

	*val = resp.data.log_data.level;

	return 0;
}

/**
 * @brief Sets the log level of the camera-rtcpu
 *
 * This function sets a new log level for the camera-rtcpu.
 * It performs the following operations:
 * - Validates the input value can be represented as a u32
 * - Prepares a SET_LOGLEVEL request with the new level
 * - Sends request using @ref camrtc_ivc_dbg_xact()
 * - Handles different response status codes
 *
 * @param[in] data  Private data pointer (IVC channel)
 *                  Valid value: non-NULL
 * @param[in] val   New log level value
 *                  Valid range: Can be represented as u32
 *
 * @retval 0  Log level set successfully
 * @retval -EINVAL Value out of range or invalid parameter
 * @retval -EPROTO Unexpected response status
 * @retval (int) Error code from @ref camrtc_ivc_dbg_xact()
 */
static int camrtc_dbgfs_store_loglevel(void *data, u64 val)
{
	struct tegra_ivc_channel *ch = data;
	struct camrtc_dbg_request req = {
		.req_type = CAMRTC_REQ_SET_LOGLEVEL,
	};
	struct camrtc_dbg_response resp;
	int ret;

	if ((u32)val != val)
		return -EINVAL;

	req.data.log_data.level = val;

	ret = camrtc_ivc_dbg_xact(ch, &req, &resp, 0);
	if (ret)
		return ret;

	if (resp.status == CAMRTC_STATUS_INVALID_PARAM)
		return -EINVAL;
	else if (resp.status != CAMRTC_STATUS_OK)
		return -EPROTO;

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(camrtc_dbgfs_fops_loglevel,
			camrtc_dbgfs_show_loglevel,
			camrtc_dbgfs_store_loglevel,
			"%lld\n");

/**
 * @brief Executes a MODS test on the camera-rtcpu and displays the result
 *
 * This function runs a MODS (Modular Operational Diagnostic Suite) test
 * on the camera-rtcpu and shows the status code.
 * It performs the following operations:
 * - Retrieves test parameters from the debug structure
 * - Prepares a MODS_TEST request with case, loops, and DMA channels
 * - Sends request using @ref camrtc_ivc_dbg_xact() with adjusted timeout
 * - Outputs status code to the sequence file using @ref seq_printf()
 *
 * @param[in] file  Pointer to sequence file for output
 *                  Valid value: non-NULL
 * @param[in] data  Private data pointer
 *                  Valid value: any value
 *
 * @retval 0  Test executed successfully
 * @retval (int) Error code from @ref camrtc_ivc_dbg_xact()
 */
static int camrtc_show_mods_result(struct seq_file *file, void *data)
{
	struct tegra_ivc_channel *ch = file->private;
	struct camrtc_debug *crd = tegra_ivc_channel_get_drvdata(ch);
	struct camrtc_dbg_request req = {
		.req_type = CAMRTC_REQ_MODS_TEST,
	};
	struct camrtc_dbg_response resp;
	int ret;
	unsigned long timeout = crd->parameters.completion_timeout;
	u32 loops = crd->parameters.mods_loops;

	req.data.mods_data.mods_case = crd->parameters.mods_case;
	req.data.mods_data.mods_loops = loops;
	req.data.mods_data.mods_dma_channels = crd->parameters.mods_dma_channels;

	ret = camrtc_ivc_dbg_xact(ch, &req, &resp, loops * timeout);
	if (ret == 0)
		seq_printf(file, "mods=%u\n", resp.status);

	return ret;
}

DEFINE_SEQ_FOPS(camrtc_dbgfs_fops_mods_result, camrtc_show_mods_result);

/**
 * @brief Retrieves and displays the FreeRTOS state from the camera-rtcpu
 *
 * This function retrieves the current state of the FreeRTOS running on the
 * camera-rtcpu and displays it in the sequence file.
 * It performs the following operations:
 * - Prepares an RTOS_STATE request
 * - Sends request using @ref camrtc_ivc_dbg_xact()
 * - Outputs the RTOS state information to the sequence file using @ref seq_printf()
 *
 * @param[in] file  Pointer to sequence file for output
 *                  Valid value: non-NULL
 * @param[in] data  Private data pointer
 *                  Valid value: any value
 *
 * @retval 0  State retrieved and displayed successfully
 * @retval (int) Error code from @ref camrtc_ivc_dbg_xact()
 */
static int camrtc_dbgfs_show_freertos_state(struct seq_file *file, void *data)
{
	struct tegra_ivc_channel *ch = file->private;
	struct camrtc_dbg_request req = {
		.req_type = CAMRTC_REQ_RTOS_STATE,
	};
	struct camrtc_dbg_response resp;
	int ret = 0;

	ret = camrtc_ivc_dbg_xact(ch, &req, &resp, 0);
	if (ret == 0) {
		seq_printf(file, "%.*s",
			(int) sizeof(resp.data.rtos_state_data.rtos_state),
			resp.data.rtos_state_data.rtos_state);
	}

	return ret;
}

DEFINE_SEQ_FOPS(camrtc_dbgfs_fops_freertos_state,
		camrtc_dbgfs_show_freertos_state);

/**
 * @brief Converts a byte count to kilobytes, rounding up
 *
 * This function converts a byte count to kilobytes, rounding up to the
 * nearest kilobyte.
 * It performs the following operations:
 * - Adds 1023 to the input value using @ref __builtin_uadd_overflow for
 *   safe arithmetic with overflow checking
 * - Divides by 1024 to convert bytes to kilobytes with ceiling rounding
 *
 * @param[in] x  Byte count to convert to kilobytes
 *                Valid range: Any uint32_t value
 *
 * @retval (uint32_t) The input value in kilobytes, rounded up
 */
static inline uint32_t ToKilobytes(uint32_t x)
{
	(void)__builtin_uadd_overflow(x, 1023U, &x);
	return (x / 1024U);
}

/**
 * @brief Retrieves and displays memory usage statistics from the camera-rtcpu
 *
 * This function retrieves memory usage information from the camera-rtcpu
 * and formats it for display. It performs the following operations:
 * - Prepares a GET_MEM_USAGE request
 * - Sends request using @ref camrtc_ivc_dbg_xact()
 * - Calculates total memory usage using @ref __builtin_uadd_overflow for safe arithmetic
 * - Formats and displays memory usage statistics using @ref seq_printf()
 * - Displays values in bytes and kilobytes using @ref ToKilobytes()
 *
 * @param[in] file  Pointer to sequence file for output
 *                  Valid value: non-NULL
 * @param[in] data  Private data pointer
 *                  Valid value: any value
 *
 * @retval 0  Statistics retrieved and displayed successfully
 * @retval (int) Error code from @ref camrtc_ivc_dbg_xact()
 */
static int camrtc_dbgfs_show_memstat(struct seq_file *file, void *data)
{
	struct tegra_ivc_channel *ch = file->private;
	struct camrtc_dbg_request req = {
		.req_type = CAMRTC_REQ_GET_MEM_USAGE,
	};
	struct camrtc_dbg_response resp;
	int ret = 0;

	ret = camrtc_ivc_dbg_xact(ch, &req, &resp, 0);
	if (ret == 0) {
		const struct camrtc_dbg_mem_usage *m = &resp.data.mem_usage;
		uint32_t total, addRet;
		(void)__builtin_uadd_overflow(m->text, m->bss, &addRet);
		(void)__builtin_uadd_overflow(addRet, m->data, &addRet);
		(void)__builtin_uadd_overflow(addRet, m->heap, &addRet);
		(void)__builtin_uadd_overflow(addRet, m->stack, &addRet);
		(void)__builtin_uadd_overflow(addRet, m->free_mem, &total);

		seq_printf(file, "%7s %7s %7s %7s %7s %7s %7s\n",
			"text", "bss", "data", "heap", "sys", "free", "TOTAL");
		seq_printf(file, "%7u\t%7u\t%7u\t%7u\t%7u\t%7u\t%7u\n",
			m->text, m->bss, m->data, m->heap, m->stack, m->free_mem, total);
		seq_printf(file, "%7u\t%7u\t%7u\t%7u\t%7u\t%7u\t%7u (in kilobytes)\n",
			ToKilobytes(m->text), ToKilobytes(m->bss), ToKilobytes(m->data),
			ToKilobytes(m->heap), ToKilobytes(m->stack),
			ToKilobytes(m->free_mem), ToKilobytes(total));
	}

	return ret;
}

DEFINE_SEQ_FOPS(camrtc_dbgfs_fops_memstat, camrtc_dbgfs_show_memstat);

/**
 * @brief Retrieves and displays interrupt statistics from the camera-rtcpu
 *
 * This function retrieves interrupt statistics from the camera-rtcpu
 * and formats them for display. It performs the following operations:
 * - Checks if IRQ statistics are supported in the build (CAMRTC_REQ_GET_IRQ_STAT)
 * - Allocates memory for the response using @ref kzalloc()
 * - Prepares a GET_IRQ_STAT request
 * - Sends request using @ref camrtc_ivc_dbg_full_frame_xact()
 * - Formats and displays IRQ statistics including count, runtime, and names using @ref seq_printf()
 * - Tracks maximum runtime across all interrupts
 * - Displays total statistics
 * - Frees allocated memory using @ref kfree()
 *
 * @param[in] file  Pointer to sequence file for output
 *                  Valid value: non-NULL
 * @param[in] data  Private data pointer
 *                  Valid value: any value
 *
 * @retval 0  Statistics retrieved and displayed successfully
 * @retval -ENOMSG  IRQ statistics not supported or enabled
 * @retval (int) Error code from @ref camrtc_ivc_dbg_full_frame_xact()
 */
static int camrtc_dbgfs_show_irqstat(struct seq_file *file, void *data)
{
	int ret = -ENOMSG;
#ifdef CAMRTC_REQ_GET_IRQ_STAT
	struct tegra_ivc_channel *ch = file->private;
	struct camrtc_dbg_request req = {
		.req_type = CAMRTC_REQ_GET_IRQ_STAT,
	};
	void *mem = kzalloc(ch->ivc.frame_size, GFP_KERNEL | __GFP_ZERO);
	struct camrtc_dbg_response *resp = mem;
	const struct camrtc_dbg_irq_stat *stat = &resp->data.irq_stat;
	uint32_t i;
	uint32_t max_runtime = 0;

	ret = camrtc_ivc_dbg_full_frame_xact(ch, &req, sizeof(req),
			resp, ch->ivc.frame_size, 0);
	if (ret != 0)
		goto done;

	seq_printf(file, "Irq#\tCount\tRuntime\tMax rt\tName\n");

	for (i = 0; i < stat->n_irq; i++) {
		seq_printf(file, "%u\t%u\t%llu\t%u\t%.*s\n",
			stat->irqs[i].irq_num,
			stat->irqs[i].num_called,
			stat->irqs[i].runtime,
			stat->irqs[i].max_runtime,
			(int)sizeof(stat->irqs[i].name), stat->irqs[i].name);

		if (max_runtime < stat->irqs[i].max_runtime)
			max_runtime = stat->irqs[i].max_runtime;
	}

	seq_printf(file, "-\t%llu\t%llu\t%u\t%s\n", stat->total_called,
		stat->total_runtime, max_runtime, "total");

done:
	kfree(mem);
#endif
	return ret;
}

DEFINE_SEQ_FOPS(camrtc_dbgfs_fops_irqstat, camrtc_dbgfs_show_irqstat);

/**
 * @brief Calculates the maximum size available for test data
 *
 * This function determines the maximum amount of data that can be included
 * in a test request. It performs the following operations:
 * - Uses @ref __builtin_uaddl_overflow to safely add the IVC frame size to the
 *   offset of the test data field in the request structure
 * - Returns the calculated maximum size
 *
 * @param[in] ch  Pointer to the IVC channel
 *                Valid value: non-NULL
 *
 * @retval (size_t) Maximum size in bytes that can be used for test data
 */
static size_t camrtc_dbgfs_get_max_test_size(
	const struct tegra_ivc_channel *ch)
{
	size_t ret;
	(void)__builtin_uaddl_overflow(ch->ivc.frame_size,
				offsetof(struct camrtc_dbg_request, data.run_mem_test_data.data), &ret);
	return ret;
}

/**
 * @brief Reads test case data from the debugfs file
 *
 * This function implements the read file operation for test case data.
 * It performs the following operations:
 * - Retrieves the camera debug data for the IVC channel
 * - Uses @ref simple_read_from_buffer to copy data from the test case buffer
 *   to user space
 *
 * @param[in] file   Pointer to the file object
 *                   Valid value: non-NULL
 * @param[out] buf   User space buffer to read into
 *                   Valid value: non-NULL
 * @param[in] count  Number of bytes to read
 *                   Valid range: >= 0
 * @param[in,out] f_pos  Pointer to file position
 *                       Valid value: non-NULL
 *
 * @retval (ssize_t) Number of bytes read on success, or negative error code
 */
static ssize_t camrtc_dbgfs_read_test_case(struct file *file,
		char __user *buf, size_t count, loff_t *f_pos)
{
	struct tegra_ivc_channel *ch = file->f_inode->i_private;
	struct camrtc_debug *crd = tegra_ivc_channel_get_drvdata(ch);

	return simple_read_from_buffer(buf, count, f_pos,
				crd->parameters.test_case,
				crd->parameters.test_case_size);
}

/**
 * @brief Writes test case data to the debugfs file
 *
 * This function implements the write file operation for test case data.
 * It performs the following operations:
 * - Retrieves the camera debug data for the IVC channel
 * - Validates the maximum size for the test case
 * - Uses @ref simple_write_to_buffer to copy data from user space to the test case buffer
 * - Updates the test case size with the current file position
 * - Marks all input memory buffers as empty
 *
 * @param[in] file   Pointer to the file object
 *                   Valid value: non-NULL
 * @param[in] buf    User space buffer to write from
 *                   Valid value: non-NULL
 * @param[in] count  Number of bytes to write
 *                   Valid range: >= 0
 * @param[in,out] f_pos  Pointer to file position
 *                       Valid value: non-NULL
 *
 * @retval (ssize_t) Number of bytes written on success, or negative error code
 */
static ssize_t camrtc_dbgfs_write_test_case(struct file *file,
		const char __user *buf, size_t count, loff_t *f_pos)
{
	struct tegra_ivc_channel *ch = file->f_inode->i_private;
	struct camrtc_debug *crd = tegra_ivc_channel_get_drvdata(ch);
	char *test_case = crd->parameters.test_case;
	size_t max_size = camrtc_dbgfs_get_max_test_size(ch);
	int i;
	ssize_t ret;

	ret = simple_write_to_buffer(test_case, max_size, f_pos, buf, count);

	if (ret >= 0)
		crd->parameters.test_case_size = *f_pos;

	/* Mark input buffers empty */
	for (i = 0; i < ARRAY_SIZE(crd->mem); i++)
		crd->mem[i].used = 0;

	return ret;
}

static const struct file_operations camrtc_dbgfs_fops_test_case = {
	.read = camrtc_dbgfs_read_test_case,
	.write = camrtc_dbgfs_write_test_case,
};

/**
 * @brief Gets the appropriate device for memory allocations
 *
 * This function determines the appropriate device to use for memory allocations.
 * It performs the following operations:
 * - Checks if the VI device is available (mem_devices[1])
 * - If VI is available, returns the VI device for consistent allocations
 * - Otherwise, falls back to the primary memory device (mem_devices[0])
 *
 * This selection is necessary because if VI misses stage-1 SMMU translation,
 * allocations need to be contiguous. Using VI ensures compatibility across contexts.
 *
 * @param[in] crd  Pointer to the camera debug structure
 *                 Valid value: non-NULL
 *
 * @retval (struct device *) Pointer to the appropriate device for memory allocations
 */
static struct device *camrtc_dbgfs_memory_dev(
	const struct camrtc_debug *crd)
{
	/*
	 * If VI misses stage-1 SMMU translation, the allocations need
	 * to be contiguous. Just allocate everything through VI and
	 * map it to other contexts separately.
	 */
	if (crd->mem_devices[1] != NULL)
		return crd->mem_devices[1];
	else
		return crd->mem_devices[0];
}

/**
 * @brief Reads test memory data from the debugfs file
 *
 * This function implements the read file operation for test memory data.
 * It performs the following operations:
 * - Retrieves the test memory structure for the specific memory region
 * - Uses @ref simple_read_from_buffer to copy data from the memory buffer to user space
 *
 * @param[in] file   Pointer to the file object
 *                   Valid value: non-NULL
 * @param[out] buf   User space buffer to read into
 *                   Valid value: non-NULL
 * @param[in] count  Number of bytes to read
 *                   Valid range: >= 0
 * @param[in,out] f_pos  Pointer to file position
 *                       Valid value: non-NULL
 *
 * @retval (ssize_t) Number of bytes read on success, or negative error code
 */
static ssize_t camrtc_dbgfs_read_test_mem(struct file *file,
		char __user *buf, size_t count, loff_t *f_pos)
{
	struct camrtc_test_mem *mem = file->f_inode->i_private;

	return simple_read_from_buffer(buf, count, f_pos, mem->ptr, mem->used);
}

/**
 * @brief Writes data to test memory through the debugfs file
 *
 * This function implements the write file operation for test memory.
 * It performs the following operations:
 * - Retrieves the test memory structure and camera debug container
 * - Gets the appropriate memory device and IOMMU domain
 * - Expands the memory buffer if needed, handling both reserved memory and normal allocation
 * - For reserved memory, maps the physical address to IOVA using @ref dma_map_single()
 * - For normal allocation, uses @ref dma_alloc_coherent() and copies existing content
 * - Updates the physical address based on IOMMU state
 * - Writes data to memory using @ref simple_write_to_buffer()
 * - Updates usage information and handles cleanup on zero usage
 *
 * @param[in] file   Pointer to the file object
 *                   Valid value: non-NULL
 * @param[in] buf    User space buffer to write from
 *                   Valid value: non-NULL
 * @param[in] count  Number of bytes to write
 *                   Valid range: >= 0
 * @param[in,out] f_pos  Pointer to file position
 *                       Valid value: non-NULL
 *
 * @retval (ssize_t) Number of bytes written on success, or negative error code
 */
static ssize_t camrtc_dbgfs_write_test_mem(struct file *file,
		const char __user *buf, size_t count, loff_t *f_pos)
{
	struct camrtc_test_mem *mem = file->f_inode->i_private;
	struct camrtc_debug *crd = container_of(
		mem, struct camrtc_debug, mem[mem->index]);
	struct device *mem_dev = camrtc_dbgfs_memory_dev(crd);
	struct iommu_domain *domain = iommu_get_domain_for_dev(mem_dev);
	ssize_t ret, addRet;

	(void)__builtin_uaddl_overflow(*f_pos, count, &addRet);
	if (addRet > mem->size) {
		if (_camdbg_rmem.enabled) {
			size_t size = round_up(addRet, 64 * 1024);
			void *ptr = phys_to_virt(
				_camdbg_rmem.mem_ctxs[mem->index].address);
			unsigned long rmem_size =
				_camdbg_rmem.mem_ctxs[mem->index].size;

			if (size > rmem_size) {
				pr_err("%s: not enough memory\n", __func__);
				return -ENOMEM;
			}

			if (mem->ptr)
				dma_unmap_single(mem_dev, mem->iova, mem->size,
						DMA_BIDIRECTIONAL);

			/* same addr, no overwrite concern */
			mem->ptr = ptr;
			mem->size = size;

			mem->iova = dma_map_single(mem_dev, mem->ptr,
						mem->size, DMA_BIDIRECTIONAL);
			if (dma_mapping_error(mem_dev,  mem->iova)) {
				pr_err("%s: dma map failed\n", __func__);
				return -ENOMEM;
			}
		} else {
			size_t size = round_up(addRet, 64 * 1024);
			dma_addr_t iova;
			void *ptr = dma_alloc_coherent(mem_dev, size, &iova,
					GFP_KERNEL | __GFP_ZERO);
			if (ptr == NULL)
				return -ENOMEM;
			if (mem->ptr) {
				memcpy(ptr, mem->ptr, mem->used);
				dma_free_coherent(mem_dev, mem->size, mem->ptr,
					mem->iova);
			}
			mem->ptr = ptr;
			mem->size = size;
			mem->iova = iova;
		}

		/* If mem_dev is not connected to SMMU, the iova is physical */
		if (domain)
			mem->phys_addr = iommu_iova_to_phys(domain, mem->iova);
		else
			mem->phys_addr = mem->iova;
	}

	ret = simple_write_to_buffer(mem->ptr, mem->size, f_pos, buf, count);

	if (ret >= 0) {
		mem->used = *f_pos;

		if (mem->used == 0 && mem->ptr != NULL) {
			if (_camdbg_rmem.enabled)
				dma_unmap_single(mem_dev, mem->iova, mem->size,
						DMA_BIDIRECTIONAL);
			else
				dma_free_coherent(mem_dev, mem->size, mem->ptr,
						mem->iova);

			memset(mem, 0, sizeof(*mem));
		}
	}

	return ret;
}

/**
 * @brief File operations for test memory
 *
 * This structure defines the file operations for test memory in the debugfs
 * interface, providing read and write access to the memory buffers used for
 * testing the camera-rtcpu.
 * - read: Implemented by @ref camrtc_dbgfs_read_test_mem
 * - write: Implemented by @ref camrtc_dbgfs_write_test_mem
 */
static const struct file_operations camrtc_dbgfs_fops_test_mem = {
	.read = camrtc_dbgfs_read_test_mem,
	.write = camrtc_dbgfs_write_test_mem,
};

#define BUILD_BUG_ON_MISMATCH(s1, f1, s2, f2) \
	BUILD_BUG_ON(offsetof(s1, data.f1) != offsetof(s2, data.f2))

/**
 * @brief Runs a test and displays the result
 *
 * This function executes a test on the camera-rtcpu by sending the test case data
 * via IVC channel and writes the test results to the sequence file. It performs
 * the following operations:
 * - Copies the test case data to the request buffer
 * - Calculates the timeout in nanoseconds
 * - Executes the transaction using @ref camrtc_ivc_dbg_full_frame_xact
 * - Flushes the trace buffer using @ref tegra_camrtc_flush_trace
 * - Displays the test result status and runtime
 * - Writes the result text to the sequence file
 *
 * @param[in] file         Pointer to the sequence file
 * @param[in] req          Pointer to the debug request structure
 * @param[out] resp        Pointer to the debug response structure
 * @param[in] data_offset  Offset in the request/response structure where data begins
 *
 * @retval 0 on success
 * @retval (int) Return code from @ref camrtc_ivc_dbg_full_frame_xact() or
 * @ref tegra_ivc_channel_runtime_get()
 */
static int camrtc_test_run_and_show_result(struct seq_file *file,
				struct camrtc_dbg_request *req,
				struct camrtc_dbg_response *resp,
				size_t data_offset)
{
	struct tegra_ivc_channel *ch = file->private;
	struct camrtc_debug *crd = tegra_ivc_channel_get_drvdata(ch);
	const char *test_case = crd->parameters.test_case;
	size_t test_case_size = crd->parameters.test_case_size;
	unsigned long timeout = crd->parameters.test_timeout;
	uint64_t ns;
	size_t req_size = ch->ivc.frame_size;
	size_t resp_size = ch->ivc.frame_size;
	int ret;
	const char *result = (const void *)resp + data_offset;
	size_t result_size;
	const char *nul;

	(void)__builtin_usubl_overflow(resp_size, data_offset, &result_size);
	if (test_case_size > camrtc_dbgfs_get_max_test_size(ch)) {
		dev_warn(&ch->dev, "%s: test_case_size > camrtc_dbgfs_get_max_test_size(ch)\n", __func__);
		test_case_size = camrtc_dbgfs_get_max_test_size(ch);
	}

	memcpy((char *)req + data_offset, test_case, test_case_size);

	/* Timeout is in ms, run_test_data.timeout in ns */
	if (timeout > 40)
		ns = 1000000ULL * (timeout - 20);
	else
		ns = 1000000ULL * (timeout / 2);

	BUILD_BUG_ON_MISMATCH(
		struct camrtc_dbg_request, run_mem_test_data.timeout,
		struct camrtc_dbg_request, run_test_data.timeout);

	ret = tegra_ivc_channel_runtime_get(ch);
	if (ret < 0)
		return ret;

	req->data.run_test_data.timeout = ns;

	ret = camrtc_ivc_dbg_full_frame_xact(ch, req, req_size,
					resp, resp_size, timeout);

	tegra_camrtc_flush_trace(camrtc_get_device(ch));

	if (ret < 0) {
		if (ret != -ECONNRESET) {
			dev_info(&ch->dev, "rebooting after a failed test run");
			(void)tegra_camrtc_reboot(camrtc_get_device(ch));
		}
		goto runtime_put;
	}

	BUILD_BUG_ON_MISMATCH(
		struct camrtc_dbg_response, run_mem_test_data.timeout,
		struct camrtc_dbg_response, run_test_data.timeout);

	ns = resp->data.run_test_data.timeout;

	seq_printf(file, "result=%u runtime=%llu.%06llu ms\n\n",
		resp->status, ns / 1000000, ns % 1000000);

	nul = memchr(result, '\0', result_size);
	if (nul)
		seq_write(file, result, nul - result);
	else
		seq_write(file, result, result_size);

runtime_put:
	tegra_ivc_channel_runtime_put(ch);

	return ret;
}

/**
 * @brief Unmaps all or selected DMA mappings for a test memory buffer
 *
 * This function unmaps DMA mappings previously created for a test memory buffer
 * across multiple devices. It performs the following operations:
 * - Checks if the memory buffer is mapped (ptr is not NULL)
 * - Iterates through all mapped devices in the memory structure
 * - Unmaps the memory from each device using @ref dma_unmap_single
 * - Optionally keeps the primary memory device mapping if 'all' is false
 *
 * @param[in] crd  Pointer to the camera debug structure
 * @param[in] mem  Pointer to the test memory structure containing mappings
 * @param[in] all  Flag to indicate if all mappings should be unmapped
 *                 - true: unmap all device mappings
 *                 - false: keep the primary memory device mapping
 */
static void camrtc_run_rmem_unmap_all(struct camrtc_debug *crd,
		struct camrtc_test_mem *mem, bool all)
{
	int i;
	struct device *mem_dev = camrtc_dbgfs_memory_dev(crd);

	/* Nothing to unmap */
	if (mem->ptr == NULL)
		return;

	for (i = 0; i < mem->dev_index; i++) {
		struct device *dev = mem->devices[i].dev;
		dma_addr_t iova = mem->devices[i].dev_iova;

		if (dev == NULL)
			break;

		/* keep mem_dev mapped unless forced */
		if (!all && (dev == mem_dev))
			continue;

		dma_unmap_single(dev, iova,
			mem->size, DMA_BIDIRECTIONAL);
	}
}

/**
 * @brief Maximum value for a signed integer
 *
 * This macro defines the maximum value that can be stored in a signed integer.
 * It represents (2^31 - 1) for typical 32-bit systems.
 */
#define INT_MAX ((int)(~0U >> 1))

/**
 * @brief Maps a memory buffer for DMA access from a specific device
 *
 * This function maps a memory buffer for DMA access from a given device,
 * handling the mapping differently based on whether the target device is
 * the same as the memory device or using reserved memory. It performs
 * the following operations:
 * - Validates that the device index hasn't exceeded maximum allowed devices
 * - If mapping to the memory device, reuses the existing IOVA and syncs
 * - For reserved memory, creates a new DMA mapping with @ref dma_map_single
 * - For non-reserved memory, creates a scatter-gather table and maps it
 * - Records the device and IOVA in the memory structure
 *
 * @param[in] ch           Pointer to the IVC channel
 * @param[in] mem_dev      Pointer to the primary memory device
 * @param[in] dev          Pointer to the target device for mapping
 * @param[in] sgt          Pointer to the scatter-gather table
 * @param[in,out] mem      Pointer to the test memory structure
 * @param[out] return_iova Pointer to store the resulting IOVA address
 *
 * @retval 0 on success
 * @retval (int) Error code from @ref dma_get_sgtable
 * @retval -ENOMEM if device list exhausted or failed to allocate memory with @ref dma_map_single()
 * @retval -ENXIO if failed to map memory
 * @retval -EINVAL if sync operation failed
 */
static int camrtc_run_mem_map(struct tegra_ivc_channel *ch,
		struct device *mem_dev,
		struct device *dev,
		struct sg_table *sgt,
		struct camrtc_test_mem *mem,
		uint64_t *return_iova)
{
	int ret = 0;

	*return_iova = 0ULL;

	if (dev == NULL)
		return 0;

	if (mem->dev_index >= CAMRTC_TEST_CAM_DEVICES) {
		pr_err("%s: device list exhausted\n", __func__);
		return -ENOMEM;
	}

	if (mem_dev == dev) {
		*return_iova = mem->iova;
		dma_sync_single_for_device(dev, mem->iova, mem->size,
		DMA_BIDIRECTIONAL);
		goto done;
	}

	if (_camdbg_rmem.enabled) {
		*return_iova = dma_map_single(dev, mem->ptr,
					mem->size, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev, *return_iova)) {
			pr_err("%s: dma map failed\n", __func__);
			*return_iova = 0ULL;
			return -ENOMEM;
		}
		dma_sync_single_for_device(dev, mem->iova, mem->size,
					   DMA_BIDIRECTIONAL);
	} else {
		ret = dma_get_sgtable(dev, sgt, mem->ptr, mem->iova, mem->size);
		if (ret < 0) {
			dev_err(&ch->dev, "dma_get_sgtable for %s failed\n",
				dev_name(dev));
			return ret;
		}

		if (!dma_map_sg(dev, sgt->sgl, sgt->orig_nents,
				DMA_BIDIRECTIONAL)) {
			dev_err(&ch->dev, "failed to map %s mem at 0x%llx\n",
				dev_name(dev), (u64)mem->iova);
			sg_free_table(sgt);
			ret = -ENXIO;
		}

		*return_iova = sgt->sgl->dma_address;
		if (sgt->nents <= INT_MAX)
			dma_sync_sg_for_device(dev, sgt->sgl, (int)sgt->nents, DMA_BIDIRECTIONAL);
		else
			ret = -EINVAL;
	}

done:
	mem->devices[mem->dev_index].dev = dev;
	mem->devices[mem->dev_index++].dev_iova = *return_iova;

	return ret;
}

/**
 * @brief Sets the memory bandwidth for camera operation
 *
 * This function configures the memory bandwidth through the interconnect
 * controller path for camera operations. It performs the following operations:
 * - Checks if an interconnect path is available
 * - Sets the bandwidth using @ref icc_set_bw with the provided value
 * - Logs success or failure of the bandwidth setting operation
 *
 * @param[in] crd  Pointer to the camera debug structure
 * @param[in] bw   Bandwidth value to set in bytes per second
 */
static void camrtc_membw_set(struct camrtc_debug *crd, u32 bw)
{
	int ret;

	if (crd->icc_path) {
		ret = icc_set_bw(crd->icc_path, 0, bw);
		if (ret)
			dev_err(crd->mem_devices[0],
				"set icc bw [%u] failed: %d\n", bw, ret);
		else
			dev_dbg(crd->mem_devices[0], "requested icc bw %u\n", bw);
	}
}

/**
 * @brief Runs a memory test on the camera-rtcpu
 *
 * This function executes a memory test by setting up memory mappings across
 * multiple devices (RCE, VI, ISP, VI2, ISP1), sending test data to the camera-rtcpu,
 * and processing the results. It performs the following operations:
 * - Sets the memory bandwidth using @ref camrtc_membw_set
 * - Allocates scratch memory if not already allocated
 * - Maps memory across multiple camera devices using @ref camrtc_run_mem_map
 * - Runs the test and displays results using @ref camrtc_test_run_and_show_result
 * - Synchronizes memory for CPU access
 * - Cleans up by unmapping memory and resetting mapping information
 *
 * @param[in] file       Pointer to the sequence file
 * @param[in,out] req    Pointer to the debug request structure
 * @param[out] resp      Pointer to the debug response structure
 *
 * @retval 0 on success
 * @retval (int) Error code from @ref camrtc_ivc_dbg_full_frame_xact()
 * @retval -ENOMEM if failed to map memory with @ref dma_map_single() or @ref dma_alloc_coherent()
 */
static int camrtc_run_mem_test(struct seq_file *file,
			struct camrtc_dbg_request *req,
			struct camrtc_dbg_response *resp)
{
	struct tegra_ivc_channel *ch = file->private;
	struct camrtc_debug *crd = tegra_ivc_channel_get_drvdata(ch);
	struct camrtc_dbg_test_mem *testmem;
	size_t i;
	int ret = 0;
	struct device *mem_dev = camrtc_dbgfs_memory_dev(crd);
	struct device *rce_dev = crd->mem_devices[0];
	struct sg_table rce_sgt[ARRAY_SIZE(crd->mem)];
	struct device *vi_dev = crd->mem_devices[1];
	struct sg_table vi_sgt[ARRAY_SIZE(crd->mem)];
	struct device *isp_dev = crd->mem_devices[2];
	struct sg_table isp_sgt[ARRAY_SIZE(crd->mem)];
	struct device *vi2_dev = crd->mem_devices[3];
	struct sg_table vi2_sgt[ARRAY_SIZE(crd->mem)];
	struct camrtc_test_mem *mem0 = &crd->mem[0];
	struct device *isp1_dev = crd->mem_devices[4];
	struct sg_table isp1_sgt[ARRAY_SIZE(crd->mem)];

	memset(rce_sgt, 0, sizeof(rce_sgt));
	memset(vi_sgt, 0, sizeof(vi_sgt));
	memset(isp_sgt, 0, sizeof(isp_sgt));
	memset(vi2_sgt, 0, sizeof(vi2_sgt));
	memset(isp1_sgt, 0, sizeof(isp1_sgt));

	req->req_type = CAMRTC_REQ_RUN_MEM_TEST;

	/* Allocate 6MB scratch memory in mem0 by default */
	if (!mem0->used) {
		const size_t size = 6U << 20U; /* 6 MB */
		dma_addr_t iova;
		void *ptr;
		struct iommu_domain *domain = iommu_get_domain_for_dev(mem_dev);

		if (mem0->ptr) {
			if (_camdbg_rmem.enabled)
				camrtc_run_rmem_unmap_all(crd, mem0, true);
			else
				dma_free_coherent(mem_dev, mem0->size,
					mem0->ptr, mem0->iova);

			memset(mem0, 0, sizeof(*mem0));
		}

		if (_camdbg_rmem.enabled) {
			if (_camdbg_rmem.mem_ctxs[0].size < size) {
				pr_err(
				"%s: mem [%lu] < req size [%lu]\n",
				__func__, _camdbg_rmem.mem_ctxs[0].size,
				size);
				return -ENOMEM;
			}

			ptr = phys_to_virt(_camdbg_rmem.mem_ctxs[0].address);

			iova = dma_map_single(mem_dev, ptr, size,
						DMA_BIDIRECTIONAL);
			if (dma_mapping_error(mem_dev, iova)) {
				pr_err("%s: dma map failed\n", __func__);
				return -ENOMEM;
			}
		} else {
			ptr = dma_alloc_coherent(mem_dev, size, &iova,
					GFP_KERNEL | __GFP_ZERO);
			if (ptr == NULL)
				return -ENOMEM;
		}

		mem0->ptr = ptr;
		mem0->size = size;

		/* If mem_dev is not connected to SMMU, the iova is physical */
		if (domain)
			mem0->phys_addr = iommu_iova_to_phys(domain, iova);
		else
			mem0->phys_addr = iova;

		mem0->iova = iova;
		mem0->used = size;
	}

	camrtc_membw_set(crd, crd->parameters.test_bw);

	for (i = 0; i < ARRAY_SIZE(crd->mem); i++) {
		struct camrtc_test_mem *mem = &crd->mem[i];

		if (mem->used == 0)
			continue;

		testmem = &req->data.run_mem_test_data.mem[i];

		testmem->size = mem->used;
		testmem->page_size = PAGE_SIZE;
		testmem->phys_addr = mem->phys_addr;

		ret = camrtc_run_mem_map(ch, mem_dev, rce_dev, &rce_sgt[i], mem,
			&testmem->rtcpu_iova);
		if (ret < 0)
			goto unmap;

		ret = camrtc_run_mem_map(ch, mem_dev, vi_dev, &vi_sgt[i], mem,
			&testmem->vi_iova);
		if (ret < 0)
			goto unmap;

		ret = camrtc_run_mem_map(ch, mem_dev, isp_dev, &isp_sgt[i], mem,
			&testmem->isp_iova);
		if (ret < 0)
			goto unmap;

		ret = camrtc_run_mem_map(ch, mem_dev, vi2_dev, &vi2_sgt[i], mem,
			&testmem->vi2_iova);
		if (ret < 0)
			goto unmap;

		ret = camrtc_run_mem_map(ch, mem_dev, isp1_dev, &isp1_sgt[i], mem,
			&testmem->isp1_iova);
		if (ret < 0)
			goto unmap;

	}

	BUILD_BUG_ON_MISMATCH(
		struct camrtc_dbg_request, run_mem_test_data.data,
		struct camrtc_dbg_response, run_mem_test_data.data);

	ret = camrtc_test_run_and_show_result(file, req, resp,
					offsetof(struct camrtc_dbg_response,
						data.run_mem_test_data.data));
	if (ret < 0)
		goto unmap;

	for (i = 0; i < ARRAY_SIZE(crd->mem); i++) {
		struct camrtc_test_mem *mem = &crd->mem[i];

		if (mem->size == 0)
			continue;

		testmem = &resp->data.run_mem_test_data.mem[i];
		if (testmem->size > mem->size)
			dev_warn(mem_dev, "%s: testmem->size > mem->size\n", __func__);
		else
			mem->used = testmem->size;

		if (_camdbg_rmem.enabled) {
			dma_sync_single_for_cpu(mem_dev, mem->iova, mem->used,
						DMA_BIDIRECTIONAL);
		} else {
			if (rce_sgt[i].sgl) {
				dma_sync_sg_for_cpu(mem_dev, rce_sgt[i].sgl,
						    rce_sgt[i].orig_nents, DMA_BIDIRECTIONAL);
			}
			if (vi_sgt[i].sgl) {
				dma_sync_sg_for_cpu(mem_dev, vi_sgt[i].sgl,
						    vi_sgt[i].orig_nents, DMA_BIDIRECTIONAL);
			}
			if (isp_sgt[i].sgl) {
				dma_sync_sg_for_cpu(mem_dev, isp_sgt[i].sgl,
						    isp_sgt[i].orig_nents, DMA_BIDIRECTIONAL);
			}
			if (vi2_sgt[i].sgl) {
				dma_sync_sg_for_cpu(mem_dev, vi2_sgt[i].sgl,
						    vi2_sgt[i].orig_nents, DMA_BIDIRECTIONAL);
			}
		}
	}

unmap:
	camrtc_membw_set(crd, 0);

	for (i = 0; i < ARRAY_SIZE(vi_sgt); i++) {
		if (rce_sgt[i].sgl) {
			dma_unmap_sg(rce_dev, rce_sgt[i].sgl,
				rce_sgt[i].orig_nents, DMA_BIDIRECTIONAL);
			sg_free_table(&rce_sgt[i]);
		}
		if (vi_sgt[i].sgl) {
			dma_unmap_sg(vi_dev, vi_sgt[i].sgl,
				vi_sgt[i].orig_nents, DMA_BIDIRECTIONAL);
			sg_free_table(&vi_sgt[i]);
		}
		if (isp_sgt[i].sgl) {
			dma_unmap_sg(isp_dev, isp_sgt[i].sgl,
				isp_sgt[i].orig_nents, DMA_BIDIRECTIONAL);
			sg_free_table(&isp_sgt[i]);
		}
		if (vi2_sgt[i].sgl) {
			dma_unmap_sg(vi2_dev, vi2_sgt[i].sgl,
				vi2_sgt[i].orig_nents, DMA_BIDIRECTIONAL);
			sg_free_table(&vi2_sgt[i]);
		}
		if (isp1_sgt[i].sgl) {
			dma_unmap_sg(isp1_dev, isp1_sgt[i].sgl,
				isp1_sgt[i].orig_nents, DMA_BIDIRECTIONAL);
			sg_free_table(&isp1_sgt[i]);
		}
	}

	if (_camdbg_rmem.enabled) {
		for (i = 0; i < ARRAY_SIZE(crd->mem); i++) {
			struct camrtc_test_mem *mem = &crd->mem[i];
			camrtc_run_rmem_unmap_all(crd, mem, false);
		}
	}

	/* Reset mapping info, memory can still be used by cpu tests */
	for (i = 0; i < ARRAY_SIZE(crd->mem); i++) {
		crd->mem[i].dev_index = 0U;
		memset(&crd->mem[i].devices, 0,
				(ARRAY_SIZE(crd->mem[i].devices) *
				sizeof(struct camrtc_test_device)));
	}

	return ret;
}

/**
 * @brief Show function for test result debugfs file
 *
 * This function is called when the test result debugfs file is read.
 * It allocates memory for request and response structures, runs the
 * memory test using @ref camrtc_run_mem_test, and displays the results.
 *
 * @param[in] file  Pointer to the sequence file
 * @param[in] data  Private data pointer (unused)
 *
 * @retval 0 on success
 * @retval (int) Error code from @ref camrtc_run_mem_test
 * @retval -ENOMEM if failed to allocate memory with @ref kzalloc()
 */
static int camrtc_dbgfs_show_test_result(struct seq_file *file, void *data)
{
	struct tegra_ivc_channel *ch = file->private;
	size_t mulRet;
	void *mem;
	struct camrtc_dbg_request *req;
	struct camrtc_dbg_response *resp;
	int ret;

	(void)__builtin_umull_overflow(2UL, ch->ivc.frame_size, &mulRet);
	mem = kzalloc(mulRet, GFP_KERNEL | __GFP_ZERO);
	req = mem;
	resp = mem + ch->ivc.frame_size;

	if (mem == NULL)
		return -ENOMEM;

	ret = camrtc_run_mem_test(file, req, resp);
	kfree(mem);

	return ret;
}

/**
 * @brief File operations for test result
 *
 * This macro defines the file operations for the test result debugfs file.
 * It specifies that @ref camrtc_dbgfs_show_test_result should be called
 * when the file is read.
 */
DEFINE_SEQ_FOPS(camrtc_dbgfs_fops_test_result, camrtc_dbgfs_show_test_result);

/**
 * @brief Show function for test list debugfs file
 *
 * This function is called when the test list debugfs file is read.
 * It sends a request to the camera-rtcpu to retrieve the list of available
 * tests and displays the results in the sequence file. It performs the
 * following operations:
 * - Prepares a request to run the "list" test
 * - Executes the transaction using @ref camrtc_ivc_dbg_full_frame_xact
 * - Formats and writes the test list to the sequence file
 *
 * @param[in] file  Pointer to the sequence file
 * @param[in] data  Private data pointer (unused)
 *
 * @retval 0 on success
 * @retval (int) Error code from @ref camrtc_ivc_dbg_full_frame_xact()
 * @retval -ENOMEM if failed to allocate memory with @ref kzalloc()
 */
static int camrtc_dbgfs_show_test_list(struct seq_file *file, void *data)
{
	struct tegra_ivc_channel *ch = file->private;
	struct camrtc_dbg_request req = {
		.req_type = CAMRTC_REQ_RUN_TEST,
	};
	struct camrtc_dbg_response *resp;
	int ret;

	resp = kzalloc(ch->ivc.frame_size, GFP_KERNEL | __GFP_ZERO);
	if (resp == NULL)
		return -ENOMEM;

	memset(req.data.run_test_data.data, 0,
		sizeof(req.data.run_test_data.data));
	strscpy(req.data.run_test_data.data, "list\n",
		sizeof(req.data.run_test_data.data));

	ret = camrtc_ivc_dbg_full_frame_xact(ch, &req, sizeof(req),
					resp, ch->ivc.frame_size, 0);
	if (ret == 0 && resp->status == CAMRTC_STATUS_OK) {
		char const *list = (char const *)resp->data.run_test_data.data;
		size_t textsize = ch->ivc.frame_size -
			offsetof(struct camrtc_dbg_response,
				data.run_test_data.data);
		size_t i;

		/* Remove first line */
		for (i = 0; i < textsize; i++)
			if (list[i] == '\n')
				break;
		for (; i < textsize; i++)
			if (list[i] != '\n' && list[i] != '\r')
				break;

		seq_printf(file, "%.*s", (int)(textsize - i), list + i);
	}

	kfree(resp);

	return ret;
}

/**
 * @brief File operations for test list
 *
 * This macro defines the file operations for the test list debugfs file.
 * It specifies that @ref camrtc_dbgfs_show_test_list should be called
 * when the file is read.
 */
DEFINE_SEQ_FOPS(camrtc_dbgfs_fops_test_list, camrtc_dbgfs_show_test_list);

/**
 * @brief Sends a coverage control message to the camera-rtcpu
 *
 * This function sends a message to control the Falcon coverage functionality.
 * It performs the following operations:
 * - Prepares a request with coverage control parameters
 * - Executes the transaction using @ref camrtc_ivc_dbg_xact
 * - Handles error cases and status checking
 *
 * @param[in] cov    Pointer to the Falcon coverage structure
 * @param[out] resp  Pointer to the debug response structure
 * @param[in] flush  Flag to indicate if coverage data should be flushed
 * @param[in] reset  Flag to indicate if coverage data should be reset
 *
 * @retval 0 on success
 * @retval (int) Error code from @ref camrtc_ivc_dbg_xact
 * @retval -ENODEV if IVC error or bad status
 * @retval -EOVERFLOW if coverage buffer is full
 */
static int camrtc_coverage_msg(struct camrtc_falcon_coverage *cov,
			struct camrtc_dbg_response *resp,
			bool flush, bool reset)
{
	struct camrtc_dbg_request req = {
		.req_type = CAMRTC_REQ_SET_FALCON_COVERAGE,
		.data = {
			.coverage_data = {
				.falcon_id = cov->id,
				.size = cov->enabled ? cov->mem.size : 0,
				.iova = cov->enabled ? cov->falc_iova : 0,
				.flush = flush ? 1 : 0,
				.reset = reset ? 1 : 0,
			},
		},
	};
	struct tegra_ivc_channel *ch = cov->ch;
	int ret;

	ret = camrtc_ivc_dbg_xact(ch, &req, resp, 200);

	if (ret || (resp->status != CAMRTC_STATUS_OK)) {
		dev_warn(&ch->dev, "Coverage IVC error: %d, status %u, id %u\n",
				ret, resp->status, cov->id);
		ret = -ENODEV;
	} else if (resp->data.coverage_stat.full == 1) {
		ret = -EOVERFLOW;
	}

	return ret;
}

/**
 * @brief Checks if Falcon coverage is supported
 *
 * This function determines whether the Falcon coverage functionality is
 * supported by sending a test message to the camera-rtcpu.
 *
 * @param[in] cov  Pointer to the Falcon coverage structure
 *
 * @retval true if coverage is supported
 * @retval false otherwise
 */
static bool camrtc_coverage_is_supported(struct camrtc_falcon_coverage *cov)
{
	struct camrtc_dbg_response resp;

	(void)camrtc_coverage_msg(cov, &resp, false, false);

	return (resp.status == CAMRTC_STATUS_OK);
}

/**
 * @brief Read callback for Falcon coverage debugfs file
 *
 * This function is called when the Falcon coverage debugfs file is read.
 * It retrieves coverage data from the Falcon processor and copies it to
 * user space. It performs the following operations:
 * - Checks if coverage is enabled
 * - Flushes coverage data from the Falcon if at the beginning of the file
 * - Synchronizes memory for CPU access
 * - Copies data to user space using @ref simple_read_from_buffer
 *
 * @param[in] file   Pointer to the file structure
 * @param[out] buf   User space buffer
 * @param[in] count  Number of bytes to read
 * @param[in,out] f_pos  File position pointer
 *
 * @retval (ssize_t) Number of bytes read on success
 * @retval (int) Error code from @ref simple_read_from_buffer or @ref camrtc_coverage_msg
 * @retval -ENODEV if coverage is not enabled
 */
static ssize_t camrtc_read_falcon_coverage(struct file *file,
		char __user *buf, size_t count, loff_t *f_pos)
{
	struct camrtc_falcon_coverage *cov = file->f_inode->i_private;
	struct camrtc_dbg_response resp;
	ssize_t ret = 0;

	if (!cov->enabled) {
		ret = -ENODEV;
		goto done;
	}

	/* In the beginning, do a flush */
	if (*f_pos == 0) {
		/* Flush falcon buffer */
		ret = camrtc_coverage_msg(cov, &resp, true, false);

		if (ret)
			goto done;

		cov->mem.used = resp.data.coverage_stat.bytes_written;

		dma_sync_single_for_cpu(cov->mem_dev, cov->mem.iova,
				cov->mem.size, DMA_BIDIRECTIONAL);
	}

	ret = simple_read_from_buffer(buf, count, f_pos,
			cov->mem.ptr, cov->mem.used);
done:
	return ret;
}

/**
 * @brief Write callback for Falcon coverage debugfs file
 *
 * This function is called when the Falcon coverage debugfs file is written.
 * Writing to this file resets the coverage data. It performs the following operations:
 * - Checks if coverage is enabled
 * - Clears the coverage memory buffer
 * - Sends a reset message to the Falcon using @ref camrtc_coverage_msg
 *
 * @param[in] file   Pointer to the file structure
 * @param[in] buf    User space buffer (content ignored)
 * @param[in] count  Number of bytes written
 * @param[in,out] f_pos  File position pointer
 *
 * @retval (ssize_t) Number of bytes written on success
 * @retval -ENODEV if coverage is not enabled
 */
static ssize_t camrtc_write_falcon_coverage(struct file *file,
		const char __user *buf, size_t count, loff_t *f_pos)
{
	struct camrtc_falcon_coverage *cov = file->f_inode->i_private;
	struct camrtc_dbg_response resp;
	ssize_t ret = count;

	if (cov->enabled) {
		memset(cov->mem.ptr, 0, cov->mem.size);
		if (camrtc_coverage_msg(cov, &resp, false, true))
			ret = -ENODEV;
		else
			*f_pos += count;
	} else {
		ret = -ENODEV;
	}

	return ret;
}

/**
 * @brief File operations for Falcon coverage
 *
 * This structure defines the file operations for the Falcon coverage debugfs file.
 * - read: Implemented by @ref camrtc_read_falcon_coverage
 * - write: Implemented by @ref camrtc_write_falcon_coverage
 */
static const struct file_operations camrtc_dbgfs_fops_falcon_coverage = {
	.read = camrtc_read_falcon_coverage,
	.write = camrtc_write_falcon_coverage,
};

/**
 * @brief Enables Falcon coverage functionality
 *
 * This function enables the coverage functionality for a Falcon processor.
 * It performs the following operations:
 * - Checks if coverage is already enabled
 * - Verifies coverage is supported using @ref camrtc_coverage_is_supported
 * - Allocates memory for coverage data using @ref dma_alloc_coherent
 * - Maps the memory to the Falcon processor using @ref camrtc_run_mem_map
 * - Acquires runtime PM reference to keep rtcpu alive
 *
 * @param[in,out] cov  Pointer to the Falcon coverage structure
 *
 * @retval 0 on success
 * @retval (int) Error code from @ref tegra_ivc_channel_runtime_get()
 * @retval -ENODEV if coverage is not supported
 */
static int camrtc_falcon_coverage_enable(struct camrtc_falcon_coverage *cov)
{
	struct tegra_ivc_channel *ch = cov->ch;
	struct device *mem_dev = cov->mem_dev;
	struct device *falcon_dev = cov->falcon_dev;
	struct camrtc_dbg_response resp;
	int ret = 0;

	if (cov->enabled)
		goto done;

	if (!camrtc_coverage_is_supported(cov)) {
		ret = -ENODEV;
		goto done;
	}

	cov->mem.ptr = dma_alloc_coherent(mem_dev,
				FALCON_COVERAGE_MEM_SIZE,
				&cov->mem.iova,
				GFP_KERNEL | __GFP_ZERO);
	if (cov->mem.ptr == NULL) {
		dev_warn(&ch->dev,
			"Failed to allocate Falcon 0x%02x coverage memory!\n",
			cov->id);
		goto error;
	}

	cov->mem.size = FALCON_COVERAGE_MEM_SIZE;

	if (camrtc_run_mem_map(ch, cov->mem_dev, falcon_dev,
				&cov->sgt, &cov->mem,
				&cov->falc_iova)) {
		dev_warn(&ch->dev,
			"Failed to map Falcon 0x%02x coverage memory\n",
			cov->id);
		goto clean_mem;
	}

	/* Keep rtcpu alive when falcon coverage is in use. */
	ret = tegra_ivc_channel_runtime_get(ch);
	if (ret < 0)
		goto clean_mem;

	cov->enabled = true;

	/* Sync state with rtcpu */
	camrtc_coverage_msg(cov, &resp, false, false);

	dev_dbg(&ch->dev, "Falcon 0x%02x code coverage enabled.\n",
			cov->id);

done:
	return ret;

clean_mem:
	dma_free_coherent(mem_dev, cov->mem.size, cov->mem.ptr, cov->mem.iova);
	memset(&cov->mem, 0, sizeof(struct camrtc_test_mem));
	cov->enabled = false;

error:
	return ret;
}

/**
 * @brief Disables Falcon coverage functionality
 *
 * This function disables the coverage functionality for a Falcon processor
 * and releases associated resources. It performs the following operations:
 * - Checks if coverage is already disabled
 * - Disables coverage by sending a control message to the Falcon
 * - Unmaps DMA memory from the Falcon processor
 * - Frees the coverage memory using @ref dma_free_coherent
 * - Releases the runtime PM reference
 *
 * @param[in,out] cov  Pointer to the Falcon coverage structure
 */
static void camrtc_falcon_coverage_disable(struct camrtc_falcon_coverage *cov)
{
	struct tegra_ivc_channel *ch = cov->ch;
	struct device *mem_dev = cov->mem_dev;
	struct device *falcon_dev = cov->falcon_dev;
	struct camrtc_dbg_response resp;

	if (!cov->enabled)
		return;

	/* Disable and sync with rtpcu */
	cov->enabled = false;
	camrtc_coverage_msg(cov, &resp, false, false);

	if (cov->sgt.sgl) {
		dma_unmap_sg(falcon_dev, cov->sgt.sgl,
			cov->sgt.orig_nents, DMA_BIDIRECTIONAL);
		sg_free_table(&cov->sgt);
	}

	if (cov->mem.ptr) {
		dma_free_coherent(mem_dev, cov->mem.size,
			cov->mem.ptr, cov->mem.iova);
		memset(&cov->mem, 0, sizeof(struct camrtc_test_mem));
	}

	tegra_ivc_channel_runtime_put(ch);
}

/**
 * @brief Show callback for coverage enable debugfs file
 *
 * This function is called when the coverage enable debugfs file is read.
 * It returns the current enabled state of the Falcon coverage.
 *
 * @param[in] data  Private data pointer (Falcon coverage structure)
 * @param[out] val  Pointer to store the enabled state (1 = enabled, 0 = disabled)
 *
 * @retval 0 on success
 */
static int camrtc_dbgfs_show_coverage_enable(void *data, u64 *val)
{
	struct camrtc_falcon_coverage *cov = data;

	*val = cov->enabled ? 1 : 0;

	return 0;
}

/**
 * @brief Store callback for coverage enable debugfs file
 *
 * This function is called when the coverage enable debugfs file is written.
 * It enables or disables the Falcon coverage based on the written value.
 *
 * @param[in] data  Private data pointer (Falcon coverage structure)
 * @param[in] val   Value to set (non-zero = enable, zero = disable)
 *
 * @retval 0 on success
 * @retval (int) Error code from @ref camrtc_falcon_coverage_enable
 */
static int camrtc_dbgfs_store_coverage_enable(void *data, u64 val)
{
	struct camrtc_falcon_coverage *cov = data;
	bool enable = (val != 0) ? true : false;
	int ret = 0;

	if (cov->enabled != enable) {
		if (enable)
			ret = camrtc_falcon_coverage_enable(cov);
		else
			camrtc_falcon_coverage_disable(cov);
	}

	return ret;
}

DEFINE_SIMPLE_ATTRIBUTE(camrtc_dbgfs_fops_coverage_enable,
			camrtc_dbgfs_show_coverage_enable,
			camrtc_dbgfs_store_coverage_enable,
			"%lld\n");

#define TEGRA_APS_AST_CONTROL			0x0
#define TEGRA_APS_AST_STREAMID_CTL		0x20
#define TEGRA_APS_AST_REGION_0_SLAVE_BASE_LO	0x100
#define TEGRA_APS_AST_REGION_0_SLAVE_BASE_HI	0x104
#define TEGRA_APS_AST_REGION_0_MASK_LO		0x108
#define TEGRA_APS_AST_REGION_0_MASK_HI		0x10c
#define TEGRA_APS_AST_REGION_0_MASTER_BASE_LO	0x110
#define TEGRA_APS_AST_REGION_0_MASTER_BASE_HI	0x114
#define TEGRA_APS_AST_REGION_0_CONTROL		0x118

#define TEGRA_APS_AST_REGION_STRIDE		0x20

#define AST_RGN_CTRL_VM_INDEX		15
#define AST_RGN_CTRL_SNOOP		BIT(2)

#define AST_ADDR_MASK64			(~0xfffULL)

struct tegra_ast_region_info {
	u8  enabled;
	u8  lock;
	u8  snoop;
	u8  non_secure;

	u8  ns_passthru;
	u8  carveout_id;
	u8  carveout_al;
	u8  vpr_rd;

	u8  vpr_wr;
	u8  vpr_passthru;
	u8  vm_index;
	u8  physical;

	u8  stream_id;
	u8  stream_id_enabled;
	u8  pad[2];

	u64 slave;
	u64 mask;
	u64 master;
	u32 control;
};

/**
 * @brief Gets AST region configuration information
 *
 * This function reads the configuration of an AST region from hardware
 * registers and populates a region information structure. It performs
 * the following operations:
 * - Reads control register and extracts flags
 * - Reads stream ID configuration
 * - Reads slave, mask, and master address values
 * - Formats the information into the provided structure
 *
 * @param[in] base   Base address of the AST registers
 * @param[in] region Region number to retrieve
 * @param[out] info  Pointer to store the region information
 */
static void tegra_ast_get_region_info(void __iomem *base,
			u32 region,
			struct tegra_ast_region_info *info)
{
	u32 offset;
	u32 vmidx, stream_id, gcontrol, control;
	u64 lo, hi;

	(void)__builtin_umul_overflow(region, TEGRA_APS_AST_REGION_STRIDE, &offset);
	control = readl(base + TEGRA_APS_AST_REGION_0_CONTROL + offset);
	info->control = control;

	info->lock = (control & BIT(0)) != 0;
	info->snoop = (control & BIT(2)) != 0;
	info->non_secure = (control & BIT(3)) != 0;
	info->ns_passthru = (control & BIT(4)) != 0;
	info->carveout_id = (control >> 5) & (0x1f);
	info->carveout_al = (control >> 10) & 0x3;
	info->vpr_rd = (control & BIT(12)) != 0;
	info->vpr_wr = (control & BIT(13)) != 0;
	info->vpr_passthru = (control & BIT(14)) != 0;
	vmidx = (control >> AST_RGN_CTRL_VM_INDEX) & 0xf;
	info->vm_index = vmidx;
	info->physical = (control & BIT(19)) != 0;

	if (info->physical) {
		gcontrol = readl(base + TEGRA_APS_AST_CONTROL);
		info->stream_id = (gcontrol >> 22) & 0x7F;
		info->stream_id_enabled = 1;
	} else {
		stream_id = readl(base + TEGRA_APS_AST_STREAMID_CTL +
				(4 * vmidx));
		info->stream_id = (stream_id >> 8) & 0xFF;
		info->stream_id_enabled = (stream_id & BIT(0)) != 0;
	}

	lo = readl(base + TEGRA_APS_AST_REGION_0_SLAVE_BASE_LO + offset);
	hi = readl(base + TEGRA_APS_AST_REGION_0_SLAVE_BASE_HI + offset);

	info->slave = ((hi << 32U) + lo) & AST_ADDR_MASK64;
	info->enabled = (lo & BIT(0)) != 0;

	hi = readl(base + TEGRA_APS_AST_REGION_0_MASK_HI + offset);
	lo = readl(base + TEGRA_APS_AST_REGION_0_MASK_LO + offset);

	info->mask = ((hi << 32) + lo) | ~AST_ADDR_MASK64;

	hi = readl(base + TEGRA_APS_AST_REGION_0_MASTER_BASE_HI + offset);
	lo = readl(base + TEGRA_APS_AST_REGION_0_MASTER_BASE_LO + offset);

	info->master = ((hi << 32U) + lo) & AST_ADDR_MASK64;
}

/**
 * @brief Maps I/O memory by name from device tree
 *
 * This function maps I/O memory based on a register name from the device tree.
 * It performs the following operations:
 * - Looks up the register index by name in the device tree
 * - Maps the I/O memory using @ref of_iomap if found
 *
 * @param[in] dev   Pointer to the device structure
 * @param[in] name  Name of the register to map
 *
 * @retval (void __iomem *) Mapped I/O memory address on success
 * @retval (void __iomem *) Error pointer on failure
 */
static void __iomem *iomap_byname(struct device *dev, const char *name)
{
	int index = of_property_match_string(dev->of_node, "reg-names", name);
	if (index < 0)
		return IOMEM_ERR_PTR(-ENOENT);

	return of_iomap(dev->of_node, index);
}

/**
 * @brief Show function for AST region debugfs file
 *
 * This function displays information about an AST (Address Space Translator) region
 * in a sequence file. It performs the following operations:
 * - Gets the region configuration using @ref tegra_ast_get_region_info
 * - Displays whether the region is enabled or disabled
 * - If enabled, displays details about the region's configuration:
 *   - Slave and master addresses
 *   - Region size
 *   - Security flags (lock, snoop, non-secure, etc.)
 *   - Carveout settings
 *   - VPR settings
 *   - VM index and physical mode
 *   - Stream ID information
 *
 * @param[in] file  Pointer to the sequence file for output
 * @param[in] base  Base address of the AST registers
 * @param[in] index AST region index to display
 */
static void camrtc_dbgfs_show_ast_region(struct seq_file *file,
						void __iomem *base, u32 index)
{
	struct tegra_ast_region_info info;

	tegra_ast_get_region_info(base, index, &info);

	seq_printf(file, "ast region %u %s\n", index,
		info.enabled ? "enabled" : "disabled");

	if (!info.enabled)
		return;

	seq_printf(file,
		"\tslave=0x%llx\n"
		"\tmaster=0x%llx\n"
		"\tsize=0x%llx\n"
		"\tlock=%u snoop=%u non_secure=%u ns_passthru=%u\n"
		"\tcarveout_id=%u carveout_al=%u\n"
		"\tvpr_rd=%u vpr_wr=%u vpr_passthru=%u\n"
		"\tvm_index=%u physical=%u\n"
		"\tstream_id=%u (enabled=%u)\n",
		info.slave, info.master, info.mask + 1,
		info.lock, info.snoop,
		info.non_secure, info.ns_passthru,
		info.carveout_id, info.carveout_al,
		info.vpr_rd, info.vpr_wr, info.vpr_passthru,
		info.vm_index, info.physical,
		info.stream_id, info.stream_id_enabled);
}

/**
 * @brief Structure to hold information about an AST node in debugfs
 *
 * This structure represents an AST (Address Space Translator) node
 * in the debugfs interface, containing references to the IVC channel,
 * name of the AST, and a bitmask of regions to display.
 */
struct camrtc_dbgfs_ast_node {
	struct tegra_ivc_channel *ch;  /**< IVC channel for the AST */
	const char *name;              /**< Name of the AST */
	uint8_t mask;                  /**< Bitmask of regions to display */
};

/**
 * @brief Show function for AST debugfs file
 *
 * This function is called when an AST debugfs file is read.
 * It displays information about the specified AST regions.
 * It performs the following operations:
 * - Gets the AST node from the private data
 * - Maps the AST registers using @ref iomap_byname
 * - Iterates through the regions specified in the mask
 * - Displays each region using @ref camrtc_dbgfs_show_ast_region
 * - Unmaps the registers with @ref iounmap
 *
 * @param[in] file  Pointer to the sequence file
 * @param[in] data  Private data pointer (unused)
 *
 * @retval 0 on success
 * @retval -ENOMEM if @ref iomap_byname fails
 */
static int camrtc_dbgfs_show_ast(struct seq_file *file,
				void *data)
{
	struct camrtc_dbgfs_ast_node *node = file->private;
	void __iomem *ast;
	int i;

	ast = iomap_byname(camrtc_get_device(node->ch), node->name);
	if (ast == NULL)
		return -ENOMEM;

	for (i = 0; i <= 7; i++) {
		if (!(node->mask & BIT(i)))
			continue;

		camrtc_dbgfs_show_ast_region(file, ast, i);

		if (node->mask & (node->mask - 1)) /* are multiple bits set? */
			seq_puts(file, "\n");
	}

	iounmap(ast);
	return 0;
}

/**
 * @brief File operations for AST
 *
 * This macro defines the file operations for the AST debugfs file.
 * It specifies that @ref camrtc_dbgfs_show_ast should be called
 * when the file is read.
 */
DEFINE_SEQ_FOPS(camrtc_dbgfs_fops_ast, camrtc_dbgfs_show_ast);

/**
 * @brief Common AST register definitions
 *
 * This array defines the common registers in the AST (Address Space Translator)
 * that are displayed in the debugfs interface.
 */
static const struct debugfs_reg32 ast_common_regs[] = {
	{ .name = "control", 0x0 },
	{ .name = "error_status", 0x4 },
	{ .name = "error_addr_lo", 0x8 },
	{ .name = "error_addr_h", 0xC },
	{ .name = "streamid_ctl_0", 0x20 },
	{ .name = "streamid_ctl_1", 0x24 },
	{ .name = "streamid_ctl_2", 0x28 },
	{ .name = "streamid_ctl_3", 0x2C },
	{ .name = "streamid_ctl_4", 0x30 },
	{ .name = "streamid_ctl_5", 0x34 },
	{ .name = "streamid_ctl_6", 0x38 },
	{ .name = "streamid_ctl_7", 0x3C },
	{ .name = "streamid_ctl_8", 0x40 },
	{ .name = "streamid_ctl_9", 0x44 },
	{ .name = "streamid_ctl_10", 0x48 },
	{ .name = "streamid_ctl_11", 0x4C },
	{ .name = "streamid_ctl_12", 0x50 },
	{ .name = "streamid_ctl_13", 0x54 },
	{ .name = "streamid_ctl_14", 0x58 },
	{ .name = "streamid_ctl_15", 0x5C },
	{ .name = "write_block_status", 0x60 },
	{ .name = "read_block_status", 0x64 },
};

/**
 * @brief AST region register definitions
 *
 * This array defines the registers for each AST region that are
 * displayed in the debugfs interface.
 */
static const struct debugfs_reg32 ast_region_regs[] = {
	{ .name = "slave_lo", 0x100 },
	{ .name = "slave_hi", 0x104 },
	{ .name = "mask_lo", 0x108 },
	{ .name = "mask_hi", 0x10C },
	{ .name = "master_lo", 0x110 },
	{ .name = "master_hi", 0x114 },
	{ .name = "control", 0x118 },
};

/**
 * @brief Creates debugfs files for AST registers
 *
 * This function creates debugfs files to display AST registers.
 * It performs the following operations:
 * - Maps the AST registers using @ref iomap_byname
 * - Sets up the common registers debugfs file
 * - Creates a debugfs file for each region's registers
 *
 * @param[in] ch       Pointer to the IVC channel
 * @param[in] dir      Parent debugfs directory
 * @param[in] ars      Pointer to the AST regset structure
 * @param[in] ast_name Name of the AST
 *
 * @retval 0 on success
 * @retval -ENOMEM if @ref iomap_byname fails
 */
static int ast_regset_create_files(struct tegra_ivc_channel *ch,
				struct dentry *dir,
				struct ast_regset *ars,
				char const *ast_name)
{
	void __iomem *base;
	int i;

	base = iomap_byname(camrtc_get_device(ch), ast_name);
	if (IS_ERR_OR_NULL(base))
		return -ENOMEM;

	ars->common.base = base;
	ars->common.regs = ast_common_regs;
	ars->common.nregs = ARRAY_SIZE(ast_common_regs);

	debugfs_create_regset32("regs-common", 0444, dir, &ars->common);

	for (i = 0; i < ARRAY_SIZE(ars->region); i++) {
		char name[16];

		snprintf(name, sizeof(name), "regs-region%u", i);

		ars->region[i].base = base + i * TEGRA_APS_AST_REGION_STRIDE;
		ars->region[i].regs = ast_region_regs;
		ars->region[i].nregs = ARRAY_SIZE(ast_region_regs);

		debugfs_create_regset32(name, 0444, dir, &ars->region[i]);
	}

	return 0;
}

/**
 * @brief Populates the debugfs directory with camera RTCPU debug files
 *
 * This function creates the debugfs directory structure and files for
 * camera RTCPU debugging. It performs the following operations:
 * - Creates the main debugfs directory with name from device tree or default "camrtc"
 * - Creates coverage directories for VI and ISP
 * - Creates files for falcon coverage data and enable controls
 * - Creates files for version, reboot, ping, SM-ping, log-level, etc.
 * - Sets up timeout, test case, and test memory debugfs files
 * - Creates AST debugfs files if AST is available
 *
 * @param[in] ch  Pointer to the IVC channel
 *
 * @retval 0 on success
 * @retval -ENOMEM if @ref debugfs_create_dir fails
 */
static int camrtc_debug_populate(struct tegra_ivc_channel *ch)
{
	struct camrtc_debug *crd = tegra_ivc_channel_get_drvdata(ch);
	struct dentry *dir;
	struct dentry *coverage;
	struct dentry *vi;
	struct dentry *isp;
	struct camrtc_dbgfs_ast_node *ast_nodes;
	unsigned int i, dma, region;
	char const *name = "camrtc";

	of_property_read_string(ch->dev.of_node, NV(debugfs), &name);

	crd->root = dir = debugfs_create_dir(name, NULL);
	if (dir == NULL)
		return -ENOMEM;

	coverage = debugfs_create_dir("coverage", dir);
	if (coverage == NULL)
		goto error;
	vi = debugfs_create_dir("vi", coverage);
	if (vi == NULL)
		goto error;
	isp = debugfs_create_dir("isp", coverage);
	if (isp == NULL)
		goto error;
	if (!debugfs_create_file("data", 0600, vi,
			&crd->vi_falc_coverage,
			&camrtc_dbgfs_fops_falcon_coverage))
		goto error;
	if (!debugfs_create_file("enable", 0600, vi,
			&crd->vi_falc_coverage,
			&camrtc_dbgfs_fops_coverage_enable))
		goto error;
	if (!debugfs_create_file("data", 0600, isp,
			&crd->isp_falc_coverage,
			&camrtc_dbgfs_fops_falcon_coverage))
		goto error;
	if (!debugfs_create_file("enable", 0600, isp,
			&crd->isp_falc_coverage,
			&camrtc_dbgfs_fops_coverage_enable))
		goto error;

	if (!debugfs_create_file("version", 0444, dir, ch,
			&camrtc_dbgfs_fops_version))
		goto error;
	if (!debugfs_create_file("reboot", 0400, dir, ch,
			&camrtc_dbgfs_fops_reboot))
		goto error;
	if (!debugfs_create_file("ping", 0444, dir, ch,
			&camrtc_dbgfs_fops_ping))
		goto error;
	if (!debugfs_create_file("sm-ping", 0444, dir, ch,
			&camrtc_dbgfs_fops_sm_ping))
		goto error;
	if (!debugfs_create_file("log-level", 0644, dir, ch,
			&camrtc_dbgfs_fops_loglevel))
		goto error;

	debugfs_create_u32("timeout", 0644, dir,
			&crd->parameters.completion_timeout);

	if (!debugfs_create_file("forced-reset-restore", 0400, dir, ch,
			&camrtc_dbgfs_fops_forced_reset_restore))
		goto error;

	if (!debugfs_create_file("irqstat", 0444, dir, ch,
			&camrtc_dbgfs_fops_irqstat))
		goto error;
	if (!debugfs_create_file("memstat", 0444, dir, ch,
			&camrtc_dbgfs_fops_memstat))
		goto error;

	dir = debugfs_create_dir("mods", crd->root);
	if (!dir)
		goto error;

	debugfs_create_u32("case", 0644, dir,
			&crd->parameters.mods_case);

	debugfs_create_u32("loops", 0644, dir,
			&crd->parameters.mods_loops);

	debugfs_create_x32("dma_channels", 0644, dir,
			&crd->parameters.mods_dma_channels);

	if (!debugfs_create_file("result", 0400, dir, ch,
			&camrtc_dbgfs_fops_mods_result))
		goto error;

	dir = debugfs_create_dir("rtos", crd->root);
	if (!dir)
		goto error;
	if (!debugfs_create_file("state", 0444, dir, ch,
			&camrtc_dbgfs_fops_freertos_state))
		goto error;

	dir = debugfs_create_dir("test", crd->root);
	if (!dir)
		goto error;
	if (!debugfs_create_file("available", 0444, dir, ch,
			&camrtc_dbgfs_fops_test_list))
		goto error;
	if (!debugfs_create_file("case", 0644, dir, ch,
			&camrtc_dbgfs_fops_test_case))
		goto error;
	if (!debugfs_create_file("result", 0400, dir, ch,
			&camrtc_dbgfs_fops_test_result))
		goto error;

	debugfs_create_u32("timeout", 0644, dir,
			&crd->parameters.test_timeout);

	for (i = 0; i < ARRAY_SIZE(crd->mem); i++) {
		char name[8];

		crd->mem[i].index = i;
		snprintf(name, sizeof(name), "mem%u", i);
		if (!debugfs_create_file(name, 0644, dir,
			&crd->mem[i], &camrtc_dbgfs_fops_test_mem))
			goto error;
	}

	ast_nodes = devm_kzalloc(&ch->dev, 18 * sizeof(*ast_nodes),
					GFP_KERNEL);
	if (unlikely(ast_nodes == NULL))
		goto error;

	for (dma = 0; dma <= 1; dma++) {
		const char *ast_name = dma ? "ast-dma" : "ast-cpu";

		dir = debugfs_create_dir(ast_name, crd->root);
		if (dir == NULL)
			goto error;

		ast_regset_create_files(ch, dir, &crd->ast_regsets[dma],
					ast_name);

		ast_nodes->ch = ch;
		ast_nodes->name = ast_name;
		ast_nodes->mask = 0xff;

		if (!debugfs_create_file("all", 0444, dir, ast_nodes,
						&camrtc_dbgfs_fops_ast))
			goto error;

		ast_nodes++;

		for (region = 0; region < 8; region++) {
			char name[8];

			snprintf(name, sizeof name, "%u", region);

			ast_nodes->ch = ch;
			ast_nodes->name = ast_name;
			ast_nodes->mask = BIT(region);

			if (!debugfs_create_file(name, 0444, dir, ast_nodes,
						&camrtc_dbgfs_fops_ast))
				goto error;

			ast_nodes++;
		}
	}

	return 0;
error:
	debugfs_remove_recursive(crd->root);
	return -ENOMEM;
}

/**
 * @brief Gets a linked device from device tree
 *
 * This function retrieves a device linked through a phandle in the device tree.
 * It performs the following operations:
 * - Gets the device node using @ref of_parse_phandle
 * - Checks if the node exists
 * - Finds the platform device using @ref of_find_device_by_node
 * - Returns the device pointer or NULL if not found
 *
 * @param[in] dev    Pointer to the parent device
 * @param[in] name   Name of the phandle property
 * @param[in] index  Index in the phandle array
 *
 * @retval (struct device *) Pointer to the linked device on success
 * @retval NULL if not found
 */
static struct device *camrtc_get_linked_device(
	struct device *dev, char const *name, int index)
{
	struct device_node *np;
	struct platform_device *pdev;

	np = of_parse_phandle(dev->of_node, name, index);
	if (np == NULL)
		return NULL;

	pdev = of_find_device_by_node(np);
	of_node_put(np);

	if (pdev == NULL) {
		dev_warn(dev, "%s[%u] node has no device\n", name, index);
		return NULL;
	}

	return &pdev->dev;
}

/**
 * @brief Probe function for camera RTCPU debug driver
 *
 * This function initializes the camera RTCPU debug driver.
 * It performs the following operations:
 * - Validates IVC frame sizes using @ref BUG_ON
 * - Allocates memory for the debug structure using @ref devm_kzalloc
 * - Initializes parameters with default values
 * - Reads configuration from device tree
 * - Initializes mutex and wait queue
 * - Sets up memory devices using @ref camrtc_get_linked_device
 * - Configures falcon coverage structures
 * - Gets the interconnect path for bandwidth control using @ref devm_of_icc_get
 * - Populates the debugfs interface using @ref camrtc_debug_populate
 *
 * @param[in] ch  Pointer to the IVC channel
 *
 * @retval 0 on success
 * @retval -ENOMEM if @ref devm_kzalloc fails
 * @retval -ENOMEM if @ref camrtc_debug_populate fails
 * @retval (int) Error code from @ref dev_err_probe()
 */
static int camrtc_debug_probe(struct tegra_ivc_channel *ch)
{
	struct device *dev = &ch->dev;
	struct camrtc_debug *crd;
	uint32_t bw;
	uint32_t i;
	size_t addRet;

	BUG_ON(ch->ivc.frame_size < sizeof(struct camrtc_dbg_request));
	BUG_ON(ch->ivc.frame_size < sizeof(struct camrtc_dbg_response));

	(void)__builtin_uaddl_overflow(sizeof(*crd), ch->ivc.frame_size, &addRet);
	crd = devm_kzalloc(dev, addRet, GFP_KERNEL);
	if (unlikely(crd == NULL))
		return -ENOMEM;

	crd->channel = ch;
	crd->parameters.test_case = (char *)(crd + 1);
	crd->parameters.mods_case = CAMRTC_MODS_TEST_BASIC;
	crd->parameters.mods_loops = 20;
	crd->parameters.mods_dma_channels = 0;

	if (of_property_read_u32(dev->of_node,
			NV(ivc-timeout),
			&crd->parameters.completion_timeout))
		crd->parameters.completion_timeout = 50;

	if (of_property_read_u32(dev->of_node,
			NV(test-timeout),
			&crd->parameters.test_timeout))
		crd->parameters.test_timeout = 1000;

	mutex_init(&crd->mutex);
	init_waitqueue_head(&crd->waitq);

	tegra_ivc_channel_set_drvdata(ch, crd);

	for (i = 0; i < CAMRTC_TEST_CAM_DEVICES; i++)
		crd->mem_devices[i] = camrtc_get_linked_device(dev, NV(mem-map), i);

	crd->vi_falc_coverage.id = CAMRTC_DBG_FALCON_ID_VI;
	crd->vi_falc_coverage.mem_dev = camrtc_dbgfs_memory_dev(crd);
	crd->vi_falc_coverage.falcon_dev = crd->mem_devices[1];
	crd->vi_falc_coverage.ch = ch;

	crd->isp_falc_coverage.id = CAMRTC_DBG_FALCON_ID_ISP;
	crd->isp_falc_coverage.mem_dev = crd->mem_devices[0];
	crd->isp_falc_coverage.falcon_dev = crd->mem_devices[2];
	crd->isp_falc_coverage.ch = ch;

	if (of_property_read_u32(dev->of_node, NV(test-bw), &bw) == 0) {
		crd->parameters.test_bw = bw;

		dev_dbg(dev, "using emc bw %u for tests\n", bw);
	}

	if (crd->mem_devices[0] == NULL) {
		dev_dbg(dev, "missing %s\n", NV(mem-map));
		crd->mem_devices[0] = get_device(camrtc_get_device(ch));
	}

	crd->icc_path = devm_of_icc_get(crd->mem_devices[0], "write");
	if (IS_ERR(crd->icc_path))
		return dev_err_probe(dev, PTR_ERR(crd->icc_path),
				     "failed to get icc write handle\n");

	if (camrtc_debug_populate(ch))
		return -ENOMEM;

	return 0;
}

/**
 * @brief Remove function for camera RTCPU debug driver
 *
 * This function cleans up resources when the camera RTCPU debug driver is removed.
 * It performs the following operations:
 * - Disables falcon coverage using @ref camrtc_falcon_coverage_disable
 * - Frees all allocated test memory using @ref dma_free_coherent
 * - Releases references to memory devices using @ref put_device
 * - Removes all debugfs entries using @ref debugfs_remove_recursive
 *
 * @param[in] ch  Pointer to the IVC channel
 */
static void camrtc_debug_remove(struct tegra_ivc_channel *ch)
{
	struct camrtc_debug *crd = tegra_ivc_channel_get_drvdata(ch);
	int i;
	struct device *mem_dev = camrtc_dbgfs_memory_dev(crd);

	camrtc_falcon_coverage_disable(&crd->vi_falc_coverage);
	camrtc_falcon_coverage_disable(&crd->isp_falc_coverage);

	for (i = 0; i < ARRAY_SIZE(crd->mem); i++) {
		struct camrtc_test_mem *mem = &crd->mem[i];

		if (mem->size == 0)
			continue;

		dma_free_coherent(mem_dev, mem->size, mem->ptr, mem->iova);
		memset(mem, 0, sizeof(*mem));
	}

	for (i = 0; i < CAMRTC_TEST_CAM_DEVICES; i++) {
		if (crd->mem_devices[i] != NULL)
			put_device(crd->mem_devices[i]);
	}

	debugfs_remove_recursive(crd->root);
}

/**
 * @brief IVC channel operations for camera RTCPU debug driver
 *
 * This structure defines the operations for the camera RTCPU debug driver.
 * - probe: @ref camrtc_debug_probe - Called when the driver is probed
 * - remove: @ref camrtc_debug_remove - Called when the driver is removed
 * - notify: @ref camrtc_debug_notify - Called when IVC notification is received
 */
static const struct tegra_ivc_channel_ops tegra_ivc_channel_debug_ops = {
	.probe	= camrtc_debug_probe,
	.remove	= camrtc_debug_remove,
	.notify	= camrtc_debug_notify,
};

/**
 * @brief Device tree compatible strings for camera RTCPU debug driver
 *
 * This array defines the compatible strings that match this driver.
 */
static const struct of_device_id camrtc_debug_of_match[] = {
	{ .compatible = "nvidia,tegra186-camera-ivc-protocol-debug" },
	{ },
};
MODULE_DEVICE_TABLE(of, camrtc_debug_of_match);

/**
 * @brief IVC driver structure for camera RTCPU debug driver
 *
 * This structure defines the IVC driver for camera RTCPU debugging.
 * It includes basic driver information, device type, and operations.
 */
static struct tegra_ivc_driver camrtc_debug_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.bus	= &tegra_ivc_bus_type,
		.name	= "tegra-camera-rtcpu-debugfs",
		.of_match_table = camrtc_debug_of_match,
	},
	.dev_type	= &tegra_ivc_channel_type,
	.ops.channel	= &tegra_ivc_channel_debug_ops,
};
tegra_ivc_subsys_driver_default(camrtc_debug_driver);

MODULE_DESCRIPTION("Debug Driver for Camera RTCPU");
MODULE_AUTHOR("Pekka Pessi <ppessi@nvidia.com>");
MODULE_LICENSE("GPL v2");
