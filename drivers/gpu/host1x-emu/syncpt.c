// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-fence.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include "dev.h"
#include "syncpt.h"

#ifdef HOST1X_EMU_SYNC_INC_TASKLET
static void tasklet_fn(struct tasklet_struct *unused);
static DEFINE_PER_CPU(struct host1x_syncpt *, tasklet_sp);

DECLARE_TASKLET(syncpt_tasklet, tasklet_fn);

static void tasklet_fn(struct tasklet_struct *unused)
{
	struct host1x_syncpt *sp = NULL;

	sp = this_cpu_read(tasklet_sp);
	if (sp != NULL)
		host1x_poll_irq_check_syncpt_fence(sp);
}
#endif

static void syncpt_release(struct kref *ref)
{
    struct host1x_syncpt *sp = container_of(ref, struct host1x_syncpt, ref);

    atomic_set(&sp->max_val, HOST1X_EMU_EXPORT_CALL(host1x_syncpt_read(sp)));
    sp->locked = false;

    mutex_lock(&sp->host->syncpt_mutex);
    kfree(sp->name);
    sp->name = NULL;
    sp->client_managed = false;
    mutex_unlock(&sp->host->syncpt_mutex);
}

void host1x_syncpt_restore(struct host1x *host)
{
    struct host1x_syncpt *sp_base = host->syncpt;
    unsigned int i;

    for (i = host->syncpt_base; i < host->syncpt_end; i++) {
        host1x_hw_syncpt_restore(host, sp_base + i);
    }
    wmb();
}

void host1x_syncpt_save(struct host1x *host)
{
    struct host1x_syncpt *sp_base = host->syncpt;
    unsigned int i;

    for (i = 0; i < host1x_syncpt_nb_pts(host); i++) {
        if (host1x_syncpt_client_managed(sp_base + i))
            host1x_hw_syncpt_load(host, sp_base + i);
        else
            WARN_ON(!host1x_syncpt_idle(sp_base + i));
    }
}

/**
 * Updates the cached syncpoint value by reading a new value
 * from the memory
 */
u32 host1x_syncpt_load(struct host1x_syncpt *sp)
{
    u32 val;

    val = host1x_hw_syncpt_load(sp->host, sp);
    return val;
}

/**
 * Returns true if syncpoint is expired, false if we may need to wait
 */
bool host1x_syncpt_is_expired(struct host1x_syncpt *sp, u32 thresh)
{
    u32 current_val;

    smp_rmb();

    current_val = (u32)atomic_read(&sp->min_val);
    return ((current_val - thresh) & 0x80000000U) == 0U;
}

int host1x_syncpt_init(struct host1x *host)
{
    unsigned int i;
    struct host1x_syncpt *syncpt;

    syncpt = devm_kcalloc(host->dev, host->syncpt_count, sizeof(*syncpt),
                  GFP_KERNEL);
    if (!syncpt) {
        pr_info("Host1x-EMU: Memory allocation for syncpoint structure failed\n");
        return -ENOMEM;
    }

    for (i = 0; i < host->syncpt_count; i++) {
        syncpt[i].id = i;
        syncpt[i].host = host;
        syncpt[i].client_managed = true;
        /*Setting default syncpoint read-only pool*/
        syncpt[i].pool = &host->pools[host->ro_pool_id];
    }

    for (i = 0; i < host->num_pools; i++) {
        struct host1x_syncpt_pool *pool = &host->pools[i];
        unsigned int j;

        for (j = pool->sp_base; j < pool->sp_end; j++)
            syncpt[j].pool = pool;
    }

    mutex_init(&host->syncpt_mutex);
    host->syncpt = syncpt;

    return 0;
}

void host1x_syncpt_deinit(struct host1x *host)
{
    struct host1x_syncpt *sp = host->syncpt;
    unsigned int i;

    for (i = 0; i < host->syncpt_count; i++, sp++)
        kfree(sp->name);

    /**
     * Deallocating syncpoint array.
     * Syncpoint deinit is invoked from drvier remove callback
     * or drvier probe failure.
     */
    kfree(host->syncpt);
}

