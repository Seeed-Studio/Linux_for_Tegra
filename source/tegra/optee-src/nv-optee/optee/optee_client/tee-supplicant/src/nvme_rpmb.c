/*
 * Copyright (c) 2016, Linaro Limited
 * All rights reserved.
 * Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <fcntl.h>
#include <linux/types.h>
#include <netinet/in.h>
#include <pthread.h>
#include <nvme_rpmb.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <tee_client_api.h>
#include <teec_trace.h>
#include <tee_supplicant.h>
#include <unistd.h>
#include <errno.h>

struct nvme_psd_31{
	uint16_t		mp;
	uint8_t			rsvd2;
	uint8_t			flags;
	uint32_t		enlat;
	uint32_t		exlat;
	uint8_t			rrt;
	uint8_t			rrl;
	uint8_t			rwt;
	uint8_t			rwl;
	uint16_t		idlp;
	uint8_t			ips;
	uint8_t			rsvd19;
	uint16_t		actp;
	uint8_t			apws;
	uint8_t			rsvd23[9];
};

struct nvme_identify_controller {
	uint16_t		vid;
	uint16_t		ssvid;
	char			sn[20];
	char			mn[40];
	char			fr[8];
	uint8_t			rab;
	uint8_t			ieee[3];
	uint8_t			cmic;
	uint8_t			mdts;
	uint16_t		cntlid;
	uint32_t		ver;
	uint32_t		rtd3r;
	uint32_t		rtd3e;
	uint32_t		oaes;
	uint32_t		ctratt;
	uint16_t		rrls;
	uint8_t			rsvd102[9];
	uint8_t			cntrltype;
	uint8_t			fguid[16];
	uint16_t		crdt1;
	uint16_t		crdt2;
	uint16_t		crdt3;
	uint8_t			rsvd134[119];
	uint8_t			nvmsr;
	uint8_t			vwci;
	uint8_t			mec;
	uint16_t		oacs;
	uint8_t			acl;
	uint8_t			aerl;
	uint8_t			frmw;
	uint8_t			lpa;
	uint8_t			elpe;
	uint8_t			npss;
	uint8_t			avscc;
	uint8_t			apsta;
	uint16_t		wctemp;
	uint16_t		cctemp;
	uint16_t		mtfa;
	uint32_t		hmpre;
	uint32_t		hmmin;
	uint8_t			tnvmcap[16];
	uint8_t			unvmcap[16];
	uint32_t		rpmbs;
	uint16_t		edstt;
	uint8_t			dsto;
	uint8_t			fwug;
	uint16_t		kas;
	uint16_t		hctma;
	uint16_t		mntmt;
	uint16_t		mxtmt;
	uint32_t		sanicap;
	uint32_t		hmminds;
	uint16_t		hmmaxd;
	uint16_t		nsetidmax;
	uint16_t		endgidmax;
	uint8_t			anatt;
	uint8_t			anacap;
	uint32_t		anagrpmax;
	uint32_t		nanagrpid;
	uint32_t		pels;
	uint16_t		domainid;
	uint8_t			rsvd358[10];
	uint8_t			megcap[16];
	uint8_t			rsvd384[128];
	uint8_t			sqes;
	uint8_t			cqes;
	uint16_t		maxcmd;
	uint32_t		nn;
	uint16_t		oncs;
	uint16_t		fuses;
	uint8_t			fna;
	uint8_t			vwc;
	uint16_t		awun;
	uint16_t		awupf;
	uint8_t			icsvscc;
	uint8_t			nwpc;
	uint16_t		acwu;
	uint16_t		ocfs;
	uint32_t		sgls;
	uint32_t		mnan;
	uint8_t			maxdna[16];
	uint32_t		maxcna;
	uint8_t			rsvd564[204];
	char			subnqn[256];
	uint8_t			rsvd1024[768];
	uint32_t		ioccsz;
	uint32_t		iorcsz;
	uint16_t		icdoff;
	uint8_t			fcatt;
	uint8_t			msdbd;
	uint16_t		ofcs;
	uint8_t			dctype;
	uint8_t			rsvd1807[241];
	struct nvme_psd_31	psd[32];
	uint8_t			vs[1024];
};

/* identify command */
#define NVME_IDENTIFY_CDW10_CNTID_SHIFT			16
#define NVME_IDENTIFY_CDW10_CNS_SHIFT			0
#define NVME_IDENTIFY_CDW11_CNSSPECID_SHIFT		0
#define NVME_IDENTIFY_CDW11_CSI_SHIFT			24
#define NVME_IDENTIFY_CDW14_UUID_SHIFT			0
#define NVME_IDENTIFY_CDW10_CNTID_MASK			0xffff
#define NVME_IDENTIFY_CDW10_CNS_MASK			0xff
#define NVME_IDENTIFY_CDW11_CNSSPECID_MASK		0xffff
#define NVME_IDENTIFY_CDW11_CSI_MASK			0xff
#define NVME_IDENTIFY_CDW14_UUID_MASK			0x7f

