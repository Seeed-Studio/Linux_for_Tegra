// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include "pva_kmd_silicon_elf_parser.h"
#include "pva_kmd_utils.h"
#include "pva_kmd_limits.h"

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

// CERT complains about casts from const uint8_t*, so do intermediate cast to void*
static inline const void *uint_8_to_void(const uint8_t *const p)
{
	return (const void *)p;
}

bool elf_header_check(const elf_ct e)
{
	const elfFileHeader *efh = (const elfFileHeader *)e;
	if ((PVA_ELFCLASS32 == efh->oclass) &&
	    (PVA_ELFMAGIC_LSB == *(const elfWord *)e)) {
		return true;
	}
	return false;
}

/**
 * @brief Return pointer to ELF file header
 *
 * Cast the elf image data to \ref elfFileHeader*
 *
 * @param [in] e pointer to elf image data
 * @return
 * - Valid poniter to ELF file header
 * - NULL if \a e is NULL or correct elf magic ID is not present
 * in first 4 bytes of elf file pointed by \a e.
 *
 */
static const elfFileHeader *elf_file_header(const elf_ct e)
{
	return (const elfFileHeader *)e;
}

/**
 * @brief Get start address of the section table.
 *
 * @param[in] e pointer to elf image
 * @return const elfSectionHeader*
 * - Valid address of section header.
 * - NULL if \a e is NULL or Header in ELF file is NULL.
 */
static inline const elfSectionHeader *elf_section_table(const elf_parser_ctx e)
{
	const elfFileHeader *efh = elf_file_header(e.elf_file);
	const char *p = (const char *)e.elf_file;

	if (efh->shoff > e.size) {
		pva_kmd_log_err("Invalid Section header Offset");
		return NULL;
	}
	p = &p[efh->shoff];
	// proper ELF should always have offsets be aligned,
	// but add check just in case.
	return (const elfSectionHeader *)(const void *)(p);
}

/**
 * @brief Get the size of ELF section
 *
 * @param esh pointer to ELF section header
 * @return elfWord
 * - size of the corresponding section header.
 * - 0, if \a esh is NULL.
 *
 */
static elfWord elf_section_size(const elfSectionHeader *esh)
{
	if (NULL == esh) {
		return UZERO;
	}
	return (elfWord)esh->size;
}

elfWord elf_shnum(const elf_parser_ctx e)
{
	const elfFileHeader *efh = elf_file_header(e.elf_file);
	if (NULL == efh) {
		return UZERO;
	}
	if (UZERO == efh->shnum) {
		/* get value from size of first (empty) section */
		/* to avoid recursion, don't call elf_section_header(0) */
		const elfSectionHeader *esh = elf_section_table(e);
		// if esh is somehow NULL, section_size will return UZERO
		elfWord size = elf_section_size(esh);
		if (size > e.size) { // make sure we don't lose precision
			return UZERO;
		} else {
			return size;
		}
	} else {
		return (elfWord)efh->shnum;
	}
}

const elfSectionHeader *elf_section_header(const elf_parser_ctx e,
					   unsigned int index)
{
	const elfSectionHeader *esh = elf_section_table(e);
	if (NULL == esh) {
		return NULL;
	}
	if (index >= elf_shnum(e)) {
		return NULL;
	}

	esh = &esh[index];
	return esh;
}

static inline elfOff get_table_end(elfWord num, elfHalf entsize, elfOff off)
{
	elfOff end;
	elfWord tablesize = 0;
	/**
	 * Guaranteed to be less than UINT32_MAX and not overflow
	 * num if set as efh->shnum is UINT16_MAX
	 * num if set as section_header->size is file size of ELF which
	 * is bound to 2 MB
	 */
	tablesize = safe_mulu32(num, (uint32_t)entsize);

	end = off + tablesize;
	if (end < off) {
		return UZERO; //Wrap around error
	}
	return end;
}

