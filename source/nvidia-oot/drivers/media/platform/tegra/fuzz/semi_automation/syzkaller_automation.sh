# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#!/bin/bash

# Display help message with usage instructions and available options
show_help() {
    echo "Shell Script to syzkaller automation"
    echo "Usage:"
    echo "  source ./syzkaller_automation.sh [options]"
    echo ""
    echo "Options:"
    echo "  --fuzz_module         test mode: all cdi_mgr.txt cdi_dev.txt capture_vi_channel.txt"
    echo "                                   capture_isp_channel.txt cam_fsync.txt"
    echo "  --tegra_top           project directory"
    echo "  --colossus_ip         Colossus ip address"
    echo "  --tegra_ip            Tegra ip address"
    echo "  --mode MODE           Automation Mode valid mode: semi, full (default:semi)"
    echo "  -h, --help            Show this help message"
    echo ""
    echo "Examples:"
    echo "  source ./syzkaller_automation.sh \\"
    echo "    --fuzz_module all \\"
    echo "    --tegra_top /home/xx/automotive-dev-main-xxx \\"
    echo "    --colossus_ip 10.176.197.155 \\"
    echo "    --tegra_ip 192.168.1.141 \\"
    echo "    --mode full"
}

# Display confirmation message and wait for user input
# Returns: 0 on success (Y), 1 on failure (N or invalid input)
confirm_message() {
    RED='\033[0;31m'
    NC='\033[0m' # No Color
    msg="*********************************************************************************\n"
    msg+="*                                                                               *\n"
    msg+="*                                please    confirm                              *\n"
    msg+="*                                                                               *\n"
    msg+="*********************************************************************************"
    echo -e "${RED}${msg}${NC}"
    local patch_base="https://git-master.nvidia.com/r/c/"
    echo -e "1. When building the system image, please ensure\n" \
          "  that the following two changes are included:\n" \
          "  ${patch_base}linux-stable/+/3326676\n" \
          "  ${patch_base}device/hardware/nvidia/platform/t23x/auto-pct/+/3207558\n"\
          "  Recompile the foundation and linux none debug, and flash the board.\n"
    echo -e "2. Please update the configs file. <sshkey>,<user>,<repo-path> and module_obj path.\n"\
          "  sshkey: /home/user/.ssh/id_rsa\n" \
          "  repo-path:automotive-dev-main-20250310T181236003\n"
    echo -e "3. Please add LocalForward 50000 127.0.0.1:50000 to the ~/.ssh/config file.\n"
    echo -e "Please confirm whether the above operations have been completed: (y/N)"

    read -r user_input

    if [[ "$user_input" =~ ^[Yy]$ ]]; then
        echo "continue syzkaller automation...."
    elif [[ "$user_input" =~ ^[Nn]$ ]]; then
        echo "exit syzkaller automation...."
        return 1
    else
        echo "Invalid input. Please enter Y or N."
        return 1
    fi
}

# Setup syzkaller environment
# Checks installation, creates directories, and configures toolchain
# Returns: 0 on success, 1 on failure
syzkaller_setup() {
    echo "check syzkaller installation"
    if [ -d "$SYZKALLER_DIR" ]; then
        echo "syzkaller directory exists:${SYZKALLER_DIR}"
        if [ -d "$SYZKALLER_DIR/syzkaller" ]; then
            echo "syzkaller already installation"
        else
            echo "syzkaller directory not found:${SYZKALLER_DIR}/syzkaller"
            syzkaller_installation
        fi
    else
        echo "directory not found:${SYZKALLER_DIR}"
        mkdir -p "$SYZKALLER_DIR"
        syzkaller_installation
    fi

    echo "check go installation"

    if [ -d "$GO_DIR" ]; then
        echo "go already installation"
    else
        echo "go directory not found:${GO_DIR}"
        mkdir -p $GO_DIR
        cd $GO_DIR
        wget https://dl.google.com/go/go1.22.2.linux-amd64.tar.gz
        tar -xf go1.22.2.linux-amd64.tar.gz
    fi

    if command -v clang >/dev/null 2>&1 && dpkg -l clang 2>/dev/null | grep -q ^ii; then
        echo "Clang already installed"
    else
        echo "need install Clang"
        sudo apt update
        sudo apt-get install -y clang
    fi

    TOOLCHAIN=/home/jenkins/p4/sw/embedded/tools/toolchains/bootlin/gcc-13.2.0
    CC=$TOOLCHAIN/aarch64--glibc--bleeding-edge-2024.02-1/bin/aarch64-buildroot-linux-gnu-gcc
    CXX=$TOOLCHAIN/aarch64--glibc--bleeding-edge-2024.02-1/bin/aarch64-buildroot-linux-gnu-g++
    export PATH=$TOOLCHAIN/aarch64--glibc--bleeding-edge-2024.02-1/bin:$PATH

    echo "check binutils"
    rm -rf ~/bin/aarch64
    mkdir -p ~/bin/aarch64

    ln -s `which aarch64-buildroot-linux-gnu-addr2line` ~/bin/aarch64/addr2line
    ln -s `which aarch64-buildroot-linux-gnu-nm` ~/bin/aarch64/nm
    ln -s `which aarch64-buildroot-linux-gnu-objdump` ~/bin/aarch64/objdump
    ln -s `which aarch64-buildroot-linux-gnu-readelf` ~/bin/aarch64/readelf
    export PATH=~/bin/aarch64:$PATH

}

