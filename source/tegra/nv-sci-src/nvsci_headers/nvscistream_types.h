/*
 * Copyright (c) 2020-2022 NVIDIA Corporation. All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and
 * proprietary rights in and to this software, related documentation and any
 * modifications thereto. Any use, reproduction, disclosure or distribution
 * of this software and related documentation without an express license
 * agreement from NVIDIA Corporation is strictly prohibited.
 */
/**
 * @file
 *
 * @brief <b> NVIDIA Software Communications Interface (SCI) : NvSciStream </b>
 *
 * The NvSciStream library is a layer on top of NvSciBuf and NvSciSync libraries
 * to provide utilities for streaming sequences of data packets between
 * multiple application modules to support a wide variety of use cases.
 */
#ifndef NVSCISTREAM_TYPES_H
#define NVSCISTREAM_TYPES_H

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

#include "nvscierror.h"
#include "nvscibuf.h"
#include "nvscisync.h"
#include "nvsciipc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Temporary comment out some c2c code, which can be re-enable after
 *   swtiching the C2C block to use NvSciEvent model
 */
#define C2C_EVENT_SERVICE 0

/**
 * @defgroup nvsci_stream_data_types NvSciStream Data Types
 *
 * Contains a list of NvSciStream datatypes.
 *
 * @ingroup nvsci_stream
 * @{
 */

/*! \brief Handle to a block.*/
typedef uintptr_t NvSciStreamBlock;

/*! \brief NvSciStream assigned handle for a packet.*/
typedef uintptr_t NvSciStreamPacket;

/*! \brief Application assigned cookie for a
 *   NvSciStreamPacket.
 */
typedef uintptr_t NvSciStreamCookie;

/*! \brief Constant variable denoting an invalid
 *   NvSciStreamPacket.
 *
 *  \implements{19620996}
 */
static const NvSciStreamPacket NvSciStreamPacket_Invalid = 0U;

/*! \brief Constant variable denoting an invalid
 *   NvSciStreamCookie.
 *
 *  \implements{19620999}
 */
static const NvSciStreamCookie NvSciStreamCookie_Invalid = 0U;

/**
 * \cond Non-doxygen comment
 * page nvsci_stream_logical_data_types NvSciStream logical data types
 * section NvSciStream logical data types
 *  Block: A block is a modular portion of a stream which resides
 *  in a single process and manages one aspect of the stream's
 *  behavior. Blocks are connected together in a tree to form
 *  arbitrary streams.
 *
 *  NvSciStream supports the following types of blocks:
 *    - Producer: Producer is a type of block responsible for
 *      generating stream data. Each stream begins with a producer
 *      block, it is also referred to as the upstream endpoint of the
 *      stream.
 *
 *    - Consumer: Consumer is a type of block responsible for
 *      processing stream data. Each stream ends with one or
 *      more consumer blocks, it is also referred to as the
 *      downstream endpoint of the stream.
 *
 *    - Pool: Pool is a type of block containing the set of
 *      packets available for use by @a Producer. NvSciStream
 *      supports only a static pool in which the number of
 *      packets managed by the pool is fixed when the pool
 *      is created.
 *
 *    - Queue: Queue is a type of block containing set of
 *      packets available for use by @a Consumer.
 *      NvSciStream supports two types of queue blocks:
 *         - Fifo: A Fifo Queue block is used when all
 *           packets must be acquired by the consumer in
 *           the order received. Packets will wait in the FIFO
 *           until the consumer acquires them.
 *         - Mailbox: Mailbox Queue block is used when the consumer
 *           should acquire the most recent data. If a new
 *           packet is inserted in the mailbox when one
 *           is already waiting, the previous one will be skipped
 *           and its buffers will immediately be returned to the
 *           Producer for reuse.
 *
 *    - Multicast: Multicast is a type of block which is responsible
 *      for connecting separate pipelines when a stream has more than
 *      one Consumer.
 *
 *    - IpcSrc - IpcSrc is a type of block which is the upstream
 *      half of an IPC block pair which allows NvSciSyncObj waiter
 *      requirements, NvSciSyncObj(s), packet element
 *      information and packets to be transmitted to or received from
 *      the downstream half of the stream which resides in another
 *      process.
 *
 *    - IpcDst - IpcDst is a type of block which is the downstream
 *      half of an IPC block pair which allows NvSciSyncObj waiter
 *      requirements, NvSciSyncObj(s), packet element
 *      information and packets to be transmitted to or received from
 *      the upstream half of the stream which resides in another process.
 *
 *  Packet: A packet represents a set of NvSciBufObjs containing stream
 *  data, each NvSciBufObj it contains is also referred to as an element
 *  of the packet.
 * \endcond
 */