bool elf_has_valid_sections(const elf_parser_ctx e)
{
	elfOff max_size = UZERO;
	uint32_t i;
	elfOff ph_end, sh_end;
	const elfFileHeader *efh = elf_file_header(e.elf_file);
	if (efh == NULL) {
		return false;
	}
	ph_end = get_table_end(efh->phnum, efh->phentsize, efh->phoff);
	sh_end = get_table_end(elf_shnum(e), efh->shentsize, efh->shoff);
	max_size = max(ph_end, sh_end);
	if ((max_size == UZERO) || (max_size > e.size)) {
		return false;
	}
	for (i = UZERO; i < elf_shnum(e); ++i) {
		elfOff esh_end;
		const elfSectionHeader *esh = elf_section_header(e, i);
		/*We have already validated the whole section header array is within the file*/
		ASSERT(esh != NULL);
		esh_end = esh->offset + esh->size;
		if (esh_end < esh->offset) {
			return false; //WRAP around error;
		}
		if ((esh->type != SHT_NOBITS) && (esh_end > e.size)) {
			return false;
		}
	}
	return true;
}

/**
 * @brief Get section header index
 * get elf_file_header and check it's not null,
 * get value from link field of first (empty) section
 * if esh is somehow NULL, return esh link
 *
 * @param[in] e		elf context
 *
 * @return 		section header index
 */
static elfWord elf_shstrndx(const elf_parser_ctx e)
{
	const elfFileHeader *efh = elf_file_header(e.elf_file);
	if (NULL == efh) {
		return UZERO;
	}
	if (efh->shstrndx == SHN_XINDEX) {
		/* get value from link field of first (empty) section */
		/* to avoid recursion, don't call elf_section_header(0) */
		const elfSectionHeader *esh = elf_section_table(e);
		if (NULL == esh) {
			return UZERO;
		}
		return esh->link;
	}
	return efh->shstrndx;
}

/**
 * @brief Get name of string from strtab section
 * check elf context and section header not null,
 * check from section header for type and size are not null.
 * Get strtab section, check that stroffset doesn't wrap
 *
 * @param[in] e		elf context
 * @param[in] eshstr	pointer to elf Section header
 * @param[in] offset	offset in integer
 * 			Valid range: 0 to eshstr->size
 *
 * @return 		name of string from strtab section "eshstr" at "offset"
 */
static const char *elf_string_at_offset(const elf_parser_ctx e,
					const elfSectionHeader *eshstr,
					unsigned int offset)
{
	const char *string_table;
	elfOff string_offset;

	if (SHT_STRTAB != eshstr->type) {
		return NULL;
	}
	if (offset >= eshstr->size) {
		return NULL;
	}
	string_table = (const char *)e.elf_file;
	string_offset = eshstr->offset + offset;
	if (string_offset <
	    eshstr->offset) { // check that string_offset doesn't wrap
		return NULL;
	}
	string_table = &string_table[string_offset];
	return string_table;
}

const char *elf_section_name(const elf_parser_ctx e,
			     const elfSectionHeader *esh)
{
	const char *name;
	const elfSectionHeader *eshstr;
	elfWord shstrndx;

	/* get section header string table */
	shstrndx = elf_shstrndx(e);
	if (shstrndx == UZERO) {
		return NULL;
	}
	eshstr = elf_section_header(e, shstrndx);
	if ((NULL == esh) || (NULL == eshstr)) {
		return NULL;
	}
	name = elf_string_at_offset(e, eshstr, esh->name);
	return name;
}

const elfSectionHeader *elf_named_section_header(const elf_parser_ctx e,
						 const char *name)
{
	const elfSectionHeader *esh;
	unsigned int i;
	if (NULL == name) {
		return NULL;
	}
	esh = elf_section_table(e);
	if (NULL == esh) {
		return NULL;
	}

	/* iterate through sections till find matching name */
	for (i = UZERO; i < elf_shnum(e); ++i) {
		const char *secname = elf_section_name(e, esh);
		if (NULL != secname) {
			size_t seclen = strlen(secname);

			// use strncmp to avoid problem with input not being null-terminated,
			// but then need to check for false partial match
			if ((ZERO == strncmp(secname, name, seclen)) &&
			    (UZERO == (uint8_t)name[seclen])) {
				return esh;
			}
		}
		++esh;
	}
	return NULL;
}

