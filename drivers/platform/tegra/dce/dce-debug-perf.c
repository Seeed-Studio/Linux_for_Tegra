// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/errno.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <dce.h>
#include <dce-debug-perf.h>
#include <dce-log.h>
#include <dce-util-common.h>
#include <interface/dce-interface.h>

#define DCE_PERF_OUTPUT_FORMAT_CSV	((uint32_t)(0U))
#define DCE_PERF_OUTPUT_FORMAT_XML	((uint32_t)(1U))

static uint32_t perf_output_format = DCE_PERF_OUTPUT_FORMAT_CSV;

ssize_t dbg_dce_perf_stats_stats_fops_write(struct file *file,
					    const char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	int ret = 0;
	bool start_perf;
	struct dce_ipc_message *msg = NULL;
	struct dce_admin_ipc_cmd *req_msg;
	struct tegra_dce *d = ((struct seq_file *)file->private_data)->private;

	ret = kstrtobool_from_user(user_buf, 3ULL, &start_perf);
	if (ret) {
		dce_err(d, "Unable to parse start/stop for dce perf stats");
		goto out;
	}

	msg = dce_admin_allocate_message(d);
	if (!msg) {
		dce_err(d, "IPC msg allocation failed");
		goto out;
	}

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);

	/*
	 * Collect All Perfs
	 * Clear previous data and start collecting new perf data
	 *
	 * TODO: Add support to collect specific perf data
	 */
	req_msg->args.perf.perf_cmd.enable = DCE_ADMIN_PERF_STATS_ALL;
	req_msg->args.perf.perf_cmd.clear  = DCE_ADMIN_PERF_CLEAR_CLEAR;

	/*
	 * echo "1/y" (kstrtobool true) to start perf data collection
	 * echo "0/n" (kstrtobool false) to stop perf data collection
	 */
	ret = dce_admin_send_cmd_set_perf_stat(d, msg, start_perf);
	if (ret) {
		dce_err(d, "Failed to Set perf stat\n");
		goto out;
	}

	dce_debug(d, "DCE perf stats collection %s", start_perf ? "started" : "stopped");

out:
	dce_admin_free_message(d, msg);
	return count;
}

static void dbg_dce_perf_stats_sched_task_xml(struct seq_file *s, struct tegra_dce *d,
					      const struct dce_admin_task_stats *task_stat)
{
	seq_printf(s, "<task><name>%s</name>", task_stat->name);
	seq_printf(s, "<init>%llu</init><ready>%llu</ready>",
			task_stat->init, task_stat->ready);
	seq_printf(s, "<ready_time>%llu</ready_time>",
			task_stat->ready_time);
	seq_printf(s, "<dcache_misses>%llu</dcache_misses><instr_exec_count>%llu</instr_exec_count>",
			task_stat->dcache_misses, task_stat->inst_exec);
	seq_printf(s, "<mmio_req_count>%llu</mmio_req_count>",
			task_stat->mmio_req);
	seq_printf(s, "<stat><name>sleep</name><accumulate>%llu</accumulate>",
			task_stat->sleep.accumulate);
	seq_printf(s, "<iterations>%llu</iterations><min>%llu</min><max>%llu</max></stat>",
			task_stat->sleep.iterations,
			task_stat->sleep.min,
			task_stat->sleep.max);
	seq_printf(s, "<stat><name>run</name><accumulate>%llu</accumulate>",
			task_stat->run.accumulate);
	seq_printf(s, "<iterations>%llu</iterations><min>%llu</min><max>%llu</max></stat>",
			task_stat->run.iterations,
			task_stat->run.min,
			task_stat->run.max);
	seq_printf(s, "<stat><name>run-context</name><accumulate>%llu</accumulate>",
			task_stat->run_context.accumulate);
	seq_printf(s, "<iterations>%llu</iterations><min>%llu</min><max>%llu</max></stat>",
			task_stat->run_context.iterations,
			task_stat->run_context.min,
			task_stat->run_context.max);
	seq_puts(s, "</task>\n");
}

static void dbg_dce_perf_stats_pm_event_xml(struct seq_file *s,
					    const struct dce_admin_pm_event_stats *pm_evt)
{
	seq_printf(s, "<start>%llu</start><end>%llu</end>\n", pm_evt->start, pm_evt->end);
	seq_printf(s, "<count>%llu</count>\n", pm_evt->count);
}

