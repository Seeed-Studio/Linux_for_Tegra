// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2023-2024, NVIDIA CORPORATION & AFFILIATES.
 */

#include <config.h>
#include <crypto/crypto.h>
#include <drivers/tegra/tegra_fuse.h>
#include <initcall.h>
#include <jetson_user_key_pta_ftpm.h>
#include <kernel/boot.h>
#include <kernel/pseudo_ta.h>
#include <kernel/tee_ta_manager.h>
#include <kernel/user_access.h>
#include <kernel/user_mode_ctx.h>
#include <tee/tee_cryp_hkdf.h>
#include <tee/tee_ree_state.h>
#include <tee/tee_svc.h>
#include <libfdt.h>
#include <pta_jetson_ftpm_helper.h>
#include <user_ta_header.h>
#include <util.h>

#define EC_P256_KEY_BIT_SIZE 256UL
#define EC_P256_KEY_BYTE_SIZE (EC_P256_KEY_BIT_SIZE / 8)

#define FTPM_EK_CERT_TYPE_RSA	0x01
#define FTPM_EK_CERT_TYPE_EC	0x02
/* The seed size = max secure strength (256 bit) * 2 */
#define FTPM_EPS_ROOT_SEED_SIZE 64UL
#define FTPM_EPS_SIZE 64UL
#define FTPM_MB2_EVT_LOG_SIG_SIZE 64UL
#define FTPM_MB2_EVT_LOG_INDEX	80UL
#define FTPM_MB2_FTPM_SEED_SIZE 32UL
#define FTPM_HELPER_PTA_NAME "jetson_ftpm_helper.pta"

/*
 * fTPM TA UUID: BC50D971-D4C9-42C4-82CB-343FB7F37896
 */
#define FTPM_TA_UUID \
		{ 0xBC50D971, 0xD4C9, 0x42C4, \
			{0x82, 0xCB, 0x34, 0x3F, 0xB7, 0xF3, 0x78, 0x96} }

typedef struct {
	struct ecc_keypair silicon_id_key;
	uint8_t *silicon_id_pub_key;
	uint32_t len_silicon_id_pub_key;
	uint8_t *mb2_evt_log_sig;
	uint32_t len_mb2_evt_log_sig;
	uint8_t *tos_evt_log_sig;
	uint32_t len_tos_evt_log_sig;
	uint8_t *ftpm_seed;
	uint32_t len_ftpm_seed;
	uint8_t *ftpm_sn;
	uint32_t len_ftpm_sn;
	uint8_t *ftpm_eps_seed;
	uint32_t len_ftpm_eps_seed;
	uint8_t *rsa_ek_cert;
	uint32_t len_rsa_ek_cert;
	uint8_t *ec_ek_cert;
	uint32_t len_ec_ek_cert;
	bool is_ready;
} ftpm_property_t;
static ftpm_property_t ftpm_property;

/* Sessions opened by fTPM helper pta */
static struct tee_ta_session_head tee_ftpm_sessions =
TAILQ_HEAD_INITIALIZER(tee_ftpm_sessions);

#if defined(CFG_JETSON_FTPM_HELPER_INJECT_EPS)
static uint8_t external_eps[FTPM_EPS_SIZE];
#endif

static TEE_Result ping_ns_world(uint32_t ptypes,
				TEE_Param params[TEE_NUM_PARAMS])
{
	tee_ree_state state;
	uint32_t exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_VALUE_OUTPUT,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE,
					  TEE_PARAM_TYPE_NONE);

	if (exp_pt != ptypes)
		return TEE_ERROR_BAD_PARAMETERS;

	state = tee_get_ree_state();
	if (state == TEE_REE_STATE_REE_SUPP)
		params[0].value.a = FTPM_HELPER_PTA_NS_STATE_READY;
	else
		params[0].value.a = FTPM_HELPER_PTA_NS_STATE_NOT_READY;

	return TEE_SUCCESS;
}

