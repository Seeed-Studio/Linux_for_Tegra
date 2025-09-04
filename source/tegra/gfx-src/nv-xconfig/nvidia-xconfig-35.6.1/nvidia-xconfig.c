/*
 * nvidia-xconfig: A tool for manipulating X config files,
 * specifically for use by the NVIDIA Linux graphics driver.
 *
 * Copyright (C) 2005 NVIDIA Corporation
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
 * nvidia-xconfig.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <libgen.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "nvidia-xconfig.h"
#include "nvgetopt.h"
#include "msg.h"

#define TAB    "  "
#define BIGTAB "      "

#define ORIG_SUFFIX   ".nvidia-xconfig-original"
#define BACKUP_SUFFIX ".backup"


/*
 * print_version() - print version information
 */

static void print_version(void)
{
    nv_info_msg(NULL, "");
    nv_info_msg(NULL, "%s", NV_ID_STRING);
    nv_info_msg(TAB, "The NVIDIA X Configuration Tool.");
    nv_info_msg(NULL, "");
    nv_info_msg(TAB, "This program is used to manipulate X configuration files, "
                     "specifically to enable NVIDIA X driver functionality.");
    nv_info_msg(NULL, "");
} /* print_version() */


static void print_summary(void)
{
    nv_info_msg(NULL, "");
    nv_info_msg(TAB, "In its normal operation, nvidia-xconfig finds the system "
                     "X configuration file (or generates a new X configuration "
                     "if it cannot find the system file), makes sure the "
                     "configuration is usable by the NVIDIA X driver, applies "
                     "any updates requested on the commandline, and writes the "
                     "new configuration to file.");
    nv_info_msg(NULL, "");
    nv_info_msg(TAB, "Please see the NVIDIA README for a description of NVIDIA "
                     "X configuration file options.");
    nv_info_msg(NULL, "");
}


#include "option_table.h"


/*
 * print_help() - loop through the __options[] table, and print the
 * description of each option.
 */

static void print_help_helper(const char *name, const char *description)
{
    nv_info_msg(TAB, "%s", name);
    nv_info_msg(BIGTAB, "%s", description);
    nv_info_msg(NULL, "");
}

static void print_help(int advanced)
{
    unsigned int include_mask = 0;

    print_version();
    print_summary();

    nv_info_msg(NULL, "");
    nv_info_msg(NULL, "nvidia-xconfig [options]");
    nv_info_msg(NULL, "");

    if (!advanced) {
        /* only print options with the ALWAYS flag */
        include_mask |= NVGETOPT_HELP_ALWAYS;
    }

    nvgetopt_print_help(__options, include_mask, print_help_helper);
}



/*
 * parse_commandline() - malloc an Options structure, initialize it,
 * and fill in any pertinent data from the commandline arguments
 */