static void dbg_dce_perf_stats_show_xml(struct seq_file *s, struct tegra_dce *d,
					struct dce_admin_perf_info *perf)
{

	const struct dce_admin_pm_event_stats *pm_evt;
	const struct dce_admin_sched_stats *sched = &perf->sched;
	uint32_t	i;

	seq_puts(s, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
	seq_puts(s, "<PerfData>");
	seq_printf(s, "<tcm_ready>%llu</tcm_ready>\n", perf->tcm_ready);
	seq_printf(s, "<sched_start>%llu</sched_start>\n", perf->sched_start);
	seq_printf(s, "<signaled_ready>%llu</signaled_ready>\n", perf->ready_time);

	/*
	 * dCache-misses Perf Data
	 */
	pm_evt = &perf->pm_events[DCE_ADMIN_PM_EVT_DCACHE_MISSES];
	seq_puts(s, "<dcache-misses>\n");
	dbg_dce_perf_stats_pm_event_xml(s, pm_evt);
	seq_puts(s, "</dcache-misses>\n");

	/*
	 * instruction execution Perf Data
	 */
	pm_evt = &perf->pm_events[DCE_ADMIN_PM_EVT_INSTR_EXEC];
	seq_puts(s, "<instr-execution>\n");
	dbg_dce_perf_stats_pm_event_xml(s, pm_evt);
	seq_puts(s, "</instr-execution>\n");

	/*
	 * instruction execution Perf Data
	 */
	pm_evt = &perf->pm_events[DCE_ADMIN_PM_EVT_MEM_REQ];
	seq_puts(s, "<mmio-request>\n");
	dbg_dce_perf_stats_pm_event_xml(s, pm_evt);
	seq_puts(s, "</mmio-request>\n");

	/*
	 * Print Scheduler Perf data and Per task data
	 */
	seq_puts(s, "<sched>\n");
	seq_printf(s, "<start>%llu</start><end>%llu</end>", sched->start, sched->end);
	seq_printf(s, "<context_switches>%llu</context_switches>\n", sched->context_switches);

	for (i = 0U; i < DCE_ADMIN_PERF_ACTIVE_TASKS_NUM; i += 1U) {
		dbg_dce_perf_stats_sched_task_xml(s, d,
				&sched->tasks[i]);
	}

	seq_puts(s, "</sched>");		//sched perf data done
	seq_puts(s, "</PerfData>\n");
}


static void dbg_dce_perf_stats_sched_task_csv(struct seq_file *s, struct tegra_dce *d,
					      const struct dce_admin_task_stats *task_stat)
{
	seq_printf(s, "\"%s\",\"%llu\",\"%llu\",\"%llu\",\"%llu\",\"%llu\",\"%llu\",\"sleep\",\"%llu\",\"%llu\",\"%llu\",\"%llu\"\n",
			task_stat->name, task_stat->init, task_stat->ready,
			task_stat->ready_time, task_stat->dcache_misses,
			task_stat->inst_exec, task_stat->mmio_req,
			task_stat->sleep.accumulate,
			task_stat->sleep.iterations, task_stat->sleep.min,
			task_stat->sleep.max);
	seq_puts(s, "\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"run\",");
	seq_printf(s, "\"%llu\",\"%llu\",\"%llu\",\"%llu\"\n",
			task_stat->run.accumulate, task_stat->run.iterations,
			task_stat->run.min, task_stat->run.max);
	seq_puts(s, "\"\",\"\",\"\",\"\",\"\",\"\",\"\",\"run-context\",");
	seq_printf(s, "\"%llu\",\"%llu\",\"%llu\",\"%llu\"\n",
			task_stat->run_context.accumulate,
			task_stat->run_context.iterations,
			task_stat->run_context.min,
			task_stat->run_context.max);
}

static void dbg_dce_perf_stats_show_csv(struct seq_file *s, struct tegra_dce *d,
					struct dce_admin_perf_info *perf)
{
	const struct dce_admin_pm_event_stats *pm_evt;
	const struct dce_admin_sched_stats *sched = &perf->sched;
	uint32_t	i;