static TEE_Result query_sn(uint32_t ptypes,
			   TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t exp_pt;
	uint8_t *sn_buf = NULL;
	uint32_t sn_len;

	/* Validate the input parameters. */
	exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
				 TEE_PARAM_TYPE_NONE,
				 TEE_PARAM_TYPE_NONE,
				 TEE_PARAM_TYPE_NONE);
	if (exp_pt != ptypes)
		return TEE_ERROR_BAD_PARAMETERS;

	if (FTPM_HELPER_PTA_SN_LENGTH != params[0].memref.size)
		return TEE_ERROR_BAD_PARAMETERS;

	rc = tegra_fuse_get_sn(&sn_buf, &sn_len);
	if ((TEE_SUCCESS == rc) && (sn_len != params[0].memref.size))
		return TEE_ERROR_SHORT_BUFFER;

	memcpy(params[0].memref.buffer, sn_buf, params[0].memref.size);

	return rc;
}

static TEE_Result query_ecid(uint32_t ptypes,
			     TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t exp_pt;

	/* Validate the input parameters. */
	exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
				 TEE_PARAM_TYPE_NONE,
				 TEE_PARAM_TYPE_NONE,
				 TEE_PARAM_TYPE_NONE);
	if (exp_pt != ptypes)
		return TEE_ERROR_BAD_PARAMETERS;

	if (FTPM_HELPER_PTA_ECID_LENGTH != params[0].memref.size)
		return TEE_ERROR_BAD_PARAMETERS;

	rc = tegra_fuse_get_64bit_ecid((uint64_t*)params[0].memref.buffer);

	return rc;
}

static TEE_Result query_evt_log_sig(uint8_t *sig_buf,
				    uint32_t ptypes,
				    TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t exp_pt;

	if (!ftpm_property.is_ready)
		return TEE_ERROR_NOT_SUPPORTED;

	/* Validate the input parameters. */
	if (!sig_buf)
		return TEE_ERROR_BAD_PARAMETERS;

	exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
				 TEE_PARAM_TYPE_NONE,
				 TEE_PARAM_TYPE_NONE,
				 TEE_PARAM_TYPE_NONE);
	if (exp_pt != ptypes)
		return TEE_ERROR_BAD_PARAMETERS;

	if (params[0].memref.size < FTPM_MB2_EVT_LOG_SIG_SIZE)
		return TEE_ERROR_BAD_PARAMETERS;

	if (sig_buf == NULL)
		return TEE_ERROR_NO_DATA;

	memcpy(params[0].memref.buffer, sig_buf,
	       (FTPM_MB2_EVT_LOG_SIG_SIZE / 2));
	memcpy(((uint8_t *)params[0].memref.buffer +
		(FTPM_MB2_EVT_LOG_SIG_SIZE / 2)),
	       (sig_buf + FTPM_MB2_EVT_LOG_INDEX),
	       (FTPM_MB2_EVT_LOG_SIG_SIZE / 2));

	params[0].memref.size = FTPM_MB2_EVT_LOG_SIG_SIZE;

	return rc;
}

static TEE_Result query_ek_cert(uint32_t cert_type,
				uint32_t ptypes,
				TEE_Param params[TEE_NUM_PARAMS])
{
	TEE_Result rc = TEE_SUCCESS;
	uint32_t exp_pt;
	uint8_t *cert_buf_addr = NULL;
	uint32_t cert_len = 0;

	if (!ftpm_property.is_ready)
		return TEE_ERROR_NOT_SUPPORTED;

	exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_OUTPUT,
				 TEE_PARAM_TYPE_NONE,
				 TEE_PARAM_TYPE_NONE,
				 TEE_PARAM_TYPE_NONE);
	if (exp_pt != ptypes)
		return TEE_ERROR_BAD_PARAMETERS;

	if (params[0].memref.size < FTPM_HELPER_PTA_EK_CERT_BUF_SIZE)
		return TEE_ERROR_BAD_PARAMETERS;

	switch (cert_type) {
	case FTPM_EK_CERT_TYPE_RSA:
		cert_buf_addr = ftpm_property.rsa_ek_cert;
		cert_len = ftpm_property.len_rsa_ek_cert;
		break;
	case FTPM_EK_CERT_TYPE_EC:
		cert_buf_addr = ftpm_property.ec_ek_cert;
		cert_len = ftpm_property.len_ec_ek_cert;
		break;
	default:
		return TEE_ERROR_BAD_PARAMETERS;
	};

	if ((cert_buf_addr == NULL) || (cert_len == 0))
		return TEE_ERROR_NO_DATA;

	if (cert_len > FTPM_HELPER_PTA_EK_CERT_BUF_SIZE)
		return TEE_ERROR_SHORT_BUFFER;

	memcpy(params[0].memref.buffer, cert_buf_addr, cert_len);
	params[0].memref.size = cert_len;

	return rc;
}