static void parse_commandline(Options *op, int argc, char *argv[])
{
    int c, boolval;
    char *strval;
    int intval, disable;
    double doubleval;

    
    while (1) {
        
        c = nvgetopt(argc, argv, __options, &strval,
                     &boolval, &intval, &doubleval, &disable);

        if (c == -1)
            break;

        /* catch the boolean options */

        if ((c >= XCONFIG_BOOL_OPTION_START) &&
            (c <= (XCONFIG_BOOL_OPTION_START + XCONFIG_BOOL_OPTION_COUNT))) {

            if (!check_boolean_option(op, c - XCONFIG_BOOL_OPTION_START, boolval)) {
                goto fail;
            }
            set_boolean_option(op, c - XCONFIG_BOOL_OPTION_START, boolval);
            
            continue;
        }
        
        switch (c) {

        case 'v': print_version(); exit(0); break;
        case 'c': op->xconfig = strval; break;
        case 'o': op->output_xconfig = strval; break;
        case 't': op->tree = TRUE; break;
        case 'T': op->post_tree = TRUE; break;
        case 'h': print_help(FALSE); exit(0); break;
        case 'A': print_help(TRUE); exit(0); break;
        case 's': nv_set_verbosity(NV_VERBOSITY_WARNING); break;
        case 'a': op->enable_all_gpus = TRUE; break;
        case '1': op->only_one_screen = TRUE; break;
        
        case 'd': op->depth = intval;
            if ((op->depth != 8) &&
                (op->depth != 15) &&
                (op->depth != 16) &&
                (op->depth != 24) &&
                (op->depth != 30)) {
                fprintf(stderr, "\n");
                fprintf(stderr, "Invalid depth: %d.\n", op->depth);
                fprintf(stderr, "\n");
                goto fail;
            }
            break;

        case LAYOUT_OPTION: op->layout = strval; break;
        case SCREEN_OPTION: op->screen = strval; break;
        case DEVICE_OPTION: op->device = strval; break;
        case BUSID_OPTION:
            if (GET_BOOL_OPTION(op->boolean_option_values, ENABLE_PRIME_OPTION)) {
                fprintf(stderr, "Unable to disable BUSID with PRIME enabled.\n");
                goto fail;
            }
            op->busid = disable ? NV_DISABLE_STRING_OPTION : strval;
            break;

        case X_PREFIX_OPTION: op->gop.x_project_root = strval; break;

        case KEYBOARD_OPTION: op->gop.keyboard = strval; break;
        case KEYBOARD_LIST_OPTION: op->keyboard_list = TRUE; break;
        case KEYBOARD_DRIVER_OPTION: op->gop.keyboard_driver = strval; break;
            
        case MOUSE_OPTION: op->gop.mouse = strval; break;
        case MOUSE_LIST_OPTION: op->mouse_list = TRUE; break;
            
        case NVIDIA_CFG_PATH_OPTION: op->nvidia_cfg_path = strval; break;

        case FORCE_GENERATE_OPTION: op->force_generate = TRUE; break;

        case ACPID_SOCKET_PATH_OPTION: 
            if (disable) {
                op->acpid_socket_path = NV_DISABLE_STRING_OPTION;
            } else {
                op->acpid_socket_path = strval; 
            }
            break;

        case HANDLE_SPECIAL_KEYS_OPTION:
            {
                const char *valid_values[] = {
                    "Always",
                    "Never",
                    "WhenNeeded",
                    NULL,
                };

                int i;

                if (disable) {
                    op->handle_special_keys = NV_DISABLE_STRING_OPTION;
                    break;
                }

                for (i = 0; valid_values[i]; i++) {
                    if (!strcasecmp(strval, valid_values[i])) {
                        break;
                    }
                }

                if (valid_values[i]) {
                    op->handle_special_keys = strval;
                } else {
                    fprintf(stderr, "Invalid HandleSpecialKeys option: %s.\n", strval);
                    goto fail;
                }
                break;
            }

        case TRANSPARENT_INDEX_OPTION:

            /* mark as disabled, so we can remove the option later */
            
            if (disable) {
                op->transparent_index = -2;
                break;
            }

            if (intval < 0 || intval > 255) {
                fprintf(stderr, "\n");
                fprintf(stderr, "Invalid transparent index: %d.\n", intval);
                fprintf(stderr, "\n");
                goto fail;
            }
            op->transparent_index = intval;
            break;

        case TV_STANDARD_OPTION:
            
            {
                const char* valid_values[] = {
                    "PAL-B",
                    "PAL-D",
                    "PAL-G",
                    "PAL-H",
                    "PAL-I",
                    "PAL-K1",
                    "PAL-M",
                    "PAL-N",
                    "PAL-NC",
                    "NTSC-J",
                    "NTSC-M",
                    "HD480i",
                    "HD480p",
                    "HD720p",
                    "HD1080i",
                    "HD1080p",
                    "HD576i",
                    "HD576p",
                    NULL
                };
                int i;
                
                /* mark as disabled, so we can remove the option later */

                if (disable) {
                    op->tv_standard = NV_DISABLE_STRING_OPTION;
                    break;
                }

                for (i = 0; valid_values[i]; i++) {
                    if (!strcasecmp(strval, valid_values[i]))
                        break;
                }

                if (valid_values[i]) {
                    op->tv_standard = strval;
                } else {
                    fprintf(stderr, "Invalid TVStandard option: %s.\n", strval);
                    goto fail;
                }
            }
            break;

        case TV_OUT_FORMAT_OPTION:
            
            /* mark as disabled, so we can remove the option later */

            if (disable) {
                op->tv_out_format = NV_DISABLE_STRING_OPTION;
                break;
            }

            if (!strcasecmp(strval, "SVIDEO")) {
                op->tv_out_format = "SVIDEO";
            } else if (!strcasecmp(strval, "COMPOSITE")) {
                op->tv_out_format = "COMPOSITE";
            } else {
                fprintf(stderr, "Invalid TVOutFormat option: %s.\n", strval);
                goto fail;
            }
            break;

        case COOL_BITS_OPTION:

            /* mark as disabled, so we can remove the option later */

            if (disable) {
                op->cool_bits = -2;
                break;
            }

            op->cool_bits = intval;
            break;

        case STEREO_OPTION:

            /* mark as disabled, so we can remove the option later */
            
            if (disable) {
                op->stereo = -2;
                break;
            }

            if (intval < 0 || intval > 14) {
                fprintf(stderr, "\n");
                fprintf(stderr, "Invalid stereo: %d.\n", intval);
                fprintf(stderr, "\n");
                goto fail;
            }
            op->stereo = intval;
            break;

        case MODE_OPTION:
            if (boolval) {
                /* add this mode */
                nv_text_rows_append(&op->add_modes, strval);
            } else {
                /* remove this mode */
                nv_text_rows_append(&op->remove_modes, strval);
            }
            break;

        case MODE_LIST_OPTION:
            {
                char *token;
                token = strtok(strval, " ");
                if (!token) {
                    fprintf(stderr, "\n");
                    fprintf(stderr, "Invalid Mode List string: %s.\n", strval);
                    fprintf(stderr, "\n");
                    goto fail;
                }
                do {
                    nv_text_rows_append(&op->add_modes_list, token);
                    token = strtok(NULL, " ");
                } while (token != NULL);

                break;
            }

        case REMOVE_MODE_OPTION:
            nv_text_rows_append(&op->remove_modes, strval);
            break;

        case META_MODES_OPTION:
            op->metamodes_str = strval;
            break;

        case MULTI_GPU_OPTION: /* fall through */
        case SLI_OPTION:
            {
                const char* valid_values[] = {
                    "0",
                    "no",
                    "off",
                    "false",
                    "single",
                    "mosaic",
                    NULL
                };

                if (disable) {

                    /* mark as disabled, so we can remove the option later */
                    strval = NV_DISABLE_STRING_OPTION;

                } else {

                    /* check that the string is valid */
                    int i;

                    for (i = 0; valid_values[i]; i++) {
                        if (!strcasecmp(strval, valid_values[i]))
                            break;
                    }

                    if (!valid_values[i]) {
                        fprintf(stderr, "Invalid SLI option: %s.\n", strval);
                        goto fail;
                    }
                }

                if (c == MULTI_GPU_OPTION) {
                    op->multigpu = strval;
                } else {
                    op->sli = strval;
                }
            }
            break;

        case PRESERVE_DRIVER_NAME_OPTION: op->preserve_driver = TRUE; break;
        
        case DISABLE_SCF_OPTION: op->disable_scf = TRUE; break;
        
        case QUERY_GPU_INFO_OPTION: op->query_gpu_info = TRUE; break;

        case 'E':
            op->extract_edids_from_file = strval;
            break;

        case EXTRACT_EDIDS_OUTPUT_FILE_OPTION:
            op->extract_edids_output_file = strval;
            break;

        case NVIDIA_XINERAMA_INFO_ORDER_OPTION:
            op->nvidia_xinerama_info_order =
                disable ? NV_DISABLE_STRING_OPTION : strval;
            break;

        case METAMODE_ORIENTATION_OPTION:
            {
                const char* valid_values[] = {
                    "RightOf",
                    "LeftOf",
                    "Above",
                    "Below",
                    "Clone",
                    NULL
                };
                int i;

                if (disable) {
                    op->metamode_orientation = NV_DISABLE_STRING_OPTION;
                    break;
                }
                
                for (i = 0; valid_values[i]; i++) {
                    if (!strcasecmp(strval, valid_values[i]))
                        break;
                }

                if (!valid_values[i]) {
                    fprintf(stderr, "Invalid MetaModeOrientation option: "
                            "\"%s\".\n", strval);
                    goto fail;
                }
                
                op->metamode_orientation = strval;
            }
            break;

        case VIRTUAL_OPTION:
            {
                int ret, x, y;

                if (disable) {
                    op->virtual.x = op->virtual.y = -1;
                    break;
                }

                ret = sscanf(strval, "%dx%d", &x, &y);
                
                if (ret != 2) {
                    fprintf(stderr, "Invalid Virtual option: \"%s\".\n",
                            strval);
                    goto fail;
                }
                
                op->virtual.x = x;
                op->virtual.y = y;
                
                break;
            }

        case USE_DISPLAY_DEVICE_OPTION:
            op->use_display_device = 
                disable ? NV_DISABLE_STRING_OPTION : strval;
            break;
       
        case CUSTOM_EDID_OPTION:
            op->custom_edid = disable ? NV_DISABLE_STRING_OPTION : strval;
            break;

        case CONNECTED_MONITOR_OPTION:
            op->connected_monitor =
                disable ? NV_DISABLE_STRING_OPTION : strval;
            break;

        case REGISTRY_DWORDS_OPTION:
            op->registry_dwords = disable ? NV_DISABLE_STRING_OPTION : strval;
            break;

        case COLOR_SPACE_OPTION:
            op->color_space = disable ? NV_DISABLE_STRING_OPTION : strval;
            break;

        case COLOR_RANGE_OPTION:
            op->color_range = disable ? NV_DISABLE_STRING_OPTION : strval;
            break;

        case FLATPANEL_PROPERTIES_OPTION:
            op->flatpanel_properties =
                disable ? NV_DISABLE_STRING_OPTION : strval;
            break;

        case NVIDIA_3DVISION_USB_PATH_OPTION:
            op->nvidia_3dvision_usb_path = disable ? NV_DISABLE_STRING_OPTION : strval;
            break;

        case NVIDIA_3DVISIONPRO_CONFIG_FILE_OPTION:
            op->nvidia_3dvisionpro_config_file = disable ? NV_DISABLE_STRING_OPTION : strval;
            break;

        case NVIDIA_3DVISION_DISPLAY_TYPE_OPTION:

            /* mark as disabled, so we can remove the option later */

            if (disable) {
                op->nvidia_3dvision_display_type = -2;
                break;
            }

            if (intval < 0 || intval > 2) {
                fprintf(stderr, "\n");
                fprintf(stderr, "Invalid 3D Vision display type option: %d.\n", intval);
                fprintf(stderr, "\n");
                goto fail;
            }
            op->nvidia_3dvision_display_type = intval;
            break;

        case RESTORE_ORIGINAL_BACKUP_OPTION:
            op->restore_original_backup = TRUE;
            break;

        case NUM_X_SCREENS_OPTION:

            if (intval < 1) {
                fprintf(stderr, "\n");
                fprintf(stderr, "Invalid number of X screens: %d.\n", intval);
                fprintf(stderr, "\n");
                goto fail;
            }

            /* Enable separate X screens */
            set_boolean_option(op,
                    XCONFIG_BOOL_VAL(SEPARATE_X_SCREENS_BOOL_OPTION) -
                    XCONFIG_BOOL_OPTION_START, TRUE);

            op->num_x_screens = intval;
            break;

        case FORCE_COMPOSITION_PIPELINE_OPTION:
            op->force_composition_pipeline =
                disable ? NV_DISABLE_STRING_OPTION : strval;
            break;

        case FORCE_FULL_COMPOSITION_PIPELINE_OPTION:
            op->force_full_composition_pipeline =
                disable ? NV_DISABLE_STRING_OPTION : strval;
            break;

        case ALLOW_HMD_OPTION:
            op->allow_hmd = disable ? NV_DISABLE_STRING_OPTION : strval;
            break;

        default:
            goto fail;
        }
    }

    /* do tilde expansion on the filenames given */
    
    op->xconfig = tilde_expansion(op->xconfig);
    op->output_xconfig = tilde_expansion(op->output_xconfig);

    return;
    
 fail:
    
    fprintf(stderr, "\n");
    fprintf(stderr, "Invalid commandline, please run `%s --help` "
            "for usage information.\n", argv[0]);
    fprintf(stderr, "\n");
    exit(1);

} /* parse_commandline() */



