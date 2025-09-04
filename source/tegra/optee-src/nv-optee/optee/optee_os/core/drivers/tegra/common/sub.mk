incdirs-y += include

srcs-y += tegra_driver_common.c
srcs-$(CFG_TEGRA_FUSE) += tegra_driver_fuse.c
srcs-$(CFG_TEGRA_SE_RNG1) += tegra_driver_rng1.c
srcs-$(CFG_TEGRA_SE) += tegra_driver_se.c
srcs-y += tegra_driver_srv_intf.c
srcs-y += tegra_driver_clear_keyslots.c
