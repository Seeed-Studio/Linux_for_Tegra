/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2023, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef __NVSCIIPC_INTERFACE_H__
#define __NVSCIIPC_INTERFACE_H__

/** Invalid VUID definition */
#define NVSCIIPC_ENDPOINT_VUID_INVALID      0U
/** Invalid authentication token definition */
#define NVSCIIPC_ENDPOINT_AUTHTOKEN_INVALID 0U
/** current self SOC ID */
#define NVSCIIPC_SELF_SOCID 0xFFFFFFFFU
/** current self VM ID */
#define NVSCIIPC_SELF_VMID  0xFFFFFFFFU

/**
 * @brief Handle to the IPC endpoint.
 */
typedef uint64_t NvSciIpcEndpoint;


/**
 * @brief VUID(VM unique ID) of the IPC endpoint.
 */
typedef uint64_t NvSciIpcEndpointVuid;

/**
 * @brief authentication token of the IPC endpoint.
 */
typedef uint64_t NvSciIpcEndpointAuthToken;

/**
 * @brief Defines topology ID of the IPC endpoint.
 */
typedef struct {
	/*! Holds SOC ID */
	uint32_t SocId;
	/*! Holds VMID */
	uint32_t VmId;
} NvSciIpcTopoId;

/**********************************************************************/
/*********************** Function Definitions *************************/
/**********************************************************************/
NvSciError NvSciIpcEndpointGetAuthToken(NvSciIpcEndpoint handle,
		NvSciIpcEndpointAuthToken *authToken);

NvSciError NvSciIpcEndpointValidateAuthTokenLinuxCurrent(
		NvSciIpcEndpointAuthToken authToken,
		NvSciIpcEndpointVuid *localUserVuid);

NvSciError NvSciIpcEndpointMapVuid(NvSciIpcEndpointVuid localUserVuid,
		NvSciIpcTopoId *peerTopoId, NvSciIpcEndpointVuid *peerUserVuid);

NvSciError NvSciIpcEndpointGetVuid(NvSciIpcEndpoint handle,
		NvSciIpcEndpointVuid *vuid);

#endif /* __NVSCIIPC_INTERFACE_H__ */
