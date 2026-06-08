# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#!/bin/bash

# @brief Display the help message
display_help() {
    echo "Usage: syzkaller_host_setup.sh "
    echo
    echo "Arguments:"
    echo "  --target_ip       IP address of the target device."
    echo "  --help            Show this help message"
    echo
    echo "Examples:"
    echo "  $0 --target-ip 192.168.1.100"
}

# @brief send command to Tegra device
# @param $1 command to execute
# @return 0 success, 1 failure
send_command_tegra() {
    echo "sshpass -p $TARGET_USERNAME ssh -oUserKnownHostsFile=/dev/null \
        -oStrictHostKeyChecking=no ${TARGET_USERNAME}@${TARGET_IP} $1"
    sshpass -p $TARGET_USERNAME ssh \
        -oUserKnownHostsFile=/dev/null \
        -oStrictHostKeyChecking=no \
        ${TARGET_USERNAME}@${TARGET_IP} "$1" || return 1
}

TARGET_USERNAME=nvidia
TARGET_ROOT=root

# Main function to handle component and cleaning logic
main() {
    while [ "$#" -gt 0 ]; do
        case $1 in
                --target_ip)
                TARGET_IP="$2"
                shift 2
                ;;
                --help)
                display_help
                return 0
                ;;
                *)
                echo "Invalid option: $1"
                display_help
                return 1
                ;;
        esac
    done

    # create working directory
    send_command_tegra "mkdir /tmp/syzkaller"

    # # configure SSH
    send_command_tegra "echo 'PermitRootLogin yes' | sudo tee -a /etc/ssh/sshd_config"
    send_command_tegra "echo 'AllowTcpForwarding yes' | sudo tee -a /etc/ssh/sshd_config"

    # set root password
    send_command_tegra "echo 'root:root' | sudo chpasswd"

    # clear dmesg and restart ssh
    send_command_tegra "sudo dmesg --clear"
    send_command_tegra "sudo systemctl restart ssh"

    echo "Waiting for SSH to be ready..."
    sleep 5
    sshpass -p $TARGET_ROOT ssh-copy-id -f -o StrictHostKeyChecking=no ${TARGET_ROOT}@${TARGET_IP}
    ssh -oUserKnownHostsFile=/dev/null -oStrictHostKeyChecking=no ${TARGET_ROOT}@${TARGET_IP} exit
    if [ $? -eq 0 ]; then
        echo "[SUCCESS] SSH passwordless configured"
     else
        echo "[ERROR] Configuration failed"
        return 1
     fi

    echo "Tegra setup complete"

}
# Call main function with all passed arguments
main "$@"
