# Copyright (c) 2020, NVIDIA CORPORATION.  All Rights Reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property and
# proprietary rights in and to this software and related documentation.  Any
# use, reproduction, disclosure or distribution of this software and related
# documentation without an express license agreement from NVIDIA Corporation
# is strictly prohibited.
#
# Source for this file: $(TEGRA_TOP)/tmake/artifacts/CommonRulesNvMake.tmk

ifeq ($(NV_BUILD_CONFIGURATION_LINUX_USERSPACE_IS_EMBEDDED),1)
  ifeq ($(NV_BUILD_CONFIGURATION_IS_EXTERNAL),0)
    NVCFG_PROFILE=tegragpu_unix_arm_embedded_profile
  else ifeq ($(NV_BUILD_CONFIGURATION_IS_GNEXT),1)
    NVCFG_PROFILE=tegragpu_unix_arm_embedded_gnext_profile
  else
    NVCFG_PROFILE=tegragpu_unix_arm_embedded_external_profile
  endif
else ifeq ($(NV_BUILD_CONFIGURATION_LINUX_USERSPACE_IS_L4T),1)
  ifneq ($(NV_BUILD_CONFIGURATION_IS_EXTERNAL),0)
    ifeq ($(NV_BUILD_CONFIGURATION_IS_GNEXT),1)
      NVCFG_PROFILE=l4t_global_gnext_profile
    else
      NVCFG_PROFILE=l4t_global_external_profile
    endif
  else
      NVCFG_PROFILE=tegragpu_unix_arm_global_profile
  endif
else ifeq ($(NV_BUILD_CONFIGURATION_OS_IS_QNX),1)
  ifeq ($(NV_BUILD_CONFIGURATION_IS_EXTERNAL),0)
    NVCFG_PROFILE=tegragpu_unix_arm_embedded_profile
  else ifeq ($(NV_BUILD_CONFIGURATION_IS_GNEXT),1)
    NVCFG_PROFILE=tegragpu_unix_arm_embedded_gnext_profile
  else ifeq ($(NV_BUILD_CONFIGURATION_IS_SAFETY),1)
    NVCFG_PROFILE=tegra_with_dgpu_embedded_safety_external_profile
  else
    NVCFG_PROFILE=tegragpu_unix_arm_embedded_external_profile
  endif
else
  $(error Unsupported OS)
endif
