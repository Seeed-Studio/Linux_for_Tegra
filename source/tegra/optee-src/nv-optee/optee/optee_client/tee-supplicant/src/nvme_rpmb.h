// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES.
 */


#ifndef NVME_RPMB_H
#define NVME_RPMB_H

#include <stdint.h>
#include <stddef.h>

uint32_t nvme_rpmb_process_request(void *req, size_t req_size, void *rsp,
			      size_t rsp_size);

#endif /* NVME_RPMB_H */
