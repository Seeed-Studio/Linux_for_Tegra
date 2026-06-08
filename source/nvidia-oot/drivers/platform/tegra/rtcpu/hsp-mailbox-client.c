// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "linux/tegra-hsp-combo.h"

#include <linux/version.h>

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/mailbox_client.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/err.h>

#include "soc/tegra/camrtc-commands.h"

typedef struct mbox_client mbox_client;
struct camrtc_hsp_mbox {
	struct mbox_client client;
	struct mbox_chan *chan;
};

struct camrtc_hsp_op;

struct camrtc_hsp {
	const struct camrtc_hsp_op *op;
	struct camrtc_hsp_mbox rx;
	struct camrtc_hsp_mbox tx;
	u32 cookie;
	spinlock_t sendlock;
	void (*group_notify)(struct device *dev, u16 group);
	struct device dev;
	struct mutex mutex;
	struct completion emptied;
	wait_queue_head_t response_waitq;
	atomic_t response;
	long timeout;
	/* callback function for panic message */
	void (*panic_callback)(struct device *dev);
};

struct camrtc_hsp_op {
	int (*send)(struct camrtc_hsp *, int msg, long *timeout);
	void (*group_ring)(struct camrtc_hsp *, u16 group);
	int (*sync)(struct camrtc_hsp *, long *timeout);
	int (*resume)(struct camrtc_hsp *, long *timeout);
	int (*suspend)(struct camrtc_hsp *, long *timeout);
	int (*bye)(struct camrtc_hsp *, long *timeout);
	int (*ch_setup)(struct camrtc_hsp *, dma_addr_t iova, long *timeout);
	int (*ping)(struct camrtc_hsp *, u32 data, long *timeout);
	int (*get_fw_hash)(struct camrtc_hsp *, u32 index, long *timeout);
	int (*set_operating_point)(struct camrtc_hsp *, u32 operating_point, long *timeout);
};

/**
 * @brief Registers a callback function to be called when a panic message is received
 *
 * This function registers a callback function that will be called when a panic message
 * is received from the RCE.
 * - Validates the HSP context pointer
 * - Sets the panic_callback function pointer
 *
 * @param[in] camhsp         Pointer to the camera HSP context
 *                           Valid value: non-NULL
 * @param[in] panic_callback Function to be called when a panic message is received
 *                           Valid value: non-NULL function pointer or NULL to clear
 *
 * @retval 0         On successful registration
 * @retval -EINVAL   If the HSP context is NULL
 */
int camrtc_hsp_set_panic_callback(struct camrtc_hsp *camhsp,
		void (*panic_callback)(struct device *dev))
{
	if (camhsp == NULL) {
		dev_err(&camhsp->dev, "%s: camhsp is NULL!\n", __func__);
		return -EINVAL;
	}

	camhsp->panic_callback = panic_callback;
	return 0;
}
EXPORT_SYMBOL(camrtc_hsp_set_panic_callback);

/**
 * @brief Sends a request message over the HSP mailbox
 *
 * This function sends a request message over the HSP mailbox and handles error reporting.
 * It acts as a wrapper for the implementation-specific send operation.
 * - Calls the implementation-specific send operation using @ref camhsp->op->send()
 * - Performs error reporting using @ref dev_err() or @ref dev_dbg() based on the return value
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 * @param[in] request Request message to send
 *                    Valid value: any integer value
 * @param[in,out] timeout Pointer to timeout value in jiffies
 *                       Valid value: non-NULL
 *
 * @retval >=0      On successful transmission
 * @retval -ETIME   If the mailbox is not empty within the timeout period
 * @retval -EINVAL  If the mailbox channel is invalid
 * @retval -ENOBUFS If there is no space left in the mailbox message queue
 */
static int camrtc_hsp_send(struct camrtc_hsp *camhsp,
		int request, long *timeout)
{
	int ret = camhsp->op->send(camhsp, request, timeout);

	if (ret == -ETIME) {
		dev_err(&camhsp->dev,
			"request 0x%08x: empty mailbox timeout\n", request);
	} else if (ret == -EINVAL) {
		dev_err(&camhsp->dev,
			"request 0x%08x: invalid mbox channel\n", request);
	} else if (ret == -ENOBUFS) {
		dev_err(&camhsp->dev,
			"request 0x%08x: no space left in mbox msg queue\n", request);
	} else
		dev_dbg(&camhsp->dev,
			"request sent: 0x%08x\n", request);

	return ret;
}

/**
 * @brief Receives a response message from the HSP mailbox
 *
 * This function waits for a response message from the HSP mailbox within
 * the specified timeout period.
 * - Waits for a response using @ref wait_event_timeout()
 * - Atomically exchanges the response value using @ref atomic_xchg()
 * - Reports a timeout error using @ref dev_err() if no response is received
 * - Logs the response using @ref dev_dbg()
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 * @param[in] command The original command sent
 *                    Valid value: any integer value
 * @param[in,out] timeout Pointer to timeout value in jiffies
 *                       Valid value: non-NULL
 *
 * @retval >=0         The response value on success
 * @retval -ETIMEDOUT  If no response is received within the timeout period
 */
static int camrtc_hsp_recv(struct camrtc_hsp *camhsp,
		int command, long *timeout)
{
	int response;

	*timeout = wait_event_timeout(
		camhsp->response_waitq,
		(response = atomic_xchg(&camhsp->response, -1)) >= 0,
		*timeout);
	if (*timeout <= 0) {
		dev_err(&camhsp->dev,
			"request 0x%08x: response timeout\n", command);
		return -ETIMEDOUT;
	}

	dev_dbg(&camhsp->dev, "request 0x%08x: response 0x%08x\n",
		command, response);

	return response;
}

/**
 * @brief Sends a command and waits for a response from the HSP mailbox
 *
 * This function combines sending a command and receiving a response in a single call.
 * - Sends the command using @ref camrtc_hsp_send()
 * - If the send operation is successful, waits for a response using @ref camrtc_hsp_recv()
 *
 * @param[in] camhsp   Pointer to the camera HSP context
 *                     Valid value: non-NULL
 * @param[in] command  Command to send
 *                     Valid value: any integer value
 * @param[in,out] timeout  Pointer to timeout value in jiffies
 *                        Valid value: non-NULL
 *
 * @retval >=0         The response value on success
 * @retval <0          Error code from @ref camrtc_hsp_send() or @ref camrtc_hsp_recv()
 */
static int camrtc_hsp_sendrecv(struct camrtc_hsp *camhsp,
		int command, long *timeout)
{
	int response;
	response = camrtc_hsp_send(camhsp, command, timeout);
	if (response >= 0)
		response = camrtc_hsp_recv(camhsp, command, timeout);

	return response;
}

