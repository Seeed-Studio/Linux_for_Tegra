#!/bin/bash

# Build L4T deb packages for deb-based OTA upgrade.
# Must run after nvbuild.sh and do_copy.sh.
#
# Usage: ./build_debs.sh [OPTIONS]
#   -v, --version SUFFIX   Version suffix (default: timestamp)
#   -o, --output DIR       Output directory (default: ../ota_packages)
#   -h, --help             Show help

set -e

SCRIPT_DIR="$(cd "$(dirname "$(readlink -f "${0}")")" && pwd)"
source "${SCRIPT_DIR}/kernel_src_build_env.sh"

L4T_DIR="${SCRIPT_DIR}/.."
KERNEL_OUT_DIR="${KERNEL_OUT_DIR:=${SCRIPT_DIR}/kernel_out}"
TEMPLATE_DIR="${SCRIPT_DIR}/deb_templates"
SEEED_SUFFIX="$(date +%Y%m%d%H%M%S)"
OUTPUT_DIR=""

L4T_VERSION="36.5.0"

usage() {
	echo "Usage: ./build_debs.sh [OPTIONS]"
	echo "  -v, --version SUFFIX   Version suffix (default: timestamp)"
	echo "  -o, --output DIR       Output directory (default: ../ota_packages)"
	echo "  -h, --help             Show help"
	exit 0
}

parse_args() {
	while [ $# -gt 0 ]; do
		case "${1}" in
			-v|--version) SEEED_SUFFIX="${2}"; shift 2 ;;
			-o|--output) OUTPUT_DIR="${2}"; shift 2 ;;
			-h|--help) usage ;;
			*) echo "Error: Invalid option ${1}"; usage ;;
		esac
	done
}

check_env() {
	if [ -z "${CROSS_COMPILE}" ]; then
		echo "Error: CROSS_COMPILE env variable is not set"
		echo "Example: export CROSS_COMPILE=\$(pwd)/../l4t-gcc/aarch64--glibc--stable-2022.08-1/bin/aarch64-buildroot-linux-gnu-"
		exit 1
	fi
	if [ ! -f "${CROSS_COMPILE}gcc" ]; then
		echo "Error: Cross compiler not found at ${CROSS_COMPILE}gcc"
		exit 1
	fi
	if [ ! -f "${KERNEL_OUT_DIR}/kernel/${KERNEL_SRC_DIR}/arch/arm64/boot/Image" ]; then
		echo "Error: Kernel image not found. Run nvbuild.sh first."
		exit 1
	fi
	if [ ! -d "${KERNEL_OUT_DIR}/kernel-devicetree/generic-dts/dtbs" ]; then
		echo "Error: DTBs not found. Run nvbuild.sh first."
		exit 1
	fi
}

get_kernel_version() {
	KERNEL_VERSION="$(cat "${KERNEL_OUT_DIR}/kernel/${KERNEL_SRC_DIR}/include/config/kernel.release")"
	if [ -z "${KERNEL_VERSION}" ]; then
		echo "Error: Cannot determine kernel version"
		exit 1
	fi
	echo "Kernel version: ${KERNEL_VERSION}"
}

get_installed_size() {
	du -s "$1" 2>/dev/null | cut -f1 || echo "0"
}

