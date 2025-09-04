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
 * options.c
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "nvidia-xconfig.h"
#include "xf86Parser.h"
#include "msg.h"


typedef struct {
    unsigned int num;
    int invert;
    const char *name;
} NvidiaXConfigOption;

static const NvidiaXConfigOption __options[] = {
    
    { UBB_BOOL_OPTION,                       FALSE, "UBB" },
    { RENDER_ACCEL_BOOL_OPTION,              FALSE, "RenderAccel" },
    { NO_RENDER_EXTENSION_BOOL_OPTION,       TRUE,  "NoRenderExtension" },
    { OVERLAY_BOOL_OPTION,                   FALSE, "Overlay" },
    { CIOVERLAY_BOOL_OPTION,                 FALSE, "CIOverlay" },
    { OVERLAY_DEFAULT_VISUAL_BOOL_OPTION,    FALSE, "OverlayDefaultVisual" },
    { NO_POWER_CONNECTOR_CHECK_BOOL_OPTION,  TRUE,  "NoPowerConnectorCheck" },
    { THERMAL_CONFIGURATION_CHECK_BOOL_OPTION, FALSE, "ThermalConfigurationCheck" },
    { ALLOW_GLX_WITH_COMPOSITE_BOOL_OPTION,  FALSE, "AllowGLXWithComposite" },
    { XINERAMA_BOOL_OPTION,                  FALSE, "Xinerama" },
    { NVIDIA_XINERAMA_INFO_BOOL_OPTION,      FALSE, "nvidiaXineramaInfo" },
    { NOFLIP_BOOL_OPTION,                    TRUE,  "NoFlip" },
    { DAC_8BIT_BOOL_OPTION,                  FALSE, "Dac8Bit" },
    { USE_EDID_FREQS_BOOL_OPTION,            FALSE, "UseEdidFreqs" },
    { USE_EDID_BOOL_OPTION,                  FALSE, "UseEdid" },
    { FORCE_STEREO_FLIPPING_BOOL_OPTION,     FALSE, "ForceStereoFlipping" },
    { MULTISAMPLE_COMPATIBILITY_BOOL_OPTION, FALSE, "MultisampleCompatibility" },
    { EXACT_MODE_TIMINGS_DVI_BOOL_OPTION,    FALSE, "ExactModeTimingsDVI" },
    { ADD_ARGB_GLX_VISUALS_BOOL_OPTION,      FALSE, "AddARGBGLXVisuals" },
    { DISABLE_GLX_ROOT_CLIPPING_BOOL_OPTION, FALSE, "DisableGLXRootClipping" },
    { USE_EDID_DPI_BOOL_OPTION,              FALSE, "UseEdidDpi" },
    { DAMAGE_EVENTS_BOOL_OPTION,             FALSE, "DamageEvents" },
    { CONSTANT_DPI_BOOL_OPTION,              FALSE, "ConstantDPI" },
    { PROBE_ALL_GPUS_BOOL_OPTION,            FALSE, "ProbeAllGpus" },
    { INCLUDE_IMPLICIT_METAMODES_BOOL_OPTION,FALSE, "IncludeImplicitMetaModes" },
    { USE_EVENTS_BOOL_OPTION,                FALSE, "UseEvents" },
    { CONNECT_TO_ACPID_BOOL_OPTION,          FALSE, "ConnectToAcpid" },
    { MODE_DEBUG_BOOL_OPTION,                FALSE, "ModeDebug" },
    { BASE_MOSAIC_BOOL_OPTION,               FALSE, "BaseMosaic" },
    { ALLOW_EMPTY_INITIAL_CONFIGURATION,     FALSE, "AllowEmptyInitialConfiguration" },
    { INBAND_STEREO_SIGNALING,               FALSE, "InbandStereoSignaling" },
    { FORCE_YUV_420,                         FALSE, "ForceYUV420" },
    { ENABLE_EXTERNAL_GPU_BOOL_OPTION,       FALSE, "AllowExternalGpus" },
    { 0,                                     FALSE, NULL },
};



/*
 * get_option() - get the NvidiaXConfigOption entry for the given
 * option index
 */

