// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_executable.h"
#include "pva_kmd_silicon_elf_parser.h"
#include "pva_kmd_utils.h"
#include "pva_resource.h"
#include "pva_kmd_device.h"
#include "pva_api_types.h"
#include "pva_kmd_t23x.h"
#include "pva_kmd_t26x.h"
#include "pva_math_utils.h"

/**
 *  enum to identify different segments of VPU ELF
 */
enum pva_elf_seg_type {
	/** Code segment in VPU ELF */
	PVA_SEG_VPU_CODE = 0U,
	/** DATA segment in VPU ELF */
	PVA_SEG_VPU_DATA,
	/** DATA segment in VPU ELF containing symbol information*/
	PVA_SEG_VPU_IN_PARAMS,
	/** Not a valid segment in VPU ELF */
	PVA_SEG_VPU_MAX_TYPE
};

/** Maximum number of characters in symbol name */
#define ELF_MAXIMUM_SYMBOL_LENGTH 64U

/** Maximum number of characters in section name */
#define ELF_MAXIMUM_SECTION_NAME 64

/** Section name of EXPORTS section */
#define ELF_EXPORTS_SECTION "EXPORTS"

/** Section name of EXPORTS section name length */
#define ELF_EXPORTS_SECTION_NAME_LENGTH 7

/** Alignment needed for Data section of ELFs */
#define DATA_SECTION_ALIGNMENT 32U

/** Alignment needed for Text section of ELFs */
#define TEXT_SECTION_ALIGNMENT 128U

/** VPU icache size: 16KB */
#define VPU_ICACHE_SIZE (16U * 1024U)

/** This value indicates the that current symbol can be ignored in the VPU ELF */
#define SYM_IGNORE 1

#define SIZE_EXPORTS_TABLE_ENTRY (3U * sizeof(uint32_t))

static uint32_t change_byte_order(uint32_t word)
{
	uint32_t out_word = 0U;
	out_word = PVA_INSERT(PVA_EXTRACT(word, 31, 24, uint32_t), 7, 0);
	out_word |= PVA_INSERT(PVA_EXTRACT(word, 23, 16, uint32_t), 15, 8);
	out_word |= PVA_INSERT(PVA_EXTRACT(word, 15, 8, uint32_t), 23, 16);
	out_word |= PVA_INSERT(PVA_EXTRACT(word, 7, 0, uint32_t), 31, 24);
	return out_word;
}

/*
 * Define mapping from VPU data, rodata and program sections into
 * corresponding segment types.
 */
static const struct pack_rule {
	const char *elf_sec_name;
	int32_t pva_type;
} pack_rules[] = { {
			   .elf_sec_name = ".data",
			   .pva_type = (int32_t)PVA_SEG_VPU_DATA,
		   },
		   {
			   .elf_sec_name = ".rodata",
			   .pva_type = (int32_t)PVA_SEG_VPU_DATA,
		   },
		   {
			   .elf_sec_name = ".text",
			   .pva_type = (int32_t)PVA_SEG_VPU_CODE,
		   } };

/**
* \brief Compares the \a section_name with all
* vpu elf section names until it finds a match and
* then return corresponding segment type.
* If the segment type is \ref PVA_SEG_VPU_DATA, then it further
* checks if its PVA_SEG_VPU_IN_PARAMS.
* \param[in] section_name Name of the section to be searched for, in VPU ELF
* \return returns corresponding value from enum pva_elf_seg_type.
*/
static int32_t find_pva_ucode_segment_type(const char *section_name)
{
	uint32_t i;
	int32_t ret = (int32_t)PVA_SEG_VPU_MAX_TYPE;

	for (i = 0; i < PVA_ARRAY_SIZE(pack_rules); i += 1U) {
		/* Ignore the suffix of the section name */
		if (strncmp(section_name, pack_rules[i].elf_sec_name,
			    strlen(pack_rules[i].elf_sec_name)) == 0) {
			ret = pack_rules[i].pva_type;
			break;
		}
	}
	if (ret == (int32_t)PVA_SEG_VPU_DATA) {
		uint64_t section_name_len =
			strnlen(section_name, ELF_MAXIMUM_SECTION_NAME);
		uint64_t exports_section_name_len =
			ELF_EXPORTS_SECTION_NAME_LENGTH;
		// Check Export section present in DATA segment. Only support export sections.
		if ((section_name_len >= exports_section_name_len) &&
		    (strncmp((section_name +
			      (section_name_len - exports_section_name_len)),
			     ELF_EXPORTS_SECTION,
			     (size_t)exports_section_name_len)) == 0) {
			ret = (int32_t)PVA_SEG_VPU_IN_PARAMS;
		}
	}

	return ret;
}