#if defined(CFG_JETSON_FTPM_HELPER_INJECT_EPS)
static TEE_Result inject_eps(uint32_t ptypes, TEE_Param params[TEE_NUM_PARAMS])
{
	uint32_t exp_pt;

	/* Validate the input parameters. */
	exp_pt = TEE_PARAM_TYPES(TEE_PARAM_TYPE_MEMREF_INPUT,
				 TEE_PARAM_TYPE_NONE,
				 TEE_PARAM_TYPE_NONE,
				 TEE_PARAM_TYPE_NONE);
	if (exp_pt != ptypes)
		return TEE_ERROR_BAD_PARAMETERS;

	if (params[0].memref.buffer == NULL || params[0].memref.size != FTPM_EPS_SIZE)
		return TEE_ERROR_BAD_PARAMETERS;

	memcpy(external_eps, params[0].memref.buffer, FTPM_EPS_SIZE);
	return TEE_SUCCESS;
}
#endif

static TEE_Result invoke_command(void *psess __unused,
				 uint32_t cmd, uint32_t ptypes,
				 TEE_Param params[TEE_NUM_PARAMS])
{
	switch (cmd) {
	case FTPM_HELPER_PTA_CMD_PING_NS:
		return ping_ns_world(ptypes, params);
	case FTPM_HELPER_PTA_CMD_QUERY_SN:
		return query_sn(ptypes, params);
	case FTPM_HELPER_PTA_CMD_QUERY_ECID:
		return query_ecid(ptypes, params);
	case FTPM_HELPER_PTA_CMD_GET_EVT_LOG_SIG_MB2:
		return query_evt_log_sig(ftpm_property.mb2_evt_log_sig,
					 ptypes,
					 params);
	case FTPM_HELPER_PTA_CMD_GET_EVT_LOG_SIG_TOS:
		return query_evt_log_sig(ftpm_property.tos_evt_log_sig,
					 ptypes,
					 params);
	case FTPM_HELPER_PTA_CMD_GET_RSA_EK_CERT:
		return query_ek_cert(FTPM_EK_CERT_TYPE_RSA, ptypes, params);
	case FTPM_HELPER_PTA_CMD_GET_EC_EK_CERT:
		return query_ek_cert(FTPM_EK_CERT_TYPE_EC, ptypes, params);
#if defined(CFG_JETSON_FTPM_HELPER_INJECT_EPS)
	case FTPM_HELPER_PTA_CMD_INJECT_EPS:
		return inject_eps(ptypes, params);
#endif
	default:
		break;
	}

	return TEE_ERROR_NOT_IMPLEMENTED;
}

static TEE_Result open_session(uint32_t param_types __unused,
			       TEE_Param params[TEE_NUM_PARAMS] __unused,
			       void **sess_ctx __unused)
{
	struct ts_session *s = NULL;

	/* Check that we're called from a user TA */
	s = ts_get_calling_session();
	if (!s || !is_user_ta_ctx(s->ctx))
		return TEE_ERROR_ACCESS_DENIED;

	return TEE_SUCCESS;
}

pseudo_ta_register(.uuid = FTPM_HELPER_PTA_UUID,
		   .name = FTPM_HELPER_PTA_NAME,
		   .flags = PTA_DEFAULT_FLAGS,
		   .open_session_entry_point = open_session,
		   .invoke_command_entry_point = invoke_command);