static const NvidiaXConfigOption *get_option(const int n)
{
    int i;

    for (i = 0; __options[i].name; i++) {
        if (__options[i].num == n) return &__options[i];
    }
    return NULL;
    
} /* get_option() */

/*
 * check_boolean_option() - verify the boolean option 'c' can be
 * applied given other set options.
 */

int check_boolean_option(Options *op, const int c, const int boolval)
{
    switch (c) {

    case ENABLE_PRIME_OPTION:
        /*
         * Prime requires these features, so if the user
         * has disabled it we have a conflict.
         */
        if (GET_BOOL_OPTION(op->boolean_options,
                            ALLOW_EMPTY_INITIAL_CONFIGURATION)
            &&
            !GET_BOOL_OPTION(op->boolean_option_values,
                             ALLOW_EMPTY_INITIAL_CONFIGURATION))
        {
            fprintf(stderr, "Unable to enable PRIME with "
                            "ALLOW_EMPTY_INITIAL_CONFIGURATION disabled.\n");
            return FALSE;
        } else if (op->busid == NV_DISABLE_STRING_OPTION) {
            fprintf(stderr, "Unable to enable PRIME with BUSID disabled.\n");
            return FALSE;
        }
        break;

    case ALLOW_EMPTY_INITIAL_CONFIGURATION:
        if (GET_BOOL_OPTION(op->boolean_option_values,
            ENABLE_PRIME_OPTION)
            && !boolval)
        {
            fprintf(stderr, "Unable to disable"
                            "ALLOW_EMPTY_INITIAL_CONFIGURATION with"
                            "PRIME enabled.\n");
            return FALSE;
        }
        break;

    default: break;
    }

    return TRUE;
}

/*
 * set_boolean_option() - set boolean option 'c' to the given 'boolval'
 */

void set_boolean_option(Options *op, const int c, const int boolval)
{
    u32 bit;
    
    bit = GET_BOOL_OPTION_BIT(c);
    
    GET_BOOL_OPTION_SLOT(op->boolean_options, c) |= bit;
    
    if (boolval) {
        GET_BOOL_OPTION_SLOT(op->boolean_option_values, c) |= bit;

        /* Options that must be paired together */
        switch (c) {
         case ENABLE_PRIME_OPTION: {
             set_boolean_option(op, ALLOW_EMPTY_INITIAL_CONFIGURATION, TRUE);
             break;
         }
        }
    } else {
        GET_BOOL_OPTION_SLOT(op->boolean_option_values, c) &= ~bit;
    }
} /* set_boolean_option() */



/*
 * validate_composite() - check whether any options conflict with the
 * Composite extension; update the composite option value, if
 * appropriate.
 */

void validate_composite(Options *op, XConfigPtr config)
{
    int composite_specified;
    int xinerama_enabled;
    int overlay_enabled;
    int cioverlay_enabled;
    int ubb_enabled;
    int stereo_enabled;
    char *err_str;


    composite_specified = GET_BOOL_OPTION(op->boolean_options,
                                          COMPOSITE_BOOL_OPTION);

    xinerama_enabled = (GET_BOOL_OPTION(op->boolean_options,
                                        XINERAMA_BOOL_OPTION) &&
                        GET_BOOL_OPTION(op->boolean_option_values,
                                        XINERAMA_BOOL_OPTION));

    overlay_enabled = (GET_BOOL_OPTION(op->boolean_options,
                                       OVERLAY_BOOL_OPTION) &&
                       GET_BOOL_OPTION(op->boolean_option_values,
                                       OVERLAY_BOOL_OPTION));

    cioverlay_enabled = (GET_BOOL_OPTION(op->boolean_options,
                                         CIOVERLAY_BOOL_OPTION) &&
                         GET_BOOL_OPTION(op->boolean_option_values,
                                         CIOVERLAY_BOOL_OPTION));

    ubb_enabled = (GET_BOOL_OPTION(op->boolean_options,
                                   UBB_BOOL_OPTION) &&
                   GET_BOOL_OPTION(op->boolean_option_values,
                                   UBB_BOOL_OPTION));

    stereo_enabled = !!(op->stereo > 0);

    err_str = xconfigValidateComposite(config,
                                       &(op->gop),
                                       composite_specified,
                                       xinerama_enabled,
                                       op->depth,
                                       overlay_enabled,
                                       cioverlay_enabled,
                                       ubb_enabled,
                                       stereo_enabled);

    /*
     * if we have to disable the composite extension, print a warning
     * and set the option value.
     *
     * We need to be careful to only set the option value if the X
     * server is going to recognize the Extension section and the
     * composite option.  We guess whether the server will recognize
     * the option: if get_xserver_in_use() thinks the X server
     * supports the "Composite" extension, or the current config
     * already has an extension section, or the user specified the
     * composite option.
     */

    if (err_str) {
        nv_warning_msg("The Composite X extension does not currently interact "
                       "well with the %s option(s); the Composite X extension "
                       "will be disabled.", err_str);

        set_boolean_option(op, COMPOSITE_BOOL_OPTION, FALSE);
        nvfree(err_str);
    }
} /* validate_composite() */