static enum pva_error validate_elf(const elf_parser_ctx elf)
{
	enum pva_error err = PVA_SUCCESS;

	if (!elf_header_check(elf.elf_file)) {
		pva_kmd_log_err("Invalid 32 bit VPU ELF");
		err = PVA_INVAL;
		goto done;
	}

	if (!elf_has_valid_sections(elf)) {
		pva_kmd_log_err("ELF has invalid sections");
		err = PVA_INVAL;
	}
done:
	return err;
}

static int32_t validate_symbol(elf_parser_ctx elf, uint32_t symbol_entry_id,
			       const elfSymbol **sym)
{
	const elfSectionHeader *sym_scn;
	const char *section_name = NULL;
	int32_t section_type = (int32_t)PVA_SEG_VPU_MAX_TYPE;
	int32_t err = 0;

	*sym = elf_symbol(elf, symbol_entry_id);
	if ((*sym == NULL) || ((*sym)->size == 0U) ||
	    (ELF_ST_BIND(*sym) != STB_GLOBAL) ||
	    (ELF_ST_TYPE(*sym) == STT_FUNC)) {
		err = SYM_IGNORE;
		goto end;
	}

	sym_scn = elf_section_header(elf, (*sym)->shndx);
	section_name = elf_section_name(elf, sym_scn);
	if (section_name == NULL) {
		err = SYM_IGNORE;
		goto end;
	}
	section_type = find_pva_ucode_segment_type(section_name);
	if (section_type != (int32_t)PVA_SEG_VPU_IN_PARAMS) {
		err = SYM_IGNORE;
		goto end;
	}
	err = 0;
end:
	if (err != 0) {
		*sym = NULL;
	}
	return err;
}

static enum pva_error count_symbols(const elf_parser_ctx elf,
				    uint32_t *out_num_symbols)
{
	enum pva_error err = PVA_SUCCESS;
	pva_math_error math_err = MATH_OP_SUCCESS;
	const elfSectionHeader *section_header;
	uint32_t i, ent_count;
	const elfSymbol *sym;
	int32_t ret;
	uint32_t num_symbols = 0;

	section_header = elf_named_section_header(elf, ".symtab");

	if (section_header == NULL) {
		err = PVA_INVAL;
		pva_kmd_log_err("No symbol table found");
		goto done;
	}

	ent_count = section_header->size / section_header->entsize;
	for (i = 0; i < ent_count; i++) {
		ret = validate_symbol(elf, i, &sym);
		if (ret < 0) {
			err = PVA_INVAL;
			pva_kmd_log_err("Validation of symbol failed");
			goto done;
		}
		if (ret == SYM_IGNORE) {
			continue;
		}
		num_symbols = addu32(num_symbols, 1U, &math_err);
	}
	if (math_err != MATH_OP_SUCCESS) {
		err = PVA_ERR_MATH_OP;
		pva_kmd_log_err("count_symbols math error");
		goto done;
	}

	*out_num_symbols = num_symbols;
done:
	return err;
}

