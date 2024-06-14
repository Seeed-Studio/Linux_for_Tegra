/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides constants for binding nvidia,tegra186-hsp.
 */

#ifndef _DT_BINDINGS_MAILBOX_TEGRA186_HSP_OOT_H
#define _DT_BINDINGS_MAILBOX_TEGRA186_HSP_OOT_H

#include <dt-bindings/mailbox/tegra186-hsp.h>

/*
 * These define the types of shared mailbox supported based on data size.
 */
#ifdef TEGRA_HSP_MBOX_TYPE_SM_128BIT
#undef TEGRA_HSP_MBOX_TYPE_SM_128BIT
#endif

#define TEGRA_HSP_MBOX_TYPE_SM_128BIT 0x4

/*
 * Shared interrupt source, mapped with mailboxes
 */
#define TEGRA_HSP_SHARED_IRQ_MASK 0xffff0000
#define TEGRA_HSP_SHARED_IRQ_OFFSET (16)
#define TEGRA_HSP_SHARED_IRQ(x) (((x) << TEGRA_HSP_SHARED_IRQ_OFFSET) & TEGRA_HSP_SHARED_IRQ_MASK)

#endif
