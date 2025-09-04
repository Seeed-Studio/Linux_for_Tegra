/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "nvgst_x11_common.h"

void
nvgst_x11_init (displayCtx * dpyCtx)
{
  dpyCtx->isDPMSdisabled = 0;
  int screen = 0;
  dpyCtx->mDisplay = XOpenDisplay (NULL);
  if (!dpyCtx->mDisplay)
    printf
        ("\nCannot open display specified with DISPLAY environment variable\n");
  else {
    screen = DefaultScreen (dpyCtx->mDisplay);
    dpyCtx->display_width = DisplayWidth (dpyCtx->mDisplay, screen);
    dpyCtx->display_height = DisplayHeight (dpyCtx->mDisplay, screen);
  }
}

void
nvgst_x11_uninit (displayCtx * dpyCtx)
{
  if (dpyCtx->window)
    XDestroyWindow (dpyCtx->mDisplay, dpyCtx->window);
  XCloseDisplay (dpyCtx->mDisplay);
  dpyCtx->mDisplay = NULL;
}

void
saver_off (displayCtx * dpyCtx)
{
  int nothing;
  if (DPMSQueryExtension (dpyCtx->mDisplay, &nothing, &nothing)) {
    BOOL enabled;
    CARD16 powerLevel;

    DPMSInfo (dpyCtx->mDisplay, &powerLevel, &enabled);
    if (enabled) {
      DPMSDisable (dpyCtx->mDisplay);
      DPMSInfo (dpyCtx->mDisplay, &powerLevel, &enabled);
      if (enabled) {
        printf ("\ncould not disable DPMS\n");
      }
    } else {
      printf ("\nDPMS already DISABLED\n");
      dpyCtx->isDPMSdisabled = 1;
    }
  } else
    printf ("\nserver does not have extension for -dpms option\n");
}

void
saver_on (displayCtx * dpyCtx)
{
  int nothing;
  if (DPMSQueryExtension (dpyCtx->mDisplay, &nothing, &nothing)) {
    BOOL enabled;
    CARD16 powerLevel;

    DPMSInfo (dpyCtx->mDisplay, &powerLevel, &enabled);
    if (!enabled) {
      if (!dpyCtx->isDPMSdisabled)
        DPMSEnable (dpyCtx->mDisplay);
      DPMSInfo (dpyCtx->mDisplay, &powerLevel, &enabled);
      if (!enabled && !dpyCtx->isDPMSdisabled) {
        printf ("\ncould not enable DPMS\n");
      }
    } else
      printf ("\nDPMS already ENABLED\n");
  } else
    printf ("\nserver does not have extension for -dpms option\n");

}

void
nvgst_create_window (displayCtx * dpyCtx, char *title)
{
  int screen = 0;
  XTextProperty xproperty;

  if (dpyCtx->mDisplay) {
    screen = DefaultScreen (dpyCtx->mDisplay);
    if (!dpyCtx->width && !dpyCtx->height) {
      dpyCtx->width = DisplayWidth (dpyCtx->mDisplay, screen);
      dpyCtx->height = DisplayHeight (dpyCtx->mDisplay, screen);
    }

    dpyCtx->window = XCreateSimpleWindow (dpyCtx->mDisplay,
        RootWindow (dpyCtx->mDisplay, screen),
        dpyCtx->x, dpyCtx->y, dpyCtx->width, dpyCtx->height, 0, 0,
        BlackPixel (dpyCtx->mDisplay, screen));

    XSetWindowBackgroundPixmap (dpyCtx->mDisplay, dpyCtx->window, None);

    if (title) {
      if ((XStringListToTextProperty (((char **) &title), 1, &xproperty)) != 0) {
        XSetWMName (dpyCtx->mDisplay, dpyCtx->window, &xproperty);
        XFree (xproperty.value);

      }
    } else
      printf ("\ncan't set title to window, title NULL\n");

    /* Tell the window manager we'd like delete client messages instead of
     * being killed */
    Atom wmDeleteMessage =
        XInternAtom (dpyCtx->mDisplay, "WM_DELETE_WINDOW", False);
    if (wmDeleteMessage != None) {
      XSetWMProtocols (dpyCtx->mDisplay, dpyCtx->window, &wmDeleteMessage, 1);
    }

    XMapRaised (dpyCtx->mDisplay, dpyCtx->window);

    XSync (dpyCtx->mDisplay, 1);        //discard the events for now
  } else
    printf ("\ncan't create window, Display NULL\n");

}

void
nvgst_destroy_window (displayCtx * dpyCtx)
{
  XDestroyWindow (dpyCtx->mDisplay, dpyCtx->window);
  dpyCtx->window = (Window) NULL;
}
