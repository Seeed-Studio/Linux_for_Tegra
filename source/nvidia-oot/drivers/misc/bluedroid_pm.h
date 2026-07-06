// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2019-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved */

#ifndef _BLUEDROID_PM_H
#define _BLUEDROID_PM_H
void bluedroid_pm_set_ext_state(bool blocked);
void bt_wlan_lock(void);
void bt_wlan_unlock(void);
#endif /* _BLUEDROID_PM_H */
