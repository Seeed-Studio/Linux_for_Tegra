// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2016-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * NVDLA debug utils
 */

#include <linux/arm64-barrier.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include "port/nvdla_host_wrapper.h"
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <uapi/linux/nvhost_nvdla_ioctl.h>

#include "dla_os_interface.h"
#include "nvdla.h"
#include "nvdla_debug.h"
#include "dla_t25x_fw_version.h"
#include "port/nvdla_fw.h"
#include "port/nvdla_device.h"
#include "port/nvdla_pm.h"

/*
 * Header in ring buffer consist (start, end) two uint32_t values.
 * Trace data content starts from the offset below.
 */
#define TRACE_DATA_OFFSET	(2 * sizeof(uint32_t))

#define dla_set_trace_enable(pdev, trace_enable)		\
	debug_set_trace_event_config(pdev, trace_enable,	\
			DLA_SET_TRACE_ENABLE);			\

#define dla_set_trace_event_mask(pdev, event_mask)		\
	debug_set_trace_event_config(pdev, event_mask,	\
			DLA_SET_TRACE_EVENT_MASK);		\

static LIST_HEAD(s_debug_ctrl_list);
static DEFINE_MUTEX(s_debug_ctrl_list_lock);

struct nvdla_debug_ctrl {
	struct platform_device *pdev;

	/**
	 * Members that are local to this debugfs and that is to be persisted
	 * shall be part of this debug control node
	 **/

	struct list_head list;
};

#define DLA_PRETTYCONFIG_KEY_MAX_LEN 256U
struct prettyconfig {
	char key[DLA_PRETTYCONFIG_KEY_MAX_LEN];
	uint32_t nvalues;
	char values[DLA_PRETTYCONFIG_KEY_MAX_LEN][DLA_DVFS_FREQ_LIST_MAX];
};

static int32_t parse_pretty_config(struct platform_device *pdev,
		char *user_value, struct prettyconfig *prettyconfig)
{
	char *sep;
	char *value;
	char *next_val;
	uint32_t value_count = 0;
	uint32_t keylen;

	// Input validation
	if (!user_value || !prettyconfig) {
		nvdla_dbg_err(pdev, "NULL input parameters\n");
		return -EINVAL;
	}

	// Find the separator '='
	sep = strchr(user_value, '=');
	if (!sep || sep == user_value) {
		nvdla_dbg_err(pdev, "invalid format, expect key=value1,value2,...\n");
		return -EINVAL;
	}

	// Copy key (everything before '=')
	keylen = sep - user_value;
	if (keylen >= sizeof(prettyconfig->key)) {
		nvdla_dbg_err(pdev, "key too long\n");
		return -EINVAL;
	}
	memcpy(prettyconfig->key, user_value, keylen);
	prettyconfig->key[keylen] = '\0';

	// Move to first value
	value = sep + 1;
	if (!*value) {
		nvdla_dbg_err(pdev, "no values after '='\n");
		return -EINVAL;
	}

	// Parse comma-separated values
	while (value && value_count < DLA_DVFS_FREQ_LIST_MAX) {
		// Find next comma
		next_val = strchr(value, ',');
		if (next_val) {
			*next_val = '\0';
			next_val++;
		}

		// Skip empty values
		if (*value == '\0') {
			value = next_val;
			continue;
		}

		// Copy value
		if (strlen(value) >= sizeof(prettyconfig->values[0])) {
			nvdla_dbg_err(pdev, "value too long: '%s'\n", value);
			return -EINVAL;
		}
		strncpy(prettyconfig->values[value_count], value, strlen(value));
		prettyconfig->values[value_count][strlen(value)] = '\0';
		value_count++;

		value = next_val;
	}

	if (value_count == 0) {
		nvdla_dbg_err(pdev, "no valid values found\n");
		return -EINVAL;
	}

	prettyconfig->nvalues = value_count;
	return 0;
}

static struct nvdla_debug_ctrl *s_nvdla_debug_ctrl_get_by_pdev(
	struct platform_device *pdev)
{
	struct nvdla_debug_ctrl *ctrl = NULL;

	mutex_lock(&s_debug_ctrl_list_lock);
	list_for_each_entry(ctrl, &s_debug_ctrl_list, list) {
		if (ctrl->pdev == pdev)
			break;
	}
	spec_bar(); /* break_spec_p#5_1 */
	mutex_unlock(&s_debug_ctrl_list_lock);

	return ctrl;
}

static int s_nvdla_get_pdev_from_file(struct file *file, struct platform_device **pdev)
{
	struct seq_file *priv_data;
	struct nvdla_device *nvdla_dev;

	if (!file || !pdev)
		return -1;

	priv_data = file->private_data;
	if (!priv_data)
		return -1;

	nvdla_dev = (struct nvdla_device *) priv_data->private;
	if (!nvdla_dev)
		return -1;

	*pdev = nvdla_dev->pdev;
	if (!*pdev)
		return -1;

	return 0;
}

static int s_nvdla_get_pdev_from_seq(struct seq_file *s, struct platform_device **pdev)
{
	struct nvdla_device *nvdla_dev;

	if (!s || !s->private || !pdev)
		return -1;

	nvdla_dev = (struct nvdla_device *) s->private;
	*pdev = nvdla_dev->pdev;

	if (!*pdev)
		return -1;

	return 0;
}


static int nvdla_fw_ver_show(struct seq_file *s, void *unused)
{
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;
	int err;

	nvdla_dev = (struct nvdla_device *)s->private;
	pdev = nvdla_dev->pdev;

	/* update fw_version if engine is not yet powered on */
	err = nvdla_module_busy(pdev);
	if (err)
		return err;
	nvdla_module_idle(pdev);

	seq_printf(s, "%u.%u.%u\n",
		((nvdla_dev->fw_version >> 16) & 0xff),
		((nvdla_dev->fw_version >> 8) & 0xff),
		(nvdla_dev->fw_version & 0xff));

	return 0;

}

static int nvdla_fw_ver_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvdla_fw_ver_show, inode->i_private);
}

static const struct file_operations nvdla_fw_ver_fops = {
	.open		= nvdla_fw_ver_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int debug_dla_tracedump_show(struct seq_file *s, void *data)
{
	char *bufptr;
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;
	uint32_t i = 0, cindex = 0;
	uint32_t offset = TRACE_DATA_OFFSET;
	uint32_t start, end, datasize;

	nvdla_dev = (struct nvdla_device *)s->private;
	pdev = nvdla_dev->pdev;

	if (nvdla_dev->trace_dump_va && nvdla_dev->trace_enable) {
		bufptr = (char *)nvdla_dev->trace_dump_va;

		if (!strcmp(bufptr, ""))
			return 0;

		memcpy(&start, bufptr, sizeof(uint32_t));
		memcpy(&end, ((char *)bufptr + sizeof(uint32_t)),
			sizeof(uint32_t));

		i = start;

		if (start == (end + 1))
			datasize = (uint32_t)TRACE_BUFFER_SIZE - offset;
		else
			datasize = end - start;

		while (cindex < datasize) {
			seq_printf(s, "%c", bufptr[i]);
			i++;
			i = ((i - offset) % (TRACE_BUFFER_SIZE - offset)) +
				offset;
			cindex++;

			if ((bufptr[i] == '\n') && (cindex < datasize)) {
				seq_printf(s, "%c", bufptr[i]);

				/* skip extra new line chars */
				while ((bufptr[i] == '\n') &&
					(cindex < datasize)) {
					i++;
					i = ((i - offset) %
						(TRACE_BUFFER_SIZE - offset)) +
						offset;
					cindex++;
				}
			}
		}

		seq_printf(s, "%c", '\n');
	}

	return 0;
}

static int debug_dla_enable_trace_show(struct seq_file *s, void *data)
{
	struct nvdla_device *nvdla_dev = (struct nvdla_device *)s->private;

	seq_printf(s, "%u\n", nvdla_dev->trace_enable);
	return 0;
}

static int debug_dla_eventmask_show(struct seq_file *s, void *data)
{
	struct nvdla_device *nvdla_dev = (struct nvdla_device *)s->private;

	seq_printf(s, "%u\n", nvdla_dev->events_mask);
	return 0;
}

static int debug_dla_eventmask_help_show(struct seq_file *s, void *data)
{
	seq_printf(s, "%s\n",
		"\nDla Firmware has following different tracing categories:");
	seq_printf(s, "%s\n", "  BIT(0) -  Processor\n"
				  "  BIT(1) -  Falcon\n"
				  "  BIT(2) -  Events\n"
				  "  BIT(3) -  Scheduler Queue\n"
				  "  BIT(4) -  Operation Cache\n");
	seq_printf(s, "%s\n", "To enable all type of tracing events,"
				  "set all bits ( 0 - 4 ): ");
	seq_printf(s, "%s\n\n", "  echo 31 > events_mask");
	return 0;
}

static int debug_dla_bintracedump_show(struct seq_file *s, void *data)
{
	char *bufptr;
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;
	uint32_t i = 0;
	uint32_t offset = TRACE_DATA_OFFSET;
	uint32_t start, end, datasize;

	nvdla_dev = (struct nvdla_device *)s->private;
	pdev = nvdla_dev->pdev;

	if (nvdla_dev->trace_dump_va && nvdla_dev->trace_enable) {
		bufptr = (char *)nvdla_dev->trace_dump_va;

		if (!strcmp(bufptr, ""))
			return 0;

		memcpy(&start, bufptr, sizeof(uint32_t));
		memcpy(&end, ((char *)bufptr + sizeof(uint32_t)),
			sizeof(uint32_t));

		i = start;

		if (start == (end + 1))
			datasize = (uint32_t)TRACE_BUFFER_SIZE - offset;
		else
			datasize = end - start;

		/* to read trace buffer from 0th index */
		i = 0;
		 /* in this case, datasize includes header data also */
		datasize += offset;

		/* Dump data in binary format. */
		while (i < datasize)
			seq_printf(s, "%c", bufptr[i++]);
	}

	return 0;
}

static int debug_dla_en_fw_gcov_show(struct seq_file *s, void *data)
{
	struct nvdla_device *nvdla_dev = (struct nvdla_device *)s->private;

	seq_printf(s, "%u\n", nvdla_dev->en_fw_gcov);
	return 0;
}

static ssize_t debug_dla_en_fw_gcov_alloc(struct file *file,
	const char __user *buffer, size_t count, loff_t *off)
{
	int ret;
	u32 val;
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;
	struct seq_file *p = file->private_data;
	char str[] = "0123456789abcdef";

	nvdla_dev = (struct nvdla_device *)p->private;
	pdev = nvdla_dev->pdev;
	count = min_t(size_t, strlen(str), count);
	if (copy_from_user(str, buffer, count))
		return -EFAULT;

	mutex_lock(&p->lock);
	/* get value entered by user in variable val */
	ret = sscanf(str, "%u", &val);
	mutex_unlock(&p->lock);

	if (ret != 1) {
		nvdla_dbg_err(pdev, "Incorrect input!");
		goto invalid_input;
	}

	/*  alloc gcov region */
	if (val == 1) {
		ret = nvdla_alloc_gcov_region(pdev);
		if (ret) {
			nvdla_dbg_err(pdev, "failed to allocate gcov region.");
			goto op_failed;
		}
		nvdla_dev->en_fw_gcov = 1;
	} else if (val == 0) {
		if (nvdla_dev->en_fw_gcov == 0)
			return count;
		ret = nvdla_free_gcov_region(pdev, true);
		if (ret) {
			nvdla_dbg_err(pdev, "failed to free gcov region.");
			goto op_failed;
		}
		nvdla_dev->en_fw_gcov = 0;
	} else {
		nvdla_dbg_err(pdev, "inval i/p. Valid i/p: 0 and 1");
		ret = -EINVAL;
		goto op_failed;
	}

	return count;

op_failed:
invalid_input:
	return ret;
}

static int debug_dla_fw_gcov_gcda_show(struct seq_file *s, void *data)
{
	char *bufptr;
	uint32_t datasize;
	struct nvdla_device *nvdla_dev;

	nvdla_dev = (struct nvdla_device *)s->private;
	if (nvdla_dev->gcov_dump_va && nvdla_dev->en_fw_gcov) {
		bufptr = (char *)nvdla_dev->gcov_dump_va;

		datasize = (uint32_t)GCOV_BUFFER_SIZE;
		seq_write(s, bufptr, datasize);
	}

	return 0;
}

static int nvdla_get_stats(struct nvdla_device *nvdla_dev)
{
	int err = 0;
	struct nvdla_cmd_data cmd_data;
	struct platform_device *pdev;

	/* prepare command data */
	cmd_data.method_id = DLA_CMD_GET_STATISTICS;
	cmd_data.method_data = ALIGNED_DMA(nvdla_dev->utilization_mem_pa);
	cmd_data.wait = true;

	pdev = nvdla_dev->pdev;
	if (pdev == NULL)
		return -EFAULT;

	/* pass set debug command to falcon */
	err = nvdla_fw_send_cmd(pdev, &cmd_data);
	if (err != 0)
		nvdla_dbg_err(pdev, "failed to send get stats command");

	return err;
}

static int debug_dla_fw_resource_util_show(struct seq_file *s, void *data)
{
	int err = 0;
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;

	unsigned int utilization, util_rate_characteristic, util_rate_mantissa;

	if (s == NULL) {
		err = -EFAULT;
		goto fail_no_dev;
	}

	nvdla_dev = (struct nvdla_device *) s->private;
	if (nvdla_dev == NULL) {
		err = -EFAULT;
		goto fail_no_dev;
	}

	pdev = nvdla_dev->pdev;
	if (pdev == NULL) {
		err = -EFAULT;
		goto fail_no_dev;
	}

	if (atomic_read(&pdev->dev.power.usage_count) == 0) {
		/* Print 0% utilization rate if power refcount for DLA
		 * is zero i.e., DLA is not turned on
		 */
		util_rate_characteristic = 0;
		util_rate_mantissa = 0;
	} else {
		/* make sure that device is powered on */
		err = nvdla_module_busy(pdev);
		if (err != 0) {
			nvdla_dbg_err(pdev, "failed to power on\n");
			err = -ENODEV;
			goto fail_no_dev;
		}

		err = nvdla_get_stats(nvdla_dev);
		if (err != 0) {
			nvdla_dbg_err(pdev, "Failed to send get stats command");
			nvdla_module_idle(pdev);
			goto fail_no_dev;
		}
		utilization = *(unsigned int *)nvdla_dev->utilization_mem_va;
		util_rate_characteristic = (utilization / 10000);
		util_rate_mantissa = (utilization % 10000);
		nvdla_module_idle(pdev);
	}

	seq_printf(s, "%u.%04u\n", util_rate_characteristic, util_rate_mantissa);

fail_no_dev:
	return err;
}

static int nvdla_get_window_size(struct nvdla_device *nvdla_dev)
{
	int err = 0;
	struct nvdla_cmd_data cmd_data;
	struct platform_device *pdev;

	/* prepare command data */
	cmd_data.method_id = DLA_CMD_GET_STAT_WINDOW_SIZE;
	cmd_data.method_data = ALIGNED_DMA(nvdla_dev->window_mem_pa);
	cmd_data.wait = true;

	pdev = nvdla_dev->pdev;
	if (pdev == NULL) {
		err = -EFAULT;
		goto fail_no_dev;
	}

	/* make sure that device is powered on */
	err = nvdla_module_busy(pdev);
	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to power on\n");
		err = -ENODEV;
		goto fail_no_dev;
	}

	/* pass set debug command to falcon */
	err = nvdla_fw_send_cmd(pdev, &cmd_data);
	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to send set window command");
		goto fail_to_send_cmd;
	}