unsigned int host1x_syncpt_nb_pts(struct host1x *host)
{
    return host->syncpt_count;
}

/**
 * host1x_get_dma_mask() - query the supported DMA mask for host1x
 * @host1x: host1x instance
 *
 * Note that this returns the supported DMA mask for host1x, which can be
 * different from the applicable DMA mask under certain circumstances.
 */
HOST1X_EMU_EXPORT_DECL(u64, host1x_get_dma_mask(struct host1x *host1x))
{
    return host1x->info->dma_mask;
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_get_dma_mask);

/**
 * @brief Increment syncpoint refcount
 *
 * @sp: syncpoint
 */
HOST1X_EMU_EXPORT_DECL(struct host1x_syncpt*, host1x_syncpt_get(
                                struct host1x_syncpt *sp))
{
    kref_get(&sp->ref);

    return sp;
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_syncpt_get);

/**
 * @brief Obtain a syncpoint by ID
 *
 * @host: host1x controller
 * @id: syncpoint ID
 */
HOST1X_EMU_EXPORT_DECL(struct host1x_syncpt*, host1x_syncpt_get_by_id(
                                struct host1x *host, unsigned int id))
{
    if (id >= host->syncpt_count)
        return NULL;

    if (kref_get_unless_zero(&host->syncpt[id].ref))
        return &host->syncpt[id];
    else
        return NULL;
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_syncpt_get_by_id);

/**
 * @brief Obtain a syncpoint by ID but don't increase the refcount.
 *
 * @host: host1x controller
 * @id: syncpoint ID
 */
HOST1X_EMU_EXPORT_DECL(struct host1x_syncpt*, host1x_syncpt_get_by_id_noref(struct host1x *host, unsigned int id))
{
    if (id >= host->syncpt_count)
        return NULL;

    return &host->syncpt[id];
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_syncpt_get_by_id_noref);

/**
 * @brief Read the current syncpoint value
 *
 * @sp: host1x syncpoint
 */
HOST1X_EMU_EXPORT_DECL(u32, host1x_syncpt_read(struct host1x_syncpt *sp))
{
    return host1x_syncpt_load(sp);
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_syncpt_read);

/**
 * @brief Read minimum syncpoint value.
 *
 * The minimum syncpoint value is a shadow of the current sync point value
 * in syncpoint-memory.
 *
 * @sp: host1x syncpoint
 *
 */
HOST1X_EMU_EXPORT_DECL(u32, host1x_syncpt_read_min(struct host1x_syncpt *sp))
{
    smp_rmb();

    return (u32)atomic_read(&sp->min_val);
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_syncpt_read_min);

/**
 * @brief Read maximum syncpoint value.
 *
 * The maximum syncpoint value indicates how many operations there are in queue,
 * either in channel or in a software thread.
 *
 * @sp: host1x syncpoint
 *
 */
HOST1X_EMU_EXPORT_DECL(u32, host1x_syncpt_read_max(struct host1x_syncpt *sp))
{
    smp_rmb();
    return (u32)atomic_read(&sp->max_val);
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_syncpt_read_max);

/**
 * @brief Increment syncpoint value from CPU, updating cache
 * @sp: host1x syncpoint
 */
HOST1X_EMU_EXPORT_DECL(int, host1x_syncpt_incr(struct host1x_syncpt *sp))
{
	int err;

	err = host1x_hw_syncpt_cpu_incr(sp->host, sp);
#ifdef HOST1X_EMU_SYNC_INC_TASKLET
	/*Improve Signaling performance*/
	this_cpu_write(tasklet_sp, sp);
	tasklet_schedule(&syncpt_tasklet);
#else
	host1x_poll_irq_check_syncpt_fence(sp);
#endif
	return err;
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_syncpt_incr);

/**
 * @brief Update the value sent to hardware
 *
 * @sp: host1x syncpoint
 * @incrs: number of increments
 */