build_package() {
	local pkg_name="$1"
	local staging_template="${TEMPLATE_DIR}/${pkg_name}"

	if [ ! -d "${staging_template}" ]; then
		echo "Error: Template directory not found for ${pkg_name}"
		return 1
	fi

	# Use kernel version prefix for kernel-related packages (NVIDIA convention)
	local ver="${PKG_VERSION}"
	case "${pkg_name}" in
		nvidia-l4t-kernel|nvidia-l4t-kernel-*|nvidia-l4t-display-kernel)
			ver="${KERNEL_PKG_VERSION}"
			;;
	esac

	echo "========================================="
	echo "Building ${pkg_name} (version: ${ver})..."
	echo "========================================="

	local STAGING_DIR
	STAGING_DIR="$(mktemp -d)"
	trap "rm -rf '${STAGING_DIR}'" RETURN

	# DEBIAN directory
	mkdir -p "${STAGING_DIR}/DEBIAN"

	# Fill control template (use package-specific version)
	sed \
		-e "s|@VERSION@|${ver}|g" \
		-e "s|@KERNEL_VERSION@|${KERNEL_VERSION}|g" \
		-e "s|@L4T_REVISION@|${L4T_REVISION}|g" \
		-e "s|@L4T_VERSION@|${L4T_VERSION}|g" \
		-e "s|@SIZE@|$(get_installed_size "${STAGING_DIR}")|g" \
		"${staging_template}/DEBIAN/control" > "${STAGING_DIR}/DEBIAN/control"

	# Copy maintainer scripts
	for script in preinst postinst prerm postrm; do
		if [ -f "${staging_template}/DEBIAN/${script}" ]; then
			sed \
				-e "s|@VERSION@|${ver}|g" \
				-e "s|@KERNEL_VERSION@|${KERNEL_VERSION}|g" \
				-e "s|@L4T_REVISION@|${L4T_REVISION}|g" \
				-e "s|@L4T_VERSION@|${L4T_VERSION}|g" \
				-e "s|@SIZE@|$(get_installed_size "${STAGING_DIR}")|g" \
				"${staging_template}/DEBIAN/${script}" > "${STAGING_DIR}/DEBIAN/${script}"
			chmod 755 "${STAGING_DIR}/DEBIAN/${script}"
		fi
	done

		# Package-specific file population (call the function)
		if ! "populate_${pkg_name//-/_}" "${STAGING_DIR}"; then
			echo ""
			echo "========================================="
			echo "FATAL: Failed to populate ${pkg_name}"
			echo "========================================="
			echo "Check that the required build artifacts exist in kernel_out/."
			echo "Run nvbuild.sh first, then do_copy.sh."
			echo "========================================="
			exit 1
		fi

		# Update Installed-Size
		local installed_size
		installed_size="$(get_installed_size "${STAGING_DIR}")"
		sed -i "s|@SIZE@|${installed_size}|g" "${STAGING_DIR}/DEBIAN/control"

		# Build deb
		local deb_file="${OUTPUT_DIR}/${pkg_name}_${ver}_arm64.deb"
		if ! dpkg-deb --build -Zxz "${STAGING_DIR}" "${deb_file}"; then
			echo ""
			echo "========================================="
			echo "FATAL: Failed to build ${pkg_name}"
			echo "========================================="
			echo "Check DEBIAN/control for template errors."
			echo "========================================="
			exit 1
		fi

		echo "Built: ${deb_file}"
		echo ""
	}

# ---- Package population functions ----

populate_nvidia_l4t_kernel() {
	local staging="$1"
	local kernel_dir="${KERNEL_OUT_DIR}/kernel/${KERNEL_SRC_DIR}"
	local mod_dir="${KERNEL_OUT_DIR}/kernel"

	echo "  Installing kernel Image..."
	mkdir -p "${staging}/boot"
	cp "${kernel_dir}/arch/arm64/boot/Image" "${staging}/boot/Image"

	echo "  Installing in-tree kernel modules..."
	mkdir -p "${staging}/lib/modules/${KERNEL_VERSION}"
	# Install modules to a temporary root, then copy to staging
	local tmp_mod_root
	tmp_mod_root="$(mktemp -d)"
	make -C "${mod_dir}" \
		ARCH=arm64 \
		CROSS_COMPILE="${CROSS_COMPILE}" \
		INSTALL_MOD_PATH="${tmp_mod_root}" \
		INSTALL_MOD_STRIP=1 \
		modules_install 2>/dev/null || true
	# Copy only the kernel/ subdirectory (in-tree modules), skip updates/
	if [ -d "${tmp_mod_root}/lib/modules/${KERNEL_VERSION}" ]; then
		cp -a "${tmp_mod_root}/lib/modules/${KERNEL_VERSION}/kernel" \
			"${staging}/lib/modules/${KERNEL_VERSION}/"
		# Copy modules.dep and friends
		for f in modules.builtin modules.builtin.modinfo modules.order modules.dep modules.dep.bin \
			modules.alias modules.alias.bin modules.softdep modules.symbols modules.symbols.bin; do
			if [ -f "${tmp_mod_root}/lib/modules/${KERNEL_VERSION}/${f}" ]; then
				cp "${tmp_mod_root}/lib/modules/${KERNEL_VERSION}/${f}" \
					"${staging}/lib/modules/${KERNEL_VERSION}/"
			fi
		done
	fi
	rm -rf "${tmp_mod_root}"
	# Remove build/source symlinks
	rm -f "${staging}/lib/modules/${KERNEL_VERSION}/build"
	rm -f "${staging}/lib/modules/${KERNEL_VERSION}/source"
}

