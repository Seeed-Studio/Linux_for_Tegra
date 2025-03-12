/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */
#ifndef PVA_KMD_SILICON_ELF_PARSER_H
#define PVA_KMD_SILICON_ELF_PARSER_H
#include "pva_api.h"

#define ZERO 0
#define UZERO 0U
#define ULLZERO 0ULL

/*
 * Define mapping from VPU data, rodata and program sections into
 * corresponding segment types.
 */
typedef const void *elf_ct; /* points to const image of elf file */

/**
 * Struct containing the ELF Buffer and size of the buffer.
 */
typedef struct {
	/** Pointer to buffer containing ELF File */
	elf_ct elf_file;
	/** Size of the buffer containing ELF File */
	uint64_t size;
} elf_parser_ctx;

/*--------------------------------- Types ----------------------------------*/
/** unsinged 8-bit data type */
typedef uint8_t elfByte;
/** unsinged 16-bit data type */
typedef uint16_t elfHalf;
/** unsinged 32-bit data type */
typedef uint32_t elfWord;
/** unsinged 32-bit data type */
typedef uint32_t elfAddr;
/** unsinged 32-bit data type */
typedef uint32_t elfOff;

/**
 * @brief ELF File Header
 *
 */
typedef struct {
	/** ELF magic number : 0x7f,0x45,0x4c,0x46 */
	elfWord magic;
	/** Object file class */
	elfByte oclass;
	/** Data encoding */
	elfByte data;
	/** Object format version */
	elfByte formatVersion;
	/** OS application binary interface */
	elfByte abi;
	/** Version of abi */
	elfByte abiVersion;
	/** Elf ident padding */
	elfByte padd[7];
	/** Object file type */
	elfHalf type;
	/** Architecture */
	elfHalf machine;
	/** Object file version */
	elfWord version;
	/** Entry point virtual address */
	elfAddr entry;
	/** Program header table file offset */
	elfOff phoff;
	/** Section header table file offset */
	elfOff shoff;
	/** Processor-specific flags */
	elfWord flags;
	/** ELF header size in bytes */
	elfHalf ehsize;
	/** Program header table entry size */
	elfHalf phentsize;
	/** Program header table entry count */
	elfHalf phnum;
	/** Section header table entry size */
	elfHalf shentsize;
	/** Section header table entry count */
	elfHalf shnum;
	/** Section header string table index */
	elfHalf shstrndx;
} elfFileHeader;

/** ELF magic number in big endian */
#define ELFMAGIC 0x7f454c46U
#define ELFMAGIC_LSB 0x464c457fU // ELF magic number in little endian
#define ELFCLASS32 1U // 32 bit object file

#define EV_NONE 0 // Invalid version
#define EV_CURRENT 1 // Current version

/**
 * @brief ELF Section Header
 *
 */
typedef struct {
	/** Section name, string table index */
	elfWord name;
	/** Type of section */
	elfWord type;
	/** Miscellaneous section attributes */
	elfWord flags;
	/** Section virtual addr at execution */
	elfAddr addr;
	/** Section file offset */
	elfOff offset;
	/** Size of section in bytes */
	elfWord size;
	/** Index of another section */
	elfWord link;
	/** Additional section information */
	elfWord info;
	/** Section alignment */
	elfWord addralign;
	/** Entry size if section holds table */
	elfWord entsize;
} elfSectionHeader;

