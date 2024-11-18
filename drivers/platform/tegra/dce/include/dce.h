/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef TEGRA_DCE_H
#define TEGRA_DCE_H

#include <dce-os-types.h>
#include <dce-os-log.h>
#include <dce-ipc.h>
#include <dce-hsp.h>
#include <dce-os-lock.h>
#include <dce-os-cond.h>
#include <dce-regs.h>
#include <dce-os-worker.h>
#include <dce-fsm.h>
#include <dce-pm.h>
#include <dce-mailbox.h>
#include <dce-client-ipc-internal.h>
#include <dce-os-work.h>

#define DCE_WARN_ON_NOT_NULL(x) \
	do { \
		if (x != NULL) { \
			dce_os_warn(d, "Unexpected non-NULL value for " #x "\n"); \
		} \
	} while (0)

#define DCE_MAX_CPU_IRQS 4

/**
 * DCE Boot Status : DCE Driver Init States
 *
 * @DCE_EARLY_INIT_* : Driver Init Before Bootstrap
 * @DCE_AST_CONFIG_* : Used When DCE-CPU Driver Loads the Firmware
 */
#define DCE_EARLY_INIT_START		DCE_BIT(31)
#define DCE_EARLY_INIT_FAILED		DCE_BIT(30)
#define DCE_EARLY_INIT_DONE		DCE_BIT(29)
#define DCE_AST_CONFIG_START		DCE_BIT(28)
#define DCE_AST_CONFIG_FAILED		DCE_BIT(27)
#define DCE_AST_CONFIG_DONE		DCE_BIT(26)
/**
 * DCE Boot Status: FW Boot States
 */
#define DCE_FW_EARLY_BOOT_START         DCE_BIT(16)
#define DCE_FW_EARLY_BOOT_FAILED	DCE_BIT(15)
#define DCE_FW_EARLY_BOOT_DONE		DCE_BIT(14)
#define DCE_FW_BOOTSTRAP_START		DCE_BIT(13)
#define DCE_FW_BOOTSTRAP_FAILED		DCE_BIT(12)
#define DCE_FW_BOOTSTRAP_DONE		DCE_BIT(11)
#define DCE_FW_ADMIN_SEQ_START		DCE_BIT(10)
#define DCE_FW_ADMIN_SEQ_FAILED		DCE_BIT(9)
#define DCE_FW_ADMIN_SEQ_DONE		DCE_BIT(8)
#define DCE_FW_SUSPENDED		DCE_BIT(2)
#define DCE_FW_BOOT_DONE		DCE_BIT(1)
#define DCE_STATUS_FAILED		DCE_BIT(0)
#define DCE_STATUS_UNKNOWN		((u32)(0))

struct tegra_dce;

/**
 * struct dce_platform_data - Data Structure to hold platform specific DCE
 *				cluster data.
 */
struct dce_platform_data {
	/**
	 * @fw_dce_addr : Stores the firmware address that DCE sees before being
	 * converted by AST.
	 */
	u32 fw_dce_addr;
	/**
	 * fw_image_size : Stores the max size of DCE fw.
	 */
	u32 fw_img_size;
	/**
	 * @fw_info_valid : Tells if the above address and size info are valid.
	 * CPU driver will use this info just for debug purpose.
	 */
	bool fw_info_valid;
	/**
	 * @no_of_asts : Stores max no. of ASTs in the DCE Cluster
	 */
	u8 no_of_asts;
	/**
	 * phys_stream_id : Physical stream ID to be programmed for debug
	 * purpose only.
	 */
	u32 phys_stream_id;
	/**
	 * stream_id : Stream ID to program the ASTs in debug mode
	 * only.
	 */
	u8 stream_id;
	/**
	 * hsp_id - HSP instance id used for dce communication
	 */
	u32 hsp_id;
	/**
	 * fw_vmindex : VMIndex to program the AST region to read FW in debug
	 * mode only.
	 */
	u8 fw_vmindex;
	/**
	 * fw_carveout_id : Carveout ID to program the AST region to read FW in
	 * debug mode only.
	 */
	u8 fw_carveout_id;
	/**
	 * @fw_name : Stores dce fw name
	 */
	const char *fw_name;
	/**
	 * @use_physical_id : Use physical streamid
	 */
	bool use_physical_id;
};

/**
 * struct dce_firmware - Contains dce firmware info
 *
 * @data : u8 pointer to hold the fw data
 * @size : size of the fw
 * @dma_handle : stores the dma_handle for firmware
 */
struct dce_firmware {
	u8 *data;
	size_t size;
	u64 dma_handle;
};

/**
 * struct dce_admin - Contains dce admin info
 *
 * @admin_msg_buffer: struct dce_ipc_message pointer to hold admin message buffer.
 */
struct dce_admin {
	struct dce_ipc_message *msg_buffer;
};

/**
 * struct tegra_dce - Primary OS independent tegra dce structure to hold dce
 * cluster's and it's element's runtime info.
 */
struct tegra_dce {
	/**
	 * @irq - Array of irqs to be handled by cpu from dce cluster.
	 */
	u32 irq[DCE_MAX_CPU_IRQS];
	/**
	 * @fsm_info - Data Structure to manage dce FSM states.
	 */
	struct dce_fsm_info fsm_info;
	/**
	 * dce_bootstrap_work : dce work to be executed to start FSM flow
	 */
	struct dce_os_work_struct dce_bootstrap_work;
	/**
	 * dce_resume_work : dce work to executed dce resume flow
	 */
	struct dce_os_work_struct dce_resume_work;
	/**
	 * dce_sc7_state : structure to save/restore state during sc7 enter/exit
	 */
	struct dce_sc7_state sc7_state;
	/**
	 * dce_wait_info - Data structure to manage wait for different event types
	 */
	struct dce_wait_cond ipc_waits[DCE_MAX_WAIT];
	/**
	 * dce_bootstrap_done - Data structure to manage wait for boot done
	 */
	struct dce_os_cond dce_bootstrap_done;
	/**
	 * @d_mb - Stores the current status of dce mailbox interfaces.
	 */
	struct dce_mailbox_interface d_mb[DCE_MAILBOX_MAX_INTERFACES];
	/**
	 * @d_ipc - Stores the ipc related data between CPU and DCE.
	 */
	struct dce_ipc d_ipc;
	/**
	 * @d_clients - Stores all dce clients data.
	 */
	struct tegra_dce_client_ipc *d_clients[DCE_CLIENT_IPC_TYPE_MAX];

	/**
	 * @d_async_ipc_info - stores data to handle async events
	 */
	struct tegra_dce_async_ipc_info d_async_ipc;
	/**
	 * @hsp_id - HSP instance id used for dce communication
	 */
	u32 hsp_id;
	/**
	 * @hsp - Struct to store function pointers for HSP access.
	 */
	struct dce_hsp_fn hsp;
	/**
	 * @boot_status - u32 variable to store dce's boot status.
	 */
	u32 boot_status;
	/**
	 * @boot_complete - Boolean variable to store dce's boot status.
	 */
	bool boot_complete;
	/**
	 * @ast_config_complete - Boolean variable to store dce's ast
	 * configuration status.
	 */
	bool ast_config_complete;
	/**
	 * @reset_complete - Boolean variable to store dce's reset status.
	 */
	bool reset_complete;
	/**
	 * @load_complete - Boolean variable to store dce's fw load status.
	 */
	bool load_complete;
	/**
	 * @log_level - Stores the log level for dce cpu prints.
	 */
	u32 log_level;
	/**
	 * @fw_data - Stores info regardign firmware to be used runtime.
	 */
	struct dce_firmware *fw_data;

	struct dce_admin admin_data;
};

/**
 * dce_set_admin_msg_buffer - Set dce admin msg buffer pointer.
 *
 * @d : Pointer to tegra_dce struct.
 * @admin_msg_buffer : Pointer to admin message buffer.
 *
 * Return : void
 */
static inline void dce_set_admin_msg_buffer(struct tegra_dce *d,
	struct dce_ipc_message *admin_msg_buffer)
{
	d->admin_data.msg_buffer = admin_msg_buffer;
}

/**
 * dce_get_admin_msg_buffer - Get dce admin msg buffer pointer.
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : Pointer to admin message buffer.
 */
static inline struct dce_ipc_message *dce_get_admin_msg_buffer(struct tegra_dce *d)
{
	return d->admin_data.msg_buffer;
}

/**
 * dce_set_boot_complete - updates the current dce boot complete status.
 *
 * @d : Pointer to tegra_dce struct.
 * @val : true or false.
 *
 * Return : void
 */
static inline void dce_set_boot_complete(struct tegra_dce *d, bool val)
{
	d->boot_complete = val;
	if (!val)
		d->boot_status &= (~DCE_FW_BOOT_DONE);
}

/**
 * dce_is_bootcmds_done - Checks if dce bootstrap bootcmds done.
 *
 * Chekc if all the mailbox boot commands are completed
 *
 * @d - Pointer to tegra_dce struct.
 *
 * Return : True if bootcmds are completed
 */
static inline bool dce_is_bootcmds_done(struct tegra_dce *d)
{
	return (d->boot_status & DCE_FW_BOOTSTRAP_DONE) ? true : false;
}

/**
 * dce_is_bootstrap_done - check if dce bootstrap is done.
 *
 * @d : Pointer to tegra_dce struct.
 *
 * Return : true if bootstrap done else false
 */
static inline bool dce_is_bootstrap_done(struct tegra_dce *d)
{
	return (d->boot_status & DCE_FW_BOOT_DONE) ? true : false;
}

/**
 * dce_set_ast_config_status - updates the current status of ast configuration.
 *
 * @d : Pointer to tegra_dce struct.
 * @val : true or false.
 *
 * Return : void
 */
static inline void dce_set_ast_config_status(struct tegra_dce *d, bool val)
{
	d->ast_config_complete = val;
}

/**
 * dce_set_dce_reset_status - updates the current status of dce reset.
 *
 * @d : Pointer to tegra_dce struct.
 * @val : true or false.
 *
 * Return : void
 */
static inline void dce_set_dce_reset_status(struct tegra_dce *d, bool val)
{
	d->reset_complete = val;
}

/**
 * dce_set_load_fw_status - updates the current status of fw loading.
 *
 * @d : Pointer to tegra_dce struct.
 * @val : true or false stating fw load is complete or incomplete respectiveely.
 *
 * Return : void
 */
static inline void dce_set_load_fw_status(struct tegra_dce *d, bool val)
{
	d->load_complete = val;
}

/**
 * Common Utility Functions. Description can be found with
 * function definitions.
 */
void dce_driver_start(struct tegra_dce *d);
void dce_driver_stop(struct tegra_dce *d);
int dce_driver_init(struct tegra_dce *d);
void dce_driver_deinit(struct tegra_dce *d);

int dce_start_boot_flow(struct tegra_dce *d);
void dce_bootstrap_work_fn(struct tegra_dce *d);
int dce_start_bootstrap_flow(struct tegra_dce *d);
int dce_boot_interface_init(struct tegra_dce *d);
void dce_boot_interface_deinit(struct tegra_dce *d);
int dce_handle_boot_cmd_requested_event(struct tegra_dce *d, void *params);
int dce_handle_boot_cmd_received_event(struct tegra_dce *d, void *params);
int dce_handle_boot_complete_requested_event(struct tegra_dce *d, void *params);
int dce_handle_boot_complete_received_event(struct tegra_dce *d, void *params);

int dce_admin_init(struct tegra_dce *d);
void dce_admin_deinit(struct tegra_dce *d);
int dce_start_admin_seq(struct tegra_dce *d);
int dce_admin_send_msg(struct tegra_dce *d,
		struct dce_ipc_message *msg);
void dce_admin_ivc_channel_reset(struct tegra_dce *d);
int dce_admin_get_ipc_channel_info(struct tegra_dce *d,
					struct dce_ipc_queue_info *q_info);
int dce_admin_send_cmd_echo(struct tegra_dce *d,
			    struct dce_ipc_message *msg);
int dce_admin_send_cmd_ext_test(struct tegra_dce *d,
				struct dce_ipc_message *msg);
int dce_admin_send_prepare_sc7(struct tegra_dce *d,
			       struct dce_ipc_message *msg);
int dce_admin_send_enter_sc7(struct tegra_dce *d,
			     struct dce_ipc_message *msg);
int dce_admin_handle_ipc_requested_event(struct tegra_dce *d, void *params);
int dce_admin_handle_ipc_received_event(struct tegra_dce *d, void *params);
int dce_admin_send_cmd_get_perf_stat(struct tegra_dce *d,
				     struct dce_ipc_message *msg);
int dce_admin_send_cmd_set_perf_stat(struct tegra_dce *d,
				     struct dce_ipc_message *msg,
				     bool start_perf);
int dce_admin_send_cmd_stop_perf_stat(struct tegra_dce *d,
				       struct dce_ipc_message *msg);
int dce_admin_send_cmd_get_perf_events(struct tegra_dce *d,
				       struct dce_ipc_message *msg);
int dce_admin_send_cmd_clear_perf_events(struct tegra_dce *d,
				       struct dce_ipc_message *msg);
int dce_admin_ipc_wait(struct tegra_dce *d);
void dce_admin_ipc_handle_signal(struct tegra_dce *d, u32 ch_type);

bool dce_fw_boot_complete(struct tegra_dce *d);
void dce_request_fw_boot_complete(struct tegra_dce *d);

/**
 * Functions to be used in debug mode only.
 *
 * TODO : Have sanity checks for these not to be
 * used in non-debug mode.
 */
void dce_config_ast(struct tegra_dce *d);
int dce_reset_dce(struct tegra_dce *d);

#ifdef CONFIG_DEBUG_FS
void dce_init_debug(struct tegra_dce *d);
void dce_remove_debug(struct tegra_dce *d);
#endif

#endif
