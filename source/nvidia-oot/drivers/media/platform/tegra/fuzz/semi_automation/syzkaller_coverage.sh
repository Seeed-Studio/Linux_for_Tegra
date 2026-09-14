# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#!/bin/bash

# Display help message with usage instructions and available options
show_help() {
    echo "Shell Script for syzkaller coverage automation"
    echo "Usage:"
    echo "  source ./syzkaller_coverage.sh [options]"
    echo ""
    echo "Options:"
    echo "  --fuzz_module NAME    Specify fuzz module: all, cam_fsync, cdi, kmd_capture"
    echo "  --tegra_top PATH      Specify the project root directory"
    echo "  -h, --help            Show this help message"
    echo ""
    echo "Examples:"
    echo "  source ./syzkaller_coverage.sh \\"
    echo "    --fuzz_module all \\"
    echo "    --tegra_top /home/xx/automotive-dev-main-xxx"
    echo ""
    echo "fuzz_module details:"
    echo "  cam_fsync     Generate coverage report for cam_fsync module"
    echo "  cdi           Generate coverage reports for cdi_mgr and cdi_dev modules"
    echo "  kmd_capture   Generate coverage reports for capture_vi_channel and capture_isp_channel modules"
    echo "  all           Generate coverage reports for all above modules"
}

# Function to fetch coverage binary
fetch_coverage_bin() {
    local bin_name=$1
    local dest_dir=$2
    echo "scp -P 20000 root@127.0.0.1:/output/${bin_name} ${dest_dir}/"
    scp -P 20000 root@127.0.0.1:/output/${bin_name} "${dest_dir}/" || {
        echo "Failed to fetch ${bin_name}"
        return 1
    }
}

# Function to generate coverage report
generate_report() {
    local ko_file=$1
    local trace_file=$2
    local output_lcov=$3
    local output_html=$4
    local title=$5

    python gen_lcov.py \
        --binutils-dir ~/bin/aarch64 \
        --ko-file "$ko_file" \
        --trace-file "$trace_file" \
        --output-lcov "$output_lcov" \
        --output-html "$output_html" \
        --title "$title" || {
            echo "Failed to generate report for $title"
            return 1
        }
}

# Main function
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

    if [ ! -d "$TEGRA_TOP/workdir/$fuzz_module/coverage_reports" ]; then
        echo "create workdir directory:$TEGRA_TOP/workdir/$fuzz_module/coverage_reports"
        mkdir -p "$TEGRA_TOP/workdir/$fuzz_module/coverage_reports"
    fi

    cd ${HOME}/syzkaller/syzkaller-scripts

    coverage_dir="$TEGRA_TOP/workdir/$fuzz_module/coverage_reports"
    out_base="$TEGRA_TOP/out/embedded-linux-generic-debug-none/nvidia/kernel-oot"
    ko_dir="$out_base/kernel-rt_patches-ootm-nvidia-public/nvidia-public/drivers/media/platform/tegra"
    case $fuzz_module in
        cam_fsync)
            fetch_coverage_bin "cam_fsync_coverage.bin" "$coverage_dir"

            generate_report \
                "$ko_dir/cam_fsync/cam_fsync.ko" \
                "$coverage_dir/cam_fsync_coverage.bin" \
                "$coverage_dir/cam_fsync_coverage.lcov" \
                "$coverage_dir/cam_fsync_coverage_report" \
                "cam fsync Coverage Report"
            ;;
        cdi)
            fetch_coverage_bin "cdi_dev_coverage.bin" "$coverage_dir"
            fetch_coverage_bin "cdi_mgr_coverage.bin" "$coverage_dir"

            generate_report \
                "$ko_dir/cdi/cdi_mgr.ko" \
                "$coverage_dir/cdi_mgr_coverage.bin" \
                "$coverage_dir/cdi_mgr_coverage.lcov" \
                "$coverage_dir/cdi_mgr_coverage_report" \
                "cdi mgr Coverage Report"

            generate_report \
                "$ko_dir/cdi/cdi_dev.ko" \
                "$coverage_dir/cdi_dev_coverage.bin" \
                "$coverage_dir/cdi_dev_coverage.lcov" \
                "$coverage_dir/cdi_dev_coverage_report" \
                "cdi dev Coverage Report"
            ;;
        fusa-capture)
            fetch_coverage_bin "tegra_camera_coverage.bin" "$coverage_dir"
            fetch_coverage_bin "tegra_capture_isp_coverage.bin" "$coverage_dir"

            generate_report \
                "$ko_dir/camera/tegra-camera.ko" \
                "$coverage_dir/tegra_camera_coverage.bin" \
                "$coverage_dir/capture_vi_channel_coverage.lcov" \
                "$coverage_dir/capture_vi_channel_coverage_report" \
                "capture vi channel Coverage Report"

            generate_report \
                "$ko_dir/camera/tegra-capture-isp.ko" \
                "$coverage_dir/tegra_capture_isp_coverage.bin" \
                "$coverage_dir/capture_isp_channel_coverage.lcov" \
                "$coverage_dir/capture_isp_channel_coverage_report" \
                "capture isp channel Coverage Report"
            ;;
        all)
            fetch_coverage_bin "cam_fsync_coverage.bin" "$coverage_dir"
            fetch_coverage_bin "cdi_dev_coverage.bin" "$coverage_dir"
            fetch_coverage_bin "cdi_mgr_coverage.bin" "$coverage_dir"
            fetch_coverage_bin "tegra_camera_coverage.bin" "$coverage_dir"
            fetch_coverage_bin "tegra_capture_isp_coverage.bin" "$coverage_dir"

            generate_report \
                "$ko_dir/cam_fsync/cam_fsync.ko" \
                "$coverage_dir/cam_fsync_coverage.bin" \
                "$coverage_dir/cam_fsync_coverage.lcov" \
                "$coverage_dir/cam_fsync_coverage_report" \
                "cam fsync Coverage Report"

            generate_report \
                "$ko_dir/cdi/cdi_mgr.ko" \
                "$coverage_dir/cdi_mgr_coverage.bin" \
                "$coverage_dir/cdi_mgr_coverage.lcov" \
                "$coverage_dir/cdi_mgr_coverage_report" \
                "cdi mgr Coverage Report"

            generate_report \
                "$ko_dir/cdi/cdi_dev.ko" \
                "$coverage_dir/cdi_dev_coverage.bin" \
                "$coverage_dir/cdi_dev_coverage.lcov" \
                "$coverage_dir/cdi_dev_coverage_report" \
                "cdi dev Coverage Report"

            generate_report \
                "$ko_dir/camera/tegra-camera.ko" \
                "$coverage_dir/tegra_camera_coverage.bin" \
                "$coverage_dir/capture_vi_channel_coverage.lcov" \
                "$coverage_dir/capture_vi_channel_coverage_report" \
                "capture vi channel Coverage Report"

            generate_report \
                "$ko_dir/camera/tegra-capture-isp.ko" \
                "$coverage_dir/tegra_capture_isp_coverage.bin" \
                "$coverage_dir/capture_isp_channel_coverage.lcov" \
                "$coverage_dir/capture_isp_channel_coverage_report" \
                "capture isp channel Coverage Report"
            ;;
    esac
}

# Call main function with all passed arguments
main "$@"
