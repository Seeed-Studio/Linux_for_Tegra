/* SPDX-License-Identifier: GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#ifndef PVA_KMD_SILICON_ELF_PARSER_H
#define PVA_KMD_SILICON_ELF_PARSER_H
#include "pva_api.h"

/**
 * @brief Zero constant for signed integer values
 *
 * @details Standard zero constant used throughout ELF parsing operations
 * for initialization and comparison of signed integer values.
 */
#define ZERO 0

/**
 * @brief Zero constant for unsigned integer values
 *
 * @details Standard zero constant used throughout ELF parsing operations
 * for initialization and comparison of unsigned integer values.
 */
#define UZERO 0U

/**
 * @brief Zero constant for unsigned long long values
 *
 * @details Standard zero constant used throughout ELF parsing operations
 * for initialization and comparison of unsigned long long integer values.
 */
#define ULLZERO 0ULL

/*
 * Define mapping from VPU data, rodata and program sections into
 * corresponding segment types.
 */

/**
 * @brief Constant pointer to ELF file image
 *
 * @details Type definition for a constant pointer to the in-memory image
 * of an ELF file. This pointer type ensures that the ELF file content
 * cannot be modified through this reference, maintaining data integrity
 * during parsing operations.
 */
typedef const void *elf_ct; /* points to const image of elf file */

/**
 * @brief ELF parser context structure
 *
 * @details This structure contains the ELF file buffer and its size,
 * providing the complete context needed for ELF parsing operations.
 * It encapsulates both the file data and metadata required for safe
 * and efficient ELF file processing.
 */
typedef struct {
	/**
	 * @brief Pointer to buffer containing ELF file data
	 * Valid value: non-null, points to valid ELF file content
	 */
	elf_ct elf_file;

	/**
	 * @brief Size of the ELF file buffer in bytes
	 * Valid range: [1 .. UINT64_MAX]
	 */
	uint64_t size;
} elf_parser_ctx;

/*--------------------------------- Types ----------------------------------*/

/**
 * @brief Unsigned 8-bit data type for ELF operations
 *
 * @details Standard 8-bit unsigned integer type used for byte-level
 * operations in ELF file parsing and processing.
 */
typedef uint8_t elfByte;

/**
 * @brief Unsigned 16-bit data type for ELF operations
 *
 * @details Standard 16-bit unsigned integer type used for half-word
 * operations in ELF file parsing, typically for counts and indices.
 */
typedef uint16_t elfHalf;

/**
 * @brief Unsigned 32-bit data type for ELF operations
 *
 * @details Standard 32-bit unsigned integer type used for word-sized
 * operations in ELF file parsing, including offsets and sizes.
 */
typedef uint32_t elfWord;

/**
 * @brief ELF address type (32-bit)
 *
 * @details 32-bit unsigned integer type specifically used for representing
 * memory addresses within ELF files, including virtual addresses and
 * entry points.
 */
typedef uint32_t elfAddr;

/**
 * @brief ELF file offset type (32-bit)
 *
 * @details 32-bit unsigned integer type specifically used for representing
 * file offsets within ELF files, including section and header offsets.
 */
typedef uint32_t elfOff;

/**
 * @brief ELF File Header structure
 *
 * @details This structure represents the standard ELF file header that appears
 * at the beginning of every ELF file. It contains essential metadata about
 * the ELF file including architecture, entry point, and header table locations.
 * The header provides the fundamental information needed to parse the rest
 * of the ELF file correctly.
 */
