# SPDX-License-Identifier: GPL-2.0-only
# SPDX-FileCopyrightText: Copyright (c) 2022-2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

LINUXINCLUDE += -I$(srctree.nvidia-oot)/include
LINUXINCLUDE += -I$(srctree.nvidia-oot)/drivers/gpu/host1x/hw/
LINUXINCLUDE += -I$(srctree.nvidia-oot)/drivers/video/tegra/host/
LINUXINCLUDE += -I$(srctree.nvidia-oot)/drivers/gpu/host1x/include
LINUXINCLUDE += -I$(srctree.hwpm)/include

obj-m += block/tegra_virt_storage/
ifdef CONFIG_PSTORE
obj-m += block/tegra_oops_virt_storage/
endif
ifdef CONFIG_BT
obj-m += bluetooth/realtek/
endif
obj-m += c2c/
obj-m += clink/
obj-m += cpuidle/
obj-m += crypto/
ifdef CONFIG_PM_DEVFREQ
obj-m += devfreq/
endif
obj-m += firmware/tegra/
obj-m += gpio/
ifdef CONFIG_TEGRA_HOST1X
obj-m += gpu/
endif
ifdef CONFIG_HWMON
obj-m += hwmon/
endif
ifdef CONFIG_I2C
obj-m += i2c/busses/
obj-m += mfd/
ifdef CONFIG_IIO
obj-m += bmi088/
endif
endif
ifdef CONFIG_MEMORY
obj-m += memory/
endif
ifdef CONFIG_MTD
obj-m += mtd/devices/
endif
obj-m += misc/
obj-m += net/
obj-m += nvpps/
obj-m += nvpmodel/
obj-m += nv-p2p/
ifdef CONFIG_PCI
obj-m += pci/
ifdef CONFIG_TTY
obj-m += tty/
endif
endif
obj-m += pinctrl/
obj-m += platform/tegra/
obj-m += power/
obj-m += pwm/
obj-m += ras/
obj-m += regulator/
obj-m += rtc/
obj-m += cpuidle/
ifdef CONFIG_SCSI_UFSHCD
obj-m += scsi/ufs/
endif
obj-m += soc/tegra/
ifdef CONFIG_SPI
obj-m += spi/
endif
ifdef CONFIG_I2C
obj-m += thermal/
endif
obj-m += watchdog/
ifdef CONFIG_TEGRA_HOST1X
obj-m += video/tegra/
endif
obj-m += virt/tegra/
ifdef CONFIG_TEGRA_HOST1X
obj-m += media/
endif
obj-m += staging/platform/tegra/gte/
obj-m += staging/platform/tegra/gte_test/
obj-m += nv-virtio/
obj-m += usb/