/* ---------------------------------------------------------------------- */
/* Protocol nvidia,tegra-camrtc-hsp-vm */

/**
 * @brief Handles notification of mailbox receive events
 *
 * This function is called by the mailbox framework when data is received.
 * It processes the received message and performs the appropriate action:
 * - Retrieves the HSP context using @ref dev_get_drvdata()
 * - Extracts status and group information from the message
 * - Validates the HSP context
 * - Handles unknown messages using @ref dev_dbg()
 * - Calls the group notification callback if a group notification is received
 * - For response messages, sets the response and wakes up waiters using @ref atomic_set() and @ref wake_up()
 *
 * @param[in] cl    Pointer to the mailbox client
 *                  Valid value: non-NULL
 * @param[in] data  Received message data
 *                  Valid value: any value
 */
static void camrtc_hsp_rx_full_notify(mbox_client *cl, void *data)
{
	struct camrtc_hsp *camhsp = dev_get_drvdata(cl->dev);
	u32 status, group;

	u32 msg = (u32) (unsigned long) data;
	status = CAMRTC_HSP_SS_FW_MASK;
	status >>= CAMRTC_HSP_SS_FW_SHIFT;
	group = status & CAMRTC_HSP_SS_IVC_MASK;

	if (camhsp == NULL) {
		dev_warn(cl->dev, "%s: camhsp is NULL!\n", __func__);
		return;
	}

	if (CAMRTC_HSP_MSG_ID(msg) == CAMRTC_HSP_UNKNOWN)
		dev_dbg(&camhsp->dev, "request message unknown 0x%08x\n", msg);

	if (group != 0)
		camhsp->group_notify(camhsp->dev.parent, (u16)group);

	/* Other interrupt bits are ignored for now */

	if (CAMRTC_HSP_MSG_ID(msg) == CAMRTC_HSP_IRQ) {
		/* We are done here */
	} else if (CAMRTC_HSP_MSG_ID(msg) == CAMRTC_HSP_PANIC) {
		dev_err(&camhsp->dev, "%s: receive CAMRTC_HSP_PANIC message!\n", __func__);
		if (camhsp->panic_callback != NULL) {
			camhsp->panic_callback(camhsp->dev.parent);
		} else {
			dev_warn(&camhsp->dev, "%s: No panic callback function is registered.\n", __func__);
		}
	} else if (CAMRTC_HSP_MSG_ID(msg) < CAMRTC_HSP_HELLO) {
		/* Rest of the unidirectional messages are now ignored */
	} else {
		atomic_set(&camhsp->response, msg);
		wake_up(&camhsp->response_waitq);
	}
}

/**
 * @brief Handles notification of mailbox transmit completion
 *
 * This function is called by the mailbox framework when the transmit queue
 * is emptied. It signals completion to any waiting threads.
 * - Completes the "emptied" completion using @ref complete()
 *
 * @param[in] cl          Pointer to the mailbox client
 *                        Valid value: non-NULL
 * @param[in] data        Data from the mailbox controller
 *                        Valid value: any value
 * @param[in] empty_value Completion status
 *                        Valid value: any value
 */
static void camrtc_hsp_tx_empty_notify(mbox_client *cl, void *data, int empty_value)
{
	struct camrtc_hsp *camhsp = dev_get_drvdata(cl->dev);

	(void)empty_value;	/* ignored */

	complete(&camhsp->emptied);
}

static int camrtc_hsp_vm_send(struct camrtc_hsp *camhsp,
		int request, long *timeout);
static void camrtc_hsp_vm_group_ring(struct camrtc_hsp *camhsp, u16 group);
static void camrtc_hsp_vm_send_irqmsg(struct camrtc_hsp *camhsp);
static int camrtc_hsp_vm_sync(struct camrtc_hsp *camhsp, long *timeout);
static int camrtc_hsp_vm_hello(struct camrtc_hsp *camhsp, long *timeout);
static int camrtc_hsp_vm_protocol(struct camrtc_hsp *camhsp, long *timeout);
static int camrtc_hsp_vm_resume(struct camrtc_hsp *camhsp, long *timeout);
static int camrtc_hsp_vm_suspend(struct camrtc_hsp *camhsp, long *timeout);
static int camrtc_hsp_vm_bye(struct camrtc_hsp *camhsp, long *timeout);
static int camrtc_hsp_vm_ch_setup(struct camrtc_hsp *camhsp,
		dma_addr_t iova, long *timeout);
static int camrtc_hsp_vm_ping(struct camrtc_hsp *camhsp,
		u32 data, long *timeout);
static int camrtc_hsp_vm_get_fw_hash(struct camrtc_hsp *camhsp,
		u32 index, long *timeout);
static int camrtc_hsp_vm_set_operating_point(struct camrtc_hsp *camhsp,
		u32 operating_point, long *timeout);

static const struct camrtc_hsp_op camrtc_hsp_vm_ops = {
	.send = camrtc_hsp_vm_send,
	.group_ring = camrtc_hsp_vm_group_ring,
	.sync = camrtc_hsp_vm_sync,
	.resume = camrtc_hsp_vm_resume,
	.suspend = camrtc_hsp_vm_suspend,
	.bye = camrtc_hsp_vm_bye,
	.ping = camrtc_hsp_vm_ping,
	.ch_setup = camrtc_hsp_vm_ch_setup,
	.get_fw_hash = camrtc_hsp_vm_get_fw_hash,
	.set_operating_point = camrtc_hsp_vm_set_operating_point,
};

/**
 * @brief Sends a message over the VM HSP mailbox
 *
 * This function sends a message over the VM HSP mailbox using the provided channel.
 * It performs the following operations:
 * - Acquires the send lock using @ref spin_lock_irqsave()
 * - Clears any previous response using @ref atomic_set()
 * - Sends the message using @ref mbox_send_message()
 * - Releases the send lock using @ref spin_unlock_irqrestore()
 *
 * @param[in] camhsp   Pointer to the camera HSP context
 *                     Valid value: non-NULL
 * @param[in] request  Message to send
 *                     Valid value: any integer value
 * @param[in,out] timeout Pointer to timeout value in jiffies
 *                        Valid value: non-NULL
 *
 * @retval (int) response from @ref mbox_send_message()
 */
static int camrtc_hsp_vm_send(struct camrtc_hsp *camhsp,
		int request, long *timeout)
{
	int response;
	unsigned long flags;

	spin_lock_irqsave(&camhsp->sendlock, flags);
	atomic_set(&camhsp->response, -1);
	response = mbox_send_message(camhsp->tx.chan, (void *)(unsigned long) request);
	spin_unlock_irqrestore(&camhsp->sendlock, flags);

	return response;
}

