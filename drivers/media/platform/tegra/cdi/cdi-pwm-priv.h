/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2016-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef __CDI_PWM_PRIV_H__
#define __CDI_PWM_PRIV_H__

struct cdi_pwm_info {
#if !defined(NV_PWM_CHIP_STRUCT_HAS_STRUCT_DEVICE)
	struct pwm_chip chip;
#endif
	struct pwm_device *pwm;
	atomic_t in_use;
	struct mutex mutex;
	bool force_on;
};

#endif  /* __CDI_PWM_PRIV_H__ */
