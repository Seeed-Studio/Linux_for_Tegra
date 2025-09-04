#!/bin/bash

# This is the default ekb key.
echo "00000000000000000000000000000000" > ekb.key

# Generate a unique passphrase.
python3 gen_luks_passphrase.py -k ekb.key \
			       -c "UUID of the disk" \
			       -u \
			       -e "0x880219116451e2c60c00000001ff0140"

