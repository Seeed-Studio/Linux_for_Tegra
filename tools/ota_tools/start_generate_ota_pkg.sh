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

ORIG_TARGET_BOARD="$TARGET_BOARD"

# R35 specific handling (may rewrite TARGET_BOARD)
# Since the OTA process needs to match the board configuration file, 
# and the configuration file names of some boards in the R35 version are inconsistent with those in jectpack6, they are processed here.
if [[ "$BSP_VERSION" == *R35* ]]; then
    echo -e "${GREEN}Detected R35 version, processing target_board compatibility copy logic...${NC}"
    case "$ORIG_TARGET_BOARD" in
        reserver-industrial-orin-j401)
            src="${TARGET_BSP}/reserver-industrial-orin-j401.conf"
            dst="${TARGET_BSP}/reserver-orin-industrial.conf"
            new_board="reserver-orin-industrial"
            ;;
        recomputer-industrial-orin-j201)
            src="${TARGET_BSP}/recomputer-industrial-orin-j201.conf"
            dst="${TARGET_BSP}/recomputer-orin-industrial.conf"
            new_board="recomputer-orin-industrial"
            ;;
        recomputer-orin-j401)
            src="${TARGET_BSP}/recomputer-orin-j401.conf"
            dst="${TARGET_BSP}/recomputer-orin.conf"
            new_board="recomputer-orin"
            ;;
        *)
            src=""
            ;;
    esac
    if [[ -n "${src}" ]]; then
        if [[ -f "${src}" ]]; then
            cp -f "${src}" "${dst}"
            echo -e "${GREEN}Copied: ${src} -> ${dst}${NC}"
            if [[ -n "${new_board:-}" ]]; then
                export TARGET_BOARD="$new_board"
                echo -e "${GREEN}TARGET_BOARD has been rewritten to normalized name: ${TARGET_BOARD}${NC}"
            fi
        else
            echo -e "${RED}Source file not found: ${src}${NC}"
            exit 1
        fi
    else
        echo -e "${YELLOW}Current target_board (${ORIG_TARGET_BOARD}) not in R35 copy list, keeping original.${NC}"
    fi
fi

echo
echo -e "${GREEN}=== Configuration Summary ===${NC}"
echo "BASE_BSP          = $BASE_BSP"
echo "TARGET_BSP        = $TARGET_BSP"
echo "Original target_board = $ORIG_TARGET_BOARD"
echo "Current TARGET_BOARD  = $TARGET_BOARD"
echo "bsp_version       = $BSP_VERSION"
echo

# Select generation script path
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