/**
 * @brief updates symbol information (type, addr and size) from
 * VPU ELF PVA_SEG_VPU_IN_PARAMS segment.
 *
 * Data about symbol information in EXPORTS section of ELF is present as follows.
 * typedef struct {
 *   uint32_t type; From VMEM_TYPE enums
 *   uint32_t addr_offset; Offset from VMEM base
 *   uint32_t size; Size of VMEM region in bytes
 * };
 * @param[in] elf pointer to const image of elf file.
 * @param[in] section_header pointer to VPU ELF PVA_SEG_VPU_IN_PARAMS section header
 * @param[in, out] symbol_info pointer to ELF image symbol which needs to be updated.
*/
static enum pva_error
update_exports_symbol(elf_parser_ctx elf,
		      const elfSectionHeader *section_header,
		      struct pva_symbol_info *symbol_info)
{
	const elfByte *data;
	uint32_t symOffset = 0U;
	enum pva_error err = PVA_SUCCESS;
	pva_math_error math_err = MATH_OP_SUCCESS;

	if ((section_header == NULL) ||
	    (symbol_info->vmem_addr < section_header->addr) ||
	    (addu32(symbol_info->vmem_addr, (uint32_t)SIZE_EXPORTS_TABLE_ENTRY,
		    &math_err) >
	     addu32(section_header->addr, section_header->size, &math_err))) {
		err = PVA_INVAL;
		goto done;
	} else {
		symOffset = subu32(symbol_info->vmem_addr, section_header->addr,
				   &math_err);
	}
	data = elf_section_contents(elf, section_header);
	if (data == NULL) {
		pva_kmd_log_err("Export section in ELF is NULL");
		err = PVA_INVAL;
		goto done;
	}
	symbol_info->symbol_type = *(uint8_t *)((uintptr_t)&data[symOffset]);
	if ((symbol_info->symbol_type == (uint8_t)PVA_SYM_TYPE_INVALID) ||
	    (symbol_info->symbol_type >= (uint8_t)PVA_SYM_TYPE_MAX)) {
		pva_kmd_log_err("Invalid symbol type found");
		err = PVA_INVAL;
		goto done;
	}
	symbol_info->vmem_addr =
		*(uint32_t *)((uintptr_t)&data[symOffset + sizeof(uint32_t)]);
	symbol_info->size = *(uint32_t *)((
		uintptr_t)&data[symOffset + (2UL * sizeof(uint32_t))]);
	if (math_err != MATH_OP_SUCCESS) {
		pva_kmd_log_err("update_exports_symbol math error");
		err = PVA_ERR_MATH_OP;
		goto done;
	}
done:
	return err;
}

static bool validate_vmem_offset(const uint32_t vmem_offset,
				 const uint32_t size,
				 const uint8_t vmem_region_count,
				 const struct vmem_region *vmem_regions_tab)
{
	bool valid = false;
	uint32_t i = 0U;
	uint32_t prev_idx;
	pva_math_error math_err = MATH_OP_SUCCESS;

	for (i = vmem_region_count; i > 0U; i--) {
		prev_idx = subu32(i, 1U, &math_err);
		if (vmem_offset >= vmem_regions_tab[prev_idx].start) {
			break;
		}
	}

	if ((i > 0U) && (addu32(vmem_offset, size, &math_err) <=
			 vmem_regions_tab[prev_idx].end)) {
		valid = true;
	}

	return (math_err != MATH_OP_SUCCESS) ? false : valid;
}

static enum pva_error copy_symbol(elf_parser_ctx elf, const elfSymbol *sym,
				  const char *symname,
				  struct pva_symbol_info *symbol_info,
				  const uint8_t vmem_region_count,
				  const struct vmem_region *vmem_regions_tab)
{
	const elfSectionHeader *sym_scn;
	enum pva_error err = PVA_SUCCESS;

	size_t symname_len = strnlen(symname, PVA_MAX_SYMBOL_NAME_LEN);
	if (symname_len > 0U) {
		(void)memcpy(symbol_info->name, symname, symname_len);
	}
	symbol_info->name[PVA_MAX_SYMBOL_NAME_LEN] = '\0';

	symbol_info->size = sym->size;
	symbol_info->vmem_addr = sym->value;

	sym_scn = elf_section_header(elf, sym->shndx);
	err = update_exports_symbol(elf, sym_scn, symbol_info);
	if (err != PVA_SUCCESS) {
		pva_kmd_log_err("Updating symbol from EXPORTS table failed");
		goto out;
	}

	if (!validate_vmem_offset(symbol_info->vmem_addr, symbol_info->size,
				  vmem_region_count, vmem_regions_tab)) {
		pva_kmd_log_err("Invalid symbol vmem offset in ELF");
		err = PVA_INVAL;
		goto out;
	}

out:
	return err;
}