HOST1X_EMU_EXPORT_DECL(u32, host1x_syncpt_incr_max(struct host1x_syncpt *sp, u32 incrs))
{
    return (u32)atomic_add_return(incrs, &sp->max_val);
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_syncpt_incr_max);

/**
 * @brief Allocate a syncpoint
 *
 * Allocates a hardware syncpoint for the caller's use. The caller then has
 * the sole authority to mutate the syncpoint's value until it is freed again.
 *
 * If no free syncpoints are available, or a NULL name was specified, returns
 * NULL.
 *
 * @host: host1x device data
 * @flags: bitfield of HOST1X_SYNCPT_* flags
 * @name: name for the syncpoint for use in debug prints
 */
HOST1X_EMU_EXPORT_DECL(struct host1x_syncpt*, host1x_syncpt_alloc(struct host1x *host,
                                          unsigned long flags,
                                          const char *name))
{
    struct host1x_syncpt *sp = host->syncpt + host->syncpt_base;
    struct host1x_syncpt_pool *pool = NULL;
    char *full_name;
    unsigned int i;

    if (!name) {
        dev_err(host->dev, "syncpoints name null\n");
        return NULL;
    }

    /* Only Read only pool*/
    if (host->num_pools == 0) {
        dev_err(host->dev,
            "Syncpoints alloc fail, only RO-Only pool avialable\n");
        return NULL;
    }

    /**
     * TODO: Update this based on new pools logic
     */
    if (flags & HOST1X_SYNCPT_GPU) {
        for (i = 0; i < host->num_pools; i++) {
            if (!strcmp(host->pools[i].name, "gpu")) {
                pool = &host->pools[i];
                break;
            }
        }
    }

    /**
     * TODO: syncpt_mutex is for entire synpoint list
     *       maybe, update this to syncpoint-pool level lock
     */
    mutex_lock(&host->syncpt_mutex);

    /**
     * TODO: Optimize syncpoint allocation, serial allocation
     * dosen't effectively utilize per pool polling thread.
     */
	/* FIXME: WAR to allocate syncpoint from index 1, As at client level synpt-id 0 is invalid*/
	for (i = host->syncpt_base + 1, sp = sp + 1; i < host->syncpt_end; i++, sp++) {

		/* Do pool verification if pool selected */
		if ((pool != NULL) && (sp->pool != pool))
			continue;

		/* Skip if pool is read only pool */
		if (sp->pool == &host->pools[host->ro_pool_id])
			continue;

		if (kref_read(&sp->ref) == 0) {
			break;
		}
	}

    if (i >= host->syncpt_end) {
        goto unlock;
    }

    full_name = kasprintf(GFP_KERNEL, "%u-%s", sp->id, name);
    if (!full_name) {
        goto unlock;
    }
    sp->name = full_name;

    if (flags & HOST1X_SYNCPT_CLIENT_MANAGED)
        sp->client_managed = true;
    else
        sp->client_managed = false;

    kref_init(&sp->ref);

    mutex_unlock(&host->syncpt_mutex);
    return sp;

unlock:
    mutex_unlock(&host->syncpt_mutex);
    return NULL;
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_syncpt_alloc);

/**
 * @brief Free a requested syncpoint
 *
 * Release a syncpoint previously allocated using host1x_syncpt_request().
 * A host1x client driver should call this when the syncpoint is no longer
 * in use.
 *
 * @sp: host1x syncpoint
 */
HOST1X_EMU_EXPORT_DECL(void, host1x_syncpt_put(struct host1x_syncpt *sp))
{
    if (!sp)
        return;

    kref_put(&sp->ref, syncpt_release);
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_syncpt_put);

/**
 * @brief Retrieve syncpoint ID
 * @sp: host1x syncpoint
 *
 * Given a pointer to a struct host1x_syncpt, retrieves its ID. This ID is
 * often used as a value to program into registers that control how hardware
 * blocks interact with syncpoints.
 */