fail_to_send_cmd:
	nvdla_module_idle(pdev);
fail_no_dev:
	return err;
}

static int debug_dla_fw_stat_window_show(struct seq_file *s, void *data)
{
	int err;
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;

	if (s == NULL) {
		err = -EFAULT;
		goto fail;
	}

	nvdla_dev = (struct nvdla_device *) s->private;
	if (nvdla_dev == NULL) {
		err = -EFAULT;
		goto fail;
	}

	pdev = nvdla_dev->pdev;
	if (pdev == NULL) {
		err = -EFAULT;
		goto fail;
	}

	err = nvdla_get_window_size(nvdla_dev);
	if (err != 0) {
		nvdla_dbg_err(pdev, "Failed to get window size");
		goto fail;
	}

	seq_printf(s, "%u\n", *(unsigned int *)nvdla_dev->window_mem_va);

	return 0;

fail:
	return err;
}

static int debug_dla_fw_ping_show(struct seq_file *s, void *data)
{
	(void) data;
	seq_puts(s, "0\n");
	return 0;
}

// fw dvfs show functions
static int debug_dla_fw_dvfs_prettyconfig_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct dla_dvfs_config config;
	uint32_t i;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		return -1;

	err = nvdla_pm_get_dvfs_config(pdev, &config);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to get DVFS config");
		return err;
	}

	/* Basic config */
	seq_printf(s, "mode                        : %u\n", config.mode);
	seq_printf(s, "roundrobin_interval_ms      : %u\n", config.periodicity_ms);
	seq_printf(s, "telemetry_enable            : %u\n", config.telemetry_enable);
	seq_printf(s, "default_priority            : %u\n", config.priority);

	/* Round robin config */
	seq_puts(s, "roundrobin_freq_kHz         : ");
	for (i = 0; i < config.num_roundrobin_freqs; i++) {
		seq_printf(s, "%u", config.roundrobin_freq_kHz[i]);
		if (i < config.num_roundrobin_freqs - 1)
			seq_puts(s, ", ");
	}
	seq_puts(s, "\n");

	/* Priority config */
	seq_printf(s, "pri_lookahead0              : %u\n", config.pri_lookahead0);
	seq_printf(s, "pri_lookahead1              : %u\n", config.pri_lookahead1);
	seq_printf(s, "pri_hyst                    : %u\n", config.pri_hyst);

	/* Memory stall config*/
	seq_printf(s, "mem_stall_thresholds        : %u, %u, %u\n",
		config.mem_stall_thresh1,
		config.mem_stall_thresh2,
		config.mem_stall_thresh3);

	seq_printf(s, "mem_stall_lvl2freq_throttle : %u, %u, %u, %u\n",
		config.mem_stall_lvl2freq_throttle_no,
		config.mem_stall_lvl2freq_throttle_low,
		config.mem_stall_lvl2freq_throttle_med,
		config.mem_stall_lvl2freq_throttle_high);

	/* Arbitration config */
	seq_printf(s, "arb_freq_weights            : %u, %u, %u\n",
		config.arb_freq_weight0,
		config.arb_freq_weight1,
		config.arb_freq_weight2);
	seq_printf(s, "arb_policy                  : %u\n", config.arb_policy_selection);

	/* Utilization config */
	seq_printf(s, "util2freq_indices           : %u, %u\n",
		config.util2freq_index_lo_util,
		config.util2freq_index_hi_util);
	seq_printf(s, "util_thresholds             : %u, %u\n",
		config.util_thresh_lo_util,
		config.util_thresh_hi_util);

	return 0;
}

static int debug_dla_fw_dvfs_prettyconfig_help_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		return -1;

	seq_puts(s, "\n\n\t\tDVFS Pretty Config Help\n");
	seq_puts(s, "\tUsage: echo -n key=value1,value2,... >"
		"/sys/kernel/debug/nvdla0/firmware/dvfs/prettyconfig\n\n");
	seq_puts(s, "\tKeys:\n");
	seq_puts(s, "\tmode                        : "
		"0=Disable, 1=RoundRobin, 2=DVFS\n");
	seq_puts(s, "\troundrobin_interval_ms      : "
		"Time in ms (min 2ms)\n");
	seq_puts(s, "\troundrobin_freq_khz         : "
		"Comma-separated frequencies (max 10)\n");
	seq_puts(s, "\ttelemetry_enable            : "
		"0=Off, 1=On, DVFS telemetry\n");
	seq_puts(s, "\tdefault_priority            : "
		"0-31 default priority\n");
	seq_puts(s, "\tpri_lookahead0              : "
		"Initial lookahead num networks\n");
	seq_puts(s, "\tpri_lookahead1              : "
		"After 2ms lookahead num networks\n");
	seq_puts(s, "\tpri_hyst                    : "
		"Hysteresis (num of DVFS loop cycles to lookback)\n");
	seq_puts(s, "\tmem_stall_thresholds        : "
		"mem_stall_thresh1, mem_stall_thresh2, mem_stall_thresh3\n");
	seq_puts(s, "\tmem_stall_lvl2freq_throttle : "
		"no, low, med, high throttle values (in ascending order)\n");
	seq_puts(s, "\tarb_freq_weights            : "
		"freq_pri_wt0, freq_stall_wt1, freq_util_wt2 (sum to 100)\n");
	seq_puts(s, "\tarb_policy                  : "
		"0=Max, 1=WeightedAvg\n");
	seq_puts(s, "\tutil2freq_indices           : "
		"util2freq_index_lo_util, util2freq_index_hi_util\n");
	seq_puts(s, "\tutil_thresholds             : "
		"util_thresh_lo_util, util_thresh_hi_util\n");

	return 0;
}

static int debug_dla_fw_dvfs_mode_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	(void) data;


	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		return -1;
	nvdla_dbg_info(pdev, "[DVFS] mode show");
	return 0;
}

static int debug_dla_fw_dvfs_enable_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		return -1;
	nvdla_dbg_info(pdev, "[DVFS] enable show");
	return 0;
}

static int debug_dla_fw_dvfs_freqlist_khz_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		return -1;
	nvdla_dbg_info(pdev, "[DVFS] freqlist_khz show");
	return 0;
}

static int debug_dla_fw_dvfs_periodicity_ms_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		return -1;
	nvdla_dbg_info(pdev, "[DVFS] periodicity_ms show");
	return 0;
}

static int nvdla_get_dvfs_statdump(struct nvdla_device *nvdla_dev)
{
	int err = 0;
	struct nvdla_cmd_data cmd_data;
	struct platform_device *pdev;

	/* prepare command data */
	cmd_data.method_id = DLA_CMD_GET_STATISTICS2;
	/* method data is not used for this command, passing 0U */
	cmd_data.method_data = ALIGNED_DMA(0U);
	cmd_data.wait = true;

	pdev = nvdla_dev->pdev;
	if (pdev == NULL) {
		err = -EFAULT;
		goto fail_no_dev;
	}

	/* make sure that device is powered on */
	err = nvdla_module_busy(pdev);
	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to power on\n");
		err = -ENODEV;
		goto fail_no_dev;
	}

	/* pass set debug command to falcon */
	err = nvdla_fw_send_cmd(pdev, &cmd_data);
	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to send get dvfs statdump command");
		goto fail_to_send_cmd;
	}

fail_to_send_cmd:
	nvdla_module_idle(pdev);
fail_no_dev:
	return err;
}

static int debug_dla_fw_dvfs_statdump_show(struct seq_file *s, void *data)
{
	int err;
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;

	if (s == NULL) {
		err = -EFAULT;
		goto fail;
	}

	nvdla_dev = (struct nvdla_device *) s->private;
	if (nvdla_dev == NULL) {
		err = -EFAULT;
		goto fail;
	}

	pdev = nvdla_dev->pdev;
	if (pdev == NULL) {
		err = -EFAULT;
		goto fail;
	}

	err = nvdla_get_dvfs_statdump(nvdla_dev);
	if (err != 0) {
		nvdla_dbg_err(pdev, "Failed to get dvfs statdump");
		goto fail;
	}

	return 0;

fail:
	return err;
}

// ctrl clk show functions
static int debug_dla_ctrl_clk_activetime_us_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_pm_stat stat;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/CLK] activetime_us show");

	err = nvdla_pm_get_stat(pdev, &stat);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to fetch stat. err: %d", err);
		goto fail;
	}

	seq_printf(s, "%llu\n", stat.clock_active_time_us);

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_clk_enable_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	bool gated;

	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get pdev. err: %d", err);
		goto fail;
	}
	nvdla_dbg_info(pdev, "[CTRL/CLK] enable show");

	err = nvdla_pm_clock_is_gated(pdev, &gated);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get status. err: %d", err);
		goto fail;
	}

	seq_printf(s, "%x\n", (int) !gated);
	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_clk_core_khz_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	uint32_t freq_khz;

	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/CLK] core_khz show");

	err = nvdla_pm_clock_get_core_freq(pdev, &freq_khz);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to fetch frequency. err: %d\n",
			err);
		goto fail;
	}

	seq_printf(s, "%u\n", freq_khz);
	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_clk_mcu_khz_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	uint32_t freq_khz;

	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/CLK] mcu_khz show");

	err = nvdla_pm_clock_get_mcu_freq(pdev, &freq_khz);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to fetch frequency. err: %d\n",
			err);
		goto fail;
	}

	seq_printf(s, "%u\n", freq_khz);
	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_clk_idlecount_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_pm_stat stat;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/CLK] idlecount show");

	err = nvdla_pm_get_stat(pdev, &stat);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to fetch stat. err: %d", err);
		goto fail;
	}

	seq_printf(s, "%llu\n", stat.clock_idle_count);

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_clk_idledelay_us_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_debug_ctrl *ctrl;
	uint32_t delay_us;

	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;


	nvdla_dbg_info(pdev, "[CTRL/CLK] idledelay_us show");

	ctrl = s_nvdla_debug_ctrl_get_by_pdev(pdev);
	if (ctrl == NULL) {
		nvdla_dbg_err(pdev, "No ctrl node available\n");
		err = -ENOMEM;
		goto fail;
	}

	err = nvdla_pm_clock_gate_get_delay_us(pdev, &delay_us);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to get delay us. err: %d\n", err);
		goto fail;
	}

	seq_printf(s, "%u\n", (uint32_t) delay_us);

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_clk_idletime_us_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_pm_stat stat;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/CLK] idletime_us show");

	err = nvdla_pm_get_stat(pdev, &stat);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to fetch stat. err: %d", err);
		goto fail;
	}

	seq_printf(s, "%llu\n", stat.clock_idle_time_us);

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_clk_statdump_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_pm_stat stat;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/CLK] statdump show");

	err = nvdla_pm_get_stat(pdev, &stat);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to fetch stat. err: %d", err);
		goto fail;
	}

	seq_puts(s, "CG entry latency in us (max/min/total): ");
	if (stat.cg_entry_latency_us_total > 0) {
		seq_printf(s, "%llu / %llu / %llu\n",
			stat.cg_entry_latency_us_max,
			stat.cg_entry_latency_us_min,
			stat.cg_entry_latency_us_total);
	} else {
		/* Data not available */
		seq_puts(s, "- / - / -\n");
	}

	seq_puts(s, "CG exit latency in us (max/min/total): ");
	if (stat.cg_exit_latency_us_total > 0) {
		seq_printf(s, "%llu / %llu / %llu\n",
			stat.cg_exit_latency_us_max,
			stat.cg_exit_latency_us_min,
			stat.cg_exit_latency_us_total);
	} else {
		/* Data not available */
		seq_puts(s, "- / - / -\n");
	}

	seq_printf(s, "CG Entry count: %llu\n", stat.clock_idle_count);
	seq_printf(s, "CG Exit count: %llu\n", stat.clock_active_count - 1U);

	return 0;

