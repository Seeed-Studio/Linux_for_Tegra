/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2025, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 */

/**
 * @file camrtc-commands.h Commands for the HSP-VM and legacy HSP protocols.
 */

#ifndef INCLUDE_CAMRTC_COMMANDS_H
#define INCLUDE_CAMRTC_COMMANDS_H

#include "camrtc-common.h"

/**
 * @brief Create HSP-VM message.
 *
 * Combine a 7-bit command ID with a 24-bit parameter in order to
 * form a 32-bit HSP-VM message.
 *
 * @pre None.
 *
 * @param	_id	Command ID [1,0x7F]. See @ref HspVmCmds
 *			"HSP-VM commands" for valid values.
 * @param	_param	Command-specific parameter value [0, 0xFFFFFF].
 *			See @ref @HspVmCmds "HSP-VM commands" for
 *			parameter definitions.
 * @return		32-bit HSP mailbox message (uint32_t)
 */
#define CAMRTC_HSP_MSG(_id, _param) \
	(((uint32_t)(_id) << MK_U32(24)) | ((uint32_t)(_param) & MK_U32(0xffffff)))

/**
 * @brief Decode command ID from HSP-VM message.
 *
 * Decode a 7-bit command ID from a 32-bit HSP-VM message.
 *
 * @pre None.
 *
 * @param	_msg	32-bit HSP mailbox message (uint32_t).
 * @return		Command ID [0,0x7F]. See @ref HspVmCmds
 *			"HSP-VM commands" for valid values.
 */
#define CAMRTC_HSP_MSG_ID(_msg) \
	(((_msg) >> MK_U32(24)) & MK_U32(0x7f))

/**
 * @brief Decode parameter value from HSP-VM message.
 *
 * Decode a 24-bit parameter value from a 32-bit HSP-VM message.
 *
 * @param	_msg	32-bit HSP mailbox message (uint32_t).
 * @return		Command-specific parameter value [0, 0xFFFFFF].
 *			See @ref @HspVmCmds "HSP-VM commands" for
 *			parameter definitions.
 */
#define CAMRTC_HSP_MSG_PARAM(_msg)		\
	((uint32_t)(_msg) & MK_U32(0xffffff))

/**
 * @defgroup HspVmCmds HSP-VM commands
 *
 * The HSP-VM protocol is a synchronous request-reply protocol (except for
 * one notable @ref CAMRTC_HSP_IRQ "exception"), where the CCPLEX VM client
 * first writes a command word into a HSP shared mailbox and then waits for
 * RCE FW to respond over the same mailbox. The responses are formatted
 * with the same command ID as the associated requests, but the parameter
 * values in each direction may differ.
 *
 * The protocol observes a state machine with state transitions triggered
 * by message interactions. The protocol is described in more detail in
 * the unit design documentation.
 *
 * The 32-bit HSP messages have the following format:
 *
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 31    | Valid bit (managed by HSP driver)                 |
 * |       |  0 = mailbox is empty                             |
 * |       |  1 = mailbox is populated with a message          |
 * +-------+---------------------------------------------------+
 * | 30:24 | Command ID                                        |
 * +-------+---------------------------------------------------+
 * | 23:0  | Command parameter                                 |
 * +-------+---------------------------------------------------+
 * @endrst
 *
 * The command ID and parameter values are described below.
 * @{
 */