populate_nvidia_l4t_kernel_dtbs() {
	local staging="$1"
	local dtb_dir="${KERNEL_OUT_DIR}/kernel-devicetree/generic-dts/dtbs"

	echo "  Installing DTBs..."
	mkdir -p "${staging}/boot"
	if [ -d "${dtb_dir}" ]; then
		cp "${dtb_dir}"/*.dtb "${staging}/boot/" 2>/dev/null || true
		cp "${dtb_dir}"/*.dtbo "${staging}/boot/" 2>/dev/null || true
	fi
}

populate_nvidia_l4t_kernel_headers() {
	local staging="$1"
	local kernel_dir="${KERNEL_OUT_DIR}/kernel/${KERNEL_SRC_DIR}"
	local header_dir="${staging}/usr/src/linux-headers-${KERNEL_VERSION}"

	echo "  Installing kernel headers..."
	mkdir -p "${header_dir}"

	# Copy files needed for out-of-tree module builds
	cd "${kernel_dir}"
	# Makefile and Kconfig
	find . -maxdepth 1 \( -name 'Makefile*' -o -name 'Kconfig*' \) \
		-exec cp --parents {} "${header_dir}/" \;
	# Architecture-specific headers
	find arch/arm64/include -type f 2>/dev/null | \
		cpio -pdm "${header_dir}/" 2>/dev/null || true
	# Generic include
	find include -type f 2>/dev/null | \
		cpio -pdm "${header_dir}/" 2>/dev/null || true
	# Scripts
	find scripts -type f 2>/dev/null | \
		cpio -pdm "${header_dir}/" 2>/dev/null || true
	# Generated files
	cp -a .config "${header_dir}/" 2>/dev/null || true
	cp Module.symvers "${header_dir}/" 2>/dev/null || true
	cp System.map "${header_dir}/" 2>/dev/null || true
	# Compiled objects for module building
	find arch/arm64 -name '*.o' -path '*/built-in*' | \
		cpio -pdm "${header_dir}/" 2>/dev/null || true
	cd "${SCRIPT_DIR}"
}

populate_nvidia_l4t_kernel_oot_modules() {
	local staging="$1"
	local mod_staging="${staging}/lib/modules/${KERNEL_VERSION}/updates"

	echo "  Installing OOT modules..."
	mkdir -p "${mod_staging}"

	# Collect OOT .ko files
	for module_src in nvgpu/drivers/gpu/nvgpu nvidia-oot hwpm/drivers/tegra/hwpm; do
		local src_dir="${KERNEL_OUT_DIR}/${module_src}"
		if [ -d "${src_dir}" ]; then
			find "${src_dir}" -name '*.ko' -exec cp {} "${mod_staging}/" \;
		fi
	done

	# nvethernetrm
	if [ -d "${KERNEL_OUT_DIR}/nvethernetrm" ]; then
		find "${KERNEL_OUT_DIR}/nvethernetrm" -name '*.ko' -exec cp {} "${mod_staging}/" \;
	fi
}

populate_nvidia_l4t_kernel_oot_headers() {
	local staging="$1"
	local dest="${staging}/usr/src/nvidia"

	echo "  Installing OOT headers..."
	mkdir -p "${dest}"

	# Copy OOT include directories
	for inc_src in nvidia-oot/include hwpm/include; do
		local src_dir="${KERNEL_OUT_DIR}/${inc_src}"
		if [ -d "${src_dir}" ]; then
			cp -a "${src_dir}" "${dest}/"
		fi
	done

	# Merge Module.symvers
	cat "${KERNEL_OUT_DIR}/nvidia-oot/Module.symvers" \
		"${KERNEL_OUT_DIR}/hwpm/drivers/tegra/hwpm/Module.symvers" 2>/dev/null \
		> "${dest}/Module.symvers" || true
}

