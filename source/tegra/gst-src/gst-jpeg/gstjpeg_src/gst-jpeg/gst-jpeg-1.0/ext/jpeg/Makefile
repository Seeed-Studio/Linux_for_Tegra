################################################################################
# Copyright (c) 2018-2023, NVIDIA CORPORATION. All rights reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#
################################################################################

CC:= g++
TARGET_NAME:= libnvdsgst_jpeg.so

OPEN_CV:=0
IS_GST_PLUGIN:=1

SRCS:= gstjpeg.c gstjpegdec.c gstjpegenc.c

CFLAGS:= -fPIC -std=c++11 -ggdb -DUSE_TARGET_GPU -fpermissive -DGPU_ACCELERATE

INC_PATHS:= ../gst-nvbufferpool \
						../../../../../../external/jpeg/libjpeg-8b/ \
						../../../../../../nvutils/nvbufsurface/ \
						/usr/local/cuda/include \
						/usr/local/cuda-10.0/include \
		        ../../utils/nv_csvparser \
		        DS_SRC_ROOT_DIR/src/gst-utils/gstnvdshelper \
		        ../../../../gstnvmeta \
		        ../../../external/3rdparty \
						../../gst-libs/

ifndef BUILD_DVS
INC_PATHS+=	DS_SRC_ROOT_DIR/external/cuda/video_codec_sdk8.0_includes
endif

BUILD_DIR:=../../../../../../deepstream/sdk/build/src/gst-plugins/gst-jpeg

NEEDS_CUDA:=1
NEEDS_NPP:=1

LIBS:= 	-LDS_BUILD_ROOT_DIR/src/gst-utils/gstnvdshelper -lnvdsgst_helper \
	-L../../../external/cuda \
	-L/usr/local/cuda-10.0/lib64 \
	-lrt -ldl -lpthread -lcudart -lnvbufsurface \
 	-L../../../../../../deepstream/sdk/build/libs/ -lnvds_lljpeg

PACKAGE_BINARY_IN_DS:=1

include ../../../../../../deepstream/sdk/Rules.mk
