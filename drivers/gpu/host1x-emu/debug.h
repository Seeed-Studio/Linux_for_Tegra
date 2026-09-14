/*
 * SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef __HOST1X_EMU_DEBUG_H
#define __HOST1X_EMU_DEBUG_H

#include <linux/debugfs.h>
#include <linux/seq_file.h>

struct host1x;

struct output {
    void (*fn)(void *ctx, const char *str, size_t len, bool cont);
    void *ctx;
    char buf[256];
};

static inline void write_to_seqfile(void *ctx, const char *str, size_t len,
                                    bool cont)
{
    seq_write((struct seq_file *)ctx, str, len);
}

static inline void write_to_printk(void *ctx, const char *str, size_t len,
                                   bool cont)
{
    if (cont)
        pr_cont("%s", str);
    else
        pr_info("%s", str);
}

void __printf(2, 3) host1x_debug_output(struct output *o, const char *fmt, ...);
void __printf(2, 3) host1x_debug_cont(struct output *o, const char *fmt, ...);

void host1x_debug_init(struct host1x *host1x);
void host1x_debug_deinit(struct host1x *host1x);
void host1x_debug_dump_syncpts(struct host1x *host1x);

void host1x_sanity_init(struct host1x *host1x);
void nvhost_sanity_init(struct host1x *host1x);
#endif