#define NVME_IDENTIFY_CNS_CTRL				0x01
#define NVME_CSI_NVM					0x0
#define NVME_NSID_NONE					0x0
#define NVME_CNTLID_NONE				0x0
#define NVME_CNSSPECID_NONE				0x0
#define NVME_UUID_NONE					0x0

#define NVME_DEV_SN_LENGTH				20

/* security comamnd */
#define NVME_SECURITY_NSSF_SHIFT			0
#define NVME_SECURITY_SPSP0_SHIFT			8
#define NVME_SECURITY_SPSP1_SHIFT			16
#define NVME_SECURITY_SECP_SHIFT			24
#define NVME_SECURITY_NSSF_MASK				0xff
#define NVME_SECURITY_SPSP0_MASK			0xff
#define NVME_SECURITY_SPSP1_MASK			0xff
#define NVME_SECURITY_SECP_MASK                 	0xff

#define NVME_RPMB_SECP					0xEA
#define NVME_RPMB_SPSP0					0x01
#define NVME_RPMB_SPSP1					0x00

/* NVMe admin opcodes */
#define NVME_ADMIN_IDENTIFY				0x06
#define NVME_ADMIN_SECURITY_SEND			0x81
#define NVME_ADMIN_SECURITY_RECV			0x82

#define NVME_IDENTIFY_DATA_SIZE				4096

/* default timeout */
#define NVME_DEFAULT_IOCTL_TIMEOUT			0

/* Request */
struct nvme_rpmb_req {
	uint16_t cmd;
#define NVME_RPMB_CMD_DATA_REQ				0x00
#define NVME_RPMB_CMD_GET_RPMBS_INFO			0x01
	uint16_t dev_id;
	/* Optional data frames (rpmb_data_frame) follow */
};
#define NVME_RPMB_REQ_DATA(req) ((void *)((struct nvme_rpmb_req *)(req) + 1))

/*
 * This structure is shared with OP-TEE and the NVMe ioctl layer.
 * It is the "data frame for RPMB access" defined by NVMe spec,
 * minus the start and stop bits.
 */