/**
 * This is a @ref HspVmCmds "HSP-VM command" that signals the peer that
 * some action is needed, for example, that new IVC messages are available
 * in the receive queue of the peer. Unlike the other HSP-VM messages,
 * the CAMRTC_HSP_IRQ message can be sent by either by the client
 * (e.g. VM on CCPLEX) or RCE itself. The message is asynchronous and
 * is never directly acknowledged by the peer.
 *
 * @note Each peer is required to check its IVC receive queue whenever
 * any HSP message is received, not just when a CAMRTC_HSP_IRQ is received.
 * Therefore, the CAMRTC_HSP_IRQ message need not be sent if another message
 * for the peer is already pending in the HSP shared mailbox. For the
 * same reason, it is permitted to overwrite an outgoing CAMRTC_HSP_IRQ
 * message populated in a HSP mailbox with another outgoing HSP message.
 *
 * The HSP-VM protocol defines an optional mechanism to optimize the
 * IVC wakeups. Each IVC channel is associated with one or more
 * @ref camrtc_tlv_ivc_setup::channel_group "channel wakeup groups".
 * The sender may use an associated HSP @ref HspSsReg "shared semaphore"
 * to set @ref CAMRTC_HSP_SS_IVC_MASK "IVC group bits" to indicate that
 * the receiver needs to inspect the IVC channels belonging to those
 * groups for incoming messages.
 *
 * RCE FW uses bits [7:0] (@ref CAMRTC_HSP_SS_IVC_MASK "IVC group bits"
 * << @ref CAMRTC_HSP_SS_FW_SHIFT) of the @ref HspSsReg
 * "shared semaphore register" to signal outgoing messages to CCPLEX VM.
 *
 * CCPLEX VM uses bits [23:16] (@ref CAMRTC_HSP_SS_IVC_MASK
 * "IVC group bits" << @ref CAMRTC_HSP_SS_VM_SHIFT) of the @ref HspSsReg
 * "shared semaphore register" to signal outgoing messages to RCE FW.
 *
 * If no IVC group bits are set (sender likely does not support the
 * optimization) or the receiver itself does not support the
 * optimization, the receiver must always inspect all IVC channels for
 * state changes and incoming messages after receiving a HSP message.
 *
 * Other bits in the 32-bit @ref HspSsReg "shared semaphore register"
 * are reserved for additional signalling between CCPLEX VM and RCE.
 * These bits are currently unused.
 *
 * @pre IVC channels have been set up with @ref CAMRTC_HSP_CH_SETUP and
 *      RCE has been placed into active state with @ref CAMRTC_HSP_RESUME.
 *
 * @par Request
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_IRQ                                    |
 * +-------+---------------------------------------------------+
 * | 23:0  | 0x000000                                          |
 * +-------+---------------------------------------------------+
 * @endrst
 *
 * @par Response
 * None
 */
#define CAMRTC_HSP_IRQ			MK_U32(0x00)

/**
 * @brief HELLO message
 *
 * The CAMRTC_HSP_HELLO message is sent by the client in the beginning of
 * a HSP-VM session. RCE FW will respond with a CAMRTC_HSP_HELLO of its
 * own.
 *
 * The HELLO message exchange ensures there are no unprocessed messages
 * in transit within VM or RCE FW. The parameter field contains a
 * randomly chosen cookie value that will identify the new session.
 * RCE FW stores the cookie value and responds in acknowledgement.
 *
 * If RCE FW already has an active HSP-VM session for the HSP mailbox,
 * with a different cookie value, RCE FW will reset its HSP-VM machine
 * for the client before responding. This mechanism allows RCE FW to
 * detect unexpected client restarts.
 *
 * @pre None.
 *
 * @par Request
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_HELLO                                  |
 * +-------+---------------------------------------------------+
 * | 23:0  | Cookie [0x1,0xFFFFFF]                             |
 * +-------+---------------------------------------------------+
 * @endrst
 *
 * @par Response
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_HELLO                                  |
 * +-------+---------------------------------------------------+
 * | 23:0  | Cookie, value copied from request message         |
 * +-------+---------------------------------------------------+
 * @endrst
 */
#define CAMRTC_HSP_HELLO		MK_U32(0x40)

/**
 * @brief BYE message
 *
 * VM session close in indicated using BYE message,
 * RCE FW reclaims the resources assigned to given VM.
 * It must be sent before the Camera VM shuts down self.
 *
 * @pre @ref CAMRTC_HSP_HELLO exchange has been completed.
 *
 * @par Request
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_BYE                                    |
 * +-------+---------------------------------------------------+
 * | 23:0  | Undefined                                         |
 * +-------+---------------------------------------------------+
 * @endrst
 *
 * @par Response
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_BYE                                    |
 * +-------+---------------------------------------------------+
 * | 23:0  | 0x000000                                          |
 * +-------+---------------------------------------------------+
 * @endrst
 */
