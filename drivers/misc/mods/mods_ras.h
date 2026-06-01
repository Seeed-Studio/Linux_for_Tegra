/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2019-2023, NVIDIA CORPORATION.  All rights reserved. */

#include "mods_internal.h"

void enable_cpu_core_reporting(u64 config);

/*Set the ERR_SEL register to choose the
 *node for which to enable/disable errors for
 */
void set_err_sel(u64 sel_val);

/*Set the ERR_CTRL register selected
 *by ERR_SEL
 */
void set_err_ctrl(u64 ctrl_val);

/*Get the ERR_CTRL register selected
 *by ERR_SEL
 */
u64 get_err_ctrl(void);