/*! \brief Defines NvSciStream attributes that are queryable.
 *
 *  \implements{19621074}
 */
typedef enum {
    /*! \brief Maximum number of elements allowed per packet. */
    NvSciStreamQueryableAttrib_MaxElements          = 0x000000,
    /*! \brief Maximum number of NvSciSyncObjs allowed. */
    NvSciStreamQueryableAttrib_MaxSyncObj           = 0x000001,
    /*! \brief Maximum number of multicast outputs allowed. */
    NvSciStreamQueryableAttrib_MaxMulticastOutputs  = 0x000002
} NvSciStreamQueryableAttrib;

/*! \brief Most queries on one block request information received from
 *!   another block. Where there is ambiguity in the kind of block the
 *!   information originated from, this type is used to specify it.
 */
typedef enum {
    /*! \brief Query information received from the producer */
    NvSciStreamBlockType_Producer,
    /*! \brief Query information received from a consumer */
    NvSciStreamBlockType_Consumer,
    /*! \brief Query information received from the pool */
    NvSciStreamBlockType_Pool
} NvSciStreamBlockType;

/*! \brief Setup information will be broken into several distinct groups,
 *!   with restrictions on when they can be specified and queried. The
 *!   application will indicate when each group has been fully specified
 *!   and is ready to send (for *Export groups) or when it has finished
 *!   querying the group and the driver can reclaim space and allow dependent
 *!   operations to proceed (for *Import groups).
 */