/**
 * @brief Rings a group doorbell by sending an IRQ message
 *
 * This function rings a group doorbell by sending an IRQ message to the RTCPU.
 * It performs the following operations:
 * - Calls @ref camrtc_hsp_vm_send_irqmsg() to send an IRQ message
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 * @param[in] group   Group identifier to ring the doorbell for
 *                    Valid value: any 16-bit value
 */
static void camrtc_hsp_vm_group_ring(struct camrtc_hsp *camhsp,
		u16 group)
{
	camrtc_hsp_vm_send_irqmsg(camhsp);
}

/**
 * @brief Sends an IRQ message to the RTCPU via HSP
 *
 * This function sends an interrupt request message to the RTCPU over the HSP
 * mailbox. It performs the following operations:
 * - Creates an IRQ message with parameter value 1 using @ref CAMRTC_HSP_MSG()
 * - Acquires a spinlock to ensure thread safety using @ref spin_lock_irqsave()
 * - Sends the message using @ref mbox_send_message()
 * - Releases the spinlock using @ref spin_unlock_irqrestore()
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 */
static void camrtc_hsp_vm_send_irqmsg(struct camrtc_hsp *camhsp)
{
	int irqmsg = CAMRTC_HSP_MSG(CAMRTC_HSP_IRQ, 1);
	int response;
	unsigned long flags;

	spin_lock_irqsave(&camhsp->sendlock, flags);
	response = mbox_send_message(camhsp->tx.chan, (void *)(unsigned long) irqmsg);
	spin_unlock_irqrestore(&camhsp->sendlock, flags);
}

/**
 * @brief Sends a message and waits for a response over the VM HSP mailbox
 *
 * This function sends a message and waits for a response using the common
 * send/receive function. It performs additional validation on the response
 * message to ensure it matches the request. The operations include:
 * - Calls @ref camrtc_hsp_sendrecv() to send the message and receive a response
 * - Validates that the message ID in the response matches the request using @ref CAMRTC_HSP_MSG_ID()
 * - Reports errors using @ref dev_err() if the response doesn't match the request
 * - Extracts and returns only the parameter portion of the response using @ref CAMRTC_HSP_MSG_PARAM()
 *
 * @param[in] camhsp   Pointer to the camera HSP context
 *                     Valid value: non-NULL
 * @param[in] request  Request message to send
 *                     Valid value: any integer value
 * @param[in,out] timeout  Pointer to timeout value in jiffies
 *                        Valid value: non-NULL
 *
 * @retval >=0        The parameter portion of the response message
 * @retval <0         Error code from @ref camrtc_hsp_sendrecv()
 * @retval -EIO       If the response message ID doesn't match the request
 */
static int camrtc_hsp_vm_sendrecv(struct camrtc_hsp *camhsp,
		int request, long *timeout)
{
	int response = camrtc_hsp_sendrecv(camhsp, request, timeout);

	if (response < 0)
		return response;

	if (CAMRTC_HSP_MSG_ID(request) != CAMRTC_HSP_MSG_ID(response)) {
		dev_err(&camhsp->dev,
			"request 0x%08x mismatch with response 0x%08x\n",
			request, response);
		return -EIO;
	}

	/* Return the 24-bit parameter only */
	return CAMRTC_HSP_MSG_PARAM(response);
}

/**
 * @brief Reads boot log messages from the RTCPU
 *
 * This function reads and processes boot log messages from the RTCPU during
 * the initialization process. It performs the following operations:
 * - Validates the HSP context is non-NULL
 * - Uses @ref camrtc_hsp_recv() to wait for and receive boot log messages
 * - Processes different types of boot messages (complete, stage, error)
 * - Logs messages at appropriate log levels using @ref dev_info(), @ref dev_dbg(), and @ref dev_err()
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 *
 * @retval 0          On successful completion of the boot log reading
 * @retval -ETIMEDOUT If timed out waiting for boot messages
 * @retval -EINVAL    If received a boot error message or invalid HSP context
 */
static int camrtc_hsp_vm_read_boot_log(struct camrtc_hsp *camhsp)
{
	uint32_t boot_log;
	long msg_timeout;

	if (camhsp == NULL) {
		pr_warn("%s: camhsp is NULL!\n", __func__);
		return -EINVAL;
	}

	msg_timeout = camhsp->timeout;

	for (;;) {
		boot_log = camrtc_hsp_recv(camhsp, CAMRTC_HSP_BOOT_COMPLETE, &msg_timeout);

		if (boot_log == -ETIMEDOUT) {
			dev_warn(&camhsp->dev,
				"%s: Error reading mailbox. Failed to obtain RTCPU boot logs\n", __func__);
			return boot_log;
		}

		if (CAMRTC_HSP_MSG_ID(boot_log) == CAMRTC_HSP_BOOT_COMPLETE) {
			dev_info(&camhsp->dev, "%s: RTCPU boot complete\n", __func__);
			break;
		} else if (CAMRTC_HSP_MSG_ID(boot_log) == CAMRTC_HSP_BOOT_STAGE) {
			dev_dbg(&camhsp->dev, "%s: RTCPU boot stage: %x\n", __func__, boot_log);
		} else if (CAMRTC_HSP_MSG_ID(boot_log) == CAMRTC_HSP_BOOT_ERROR) {
			uint32_t err_code = CAMRTC_HSP_MSG_PARAM(boot_log);
			dev_err(&camhsp->dev, "%s: RTCPU boot failure message received: 0x%x",
				__func__, err_code);
			return -EINVAL;
		} else {
			dev_warn(&camhsp->dev,
				"%s: Unexpected message received during RTCPU boot: 0x%08x\n",
				__func__, boot_log);
		}

	}

	return 0U;
}

/**
 * @brief Synchronizes with the RTCPU via HSP
 *
 * This function performs the initial handshake with the RTCPU by reading
 * boot logs and establishing communication. It performs the following operations:
 * - Reads boot logs using @ref camrtc_hsp_vm_read_boot_log()
 * - Logs a warning if boot logs cannot be read but continues with handshake
 * - Sends a hello message using @ref camrtc_hsp_vm_hello() to establish communication
 * - Stores the cookie (response) returned from hello message
 * - Negotiates protocol version using @ref camrtc_hsp_vm_protocol()
 *
 * @param[in] camhsp   Pointer to the camera HSP context
 *                     Valid value: non-NULL
 * @param[in,out] timeout  Pointer to timeout value in jiffies
 *                        Valid value: non-NULL
 *
 * @retval >=0        On successful synchronization (protocol version)
 * @retval <0         Error code from @ref camrtc_hsp_vm_hello() or @ref camrtc_hsp_vm_protocol()
 */