typedef struct {
	/**
	 * @brief ELF magic number identification
	 * Expected value: 0x7f454c46 (0x7f,'E','L','F')
	 */
	elfWord magic;

	/**
	 * @brief Object file class (32/64-bit)
	 * Valid values: PVA_ELFCLASS32 (1) for 32-bit objects
	 */
	elfByte oclass;

	/**
	 * @brief Data encoding (endianness)
	 * Valid values: 1 (little-endian), 2 (big-endian)
	 */
	elfByte data;

	/**
	 * @brief ELF object format version
	 * Valid values: PVA_EV_CURRENT (1) for current version
	 */
	elfByte formatVersion;

	/**
	 * @brief Operating system/ABI identification
	 * Valid range: [0 .. 255]
	 */
	elfByte abi;

	/**
	 * @brief ABI version number
	 * Valid range: [0 .. 255]
	 */
	elfByte abiVersion;

	/**
	 * @brief ELF identification padding bytes
	 * Must be zero-filled for standard compliance
	 */
	elfByte padd[7];

	/**
	 * @brief Object file type
	 * Valid values: ET_EXEC, ET_DYN, ET_REL, etc.
	 */
	elfHalf type;

	/**
	 * @brief Target architecture
	 * Valid values: EM_ARM, EM_X86_64, etc.
	 */
	elfHalf machine;

	/**
	 * @brief Object file version
	 * Valid values: PVA_EV_CURRENT (1) for current version
	 */
	elfWord version;

	/**
	 * @brief Entry point virtual address
	 * Valid range: [0 .. UINT32_MAX]
	 */
	elfAddr entry;

	/**
	 * @brief Program header table file offset
	 * Valid range: [0 .. file_size-1]
	 */
	elfOff phoff;

	/**
	 * @brief Section header table file offset
	 * Valid range: [0 .. file_size-1]
	 */
	elfOff shoff;

	/**
	 * @brief Processor-specific flags
	 * Valid range: [0 .. UINT32_MAX]
	 */
	elfWord flags;

	/**
	 * @brief ELF header size in bytes
	 * Expected value: sizeof(elfFileHeader)
	 */
	elfHalf ehsize;

	/**
	 * @brief Program header table entry size
	 * Valid range: [1 .. UINT16_MAX]
	 */
	elfHalf phentsize;

	/**
	 * @brief Program header table entry count
	 * Valid range: [0 .. UINT16_MAX]
	 */
	elfHalf phnum;

	/**
	 * @brief Section header table entry size
	 * Expected value: sizeof(elfSectionHeader)
	 */
	elfHalf shentsize;

	/**
	 * @brief Section header table entry count
	 * Valid range: [0 .. UINT16_MAX]
	 */
	elfHalf shnum;

	/**
	 * @brief Section header string table index
	 * Valid range: [0 .. shnum-1] or SHN_UNDEF
	 */
	elfHalf shstrndx;
} elfFileHeader;

/**
 * @brief ELF magic number in big endian format
 *
 * @details Standard ELF magic number (0x7f454c46) representing the signature
 * bytes 0x7f, 'E', 'L', 'F' in big endian byte order.
 */
#define PVA_ELFMAGIC 0x7f454c46U

/**
 * @brief ELF magic number in little endian format
 *
 * @details Standard ELF magic number representing the signature bytes
 * 0x7f, 'E', 'L', 'F' in little endian byte order (0x464c457f).
 */
#define PVA_ELFMAGIC_LSB 0x464c457fU // ELF magic number in little endian

/**
 * @brief ELF class identifier for 32-bit objects
 *
 * @details Class identifier indicating this ELF file contains 32-bit
 * objects and addresses. Used in the e_ident[EI_CLASS] field.
 */
#define PVA_ELFCLASS32 1U // 32 bit object file

/**
 * @brief Invalid ELF version identifier
 *
 * @details Version identifier indicating an invalid or unrecognized
 * ELF file version.
 */
#define PVA_EV_NONE 0 // Invalid version

/**
 * @brief Current ELF version identifier
 *
 * @details Version identifier for the current ELF file format version.
 * This is the standard version used for modern ELF files.
 */
#define PVA_EV_CURRENT 1 // Current version

/**
 * @brief ELF Section Header structure
 *
 * @details This structure represents an entry in the ELF section header table.
 * Each section header describes a section within the ELF file, including its
 * type, location, size, and attributes. Sections contain the actual program
 * code, data, symbol tables, and other information needed for linking and
 * execution.
 */
