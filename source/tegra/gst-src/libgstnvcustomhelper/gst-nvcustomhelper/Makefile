################################################################################
# SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.
################################################################################

CC:= gcc
TARGET_NAME:= libgstnvcustomhelper.so

SRCS:= gst-nvcustomevent.c

RELEASE_SRC_DIR_NAME:=libs/gstnvcustomhelper

RELEASE_SRC_FILES:= Makefile.public=Makefile \
    README=README \
    gst-nvcustomevent.c=gst-nvcustomevent.c \
    gst-nvcustomevent.h=gst-nvcustomevent.h

PUBLIC_HEADER_FILES:= gst-nvcustomevent.h=gst-nvcustomevent.h

INC_PATHS:= NV_ROOT_DIR/nvutils/gst-nvcustomhelper

LIBS:= 

NEEDS_CUDA:=0

NEEDS_GST:=1

IS_GST_UTIL:=1

PACKAGE_BINARY_IN_DS:=1

CAN_PUBLIC_LINK:=1

BUILD_DIR:=../../deepstream/sdk/build/libs

IGNORE_DS_PACKAGE_NAMING=1

include ../../deepstream/sdk/Rules.mk
