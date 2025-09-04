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
 * multiple_screens.c
 */

#include "nvidia-xconfig.h"
#include "xf86Parser.h"
#include "msg.h"

#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>


static int enable_separate_x_screens(Options *op, XConfigPtr config,
                                     XConfigLayoutPtr layout);
static int disable_separate_x_screens(Options *op, XConfigPtr config,
                                      XConfigLayoutPtr layout);

static int set_xinerama(int xinerama_enabled, XConfigLayoutPtr layout);

static XConfigDisplayPtr clone_display_list(XConfigDisplayPtr display0);
static XConfigDevicePtr clone_device(XConfigDevicePtr device0, int idx);
static XConfigScreenPtr clone_screen(XConfigScreenPtr screen0, int idx);

static void create_adjacencies(Options *op, XConfigPtr config,
                               XConfigLayoutPtr layout);

static int enable_all_gpus(Options *op, XConfigPtr config,
                           XConfigLayoutPtr layout);

static void free_unused_devices(Options *op, XConfigPtr config);
static void free_unused_monitors(Options *op, XConfigPtr config);

static int only_one_screen(Options *op, XConfigPtr config,
                           XConfigLayoutPtr layout);

/*
 * get_screens_to_clone() - try to detect automatically how many heads has each
 * device in order to use that number to create more than two separate X
 * screens. If the user specifies the option --num-x-screens <quantity>, that
 * value is used. If neither the user specifies the quantity or the number of
 * heads can be detected automatically, it uses 2 heads (the standard
 * behavior). This function returns an array of size nscreens with the number
 * of screens to clone per screen candidate. The caller is responsible of
 * freeing the memory of that array.
 */
static int* get_screens_to_clone(Options *op,
                                 const XConfigScreenPtr *screen_candidates,
                                 int nscreens)
{
    DevicesPtr pDevices;
    int *screens_to_clone, *supported_screens;
    int i, j, devs_found;

    screens_to_clone = nvalloc(nscreens * sizeof(int));
    supported_screens = nvalloc(nscreens * sizeof(int));

    /* Detect the number of supported screens per screen candidate */
    devs_found = FALSE;
    pDevices = find_devices(op);
    if (pDevices) {
        for (i = 0; i < nscreens; i++) {
            int bus, slot, scratch;

            if (!screen_candidates[i]) {
                continue;
            }

            /* parse the bus id for this candidate screen */
            if (!xconfigParsePciBusString(screen_candidates[i]->device->busid,
                                          &bus, &slot, &scratch)) {
                continue;
            }

            for (j = 0; j < pDevices->nDevices; j++) {
                if ((pDevices->devices[j].dev.bus == bus) &&
                    (pDevices->devices[j].dev.slot == slot)) {

                    if (pDevices->devices[j].crtcs > 0) {
                        supported_screens[i] = pDevices->devices[j].crtcs;
                    }
                    break;
                }
            }
        }
        free_devices(pDevices);
        devs_found = TRUE;
    }

    /* If user has defined a number of screens per GPU, use that value */
    if (op->num_x_screens > 0) {
        for (i = 0; i < nscreens; i++) {
            if (!screen_candidates[i]) {
                continue;
            }

            /* Print error when user specifies more X screens than supported */
            if (devs_found && op->num_x_screens > supported_screens[i]) {
                nv_warning_msg("Number of X screens specified is higher than the "
                               "supported quantity (%d)", supported_screens[i]);
            }

            screens_to_clone[i] = op->num_x_screens;
        }

        goto done;
    }

    for (i = 0; i < nscreens; i++) {
        if (screen_candidates[i]) {
            if (devs_found) {
                /* If devices found, use the supported screens number */
                screens_to_clone[i] = supported_screens[i];
            }
            else {
                /* Default behavior (2 heads per GPU) */
                screens_to_clone[i] = 2;
            }
        }
    }

done:
    nvfree(supported_screens);
    return screens_to_clone;
}

/*
 * clean_screen_list() - Used by enable_separate_x_screens and
 * disable_separate_x_screens. Given the array screen_list and the config
 * pointer, this function will leave only one screen per different busid in both
 * screen_list array and config->screens list (i.e all the resulting screens in
 * screen_list and config->screens will have an unique busid).
 */
