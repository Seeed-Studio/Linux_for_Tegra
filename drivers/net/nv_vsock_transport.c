// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

/* This module implements a custom VSOCK transport for NVIDIA platforms
 * supporting both Host-to-Guest (H2G) and Guest-to-Host (G2H) communication.
 */

#include <nvidia/conftest.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <net/af_vsock.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <soc/tegra/virt/hv-ivc.h>
#include <linux/of.h>
#include <linux/string.h>
#include <linux/xarray.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>

/* NVIDIA VSOCK transport features - implement both H2G and G2H */
#define NV_VSOCK_FEATURES (VSOCK_TRANSPORT_F_H2G | VSOCK_TRANSPORT_F_G2H)

/* Maximum queued RX data size per socket (256KB) */
#define NV_VSOCK_MAX_SOCKET_RX_SIZE	(256 * 1024)

/* Frame header flags */
#define NV_VSOCK_FRAME_FLAG_CONNECT		0x00000001
#define NV_VSOCK_FRAME_FLAG_CONNECT_RESP	0x00000002
#define NV_VSOCK_FRAME_FLAG_RESET		0x00000004
#define NV_VSOCK_FRAME_FLAG_SHUTDOWN		0x00000008
#define NV_VSOCK_SHUTDOWN_RCV			0x00000010
#define NV_VSOCK_SHUTDOWN_SEND			0x00000020
#define NV_VSOCK_FRAME_FLAG_ACK_VALID		0x00000040

struct nv_vsock_frame_hdr {
	u32 src_port;
	u32 dst_port;
	u32 len;
	u32 flags;
	u32 seq_num;
	u32 ack_num;
} __packed;

struct nv_vsock_ivc_channel {
	struct platform_device *pdev;
	u32 remote_cid;
	struct tegra_hv_ivc_cookie *ivck;
	/* Lock for IVC queue writes */
	spinlock_t tx_lock;
	struct workqueue_struct *rx_workqueue;
	struct work_struct rx_work;
	unsigned int irq;
	u32 ivc_queue_id;
	/* Counters */
	u64 write_get_next_frame_fails;
	struct dentry *debugfs_dir;
};

struct nv_vsock_transport {
	struct vsock_transport transport;
	struct platform_device *pdev;
	u32 guest_cid;
	u32 max_rx_buffer_size;
	struct xarray ivc_channels;
	struct device_node *hv_dn;
	struct dentry *debugfs_root;
	struct dentry *debugfs_sockets_dir;
};

struct nv_vsock_rx_data {
	struct list_head list;
	size_t len;
	size_t consumed;
	u8 data[];
};

struct nv_vsock_sock {
	struct vsock_sock *vsk;
	struct list_head rx_queue;
	/* Spin lock for rx_queue */
	spinlock_t rx_lock;
	/* Control event handling to avoid HOL on lock_sock */
	struct list_head ctrl_queue;
	/* Lock for ctrl_queue */
	spinlock_t ctrl_lock;
	struct work_struct ctrl_work;
	size_t queued_rx_size;
	/* TX/RX accounting */
	u64 bytes_tx;
	u64 frames_tx;
	u64 bytes_rx;
	u64 frames_rx;
	u32 next_seq_num;
	u32 next_ack_num;
	u32 last_ack_sent;
	u32 last_ack_received;
	bool connect_pending;
	/* True when no further RX data must be queued */
	bool rx_shutdown;
	/* debugfs handle for this socket */
	struct dentry *debugfs_dir;
};

static struct nv_vsock_transport nv_transport;

struct nv_vsock_ctrl_evt {
	struct list_head list;
	struct nv_vsock_frame_hdr hdr;
	struct nv_vsock_ivc_channel *channel;
	struct sock *sk;
};

static void nv_vsock_debugfs_add_channel(struct nv_vsock_ivc_channel *channel);
static void nv_vsock_debugfs_remove_channel(struct nv_vsock_ivc_channel *channel);
static void nv_vsock_debugfs_add_socket(struct nv_vsock_sock *nvs);
static void nv_vsock_debugfs_remove_socket(struct nv_vsock_sock *nvs);

static void nv_vsock_ctrl_work_fn(struct work_struct *work);

/* Wrap-safe sequence comparison */
#define nv_seq_after(a, b) ((s32)((a) - (b)) > 0)

#if defined(NV_VSOCK_TRANSPORT_STRUCT_ALLOW_CALLBACKS_HAVE_VSK_ARG) /* Linux v7.0 */
static bool nv_vsock_dgram_allow(struct vsock_sock *vsk, u32 cid, u32 port)
#else
static bool nv_vsock_dgram_allow(u32 cid, u32 port)
#endif
{
	return false;
}

#if defined(NV_VSOCK_TRANSPORT_STRUCT_ALLOW_CALLBACKS_HAVE_VSK_ARG) /* Linux v7.0 */
static bool nv_vsock_seqpacket_allow(struct vsock_sock *vsk, u32 remote_cid)
#else
static bool nv_vsock_seqpacket_allow(u32 remote_cid)
#endif
{
	return false;
}

static struct nv_vsock_sock *nv_vsock_get_transport_data(struct vsock_sock *vsk)
{
	return (struct nv_vsock_sock *)vsk->trans;
}

static int nv_vsock_init(struct vsock_sock *vsk, struct vsock_sock *psk)
{
	struct nv_vsock_sock *nvs;

	nvs = kzalloc(sizeof(*nvs), GFP_KERNEL);
	if (!nvs)
		return -ENOMEM;

	nvs->vsk = vsk;
	INIT_LIST_HEAD(&nvs->rx_queue);
	spin_lock_init(&nvs->rx_lock);
	INIT_LIST_HEAD(&nvs->ctrl_queue);
	spin_lock_init(&nvs->ctrl_lock);
	INIT_WORK(&nvs->ctrl_work, nv_vsock_ctrl_work_fn);

	nvs->connect_pending = false;
	nvs->rx_shutdown = false;

	/* Initialize counters */
	nvs->bytes_tx = 0;
	nvs->frames_tx = 0;
	nvs->bytes_rx = 0;
	nvs->frames_rx = 0;

	vsk->trans = nvs;

	return 0;
}

static void nv_vsock_purge_rx_queue(struct nv_vsock_sock *nvs)
{
	struct nv_vsock_rx_data *rx_data, *tmp;

	spin_lock(&nvs->rx_lock);
	list_for_each_entry_safe(rx_data, tmp, &nvs->rx_queue, list) {
		list_del(&rx_data->list);
		kfree(rx_data);
	}

	/* After flushing queued data prevent further queueing */
	nvs->rx_shutdown = true;
	spin_unlock(&nvs->rx_lock);
}

static void nv_vsock_purge_ctrl_queue(struct nv_vsock_sock *nvs)
{
	struct nv_vsock_ctrl_evt *evt, *tmp;

	spin_lock(&nvs->ctrl_lock);
	list_for_each_entry_safe(evt, tmp, &nvs->ctrl_queue, list) {
		list_del(&evt->list);
		if (evt->sk)
			sock_put(evt->sk);
		kfree(evt);
	}
	spin_unlock(&nvs->ctrl_lock);
}

/* Destruct transport */
static void nv_vsock_destruct(struct vsock_sock *vsk)
{
	struct nv_vsock_sock *nvs = nv_vsock_get_transport_data(vsk);

	if (!nvs)
		return;

	/* Free any pending packets */
	nv_vsock_purge_rx_queue(nvs);

	/* Cancel and drain control work, then purge events */
	cancel_work_sync(&nvs->ctrl_work);
	nv_vsock_purge_ctrl_queue(nvs);

	/* Remove debugfs for this socket */
	nv_vsock_debugfs_remove_socket(nvs);

	nvs->queued_rx_size = 0;
	kfree(nvs);
	vsk->trans = NULL;
}