# Install syzkaller by cloning from GitHub repository
# Returns: 0 on success, 1 on failure
syzkaller_installation() {
    echo "begin installation syzkaller..."
    cd "$SYZKALLER_DIR" || exit
    git clone https://gitlab-master.nvidia.com/williliu/syzkaller.git
    if [ $? -ne 0 ]; then
        echo "Error: Failed to clone the syzkaller repository"
        return 1
    fi
    echo "syzkaller installation complete"
}

# Build syzkaller
# Sets up environment variables and compiles syzkaller
# Returns: 0 on success, 1 on failure
syzkaller_build() {
    echo "begin build syzkaller..."

    if [ ! -d "$SYZKALLER_DIR/syzkaller" ]; then
        echo "Error: syzkaller directory not found: $SYZKALLER_DIR/syzkaller"
        return 1
    fi
    cd $SYZKALLER_DIR/syzkaller
    export GOROOT=$GO_DIR/go
    export PATH=$GOROOT/bin:$PATH
    export GOPATH=$SYZKALLER_DIR/syzkaller

    OS="linux"
    ARCH="arm64"
    SOURCEDIR="$TEGRA_TOP/kernel/kernel-oot"
    build_base_path="$TEGRA_TOP/out/embedded-linux-generic-debug-none/nvidia/kernel-oot/"
    BUILDDIR="$build_base_path/kernel_prod-rt_patches-nvidia-oot/linux-headers/"

    echo "syzkaller building..."

    make HOSTOS=linux HOSTARCH=amd64 TARGETOS=linux TARGETARCH=arm64 ./bin/syz-extract

    if [ ! -d "$SYZKALLER_DIR/syzkaller/sys/linux" ]; then
        echo "syzkaller/sys/linux directory not found"
        mkdir -p $SYZKALLER_DIR/syzkaller/sys/linux
    fi

    if [ "$fuzz_module" = "all" ]; then
        for module_file in $(list_tests); do
            source_file="$(get_module_file_path $module_file)"
            echo "source_file: $source_file"
            if [ ! -f "$source_file" ]; then
                echo "Error: Source file not found: $source_file"
                return 1
            else
                cp "$source_file" "$SYZKALLER_DIR/syzkaller/sys/linux"
            fi
        done

        for module_file in $(list_tests); do

            INCLUDEDIRS="$(get_module_include_path $module_file)"

            echo "run syzkaller:$SYZKALLER_DIR/syzkaller/bin/syz-extract" \
                 " -os $OS -arch $ARCH" \
                 " -sourcedir $SOURCEDIR" \
                 " -builddir $BUILDDIR" \
                 " -includedirs $INCLUDEDIRS $module_file"
            $SYZKALLER_DIR/syzkaller/bin/syz-extract \
                -os $OS \
                -arch $ARCH \
                -sourcedir $SOURCEDIR \
                -builddir $BUILDDIR \
                -includedirs $INCLUDEDIRS \
                $module_file
            if [ $? -ne 0 ]; then
                echo "Error: Failed to run syz-extract on $module_file."
                return 1
            else
                const_file="$SYZKALLER_DIR/syzkaller/sys/linux/${module_file}.const"
                if [ -f "$const_file" ]; then
                    echo "Success: Generated valid const file $const_file"
                else
                    echo "Warning: Expected const file $const_file was not generated."
                fi
            fi
        done
    fi

    make HOSTOS=linux HOSTARCH=amd64 TARGETOS=linux TARGETARCH=arm64
}

# List all available test modules
list_tests() {
  echo "$module_tests"
}

# Get include paths for specified module
# Parameters:
#   $1 - module name
# Returns: String containing include paths
get_module_include_path() {
    local module=$1
    local base_path="$TEGRA_TOP/kernel/nvidia-oot"
    case $module in
        cdi_mgr.txt)
            echo "${base_path}/include,"\
                 "${base_path}/include/uapi/asm-generic" | tr -d ' '
            ;;
        cdi_dev.txt)
            echo "${base_path}/include"
            ;;
        capture_vi_channel.txt)
            echo "${base_path},${base_path}/include,${base_path}/include/uapi,"\
                 "${TEGRA_TOP}/camera/firmware-api/kernel-include,"\
                 "${TEGRA_TOP}/core/include" | tr -d ' '
            ;;
        capture_isp_channel.txt)
            echo "${base_path},${base_path}/include,${base_path}/include/uapi,"\
                 "${TEGRA_TOP}/camera/firmware-api/kernel-include,"\
                 "${TEGRA_TOP}/core/include" | tr -d ' '
            ;;
        cam_fsync.txt)
            echo "${base_path}/include/uapi/media" | tr -d ' '
            ;;
      esac
}

