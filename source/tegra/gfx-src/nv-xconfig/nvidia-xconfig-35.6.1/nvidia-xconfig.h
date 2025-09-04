/*
 * nvidia-xconfig: A tool for manipulating X config files,
 * specifically for use by the NVIDIA Linux graphics driver.
 *
 * Copyright (C) 2004 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses>.
 *
 *
 * nvidia-xconfig.h
 */

#ifndef __NVIDIA_XCONFIG_H__
#define __NVIDIA_XCONFIG_H__

#include "xf86Parser.h"
#include "nvidia-cfg.h"
#include "common-utils.h"

#include <sys/types.h>


/* Boolean options */
#define UBB_BOOL_OPTION                         1
#define RENDER_ACCEL_BOOL_OPTION                2
#define NO_RENDER_EXTENSION_BOOL_OPTION         3
#define OVERLAY_BOOL_OPTION                     4
#define CIOVERLAY_BOOL_OPTION                   5
#define OVERLAY_DEFAULT_VISUAL_BOOL_OPTION      6
#define NO_POWER_CONNECTOR_CHECK_BOOL_OPTION    8
#define ALLOW_GLX_WITH_COMPOSITE_BOOL_OPTION    10
#define SEPARATE_X_SCREENS_BOOL_OPTION          13
#define XINERAMA_BOOL_OPTION                    14
#define NVIDIA_XINERAMA_INFO_BOOL_OPTION        15
#define NOFLIP_BOOL_OPTION                      16
#define DAC_8BIT_BOOL_OPTION                    17
#define USE_EDID_FREQS_BOOL_OPTION              18
#define USE_EDID_BOOL_OPTION                    19
#define FORCE_STEREO_FLIPPING_BOOL_OPTION       21
#define MULTISAMPLE_COMPATIBILITY_BOOL_OPTION   22
#define EXACT_MODE_TIMINGS_DVI_BOOL_OPTION      24
#define ADD_ARGB_GLX_VISUALS_BOOL_OPTION        27
#define COMPOSITE_BOOL_OPTION                   28
#define DISABLE_GLX_ROOT_CLIPPING_BOOL_OPTION   29
#define USE_EDID_DPI_BOOL_OPTION                30
#define DAMAGE_EVENTS_BOOL_OPTION               31
#define CONSTANT_DPI_BOOL_OPTION                32
#define PROBE_ALL_GPUS_BOOL_OPTION              33
#define INCLUDE_IMPLICIT_METAMODES_BOOL_OPTION  34
#define USE_EVENTS_BOOL_OPTION                  35
#define CONNECT_TO_ACPID_BOOL_OPTION            36
#define MODE_DEBUG_BOOL_OPTION                  37
#define THERMAL_CONFIGURATION_CHECK_BOOL_OPTION 38
#define PRESERVE_BUSID_BOOL_OPTION              39
#define BASE_MOSAIC_BOOL_OPTION                 40
#define ALLOW_EMPTY_INITIAL_CONFIGURATION       41
#define INBAND_STEREO_SIGNALING                 42
#define FORCE_YUV_420                           43
#define ENABLE_PRIME_OPTION                     44
#define ENABLE_EXTERNAL_GPU_BOOL_OPTION         45

#define XCONFIG_BOOL_OPTION_COUNT (ENABLE_EXTERNAL_GPU_BOOL_OPTION + 1)

/* # of 32-bit variables needed to hold all the boolean options (bits) */
#define XCONFIG_BOOL_OPTION_SLOTS  \
  ((XCONFIG_BOOL_OPTION_COUNT)/32) +      \
  (((XCONFIG_BOOL_OPTION_COUNT)%32)?1:0)

#define GET_BOOL_OPTION_SLOT(BLOCKS, VAR)  \
  ((BLOCKS)[(u32)((VAR)/32)])

#define GET_BOOL_OPTION_BIT(VAR)  \
  (1<<(u32)((VAR)%32))

#define GET_BOOL_OPTION(BLOCKS, VAR)        \
  (GET_BOOL_OPTION_SLOT((BLOCKS), (VAR)) &  \
   GET_BOOL_OPTION_BIT(VAR))


