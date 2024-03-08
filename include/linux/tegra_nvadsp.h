/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
 */

#ifndef __LINUX_TEGRA_NVADSP_H
#define __LINUX_TEGRA_NVADSP_H

#include <linux/types.h>
#include <linux/timer.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/completion.h>

typedef int status_t;

/*
 * Shared Semaphores
 */
typedef struct {
	int magic; /* 'ssem' */
	uint8_t id;
	wait_queue_head_t wait;
	struct timer_list timer;
} nvadsp_shared_sema_t;

nvadsp_shared_sema_t *
nvadsp_shared_sema_init(uint8_t nvadsp_shared_sema_id);
status_t nvadsp_shared_sema_destroy(nvadsp_shared_sema_t *);
status_t nvadsp_shared_sema_acquire(nvadsp_shared_sema_t *);
status_t nvadsp_shared_sema_release(nvadsp_shared_sema_t *);

/*
 * Arbitrated Semaphores
 */
typedef struct {
	int magic; /* 'asem' */
	uint8_t  id;
	wait_queue_head_t wait;
	struct completion comp;
} nvadsp_arb_sema_t;

nvadsp_arb_sema_t *nvadsp_arb_sema_init(uint8_t nvadsp_arb_sema_id);
status_t nvadsp_arb_sema_destroy(nvadsp_arb_sema_t *);
status_t nvadsp_arb_sema_acquire(nvadsp_arb_sema_t *);
status_t nvadsp_arb_sema_release(nvadsp_arb_sema_t *);

/*
 * Mailbox Queue
 */
#define NVADSP_MBOX_QUEUE_SIZE		32
#define NVADSP_MBOX_QUEUE_SIZE_MASK	(NVADSP_MBOX_QUEUE_SIZE - 1)
struct nvadsp_mbox_queue {
	uint32_t array[NVADSP_MBOX_QUEUE_SIZE];
	uint16_t head;
	uint16_t tail;
	uint16_t count;
	struct completion comp;
	spinlock_t lock;
};

status_t nvadsp_mboxq_enqueue(struct nvadsp_mbox_queue *, uint32_t);

/*
 * Mailbox
 */
#define NVADSP_MBOX_NAME_MAX		16
#define NVADSP_MBOX_NAME_MAX_STR	(NVADSP_MBOX_NAME_MAX + 1)

typedef status_t (*nvadsp_mbox_handler_t)(uint32_t, void *);

struct nvadsp_mbox {
	uint16_t id;
	char name[NVADSP_MBOX_NAME_MAX_STR];
	struct nvadsp_mbox_queue recv_queue;
	nvadsp_mbox_handler_t handler;
	nvadsp_mbox_handler_t ack_handler;
	void *hdata;
};

#define NVADSP_MBOX_SMSG       0x1
#define NVADSP_MBOX_LMSG       0x2

/*
 * Circular Message Queue
 */
typedef struct _msgq_message_t {
	int32_t size;		/* size of payload in words */
	int32_t payload[1];	/* variable length payload */
} msgq_message_t;

#define MSGQ_MESSAGE_HEADER_SIZE \
	(sizeof(msgq_message_t) - sizeof(((msgq_message_t *)0)->payload))
#define MSGQ_MESSAGE_HEADER_WSIZE \
	(MSGQ_MESSAGE_HEADER_SIZE / sizeof(int32_t))

typedef struct _msgq_t {
	int32_t size;		/* queue size in words */
	int32_t write_index;	/* queue write index */
	int32_t read_index;	/* queue read index */
	int32_t queue[1];	/* variable length queue */
} msgq_t;

#define MSGQ_HEADER_SIZE	(sizeof(msgq_t) - sizeof(((msgq_t *)0)->queue))
#define MSGQ_HEADER_WSIZE	(MSGQ_HEADER_SIZE / sizeof(int32_t))
#define MSGQ_MAX_QUEUE_WSIZE	(8192 - MSGQ_HEADER_WSIZE)
#define MSGQ_MSG_WSIZE(x) \
	(((sizeof(x) + sizeof(int32_t) - 1) & (~(sizeof(int32_t)-1))) >> 2)
#define MSGQ_MSG_PAYLOAD_WSIZE(x) \
	(MSGQ_MSG_WSIZE(x) - MSGQ_MESSAGE_HEADER_WSIZE)