struct nvme_rpmb_data_frame {
	uint8_t stuff_bytes[191];
	uint8_t key_mac[32];
	uint8_t target;
	uint8_t nonce[16];
	uint32_t write_counter;
	uint32_t address;
	uint32_t block_count;
	uint16_t op_result;
#define NVME_RPMB_RESULT_OK				0x00
#define NVME_RPMB_RESULT_GENERAL_FAILURE		0x01
#define NVME_RPMB_RESULT_AUTH_FAILURE			0x02
#define NVME_RPMB_RESULT_COUNTER_FAILURE		0x03
#define NVME_RPMB_RESULT_ADDRESS_FAILURE		0x04
#define NVME_RPMB_RESULT_WRITE_FAILURE			0x05
#define NVME_RPMB_RESULT_READ_FAILURE			0x06
#define NVME_RPMB_RESULT_AUTH_KEY_NOT_PROGRAMMED	0x07
#define NVME_RPMB_RESULT_INVALID_DEV_CONFIG_BLOCK	0x08
#define NVME_RPMB_RESULT_MASK				0x3F
#define NVME_RPMB_RESULT_WR_CNT_EXPIRED			0x80
	uint16_t msg_type;
#define NVME_RPMB_MSG_TYPE_REQ_AUTH_KEY_PROGRAM		0x0001
#define NVME_RPMB_MSG_TYPE_REQ_WRITE_COUNTER_READ	0x0002
#define NVME_RPMB_MSG_TYPE_REQ_AUTH_DATA_WRITE		0x0003
#define NVME_RPMB_MSG_TYPE_REQ_AUTH_DATA_READ		0x0004
#define NVME_RPMB_MSG_TYPE_REQ_RESULT_READ		0x0005
#define NVME_RPMB_MSG_TYPE_REQ_AUTH_DCB_WRITE		0x0006
#define NVME_RPMB_MSG_TYPE_REQ_AUTH_DCB_READ		0x0007
#define NVME_RPMB_MSG_TYPE_RESP_AUTH_KEY_PROGRAM	0x0100
#define NVME_RPMB_MSG_TYPE_RESP_WRITE_COUNTER_VAL_READ	0x0200
#define NVME_RPMB_MSG_TYPE_RESP_AUTH_DATA_WRITE		0x0300
#define NVME_RPMB_MSG_TYPE_RESP_AUTH_DATA_READ		0x0400
#define NVME_RPMB_MSG_TYPE_RESP_RESULT_READ		0x0500
#define NVME_RPMB_MSG_TYPE_RESP_AUTH_DCB_WRITE		0x0600
#define NVME_RPMB_MSG_TYPE_RESP_AUTH_DCB_READ		0x0700
	uint8_t data[0];
};

/*
 * ioctl() interface
 * Comes from: uapi/linux/nvme_ioctl.h
 */
struct nvme_passthru_cmd {
	__u8	opcode;
	__u8	flags;
	__u16	rsvd1;
	__u32	nsid;
	__u32	cdw2;
	__u32	cdw3;
	__u64	metadata;
	__u64	addr;
	__u32	metadata_len;
	__u32	data_len;
	__u32	cdw10;
	__u32	cdw11;
	__u32	cdw12;
	__u32	cdw13;
	__u32	cdw14;
	__u32	cdw15;
	__u32	timeout_ms;
	__u32	result;
};

#define nvme_admin_cmd nvme_passthru_cmd

#define NVME_IOCTL_ADMIN_CMD _IOWR('N', 0x41, struct nvme_admin_cmd)

static pthread_mutex_t nvme_rpmb_mutex = PTHREAD_MUTEX_INITIALIZER;

struct nvme_dev_info {
	unsigned int rpmbs;
	char sn[NVME_DEV_SN_LENGTH + 1];
};

