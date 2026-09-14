// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2019-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * mapping between nvmap_hnadle and sci_ipc entery
 */

#include <nvidia/conftest.h>

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/slab.h>
#include <linux/nvmap.h>
#include <linux/rbtree.h>
#include <linux/list.h>
#include <linux/mman.h>
#include <linux/wait.h>
#include <linux/interrupt.h>

#include <linux/nvscierror.h>
#include <linux/nvsciipc_interface.h>

#include <trace/events/nvmap.h>
#include "nvmap_dev.h"
#include "nvmap_dmabuf.h"
#include "nvmap_alloc.h"
#include "nvmap_handle.h"
#include "nvmap_handle_int.h"

#include <soc/tegra/virt/hv-ivc.h>

#define NVMAP_SCI_IPC_MAINTAIN_FREE_SID_LIST 0
#define GET_DMABUF_FROM_HANDLE(h, is_ro) (is_ro ? h->dmabuf_ro : h->dmabuf)
#define SET_DMABUF_IN_HANDLE(h, is_ro, dmabuf) do {	\
	if (is_ro)	\
		h->dmabuf_ro = dmabuf;	\
	else	\
		h->dmabuf = dmabuf;	\
} while (0)

/*
 * nvmap_ivc_context:
 *
 * This is the context that is used while executing any tegra_hv_ivc* APIs.
 *
 * This context is set up in module init phase and is accessible across clients.
 *
 */
struct nvmap_ivc_context {
	u32 queue_id;
	u32 max_retries;
	u32 max_wait_time;

	struct tegra_hv_ivc_cookie *ivck;

	/*
	 * Lock that needs to be taken before calling tegra_hv_ivc*
	 * APIs which access the ivc write queue.
	 */
	struct mutex ivc_write_queue_mlock;

	/*
	 * Lock that needs to be taken before calling tegra_hv_ivc*
	 * APIs which access the ivc read queue.
	 */
	struct mutex ivc_read_queue_mlock;

	/*
	 * Wait queue for waiting until the tegra_hv_ivc_can_write returns true.
	 */
	wait_queue_head_t write_wait_queue;

	/*
	 * id_array to keep track of unique request-response pairs where the request
	 * was sent by the local VM to the peer VM. This is needed to map the response
	 * received back from the peer VM to the corresponding request.
	 */
	struct xarray id_array;
};

static struct nvmap_ivc_context *nvmap_ivc_ctx;

/*
 * nvmap_ivc_cmd:
 * Applicable in case of inter-vm communication.
 *
 * To ask the exporter VM to perform certain operations,
 * the importer VM sends a ivc frame to the exporter VM.
 *
 * nvmap_ivc_cmd defines the operation that the exporter VM
 * needs to perform on the data sent in the ivc frame.
 *
 * nvmap_ivc_cmd is stored in the first byte of the ivc frame.
 *
 * This is the normal flow for the following sci ipc ivc commands:
 *
 * Importer VM:
 * <USER THREAD>
 * 1. send_request(packet<*_READ_REQ, req_data>)
 *    - tegra_hv_ivc_write(packet<*_READ_REQ, req_data>)
 * 2. set is_resp_ready to false
 * 3. set resp_data_user to 0
 * 4. wait_for_response_from_exporter(!is_resp_ready ? sleep() : continue)
 *
 * Exporter VM:
 * <INTERRUPT TOP HALF>
 * 5. Wake up bottom half thread
 * <INTERRUPT BOTTOM HALF>
 * 6. tegra_hv_ivc_read(packet<*_READ_REQ, req_data>)
 *    - case *_READ_REQ:
 *      - process_request_and_send_response_to_exporter VM(req_data)
 *        - tegra_hv_ivc_write(packet<*_READ_RESP, resp_data>)
 *
 * Importer VM:
 * <INTERRUPT TOP HALF>
 * 8. Wake up bottom half thread
 * <INTERRUPT BOTTOM HALF>
 * 9. tegra_hv_ivc_read(packet<*_READ_RESP, resp_data>)
 *    - case *_READ_RESP:
 *      - set is_resp_ready to true
 *      - copy resp_data to resp_data_user
 *      - wake up user thread that is waiting on is_resp_ready
 *
 * <USER THREAD>
 * 10. wait_for_response_from_exporter(!is_resp_ready ? sleep() : continue)
 * 11. read_resp(resp_data_user)
 * 12. Using the data in resp_data_user, proceed to execute the rest of the code.
 *
 * NVMAP_IVC_CTP_FIND_SCI_IPC_ENTRY_READ_REQ:
 * Command the exporter VM to:
 * 1. Interpret the data it received as a struct nvmap_ivc_req
 *    object.
 * 2. Find the sci ipc entry for the given (sci_ipc_id, peer_vuid, localu_vuid,
 *    flags)
 * 3. Perform refcount updates if requested by the importer VM
 * 4. Send a ivc frame back to the importer VM with the command
 *    NVMAP_IVC_PTC_FIND_SCI_IPC_ENTRY_READ_RESP and the response data.
 *    4.1. If a matching sci ipc entry is found, the response will
 *         be the ivm_id of the matching sci ipc entry.
 *    4.2. If no matching sci ipc entry is found, response will be
 *         NVMAP_IVC_STATUS_FAILURE.
 *
 * NVMAP_IVC_CTP_FIND_HANDLE_FROM_IVM_ID_READ_REQ:
 * Command the exporter VM to:
 * 1. Interpret the data it received as a struct nvmap_ivc_req
 *    object.
 * 2. Decrement the refcount of the handle for the given (ivm_id) and send a
 *    ivc frame back to the importer VM with the command
 *    NVMAP_IVC_PTC_FIND_HANDLE_FROM_IVM_ID_READ_RESP and the response data.
 *    2.1. If the refcount is decremented successfully, the data will contain
 *         NVMAP_IVC_STATUS_SUCCESS.
 *    2.2. If the refcount is not decremented successfully, resp will be
 *         NVMAP_IVC_STATUS_FAILURE.
 *
 * NVMAP_IVC_PTC_FIND_SCI_IPC_ENTRY_READ_RESP:
 * NVMAP_IVC_PTC_FIND_HANDLE_FROM_IVM_ID_READ_RESP:
 * Command the exporter VM to:
 * 1. Find the corresponding waiter for the response
 * 2. Copy response data to the waiter
 * 3. Wake up waiter
 */
enum nvmap_ivc_cmd {
	NVMAP_IVC_INVALID_CMD_MIN = 0,
	NVMAP_IVC_CTP_FIND_SCI_IPC_ENTRY_READ_REQ,
	NVMAP_IVC_PTC_FIND_SCI_IPC_ENTRY_READ_RESP,
	NVMAP_IVC_CTP_FIND_HANDLE_FROM_IVM_ID_READ_REQ,
	NVMAP_IVC_PTC_FIND_HANDLE_FROM_IVM_ID_READ_RESP,
	NVMAP_IVC_INVALID_CMD_MAX
};