#define CAMRTC_HSP_BYE			MK_U32(0x41)

/**
 * @brief RESUME message
 *
 * The CAMRTC_HSP_RESUME message is sent when VM wants to activate
 * the RCE FW and access the camera hardware through it. The
 * RESUME message is required after CAMRTC_HSP_HELLO, as the
 * state machine will always be in inactive state at first.
 *
 * The Cookie value given as parameter must match the stored
 * cookie value for the active HSP-VM session, otherwise
 * RCE FW will ignore the command respond with RTCPU_RESUME_ERROR.
 *
 * @pre @ref CAMRTC_HSP_HELLO exchange has been completed.
 *
 * @par Request
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_RESUME                                 |
 * +-------+---------------------------------------------------+
 * | 23:0  | Cookie                                            |
 * +-------+---------------------------------------------------+
 * @endrst
 *
 * @par Response
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_RESUME                                 |
 * +-------+---------------------------------------------------+
 * | 23:0  | 0x000000 or RTCPU_RESUME_ERROR (0xFFFFFF)         |
 * +-------+---------------------------------------------------+
 * @endrst
 */
#define CAMRTC_HSP_RESUME		MK_U32(0x42)

/**
 * @brief SUSPEND message
 *
 * The CAMRTC_HSP_SUSPEND message is sent when VM wants to deactivate
 * the RCE FW and let it power off the camera hardware if there are
 * no other users. The SUSPEND message is normally sent when entering
 * runtime suspend state (Linux). RCE FW can be brought back into
 * active state by sending the @ref CAMRTC_HSP_RESUME "RESUME"
 * message.
 *
 * @note RCE FW can support multiple HSP-VM clients. Every one of them
 * must be in inactve state, before camera hardware is powered off.
 *
 * @pre @ref CAMRTC_HSP_HELLO exchange has been completed.
 *
 * @par Request
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_SUSPEND                                |
 * +-------+---------------------------------------------------+
 * | 23:0  | Undefined                                         |
 * +-------+---------------------------------------------------+
 * @endrst
 *
 * @par Response
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_SUSPEND                                |
 * +-------+---------------------------------------------------+
 * | 23:0  | 0x000000                                          |
 * +-------+---------------------------------------------------+
 * @endrst
 */
#define CAMRTC_HSP_SUSPEND		MK_U32(0x43)

/**
 * Used to set up a shared memory area (such as IVC channels, trace buffer etc)
 * between Camera VM and RCE FW.
 */
/**
 * @brief CH_SETUP message
 *
 * The CAMRTC_CH_SETUP message is sent when VM wants to set
 * up shared memory areas for IVC channels, debug trace, or
 * other purposes.
 *
 * @pre @ref CAMRTC_HSP_HELLO exchange has been completed.
 *      @ref CAMRTC_HSP_PROTOCOL exchange is recommended to
 *      have been completed.
 *
 * @par Request
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_CH_SETUP                               |
 * +-------+---------------------------------------------------+
 * | 23:0  | Bits [31:8] of IOVA pointing to channel setup     |
 * |       | structures in shared memory. See 1)               |
 * +-------+---------------------------------------------------+
 * @endrst
 *
 * 1) A 32-bit IOVA pointing to one or more @ref camrtc_tlv
 *    structures in shared memory, containing detailed channel
 *    setup information.
 *
 * @par Response
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_CH_SETUP                               |
 * +-------+---------------------------------------------------+
 * | 23:0  | Channel setup error code. See 2)                  |
 * +-------+---------------------------------------------------+
 * @endrst
 *
 * 2) @ref CamRTCChannelErrors "Channel setup errors".
 */
#define CAMRTC_HSP_CH_SETUP		MK_U32(0x44)

