/*
 * nvidia-xconfig: A tool for manipulating X config files,
 * specifically for use by the NVIDIA Linux graphics driver.
 *
 * Copyright (C) 2006 NVIDIA Corporation
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
 * query_gpu_info.c
 */

#include "nvidia-xconfig.h"
#include "msg.h"
#include <string.h>

static char *display_device_mask_to_display_device_name(unsigned int mask);



/*
 * query_gpu_info() - query information about the GPU, and print it
 * out
 */

int query_gpu_info(Options *op)
{
    DevicesPtr pDevices;
    DisplayDevicePtr pDisplayDevice;
    int i, j;
    char *name, *busid;

    /* query the GPU information */

    pDevices = find_devices(op);
    
    if (!pDevices) {
        nv_error_msg("Unable to query GPU information");
        return FALSE;
    }
    
    /* print the GPU information */

    nv_info_msg(NULL, "Number of GPUs: %d", pDevices->nDevices);

    for (i = 0; i < pDevices->nDevices; i++) {
        
        nv_info_msg(NULL, "");
        nv_info_msg(NULL, "GPU #%d:", i);
        nv_info_msg(TAB, "Name      : %s", pDevices->devices[i].name);
        nv_info_msg(TAB, "UUID      : %s", pDevices->devices[i].uuid);

        busid = nv_format_busid(op, i);
        nv_info_msg(TAB, "PCI BusID : %s", busid);
        nvfree(busid);

        nv_info_msg(NULL, "");
        nv_info_msg(TAB, "Number of Display Devices: %d",
                    pDevices->devices[i].nDisplayDevices);
        nv_info_msg(NULL, "");

        for (j = 0; j < pDevices->devices[i].nDisplayDevices; j++) {

            pDisplayDevice = &pDevices->devices[i].displayDevices[j];

            name = display_device_mask_to_display_device_name
                (pDisplayDevice->mask);
            
            if (!name) name = nvstrdup("Unknown");

            nv_info_msg(TAB, "Display Device %d (%s):", j, name);
        
            nvfree(name);

            /*
             * convenience macro to first check that the value is
             * non-zero
             */
            
            #define PRT(_fmt, _val)                      \
                if (_val) {                              \
                    nv_info_msg(BIGTAB, (_fmt), (_val)); \
                }
            
            if (pDisplayDevice->info_valid) {
                
                PRT("EDID Name             : %s", 
                    pDisplayDevice->info.monitor_name);
                
                PRT("Minimum HorizSync     : %.3f kHz",
                    pDisplayDevice->info.min_horiz_sync/1000.0);
                
                PRT("Maximum HorizSync     : %.3f kHz",
                    pDisplayDevice->info.max_horiz_sync/1000.0);
                
                PRT("Minimum VertRefresh   : %d Hz",
                    pDisplayDevice->info.min_vert_refresh);
                
                PRT("Maximum VertRefresh   : %d Hz",
                    pDisplayDevice->info.max_vert_refresh);
                
                PRT("Maximum PixelClock    : %.3f MHz",
                    pDisplayDevice->info.max_pixel_clock/1000.0);
                
                PRT("Maximum Width         : %d pixels",
                    pDisplayDevice->info.max_xres);
                
                PRT("Maximum Height        : %d pixels",
                    pDisplayDevice->info.max_yres);
                
                PRT("Preferred Width       : %d pixels",
                    pDisplayDevice->info.preferred_xres);
                
                PRT("Preferred Height      : %d pixels",
                    pDisplayDevice->info.preferred_yres);
                
                PRT("Preferred VertRefresh : %d Hz",
                    pDisplayDevice->info.preferred_refresh);
                
                PRT("Physical Width        : %d mm",
                    pDisplayDevice->info.physical_width);
                
                PRT("Physical Height       : %d mm",
                    pDisplayDevice->info.physical_height);
                
            } else {
                nv_info_msg(BIGTAB, "No EDID information available.");
            }
            
            nv_info_msg(NULL, "");
        }
    }
    
    free_devices(pDevices);

    return TRUE;
    
} /* query_gpu_info() */




/*
 * diaplay_mask/display_name conversions: the NV-CONTROL X extension
 * identifies a display device by a bit in a display device mask.  The
 * below function translates from a display mask to a string
 * describing the display devices.
 */

#define BITSHIFT_CRT  0
#define BITSHIFT_TV   8
#define BITSHIFT_DFP 16

#define BITMASK_ALL_CRT (0xff << BITSHIFT_CRT)
#define BITMASK_ALL_TV  (0xff << BITSHIFT_TV)
#define BITMASK_ALL_DFP (0xff << BITSHIFT_DFP)

/*
 * display_device_mask_to_display_name() - construct a string
 * describing the given display device mask.
 */

#define DISPLAY_DEVICE_STRING_LEN 256

static char *display_device_mask_to_display_device_name(unsigned int mask)
{
    char *s;
    int first = TRUE;
    unsigned int devcnt, devmask;
    char *display_device_name_string;

    display_device_name_string = nvalloc(DISPLAY_DEVICE_STRING_LEN);

    s = display_device_name_string;

    devmask = 1 << BITSHIFT_CRT;
    devcnt = 0;
    while (devmask & BITMASK_ALL_CRT) {
        if (devmask & mask) {
            if (first) first = FALSE;
            else s += sprintf(s, ", ");
            s += sprintf(s, "CRT-%X", devcnt);
        }
        devmask <<= 1;
        devcnt++;
    }

    devmask = 1 << BITSHIFT_DFP;
    devcnt = 0;
    while (devmask & BITMASK_ALL_DFP) {
        if (devmask & mask)  {
            if (first) first = FALSE;
            else s += sprintf(s, ", ");
            s += sprintf(s, "DFP-%X", devcnt);
        }
        devmask <<= 1;
        devcnt++;
    }
    
    devmask = 1 << BITSHIFT_TV;
    devcnt = 0;
    while (devmask & BITMASK_ALL_TV) {
        if (devmask & mask)  {
            if (first) first = FALSE;
            else s += sprintf(s, ", ");
            s += sprintf(s, "TV-%X", devcnt);
        }
        devmask <<= 1;
        devcnt++;
    }
    
    *s = '\0';
    
    return (display_device_name_string);

} /* display_device_mask_to_display_name() */