static void clean_screen_list(XConfigScreenPtr *screen_list,
                              XConfigPtr config,
                              int nscreens)
{
    int i, j;
    int *bus, *slot, scratch;

    /* trim out duplicates and get the bus ids*/

    bus = nvalloc(sizeof(int) * nscreens);
    slot = nvalloc(sizeof(int) * nscreens);

    for (i = 0; i < nscreens; i++) {
        bus[i] = -1;
    }
    
    for (i = 0; i < nscreens; i++) {
        if (!screen_list[i] || (bus[i] == -1 &&
            !xconfigParsePciBusString(screen_list[i]->device->busid,
                                      &bus[i], &slot[i], &scratch))) {
            continue;
        }

        for (j = i+1; j < nscreens; j++) {
            if (!screen_list[j] || (bus[j] == -1 &&
                !xconfigParsePciBusString(screen_list[j]->device->busid,
                                          &bus[j], &slot[j], &scratch))) {
                continue;
            }

            if ((bus[i] == bus[j]) && (slot[i] == slot[j])) {
                screen_list[j] = NULL;
            }
        }
    }

    /*
     * for every screen in the screen list, scan through all
     * screens; if any screen, other than *this* screen has the same
     * busid, remove it
     */
    
    for (i = 0; i < nscreens; i++) {
        XConfigScreenPtr screen, prev;
        int bus0, slot0;

        if (!screen_list[i]) {
            continue;
        }

        screen = config->screens;
        prev = NULL;
        
        while (screen) {
            if (screen_list[i] == screen) {
                goto next_screen;
            }
            if (!screen->device) {
                goto next_screen;
            }
            if (!screen->device->busid) {
                goto next_screen;
            }
            if (!xconfigParsePciBusString(screen->device->busid,
                                          &bus0, &slot0, &scratch)) {
                goto next_screen;
            }
            
            if ((bus[i] == bus0) && (slot[i] == slot0)) {
                XConfigScreenPtr next;

                if (prev) {
                    prev->next = screen->next;
                }
                else {
                    config->screens = screen->next;
                }

                next = screen->next;
                screen->next = NULL;
                xconfigFreeScreenList(&screen);
                screen = next;
            }
            else {
                
            next_screen:
                
                prev = screen;
                screen = screen->next;
            }
        }

        screen_list[i]->device->screen = -1;
    }
}

/*
 * apply_multi_screen_options() - there are 4 options that can affect
 * multiple X screens:
 *
 * - add X screens for all GPUS in the system
 * - separate X screens on one GPU (turned on or off)
 * - only one X screen
 * - Xinerama
 *
 * apply these options in that order
 */

int apply_multi_screen_options(Options *op, XConfigPtr config,
                               XConfigLayoutPtr layout)
{
    if (op->enable_all_gpus) {
        if (!enable_all_gpus(op, config, layout)) return FALSE;
    }
    

    if (GET_BOOL_OPTION(op->boolean_options,
                        SEPARATE_X_SCREENS_BOOL_OPTION)) {
        if (GET_BOOL_OPTION(op->boolean_option_values,
                            SEPARATE_X_SCREENS_BOOL_OPTION)) {
            if (!enable_separate_x_screens(op, config, layout)) return FALSE;
        } else {
            if (!disable_separate_x_screens(op, config, layout)) return FALSE;
        }
    }
    
    if (GET_BOOL_OPTION(op->boolean_options,
                        XINERAMA_BOOL_OPTION)) {
        if (!set_xinerama(GET_BOOL_OPTION(op->boolean_option_values,
                                          XINERAMA_BOOL_OPTION),
                          layout)) return FALSE;
    }
    
    if (op->only_one_screen) {
        if (!only_one_screen(op, config, layout)) return FALSE;
    }
    
    return TRUE;
    
} /* apply_multi_screen_options() */



/*
 * find_devices() - dlopen the nvidia-cfg library and query the
 * available information about the GPUs in the system.
 */