/*
 * remove_option() - make sure the named option does not exist in any
 * of the possible option lists:
 *
 * Options related to drivers can be present in the Screen, Device and
 * Monitor sections and the Display subsections.  The order of
 * precedence is Display, Screen, Monitor, Device.
 */

static void remove_option(XConfigScreenPtr screen, const char *name)
{
    XConfigDisplayPtr display;

    if (!screen) return;

    if (screen->device) {
        xconfigRemoveNamedOption(&screen->device->options, name, NULL);
    }
    if (screen->monitor) {
        xconfigRemoveNamedOption(&screen->monitor->options, name, NULL);
    }
    xconfigRemoveNamedOption(&screen->options, name, NULL);
    
    for (display = screen->displays; display; display = display->next) {
        xconfigRemoveNamedOption(&display->options, name, NULL);
    }
} /* remove_option() */



/*
 * get_screen_option() - get the option structure with the specified
 * name, searching all the option lists associated with this screen
 */

static XConfigOptionPtr get_screen_option(XConfigScreenPtr screen,
                                          const char *name)
{
    XConfigDisplayPtr display;
    XConfigOptionPtr opt;

    if (!screen) return NULL;

    if (screen->device) {
        opt = xconfigFindOption(screen->device->options, name);
        if (opt) return opt;
    }
    if (screen->monitor) {
        opt = xconfigFindOption(screen->monitor->options, name);
        if (opt) return opt;
    }

    opt = xconfigFindOption(screen->options, name);
    if (opt) return opt;

    for (display = screen->displays; display; display = display->next) {
        opt = xconfigFindOption(display->options, name);
        if (opt) return opt;
    }

    return NULL;

} /* get_screen_option() */



/*
 * set_option_value() - set the given option to the specified value
 */

static void set_option_value(XConfigScreenPtr screen,
                             const char *name, const char *val)
{
    /* first, remove the option to make sure it doesn't exist
       elsewhere */

    remove_option(screen, name);

    /* then, add the option to the screen's option list */

    xconfigAddNewOption(&screen->options, name, val);

} /* set_option_value() */



/*
 * find_metamode_offset() - find the first metamode offset in
 * 'string'; returns a pointer to the start of the offset
 * specification and assigns 'end' (if non-NULL) to the first character
 * beyond the offset specification.
 */

static char *find_metamode_offset(char *string, char **end)
{
    enum {
        StateBeforeOffset,
        StateInFirstPlus,
        StateInFirstNumber,
        StateInSecondPlus,
        StateInSecondNumber
    } state = StateBeforeOffset;

    char *s, *start = NULL, c;

    for (s = string; s && *s; s++) {
        c = *s;
        switch (state) {

        case StateBeforeOffset:
            if ((c == '-') || (c == '+')) {
                start = s;
                state = StateInFirstPlus;
            }
            break;

        case StateInFirstPlus:
            if (isspace(c)) state = StateInFirstPlus;
            else if (isdigit(c)) state = StateInFirstNumber;
            else state = StateBeforeOffset;
            break;

        case StateInFirstNumber:
            if (isdigit(c) || isspace(c)) state = StateInFirstNumber;
            else if ((c == '-') || (c == '+')) state = StateInSecondPlus;
            else state = StateBeforeOffset;
            break;

        case StateInSecondPlus:
            if (isspace(c)) state = StateInSecondPlus;
            else if (isdigit(c)) state = StateInSecondNumber;
            else state = StateBeforeOffset;
            break;

        case StateInSecondNumber:
            if (isdigit(c)) state = StateInSecondNumber;
            else goto done;
            break;
        }
    }

 done:
    if (state == StateInSecondNumber) {
        if (end) *end = s;
        return start;
    }

    return NULL;

} /* find_metamode_offset() */