/*
 * nvmap_ivc_status:
 *
 * This is the status of the operation performed by the exporter VM.
 *
 */
enum nvmap_ivc_status {
	NVMAP_IVC_STATUS_FAILURE = 0,
	NVMAP_IVC_STATUS_SUCCESS
};

/*
 * nvmap_find_sci_ipc_entry_req:
 *
 * This is the unique tuple that the importer sends to the exporter to find a
 * matching sci ipc entry.
 *
 */
struct __packed nvmap_find_sci_ipc_entry_req {
	/*
	 * The set of values which uniquely identify a import operation
	 */
	u64 sci_ipc_id;
	u64 peer_vuid;
	u64 localu_vuid;
	u32 flags;

	/*
	 * Additional flags to signal the updates needed once the matching
	 * sci ipc entry is found.
	 */
	bool dec_handle_refcount;
	bool dec_sci_ipc_entry_refcount;

};

/*
 * nvmap_find_handle_from_ivm_id_req:
 *
 * This is the unique tuple that the importer sends to the exporter to find a
 * matching handle.
 *
 */
struct __packed nvmap_find_handle_from_ivm_id_req {
	/*
	 * The VM ID of the importer VM.
	 */
	u64 vm_id;

	/*
	 * The IVM ID of the handle.
	 */
	u64 ivm_id;

	/*
	 * Additional flags to signal the updates needed once the matching
	 * handle is found.
	 */
	bool dec_handle_refcount;
};

/*
 * nvmap_ivc_req:
 *
 * This is the request data that is sent by the importer VM
 * to the exporter VM, while executing IMPORT/FREE APIs.
 *
 */
struct __packed nvmap_ivc_req {
	union {
		/*
		 * When cmd = NVMAP_IVC_CTP_FIND_SCI_IPC_ENTRY_READ_REQ
		 */
		struct nvmap_find_sci_ipc_entry_req find_entry_req;

		/*
		 * When cmd = NVMAP_IVC_CTP_FIND_HANDLE_FROM_IVM_ID_READ_REQ
		 */
		struct nvmap_find_handle_from_ivm_id_req find_handle_req;
	};
};

/*
 * nvmap_ivc_packet:
 * This is packet which is actually read from/written into the IVC queue.
 *
 */
struct __packed nvmap_ivc_packet {
	u64 cmd;
	u32 comm_id; /* comm_id is 32-bit as xa_alloc expects a 32-bit id */
	struct nvmap_ivc_req req;
	u64 resp;
};

/*
 * nvmap_ivc_communicate:
 *
 * This is used to
 * - Store the request [User thread]
 * - Wait for the response to be ready [User thread]
 * - Fetch the comm details for which the response has just
 *   arrived (using comm_id and xarray) [Bottom half thread]
 * - Validate the request details [Bottom half thread]
 * - Store the response so that the user thread can access it [Bottom half thread]
 * - Wake up the user thread waiting on this response [Bottom half thread]
 * - Read and process the response [User thread]
 *
 * Note: This struct must NOT be packed to ensure proper alignment
 * of the wait_queue_head_t field, which requires alignment for its
 * internal spinlock and other fields.
 */
struct nvmap_ivc_communicate {
	struct nvmap_ivc_packet comm_packet;
	wait_queue_head_t resp_wait_queue;
	bool is_resp_ready;
};

struct nvmap_sci_ipc {
	struct rb_root entries;
	struct mutex mlock;
#if NVMAP_SCI_IPC_MAINTAIN_FREE_SID_LIST == 1
	struct list_head free_sid_list;
};

struct free_sid_node {
	struct list_head list;
	u64 sid;
#endif
};


/* An rb-tree root node for holding sci_ipc_id of clients */
struct nvmap_sci_ipc_entry {
	struct rb_node entry;
	struct nvmap_client *client;
	struct nvmap_handle *handle;
	u64 sci_ipc_id;
	u64 peer_vuid;
	u64 localu_vuid;
	u32 flags;
	u32 refcount;
};

static struct nvmap_sci_ipc *nvmapsciipc;

#if NVMAP_SCI_IPC_MAINTAIN_FREE_SID_LIST == 1
static int nvmap_decrement_sci_ipc_entry_refcount(struct nvmap_sci_ipc_entry *entry)
#else
static void nvmap_decrement_sci_ipc_entry_refcount(struct nvmap_sci_ipc_entry *entry)
#endif
{
	entry->refcount--;
	if (entry->refcount == 0U) {
#if NVMAP_SCI_IPC_MAINTAIN_FREE_SID_LIST == 1
		struct free_sid_node *free_node;

		free_node = kzalloc(sizeof(*free_node), GFP_KERNEL);
		if (!free_node) {
			kfree(entry);
			return -ENOMEM;
		}
		free_node->sid = entry->sci_ipc_id;
		list_add_tail(&free_node->list, &nvmapsciipc->free_sid_list);
#endif
		rb_erase(&entry->entry, &nvmapsciipc->entries);
		kfree(entry);
	}
#if NVMAP_SCI_IPC_MAINTAIN_FREE_SID_LIST == 1
	return 0;
#endif
}


static struct nvmap_sci_ipc_entry *nvmap_find_sci_ipc_entry(
	u32 flags, u64 sci_ipc_id, NvSciIpcEndpointVuid localu_vuid,
	NvSciIpcEndpointVuid peer_vuid)
{
	struct rb_node *node = nvmapsciipc->entries.rb_node;
	struct nvmap_sci_ipc_entry *entry;

	while (node) {
		entry = rb_entry(node, struct nvmap_sci_ipc_entry, entry);
		if (entry == NULL) {
			WARN("INVALID_SCI_IPC_ENTRY", "Found NULL entry for a non-NULL node\n");
			return NULL;
		}

		if (sci_ipc_id < entry->sci_ipc_id) {
			node = node->rb_left;
		} else if (sci_ipc_id > entry->sci_ipc_id) {
			node = node->rb_right;
		} else {
			/* ID matches, check if other criteria match */
			if (entry->handle &&
				entry->peer_vuid == localu_vuid &&
				entry->localu_vuid == peer_vuid &&
				(entry->flags & flags) == flags)
				return entry;

			/* Go right to check for other entries with same ID */
			node = node->rb_right;
		}
	}

	return NULL;
}

