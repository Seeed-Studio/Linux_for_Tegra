#!/bin/bash

# Build a side-by-side PREEMPT_RT kernel bundle for an L4T root filesystem.
#
# The bundle deliberately has a different package name from
# nvidia-l4t-kernel.  It installs Image.real-time and a separate module tree,
# so the stock kernel remains available as the "primary" extlinux entry.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${0}")" && pwd)"
BSP_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ROOTFS_DIR="${ROOTFS_DIR:-${BSP_DIR}/rootfs}"
KERNEL_OUT="${KERNEL_OUT:-${BSP_DIR}/source/kernel_out}"
OUT_DIR="${OUT_DIR:-${BSP_DIR}/kernel}"
MAINTAINER="${MAINTAINER:-L4T RT builder <root@localhost>}"
RT_PACKAGE_NAME="${RT_PACKAGE_NAME:-nvidia-l4t-kernel-rt}"
RT_PACKAGE_VERSION="${RT_PACKAGE_VERSION:-}"
BASE_KERNEL_DEB="${BASE_KERNEL_DEB:-}"

usage()
{
	cat <<EOF
Usage: ${0##*/}

Environment overrides:
  ROOTFS_DIR            rootfs containing /lib/modules/<rt-release>
  KERNEL_OUT            nvbuild.sh output directory
  OUT_DIR               directory for the resulting .deb
  RT_IMAGE              RT kernel Image to install as /boot/Image.real-time
  RT_MODULES            directory containing the RT module tree
  BASE_KERNEL_DEB       stock nvidia-l4t-kernel package used for dependency
  RT_PACKAGE_VERSION    Debian package version (default: <rt-release>+rt1)
  MAINTAINER            Debian maintainer string
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
	usage
	exit 0
fi

RELEASE_FILE="${KERNEL_OUT}/kernel/kernel-noble/include/config/kernel.release"
if [[ ! -r "${RELEASE_FILE}" ]]; then
	echo "ERROR: ${RELEASE_FILE} not found. Build with ./nvbuild.sh -r first."
	exit 1
fi
KERNEL_RELEASE="$(tr -d '[:space:]' < "${RELEASE_FILE}")"
if [[ "${KERNEL_RELEASE}" != *-rt-tegra ]]; then
	echo "ERROR: ${KERNEL_RELEASE} is not an RT release"
	exit 1
fi

RT_IMAGE="${RT_IMAGE:-}"
if [[ -z "${RT_IMAGE}" ]]; then
	RT_IMAGE="${KERNEL_OUT}/kernel/kernel-noble/arch/arm64/boot/Image"
	if [[ ! -f "${RT_IMAGE}" ]]; then
		RT_IMAGE="${ROOTFS_DIR}/boot/Image.real-time"
	fi
	if [[ ! -f "${RT_IMAGE}" ]]; then
		RT_IMAGE="${ROOTFS_DIR}/boot/Image"
	fi
fi
if [[ ! -f "${RT_IMAGE}" ]]; then
	echo "ERROR: RT Image not found"
	exit 1
fi
IMAGE_VERSION="$(strings "${RT_IMAGE}" | grep -m1 -oE 'Linux version [^ ]+' || true)"
if [[ "${IMAGE_VERSION}" != "Linux version ${KERNEL_RELEASE}" ]]; then
	echo "ERROR: ${RT_IMAGE} does not contain Linux version ${KERNEL_RELEASE}"
	exit 1
fi

RT_MODULES="${RT_MODULES:-${ROOTFS_DIR}/lib/modules/${KERNEL_RELEASE}}"
if [[ ! -d "${RT_MODULES}" ]]; then
	echo "ERROR: ${RT_MODULES} not found. Run nvbuild.sh -r -i with INSTALL_MOD_PATH first."
	exit 1
fi

if [[ -z "${RT_PACKAGE_VERSION}" ]]; then
	RT_PACKAGE_VERSION="${KERNEL_RELEASE}+rt1"
fi

# JP7.2/R39.2.0 kernel release format: 6.8.12-1021-rt-tegra (SRU number from
# debian.nvidia-tegra/changelog); generic counterpart: 6.8.12-1021-tegra.
GENERIC_RELEASE="${KERNEL_RELEASE%-rt-tegra}-tegra"
if [[ -z "${BASE_KERNEL_DEB}" ]]; then
	base_kernel_debs=("${BSP_DIR}"/kernel/nvidia-l4t-kernel_*.deb)
	if [[ ${#base_kernel_debs[@]} -eq 1 && -f "${base_kernel_debs[0]}" ]]; then
		BASE_KERNEL_DEB="${base_kernel_debs[0]}"
	fi
fi
BASE_KERNEL_DEPENDS=""
if [[ -n "${BASE_KERNEL_DEB}" ]]; then
	if [[ ! -f "${BASE_KERNEL_DEB}" ]]; then
		echo "ERROR: BASE_KERNEL_DEB does not exist: ${BASE_KERNEL_DEB}"
		exit 1
	fi
	BASE_KERNEL_VERSION="$(dpkg-deb -f "${BASE_KERNEL_DEB}" Version)"
	BASE_KERNEL_DEPENDS=", nvidia-l4t-kernel (= ${BASE_KERNEL_VERSION})"
fi

WORK_DIR="$(mktemp -d)"
cleanup()
{
	rm -rf "${WORK_DIR}"
}
trap cleanup EXIT

mkdir -p "${WORK_DIR}/boot" "${WORK_DIR}/lib/modules" "${WORK_DIR}/DEBIAN"
install -D -m 0644 "${RT_IMAGE}" "${WORK_DIR}/boot/Image.real-time"

# Do not package the host-side absolute build/source symlinks emitted by
# modules_install.  They are not required at runtime and would be invalid on
# the target.  A separate headers package can add valid links later.
rsync -a --exclude='/build' --exclude='/source' \
	"${RT_MODULES}/" "${WORK_DIR}/lib/modules/${KERNEL_RELEASE}/"

INSTALLED_SIZE="$(du -sk "${WORK_DIR}" | awk '{print $1}')"
cat > "${WORK_DIR}/DEBIAN/control" <<EOF
Package: ${RT_PACKAGE_NAME}
Version: ${RT_PACKAGE_VERSION}
Section: kernel
Priority: optional
Architecture: arm64
Maintainer: ${MAINTAINER}
Depends: kmod, nvidia-l4t-initrd${BASE_KERNEL_DEPENDS}
Installed-Size: ${INSTALLED_SIZE}
Description: PREEMPT_RT kernel bundle for NVIDIA L4T
 This package installs the PREEMPT_RT kernel as /boot/Image.real-time and
 keeps the generic NVIDIA kernel package intact for fallback booting.
EOF

cat > "${WORK_DIR}/DEBIAN/preinst" <<EOF
#!/bin/sh
set -e

EXPECTED_GENERIC_RELEASE='${GENERIC_RELEASE}'

case "\${1:-}" in
    install|upgrade)
        if [ ! -f /boot/Image ]; then
            echo "ERROR: /boot/Image is missing; no generic fallback kernel is available." >&2
            exit 1
        fi
        image_release="\$(grep -a -m 1 -oE 'Linux version [^ ]+' /boot/Image | sed 's/^Linux version //')"
        if [ "\${image_release}" != "\${EXPECTED_GENERIC_RELEASE}" ]; then
            echo "ERROR: /boot/Image is \${image_release:-unknown}, expected \${EXPECTED_GENERIC_RELEASE}." >&2
            echo "Restore the stock generic kernel before installing the RT bundle." >&2
            exit 1
        fi
        if [ ! -d "/lib/modules/\${EXPECTED_GENERIC_RELEASE}" ]; then
            echo "ERROR: /lib/modules/\${EXPECTED_GENERIC_RELEASE} is missing." >&2
            exit 1
        fi
        ;;
esac
exit 0
EOF

cat > "${WORK_DIR}/DEBIAN/postinst" <<EOF
#!/bin/sh
set -e

KERNEL_RELEASE='${KERNEL_RELEASE}'

# L4T 36.4.3's nv-update-initrd deletes its file list inside the per-image
# loop, so only the first image (/boot/Image) gets its modules copied into
# the initrd; the RT tree is silently skipped.  R39.2.0 reworks the loop and
# is not affected; the guard below simply does not match there.
if [ -f /usr/sbin/nv-update-initrd ] && grep -q 'copy_files_initrd "\${DESTDIR}" "\${temp_list}" "\${_kernel_version}"' /usr/sbin/nv-update-initrd 2>/dev/null; then
    sed -i '/copy_files_initrd "\${DESTDIR}" "\${temp_list}" "\${_kernel_version}"/{n;d}' /usr/sbin/nv-update-initrd
fi

if [ "\${1:-}" = configure ]; then
    depmod -a "\${KERNEL_RELEASE}"
    if [ -x /usr/sbin/nv-update-initrd ]; then
        /usr/sbin/nv-update-initrd
    fi
    if [ -x /usr/sbin/nv-update-extlinux ]; then
        /usr/sbin/nv-update-extlinux real-time -a net.ifnames=0
    fi
fi
exit 0
EOF

cat > "${WORK_DIR}/DEBIAN/postrm" <<EOF
#!/bin/sh
set -e

KERNEL_RELEASE='${KERNEL_RELEASE}'

if [ "\${1:-}" = remove ] || [ "\${1:-}" = purge ]; then
    # Select the stock entry before removing the RT image.  The RT entry is
    # intentionally left in extlinux.conf as a harmless stale menu entry;
    # it can be removed manually after verifying the generic kernel boots.
    if [ -x /usr/sbin/nv-update-extlinux ]; then
        /usr/sbin/nv-update-extlinux generic -a net.ifnames=0 || true
    fi
    depmod -a "\${KERNEL_RELEASE}" || true
    if [ -x /usr/sbin/nv-update-initrd ]; then
        /usr/sbin/nv-update-initrd || true
    fi
fi
exit 0
EOF
chmod 0755 "${WORK_DIR}/DEBIAN/preinst" "${WORK_DIR}/DEBIAN/postinst" \
	"${WORK_DIR}/DEBIAN/postrm"

mkdir -p "${OUT_DIR}"
OUTPUT="${OUT_DIR}/${RT_PACKAGE_NAME}_${RT_PACKAGE_VERSION}_arm64.deb"
if command -v fakeroot >/dev/null 2>&1; then
	fakeroot dpkg-deb --build "${WORK_DIR}" "${OUTPUT}" >/dev/null
else
	dpkg-deb --build --root-owner-group "${WORK_DIR}" "${OUTPUT}" >/dev/null
fi

echo "Created: ${OUTPUT}"
dpkg-deb -f "${OUTPUT}" Package Version Depends
echo "Kernel release: ${KERNEL_RELEASE}"