fail:
	return err;
}

// ctrl power show functions
static int debug_dla_ctrl_power_activetime_us_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_pm_stat stat;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/POWER] activetime_us show");

	err = nvdla_pm_get_stat(pdev, &stat);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to fetch stat. err: %d", err);
		goto fail;
	}

	seq_printf(s, "%llu\n", stat.power_active_time_us);

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_power_enable_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	bool gated;

	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get pdev. err: %d", err);
		goto fail;
	}
	nvdla_dbg_info(pdev, "[CTRL/POWER] enable show");

	err = nvdla_pm_power_is_gated(pdev, &gated);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get status. err: %d", err);
		goto fail;
	}

	seq_printf(s, "%x\n", (int) !gated);
	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_power_idlecount_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_pm_stat stat;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/POWER] idlecount show");

	err = nvdla_pm_get_stat(pdev, &stat);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to fetch stat. err: %d", err);
		goto fail;
	}

	seq_printf(s, "%llu\n", stat.power_idle_count);

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_power_autosuspenddelay_us_show(struct seq_file *s,
	void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvhost_device_data *pdata;
	struct nvdla_debug_ctrl *ctrl;
	struct dla_lpwr_config config;
	uint32_t delay_us;

	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;


	nvdla_dbg_info(pdev, "[CTRL/POWER] autosuspenddelay_us show");

	ctrl = s_nvdla_debug_ctrl_get_by_pdev(pdev);
	if (ctrl == NULL) {
		nvdla_dbg_err(pdev, "No ctrl node available\n");
		err = -ENOMEM;
		goto fail;
	}

#if defined(NVDLA_HAVE_CONFIG_FWSUSPEND) && (NVDLA_HAVE_CONFIG_FWSUSPEND == 1)
	err = nvdla_pm_get_lpwr_config(pdev, &config);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to get config. err: %d", err);
		goto fail;
	}

	delay_us = config.idledelay_us;
	(void) pdata;
#else
	pdata = platform_get_drvdata(pdev);
	if (pdata == NULL) {
		err = -EINVAL;
		goto fail;
	}

	delay_us = pdata->autosuspend_delay;
	(void) config;
#endif /* NVDLA_HAVE_CONFIG_FWSUSPEND */

	seq_printf(s, "%u\n", (uint32_t) delay_us);

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_power_idledelay_us_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_debug_ctrl *ctrl;
	uint32_t delay_us;

	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;


	nvdla_dbg_info(pdev, "[CTRL/POWER] idledelay_us show");

	ctrl = s_nvdla_debug_ctrl_get_by_pdev(pdev);
	if (ctrl == NULL) {
		nvdla_dbg_err(pdev, "No ctrl node available\n");
		err = -ENOMEM;
		goto fail;
	}

	err = nvdla_pm_power_gate_get_delay_us(pdev, &delay_us);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to get delay us. err: %d\n", err);
		goto fail;
	}

	seq_printf(s, "%u\n", (uint32_t) delay_us);

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_power_idletime_us_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_pm_stat stat;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/POWER] idletime_us show");

	err = nvdla_pm_get_stat(pdev, &stat);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to fetch stat. err: %d", err);
		goto fail;
	}

	seq_printf(s, "%llu\n", stat.power_idle_time_us);

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_power_mode_show(struct seq_file *s, void *data)
{
	int err;

	struct platform_device *pdev;
	struct dla_lpwr_config config;

	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/POWER] mode show");

#if defined(NVDLA_HAVE_CONFIG_FWSUSPEND) && (NVDLA_HAVE_CONFIG_FWSUSPEND == 1)
	err = nvdla_pm_get_lpwr_config(pdev, &config);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to get config. err: %d", err);
		goto fail;
	}

	switch (config.idle_notification_enable) {
	case DLA_IDLE_NOTIFICATION_DISABLE: {
		seq_puts(s, "alwayson\n");
		break;
	}
	case DLA_IDLE_NOTIFICATION_ENABLE: {
		seq_puts(s, "auto\n");
		break;
	}
	default:
		break;
	}
#endif

	(void) config;

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_power_statdump_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_pm_stat stat;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/POWER] statdump show");

	err = nvdla_pm_get_stat(pdev, &stat);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to fetch stat. err: %d", err);
		goto fail;
	}

	seq_puts(s, "PG entry latency in us (max/min/total): ");
	if (stat.pg_entry_latency_us_total > 0) {
		seq_printf(s, "%llu / %llu / %llu\n",
			stat.pg_entry_latency_us_max,
			stat.pg_entry_latency_us_min,
			stat.pg_entry_latency_us_total);
	} else {
		/* Data not available */
		seq_puts(s, "- / - / -\n");
	}

	seq_puts(s, "PG exit latency in us (max/min/total): ");
	if (stat.pg_exit_latency_us_total > 0) {
		seq_printf(s, "%llu / %llu / %llu\n",
			stat.pg_exit_latency_us_max,
			stat.pg_exit_latency_us_min,
			stat.pg_exit_latency_us_total);
	} else {
		/* Data not available */
		seq_puts(s, "- / - / -\n");
	}

	seq_printf(s, "PG Entry count: %llu\n", stat.power_idle_count);
	seq_printf(s, "PG Exit count: %llu\n", stat.power_active_count - 1U);

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_power_vftable_mv_khz_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_pm_info info;
	uint32_t ii;

	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/POWER] vftable_mv_khz show");

	err = nvdla_pm_get_info(pdev, &info);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to fetch pm info. err: %d", err);
		goto fail;
	}

	seq_puts(s, "Voltage(mV)\tFrequency(kHz)\n");
	for (ii = 0U; ii < info.num_vftable_entries; ii++) {
		/* Tab separated for pretty alignment */
		seq_printf(s, "%5u\t%5u\n", info.vftable_voltage_mV[ii],
			info.vftable_freq_kHz[ii]);
	}

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_power_voltage_mv_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	uint32_t voltage;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/POWER] voltage_mv show");

	err = nvdla_pm_get_current_voltage(pdev, &voltage);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to fetch voltage. err: %d", err);
		goto fail;
	}

	seq_printf(s, "%u\n", voltage);

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_power_draw_mw_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	uint32_t power_draw;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/POWER] power_draw_mw show");

	err = nvdla_pm_get_current_power_draw(pdev, &power_draw);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to fetch power_draw. err: %d", err);
		goto fail;
	}

	seq_printf(s, "%u\n", power_draw);

	return 0;

fail:
	return err;
}

// ctrl rail show functions
static int debug_dla_ctrl_rail_activetime_us_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_pm_stat stat;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/RAIL] activetime_us show");

	err = nvdla_pm_get_stat(pdev, &stat);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to fetch stat. err: %d", err);
		goto fail;
	}

	seq_printf(s, "%llu\n", stat.rail_active_time_us);

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_rail_enable_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	bool gated;

	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get pdev. err: %d", err);
		goto fail;
	}
	nvdla_dbg_info(pdev, "[CTRL/RAIL] enable show");

	err = nvdla_pm_rail_is_gated(pdev, &gated);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get status. err: %d", err);
		goto fail;
	}

	seq_printf(s, "%x\n", (int) !gated);
	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_rail_idlecount_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_pm_stat stat;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/RAIL] idlecount show");

	err = nvdla_pm_get_stat(pdev, &stat);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to fetch stat. err: %d", err);
		goto fail;
	}

	seq_printf(s, "%llu\n", stat.rail_idle_count);

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_rail_idledelay_us_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_debug_ctrl *ctrl;
	uint32_t delay_us;

	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;


	nvdla_dbg_info(pdev, "[CTRL/RAIL] idledelay_us show");

	ctrl = s_nvdla_debug_ctrl_get_by_pdev(pdev);
	if (ctrl == NULL) {
		nvdla_dbg_err(pdev, "No ctrl node available\n");
		err = -ENOMEM;
		goto fail;
	}

	err = nvdla_pm_rail_gate_get_delay_us(pdev, &delay_us);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to get delay us. err: %d\n", err);
		goto fail;
	}

	seq_printf(s, "%u\n", (uint32_t) delay_us);

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_rail_idletime_us_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_pm_stat stat;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/RAIL] idletime_us show");

	err = nvdla_pm_get_stat(pdev, &stat);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to fetch stat. err: %d", err);
		goto fail;
	}

	seq_printf(s, "%llu\n", stat.rail_idle_time_us);

	return 0;

fail:
	return err;
}

static int debug_dla_ctrl_rail_statdump_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_pm_stat stat;
	(void) data;

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/RAIL] statdump show");

	err = nvdla_pm_get_stat(pdev, &stat);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to fetch stat. err: %d", err);
		goto fail;
	}

	seq_puts(s, "RG entry latency in us (max/min/total): ");
	if (stat.rg_entry_latency_us_total > 0) {
		seq_printf(s, "%llu / %llu / %llu\n",
			stat.rg_entry_latency_us_max,
			stat.rg_entry_latency_us_min,
			stat.rg_entry_latency_us_total);
	} else {
		/* Data not available */
		seq_puts(s, "- / - / -\n");
	}

	seq_puts(s, "RG exit latency in us (max/min/total): ");
	if (stat.rg_exit_latency_us_total > 0) {
		seq_printf(s, "%llu / %llu / %llu\n",
			stat.rg_exit_latency_us_max,
			stat.rg_exit_latency_us_min,
			stat.rg_exit_latency_us_total);
	} else {
		/* Data not available */
		seq_puts(s, "- / - / -\n");
	}

	seq_printf(s, "RG Entry count: %llu\n", stat.rail_idle_count);
	seq_printf(s, "RG Exit count: %llu\n", stat.rail_active_count - 1U);

	return 0;

fail:
	return err;
}

/*
 * When the user calls this debugfs node, the configurable
 * window size value is passed down to the FW
 */
static int nvdla_set_window_size(struct nvdla_device *nvdla_dev)
{
	int err = 0;
	struct nvdla_cmd_data cmd_data;
	struct platform_device *pdev;

	/* prepare command data */
	cmd_data.method_id = DLA_CMD_SET_STAT_WINDOW_SIZE;
	cmd_data.method_data = ALIGNED_DMA(nvdla_dev->window_mem_pa);
	cmd_data.wait = true;

	pdev = nvdla_dev->pdev;
	if (pdev == NULL) {
		err = -EFAULT;
		goto fail_no_dev;
	}

	/* make sure that device is powered on */
	err = nvdla_module_busy(pdev);
	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to power on\n");
		err = -ENODEV;
		goto fail_no_dev;
	}

	/* pass set debug command to falcon */
	err = nvdla_fw_send_cmd(pdev, &cmd_data);
	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to send set window command");
		goto fail_to_send_cmd;
	}

fail_to_send_cmd:
	nvdla_module_idle(pdev);
fail_no_dev:
	return err;
}

static ssize_t debug_dla_fw_stat_window_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct seq_file *priv_data;
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;
	long write_value;
	u32 *window_va;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	priv_data = file->private_data;
	if (priv_data == NULL)
		goto fail;

	nvdla_dev = (struct nvdla_device *) priv_data->private;
	if (nvdla_dev == NULL)
		goto fail;

	pdev = nvdla_dev->pdev;
	if (pdev == NULL)
		goto fail;

	window_va = nvdla_dev->window_mem_va;
	if (write_value < UINT_MAX)
		*window_va = write_value;

	err = nvdla_set_window_size(nvdla_dev);
	if (err != 0) {
		nvdla_dbg_err(pdev, "Failed to send set window size command");
		goto fail;
	}

	return count;

fail:
	return -1;
}

static ssize_t debug_dla_fw_ping_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct platform_device *pdev;
	long write_value;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	err = s_nvdla_get_pdev_from_file(file, &pdev);
	if (err < 0)
		goto fail;

	if (write_value > 0) {
		struct nvdla_ping_args args = { write_value, 0 };
		uint32_t golden = (write_value * 4U);

		nvdla_dbg_info(pdev, "[PING] challenge: %u\n",
			(unsigned int) write_value);
		nvdla_dbg_info(pdev, "[PING] golden: %u\n", golden);

		err = nvdla_ping(pdev, &args);
		if (err < 0) {
			nvdla_dbg_err(pdev, "failed to ping\n");
			goto fail;
		}

		if (args.out_response != golden) {
			nvdla_dbg_err(pdev, "[PING] response != golden (%u != %u)\n",
				args.out_response, golden);
			goto fail;
		}

		nvdla_dbg_info(pdev, "[PING] successful\n");
	}

	return count;

fail:
	return -1;
}

