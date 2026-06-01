/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#include "dev.h"
#include "debug.h"
#include "include/linux/symbol-emu.h"

void host1x_debug_output(struct output *o, const char *fmt, ...)
{
    va_list args;
    int len;

    va_start(args, fmt);
    len = vsnprintf(o->buf, sizeof(o->buf), fmt, args);
    va_end(args);

    o->fn(o->ctx, o->buf, len, false);
}

void host1x_debug_cont(struct output *o, const char *fmt, ...)
{
    va_list args;
    int len;

    va_start(args, fmt);
    len = vsnprintf(o->buf, sizeof(o->buf), fmt, args);
    va_end(args);

    o->fn(o->ctx, o->buf, len, true);
}

static void show_syncpts(struct host1x *m, struct output *o, bool show_all)
{
    unsigned long irqflags;
    struct list_head *pos;
    unsigned int i;

    host1x_debug_output(o, "---- Emulated Syncpts ----\n");
    for (i = 0; i < host1x_syncpt_nb_pts(m); i++) {
        u32 max = HOST1X_EMU_EXPORT_CALL(host1x_syncpt_read_max(m->syncpt + i));
        u32 min = host1x_syncpt_load(m->syncpt + i);
        unsigned int waiters = 0;

        spin_lock_irqsave(&m->syncpt[i].fences.lock, irqflags);
        list_for_each(pos, &m->syncpt[i].fences.list)
                waiters++;
        spin_unlock_irqrestore(&m->syncpt[i].fences.lock, irqflags);

        if (!kref_read(&m->syncpt[i].ref))
            continue;

        if (!min && !max && !waiters)
            continue;

        host1x_debug_output(o,
                    "id %u (%s) min %d max %d (%d waiters)\n",
                    i, m->syncpt[i].name, min, max, waiters);
    }
    host1x_debug_output(o, "\n");
}

static int host1x_debug_show(struct seq_file *s, void *unused)
{
    struct output o = {
        .fn = write_to_seqfile,
        .ctx = s
    };

    show_syncpts(s->private, &o, false);
    return 0;
}

void host1x_debug_dump_syncpts(struct host1x *host1x)
{
    struct output o = {
        .fn = write_to_printk
    };

    show_syncpts(host1x, &o, false);
}

static int host1x_debug_open(struct inode *inode, struct file *file)
{
    return single_open(file, host1x_debug_show, inode->i_private);
}

static const struct file_operations host1x_debug_fops = {
    .open           = host1x_debug_open,
    .read           = seq_read,
    .llseek         = seq_lseek,
    .release        = single_release,
};

static void host1x_debugfs_init(struct host1x *host1x)
{
    struct dentry *de;
    char dir_name[64];
    int numa_node = dev_to_node(host1x->dev);

    if (numa_node != NUMA_NO_NODE)
        sprintf(dir_name, "tegra-host1x-emu.%d", numa_node);
    else
        sprintf(dir_name, "tegra-host1x-emu");

    de = debugfs_create_dir(dir_name, NULL);
    host1x->debugfs = de;

    /* Status File*/
    debugfs_create_file("status", S_IRUGO, de, host1x, &host1x_debug_fops);
}

static void host1x_debugfs_exit(struct host1x *host1x)
{
    debugfs_remove_recursive(host1x->debugfs);
}

void host1x_debug_init(struct host1x *host1x)
{
    if (IS_ENABLED(CONFIG_DEBUG_FS)) {
        host1x_debugfs_init(host1x);
    }
}

void host1x_debug_deinit(struct host1x *host1x)
{
    if (IS_ENABLED(CONFIG_DEBUG_FS))
        host1x_debugfs_exit(host1x);
}
