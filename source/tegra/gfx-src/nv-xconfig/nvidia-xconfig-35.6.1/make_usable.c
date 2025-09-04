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
 * make_usable.c
 */

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

#include "nvidia-xconfig.h"
#include "xf86Parser.h"
#include "configProcs.h"
#include "msg.h"
#include "nvpci-utils.h"


static void ensure_module_loaded(XConfigPtr config, char *name);
static int update_device(Options *op, XConfigPtr config, XConfigDevicePtr device);
static void update_depth(Options *op, XConfigScreenPtr screen);
static void update_display(Options *op, XConfigScreenPtr screen);

/*
 * ensure_module_loaded() - make sure the given module is present
 */

static void ensure_module_loaded(XConfigPtr config, char *name) {
    XConfigLoadPtr load;
    int found = FALSE;

    for (load = config->modules->loads; load && !found; load = load->next) {
        if (xconfigNameCompare(name, load->name) == 0) found = TRUE;
    }

    if (!found) {
        xconfigAddNewLoadDirective(&config->modules->loads,
                                   name, XCONFIG_LOAD_MODULE,
                                   NULL, FALSE);
    }
} /* ensure_module_loaded */

/*
 * update_modules() - make sure the glx module is present, and remove
 * the GLcore and dri modules if they are present.
 */

int update_modules(XConfigPtr config)
{
    XConfigLoadPtr load, next;

    /*
     * Return early if the original X configuration file lacked a
     * "Module" section, and rely on the server's builtin list
     * of modules to load, instead. We can safely do this if the
     * X server is an X.Org server or XFree86 release >= 4.4.0. On
     * installations with older XFree86 servers, the vendor's X
     * configuration utility should have added a "Module" section
     * we can extend, if necessary.
     */
    if (config->modules == NULL)
        return FALSE;

    /* make sure all our required modules are loaded */
    ensure_module_loaded(config, "glx");
#if defined(NV_SUNOS)
    ensure_module_loaded(config, "xtsol");
#endif // defined(NV_SUNOS)

    /* make sure GLcore and dri are not loaded */

    load = config->modules->loads;
    while (load) {
        next = load->next;
        if (xconfigNameCompare("GLcore", load->name) == 0) {
            xconfigRemoveLoadDirective(&config->modules->loads, load);
        } else if (xconfigNameCompare("dri", load->name) == 0) {
            xconfigRemoveLoadDirective(&config->modules->loads, load);
        }
        load = next;
    }

    return TRUE;
    
} /* update_modules() */



/*
 * update_screen() - apply any requested updates to the given screen
 */

int update_screen(Options *op, XConfigPtr config, XConfigScreenPtr screen)
{
    /* migrate any options from device to screen to avoid conflicts */
    screen->options = xconfigOptionListMerge(screen->options,
                                             screen->device->options);
    screen->device->options = NULL;
    
    update_display(op, screen);
    update_depth(op, screen);
    update_device(op, config, screen->device);
    update_options(op, screen);
    
    return TRUE;

} /* update_screen() */



/*
 * get_layout() - get the right layout from the config that we should
 * edit
 */

XConfigLayoutPtr get_layout(Options *op, XConfigPtr config)
{
    XConfigLayoutPtr layout;

    /* select a screenLayout to use */
    
    if (op->layout) {
        
        /* if a layout was specified on the commandline, use that */
        
        layout = xconfigFindLayout(op->layout, config->layouts);
        if (!layout) {
            nv_error_msg("Unable to find layout \"%s\".\n", op->layout);
            return NULL;
        }
    } else {
        
        /* otherwise, use the first layout in the config file */
        
        if (!config->layouts) {
            nv_error_msg("unable to select ScreenLayout to use.\n");
            return NULL;
        }
        
        layout = config->layouts;
    }

    return layout;

} /* get_layout() */



/*
 * update_extensions() - apply any requested updates to the Extensions
 * section; currently, this only applies to the Composite option.
 */

int update_extensions(Options *op, XConfigPtr config)
{
    char *value;

    /* validate the composite option against any other options specified */

    validate_composite(op, config);

    if (GET_BOOL_OPTION(op->boolean_options, COMPOSITE_BOOL_OPTION)) {

        /* if we don't already have the Extensions section, create it now */

        if (!config->extensions) {
            config->extensions = calloc(1, sizeof(XConfigExtensionsRec));
        }

        /* remove any existing composite extension option */
        xconfigRemoveNamedOption(&(config->extensions->options), 
                                 op->gop.compositeExtensionName,
                                 NULL);


        /* determine the value to set for the Composite option */

        value = GET_BOOL_OPTION(op->boolean_option_values,
                                COMPOSITE_BOOL_OPTION) ?
            "Enable" : "Disable";
        
        /* add the option */
        xconfigAddNewOption(&config->extensions->options, 
                            op->gop.compositeExtensionName,
                            value);
    }
    
    return TRUE;

} /* update_extensions() */


/*
 * update_server_flags() - update the server flags section with any
 * server flag options; the only option so far is "HandleSpecialKeys"
 */

int update_server_flags(Options *op, XConfigPtr config)
{
    if (!op->handle_special_keys) return TRUE;

    if (!config->flags) {
        config->flags = nvalloc(sizeof(XConfigFlagsRec));
        if ( !config->flags ) {
            return FALSE;
        }
    }

    if (config->flags->options) {
        xconfigRemoveNamedOption(&(config->flags->options),
                                 "HandleSpecialKeys", NULL);
    }

    if (op->handle_special_keys != NV_DISABLE_STRING_OPTION) {
        xconfigAddNewOption(&config->flags->options, "HandleSpecialKeys",
                            op->handle_special_keys);
    }

    return TRUE;

} /* update_server_flags() */