HOST1X_EMU_EXPORT_DECL(u32, host1x_syncpt_id(struct host1x_syncpt *sp))
{
    return sp->id;
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_syncpt_id);

/**
 * @brief Wait for a syncpoint to reach a given threshold value.
 *
 * @sp: host1x syncpoint
 * @thresh: threshold
 * @timeout: maximum time to wait for the syncpoint to reach the given value
 * @value: return location for the syncpoint value
 * @ts: return location for completion timestamp
 */
HOST1X_EMU_EXPORT_DECL(int, host1x_syncpt_wait_ts(struct host1x_syncpt *sp,
                            u32 thresh, long timeout, u32 *value, ktime_t *ts))
{
    ktime_t             spin_timeout;
    ktime_t             time;
    struct dma_fence    *fence;
    long                wait_err;

    if (timeout < 0)
        timeout = LONG_MAX;

    /*
     * Even 1 jiffy is longer than 50us, so assume timeout is over 50us
     * always except for polls (timeout=0)
     */
    spin_timeout = ktime_add_us(ktime_get(), timeout > 0 ? 50 : 0);
    for (;;) {
        host1x_hw_syncpt_load(sp->host, sp);
        time = ktime_get();
        if (value)
            *value = host1x_syncpt_load(sp);
        if (ts)
            *ts = time;
        if (host1x_syncpt_is_expired(sp, thresh))
            return 0;
        if (ktime_compare(time, spin_timeout) > 0)
            break;
        udelay(5);
    }

    if (timeout == 0)
        return -EAGAIN;

    fence = HOST1X_EMU_EXPORT_CALL(host1x_fence_create(sp, thresh, false));
    if (IS_ERR(fence))
        return PTR_ERR(fence);

    wait_err = dma_fence_wait_timeout(fence, true, timeout);
    if (wait_err == 0)
        HOST1X_EMU_EXPORT_CALL(host1x_fence_cancel(fence));

    if (value)
        *value = host1x_syncpt_load(sp);
    if (ts)
        *ts = fence->timestamp;

    dma_fence_put(fence);

    /*
     * Don't rely on dma_fence_wait_timeout return value,
     * since it returns zero both on timeout and if the
     * wait completed with 0 jiffies left.
     */
    host1x_hw_syncpt_load(sp->host, sp);
    if (wait_err == 0 && !host1x_syncpt_is_expired(sp, thresh))
        return -EAGAIN;
    else if (wait_err < 0)
        return wait_err;
    else
        return 0;
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_syncpt_wait_ts);

/**
 * @brief Get the physical address of the syncpoint aperture, stride and number of syncpoints
 *
 * @param host: host1x instance
 * @param base: physical address of syncpoint aperture
 * @param stride: stride between syncpoints
 * @param num_syncpts: number of syncpoints
 *
 * @return 0 if successful
 */
HOST1X_EMU_EXPORT_DECL(int, host1x_syncpt_get_shim_info(struct host1x *host,
			phys_addr_t *base, u32 *stride, u32 *num_syncpts))
{
	if (!host || !base || !stride || !num_syncpts)
		return -EINVAL;

	*base = host->syncpt_phy_apt;
	*stride = host->syncpt_page_size;
	*num_syncpts = host->syncpt_count;

	return 0;
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_syncpt_get_shim_info);

/**
 * @brief Wait for a syncpoint to reach a given threshold value
 *
 * @sp: host1x syncpoint
 * @thresh: threshold
 * @timeout: maximum time to wait for the syncpoint to reach the given value
 * @value: return location for the syncpoint value
 */
HOST1X_EMU_EXPORT_DECL(int, host1x_syncpt_wait(struct host1x_syncpt *sp,
                       u32 thresh, long timeout, u32 *value))
{
    return HOST1X_EMU_EXPORT_CALL(host1x_syncpt_wait_ts(sp, thresh,
                                        timeout, value, NULL));
}
HOST1X_EMU_EXPORT_SYMBOL(host1x_syncpt_wait);