/*
 * load_default_options - malloc an Options structure
 * and initialize it with default values.
 *
 */

static Options *load_default_options(void)
{
    Options *op;

    op = (Options *) nvalloc(sizeof(Options));
    if (!op) return NULL;
    
    op->depth = 24;
    op->transparent_index = -1;
    op->stereo = -1;
    op->cool_bits = -1;
    op->nvidia_3dvision_display_type = -1;
    op->tv_over_scan = -1.0;
    op->num_x_screens = -1;

    xconfigGenerateLoadDefaultOptions(&op->gop);

    /*
     * XXX save the option structure so that printing routines can
     * access it (and so that we don't have to carry the op everywhere
     * just so that we can pass it to printing routines).
     */
    {
        extern Options *__op;
        
        __op = op;
    }
    
    return op;

} /* load_default_options() */



/*
 * backup_file() - create a backup of orig_filename, naming the backup
 * file "<orig_filename>.<suffix>".
 *
 * XXX If we fail to write to the backup file (eg, it is in a
 * read-only directory), then we should do something intelligent like
 * write the backup to the user's home directory.
 */

static int backup_file(Options *op, const char *orig_filename,
                       const char *suffix)
{
    char *filename;
    int ret = FALSE;
    
    /* construct the backup filename */
    
    filename = nvstrcat(orig_filename, suffix, NULL);
    
    /* if the backup file already exists, remove it */

    if (access(filename, F_OK) == 0) {
        if (unlink(filename) != 0) {
            nv_error_msg("Unable to create backup file '%s' (%s)",
                         filename, strerror(errno));
            goto done;
        }
    }

    /* copy the file */

    if (!copy_file(orig_filename, filename, 0644)) {
        /* copy_file() prints out its own error messages */
        goto done;
    }
    
    nv_info_msg(NULL, "Backed up file '%s' as '%s'", orig_filename, filename);
    ret = TRUE;
    
 done:

    free(filename);
    
    return ret;
    
} /* backup_file() */