static TEEC_Result nvme_identify_command(int fd, struct nvme_identify_controller *id)
{
	uint32_t cdw10 = (((NVME_CNTLID_NONE << NVME_IDENTIFY_CDW10_CNTID_SHIFT))
		& NVME_IDENTIFY_CDW10_CNTID_MASK)
		| (((NVME_IDENTIFY_CNS_CTRL << NVME_IDENTIFY_CDW10_CNS_SHIFT))
		& NVME_IDENTIFY_CDW10_CNS_MASK);
	uint32_t cdw11 = (((NVME_CNSSPECID_NONE << NVME_IDENTIFY_CDW11_CNSSPECID_SHIFT))
		& NVME_IDENTIFY_CDW11_CNSSPECID_MASK)
		| (((NVME_CSI_NVM << NVME_IDENTIFY_CDW11_CSI_SHIFT))
		& NVME_IDENTIFY_CDW11_CSI_MASK);
	uint32_t cdw14 = (NVME_UUID_NONE << NVME_IDENTIFY_CDW14_UUID_SHIFT)
		& NVME_IDENTIFY_CDW14_UUID_MASK;

	struct nvme_passthru_cmd cmd = {
		.opcode		= NVME_ADMIN_IDENTIFY,
		.nsid		= NVME_NSID_NONE,
		.addr		= (uint64_t)(uintptr_t)id,
		.data_len	= NVME_IDENTIFY_DATA_SIZE,
		.cdw10		= cdw10,
		.cdw11		= cdw11,
		.cdw14		= cdw14,
		.timeout_ms	= NVME_DEFAULT_IOCTL_TIMEOUT,
	};

	int err = ioctl(fd, NVME_IOCTL_ADMIN_CMD, cmd);
	if (err != 0) {
		EMSG("Failed to get get nvme rpmb support info\n");
		return TEEC_ERROR_GENERIC;
	}

	return TEEC_SUCCESS;
}

static TEEC_Result nvme_security_command(int fd, uint8_t opcode, uint32_t nsid,
		uint32_t cdw10, uint32_t cdw11, void *data, uint32_t data_len) {
	struct nvme_passthru_cmd cmd = {
		.opcode		= opcode,
		.nsid		= nsid,
		.cdw10		= cdw10,
		.cdw11		= cdw11,
		.data_len	= data_len,
		.addr		= (uint64_t)(uintptr_t)data,
		.timeout_ms	= NVME_DEFAULT_IOCTL_TIMEOUT,
	};

	int err = ioctl(fd, NVME_IOCTL_ADMIN_CMD, cmd);
	if (err != 0) {
		EMSG("Failed to send nvme security command with err = %d\n", err);
		return TEEC_ERROR_GENERIC;
	}

	return TEEC_SUCCESS;
}

static TEEC_Result nvme_security_send(int fd, uint32_t nsid,
		uint32_t cdw10, uint32_t cdw11, void *data, uint32_t data_len)
{
	return nvme_security_command(fd, NVME_ADMIN_SECURITY_SEND,
			nsid, cdw10, cdw11, data, data_len);
}

static TEEC_Result nvme_security_receive(int fd, uint32_t nsid,
		uint32_t cdw10, uint32_t cdw11, void *data, uint32_t data_len)
{
	return nvme_security_command(fd, NVME_ADMIN_SECURITY_RECV,
			nsid, cdw10, cdw11, data, data_len);
}

static TEEC_Result rpmb_read_request(int fd,
				struct nvme_rpmb_data_frame *req, size_t req_size,
				struct nvme_rpmb_data_frame *rsp, size_t rsp_size)
{
	TEEC_Result res = TEEC_ERROR_GENERIC;
	uint8_t nssf = req->target;
	uint8_t spsp0 = NVME_RPMB_SPSP0;
	uint8_t spsp1 = NVME_RPMB_SPSP1;
	uint8_t secp = NVME_RPMB_SECP;
	uint32_t cdw10;
	uint32_t cdw11;

	cdw10 =  (((nssf & NVME_SECURITY_NSSF_MASK) << NVME_SECURITY_NSSF_SHIFT)
		| ((spsp0 & NVME_SECURITY_SPSP0_MASK) << NVME_SECURITY_SPSP0_SHIFT)
		| ((spsp1 & NVME_SECURITY_SPSP1_MASK) << NVME_SECURITY_SPSP1_SHIFT)
			| ((secp & NVME_SECURITY_SECP_MASK) << NVME_SECURITY_SECP_SHIFT));
	cdw11 = req_size;

	res = nvme_security_send(fd, 0, cdw10, cdw11, (void *)req, (uint32_t)req_size);
	if (res) {
		EMSG("Send security cmd error with res = 0x%08x\n", res);
		goto error_out;
	}