static TEE_Result get_prop_endorsement_seed(struct ts_session *sess __maybe_unused,
					    void *buf, size_t *blen)
{
	TEE_Result rc = TEE_SUCCESS;
	uint8_t eps[FTPM_EPS_SIZE] = { 0 };
	uint32_t eps_len = sizeof(eps);
	uint8_t root_seed[FTPM_EPS_ROOT_SEED_SIZE] = { 0 };
	uint8_t *sn = NULL;
	uint32_t len_sn;
	static const char root_seed_info[] = {'R', 'o', 'o', 't', '_',
					      'S', 'e', 'e', 'd'};
	static const uint8_t root_seed_salt[] = { 0x00 };
	const TEE_UUID ftpm_ta_uuid = FTPM_TA_UUID;
	bool is_allowed_ta = false;

#if defined(CFG_JETSON_FTPM_HELPER_INJECT_EPS)
	memcpy(eps, external_eps, eps_len);
	goto out;
#endif

	/* Check that we're called by the fTPM TA */
	if (memcmp(&sess->ctx->uuid, &ftpm_ta_uuid, sizeof(TEE_UUID)) == 0)
		is_allowed_ta = true;

	/*
	 * For xtest regression 1006 test.
	 *
	 * Caution:
	 *   When fTPM is enabled, please make sure the TA_SIGN_KEY has been
	 *   replaced by your private key to avoid the malicious REE FS TA to
	 *   be loaded and executed by the OP-TEE which could cause the EPS
	 *   leakage. By default, the query is only allowed by the fTPM TA.
	 *   Otherwise always returns 16 byte zero values.
	 */
	if (!is_allowed_ta) {
		uint32_t zlen = 16;

		if (*blen > zlen)
			memset(buf, 0, zlen);
		else
			memset(buf, 0, *blen);
		*blen = zlen;

		return TEE_SUCCESS;
	}

	if (!ftpm_property.is_ready)
		return TEE_ERROR_ITEM_NOT_FOUND;

	if (!ftpm_property.ftpm_seed || !ftpm_property.ftpm_sn)
		return TEE_ERROR_BAD_STATE;

	/* Derive fTPM Root Seed. */
	rc = tee_cryp_hkdf(TEE_MAIN_ALGO_SHA256,
			   ftpm_property.ftpm_seed,
			   ftpm_property.len_ftpm_seed,
			   root_seed_salt, sizeof(root_seed_salt),
			   (uint8_t *)root_seed_info, sizeof(root_seed_info),
			   root_seed, sizeof(root_seed));
	if (rc != TEE_SUCCESS)
		goto out;

	rc = tegra_fuse_get_sn(&sn, &len_sn);
	if (rc != TEE_SUCCESS)
		goto out;

	if (memcmp(sn, ftpm_property.ftpm_sn, len_sn) != 0) {
		rc = TEE_ERROR_ACCESS_DENIED;
		goto out;
	}

	/* Derive EPS. */
	rc = tee_cryp_hkdf(TEE_MAIN_ALGO_SHA256,
			   root_seed, sizeof(root_seed),
			   ftpm_property.ftpm_eps_seed,
			   ftpm_property.len_ftpm_eps_seed,
			   sn, len_sn,
			   eps, sizeof(eps));

out:
	if (rc != TEE_SUCCESS)
		return rc;

	if (*blen < sizeof(eps)) {
		eps_len = *blen;
	} else {
		*blen = sizeof(eps);
	}

	return copy_to_user(buf, eps, eps_len);
}

static const struct tee_props vendor_propset_array_tee[] = {
	{
		.name = "com.microsoft.ta.endorsementSeed",
		.prop_type = USER_TA_PROP_TYPE_BINARY_BLOCK,
		.get_prop_func = get_prop_endorsement_seed,
	},
};

const struct tee_vendor_props vendor_props_tee = {
	.props = vendor_propset_array_tee,
	.len = ARRAY_SIZE(vendor_propset_array_tee),
};

static const char *const ftpm_helper_dt_match_table[] = {
	"nvidia,ftpm-contents",
};