/*
 * find_xconfig() - search for an X config file.
 *
 * We search for the filename is this order:
 *
 * 1) "--output-xconfig" option
 *
 * 2) config->filename
 *
 * 3) use xf86openConfigFile()
 */

static char *find_xconfig(Options *op, XConfigPtr config)
{
    char *filename = NULL;
    
    /* 1) "--output-xconfig" option */
    
    if (op->output_xconfig) {
        filename = nvstrdup(op->output_xconfig);
    }
    
    /* config->filename */

    if (!filename && config && config->filename) {
        filename = nvstrdup(config->filename);
    }
    
    /* use xf86openConfigFile() */
    
    if (!filename) {
        const char *f;
        f = xconfigOpenConfigFile(NULL, op->gop.x_project_root);
        if (f) {
            /* dup the string since closing the config file will free
               the string */
            filename = nvstrdup(f);
            xconfigCloseConfigFile();
        }
    }

    if (!filename) {
        filename = nvstrdup("/etc/X11/xorg.conf");
    }

    return filename;
} /* find_xconfig() */


/*
 * restore_backup - search for a backup file with the given suffix;
 * if one is found, restore it.
 */
static int restore_backup(Options *op, XConfigPtr config, const char *suffix)
{
    char *filename = find_xconfig(op, config);
    char *backup = nvstrcat(filename, suffix, NULL);
    struct stat st;
    int ret = FALSE;

    if (lstat(backup, &st) != 0) {
        nv_error_msg("Unable to restore from original backup file '%s' (%s)",
                     backup, strerror(errno));
        goto done;
    }

    /*
     * do not restore files if the permissions might allow a malicious user to
     * modify the backup, potentially tricking an administrator into restoring
     * the modified backup.
     */
    if (!S_ISREG(st.st_mode)  /* non-regular files */ ||
        st.st_uid != 0        /* not owned by root*/  ||
        (st.st_gid != 0 && (st.st_mode & S_IWGRP)) /* non-root group write */ ||
        (st.st_mode & S_IWOTH) /* world writable */ ) {
        nv_error_msg("The permissions of the original backup file '%s' are too "
                     "loose to be trusted. The file will not be restored.", backup);
        goto done;
    }
    
    /*
     * if the backup is empty, assume that no original x config file existed
     * and delete the current X config file.
     */

    if (st.st_size == 0) {
        if (unlink(filename) != 0) {
            nv_error_msg("Unable to remove file '%s' (%s)",
                         filename, strerror(errno));
             goto done;
        }
    } else {
        /* copy the file */

        if (!copy_file(backup, filename, 0644)) {
            /* copy_file() prints out its own error messages */
            goto done;
        }
    }

    /* remove backup: a new one is created if nvidia-xconfig is run again */

    if (access(backup, F_OK) == 0) {
        if (unlink(backup) != 0) {
            nv_error_msg("Unable to remove backup file '%s' (%s)",
                         backup, strerror(errno));
            goto done;
        }
    }

    if (st.st_size == 0) {
        nv_info_msg(NULL, "The backup file '%s' was empty. This usually means "
                          "that nvidia-xconfig did not find an X configuration "
                          "file the first time it was run. The X configuration "
                          "file '%s' was deleted.",
                    backup, filename);
    } else {
        nv_info_msg(NULL, "Restored backup file '%s' to '%s'", backup, filename);
    }

    ret = TRUE;

 done:

    free(backup);
    free(filename);
    
    return ret;
} /* restore_backup() */