typedef struct {
	/**
	 * @brief Section name string table index
	 * Valid range: [0 .. string_table_size-1]
	 */
	elfWord name;

	/**
	 * @brief Section type identifier
	 * Valid values: SHT_NULL, SHT_PROGBITS, SHT_SYMTAB, etc.
	 */
	elfWord type;

	/**
	 * @brief Section attribute flags
	 * Valid values: SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR, etc.
	 */
	elfWord flags;

	/**
	 * @brief Section virtual address at execution
	 * Valid range: [0 .. UINT32_MAX]
	 */
	elfAddr addr;

	/**
	 * @brief Section file offset
	 * Valid range: [0 .. file_size-1]
	 */
	elfOff offset;

	/**
	 * @brief Section size in bytes
	 * Valid range: [0 .. UINT32_MAX]
	 */
	elfWord size;

	/**
	 * @brief Index of related section
	 * Valid range: [0 .. shnum-1] or special values
	 */
	elfWord link;

	/**
	 * @brief Additional section-specific information
	 * Valid range: [0 .. UINT32_MAX]
	 */
	elfWord info;

	/**
	 * @brief Section alignment constraint
	 * Valid values: 0 or positive powers of 2
	 */
	elfWord addralign;

	/**
	 * @brief Size of entries if section contains table
	 * Valid range: [0 .. UINT32_MAX]
	 */
	elfWord entsize;
} elfSectionHeader;

// Section Header Types
/**
 * @brief NULL section type (entry unused)
 *
 * @details Section header type indicating an unused section header entry.
 * This type marks inactive entries in the section header table.
 */
#define SHT_NULL 0x00U

/**
 * @brief Loadable program data section type
 *
 * @details Section header type for sections containing program code or data
 * that should be loaded into memory during program execution.
 */
#define SHT_PROGBITS 0x01U

/**
 * @brief Symbol table section type
 *
 * @details Section header type for sections containing symbol table entries
 * used for linking and debugging.
 */
#define SHT_SYMTAB 0x02U

/**
 * @brief String table section type
 *
 * @details Section header type for sections containing null-terminated
 * strings used for symbol names and section names.
 */
#define SHT_STRTAB 0x03U

/**
 * @brief Relocation table with addends section type
 *
 * @details Section header type for relocation tables that include explicit
 * addend values for relocation calculations.
 */
#define SHT_RELA 0x04U

/**
 * @brief Hash table section type
 *
 * @details Section header type for hash tables used to accelerate symbol
 * lookup operations during dynamic linking.
 */
#define SHT_HASH 0x05U

/**
 * @brief Dynamic linking information section type
 *
 * @details Section header type for sections containing information needed
 * for dynamic linking, such as shared library dependencies.
 */
#define SHT_DYNAMIC 0x06U

/**
 * @brief Note section type
 *
 * @details Section header type for sections containing auxiliary information
 * that marks or describes the file in some way.
 */
#define SHT_NOTE 0x07U

/**
 * @brief No file data section type
 *
 * @details Section header type for sections that occupy memory space but
 * contain no actual file data (e.g., .bss sections).
 */
#define SHT_NOBITS 0x08U

/**
 * @brief Relocation table without addends section type
 *
 * @details Section header type for relocation tables where addend values
 * are stored in the location being relocated.
 */
#define SHT_REL 0x09U

/**
 * @brief Reserved section type
 *
 * @details Reserved section header type for future use. Should not be
 * used in current ELF files.
 */
#define SHT_SHLIB 0x0aU

/**
 * @brief Dynamic linker symbol table section type
 *
 * @details Section header type for symbol tables used specifically by
 * the dynamic linker during runtime linking operations.
 */
#define SHT_DYNSYM 0x0bU

/**
 * @brief Initialization function pointer array section type
 *
 * @details Section header type for arrays containing pointers to
 * initialization functions called before main().
 */
#define SHT_INIT_ARRAY 0x0eU

/**
 * @brief Finalization function pointer array section type
 *
 * @details Section header type for arrays containing pointers to
 * finalization functions called after main() or at program termination.
 */