static int camrtc_hsp_vm_sync(struct camrtc_hsp *camhsp, long *timeout)
{
	int response;

	response = camrtc_hsp_vm_read_boot_log(camhsp);

	if (response < 0) {
		/* Failing to read boot logs is not a fatal error.
		 * Log error and continue with HSP handshake. */
		dev_warn(&camhsp->dev, "%s: Failed to read boot logs", __func__);
	}

	response = camrtc_hsp_vm_hello(camhsp, timeout);

	if (response >= 0) {
		camhsp->cookie = response;
		response = camrtc_hsp_vm_protocol(camhsp, timeout);
	}

	return response;
}

/**
 * @brief Generates a cookie value for HSP handshake
 *
 * This function generates a 24-bit cookie value used for handshake and
 * subsequent communication with the RTCPU. The cookie serves as an
 * identifier for the session. It performs the following operations:
 * - Uses @ref sched_clock() to get a timestamp
 * - Extracts the lower bits using @ref CAMRTC_HSP_MSG_PARAM()
 * - Ensures the cookie is never zero by incrementing if needed
 *
 * @return u32 Cookie value (non-zero 24-bit value)
 */
static u32 camrtc_hsp_vm_cookie(void)
{
	u32 value = CAMRTC_HSP_MSG_PARAM(sched_clock() >> 5U);

	if (value == 0)
		value++;

	return value;
}

/**
 * @brief Sends a HELLO message to the RTCPU and waits for echo
 *
 * This function initiates communication with the RTCPU by sending a HELLO
 * message with a unique cookie and waiting for the same message to be echoed
 * back. It performs the following operations:
 * - Creates a HELLO message with a unique cookie using @ref CAMRTC_HSP_MSG() and @ref camrtc_hsp_vm_cookie()
 * - Sends the message using @ref camrtc_hsp_send()
 * - Repeatedly calls @ref camrtc_hsp_recv() until the echoed message is received or timeout
 *
 * @param[in] camhsp   Pointer to the camera HSP context
 *                     Valid value: non-NULL
 * @param[in,out] timeout  Pointer to timeout value in jiffies
 *                        Valid value: non-NULL
 *
 * @retval request  On successful handshake (value of the HELLO message with cookie)
 * @retval <0       Error code from @ref camrtc_hsp_send() or @ref camrtc_hsp_recv()
 */
static int camrtc_hsp_vm_hello(struct camrtc_hsp *camhsp, long *timeout)
{
	int request = CAMRTC_HSP_MSG(CAMRTC_HSP_HELLO, camrtc_hsp_vm_cookie());
	int response = camrtc_hsp_send(camhsp, request, timeout);

	if (response < 0)
		return response;

	for (;;) {
		response = camrtc_hsp_recv(camhsp, request, timeout);

		/* Wait until we get the HELLO message we sent */
		if (response == request)
			break;

		/* ...or timeout */
		if (response < 0)
			break;
	}

	return response;
}

/**
 * @brief Negotiates protocol version with RTCPU
 *
 * This function negotiates the protocol version to use for communication with
 * the RTCPU. It performs the following operations:
 * - Creates a protocol message with the driver's version using @ref CAMRTC_HSP_MSG()
 * - Sends the message and waits for response using @ref camrtc_hsp_vm_sendrecv()
 *
 * @param[in] camhsp   Pointer to the camera HSP context
 *                     Valid value: non-NULL
 * @param[in,out] timeout  Pointer to timeout value in jiffies
 *                        Valid value: non-NULL
 *
 * @retval >=0        On successful negotiation (protocol version)
 * @retval <0         Error code from @ref camrtc_hsp_vm_sendrecv()
 */
static int camrtc_hsp_vm_protocol(struct camrtc_hsp *camhsp, long *timeout)
{
	int request = CAMRTC_HSP_MSG(CAMRTC_HSP_PROTOCOL,
			RTCPU_DRIVER_SM6_VERSION);

	return camrtc_hsp_vm_sendrecv(camhsp, request, timeout);
}

/**
 * @brief Resumes the RTCPU firmware
 *
 * This function resumes the RTCPU firmware by sending a RESUME message with
 * the current cookie value. It performs the following operations:
 * - Creates a RESUME message with the current cookie using @ref CAMRTC_HSP_MSG()
 * - Sends the message and waits for response using @ref camrtc_hsp_vm_sendrecv()
 *
 * @param[in] camhsp   Pointer to the camera HSP context
 *                     Valid value: non-NULL
 * @param[in,out] timeout  Pointer to timeout value in jiffies
 *                        Valid value: non-NULL
 *
 * @retval (int)      Error code from @ref camrtc_hsp_vm_sendrecv()
 */
static int camrtc_hsp_vm_resume(struct camrtc_hsp *camhsp, long *timeout)
{
	int request = CAMRTC_HSP_MSG(CAMRTC_HSP_RESUME, camhsp->cookie);

	return camrtc_hsp_vm_sendrecv(camhsp, request, timeout);
}

/**
 * @brief Suspends the RTCPU firmware
 *
 * This function suspends the RTCPU firmware by sending a SUSPEND message with
 * a zero cookie value. It performs the following operations:
 * - Creates a SUSPEND message with a zero cookie using @ref CAMRTC_HSP_MSG()
 * - Sends the message and waits for response using @ref camrtc_hsp_vm_sendrecv()
 *
 * @param[in] camhsp   Pointer to the camera HSP context
 *                     Valid value: non-NULL
 * @param[in,out] timeout  Pointer to timeout value in jiffies
 *                        Valid value: non-NULL
 *
 * @retval (int)      Error code from @ref camrtc_hsp_vm_sendrecv()
 */
static int camrtc_hsp_vm_suspend(struct camrtc_hsp *camhsp, long *timeout)
{
	u32 request = CAMRTC_HSP_MSG(CAMRTC_HSP_SUSPEND, 0);

	return camrtc_hsp_vm_sendrecv(camhsp, request, timeout);
}

/**
 * @brief Sends a BYE message to the RTCPU
 *
 * This function sends a BYE message to the RTCPU to terminate the communication
 * session. It performs the following operations:
 * - Creates a BYE message with a zero cookie using @ref CAMRTC_HSP_MSG()
 * - Sends the message and waits for response using @ref camrtc_hsp_vm_sendrecv()
 *
 * @param[in] camhsp   Pointer to the camera HSP context
 *                     Valid value: non-NULL
 * @param[in,out] timeout  Pointer to timeout value in jiffies
 *                        Valid value: non-NULL
 *
 * @retval (int)      Error code from @ref camrtc_hsp_vm_sendrecv()
 */