static enum pva_error
fill_symbol_table(const elf_parser_ctx elf,
		  struct pva_kmd_exec_symbol_table *sym_table,
		  const uint8_t vmem_region_count,
		  const struct vmem_region *vmem_regions_tab)
{
	enum pva_error err = PVA_SUCCESS;
	pva_math_error math_err = MATH_OP_SUCCESS;
	const elfSectionHeader *section_header;
	uint32_t i, ent_count;
	const elfSymbol *sym;
	const char *symname;
	int32_t ret;
	uint32_t export_sym_idx = 0;

	section_header = elf_named_section_header(elf, ".symtab");

	if (section_header == NULL) {
		err = PVA_INVAL;
		pva_kmd_log_err("No symbol table found");
		goto done;
	}

	ent_count = section_header->size / section_header->entsize;
	for (i = 0; i < ent_count; i++) {
		struct pva_symbol_info *symbol_info;

		ret = validate_symbol(elf, i, &sym);
		if (ret < 0) {
			err = PVA_INVAL;
			pva_kmd_log_err("Validation of symbol failed");
			goto done;
		}
		if (ret == SYM_IGNORE) {
			continue;
		}

		symbol_info = &sym_table->symbols[export_sym_idx];
		ASSERT(symbol_info != NULL);
		symname = elf_symbol_name(elf, section_header, i);
		if (symname == NULL) {
			err = PVA_INVAL;
			pva_kmd_log_err("elf_symbol_name failed");
			goto done;
		}
		err = copy_symbol(elf, sym, symname, symbol_info,
				  vmem_region_count, vmem_regions_tab);
		if (err != PVA_SUCCESS) {
			goto done;
		}
		symbol_info->symbol_id =
			addu32(export_sym_idx, PVA_SYMBOL_ID_BASE, &math_err);
		export_sym_idx = addu32(export_sym_idx, 1U, &math_err);
		if (math_err != MATH_OP_SUCCESS) {
			err = PVA_ERR_MATH_OP;
			pva_kmd_log_err("fill_symbol_table math error");
			goto done;
		}
	}
done:
	return err;
}

/**
 * The simplify caller's life: the input ptr should always be considered freed
 * after this call. The returned new ptr should always be considered a new
 * allocation and it needs to be freed if not NULL.
 */
static void *pva_realloc(void *ptr, uint32_t old_size, uint32_t new_size)
{
	void *new_buffer;

	if (ptr == NULL) {
		return pva_kmd_zalloc(new_size);
	}

	if (new_size <= old_size) {
		return ptr;
	}

	new_buffer = pva_kmd_zalloc(new_size);
	if (new_buffer == NULL) {
		goto out;
	}

	memcpy(new_buffer, ptr, old_size);

out:
	pva_kmd_free(ptr);
	return new_buffer;
}

static void *copy_text_section(const elf_parser_ctx elf,
			       const elfSectionHeader *section_header,
			       void *out_buffer, uint32_t *buffer_size)
{
	const elfByte *elf_data;
	uint32_t const *word;
	uint32_t *dst_word;
	uint32_t wi;
	/* The load address in section header is in words (uint32_t) */
	uint32_t load_addr_bytes =
		safe_mulu32(section_header->addr, (uint32_t)sizeof(uint32_t));
	uint32_t needed_size =
		safe_addu32(load_addr_bytes, section_header->size);

	// Align required text section size
	needed_size =
		safe_pow2_roundup_u32(needed_size, TEXT_SECTION_ALIGNMENT);

	if (needed_size > *buffer_size) {
		out_buffer = pva_realloc(out_buffer, *buffer_size, needed_size);
		*buffer_size = needed_size;
	}

	if (out_buffer == NULL) {
		return NULL;
	}

	elf_data = elf_section_contents(elf, section_header);
	if (elf_data == NULL) {
		pva_kmd_log_err("copy_text_section elf_data error");
		return NULL;
	}

	word = (uint32_t const *)elf_data;

	dst_word = (uint32_t *)((uintptr_t)out_buffer + load_addr_bytes);
	for (wi = 0; wi < (section_header->size / sizeof(uint32_t)); wi++) {
		dst_word[wi] = change_byte_order(word[wi]);
	}

	return out_buffer;
}