/*
 * write_xconfig() - write the Xconfig to file.
 */

static int write_xconfig(Options *op, XConfigPtr config, int first_touch)
{
    char *filename = find_xconfig(op, config);
    char *d, *tmp = NULL;
    int ret = FALSE;

    /*
     * XXX it's strange that lack of permission to write to the target
     * location (the likely case with users not having write
     * permission on /etc/X11/ will fail at backup time, rather than
     * when we try to write the new file.  Perhaps we should backup to
     * a temporary location where we know we will have write access,
     * try to do the write, and only if we succeed in the write, move
     * the backup file into place.
     */
    
    /* check that we can write to this location */
    
    tmp = nvstrdup(filename);
    d = dirname(tmp);
    if (access(d, W_OK) != 0) {
        nv_error_msg("Unable to write to directory '%s'.", d);
        goto done;
    }
    
    /* 
     * if the file already exists, create a backup first. if this is our first
     * time writing the x config, create a separate "original" backup file.
     */
    
    if (access(filename, F_OK) == 0) {
        if (first_touch && !backup_file(op, filename, ORIG_SUFFIX)) goto done;
        if (!backup_file(op, filename, BACKUP_SUFFIX)) goto done;
    }

    /* 
     * if no file exists, and this is our first time writing the x config, back
     * up an empty file to use as the "original" backup.
     */

    else if (first_touch) {
        char *fakeorig = nvstrcat(filename, ORIG_SUFFIX, NULL);
        if (!copy_file("/dev/null", fakeorig, 0644)) {
            nv_warning_msg("Unable to write an empty backup file \"%s\".",
                           fakeorig);
        }
        free(fakeorig);
    }
    
    /* write the config file */

    if (!xconfigWriteConfigFile(filename, config)) {
        nv_error_msg("Unable to write file \"%s\"; please use the "
                     "\"--output-xconfig\" commandline option to specify "
                     "an alternative output file.", filename);
        goto done;
    }

    nv_info_msg(NULL, "New X configuration file written to '%s'", filename);
    nv_info_msg(NULL, "");
    
    
    /* Set the default depth in the Solaris Management Facility 
     * to the default depth of the first screen 
     */
    if (op->disable_scf == FALSE) {
        if (!update_scf_depth(config->screens[0].defaultdepth)) {
            goto done;
        }
    }
    
    ret = TRUE;
    
 done:

    if (filename) free(filename);
    if (tmp) free(tmp);

    return ret;

} /* write_xconfig() */