static int camrtc_hsp_vm_bye(struct camrtc_hsp *camhsp, long *timeout)
{
	u32 request = CAMRTC_HSP_MSG(CAMRTC_HSP_BYE, 0);

	camhsp->cookie = 0U;

	return camrtc_hsp_vm_sendrecv(camhsp, request, timeout);
}

/**
 * @brief Sets up the channel for communication with the RTCPU
 *
 * This function sets up the channel for communication with the RTCPU by sending
 * a CH_SETUP message with the provided IOVA value. It performs the following operations:
 * - Creates a CH_SETUP message with the IOVA value using @ref CAMRTC_HSP_MSG()
 * - Sends the message and waits for response using @ref camrtc_hsp_vm_sendrecv()
 *
 * @param[in] camhsp   Pointer to the camera HSP context
 *                     Valid value: non-NULL
 * @param[in] iova     IOVA value to set up the channel with
 *                     Valid value: any DMA address
 * @param[in,out] timeout  Pointer to timeout value in jiffies
 *                        Valid value: non-NULL
 *
 * @retval (int)      Error code from @ref camrtc_hsp_vm_sendrecv()
 */
static int camrtc_hsp_vm_ch_setup(struct camrtc_hsp *camhsp,
		dma_addr_t iova, long *timeout)
{
	u32 request = CAMRTC_HSP_MSG(CAMRTC_HSP_CH_SETUP, iova >> 8);

	return camrtc_hsp_vm_sendrecv(camhsp, request, timeout);
}

/**
 * @brief Pings the RTCPU
 *
 * This function pings the RTCPU by sending a PING message with the provided data.
 * It performs the following operations:
 * - Creates a PING message with the provided data using @ref CAMRTC_HSP_MSG()
 * - Sends the message and waits for response using @ref camrtc_hsp_vm_sendrecv()
 *
 * @param[in] camhsp   Pointer to the camera HSP context
 *                     Valid value: non-NULL
 * @param[in] data     Data to send in the PING message
 *                     Valid value: any 32-bit value
 * @param[in,out] timeout  Pointer to timeout value in jiffies
 *                        Valid value: non-NULL
 *
 * @retval (int)      Error code from @ref camrtc_hsp_vm_sendrecv()
 */
static int camrtc_hsp_vm_ping(struct camrtc_hsp *camhsp, u32 data,
		long *timeout)
{
	u32 request = CAMRTC_HSP_MSG(CAMRTC_HSP_PING, data);

	return camrtc_hsp_vm_sendrecv(camhsp, request, timeout);
}

/**
 * @brief Gets the firmware hash from the RTCPU
 *
 * This function gets the firmware hash from the RTCPU by sending a FW_HASH
 * message with the provided index. It performs the following operations:
 * - Creates a FW_HASH message with the provided index using @ref CAMRTC_HSP_MSG()
 * - Sends the message and waits for response using @ref camrtc_hsp_vm_sendrecv()
 *
 * @param[in] camhsp   Pointer to the camera HSP context
 *                     Valid value: non-NULL
 * @param[in] index    Index of the firmware hash to get
 *                     Valid value: any 32-bit value
 * @param[in,out] timeout  Pointer to timeout value in jiffies
 *                        Valid value: non-NULL
 *
 * @retval (int)      Error code from @ref camrtc_hsp_vm_sendrecv()
 */
static int camrtc_hsp_vm_get_fw_hash(struct camrtc_hsp *camhsp, u32 index,
		long *timeout)
{
	u32 request = CAMRTC_HSP_MSG(CAMRTC_HSP_FW_HASH, index);

	return camrtc_hsp_vm_sendrecv(camhsp, request, timeout);
}

/**
 * @brief Sets the operating point for the RTCPU
 *
 * This function sets the operating point for the RTCPU by sending a SET_OP_POINT
 * message with the provided operating point value. It performs the following operations:
 * - Creates a SET_OP_POINT message with the provided operating point value using @ref CAMRTC_HSP_MSG()
 * - Sends the message and waits for response using @ref camrtc_hsp_vm_sendrecv()
 *
 * @param[in] camhsp   Pointer to the camera HSP context
 *                     Valid value: non-NULL
 * @param[in] operating_point  Operating point value to set
 *                     Valid value: any 32-bit value
 * @param[in,out] timeout  Pointer to timeout value in jiffies
 *                        Valid value: non-NULL
 *
 * @retval (int)      Error code from @ref camrtc_hsp_vm_sendrecv()
 */
static int camrtc_hsp_vm_set_operating_point(struct camrtc_hsp *camhsp, u32 operating_point,
		long *timeout)
{
	u32 request = CAMRTC_HSP_MSG(CAMRTC_HSP_SET_OP_POINT, operating_point);

	return camrtc_hsp_vm_sendrecv(camhsp, request, timeout);
}

/**
 * @brief Probes for a VM HSP device
 *
 * This function probes for a VM HSP device by searching through the device tree
 * for a compatible node with the name "nvidia,tegra-camrtc-hsp-vm". It performs
 * the following operations:
 * - Gets the parent device node using @ref of_node_get()
 * - Searches for a child node with the compatible string "nvidia,tegra-camrtc-hsp-vm"
 * - Returns the first available child node if found
 * - Returns NULL if no compatible node is found
 *
 * @param[in] parent  Pointer to the parent device node
 *                    Valid value: non-NULL
 *
 * @retval Pointer to the available child node if found
 * @retval NULL if no compatible node is found
 */
static struct device_node *hsp_vm_get_available(const struct device_node *parent)
{
	const char *compatible = "nvidia,tegra-camrtc-hsp-vm";
	struct device_node *child;

	for_each_child_of_node(parent, child) {
		if (of_device_is_compatible(child, compatible) &&
			of_device_is_available(child))
			break;
	}
	return child;
}

/**
 * @brief Probes and initializes an HSP VM device
 *
 * This function probes and initializes a VM HSP device. It performs the following operations:
 * - Gets an available HSP VM device node using @ref hsp_vm_get_available()
 * - Requests mailbox channels for RX and TX using @ref mbox_request_channel_byname()
 * - Sets up the operations structure and device name using @ref dev_set_name()
 * - Reports errors using @ref dev_err() if channel requests fail
 * - Releases resources on failure using @ref of_node_put()
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 *
 * @retval 0           On successful probe
 * @retval -ENOTSUPP   If no compatible HSP VM device is found
 * @retval -EPROBE_DEFER  If probe should be deferred
 * @retval (int)       Error code from @ref mbox_request_channel_byname()
 */
