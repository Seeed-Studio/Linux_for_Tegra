#!/usr/bin/env bash
#
# Interactively collect required variables and generate OTA package automatically
# Usage (source to retain environment variables):
#   source ./start_generate_ota_pkg.sh
#

set -euo pipefail

GREEN="\033[32m"
RED="\033[31m"
YELLOW="\033[33m"
NC="\033[0m"

trap 'echo -e "\n${YELLOW}Interrupted${NC}"; exit 1' INT

ask_dir() {
    local var_name="$1"
    local prompt="$2"
    local value=""
    while true; do
        read -e -p "$prompt" value
        if [[ -z "$value" ]]; then
            echo -e "${RED}Cannot be empty, please retry${NC}"
            continue
        fi
        if [[ "$value" == "q" ]]; then
            echo "Exit"
            exit 1
        fi
        if [[ ! -d "$value" ]]; then
            echo -e "${RED}Directory does not exist: $value${NC}"
            continue
        fi
        value="$(realpath "$value")"
        printf -v "$var_name" "%s" "$value"
        export "$var_name"="$value"
        break
    done
}

ask_value() {
    local var_name="$1"
    local prompt="$2"
    local value=""
    while true; do
        read -p "$prompt" value
        if [[ -z "$value" ]]; then
            echo -e "${RED}Cannot be empty, please retry${NC}"
            continue
        fi
        if [[ "$value" == "q" ]]; then
            echo "Exit"
            exit 1
        fi
        printf -v "$var_name" "%s" "$value"
        break
    done
}

echo -e "${GREEN}=== OTA Environment Initialization ===${NC}"
echo "Enter q at any prompt to quit"
echo

ask_dir  BASE_BSP   "Enter BASE_BSP path: "
ask_dir  TARGET_BSP "Enter TARGET_BSP path: "
ask_value target_board  "Enter target_board name: "
ask_value bsp_version   "Enter bsp_version (Rmm-n): "

export BASE_BSP TARGET_BSP
export TARGET_BOARD="$target_board"
export BSP_VERSION="$bsp_version"

echo
echo -e "${GREEN}=== Configuration Summary ===${NC}"
echo "BASE_BSP     = $BASE_BSP"
echo "TARGET_BSP   = $TARGET_BSP"
echo "TARGET_BOARD = $TARGET_BOARD"
echo "bsp_version  = $BSP_VERSION"
echo

cd "$TARGET_BSP"
GEN_SCRIPT="tools/ota_tools/version_upgrade/l4t_generate_ota_package.sh"

if [[ ! -f "$GEN_SCRIPT" ]]; then
    echo -e "${RED}Generation script not found: $GEN_SCRIPT${NC}"
    exit 1
fi

echo -e "${GREEN}Start generating OTA package: ${GEN_SCRIPT}${NC}"
echo "Command: sudo -E ./$GEN_SCRIPT --external-device nvme0n1 -S 80GiB $TARGET_BOARD $BSP_VERSION"
sudo -E "./$GEN_SCRIPT" --external-device nvme0n1 -S 80GiB "$TARGET_BOARD" "$BSP_VERSION"

echo -e "${GREEN}OTA package generation finished${NC}"
echo
echo "You can find the generated OTA package in:"
echo "$TARGET_BSP/Linux_for_Tegra/bootloader/${TARGET_BOARD}/"
echo

