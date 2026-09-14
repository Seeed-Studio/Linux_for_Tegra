// SPDX-License-Identifier: GPL-2.0-only
/* SPDX-FileCopyrightText: Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved. */

#include "mods_internal.h"

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/types.h>

#include <soc/tegra/ivc.h>
#include <soc/tegra/bpmp.h>

#define IVC_CHANNEL_SIZE  256
#define MRQ_MSG_SIZE      128
#define BPMP_MAIL_DO_ACK  (1U << 0U)
#define BPMP_IVC_TIMEOUT  120000 /* really large timeout to support simulation platforms */

static DEFINE_MUTEX(mods_bpmpipc_lock);

static const u32 MODS_CMD_UPHY_LANE_EOM_SCAN = 9;

struct mods_cmd_uphy_lane_eom_scan_request {
	u32 brick;
	u32 lane;
	u32 pcie_gen5;
};

struct mods_cmd_uphy_lane_eom_scan_response {
	u32 data;
};

struct mods_mrq_uphy_request {
	u16 lane;
	u16 cmd;
	struct mods_cmd_uphy_lane_eom_scan_request lane_eom_scan;
};

struct mods_mrq_uphy_response {
	struct mods_cmd_uphy_lane_eom_scan_response eom_status;
};

struct bpmp_ipc_ch {
	bool is_init;
	struct tegra_ivc ivc;
	void __iomem *db_base;
	void __iomem *req_base;
	void __iomem *resp_base;
	phys_addr_t db_phys_addr;
	phys_addr_t req_phys_addr;
	phys_addr_t resp_phys_addr;
};

static struct bpmp_ipc_ch mods_bpmp_ch = {.is_init = false};

static void bpmp_ivc_notify(struct tegra_ivc *ivc, void *data)
{
	struct bpmp_ipc_ch *bpmp_ipc_ch = (struct bpmp_ipc_ch *)data;

	__raw_writel(1, bpmp_ipc_ch->db_base);
}

static int bpmp_ipc_send(struct mods_client *client,
			 struct tegra_ivc *ivc,
			 const void *data,
			 size_t sz)
{
#if (KERNEL_VERSION(6, 2, 0) <= MODS_KERNEL_VERSION) || defined(__IOSYS_MAP_H__)
	int err;
	struct iosys_map ob;

	err = tegra_ivc_write_get_next_frame(ivc, &ob);
	if (err) {
		cl_error("failed to get next tegra-ivc output frame!\n");
		iosys_map_clear(&ob);
		return err;
	}
	iosys_map_memcpy_to(&ob, 0, data, sz);
#else
	void *frame;

	frame = tegra_ivc_write_get_next_frame(ivc);
	if (IS_ERR(frame)) {
		cl_error("failed to get next tegra-ivc output frame!\n");
		return PTR_ERR(frame);
	}

	memcpy_toio(frame, data, sz);
#endif

	return tegra_ivc_write_advance(ivc);
}

static int bpmp_ipc_recv(struct mods_client *client,
			 struct tegra_ivc *ivc,
			 void *data,
			 size_t sz,
			 u32 timeout_ms)
{
	int err;
#if (KERNEL_VERSION(6, 2, 0) <= MODS_KERNEL_VERSION) || defined(__IOSYS_MAP_H__)
	struct iosys_map ib;
#else
	const void *frame;
#endif
	ktime_t end;

	end = ktime_add_ms(ktime_get(), timeout_ms);

#if (KERNEL_VERSION(6, 2, 0) <= MODS_KERNEL_VERSION) || defined(__IOSYS_MAP_H__)
	do {
		err = tegra_ivc_read_get_next_frame(ivc, &ib);
		if (!err)
			break;
	} while (ktime_before(ktime_get(), end));
	if (err) {
		iosys_map_clear(&ib);
		err = tegra_ivc_read_get_next_frame(ivc, &ib);
		if (err) {
			cl_error("get next tegra-ivc input frame timeout\n");
			iosys_map_clear(&ib);
			return err;
		}
	}
	iosys_map_memcpy_from(data, &ib, 0, sz);
#else
	do {
		frame = tegra_ivc_read_get_next_frame(ivc);
		if (!IS_ERR(frame))
			break;
	} while (ktime_before(ktime_get(), end));

	if (IS_ERR(frame)) {
		frame = tegra_ivc_read_get_next_frame(ivc);

		if (IS_ERR(frame)) {
			cl_error("get next tegra-ivc input frame timeout\n");
			return -ETIMEDOUT;
		}
	}
	memcpy_fromio(data, frame, sz);
#endif

	err = tegra_ivc_read_advance(ivc);
	if (err < 0)
		cl_error("tegra_ivc read failed: %d\n", err);

	return err;
}

