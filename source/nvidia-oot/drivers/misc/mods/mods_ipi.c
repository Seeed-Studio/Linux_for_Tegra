// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved. */

#include "mods_internal.h"

#include <asm/barrier.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <linux/smp.h>

#define MAX_IPI_WFI_LOOPS	5
#define MAX_IPI_WFE_LOOPS	10

static void ipi_wfi_cpu(void *p)
{
	struct MODS_SEND_IPI *p_ipi = (struct MODS_SEND_IPI *)p;
	u32 i;

	for (i = 0; i < p_ipi->num_loops; i++)
		wfi();
}

static void ipi_wfe_cpu(void *p)
{
	struct MODS_SEND_IPI *p_ipi = (struct MODS_SEND_IPI *)p;
	u32 i;

	for (i = 0; i < p_ipi->num_loops; i++)
		wfe();
}

int esc_mods_send_ipi(struct mods_client *client, struct MODS_SEND_IPI *p)
{
	LOG_ENT();

	switch (p->ipi_type) {
	case MODS_IPI_KICK: {
		kick_all_cpus_sync();
		break;
	}
	case MODS_IPI_WFI: {
		if (p->num_loops > MAX_IPI_WFI_LOOPS) {
			cl_error("num_loops %u exceed maximum limit %d for MODS_IPI_WFI\n",
				p->num_loops,
				MAX_IPI_WFI_LOOPS);
			LOG_EXT();
			return -EINVAL;
		}
		preempt_disable();
		smp_call_function_many(cpu_online_mask, ipi_wfi_cpu, (void *)p, 1);
		preempt_enable();
		break;
	}
	case MODS_IPI_WFE: {
		if (p->num_loops > MAX_IPI_WFE_LOOPS) {
			cl_error("num_loops %u exceed maximum limit %d for MODS_IPI_WFE\n",
				p->num_loops,
				MAX_IPI_WFE_LOOPS);
			LOG_EXT();
			return -EINVAL;
		}
		preempt_disable();
		smp_call_function_many(cpu_online_mask, ipi_wfe_cpu, (void *)p, 1);
		preempt_enable();
		break;
	}
	default: {
		cl_error("unsupported MODS_IPI_TYPE\n");
		LOG_EXT();
		return -EINVAL;
	}
	}

	LOG_EXT();
	return 0;
}