/**
 * @brief PING message
 *
 * The CAMRTC_HSP_PING message provides a simple connectivity
 * test to see if RCE FW is responding. The client can send
 * any value in the parameter field and RCE FW will echo it
 * back.
 *
 * @pre @ref CAMRTC_HSP_HELLO exchange has been completed.
 *
 * @par Request
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_PING                                   |
 * +-------+---------------------------------------------------+
 * | 23:0  | Ping payload [0x0, 0xFFFFFF ]                     |
 * +-------+---------------------------------------------------+
 * @endrst
 *
 * @par Response
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_SUSPEND                                |
 * +-------+---------------------------------------------------+
 * | 23:0  | Ping payload, copied from request                 |
 * +-------+---------------------------------------------------+
 * @endrst
 */
#define CAMRTC_HSP_PING			MK_U32(0x45)
/**
 * SHA1 hash code for RCE FW binary.
 */

/**
 * @brief FW_HASH message
 *
 * The CAMRTC_FW_HASH message can be used to query the RCE FW
 * firmware hash (SHA1) value from RCE FW. Retrieving the full
 * hash value requires multiple queries, one for each byte.
 *
 * @pre @ref CAMRTC_HSP_HELLO exchange has been completed.
 *
 * @par Request
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_FW_HASH                                |
 * +-------+---------------------------------------------------+
 * | 23:0  | Hash byte index [0, 19]                           |
 * +-------+---------------------------------------------------+
 * @endrst
 *
 * @par Response
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_FW_HASH                                |
 * +-------+---------------------------------------------------+
 * | 23:0  | Hash byte [0,0xFF] or                             |
 * |       | RTCPU_FW_HASH_ERROR (0xFFFFFF)                    |
 * +-------+---------------------------------------------------+
 * @endrst
 */
#define CAMRTC_HSP_FW_HASH		MK_U32(0x46)

/**
 * The VM includes its protocol version as a parameter to PROTOCOL message.
 * FW responds with its protocol version, or RTCPU_FW_INVALID_VERSION
 * if the VM protocol is not supported.
 */
/**
 * @brief PROTOCOL message
 *
 * The CAMRTC_HSP_PROTOCOL message is used to exchange HSP-VM
 * protocol versions between client and RCE FW. While the message
 * is optional, it is recommended to use it after the HELLO
 * exchange in order to enable all supported protocol features.
 *
 * @pre @ref CAMRTC_HSP_HELLO exchange has been completed.
 *
 * @par Request
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_PROTOCOL                               |
 * +-------+---------------------------------------------------+
 * | 23:0  | HSP-VM host version. See 1)                       |
 * +-------+---------------------------------------------------+
 * @endrst
 *
 *
 * 1) @ref HspHostVersions "Host HSP protocol version"
 *
 * @par Response
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_PROTOCOL                               |
 * +-------+---------------------------------------------------+
 * | 23:0  | HSP-VM FW version. See 2)                         |
 * +-------+---------------------------------------------------+
 * @endrst
 *
 * 2) @ref HspFwVersions   "Firmware HSP protocol version"
 */
#define CAMRTC_HSP_PROTOCOL		MK_U32(0x47)

/**
 * @brief SET_OP_POINT message
 *
 * The CAMRTC_HSP_SET_OP_POINT message is used to apply the
 * specified operating point to Camera IP. Two operating points
 * are supported:
 *
 * Operating Point 0: Camera IP runs at maximum clock speeds.
 * Operating Point 6: Camera IP runs at reduced clock speeds.
 *
 * Camera IP:
 * - RCE HW
 * - VI HW
 * - ISP HW
 * - NVCSI
 *
 * @pre @ref CAMRTC_HSP_HELLO exchange has been completed.
 *
 * @par Request
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_SET_OP_POINT                           |
 * +-------+---------------------------------------------------+
 * | 23:0  | Operating Point {0, 6}                            |
 * +-------+---------------------------------------------------+
 * @endrst
 *
 * @par Response
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_SET_OP_POINT                           |
 * +-------+---------------------------------------------------+
 * | 23:0  | 0x000000                                          |
 * +-------+---------------------------------------------------+
 * @endrst
 */
#define CAMRTC_HSP_SET_OP_POINT  MK_U32(0x48)