	seq_puts(s, "\"tcm-ready\",\"sched-start\",\"signaled-ready\",");
	seq_puts(s, "\"dcache:start-time\",\"dcache:end-time\",\"dcache:miss_cnt\",");
	seq_puts(s, "\"inst_exec:start-time\",\"inst_exec:end-time\",\"inst_exec:count\",");
	seq_puts(s, "\"mmio_req:start-time\",\"mmio_req:end-time\",\"mmio_req:count\",");
	seq_puts(s, "\"sched:start-time\",\"sched:end-time\",\"sched:context_switches\"\n");
	seq_printf(s, "\"%llu\",\"%llu\",\"%llu\",", perf->tcm_ready,
			 perf->sched_start, perf->ready_time);

	/*
	 * Print Cache Perf data
	 */
	pm_evt = &perf->pm_events[DCE_ADMIN_PM_EVT_DCACHE_MISSES];
	seq_printf(s, "\"%llu\",\"%llu\",\"%llu\",",
			pm_evt->start, pm_evt->end, pm_evt->count);

	/*
	 * Print Instruction exec Perf data
	 */
	pm_evt = &perf->pm_events[DCE_ADMIN_PM_EVT_INSTR_EXEC];
	seq_printf(s, "\"%llu\",\"%llu\",\"%llu\",",
			pm_evt->start, pm_evt->end, pm_evt->count);

	/*
	 * Print Instruction exec Perf data
	 */
	pm_evt = &perf->pm_events[DCE_ADMIN_PM_EVT_MEM_REQ];
	seq_printf(s, "\"%llu\",\"%llu\",\"%llu\",",
			pm_evt->start, pm_evt->end, pm_evt->count);
	/*
	 * Print Scheduler Perf data and Per task data
	 */
	seq_printf(s, "\"%llu\",\"%llu\",\"%llu\"\n",
			sched->start, sched->end,
			sched->context_switches);