DevicesPtr find_devices(Options *op)
{
    DevicesPtr pDevices = NULL;
    DisplayDevicePtr pDisplayDevice;
    int i, j, n, count = 0;
    int swappedIndex;
    unsigned int mask, bit;
    DeviceRec tmpDevice;
    NvCfgPciDevice *devs = NULL;
    NvCfgBool is_primary_device;
    NvCfgBool ret;
    char *lib_path;
    void *lib_handle;

    NvCfgBool (*__getDevices)(int *n, NvCfgDevice **devs);
    NvCfgBool (*__openDevice)(int bus, int slot, NvCfgDeviceHandle *handle);
    NvCfgBool (*__getPciDevices)(int *n, NvCfgPciDevice **devs);
    NvCfgBool (*__openPciDevice)(int domain, int bus, int slot, int function,
                                 NvCfgDeviceHandle *handle);
    NvCfgBool (*__getNumCRTCs)(NvCfgDeviceHandle handle, int *crtcs);
    NvCfgBool (*__getProductName)(NvCfgDeviceHandle handle, char **name);
    NvCfgBool (*__getDisplayDevices)(NvCfgDeviceHandle handle,
                                     unsigned int *display_device_mask);
    NvCfgBool (*__getEDID)(NvCfgDeviceHandle handle,
                           unsigned int display_device,
                           NvCfgDisplayDeviceInformation *info);
    NvCfgBool (*__isPrimaryDevice)(NvCfgDeviceHandle handle,
                                  NvCfgBool *is_primary_device);
    NvCfgBool (*__closeDevice)(NvCfgDeviceHandle handle);
    NvCfgBool (*__getDeviceUUID)(NvCfgDeviceHandle handle, char **uuid);
    
    /* dlopen() the nvidia-cfg library */
    
#define __LIB_NAME "libnvidia-cfg.so.1"

    if (op->nvidia_cfg_path) {
        lib_path = nvstrcat(op->nvidia_cfg_path, "/", __LIB_NAME, NULL);
    } else {
        lib_path = strdup(__LIB_NAME);
    }
    
    lib_handle = dlopen(lib_path, RTLD_NOW);
    
    nvfree(lib_path);
    
    if (!lib_handle) {
        nv_warning_msg("error opening %s: %s.", __LIB_NAME, dlerror());
        return NULL;
    }
    
#define __GET_FUNC(proc, name)                                        \
    (proc) = dlsym(lib_handle, (name));                               \
    if (!(proc)) {                                                    \
        nv_warning_msg("error retrieving symbol %s from %s: %s",      \
                       (name), __LIB_NAME, dlerror());                \
        dlclose(lib_handle);                                          \
        return NULL;                                                  \
    }

    /* required functions */
    __GET_FUNC(__getDevices, "nvCfgGetDevices");
    __GET_FUNC(__openDevice, "nvCfgOpenDevice");
    __GET_FUNC(__getPciDevices, "nvCfgGetPciDevices");
    __GET_FUNC(__openPciDevice, "nvCfgOpenPciDevice");
    __GET_FUNC(__getNumCRTCs, "nvCfgGetNumCRTCs");
    __GET_FUNC(__getProductName, "nvCfgGetProductName");
    __GET_FUNC(__getDisplayDevices, "nvCfgGetDisplayDevices");
    __GET_FUNC(__getEDID, "nvCfgGetEDID");
    __GET_FUNC(__closeDevice, "nvCfgCloseDevice");
    __GET_FUNC(__getDeviceUUID, "nvCfgGetDeviceUUID");
    __GET_FUNC(__isPrimaryDevice,"nvCfgIsPrimaryDevice");
    
    if (__getPciDevices(&count, &devs) != NVCFG_TRUE) {
        return NULL;
    }

    if (count == 0) return NULL;

    pDevices = nvalloc(sizeof(DevicesRec));
    
    pDevices->devices = nvalloc(sizeof(DeviceRec) * count);

    pDevices->nDevices = count;

    for (i = 0; i < count; i++) {
        
        pDevices->devices[i].dev = devs[i];
        
        if (__openPciDevice(devs[i].domain, devs[i].bus, devs[i].slot, 0,
                            &(pDevices->devices[i].handle)) != NVCFG_TRUE) {
            goto fail;
        }
        
        if (__getNumCRTCs(pDevices->devices[i].handle,
                          &pDevices->devices[i].crtcs) != NVCFG_TRUE) {
            goto fail;
        }

        if (__getProductName(pDevices->devices[i].handle,
                             &pDevices->devices[i].name) != NVCFG_TRUE) {
            /* This call may fail with little impact to the Device section */
            pDevices->devices[i].name = NULL;
        }

        if (__getDeviceUUID(pDevices->devices[i].handle,
                            &pDevices->devices[i].uuid) != NVCFG_TRUE) {
            goto fail;
        }
        if (__getDisplayDevices(pDevices->devices[i].handle, &mask) !=
            NVCFG_TRUE) {
            goto fail;
        }
        
        pDevices->devices[i].displayDeviceMask = mask;

        /* count the number of display devices */
        
        for (n = j = 0; j < 32; j++) {
            if (mask & (1 << j)) n++;
        }
        
        pDevices->devices[i].nDisplayDevices = n;

        if (n) {

            /* allocate the info array of the right size */
            
            pDevices->devices[i].displayDevices =
                nvalloc(sizeof(DisplayDeviceRec) * n);
            
            /* fill in the info array */
            
            for (n = j = 0; j < 32; j++) {
                bit = 1 << j;
                if (!(bit & mask)) continue;
                
                pDisplayDevice = &pDevices->devices[i].displayDevices[n];
                pDisplayDevice->mask = bit;

                if (__getEDID(pDevices->devices[i].handle, bit,
                              &pDisplayDevice->info) != NVCFG_TRUE) {
                    pDisplayDevice->info_valid = FALSE;
                } else {
                    pDisplayDevice->info_valid = TRUE;
                }
                n++;
            }
        } else {
            pDevices->devices[i].displayDevices = NULL;
        }

        /* Use this index instead of i to close device after a possible swap */
        swappedIndex = i;

        if ((i != 0) &&
            (__isPrimaryDevice(pDevices->devices[i].handle,
                               &is_primary_device) == NVCFG_TRUE) &&
            (is_primary_device == NVCFG_TRUE)) {
            memcpy(&tmpDevice, &pDevices->devices[0], sizeof(DeviceRec));
            memcpy(&pDevices->devices[0], &pDevices->devices[i], sizeof(DeviceRec));
            memcpy(&pDevices->devices[i], &tmpDevice, sizeof(DeviceRec));
            swappedIndex = 0;
        }
        
        ret = __closeDevice(pDevices->devices[swappedIndex].handle);
        pDevices->devices[swappedIndex].handle = NULL;

        if (ret != NVCFG_TRUE) {
            goto fail;
        }
    }
    
    goto done;
    
 fail:

    nv_warning_msg("Unable to use the nvidia-cfg library to query NVIDIA "
                   "hardware.");

    for (i = 0; i < pDevices->nDevices; i++) {
        /* close the opened device */
        if (pDevices->devices[i].handle) {
            __closeDevice(pDevices->devices[i].handle);
        }
    }

    free_devices(pDevices);
    pDevices = NULL;

    /* fall through */

 done:
    
    if (devs) free(devs);
    
    return pDevices;
    
} /* find_devices() */