/*
 * find_banner_prefix() - helper for update_banner(); like
 * 'strstr("# nvidia-xconfig:")' but allows arbitrary whitespace between
 * '#' and 'n'.
 */

static char *find_banner_prefix(char *str)
{
    char *s, *comment = NULL;

    for (s = str; s && *s; s++) {
        char c = *s;

        /*
         * if we aren't presently looking at a comment, and we found the
         * start of a comment, then save it and goto the next char
         */

        if ((!comment) && (c == '#')) {
            comment = s;
            continue;
        }

        if (comment) {
            /* ignore space within the comment */
            if (isspace(c)) {
                continue;
            }
            /* if the prefix matches, then return the start of the comment */
            if (strncmp(s, "nvidia-xconfig:", 15) == 0) {
                return comment;
            }
        }

        /* anything else forces us out of any current comment */
        comment = NULL;
    }
    return NULL;

} /* find_banner_prefix() */



/*
 * update_banner() - add our banner at the top of the config, but
 * first we need to remove any lines that already include our prefix
 * (because presumably they are a banner from an earlier run of
 * nvidia-xconfig)
 */

static void update_banner(XConfigPtr config)
{
    static const char *banner =
        "X configuration file generated by nvidia-xconfig\n";
    static const char *prefix =
        "# nvidia-xconfig: ";

    char *s = config->comment;
    char *line, *eol, *tmp;
    
    /* remove all lines that begin with the prefix */
    
    while (s && (line = find_banner_prefix(s))) {
        
        eol = strchr(line, '\n'); /* find the end of the line */
        
        if (eol) {
            eol++;
            if (*eol == '\0') eol = NULL;
        }
        
        if (line == s) { /* the line with the prefix is at the start */
            if (eol) {   /* there is more after the prefix line */
                tmp = strdup(eol);
                free(s);
                s = tmp;
            } else {     /* the prefix line is the only line */
                free(s);
                s = NULL;
            }
        } else {         /* prefix line is in the middle or end */
            *line = '\0';
            tmp = nvstrcat(s, eol, NULL);
            free(s);
            s = tmp;
        }
    }
    
    /* add our prefix lines at the start of the comment */
    
    config->comment = nvstrcat(prefix, banner, "# " NV_ID_STRING "\n", s, NULL);
    
    if (s) free(s);
    
} /* update_banner() */