	cdw11 = rsp_size;
	res = nvme_security_receive(fd, 0, cdw10, cdw11, (void *)rsp, (uint32_t)rsp_size);
	if (res) {
		EMSG("Receive security cmd error with res = 0x%08x\n", res);
		goto error_out;
	}

error_out:
	return res;
}

static TEEC_Result rpmb_write_request(int fd,
				struct nvme_rpmb_data_frame *req, size_t req_size,
				struct nvme_rpmb_data_frame *rsp, size_t rsp_size)
{
	TEEC_Result res = TEEC_ERROR_GENERIC;
	uint8_t nssf = req->target;
	uint8_t spsp0 = NVME_RPMB_SPSP0;
	uint8_t spsp1 = NVME_RPMB_SPSP1;
	uint8_t secp = NVME_RPMB_SECP;
	uint32_t cdw10;
	uint32_t cdw11;

	cdw10 =  (((nssf & NVME_SECURITY_NSSF_MASK) << NVME_SECURITY_NSSF_SHIFT)
		| ((spsp0 & NVME_SECURITY_SPSP0_MASK) << NVME_SECURITY_SPSP0_SHIFT)
		| ((spsp1 & NVME_SECURITY_SPSP1_MASK) << NVME_SECURITY_SPSP1_SHIFT)
		| ((secp & NVME_SECURITY_SECP_MASK) << NVME_SECURITY_SECP_SHIFT));
	cdw11 = req_size;

	res = nvme_security_send(fd, 0, cdw10, cdw11, (void *)req, (uint32_t)req_size);
	if (res) {
		EMSG("send security cmd error with res = 0x%08x\n", res);
		goto error_out;
	}

	memset(rsp, 0, sizeof(struct nvme_rpmb_data_frame));
	rsp->target = req->target;
	rsp->msg_type = NVME_RPMB_MSG_TYPE_REQ_RESULT_READ;
	cdw11 = rsp_size;
	res = nvme_security_send(fd, 0, cdw10, cdw11, (void *)rsp, (uint32_t)rsp_size);
	if (res) {
		EMSG("send security cmd error with res = 0x%08x\n", res);
		goto error_out;
	}

	memset(rsp, 0, rsp_size);
	res = nvme_security_receive(fd, 0, cdw10, cdw11, (void *)rsp, (uint32_t)rsp_size);
	if (res || rsp->op_result != 0) {
		EMSG("receive security cmd error with res = 0x%08x\n", res);
		goto error_out;
	}

error_out:
	return res;
}

/* Open and/or return file descriptor to RPMB partition of device dev_id */
static int open_nvme_rpmb_fd(uint16_t dev_id)
{
	static int id;
	static int fd = -1;
	char path[PATH_MAX] = { 0 };

	DMSG("dev_id = %u", dev_id);
	if (fd < 0) {
		snprintf(path, sizeof(path), "/dev/nvme%u", dev_id);
		fd = open(path, O_RDWR);
		if (fd < 0) {
			EMSG("Could not open %s (%s)", path, strerror(errno));
			return -1;
		}
		id = dev_id;
	}
	if (id != dev_id) {
		EMSG("Only one NVMe device is supported");
		return -1;
	}
	return fd;
}

/* Open NVMe device dev_id */
static int nvme_fd(uint16_t dev_id)
{
	int fd = 0;
	char path[PATH_MAX] = { 0 };

	IMSG("NVMe dev_id = %u", dev_id);
	snprintf(path, sizeof(path), "/dev/nvme%u", dev_id);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		EMSG("Could not open %s\n", path);

	return fd;
}

static void close_nvme_fd(int fd)
{
	close(fd);
}

