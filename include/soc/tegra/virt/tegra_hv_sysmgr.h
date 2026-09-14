/*
 * SPDX-License-Identifier: GPL-2.0-only
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef TEGRA_HV_SYSMGR_H
#define TEGRA_HV_SYSMGR_H

#include <linux/types.h>

/**
 * @defgroup tegra_hv_sysmgr Hypervisor System Manager
 * @{
 */

/** @brief Max size of IVC Message supported by IVC Queue */
#define SYSMGR_IVCMSG_SIZE_MAX 64

/** @brief Represents System Manager Message Types */
enum hv_sysmgr_msg_type {
	/** @brief System Manager Message representing Guest Event */
	HV_SYSMGR_MSG_TYPE_GUEST_EVENT		= 1,
	/** @brief System Manager Message representing VM PM Ctl Event */
	HV_SYSMGR_MSG_TYPE_VM_PM_CTL_CMD	= 2,
	/** @brief System Manager Message representing Invalid Event */
	HV_SYSMGR_MSG_TYPE_INVALID
};

/** @brief Represents System Manager Request Command Ids */
enum hv_sysmgr_cmd_id {
	/** @brief System Manager Shutdown Command Id */
	HV_SYSMGR_CMD_NORMAL_SHUTDOWN	= 0x0,
	/** @brief System Manager Reboot Command Id */
	HV_SYSMGR_CMD_NORMAL_REBOOT	= 0x1,
	/** @brief System Manager Suspend Command Id */
	HV_SYSMGR_CMD_NORMAL_SUSPEND	= 0x2,
	/** @brief System Manager Resume Command Id */
	HV_SYSMGR_CMD_NORMAL_RESUME	= 0x3,
	/** @brief System Manager Invalid Command Id */
	HV_SYSMGR_CMD_INVALID		= 0xFFFFFFFF,
};

/** @brief Represents System Manager Response Command Ids */
enum hv_sysmgr_resp_id {
	/** @brief System Manager Accepted Command Id */
	HV_SYSMGR_RESP_ACCEPTED		= 0x0,
	/** @brief System Manager Unknown Command Id */
	HV_SYSMGR_RESP_UNKNOWN_COMMAND	= 0xF,
};

/**
 * @brief This struct comes as payload of hv_pm_ctl_message.
 * The __packed attribute ensures no padding is added.
 */
struct __packed hv_sysmgr_command {
	/** @brief Command Id */
	uint32_t cmd_id;
	/** @brief Response Id */
	uint32_t resp_id;
};

/**
 * @brief System Manager Message structure send or receive over IVC Queue.
 * The __packed attribute ensures no padding is added.
 */
struct __packed hv_sysmgr_message {
	/** @brief Message Type from hv_sysmgr_msg_type structure */
	uint32_t msg_type;
	/** @brief Hypervisor sets to an opaque cookie */
	uint32_t socket_id;
	/** @brief client data area. Payload of type hv_sysmgr_command structure */
	uint8_t client_data[SYSMGR_IVCMSG_SIZE_MAX];
};


/** @brief QUERY_SYSTEM_STATE COMMAND DATA LAYOUT */
struct hyp_sys_state_info {
	/** @brief Indicates System State Transition */
	uint32_t sys_transition_mask;

	/** @brief Indicates which VM shutdown request is pending */
	uint32_t vm_shutdown_mask;

	/** @brief Indicates which VM reboot request is pending */
	uint32_t vm_reboot_mask;

	/** @brief Indicates which VM suspend request is pending */
	uint32_t vm_suspend_phase_1_mask;
	/** @brief Indicates which VM suspend request is pending */
	uint32_t vm_suspend_phase_2_mask;

	/** @brief Indicates which VM resume request is pending */
	uint32_t vm_resume_mask;
};

/** @brief Power management calls ID's used by SYSMGR to manage LOCAL/GLOBAL EVENTS */
enum system_function_id {
	/** @brief Represents Invalid Function */
	INVALID_FUNC,
	/** @brief
	 * This is used to get reboot/shutdown masks per VM from hypervisor.
	 * Hypervisor updates state fields on a PSCI event from the VM.
	 */
	QUERY_SYSTEM_STATE,
	/** @brief Represents Guest Shutdown Init State */
	GUEST_SHUTDOWN_INIT,
	/** @brief Represents Guest Shutdown Complete State */
	GUEST_SHUTDOWN_COMPLETE,
	/** @brief Represents Guest Reboot Init State */
	GUEST_REBOOT_INIT,
	/** @brief Represents Guest Reboot Continue State */
	GUEST_REBOOT_CONTINUE,
	/** @brief Represents Guest Reboot Complete State */
	GUEST_REBOOT_COMPLETE,
	/** @brief Represents System Shutdown Init State */
	SYSTEM_SHUTDOWN_INIT,
	/** @brief Represents System Shutdown Complete State */
	SYSTEM_SHUTDOWN_COMPLETE,
	/** @brief Represents System Reboot Init State */
	SYSTEM_REBOOT_INIT,
	/** @brief Represents System Reboot Complete State */
	SYSTEM_REBOOT_COMPLETE,
	/** @brief Represents Guest Suspend Request State */
	GUEST_SUSPEND_REQ,
	/** @brief Represents Guest Suspend Init State */
	GUEST_SUSPEND_INIT,
	/** @brief Represents Guest Suspend Complete State */
	GUEST_SUSPEND_COMPLETE,
	/** @brief Represents Guest Resume Init State */
	GUEST_RESUME_INIT,
	/** @brief Represents Guest Resume Complete State */
	GUEST_RESUME_COMPLETE,
	/** @brief Represents Guest Pause State */
	GUEST_PAUSE,
	/** @brief Represents System Suspend Init State */
	SYSTEM_SUSPEND_INIT,
	/** @brief Represents System Suspend Complete State */
	SYSTEM_SUSPEND_COMPLETE,
	GUEST_ENTER_VM_OP,
	/** @brief Represents Max Possible Function Id */
	MAX_FUNC_ID,
};