static int nvmap_ivc_write_packet(struct nvmap_ivc_packet *packet)
{
	int ret = 0, bytes_written = 0;
	long remain = 0;

	/*
	 * Wait without lock first. This is safe because:
	 * 1. can_write() only reads queue state (no modification)
	 * 2. Spurious wakeups are harmless - we recheck under lock
	 * 3. We hold the lock during actual write to prevent races
	 */
	remain = wait_event_interruptible_timeout(nvmap_ivc_ctx->write_wait_queue,
		tegra_hv_ivc_can_write(nvmap_ivc_ctx->ivck),
		usecs_to_jiffies(nvmap_ivc_ctx->max_wait_time));
	if (remain == 0) {
		pr_debug("Failed to wait for write (timed out)\n");
		ret = -ETIMEDOUT;
		goto exit;
	} else if (remain < 0) {
		pr_err("Failed to wait for write (interrupted) with error: %ld\n", remain);
		ret = -EINTR;
		goto exit;
	}

	/* Lock before write to ensure atomicity */
	mutex_lock(&nvmap_ivc_ctx->ivc_write_queue_mlock);

	/*
	 * Recheck can_write under lock. Even though we waited for it,
	 * another thread might have consumed the last slot before we got the lock.
	 * Return -ETIMEDOUT so retry logic can handle it.
	 */
	if (!tegra_hv_ivc_can_write(nvmap_ivc_ctx->ivck)) {
		pr_debug("Queue became full after wait, will retry\n");
		ret = -ETIMEDOUT;
		goto unlock;
	}

	/* Write the packet to the IVC channel */
	bytes_written = tegra_hv_ivc_write(nvmap_ivc_ctx->ivck, packet,
		sizeof(struct nvmap_ivc_packet));
	if (bytes_written != sizeof(struct nvmap_ivc_packet)) {
		pr_err("Failed to write packet with bytes_written: %d, expected: %lu\n",
			bytes_written, sizeof(struct nvmap_ivc_packet));
		ret = -EIO;
	}

unlock:
	mutex_unlock(&nvmap_ivc_ctx->ivc_write_queue_mlock);
exit:
	return ret;
}

static int nvmap_ivc_write_packet_with_retries(struct nvmap_ivc_packet *packet, u32 retries)
{
	int ret = -EINVAL;

	if (packet == NULL || retries == 0U)
		return ret;
	/*
	 * Introducing retries to handle the case where:
	 * 1. tegra_hv_ivc_can_write has been checked to be true in current thread.
	 * 2. Other threads write to the IVC channel, before the current thread
	 *    could, making the IVC queue full again.
	 * 3. Shall retry tegra_hv_ivc_write for retries times.
	 */
	for (u32 i = 0; i < retries; i++) {
		ret = nvmap_ivc_write_packet(packet);
		if (ret != -ETIMEDOUT)
			break;
	}

	return ret;
}

static int nvmap_ivc_wait_for_response(struct nvmap_ivc_communicate *comm)
{
	long remain = 0;
	int ret = -EINVAL;

	remain = wait_event_interruptible_timeout(comm->resp_wait_queue,
		comm->is_resp_ready, usecs_to_jiffies(nvmap_ivc_ctx->max_wait_time));
	if (remain == 0) {
		pr_debug("Failed to read response (timed out)\n");
		ret = -ETIMEDOUT;
	} else if (remain < 0) {
		pr_err("Failed to read response (interrupted) with error: %ld\n", remain);
		ret = -EINTR;
	} else {
		pr_debug("Response is ready for reading\n");
		ret = 0;
	}

	return ret;
}

static irqreturn_t nvmap_ivc_top_half(int irq, void *data)
{
	struct nvmap_ivc_context *ctx = (struct nvmap_ivc_context *)data;

	pr_debug("IVC top half ISR triggered\n");

	if (tegra_hv_ivc_channel_notified(ctx->ivck)) {
		pr_debug("IVC channel not usable in top half ISR\n");
		return IRQ_HANDLED;
	}

	return IRQ_WAKE_THREAD;
}

static void nvmap_ivc_find_sci_ipc_entry_read_req(struct nvmap_ivc_packet *packet)
{
	struct nvmap_sci_ipc_entry *entry = NULL;

	/* Initialize the response with failure value */
	packet->resp = NVMAP_IVC_STATUS_FAILURE;

	/* Search for the sci ipc entry */
	mutex_lock(&nvmapsciipc->mlock);
	entry = nvmap_find_sci_ipc_entry(packet->req.find_entry_req.flags,
		packet->req.find_entry_req.sci_ipc_id, packet->req.find_entry_req.localu_vuid,
		packet->req.find_entry_req.peer_vuid);
	if (!entry)
		pr_err("Sci_ipc_entry not found!\n");
	else {
		pr_debug("FOUND A MATCH: ivm_id: %llu entry->refcount: %d handle->ref: %d\n",
			entry->handle->ivm_id, entry->refcount,
			atomic_read(&entry->handle->ref));

		/* Populate the response */
		packet->resp = entry->handle->ivm_id;

		if (packet->req.find_entry_req.dec_handle_refcount)
			nvmap_handle_put(entry->handle);

		if (packet->req.find_entry_req.dec_sci_ipc_entry_refcount)
#if NVMAP_SCI_IPC_MAINTAIN_FREE_SID_LIST == 1
			if (nvmap_decrement_sci_ipc_entry_refcount(entry)) {
				pr_err("Failed to decrement sci ipc entry refcount\n");
				packet->resp = NVMAP_IVC_STATUS_FAILURE;
				goto unlock;
			}
#else
			nvmap_decrement_sci_ipc_entry_refcount(entry);
#endif
	}

#if NVMAP_SCI_IPC_MAINTAIN_FREE_SID_LIST == 1
unlock:
#endif
	mutex_unlock(&nvmapsciipc->mlock);

	/* Set the cmd to the correct response cmd */
	packet->cmd = NVMAP_IVC_PTC_FIND_SCI_IPC_ENTRY_READ_RESP;

	/* Return the response of the update from the producer to the consumer */
	if (nvmap_ivc_write_packet_with_retries(packet, nvmap_ivc_ctx->max_retries))
		pr_err("Failed to write response packet\n");
}