/* Send frame via IVC channel */
static ssize_t nv_vsock_ivc_send_frame(struct nv_vsock_ivc_channel *channel,
				       struct nv_vsock_frame_hdr *hdr,
				       struct msghdr *msg, size_t len)
{
	u32 orig_seq_num = hdr->seq_num;
	size_t max_data_per_frame;
	size_t remaining = len;
	size_t sent_total = 0;
	struct iov_iter iter;
	size_t to_send;
	void *frame;
	int ret;

	max_data_per_frame = channel->ivck->frame_size - sizeof(*hdr);
	spin_lock(&channel->tx_lock);

	/* Use the message's existing iterator if present */
	if (msg)
		iter = msg->msg_iter;

	/* Single loop handles both zero-length control frames and data frames */
	do {
		to_send = remaining > 0 ? min(remaining, max_data_per_frame) : 0;

		frame = tegra_hv_ivc_write_get_next_frame(channel->ivck);
		if (IS_ERR(frame)) {
			channel->write_get_next_frame_fails++;
			spin_unlock(&channel->tx_lock);
			return len == 0 ? 0 : sent_total;
		}

		hdr->len = to_send;
		/* Advance sequence number for each chunked frame */
		hdr->seq_num = orig_seq_num + (len - remaining);

		memcpy(frame, hdr, sizeof(*hdr));
		if (to_send > 0 && msg) {
			ret = copy_from_iter((char *)frame + sizeof(*hdr), to_send, &iter);
			if (ret != to_send) {
				spin_unlock(&channel->tx_lock);
				return sent_total;
			}
		}

		ret = tegra_hv_ivc_write_advance(channel->ivck);
		if (ret < 0) {
			spin_unlock(&channel->tx_lock);
			return len == 0 ? 0 : sent_total;
		}

		/* Update remaining bytes */
		if (to_send > 0) {
			remaining -= to_send;
			sent_total += to_send;
		}
	} while (remaining > 0);

	spin_unlock(&channel->tx_lock);

	/* Return positive to indicate success for control frames */
	if (len == 0)
		return 1;

	return sent_total;
}

static struct nv_vsock_ivc_channel *nv_vsock_get_ivc_channel(u32 remote_cid)
{
	return xa_load(&nv_transport.ivc_channels, remote_cid);
}

static int nv_vsock_connect(struct vsock_sock *vsk)
{
	struct nv_vsock_sock *nvs = nv_vsock_get_transport_data(vsk);
	u32 remote_port = vsk->remote_addr.svm_port;
	u32 remote_cid = vsk->remote_addr.svm_cid;
	struct nv_vsock_frame_hdr frame_hdr = {};
	struct nv_vsock_ivc_channel *channel;
	ssize_t sent;

	if (!nvs)
		return -ENOTCONN;

	channel = nv_vsock_get_ivc_channel(remote_cid);
	if (!channel) {
		dev_dbg(&nv_transport.pdev->dev, "No IVC channel to CID %u\n",
			remote_cid);
		return -ENETUNREACH;
	}

	frame_hdr.src_port = vsk->local_addr.svm_port;
	frame_hdr.dst_port = remote_port;
	frame_hdr.flags = NV_VSOCK_FRAME_FLAG_CONNECT;
	frame_hdr.seq_num = 0;
	frame_hdr.ack_num = 0;

	sent = nv_vsock_ivc_send_frame(channel, &frame_hdr, NULL, 0);
	if (!sent) {
		dev_dbg(&nv_transport.pdev->dev, "Failed to send connect request to CID %u port %u\n",
			remote_cid, remote_port);
		return -ENETUNREACH;
	}

	/* Create per-socket debugfs entry now that addresses are known */
	nv_vsock_debugfs_add_socket(nvs);

	/* Mark connection as pending - VSOCK core will handle waiting */
	nvs->connect_pending = true;

	dev_dbg(&nv_transport.pdev->dev, "Connect request sent to CID %u port %u\n",
		remote_cid, remote_port);
	return 0;
}

/* Send reset response for invalid requests */
static void nv_vsock_send_reset(struct nv_vsock_ivc_channel *channel,
				struct nv_vsock_frame_hdr *frame_hdr)
{
	struct nv_vsock_frame_hdr reset_hdr = {};
	ssize_t sent;

	if (frame_hdr->flags & NV_VSOCK_FRAME_FLAG_RESET)
		return;

	reset_hdr.src_port = frame_hdr->dst_port;
	reset_hdr.dst_port = frame_hdr->src_port;
	reset_hdr.len = 0;
	reset_hdr.flags = NV_VSOCK_FRAME_FLAG_RESET;
	reset_hdr.seq_num = 0;
	reset_hdr.ack_num = 0;

	sent = nv_vsock_ivc_send_frame(channel, &reset_hdr, NULL, 0);
	if (!sent)
		dev_dbg(&channel->pdev->dev, "Failed to send reset response to CID %u port %u\n",
			channel->remote_cid, frame_hdr->src_port);
}

/* Handle connect request from remote peer */
static
void nv_vsock_handle_connect_request(struct nv_vsock_ivc_channel *channel,
				     struct nv_vsock_frame_hdr *frame_hdr,
				     struct sock *sk)
{
	struct platform_device *pdev = channel->pdev;
	struct nv_vsock_frame_hdr resp_hdr = {};
	u32 src_port = frame_hdr->src_port;
	u32 dst_port = frame_hdr->dst_port;
	struct nv_vsock_sock *child_nvs;
	struct vsock_sock *vchild;
	struct vsock_sock *vsk;
	struct sock *child;
	ssize_t sent;
	int ret;

	if (!(frame_hdr->flags & NV_VSOCK_FRAME_FLAG_CONNECT)) {
		dev_dbg(&pdev->dev, "Connect request to non-listening socket CID %u to port %u\n",
			channel->remote_cid, frame_hdr->dst_port);
		nv_vsock_send_reset(channel, frame_hdr);
		return;
	}

	vsk = vsock_sk(sk);

	if (sk_acceptq_is_full(sk)) {
		dev_dbg(&pdev->dev, "Accept queue full for CID %u port %u\n",
			channel->remote_cid, src_port);
		nv_vsock_send_reset(channel, frame_hdr);
		return;
	}

	child = vsock_create_connected(sk);
	if (!child) {
		dev_dbg(&pdev->dev, "Failed to create child socket for CID %u port %u\n",
			channel->remote_cid, src_port);
		nv_vsock_send_reset(channel, frame_hdr);
		return;
	}

	sk_acceptq_added(sk);

	lock_sock_nested(child, SINGLE_DEPTH_NESTING);

	/* Set up child socket addresses */
	vchild = vsock_sk(child);
	vsock_addr_init(&vchild->local_addr, nv_transport.guest_cid, dst_port);
	vsock_addr_init(&vchild->remote_addr, channel->remote_cid, src_port);

	/* Assign transport to child socket */
	ret = vsock_assign_transport(vchild, vsk);
	if (ret || vchild->transport != &nv_transport.transport) {
		release_sock(child);
		sk_acceptq_removed(sk);
		sock_put(child);
		dev_dbg(&pdev->dev, "Failed to assign transport to child socket\n");
		nv_vsock_send_reset(channel, frame_hdr);
		return;
	}

	/* Create per-socket debugfs for the newly connected child */
	child_nvs = nv_vsock_get_transport_data(vchild);
	if (child_nvs)
		nv_vsock_debugfs_add_socket(child_nvs);