/**
 * @brief Aggregate all text sections into a single, dynamically
 * allocated buffer.
 *
 * The placement of text sections needs to take into account of the loading
 * addresses.
 *
 * The endianness of text section needs to be changed.
 *
 * Caller is responsible for freeing the returned buffer.
 */
static void *aggregate_text_sections(const elf_parser_ctx elf,
				     uint32_t *out_size)
{
	const elfSectionHeader *section_header;
	uint32_t index = 0;
	const char *section_name;
	const elfWord sectionCount = elf_shnum(elf);
	void *sections_content = NULL;
	uint32_t sections_size = 0;

	for (index = 0; index < sectionCount; index++) {
		int32_t segment_type;

		section_header = elf_section_header(elf, index);
		if (section_header == NULL) {
			pva_kmd_log_err(
				"aggregate_text_sections elf_section_header error");
			goto out;
		}

		section_name = elf_section_name(elf, section_header);
		if (section_name == NULL) {
			pva_kmd_log_err(
				"aggregate_text_sections elf_section_name error");
			goto out;
		}
		segment_type = find_pva_ucode_segment_type(section_name);
		if ((section_header->type == SHT_PROGBITS) &&
		    (segment_type == (int32_t)PVA_SEG_VPU_CODE)) {
			sections_content =
				copy_text_section(elf, section_header,
						  sections_content,
						  &sections_size);
			if (sections_content == NULL) {
				pva_kmd_log_err(
					"aggregate_text_sections copy_text_section error");
				goto out;
			}
		}
	}
out:
	*out_size = sections_size;
	return sections_content;
}

static void copy_data_section(const elf_parser_ctx elf,
			      const elfSectionHeader *section_header,
			      void *out_buffer, uint32_t *buffer_offset,
			      uint32_t buffer_size)
{
	const elfByte *elf_data;
	void *dst;
	uint32_t aligned_size = safe_pow2_roundup_u32(section_header->size,
						      DATA_SECTION_ALIGNMENT);
	uint32_t size = safe_addu32(*buffer_offset, aligned_size);
	ASSERT(size <= buffer_size);

	dst = pva_offset_pointer(out_buffer, *buffer_offset);

	elf_data = elf_section_contents(elf, section_header);

	ASSERT(elf_data != NULL);

	memcpy(dst, elf_data, section_header->size);

	*buffer_offset = safe_addu32(*buffer_offset, aligned_size);
}

static enum pva_error count_data_sections(const elf_parser_ctx elf,
					  uint32_t *out_n_data_sections,
					  uint32_t *out_total_size)
{
	const elfSectionHeader *section_header;
	uint32_t index = 0;
	const char *section_name;
	const elfWord sectionCount = elf_shnum(elf);
	uint32_t n_data_sections = 0;
	uint32_t total_size = 0;
	enum pva_error err = PVA_SUCCESS;
	pva_math_error math_err = MATH_OP_SUCCESS;

	for (index = 0; index < sectionCount; index++) {
		int32_t segment_type;

		section_header = elf_section_header(elf, index);
		if (section_header == NULL) {
			err = PVA_INVAL;
			goto out;
		}

		section_name = elf_section_name(elf, section_header);
		if (section_name == NULL) {
			err = PVA_INVAL;
			goto out;
		}
		segment_type = find_pva_ucode_segment_type(section_name);
		if ((section_header->type == SHT_PROGBITS) &&
		    (segment_type == (int32_t)PVA_SEG_VPU_DATA)) {
			n_data_sections =
				addu32(n_data_sections, 1U, &math_err);
			total_size += safe_pow2_roundup_u32(
				section_header->size, DATA_SECTION_ALIGNMENT);
		}
	}
	if (math_err != MATH_OP_SUCCESS) {
		err = PVA_ERR_MATH_OP;
		pva_kmd_log_err("count_data_sections math error");
		goto out;
	}
	*out_n_data_sections = n_data_sections;
	*out_total_size = total_size;
out:
	return err;
}