/*
 * remove_metamode_offsets() - remove any offset specifications from
 * the MetaMode option for this screen; if we find any offsets, return
 * TRUE and assign old_metamodes and new_metamodes to copies of the
 * MetaModes string before and after removing the offsets.  If no
 * offsets appear in the MetaModes string, return FALSE.
 */

static int remove_metamode_offsets(XConfigScreenPtr screen,
                                   char **old_metamodes, char **new_metamodes)
{
    char *start, *end = NULL;
    char *new_string;
    char *n, *o, *tmp;

    XConfigOptionPtr opt = get_screen_option(screen, "MetaModes");

    /* return if no MetaModes option */

    if (!opt || !opt->val) return FALSE;

    /* return if no explicit offsets in the MetaModes option */

    if (!find_metamode_offset(opt->val, NULL)) return FALSE;

    if (old_metamodes) *old_metamodes = nvstrdup(opt->val);

    /*
     * if we get this far, there are offsets in the MetaModes string;
     * build a new string without the offsets
     */

    new_string = nvstrdup(opt->val);

    o = start = opt->val;
    n = new_string;

    while (1) {

        tmp = find_metamode_offset(start, &end);

        if (tmp) *tmp = '\0';

        while (*o) *n++ = *o++;

        *n = '\0';

        if (!tmp) break;

        o = start = end;
    }

    nvfree(opt->val);

    opt->val = new_string;

    if (new_metamodes) *new_metamodes = nvstrdup(opt->val);

    return TRUE;

} /* remove_metamode_offsets() */



/*
 * update_display_options() - update the Display SubSection options
 */

static void update_display_options(Options *op, XConfigScreenPtr screen)
{
    XConfigDisplayPtr display;
    int i;
    
    /* update the mode list, based on what we have on the commandline */
    
    for (display = screen->displays; display; display = display->next) {

        /*
         * if virtual.[xy] are less than 0, then clear the virtual
         * screen size; if they are greater than 0, assign the virtual
         * screen size; if they are 0, leave the virtual screen size
         * alone
         */

        if ((op->virtual.x < 0) || (op->virtual.y < 0)) {
            display->virtualX = display->virtualY = 0;
        } else if (op->virtual.x || op->virtual.y) {
            display->virtualX = op->virtual.x;
            display->virtualY = op->virtual.y;
        }
        
        for (i = 0; i < op->remove_modes.n; i++) {
            xconfigRemoveMode(&display->modes, op->remove_modes.t[i]);
        }
        for (i = 0; i < op->add_modes.n; i++) {
            xconfigAddMode(&display->modes, op->add_modes.t[i]);
        }
        if (op->add_modes_list.n) {
            int mode_list_size = op->add_modes_list.n;

            xconfigFreeModeList(&display->modes);
            display->modes = NULL;

            /*
             * xconfigAddMode() prepends, rather than appends, so add the
             * modes in reverse order
             */

            for (i = 0; i < op->add_modes_list.n; i++) {
                xconfigAddMode(&display->modes,
                               op->add_modes_list.t[mode_list_size-i-1]);
            }
        }
        
        /* XXX should we sort the mode list? */

        /*
         * XXX should we update the mode list with what we can get
         * through libnvidia-cfg?
         */
    }
    
} /* update_display_options() */



/*
 * update_options() - update the X Config options, based on the
 * command line arguments.
 */