/*
 * free_devices()
 */

void free_devices(DevicesPtr pDevices)
{
    int i;
    
    if (!pDevices) return;
    
    for (i = 0; i < pDevices->nDevices; i++) {
        if (pDevices->devices[i].displayDevices) {
            nvfree(pDevices->devices[i].displayDevices);
        }
    }
    
    if (pDevices->devices) {
        nvfree(pDevices->devices);
    }
        
    nvfree(pDevices);
    
} /* free_devices() */



/*
 * set_xinerama() - This makes sure there is a ServerLayout
 * section and sets the "Xinerama" option
 */

static int set_xinerama(int xinerama_enabled, XConfigLayoutPtr layout)
{
    xconfigAddNewOption(&(layout->options),
                        "Xinerama",
                        (xinerama_enabled ? "1" : "0"));

    return TRUE;

} /* set_xinerama() */



/*
 * enable_separate_x_screens() - this effectively clones each screen
 * that is on a unique GPU.
 *
 * Algorithm:
 *
 * step 1: build a list of screens to be cloned
 *
 * step 2: assign a busID to every screen that is in the list (if
 * BusIDs are not already assigned)
 *
 * step 3: clean the candidate list
 *
 * step 4: get the number of clones per screen candidate
 *
 * step 4: clone each eligible screen
 * 
 * step 5: update adjacency list (just wipe the list and restart)
 */