	seq_puts(s, "\"sched:task_name\",\"sched:task-init\",\"sched:task-ready\",");
	seq_puts(s, "\"sched:task-ready-time\",\"sched:task-stat:dcache-misses\",");
	seq_puts(s, "\"sched:task-stat:instr_exec\",\"sched:task-stat:mem_req\",");
	seq_puts(s, "\"sched:task-stat:name\",");
	seq_puts(s, "\"sched:task-stat:accumulate\",\"sched:task-stat:iterations\",");
	seq_puts(s, "\"sched:task-stat:min\",\"sched:task-stat:max\"\n");
	for (i = 0U; i < DCE_ADMIN_PERF_ACTIVE_TASKS_NUM; i += 1U) {
		dbg_dce_perf_stats_sched_task_csv(s, d,
				&sched->tasks[i]);
	}
}

static int dbg_dce_perf_stats_stats_fops_show(struct seq_file *s, void *data)
{
	int ret = 0;
	struct dce_ipc_message *msg = NULL;
	struct dce_admin_ipc_resp *resp_msg;
	struct dce_admin_perf_info *perf;
	struct tegra_dce *d = (struct tegra_dce *)s->private;

	msg = dce_admin_allocate_message(d);
	if (!msg) {
		dce_err(d, "IPC msg allocation failed");
		goto out;
	}

	ret = dce_admin_send_cmd_get_perf_stat(d, msg);
	if (ret) {
		dce_err(d, "Failed to Get perf stat\n");
		goto out;
	}

	resp_msg = (struct dce_admin_ipc_resp *) (msg->rx.data);
	perf = &resp_msg->args.perf.info.sched_stats;

	if (perf_output_format == DCE_PERF_OUTPUT_FORMAT_CSV)
		dbg_dce_perf_stats_show_csv(s, d, perf);
	else
		dbg_dce_perf_stats_show_xml(s, d, perf);

out:
	dce_admin_free_message(d, msg);
	return 0;
}

int dbg_dce_perf_stats_stats_fops_open(struct inode *inode,
				       struct file *file)
{
	return single_open(file, dbg_dce_perf_stats_stats_fops_show,
			   inode->i_private);
}

/*
 * Debugfs nodes for displaying a help message about perf stats capture
 */
static int dbg_dce_perf_stats_help_fops_show(struct seq_file *s, void *data)
{
/**
 * Writing
 * '0' to /sys/kernel/debug/tegra_dce/perf/stats/stats
 *    - Clear the Perf stats data
 * '1' to /sys/kernel/debug/tegra_dce/perf/stats/stats
 *    - Start the Perf stats data
 * To capture fresh Perf stats, first clear and Start.
 *
 * The result can be printed in csv or xml format
 *
 * '0' to /sys/kernel/debug/tegra_dce/perf/format
 *   - Data is collected in CSV format
 * '1' to /sys/kernel/debug/tegra_dce/perf/format
 *   - Data is collected in XML format
 *  Default is set for CSV
 *
 *  cat /sys/kernel/debug/tegra_dce/perf/stats/stats
 *    - get the perf data in selected format
 *
 */
	seq_printf(s, "DCE Perf Capture\n"
		      "----------------------\n"
		      "  echo '0' > perf/stats/stats : Clear perf data\n"
		      "  cat perf/stats/stats : get perf data in selected format\n"
		      "  echo <0/1> > perf/format : get data in CSV/XML format\n");
	return 0;
}

int dbg_dce_perf_stats_help_fops_open(struct inode *inode,
				      struct file *file)
{
	return single_open(file, dbg_dce_perf_stats_help_fops_show,
			   inode->i_private);
}

ssize_t dbg_dce_perf_format_fops_write(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	int ret = 0;
	u32 format;
	struct tegra_dce *d = ((struct seq_file *)file->private_data)->private;

	ret = kstrtou32_from_user(user_buf, count, 10, &format);
	if (ret) {
		dce_err(d, "Invalid format!");
		goto done;
	}

	if (format == DCE_PERF_OUTPUT_FORMAT_CSV)
		perf_output_format = DCE_PERF_OUTPUT_FORMAT_CSV;
	else if (format == DCE_PERF_OUTPUT_FORMAT_XML)
		perf_output_format = DCE_PERF_OUTPUT_FORMAT_XML;
	else
		dce_err(d, "Invalid format [%u]!", format);

done:
	return count;
}

static int dbg_dce_perf_format_fops_show(struct seq_file *s, void *data)
{
	seq_printf(s, "Supported Formats\n"
		      "----------------------\n"
		      "CSV:0 XML:1\n"
		      "Current format:%s\n",
		      (perf_output_format == DCE_PERF_OUTPUT_FORMAT_CSV) ? "CSV" : "XML");
	return 0;
}

int dbg_dce_perf_format_fops_open(struct inode *inode, struct file *file)
{
	return single_open(file, dbg_dce_perf_format_fops_show,
			   inode->i_private);
}

/*
 * Debugfs nodes for displaying a help message about perf stats capture
 */
static int dbg_dce_perf_events_help_fops_show(struct seq_file *s, void *data)
{
/**
 * Writing
 * '0' to /sys/kernel/debug/tegra_dce/perf/events/events
 *    - Clear the Perf Events data
 * 'Bit masked value' to /kernel/debug/tegra_dce/perf/events/events
 *    - Enable an event for the Perf Events data capture.
 *  Clear bit mask if the event perf is not needed to be captured.
 *
 * The result can be printed in csv or xml format
 *
 * '0' to /sys/kernel/debug/tegra_dce/perf/format
 *   - Data is collected in CSV format
 * '1' to /sys/kernel/debug/tegra_dce/perf/format
 *   - Data is collected in XML format
 *  Default is set for CSV
 *
 *  cat /sys/kernel/debug/tegra_dce/perf/events/events
 *    - get the perf event data in selected format
 *    - shows event name/ accumulated time/ iterations happened
 *    - min and max time taken to run an event iteration
 */
	seq_printf(s, "DCE Perf Events Capture\n"
		      "----------------------\n"
		      " echo '0' > perf/events/events: Clear perf events data\n"
		      " echo 'bit masked value' > perf/events/events: Enable perf event capture\n"
		      " cat perf/events/events: get perf events data in selected format\n"
		      " echo <0/1> > perf/format: get data in CSV/XML format\n");
	return 0;
}

int dbg_dce_perf_events_help_fops_open(struct inode *inode,
				       struct file *file)
{
	return single_open(file, dbg_dce_perf_events_help_fops_show,
			   inode->i_private);
}

ssize_t dbg_dce_perf_events_events_fops_write(struct file *file,
					      const char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	int ret = 0;
	uint32_t events;
	struct dce_ipc_message *msg = NULL;
	struct dce_admin_ipc_cmd *req_msg;
	struct tegra_dce *d = ((struct seq_file *)file->private_data)->private;

	/*
	 * Writing "0/n" will clear the events
	 * Write Bit Masked value to enable events
	 */
	ret = kstrtou32_from_user(user_buf, count, 0, &events);
	if (ret) {
		dce_err(d, "Unable to parse for event perf stats");
		goto out;
	}

	msg = dce_admin_allocate_message(d);
	if (!msg) {
		dce_err(d, "IPC msg allocation failed");
		goto out;
	}

	req_msg = (struct dce_admin_ipc_cmd *)(msg->tx.data);

	if (events == 0x0U) {
		// Clear event
		req_msg->args.perf.event_cmd.clear = DCE_ADMIN_EVENT_CLEAR_CLEAR;
	} else {
		// Disable all Events if enable : 0x80000000
		// Elase enable only Bit masked events
		req_msg->args.perf.event_cmd.clear = DCE_ADMIN_EVENT_CLEAR_RETAIN;
		req_msg->args.perf.event_cmd.enable = events;
	}

	ret = dce_admin_send_cmd_clear_perf_events(d, msg);
	if (ret) {
		dce_err(d, "Failed to Clear perf events\n");
		goto out;
	}
out:
	dce_admin_free_message(d, msg);
	return count;
}

static void dbg_dce_perf_events_show_csv(struct seq_file *s, struct tegra_dce *d,
					 const struct dce_admin_event_info *events)
{
	int i = 0;

	seq_puts(s, "\"Evt Name\",\"accumulate\",\"iterations\",\"min\",\"max\"\n");

	for (i = 0; i < DCE_ADMIN_NUM_EVENTS; i++) {
		seq_printf(s, "\"%s\",\"%llu\",\"%llu\",\"%llu\",\"%llu\"\n",
			   events->events[i].name, events->events[i].event.accumulate,
			   events->events[i].event.iterations, events->events[i].event.min,
			   events->events[i].event.max);
	}

}

static void dbg_dce_perf_events_show_xml(struct seq_file *s, struct tegra_dce *d,
					 const struct dce_admin_event_info *events)
{
	int i = 0;

	seq_puts(s, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
	seq_puts(s, "<EventsPerfData>");

	for (i = 0; i < DCE_ADMIN_NUM_EVENTS; i++) {
		seq_printf(s, "<name>%s</name>\n", events->events[i].name);
		seq_printf(s, "<accumulate>%llu</accumulate>\n",
				events->events[i].event.accumulate);
		seq_printf(s, "<iterations>%llu</iterations>\n",
				events->events[i].event.iterations);
		seq_printf(s, "<min>%llu</min>\n", events->events[i].event.min);
		seq_printf(s, "<max>%llu</max>\n", events->events[i].event.max);
	}

	seq_puts(s, "</EventsPerfData>\n");
}

static int dbg_dce_perf_events_events_fops_show(struct seq_file *s, void *data)
{
	int ret = 0;
	struct dce_ipc_message *msg = NULL;
	struct dce_admin_ipc_resp *resp_msg;
	struct dce_admin_event_info *events;
	struct tegra_dce *d = (struct tegra_dce *)s->private;

	msg = dce_admin_allocate_message(d);
	if (!msg) {
		dce_err(d, "IPC msg allocation failed");
		goto out;
	}

	ret = dce_admin_send_cmd_get_perf_events(d, msg);
	if (ret) {
		dce_err(d, "Failed to Get perf stat\n");
		goto out;
	}

	resp_msg = (struct dce_admin_ipc_resp *) (msg->rx.data);
	events = &resp_msg->args.perf.info.events_stats;

	if (perf_output_format == DCE_PERF_OUTPUT_FORMAT_CSV)
		dbg_dce_perf_events_show_csv(s, d, events);
	else
		dbg_dce_perf_events_show_xml(s, d, events);

out:
	dce_admin_free_message(d, msg);
	return 0;
}

int dbg_dce_perf_events_events_fops_open(struct inode *inode,
					 struct file *file)
{
	return single_open(file, dbg_dce_perf_events_events_fops_show,
			   inode->i_private);
}