# Get file path for specified module
# Parameters:
#   $1 - module name
# Returns: Complete path to module file
get_module_file_path() {
    local module=$1
    local base_path="$TEGRA_TOP/kernel/nvidia-oot/drivers/media/platform/tegra/fuzz"
    case $module in
        cdi_mgr.txt)
            echo "${base_path}/cdi/cdi_mgr.txt"
            ;;
        cdi_dev.txt)
            echo "${base_path}/cdi/cdi_dev.txt"
            ;;
        isc_mgr.txt)
            echo "${base_path}/isc/isc_mgr.txt"
            ;;
        capture_vi_channel.txt)
            echo "${base_path}/kmd_capture/capture_vi_channel.txt"
            ;;
        capture_isp_channel.txt)
            echo "${base_path}/kmd_capture/capture_isp_channel.txt"
            ;;
        cam_fsync.txt)
            echo "${base_path}/cam_fsync/cam_fsync.txt"
            ;;
      esac
}

# Execute script on Colossus machine
# Parameters:
#   $1 - Colossus IP address
#   $2 - Script command to execute
run_script_colossus() {
    local colossus_ip=$1
    local script_cmd=$2
    ssh -oUserKnownHostsFile=/dev/null -oStrictHostKeyChecking=no "$colossus_ip" "$script_cmd"
}

# Get script path for automation
get_script_path() {
    echo "$TEGRA_TOP/kernel/nvidia-oot/drivers/media/platform/tegra/fuzz/semi_automation"
}

SYZKALLER_DIR="${HOME}/syzkaller"
GO_DIR="${HOME}/go"
# Test case lists as strings
module_tests="cdi_mgr.txt
cdi_dev.txt
capture_vi_channel.txt
capture_isp_channel.txt
cam_fsync.txt"

automation_mode="semi"

# Main function
# Processes command line arguments and coordinates automation workflow
# Returns: 0 on success, 1 on failure
# Handles:
# - Parameter validation
# - Environment setup
# - Syzkaller build and configuration
# - SSH setup and tunneling
# - Fuzzer execution
main() {

    if [ $# -eq 0 ]; then
        show_help
        return 1
    fi

    # Parse command-line options
    while [ $# -gt 0 ]; do
        case $1 in
        --fuzz_module)
            fuzz_module="$2"
            shift 2
            ;;
        --tegra_top)
            TEGRA_TOP="$2"
            shift 2
            ;;
        --colossus_ip)
            colossus_ip_address="$2"
            shift 2
            ;;
        --tegra_ip)
            tegra_ip_address="$2"
            shift 2
            ;;
        --mode)
            automation_mode="$2"
            shift 2
            ;;
        -h|--help)
            show_help
            return 0
            ;;
        *)
            echo "Invalid option: $1"
            show_help
            return 1
            ;;
        esac
    done

    if [ "$automation_mode" = "semi" ]; then
        confirm_message || return 1
    fi

    syzkaller_setup || return 1

    syzkaller_build || return 1

    local colossus="$USER@$colossus_ip_address"
    echo "Colossus:$colossus"
    echo "tegra IP:$tegra_ip_address"

    local script_cmd="source $(get_script_path)/syzkaller_host_setup.sh --target_ip $tegra_ip_address"
    echo "script_cmd:$script_cmd"
    run_script_colossus "$colossus" "$script_cmd"

    echo "To setup passwordless SSH to host"
    ssh-copy-id -f $colossus

    echo "To make a SSH tunnel from drivefarm to host"
    ps -ef | grep "ssh.*-L 20000" | awk '{print $2}' | xargs -r sudo kill -9
    ssh -o StrictHostKeyChecking=no -fN -L 20000:$tegra_ip_address:22 $colossus

    echo "To setup passwordless SSH to target"
    ssh-copy-id -f -p 20000 root@127.0.0.1

    echo "To make a SSH tunnel from drivefarm to target"
    ssh -fN -p 20000 root@127.0.0.1

    # It needs to be executed on the local PC.
    # ssh -fN -L 50000:127.0.0.1:50000 ubuntu24

    config_base_path="$TEGRA_TOP/kernel/nvidia-oot/drivers/media/platform/tegra/fuzz"
    config_file="$config_base_path/syzkaller_config/configs"
    if [ ! -f "$config_file" ]; then
        echo "Error: Config file not found at $config_file"
        return 1
    fi
    cp $config_file $SYZKALLER_DIR/syzkaller/bin/
    echo "run syzkaller fuzzer"
    cd $SYZKALLER_DIR/syzkaller/bin
    ./syz-manager -config=configs

}

# Call main function with all passed arguments
main "$@"