	child->sk_state = TCP_ESTABLISHED;

	/* Insert child socket into connected table and accept queue */
	vsock_insert_connected(vchild);
	vsock_enqueue_accept(sk, child);

	/* Send connect response */
	resp_hdr.src_port = dst_port;
	resp_hdr.dst_port = src_port;
	resp_hdr.len = 0;
	resp_hdr.flags = NV_VSOCK_FRAME_FLAG_CONNECT_RESP;
	resp_hdr.seq_num = 0;
	resp_hdr.ack_num = 0;

	sent = nv_vsock_ivc_send_frame(channel, &resp_hdr, NULL, 0);
	if (!sent)
		dev_dbg(&pdev->dev, "Error sending connect response to CID %u port %u\n",
			channel->remote_cid, src_port);

	release_sock(child);

	/* Notify listening socket that a connection is ready */
	sk->sk_data_ready(sk);
}

/* Send shutdown frame */
static int nv_vsock_send_shutdown(struct vsock_sock *vsk, int mode)
{
	struct nv_vsock_sock *nvs = nv_vsock_get_transport_data(vsk);
	struct nv_vsock_frame_hdr shutdown_hdr = {};
	struct nv_vsock_ivc_channel *channel;
	ssize_t sent;

	if (!nvs)
		return -ENOTCONN;

	/* Find IVC channel for remote CID */
	channel = xa_load(&nv_transport.ivc_channels, vsk->remote_addr.svm_cid);
	if (!channel) {
		dev_dbg(&nv_transport.pdev->dev, "No IVC channel for CID %u\n",
			vsk->remote_addr.svm_cid);
		return -ENOTCONN;
	}

	/* Prepare shutdown frame */
	shutdown_hdr.src_port = vsk->local_addr.svm_port;
	shutdown_hdr.dst_port = vsk->remote_addr.svm_port;
	shutdown_hdr.len = 0;
	shutdown_hdr.flags = NV_VSOCK_FRAME_FLAG_SHUTDOWN;
	shutdown_hdr.seq_num = 0;
	shutdown_hdr.ack_num = 0;

	if (mode & RCV_SHUTDOWN)
		shutdown_hdr.flags |= NV_VSOCK_SHUTDOWN_RCV;
	if (mode & SEND_SHUTDOWN)
		shutdown_hdr.flags |= NV_VSOCK_SHUTDOWN_SEND;

	sent = nv_vsock_ivc_send_frame(channel, &shutdown_hdr, NULL, 0);
	if (!sent) {
		dev_dbg(&nv_transport.pdev->dev, "nv_vsock: Failed to send shutdown to CID %u port %u (mode %d)\n",
			vsk->remote_addr.svm_cid, vsk->remote_addr.svm_port, mode);
		return -EIO;
	}

	return 0;
}

/* Release transport */
static void nv_vsock_release(struct vsock_sock *vsk)
{
	struct sock *sk = sk_vsock(vsk);
	struct nv_vsock_sock *nvs;

	nvs = nv_vsock_get_transport_data(vsk);
	if (!nvs)
		return;

	/* For stream sockets, check if we need to close the connection */
	if (sk->sk_type == SOCK_STREAM) {
		/* If socket is still connected, we need to close it */
		if (sk->sk_state == TCP_ESTABLISHED ||
		    sk->sk_state == TCP_CLOSING) {
			/* Send shutdown to remote peer if possible */
			if (sk->sk_state == TCP_ESTABLISHED)
				(void)nv_vsock_send_shutdown(vsk,
							     SHUTDOWN_MASK);
			sock_set_flag(sk, SOCK_DONE);
		} else if (nvs->connect_pending) {
			/* Cancel pending connection */
			nvs->connect_pending = false;
			sk->sk_state = TCP_CLOSE;
			sk->sk_err = ECONNRESET;
			sk_error_report(sk);
		}
	}

	vsock_remove_sock(vsk);
}

/* Handle connect response from remote peer */
static
void nv_vsock_handle_connect_response(struct nv_vsock_ivc_channel *channel,
				      struct nv_vsock_frame_hdr *frame_hdr,
				      struct sock *sk)
{
	struct platform_device *pdev = channel->pdev;
	struct nv_vsock_sock *nvs;
	struct vsock_sock *vsk;

	if (!(frame_hdr->flags & NV_VSOCK_FRAME_FLAG_CONNECT_RESP))
		goto reset_and_return;

	vsk = sk ? vsock_sk(sk) : NULL;
	nvs = vsk ? nv_vsock_get_transport_data(vsk) : NULL;

	if (!nvs || !nvs->connect_pending || !vsk) {
		dev_dbg(&pdev->dev, "No pending connect for response CID %u port %u\n",
			channel->remote_cid, frame_hdr->src_port);
		goto reset_and_return;
	}

	/* Update socket state to established */
	sk->sk_state = TCP_ESTABLISHED;
	sk->sk_socket->state = SS_CONNECTED;

	vsock_insert_connected(vsk);
	nvs->connect_pending = false;

	/* Notify waiting process */
	sk->sk_state_change(sk);

	return;

reset_and_return:
	nv_vsock_send_reset(channel, frame_hdr);
}

static ssize_t nv_vsock_stream_dequeue(struct vsock_sock *vsk,
				       struct msghdr *msg, size_t len, int flags)
{
	struct nv_vsock_sock *nvs = nv_vsock_get_transport_data(vsk);
	struct nv_vsock_rx_data *rx_data, *tmp;
	size_t remaining = len;
	ssize_t copied = 0;
	size_t to_copy;
	size_t available;

	if (!nvs)
		return -ENOTCONN;

	spin_lock(&nvs->rx_lock);

	/* Process RX data until we've copied the requested amount or queue is empty */
	list_for_each_entry_safe(rx_data, tmp, &nvs->rx_queue, list) {
		if (remaining <= 0 || !rx_data)
			break;

		/* Calculate available data in this RX data */
		available = rx_data->len - rx_data->consumed;
		if (available == 0) {
			/* This RX data is fully consumed, remove it */
			list_del(&rx_data->list);
			kfree(rx_data);
			continue;
		}

		/* Copy data from RX data to user buffer */
		to_copy = min_t(size_t, remaining, available);
		if (memcpy_to_msg(msg, rx_data->data + rx_data->consumed, to_copy)) {
			/* Copy failed - return bytes copied so far */
			spin_unlock(&nvs->rx_lock);
			return copied;
		}

		copied += to_copy;
		remaining -= to_copy;
		nvs->queued_rx_size -= to_copy;
		nvs->next_ack_num += to_copy;
		rx_data->consumed += to_copy;
	}

	spin_unlock(&nvs->rx_lock);

	return copied;
}

static u64 nv_vsock_get_max_rx_buffer_size(void)
{
	return nv_transport.max_rx_buffer_size;
}

static u64 nv_vsock_stream_rcvhiwat(struct vsock_sock *vsk)
{
	return nv_vsock_get_max_rx_buffer_size();
}

static bool nv_vsock_can_send_data(struct nv_vsock_sock *nvs, size_t len)
{
	/* Calculate remote RX space based on what we've sent vs what's been ACKed */
	u32 bytes_sent_unacked = (u32)(READ_ONCE(nvs->next_seq_num) -
				       READ_ONCE(nvs->last_ack_received));
	u64 max_rx = nv_vsock_get_max_rx_buffer_size();

	/* If we've already filled or exceeded the peer window, do not send. */
	if (bytes_sent_unacked >= max_rx)
		return false;

	/* Prevent unsigned underflow; only allow if request fits in available space. */
	return (u64)len <= (max_rx - bytes_sent_unacked);
}