/**
 * @brief Aggregate all data sections into a single, dynamically
 * allocated buffer.
 *
 * The offset of each data section must be aligned to DATA_SEGMENT_ALIGNMENT.
 *
 * The caller must free the returned data buffer and out_section_infos.
 *
 */
static void *
aggregate_data_sections(const elf_parser_ctx elf, uint32_t n_data_sections,
			uint32_t total_sections_size,
			struct pva_fw_data_section_info **out_section_infos)
{
	const elfSectionHeader *section_header;
	uint32_t index = 0;
	const char *section_name;
	const elfWord sectionCount = elf_shnum(elf);
	void *sections_content = NULL;
	struct pva_fw_data_section_info *section_infos;
	uint32_t buffer_offset = 0;
	uint32_t sec_idx = 0;

	sections_content = pva_kmd_zalloc(total_sections_size);
	if (sections_content == NULL) {
		goto err_out;
	}
	section_infos =
		pva_kmd_zalloc(sizeof(*section_infos) * n_data_sections);
	if (section_infos == NULL) {
		goto free_content;
	}

	for (index = 0; index < sectionCount; index++) {
		int32_t segment_type;

		section_header = elf_section_header(elf, index);
		/* Already checked when count data sections */
		ASSERT(section_header != NULL);

		section_name = elf_section_name(elf, section_header);
		ASSERT(section_name != NULL);
		segment_type = find_pva_ucode_segment_type(section_name);
		if ((section_header->type == SHT_PROGBITS) &&
		    (segment_type == (int32_t)PVA_SEG_VPU_DATA)) {
			section_infos[sec_idx].data_buf_off = buffer_offset;
			section_infos[sec_idx].vmem_addr = section_header->addr;
			section_infos[sec_idx].size = section_header->size;
			sec_idx = safe_addu32(sec_idx, 1U);

			copy_data_section(elf, section_header, sections_content,
					  &buffer_offset, total_sections_size);
		}
	}

	*out_section_infos = section_infos;
	return sections_content;
free_content:
	pva_kmd_free(sections_content);
err_out:
	return NULL;
}

/**
 * @brief layout text and data sections in a single continuous buffer that is
 * mapped to PVA IOVA space (user SID).
 *
 * We need to pad text size by an entire VPU icache size to avoid SMMU fault
 * when prefetching.
 */
static struct pva_kmd_device_memory *
load_sections(struct pva_kmd_device *pva, uint8_t smmu_id,
	      const void *text_section_buf, uint32_t text_size,
	      const void *data_section_buf, uint32_t data_size,
	      uint32_t *out_data_begin_offset)
{
	uint32_t size = safe_addu32(text_size, (uint32_t)VPU_ICACHE_SIZE);
	uint32_t alloc_size = safe_addu32(size, data_size);
	uint32_t data_begin = safe_addu32(text_size, (uint32_t)VPU_ICACHE_SIZE);
	struct pva_kmd_device_memory *dev_mem;

	ASSERT(TEXT_SECTION_ALIGNMENT >= DATA_SECTION_ALIGNMENT);
	/* This is guaranteed to be true as TEXT_SECTION_ALIGNMENT is more strict */
	ASSERT(data_begin % DATA_SECTION_ALIGNMENT == 0);

	/* Map it as read-only. TODO: when VPU debugger is supported, we may
	 * need to map text as READ_WRITE conditionally. */
	dev_mem = pva_kmd_device_memory_alloc_map(alloc_size, pva,
						  PVA_ACCESS_RO, smmu_id);
	if (dev_mem == NULL) {
		goto out;
	}

	memcpy(dev_mem->va, text_section_buf, text_size);
	memcpy(pva_offset_pointer(dev_mem->va, data_begin), data_section_buf,
	       data_size);

	*out_data_begin_offset = data_begin;
out:
	return dev_mem;
}