static TEE_Result dt_get_sub_node_data(void *fdt,
				       const char *comp_str,
				       void **ret_addr,
				       uint32_t *ret_len)
{
	TEE_Result rc = TEE_SUCCESS;
	int node = -1;
	paddr_t pbase;
	ssize_t sz;
	uint8_t *reg_addr = NULL;
	uint8_t *tmp_addr = NULL;

	node = fdt_node_offset_by_compatible(fdt, 0, comp_str);
	if (node < 0) {
		rc = TEE_ERROR_ITEM_NOT_FOUND;
		goto fail;
	}

	if (fdt_get_status(fdt, node) != DT_STATUS_OK_SEC) {
		IMSG("The DICE DT sub node \"%s\" is not enabled.\n", comp_str);
		rc = TEE_ERROR_GENERIC;
		goto fail;
	}

	pbase = fdt_reg_base_address(fdt, node);
	if (pbase == DT_INFO_INVALID_REG) {
		rc = TEE_ERROR_GENERIC;
		goto fail;
	}

	sz = fdt_reg_size(fdt, node);
	if (sz < 0) {
		rc = TEE_ERROR_GENERIC;
		goto fail;
	}

	reg_addr = (uint8_t *)core_mmu_add_mapping(MEM_AREA_RAM_SEC, pbase, sz);
	if (!reg_addr) {
		EMSG("Failed to map (%s) %zu bytes at PA 0x%"PRIxPA,
		     comp_str, (size_t)sz, pbase);
		rc = TEE_ERROR_GENERIC;
		goto fail;
	}

	tmp_addr = calloc(1, sz);
	if (!tmp_addr) {
		rc = TEE_ERROR_OUT_OF_MEMORY;
		goto fail;
	}
	memcpy(tmp_addr, reg_addr, sz);
	*ret_addr = (void *)tmp_addr;
	*ret_len = sz;

	/* Clear the source. */
	memset(reg_addr, 0, sz);

fail:
	if (reg_addr)
		core_mmu_remove_mapping(MEM_AREA_RAM_SEC, reg_addr, sz);

	return rc;
}

static TEE_Result ftpm_helper_dt_init(void)
{
	TEE_Result rc = TEE_SUCCESS;
	void *fdt = NULL;
	int node = -1;
	uint32_t i = 0;
	static const char ftpm_seed_str[] = "nvidia,ftpm-seed";
	static const char silicon_id_pub_key_str[] = "nvidia,ftpm-silicon-id-pubkey";
	static const char mb2_evt_log_sig_str[] = "nvidia,ftpm-mb2-event-log-sig";
	static const char tos_evt_log_sig_str[] = "nvidia,ftpm-tos-event-log-sig";

	fdt = get_dt();
	if (!fdt) {
		EMSG("%s: DTB is not present.", __func__);
		rc = TEE_ERROR_ITEM_NOT_FOUND;
		goto fail;
	}

	for (i = 0; i < ARRAY_SIZE(ftpm_helper_dt_match_table); i++) {
		node = fdt_node_offset_by_compatible(fdt, 0,
						     ftpm_helper_dt_match_table[i]);
		if (node >= 0)
			break;
	}

	if (node < 0) {
		EMSG("%s: DT not found (%x).", __func__, node);
		rc = TEE_ERROR_ITEM_NOT_FOUND;
		goto fail;
	}

	if (fdt_get_status(fdt, node) != DT_STATUS_OK_SEC) {
		IMSG("fTPM ID is not enabled.\n");
		rc = TEE_ERROR_GENERIC;
		goto fail;
	}

	/* fTPM Seed */
	rc = dt_get_sub_node_data(fdt, ftpm_seed_str,
				  (void *)&ftpm_property.ftpm_seed,
				  &ftpm_property.len_ftpm_seed);
	if (rc)
		goto fail;

	if (ftpm_property.len_ftpm_seed != FTPM_MB2_FTPM_SEED_SIZE) {
		rc = TEE_ERROR_BAD_FORMAT;
		goto fail;
	}

	/* Silicon ID pub key */
	rc = dt_get_sub_node_data(fdt, silicon_id_pub_key_str,
				  (void *)&ftpm_property.silicon_id_pub_key,
				  &ftpm_property.len_silicon_id_pub_key);
	if (rc)
		goto fail;

	if (ftpm_property.len_silicon_id_pub_key != (EC_P256_KEY_BYTE_SIZE * 2)) {
		rc = TEE_ERROR_BAD_FORMAT;
		goto fail;
	}

	crypto_bignum_bin2bn(ftpm_property.silicon_id_pub_key,
			     EC_P256_KEY_BYTE_SIZE,
			     ftpm_property.silicon_id_key.x);
	crypto_bignum_bin2bn((ftpm_property.silicon_id_pub_key +
			      EC_P256_KEY_BYTE_SIZE),
			     EC_P256_KEY_BYTE_SIZE,
			     ftpm_property.silicon_id_key.y);

	/* MB2 event log signature */
	rc = dt_get_sub_node_data(fdt, mb2_evt_log_sig_str,
				  (void *)&ftpm_property.mb2_evt_log_sig,
				  &ftpm_property.len_mb2_evt_log_sig);
	if (rc)
		goto fail;

	/* TOS event log signature */
	rc = dt_get_sub_node_data(fdt, tos_evt_log_sig_str,
				  (void *)&ftpm_property.tos_evt_log_sig,
				  &ftpm_property.len_tos_evt_log_sig);

fail:
	return rc;
}