/* Stream enqueue */
static ssize_t nv_vsock_stream_enqueue(struct vsock_sock *vsk,
				       struct msghdr *msg, size_t len)
{
	struct nv_vsock_sock *nvs = nv_vsock_get_transport_data(vsk);
	struct nv_vsock_frame_hdr frame_hdr = {};
	struct nv_vsock_ivc_channel *channel;
	u32 next_ack;
	size_t sent;

	if (!nvs)
		return -ENOTCONN;

	channel = nv_vsock_get_ivc_channel(vsk->remote_addr.svm_cid);
	if (!channel) {
		/* IVC channel should have been created during probe */
		dev_err(&nv_transport.pdev->dev, "No IVC channel found for remote CID %u\n",
			vsk->remote_addr.svm_cid);
		return -ENOTCONN;
	}

	/* Check flow control - don't send if remote buffer is full */
	if (!nv_vsock_can_send_data(nvs, len)) {
		/* Remote buffer is full - return 0 to indicate no data sent */
		return 0;
	}

	/* Prepare frame header with flow control information */
	next_ack = READ_ONCE(nvs->next_ack_num);
	frame_hdr.src_port = vsk->local_addr.svm_port;
	frame_hdr.dst_port = vsk->remote_addr.svm_port;
	frame_hdr.seq_num = nvs->next_seq_num;
	frame_hdr.ack_num = next_ack;
	frame_hdr.flags = NV_VSOCK_FRAME_FLAG_ACK_VALID;  /* Always include ACK with data frames */

	/* Send frame via IVC channel */
	sent = nv_vsock_ivc_send_frame(channel, &frame_hdr, msg, len);
	if (!sent)
		return 0;

	/* Update TX-side flow control state */
	nvs->next_seq_num += sent;
	nvs->last_ack_sent = next_ack;

	/* Update TX counters */
	nvs->bytes_tx += sent;
	nvs->frames_tx += 1;

	return sent;
}

/* Stream has data */
static s64 nv_vsock_stream_has_data(struct vsock_sock *vsk)
{
	struct nv_vsock_sock *nvs = nv_vsock_get_transport_data(vsk);
	s64 data_available = 0;

	if (!nvs)
		return 0;

	/* Return amount of data queued to this socket */
	spin_lock(&nvs->rx_lock);
	data_available = nvs->queued_rx_size;
	spin_unlock(&nvs->rx_lock);

	return data_available;
}

static s64 nv_vsock_stream_has_space(struct vsock_sock *vsk)
{
	struct nv_vsock_sock *nvs = nv_vsock_get_transport_data(vsk);
	u32 bytes_sent_unacked;
	u64 max_rx;

	if (!nvs)
		return 0;

	/* Return available space based on flow control; handle wrap safely */
	max_rx = nv_vsock_get_max_rx_buffer_size();
	bytes_sent_unacked = (u32)(nvs->next_seq_num - nvs->last_ack_received);

	if (bytes_sent_unacked >= max_rx)
		return 0;

	return (s64)(max_rx - bytes_sent_unacked);
}

static bool nv_vsock_stream_is_active(struct vsock_sock *vsk)
{
	return vsk->sk.sk_state == TCP_ESTABLISHED;
}

/* Stream allow */
#if defined(NV_VSOCK_TRANSPORT_STRUCT_ALLOW_CALLBACKS_HAVE_VSK_ARG) /* Linux v7.0 */
static bool nv_vsock_stream_allow(struct vsock_sock *vsk, u32 cid, u32 port)
#else
static bool nv_vsock_stream_allow(u32 cid, u32 port)
#endif
{
	/* Allow all connections for now */
	return true;
}

static int nv_vsock_notify_poll_in(struct vsock_sock *vsk, size_t target,
				   bool *data_ready_now)
{
	struct nv_vsock_sock *nvs = nv_vsock_get_transport_data(vsk);
	s64 queued_data;

	if (!nvs) {
		*data_ready_now = false;
		return 0;
	}

	/* Check how much data is queued in the socket's RX buffer */
	queued_data = nv_vsock_stream_has_data(vsk);
	*data_ready_now = (queued_data > target);

	return 0;
}

static int nv_vsock_notify_poll_out(struct vsock_sock *vsk, size_t target,
				    bool *space_available_now)
{
	struct nv_vsock_sock *nvs = nv_vsock_get_transport_data(vsk);
	s64 available_space;

	if (!nvs) {
		*space_available_now = false;
		return 0;
	}

	/* Check if socket has enough space to receive data */
	available_space = nv_vsock_stream_has_space(vsk);
	*space_available_now = (available_space >= target);

	return 0;
}

static int nv_vsock_notify_recv_init(struct vsock_sock *vsk, size_t target,
				     struct vsock_transport_recv_notify_data *data)
{
	return 0;
}

static int nv_vsock_notify_recv_pre_block(struct vsock_sock *vsk, size_t target,
					  struct vsock_transport_recv_notify_data *data)
{
	return 0;
}

static int nv_vsock_notify_recv_pre_dequeue(struct vsock_sock *vsk, size_t target,
					    struct vsock_transport_recv_notify_data *data)
{
	return 0;
}

static bool nv_vsock_should_send_ack(struct nv_vsock_sock *nvs)
{
	/* Coalesced ACK policy:
	 * - ACK when we've consumed at least 1/8 of the advertised buffer
	 *   since the previous ACK (minimum 4KB), or
	 * - ACK when the receive queue is fully drained (free all credits).
	 */
	u32 bytes_consumed_since_last_ack = READ_ONCE(nvs->next_ack_num) -
					    READ_ONCE(nvs->last_ack_sent);
	u64 max_rx = nv_vsock_get_max_rx_buffer_size();
	u32 threshold = (u32)(max_rx / 8);

	if (bytes_consumed_since_last_ack >= threshold)
		return true;

	/* If the app drained the queue, promptly return credits. */
	return nvs->queued_rx_size == 0;
}

static void nv_vsock_send_ack(struct nv_vsock_sock *nvs)
{
	struct nv_vsock_frame_hdr ack_hdr = {};
	struct nv_vsock_ivc_channel *channel;

	channel = nv_vsock_get_ivc_channel(nvs->vsk->remote_addr.svm_cid);
	if (!channel)
		return;

	/* Send empty ACK frame */
	ack_hdr.src_port = nvs->vsk->local_addr.svm_port;
	ack_hdr.dst_port = nvs->vsk->remote_addr.svm_port;
	ack_hdr.ack_num = nvs->next_ack_num;
	ack_hdr.flags = NV_VSOCK_FRAME_FLAG_ACK_VALID;  /* This is an explicit ACK frame */

	/* Send ACK frame */
	nv_vsock_ivc_send_frame(channel, &ack_hdr, NULL, 0);

	nvs->last_ack_sent = nvs->next_ack_num;
}

/* Notify receive post dequeue */
static int nv_vsock_notify_recv_post_dequeue(struct vsock_sock *vsk, size_t target,
					     ssize_t copied, bool data_read,
					     struct vsock_transport_recv_notify_data *data)
{
	struct nv_vsock_sock *nvs = nv_vsock_get_transport_data(vsk);

	if (!nvs)
		return 0;

	/* Only send an ACK if our policy says so. */
	if (data_read && copied > 0 && nv_vsock_should_send_ack(nvs))
		nv_vsock_send_ack(nvs);

	return 0;
}

/* Get local CID */
static int nv_vsock_notify_send_init(struct vsock_sock *vsk,
				     struct vsock_transport_send_notify_data *data)
{
	return 0;
}