#define NVDLA_DEBUG_PRETTYCONFIG_MAX_LEN (256U * 12U)
static ssize_t debug_dla_fw_dvfs_prettyconfig_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct platform_device *pdev;
	char *user_data = NULL;
	int user_data_length;
	struct prettyconfig *dvfs_prettyconfig = NULL;
	struct dla_dvfs_config *dvfs_config = NULL;

	err = s_nvdla_get_pdev_from_file(file, &pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to get platform device\n");
		return err;
	}

	user_data = kzalloc(NVDLA_DEBUG_PRETTYCONFIG_MAX_LEN, GFP_KERNEL);
	if (!user_data) {
		err = -ENOMEM;
		goto fail;
	}

	dvfs_prettyconfig = kzalloc(sizeof(*dvfs_prettyconfig), GFP_KERNEL);
	if (!dvfs_prettyconfig) {
		err = -ENOMEM;
		goto fail;
	}

	dvfs_config = kzalloc(sizeof(*dvfs_config), GFP_KERNEL);
	if (!dvfs_config) {
		err = -ENOMEM;
		goto fail;
	}

	user_data_length = simple_write_to_buffer(user_data,
					(NVDLA_DEBUG_PRETTYCONFIG_MAX_LEN - 1U),
					off,
					buffer,
					count);
	user_data[user_data_length] = '\0';

	nvdla_dbg_info(pdev, "received data: '%s'\n", user_data);

	err = parse_pretty_config(pdev, user_data, dvfs_prettyconfig);
	if (err < 0) {
		nvdla_dbg_err(pdev, "parse_pretty_config failed: %d\n", err);
		goto fail;
	}

	nvdla_dbg_info(pdev, "prettyconfig.key = %s\n", dvfs_prettyconfig->key);
	nvdla_dbg_info(pdev, "prettyconfig.nvalues = %u\n", dvfs_prettyconfig->nvalues);
	if (dvfs_prettyconfig->nvalues > 0) {
		for (int i = 0; i < dvfs_prettyconfig->nvalues; i++)
			nvdla_dbg_info(pdev, "prettyconfig.values[%d] = %s\n",
							i, dvfs_prettyconfig->values[i]);
	}

	err = nvdla_pm_get_dvfs_config(pdev, dvfs_config);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to get config. err: %d", err);
		goto fail;
	}

	if (strcmp(dvfs_prettyconfig->key, "mode") == 0) {
		uint8_t mode;

		err = kstrtou8(dvfs_prettyconfig->values[0], 10, &mode);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Invalid dvfs mode value");
			goto fail;
		}
		switch (mode) {
		case DLA_DVFS_MODE_DISABLE:
		case DLA_DVFS_MODE_ROUNDROBIN:
		case DLA_DVFS_MODE_DVFS:
			dvfs_config->mode = mode;
			break;
		default:
			err = -EINVAL;
			nvdla_dbg_err(pdev, "Invalid dvfs mode: %u", mode);
			goto fail;
		}
	} else if (strcmp(dvfs_prettyconfig->key, "roundrobin_interval_ms") == 0) {
		uint32_t periodicity_ms;

		err = kstrtou32(dvfs_prettyconfig->values[0], 10, &periodicity_ms);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Invalid roundrobin_periodicity_ms value");
			goto fail;
		}
		if (periodicity_ms < DLA_DVFS_PERIODICITY_MS_MIN) {
			err = -EINVAL;
			nvdla_dbg_err(pdev, "Periodicity must be >= %u ms",
				DLA_DVFS_PERIODICITY_MS_MIN);
			goto fail;
		}
		dvfs_config->periodicity_ms = periodicity_ms;
		nvdla_dbg_info(pdev, "periodicity_ms = %u\n", periodicity_ms);

	} else if (strcmp(dvfs_prettyconfig->key, "roundrobin_freq_khz") == 0) {
		uint32_t i;

		if (dvfs_prettyconfig->nvalues > DLA_DVFS_FREQ_LIST_MAX) {
			nvdla_dbg_err(pdev, "Too many frequencies specified");
			goto fail;
		}
		dvfs_config->num_roundrobin_freqs = dvfs_prettyconfig->nvalues;
		for (i = 0; i < dvfs_prettyconfig->nvalues; i++) {
			err = kstrtou32(dvfs_prettyconfig->values[i], 10,
				&dvfs_config->roundrobin_freq_kHz[i]);
			if (err < 0) {
				nvdla_dbg_err(pdev, "Invalid frequency value");
				goto fail;
			}
			nvdla_dbg_info(pdev, "roundrobin_freq_kHz[%d] = %u\n",
							i, dvfs_config->roundrobin_freq_kHz[i]);
		}
	} else if (strcmp(dvfs_prettyconfig->key, "telemetry_enable") == 0) {
		uint8_t telemetry_enable;

		err = kstrtou8(dvfs_prettyconfig->values[0], 10, &telemetry_enable);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Invalid telemetry enable value");
			goto fail;
		}
		switch (telemetry_enable) {
		case DLA_DVFS_TELEMETRY_DISABLE:
		case DLA_DVFS_TELEMETRY_ENABLE:
			dvfs_config->telemetry_enable = telemetry_enable;
			break;
		default:
			err = -EINVAL;
			nvdla_dbg_err(pdev, "Invalid telemetry enable value");
			goto fail;
		}
	} else if (strcmp(dvfs_prettyconfig->key, "default_priority") == 0) {
		uint8_t priority;

		err = kstrtou8(dvfs_prettyconfig->values[0], 10, &priority);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Invalid priority value");
			goto fail;
		}
		if (priority < DLA_DVFS_PRIORITY_MIN || priority > DLA_DVFS_PRIORITY_MAX) {
			err = -EINVAL;
			nvdla_dbg_err(pdev, "Priority must be between %u and %u",
				DLA_DVFS_PRIORITY_MIN, DLA_DVFS_PRIORITY_MAX);
			goto fail;
		}
		dvfs_config->priority = priority;
	} else if (strcmp(dvfs_prettyconfig->key, "pri_lookahead0") == 0) {
		uint8_t lookahead;

		err = kstrtou8(dvfs_prettyconfig->values[0], 10, &lookahead);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Invalid lookahead value");
			goto fail;
		}
		dvfs_config->pri_lookahead0 = lookahead;
	} else if (strcmp(dvfs_prettyconfig->key, "pri_lookahead1") == 0) {
		uint8_t lookahead;

		err = kstrtou8(dvfs_prettyconfig->values[0], 10, &lookahead);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Invalid lookahead value");
			goto fail;
		}
		dvfs_config->pri_lookahead1 = lookahead;
	} else if (strcmp(dvfs_prettyconfig->key, "pri_hyst") == 0) {
		uint32_t hyst;

		err = kstrtou32(dvfs_prettyconfig->values[0], 10, &hyst);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Invalid hysteresis value");
			goto fail;
		}
		dvfs_config->pri_hyst = hyst;
	} else if (strcmp(dvfs_prettyconfig->key, "mem_stall_thresholds") == 0) {
		uint32_t thresh1, thresh2, thresh3;

		if (dvfs_prettyconfig->nvalues != 3) {
			nvdla_dbg_err(pdev, "Need 3 threshold values");
			goto fail;
		}
		err = kstrtou32(dvfs_prettyconfig->values[0], 10, &thresh1);
		if (err < 0)
			goto fail;
		err = kstrtou32(dvfs_prettyconfig->values[1], 10, &thresh2);
		if (err < 0)
			goto fail;
		err = kstrtou32(dvfs_prettyconfig->values[2], 10, &thresh3);
		if (err < 0)
			goto fail;

		if (thresh1 >= thresh2 || thresh2 >= thresh3) {
			err = -EINVAL;
			nvdla_dbg_err(pdev, "Thresholds must be in ascending order");
			goto fail;
		}

		dvfs_config->mem_stall_thresh1 = thresh1;
		dvfs_config->mem_stall_thresh2 = thresh2;
		dvfs_config->mem_stall_thresh3 = thresh3;
	} else if (strcmp(dvfs_prettyconfig->key, "mem_stall_lvl2freq_throttle") == 0) {
		uint8_t throttle_no, throttle_low, throttle_med, throttle_high;

		if (dvfs_prettyconfig->nvalues != 4) {
			nvdla_dbg_err(pdev, "Need 4 throttle values");
			goto fail;
		}
		err = kstrtou8(dvfs_prettyconfig->values[0], 10, &throttle_no);
		if (err < 0)
			goto fail;
		err = kstrtou8(dvfs_prettyconfig->values[1], 10, &throttle_low);
		if (err < 0)
			goto fail;
		err = kstrtou8(dvfs_prettyconfig->values[2], 10, &throttle_med);
		if (err < 0)
			goto fail;
		err = kstrtou8(dvfs_prettyconfig->values[3], 10, &throttle_high);
		if (err < 0)
			goto fail;

		if (throttle_no >= throttle_low || throttle_low >= throttle_med ||
			throttle_med >= throttle_high) {
			err = -EINVAL;
			nvdla_dbg_err(pdev, "Throttle values must be in ascending order");
			goto fail;
		}

		dvfs_config->mem_stall_lvl2freq_throttle_no = throttle_no;
		dvfs_config->mem_stall_lvl2freq_throttle_low = throttle_low;
		dvfs_config->mem_stall_lvl2freq_throttle_med = throttle_med;
		dvfs_config->mem_stall_lvl2freq_throttle_high = throttle_high;
	} else if (strcmp(dvfs_prettyconfig->key, "arb_freq_weights") == 0) {
		uint32_t weight0, weight1, weight2;

		err = kstrtou32(dvfs_prettyconfig->values[0], 10, &weight0);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Invalid weight0 value");
			goto fail;
		}

		err = kstrtou32(dvfs_prettyconfig->values[1], 10, &weight1);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Invalid weight1 value");
			goto fail;
		}

		err = kstrtou32(dvfs_prettyconfig->values[2], 10, &weight2);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Invalid weight2 value");
			goto fail;
		}

		if ((weight0 + weight1 + weight2) != 100U) {
			err = -EINVAL;
			nvdla_dbg_err(pdev, "Weights must sum to 100");
			goto fail;
		}

		dvfs_config->arb_freq_weight0 = weight0;
		dvfs_config->arb_freq_weight1 = weight1;
		dvfs_config->arb_freq_weight2 = weight2;
	} else if (strcmp(dvfs_prettyconfig->key, "arb_policy") == 0) {
		uint8_t policy;

		err = kstrtou8(dvfs_prettyconfig->values[0], 10, &policy);
		if (err < 0) {
			nvdla_dbg_err(pdev, "Invalid policy value");
			goto fail;
		}
		switch (policy) {
		case DVFS_POLICY_MAX:
		case DVFS_POLICY_WEIGHTED_AVG:
			dvfs_config->arb_policy_selection = policy;
			break;
		default:
			err = -EINVAL;
			nvdla_dbg_err(pdev, "Invalid arb policy value");
			goto fail;
		}
	} else if (strcmp(dvfs_prettyconfig->key, "util2freq_indices") == 0) {
		uint8_t lo_util, hi_util;

		if (dvfs_prettyconfig->nvalues != 2) {
			nvdla_dbg_err(pdev, "Need 2 util index values");
			goto fail;
		}
		err = kstrtou8(dvfs_prettyconfig->values[0], 10, &lo_util);
		if (err < 0)
			goto fail;
		err = kstrtou8(dvfs_prettyconfig->values[1], 10, &hi_util);
		if (err < 0)
			goto fail;

		dvfs_config->util2freq_index_lo_util = lo_util;
		dvfs_config->util2freq_index_hi_util = hi_util;
	} else if (strcmp(dvfs_prettyconfig->key, "util_thresholds") == 0) {
		uint8_t lo_thresh, hi_thresh;

		if (dvfs_prettyconfig->nvalues != 2) {
			nvdla_dbg_err(pdev, "Need 2 util threshold values");
			goto fail;
		}
		err = kstrtou8(dvfs_prettyconfig->values[0], 10, &lo_thresh);
		if (err < 0)
			goto fail;
		err = kstrtou8(dvfs_prettyconfig->values[1], 10, &hi_thresh);
		if (err < 0)
			goto fail;

		if (lo_thresh >= hi_thresh) {
			err = -EINVAL;
			nvdla_dbg_err(pdev, "Low threshold must be less than high threshold");
			goto fail;
		}

		dvfs_config->util_thresh_lo_util = lo_thresh;
		dvfs_config->util_thresh_hi_util = hi_thresh;
	} else {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "Invalid dvfs config key: %s", dvfs_prettyconfig->key);
		goto fail;
	}

	err = nvdla_pm_set_dvfs_config(pdev, dvfs_config);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to set dvfs config. err: %d", err);
		goto fail;
	}

fail:
	kfree(dvfs_config);
	kfree(dvfs_prettyconfig);
	kfree(user_data);
	if (err < 0)
		return err;
	return count;
}

static ssize_t debug_dla_fw_dvfs_mode_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct platform_device *pdev;
	long write_value;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	err = s_nvdla_get_pdev_from_file(file, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[DVFS] mode = %u\n", (unsigned int) write_value);

	return count;

fail:
	return -1;
}

static ssize_t debug_dla_fw_dvfs_enable_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct platform_device *pdev;
	long write_value;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	err = s_nvdla_get_pdev_from_file(file, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[DVFS] enable = %u\n", (unsigned int) write_value);

	return count;

fail:
	return -1;
}

static ssize_t debug_dla_fw_dvfs_freqlist_khz_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct platform_device *pdev;
	long write_value;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	err = s_nvdla_get_pdev_from_file(file, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[DVFS] freqlist_khz = %u\n", (unsigned int) write_value);

	return count;

fail:
	return -1;
}