/**
 * @brief Get section header
 * Get elf_section_table pointer and check it and
 * iterate through sections till find matching type
 *
 * @param[in] e		elf context
 * @param[in] type	type in word size
 *
 * @return 		elf section header with given "type"
 */
static const elfSectionHeader *elf_typed_section_header(const elf_parser_ctx e,
							elfWord type)
{
	unsigned int i;
	const elfSectionHeader *esh = elf_section_table(e);
	if (NULL == esh) {
		return NULL;
	}

	/* iterate through sections till find matching type */
	for (i = UZERO; i < elf_shnum(e); ++i) {
		if (esh->type == type) {
			return esh;
		}
		++esh;
	}
	return NULL;
}

const elfByte *elf_section_contents(const elf_parser_ctx e,
				    const elfSectionHeader *esh)
{
	const elfByte *p;
	if ((NULL == e.elf_file) || (NULL == esh)) {
		return NULL;
	}
	p = (const elfByte *)e.elf_file;
	if ((esh->offset > e.size) ||
	    ((uint64_t)((uint64_t)esh->offset + (uint64_t)esh->size) >
	     e.size)) {
		return NULL;
	}
	return &p[esh->offset];
}

const elfSymbol *elf_symbol(const elf_parser_ctx e, unsigned int index)
{
	const elfSectionHeader *esh;
	const elfSymbol *esymtab;
	const uint8_t *p = e.elf_file;
	uint8_t align = 0;
	/* get symbol table */
	esh = elf_typed_section_header(e, SHT_SYMTAB);
	if ((NULL == esh) || (UZERO == esh->entsize)) {
		return NULL;
	}
	if (index >= (esh->size / esh->entsize)) {
		return NULL;
	}
	if (esh->addralign <= (uint8_t)U8_MAX) {
		align = (uint8_t)esh->addralign;
	} else {
		return NULL;
	}
	if ((uint64_t)((uint64_t)esh->size + (uint64_t)esh->offset) > e.size) {
		return NULL;
	}
	p = &p[esh->offset];
	esymtab = (const elfSymbol *)uint_8_to_void(p);
	if ((align != 0U) && ((((uintptr_t)(esymtab) % align) != UZERO))) {
		return NULL;
	}

	return &esymtab[index];
}

const char *elf_symbol_name(const elf_parser_ctx e, const elfSectionHeader *esh,
			    unsigned int index)
{
	const elfSectionHeader *eshstr;
	const elfSymbol *esymtab;
	const elfSymbol *esym;
	const char *name;
	const char *p;
	uint8_t align = 0;

	if ((NULL == esh) || (UZERO == esh->entsize)) {
		return NULL;
	}
	if (SHT_SYMTAB != esh->type) {
		return NULL;
	}
	if (index >= (esh->size / esh->entsize)) {
		return NULL;
	}
	/* get string table */
	eshstr = elf_section_header(e, esh->link);
	if (NULL == eshstr) {
		return NULL;
	}
	p = (const char *)e.elf_file;
	if (esh->addralign <= (uint8_t)U8_MAX) {
		align = (uint8_t)esh->addralign;
	} else {
		return NULL;
	}
	if (esh->offset > e.size) {
		return NULL;
	}
	p = &p[esh->offset];
	esymtab = (const elfSymbol *)(const void *)(p);
	if ((align != 0U) && ((((uintptr_t)(esymtab) % align) != UZERO))) {
		return NULL;
	}
	esym = &esymtab[index];
	name = elf_string_at_offset(e, eshstr, esym->name);
	return name;
}