/* Notify send pre block */
static int nv_vsock_notify_send_pre_block(struct vsock_sock *vsk,
					  struct vsock_transport_send_notify_data *data)
{
	return 0;
}

/* Notify send pre enqueue */
static int nv_vsock_notify_send_pre_enqueue(struct vsock_sock *vsk,
					    struct vsock_transport_send_notify_data *data)
{
	return 0;
}

/* Notify send post enqueue */
static int nv_vsock_notify_send_post_enqueue(struct vsock_sock *vsk, ssize_t written,
					     struct vsock_transport_send_notify_data *data)
{
	return 0;
}

/* Notify buffer size */
static void nv_vsock_notify_buffer_size(struct vsock_sock *vsk, u64 *val)
{
	*val = nv_vsock_get_max_rx_buffer_size();
}

static u32 nv_vsock_get_local_cid(void)
{
	return nv_transport.guest_cid;
}

/* Cancel packet */
static int nv_vsock_cancel_pkt(struct vsock_sock *vsk)
{
	struct nv_vsock_sock *nvs = nv_vsock_get_transport_data(vsk);

	if (!nvs)
		return 0;

	/* Clear queues */
	nv_vsock_purge_rx_queue(nvs);

	return 0;
}

static struct nv_vsock_rx_data *nv_vsock_alloc_rx_data(size_t len, gfp_t gfp_mask)
{
	struct nv_vsock_rx_data *rx_data;

	/* Allocate struct and data buffer in one allocation */
	rx_data = kzalloc(sizeof(*rx_data) + len, gfp_mask);
	if (!rx_data)
		return NULL;

	rx_data->len = len;
	INIT_LIST_HEAD(&rx_data->list);  /* Still needed for list_head */

	return rx_data;
}

static void nv_vsock_handle_ack(struct nv_vsock_sock *nvs, u32 ack_num)
{
	u32 prev_last_ack = READ_ONCE(nvs->last_ack_received);

	/* Update our tracking of what the remote has acknowledged */
	if (nv_seq_after(ack_num, prev_last_ack)) {
		nvs->last_ack_received = ack_num;
		/* Notify writers that remote window has grown */
		if (nvs->vsk) {
			struct sock *sk = sk_vsock(nvs->vsk);

			if (sk && sk->sk_write_space)
				sk->sk_write_space(sk);
		}
	}
}

static bool nv_sock_handle_reset(struct nv_vsock_ivc_channel *channel,
				 struct nv_vsock_frame_hdr *frame_hdr,
				 struct sock *sk)
{
	struct nv_vsock_sock *nvs;
	struct vsock_sock *vsk;

	if (!(frame_hdr->flags & NV_VSOCK_FRAME_FLAG_RESET))
		return false;

	vsk = sk ? vsock_sk(sk) : NULL;
	nvs = vsk ? nv_vsock_get_transport_data(vsk) : NULL;
	if (nvs && vsk) {
		if (nvs->connect_pending) {
			/* Abort connecting socket */
			nvs->connect_pending = false;
			sk->sk_state = TCP_CLOSE;
			sk->sk_err = ECONNRESET;
			sk_error_report(sk);
		} else if (sk->sk_state == TCP_ESTABLISHED) {
			/* Close established connection */
			sk->sk_state = TCP_CLOSE;
			sk->sk_err = ECONNRESET;
			sk_error_report(sk);
			vsock_remove_sock(vsk);
		}
	}
	return true;
}

static bool nv_handle_shutdown(struct nv_vsock_ivc_channel *channel,
			       struct nv_vsock_frame_hdr *frame_hdr,
			       struct sock *sk)
{
	struct nv_vsock_sock *nvs;
	struct vsock_sock *vsk;

	/* Handle shutdown frames - only for established sockets */
	if (!(frame_hdr->flags & NV_VSOCK_FRAME_FLAG_SHUTDOWN))
		return false;

	vsk = sk ? vsock_sk(sk) : NULL;
	nvs = vsk ? nv_vsock_get_transport_data(vsk) : NULL;

	if (nvs && vsk && sk->sk_state == TCP_ESTABLISHED) {
		u32 shutdown_mode = 0;

		if (frame_hdr->flags & NV_VSOCK_SHUTDOWN_RCV)
			shutdown_mode |= RCV_SHUTDOWN;
		if (frame_hdr->flags & NV_VSOCK_SHUTDOWN_SEND)
			shutdown_mode |= SEND_SHUTDOWN;
		vsk->peer_shutdown |= shutdown_mode;

		if (vsk->peer_shutdown == SHUTDOWN_MASK) {
			if (vsock_stream_has_data(vsk) <= 0 && !sock_flag(sk, SOCK_DONE)) {
				sk->sk_state = TCP_CLOSING;
				sock_set_flag(sk, SOCK_DONE);
				sk->sk_state_change(sk);
				vsock_remove_sock(vsk);
			}
		}
		/* Notify socket state change */
		sk->sk_state_change(sk);
	} else {
		/* Not established: reset to sync state with peer */
		nv_vsock_send_reset(channel, frame_hdr);
	}
	return true;
}

/* Shutdown */
static int nv_vsock_shutdown(struct vsock_sock *vsk, int mode)
{
	struct nv_vsock_sock *nvs = nv_vsock_get_transport_data(vsk);

	if (!nvs)
		return -ENOTCONN;

	/* Send shutdown frame to remote peer */
	return nv_vsock_send_shutdown(vsk, mode);
}

/* IVC interrupt handler */
static irqreturn_t nv_vsock_ivc_interrupt(int irq, void *data)
{
	struct nv_vsock_ivc_channel *channel = data;

	if (tegra_hv_ivc_channel_notified(channel->ivck) != 0)
		return IRQ_HANDLED;

	/* If there's data available, queue RX work to process it */
	if (tegra_hv_ivc_can_read(channel->ivck))
		queue_work(channel->rx_workqueue, &channel->rx_work);

	return IRQ_HANDLED;
}

static void nv_vsock_deliver_frame_to_socket(struct nv_vsock_ivc_channel *channel,
					     struct nv_vsock_sock *nvs,
					     u32 dst_port,
					     struct nv_vsock_rx_data *rx_data)
{
	struct platform_device *pdev = channel->pdev;
	struct sock *sk = &nvs->vsk->sk;

	/* Check if adding this RX data would exceed the queue size limit */
	spin_lock(&nvs->rx_lock);
	if (nvs->rx_shutdown) {
		/* Do not queue further data once shutdown has been observed */
		spin_unlock(&nvs->rx_lock);
		dev_dbg(&pdev->dev, "Dropping RX data on shutdown socket for port %u (len: %zu)\n",
			dst_port, rx_data->len);
		kfree(rx_data);
		return;
	}

	if (nvs->queued_rx_size + rx_data->len >
	    nv_vsock_get_max_rx_buffer_size()) {
		dev_warn(&pdev->dev, "Socket RX queue full for port %u (queued: %zu, frame: %zu, limit: %llu) - queuing for later\n",
			 dst_port, nvs->queued_rx_size, rx_data->len,
			 nv_vsock_get_max_rx_buffer_size());
		spin_unlock(&nvs->rx_lock);

		/* Queue delayed work to retry later */
		queue_work(channel->rx_workqueue, &channel->rx_work);
		return;
	}

	/* Add RX data to socket's RX queue and update queued size */
	list_add_tail(&rx_data->list, &nvs->rx_queue);
	nvs->queued_rx_size += rx_data->len;
	/* Update RX counters */
	nvs->bytes_rx += rx_data->len;
	nvs->frames_rx += 1;
	spin_unlock(&nvs->rx_lock);