static int camrtc_hsp_vm_probe(struct camrtc_hsp *camhsp)
{
	struct device_node *np = camhsp->dev.parent->of_node;
	int err = -ENOTSUPP;
	const char *obtain = "";

	np = hsp_vm_get_available(np);
	if (np == NULL) {
		dev_err(&camhsp->dev, "no hsp protocol \"%s\"\n",
			"nvidia,tegra-camrtc-hsp-vm");
		return -ENOTSUPP;
	}

	camhsp->dev.of_node = np;

	camhsp->rx.chan = mbox_request_channel_byname(&camhsp->rx.client, "vm-rx");
	if (IS_ERR(camhsp->rx.chan)) {
		err = PTR_ERR(camhsp->rx.chan);
		goto fail;
	}

	camhsp->tx.chan = mbox_request_channel_byname(&camhsp->tx.client, "vm-tx");
	if (IS_ERR(camhsp->tx.chan)) {
		err = PTR_ERR(camhsp->tx.chan);
		goto fail;
	}

	camhsp->op = &camrtc_hsp_vm_ops;
	dev_set_name(&camhsp->dev, "%s:%s",
		dev_name(camhsp->dev.parent), camhsp->dev.of_node->name);
	dev_dbg(&camhsp->dev, "probed\n");

	return 0;

fail:
	if (err != -EPROBE_DEFER) {
		dev_err(&camhsp->dev, "%s: failed to obtain %s: %d\n",
			np->name, obtain, err);
	}
	of_node_put(np);
	return err;
}

/* ---------------------------------------------------------------------- */
/* Public interface */

/**
 * @brief Rings a group doorbell
 *
 * This function rings a group doorbell by calling the group_ring operation
 * on the HSP context. It performs the following operations:
 * - Validates the HSP context is non-NULL
 * - Calls the group_ring operation on the HSP context using @ref group_ring()
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 * @param[in] group   Group identifier to ring the doorbell for
 *                    Valid value: any 16-bit value
 */
void camrtc_hsp_group_ring(struct camrtc_hsp *camhsp,
		u16 group)
{
	if (camhsp == NULL)
		pr_warn("%s: camhsp is NULL!\n", __func__);
	else
		camhsp->op->group_ring(camhsp, group);
}
EXPORT_SYMBOL(camrtc_hsp_group_ring);

/**
 * @brief Synchronizes the HSP
 *
 * This function synchronizes the HSP by calling the sync operation
 * on the HSP context. It performs the following operations:
 * - Validates the HSP context is non-NULL
 * - Locks the mutex using @ref mutex_lock()
 * - Calls the sync operation on the HSP context using @ref sync()
 * - Unlocks the mutex using @ref mutex_unlock()
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 *
 * @retval (int)      Return code from @ref sync()
 * @retval -EINVAL    If the HSP context is NULL
 */
int camrtc_hsp_sync(struct camrtc_hsp *camhsp)
{
	long timeout;
	int response;

	if (camhsp == NULL) {
		pr_warn("%s: camhsp is NULL!\n", __func__);
		return -EINVAL;
	}

	timeout = camhsp->timeout;
	mutex_lock(&camhsp->mutex);
	response = camhsp->op->sync(camhsp, &timeout);
	mutex_unlock(&camhsp->mutex);

	return response;
}
EXPORT_SYMBOL(camrtc_hsp_sync);

/**
 * @brief Resumes the HSP
 *
 * This function resumes the HSP by calling the resume operation
 * on the HSP context. It performs the following operations:
 * - Validates the HSP context is non-NULL
 * - Locks the mutex using @ref mutex_lock()
 * - Calls the resume operation on the HSP context using @ref resume()
 * - Unlocks the mutex using @ref mutex_unlock()
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 *
 * @retval (int)      Return code from @ref resume()
 * @retval -EINVAL    If the HSP context is NULL
 */
int camrtc_hsp_resume(struct camrtc_hsp *camhsp)
{
	long timeout;
	int response;

	if (camhsp == NULL) {
		pr_warn("%s: camhsp is NULL!\n", __func__);
		return -EINVAL;
	}

	timeout = camhsp->timeout;
	mutex_lock(&camhsp->mutex);
	response = camhsp->op->resume(camhsp, &timeout);
	mutex_unlock(&camhsp->mutex);

	return response;
}
EXPORT_SYMBOL(camrtc_hsp_resume);

/**
 * @brief Suspends the HSP
 *
 * This function suspends the HSP by calling the suspend operation
 * on the HSP context. It performs the following operations:
 * - Validates the HSP context is non-NULL
 * - Locks the mutex using @ref mutex_lock()
 * - Calls the suspend operation on the HSP context using @ref suspend()
 * - Unlocks the mutex using @ref mutex_unlock()
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 *
 * @retval (int)      Return code from @ref suspend()
 * @retval -EINVAL    If the HSP context is NULL
 * @retval -EIO       If the suspend operation fails
 */
int camrtc_hsp_suspend(struct camrtc_hsp *camhsp)
{
	long timeout;
	int response;

	if (camhsp == NULL) {
		pr_warn("%s: camhsp is NULL!\n", __func__);
		return -EINVAL;
	}

	timeout = camhsp->timeout;
	mutex_lock(&camhsp->mutex);
	response = camhsp->op->suspend(camhsp, &timeout);
	mutex_unlock(&camhsp->mutex);

	if (response != 0)
		dev_info(&camhsp->dev, "PM_SUSPEND failed: 0x%08x\n",
			response);

	return response <= 0 ? response : -EIO;
}
EXPORT_SYMBOL(camrtc_hsp_suspend);

/**
 * @brief Sets the operating point for the HSP
 *
 * This function sets the operating point for the HSP by calling the set_operating_point
 * operation on the HSP context. It performs the following operations:
 * - Validates the HSP context is non-NULL
 * - Locks the mutex using @ref mutex_lock()
 * - Calls the set_operating_point operation on the HSP context using @ref set_operating_point()
 * - Unlocks the mutex using @ref mutex_unlock()
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 * @param[in] operating_point  Operating point value to set
 *                    Valid value: any 32-bit value
 *
 * @retval (int)      Return code from @ref set_operating_point()
 * @retval -EINVAL    If the HSP context is NULL
 * @retval -EIO       If the set_operating_point operation fails
 */
int camrtc_hsp_set_operating_point(struct camrtc_hsp *camhsp, uint32_t operating_point)
{
	long timeout;
	int response;

	if (camhsp == NULL) {
		pr_warn("%s: camhsp is NULL!\n", __func__);
		return -EINVAL;
	}

	timeout = camhsp->timeout;
	mutex_lock(&camhsp->mutex);
	response = camhsp->op->set_operating_point(camhsp, operating_point, &timeout);
	mutex_unlock(&camhsp->mutex);

	if (response != 0)
		dev_info(&camhsp->dev, "HSP_SET_OP_POINT failed: 0x%08x\n",
			response);

	return response <= 0 ? response : -EIO;
}
EXPORT_SYMBOL(camrtc_hsp_set_operating_point);

