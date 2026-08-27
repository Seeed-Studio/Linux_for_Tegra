#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# A depmod wrapper

if test $# -ne 1; then
	echo "Usage: $0 <kernelrelease>" >&2
	exit 1
fi

KERNELRELEASE=$1

: ${DEPMOD:=depmod}

if ! test -r System.map ; then
	echo "Warning: modules_install: missing 'System.map' file. Skipping depmod." >&2
	exit 0
fi

# legacy behavior: "depmod" in /sbin, no /sbin in PATH
PATH="$PATH:/sbin"
if [ -z $(command -v $DEPMOD) ]; then
	echo "Warning: 'make modules_install' requires $DEPMOD. Please install it." >&2
	echo "This is probably in the kmod package." >&2
	exit 0
fi

set -- -ae -F System.map
if test -n "$INSTALL_MOD_PATH"; then
	set -- "$@" -b "$INSTALL_MOD_PATH"
fi
depmod_executed=false
exit_status=0
for config_file in $INSTALL_MOD_PATH/nv-disp-module-configs/nv-depmod-*-display.conf
do
	if test -f $config_file; then
		config_file_name=$(basename -- "$config_file")
		flavor=$(echo ${config_file_name} | cut -d'-' -f3)

		"$DEPMOD" "$@" "$KERNELRELEASE"  -C "$config_file"

		exit_status=$?
		if [ "$exit_status" -ne 0 ]; then
			break
		fi

		cp "$INSTALL_MOD_PATH/lib/modules/$KERNELRELEASE/modules.dep" "$INSTALL_MOD_PATH/nv-disp-module-configs/$flavor-modules.dep"
		cp "$INSTALL_MOD_PATH/lib/modules/$KERNELRELEASE/modules.dep.bin" "$INSTALL_MOD_PATH/nv-disp-module-configs/$flavor-modules.dep.bin"
		depmod_executed=true
	fi
done

if [ "$exit_status" -eq 0 ] && [ "$depmod_executed" = false ]; then
	exec "$DEPMOD" "$@" "$KERNELRELEASE"
fi