static int count_non_nv_gpus(void)
{
    struct pci_device_iterator *iter;
    struct pci_device *dev;
    int count = 0;

    if (pci_system_init()) {
        return -1;
    }

    iter = nvpci_find_gpu_by_vendor(PCI_MATCH_ANY);

    for (dev = pci_device_next(iter); dev; dev = pci_device_next(iter)) {
        if (dev->vendor_id != NV_PCI_VENDOR_ID) {
            count++;
        }
    }

    pci_system_cleanup();

    return count;
}




/*
 * update_device() - update the device; there is a lot of information
 * in the device that is not relevant to the NVIDIA X driver.  In
 * fact, some options, like "Chipset" can actually prevent XFree86
 * from using the NVIDIA driver.  The simplest solution is to zero out
 * the device, and only retain the few fields that are meaningful for
 * the NVIDIA X driver.
 */

static int update_device(Options *op, XConfigPtr config, XConfigDevicePtr device)
{
    char *identifier, *vendor, *comment, *board, *busid, *driver;
    int screen;
    size_t index_id;
    XConfigDevicePtr next;
    XConfigOptionPtr options;

    next = device->next;
    options = device->options;
    identifier = device->identifier;
    vendor = device->vendor;
    comment = device->comment;
    screen = device->screen;
    board = device->board;
    busid = device->busid;
    driver = device->driver;
    index_id = device->index_id;

    memset(device, 0, sizeof(XConfigDeviceRec));

    device->next = next;
    device->options = options;
    device->identifier = identifier;
    device->vendor = vendor;
    device->comment = comment;
    device->screen = screen;
    device->board = board;
    device->index_id = index_id;

    /*
     * Considering five conditions, in order, while populating busid field
     * 1. If the field is required for the configuration chosen and not passed in
     * 2. If the user specified "--no-busid", obey that
     * 3. If we want to write busid with option --busid
     * 4. If we want to preserve existing bus id
     * 5. If there are multiple screens
     * 6. If the system has any non-NVIDIA GPUs
     */

    if (GET_BOOL_OPTION(op->boolean_option_values, ENABLE_PRIME_OPTION) &&
        op->busid == NULL) {
        device->busid = nv_format_busid(op, device->index_id);
        if (device->busid == NULL) {
            return FALSE;
    }
    } else if (op->busid == NV_DISABLE_STRING_OPTION) {
        device->busid = NULL;
    } else if (op->busid) {
        device->busid = op->busid;
    } else if (GET_BOOL_OPTION(op->boolean_options,
                               PRESERVE_BUSID_BOOL_OPTION)) {
        if (GET_BOOL_OPTION(op->boolean_option_values,
                            PRESERVE_BUSID_BOOL_OPTION)) {
            device->busid = busid;
        } else {
            device->busid = NULL;
        }
    } else if (config->screens->next) {
        /* enable_separate_x_screens() already generated a busid string */
        device->busid = busid;
    } else if (count_non_nv_gpus() > 0) {
        device->busid = nv_format_busid(op, device->index_id);
        if (device->busid == NULL) {
            return FALSE;
        }
    }

    device->chipid = -1;
    device->chiprev = -1;
    device->irq = -1;

    if (op->preserve_driver) {
        device->driver = driver;
    } else {
        device->driver = "nvidia";
    }
    
    return TRUE;
    
} /* update_device() */



/*
 * update_depth() - make sure there is a display section with the
 * default depth, and possibly update the default depth
 */

static void update_depth(Options *op, XConfigScreenPtr screen)
{
    XConfigDisplayPtr display;
    int found;
    
    /* update the depth */
    if ((op->depth == 8) || (op->depth == 15) ||
        (op->depth == 16) || (op->depth == 24) ||
        (op->depth == 30)) {
        screen->defaultdepth = op->depth;
    } else {
        /* read the default depth to SVC and set it as the default depth */
        int scf_depth;
        
        if (read_scf_depth(&scf_depth) && scf_depth != screen->defaultdepth) {
            nv_warning_msg("The default depth of %d read from "
                           "the Solaris Management Facility is set as the "
                           "default depth for screen \"%s\"", scf_depth,
                           screen->identifier);
            screen->defaultdepth = scf_depth;
        }
    }         
    
    /*
     * if there is no display at the default depth, force the first
     * display section to that depth
     */
    
    found = FALSE;
        
    for (display = screen->displays; display; display = display->next) {
        if (display->depth == screen->defaultdepth) {
            found = TRUE;
            break;
        }
    }
    
    if (!found) {
        screen->displays->depth = screen->defaultdepth;
    }
    
} /* update_depth() */



/*
 * update_display() - if there are no display subsections, create one
 */

static void update_display(Options *op, XConfigScreenPtr screen)
{
    
    if (!screen->displays) {
        XConfigDisplayPtr display;
        XConfigModePtr mode = NULL;
        
        xconfigAddMode(&mode, "nvidia-auto-select");
        
        display = nvalloc(sizeof(XConfigDisplayRec));
        display->depth = screen->defaultdepth;
        display->modes = mode;
        display->frameX0 = -1;
        display->frameY0 = -1;
        display->black.red = -1;
        display->white.red = -1;
        
        screen->displays = display;
    }

} /* update_display() */