/*
* Section Header Type
*/
#define SHT_NULL 0x00U /// NULL section (entry unused)
#define SHT_PROGBITS 0x01U /// Loadable program data
#define SHT_SYMTAB 0x02U /// Symbol table
#define SHT_STRTAB 0x03U /// String table
#define SHT_RELA 0x04U /// Relocation table with addents
#define SHT_HASH 0x05U /// Hash table
#define SHT_DYNAMIC 0x06U /// Information for dynamic linking
#define SHT_NOTE 0x07U /// Information that marks file
#define SHT_NOBITS 0x08U /// Section does not have data in file
#define SHT_REL 0x09U /// Relocation table without addents
#define SHT_SHLIB 0x0aU /// Reserved
#define SHT_DYNSYM 0x0bU /// Dynamic linker symbol table
#define SHT_INIT_ARRAY 0x0eU /// Array of pointers to init funcs
#define SHT_FINI_ARRAY 0x0fU /// Array of function to finish funcs
#define SHT_PREINIT_ARRAY 0x10U /// Array of pointers to pre-init functions
#define SHT_GROUP 0x11U /// Section group
#define SHT_SYMTAB_SHNDX 0x12U /// Table of 32bit symtab shndx
#define SHT_LOOS 0x60000000U /// Start OS-specific.
#define SHT_HIOS 0x6fffffffU /// End OS-specific type
#define SHT_LOPROC 0x70000000U /// Start of processor-specific
#define SHT_HIPROC 0x7fffffffU /// End of processor-specific
#define SHT_LOUSER 0x80000000U /// Start of application-specific
#define SHT_HIUSER 0x8fffffffU /// End of application-specific

/*
* Special section index
*/
#define SHN_UNDEF 0U // Undefined section
#define SHN_LORESERVE 0xff00U // lower bound of reserved indexes
#define SHN_ABS 0xfff1U // Associated symbol is absolute
#define SHN_COMMON 0xfff2U // Associated symbol is common
#define SHN_XINDEX 0xffffU // Index is in symtab_shndx

/*
* Special section names
*/
#define SHNAME_SHSTRTAB ".shstrtab" /// section string table
#define SHNAME_STRTAB ".strtab" /// string table
#define SHNAME_SYMTAB ".symtab" /// symbol table
#define SHNAME_SYMTAB_SHNDX ".symtab_shndx" /// symbol table shndx array
#define SHNAME_TEXT ".text." /// suffix with entry name

/**
 * @brief Symbol's information
 *
 */
typedef struct {
	/** Symbol name, index in string tbl */
	elfWord name;
	/** Value of the symbol */
	elfAddr value;
	/** Associated symbol size */
	elfWord size;
	/** Type and binding attributes */
	elfByte info;
	/** Extra flags */
	elfByte other;
	/** Associated section index */
	elfHalf shndx;
} elfSymbol;

/** Get the \a binding info of the symbol */
#define ELF_ST_BIND(s) ((elfWord)((s)->info) >> 4)
/** Get the \a type info of the symbol */
#define ELF_ST_TYPE(s) ((elfWord)((s)->info) & 0xfU)

/*
* ELF symbol type
*/
#define STT_NOTYPE 0U // No type known
#define STT_OBJECT 1U // Data symbol
#define STT_FUNC 2U // Code symbol
#define STT_SECTION 3U // Section
#define STT_FILE 4U // File
#define STT_COMMON 5U // Common symbol
#define STT_LOOS 10U // Start of OS-specific

/*
* ELF symbol scope (binding)
*/
#define STB_LOCAL 0U /// Symbol not visible outside object
#define STB_GLOBAL 1U /// Symbol visible outside object
#define STB_WEAK 2U /// Weak symbol

/*
 * The following routines that return file/program/section headers
 * all return NULL when not found.
 */

/*
 *  Typical elf readers create a table of information that is passed
 *  to the different routines.  For simplicity, we're going to just
 *  keep the image of the whole file and pass that around.  Later, if we see
 *  a need to speed this up, we could consider changing elf_parser_ctx to be something
 *  more complicated.
 */

/**
 * @brief Checks if the file stored in \a e is a 32-bit elf file
 * and if the first 4 bytes contain elf magic ID.
 *
 * @param[in] e		elf context containing complete ELF in a const buffer
 *
 * @return
 *     - TRUE if valid 32-bit elf file and correct elf magic ID present
 *       in first 4 bytes of elf file
 *     - FALSE if either of the above condition is not met
 */
bool elf_header_check(const elf_ct e);