#define SHT_FINI_ARRAY 0x0fU

/**
 * @brief Pre-initialization function pointer array section type
 *
 * @details Section header type for arrays containing pointers to
 * pre-initialization functions called before any initialization functions.
 */
#define SHT_PREINIT_ARRAY 0x10U

/**
 * @brief Section group section type
 *
 * @details Section header type for sections that define section groups
 * for COMDAT (common data) handling.
 */
#define SHT_GROUP 0x11U

/**
 * @brief Extended symbol table index section type
 *
 * @details Section header type for sections containing extended symbol
 * table indices for large symbol tables.
 */
#define SHT_SYMTAB_SHNDX 0x12U

/**
 * @brief Start of OS-specific section types
 *
 * @details Lower bound for operating system-specific section header types.
 * Values in range [SHT_LOOS .. SHT_HIOS] are reserved for OS-specific use.
 */
#define SHT_LOOS 0x60000000U

/**
 * @brief End of OS-specific section types
 *
 * @details Upper bound for operating system-specific section header types.
 * Values in range [SHT_LOOS .. SHT_HIOS] are reserved for OS-specific use.
 */
#define SHT_HIOS 0x6fffffffU

/**
 * @brief Start of processor-specific section types
 *
 * @details Lower bound for processor-specific section header types.
 * Values in range [SHT_LOPROC .. SHT_HIPROC] are reserved for processor-specific use.
 */
#define SHT_LOPROC 0x70000000U

/**
 * @brief End of processor-specific section types
 *
 * @details Upper bound for processor-specific section header types.
 * Values in range [SHT_LOPROC .. SHT_HIPROC] are reserved for processor-specific use.
 */
#define SHT_HIPROC 0x7fffffffU

/**
 * @brief Start of application-specific section types
 *
 * @details Lower bound for application-specific section header types.
 * Values in range [SHT_LOUSER .. SHT_HIUSER] are available for application use.
 */
#define SHT_LOUSER 0x80000000U

/**
 * @brief End of application-specific section types
 *
 * @details Upper bound for application-specific section header types.
 * Values in range [SHT_LOUSER .. SHT_HIUSER] are available for application use.
 */
#define SHT_HIUSER 0x8fffffffU

// Special section indices
/**
 * @brief Undefined section index
 *
 * @details Special section index value indicating an undefined, missing,
 * irrelevant, or meaningless section reference.
 */
#define SHN_UNDEF 0U

/**
 * @brief Lower bound of reserved section indices
 *
 * @details Section indices in range [SHN_LORESERVE .. 0xffff] are reserved
 * for special purposes and do not reference normal sections.
 */
#define SHN_LORESERVE 0xff00U

/**
 * @brief Absolute symbol section index
 *
 * @details Special section index indicating that the associated symbol
 * has an absolute value that is not affected by relocation.
 */
#define SHN_ABS 0xfff1U

/**
 * @brief Common symbol section index
 *
 * @details Special section index indicating that the associated symbol
 * labels a common block that has not yet been allocated.
 */
#define SHN_COMMON 0xfff2U

/**
 * @brief Extended section index indicator
 *
 * @details Special section index indicating that the actual section index
 * is stored in the SHT_SYMTAB_SHNDX section.
 */
#define SHN_XINDEX 0xffffU

// Special section names
/**
 * @brief Section header string table section name
 *
 * @details Standard name for the section containing section header names.
 * This section contains null-terminated strings used as section names.
 */
#define SHNAME_SHSTRTAB ".shstrtab"

/**
 * @brief String table section name
 *
 * @details Standard name for the main string table section containing
 * symbol names and other string data.
 */
#define SHNAME_STRTAB ".strtab"

/**
 * @brief Symbol table section name
 *
 * @details Standard name for the main symbol table section containing
 * symbol table entries for linking and debugging.
 */
#define SHNAME_SYMTAB ".symtab"