static TEE_Result ftpm_helper_ekb_init(void)
{
	TEE_Result rc = TEE_SUCCESS;

	rc = jetson_user_key_pta_query_ftpm_prop(EKB_FTPM_SN,
						 &ftpm_property.ftpm_sn,
						 &ftpm_property.len_ftpm_sn);
	if (rc)
		goto fail;

	rc = jetson_user_key_pta_query_ftpm_prop(EKB_FTPM_EPS_SEED,
						 &ftpm_property.ftpm_eps_seed,
						 &ftpm_property.len_ftpm_eps_seed);
	if (rc)
		goto fail;

	rc = jetson_user_key_pta_query_ftpm_prop(EKB_FTPM_RSA_EK_CERT,
						 &ftpm_property.rsa_ek_cert,
						 &ftpm_property.len_rsa_ek_cert);
	if (rc)
		goto fail;

	rc = jetson_user_key_pta_query_ftpm_prop(EKB_FTPM_EC_EK_CERT,
						 &ftpm_property.ec_ek_cert,
						 &ftpm_property.len_ec_ek_cert);

fail:
	return rc;
}

static TEE_Result init_ftpm_ta(void)
{
	TEE_Result rc = TEE_SUCCESS;
	static const TEE_UUID ftpm_ta_uuid = FTPM_TA_UUID;
	TEE_UUID pta_uuid = FTPM_HELPER_PTA_UUID;
	TEE_ErrorOrigin err_orig = TEE_ORIGIN_TEE;
	struct tee_ta_session *s = NULL;
	struct tee_ta_param param;
	TEE_Identity clnt_id;

	clnt_id.login = TEE_LOGIN_TRUSTED_APP;
	memcpy(&clnt_id.uuid, &pta_uuid, sizeof(TEE_UUID));
	param.types = TEE_PARAM_TYPES(TEE_PARAM_TYPE_NONE,
				      TEE_PARAM_TYPE_NONE,
				      TEE_PARAM_TYPE_NONE,
				      TEE_PARAM_TYPE_NONE);

	rc = tee_ta_open_session(&err_orig, &s, &tee_ftpm_sessions,
				 &ftpm_ta_uuid, &clnt_id,
				 TEE_TIMEOUT_INFINITE, &param);
	if (rc != TEE_SUCCESS) {
		DMSG("Fail to open session with fTPM TA");
		return rc;
	}

	tee_ta_close_session(s, &tee_ftpm_sessions, &clnt_id);

	return rc;
}

static TEE_Result jetson_ftpm_helper_pta_init(void)
{
	TEE_Result rc = TEE_SUCCESS;

	/* Launch fTPM TA */
	rc = init_ftpm_ta();

	/* Initialize fTPM properties */
	memset(&ftpm_property, 0, sizeof(ftpm_property_t));
#if defined(CFG_JETSON_FTPM_HELPER_INJECT_EPS)
	memset(external_eps, 0, sizeof(external_eps));
#endif

	/* Alloc the silicon ID key */
	rc = crypto_acipher_alloc_ecc_keypair(&ftpm_property.silicon_id_key,
					      TEE_TYPE_ECDSA_KEYPAIR,
					      EC_P256_KEY_BIT_SIZE);
	if (rc != TEE_SUCCESS)
		goto ftpm_prop_fail;

	ftpm_property.silicon_id_key.curve = TEE_ECC_CURVE_NIST_P256;

	rc = ftpm_helper_dt_init();
	if (rc != TEE_SUCCESS)
		goto ftpm_prop_fail;

	rc = ftpm_helper_ekb_init();

ftpm_prop_fail:
	if (rc != TEE_SUCCESS) {
		IMSG("ftpm-helper PTA: fTPM DT or EKB is not available. fTPM provisioning is not supported.");
		ftpm_property.is_ready = false;
	} else {
		ftpm_property.is_ready = true;
	}

	return TEE_SUCCESS;
}

driver_init_late(jetson_ftpm_helper_pta_init);
