/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#ifndef _M_TTCAN_IVC_H
#define  _M_TTCAN_IVC_H

/*Size of data in an element */
enum m_ttcan_ivc_msgid {
	MTTCAN_MSG_TX = 1,
	MTTCAN_MSG_RX = 2,
	MTTCAN_MSG_TX_COMPL = 3,
	MTTCAN_MSG_STAT_CHG = 4,
	MTTCAN_MSG_BERR_CHG = 5,
	MTTCAN_MSG_RX_LOST_FRAME = 6,
	MTTCAN_MSG_TXEVT = 7,
	MTTCAN_CMD_CAN_ENABLE = 8,
	MTTCAN_MSG_LAST
};

#endif