/**
 * @brief Extended symbol table index section name
 *
 * @details Standard name for sections containing extended symbol table
 * indices for large symbol tables.
 */
#define SHNAME_SYMTAB_SHNDX ".symtab_shndx"

/**
 * @brief Text section name prefix
 *
 * @details Standard prefix for text (code) section names. The full name
 * typically includes a suffix with the entry point name.
 */
#define SHNAME_TEXT ".text."

/**
 * @brief ELF Symbol table entry structure
 *
 * @details This structure represents an entry in the ELF symbol table.
 * Symbol table entries describe symbols (functions, variables, etc.) within
 * the ELF file, including their names, values, sizes, and binding attributes.
 * The symbol table is essential for linking and debugging operations.
 */
typedef struct {
	/**
	 * @brief Symbol name string table index
	 * Valid range: [0 .. string_table_size-1]
	 */
	elfWord name;

	/**
	 * @brief Symbol value (address or constant)
	 * Valid range: [0 .. UINT32_MAX]
	 */
	elfAddr value;

	/**
	 * @brief Symbol size in bytes
	 * Valid range: [0 .. UINT32_MAX]
	 */
	elfWord size;

	/**
	 * @brief Symbol type and binding attributes
	 * Bits 0-3: symbol type, Bits 4-7: symbol binding
	 */
	elfByte info;

	/**
	 * @brief Extra symbol flags
	 * Valid range: [0 .. 255]
	 */
	elfByte other;

	/**
	 * @brief Associated section header index
	 * Valid range: [0 .. shnum-1] or special values (SHN_*)
	 */
	elfHalf shndx;
} elfSymbol;

/* CERT DCL37-C: Avoid reserved identifiers starting with ELF */
/**
 * @brief Extract symbol binding from info field
 *
 * @details Macro to extract the binding information (bits 4-7) from the
 * symbol's info field. Binding indicates the symbol's visibility and linkage.
 *
 * @param[in] s Pointer to @ref elfSymbol structure
 *              Valid value: non-null pointer to valid symbol
 *
 * @retval elfWord Symbol binding value (STB_LOCAL, STB_GLOBAL, STB_WEAK)
 */
#define PVA_ELF_ST_BIND(s) ((elfWord)((s)->info) >> 4)

/**
 * @brief Extract symbol type from info field
 *
 * @details Macro to extract the type information (bits 0-3) from the
 * symbol's info field. Type indicates what the symbol represents.
 *
 * @param[in] s Pointer to @ref elfSymbol structure
 *              Valid value: non-null pointer to valid symbol
 *
 * @retval elfWord Symbol type value (STT_NOTYPE, STT_OBJECT, STT_FUNC, etc.)
 */
#define PVA_ELF_ST_TYPE(s) ((elfWord)((s)->info) & 0xfU)

// ELF symbol types
/**
 * @brief Symbol type: No type specified
 *
 * @details Symbol type indicating that the symbol's type is not specified
 * or not known.
 */
#define STT_NOTYPE 0U

/**
 * @brief Symbol type: Data object
 *
 * @details Symbol type indicating that the symbol represents a data object
 * such as a variable or array.
 */
#define STT_OBJECT 1U

/**
 * @brief Symbol type: Function or executable code
 *
 * @details Symbol type indicating that the symbol represents a function
 * or other executable code.
 */
#define STT_FUNC 2U

/**
 * @brief Symbol type: Section reference
 *
 * @details Symbol type indicating that the symbol is associated with a
 * section, typically used for relocation purposes.
 */
#define STT_SECTION 3U

/**
 * @brief Symbol type: Source file name
 *
 * @details Symbol type indicating that the symbol provides the name of
 * the source file associated with the object file.
 */
#define STT_FILE 4U

/**
 * @brief Symbol type: Common data object
 *
 * @details Symbol type indicating that the symbol represents a common
 * symbol (uninitialized data that may be merged with other common symbols).
 */
#define STT_COMMON 5U