typedef enum {
    /*! \brief Specification of element-related support.
     *
     * In the producer and consumer(s), this group contains the list of
     *   elements supported by the endpoint. Marking this complete causes
     *   the list to be sent to the pool.
     *
     * In the pool, this group contains the list of elements which will
     *   be used for the final packet layout. Marking this complete causes
     *   the list to be sent to the producer and consumer(s). This group of
     *   operations are not supported on the secondary pool.
     *
     * When element export is complete, the following functions are no longer
     *   available on the block:
     *   - NvSciStreamBlockElementAttrSet()
     *
     * When element export is complete, the following functions become
     *   available on the pool:
     *   - NvSciStreamPoolPacketCreate()
     *   - NvSciStreamPoolPacketInsertBuffer()
     *   - NvSciStreamPoolPacketComplete()
     *   - NvSciStreamPoolPacketStatusAcceptGet()
     *   - NvSciStreamPoolPacketStatusValueGet()
     */
    NvSciStreamSetup_ElementExport    = 0x0001,

    /*! \brief Processing of element-related support
     *
     * In the pool, this group contains the lists of supported elements sent by
     *   the producer and consumer(s). Marking this complete releases resources
     *   associated with these lists. This must be completed before element
     *   export can complete.
     *
     * In the producer and consumer(s), this group contains the list of
     *   elements sent by the pool for use in the final packet layout, as
     *   well as the flags tracking whether the block uses each element (for
     *   consumers only). Marking this complete releases resources associated
     *   with the element list, sends the usage list through the stream for
     *   use in optimization, and unblocks any pending
     *   NvSciStreamEventType_PacketCreate events waiting at the block.
     *
     * When element import is complete, the following functions are no longer
     *   available on the block:
     *   - NvSciStreamBlockElementCountGet()
     *   - NvSciStreamBlockElementAttrGet()
     *   - NvSciStreamBlockElementUsageSet()
     */
    NvSciStreamSetup_ElementImport    = 0x0002,

    /*! \brief Specification of all packets
     *
     * In the pool, this group contains the lists of all packet definitions.
     *   Marking this complete releases resources associated with the packet
     *   setup, and indicates no more packets will be created. The producer
     *   and consumer will receive an NvSciStreamEventType_PacketsComplete
     *   event after receiving all of the individual packets.
     *
     * When packet export is completed, the following functions are no longer
     *   available on the pool:
     *   - NvSciStreamPoolPacketCreate()
     *   - NvSciStreamPoolPacketInsertBuffer()
     *   - NvSciStreamPoolPacketComplete()
     */
    NvSciStreamSetup_PacketExport     = 0x0011,

    /*! \brief Mapping of all packets
     *
     * In the producer and consumer, this group contains the lists of all
     *   packet definitions and their status. This cannot be completed
     *   until the NvSciStreamEventType_PacketsComplete event arrives from
     *   the pool and status has been returned for all received packets.
     *
     * In the pool, this group contains the packet status returned by
     *   the producer and consumers. This cannot be completed until
     *   packet export has finished and NvSciStreamEventType_PacketStatus
     *   events have arrived for all packets.
     *
     * Marking this complete releases resources associated with the packet
     *   setup.
     *
     * When packet import is completed, the following functions are no longer
     *   available on the relevant blocks:
     *   - NvSciStreamBlockPacketNewHandleGet()
     *   - NvSciStreamBlockPacketBufferGet()
     *   - NvSciStreamBlockPacketStatusSet()
     *   - NvSciStreamPoolPacketStatusAcceptGet()
     *   - NvSciStreamPoolPacketStatusValueGet()
     */
    NvSciStreamSetup_PacketImport     = 0x0012,

    /*! \brief Specification of waiter sync attributes
     *
     * In the producer and consumer, this group contains the per-element
     *   NvSciSync attribute lists containing the requirements to wait for
     *   sync objects signalled by the opposing endpoints. This cannot be
     *   completed until NvSciStreamSetup_ElementImport is completed, so
     *   the list of used elements is known.
     *
     * When waiter information export is completed, the following functions
     *   are no longer available:
     *   - NvSciStreamBlockElementWaiterAttrSet()
     */
    NvSciStreamSetup_WaiterAttrExport = 0x0021,

    /*! \brief Processing of waiter sync attributes
     *
     * In the producer and consumer, this group contains the per-element
     *   NvSciSync attribute lists containing the requirements provided
     *   by the opposing endpoints so that they can wait for sync objects
     *   signalled by this endpoint. This cannot be completed until
     *   NvSciStreamSetup_ElementImport is completed and the
     *   NvSciStreamEventType_WaiterAttr event has arrived.
     *
     * When waiter information import is completed, the following functions
     *   are no longer available:
     *   - NvSciStreamBlockElementWaiterAttrGet()
     */
    NvSciStreamSetup_WaiterAttrImport = 0x0022,

    /*! \brief Specification of signaling sync objects
     *
     * In the producer and consumer, this group contains the per-element
     *   NvSciSync objects used to signal when writing and reading,
     *   respectively, of each element has completed. This cannot be
     *   completed until NvSciStreamSetup_WaiterAttrImport is completed.
     *
     * When signal information export is completed, the following functions
     *   are no longer available:
     *   - NvSciStreamBlockElementSignalObjSet()
     */
    NvSciStreamSetup_SignalObjExport  = 0x0031,

    /*! \brief Mapping of signaling sync objects
     *
     * In the producer and consumer, this group contains the per-element
     *   NvSciSync objects that are signalled when the opposing endpoint(s)
     *   are done reading and writing, respectively, each element. This cannot
     *   be completed until the NvSciStreamEventType_SignalObj event has
     *   arrived.
     *
     * When waiter information import is completed, the following functions
     *   are no longer available:
     *   - NvSciStreamBlockElementSignalObjGet()
     */
    NvSciStreamSetup_SignalObjImport  = 0x0032

} NvSciStreamSetup;

/*! \brief Defines event types for the blocks.
 *
 *  \implements{19621083}
 */