void msgq_init(msgq_t *msgq, int32_t size);
int32_t msgq_queue_message(msgq_t *msgq, const msgq_message_t *message);
int32_t msgq_dequeue_message(msgq_t *msgq, msgq_message_t *message);
#define msgq_discard_message(msgq) msgq_dequeue_message(msgq, NULL)

/*
 * ADSP OS
 */

typedef const void *nvadsp_os_handle_t;

/*
 * ADSP TSC
 */
uint64_t nvadsp_get_timestamp_counter(void);

/*
 * ADSP OS App
 */
#define ARGV_SIZE_IN_WORDS         128

enum {
	NVADSP_APP_STATE_UNKNOWN,
	NVADSP_APP_STATE_INITIALIZED,
	NVADSP_APP_STATE_STARTED,
	NVADSP_APP_STATE_STOPPED
};

enum adsp_app_status_msg {
	OS_LOAD_ADSP_APPS,
	RUN_ADSP_APP,
	ADSP_APP_INIT,
	ADSP_APP_START,
	ADSP_APP_START_STATUS,
	ADSP_APP_COMPLETE_STATUS
};

struct nvadsp_app_info;
typedef const void *nvadsp_app_handle_t;
typedef void (*app_complete_status_notifier)(struct nvadsp_app_info *,
	enum adsp_app_status_msg, int32_t);

typedef struct adsp_app_mem {
	/* DRAM segment*/
	void      *dram;
	/* DRAM in shared memory segment. uncached */
	void      *shared;
	/* DRAM in shared memory segment. write combined */
	void      *shared_wc;
	/*  ARAM if available, DRAM OK */
	void      *aram;
	/* set to 1 if ARAM allocation succeeded else 0 meaning allocated from
	 * dram.
	 */
	uint32_t   aram_flag;
	/* ARAM Segment. exclusively */
	void      *aram_x;
	/* set to 1 if ARAM allocation succeeded */
	uint32_t   aram_x_flag;
} adsp_app_mem_t;

typedef struct adsp_app_iova_mem {
	/* DRAM segment*/
	uint32_t	dram;
	/* DRAM in shared memory segment. uncached */
	uint32_t	shared;
	/* DRAM in shared memory segment. write combined */
	uint32_t	shared_wc;
	/*  ARAM if available, DRAM OK */
	uint32_t	aram;
	/*
	 * set to 1 if ARAM allocation succeeded else 0 meaning allocated from
	 * dram.
	 */
	uint32_t	aram_flag;
	/* ARAM Segment. exclusively */
	uint32_t	aram_x;
	/* set to 1 if ARAM allocation succeeded */
	uint32_t	aram_x_flag;
} adsp_app_iova_mem_t;


typedef struct nvadsp_app_args {
	 /* number of arguments passed in */
	int32_t  argc;
	/* binary representation of arguments */
	int32_t  argv[ARGV_SIZE_IN_WORDS];
} nvadsp_app_args_t;

typedef struct nvadsp_app_info {
	const char *name;
	const int instance_id;
	const uint32_t token;
	const int state;
	adsp_app_mem_t mem;
	adsp_app_iova_mem_t iova_mem;
	struct list_head node;
	uint32_t stack_size;
	const void *handle;
	int return_status;
	struct completion wait_for_app_complete;
	struct completion wait_for_app_start;
	app_complete_status_notifier complete_status_notifier;
	struct work_struct complete_work;
	enum adsp_app_status_msg status_msg;
	void *priv;
} nvadsp_app_info_t;

#ifdef CONFIG_TEGRA_ADSP_DFS
/*
 * Override adsp freq and reinit actmon counters
 *
 * @params:
 * freq: adsp freq in KHz
 * return - final freq got set.
 *		- 0, incase of error.
 *
 */
unsigned long adsp_override_freq(unsigned long freq);
void adsp_update_dfs_min_rate(unsigned long freq);

/* Enable / disable dynamic freq scaling */
void adsp_update_dfs(bool enable);
#else
static inline unsigned long adsp_override_freq(unsigned long freq)
{
	return 0;
}

static inline void adsp_update_dfs_min_rate(unsigned long freq)
{
	return;
}

static inline void adsp_update_dfs(bool enable)
{
	return;
}
#endif

void *nvadsp_aram_request(void *aram_handle, const char *name, size_t size);
bool nvadsp_aram_release(void *aram_handle, void *handle);

unsigned long nvadsp_aram_get_address(void *handle);
void nvadsp_aram_print(void *aram_handle);

int adsp_usage_set(unsigned int  val);
unsigned int adsp_usage_get(void);