/**
 * @brief BOOT_STAGE logging message
 *
 * The CAMRTC_HSP_BOOT_STAGE message is a unidirectional
 * message from RCE to client to log the currently running
 * init function's feature ID and init level.
 *
 * @pre None
 *
 * @par Response
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_BOOT_STAGE                             |
 * +-------+---------------------------------------------------+
 * | 23:16 | Init step feature ID                              |
 * +-------+---------------------------------------------------+
 * | 15:8  | Init step level                                   |
 * +-------+---------------------------------------------------+
 * | 7:0   | Undefined                                         |
 * +-------+---------------------------------------------------+
 * @endrst
 */
#define CAMRTC_HSP_BOOT_STAGE  MK_U32(0x49)

/**
 * @brief BOOT_ERROR logging message
 *
 * The CAMRTC_HSP_BOOT_ERROR message is a unidirectional
 * message from RCE to client to log that an RCE init handler
 * returned error and RCE has halted.
 *
 * @pre None
 *
 * @par Response
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_BOOT_ERROR                             |
 * +-------+---------------------------------------------------+
 * | 23:0  | RTOS error                                        |
 * +-------+---------------------------------------------------+
 * @endrst
 */
#define CAMRTC_HSP_BOOT_ERROR  MK_U32(0x4A)

/**
 * @brief BOOT_COMPLETE logging message
 *
 * The CAMRTC_HSP_BOOT_COMPLETE message is a unidirectional
 * message from RCE to client to log that RCE early boot has
 * completed.
 *
 * @pre None
 *
 * @par Response
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_BOOT_COMPLETE                          |
 * +-------+---------------------------------------------------+
 * | 23:0  | 0x000000                                          |
 * +-------+---------------------------------------------------+
 * @endrst
 */
#define CAMRTC_HSP_BOOT_COMPLETE  MK_U32(0x4B)

/**
 * @brief PANIC message
 *
 * The CAMRTC_HSP_PANIC message message is a unidirectional message from RCE to client
 * to log that RCE is about to go into a bad state. This typically occurs when RCE
 * detects a critical hardware error or encounters an unrecoverable firmware state.
 *
 * Upon receiving this message, the client should dump out the trace buffer snapshot
 * section for debugging purposes.
 *
 * @pre @ref CAMRTC_HSP_HELLO exchange has been completed.
 *
 * @par Response
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_PANIC                                  |
 * +-------+---------------------------------------------------+
 * | 23:0  | 0x000000                                          |
 * +-------+---------------------------------------------------+
 * @endrst
 */
#define CAMRTC_HSP_PANIC		MK_U32(0x4C)

/** Reserved, not to be used. */
#define CAMRTC_HSP_RESERVED_5E		MK_U32(0x5E) /* bug 200395605 */

/**
 * @brief UNKNOWN HSP command response.
 *
 * The CAMRTC_HSP_UNKNOWN message is used for responding with
 * an error to a client that has sent an unsupported HSP command.
 *
 * @pre An unsupported HSP command has been sent to RCE FW.
 *
 * @par Response
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 30:24 | CAMRTC_HSP_UNKNOWN                                |
 * +-------+---------------------------------------------------+
 * | 23:0  | Unsupported HSP command ID                        |
 * +-------+---------------------------------------------------+
 * @endrst
 */
#define CAMRTC_HSP_UNKNOWN		MK_U32(0x7F)
/** @} */

/**
 * @defgroup HspSsReg HSP shared semaphore register.
 *
 * A HSP shared semaphore register may be associated with the HSP
 * shared mailbox to provide additional signaling capabilities
 * between VM and RCE FW. The 32-bit shared semaphore contains
 * 32 bits which may set or cleared individually without affecting
 * other bits in the semaphore register. The semaphore bits can
 * be used to signal different conditions to the peer. Half of
 * the bits are assigned to RCE FW and the other half to CCPLEX VM.
 * The assignment are described in the following table.
 *
 * @rststar
 * +-------+---------------------------------------------------+
 * | Bits  | Description                                       |
 * +=======+===================================================+
 * | 31    | Reserved, not to be used. (Bug 200395605)         |
 * +-------+---------------------------------------------------+
 * | 30:24 | Reserved bits for CCPLEX VM.                      |
 * +-------+---------------------------------------------------+
 * | 23:16 | IVC group bits for CCPLEX VM.                     |
 * +-------+---------------------------------------------------+
 * | 15:8  | Reserved bits for RCE FW.                         |
 * +-------+---------------------------------------------------+
 * | 7:0   | IVC group bits for RCE FW.                        |
 * +-------+---------------------------------------------------+
 * @endrst
 *
 * @{
 */