static struct pva_kmd_device_memory *
load_metainfo(struct pva_kmd_device *pva, uint64_t section_iova,
	      uint32_t text_size, uint32_t data_begin_off, uint32_t data_size,
	      struct pva_fw_data_section_info const *section_infos,
	      uint32_t n_data_sections, struct pva_symbol_info *symbol_table,
	      uint32_t n_symbols)
{
	struct pva_kmd_device_memory *dev_mem;
	struct pva_exec_bin_resource *metainfo;
	struct pva_fw_vmem_buffer *vmem_buffers_mem;
	struct pva_fw_data_section_info *data_sections_mem;
	uint32_t i;
	uint32_t alloc_size = (uint32_t)sizeof(struct pva_exec_bin_resource);
	pva_math_error math_err = MATH_OP_SUCCESS;

	alloc_size =
		addu32(alloc_size,
		       mulu32(n_data_sections,
			      (uint32_t)sizeof(struct pva_fw_data_section_info),
			      &math_err),
		       &math_err);

	alloc_size = addu32(alloc_size,
			    mulu32(n_symbols,
				   (uint32_t)sizeof(struct pva_fw_vmem_buffer),
				   &math_err),
			    &math_err);

	dev_mem = pva_kmd_device_memory_alloc_map(
		alloc_size, pva, PVA_ACCESS_RO, PVA_R5_SMMU_CONTEXT_ID);
	if (dev_mem == NULL) {
		goto out;
	}

	metainfo = dev_mem->va;
	metainfo->code_addr_hi = iova_hi(section_iova);
	metainfo->code_addr_lo = iova_lo(section_iova);
	metainfo->code_size = text_size;
	metainfo->data_section_addr_hi =
		iova_hi(addu64(section_iova, data_begin_off, &math_err));
	metainfo->data_section_addr_lo =
		iova_lo(addu64(section_iova, data_begin_off, &math_err));
	metainfo->num_data_sections = n_data_sections;
	metainfo->num_vmem_buffers = n_symbols;

	data_sections_mem = pva_offset_pointer(metainfo, sizeof(*metainfo));
	if (n_data_sections > 0U && section_infos != NULL) {
		memcpy(data_sections_mem, section_infos,
		       mulu32(n_data_sections, (uint32_t)sizeof(*section_infos),
			      &math_err));
	}

	vmem_buffers_mem = pva_offset_pointer(
		data_sections_mem,
		mulu32(n_data_sections, (uint32_t)sizeof(*section_infos),
		       &math_err));
	if (math_err != MATH_OP_SUCCESS) {
		dev_mem = NULL;
		goto out;
	}

	for (i = 0; i < n_symbols; i++) {
		vmem_buffers_mem[i].addr =
			PVA_INSERT(symbol_table[i].vmem_addr,
				   PVA_FW_VMEM_ADDR_MSB, PVA_FW_VMEM_ADDR_LSB) |
			PVA_INSERT((uint32_t)symbol_table[i].symbol_type,
				   PVA_FW_SYM_TYPE_MSB, PVA_FW_SYM_TYPE_LSB);
		vmem_buffers_mem[i].size = symbol_table[i].size;
	}

out:
	return dev_mem;
}

enum pva_error
pva_kmd_load_executable(const void *executable_data, uint32_t executable_size,
			struct pva_kmd_device *pva, uint8_t dma_smmu_id,
			struct pva_kmd_exec_symbol_table *out_symbol_table,
			struct pva_kmd_device_memory **out_metainfo,
			struct pva_kmd_device_memory **out_sections)
{
	enum pva_error err = PVA_SUCCESS;
	pva_math_error math_err = MATH_OP_SUCCESS;
	elf_parser_ctx elf = { 0 };
	uint32_t num_symbols = 0;
	uint32_t n_data_sections;
	uint32_t total_data_section_size = 0;
	struct pva_fw_data_section_info *section_infos = NULL;
	void *data_section_buf = NULL;
	void *text_section_buf = NULL;
	uint32_t total_text_section_size = 0;
	struct pva_kmd_device_memory *metainfo_mem = NULL;
	struct pva_kmd_device_memory *sections_mem = NULL;
	uint32_t data_begin_off;