static TEEC_Result nvme_rpmb_get_rpmbs_info(uint16_t dev_id, struct nvme_dev_info *info)
{
	int fd = 0;
	TEEC_Result res = TEEC_ERROR_GENERIC;
	struct nvme_identify_controller id;

	memset(info, 0, sizeof(struct nvme_dev_info));

	fd = nvme_fd(dev_id);
	if (fd < 0)
		return TEEC_ERROR_BAD_PARAMETERS;

	res = nvme_identify_command(fd, &id);
	if (res != TEEC_SUCCESS)
		goto out;

	memcpy(info->sn, id.sn, NVME_DEV_SN_LENGTH);
	info->rpmbs = id.rpmbs;

out:
	close_nvme_fd(fd);
	return res;
}

static TEEC_Result nvme_rpmb_data_req(int fd,
				struct nvme_rpmb_data_frame *req, size_t req_size,
				struct nvme_rpmb_data_frame *rsp, size_t rsp_size)
{
	TEEC_Result res;

	switch (req->msg_type) {
	case NVME_RPMB_MSG_TYPE_REQ_AUTH_KEY_PROGRAM:
	case NVME_RPMB_MSG_TYPE_REQ_AUTH_DATA_WRITE:
	case NVME_RPMB_MSG_TYPE_REQ_AUTH_DCB_WRITE:
		res = rpmb_write_request(fd, req, req_size, rsp, rsp_size);
		break;
	case NVME_RPMB_MSG_TYPE_REQ_WRITE_COUNTER_READ:
	case NVME_RPMB_MSG_TYPE_REQ_AUTH_DATA_READ:
	case NVME_RPMB_MSG_TYPE_REQ_AUTH_DCB_READ:
		res = rpmb_read_request(fd, req, req_size, rsp, rsp_size);
		break;
	default:
		EMSG("command 0x%08x not support\n", req->msg_type);
		res = TEEC_ERROR_BAD_PARAMETERS;
		break;
	}

	return res;
}

/*
 * req is one struct rpmb_req followed by one struct nvme_rpmb_data_frame
 * rsp is either one struct nvme_dev_info or one nvme_rpmb_data_frame
 */
static TEEC_Result nvme_rpmb_process_request_unlocked(
						void *req, size_t req_size,
						void *rsp, size_t rsp_size)
{
	uint32_t res = TEEC_ERROR_GENERIC;
	struct nvme_rpmb_req *sreq = req;
	int fd = 0;

	if (req_size < sizeof(struct nvme_rpmb_req))
		return TEEC_ERROR_BAD_PARAMETERS;

	switch (sreq->cmd) {
	case NVME_RPMB_CMD_DATA_REQ:
		/*
		 * To ensure only one device can be used, we will not close the device
		 * after it is opened.
		 */
		fd = open_nvme_rpmb_fd(sreq->dev_id);
		if (fd < 0)
			return TEEC_ERROR_BAD_PARAMETERS;
		res = nvme_rpmb_data_req(fd, NVME_RPMB_REQ_DATA(req),
				req_size - sizeof(struct nvme_rpmb_req), rsp, rsp_size);
		break;

	case NVME_RPMB_CMD_GET_RPMBS_INFO:
		if (req_size != sizeof(struct nvme_rpmb_req) ||
		    rsp_size != sizeof(struct nvme_dev_info)) {
			EMSG("Invalid req/rsp size");
			return TEEC_ERROR_BAD_PARAMETERS;
		}
		res = nvme_rpmb_get_rpmbs_info(sreq->dev_id, (struct nvme_dev_info *)rsp);
		break;

	default:
		EMSG("Unsupported RPMB command: %d", sreq->cmd);
		res = TEEC_ERROR_BAD_PARAMETERS;
		break;
	}

	return res;
}


TEEC_Result nvme_rpmb_process_request(void *req, size_t req_size,
						void *rsp, size_t rsp_size)
{
	uint32_t res = 0;

	tee_supp_mutex_lock(&nvme_rpmb_mutex);
	res = nvme_rpmb_process_request_unlocked(req, req_size, rsp, rsp_size);
	tee_supp_mutex_unlock(&nvme_rpmb_mutex);

	return res;
}