	/* Notify socket of data availability */
	vsock_data_ready(sk);
}

static
void nv_vsock_handle_ctrl_frame_locked(struct nv_vsock_ivc_channel *channel,
				       struct nv_vsock_frame_hdr *frame_hdr,
				       struct sock *sk)
{
	int curr_state;

	curr_state = READ_ONCE(sk->sk_state);
	switch (curr_state) {
	case TCP_LISTEN:
		nv_vsock_handle_connect_request(channel, frame_hdr, sk);
		break;
	case TCP_SYN_SENT:
		nv_vsock_handle_connect_response(channel, frame_hdr, sk);
		break;
	case TCP_CLOSING:
		(void)nv_sock_handle_reset(channel, frame_hdr, sk);
		break;
	default:
		nv_vsock_send_reset(channel, frame_hdr);
		break;
	}
}

static void nv_vsock_ctrl_work_fn(struct work_struct *work)
{
	struct nv_vsock_ctrl_evt *evt;
	struct nv_vsock_sock *nvs;

	nvs = container_of(work, struct nv_vsock_sock, ctrl_work);

	for (;;) {
		spin_lock(&nvs->ctrl_lock);
		evt = list_first_entry_or_null(&nvs->ctrl_queue,
					       struct nv_vsock_ctrl_evt, list);
		if (evt)
			list_del(&evt->list);
		spin_unlock(&nvs->ctrl_lock);

		if (!evt)
			break;

		if (evt->sk) {
			lock_sock(evt->sk);
			nv_vsock_handle_ctrl_frame_locked(evt->channel, &evt->hdr, evt->sk);
			release_sock(evt->sk);
			sock_put(evt->sk);
		}

		kfree(evt);
	}
}

static bool nv_vsock_queue_ctrl_event(struct nv_vsock_ivc_channel *channel,
				      struct nv_vsock_sock *nvs,
				      struct nv_vsock_frame_hdr *frame_hdr,
				      struct sock *sk)
{
	struct nv_vsock_ctrl_evt *evt;

	/* Allocate event and enqueue; fallback to sync handling on failure */
	evt = kzalloc(sizeof(*evt), GFP_ATOMIC);
	if (!evt)
		return false;

	evt->hdr = *frame_hdr;
	evt->channel = channel;
	evt->sk = sk;
	if (evt->sk)
		sock_hold(evt->sk);

	spin_lock(&nvs->ctrl_lock);
	list_add_tail(&evt->list, &nvs->ctrl_queue);
	spin_unlock(&nvs->ctrl_lock);

	schedule_work(&nvs->ctrl_work);

	return true;
}

/* IVC RX work function */
static void nv_vsock_ivc_rx_work(struct work_struct *work)
{
	struct nv_vsock_ivc_channel *channel;
	struct nv_vsock_rx_data *rx_data;
	struct platform_device *pdev;
	u32 src_port, dst_port;
	size_t payload_len;
	int ret;

	channel = container_of(work, struct nv_vsock_ivc_channel, rx_work);
	pdev = channel->pdev;
	/* Disable IRQ while polling */
	disable_irq(channel->irq);

	/* Process all available frames */
	while (tegra_hv_ivc_can_read(channel->ivck)) {
		struct nv_vsock_frame_hdr *frame_hdr;
		struct sockaddr_vm src_addr, dst_addr;
		struct nv_vsock_sock *nvs;
		struct vsock_sock *vsk;
		struct sock *sk;
		int curr_state;
		void *frame;

		frame = tegra_hv_ivc_read_get_next_frame(channel->ivck);
		if (IS_ERR(frame)) {
			queue_work(channel->rx_workqueue, &channel->rx_work);
			break;
		}

		frame_hdr = frame;
		src_port = frame_hdr->src_port;
		dst_port = frame_hdr->dst_port;
		payload_len = frame_hdr->len;

		/* Look up socket once for this frame using vsock core */
		vsock_addr_init(&src_addr, channel->remote_cid, src_port);
		vsock_addr_init(&dst_addr, nv_transport.guest_cid, dst_port);
		sk = vsock_find_connected_socket(&src_addr, &dst_addr);
		if (!sk)
			sk = vsock_find_bound_socket(&dst_addr);
		vsk = sk ? vsock_sk(sk) : NULL;
		nvs = (vsk && vsk->transport == &nv_transport.transport) ?
			nv_vsock_get_transport_data(vsk) : NULL;
		/* If no socket found: send RESET and continue */
		if (!sk) {
			nv_vsock_send_reset(channel, frame_hdr);
			goto advance_and_continue;
		}

		curr_state = READ_ONCE(sk->sk_state);

		/* At this point we have a socket. Optimize established fast-path first. */
		if (curr_state == TCP_ESTABLISHED) {
			if (nvs && (frame_hdr->flags & NV_VSOCK_FRAME_FLAG_ACK_VALID))
				nv_vsock_handle_ack(nvs, frame_hdr->ack_num);
			goto process_frame;
		} else {
			/* Process connect response inline to avoid ordering issues */
			if (curr_state == TCP_SYN_SENT) {
				lock_sock(sk);
				nv_vsock_handle_connect_response(channel, frame_hdr, sk);
				release_sock(sk);
			} else {
				if (!nvs || !nv_vsock_queue_ctrl_event(channel, nvs,
								       frame_hdr, sk)) {
					/* Fallback to synchronous handling if enqueue failed */
					lock_sock(sk);
					nv_vsock_handle_ctrl_frame_locked(channel,
									  frame_hdr,
									  sk);
					release_sock(sk);
				}
			}
			goto advance_and_continue;
		}

process_frame:

		if (!frame_hdr->seq_num && !payload_len) {
			if (frame_hdr->flags & NV_VSOCK_FRAME_FLAG_SHUTDOWN)
				nv_handle_shutdown(channel, frame_hdr, sk);

			if (frame_hdr->flags & NV_VSOCK_FRAME_FLAG_RESET)
				nv_sock_handle_reset(channel, frame_hdr, sk);

			/* If we get here with anything other than an ACK it's invalid */
			if (!(frame_hdr->flags & NV_VSOCK_FRAME_FLAG_ACK_VALID))
				nv_vsock_send_reset(channel, frame_hdr);

			goto advance_and_continue;
		}
		/* This is a data frame - require a valid, established socket */
		if (!nvs || !vsk) {
			dev_dbg(&pdev->dev, "nv_vsock: No socket for data frame CID %u to port %u\n",
				channel->remote_cid, frame_hdr->dst_port);
			nv_vsock_send_reset(channel, frame_hdr);
			goto advance_and_continue;
		}

		/* Allocate RX data for the payload (not including frame header) */
		rx_data = nv_vsock_alloc_rx_data(payload_len, GFP_ATOMIC);
		if (!rx_data) {
			queue_work(channel->rx_workqueue, &channel->rx_work);
			sock_put(sk);
			break;
		}

		/* Copy only the payload data (skip frame header) */
		memcpy(rx_data->data, (char *)frame + sizeof(*frame_hdr), payload_len);

		/* Deliver frame to the appropriate socket based on port numbers */
		nv_vsock_deliver_frame_to_socket(channel, nvs, dst_port, rx_data);

advance_and_continue:
		if (sk)
			sock_put(sk);
		ret = tegra_hv_ivc_read_advance(channel->ivck);
		if (ret < 0)
			break;
	}
	/* Re-enable IRQ after processing all frames */
	enable_irq(channel->irq);
}
/* Create new IVC channel for remote CID */
static int nv_vsock_create_ivc_channel(struct platform_device *pdev,
				       u32 remote_cid,
				       u32 ivc_queue_id)
{
	struct nv_vsock_ivc_channel *channel;
	struct tegra_hv_ivc_cookie *ivck;
	const char *irq_name;
	int ret = -ENOMEM;

	ivck = tegra_hv_ivc_reserve(nv_transport.hv_dn, ivc_queue_id, NULL);
	if (IS_ERR(ivck)) {
		dev_err(&pdev->dev, "Failed to reserve IVC channel %u: %ld\n",
			ivc_queue_id, PTR_ERR(ivck));
		return PTR_ERR(ivck);
	}

	channel = kzalloc(sizeof(*channel), GFP_KERNEL);
	if (!channel)
		goto err_unreserve;

	channel->pdev = pdev;
	channel->remote_cid = remote_cid;
	channel->ivck = ivck;
	channel->irq = ivck->irq;
	channel->ivc_queue_id = ivc_queue_id;

	/* Create dedicated workqueue for this channel */
	{
		char wq_name[32];

		snprintf(wq_name, sizeof(wq_name), "nv_vsock_rx-%u",
			 ivc_queue_id);
		channel->rx_workqueue = create_singlethread_workqueue(wq_name);
		if (!channel->rx_workqueue)
			goto err_channel;
	}

	INIT_WORK(&channel->rx_work, nv_vsock_ivc_rx_work);
	spin_lock_init(&channel->tx_lock);

	tegra_hv_ivc_channel_reset(ivck);

	irq_name = devm_kasprintf(&nv_transport.pdev->dev, GFP_KERNEL,
				  "nv_vsock_ivc-%u", ivc_queue_id);
	if (!irq_name)
		goto err_channel;

	ret = devm_request_irq(&nv_transport.pdev->dev, channel->irq,
			       nv_vsock_ivc_interrupt, 0,
			       irq_name, channel);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request IRQ %d for channel %u: %d\n",
			channel->irq, ivc_queue_id, ret);
		goto err_channel;
	}

	ret = xa_insert(&nv_transport.ivc_channels, remote_cid, channel, GFP_KERNEL);
	if (ret) {
		dev_err(&pdev->dev, "Failed to insert channel for CID %u: %d\n",
			remote_cid, ret);
		goto err_irq;
	}

	dev_dbg(&pdev->dev, "Created IVC channel %u for remote CID %u\n",
		ivc_queue_id, remote_cid);
	/* Add debugfs entry for this channel */
	nv_vsock_debugfs_add_channel(channel);

	return 0;