static int enable_separate_x_screens(Options *op, XConfigPtr config,
                                     XConfigLayoutPtr layout)
{
    XConfigScreenPtr screen, *screenlist = NULL;
    XConfigAdjacencyPtr adj;
    int* screens_to_clone = NULL;

    int i, nscreens = 0;
    int have_busids;
    
    /* step 1: build the list of screens that are candidate to be cloned */
    
    if (op->screen) {
        screen = xconfigFindScreen(op->screen, config->screens);
        if (!screen) {
            nv_error_msg("Unable to find screen '%s'.", op->screen);
            return FALSE;
        }
        
        screenlist = nvalloc(sizeof(XConfigScreenPtr));
        screenlist[0] = screen;
        nscreens = 1;
        
    } else {
        for (adj = layout->adjacencies; adj; adj = adj->next) {
            nscreens++;
            screenlist = realloc(screenlist,
                                 sizeof(XConfigScreenPtr) * nscreens);
            
            screenlist[nscreens-1] = adj->screen;
        }
    }
    
    if (!nscreens) return FALSE;
    
    /* step 2: do all screens in the list have a bus ID? */
    
    have_busids = TRUE;
    
    for (i = 0; i < nscreens; i++) {
        if (screenlist[i] &&
            screenlist[i]->device &&
            screenlist[i]->device->busid) {
            // this screen has a busid
        } else {
            have_busids = FALSE;
            break;
        }
    }
    
    /*
     * if not everyone has a bus id, then let's assign bus ids to all
     * the screens; XXX what if _some_ already have busids?  Too bad,
     * we're going to reassign all of them.
     */
    
    if (!have_busids) {
        DevicesPtr pDevices;
        
        pDevices = find_devices(op);
        if (!pDevices) {
            nv_error_msg("Unable to determine number or location of "
                         "GPUs in system; cannot "
                         "honor '--separate-x-screens' option.");
            return FALSE;
        }
        
        for (i = 0; i < nscreens; i++) {
            if (i >= pDevices->nDevices) {
                /*
                 * we have more screens than GPUs, this screen is no
                 * longer a candidate
                 */
                screenlist[i] = NULL;
                continue;
            }
            
            screenlist[i]->device->busid = nv_format_busid(op, i);

            screenlist[i]->device->board = nvstrdup(pDevices->devices[i].name);
        }

        free_devices(pDevices);
    }

    /* step 3 */
    clean_screen_list(screenlist, config, nscreens);
    
    /* step 4 */
    screens_to_clone = get_screens_to_clone(op, screenlist, nscreens);
    
    /* step 5: clone each eligible screen */
    
    for (i = 0; i < nscreens; i++) {
        if (!screenlist[i]) continue;

        while (--screens_to_clone[i] > 0) {
            clone_screen(screenlist[i], screens_to_clone[i]);
        }
    }

    nvfree(screens_to_clone);
    
    /* step 6: wipe the existing adjacencies and recreate them */
    
    xconfigFreeAdjacencyList(&layout->adjacencies);
    
    create_adjacencies(op, config, layout);

    /* free unused device and monitor sections */
    
    free_unused_devices(op, config);
    free_unused_monitors(op, config);

    /* free stuff */

    free(screenlist);

    return TRUE;

} /* enable_separate_x_screens() */


/*
 * disable_separate_x_screens() - remove multiple screens that are
 * configured for the same GPU.
 *
 * Algorithm:
 *
 * step 1: find which screens need to be "de-cloned" (either
 * op->screen or all screens in the layout)
 *
 * step 2: narrow that list down to screens that have a busid
 * specified
 *
 * step 3: clean the candidate list
 *
 * step 4: recompute the adjacency list
 */

static int disable_separate_x_screens(Options *op, XConfigPtr config,
                                      XConfigLayoutPtr layout)
{
    XConfigScreenPtr screen, *screenlist = NULL;
    XConfigAdjacencyPtr adj;
    
    int i, nscreens = 0;
    
    /* step 1: build the list of screens that are candidate to be de-cloned */
    
    if (op->screen) {
        screen = xconfigFindScreen(op->screen, config->screens);
        if (!screen) {
            nv_error_msg("Unable to find screen '%s'.", op->screen);
            return FALSE;
        }
        
        screenlist = nvalloc(sizeof(XConfigScreenPtr));
        screenlist[0] = screen;
        nscreens = 1;
        
    } else {
        for (adj = layout->adjacencies; adj; adj = adj->next) {
            nscreens++;
            screenlist = realloc(screenlist,
                                 sizeof(XConfigScreenPtr) * nscreens);
            
            screenlist[nscreens-1] = adj->screen;
        }
    }
    
    /*
     * step 2: limit the list to screens that have a BusID; parse the busIDs
     * while we're at it
     */
    
    for (i = 0; i < nscreens; i++) {
        int bus, slot, scratch;
        if (screenlist[i] &&
            screenlist[i]->device &&
            screenlist[i]->device->busid &&
            xconfigParsePciBusString(screenlist[i]->device->busid,
                                     &bus, &slot, &scratch)) {
            // this screen has a valid busid
        } else {
            screenlist[i] = NULL;
        }
    }

    /* step 3 */
    clean_screen_list(screenlist, config, nscreens);
    
    /* step 4: wipe the existing adjacencies and recreate them */
    
    xconfigFreeAdjacencyList(&layout->adjacencies);
    
    create_adjacencies(op, config, layout);

    /* free unused device and monitor sections */
    
    free_unused_devices(op, config);
    free_unused_monitors(op, config);

    /* free stuff */

    free(screenlist);
    
    return TRUE;
    
} /* disable_separate_x_screens() */