/* Multi-instance capable interface - START */

/**
 * nvadsp_get_handle: Fetch driver instance using unique identfier
 *
 * @dev_str : Unique identifier string for the driver instance
 *             (trailing unique identifer in compatible DT property,
 *             e.g. "adsp", "adsp1", etc.)
 *
 * Return : Driver instance handle on success, else NULL
 */
struct nvadsp_handle *nvadsp_get_handle(const char *dev_str);

/**
 * struct nvadsp_handle:
 *         - Function interfaces exposed on the driver handle
 *         - First argument to each API is the handle
 *            returned from nvadsp_get_handle()
 */
struct nvadsp_handle {
	/* Mailbox interfaces */
	status_t (*mbox_open)(struct nvadsp_handle *nvadsp_handle,
				struct nvadsp_mbox *mbox,
				uint16_t *mid, const char *name,
				nvadsp_mbox_handler_t handler, void *hdata);
	status_t (*mbox_close)(struct nvadsp_handle *nvadsp_handle,
				struct nvadsp_mbox *mbox);
	status_t (*mbox_send)(struct nvadsp_handle *nvadsp_handle,
				struct nvadsp_mbox *mbox, uint32_t data,
				uint32_t flags, bool block, unsigned int timeout);
	status_t (*mbox_recv)(struct nvadsp_handle *nvadsp_handle,
				struct nvadsp_mbox *mbox, uint32_t *data,
				bool block, unsigned int timeout);
	void (*mbox_set_ack_handler)(struct nvadsp_handle *nvadsp_handle,
				struct nvadsp_mbox *mbox,
				nvadsp_mbox_handler_t handler);

	/* OS interfaces */
	void (*get_os_version)(struct nvadsp_handle *nvadsp_handle,
				char *buf, int buf_size);
	int (*os_load)(struct nvadsp_handle *nvadsp_handle);
	int (*os_start)(struct nvadsp_handle *nvadsp_handle);
	int (*os_suspend)(struct nvadsp_handle *nvadsp_handle);
	void (*os_stop)(struct nvadsp_handle *nvadsp_handle);

	/* Debug interfaces */
	void (*set_adma_dump_reg)(struct nvadsp_handle *nvadsp_handle,
				void (*cb_adma_regdump)(void));
	void (*dump_adsp_sys)(struct nvadsp_handle *nvadsp_handle);

	/* Memory interfaces */
	void *(*alloc_coherent)(struct nvadsp_handle *nvadsp_handle,
			size_t size, dma_addr_t *da, gfp_t flags);
	void (*free_coherent)(struct nvadsp_handle *nvadsp_handle,
			size_t size, void *va, dma_addr_t da);

	/* App interfaces */
	nvadsp_app_handle_t (*app_load)(struct nvadsp_handle *nvadsp_handle,
			const char *appname, const char *appfile);
	nvadsp_app_info_t *(*app_init)(struct nvadsp_handle *nvadsp_handle,
			nvadsp_app_handle_t handle, nvadsp_app_args_t *args);
	int (*app_start)(struct nvadsp_handle *nvadsp_handle,
		nvadsp_app_info_t *app);
	nvadsp_app_info_t *(*run_app)(struct nvadsp_handle *nvadsp_handle,
		nvadsp_os_handle_t os_handle,
		const char *appfile, nvadsp_app_args_t *app_args,
		app_complete_status_notifier notifier, uint32_t stack_sz,
		uint32_t core_id, bool block);
	int (*app_stop)(struct nvadsp_handle *nvadsp_handle,
				nvadsp_app_info_t *app);
	int (*app_deinit)(struct nvadsp_handle *nvadsp_handle,
				nvadsp_app_info_t *app);
	void (*exit_app)(struct nvadsp_handle *nvadsp_handle,
				nvadsp_app_info_t *app, bool terminate);
	void (*app_unload)(struct nvadsp_handle *nvadsp_handle,
				nvadsp_app_handle_t handle);
	void (*set_app_complete_cb)(struct nvadsp_handle *nvadsp_handle,
		nvadsp_app_info_t *info, app_complete_status_notifier notifier);
	void (*wait_for_app_complete)(struct nvadsp_handle *nvadsp_handle,
				nvadsp_app_info_t *info);
	long (*wait_for_app_complete_timeout)(
		struct nvadsp_handle *nvadsp_handle,
		nvadsp_app_info_t *info, unsigned long timeout);
};

/* Multi-instance capable interface - END */