/**
 * @brief Symbol type: Start of OS-specific types
 *
 * @details Lower bound for operating system-specific symbol types.
 * Values >= STT_LOOS are reserved for OS-specific symbol types.
 */
#define STT_LOOS 10U

// ELF symbol binding (scope)
/**
 * @brief Symbol binding: Local scope
 *
 * @details Symbol binding indicating that the symbol is not visible
 * outside the object file containing its definition.
 */
#define STB_LOCAL 0U

/**
 * @brief Symbol binding: Global scope
 *
 * @details Symbol binding indicating that the symbol is visible to all
 * object files being combined and can satisfy undefined references.
 */
#define STB_GLOBAL 1U

/**
 * @brief Symbol binding: Weak symbol
 *
 * @details Symbol binding indicating that the symbol is a weak symbol
 * that can be overridden by global symbols of the same name.
 */
#define STB_WEAK 2U

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
 * @brief Check if buffer contains a valid 32-bit ELF file
 *
 * @details This function validates that the buffer contains a valid 32-bit ELF
 * file by checking the ELF magic number in the first 4 bytes and verifying
 * that the file class indicates a 32-bit object. The function performs basic
 * format validation to ensure the buffer can be safely processed as an ELF file.
 *
 * @param[in] e ELF context containing complete ELF file in a const buffer
 *              Valid value: non-null pointer to valid ELF file data
 *
 * @retval true  Valid 32-bit ELF file with correct magic number
 * @retval false Invalid ELF file, wrong class, or incorrect magic number
 */
bool elf_header_check(const elf_ct e);

/**
 * @brief Get number of sections in ELF section header table
 *
 * @details This function returns the number of sections in the ELF section
 * header table. It reads the ELF file header and extracts the section count,
 * handling the case where the section count is stored in the first (empty)
 * section header for large files. Returns zero if the section header is
 * invalid or not accessible.
 *
 * @param[in] e ELF context containing complete ELF file in a const buffer
 *              Valid value: valid @ref elf_parser_ctx with non-null elf_file
 *
 * @retval elfWord Number of sections in the section header table
 *                 Valid range: [0 .. UINT16_MAX] or extended count
 */
elfWord elf_shnum(const elf_parser_ctx e);

/**
 * @brief Validate all sections in the ELF file
 *
 * @details This function performs comprehensive validation of all sections
 * in the ELF file. It checks each section for valid offset (within file
 * bounds), non-zero section size for non-empty sections, and ensures that
 * offset + section size does not exceed file boundaries. This validation
 * is essential for safe ELF parsing and prevents buffer overruns.
 *
 * @param[in] e ELF context containing complete ELF file in a const buffer
 *              Valid value: valid @ref elf_parser_ctx with non-null elf_file
 *
 * @retval true  All sections are valid and within file bounds
 * @retval false One or more sections are invalid or out of bounds
 */
bool elf_has_valid_sections(const elf_parser_ctx e);

/**
 * @brief Get ELF section header by index
 *
 * @details This function retrieves a section header from the ELF section
 * header table at the specified index. It validates the index against the
 * section count and returns a pointer to the section header if valid.
 * The function ensures safe access to section headers and prevents
 * out-of-bounds access.
 *
 * @param[in] e     ELF context containing complete ELF file in a const buffer
 *                  Valid value: valid @ref elf_parser_ctx with non-null elf_file
 * @param[in] index Section header index to retrieve
 *                  Valid range: [0 .. elf_shnum(e)-1]
 *
 * @retval non-NULL Valid pointer to @ref elfSectionHeader at specified index
 * @retval NULL     Invalid index or section header not accessible
 */
const elfSectionHeader *elf_section_header(const elf_parser_ctx e,
					   unsigned int index);

/**
 * @brief Get section name from section header
 *
 * @details This function retrieves the name of a section by looking up the
 * name string in the section header string table. It uses the name index
 * from the section header to locate the corresponding null-terminated string
 * in the string table. The function handles string table validation and
 * bounds checking.
 *
 * @param[in] e   ELF context containing complete ELF file in a const buffer
 *                Valid value: valid @ref elf_parser_ctx with non-null elf_file
 * @param[in] esh Valid section header whose name is requested
 *                Valid value: non-null pointer to valid @ref elfSectionHeader
 *
 * @retval non-NULL Null-terminated string containing section name
 * @retval NULL     Invalid section header or name index out of bounds
 */