static void nvmap_ivc_find_handle_from_ivm_id_read_req(struct nvmap_ivc_packet *packet)
{
	struct nvmap_handle *handle = NULL;

	/* Initialize the response with failure value */
	packet->resp = NVMAP_IVC_STATUS_FAILURE;

	/* Search for handle using ivm_id (handle->ref += 1) */
	handle = nvmap_get_handle_from_ivm_id(packet->req.find_handle_req.ivm_id);
	if (!handle)
		pr_err("Handle not found: ivm_id: %llu\n",
			packet->req.find_handle_req.ivm_id);
	else {
		pr_debug("FOUND A MATCH: ivm_id: %llu, peer VM ID: %u, handle->ref: %d\n",
			handle->ivm_id, handle->peer, atomic_read(&handle->ref));

		/*
		 * Decrement the handle refcount if requested.
		 */
		if (packet->req.find_handle_req.dec_handle_refcount)
			nvmap_handle_put(handle);

		packet->resp = NVMAP_IVC_STATUS_SUCCESS;

		/*
		 * Decrement the extra refcount held in
		 * nvmap_get_handle_from_ivm_id().
		 */
		nvmap_handle_put(handle);
	}

	/* Set the cmd to the correct response cmd */
	packet->cmd = NVMAP_IVC_PTC_FIND_HANDLE_FROM_IVM_ID_READ_RESP;

	/* Return the response of the update from the producer to the consumer */
	if (nvmap_ivc_write_packet_with_retries(packet, nvmap_ivc_ctx->max_retries))
		pr_err("Failed to write response packet\n");
}

static void nvmap_ivc_read_resp(struct nvmap_ivc_packet *packet)
{
	struct nvmap_ivc_communicate *saved_comm = NULL;

	/* Find the matching request in id-array */
	saved_comm = xa_load(&nvmap_ivc_ctx->id_array, packet->comm_id);
	if (saved_comm &&
		(memcmp(&saved_comm->comm_packet.req, &packet->req,
			sizeof(struct nvmap_ivc_req)) == 0)) {
		saved_comm->comm_packet.resp = packet->resp;
		saved_comm->is_resp_ready = true;
		wake_up_interruptible(&saved_comm->resp_wait_queue);
	} else
		pr_err("Comm with comm_id: %u not found\n", packet->comm_id);
}

static irqreturn_t nvmap_ivc_bottom_half(int irq, void *data)
{
	struct nvmap_ivc_context *ctx = (struct nvmap_ivc_context *)data;

	/*
	 * Write interrupt happens only when the queue becomes non-full.
	 * So, wakeup all the threads which are waiting to write something on the queue.
	 */
	mutex_lock(&ctx->ivc_write_queue_mlock);
	if (tegra_hv_ivc_can_write(ctx->ivck))
		wake_up_interruptible(&ctx->write_wait_queue);
	mutex_unlock(&ctx->ivc_write_queue_mlock);

	/*
	 * Read packets until the queue is empty. This is needed as a read interrupt happens
	 * only when the queue becomes non-empty. Before the bottom half ISR is called,
	 * additional packets may be written to the queue. So, if we only read one packet,
	 * other packets will remain unread.
	 */
	while (true) {
		int ret = -EINVAL;
		struct nvmap_ivc_packet packet = {0};

		mutex_lock(&ctx->ivc_read_queue_mlock);
		if (tegra_hv_ivc_can_read(ctx->ivck)) {
			/* Read the packet from the IVC channel */
			ret = tegra_hv_ivc_read(ctx->ivck, &packet,
				sizeof(struct nvmap_ivc_packet));
			if (ret == sizeof(struct nvmap_ivc_packet))
				ret = 0;
			else
				pr_err("IVC read queue not empty, but read failed: %d\n", ret);
		}
		mutex_unlock(&ctx->ivc_read_queue_mlock);

		if (ret)
			return IRQ_HANDLED;

		switch (packet.cmd) {
		case NVMAP_IVC_CTP_FIND_SCI_IPC_ENTRY_READ_REQ:
			nvmap_ivc_find_sci_ipc_entry_read_req(&packet);
			break;
		case NVMAP_IVC_CTP_FIND_HANDLE_FROM_IVM_ID_READ_REQ:
			nvmap_ivc_find_handle_from_ivm_id_read_req(&packet);
			break;
		case NVMAP_IVC_PTC_FIND_SCI_IPC_ENTRY_READ_RESP:
		case NVMAP_IVC_PTC_FIND_HANDLE_FROM_IVM_ID_READ_RESP:
			nvmap_ivc_read_resp(&packet);
			break;
		default:
			pr_err("Invalid command: %llu\n", packet.cmd);
			break;
		}
	}

	return IRQ_HANDLED;
}

int nvmap_validate_sci_ipc_params(struct nvmap_client *client,
			NvSciIpcEndpointAuthToken auth_token,
			NvSciIpcEndpointVuid *pr_vuid,
			NvSciIpcTopoId *pr_topoid,
			NvSciIpcEndpointVuid *lu_vuid)
{
	NvSciError err = NvSciError_Success;
	int ret = 0;

	err = NvSciIpcEndpointValidateAuthTokenLinuxCurrent(auth_token,
			lu_vuid);
	if (err != NvSciError_Success) {
		ret = -EINVAL;
		goto out;
	}

	err = NvSciIpcEndpointMapVuid(*lu_vuid, pr_topoid, pr_vuid);
	if (err != NvSciError_Success) {
		ret = -EINVAL;
		goto out;
	}

out:
	return ret;
}

static u64 nvmap_unique_sci_ipc_id(void)
{
	static atomic_t unq_id = { 0 };

#if NVMAP_SCI_IPC_MAINTAIN_FREE_SID_LIST == 1
	u64 id;

	if (list_empty(&nvmapsciipc->free_sid_list) == 0) {
		struct free_sid_node *fnode = list_first_entry(
			&nvmapsciipc->free_sid_list,
			typeof(*fnode),
			list);

		id = fnode->sid;
		list_del(&fnode->list);
		kfree(fnode);
		goto ret_id;
	}

	id = atomic_add_return(2, &unq_id);

ret_id:
#else
	u64 id = atomic_add_return(2, &unq_id);
#endif

	/* unq_id has wrapped around and is now 0 again */
	WARN_ON(id == 0);
	return id;
}

static struct nvmap_sci_ipc_entry *nvmap_search_sci_ipc_entry(
	struct rb_root *root,
	struct nvmap_handle *h,
	u32 flags,
	NvSciIpcEndpointVuid peer_vuid,
	NvSciIpcEndpointVuid localu_vuid)
{
	struct rb_node *node;  /* top of the tree */
	struct nvmap_sci_ipc_entry *entry;

	for (node = rb_first(root); node; node = rb_next(node)) {
		entry = rb_entry(node, struct nvmap_sci_ipc_entry, entry);

		if (entry && entry->handle == h
			&& entry->flags == flags
			&& entry->peer_vuid == peer_vuid
			&& entry->localu_vuid == localu_vuid)
			return entry;
	}
	return NULL;
}

static void nvmap_insert_sci_ipc_entry(struct rb_root *root,
		struct nvmap_sci_ipc_entry *new)
{
	struct nvmap_sci_ipc_entry *entry;
	struct rb_node *parent = NULL;
	u64 sid = new->sci_ipc_id;
	struct rb_node **link;