static ssize_t debug_dla_fw_dvfs_periodicity_ms_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct platform_device *pdev;
	long write_value;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	err = s_nvdla_get_pdev_from_file(file, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[DVFS] periodicity_ms = %u\n", (unsigned int) write_value);

	return count;

fail:
	return -1;
}

// ctrl clk write functions
static ssize_t debug_dla_ctrl_clk_enable_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct platform_device *pdev;
	long write_value;
	struct nvdla_debug_ctrl *ctrl;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	err = s_nvdla_get_pdev_from_file(file, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/CLK] enable = %u\n", (unsigned int) write_value);

	ctrl = s_nvdla_debug_ctrl_get_by_pdev(pdev);
	if (ctrl == NULL) {
		nvdla_dbg_err(pdev, "No ctrl node available\n");
		goto fail;
	}

	if (write_value > 0U)
		err = nvdla_pm_clock_ungate(pdev);
	else
		err = nvdla_pm_clock_gate(pdev, false);

	return count;

fail:
	return -1;
}

static ssize_t debug_dla_ctrl_clk_core_khz_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct seq_file *priv_data;
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;
	long write_value;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	priv_data = file->private_data;
	if (priv_data == NULL)
		goto fail;

	nvdla_dev = (struct nvdla_device *) priv_data->private;
	if (nvdla_dev == NULL)
		goto fail;

	pdev = nvdla_dev->pdev;
	if (pdev == NULL)
		goto fail;

	if (write_value > UINT_MAX)
		goto fail;

	err = nvdla_pm_clock_set_core_freq(pdev, write_value);
	if (err != 0) {
		nvdla_dbg_err(pdev, "Failed to send set core freq command");
		goto fail;
	}

	return count;

fail:
	return -1;
}

static ssize_t debug_dla_ctrl_clk_mcu_khz_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct seq_file *priv_data;
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;
	long write_value;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	priv_data = file->private_data;
	if (priv_data == NULL)
		goto fail;

	nvdla_dev = (struct nvdla_device *) priv_data->private;
	if (nvdla_dev == NULL)
		goto fail;

	pdev = nvdla_dev->pdev;
	if (pdev == NULL)
		goto fail;

	if (write_value > UINT_MAX)
		goto fail;

	err = nvdla_pm_clock_set_mcu_freq(pdev, write_value);
	if (err != 0) {
		nvdla_dbg_err(pdev, "Failed to send set mcu freq command");
		goto fail;
	}

	return count;

fail:
	return -1;
}

static ssize_t debug_dla_ctrl_clk_idledelay_us_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct platform_device *pdev;
	long write_value;
	struct nvdla_debug_ctrl *ctrl;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	err = s_nvdla_get_pdev_from_file(file, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/CLK] idledelay_us = %u\n", (unsigned int) write_value);

	ctrl = s_nvdla_debug_ctrl_get_by_pdev(pdev);
	if (ctrl == NULL) {
		nvdla_dbg_err(pdev, "No ctrl node available\n");
		goto fail;
	}

	err = nvdla_pm_clock_gate_set_delay_us(pdev, (uint32_t) write_value);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to set delay us. err: %d\n", err);
		goto fail;
	}

	return count;

fail:
	return -1;
}

// ctrl power write functions
static ssize_t debug_dla_ctrl_power_enable_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct platform_device *pdev;
	long write_value;
	struct nvdla_debug_ctrl *ctrl;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	err = s_nvdla_get_pdev_from_file(file, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/POWER] enable = %u\n",
		(unsigned int) write_value);

	ctrl = s_nvdla_debug_ctrl_get_by_pdev(pdev);
	if (ctrl == NULL) {
		nvdla_dbg_err(pdev, "No ctrl node available\n");
		goto fail;
	}

	if (write_value > 0U) {
		/* Ungate for any non-zero value */
		bool was_gated;
		err = nvdla_pm_power_is_gated(pdev, &was_gated);
		if (err < 0) {
			nvdla_dbg_err(pdev, "failed to get status. err: %d", err);
			goto fail;
		}

		err = nvdla_pm_power_ungate(pdev);
		if (err < 0) {
			nvdla_dbg_err(pdev, "power ungating failed.\n");
			goto fail;
		}

		if (was_gated) {
			err = nvdla_fw_poweron(pdev);
			if (err < 0) {
				nvdla_dbg_err(pdev, "Fw poweron failed.\n");
			}
		}
	} else {
		/* Ungate for any zero value */
		err = nvdla_pm_power_gate(pdev, false);
	}

	return count;

fail:
	return -1;
}

static ssize_t debug_dla_ctrl_power_autosuspenddelay_us_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct platform_device *pdev;
	long write_value;
	struct nvdla_debug_ctrl *ctrl;
	struct dla_lpwr_config config;
	struct nvhost_device_data *pdata;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	err = s_nvdla_get_pdev_from_file(file, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/POWER] autosuspenddelay_us = %u\n", (unsigned int) write_value);

	ctrl = s_nvdla_debug_ctrl_get_by_pdev(pdev);
	if (ctrl == NULL) {
		nvdla_dbg_err(pdev, "No ctrl node available\n");
		goto fail;
	}

#if defined(NVDLA_HAVE_CONFIG_FWSUSPEND) && (NVDLA_HAVE_CONFIG_FWSUSPEND == 1)
	/* Update the low power config to the power gate delay */
	err = nvdla_pm_get_lpwr_config(pdev, &config);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to get config. err: %d", err);
		goto fail;
	}

	config.idledelay_us = (uint32_t) write_value;

	err = nvdla_pm_set_lpwr_config(pdev, &config);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to set config. err: %d", err);
		goto fail;
	}
	(void) pdata;
#else
	pdata = platform_get_drvdata(pdev);
	if (pdata == NULL) {
		err = -EINVAL;
		goto fail;
	}

	pdata->autosuspend_delay = (uint32_t) write_value;
	(void) config;
#endif /* NVDLA_HAVE_CONFIG_FWSUSPEND */

	return count;

fail:
	return -1;
}

static ssize_t debug_dla_ctrl_power_idledelay_us_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct platform_device *pdev;
	long write_value;
	struct nvdla_debug_ctrl *ctrl;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	err = s_nvdla_get_pdev_from_file(file, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/POWER] idledelay_us = %u\n", (unsigned int) write_value);

	ctrl = s_nvdla_debug_ctrl_get_by_pdev(pdev);
	if (ctrl == NULL) {
		nvdla_dbg_err(pdev, "No ctrl node available\n");
		goto fail;
	}

	err = nvdla_pm_power_gate_set_delay_us(pdev, (uint32_t) write_value);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to set delay us. err: %d\n", err);
		goto fail;
	}

	return count;

fail:
	return -1;
}

#define NVDLA_DEBUG_POWER_MODE_MAX_LEN 256U
static ssize_t debug_dla_ctrl_power_mode_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int32_t err;

	struct platform_device *pdev;
	char mode[NVDLA_DEBUG_POWER_MODE_MAX_LEN];
	int32_t mode_length;

	struct dla_lpwr_config config;

	/* Fetch user requested write-value. */
	mode_length = simple_write_to_buffer(mode,
					(NVDLA_DEBUG_POWER_MODE_MAX_LEN - 1U),
					off,
					buffer,
					count);
	mode[mode_length] = '\0';

	err = s_nvdla_get_pdev_from_file(file, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/POWER] mode = %s\n", mode);

#if defined(NVDLA_HAVE_CONFIG_FWSUSPEND) && (NVDLA_HAVE_CONFIG_FWSUSPEND == 1)
	err = nvdla_pm_get_lpwr_config(pdev, &config);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to get config. err: %d", err);
		goto fail;
	}

	if (strcmp(mode, "auto") == 0) {
		/* This will ensure that, the DLA fw will notify idle */
		config.idle_notification_enable = DLA_IDLE_NOTIFICATION_ENABLE;
	} else if (strcmp(mode, "alwayson") == 0) {
		/* This will ensure that, the DLA fw will not notify idle */
		config.idle_notification_enable = DLA_IDLE_NOTIFICATION_DISABLE;
	} else {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "Invalid power mode: %s", mode);
		goto fail;
	}

	err = nvdla_pm_set_lpwr_config(pdev, &config);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to set power mode. err: %d", err);
		goto fail;
	}
#endif /* NVDLA_HAVE_CONFIG_FWSUSPEND */

	(void) config;

	return count;

fail:
	return err;
}

// ctrl rail write functions
static ssize_t debug_dla_ctrl_rail_enable_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct platform_device *pdev;
	long write_value;
	struct nvdla_debug_ctrl *ctrl;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	err = s_nvdla_get_pdev_from_file(file, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/RAIL] enable = %u...\n",
		(unsigned int) write_value);

	ctrl = s_nvdla_debug_ctrl_get_by_pdev(pdev);
	if (ctrl == NULL) {
		nvdla_dbg_err(pdev, "No ctrl node available\n");
		goto fail;
	}

	if (write_value > 0U)
		err = nvdla_pm_rail_ungate(pdev);
	else
		err = nvdla_pm_rail_gate(pdev, false);

	return count;

fail:
	return -1;
}

static ssize_t debug_dla_ctrl_rail_idledelay_us_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct platform_device *pdev;
	long write_value;
	struct nvdla_debug_ctrl *ctrl;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	err = s_nvdla_get_pdev_from_file(file, &pdev);
	if (err < 0)
		goto fail;

	nvdla_dbg_info(pdev, "[CTRL/RAIL] idledelay_us = %u\n", (unsigned int) write_value);

	ctrl = s_nvdla_debug_ctrl_get_by_pdev(pdev);
	if (ctrl == NULL) {
		nvdla_dbg_err(pdev, "No ctrl node available\n");
		goto fail;
	}

	err = nvdla_pm_rail_gate_set_delay_us(pdev, (uint32_t) write_value);
	if (err < 0) {
		nvdla_dbg_err(pdev, "Failed to set delay us. err: %d\n", err);
		goto fail;
	}

	return count;

fail:
	return -1;
}

// virt cmd show functions

static int debug_dla_cmd_response_mode_show(struct seq_file *s, void *data)
{
	int err;
	char *mode;
	struct platform_device *pdev;
	struct nvdla_device *nvdla_dev = (struct nvdla_device *)s->private;
	(void) data;

	if (nvdla_dev == NULL) {
		err = -EFAULT;
		goto fail;
	}

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get pdev. err %d", err);
		goto fail;
	}

	nvdla_dbg_info(pdev, "Cmd Response mode show");

	switch (nvdla_dev->response_info.response_mode) {
		case (NVDLA_RESPONSE_MODE_SWGEN): {
			mode = "SWGEN";
			break;
		}
		case (NVDLA_RESPONSE_MODE_MNOC): {
			mode = "MNOC";
			break;
		}
		default: {
			nvdla_dbg_err(pdev, "failed to get cmd mode");
			goto fail;
		}
	}

	seq_printf(s, "mode: %s\n", mode);
	return 0;

fail:
	return err;
}

static int debug_dla_cmd_response_stat_show(struct seq_file *s, void *data)
{
	int err;
	struct platform_device *pdev;
	struct nvdla_device *nvdla_dev = (struct nvdla_device *)s->private;
	(void) data;

	if (nvdla_dev == NULL) {
		err = -EFAULT;
		goto fail;
	}

	err = s_nvdla_get_pdev_from_seq(s, &pdev);
	if (err < 0) {
		nvdla_dbg_err(pdev, "failed to get pdev. err: %d", err);
		goto fail;
	}

	nvdla_dbg_info(pdev, "Cmd response stat show");

	seq_printf(s, "swgeninterrupts              : %u\n",
					nvdla_dev->response_info.num_interrupts_swgen);
	seq_printf(s, "lastexecutionerrorstatus     : %u\n",
					nvdla_dev->response_info.last_err_status_swgen);
	seq_printf(s, "lastexecutionerrortimestamp  : %llu\n",
					nvdla_dev->response_info.last_err_timestamp_swgen);

	seq_printf(s, "mnocinterrupts               : %u\n",
					nvdla_dev->response_info.num_interrupts_mnoc);
	seq_printf(s, "lastexecutionerrorstatus     : %u\n",
					nvdla_dev->response_info.last_err_status_mnoc);
	seq_printf(s, "lastexecutionerrortimestamp  : %llu\n",
					nvdla_dev->response_info.last_err_timestamp_mnoc);

	return 0;

fail:
	return err;
}

// virt cmd write functions

#define NVDLA_DEBUG_CMD_RESPONSE_MODE_MAX_LEN 256U
static ssize_t debug_dla_cmd_response_mode_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int32_t err;
	struct seq_file *priv_data;
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;
	struct nvhost_device_data *pdata;
	char mode[NVDLA_DEBUG_CMD_RESPONSE_MODE_MAX_LEN];
	int32_t mode_length;

	priv_data = file->private_data;
	if (priv_data == NULL) {
		err = -EFAULT;
		goto fail;
	}

	nvdla_dev = (struct nvdla_device *) priv_data->private;
	if (nvdla_dev == NULL) {
		err = -EFAULT;
		goto fail;
	}

	/* Fetch user requested write-value. */
	mode_length = simple_write_to_buffer(mode,
					(NVDLA_DEBUG_CMD_RESPONSE_MODE_MAX_LEN - 1U),
					off,
					buffer,
					count);
	mode[mode_length] = '\0';

	err = s_nvdla_get_pdev_from_file(file, &pdev);
	if (err < 0)
		goto fail;

	pdata = platform_get_drvdata(pdev);
	if (pdata == NULL) {
		err = -EFAULT;
		goto fail;
	}

	if (pdata->version != FIRMWARE_ENCODE_VERSION(T25X_V2)) {
		err = -EINVAL;
		goto fail;
	}

	nvdla_dbg_info(pdev, "[CMD RESPONSE] mode = %s\n", mode);

	if (strcmp(mode, "swgen") == 0) {
		/* set response mode to swgen */
		nvdla_dev->response_info.response_mode = NVDLA_RESPONSE_MODE_SWGEN;
	} else if (strcmp(mode, "mnoc") == 0) {
		/* set response mode to mnoc */
		nvdla_dev->response_info.response_mode = NVDLA_RESPONSE_MODE_MNOC;
	} else {
		err = -EINVAL;
		nvdla_dbg_err(pdev, "Invalid cmd response mode: %s", mode);
		goto fail;
	}

	return count;