/* define to store in string options */
#define NV_DISABLE_STRING_OPTION ((void *) -1)

/* 32 bit unsigned variable (used to pack booleans) */
typedef unsigned int u32;


typedef struct __options {
    int force_generate;
    int tree;
    int post_tree;
    int keyboard_list;
    int mouse_list;
    int enable_all_gpus;
    int only_one_screen;
    int disable_scf;
    int query_gpu_info;
    int preserve_driver;
    int restore_original_backup;
    
    /*
     * the option parser will set bits in boolean_options to indicate
     * whether an option was set, and will then turn the corresponding
     * bit in boolean_option_values on or off to indicate if the
     * option was true or false.
     */
    
    u32 boolean_options[XCONFIG_BOOL_OPTION_SLOTS];
    u32 boolean_option_values[XCONFIG_BOOL_OPTION_SLOTS];

    int depth;
    int transparent_index;
    int stereo;
    int cool_bits;
    int nvidia_3dvision_display_type;

    int num_x_screens;

    char *xconfig;
    char *output_xconfig;
    char *layout;
    char *screen;
    char *device;
    char *busid;
    char *multigpu;
    char *sli;

    char *nvidia_cfg_path;
    char *extract_edids_from_file;
    char *extract_edids_output_file;
    char *nvidia_xinerama_info_order;
    char *metamode_orientation;
    char *use_display_device;
    char *custom_edid;
    char *tv_standard;
    char *tv_out_format;
    char *acpid_socket_path;
    char *handle_special_keys;
    char *connected_monitor;
    char *registry_dwords;
    char *metamodes_str;
    char *color_space;
    char *color_range;
    char *flatpanel_properties;
    char *nvidia_3dvision_usb_path;
    char *nvidia_3dvisionpro_config_file;
    char *force_composition_pipeline;
    char *force_full_composition_pipeline;
    char *allow_hmd;

    double tv_over_scan;

    struct {
        int x;
        int y;
    } virtual;

    TextRows add_modes;
    TextRows add_modes_list;
    TextRows remove_modes;

    GenerateOptions gop;

} Options;

/* data structures for storing queried GPU information */

typedef struct _display_device_rec {
    NvCfgDisplayDeviceInformation info;
    int info_valid;
    unsigned int mask;
} DisplayDeviceRec, *DisplayDevicePtr;

typedef struct _device_rec {
    NvCfgPciDevice dev;
    NvCfgDeviceHandle handle;
    int crtcs;
    char *name;
    char *uuid;
    unsigned int displayDeviceMask;
    int nDisplayDevices;
    DisplayDevicePtr displayDevices;
} DeviceRec, *DevicePtr;

typedef struct {
    int nDevices;
    DevicePtr devices;
} DevicesRec, *DevicesPtr;


/* util.c */

int copy_file(const char *srcfile, const char *dstfile, mode_t mode);
char *nv_format_busid(Options *op, int index);

/* make_usable.c */

int update_modules(XConfigPtr config);
int update_screen(Options *op, XConfigPtr config, XConfigScreenPtr screen);
XConfigLayoutPtr get_layout(Options *op, XConfigPtr config);
int update_extensions(Options *op, XConfigPtr config);
int update_server_flags(Options *op, XConfigPtr config);

/* multiple_screens.c */

DevicesPtr find_devices(Options *op);
void free_devices(DevicesPtr devs);

int apply_multi_screen_options(Options *op, XConfigPtr config,
                               XConfigLayoutPtr layout);

/* tree.c */

int print_tree(Options *op, XConfigPtr config);

/* options.c */

int check_boolean_option(Options *op, const int c, const int boolval);
void set_boolean_option(Options *op, const int c, const int boolval);
void validate_composite(Options *op, XConfigPtr config);
void update_options(Options *op, XConfigScreenPtr screen);

/* lscf.c */
int update_scf_depth(int depth);
int read_scf_depth(int *depth);

/* query_gpu_info.c */

int query_gpu_info(Options *op);

/* extract_edids.c */

int extract_edids(Options *op);



#endif /* __NVIDIA_XCONFIG_H__ */