/**
 * @brief Sends a BYE message to the HSP
 *
 * This function sends a BYE message to the HSP by calling the bye operation
 * on the HSP context. It performs the following operations:
 * - Validates the HSP context is non-NULL
 * - Locks the mutex using @ref mutex_lock()
 * - Calls the bye operation on the HSP context using @ref bye()
 * - Unlocks the mutex using @ref mutex_unlock()
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 *
 * @retval (int)      Return code from @ref bye()
 * @retval -EINVAL    If the HSP context is NULL
 */
int camrtc_hsp_bye(struct camrtc_hsp *camhsp)
{
	long timeout;
	int response;

	if (camhsp == NULL) {
		pr_warn("%s: camhsp is NULL!\n", __func__);
		return -EINVAL;
	}

	timeout = camhsp->timeout;
	mutex_lock(&camhsp->mutex);
	response = camhsp->op->bye(camhsp, &timeout);
	mutex_unlock(&camhsp->mutex);

	if (response != 0)
		dev_warn(&camhsp->dev, "BYE failed: 0x%08x\n", response);

	return response;
}
EXPORT_SYMBOL(camrtc_hsp_bye);

/**
 * @brief Sets up the HSP channel
 *
 * This function sets up the HSP channel by calling the ch_setup operation
 * on the HSP context. It performs the following operations:
 * - Validates the HSP context is non-NULL
 * - Locks the mutex using @ref mutex_lock()
 * - Calls the ch_setup operation on the HSP context using @ref ch_setup()
 * - Unlocks the mutex using @ref mutex_unlock()
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 * @param[in] iova    IOVA value to set
 *                    Valid value: any 32-bit value
 *
 * @retval (int)      Error code from @ref ch_setup()
 * @retval -EINVAL    If the HSP context is NULL or the IOVA is invalid
 */
int camrtc_hsp_ch_setup(struct camrtc_hsp *camhsp, dma_addr_t iova)
{
	long timeout;
	int response;

	if (camhsp == NULL) {
		pr_warn("%s: camhsp is NULL!\n", __func__);
		return -EINVAL;
	}

	if (iova >= BIT_ULL(32) || (iova & 0xffU) != 0) {
		dev_warn(&camhsp->dev,
			"CH_SETUP invalid iova: 0x%08llx\n", iova);
		return -EINVAL;
	}

	timeout = camhsp->timeout;
	mutex_lock(&camhsp->mutex);
	response = camhsp->op->ch_setup(camhsp, iova, &timeout);
	mutex_unlock(&camhsp->mutex);

	if (response > 0)
		dev_dbg(&camhsp->dev, "CH_SETUP failed: 0x%08x\n", response);

	return response;
}
EXPORT_SYMBOL(camrtc_hsp_ch_setup);

/**
 * @brief Pings the HSP
 *
 * This function pings the HSP by calling the ping operation
 * on the HSP context. It performs the following operations:
 * - Validates the HSP context is non-NULL
 * - Locks the mutex using @ref mutex_lock()
 * - Calls the ping operation on the HSP context using @ref ping()
 * - Unlocks the mutex using @ref mutex_unlock()
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 * @param[in] data    Data value to ping
 *                    Valid value: any 32-bit value
 * @param[in] timeout Timeout value to use
 *                    Valid value: any 32-bit value
 *
 * @retval (int)      Error code from @ref ping()
 * @retval -EINVAL    If the HSP context is NULL
 */
int camrtc_hsp_ping(struct camrtc_hsp *camhsp, u32 data, long timeout)
{
	long left = timeout;
	int response;

	if (camhsp == NULL) {
		dev_warn(&camhsp->dev, "%s: camhsp is NULL!\n", __func__);
		return -EINVAL;
	}

	if (left == 0L)
		left = camhsp->timeout;

	mutex_lock(&camhsp->mutex);
	response = camhsp->op->ping(camhsp, data, &left);
	mutex_unlock(&camhsp->mutex);

	return response;
}
EXPORT_SYMBOL(camrtc_hsp_ping);

/**
 * @brief Gets the firmware hash for the HSP
 *
 * This function gets the firmware hash for the HSP by calling the get_fw_hash operation
 * on the HSP context. It performs the following operations:
 * - Validates the HSP context is non-NULL
 * - Locks the mutex using @ref mutex_lock()
 * - Calls the get_fw_hash operation on the HSP context using @ref get_fw_hash()
 * - Unlocks the mutex using @ref mutex_unlock()
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 * @param[in] hash    Hash value to get
 *                    Valid value: any 32-bit value
 * @param[in] hash_size  Size of the hash value
 *                    Valid value: any 32-bit value
 *
 * @retval (int)      Error code from @ref get_fw_hash()
 * @retval -EINVAL    If the HSP context is NULL
 * @retval -EIO       If the get_fw_hash operation fails
 * @retval 0          On success
 */
int camrtc_hsp_get_fw_hash(struct camrtc_hsp *camhsp,
		u8 hash[], size_t hash_size)
{
	int i;
	int ret = 0;
	long timeout;

	if (camhsp == NULL) {
		dev_warn(&camhsp->dev, "%s: camhsp is NULL!\n", __func__);
		return -EINVAL;
	}

	memset(hash, 0, hash_size);
	timeout = camhsp->timeout;
	mutex_lock(&camhsp->mutex);

	for (i = 0; i < hash_size; i++) {
		int value = camhsp->op->get_fw_hash(camhsp, i, &timeout);

		if (value < 0 || value > 255) {
			dev_info(&camhsp->dev,
				"FW_HASH failed: 0x%08x\n", value);
			ret = value < 0 ? value : -EIO;
			goto fail;
		}

		hash[i] = value;
	}

fail:
	mutex_unlock(&camhsp->mutex);

	return ret;
}
EXPORT_SYMBOL(camrtc_hsp_get_fw_hash);

static const struct device_type camrtc_hsp_combo_dev_type = {
	.name	= "camrtc-hsp-protocol",
};

/**
 * @brief Releases resources for a camera HSP combo device
 *
 * This function is called when a camera HSP combo device is being destroyed.
 * It performs the following operations:
 * - Gets the camera HSP context using @ref container_of()
 * - Frees the RX and TX mailbox channels using @ref mbox_free_channel() if they exist
 * - Releases the device node using @ref of_node_put()
 * - Frees the camera HSP context using @ref kfree()
 *
 * @param[in] dev  Pointer to the device being released
 *                 Valid value: non-NULL
 */