	link = &root->rb_node;
	/* Go to the bottom of the tree */
	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct nvmap_sci_ipc_entry, entry);

		if (entry->sci_ipc_id > sid)
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}

	/* Put the new node there */
	rb_link_node(&new->entry, parent, link);
	rb_insert_color(&new->entry, root);
}

int nvmap_create_sci_ipc_id(struct nvmap_client *client,
				struct nvmap_handle *h,
				u32 flags,
				u64 *sci_ipc_id,
				NvSciIpcEndpointVuid peer_vuid,
				NvSciIpcEndpointVuid localu_vuid,
				bool is_ro)
{
	struct nvmap_sci_ipc_entry *new_entry;
	struct nvmap_sci_ipc_entry *entry;
	int ret = -EINVAL;
	u64 id;

	mutex_lock(&nvmapsciipc->mlock);

	entry = nvmap_search_sci_ipc_entry(&nvmapsciipc->entries,
			h, flags, peer_vuid, localu_vuid);
	if (entry) {
		/*
		 * Increment the sci ipc entry refcount for each successful export.
		 * This is needed so that the sci ipc entry is not freed until and
		 * unless the buffer is imported as many times as it was exported.
		 */
		entry->refcount++;
		*sci_ipc_id = entry->sci_ipc_id;
		pr_debug("%d: matched Sci_Ipc_Id:%llu\n", __LINE__, *sci_ipc_id);
		ret = 0;
		goto unlock;
	} else {
		new_entry = kzalloc(sizeof(*new_entry), GFP_KERNEL);
		if (!new_entry) {
			ret = -ENOMEM;
			goto unlock;
		}
		id = nvmap_unique_sci_ipc_id();
		*sci_ipc_id = id;
		new_entry->sci_ipc_id = id;
		new_entry->client = client;
		new_entry->handle = h;
		new_entry->peer_vuid = peer_vuid;
		new_entry->localu_vuid = localu_vuid;
		new_entry->flags = flags;
		new_entry->refcount = 1;

		pr_debug("%d: New Sci_ipc_id %lld entry->pr_vuid: %llu entry->localu_vuid: %llu entry->flags: %u\n",
			__LINE__, new_entry->sci_ipc_id, new_entry->peer_vuid,
			new_entry->localu_vuid, new_entry->flags);

		nvmap_insert_sci_ipc_entry(&nvmapsciipc->entries, new_entry);
		ret = 0;
	}

unlock:
	mutex_unlock(&nvmapsciipc->mlock);
	if (!ret) {
		/*
		 * Increment handle refcount for each successful export. This is needed
		 * so that the handle is not freed until and unless the buffer is imported
		 * as many times as it was exported.
		 */
		if (!nvmap_handle_get(h))
			return -EINVAL;
	}

	return ret;
}

/*
 * Create mapping between the comm_id and nvmap_ivc_communicate
 * for inter-vm buffer sharing.
 */
static int nvmap_comm_id_alloc(struct xarray *comm_id_arr, u32 *comm_id,
	struct nvmap_ivc_communicate *comm)
{
	static u32 xa_start = U32_MAX / 2;
	int e;

	if (!comm_id_arr || !comm)
		return -EINVAL;

alloc_from_start:
	e = xa_alloc(comm_id_arr, comm_id, comm,
		       XA_LIMIT(xa_start, U32_MAX), GFP_KERNEL);

	xa_lock(comm_id_arr);
	if (!e) {
		if (*comm_id == U32_MAX)
			xa_start = U32_MAX / 2;
		else
			xa_start = *comm_id + 1;
	} else if (e == -EBUSY && xa_start != U32_MAX / 2) {
		/*
		 * xa_alloc returns -EBUSY if there are no free entries.
		 * In this case we will give one more try by resetting xa_start.
		 */
		xa_start = U32_MAX / 2;
		xa_unlock(comm_id_arr);
		goto alloc_from_start;
	}
	xa_unlock(comm_id_arr);

	return e;
}

/*
 * Clear mapping between the comm_id and nvmap_ivc_communicate
 * for inter-vm buffer sharing.
 */
static struct nvmap_ivc_communicate *nvmap_comm_id_array_release(struct xarray *comm_id_arr,
	u32 comm_id)
{
	if (!comm_id_arr || !comm_id)
		return NULL;

	return xa_erase(comm_id_arr, comm_id);
}

static int nvmap_setup_comm(struct nvmap_ivc_communicate **comm_output)
{
	int ret = -EINVAL;
	u32 comm_id = 0;
	struct nvmap_ivc_communicate *comm = NULL;

	/* Allocate a request-response pair */
	comm = kzalloc(sizeof(struct nvmap_ivc_communicate), GFP_KERNEL);
	if (!comm)
		return -ENOMEM;

	/* Initialize communicate structure */
	init_waitqueue_head(&comm->resp_wait_queue);
	comm->is_resp_ready = false;

	/* Allocate comm_id */
	ret = nvmap_comm_id_alloc(&nvmap_ivc_ctx->id_array, &comm_id, comm);
	if (ret) {
		pr_err("Failed to allocate comm_id\n");
		kfree(comm);
		return -EINVAL;
	}

	comm->comm_packet.comm_id = comm_id;
	*comm_output = comm;

	return 0;
}

static int nvmap_tear_down_comm(struct nvmap_ivc_communicate *comm)
{
	if (!comm)
		return -EINVAL;

	kfree(comm);
	return 0;
}

static int nvmap_ivc_wait_for_response_with_retries(struct nvmap_ivc_communicate *comm, u32 retries)
{
	int ret = -EINVAL;

	if (comm == NULL || retries == 0U)
		return ret;

	/*
	 * Introducing retries to handle the case where:
	 * 1. The response is late and the wait times out.
	 * 2. To receive the late response, shall go into wait for retries times.
	 */
	for (u32 i = 0; i < retries; i++) {
		ret = nvmap_ivc_wait_for_response(comm);
		if (ret != -ETIMEDOUT)
			break;
	}
	return ret;
}

/*
 * nvmap_ivc_communicate:
 * This function sends a request to the peer, waits for a response and
 * returns the response.
 *
 * Input:
 * 1. Command to send to the peer
 * 2. Request data on which the command is to be executed
 *
 * Output:
 * 1. Response from the peer
 */
static int nvmap_ivc_communicate(enum nvmap_ivc_cmd cmd,
	struct nvmap_ivc_req *req, u64 *resp)
{
	int ret = -EINVAL;
	struct nvmap_ivc_packet *packet = NULL;
	struct nvmap_ivc_communicate *comm = NULL;