static int bpmp_transfer(struct mods_client *client,
			 struct tegra_bpmp_message *msg)
{
	int err;
	struct tegra_bpmp_mb_data req;
	struct tegra_bpmp_mb_data resp;

	req.code = msg->mrq;
	req.flags = BPMP_MAIL_DO_ACK;
	memcpy(req.data, msg->tx.data, msg->tx.size);
	err = bpmp_ipc_send(client, &mods_bpmp_ch.ivc, &req, sizeof(req));

	if (err == 0) {
		err = bpmp_ipc_recv(client, &mods_bpmp_ch.ivc,
				&resp,
				sizeof(resp),
				BPMP_IVC_TIMEOUT);
	}

	if (err == 0) {
		memcpy(msg->rx.data, resp.data, msg->rx.size);
		msg->rx.ret = resp.code;
	}

	return err;
}

static int mrq_uphy_lane_eom_scan(struct mods_client *client,
				  u32  brick,
				  u32  lane,
				  u32  pcie_gen5,
				  u32  *data)
{
	int err;
	struct mods_mrq_uphy_request req = {
		.cmd = cpu_to_le32(MODS_CMD_UPHY_LANE_EOM_SCAN)
	};
	struct mods_mrq_uphy_response resp;
	struct tegra_bpmp_message msg = {
		.mrq = MRQ_UPHY,
		.tx = {
			.data = &req,
			.size = sizeof(req),
		},
		.rx = {
			.data = &resp,
			.size = sizeof(resp),
		},
	};

	req.lane_eom_scan.brick     = brick;
	req.lane_eom_scan.lane      = lane;
	req.lane_eom_scan.pcie_gen5 = pcie_gen5;

	err = bpmp_transfer(client, &msg);

	if (err < 0) {
		return err;
	} else if (msg.rx.ret < 0) {
		err = -EINVAL;
		return err;
	}

	*data = resp.eom_status.data;
	return err;
}

static int bpmp_ioremap(struct mods_client *client,
			struct bpmp_ipc_ch *bpmp_ipc_ch,
			u64 db_phys_addr,
			u64 req_phys_addr,
			u64 resp_phys_addr)
{
	bpmp_ipc_ch->db_phys_addr   = db_phys_addr;
	bpmp_ipc_ch->req_phys_addr  = req_phys_addr;
	bpmp_ipc_ch->resp_phys_addr = resp_phys_addr;

	bpmp_ipc_ch->db_base = ioremap(bpmp_ipc_ch->db_phys_addr,   64);
	if (!bpmp_ipc_ch->db_base) {
		cl_error("failed to remap aperture: 0x%llx\n",
			 (unsigned long long)bpmp_ipc_ch->db_phys_addr);
		return -ENOMEM;
	}
	bpmp_ipc_ch->req_base  = ioremap(bpmp_ipc_ch->req_phys_addr,  IVC_CHANNEL_SIZE);
	if (!bpmp_ipc_ch->req_base) {
		iounmap(bpmp_ipc_ch->db_base);
		cl_error("failed to remap aperture: 0x%llx\n",
			 (unsigned long long)bpmp_ipc_ch->req_phys_addr);
		return -ENOMEM;
	}
	bpmp_ipc_ch->resp_base = ioremap(bpmp_ipc_ch->resp_phys_addr, IVC_CHANNEL_SIZE);
	if (!bpmp_ipc_ch->resp_base) {
		iounmap(bpmp_ipc_ch->db_base);
		iounmap(bpmp_ipc_ch->req_base);
		cl_error("failed to remap aperture: 0x%llx\n",
			 (unsigned long long)bpmp_ipc_ch->resp_phys_addr);
		return -ENOMEM;
	}

	return OK;
}

static void bpmp_iounmap(struct bpmp_ipc_ch *bpmp_ipc_ch)
{
	iounmap(bpmp_ipc_ch->db_base);
	iounmap(bpmp_ipc_ch->req_base);
	iounmap(bpmp_ipc_ch->resp_base);

	bpmp_ipc_ch->db_phys_addr = 0;
	bpmp_ipc_ch->req_phys_addr = 0;
	bpmp_ipc_ch->resp_phys_addr = 0;
}