err_irq:
	devm_free_irq(&nv_transport.pdev->dev, channel->irq, channel);
err_channel:
	kfree(channel);
err_unreserve:
	tegra_hv_ivc_unreserve(ivck);

	return ret;
}

static void nv_vsock_destroy_ivc_channel(struct nv_vsock_ivc_channel *channel)
{
	if (!channel)
		return;

	xa_erase(&nv_transport.ivc_channels, channel->remote_cid);
	devm_free_irq(&nv_transport.pdev->dev, channel->irq, channel);
	nv_vsock_debugfs_remove_channel(channel);
	cancel_work_sync(&channel->rx_work);
	destroy_workqueue(channel->rx_workqueue);
	tegra_hv_ivc_unreserve(channel->ivck);
	kfree(channel);
}

/* Parse IVC queue mappings from device tree and create IVC channels */
static int nv_vsock_parse_and_create_ivc_channels(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *ivc_node;
	struct property *prop;
	u32 cid, ivc_queue_id;
	int ret;

	/* Find the ivc-queues node */
	ivc_node = of_get_child_by_name(np, "ivc-queues");
	if (!ivc_node) {
		dev_err(&pdev->dev, "No IVC queue mappings found in device tree\n");
		return -ENODEV;
	}

	/* Parse each CID mapping and create IVC channel */
	for (prop = ivc_node->properties; prop; prop = prop->next) {
		/* Check if this is a CID property (e.g., "cid-3") */
		if (strncmp(prop->name, "cid-", 4) != 0)
			continue;

		/* Extract CID from property name (e.g., "cid-3" -> 3) */
		if (sscanf(prop->name, "cid-%u", &cid) != 1) {
			dev_warn(&pdev->dev, "Invalid CID property name: %s\n", prop->name);
			continue;
		}

		/* Read the IVC queue ID value */
		if (of_property_read_u32_index(ivc_node, prop->name, 1, &ivc_queue_id) != 0) {
			dev_warn(&pdev->dev, "Failed to read IVC queue ID for CID %u\n", cid);
			continue;
		}

		/* Create IVC channel for this CID */
		ret = nv_vsock_create_ivc_channel(pdev, cid, ivc_queue_id);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to create IVC channel for CID %u: %d\n",
				cid, ret);
			of_node_put(ivc_node);
			return ret;
		}
	}

	of_node_put(ivc_node);
	return 0;
}

/* debugfs helpers */
static void nv_vsock_debugfs_add_channel(struct nv_vsock_ivc_channel *channel)
{
	char name[32];

	if (!nv_transport.debugfs_root)
		return;

	snprintf(name, sizeof(name), "ivc-queue-%u", channel->ivc_queue_id);
	channel->debugfs_dir = debugfs_create_dir(name, nv_transport.debugfs_root);
	if (!channel->debugfs_dir)
		return;

	debugfs_create_u32("remote_cid", 0444, channel->debugfs_dir,
			   &channel->remote_cid);
	debugfs_create_u32("queue_id", 0444, channel->debugfs_dir,
			   &channel->ivc_queue_id);
	debugfs_create_u32("irq", 0444, channel->debugfs_dir,
			   &channel->irq);
	debugfs_create_u32("frame_size", 0444, channel->debugfs_dir,
			   &channel->ivck->frame_size);
	debugfs_create_u32("nframes", 0444, channel->debugfs_dir,
			   &channel->ivck->nframes);
	debugfs_create_u64("write_get_next_frame_fails", 0444, channel->debugfs_dir,
			   &channel->write_get_next_frame_fails);
}

static void nv_vsock_debugfs_remove_channel(struct nv_vsock_ivc_channel *channel)
{
	debugfs_remove_recursive(channel->debugfs_dir);
	channel->debugfs_dir = NULL;
}

static void nv_vsock_debugfs_init(void)
{
	/* Create root directory */
	nv_transport.debugfs_root = debugfs_create_dir("nv_vsock_transport", NULL);
	if (!nv_transport.debugfs_root)
		return;

	/* Top-level transport attributes */
	debugfs_create_u32("guest_cid", 0444, nv_transport.debugfs_root,
			   &nv_transport.guest_cid);
	debugfs_create_u32("max_rx_size", 0444, nv_transport.debugfs_root,
			   &nv_transport.max_rx_buffer_size);

	/* Sockets subdir */
	nv_transport.debugfs_sockets_dir =
		debugfs_create_dir("sockets", nv_transport.debugfs_root);
}

static void nv_vsock_debugfs_cleanup(void)
{
	debugfs_remove_recursive(nv_transport.debugfs_root);
	nv_transport.debugfs_root = NULL;
	nv_transport.debugfs_sockets_dir = NULL;
}