/*
 * clone_display_list() - create a duplicate of the specified display
 * subsection.
 */

static XConfigDisplayPtr clone_display_list(XConfigDisplayPtr display0)
{
    XConfigDisplayPtr d = NULL, prev = NULL, head = NULL;
    
    while (display0) {
        d = nvalloc(sizeof(XConfigDisplayRec));
        memcpy(d, display0, sizeof(XConfigDisplayRec));
        if (display0->visual) d->visual = nvstrdup(display0->visual);
        if (display0->comment) d->comment = nvstrdup(display0->comment);
        d->options = xconfigOptionListDup(display0->options);
        d->next = NULL;
        if (prev) prev->next = d;
        if (!head) head = d;
        prev = d;
        display0 = display0->next;
    }
    
    return head;
    
} /* clone_display_list() */



/*
 * clone_device() - duplicate the specified device section, updating
 * the screen indices as approprate for multiple X screens on one GPU
 */

static XConfigDevicePtr clone_device(XConfigDevicePtr device0, int idx)
{
    XConfigDevicePtr device;

    device = nvalloc(sizeof(XConfigDeviceRec));
    
    device->identifier = nvasprintf("%s (%d)", device0->identifier, idx);
    
    if (device0->vendor)  device->vendor  = nvstrdup(device0->vendor);
    if (device0->board)   device->board   = nvstrdup(device0->board);
    if (device0->chipset) device->chipset = nvstrdup(device0->chipset);
    if (device0->busid)   device->busid   = nvstrdup(device0->busid);
    if (device0->card)    device->card    = nvstrdup(device0->card);
    if (device0->driver)  device->driver  = nvstrdup(device0->driver);
    if (device0->ramdac)  device->ramdac  = nvstrdup(device0->ramdac);
    if (device0->comment) device->comment = nvstrdup(device0->comment);

    /* these are needed for multiple X screens on one GPU */

    device->screen = idx;
    device0->screen = 0;

    device->chipid = -1;
    device->chiprev = -1;
    device->irq = -1;

    device->options = xconfigOptionListDup(device0->options);
    
    /* insert the new device after the original device */
    
    device->next = device0->next;
    device0->next = device;

    return device;
    
} /* clone_device() */



/*
 * clone_screen() - duplicate the given screen, for use as the ith
 * X screen on one GPU
 */

static XConfigScreenPtr clone_screen(XConfigScreenPtr screen0, int idx)
{
    XConfigScreenPtr screen = nvalloc(sizeof(XConfigScreenRec));
    
    screen->identifier = nvasprintf("%s (%d)", screen0->identifier, idx);
    
    screen->device = clone_device(screen0->device, idx);
    screen->device_name = nvstrdup(screen->device->identifier);
    
    screen->monitor = screen0->monitor;
    screen->monitor_name = nvstrdup(screen0->monitor_name);
    
    screen->defaultdepth = screen0->defaultdepth;
    
    screen->displays = clone_display_list(screen0->displays);
    
    screen->options = xconfigOptionListDup(screen0->options);
    if (screen0->comment) screen->comment = nvstrdup(screen0->comment);

    /* insert the new screen after the original screen */

    screen->next = screen0->next;
    screen0->next = screen;

    return screen;
    
} /* clone_screen() */



/*
 * create_adjacencies() - loop through all the screens in the config,
 * and add an adjacency section to the layout; this assumes that there
 * are no existing adjacencies in the layout
 */