/** @brief Represents VM state and send to System Manager to maintian VM state */
enum vm_state {
	/** @brief Represents VM state as Booted */
	VM_STATE_BOOT,
	/** @brief Represents VM state as Halted */
	VM_STATE_HALT,
	/** @brief Represents VM state as Unhalted */
	VM_STATE_UNHALT,
	/** @brief Represents VM state as Reboted */
	VM_STATE_REBOOT,
	/** @brief Represents VM state as Shutdown */
	VM_STATE_SHUTDOWN,
	/** @brief Represents VM state as Suspend */
	VM_STATE_SUSPEND,
	/** @brief Represents VM state as Resume */
	VM_STATE_RESUME,
	/** @brief Represents VM state as Invalid */
	VM_STATE_INVALID,
	/** @brief Represents VM state as Max possible */
	VM_STATE_MAX
};

/** @brief Macro to create Command using function id and vmid */
#define CREATE_CMD(func_id, vmid)	((func_id << 24U) | vmid)

/** @brief
 * This is used to get reboot/shutdown masks per VM from hypervisor.
 * Hypervisor updates state fields on a PSCI event from the VM.
 */
#define QUERY_CMD			CREATE_CMD(QUERY_SYSTEM_STATE, 0)
/** @brief Macro Represents Guest Shutdown Init State to Guest VM having vmid as VM Id */
#define GUEST_SHUTDOWN_INIT_CMD(vmid)	CREATE_CMD(GUEST_SHUTDOWN_INIT, vmid)
/** @brief Macro Represents Guest Shutdown Complete Command to Guest VM having vmid as VM Id */
#define GUEST_SHUTDOWN_COMPLETE_CMD(vmid) \
				CREATE_CMD(GUEST_SHUTDOWN_COMPLETE, vmid)
/** @brief Macro Represents Guest Reboot Init Command to Guest VM having vmid as VM Id */
#define GUEST_REBOOT_INIT_CMD(vmid)	CREATE_CMD(GUEST_REBOOT_INIT, vmid)
/** @brief Macro Represents Guest Reboot Continue Command to Guest VM having vmid as VM Id */
#define GUEST_REBOOT_CONTINUE_CMD(vmid)	CREATE_CMD(GUEST_REBOOT_CONTINUE, vmid)
/** @brief Macro Represents Guest Reboot Complete Command to Guest VM having vmid as VM Id */
#define GUEST_REBOOT_COMPLETE_CMD(vmid)	CREATE_CMD(GUEST_REBOOT_COMPLETE, vmid)
/** @brief Macro Represents System Shutdown Init Command */
#define SYS_SHUTDOWN_INIT_CMD		CREATE_CMD(SYSTEM_SHUTDOWN_INIT, 0)
/** @brief Macro Represents System Shutdown Complete Command */
#define SYS_SHUTDOWN_COMPLETE_CMD	CREATE_CMD(SYSTEM_SHUTDOWN_COMPLETE, 0)
/** @brief Macro Represents System Reboot Init Command */
#define SYS_REBOOT_INIT_CMD		CREATE_CMD(SYSTEM_REBOOT_INIT, 0)
/** @brief Macro Represents System Reboot Complete Command */
#define SYS_REBOOT_COMPLETE_CMD		CREATE_CMD(SYSTEM_REBOOT_COMPLETE, 0)
/** @brief Macro Represents Guest Resume Init Command to Guest VM having vmid as VM Id */
#define GUEST_SUSPEND_REQ_CMD(vmid)       CREATE_CMD(GUEST_SUSPEND_REQ, vmid)
/** @brief Macro Represents Guest Suspend Init Command to Guest VM having vmid as VM Id */
#define GUEST_SUSPEND_INIT_CMD(vmid)      CREATE_CMD(GUEST_SUSPEND_INIT, vmid)
/** @brief Macro Represents Guest Suspend Complete Command to Guest VM having vmid as VM Id */
#define GUEST_SUSPEND_COMPLETE_CMD(vmid) \
		CREATE_CMD(GUEST_SUSPEND_COMPLETE,vmid)
/** @brief Macro Represents Guest Resume Init Command to Guest VM having vmid as VM Id */
#define GUEST_RESUME_INIT_CMD(vmid)       CREATE_CMD(GUEST_RESUME_INIT, vmid)
/** @brief Macro Represents Guest Resume Complete Command to Guest VM having vmid as VM Id */
#define GUEST_RESUME_COMPLETE_CMD(vmid)   CREATE_CMD(GUEST_RESUME_COMPLETE, vmid)
/** @brief Macro Represents Guest Pause Command to Guest VM having vmid as VM Id */
#define GUEST_PAUSE_CMD(vmid)		CREATE_CMD(GUEST_PAUSE, vmid)
/** @brief Macro Represents System Suspend Init Command */
#define SYS_SUSPEND_INIT_CMD		CREATE_CMD(SYSTEM_SUSPEND_INIT, 0)
/** @brief Macro Represents System Suspend Complete Command */
#define SYS_SUSPEND_COMPLETE_CMD	CREATE_CMD(SYSTEM_SUSPEND_COMPLETE, 0)
#define GUEST_ENTER_VM_OP_CMD		CREATE_CMD(GUEST_ENTER_VM_OP, 0)

/** @} */

#endif /* TEGRA_HV_SYSMGR_H */