typedef enum {

    /*! \brief
     *  Indicates the stream containing the block is fully connected.
     *
     *  At all blocks, the following functions become available:
     *    - NvSciStreamBlockConsumerCountGet()
     *
     *  At the producer and consumer blocks, the element export phase may
     *    begin, and the following functions become available:
     *    - NvSciStreamBlockElementAttrSet()
     */
    NvSciStreamEventType_Connected                  = 0x004004,

    /*! \brief
     *  Indicates portions of the stream have disconnected such that no
     *    more useful work can be done with the block. Note that this
     *    event is not always triggered immediately when any disconnect
     *    occurs. For instance:
     *    - If a consumer still has packets waiting in its queue when
     *      a producer is destroyed, it will not be informed of the
     *      disconnection until all packets are acquired
     *      by calling NvSciStreamConsumerPacketAcquire().
     *
     *  Received by all blocks.
     */
    NvSciStreamEventType_Disconnected               = 0x004005,

    /*! \brief
     *  Signals the arrival of NvSciSync waiter information from the
     *    opposing endpoint(s).
     *
     *  Received by producer and consumer blocks.
     *
     *  The following function becomes available:
     *    - NvSciStreamBlockElementWaiterAttrGet()
     *    - NvSciStreamBlockElementSignalObjSet()
     */
    NvSciStreamEventType_WaiterAttr                 = 0x004013,

    /*! \brief
     *  Signals the arrival of NvSciSync signal information from the
     *    opposing endpoint(s).
     *
     *  Received by producer and consumer blocks.
     *
     *  The following function becomes available:
     *    - NvSciStreamBlockElementSignalObjGet()
     */
    NvSciStreamEventType_SignalObj                  = 0x004014,

    /*! \brief
     *  Signals the arrival of all element-related information.
     *
     *  At the pool, both the element import and export phases may begin,
     *    and the following functions become available:
     *    - NvSciStreamBlockElementCountGet()
     *    - NvSciStreamBlockElementAttrGet()
     *    - NvSciStreamBlockElementAttrSet()
     *
     *  At the producer and consumer(s), the element import and waiter
     *    information export phases may begin, and the following functions
     *    become available:
     *    - NvSciStreamBlockElementCountGet()
     *    - NvSciStreamBlockElementAttrGet()
     *    - NvSciStreamBlockElementUsageSet() (consumer only)
     *    - NvSciStreamBlockElementWaiterAttrSet()
     *
     *  Not received by any other block types.
     */
    NvSciStreamEventType_Elements                   = 0x004026,

    /*! \brief
     *  Signals the arrival of a new packet definition from the pool.
     *
     *  Received by producer and consumer blocks.
     *
     *  These events become available to the producer and consumer after
     *    they have indicated that they are done importing element
     *    information by calling NvSciStreamBlockSetupStatusSet()
     *    with NvSciStreamSetup_ElementImport. The following functions
     *    become available to query and accept or reject packet information:
     *    - NvSciStreamBlockPacketNewHandleGet()
     *    - NvSciStreamBlockPacketBufferGet()
     *    - NvSciStreamBlockPacketStatusSet()
     */
    NvSciStreamEventType_PacketCreate               = 0x004030,

    /*! \brief
     *  Signals that the pool has finished defining all of its packets.
     *
     *  Received by producer and consumer blocks.
     *
     *  This event becomes available to the producer and consumer after
     *    the pool indicates it has sent all the packets.
     */
    NvSciStreamEventType_PacketsComplete            = 0x004038,

    /*! \brief
     *  Signals that the pool has deleted a packet and no further paylaods
     *    using the packet will arrive.
     *
     *  Received by producer and consumer blocks.
     *
     *  The following function becomes available to query the deleted packet:
     *    - NvSciStreamBlockPacketOldCookieGet()
     */
    NvSciStreamEventType_PacketDelete               = 0x004032,

    /*! \brief
     *  Signals the arrival of status for a packet from producer and all
     *    consumers.
     *
     *  Received by pool blocks.
     *
     *  The following functions become available to query the packet status:
     *    - NvSciStreamPoolPacketStatusAcceptGet()
     *    - NvSciStreamPoolPacketStatusValueGet()
     */
    NvSciStreamEventType_PacketStatus               = 0x004037,

    /*! \brief
     *  Specifies a packet is available for reuse or acquire.
     *
     *  Received by producer and consumer blocks.
     */
    NvSciStreamEventType_PacketReady                = 0x004040,

    /*! \brief
     *  Specifies all setup operations have completed and streaming may begin.
     *
     *  Received by all blocks.
     */
    NvSciStreamEventType_SetupComplete              = 0x004050,

    /*! \brief
     *  Indicates a failure not directly triggered by user action.
     *
     *  Received by any block.
     */
    NvSciStreamEventType_Error                      = 0x0040FF

} NvSciStreamEventType;

#ifdef __cplusplus
}
#endif
/** @} */
#endif /* NVSCISTREAM_TYPES_H */
