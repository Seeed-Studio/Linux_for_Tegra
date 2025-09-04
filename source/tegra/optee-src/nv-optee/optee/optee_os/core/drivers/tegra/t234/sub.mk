incdirs-y += include
incdirs-y += ../common/include

srcs-$(CFG_TEGRA_SE) += tegra_se_aes_rng.c
srcs-$(CFG_TEGRA_FUSE) += tegra_fuse.c
srcs-$(CFG_RPMB_FS) += tegra_rpmb.c