const char *elf_section_name(const elf_parser_ctx e,
			     const elfSectionHeader *esh);

/**
 * @brief Find section header by name
 *
 * @details This function searches the ELF section header table for a section
 * with the specified name. It iterates through all sections, comparing each
 * section's name with the requested name string. The comparison is performed
 * using string matching on the section names retrieved from the string table.
 *
 * @param[in] e    ELF context containing complete ELF file in a const buffer
 *                 Valid value: valid @ref elf_parser_ctx with non-null elf_file
 * @param[in] name Null-terminated string containing section name to find
 *                 Valid value: non-null pointer to valid string
 *
 * @retval non-NULL Pointer to @ref elfSectionHeader with matching name
 * @retval NULL     Section with specified name not found or invalid parameters
 */
const elfSectionHeader *elf_named_section_header(const elf_parser_ctx e,
						 const char *name);

/**
 * @brief Get section contents as byte pointer
 *
 * @details This function returns a pointer to the contents of the specified
 * section within the ELF file buffer. It calculates the section's location
 * using the section header's offset and provides direct access to the
 * section data. The returned pointer allows reading the raw section contents.
 *
 * @param[in] e   ELF context containing complete ELF file in a const buffer
 *                Valid value: valid @ref elf_parser_ctx with non-null elf_file
 * @param[in] esh Valid section header for the desired section
 *                Valid value: non-null pointer to valid @ref elfSectionHeader
 *
 * @retval non-NULL Pointer to section contents within ELF file buffer
 * @retval NULL     Invalid parameters or section offset out of bounds
 */
const elfByte *elf_section_contents(const elf_parser_ctx e,
				    const elfSectionHeader *esh);

/**
 * @brief Get ELF symbol by index from symbol table
 *
 * @details This function retrieves a symbol entry from the ELF symbol table
 * at the specified index. It locates the symbol table section (SHT_SYMTAB),
 * validates the index against the symbol table size, ensures proper alignment,
 * and returns a pointer to the symbol entry. The function performs bounds
 * checking to prevent invalid memory access.
 *
 * @param[in] e     ELF context containing complete ELF file in a const buffer
 *                  Valid value: valid @ref elf_parser_ctx with non-null elf_file
 * @param[in] index Symbol index within the symbol table
 *                  Valid range: [0 .. (symbol_table_size / sizeof(elfSymbol))-1]
 *
 * @retval non-NULL Pointer to @ref elfSymbol at specified index
 * @retval NULL     Invalid index, no symbol table, or symbol not found
 */
const elfSymbol *elf_symbol(const elf_parser_ctx e, unsigned int index);

/**
 * @brief Get symbol name from symbol table section
 *
 * @details This function retrieves the name of a symbol from the specified
 * symbol table section. It validates the symbol table section header and
 * entry size, checks the index bounds, ensures proper alignment, and looks
 * up the symbol name in the associated string table. The function handles
 * string table validation and bounds checking.
 *
 * @param[in] e     ELF context containing complete ELF file in a const buffer
 *                  Valid value: valid @ref elf_parser_ctx with non-null elf_file
 * @param[in] esh   Pointer to symbol table section header
 *                  Valid value: non-null pointer to valid SHT_SYMTAB section
 * @param[in] index Symbol index within the specified symbol table section
 *                  Valid range: [0 .. (section_size / entsize)-1]
 *
 * @retval non-NULL Null-terminated string containing symbol name
 * @retval NULL     Invalid parameters, index out of bounds, or name not found
 */
const char *elf_symbol_name(const elf_parser_ctx e, const elfSectionHeader *esh,
			    unsigned int index);

#endif // PVA_KMD_SILICON_ELF_PARSER_H