static void nv_vsock_debugfs_add_socket(struct nv_vsock_sock *nvs)
{
	u32 lcid = 0, lport = 0, rcid = 0, rport = 0;
	char name[64];

	if (!nv_transport.debugfs_sockets_dir)
		return;
	if (!nvs)
		return;
	if (nvs->debugfs_dir)
		return;

	if (nvs->vsk) {
		lcid = nvs->vsk->local_addr.svm_cid;
		lport = nvs->vsk->local_addr.svm_port;
		rcid = nvs->vsk->remote_addr.svm_cid;
		rport = nvs->vsk->remote_addr.svm_port;
	} else {
		return;
	}

	snprintf(name, sizeof(name), "sock-l%u:%u-r%u:%u",
		 lcid, lport, rcid, rport);

	nvs->debugfs_dir = debugfs_create_dir(name, nv_transport.debugfs_sockets_dir);
	if (!nvs->debugfs_dir)
		return;

	debugfs_create_u64("bytes_tx", 0444, nvs->debugfs_dir, &nvs->bytes_tx);
	debugfs_create_u64("frames_tx", 0444, nvs->debugfs_dir, &nvs->frames_tx);
	debugfs_create_u64("bytes_rx", 0444, nvs->debugfs_dir, &nvs->bytes_rx);
	debugfs_create_u64("frames_rx", 0444, nvs->debugfs_dir, &nvs->frames_rx);
	debugfs_create_u32("seq_num", 0444, nvs->debugfs_dir, &nvs->next_seq_num);
	debugfs_create_u32("ack_num", 0444, nvs->debugfs_dir, &nvs->next_ack_num);
	debugfs_create_u32("last_ack_sent", 0444, nvs->debugfs_dir,
			   &nvs->last_ack_sent);
	debugfs_create_u32("last_ack_received", 0444, nvs->debugfs_dir,
			   &nvs->last_ack_received);
	debugfs_create_u8("state", 0444, nvs->debugfs_dir,
			  (u8 *)&nvs->vsk->sk.sk_state);
}

static void nv_vsock_debugfs_remove_socket(struct nv_vsock_sock *nvs)
{
	debugfs_remove_recursive(nvs->debugfs_dir);
	nvs->debugfs_dir = NULL;
}

static const struct vsock_transport nv_vsock_ops = {
	.module = THIS_MODULE,
	.init = nv_vsock_init,
	.destruct = nv_vsock_destruct,
	.release = nv_vsock_release,
	.cancel_pkt = nv_vsock_cancel_pkt,
	.connect = nv_vsock_connect,
	.dgram_allow = nv_vsock_dgram_allow,
	.stream_dequeue = nv_vsock_stream_dequeue,
	.stream_enqueue = nv_vsock_stream_enqueue,
	.stream_has_data = nv_vsock_stream_has_data,
	.stream_has_space = nv_vsock_stream_has_space,
	.stream_rcvhiwat = nv_vsock_stream_rcvhiwat,
	.stream_is_active = nv_vsock_stream_is_active,
	.stream_allow = nv_vsock_stream_allow,
	.seqpacket_allow = nv_vsock_seqpacket_allow,
	.notify_poll_in = nv_vsock_notify_poll_in,
	.notify_poll_out = nv_vsock_notify_poll_out,
	.notify_recv_init = nv_vsock_notify_recv_init,
	.notify_recv_pre_block = nv_vsock_notify_recv_pre_block,
	.notify_recv_pre_dequeue = nv_vsock_notify_recv_pre_dequeue,
	.notify_recv_post_dequeue = nv_vsock_notify_recv_post_dequeue,
	.notify_send_init = nv_vsock_notify_send_init,
	.notify_send_pre_block = nv_vsock_notify_send_pre_block,
	.notify_send_pre_enqueue = nv_vsock_notify_send_pre_enqueue,
	.notify_send_post_enqueue = nv_vsock_notify_send_post_enqueue,
	.notify_buffer_size = nv_vsock_notify_buffer_size,
	.shutdown = nv_vsock_shutdown,
	.get_local_cid = nv_vsock_get_local_cid,
};
/* Platform driver probe */
static int nv_vsock_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;

	nv_transport.pdev = pdev;

	/* Parse guest CID from device tree */
	ret = of_property_read_u32(np, "guest-cid", &nv_transport.guest_cid);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to read guest-cid: %d\n", ret);
		return ret;
	}

	/* Parse max RX buffer size from device tree (default to 256KB) */
	ret = of_property_read_u32(np, "max-rx-buffer-size", &nv_transport.max_rx_buffer_size);
	if (ret < 0) {
		/* Default to 256KB if not specified */
		nv_transport.max_rx_buffer_size = NV_VSOCK_MAX_SOCKET_RX_SIZE;
		dev_dbg(&pdev->dev, "Using default max RX buffer size: %u bytes\n",
			nv_transport.max_rx_buffer_size);
	} else {
		/* Validate that the size is a power of 2 */
		if (!is_power_of_2(nv_transport.max_rx_buffer_size)) {
			dev_warn(&pdev->dev, "max-rx-buffer-size (%u) is not a power of 2, using default: %u bytes\n",
				 nv_transport.max_rx_buffer_size, NV_VSOCK_MAX_SOCKET_RX_SIZE);
			nv_transport.max_rx_buffer_size = NV_VSOCK_MAX_SOCKET_RX_SIZE;
		} else {
			dev_info(&pdev->dev, "Max RX buffer size: %u bytes\n",
				 nv_transport.max_rx_buffer_size);
		}
	}

	nv_transport.transport = nv_vsock_ops;

	xa_init(&nv_transport.ivc_channels);

	/* Get hypervisor device node */
	nv_transport.hv_dn = of_find_compatible_node(NULL, NULL, "nvidia,tegra-hv");
	if (!nv_transport.hv_dn) {
		dev_err(&pdev->dev, "Failed to find tegra-hypervisor device node\n");
		return -ENODEV;
	}
	/* Initialize debugfs after basic transport fields are ready */
	nv_vsock_debugfs_init();

	/* Parse IVC queue mappings and create IVC channels */
	ret = nv_vsock_parse_and_create_ivc_channels(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to parse and create IVC channels: %d\n", ret);
		of_node_put(nv_transport.hv_dn);
		return ret;
	}

	/* Register transport with both H2G and G2H features */
	ret = vsock_core_register(&nv_transport.transport, NV_VSOCK_FEATURES);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register transport: %d\n", ret);
		of_node_put(nv_transport.hv_dn);
		return ret;
	}

	/* Transport is now active and ready to use */
	return 0;
}

/* Platform driver remove */
static int _nv_vsock_remove(struct platform_device *pdev)
{
	struct nv_vsock_ivc_channel *channel;
	unsigned long index;

	vsock_core_unregister(&nv_transport.transport);

	xa_for_each(&nv_transport.ivc_channels, index, channel) {
		nv_vsock_destroy_ivc_channel(channel);
	}

	xa_destroy(&nv_transport.ivc_channels);
	/* Remove debugfs root first to avoid stale entries */
	nv_vsock_debugfs_cleanup();

	if (nv_transport.hv_dn)
		of_node_put(nv_transport.hv_dn);

	return 0;
}

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static void nv_vsock_remove(struct platform_device *pdev)
{
	(void)_nv_vsock_remove(pdev);
}
#else
static int nv_vsock_remove(struct platform_device *pdev)
{
	return _nv_vsock_remove(pdev);
}
#endif

/* Platform driver structure */
static const struct of_device_id nv_vsock_of_match[] = {
	{ .compatible = "nvidia,nv-vsock" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, nv_vsock_of_match);

static struct platform_driver nv_vsock_driver = {
	.probe = nv_vsock_probe,
	.remove = nv_vsock_remove,
	.driver = {
		.name = "nv-vsock",
		.of_match_table = nv_vsock_of_match,
	},
};

module_platform_driver(nv_vsock_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_DESCRIPTION("NVIDIA Virtual Socket (nv_vsock) transport");
MODULE_VERSION("1.0");