static void camrtc_hsp_combo_dev_release(struct device *dev)
{
	struct camrtc_hsp *camhsp = container_of(dev, struct camrtc_hsp, dev);

	if (!IS_ERR_OR_NULL(camhsp->rx.chan))
		mbox_free_channel(camhsp->rx.chan);
	if (!IS_ERR_OR_NULL(camhsp->tx.chan))
		mbox_free_channel(camhsp->tx.chan);

	of_node_put(dev->of_node);
	kfree(camhsp);
}

/**
 * @brief Probes for a camera HSP device
 *
 * This function attempts to probe for a camera HSP device by trying different
 * probe methods. Currently, it only tries the VM HSP probe method.
 * It performs the following operations:
 * - Calls @ref camrtc_hsp_vm_probe() to attempt VM HSP probe
 * - Returns success if VM probe succeeds
 * - Returns -ENODEV if no supported HSP device is found
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 *
 * @retval 0          On successful probe
 * @retval -ENODEV    If no supported HSP device is found
 * @retval (int)      Error code from @ref camrtc_hsp_vm_probe()
 */
static int camrtc_hsp_probe(struct camrtc_hsp *camhsp)
{
	int ret;

	ret = camrtc_hsp_vm_probe(camhsp);
	if (ret != -ENOTSUPP)
		return ret;

	return -ENODEV;
}

/**
 * @brief Creates a camera HSP device
 *
 * This function creates a camera HSP device by allocating memory for the HSP context
 * and initializing its fields. It performs the following operations:
 * - Allocates memory for the HSP context using @ref kzalloc()
 * - Initializes the HSP context fields
 * - Sets the parent device using @ref dev_set_parent()
 * - Sets the group notify function using @ref camrtc_hsp_set_group_notify()
 * - Sets the command timeout using @ref camrtc_hsp_set_cmd_timeout()
 * - Initializes the mutex using @ref mutex_init()
 * - Initializes the send lock using @ref spin_lock_init()
 * - Initializes the response wait queue using @ref init_waitqueue_head()
 * - Initializes the emptied completion using @ref init_completion()
 * - Sets the response atomic variable to -1 using @ref atomic_set()
 * - Sets the device type using @ref camrtc_hsp_combo_dev_type
 * - Sets the device release function using @ref camrtc_hsp_combo_dev_release()
 * - Initializes the device using @ref device_initialize()
 * - Sets the device name using @ref dev_set_name()
 * - Disables runtime PM using @ref pm_runtime_no_callbacks()
 * - Enables runtime PM using @ref pm_runtime_enable()
 * - Sets the TX client block flag to false
 * - Sets the RX callback function using @ref camrtc_hsp_rx_full_notify()
 * - Sets the TX done function using @ref camrtc_hsp_tx_empty_notify()
 * - Sets the TX and RX client device using @ref camhsp->tx.client.dev
 * - Sets the device driver data using @ref dev_set_drvdata()
 * - Calls @ref camrtc_hsp_probe() to attempt HSP probe
 * - Adds the device using @ref device_add()
 *
 * @param[in] dev  Pointer to the device creating the HSP
 *                 Valid value: non-NULL
 * @param[in] group_notify  Function pointer to the group notify function
 *                          Valid value: non-NULL
 * @param[in] cmd_timeout  Command timeout value
 *                         Valid value: any 32-bit value
 *
 * @retval (struct camrtc_hsp *)  Pointer to the HSP context on success
 * @retval ERR_PTR(-ENOMEM)       If memory allocation fails
 * @retval ERR_PTR(-EINVAL)       If the HSP context is NULL
 * @retval ERR_PTR(int)           Error code from @ref camrtc_hsp_probe() or @ref device_add()
 */
struct camrtc_hsp *camrtc_hsp_create(
	struct device *dev,
	void (*group_notify)(struct device *dev, u16 group),
	long cmd_timeout)
{
	struct camrtc_hsp *camhsp;
	int ret = -EINVAL;

	camhsp = kzalloc(sizeof(*camhsp), GFP_KERNEL);
	if (camhsp == NULL)
		return ERR_PTR(-ENOMEM);

	camhsp->dev.parent = dev;
	camhsp->group_notify = group_notify;
	camhsp->timeout = cmd_timeout;
	mutex_init(&camhsp->mutex);
	spin_lock_init(&camhsp->sendlock);
	init_waitqueue_head(&camhsp->response_waitq);
	init_completion(&camhsp->emptied);
	atomic_set(&camhsp->response, -1);
	camhsp->panic_callback = NULL;

	camhsp->dev.type = &camrtc_hsp_combo_dev_type;
	camhsp->dev.release = camrtc_hsp_combo_dev_release;
	device_initialize(&camhsp->dev);

	dev_set_name(&camhsp->dev, "%s:%s", dev_name(dev), "hsp");

	pm_runtime_no_callbacks(&camhsp->dev);
	pm_runtime_enable(&camhsp->dev);

	camhsp->tx.client.tx_block = false;
	camhsp->rx.client.rx_callback = camrtc_hsp_rx_full_notify;
	camhsp->tx.client.tx_done = camrtc_hsp_tx_empty_notify;
	camhsp->rx.client.dev = camhsp->tx.client.dev = &(camhsp->dev);
	dev_set_drvdata(&camhsp->dev, camhsp);

	ret = camrtc_hsp_probe(camhsp);
	if (ret < 0)
		goto fail;

	ret = device_add(&camhsp->dev);
	if (ret < 0)
		goto fail;

	return camhsp;

fail:
	camrtc_hsp_free(camhsp);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(camrtc_hsp_create);

/**
 * @brief Frees a camera HSP device
 *
 * This function frees a camera HSP device by disabling runtime PM, unregistering
 * the device, and releasing the device structure. It performs the following operations:
 * - Checks if the HSP context is valid using @ref IS_ERR_OR_NULL()
 * - Disables runtime PM using @ref pm_runtime_disable()
 * - Checks if the device driver data is not NULL using @ref dev_get_drvdata()
 * - Unregisters the device using @ref device_unregister()
 * - Releases the device using @ref put_device()
 *
 * @param[in] camhsp  Pointer to the camera HSP context
 *                    Valid value: non-NULL
 */
void camrtc_hsp_free(struct camrtc_hsp *camhsp)
{
	if (IS_ERR_OR_NULL(camhsp))
		return;

	pm_runtime_disable(&camhsp->dev);

	if (dev_get_drvdata(&camhsp->dev) != NULL)
		device_unregister(&camhsp->dev);
	else
		put_device(&camhsp->dev);
}
EXPORT_SYMBOL(camrtc_hsp_free);
MODULE_LICENSE("GPL v2");
