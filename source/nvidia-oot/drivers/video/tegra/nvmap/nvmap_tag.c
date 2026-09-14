// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2016-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Allocation tag routines for nvmap
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/moduleparam.h>

#include <trace/events/nvmap.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "nvmap_dev.h"
#include "nvmap_dev_int.h"

struct nvmap_tag_entry {
	struct rb_node node;
	atomic_t ref;		/* reference count (i.e., # of duplications) */
	u32 tag;
};

static struct nvmap_tag_entry *nvmap_search_tag_entry(struct rb_root *root, u32 tag)
{
	struct rb_node *node = root->rb_node;  /* top of the tree */
	struct nvmap_tag_entry *entry;

	while (node) {
		entry = rb_entry(node, struct nvmap_tag_entry, node);

		if (entry->tag > tag)
			node = node->rb_left;
		else if (entry->tag < tag)
			node = node->rb_right;
		else
			return entry;  /* Found it */
	}
	return NULL;
}

/* must hold tag_lock */
char *__nvmap_tag_name(struct nvmap_device *dev, u32 tag)
{
	struct nvmap_tag_entry *entry;

	entry = nvmap_search_tag_entry(&dev->tags, tag);
	return entry ? (char *)(entry + 1) : "";
}