fail:
	return err;
}

static int debug_dla_enable_trace_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_enable_trace_show, inode->i_private);
}

static int debug_dla_eventmask_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_eventmask_show, inode->i_private);
}

static int debug_dla_eventmask_help_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_eventmask_help_show, inode->i_private);
}

static int debug_dla_trace_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_tracedump_show, inode->i_private);
}

static int debug_dla_bintrace_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_bintracedump_show, inode->i_private);
}

static int debug_dla_en_fw_gcov_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_en_fw_gcov_show, inode->i_private);
}

static int debug_dla_fw_gcov_gcda_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_fw_gcov_gcda_show, inode->i_private);
}

static int debug_dla_fw_resource_util_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_fw_resource_util_show, inode->i_private);
}

static int debug_dla_fw_stat_window_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_fw_stat_window_show, inode->i_private);
}

static int debug_dla_fw_ping_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_fw_ping_show, inode->i_private);
}

// fw dvfs open callback functions
static int debug_dla_fw_dvfs_prettyconfig_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_fw_dvfs_prettyconfig_show, inode->i_private);
}

static int debug_dla_fw_dvfs_prettyconfig_help_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_fw_dvfs_prettyconfig_help_show, inode->i_private);
}

static int debug_dla_fw_dvfs_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_fw_dvfs_mode_show, inode->i_private);
}

static int debug_dla_fw_dvfs_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_fw_dvfs_enable_show, inode->i_private);
}

static int debug_dla_fw_dvfs_freqlist_khz_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_fw_dvfs_freqlist_khz_show, inode->i_private);
}

static int debug_dla_fw_dvfs_periodicity_ms_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_fw_dvfs_periodicity_ms_show, inode->i_private);
}

static int debug_dla_fw_dvfs_statdump_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_fw_dvfs_statdump_show, inode->i_private);
}

// ctrl clk open callback functions
static int debug_dla_ctrl_clk_activetime_us_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_clk_activetime_us_show, inode->i_private);
}

static int debug_dla_ctrl_clk_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_clk_enable_show, inode->i_private);
}
static int debug_dla_ctrl_clk_core_khz_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_clk_core_khz_show, inode->i_private);
}

static int debug_dla_ctrl_clk_mcu_khz_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_clk_mcu_khz_show, inode->i_private);
}
static int debug_dla_ctrl_clk_idlecount_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_clk_idlecount_show, inode->i_private);
}

static int debug_dla_ctrl_clk_idledelay_us_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_clk_idledelay_us_show, inode->i_private);
}
static int debug_dla_ctrl_clk_idletime_us_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_clk_idletime_us_show, inode->i_private);
}

static int debug_dla_ctrl_clk_statdump_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_clk_statdump_show, inode->i_private);
}

// ctrl power open callback functions
static int debug_dla_ctrl_power_activetime_us_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_power_activetime_us_show, inode->i_private);
}

static int debug_dla_ctrl_power_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_power_enable_show, inode->i_private);
}

static int debug_dla_ctrl_power_idlecount_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_power_idlecount_show, inode->i_private);
}

static int debug_dla_ctrl_power_idledelay_us_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_power_idledelay_us_show, inode->i_private);
}

static int debug_dla_ctrl_power_autosuspenddelay_us_open(struct inode *inode,
	struct file *file)
{
	return single_open(file, debug_dla_ctrl_power_autosuspenddelay_us_show,
				inode->i_private);
}

static int debug_dla_ctrl_power_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_power_mode_show, inode->i_private);
}

static int debug_dla_ctrl_power_idletime_us_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_power_idletime_us_show, inode->i_private);
}

static int debug_dla_ctrl_power_statdump_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_power_statdump_show, inode->i_private);
}

static int debug_dla_ctrl_power_vftable_mv_khz_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_power_vftable_mv_khz_show, inode->i_private);
}

static int debug_dla_ctrl_power_voltage_mv_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_power_voltage_mv_show, inode->i_private);
}

static int debug_dla_ctrl_power_draw_mw_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_power_draw_mw_show, inode->i_private);
}
// ctrl rail open callback functions
static int debug_dla_ctrl_rail_activetime_us_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_rail_activetime_us_show, inode->i_private);
}

static int debug_dla_ctrl_rail_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_rail_enable_show, inode->i_private);
}

static int debug_dla_ctrl_rail_idlecount_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_rail_idlecount_show, inode->i_private);
}

static int debug_dla_ctrl_rail_idledelay_us_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_rail_idledelay_us_show, inode->i_private);
}

static int debug_dla_ctrl_rail_idletime_us_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_rail_idletime_us_show, inode->i_private);
}

static int debug_dla_ctrl_rail_statdump_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_ctrl_rail_statdump_show, inode->i_private);
}

// virt cmd open callback functions
static int debug_dla_cmd_response_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_cmd_response_mode_show, inode->i_private);
}

static int debug_dla_cmd_response_stat_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_cmd_response_stat_show, inode->i_private);
}

static int debug_set_trace_event_config(struct platform_device *pdev,
	u32 value, u32 sub_cmd)
{
	int err = 0;
	struct nvdla_cmd_mem_info trace_events_mem_info;
	struct dla_debug_config *trace_event;
	struct nvdla_cmd_data cmd_data;

	/* make sure that device is powered on */
	err = nvdla_module_busy(pdev);
	if (err) {
		nvdla_dbg_err(pdev, "failed to power on\n");
		err = -ENODEV;
		goto fail_to_on;
	}

	/* assign memory for command */
	err = nvdla_get_cmd_memory(pdev, &trace_events_mem_info);
	if (err) {
		nvdla_dbg_err(pdev, "dma alloc for command failed");
		goto alloc_failed;
	}

	trace_event = (struct dla_debug_config *)trace_events_mem_info.va;
	trace_event->sub_cmd = sub_cmd;
	trace_event->data = (u64)value;

	/* prepare command data */
	cmd_data.method_id = DLA_CMD_SET_DEBUG;
	cmd_data.method_data = ALIGNED_DMA(trace_events_mem_info.pa);
	cmd_data.wait = true;

	/* pass set debug command to falcon */
	err = nvdla_fw_send_cmd(pdev, &cmd_data);

	/* free memory allocated for trace event command */
	nvdla_put_cmd_memory(pdev, trace_events_mem_info.index);

	if (err != 0) {
		nvdla_dbg_err(pdev, "failed to send set debug command");
		goto send_cmd_failed;
	}

	nvdla_module_idle(pdev);
	return err;

send_cmd_failed:
alloc_failed:
	nvdla_module_idle(pdev);
fail_to_on:
	return err;
}

static ssize_t debug_dla_eventmask_set(struct file *file,
	const char __user *buffer, size_t count, loff_t *off)
{
	int ret;
	u32 val;
	struct platform_device *pdev;
	struct nvdla_device *nvdla_dev;
	struct seq_file *p = file->private_data;
	char str[] = "0123456789abcdef";

	nvdla_dev = (struct nvdla_device *)p->private;
	pdev = nvdla_dev->pdev;
	count = min_t(size_t, strlen(str), count);
	if (copy_from_user(str, buffer, count))
		return -EFAULT;

	mutex_lock(&p->lock);
	/* get value entered by user in variable val */
	ret = sscanf(str, "%u", &val);
	/* Check valid values for event_mask */
	if (ret == 1 && val <= 31)
		nvdla_dev->events_mask = val;
	mutex_unlock(&p->lock);

	if (ret != 1) {
		nvdla_dbg_err(pdev, "Incorrect input!");
		goto invalid_input;
	}

	/*
	 * Currently only five trace categories are added,
	 * and hence only five bits are being used to enable/disable
	 * the trace categories.
	 */
	if (val > 31) {
		nvdla_dbg_err(pdev,
			"invalid input, please"
			" check /d/nvdla*/firmware/trace/events/help");
		ret = -EINVAL;
		goto invalid_input;
	}

	/*  set event_mask config  */
	ret = dla_set_trace_event_mask(pdev, nvdla_dev->events_mask);
	if (ret) {
		nvdla_dbg_err(pdev, "failed to set event mask.");
		goto set_event_mask_failed;
	}

	return count;

set_event_mask_failed:
invalid_input:
	return ret;
}

static ssize_t debug_dla_enable_trace_set(struct file *file,
	const char __user *buffer, size_t count, loff_t *off)
{
	int ret;
	u32 val;
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;
	struct seq_file *p = file->private_data;
	char str[] = "0123456789abcdef";

	nvdla_dev = (struct nvdla_device *)p->private;
	pdev = nvdla_dev->pdev;
	count = min_t(size_t, strlen(str), count);
	if (copy_from_user(str, buffer, count))
		return -EFAULT;

	mutex_lock(&p->lock);
	/* get value entered by user in variable val */
	ret = sscanf(str, "%u", &val);
	/* Check valid values for trace_enable */
	if (ret == 1 && (val == 0 || val == 1))
		nvdla_dev->trace_enable = val;
	mutex_unlock(&p->lock);

	if (ret != 1) {
		nvdla_dbg_err(pdev, "Incorrect input!");
		goto invalid_input;
	}

	if (val != 0 && val != 1) {
		nvdla_dbg_err(pdev,
			"invalid input, please"
			" enter 0(disable) or 1(enable)!");
		ret = -EINVAL;
		goto invalid_input;
	}

	/*  set trace_enable config  */
	ret = dla_set_trace_enable(pdev, nvdla_dev->trace_enable);
	if (ret) {
		nvdla_dbg_err(pdev, "failed to enable trace events.");
		goto set_trace_enable_failed;
	}

	return count;

set_trace_enable_failed:
invalid_input:
	return ret;
}

static ssize_t debug_dla_fw_reload_set(struct file *file,
	const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct seq_file *p = file->private_data;
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;
	long val;
	int ref_cnt;
	unsigned long end_jiffies;

	if (!p)
		return -EFAULT;
	nvdla_dev = (struct nvdla_device *)p->private;
	if (!nvdla_dev)
		return -EFAULT;
	pdev = nvdla_dev->pdev;
	if (!pdev)
		return -EFAULT;

	err = kstrtol_from_user(buffer, count, 10, &val);
	if (err < 0)
		return err;

	if (!val)
		return count; /* "0" does nothing */


	/* check current power ref count and make forced idle to
	 * suspend.
	 */
	ref_cnt = atomic_read(&pdev->dev.power.usage_count);
	nvdla_module_idle_mult(pdev, ref_cnt);

	/* check and wait until module is idle (with a timeout) */
	end_jiffies = jiffies + msecs_to_jiffies(2000);
	do {
		msleep(1);
		ref_cnt = atomic_read(&pdev->dev.power.usage_count);
	} while (ref_cnt != 0 && time_before(jiffies, end_jiffies));

	if (ref_cnt != 0)
		return -EBUSY;

	nvdla_dbg_info(pdev, "firmware reload requesting..\n");

	err = nvdla_fw_reload(pdev);
	if (err)
		return err; /* propagate firmware reload errors */

	/* make sure device in clean state by reset */
	nvdla_module_reset(pdev, true);

	return count;
}

static int debug_dla_fw_reload_show(struct seq_file *s, void *data)
{
	seq_puts(s, "0\n");
	return 0;
}

static int debug_dla_fw_reload_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_fw_reload_show, inode->i_private);
}

static const struct file_operations debug_dla_enable_trace_fops = {
		.open		= debug_dla_enable_trace_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_enable_trace_set,
};

static const struct file_operations debug_dla_eventmask_fops = {
		.open		= debug_dla_eventmask_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_eventmask_set,
};