static void create_adjacencies(Options *op, XConfigPtr config,
                               XConfigLayoutPtr layout)
{
    XConfigAdjacencyPtr adj, prev_adj;
    XConfigScreenPtr screen;
    int i;

    i = 0;
    prev_adj = NULL;
    
    for (screen = config->screens; screen; screen = screen->next) {
        
        adj = nvalloc(sizeof(XConfigAdjacencyRec));
        
        adj->scrnum = i;
        adj->screen_name = nvstrdup(screen->identifier);
        adj->screen = screen;
        
        if (prev_adj) {
            prev_adj->next = adj;
        } else {
            layout->adjacencies = adj;
        }
        
        prev_adj = adj;
        i++;
    }
    
    xconfigGenerateAssignScreenAdjacencies(layout);
    
} /* create_adjacencies() */


/*
 * enable_all_gpus() - get information for every GPU in the system,
 * and create a screen section for each
 *
 * XXX do we add new screens with reasonable defaults, or do we clone
 * the first existing X screen N times?  For now, we'll just add all
 * new X screens
 */

static int enable_all_gpus(Options *op, XConfigPtr config,
                           XConfigLayoutPtr layout)
{
    DevicesPtr pDevices;
    int i;

    pDevices = find_devices(op);
    if (!pDevices) {
        nv_error_msg("Unable to determine number of GPUs in system; cannot "
                     "honor '--enable-all-gpus' option.");
        return FALSE;
    }
    
    /* free all existing X screens, monitors, devices, and adjacencies */
    
    xconfigFreeScreenList(&config->screens);
    xconfigFreeDeviceList(&config->devices);
    xconfigFreeMonitorList(&config->monitors);
    xconfigFreeAdjacencyList(&layout->adjacencies);

    /* add N new screens; this will also add device and monitor sections */
    
    for (i = 0; i < pDevices->nDevices; i++) {
        xconfigGenerateAddScreen(config,
                                 pDevices->devices[i].dev.bus,
                                 pDevices->devices[i].dev.domain,
                                 pDevices->devices[i].dev.slot,
                                 pDevices->devices[i].name, i,
                                 "nvidia", "NVIDIA Corporation");
    }
    
    free_devices(pDevices);

    /* create adjacencies for the layout */
    
    create_adjacencies(op, config, layout);

    return TRUE;
    
} /* enable_all_gpus() */



/*
 * free_unused_devices() - free unused device sections
 */

static void free_unused_devices(Options *op, XConfigPtr config)
{
    XConfigDevicePtr device, prev, next;
    XConfigScreenPtr screen;
    int found;

    /* free any unused device sections */
    
    device = config->devices;
    prev = NULL;
    
    while (device) {
        found = FALSE;
        
        for (screen = config->screens; screen; screen = screen->next) {
            if (device == screen->device) {
                found = TRUE;
                break;
            }
        }
        
        if (!found) {
            if (prev) {
                prev->next = device->next;
            } else {
                config->devices = device->next;
            }
            next = device->next;
            device->next = NULL;
            xconfigFreeDeviceList(&device);
            device = next;
        } else {
            prev = device;
            device = device->next;
        }
    }
} /* free_unused_devices() */



/*
 * free_unused_monitors() - free unused monitor sections
 */

static void free_unused_monitors(Options *op, XConfigPtr config)
{
    XConfigMonitorPtr monitor, prev, next;
    XConfigScreenPtr screen;
    int found;

    /* free any unused monitor sections */
    
    monitor = config->monitors;
    prev = NULL;
    
    while (monitor) {
        found = FALSE;
        
        for (screen = config->screens; screen; screen = screen->next) {
            if (monitor == screen->monitor) {
                found = TRUE;
                break;
            }
        }
        
        if (!found) {
            if (prev) {
                prev->next = monitor->next;
            } else {
                config->monitors = monitor->next;
            }
            next = monitor->next;
            monitor->next = NULL;
            xconfigFreeMonitorList(&monitor);
            monitor = next;
        } else {
            prev = monitor;
            monitor = monitor->next;
        }
    }
} /* free_unused_monitors() */



/*
 * only_one_screen() - delete all screens after the first one
 */

static int only_one_screen(Options *op, XConfigPtr config,
                           XConfigLayoutPtr layout)
{
    if (!config->screens) return FALSE;
    
    /* free all existing X screens after the first */
    
    xconfigFreeScreenList(&config->screens->next);
    
    /* free all adjacencies */
    
    xconfigFreeAdjacencyList(&layout->adjacencies);
    
    /* add new adjacency */
    
    create_adjacencies(op, config, layout);
    
    /* removed unused device and monitor sections */
    
    free_unused_devices(op, config);
    free_unused_monitors(op, config);

    return TRUE;

} /* only_one_screen() */