/* Legacy interface - START (works if only one driver instance is probed) */

static inline status_t nvadsp_mbox_open(struct nvadsp_mbox *mbox,
				uint16_t *mid, const char *name,
				nvadsp_mbox_handler_t handler, void *hdata)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->mbox_open(h, mbox, mid, name, handler, hdata);
}

static inline status_t nvadsp_mbox_close(struct nvadsp_mbox *mbox)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->mbox_close(h, mbox);
}

static inline status_t nvadsp_mbox_send(struct nvadsp_mbox *mbox,
				uint32_t data, uint32_t flags,
				bool block, unsigned int timeout)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->mbox_send(h, mbox, data, flags, block, timeout);
}

static inline status_t nvadsp_mbox_recv(struct nvadsp_mbox *mbox,
				uint32_t *data, bool block,
				unsigned int timeout)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->mbox_recv(h, mbox, data, block, timeout);
}

static inline void register_ack_handler(struct nvadsp_mbox *mbox,
			nvadsp_mbox_handler_t handler)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->mbox_set_ack_handler(h, mbox, handler);
}

static inline void nvadsp_get_os_version(char *buf, int buf_size)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->get_os_version(h, buf, buf_size);
}

static inline int nvadsp_os_load(void)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->os_load(h);
}

static inline int nvadsp_os_start(void)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->os_start(h);
}

static inline int nvadsp_os_suspend(void)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->os_suspend(h);
}

static inline void nvadsp_os_stop(void)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->os_stop(h);
}

static inline void nvadsp_set_adma_dump_reg(void (*cb_adma_regdump)(void))
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->set_adma_dump_reg(h, cb_adma_regdump);
}

static inline void dump_adsp_sys(void)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->dump_adsp_sys(h);
}

static inline void *nvadsp_alloc_coherent(
			size_t size, dma_addr_t *da, gfp_t flags)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->alloc_coherent(h, size, da, flags);
}

static inline void nvadsp_free_coherent(size_t size, void *va, dma_addr_t da)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->free_coherent(h, size, va, da);
}

static inline nvadsp_app_handle_t nvadsp_app_load(
			const char *appname, const char *appfile)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->app_load(h, appname, appfile);
}

static inline nvadsp_app_info_t *nvadsp_app_init(
		nvadsp_app_handle_t handle, nvadsp_app_args_t *args)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->app_init(h, handle, args);
}

static inline int nvadsp_app_start(nvadsp_app_info_t *app)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->app_start(h, app);
}

static inline nvadsp_app_info_t *nvadsp_run_app(
	nvadsp_os_handle_t os_handle, const char *appfile,
	nvadsp_app_args_t *app_args, app_complete_status_notifier notifier,
	uint32_t stack_sz, uint32_t core_id, bool block)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->run_app(h, os_handle, appfile, app_args,
			notifier, stack_sz, core_id, block);
}

static inline int nvadsp_app_stop(nvadsp_app_info_t *app)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->app_stop(h, app);
}

static inline int nvadsp_app_deinit(nvadsp_app_info_t *app)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->app_deinit(h, app);
}

static inline void nvadsp_exit_app(nvadsp_app_info_t *app, bool terminate)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->exit_app(h, app, terminate);
}

static inline void nvadsp_app_unload(nvadsp_app_handle_t handle)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->app_unload(h, handle);
}

static inline void set_app_complete_notifier(
			nvadsp_app_info_t *info,
			app_complete_status_notifier notifier)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->set_app_complete_cb(h, info, notifier);
}

static inline void wait_for_nvadsp_app_complete(nvadsp_app_info_t *info)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->wait_for_app_complete(h, info);
}

/**
 * wait_for_nvadsp_app_complete_timeout:
 * @info:  pointer to nvadsp_app_info_t
 * @timeout:  timeout value in jiffies
 *
 * This waits for either a completion of a specific app to be signaled or for a
 * specified timeout to expire. It is interruptible. The timeout is in jiffies.
 *
 * The return value is -ERESTARTSYS if interrupted, 0 if timed out,
 * positive (at least 1, or number of jiffies left till timeout) if completed.
 */
static inline long wait_for_nvadsp_app_complete_timeout(
			nvadsp_app_info_t *info, unsigned long timeout)
{
	struct nvadsp_handle *h = nvadsp_get_handle("");
	return h->wait_for_app_complete_timeout(h, info, timeout);
}

/* Legacy interface - END */

#endif /* __LINUX_TEGRA_NVADSP_H */