	elf.elf_file = executable_data;
	elf.size = executable_size;
	err = validate_elf(elf);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	err = count_symbols(elf, &num_symbols);
	if (err != PVA_SUCCESS) {
		goto err_out;
	}

	out_symbol_table->n_symbols = num_symbols;
	if (num_symbols > 0) {
		out_symbol_table->symbols = pva_kmd_zalloc(
			mulu32((uint32_t)sizeof(struct pva_symbol_info),
			       num_symbols, &math_err));
		if (out_symbol_table->symbols == NULL) {
			err = PVA_NOMEM;
			goto err_out;
		}
		if (math_err != MATH_OP_SUCCESS) {
			err = PVA_ERR_MATH_OP;
			pva_kmd_log_err("pva_kmd_load_executable math error");
			goto err_out;
		}
	}

	err = fill_symbol_table(elf, out_symbol_table,
				pva->hw_consts.n_vmem_regions,
				pva->vmem_regions_tab);
	if (err != PVA_SUCCESS) {
		goto free_syms;
	}

	text_section_buf =
		aggregate_text_sections(elf, &total_text_section_size);
	/* Must have text sections */
	if (text_section_buf == NULL) {
		pva_kmd_log_err(
			"pva_kmd_load_executable aggregate_text_sections error");
		goto free_syms;
	}

	err = count_data_sections(elf, &n_data_sections,
				  &total_data_section_size);
	if (err != PVA_SUCCESS) {
		goto free_text_buf;
	}

	/* It's OK to not have data sections */
	if (total_data_section_size != 0) {
		data_section_buf =
			aggregate_data_sections(elf, n_data_sections,
						total_data_section_size,
						&section_infos);
		ASSERT(data_section_buf != NULL);
	}

	sections_mem = load_sections(pva, dma_smmu_id, text_section_buf,
				     total_text_section_size, data_section_buf,
				     total_data_section_size, &data_begin_off);
	if (sections_mem == NULL) {
		goto free_data_buf;
	}

	metainfo_mem =
		load_metainfo(pva, sections_mem->iova, total_text_section_size,
			      data_begin_off, total_data_section_size,
			      section_infos, n_data_sections,
			      out_symbol_table->symbols, num_symbols);
	if (metainfo_mem == NULL) {
		goto free_sec_mem;
	}
	/* Success. Now clean up temporary allocations */
	if (data_section_buf != NULL) {
		pva_kmd_free(data_section_buf);
	}
	if (section_infos != NULL) {
		pva_kmd_free(section_infos);
	}
	pva_kmd_free(text_section_buf);

	*out_metainfo = metainfo_mem;
	*out_sections = sections_mem;

	return PVA_SUCCESS;
free_sec_mem:
	pva_kmd_device_memory_free(sections_mem);
free_data_buf:
	if (data_section_buf != NULL) {
		pva_kmd_free(data_section_buf);
	}
	if (section_infos != NULL) {
		pva_kmd_free(section_infos);
	}
free_text_buf:
	pva_kmd_free(text_section_buf);
free_syms:
	pva_kmd_free(out_symbol_table->symbols);
err_out:
	return err;
}

void pva_kmd_unload_executable(struct pva_kmd_exec_symbol_table *symbol_table,
			       struct pva_kmd_device_memory *metainfo,
			       struct pva_kmd_device_memory *sections)
{
	pva_kmd_device_memory_free(metainfo);
	pva_kmd_device_memory_free(sections);
	if (symbol_table->symbols != NULL) {
		pva_kmd_free(symbol_table->symbols);
		symbol_table->symbols = NULL;
	}
}