populate_nvidia_l4t_display_kernel() {
	local staging="$1"
	local mod_staging="${staging}/lib/modules/${KERNEL_VERSION}/updates/opensrc-disp"

	echo "  Installing display kernel modules..."
	mkdir -p "${mod_staging}"

	local disp_dir="${KERNEL_OUT_DIR}/nvdisplay/kernel-open"
	if [ -d "${disp_dir}" ]; then
		find "${disp_dir}" -name '*.ko' -exec cp {} "${mod_staging}/" \;
	fi
}

populate_nvidia_l4t_initrd() {
	local staging="$1"
	local initrd_src="${L4T_DIR}/bootloader/l4t_initrd.img"

	# Fallback: look for initrd in Linux_for_Tegra subdirectory
	if [ ! -f "${initrd_src}" ]; then
		initrd_src="${SCRIPT_DIR}/../Linux_for_Tegra/bootloader/l4t_initrd.img"
	fi

	if [ ! -f "${initrd_src}" ]; then
		echo ""
		echo "========================================="
		echo "FATAL: initrd not found!"
		echo "========================================="
		echo "Searched:"
		echo "  - ${L4T_DIR}/bootloader/l4t_initrd.img"
		echo "  - ${SCRIPT_DIR}/../Linux_for_Tegra/bootloader/l4t_initrd.img"
		echo ""
		echo "The initrd is required for NVMe boot. Without it the device"
		echo "will not boot after upgrade. Build aborted."
		echo "========================================="
		exit 1
	fi

	echo "  Installing initrd..."
	mkdir -p "${staging}/boot"
	cp "${initrd_src}" "${staging}/boot/initrd"
}

# ---- Main ----

parse_args "$@"
PKG_VERSION="${L4T_VERSION}-${SEEED_SUFFIX}"

if [ -z "${OUTPUT_DIR}" ]; then
	OUTPUT_DIR="${L4T_DIR}/ota_packages"
fi

echo "========================================="
echo "L4T Deb Package Builder"
echo "========================================="
echo "L4T Version:    ${L4T_VERSION}"
echo "Package Version: ${PKG_VERSION}"
echo "Kernel dir:     ${KERNEL_OUT_DIR}"
echo "Output dir:     ${OUTPUT_DIR}"
echo ""

check_env
get_kernel_version

# NVIDIA convention: kernel packages use <kernel_ver>-<l4t_ver>-<suffix>
KERNEL_PKG_VERSION="${KERNEL_VERSION}-${L4T_VERSION}-${SEEED_SUFFIX}"
# L4T revision for nv_tegra_release (e.g. "36.5.0" -> "5.0")
L4T_REVISION="$(echo "${L4T_VERSION}" | cut -d. -f2,3)"
echo "Kernel Pkg Ver:  ${KERNEL_PKG_VERSION}"

mkdir -p "${OUTPUT_DIR}"

# Build packages in order
PACKAGES=(
	nvidia-l4t-kernel
	nvidia-l4t-kernel-dtbs
	nvidia-l4t-kernel-headers
	nvidia-l4t-kernel-oot-modules
	nvidia-l4t-kernel-oot-headers
	nvidia-l4t-display-kernel
	nvidia-l4t-initrd
)

BUILT=()
SKIPPED=()

for pkg in "${PACKAGES[@]}"; do
	if build_package "${pkg}"; then
		BUILT+=("${pkg}")
	else
		SKIPPED+=("${pkg}")
	fi
done

echo "========================================="
echo "Build Summary"
echo "========================================="
echo "Built:   ${#BUILT[@]} packages"
for pkg in "${BUILT[@]}"; do echo "  - ${pkg}"; done
if [ ${#SKIPPED[@]} -gt 0 ]; then
	echo "Skipped: ${#SKIPPED[@]} packages"
	for pkg in "${SKIPPED[@]}"; do echo "  - ${pkg}"; done
fi
echo ""
echo "Output: ${OUTPUT_DIR}/"
ls -lh "${OUTPUT_DIR}"/*.deb 2>/dev/null || echo "No deb files produced"