/**
 * @brief Provide number of sections in sections header table
 * get elf_file_header and check it's not null,
 * get value from size of first (empty) section
 * if esh is NULL, section_size will return zero
 *
 * @param[in] e		elf context containing complete ELF in a const buffer
 *
 * @return 		section header number
 */
elfWord elf_shnum(const elf_parser_ctx e);

/**
 * @brief This function checks all sections in the elf to be valid
 *
 * The function validates all sections as follows:
 * - Valid section offset i.e. within file bounds.
 * - Valid section size i.e. non-zero section size
 *   and offset + section size is within file bounds
 *
 * @param[in]e		elf context containing completeELF in a const buffer  
 *
 * @return
 * 	- TRUE if all sections are valid
 * 	- FALSE if any invalid section found
 */
bool elf_has_valid_sections(const elf_parser_ctx e);

/**
 * @brief This function traverses the elf and
 * returns a valid \ref elfSectionHeader if present
 * at the index provided
 *
 * @param[in] e		elf context containing complete ELF in a const buffer
 * @param[in] index	The index of the elfSectionHeader that is requested
 * 			Valid range : 0 to elf_shnum(e)
 *
 * @return
 *     - valid elfSectionHeader from elf if index is valid and if sectionHeader is present
 *     - NULL if invalid or out of bounds index
 */
const elfSectionHeader *elf_section_header(const elf_parser_ctx e,
					   unsigned int index);

/**
 * @brief This function obtains the name of the \ref elfSectionHeader
 * by going to the index specified by elfSectionHeader->name in the string table
 * of the elf
 *
 * @param[in] e			elf context
 *
 * @param[in] esh		Valid \ref elfSectionHeader whose name is requested
 *
 * @return
 *     - Non NULL character array containing name of the elfSectionHeader
 *       if found in elf String Table
 *     - NULL if invalid elfSectionHeader or invalid index in elfSectionHeader->name
 *       going out of bounds of string table of elf
 */
const char *elf_section_name(const elf_parser_ctx e,
			     const elfSectionHeader *esh);

/**
 * @brief Provide elf section header with given "name".
 * check elf context not a null, get elf_section_table and
 *  then iterate through sections till find matching name
 *
 * @param[in] e		elf context
 * @param[in] name	name of section
 *
 * @return
 *     - elf section header with given "name"
 *     - NULL if @a name is NULL or invalid elfSectionHeader is found
 */
const elfSectionHeader *elf_named_section_header(const elf_parser_ctx e,
						 const char *name);

/**
 * @brief Provide contents of section.
 * check elf context and section header not a null,
 *  return byte pointer of section header offset of elf context
 * @param[in] e		elf context
 * @param[in] esh	section header
 *
 i* @return 		Bytepointer of elf (NULL if e or esh == NULL)
 */
const elfByte *elf_section_contents(const elf_parser_ctx e,
				    const elfSectionHeader *esh);

/**
 * @brief Get ELF symbol
 * get elf_typed_section_header section header,
 * check header or it's entsize not null.
 * check index is not crossing section header & table size
 * Also make sure it is address aligned and get symbol table.
 *
 * @param[in] e		elf context
 * @param[in] index 	unsigned index
 * 			Valid range: 0 to number of entries in SHT_SYMTAB of e
 *
 * @return 		elf symbol at given index (NULL if not found).
 */
const elfSymbol *elf_symbol(const elf_parser_ctx e, unsigned int index);

/**
 * @brief Get symbol table section
 * check section header or it's entsize not null.
 * check index is not crossing section header & table size
 * get elf_section_header and Also make sure it is address
 * aligned and get symbol table.
 *
 * @param[in] e		elf context
 * @param[in] esh 	pointer to structure elfSectionHeader
 * @param[in] index 	unsigned index
 * 			Valid range: 0 to number of entries in SHT_SYMTAB of e
 *
 * @return 		name of symbol from symtab section "esh" at "index".
 */
const char *elf_symbol_name(const elf_parser_ctx e, const elfSectionHeader *esh,
			    unsigned int index);

#endif // PVA_KMD_SILICON_ELF_PARSER_H