	/* Validate IVC context is initialized */
	if (!nvmap_ivc_ctx || !nvmap_ivc_ctx->ivck) {
		pr_err("IVC context not initialized\n");
		return -EINVAL;
	}

	/* Validate the input parameters */
	if (cmd < NVMAP_IVC_INVALID_CMD_MIN || cmd >= NVMAP_IVC_INVALID_CMD_MAX ||
		IS_ERR_OR_NULL(req) || IS_ERR_OR_NULL(resp)) {
		pr_err("Invalid input parameters\n");
		return -EINVAL;
	}

	/* Setup the communication structure object */
	ret = nvmap_setup_comm(&comm);
	if (ret) {
		pr_err("Failed to setup communication with error: %d\n", ret);
		return ret;
	}

	/* Populate the packet */
	packet = &comm->comm_packet;
	packet->cmd = cmd;
	memcpy(&packet->req, req, sizeof(struct nvmap_ivc_req));

	/* Send the request from the consumer to the producer */
	ret = nvmap_ivc_write_packet_with_retries(packet, nvmap_ivc_ctx->max_retries);
	if (ret)
		goto free_id;

	/* Wait for the response from the peer */
	ret = nvmap_ivc_wait_for_response_with_retries(comm, nvmap_ivc_ctx->max_retries);
	if (ret)
		goto free_id;

	*resp = comm->comm_packet.resp;

free_id:
	nvmap_comm_id_array_release(&nvmap_ivc_ctx->id_array, packet->comm_id);
	nvmap_tear_down_comm(comm);

	return ret;
}

int nvmap_decrement_handle_refcount_by_ivm_id(u64 ivm_id, u64 vm_id)
{
	struct nvmap_ivc_req req = {0};
	u64 resp = 0;
	int ret = -EINVAL;

	/* Populate the request */
	req.find_handle_req.ivm_id = ivm_id;
	req.find_handle_req.vm_id = vm_id;
	req.find_handle_req.dec_handle_refcount = true;

	/* Start communication with the peer */
	ret = nvmap_ivc_communicate(NVMAP_IVC_CTP_FIND_HANDLE_FROM_IVM_ID_READ_REQ,
			&req, &resp);
	if (ret != 0 || resp != NVMAP_IVC_STATUS_SUCCESS) {
		pr_err("Failed to decrement handle refcount of peer for ivm_id: %llu with ret: %d, resp: %llu\n",
			ivm_id, ret, resp);
		ret = ret ? ret : -EINVAL;
	}

	return ret;
}