/** Mask for shared semaphore bits (FW->VM) */
#define CAMRTC_HSP_SS_FW_MASK		MK_U32(0xFFFF)

/** Shift value for shared semaphore bits (FW->VM) */
#define CAMRTC_HSP_SS_FW_SHIFT		MK_U32(0)

/** Mask for shared semaphore bits (VM->FW) */
#define CAMRTC_HSP_SS_VM_MASK		MK_U32(0x7FFF0000)

/** Shift value for shared semaphore bits (VM->FW) */
#define CAMRTC_HSP_SS_VM_SHIFT		MK_U32(16)

/** Shared semaphore bits used to identify IVC channel groups */
#define CAMRTC_HSP_SS_IVC_MASK		MK_U32(0xFF)
/** @} */

/**
 * @defgroup HspVmI2cCmds I2C commands for the HSP-VM protocol.
 * Currently not implemented.
 * @{
 */

/**
 * Configure I2C controller. This will activate the given
 * I2C controller and configure the base address of a shared memory
 * buffer for subsequent I2C transfers. Buffer must be page aligned.
 *
 * param[23:20]	= I2C bus ID
 * param[19:0]	= Bits [31:12] of shared memory buffer bus address
 */
#define CAMRTC_HSP_I2C_INIT		MK_U32(0x10)

/**
 * Configure I2C device address. This will set the slave device
 * for subsequent I2C transfers.
 *
 * param[23:20]	= I2C bus ID
 * param[10:10]	= 10-bit address flag
 * param[9:0]	= device address
 */
#define CAMRTC_HSP_I2C_DEVICE		MK_U32(0x11)

/**
 * Perform I2C transfer. The same shared memory buffer will be used
 * for send and receive. Either byte count may be set to zero.
 *
 * param[23:12]	= recv byte count
 * param[11:0]	= send byte count
 */
#define CAMRTC_HSP_I2C_XFER		MK_U32(0x12)
/** @} */

/**
 * @defgroup HspMailboxMsgs Definitions for "nvidia,tegra-hsp-mailbox" protocol
 * @{
 */