static int bpmp_ipc_channel_init(struct mods_client *client,
				 struct bpmp_ipc_ch *bpmp_ipc_ch)
{
	int err;
	ktime_t end;

#if (KERNEL_VERSION(6, 2, 0) <= MODS_KERNEL_VERSION) || defined(__IOSYS_MAP_H__)
	struct iosys_map rx, tx;

	iosys_map_set_vaddr_iomem(&rx, bpmp_ipc_ch->resp_base);
	iosys_map_set_vaddr_iomem(&tx, bpmp_ipc_ch->req_base);

	err = tegra_ivc_init(&bpmp_ipc_ch->ivc, NULL,
			     &rx, bpmp_ipc_ch->resp_phys_addr,
			     &tx, bpmp_ipc_ch->req_phys_addr,
			     1, MRQ_MSG_SIZE,
			     bpmp_ivc_notify, bpmp_ipc_ch);
#else
	err = tegra_ivc_init(&bpmp_ipc_ch->ivc, NULL,
			 bpmp_ipc_ch->resp_base, 0,
			 bpmp_ipc_ch->req_base, 0,
			 1, MRQ_MSG_SIZE,
			 bpmp_ivc_notify, bpmp_ipc_ch);
#endif

	if (err != 0) {
		cl_error("tegra-ivc init failed: %d\n", err);
		return err;
	}

	tegra_ivc_reset(&bpmp_ipc_ch->ivc);

	end = ktime_add_us(ktime_get(), 2000 * 1000);

	while (tegra_ivc_notified(&bpmp_ipc_ch->ivc) != 0) {
		usleep_range(100, 200);
		if (ktime_after(ktime_get(), end)) {
			cl_error("initialize IVC connection timeout\n");
			err = -ETIMEDOUT;
			break;
		}
	}

	bpmp_ipc_ch->is_init = true;

	return err;
}

static void bpmp_ipc_channel_uninit(struct bpmp_ipc_ch *bpmp_ipc_ch)
{
	tegra_ivc_cleanup(&bpmp_ipc_ch->ivc);
}

int mods_bpmpipc_init(struct mods_client *client,
		      u64 db_phys_addr,
		      u64 req_phys_addr,
		      u64 resp_phys_addr)
{
	int err = OK;

	if (mods_bpmp_ch.is_init) {
		if (mods_bpmp_ch.db_phys_addr == db_phys_addr &&
		    mods_bpmp_ch.req_phys_addr == req_phys_addr &&
		    mods_bpmp_ch.resp_phys_addr == resp_phys_addr)
			return OK;
		mods_bpmpipc_cleanup();
	}

	err = bpmp_ioremap(client,
			   &mods_bpmp_ch,
			   db_phys_addr,
			   req_phys_addr,
			   resp_phys_addr);
	if (err != OK)
		return err;

	err = bpmp_ipc_channel_init(client, &mods_bpmp_ch);
	if (err != OK) {
		bpmp_iounmap(&mods_bpmp_ch);
		return err;
	}

	mods_bpmp_ch.is_init = true;
	mods_debug_printk(DEBUG_TEGRADMA, "bpmp ipc init done\n");

	return err;
}

void mods_bpmpipc_cleanup(void)
{
	if (!mods_bpmp_ch.is_init)
		return;

	bpmp_ipc_channel_uninit(&mods_bpmp_ch);
	bpmp_iounmap(&mods_bpmp_ch);
	mods_bpmp_ch.is_init = false;
}

int esc_mods_bpmp_uphy_lane_eom_scan(struct mods_client *client,
				     struct MODS_BPMP_UPHY_LANE_EOM_SCAN_PARAMS *p)
{
	int err = OK;

	mutex_lock(&mods_bpmpipc_lock);

	err = mods_bpmpipc_init(client,
				p->db_phys_addr,
				p->req_phys_addr,
				p->resp_phys_addr);
	if (err != OK)
		goto error;

	err = mrq_uphy_lane_eom_scan(client,
				     p->brick,
				     p->lane,
				     p->pcie_gen5,
				     &p->data);

	if (err != OK)
		cl_error("mrq uphy lane eom scan failed with brick(%u), lane(%u), pcie_gen5(%u)\n",
			 p->brick, p->lane, p->pcie_gen5);

error:
	mutex_unlock(&mods_bpmpipc_lock);
	return err;
}