void update_options(Options *op, XConfigScreenPtr screen)
{
    int i;
    const NvidiaXConfigOption *o;
    char *val;
    char scratch[8];

    /* update any boolean options specified on the commandline */

    for (i = 0; i < XCONFIG_BOOL_OPTION_COUNT; i++) {
        if (GET_BOOL_OPTION(op->boolean_options, i)) {
            
            /*
             * SEPARATE_X_SCREENS_BOOL_OPTION, XINERAMA_BOOL_OPTION,
             * COMPOSITE_BOOL_OPTION, PRESERVE_BUSID_BOOL_OPTION,
             * and ENABLE_EXTERNAL_GPU_BOOL_OPTION are handled separately
             */

            if (i == SEPARATE_X_SCREENS_BOOL_OPTION) continue;
            if (i == XINERAMA_BOOL_OPTION) continue;
            if (i == COMPOSITE_BOOL_OPTION) continue;
            if (i == PRESERVE_BUSID_BOOL_OPTION) continue;
            if (i == ENABLE_PRIME_OPTION) continue;
            if (i == ENABLE_EXTERNAL_GPU_BOOL_OPTION) continue;

            o = get_option(i);
            
            if (!o) {
                nv_error_msg("Unrecognized X Config option %d", i);
                continue;
            }

            if (GET_BOOL_OPTION(op->boolean_option_values, i)) {
                val = o->invert ? "False" : "True";
            } else {
                val = o->invert ? "True" : "False";
            }
            
            set_option_value(screen, o->name, val);
            nv_info_msg(NULL, "Option \"%s\" \"%s\" added to Screen \"%s\".",
                        o->name, val, screen->identifier);
        }
    }

    /* update the Display SubSection options */
    
    update_display_options(op, screen);

    /* add the transparent index option */
    
    if (op->transparent_index != -1) {
        remove_option(screen, "transparentindex");
        if (op->transparent_index != -2) {
            snprintf(scratch, 8, "%d", op->transparent_index);
            set_option_value(screen, "TransparentIndex", scratch);
        }
    }

    /* add the stereo option */
    
    if (op->stereo != -1) {
        remove_option(screen, "stereo");
        if (op->stereo != -2) {
            snprintf(scratch, 8, "%d", op->stereo);
            set_option_value(screen, "Stereo", scratch);
        }
    }

    /* add the MultiGPU option */

    if (op->multigpu) {
        remove_option(screen, "MultiGPU");
        if (op->multigpu != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "MultiGPU", op->multigpu);
        }
    }

    /* add the SLI option */

    if (op->sli) {
        remove_option(screen, "SLI");
        if (op->sli != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "SLI", op->sli);
        }
    }

    /* add the metamodes option */

    if (op->metamodes_str) {
        remove_option(screen, "MetaModes");
        if (op->metamodes_str != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "MetaModes", op->metamodes_str);
        }
    }

    /* add acpid socket path option*/
 
    if (op->acpid_socket_path) {
        remove_option(screen, "AcpidSocketPath");
        if (op->acpid_socket_path != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "AcpidSocketPath", op->acpid_socket_path);
        }
    }

    /* add the nvidia xinerama info order option */

    if (op->nvidia_xinerama_info_order) {
        remove_option(screen, "nvidiaXineramaInfoOrder");
        if (op->nvidia_xinerama_info_order != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "nvidiaXineramaInfoOrder",
                             op->nvidia_xinerama_info_order);
        }
    }

    /* add the metamode orientation option */
    
    if (op->metamode_orientation) {
        remove_option(screen, "MetaModeOrientation");
        if (op->metamode_orientation != NV_DISABLE_STRING_OPTION) {
            char *old_metamodes, *new_metamodes;
            set_option_value(screen, "MetaModeOrientation",
                             op->metamode_orientation);
            if (remove_metamode_offsets(screen,
                                        &old_metamodes, &new_metamodes)) {
                nv_warning_msg("The MetaModes option contained explicit offsets, "
                               "which would have overridden the specified "
                               "MetaModeOrientation; in order to honor the "
                               "requested MetaModeOrientation, the explicit offsets "
                               "have been removed from the MetaModes option.\n\n"
                               "Old MetaModes option: \"%s\"\n"
                               "New MetaModes option: \"%s\".",
                               old_metamodes, new_metamodes);
                nvfree(old_metamodes);
                nvfree(new_metamodes);
            }
        }
    }

    /* add the UseDisplayDevice option */
 
    if (op->use_display_device) {
        remove_option(screen, "UseDisplayDevice");
        if (op->use_display_device != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "UseDisplayDevice",
                             op->use_display_device);
        }
    }

    /* add the CustomEDID option */

    if (op->custom_edid) {
        remove_option(screen, "CustomEDID");
        if (op->custom_edid != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "CustomEDID", op->custom_edid);
        }
    }

    /* add the TVStandard option */

    if (op->tv_standard) {
        remove_option(screen, "TVStandard");
        if (op->tv_standard != NV_DISABLE_STRING_OPTION) {
           set_option_value(screen, "TVStandard", op->tv_standard);
        }
    }

    /* add the TVOutFormat option */

    if (op->tv_out_format) {
        remove_option(screen, "TVOutFormat");
        if (op->tv_out_format != NV_DISABLE_STRING_OPTION) {
           set_option_value(screen, "TVOutFormat", op->tv_out_format);
        }
    }

    /* add the Coolbits option */

    if (op->cool_bits != -1) {
        remove_option(screen, "Coolbits");
        if (op->cool_bits != -2) {
            snprintf(scratch, 8, "%d", op->cool_bits);
            set_option_value(screen, "Coolbits", scratch);
        }
    }

    /* add the ConnectedMonitor option */

    if (op->connected_monitor) {
        remove_option(screen, "ConnectedMonitor");
        if (op->connected_monitor != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "ConnectedMonitor", op->connected_monitor);
        }
    }

    if (op->registry_dwords) {
        remove_option(screen, "RegistryDwords");
        if (op->registry_dwords != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "RegistryDwords", op->registry_dwords);
        }
    }

    /* add the ColorSpace option */

    if (op->color_space) {
        remove_option(screen, "ColorSpace");
        if (op->color_space != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "ColorSpace", op->color_space);
        }
    }

    if (op->color_range) {
        remove_option(screen, "ColorRange");
        if (op->color_range != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "ColorRange", op->color_range);
        }
    }

    /* add the flatpanel properties option */

    if (op->flatpanel_properties) {
        remove_option(screen, "FlatPanelProperties");
        if (op->flatpanel_properties != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "FlatPanelProperties",
                             op->flatpanel_properties);
        }
    }

    /* add the 3DVisionUSBPath option */
    if (op->nvidia_3dvision_usb_path) {
        remove_option(screen, "3DVisionUSBPath");
        if (op->nvidia_3dvision_usb_path != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "3DVisionUSBPath", op->nvidia_3dvision_usb_path);
        }
    }

    /* add the 3DVisionProConfigFile option */
    if (op->nvidia_3dvisionpro_config_file) {
        remove_option(screen, "3DVisionProConfigFile");
        if (op->nvidia_3dvisionpro_config_file != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "3DVisionProConfigFile", op->nvidia_3dvisionpro_config_file);
        }
    }

    /* add the 3DVisionDisplayType option */

    if (op->nvidia_3dvision_display_type != -1) {
        remove_option(screen, "3DVisionDisplayType");
        if (op->nvidia_3dvision_display_type != -2) {
            snprintf(scratch, 8, "%d", op->nvidia_3dvision_display_type);
            set_option_value(screen, "3DVisionDisplayType", scratch);
        }
    }

    /* add the ForceCompositionPipeline option */

    if (op->force_composition_pipeline) {
        remove_option(screen, "ForceCompositionPipeline");
        if (op->force_composition_pipeline != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "ForceCompositionPipeline",
                             op->force_composition_pipeline);
        }
    }

    /* add the ForceFullCompositionPipeline option */

    if (op->force_full_composition_pipeline) {
        remove_option(screen, "ForceFullCompositionPipeline");
        if (op->force_full_composition_pipeline != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "ForceFullCompositionPipeline",
                             op->force_full_composition_pipeline);
        }
    }

    /* add the AllowHMD option */

    if (op->allow_hmd) {
        remove_option(screen, "AllowHMD");
        if (op->allow_hmd != NV_DISABLE_STRING_OPTION) {
            set_option_value(screen, "AllowHMD", op->allow_hmd);
        }
    }

} /* update_options() */