int nvmap_get_handle_from_sci_ipc_id(struct nvmap_client *client, u32 flags,
		u64 sci_ipc_id, NvSciIpcEndpointVuid peer_vuid,
		NvSciIpcEndpointVuid localu_vuid, u32 *handle, bool is_inter_vm)
{
	bool is_ro = (flags == PROT_READ) ? true : false;
	struct nvmap_handle_ref *ref = NULL;
	struct nvmap_sci_ipc_entry *entry;
	struct dma_buf *dmabuf = NULL;
	struct nvmap_handle *h;
	long remain;
	int ret = 0;
	int fd;
	long dmabuf_ref = 0;
	u32 id = 0;
	struct nvmap_ivc_req req = {0};
	u64 resp = 0;
	u64 ivm_id = 0;

	/* Initialize the handle to 0 */
	*handle = 0;

	pr_debug("%d: Sci_Ipc_Id %lld peer_vuid: %llu localu_vuid: %llu flags: %u\n",
		__LINE__, sci_ipc_id, peer_vuid, localu_vuid, flags);


	if (is_inter_vm) {

		/* Populate the request */
		req.find_entry_req.sci_ipc_id = sci_ipc_id;
		req.find_entry_req.peer_vuid = peer_vuid;
		req.find_entry_req.localu_vuid = localu_vuid;
		req.find_entry_req.flags = flags;
		req.find_entry_req.dec_handle_refcount = false;
		req.find_entry_req.dec_sci_ipc_entry_refcount = false;

		ret = nvmap_ivc_communicate(NVMAP_IVC_CTP_FIND_SCI_IPC_ENTRY_READ_REQ,
			&req, &resp);
		if (ret || resp == NVMAP_IVC_STATUS_FAILURE) {
			ret = -EINVAL;
			pr_err("Failed to get ivm_id, sci_ipc_id: %lld ret: %d resp: %llu\n",
				sci_ipc_id, ret, resp);
			goto unlock;
		}

		ivm_id = resp;

		/*
		 * Search the list of device's handles for the handle with the given IVM ID.
		 * If successful, a valid handle is returned with its refcount incremented.
		 * If not successful, a new handle is created from the IVM ID.
		 */
		h = nvmap_get_handle_from_ivm_id(ivm_id);
		if (IS_ERR_OR_NULL(h)) {
			pr_debug("Failed to get handle from IVM ID, creating new handle.\n");

			ref = nvmap_create_handle_from_ivm_id(client, ivm_id, is_ro);
			if (IS_ERR_OR_NULL(ref)) {
				pr_err("Failed to create handle from IVM ID\n");
				ret = IS_ERR(ref) ? PTR_ERR(ref) : -EINVAL;
				goto unlock;
			}

			h = ref->handle;
			dmabuf = GET_DMABUF_FROM_HANDLE(h, is_ro);
			goto create_handle_id;
		}
	} else {
		mutex_lock(&nvmapsciipc->mlock);
		entry = nvmap_find_sci_ipc_entry(flags, sci_ipc_id, localu_vuid, peer_vuid);
		if (entry == NULL) {
			pr_debug("%d: No matching Sci_Ipc_Id %lld found\n", __LINE__, sci_ipc_id);
			ret = -EINVAL;
			goto unlock;
		}

		h = entry->handle;
	}

	mutex_lock(&h->lock);
	/*
	 * Now check if the dmabuf exists for the given handle and permissions.
	 *
	 * The dmabuf may not exist for the handle for either of RW or RO cases for
	 * both intra-VM and inter-VM because the handle may not have any references
	 * to it with the given permissions (RO or RW) when import is called.
	 *
	 * We need to create the missing dmabuf because the subsequent call to
	 * nvmap_duplicate_handle() assumes that the dmabuf for the given handle
	 * and permissions already exists.
	 */
	dmabuf = GET_DMABUF_FROM_HANDLE(h, is_ro);
	if (dmabuf == NULL) {
		dmabuf = __nvmap_make_dmabuf(client, h, is_ro);
		if (IS_ERR(dmabuf)) {
			ret = PTR_ERR(dmabuf);
			mutex_unlock(&h->lock);
			goto unlock;
		}
		SET_DMABUF_IN_HANDLE(h, is_ro, dmabuf);
	} else {
#if defined(NV_GET_FILE_RCU_HAS_DOUBLE_PTR_FILE_ARG) /* Linux 6.7 */
		if (!get_file_rcu(&dmabuf->file)) {
#else
		if (!get_file_rcu(dmabuf->file)) {
#endif
			mutex_unlock(&h->lock);
			remain = wait_event_interruptible_timeout(h->waitq,
					!GET_DMABUF_FROM_HANDLE(h, is_ro), (long)msecs_to_jiffies(100U));
			if (remain > 0 && !GET_DMABUF_FROM_HANDLE(h, is_ro)) {
				mutex_lock(&h->lock);
				dmabuf = __nvmap_make_dmabuf(client, h, is_ro);
				if (IS_ERR(dmabuf)) {
					ret = PTR_ERR(dmabuf);
					mutex_unlock(&h->lock);
					goto unlock;
				}
				SET_DMABUF_IN_HANDLE(h, is_ro, dmabuf);
			} else {
				ret = -EINVAL;
				goto unlock;
			}
		}
	}
	mutex_unlock(&h->lock);

	/*
	 * Duplicate the handle.
	 *
	 * Skip handle validation for inter-VM case,
	 * as we already have done it in nvmap_get_handle_from_ivm_id().
	 *
	 * Refcount updates:
	 * 1. h->ref += 1 when nvmap_validate_get() is successful,
	 * 2. If nvmap_handle_ref already exists then ref->dupes += 1
	 *    else
	 *    ref->dupes = 1, ref->handle->share_count += 1,
	 *    client->handle_count += 1, dmabuf->refcount += 1.
	 */
	ref = nvmap_duplicate_handle(client, h, is_inter_vm, is_ro);
	if (IS_ERR(ref)) {
		ret = -EINVAL;
		goto unlock;
	}

	/*
	 * When new RW/RO dmabuf created or duplicated, one extra dma_buf refcount is taken so to
	 * avoid getting it freed by another process, until duplication completes. Decrement that
	 * extra refcount here.
	 *
	 * dmabuf->refcount -= 1
	 */
	dma_buf_put(dmabuf);

create_handle_id:
	/*
	 * For each import call create and return a new handle id, even if a handle reference already
	 * existed with the given permissions before calling nvmap_duplicate_handle().
	 *
	 *
	 * Increase reference dup count, so that handle is not freed accidentally
	 * due to other thread calling NvRmMemHandleFree
	 */
	atomic_inc(&ref->dupes);
	dmabuf = GET_DMABUF_FROM_HANDLE(h, is_ro);
	if (client->ida) {
		if (nvmap_id_array_id_alloc(client->ida, &id, dmabuf) < 0) {
			atomic_dec(&ref->dupes);
			if (dmabuf)
				dma_buf_put(dmabuf);
			nvmap_free_handle(client, h, is_ro);
			ret = -ENOMEM;
			goto unlock;
		}
		if (!id)
			*handle = 0;
		else
			*handle = id;
	} else {
		fd = nvmap_get_dmabuf_fd(client, h, is_ro);
		if (IS_ERR_VALUE((uintptr_t)fd)) {
			atomic_dec(&ref->dupes);
			if (dmabuf)
				dma_buf_put(dmabuf);
			nvmap_free_handle(client, h, is_ro);
			ret = -EINVAL;
			goto unlock;
		}
		*handle = fd;
		fd_install(fd, dmabuf->file);
	}

	/*
	 * Import is successful, now we must decrement appropriate refcounts
	 * to indicate the same.client
	 *
	 * Make the following recount updates:
	 * Intra-VM:
	 * 1. entry->refcount -= 1 (is inc. on each successful export call)
	 * 2. handle->refcount -= 1 (is inc. on each successful export call)
	 *
	 * Inter-VM:
	 * Send signal to the exporter VM update:
	 * 1. entry->refcount -= 1 (is inc. on each successful export call)
	 * 2. handle->refcount -= 1 (is inc. on each successful export call)
	 *
	 *    If on the importer VM we have
	 *    created handle for the first time:
	 * 3. handle->refcount += 1 (is dec. only when the importer VM sends
	 *    a signal after it was done using its handle). This prevents the
	 *    exporter VM to free its handle after all import calls are done by
	 *    the importer VM, but still the importer VM is using its handle and
	 *    hasn't completely freed it yet.
	 */
	if (is_inter_vm) {
		resp = 0;

		/* Populate the request */
		req.find_entry_req.dec_sci_ipc_entry_refcount = true;
		if (atomic_read(&ref->handle->imported_from_peer) == false) {
			/* Created new handle */
			atomic_set(&ref->handle->imported_from_peer, true);
			req.find_entry_req.dec_handle_refcount = false;
		} else {
			/* Duplicated existing handle */
			req.find_entry_req.dec_handle_refcount = true;
		}

		ret = nvmap_ivc_communicate(NVMAP_IVC_CTP_FIND_SCI_IPC_ENTRY_READ_REQ,
				&req, &resp);
		if (ret || resp == NVMAP_IVC_STATUS_FAILURE || resp != ivm_id) {
			pr_err("Failed to decrement sci ipc entry refcount of peer with ret: %d, resp: %llu\n",
				ret, resp);
			atomic_dec(&ref->dupes);
			if (dmabuf)
				dma_buf_put(dmabuf);
			nvmap_free_handle(client, h, is_ro);
			ret = -EINVAL;
		}
	} else {
		/*
		 * For intra-VM case, decrement the handle refcount and
		 * sci ipc entry refcount that was incremented when export was called.
		 */
		nvmap_handle_put(h);
#if NVMAP_SCI_IPC_MAINTAIN_FREE_SID_LIST == 1
		if (nvmap_decrement_sci_ipc_entry_refcount(entry)) {
			pr_err("Failed to decrement sci ipc entry refcount\n");
			goto unlock;
		}
#else
		nvmap_decrement_sci_ipc_entry_refcount(entry);
#endif
	}

unlock:
	if (!is_inter_vm)
		mutex_unlock(&nvmapsciipc->mlock);

	if (!ret) {
		mutex_lock(&h->lock);
		if (dmabuf && dmabuf->file) {
			dmabuf_ref = file_count(dmabuf->file);
		} else {
			dmabuf_ref = 0;
		}
		mutex_unlock(&h->lock);

		if (!client->ida)
			trace_refcount_create_handle_from_sci_ipc_id(h, dmabuf,
				atomic_read(&h->ref),
				dmabuf_ref,
				is_ro ? "RO" : "RW");
		else
			trace_refcount_get_handle_from_sci_ipc_id(h, dmabuf,
				atomic_read(&h->ref),
				is_ro ? "RO" : "RW");

		if (!IS_ERR_OR_NULL(ref))
			/* coverity[FORWARD_NULL]; FP-BUG_4598544 */
			/* coverity[cert_exp34_c_violation]; FP-BUG_4598544 */
			atomic_dec(&ref->dupes);
	}

	return ret;
}

static int nvmap_ivc_validate_params(struct nvmap_ivc_context *ctx)
{
	/* Validate that IVC channel is properly reserved */
	if (!ctx || !ctx->ivck) {
		pr_err("Invalid IVC context or channel\n");
		return -EINVAL;
	}

	if (ctx->ivck->nframes == 0 ||
		ctx->ivck->frame_size == 0 ||
		ctx->max_retries == 0 ||
		ctx->max_wait_time == 0) {
		pr_err("Invalid IVC parameters: nframes=%u, frame_size=%u, retries=%u, wait_time=%u\n",
			ctx->ivck->nframes, ctx->ivck->frame_size, ctx->max_retries,
			ctx->max_wait_time);
		return -EINVAL;
	}

	if (ctx->ivck->frame_size < sizeof(struct nvmap_ivc_packet)) {
		pr_err("IVC queue has invalid frame size: %u (minimum: %zu)\n",
			ctx->ivck->frame_size, sizeof(struct nvmap_ivc_packet));
		return -EINVAL;
	}

	return 0;
}

int nvmap_init_ivc_queue(struct device *dev)
{
	struct device_node *np = dev->of_node;
	const u32 *ivc_cfg;
	int ivc_cfg_len;
	int err;

	/* Parse IVC configuration from device tree */
	ivc_cfg = of_get_property(np, "nvmap-sci-ipc-ivc-queue", &ivc_cfg_len);
	if (!ivc_cfg) {
		pr_info("nvmap-sci-ipc-ivc-queue property not found\n");
		err = 0; /* Not an error, just no IVC queue configured */
		goto exit;
	}

	if (ivc_cfg_len / sizeof(u32) != 3) {
		pr_err("nvmap-sci-ipc-ivc-queue property has invalid length: %lu (expected 3)\n",
			ivc_cfg_len / sizeof(u32));
		err = -EINVAL;
		goto exit;
	}

	/* Allocate IVC context */
	nvmap_ivc_ctx = devm_kzalloc(dev, sizeof(*nvmap_ivc_ctx), GFP_KERNEL);
	if (!nvmap_ivc_ctx) {
		err = -ENOMEM;
		goto exit;
	}

	/* Initialize the mutexes */
	mutex_init(&nvmap_ivc_ctx->ivc_write_queue_mlock);
	mutex_init(&nvmap_ivc_ctx->ivc_read_queue_mlock);

	/* Initialize the id_array */
	xa_init_flags(&nvmap_ivc_ctx->id_array, XA_FLAGS_ALLOC1);

	/* Initialize IVC context */
	init_waitqueue_head(&nvmap_ivc_ctx->write_wait_queue);

	/* Read numbers using of_read_number to handle endianness automatically */
	nvmap_ivc_ctx->queue_id = of_read_number(ivc_cfg, 1);
	nvmap_ivc_ctx->max_retries = of_read_number(ivc_cfg + 1, 1);
	nvmap_ivc_ctx->max_wait_time = of_read_number(ivc_cfg + 2, 1);

	/* Reserve IVC channel first */
	nvmap_ivc_ctx->ivck = tegra_hv_ivc_reserve(NULL, nvmap_ivc_ctx->queue_id, NULL);
	if (IS_ERR_OR_NULL(nvmap_ivc_ctx->ivck)) {
		err = IS_ERR(nvmap_ivc_ctx->ivck) ? PTR_ERR(nvmap_ivc_ctx->ivck) : -EINVAL;
		pr_err("Failed to reserve IVC queue for queue_id: %d: %d\n",
			nvmap_ivc_ctx->queue_id, err);
		goto free_ctx;
	}

	/* Now validate parameters after IVC channel is reserved */
	err = nvmap_ivc_validate_params(nvmap_ivc_ctx);
	if (err) {
		pr_err("Invalid IVC parameters: %d\n", err);
		goto unreserve_ivc;
	}

	/* Request IRQ */
	err = request_threaded_irq(nvmap_ivc_ctx->ivck->irq, nvmap_ivc_top_half,
		nvmap_ivc_bottom_half, 0, "nvmap_ivc", nvmap_ivc_ctx);
	if (err) {
		pr_err("Failed to request IRQ: %d\n", err);
		goto unreserve_ivc;
	}

	/* Reset IVC channel */
	tegra_hv_ivc_channel_reset(nvmap_ivc_ctx->ivck);

	pr_info("IVC queue initialized: queue_id=%d, max_retries=%d, max_wait_time=%d irq=%d, peer_vmid=%d, nframes=%d, frame_size=%d\n",
		nvmap_ivc_ctx->queue_id, nvmap_ivc_ctx->max_retries, nvmap_ivc_ctx->max_wait_time,
		nvmap_ivc_ctx->ivck->irq, nvmap_ivc_ctx->ivck->peer_vmid,
		nvmap_ivc_ctx->ivck->nframes, nvmap_ivc_ctx->ivck->frame_size);

	return 0;

unreserve_ivc:
	tegra_hv_ivc_unreserve(nvmap_ivc_ctx->ivck);
free_ctx:
	devm_kfree(dev, nvmap_ivc_ctx);
	nvmap_ivc_ctx = NULL;
exit:
	return err;
}

void nvmap_deinit_ivc_queue(void)
{
	if (!nvmap_ivc_ctx) {
		pr_err("IVC context not initialized\n");
		return;
	}

	xa_destroy(&nvmap_ivc_ctx->id_array);
	tegra_hv_ivc_unreserve(nvmap_ivc_ctx->ivck);
	nvmap_ivc_ctx = NULL;
}

int nvmap_sci_ipc_init(void)
{
	nvmapsciipc = kzalloc(sizeof(*nvmapsciipc), GFP_KERNEL);
	if (!nvmapsciipc)
		return -ENOMEM;
	nvmapsciipc->entries = RB_ROOT;
	mutex_init(&nvmapsciipc->mlock);

	return 0;
}

void nvmap_sci_ipc_exit(void)
{
	struct nvmap_sci_ipc_entry *e;
	struct rb_node *n;

	mutex_lock(&nvmapsciipc->mlock);
	while ((n = rb_first(&nvmapsciipc->entries))) {
		e = rb_entry(n, struct nvmap_sci_ipc_entry, entry);
		rb_erase(&e->entry, &nvmapsciipc->entries);
		kfree(e);
	}

	mutex_unlock(&nvmapsciipc->mlock);
	kfree(nvmapsciipc);
	nvmapsciipc = NULL;
}
