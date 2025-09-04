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
#ifndef NVSCISTREAM_API_H
#define NVSCISTREAM_API_H

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif
#include "nvscierror.h"
#include "nvscibuf.h"
#include "nvscisync.h"
#include "nvsciipc.h"
#include "nvscievent.h"
#include "nvscistream_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup nvscistream_blanket_statements NvSciStream Blanket Statements
 * Generic statements applicable for NvSciStream interfaces.
 * @ingroup nvsci_stream
 * @{
 */

/**
 * \page nvscistream_page_blanket_statements NvSciStream blanket statements
 * \section nvscistream_input_parameters Input parameters
 * - NvSciStreamPacket passed as input parameter to NvSciStreamPoolPacketInsertBuffer()
 *   or NvSciStreamPoolPacketDelete() API is valid if it is returned from a successful call to
 *   NvSciStreamPoolPacketCreate() API and has not been deleted using
 *   NvSciStreamPoolPacketDelete().
 * - NvSciStreamPacket passed as input parameter to NvSciStreamBlockPacketAccept() or
 *   NvSciStreamBlockElementAccept() or NvSciStreamProducerPacketPresent() or
 *   NvSciStreamConsumerPacketRelease() API is valid if it was received earlier through a
 *   NvSciStreamEventType_PacketCreate event and NvSciStreamEventType_PacketDelete
 *   event for the same NvSciStreamPacket is not yet received.
 * - NvSciStreamBlock passed as input parameter to an API is valid if it is returned from
 *   a successful call to any one of the block create APIs and has not yet been destroyed
 *   using NvSciStreamBlockDelete() unless otherwise stated explicitly.
 * - An array of NvSciSyncFence(s) passed as an input parameter to an API is valid if it is not
 *   NULL and each entry in the array holds a valid NvSciSyncFence. The NvSciSyncFence(s)
 *   in the array should be in the order according to the indices specified for the NvSciSyncObj(s)
 *   during NvSciStreamBlockSyncObject() API. That is, if the NvSciSyncObj is set with index == 1,
 *   its NvSciSyncFence should be the 2nd element in the array. The array should be empty
 *   if the synchronousOnly flag received through NvSciStreamEventType_SyncAttr from
 *   other endpoint is true.
 * - NvSciIpcEndpoint passed as input parameter to an API is valid if it is obtained
 *   from successful call to NvSciIpcOpenEndpoint() and has not yet been freed using
 *   NvSciIpcCloseEndpoint().
 * - Pointer to NvSciEventService passed as input parameter to an API is valid if the
 *   NvSciEventService instance is obtained from successful call to
 *   NvSciEventLoopServiceCreate() and has not yet been freed using NvSciEventService::Delete().
 * - The input parameter for which the valid value is not stated in the interface specification and not
 *   covered by any of the blanket statements, it is considered that the entire range of the parameter
 *   type is valid.
 *
 * \section nvscistream_out_params Output parameters
 * - Output parameters are passed by reference through pointers. Also, since a
 *   null pointer cannot be used to convey an output parameter, API functions return
 *   an error code if a null pointer is supplied for a required output parameter unless
 *   otherwise stated explicitly. Output parameter is valid only if error code returned
 *   by an API function is NvSciError_Success unless otherwise stated explicitly.
 *
 * \section nvscistream_return_values Return values
 * - Any initialization API that allocates memory (for example, Block creation
 *   APIs) to store information provided by the caller, will return
 *   NvSciError_InsufficientMemory if the allocation fails unless otherwise
 *   stated explicitly.
 * - Any API which takes an NvSciStreamBlock as input parameter returns
 *   NvSciError_StreamBadBlock error if the NvSciStreamBlock is invalid.
 *   (Note: Currently in transition from BadParameter)
 * - Any API which is only allowed for specific block types returns
 *   NvSciError_NotSupported if the NvSciStreamBlock input parameter
 *   does not support the operation.
 * - Any API which takes an NvSciStreamPacket as input parameter returns
 *   NvSciError_StreamBadPacket error if the NvSciStreamPacket is invalid.
 *   (Note: Currently in transition from BadParameter)
 * - All block creation interfaces returns NvSciError_StreamInternalError error if the
 *   block registration fails.
 * - Any element level interface which operates on a block other than the interfaces for
 *   block creation, NvSciStreamBlockConnect(), NvSciStreamBlockEventQuery(), and
 *   NvSciStreamBlockEventServiceSetup() will return NvSciError_StreamNotConnected error
 *   code if the producer block in the stream is not connected to every consumer block in
 *   the stream or the NvSciStreamEventType_Connected event from the block is not yet queried
 *   by the application by calling NvSciStreamBlockEventQuery() interface.
 * - If any API detects an inconsistency in internal data structures,
 *   it will return an NvSciError_StreamInternalError error code.
 * - If an API function is invoked with parameters such that two or more failure modes are
 *   possible, it is guaranteed that one of those failure modes will occur, but it is not
 *   guaranteed which.
 *
 * \section nvscistream_concurrency Concurrency
 * - Unless otherwise stated explicitly in the interface specifications,
 *   any two or more NvSciStream functions can be called concurrently
 *   without any side effects. The effect will be as if the functions were
 *   executed sequentially. The order of execution is not guaranteed.
 */

/**
 * @}
 */

/**
 * @defgroup nvsci_stream Streaming APIs
 *
 * The NvSciStream library is a layer on top of NvSciBuf and NvSciSync libraries
 * that provides utilities for streaming sequences of data packets between
 * multiple application modules to support a wide variety of use cases.
 *
 * @ingroup nvsci_group_stream
 * @{
 */

 /**
 * @defgroup nvsci_stream_apis NvSciStream APIs
 *
 * Methods to setup and stream sequences of data packets.
 *
 * @ingroup nvsci_stream
 * @{
 */

/*!
 * \brief Establishes connection between two blocks referenced
 *   by the given NvSciStreamBlock(s).
 *
 * - Connects an available output of one block with an available
 *   input of another block.
 *
 * - Each input and output can only have one connection. A stream is operational
 *   when all inputs and outputs of all blocks in the stream have a connection.
 *
 * <b>Preconditions</b>
 * - The upstream block has an available output connection.
 * - The downstream block has an available input connection.
 *
 * <b>Actions</b>
 * - Establish a connection between the two blocks.
 *
 * <b>Postconditions</b>
 * - When a block has a connection to producer as well as connection to
 *   every consumer(s) in the stream, it will receive a NvSciStreamEventType_Connected
 *   event.
 *
 * \param[in] upstream NvSciStreamBlock which references an upstream block.
 *   Valid value: A valid NvSciStreamBlock which does not reference
 *   a Pool or Queue block.
 * \param[in] downstream NvSciStreamBlock which references a downstream block.
 *   Valid value: A valid NvSciStreamBlock which does not reference
 *   a Pool or Queue block.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success The connection was made successfully.
 * - ::NvSciError_InsufficientResource block referenced by @a upstream argument has
 *     no available outputs or block referenced by @a downstream argument has
 *     no available inputs.
 * - ::NvSciError_AccessDenied: block referenced by @a upstream argument
 *     or block referenced by @a downstream argument(for example, Pool or Queue)
 *     does not allow explicit connection via NvSciStreamBlockConnect().
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Async
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamBlockConnect(
    NvSciStreamBlock const upstream,
    NvSciStreamBlock const downstream
);

/*! \brief Creates an instance of producer block, associates
 *     the given pool referenced by the given NvSciStreamBlock
 *     with it and returns a NvSciStreamBlock referencing the
 *     created producer block.
 *
 *  - Creates a block for the producer end of a stream. All streams
 *  require one producer block. A producer block must have a pool
 *  associated with it for managing available packets. Producer blocks have one
 *  output connection and no input connections.
 *
 *  - Once the stream is operational, this block can be used to exchange
 *  NvSciSync information with the consumer and NvSciBuf information with the pool.
 *
 *  <b>Preconditions</b>
 *  - None.
 *
 *  <b>Actions</b>
 *  - Creates a new instance of producer block.
 *  - Associates the given pool block with the producer block.
 *
 *  <b>Postconditions</b>
 *  - The block is ready to be connected to other stream blocks.
 *  - The block can be queried for events.
 *
 *  \param [in] pool NvSciStreamBlock which references a pool block
 *    to be associated with the producer block.
 *  \param [out] producer NvSciStreamBlock which references a
 *    new producer block.
 *
 *  \return ::NvSciError, the completion code of this operation.
 *  - ::NvSciError_Success A new producer block was set up successfully.
 *  - ::NvSciError_BadParameter If the output parameter @a producer
 *      is a null pointer or the @a pool parameter does not reference a
 *      pool block.
 *  - ::NvSciError_InsufficientResource If the pool block is already
 *      associated with another producer.
 *  - ::NvSciError_StreamInternalError The producer block cannot be
 *      initialized properly.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamProducerCreate(
    NvSciStreamBlock const pool,
    NvSciStreamBlock *const producer
);

/*! \brief Creates an instance of consumer block, associates
 *     the given queue block referenced by the given NvSciStreamBlock
 *     with it and returns a NvSciStreamBlock referencing the created
 *     consumer block.
 *
 *  - Creates a block for the consumer end of a stream. All streams
 *  require at least one consumer block. A consumer block must have
 *  a queue associated with it for managing pending packets. Consumer blocks have
 *  one input connection and no output connections.
 *
 *  - Once the stream is operational, this block can be used to exchange
 *  NvSciSync information with the producer and NvSciBuf information with the pool.
 *
 *  <b>Preconditions</b>
 *  - None.
 *
 *  <b>Actions</b>
 *  - Creates a new instance of consumer block.
 *  - Associates the given queue block with the consumer block.
 *
 *  <b>Postconditions</b>
 *  - The block is ready to be connected to other stream blocks.
 *  - The block can be queried for events.
 *
 *  \param [in] queue NvSciStreamBlock which references a queue
 *    block to be associated with the consumer block.
 *  \param [out] consumer NvSciStreamBlock which references a
 *    new consumer block.
 *
 *  \return ::NvSciError, the completion code of this operation.
 *  - ::NvSciError_Success A new consumer block was set up successfully.
 *  - ::NvSciError_BadParameter If the output parameter @a consumer
 *      is a null pointer or the @a queue parameter does not reference a
 *      queue block.
 *  - ::NvSciError_InsufficientResource If the queue block is already bound to
 *      another consumer.
 *  - ::NvSciError_StreamInternalError The consumer block cannot be
 *      initialized properly.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamConsumerCreate(
    NvSciStreamBlock const queue,
    NvSciStreamBlock *const consumer
);

/*!
 * \brief Creates an instance of static pool block and returns a
 *   NvSciStreamBlock referencing the created pool block.
 *
 * - Creates a block for management of a stream's packet pool. Every producer
 * must be associated with a pool block, provided at the producer's creation.
 *
 * - A static pool has a fixed number of packets which must be created and accepted
 * before the first free packet is acquired by the producer block.
 *
 * - Once the stream is operational and the application has determined
 * the packet requirements(packet element count and packet element buffer attributes),
 * this block can be used to register NvSciBufObj(s) to each packet.
 *
 * <b>Preconditions</b>
 * - None.
 *
 * <b>Actions</b>
 * - Allocates data structures to describe packets.
 * - Initializes queue of available packets.
 *
 * <b>Postconditions</b>
 * - The block is ready to be connected to other stream blocks.
 * - The block can be queried for events.
 *
 * \param[in] numPackets Number of packets.
 * \param[out] pool NvSciStreamBlock which references a
 *   new pool block.
 *
 * \return ::NvSciError, the completion code of this operation.
 *  - ::NvSciError_Success A new pool block was set up successfully.
 *  - ::NvSciError_BadParameter The output parameter @a pool is a null pointer.
 *  - ::NvSciError_StreamInternalError The pool block cannot be initialized properly.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamStaticPoolCreate(
    uint32_t const numPackets,
    NvSciStreamBlock *const pool
);

/*!
 * \brief Creates an instance of mailbox queue block and returns a
 *   NvSciStreamBlock referencing the created mailbox queue block.
 *
 * - Creates a block for managing the packets that are ready to be
 * acquired by the consumer. This is one of the two available types of queue
 * block. Every consumer must be associated with a queue block, provided at
 * the time the consumer is created.
 *
 * - A mailbox queue holds a single packet. If a new packet arrives, the packet
 * currently being held is replaced and returned to the pool for reuse.
 *
 * - This type of queue is intended for consumer applications
 * which don't need to process every packet and always wish to have the
 * latest input available.
 *
 * - Once connected, the application does not directly interact with this block.
 * The consumer block will communicate with the queue block to obtain new packets.
 *
 * <b>Preconditions</b>
 * - None.
 *
 * <b>Actions</b>
 * - Initialize a queue to hold a single packet for acquire.
 *
 * <b>Postconditions</b>
 * - The block is ready to be connected to other stream blocks.
 *
 * \param[out] queue NvSciStreamBlock which references a
 *   new mailbox queue block.
 *
 * \return ::NvSciError, the completion code of this operation.
 *  - ::NvSciError_Success A new mailbox queue block was set up successfully.
 *  - ::NvSciError_BadParameter The output parameter @a queue is a null pointer.
 *  - ::NvSciError_StreamInternalError The mailbox queue block cannot be initialized properly.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamMailboxQueueCreate(
    NvSciStreamBlock *const queue
);

/*!
 * \brief Creates an instance of FIFO queue block and returns a
 *   NvSciStreamBlock referencing the created FIFO queue block.
 *
 * - Creates a block for tracking the list of packets available to be
 * acquired by the consumer. This is one of the two available types of queue block.
 * Every consumer must be associated with a queue block, provided at the time the
 * consumer is created.
 *
 * - A FIFO queue can hold up to the complete set of packets created,
 * which will be acquired in the order they were presented.
 *
 * - This type of queue is intended for consumer applications which require
 *   processing of every packet that is presented.
 *
 * - Once connected, the application does not directly interact with this block.
 * The consumer block will communicate with it to obtain new packets.
 *
 * <b>Preconditions</b>
 * - None.
 *
 * <b>Actions</b>
 * - Initialize a queue to manage waiting packets.
 *
 * <b>Postconditions</b>
 * - The block is ready to be connected to other stream blocks.
 *
 * \param[out] queue NvSciStreamBlock which references a
 *   new FIFO queue block.
 *
 * \return ::NvSciError, the completion code of this operation.
 *  - ::NvSciError_Success A new FIFO queue block was set up successfully.
 *  - ::NvSciError_BadParameter The output parameter @a queue is a null pointer.
 *  - ::NvSciError_StreamInternalError The FIFO queue block cannot be initialized properly.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamFifoQueueCreate(
    NvSciStreamBlock *const queue
);

/*!
 * \brief Creates an instance of multicast block and returns a
 *   NvSciStreamBlock referencing the created multicast block.
 *
 * - Creates a block allowing for one input and one or more outputs.
 *
 * - Multicast block broadcasts messages sent from upstream to all of the
 * downstream blocks.
 *
 * - Mulitcast block aggregates messages of the same type from downstream into
 * a single message before sending it upstream.
 *
 * <b>Preconditions</b>
 * - None.
 *
 * <b>Actions</b>
 * - Initializes a block that takes one input and one or more outputs.
 *
 * <b>Postconditions</b>
 * - The block is ready to be connected to other stream blocks.
 *
 * \param[in] outputCount Number of output blocks that will be connected.
 *   Valid value: 1 to NvSciStreamQueryableAttrib_MaxMulticastOutputs
 *   attribute value queried by successful call to NvSciStreamAttributeQuery()
 *   API.
 * \param[out] multicast NvSciStreamBlock which references a
 *   new multicast block.
 *
 * \return ::NvSciError, the completion code of this operation.
 *  - ::NvSciError_Success A new multicast block was set up successfully.
 *  - ::NvSciError_BadParameter The output parameter @a multicast is a null
 *     pointer or @a outputCount is larger than the number allowed.
 *  - ::NvSciError_StreamInternalError The multicast block cannot be
 *    initialized properly.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamMulticastCreate(
    uint32_t const outputCount,
    NvSciStreamBlock *const multicast
);

/*!
 * \brief Creates an instance of IpcSrc block and returns a
 *   NvSciStreamBlock referencing the created IpcSrc block.
 *
 * - Creates the upstream half of an IPC block pair which allows
 * stream information to be transmitted between processes. If producer and
 * consumer(s) present in the same process, then there is no need for
 * IPC block pair.
 *
 * - IpcSrc block has one input connection and no output connection.
 *
 * - An IpcSrc block connects to downstream through the NvSciIpcEndpoint
 * used to create the block.
 *
 * <b>Preconditions</b>
 * - None.
 *
 * <b>Actions</b>
 * - Establishes connection through NvSciIpcEndpoint.
 *
 * <b>Postconditions</b>
 * - The block is ready to be connected to other stream blocks.
 *
 * \param[in] ipcEndpoint NvSciIpcEndpoint handle.
 * \param[in] syncModule NvSciSyncModule that is used to import a
 *        NvSciSyncAttrList across an IPC boundary. This must be the
 *        module associated with the NvSciSyncAttrList that is used in
 *        specifying the NvSciSyncObj waiter requirements.
 * \param[in] bufModule NvSciBufModule that is used to import a
 *        NvSciBufAttrList across an IPC boundary. This must be the
 *        module associated with the NvSciBufAttrList that is used in
 *        specifying the packet element information.
 * \param[out] ipc NvSciStreamBlock which references a
 *   new IpcSrc block.
 *
 * \return ::NvSciError, the completion code of this operation.
 *  - ::NvSciError_Success A new IpcSrc block was set up successfully.
 *  - ::NvSciError_StreamInternalError: If connection establishment through
 *      @a ipcEndpoint fails or IpcSrc block cannot be initialized properly.
 *  - ::NvSciError_BadParameter The output parameter @a ipc is a null pointer.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamIpcSrcCreate(
    NvSciIpcEndpoint const ipcEndpoint,
    NvSciSyncModule const syncModule,
    NvSciBufModule const bufModule,
    NvSciStreamBlock *const ipc
);

/*!
 * \brief Creates an instance of IpcSrc or C2CSrc block and returns a
 *   NvSciStreamBlock referencing the created block.
 *
 * - If input NvSciIpcEndpoint is of type IPC, then this API Creates
 * the upstream half of an IPC block pair which allows stream information
 * to be transmitted between processes.
 *
 * - If input NvSciIpcEndpoint is of type C2C, then this API Creates
 * the upstream half of an C2C block pair which allows stream information
 * to be transmitted between chips.
 *
 * - If input NvSciIpcEndpoint is of type C2C and queue is not provided by
 * application, driver creates and binds a default FIFO queue with C2CSrc block.
 * Note that this default queue block is not visible to application.
 *
 * If producer and consumer(s) are present in the same process, then there is
 * no need for IPC or C2C block pairs.
 *
 * - IpcSrc/C2CSrc block has one input connection and no output connection.
 *
 * - An IpcSrc/C2CSrc block connects to downstream through the NvSciIpcEndpoint
 * used to create the block.
 *
 * <b>Preconditions</b>
 * - None.
 *
 * <b>Actions</b>
 * - Establishes connection through NvSciIpcEndpoint.
 *
 * <b>Postconditions</b>
 * - The block is ready to be connected to other stream blocks.
 *
 * \param[in] ipcEndpoint NvSciIpcEndpoint handle.
 * \param[in] syncModule NvSciSyncModule that is used to import a
 *        NvSciSyncAttrList across an IPC boundary. This must be the
 *        module associated with the NvSciSyncAttrList that is used in
 *        specifying the NvSciSyncObj waiter requirements.
 * \param[in] bufModule NvSciBufModule that is used to import a
 *        NvSciBufAttrList across an IPC boundary. This must be the
 *        module associated with the NvSciBufAttrList that is used in
 *        specifying the packet element information.
 * \param[in] queue NvSciStreamBlock that is used to enqueue the Packets
 *        from the Producer and send those packets downstream to C2CDst
 *        block.
 * \param[out] ipc NvSciStreamBlock which references a
 *   new IpcSrc or C2CSrc block.
 *
 * \return ::NvSciError, the completion code of this operation.
 *  - ::NvSciError_Success A new IpcSrc or C2CSrc block was set up successfully.
 *  - ::NvSciError_StreamInternalError: If connection establishment through
 *      @a ipcEndpoint fails or IpcSrc or C2CSrc block cannot be initialized properly
 *      or default FIFO queue cannot be initialized properly when queue is not
 *      provided by user if the @a ipcEndpoint is C2C type.
 *  - ::NvSciError_BadParameter The output parameter @a ipc is a null pointer.
 *  - ::NvSciError_BadParameter The input parameter @a queue is a invalid and the
 *      @a ipcEndpoint is C2C type.
 *  - ::NvSciError_BadParameter The input parameter @a queue is a valid and the
 *      @a ipcEndpoint is IPC type.
 *  - ::NvSciError_InsufficientResource If the queue block is already
 *      associated with any other consumer or C2CSrc block.
 * -  ::NvSciError_NotInitialized @a ipcEndpoint is uninitialized.
 * -  ::NvSciError_NotSupported Not supported in provided endpoint backend type
 * -  ::NvSciError_NotSupported C2CSrc block not supported in this safety build.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamIpcSrcCreate2(
    NvSciIpcEndpoint const ipcEndpoint,
    NvSciSyncModule const syncModule,
    NvSciBufModule const bufModule,
    NvSciStreamBlock const queue,
    NvSciStreamBlock *const ipc
);

/*!
 * \brief Creates an instance of IpcDst block and returns a
 *   NvSciStreamBlock referencing the created IpcDst block.
 *
 * - Creates the downstream half of an IPC block pair which allows
 * stream information to be transmitted between processes.If producer and
 * consumer(s) present in the same process, then there is no need for
 * IPC block pair.
 *
 * - IpcDst block has one output connection and no input connection.
 *
 * - An IpcDst block connects to upstream through the NvSciIpcEndpoint used
 * to create the block.
 *
 * <b>Preconditions</b>
 * - None.
 *
 * <b>Actions</b>
 * - Establishes connection through NvSciIpcEndpoint.
 *
 * <b>Postconditions</b>
 * - The block is ready to be connected to other stream blocks.
 *
 * \param[in] ipcEndpoint NvSciIpcEndpoint handle.
 * \param[in] syncModule NvSciSyncModule that is used to import a
 *        NvSciSyncAttrList across an IPC boundary. This must be the
 *        module associated with the NvSciSyncAttrList that is used in
 *        specifying the NvSciSyncObj waiter requirements.
 * \param[in] bufModule NvSciBufModule that is used to import a
 *        NvSciBufAttrList across an IPC boundary. This must be the
 *        module associated with the NvSciBufAttrList that is used in
 *        specifying the packet element information.
 * \param[out] ipc NvSciStreamBlock which references a
 *   new IpcDst block.
 *
 * \return ::NvSciError, the completion code of this operation.
 *  - ::NvSciError_Success A new IpcDst block was set up successfully.
 *  - ::NvSciError_StreamInternalError: If connection establishment through
 *      @a ipcEndpoint fails or IpcDst block can't be initialized properly.
 *  - ::NvSciError_BadParameter The output parameter @a ipc is a null pointer.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamIpcDstCreate(
    NvSciIpcEndpoint const ipcEndpoint,
    NvSciSyncModule const syncModule,
    NvSciBufModule const bufModule,
    NvSciStreamBlock *const ipc
);

/*!
 * \brief Creates an instance of IpcDst or C2CDst block and returns a
 *   NvSciStreamBlock referencing the created block.
 *
 * - If input NvSciIpcEndpoint is of type IPC, then this API Creates
 * the downstream half of an IPC block pair which allows stream information
 * to be transmitted between processes.
 *
 * - If input NvSciIpcEndpoint is of type C2C, then this API Creates
 * the downstream half of an C2C block pair which allows stream information
 * to be transmitted between chips.
 *
 * If producer and consumer(s) are present in the same process, then there is
 * no need for IPC or C2C block pairs.
 *
 * - IpcDst/C2CDst block has one output connection and no input connection.
 *
 * - An IpcDst/C2CDst block connects to downstream through the NvSciIpcEndpoint
 * used to create the block.
 *
 * <b>Preconditions</b>
 * - None.
 *
 * <b>Actions</b>
 * - Establishes connection through NvSciIpcEndpoint.
 *
 * <b>Postconditions</b>
 * - The block is ready to be connected to other stream blocks.
 *
 * \param[in] ipcEndpoint NvSciIpcEndpoint handle.
 * \param[in] syncModule NvSciSyncModule that is used to import a
 *        NvSciSyncAttrList across an IPC boundary. This must be the
 *        module associated with the NvSciSyncAttrList that is used in
 *        specifying the NvSciSyncObj waiter requirements.
 * \param[in] bufModule NvSciBufModule that is used to import a
 *        NvSciBufAttrList across an IPC boundary. This must be the
 *        module associated with the NvSciBufAttrList that is used in
 *        specifying the packet element information.
 * \param[in] pool NvSciStreamBlock that is used to enqueue the Packets
 *        received from C2CSrc block and send those packets downstream to
 *     .  consumer block.
 * \param[out] ipc NvSciStreamBlock which references a
 *   new IpcDst or C2CDst block.
 *
 * \return ::NvSciError, the completion code of this operation.
 *  - ::NvSciError_Success A new IpcDst or C2CDst block was set up successfully.
 *  - ::NvSciError_StreamInternalError: If connection establishment through
 *      @a ipcEndpoint fails or IpcDst or C2CDst block cannot be initialized properly.
 *  - ::NvSciError_BadParameter The output parameter @a ipc is a null pointer.
 *  - ::NvSciError_BadParameter The input parameter @a pool is a invalid and the
 *      @a ipcEndpoint is C2C type.
 *  - ::NvSciError_BadParameter The input parameter @a pool is a valid and the
 *      @a ipcEndpoint is IPC type.
 *  - ::NvSciError_InsufficientResource If the pool block is already
 *      associated with any other producer or C2CDst block.
 * -  ::NvSciError_NotInitialized @a ipcEndpoint is uninitialized.
 * -  ::NvSciError_NotSupported Not supported in provided endpoint backend type
 * -  ::NvSciError_NotSupported C2CSrc block not supported in this safety build.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamIpcDstCreate2(
    NvSciIpcEndpoint const ipcEndpoint,
    NvSciSyncModule const syncModule,
    NvSciBufModule const bufModule,
    NvSciStreamBlock const pool,
    NvSciStreamBlock *const ipc
);

/*!
* \brief Creates an instance of Limiter block and returns a
*   NvSciStreamBlock referencing the created Limiter block.
*
* - Creates a block to limit the number of packets allowed to be sent
*   downstream to a consumer block.
*
* - A limiter block can be inserted anywhere in the stream between the
*   Producer and Consumer Blocks, but its primary intent is to be inserted
*   between a Multicast block and a Consumer.
*
* <b>Preconditions</b>
* - None.
*
* <b>Actions</b>
* - Creates a new instance of Limiter block.
*
* <b>Postconditions</b>
* - The block is ready to be connected to other stream blocks.
*
* \param[in] maxPackets Number of packets allowed to be sent downstream
*   to a consumer block.
* \param[out] limiter NvSciStreamBlock which references a
*   new limiter block.
*
* \return ::NvSciError, the completion code of this operation.
*  - ::NvSciError_Success A new Limiter block was set up successfully.
*  - ::NvSciError_BadParameter The output parameter @a limiter is a null
*      pointer.
*  - ::NvSciError_StreamInternalError The Limiter block can't be initialized
*      properly.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
*/
NvSciError NvSciStreamLimiterCreate(
    uint32_t const maxPackets,
    NvSciStreamBlock *const limiter
);


/*!
* \brief Creates an instance of ReturnSync block and returns a
*   NvSciStreamBlock referencing the created ReturnSync block.
*
* - Creates a block to wait for the fences for the packets received
*   from consumer before sending them upstream.
*
* - A ReturnSync block can be inserted anywhere in the stream between the
*   Producer and Consumer Blocks, but its primary intent is to be inserted
*   between a limiter block and a Consumer.
*
* <b>Preconditions</b>
* - None.
*
* <b>Actions</b>
* - Creates a new instance of ReturnSync block.
*
* <b>Postconditions</b>
* - The block is ready to be connected to other stream blocks.
*
* \param[in] syncModule NvSciSyncModule that will be used
*   during fence wait operations.
* \param[out] returnSync NvSciStreamBlock which references a
*   new ReturnSync block.
*
* \return ::NvSciError, the completion code of this operation.
*  - ::NvSciError_Success A new ReturnSync block was set up successfully.
*  - ::NvSciError_BadParameter The output parameter @a returnSync is a null
*      pointer.
*  - ::NvSciError_StreamInternalError The ReturnSync block can't be initialized
*      properly.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
*/
NvSciError NvSciStreamReturnSyncCreate(
    NvSciSyncModule const syncModule,
    NvSciStreamBlock *const returnSync
);

/*!
* \brief Creates an instance of PresentSync block and returns a
*   NvSciStreamBlock referencing the created PresentSync block.
*
* - Creates a block to wait for the fences for the packets received
*   from producer before sending them downstream.
*
* - The primary usecase is to insert PresentSync block between producer
*   and C2CSrc block.
*
* <b>Preconditions</b>
* - None.
*
* <b>Actions</b>
* - Creates a new instance of PresentSync block.
*
* <b>Postconditions</b>
* - The block is ready to be connected to other stream blocks.
*
* \param[in] syncModule NvSciSyncModule that will be used
*   during fence wait operations.
* \param[out] presentSync NvSciStreamBlock which references a
*   new PresentSync block.
*
* \return ::NvSciError, the completion code of this operation.
*  - ::NvSciError_Success A new PresentSync block was set up successfully.
*  - ::NvSciError_BadParameter The output parameter @a presentSync is a null
*      pointer.
*  - ::NvSciError_StreamInternalError The PresentSync block can't be initialized
*      properly.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
*/
NvSciError NvSciStreamPresentSyncCreate(
    NvSciSyncModule const syncModule,
    NvSciStreamBlock *const presentSync
);

/*!
 * \brief Queries for the next event from block referenced by the given
 *   NvSciStreamBlock, optionally waiting when the an event is not yet
 *   available, then removes the event from the priority queue and returns
 *   the event type to the caller. Additional information about the event
 *   can be queried through the appropriate block functions.
 *
 * If the block is set up to use NvSciEventService, applications should call
 *   this API with zero timeout after waking up from waiting on the
 *   NvSciEventNotifier obtained from NvSciStreamBlockEventServiceSetup()
 *   interface, and applications should query all the events in the block
 *   after waking up. Wake up due to spurious events is possible, and in
 *   that case calling this function will return no event.
 *
 * <b>Preconditions</b>
 * - The block to query has been created.
 *
 * <b>Actions</b>
 * - If the block is not set up to use NvSciEventService, wait until
 *   an event is pending on the block or the specified timeout period
 *   is reached, whichever comes first.
 * - Retrieves the next event (if any) from the block, returning the
 *   NvSciStreamEventType.
 *
 * <b>Postconditions</b>
 * - As defined for each NvSciStreamEventType, dequeuing an event
 *   may allow other operations on the block to proceed.
 *
 * \param[in] block NvSciStreamBlock which references a block.
 * \param[in] timeoutUsec Timeout in microseconds (-1 to wait forever).
 * \param[out] event Location in which to return the NvSciStreamEventType.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success The output argument @a event is filled with the
 *     queried event data.
 * - ::NvSciError_Timeout The @a timeoutUsec period was reached before an
 *     event became available.
 * - ::NvSciError_BadParameter The output argument @a event is null, or
 *     @a timeoutUsec is not zero if the block has been set up to use
 *     NvSciEventService.
 * - ::NvSciError_InvalidState If no more references can be taken on
 *     the NvSciSyncObj or NvSciBufObj while duping the objects.
 * - Any error/panic behavior that NvSciSyncAttrListClone() and NvSciBufAttrListClone()
 *   can generate.
 * - Panics if NvSciSyncObj or NvSciBufObj is invalid while duping the objects.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: Yes
 *   - De-Init: No
 */
NvSciError NvSciStreamBlockEventQuery(
    NvSciStreamBlock const block,
    int64_t const timeoutUsec,
    NvSciStreamEventType *const eventType
);

/*!
 * \brief Queries the error code for an error event.
 *
 * If multiple errors occur before this query, only return the first error
 * code. All subsequent errors are ignored until the error code is queried,
 * with the assumption that they are side effects of the first one.
 *
 * <b>Preconditions</b>
 * None
 *
 * <b>Actions</b>
 * None
 *
 * <b>Postconditions</b>
 * None
 *
 * \param[in] block NvSciStreamBlock which references a block.
 * \param[out] status Pointer to location in which to store the error code.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_BadAddress: @a status is NULL.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: Yes
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockErrorGet(
    NvSciStreamBlock const block,
    NvSciError* const status
);

/*!
 * \brief Queries the number of consumers downstream of the block referenced
 *   by the given NvSciStreamBlock (or 1 if the block itself is a consumer).
 *
 * Used primarily by producer to determine the number of signaller sync
 *   objects and corresponding fences that must be waited for before
 *   reusing packet buffers.
 *
 * <b>Preconditions</b>
 * - Block must have received NvSciStreamEventType_Connected event indicating
 *   that the stream is fully connected.
 *
 * <b>Actions</b>
 * - None.
 *
 * <b>Postconditions</b>
 * - None.
 *
 * \param[in] block: NvSciStreamBlock which references a block.
 * \param[out] numConsumers: Pointer to location in which to store the count.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Count successfully retrieved.
 * - ::NvSciError_BadAddress: @a numConsumers is NULL.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockConsumerCountGet(
    NvSciStreamBlock const block,
    uint32_t* const numConsumers
);

/*!
 * \brief Indicates a group of setup operations identified by @a setupType
 *   on @a block are complete. The relevant information will be transmitted
 *   to the rest of the stream, triggering events and becoming available
 *   for query where appropriate.
 *
 * <b>Preconditions</b>
 * - See descriptions for specific NvSciStreamSetup enum values.
 *
 * <b>Actions</b>
 * - See descriptions for specific NvSciStreamSetup enum values.
 *
 * <b>Postconditions</b>
 * - See descriptions for specific NvSciStreamSetup enum values.
 *
 * \param[in] block: NvSciStreamBlock which references a block.
 * \param[in] setupType: Identifies the group of setup operations whose status
 *   is to be updated.
 * \param[in] completed: Provided for future support of dynamically
 *   reconfigurable streams. Currently must always be true.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_NotYetAvailable: Prerequisites for marking the group
 *   identified by @a setupType as complete have not been met.
 * - ::NvSciError_InsufficientData: Not all data associated with the group
 *   identified by @a setupType has been provided.
 * - ::NvSciError_AlreadyDone: The group identified by @a setupType has
 *   already been marked complete.
 * - ::NvSciError_BadParameter: @a completed is not true.
 * - ::NvSciError_Busy: An NvSciStream interface in another thread is
 *   currently interacting with the group identified by @a setupType on
 *   this @a block. The call can be tried again, but this typically
 *   indicates a flaw in application design.
 * - ::NvSciError_InsufficientMemory: Failed to allocate memory needed to
 *   process the data.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Async
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockSetupStatusSet(
    NvSciStreamBlock const block,
    NvSciStreamSetup const setupType,
    bool const completed
);

/*!
 * \brief Adds an element with the specified @a userType and @a bufAttrList
 *   to the list of elements defined by @a block.
 *
 * When called on a producer or consumer block, it adds to the list of
 *   elements which that endpoint is capable of supporting.
 * When called on a pool block, it adds to the list of elements which will
 *   be used for the final packet layout. This interface is not supported on
 *   the secondary pools.
 *
 * <b>Preconditions</b>
 * - The block must be in the phase where element export is available.
 *   For producer and consumer blocks, this begins after receiving the
 *   NvSciStreamEventType_Connected event. For pool blocks, this begins
 *   after receiving the NvSciStreamEventType_Elements event. In all cases,
 *   this phase ends after a call to NvSciStreamBlockSetupStatusSet() with
 *   NvSciStreamSetup_ElementExport.
 *
 * <b>Actions</b>
 * - Appends the specified element to the current list.
 *
 * <b>Postconditions</b>
 * - None.
 *
 * \param[in] block: NvSciStreamBlock which references a block.
 * \param[in] userType: User-defined type to identify the element.
 * \param[in] bufAttrList: Buffer attribute list for element. NvSciStream
 *   will clone the attribute list, so the caller can safely free it once
 *   the function returns.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_NotYetAvailable: The prerequisite event has not yet arrived.
 * - ::NvSciError_NoLongerAvailable: The element export phase was completed.
 * - ::NvSciError_Busy: An NvSciStream interface in another thread is
 *   currently interacting with the block's exported element information.
 *   The call can be retried, but this typically indicates a flaw in
 *   application design.
 * - ::NvSciError_Overflow: The number of elements in the list has reached
 *   the maximum allowed value.
 * - ::NvSciError_AlreadyInUse: An element with the specified @a userType
 *   already exists in the list.
 * - ::NvSciError_InsufficientMemory: Unable to allocate storage for the new
 *   element.
 * - Any error that NvSciBufAttrListClone() can generate.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockElementAttrSet(
    NvSciStreamBlock const block,
    uint32_t const userType,
    NvSciBufAttrList const bufAttrList
);

/*!
 * \brief Queries the number of elements received at the @a block from the
 *   source identified by @a queryBlockType.
 *
 * When called on a producer or consumer block, @a queryBlockType must be
 *   NvSciStreamBlockType_Pool, and the query is for the list of allocated
 *   elements sent from the pool.
 * When called on a primary pool block, @a queryBlockType may be either
 *   NvSciStreamBlockType_Producer or NvSciStreamBlockType_Consumer, and
 *   the query is for the list of supported elements sent from the referenced
 *   endpoint(s).
 * When called on a secondary pool block, @a queryBlockType may be either
 *   NvSciStreamBlockType_Producer or NvSciStreamBlockType_Consumer.
 *   If @a queryBlockType is NvSciStreamBlockType_Producer, the query is for
 *   the list of allocated elements sent from the primary pool.
 *   If @a queryBlockType is NvSciStreamBlockType_Consumer, the query is for
 *   referenced the list of supported elements sent from consumer(s).
 *
 * <b>Preconditions</b>
 * - Block must be in the phase where element import is available, after
 *   receiving an NvSciStreamEventType_Elements event and before a call to
 *   NvSciStreamBlockSetupStatusSet() with NvSciStreamSetup_ElementImport.
 *
 * <b>Actions</b>
 * - None.
 *
 * <b>Postconditions</b>
 * - None.
 *
 * \param[in] block: NvSciStreamBlock which references a block.
 * \param[in] queryBlockType: Identifies the source of the element list.
 * \param[out] numElements: Pointer to location in which to store the count.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_BadAddress: @a numElements is NULL.
 * - ::NvSciError_BadParameter: The @a queryBlockType is not valid for
 *   the @a block.
 * - ::NvSciError_NotYetAvailable: The requested element information has
 *   not yet arrived.
 * - ::NvSciError_NoLongerAvailable: The element import phase was completed.
 * - ::NvSciError_Busy: An NvSciStream interface in another thread is
 *   currently interacting with the block's imported element information.
 *   The call can be retried.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockElementCountGet(
    NvSciStreamBlock const block,
    NvSciStreamBlockType const queryBlockType,
    uint32_t* const numElements
);

/*!
 * \brief Queries the user-defined type and/or buffer attribute list for
 *   an entry, referenced by @a elemIndex, in list of elements received at
 *   the @a block from the source identified by @a queryBlockType.
 *   At least one of @a userType or @a bufAttrList must be non-NULL, but
 *   if the caller is only interested in one of the values, the other
 *   can be NULL.
 *
 * When called on a producer or consumer block, @a queryBlockType must be
 *   NvSciStreamBlockType_Pool, and the query is for the list of allocated
 *   elements sent from the pool.
 * When called on a primary pool block, @a queryBlockType may be either
 *   NvSciStreamBlockType_Producer or NvSciStreamBlockType_Consumer, and
 *   the query is for the list of supported elements sent from the referenced
 *   endpoint(s).
 * When called on a secondary pool block, @a queryBlockType may be either
 *   NvSciStreamBlockType_Producer or NvSciStreamBlockType_Consumer.
 *   If @a queryBlockType is NvSciStreamBlockType_Producer, the query is for
 *   the list of allocated elements sent from the primary pool.
 *   If @a queryBlockType is NvSciStreamBlockType_Consumer, the query is for
 *   referenced the list of supported elements sent from consumer(s).
 *
 * <b>Preconditions</b>
 * - Block must be in the phase where element import is available, after
 *   receiving an NvSciStreamEventType_Elements event and before a call to
 *   NvSciStreamBlockSetupStatusSet() with NvSciStreamSetup_ElementImport.
 *
 * <b>Actions</b>
 * - None.
 *
 * <b>Postconditions</b>
 * - None.
 *
 * \param[in] block: NvSciStreamBlock which references a block.
 * \param[in] queryBlockType: Identifies the source of the element list.
 * \param[in] elemIndex: Index of the entry in the element list to query.
 * \param[out] userType: Pointer to location in which to store the type.
 * \param[out] bufAttrList: Pointer to location in which to store the
 *   attribute list. The caller owns the attribute list handle received
 *   and should free it when it is no longer needed.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_BadAddress: @a userType and @a bufAttrList are both NULL.
 * - ::NvSciError_BadParameter: The @a queryBlockType is not valid for
 *   the @a block or the @a elemIndex is out of range.
 * - ::NvSciError_NotYetAvailable: The requested element information has
 *   not yet arrived.
 * - ::NvSciError_NoLongerAvailable: The element import phase was completed.
 * - ::NvSciError_Busy: An NvSciStream interface in another thread is
 *   currently interacting with the block's imported element information.
 *   The call can be retried.
 * - Any error that NvSciBufAttrListClone() can generate.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockElementAttrGet(
    NvSciStreamBlock const block,
    NvSciStreamBlockType const queryBlockType,
    uint32_t const elemIndex,
    uint32_t* const userType,
    NvSciBufAttrList* const bufAttrList
);

/*!
 * \brief Indicates whether the entry at @a elemIndex in the list of allocated
 *   elements will be used by the @a block. By default, all entries are
 *   are assumed to be used, so this is only necessary if an entry will not
 *   be used.
 *
 * This is only supported for consumer blocks. Producers are expected to
 *   provide all elements defined by the pool.
 *
 * For any elements that a consumer indicates it will not use, any waiter
 *   NvSciSyncAttrLists that it provides (or fails to provide) will be
 *   ignored when consolidating with other attribute lists and passing to
 *   the producer. Additionally, NvSciStream may optimize buffer resources
 *   and not share the element buffers with the consumer.
 *
 * <b>Preconditions</b>
 * - Block must be in the phase where element import is available, after
 *   receiving an NvSciStreamEventType_Elements event and before a call to
 *   NvSciStreamBlockSetupStatusSet() with NvSciStreamSetup_ElementImport.
 *
 * <b>Actions</b>
 * - Marks the element as unused by the consumer.
 *
 * <b>Postconditions</b>
 * - None.
 *
 * \param[in] block: NvSciStreamBlock which references a block.
 * \param[in] elemIndex: Index of the entry in the element list to set.
 * \param[in] used: Flag indicating whether the element is used.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_NotYetAvailable: The requested element information has
 *   not yet arrived.
 * - ::NvSciError_NoLongerAvailable: The element import phase was completed.
 * - ::NvSciError_BadParameter: The @a elemIndex is out of range.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockElementUsageSet(
    NvSciStreamBlock const block,
    uint32_t const elemIndex,
    bool const used
);

/*!
 * \brief Creates a new packet and adds it to the pool block
 *   referenced by the given NvSciStreamBlock, associates the
 *   given NvSciStreamCookie with the packet and returns a
 *   NvSciStreamPacket which references the created packet.
 *
 * <b>Preconditions</b>
 * - Pool must have successfully completed NvSciStreamSetup_ElementExport
 *   phase.
 * - For static pool, the number of packets already created has not reached the
 *   number of packets which was set when the static pool was created.
 *
 * <b>Actions</b>
 * - A new NvSciStreamPacket is assigned to the created packet
 *   and returned to the caller.
 *
 * <b>Postconditions</b>
 * - The application can register NvSciBufObj(s) to the created packet.
 *
 * \param[in] pool: NvSciStreamBlock which references a pool block.
 * \param[in] cookie: Pool's NvSciStreamCookie for the packet.
 *   Valid value: cookie != NvSciStreamCookie_Invalid.
 * \param[out] handle: NvSciStreamPacket which references the
 *   created packet.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: The operation completed successfully.
 * - ::NvSciError_BadAddress: @a handle is NULL.
 * - ::NvSciError_NotYetAvailable: Completion of element export has not
 *     yet been signaled on the pool.
 * - ::NvSciError_StreamBadCookie: @a cookie is invalid.
 * - ::NvSciError_AlreadyInUse: @a cookie is already assigned to a packet.
 * - ::NvSciError_Overflow: Pool already has maximum number of packets.
 * - ::NvSciError_InsufficientMemory: Unable to allocate memory for the new
 *     packet.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamPoolPacketCreate(
    NvSciStreamBlock const pool,
    NvSciStreamCookie const cookie,
    NvSciStreamPacket *const handle
);

/*!
 * \brief Registers an NvSciBufObj as the indexed element of the referenced
 *   NvSciStreamPacket owned by the pool block.
 *
 * <b>Preconditions</b>
 * - The packet has not yet been marked as completed.
 *
 * <b>Actions</b>
 * - The NvSciBufObj is registered to the given packet element.
 *
 * <b>Postconditions</b>
 * None
 *
 * \param[in] pool NvSciStreamBlock which references a pool block.
 * \param[in] handle NvSciStreamPacket which references the packet.
 * \param[in] index Index of element within packet.
 *   Valid value: 0 to number of elements specifed - 1.
 * \param[in] bufObj NvSciBufObj to be registered.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: The operation completed successfully.
 * - ::NvSciError_BadParameter: @a bufObj is NULL.
 * - ::NvSciError_IndexOutOfRange: @a index is out of range.
 * - ::NvSciError_NoLongerAvailable: The packet has been marked complete.
 * - Any error or panic behavior returned by NvSciBufObjRef().
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamPoolPacketInsertBuffer(
    NvSciStreamBlock const pool,
    NvSciStreamPacket const handle,
    uint32_t const index,
    NvSciBufObj const bufObj
);

/*!
 * \brief Marks a packet as complete and sends it to the rest of the stream.
 *
 * <b>Preconditions</b>
 * - The packet has not yet been marked as completed.
 *
 * <b>Actions</b>
 * - The packet is complete.
 *
 * <b>Postconditions</b>
 * - Status for the packet can be received.
 *
 * \param[in] pool NvSciStreamBlock which references a pool block.
 * \param[in] handle NvSciStreamPacket which references the packet.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: The operation completed successfully.
 * - ::NvSciError_AlreadyDone: The packet has already been marked complete.
 * - ::NvSciError_InsufficientData: Buffers were not provided for all of the
 *     packet's elements.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Async
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamPoolPacketComplete(
    NvSciStreamBlock const pool,
    NvSciStreamPacket const handle
);

/*!
 * \brief Removes a packet referenced by the given NvSciStreamPacket from
 *   the pool block referenced by the given NvSciStreamBlock.
 *
 * If the packet is currently in the pool, it is removed right away. Otherwise
 * this is deferred until the packet returns to the pool.
 *
 * <b>Preconditions</b>
 * - Specified packet must exist.
 *
 * <b>Actions</b>
 * - If the pool holds the specified packet or when it is returned to the
 *   pool, the pool releases resources associated with it and sends a
 *   NvSciStreamEventType_PacketDelete event to the producer and
 *   consumer endpoint(s).
 *
 * <b>Postconditions</b>
 * - The packet may no longer be used for pool operations.
 *
 * \param[in] pool NvSciStreamBlock which references a pool block.
 * \param[in] handle NvSciStreamPacket which references the packet.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success Packet successfully removed.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Async
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamPoolPacketDelete(
    NvSciStreamBlock const pool,
    NvSciStreamPacket const handle
);

/*!
 * \brief In producer and consumer blocks, queries the handle of a newly
 *   defined packet.
 *
 * <b>Preconditions</b>
 * - Must follow receipt of a NvSciStreamEventType_PacketCreate event for
 *   which the handle has not yet been retrieved.
 *
 * <b>Actions</b>
 * - Dequeues the pending handle.
 *
 * <b>Postconditions</b>
 * - The packet can now be queried and accepted or rejected.
 *
 * \param[in] block NvSciStreamBlock which references a producer or
 *  consumer block.
 * \param[out] handle Location in which to return the packet handle.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_BadAddress: @a handle is NULL.
 * - ::NvSciError_NotYetAvailable: The block has not yet indicated it is
 *     done importing element information.
 * - ::NvSciError_NoLongerAvailable: The block has completed importing packets.
 * - ::NvSciError_NoStreamPacket:: There is no pending new packet handle.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockPacketNewHandleGet(
    NvSciStreamBlock const block,
    NvSciStreamPacket* const handle
);

/*!
 * \brief In producer and consumer blocks, queries an indexed buffer from
 *   a packet. The resulting NvSciBufObj is owned by the caller and should
 *   be freed when it is no longer needed.
 *
 * <b>Preconditions</b>
 * - Block must not have rejected the packet, and must still be in packet
 *   import phase.
 *
 * <b>Actions</b>
 * - Returns an NvSciBufObj owned by the caller.
 *
 * <b>Postconditions</b>
 * None
 *
 * \param[in] block NvSciStreamBlock which references a producer or
 *  consumer block.
 * \param[in] handle NvSciStreamPacket which references the packet.
 * \param[in] elemIndex Index of the element to query.
 * \param[out] bufObj Location in which to store the buffer object handle.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_BadAddress: @a bufObj is NULL.
 * - ::NvSciError_NotYetAvailable: The block has not yet indicated it is
 *     done importing element information.
 * - ::NvSciError_NoLongerAvailable: The block has completed importing packets.
 * - ::NvSciError_IndexOutOfRange: @a elemIndex is out of range.
 * - Any error or panic behavior returned by NvSciBufObjRef().
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockPacketBufferGet(
    NvSciStreamBlock const block,
    NvSciStreamPacket const handle,
    uint32_t const elemIndex,
    NvSciBufObj* const bufObj
);

/*!
 * \brief In producer and consumer blocks, queries the cookie of a recently
 *   deleted packet.
 *
 * <b>Preconditions</b>
 * - Must follow receipt of a NvSciStreamEventType_PacketDelete event for
 *   which the cookie has not yet been retrieved.
 *
 * <b>Actions</b>
 * - Dequeues the pending cookie and frees packet resources.
 *
 * <b>Postconditions</b>
 * - The packet's handle can no longer be used for any operations on the block.
 *
 * \param[in] block NvSciStreamBlock which references a producer or
 *  consumer block.
 * \param[out] cookie Location in which to return the packet cookie.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_BadAddress: @a cookie is NULL.
 * - ::NvSciError_NotYetAvailable: The block has not yet indicated it is
 *     done importing element information.
 * - ::NvSciError_NoStreamPacket:: There is no pending deleted packet cookie.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockPacketOldCookieGet(
    NvSciStreamBlock const block,
    NvSciStreamCookie* const cookie
);

/*!
 * \brief In producer and consumer blocks, either accepts a packet, providing
 *   a cookie to be used in subsequent operations, or rejects it, providing
 *   an error value to be reported to the pool.
 *
 * <b>Preconditions</b>
 * - The packet has not yet been accepted or rejected.
 *
 * <b>Actions</b>
 * - Binds the cookie, if any, to the packet, and informs the pool of
 *   the status.
 *
 * <b>Postconditions</b>
 * - If rejected, the packet handle can no longer be used for any operations.
 *
 * \param[in] block NvSciStreamBlock which references a producer or
 *  consumer block.
 * \param[in] handle NvSciStreamPacket which references the packet.
 * \param[in] cookie Cookie to assign to the packet if it is accepted.
 * \param[in] status Status value which is either NvSciError_Success to
 *   indicate acceptance, or any other value to indicate rejection.
 *   (But NvSciError_StreamInternalError is not allowed.)
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_BadParameter: @a status is invalid.
 * - ::NvSciError_StreamBadCookie: The packet was accepted but @a cookie is
 *     invalid.
 * - ::NvSciError_AlreadyInUse: The packet was accepted but @a cookie was
 *     already assigned to another packet.
 * - ::NvSciError_AlreadyDone: The packet's status was already set.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Async
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockPacketStatusSet(
    NvSciStreamBlock const block,
    NvSciStreamPacket const handle,
    NvSciStreamCookie const cookie,
    NvSciError const status
);

/*!
 * \brief In pool, queries whether or not a packet has been accepted.
 *
 * <b>Preconditions</b>
 * None
 *
 * <b>Actions</b>
 * None
 *
 * <b>Postconditions</b>
 * None
 *
 * \param[in] block NvSciStreamBlock which references a pool block.
 * \param[in] handle NvSciStreamPacket which references the packet.
 * \param[out] accepted Location in which to return the acceptance.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_BadAddress: @a accepted is NULL.
 * - ::NvSciError_NotYetAvailable: Packet status has not yet arrived.
 * - ::NvSciError_NoLongerAvailable: Packet export phase has completed.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamPoolPacketStatusAcceptGet(
    NvSciStreamBlock const pool,
    NvSciStreamPacket const handle,
    bool* const accepted
);

/*!
 * \brief In pool, queries the status value for a given packet returned
 *   by a specified endpoint. Used when a packet is rejected to learn
 *   more about which endpoint(s) rejected it and why.
 *
 * <b>Preconditions</b>
 * None
 *
 * <b>Actions</b>
 * None
 *
 * <b>Postconditions</b>
 * None
 *
 * \param[in] block NvSciStreamBlock which references a pool block.
 * \param[in] handle NvSciStreamPacket which references the packet.
 * \param[in] queryBlockType Indicates whether to query status from
 *   producer or consumer endpoint.
 * \param[in] queryBlockIndex Index of the endpoint from which to query status.
 * \param[out] status Location in which to return the status value.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_BadAddress: @a status is NULL.
 * - ::NvSciError_NotYetAvailable: Packet status has not yet arrived.
 * - ::NvSciError_NoLongerAvailable: Packet export phase has completed.
 * - ::NvSciError_IndexOutOfRange: @a queryBlockIndex is out of range.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamPoolPacketStatusValueGet(
    NvSciStreamBlock const pool,
    NvSciStreamPacket const handle,
    NvSciStreamBlockType const queryBlockType,
    uint32_t const queryBlockIndex,
    NvSciError* const status
);

/*!
 * \brief Specifies @a block's NvSciSync requirements to be able to wait
 *   for sync objects provided by the opposing endpoint(s) for the element
 *   referenced by @a elemIndex. By default, the value is NULL, indicating
 *   the opposing endpoint(s) must write or read the element synchronously,
 *   unless this endpoint marked it as unused.
 *
 * This is only supported for producer and consumer blocks.
 *
 * <b>Preconditions</b>
 * - Block must be in the phase where waiter attr export is available, after
 *   receiving an NvSciStreamEventType_Elements event and before a call to
 *   NvSciStreamBlockSetupStatusSet() with NvSciStreamSetup_WaiterAttrExport.
 *
 * <b>Actions</b>
 * - Saves the attribute list for the element.
 *
 * <b>Postconditions</b>
 * - None.
 *
 * \param[in] block: NvSciStreamBlock which references a block.
 * \param[in] elemIndex: Index of the entry in the attribute list to set.
 * \param[in] waitSyncAtttList: Attribute list with waiter requirments.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_NotYetAvailable: Element information has not yet arrived.
 * - ::NvSciError_NoLongerAvailable: The waiter info export phase was
 *     completed.
 * - ::NvSciError_IndexOutOfRange: The @a elemIndex is out of range.
 * - Any error or panic behavior that NvSciSyncAttrListClone() can generate.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockElementWaiterAttrSet(
    NvSciStreamBlock const block,
    uint32_t const elemIndex,
    NvSciSyncAttrList const waitSyncAttrList
);

/*!
 * \brief Retrieves the opposing endpoints' NvSciSync requirements to be
 *   be able to wait for sync objects signalled by @a block for the element
 *   referenced by @a elemIndex. If the value is NULL, the opposing endpoint
 *   expects this block to write or read the element synchronously, and not
 *   provide any sync object. The received attribute list is owned by the
 *   caller, and should be freed when it is no longer needed.
 *
 * This is only supported for producer and consumer blocks.
 *
 * <b>Preconditions</b>
 * - Block must be in the phase where waiter attr import is available, after
 *   receiving an NvSciStreamEventType_WaiterAttr event and before a call to
 *   NvSciStreamBlockSetupStatusSet() with NvSciStreamSetup_WaiterAttrImport.
 *
 * <b>Actions</b>
 * None
 *
 * <b>Postconditions</b>
 * None
 *
 * \param[in] block NvSciStreamBlock which references a block.
 * \param[in] elemIndex: Index of the entry in the attribute list to get.
 * \param[in] waitSyncAtttList: Location in which to return the requirements.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_BadAddress: @a waitSyncAttrList is NULL.
 * - ::NvSciError_NotYetAvailable: Waiter information has not yet arrived.
 * - ::NvSciError_NoLongerAvailable: The waiter info import phase was
 *     completed.
 * - ::NvSciError_IndexOutOfRange: The @a elemIndex is out of range.
 * - Any error or panic behavior that NvSciSyncAttrListClone() can generate.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockElementWaiterAttrGet(
    NvSciStreamBlock const block,
    uint32_t const elemIndex,
    NvSciSyncAttrList* const waitSyncAttrList
);

/*!
 * \brief Specifies @a block's NvSciSync object used to signal when it is
 *   done writing or reading a buffer referenced by @a elemIndex. By default,
 *   the value is NULL, indicating that the buffer is either unused or is
 *   used synchronously and all operations will have completed by the time
 *   the endpoint returns the packet to the stream.
 *
 * This is only supported for producer and consumer blocks.
 *
 * <b>Preconditions</b>
 * - Block must be in the phase where signal object export is available, after
 *   receiving an NvSciStreamEventType_WaiterAttr event and before a call to
 *   NvSciStreamBlockSetupStatusSet() with NvSciStreamSetup_SignalObjExport.
 *
 * <b>Actions</b>
 * - Saves the sync object for the element.
 *
 * <b>Postconditions</b>
 * - None.
 *
 * \param[in] block: NvSciStreamBlock which references a block.
 * \param[in] elemIndex: Index of the entry in the sync object list to set.
 * \param[in] signalSyncObj: Signalling sync object.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_NotYetAvailable: Waiter requirements have not yet arrived.
 * - ::NvSciError_NoLongerAvailable: The signal info export phase was
 *     completed.
 * - ::NvSciError_IndexOutOfRange: The @a elemIndex is out of range.
 * - Any error or panic behavior that NvSciSyncObjRef() can generate.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockElementSignalObjSet(
    NvSciStreamBlock const block,
    uint32_t const elemIndex,
    NvSciSyncObj const signalSyncObj
);

/*!
 * \brief Retrieves an opposing endpoint's NvSciSync object which it uses
 *   to signal when it is done writing or readying a buffer referenced by
 *   @a elemIndex. If the value is NULL, the opposing endpoint either does
 *   not use the buffer or uses it synchronously, and all operations on
 *   the buffer will be completed by the time the packet is received.
 *   The received sync object is owned by the caller, and should be freed
 *   when it is no longer needed.
 *
 * This is only supported for producer and consumer blocks.
 *
 * <b>Preconditions</b>
 * - Block must be in the phase where signal object import is available, after
 *   receiving an NvSciStreamEventType_SignalObj event and before a call to
 *   NvSciStreamBlockSetupStatusSet() with NvSciStreamSetup_SignalObjImport.
 *
 * <b>Actions</b>
 * None
 *
 * <b>Postconditions</b>
 * None
 *
 * \param[in] block NvSciStreamBlock which references a block.
 * \param[in] queryBlockIndex: The index of the opposing block to query.
 *   When querying producer sync objects from the consumer, this is always 0.
 *   When querying consumer sync objects from the producer, this is the
 *   index of the consumer.
 * \param[in] elemIndex: Index of the entry in the sync object list to get.
 * \param[in] signalSyncObj: Location in which to return the sync object.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_BadAddress: @a signalSyncObj is NULL.
 * - ::NvSciError_NotYetAvailable: Signal information has not yet arrived.
 * - ::NvSciError_NoLongerAvailable: The signal info import phase was
 *     completed.
 * - ::NvSciError_IndexOutOfRange: The @a queryBlockIndex or @a elemIndex
 *   is out of range.
 * - Any error or panic behavior that NvSciSyncObjRef() can generate.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockElementSignalObjGet(
    NvSciStreamBlock const block,
    uint32_t const queryBlockIndex,
    uint32_t const elemIndex,
    NvSciSyncObj* const signalSyncObj
);

/*!
 * \brief Instructs the producer referenced by @a producer to retrieve
 *   a packet from the pool.
 *
 * If a packet is available for producer processing, its producer fences
 *   will be cleared, ownership will be moved to the application, and its
 *   cookie will be returned.
 *
 * The producer may hold multiple packets and is not required to present
 *   them in the order they were obtained.
 *
 * <b>Preconditions</b>
 * - All packets have been accepted by the producer and the consumer(s).
 * - The producer block must have received the NvSciStreamEventType_PacketReady
 *   event from pool for processing the next available packet.
 *
 * <b>Actions</b>
 * - Retrieves an available packet for producer processing and returns
 *   it to the caller.
 *
 * <b>Postconditions</b>
 * - Packet is held by the producer application.
 *
 * \param[in] producer NvSciStreamBlock which references a producer block.
 * \param[out] cookie Location in which to return the packet's cookie.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success Packet successfully retrieved.
 * - ::NvSciError_BadAddress: @a cookie is NULL.
 * - ::NvSciError_NoStreamPacket: No packet is available.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
NvSciError NvSciStreamProducerPacketGet(
    NvSciStreamBlock const producer,
    NvSciStreamCookie *const cookie
);

/*!
 * \brief Instructs the producer referenced by @a producer to insert the
 *   packet referenced by @a handle into the stream for consumer processing.
 *
 * <b>Preconditions</b>
 * - The packet must be held by the producer application.
 *
 * <b>Actions</b>
 * - The packet is sent downstream, where it will be processed according
 *   to the stream's configuration.
 *
 * <b>Postconditions</b>
 * - The packet is no longer held by the producer application.
 *
 * \param[in] producer NvSciStreamBlock which references a producer block.
 * \param[in] handle NvSciStreamPacket which references the packet.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success Packet successfully presented.
 * - ::NvSciError_StreamBadPacket @a handle is not recognized.
 * - ::NvSciError_StreamPacketInaccessible @a handle is not currently
 *   held by the application.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Async
 * - Required privileges: None
 * - API group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
NvSciError NvSciStreamProducerPacketPresent(
    NvSciStreamBlock const producer,
    NvSciStreamPacket const handle
);

/*!
 * \brief Instructs the consumer referenced by @a consumer to retrieve
 *   a packet from the queue.
 *
 * If a packet is available for consumer processing, its consumer fences
 *   will be cleared, ownership will be moved to the application, and its
 *   cookie will be returned.
 *
 * The consumer may hold multiple packets and is not required to return
 *   them in the order they were obtained.
 *
 * <b>Preconditions</b>
 * - All packets have been accepted by the producer and the consumer(s).
 * - The consumer block must have received the NvSciStreamEventType_PacketReady
 *   event from queue for processing the next available packet.
 *
 * <b>Actions</b>
 * - Retrieves an available packet for consumer processing and returns
 *   it to the caller.
 *
 * <b>Postconditions</b>
 * - Packet is held by the consumer application.
 *
 * \param[in] consumer NvSciStreamBlock which references a consumer block.
 * \param[out] cookie NvSciStreamCookie identifying the packet.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success Packet was successfully acquired.
 * - ::NvSciError_BadAddress: @a cookie is NULL.
 * - ::NvSciError_NoStreamPacket: No packet is available.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
NvSciError NvSciStreamConsumerPacketAcquire(
    NvSciStreamBlock const consumer,
    NvSciStreamCookie *const cookie
);

/*!
 * \brief Instructs the consumer referenced by @a consumer to release the
 *   packet referenced by @a handle into the stream for producer reuse.
 *
 * <b>Preconditions</b>
 * - The packet must be held by the consumer application.
 *
 * <b>Actions</b>
 * - The packet is sent upstream, where it will be processed according
 *   to the stream's configuration.

 * <b>Postconditions</b>
 * - The packet is no longer held by the consumer application.
 *
 * \param[in] consumer NvSciStreamBlock which references a consumer block.
 * \param[in] handle NvSciStreamPacket which references the packet.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success Packet was successfully released.
 * - ::NvSciError_StreamBadPacket @a handle is not recognized.
 * - ::NvSciError_StreamPacketInaccessible @a handle is not currently
 *   held by the application.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Async
 * - Required privileges: None
 * - API group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
NvSciError NvSciStreamConsumerPacketRelease(
    NvSciStreamBlock const consumer,
    NvSciStreamPacket const handle
);

/*!
 * \brief Sets the postfence which indicates when the application
 *   controlling will be done operating on the indexed buffer of
 *   a packet.
 *
 * <b>Preconditions</b>
 * - The packet must be held by the application.
 *
 * <b>Actions</b>
 * - The fence is saved in the packet until it is returned to the stream.

 * <b>Postconditions</b>
 * None
 *
 * \param[in] block NvSciStreamBlock which references a producer or
 *   consumer block.
 * \param[in] handle NvSciStreamPacket which references the packet.
 * \param[in] elemIndex Index of the buffer for the fence.
 * \param[in] postfence The fence to save.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success The operation was completed successfully.
 * - ::NvSciError_BadAddress: @a postfence is NULL.
 * - ::NvSciError_StreamBadPacket @a handle is not recognized.
 * - ::NvSciError_StreamPacketInaccessible @a handle is not currently
 *   held by the application.
 * - ::NvSciError_IndexOutOfRange @a elemIndex is not valid.
 * - Any error returned by NvSciSyncFenceDup().
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockPacketFenceSet(
    NvSciStreamBlock const block,
    NvSciStreamPacket const handle,
    uint32_t const elemIndex,
    NvSciSyncFence const *const postfence
);

/*!
 * \brief Retrieves the prefence which indicates when the the indexed
 *   opposing endpoint will be done operating on the indexed buffer
 *   of a packet.
 *
 * <b>Preconditions</b>
 * - The packet must be held by the application.
 *
 * <b>Actions</b>
 * - The fence is copied from the packet.

 * <b>Postconditions</b>
 * None
 *
 * \param[in] block NvSciStreamBlock which references a producer or
 *   consumer block.
 * \param[in] handle NvSciStreamPacket which references the packet.
 * \param[in] queryBlockIndex Index of the opposing block to query.
 *   When querying producer fences from the consumer, this is always 0.
 *   When querying consumer fences from the producer, this is the
 *   index of the consumer.
 * \param[in] elemIndex Index of the buffer for the fence.
 * \param[out] prefence Location in which to return the fence.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success The operation was completed successfully.
 * - ::NvSciError_BadAddress: @a prefence is NULL.
 * - ::NvSciError_StreamBadPacket @a handle is not recognized.
 * - ::NvSciError_StreamPacketInaccessible @a handle is not currently
 *   held by the application.
 * - ::NvSciError_IndexOutOfRange @a queryBlockIndex or @a elemIndex is
 *   not valid.
 * - Any error returned by NvSciSyncFenceDup() or NvSciSyncIpcImportFence().
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: No
 *   - Runtime: Yes
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockPacketFenceGet(
    NvSciStreamBlock const block,
    NvSciStreamPacket const handle,
    uint32_t const queryBlockIndex,
    uint32_t const elemIndex,
    NvSciSyncFence* const prefence
);

/*!
 * \brief Schedules a block referenced by the given NvSciStreamBlock
 * for destruction, disconnecting the block if this
 * hasn't already occurred.
 *
 * - The block's handle may no longer be used for any function calls.
 *
 * - Resources associated with the block may not be freed immediately.
 *
 * - Any pending packets downstream of the destroyed block will
 * still be available for the consumer to acquire.
 *
 * - No new packets upstream of the destroyed block can be presented. Once packets
 * are released, they will be freed.
 *
 * <b>Preconditions</b>
 * - None.
 *
 * <b>Actions</b>
 * - The block is scheduled for destruction.
 * - A NvSciStreamEventType_Disconnected event is sent to all upstream
 *   and downstream blocks, if they haven't received one already.
 *
 * <b>Postconditions</b>
 * - The referenced NvSciStreamBlock is no longer valid.
 * - If there is an NvSciEventNotifier bound to the referenced
 *   NvSciStreamBlock, it is unbound.
 *
 * \param[in] block NvSciStreamBlock which references a block.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success Block successfully destroyed.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Async
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamBlockDelete(
    NvSciStreamBlock const block
);

/*!
 * \brief Queries the value of one of the NvSciStreamQueryableAttrib.
 *
 * <b>Preconditions</b>
 * - None.
 *
 * <b>Actions</b>
 * - NvSciStream looks up the value for the given NvSciStreamQueryableAttrib
 *   and returns it.
 *
 * <b>Postconditions</b>
 * - None.
 *
 * \param[in] attr NvSciStreamQueryableAttrib to query.
 * \param[out] value The value queried.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success Query is successful.
 * - ::NvSciError_BadParameter @a attr is invalid or @a value is null.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamAttributeQuery(
    NvSciStreamQueryableAttrib const attr,
    int32_t *const value
);

/*!
 * Sets up the NvSciEventService on a block referenced by the given
 * NvSciStreamBlock by creating an NvSciEventNotifier to report the occurrence
 * of any events on that block. The NvSciEventNotifier is bound to the input
 * NvSciEventService and NvSciStreamBlock. Users can wait for events on the
 * block using the NvSciEventService API and then retrieve event details
 * using NvSciStreamBlockEventQuery(). Binding one or more blocks in a stream
 * to an NvSciEventService is optional. If not bound to an NvSciEventService,
 * users may instead wait for events on a block by specifying a non-zero
 * timeout in NvSciStreamBlockEventQuery(). If blocks in the same stream within
 * the same process are bound to different NvSciEventService, behavior is
 * undefined. The user is responsible for destroying the NvSciEventNotifier when
 * it's no longer needed.
 *
 * <b>Preconditions</b>
 * - After the input block is created, before calling this API, no NvSciStream
 *   API shall be called on the block.
 *
 * <b>Actions</b>
 * - Sets up the input block to use the input NvSciEventService for event
 * signaling.
 * - Creates an NvSciEventNotifier object and returns the pointer to the object
 * via @a eventNotifier.
 *
 * <b>Postconditions</b>
 * - NvSciStreamBlockEventQuery() calls with non-zero timeout on the block will
 * return error.
 *
 * \param[in] block NvSciStreamBlock which references a block.
 * \param[in] eventService Pointer to a NvSciEventService object.
 * \param[out] eventNotifier To be filled with the pointer to the created
 *  NvSciEventNotifier object.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success The setup is successful.
 * - ::NvSciError_BadParameter @a eventService is null or @a eventNotifier
 *   is null.
 * - ::NvSciError_InvalidState An NvSciStream API has already been called on the
 *   block referenced by @a block.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError NvSciStreamBlockEventServiceSetup(
    NvSciStreamBlock const block,
    NvSciEventService  *const eventService,
    NvSciEventNotifier **const eventNotifier
);

/*!
 * \brief Provides user-defined information at the producer and consumer with
 *   the specified @a userType, which can be queried by other blocks after the
 *   stream is fully connected.
 *
 * When called on a producer or consumer block, it adds to its list of
 *   user-defined information.
 *
 * <b>Preconditions</b>
 * - The block is created but not connected to any other block.
 *
 * <b>Actions</b>
 * - Appends the specified user-defined information to the information list.
 *
 * <b>Postconditions</b>
 * - None.
 *
 * \param[in] block: NvSciStreamBlock which references a block.
 * \param[in] userType: User-defined type to identify the endpoint information.
 * \param[in] dataSize: Size of the provided @a data.
 * \param[in] data: Pointer to user-defined information.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_BadParameter: @a data is a NULL pointer.
 * - ::NvSciError_AlreadyInUse: An endpoint information with the specified
 *   @a userType already exists in the list.
 * - ::NvSciError_InsufficientMemory: Unable to allocate storage for the new
 *   endpoint information.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockUserInfoSet(
    NvSciStreamBlock const block,
    uint32_t const userType,
    uint32_t const dataSize,
    void const* const data);

/*!
 * \brief Queries the user-defined information with @a userType in list of
 *   endpoint information at the @a block from the source identified by
 *   @a queryBlockType and @a queryBlockIndex.
 *
 * The @a dataSize contains the size of the input @a data. If @a data is NULL,
 *   store the total size of the queried information in @a dataSize. If not,
 *   copy the queried information into @a data and store the size of the data
 *   copied into @a data.
 *
 * <b>Preconditions</b>
 * - Block must have received NvSciStreamEventType_Connected event indicating
 *   that the stream is fully connected and before receiving
 *   NvSciStreamEventType_SetupComplete event indicating setup of the stream
 *   is complete.
 *
 * <b>Actions</b>
 * - None.
 *
 * <b>Postconditions</b>
 * - None.
 *
 * \param[in] block: NvSciStreamBlock which references a block.
 * \param[in] queryBlockType: Indicates whether to query information from
 *                            producer or consumer endpoint.
 * \param[in] queryBlockIndex: Index of the endpoint block to query.
 * \param[in] userType: User-defined type to query.
 * \param[in,out] dataSize: On input, contains the size of the memory
 *                          referenced by @a data. On output, contains the
 *                          amount of data copied into @a data, or the total
 *                          size of the queried information if @a data is NULL.
 * \param[out] data: Pointer to location in which to store the information.
 *
 * \return ::NvSciError, the completion code of this operation.
 * - ::NvSciError_Success: Operation completed successfully.
 * - ::NvSciError_BadAddress: @a dataSize is NULL or @a data is NULL when
 *                            @a dataSize is non-zero.
 * - ::NvSciError_NoLongerAvailable: Setup of the stream is completed.
 * - ::NvSciError_BadParameter: The @a queryBlockType is not valid.
 * - ::NvSciError_IndexOutOfRange: The @a queryBlockIndex is invalid for the
 *     @a queryBlockType.
 * - ::NvSciError_StreamInfoNotProvided: The queried endpoint info not exist.
 *
 * @usage
 * - Allowed context for the API call
 *   - Interrupt handler: No
 *   - Signal handler: No
 *   - Thread-safe: Yes
 *   - Re-entrant: No
 *   - Async/Sync: Sync
 * - Required privileges: None
 * - API group
 *   - Init: Yes
 *   - Runtime: No
 *   - De-Init: No
 */
NvSciError
NvSciStreamBlockUserInfoGet(
    NvSciStreamBlock const block,
    NvSciStreamBlockType const queryBlockType,
    uint32_t const queryBlockIndex,
    uint32_t const userType,
    uint32_t* const dataSize,
    void* const data);

#ifdef __cplusplus
}
#endif
/** @} */
/** @} */
#endif /* NVSCISTREAM_API_H */