/*
 * find_system_xconfig() - find the system X config file and parse it;
 * returns XConfigPtr if successful, otherwise returns NULL.
 */

static XConfigPtr find_system_xconfig(Options *op)
{
    const char *filename;
    XConfigPtr config;
    XConfigError error;

    /* Find and open the existing X config file */
    
    filename = xconfigOpenConfigFile(op->xconfig, op->gop.x_project_root);
    
    if (filename) {
        nv_info_msg(NULL, "");
        nv_info_msg(NULL, "Using X configuration file: \"%s\".", filename);
    } else {
        nv_warning_msg("Unable to locate/open X configuration file.");
        return NULL;
    }
    
    /* Read the opened X config file */
    
    error = xconfigReadConfigFile(&config);
    if (error != XCONFIG_RETURN_SUCCESS) {
        xconfigCloseConfigFile();
        return NULL;;
    }

    /* Close the X config file */
    
    xconfigCloseConfigFile();
    
    /* Sanitize the X config file */
    
    if (!xconfigSanitizeConfig(config, op->screen, &(op->gop))) {
        xconfigFreeConfig(&config);
        return NULL;
    }

    return config;

} /* find_system_xconfig() */



/*
 * apply_enable_prime_settings() -  if the ENABLE PRIME boolean option is
 * enabled, add the required xconfig additions.
 * returns a success/fail boolean.
 */

static int apply_enable_prime_settings(Options *op, XConfigPtr config,
                                       XConfigLayoutPtr layout)
{
    DevicesPtr pDevices;

    if (GET_BOOL_OPTION(op->boolean_option_values, ENABLE_PRIME_OPTION)) {
        /* Add an inactive device for the integrated graphics */
        pDevices = find_devices(op);
        if (!pDevices) {
            nv_error_msg("Unable to find any GPUs in the system.");
            return FALSE;
        }
        xconfigAddInactiveDevice(config, layout, pDevices->nDevices);

        nv_info_msg(NULL, "X Configuration file set up for PRIME. Please run "
                          "\"xrandr --setprovideroutputsource modesetting "
                          "NVIDIA-0\" and \"xrandr --auto\" to enable. "
                          "See the README for more details.");
    }
    return TRUE;
} /* apply_enable_prime_settings() */

/*
 * apply_enable_external_gpu_option() - if the ENABLE EXTERNAL GPU boolean
 * option is enabled, add the AllowExternalGpus option to the ServerLayout
 * section
 * returns a success/fail boolean
 */

static int apply_enable_external_gpu_option(Options *op, XConfigPtr config,
                                            XConfigLayoutPtr layout)
{
    int egpu = GET_BOOL_OPTION(op->boolean_option_values,
                               ENABLE_EXTERNAL_GPU_BOOL_OPTION);

    if (GET_BOOL_OPTION(op->boolean_options, ENABLE_EXTERNAL_GPU_BOOL_OPTION)) {
        xconfigAddNewOption(&(layout->options),
                            "AllowExternalGpus",
                            (egpu ? "1" : "0"));

        if (egpu) {
            nv_info_msg(NULL, "X configuration file set up to allow detection "
                              "of External GPUs. If the eGPU does not work, "
                              "you may need to authorize the associated "
                              "Thunderbolt device.\n"
                              "Warning: System may become unstable if the "
                              "eGPU is hot-unplugged while X is running.\n"
                              "See \"Configuring External and Removable "
                              "GPUs\" in the README for more details.");
        }
    }
    return TRUE;
} /* apply_enable_external_gpu_option */