#define RTCPU_COMMAND(id, value) \
	(((RTCPU_CMD_ ## id) << MK_U32(24)) | ((uint32_t)value))

#define RTCPU_GET_COMMAND_ID(value) \
	((((uint32_t)value) >> MK_U32(24)) & MK_U32(0x7f))

#define RTCPU_GET_COMMAND_VALUE(value) \
	(((uint32_t)value) & MK_U32(0xffffff))
/**
 * RCE FW waits until VM client initiates boot sync with INIT HSP command.
 */
#define RTCPU_CMD_INIT			MK_U32(0)
/**
 * VM client sends host version and expects RCE FW to respond back with
 * current FW version, as part of boot sync.
 */
#define RTCPU_CMD_FW_VERSION		MK_U32(1)
#define RTCPU_CMD_RESERVED_02		MK_U32(2)
#define RTCPU_CMD_RESERVED_03		MK_U32(3)
/**
 * Release RCE FW resources assigned to given VM client, during runtime suspend or SC7.
 */
#define RTCPU_CMD_PM_SUSPEND		MK_U32(4)
#define RTCPU_CMD_RESERVED_05		MK_U32(5)
/**
 * Used to set up a shared memory area (such as IVC channels, trace buffer etc)
 * between Camera VM and RCE FW.
 */
#define RTCPU_CMD_CH_SETUP		MK_U32(6)

/**
 * Configure I2C controller. This will activate the given
 * I2C controller and configure the base address of a shared memory
 * buffer for subsequent I2C transfers. Buffer must be page aligned.
 *
 * param[23:20]	= I2C bus ID
 * param[19:0]	= Bits [31:12] of shared memory buffer bus address
 */
#define RTCPU_CMD_I2C_INIT		MK_U32(0x10)

/**
 * Configure I2C device address. This will set the slave device
 * for subsequent I2C transfers.
 *
 * param[23:20]	= I2C bus ID
 * param[10:10]	= 10-bit address flag
 * param[9:0]	= device address
 */
#define RTCPU_CMD_I2C_DEVICE		MK_U32(0x11)

/**
 * Perform I2C transfer. The same shared memory buffer will be used
 * for send and receive. Either byte count may be set to zero.
 *
 * param[23:12]	= recv byte count
 * param[11:0]	= send byte count
 */
#define RTCPU_CMD_I2C_XFER		MK_U32(0x12)

#define RTCPU_CMD_RESERVED_5E		MK_U32(0x5E) /* bug 200395605 */
#define RTCPU_CMD_RESERVED_7D		MK_U32(0x7d)
#define RTCPU_CMD_RESERVED_7E		MK_U32(0x7e)
#define RTCPU_CMD_ERROR			MK_U32(0x7f)
/** @} */

/**
 * @defgroup HspFwVersions Firmare HSP protocol firmware
 * @{
 */
/** @deprecated Historical firmware version. */
#define RTCPU_FW_DB_VERSION		MK_U32(0)

/** @deprecated Historical firmware version. */
#define RTCPU_FW_VERSION		MK_U32(1)

/** @deprecated Historical firmware version. */
#define RTCPU_FW_SM2_VERSION		MK_U32(2)

/** @deprecated Historical firmware version. */
#define RTCPU_FW_SM3_VERSION		MK_U32(3)

/** Legacy HSP version: firmware can restore itself after suspend. */
#define RTCPU_FW_SM4_VERSION		MK_U32(4)

/** Legacy HSP version: firmware supports IVC synchronization (IVC reset). */
#define RTCPU_FW_SM5_VERSION		MK_U32(5)

/** SM6 firmware supports HSP-VM protocol. */
#define RTCPU_FW_SM6_VERSION		MK_U32(6)

/** Current firmware version. */
#define RTCPU_FW_CURRENT_VERSION	RTCPU_FW_SM6_VERSION

/** Invalid firmware version. */
#define RTCPU_FW_INVALID_VERSION	MK_U32(0xFFFFFF)
/** @} */

/**
 * @defgroup HspHostVersions Host HSP protocol versions
 * @{
 */
/** @deprecated Historical driver version. */
#define RTCPU_IVC_SANS_TRACE		MK_U32(1)

/** @deprecated Historical driver version. */
#define RTCPU_IVC_WITH_TRACE		MK_U32(2)

/** SM5 driver supports IVC synchronization (IVC reset).  */
#define RTCPU_DRIVER_SM5_VERSION	MK_U32(5)

/** SM6 driver supports HSP-VM protocol. */
#define RTCPU_DRIVER_SM6_VERSION	MK_U32(6)
/** @} */

/** Size if firmware hash value in bytes */
#define RTCPU_FW_HASH_SIZE		MK_U32(20)

/** Firmware hash error (invalid hash index) */
#define RTCPU_FW_HASH_ERROR		MK_U32(0xFFFFFF)

#define RTCPU_PM_SUSPEND_SUCCESS	MK_U32(0x100)
#define RTCPU_PM_SUSPEND_FAILURE	MK_U32(0x001)

/** Resume to active state failed (session cookie mismatch). */
#define RTCPU_RESUME_ERROR		MK_U32(0xFFFFFF)

/** @} */

/**
 * @defgroup I2cErrors I2C HSP protocol responses
 * @{
 */
#define RTCPU_I2C_DONE			 MK_U32(1)
#define RTCPU_I2C_ERROR			 MK_U32(2)
#define RTCPU_I2C_NACK			 MK_U32(3)
#define RTCPU_I2C_ARBL			 MK_U32(4)
/** @} */

#endif /* INCLUDE_CAMRTC_COMMANDS_H */