static const struct file_operations debug_dla_eventmask_help_fops = {
		.open		= debug_dla_eventmask_help_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_event_trace_fops = {
		.open		= debug_dla_trace_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_bin_event_trace_fops = {
		.open		= debug_dla_bintrace_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_en_fw_gcov_fops = {
		.open		= debug_dla_en_fw_gcov_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_en_fw_gcov_alloc,
};

static const struct file_operations debug_dla_fw_gcov_gcda_fops = {
		.open		= debug_dla_fw_gcov_gcda_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations nvdla_fw_reload_fops = {
		.open		= debug_dla_fw_reload_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_fw_reload_set,
};

static const struct file_operations debug_dla_resource_util_fops = {
		.open		= debug_dla_fw_resource_util_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_stat_window_fops = {
		.open		= debug_dla_fw_stat_window_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_fw_stat_window_write,
};

static const struct file_operations debug_dla_ping_fops = {
		.open		= debug_dla_fw_ping_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_fw_ping_write,
};

// fw dvfs file operations
static const struct file_operations debug_dla_fw_dvfs_prettyconfig_fops = {
		.open		= debug_dla_fw_dvfs_prettyconfig_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_fw_dvfs_prettyconfig_write,
};

static const struct file_operations debug_dla_fw_dvfs_prettyconfig_help_fops = {
		.open		= debug_dla_fw_dvfs_prettyconfig_help_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_fw_dvfs_mode_fops = {
		.open		= debug_dla_fw_dvfs_mode_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_fw_dvfs_mode_write,
};

static const struct file_operations debug_dla_fw_dvfs_enable_fops = {
		.open		= debug_dla_fw_dvfs_enable_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_fw_dvfs_enable_write,
};

static const struct file_operations debug_dla_fw_dvfs_freqlist_khz_fops = {
		.open		= debug_dla_fw_dvfs_freqlist_khz_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_fw_dvfs_freqlist_khz_write,
};

static const struct file_operations debug_dla_fw_dvfs_periodicity_ms_fops = {
		.open		= debug_dla_fw_dvfs_periodicity_ms_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_fw_dvfs_periodicity_ms_write,
};

static const struct file_operations debug_dla_fw_dvfs_statdump_fops = {
		.open		= debug_dla_fw_dvfs_statdump_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

// ctrl clock file operations
static const struct file_operations debug_dla_ctrl_clk_activetime_us_fops = {
		.open		= debug_dla_ctrl_clk_activetime_us_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_ctrl_clk_enable_fops = {
		.open		= debug_dla_ctrl_clk_enable_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_ctrl_clk_enable_write,
};

static const struct file_operations debug_dla_ctrl_clk_core_khz_fops = {
		.open		= debug_dla_ctrl_clk_core_khz_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_ctrl_clk_core_khz_write,
};

static const struct file_operations debug_dla_ctrl_clk_mcu_khz_fops = {
		.open		= debug_dla_ctrl_clk_mcu_khz_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_ctrl_clk_mcu_khz_write,
};

static const struct file_operations debug_dla_ctrl_clk_idlecount_fops = {
		.open		= debug_dla_ctrl_clk_idlecount_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_ctrl_clk_idledelay_us_fops = {
		.open		= debug_dla_ctrl_clk_idledelay_us_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_ctrl_clk_idledelay_us_write,
};

static const struct file_operations debug_dla_ctrl_clk_idletime_us_fops = {
		.open		= debug_dla_ctrl_clk_idletime_us_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_ctrl_clk_statdump_fops = {
		.open		= debug_dla_ctrl_clk_statdump_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

// ctrl power file operations
static const struct file_operations debug_dla_ctrl_power_activetime_us_fops = {
		.open		= debug_dla_ctrl_power_activetime_us_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_ctrl_power_enable_fops = {
		.open		= debug_dla_ctrl_power_enable_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_ctrl_power_enable_write,
};

static const struct file_operations debug_dla_ctrl_power_idlecount_fops = {
		.open		= debug_dla_ctrl_power_idlecount_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_ctrl_power_idledelay_us_fops = {
		.open		= debug_dla_ctrl_power_idledelay_us_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_ctrl_power_idledelay_us_write,
};

static const struct file_operations debug_dla_ctrl_power_autosuspenddelay_us_fops = {
		.open		= debug_dla_ctrl_power_autosuspenddelay_us_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_ctrl_power_autosuspenddelay_us_write,
};

static const struct file_operations debug_dla_ctrl_power_idletime_us_fops = {
		.open		= debug_dla_ctrl_power_idletime_us_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_ctrl_power_mode_fops = {
		.open		= debug_dla_ctrl_power_mode_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_ctrl_power_mode_write,
};

static const struct file_operations debug_dla_ctrl_power_statdump_fops = {
		.open		= debug_dla_ctrl_power_statdump_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_ctrl_power_vftable_mv_khz_fops = {
		.open		= debug_dla_ctrl_power_vftable_mv_khz_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_ctrl_power_voltage_mv_fops = {
		.open		= debug_dla_ctrl_power_voltage_mv_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_ctrl_power_draw_mw_fops = {
		.open		= debug_dla_ctrl_power_draw_mw_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

// ctrl rail file operations
static const struct file_operations debug_dla_ctrl_rail_activetime_us_fops = {
		.open		= debug_dla_ctrl_rail_activetime_us_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_ctrl_rail_enable_fops = {
		.open		= debug_dla_ctrl_rail_enable_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_ctrl_rail_enable_write,
};

static const struct file_operations debug_dla_ctrl_rail_idlecount_fops = {
		.open		= debug_dla_ctrl_rail_idlecount_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_ctrl_rail_idledelay_us_fops = {
		.open		= debug_dla_ctrl_rail_idledelay_us_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_ctrl_rail_idledelay_us_write,
};

static const struct file_operations debug_dla_ctrl_rail_idletime_us_fops = {
		.open		= debug_dla_ctrl_rail_idletime_us_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static const struct file_operations debug_dla_ctrl_rail_statdump_fops = {
		.open		= debug_dla_ctrl_rail_statdump_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

// virt cmd file operations
static const struct file_operations debug_dla_cmd_response_mode_fops = {
		.open		= debug_dla_cmd_response_mode_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
		.write		= debug_dla_cmd_response_mode_write,
};

static const struct file_operations debug_dla_cmd_response_stat_fops = {
		.open		= debug_dla_cmd_response_stat_open,
		.read		= seq_read,
		.llseek		= seq_lseek,
		.release	= single_release,
};

static void dla_fw_debugfs_init(struct platform_device *pdev)
{
	struct dentry *fw_dir, *fw_trace, *events, *fw_gcov, *fw_dvfs;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	struct dentry *dla_debugfs_root = pdata->debugfs;

	if (!dla_debugfs_root)
		return;

	fw_dir = debugfs_create_dir("firmware", dla_debugfs_root);
	if (!fw_dir)
		return;

	fw_trace = debugfs_create_dir("trace", fw_dir);
	if (!fw_trace)
		goto cleanup;

	if (!debugfs_create_file("version", 0444, fw_dir, nvdla_dev,
							&nvdla_fw_ver_fops) ||
		!debugfs_create_file("reload", 0600, fw_dir, nvdla_dev,
							&nvdla_fw_reload_fops) ||
		!debugfs_create_file("utilization_rate", 0400, fw_dir, nvdla_dev,
							&debug_dla_resource_util_fops) ||
		!debugfs_create_file("stat_window_size", 0600, fw_dir, nvdla_dev,
							&debug_dla_stat_window_fops) ||
		!debugfs_create_file("ping", 0600, fw_dir, nvdla_dev,
							&debug_dla_ping_fops) ||
		!debugfs_create_file("enable", 0644, fw_trace, nvdla_dev,
							&debug_dla_enable_trace_fops) ||
		!debugfs_create_file("text_trace", 0444, fw_trace, nvdla_dev,
							&debug_dla_event_trace_fops) ||
		!debugfs_create_file("bin_trace", 0444, fw_trace, nvdla_dev,
							&debug_dla_bin_event_trace_fops)) {
		goto cleanup;
	}

	events = debugfs_create_dir("events", fw_trace);
	if (!events ||
		!debugfs_create_file("category", 0644, events, nvdla_dev,
							&debug_dla_eventmask_fops) ||
		!debugfs_create_file("help", 0444, events, nvdla_dev,
							&debug_dla_eventmask_help_fops)) {
		goto cleanup;
	}

	fw_gcov = debugfs_create_dir("gcov", fw_dir);
	if (!fw_gcov ||
		!debugfs_create_file("enable", 0644, fw_gcov, nvdla_dev,
							&debug_dla_en_fw_gcov_fops) ||
		!debugfs_create_file("gcda", 0444, fw_gcov, nvdla_dev,
							&debug_dla_fw_gcov_gcda_fops)) {
		goto cleanup;
	}

	fw_dvfs = debugfs_create_dir("dvfs", fw_dir);
	if (!fw_dvfs ||
		!debugfs_create_file("prettyconfig", 0600, fw_dvfs, nvdla_dev,
							&debug_dla_fw_dvfs_prettyconfig_fops) ||
		!debugfs_create_file("prettyconfighelp", 0600, fw_dvfs, nvdla_dev,
							&debug_dla_fw_dvfs_prettyconfig_help_fops) ||
		!debugfs_create_file("mode", 0600, fw_dvfs, nvdla_dev,
							&debug_dla_fw_dvfs_mode_fops) ||
		!debugfs_create_file("enable", 0600, fw_dvfs, nvdla_dev,
							&debug_dla_fw_dvfs_enable_fops) ||
		!debugfs_create_file("freqlist_khz", 0600, fw_dvfs, nvdla_dev,
							&debug_dla_fw_dvfs_freqlist_khz_fops) ||
		!debugfs_create_file("periodicity_ms", 0600, fw_dvfs, nvdla_dev,
							&debug_dla_fw_dvfs_periodicity_ms_fops) ||
		!debugfs_create_file("statdump", 0400, fw_dvfs, nvdla_dev,
							&debug_dla_fw_dvfs_statdump_fops)) {
		goto cleanup;
	}

	return;

cleanup:
	debugfs_remove_recursive(fw_dir);
}

static void dla_ctrl_debugfs_init(struct platform_device *pdev)
{
	struct dentry *ctrl_dir, *ctrl_clk, *ctrl_power, *ctrl_rail;
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	struct dentry *dla_debugfs_root = pdata->debugfs;

	if (!dla_debugfs_root)
		return;

	ctrl_dir = debugfs_create_dir("ctrl", dla_debugfs_root);
	if (!ctrl_dir)
		return;

	ctrl_clk = debugfs_create_dir("clk", ctrl_dir);
	if (!ctrl_clk ||
		!debugfs_create_file("activetime_us", 0400, ctrl_clk, nvdla_dev,
							&debug_dla_ctrl_clk_activetime_us_fops) ||
		!debugfs_create_file("enable", 0600, ctrl_clk, nvdla_dev,
							&debug_dla_ctrl_clk_enable_fops) ||
		!debugfs_create_file("core_khz", 0600, ctrl_clk, nvdla_dev,
							&debug_dla_ctrl_clk_core_khz_fops) ||
		!debugfs_create_file("mcu_khz", 0600, ctrl_clk, nvdla_dev,
							&debug_dla_ctrl_clk_mcu_khz_fops) ||
		!debugfs_create_file("idlecount", 0400, ctrl_clk, nvdla_dev,
							&debug_dla_ctrl_clk_idlecount_fops) ||
		!debugfs_create_file("idledelay_us", 0600, ctrl_clk, nvdla_dev,
							&debug_dla_ctrl_clk_idledelay_us_fops) ||
		!debugfs_create_file("idletime_us", 0400, ctrl_clk, nvdla_dev,
							&debug_dla_ctrl_clk_idletime_us_fops) ||
		!debugfs_create_file("statdump", 0400, ctrl_clk, nvdla_dev,
							&debug_dla_ctrl_clk_statdump_fops)) {
		goto cleanup;
	}

	ctrl_power = debugfs_create_dir("power", ctrl_dir);
	if (!ctrl_power ||
		!debugfs_create_file("activetime_us", 0400, ctrl_power, nvdla_dev,
							&debug_dla_ctrl_power_activetime_us_fops) ||
		!debugfs_create_file("autosuspenddelay_us", 0600, ctrl_power,
			nvdla_dev,
			&debug_dla_ctrl_power_autosuspenddelay_us_fops) ||
		!debugfs_create_file("enable", 0600, ctrl_power, nvdla_dev,
							&debug_dla_ctrl_power_enable_fops) ||
		!debugfs_create_file("idlecount", 0400, ctrl_power, nvdla_dev,
							&debug_dla_ctrl_power_idlecount_fops) ||
		!debugfs_create_file("idledelay_us", 0600, ctrl_power, nvdla_dev,
							&debug_dla_ctrl_power_idledelay_us_fops) ||
		!debugfs_create_file("idletime_us", 0400, ctrl_power, nvdla_dev,
							&debug_dla_ctrl_power_idletime_us_fops) ||
		!debugfs_create_file("mode", 0600, ctrl_power, nvdla_dev,
							&debug_dla_ctrl_power_mode_fops) ||
		!debugfs_create_file("statdump", 0400, ctrl_power, nvdla_dev,
							&debug_dla_ctrl_power_statdump_fops) ||
		!debugfs_create_file("vftable_mV_kHz", 0400, ctrl_power, nvdla_dev,
							&debug_dla_ctrl_power_vftable_mv_khz_fops) ||
		!debugfs_create_file("power_mW", 0400, ctrl_power, nvdla_dev,
							&debug_dla_ctrl_power_draw_mw_fops) ||
		!debugfs_create_file("voltage_mV", 0400, ctrl_power, nvdla_dev,
							&debug_dla_ctrl_power_voltage_mv_fops)) {
		goto cleanup;
	}

	ctrl_rail = debugfs_create_dir("rail", ctrl_dir);
	if (!ctrl_rail ||
		!debugfs_create_file("activetime_us", 0400, ctrl_rail, nvdla_dev,
							&debug_dla_ctrl_rail_activetime_us_fops) ||
		!debugfs_create_file("enable", 0600, ctrl_rail, nvdla_dev,
							&debug_dla_ctrl_rail_enable_fops) ||
		!debugfs_create_file("idlecount", 0400, ctrl_rail, nvdla_dev,
							&debug_dla_ctrl_rail_idlecount_fops) ||
		!debugfs_create_file("idledelay_us", 0600, ctrl_rail, nvdla_dev,
							&debug_dla_ctrl_rail_idledelay_us_fops) ||
		!debugfs_create_file("idletime_us", 0400, ctrl_rail, nvdla_dev,
							&debug_dla_ctrl_rail_idletime_us_fops) ||
		!debugfs_create_file("statdump", 0400, ctrl_rail, nvdla_dev,
							&debug_dla_ctrl_rail_statdump_fops)) {
		goto cleanup;
	}

	return;

cleanup:
	debugfs_remove_recursive(ctrl_dir);
}

static void dla_cmd_response_debugfs_init(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	struct dentry *dla_debugfs_root = pdata->debugfs;

	if (!dla_debugfs_root)
		return;

	if (!debugfs_create_file("cmdresponsemode", 0600, dla_debugfs_root,
			nvdla_dev, &debug_dla_cmd_response_mode_fops)) {
		goto fail;
	}

	if (!debugfs_create_file("cmdresponsestat", 0400, dla_debugfs_root,
			nvdla_dev, &debug_dla_cmd_response_stat_fops)) {
		goto fail;
	}

fail:
	return;
}

#ifdef CONFIG_PM
static int debug_dla_pm_suspend_show(struct seq_file *s, void *data)
{
	int err;
	struct nvdla_device *nvdla_dev;

	if (s == NULL) {
		err = -EFAULT;
		goto fail;
	}

	nvdla_dev = (struct nvdla_device *) s->private;
	if (nvdla_dev == NULL) {
		err = -EFAULT;
		goto fail;
	}

	seq_printf(s, "%x\n", (int) nvdla_dev->is_suspended);

	return 0;

fail:
	return err;
}

static int debug_dla_pm_suspend_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_pm_suspend_show, inode->i_private);
}

static ssize_t debug_dla_pm_suspend_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct seq_file *priv_data;
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;
	long write_value;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	/* Trigger suspend & response */
	priv_data = file->private_data;
	if (priv_data == NULL)
		goto fail;

	nvdla_dev = (struct nvdla_device *) priv_data->private;
	if (nvdla_dev == NULL)
		goto fail;

	pdev = nvdla_dev->pdev;
	if (pdev == NULL)
		goto fail;

	if ((write_value > 0) && (!nvdla_dev->is_suspended)) {
		/* Trigger suspend sequence. */
		err = nvdla_module_pm_ops.prepare(&pdev->dev);
		if (err < 0)
			goto fail;

		err = nvdla_module_pm_ops.suspend(&pdev->dev);
		if (err < 0) {
			nvdla_module_pm_ops.complete(&pdev->dev);
			goto fail;
		}
	} else if ((write_value == 0) && (nvdla_dev->is_suspended)) {
		/* Trigger resume sequence. */
		err = nvdla_module_pm_ops.resume(&pdev->dev);
		if (err < 0)
			goto fail;

		nvdla_module_pm_ops.complete(&pdev->dev);
	}

	return count;

fail:
	return -1;
}

static const struct file_operations debug_dla_pm_suspend_fops = {
	.open		= debug_dla_pm_suspend_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= debug_dla_pm_suspend_write,
};

static void nvdla_pm_debugfs_init(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	struct dentry *dla_debugfs_root = pdata->debugfs;

	if (!debugfs_create_file("suspend", 0600, dla_debugfs_root,
			nvdla_dev, &debug_dla_pm_suspend_fops)) {
		goto fail_create_file_suspend;
	}

fail_create_file_suspend:
	return;
}
#endif

#if defined(NVDLA_HAVE_CONFIG_HSIERRINJ) && (NVDLA_HAVE_CONFIG_HSIERRINJ == 1)
static ssize_t debug_dla_err_inj_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	int err;
	struct seq_file *priv_data;
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;
	struct epl_error_report_frame frame;
	unsigned int instance_id;
	struct nvhost_device_data *pdata;
	long write_value;

	/* Fetch user requested write-value. */
	err = kstrtol_from_user(buffer, count, 10, &write_value);
	if (err < 0)
		goto fail;

	if (write_value > 0) {
		/* Trigger error injection */
		priv_data = file->private_data;
		if (priv_data == NULL)
			goto fail;

		nvdla_dev = (struct nvdla_device *) priv_data->private;
		if (nvdla_dev == NULL)
			goto fail;

		pdev = nvdla_dev->pdev;
		if (pdev == NULL)
			goto fail;

		pdata = platform_get_drvdata(pdev);
		if (pdata == NULL)
			goto fail;

		if (pdata->class == NV_DLA0_CLASS_ID) {
			instance_id = 0U;
			frame.error_attribute = 0U;
			frame.timestamp = 0U;
			if (write_value == 1) {
				frame.reporter_id = NVDLA0_UE_HSM_REPORTER_ID;
				frame.error_code = NVDLA0_UE_HSM_ERROR_CODE;
			} else if (write_value == 2) {
				frame.reporter_id = NVDLA0_CE_HSM_REPORTER_ID;
				frame.error_code = NVDLA0_CE_HSM_ERROR_CODE;
			} else {
				nvdla_dbg_err(pdev, "Write value out of range: [0,2]");
				goto fail;
			}
		} else {
			instance_id = 1U;
			frame.error_attribute = 0U;
			frame.timestamp = 0U;
			if (write_value == 1) {
				frame.reporter_id = NVDLA1_UE_HSM_REPORTER_ID;
				frame.error_code = NVDLA1_UE_HSM_ERROR_CODE;
			} else if (write_value == 2) {
				frame.reporter_id = NVDLA1_CE_HSM_REPORTER_ID;
				frame.error_code = NVDLA1_CE_HSM_ERROR_CODE;
			} else {
				nvdla_dbg_err(pdev, "Write value out of range: [0,2]");
				goto fail;
			}
		}

		err = nvdla_error_inj_handler(instance_id, frame,
				(void *) nvdla_dev);
		if (err < 0)
			goto fail;
	}

	return count;

fail:
	return -1;
}

static int debug_dla_err_inj_show(struct seq_file *s, void *data)
{
	seq_puts(s, "0\n");
	return 0;
}

static int debug_dla_err_inj_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_dla_err_inj_show, inode->i_private);
}

static const struct file_operations debug_dla_err_inj_fops = {
	.open		= debug_dla_err_inj_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= debug_dla_err_inj_write,
};

static void nvdla_err_inj_debugfs_init(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	struct dentry *dla_debugfs_root = pdata->debugfs;

	if (!debugfs_create_file("hsi_error_inject", 0600, dla_debugfs_root,
			nvdla_dev, &debug_dla_err_inj_fops)) {
		goto fail_create_file_err_inj;
	}

fail_create_file_err_inj:
	return;
}
#endif /* NVDLA_HAVE_CONFIG_HSIERRINJ */

#define NVDLA_DEBUG_CMD_SUBMIT_MODE_MAX_LEN 256U
static ssize_t debug_dla_submit_mode_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *off)
{
	struct seq_file *priv_data;
	struct nvdla_device *nvdla_dev;
	struct platform_device *pdev;
	char mode[NVDLA_DEBUG_CMD_SUBMIT_MODE_MAX_LEN];
	int32_t mode_length;

	/* Fetch user requested write-value. */
	mode_length = simple_write_to_buffer(mode,
					(NVDLA_DEBUG_CMD_SUBMIT_MODE_MAX_LEN - 1U),
					off,
					buffer,
					count);
	mode[mode_length] = '\0';

	if ((strcmp(mode, "mthd") != 0) && (strcmp(mode, "mnoc") != 0))
		goto fail;

	priv_data = file->private_data;
	if (priv_data == NULL)
		goto fail;

	nvdla_dev = (struct nvdla_device *) priv_data->private;
	if (nvdla_dev == NULL)
		goto fail;

	pdev = nvdla_dev->pdev;
	if (pdev == NULL)
		goto fail;

	if (strcmp(mode, "mthd") == 0)
		nvdla_dev->submit_mode = (uint32_t) NVDLA_SUBMIT_MODE_MTHD;
	else
		nvdla_dev->submit_mode = (uint32_t) NVDLA_SUBMIT_MODE_MNOC;

	return count;

fail:
	return -1;
}

static int debug_dla_submit_mode_show(struct seq_file *s, void *data)
{
	int err;
	struct nvdla_device *nvdla_dev;
	uint32_t submit_mode;

	if (s == NULL) {
		err = -EFAULT;
		goto fail;
	}

	nvdla_dev = (struct nvdla_device *) s->private;
	if (nvdla_dev == NULL) {
		err = -EFAULT;
		goto fail;
	}

	submit_mode = nvdla_dev->submit_mode;

	if ((submit_mode != (uint32_t) NVDLA_SUBMIT_MODE_MTHD) &&
			(submit_mode != (uint32_t) NVDLA_SUBMIT_MODE_MNOC)) {
		err = -EINVAL;
		goto fail;
	}

	if (submit_mode == (uint32_t) NVDLA_SUBMIT_MODE_MTHD)
		seq_puts(s, "MTHD\n");
	else
		seq_puts(s, "MNOC\n");

	return 0;

fail:
	return err;
}

static int debug_dla_submit_mode_open(struct inode *inode,
	struct file *file)
{
	return single_open(file, debug_dla_submit_mode_show,
				inode->i_private);
}

static const struct file_operations debug_dla_submit_mode_fops = {
	.open		= debug_dla_submit_mode_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= debug_dla_submit_mode_write,
};


static int debug_dla_submit_stat_show(struct seq_file *s, void *data)
{
	int err;
	struct nvdla_device *nvdla_dev;
	struct nvdla_submit_stat *submit_stat;

	if (s == NULL) {
		err = -EFAULT;
		goto fail;
	}

	nvdla_dev = (struct nvdla_device *) s->private;
	if (nvdla_dev == NULL) {
		err = -EFAULT;
		goto fail;
	}

	// MTHID-MTHDDATA submit stats
	submit_stat = &nvdla_dev->submit_stat[NVDLA_SUBMIT_MODE_MTHD];
	seq_printf(s, "(MTHD) Number of successful submits  : %llu\n",
		submit_stat->num_successes);
	seq_printf(s, "(MTHD) Number of timedout submits    : %llu\n",
		submit_stat->num_timeouts);
	seq_printf(s, "(MTHD) Number of failed submits      : %llu\n",
		submit_stat->num_fails);
	seq_printf(s, "(MTHD) Last submit status            : %d\n",
		submit_stat->last_error);
	seq_printf(s, "(MTHD) Timestamp (in ns)             : %llu\n",
		submit_stat->timestamp_ns);

	if (nvdla_is_ctx_device(nvdla_dev->pdev)) {
		// MNOC submit stats
		submit_stat = &nvdla_dev->submit_stat[NVDLA_SUBMIT_MODE_MNOC];
		seq_printf(s, "(MNOC) Number of successful submits  : %llu\n",
				submit_stat->num_successes);
		seq_printf(s, "(MNOC) Number of timedout submits    : %llu\n",
				submit_stat->num_timeouts);
		seq_printf(s, "(MNOC) Number of failed submits      : %llu\n",
				submit_stat->num_fails);
		seq_printf(s, "(MNOC) Last submit status            : %d\n",
				submit_stat->last_error);
		seq_printf(s, "(MNOC) Timestamp (in ns)             : %llu\n",
				submit_stat->timestamp_ns);
	}

	return 0;

fail:
	return err;
}

static int debug_dla_submit_stat_open(struct inode *inode,
	struct file *file)
{
	return single_open(file, debug_dla_submit_stat_show,
				inode->i_private);
}

static const struct file_operations debug_dla_submit_stat_fops = {
	.open		= debug_dla_submit_stat_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void nvdla_debug_init(struct platform_device *pdev)
{
	struct nvhost_device_data *pdata = platform_get_drvdata(pdev);
	struct nvdla_device *nvdla_dev = pdata->private_data;
	struct nvdla_debug_ctrl *ctrl;
	struct dentry *de = pdata->debugfs;

	if (!de)
		return;

	ctrl = devm_kzalloc(&pdev->dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return;

	ctrl->pdev = pdev;
	mutex_lock(&s_debug_ctrl_list_lock);
	list_add_tail(&ctrl->list, &s_debug_ctrl_list);
	mutex_unlock(&s_debug_ctrl_list_lock);

	debugfs_create_u32("debug_mask", 0644, de,
			&nvdla_dev->dbg_mask);
	debugfs_create_u32("bitbang", 0644, de, &nvdla_dev->bitbang);
#ifdef CONFIG_TEGRA_NVDLA_TRACE_PRINTK
	debugfs_create_u32("en_trace", 0644, de,
			&nvdla_dev->en_trace);
#endif
	(void) debugfs_create_file("cmdsubmitmode", 0600, de, nvdla_dev,
			&debug_dla_submit_mode_fops);

#if defined(NVDLA_HAVE_CONFIG_HSIERRINJ) && (NVDLA_HAVE_CONFIG_HSIERRINJ == 1)
	nvdla_err_inj_debugfs_init(pdev);
#endif /* NVDLA_HAVE_CONFIG_HSIERRINJ */

#ifdef CONFIG_PM
	nvdla_pm_debugfs_init(pdev);
#endif

	dla_fw_debugfs_init(pdev);
	dla_cmd_response_debugfs_init(pdev);
	dla_ctrl_debugfs_init(pdev);

	(void) debugfs_create_file("cmdsubmitstat", 0400, de, nvdla_dev,
			&debug_dla_submit_stat_fops);
}