static int update_xconfig(Options *op, XConfigPtr config)
{
    XConfigLayoutPtr layout;
    XConfigAdjacencyPtr adj;
    int updated;

    /* get the layout to update */
    
    layout = get_layout(op, config);
    if (!layout) {
        return FALSE;
    }

    /* apply multi-display options */
    
    if (!apply_multi_screen_options(op, config, layout)) {
        return FALSE;
    }

    /* apply PRIME settings */

    if (!apply_enable_prime_settings(op, config, layout)) {
        return FALSE;
    }

    /* apply eGPU setting */
    if (!apply_enable_external_gpu_option(op, config, layout)) {
        return FALSE;
    }

    /*
     * update the device and option for all screens, or the screen
     * or device that was requested.
     */
    updated = FALSE;

    for (adj = layout->adjacencies; adj; adj = adj->next) {

        if (!adj->screen) {
            continue;
        }

        /* if screen option set: skip adj if not the requested screen */

        if ((op->screen) &&
            (xconfigNameCompare(op->screen, adj->screen->identifier) != 0)) {
            continue;
        }

        /* if device option set: skip adj if not the requested device */

        if ((op->device) &&
            (xconfigNameCompare(op->device, adj->screen->device_name) != 0)) {
            continue;
        }

        update_screen(op, config, adj->screen);
        updated = TRUE;
    }

    if (op->screen && !updated) {
        nv_error_msg("Unable to find screen '%s'", op->screen);
        return FALSE;
    }

    if (op->device && !updated) {
        nv_error_msg("Unable to find device '%s'", op->device);
        return FALSE;
    }

    update_extensions(op, config);

    update_modules(config);

    update_server_flags(op, config);

    update_banner(config);
    
    return TRUE;

} /* update_xconfig() */



/*
 * main program entry point
 *
 * The intended behavior is that, by default, nvidia-xconfig make the
 * system's X config file usable by the NVIDIA X driver.  If
 * nvidia-xconfig cannot file the system's X config file, then it
 * attempts to create one and make that usable.
 *
 *
 */

int main(int argc, char *argv[])
{
    Options *op;
    int ret;
    XConfigPtr config = NULL;
    int first_touch = 0;
    
    /* Load defaults */

    op = load_default_options();
    if (!op) {
        fprintf(stderr, "\nOut of memory error.\n\n");
        return 1;
    }

    /* parse the commandline */

    parse_commandline(op, argc, argv);

    /*
     * first, check for any of special options that cause us to exit
     * early
     */

    if (op->keyboard_list) {
        nv_info_msg(NULL, "\nPossible keyboard types; the short name is what "
                          "should be passed to the \"--keyboard\" option.\n\n");
        xconfigGeneratePrintPossibleKeyboards();
        return 0;
    }

    if (op->mouse_list) {
        nv_info_msg(NULL, "\nPossible mouse types; the short name is what should "
                          "be passed to the \"--mouse\" option.\n\n");
        xconfigGeneratePrintPossibleMice();
        return 0;
    }

    if (op->query_gpu_info) {
        ret = query_gpu_info(op);
        return (ret ? 0 : 1);
    }
 
    if (op->extract_edids_from_file) {
        ret = extract_edids(op);
        return (ret ? 0 : 1);
    }

    if (op->restore_original_backup) {
        config = find_system_xconfig(op);
        xconfigGetXServerInUse(&op->gop);
        ret = restore_backup(op, config, ORIG_SUFFIX);
        return (ret ? 0 : 1);
    }

    /*
     * we want to open and parse the system's existing X config file,
     * if possible
     */

    if (!op->force_generate) {
        config = find_system_xconfig(op);
    }
    
    /*
     * pass the system config (if any) to the tree printer
     */

    if (op->tree) {
        ret = print_tree(op, config);
        return (ret ? 0 : 1);
    }
    
    /*
     * Get which X server is in use: Xorg or XFree86
     */
    xconfigGetXServerInUse(&op->gop);
    
    /*
     * if we failed to find the system's config file, generate a new
     * one
     */
    
    if (!config) {
        config = xconfigGenerate(&op->gop);
        first_touch = 1;
    }

    /*
     * if we don't have a valid config by now, something catestrophic
     * happened
     */

    if (!config) {
        nv_error_msg("Unable to generate a usable X configuration file.");
        return 1;
    }

    /* if a config file existed, check to see if it had an nvidia-xconfig
     * banner: this would suggest that we've touched this file before.
     */

    if (!first_touch) {
        first_touch = (find_banner_prefix(config->comment) == NULL);
    }

    /* now, we have a good config; apply whatever the user requested */
    
    update_xconfig(op, config);

    /* print the config in tree format, if requested */

    if (op->post_tree) {
        ret = print_tree(op, config);
        return (ret ? 0 : 1);
    }
    
    /* write the config back out to file */

    if (!write_xconfig(op, config, first_touch)) {
        return 1;
    }

    return 0;
    
} /* main() */
