/* Copyright (c) 2008 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an
 * express license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>

#include "gstegljitter.h"

GstEglJitterTool *GstEglAllocJitterTool(const char *pName, guint nTicks)
{
    GstEglJitterTool *pTool;

    assert(pName);
    assert(nTicks > 0);

    pTool = malloc(sizeof(GstEglJitterTool));
    if (!pTool)
        return NULL;

    memset(pTool, 0, sizeof(GstEglJitterTool));
    pTool->pName = malloc(strlen(pName) + 1);
    if (!pTool->pName)
    {
        free(pTool);
        return NULL;
    }
    memcpy((char *)pTool->pName, pName, strlen(pName) + 1);

    pTool->pTicks = malloc(sizeof(guint64) * nTicks);
    if (!pTool->pTicks)
    {
        free(pTool->pName);
        free(pTool);
        return NULL;
    }

    pTool->nTicksMax = nTicks;
    pTool->nTickCount = 0;
    pTool->nLastTime = 0;
    pTool->bShow = 0;

    return pTool;
}

void GstEglFreeJitterTool(GstEglJitterTool *pTool)
{
    if (!pTool)
        return;
    free(pTool->pName);
    free(pTool->pTicks);
    free(pTool);
}

void GstEglJitterToolAddPoint(GstEglJitterTool *pTool)
{
    guint64 now;
    assert(pTool);

    static guint64 s_oldtime = 0;
    static guint64 s_timedelta = 0;
    struct timeval tv;
    guint64 time;

    (void)gettimeofday( &tv, 0 );
    time = (guint64)( ( (guint64)tv.tv_sec * 1000 * 1000 ) + (guint64)tv.tv_usec );
    if (time < s_oldtime)
        s_timedelta += (s_oldtime - time);
    s_oldtime = time;
    time += s_timedelta;

    now = time;

    if (pTool->nLastTime == 0)
    {
        pTool->nLastTime = now;
        return;
    }

    pTool->pTicks[pTool->nTickCount] = now - pTool->nLastTime;
    pTool->nLastTime = now;
    pTool->nTickCount++;

    if (pTool->nTickCount < pTool->nTicksMax)
        return;

    {
        double fAvg = 0;
        double fStdDev = 0;
        guint i;

        for (i = 0; i < pTool->nTicksMax; i++)
            fAvg += pTool->pTicks[i];
        fAvg /= pTool->nTicksMax;

        for (i = 0; i < pTool->nTicksMax; i++)
            fStdDev += (fAvg - pTool->pTicks[i]) * (fAvg - pTool->pTicks[i]);
        fStdDev = sqrt(fStdDev / (pTool->nTicksMax - 1));

        if (pTool->bShow)
            printf("%s: mean: %.2f  std. dev: %.2f\n", pTool->pName,
                            fAvg, fStdDev);

        if (pTool->nPos < MAX_JITTER_HISTORY)
        {
            pTool->fAvg[pTool->nPos] = fAvg;
            pTool->fStdDev[pTool->nPos] = fStdDev;
            pTool->nPos++;
        }
    }

    pTool->nTickCount = 0;
}

void GstEglJitterToolSetShow(GstEglJitterTool *pTool, gboolean bShow)
{
    pTool->bShow = bShow;
}

void GstEglJitterToolGetAvgs(GstEglJitterTool *pTool, double *pStdDev, double *pAvg,
                          double *pHighest)
{
    guint i;

    assert(pTool);
    assert(pStdDev);
    assert(pAvg);
    assert(pHighest);

    *pStdDev = 0;
    *pAvg = 0;
    *pHighest = 0;

    if (pTool->nPos < 1)
        return;

    for (i = 1; i < pTool->nPos && i < MAX_JITTER_HISTORY; i++)
    {
        *pAvg = *pAvg + pTool->fAvg[i];
        *pStdDev = *pStdDev + pTool->fStdDev[i];

        if (pTool->fStdDev[i] > *pHighest)
            *pHighest = pTool->fStdDev[i];
    }

    *pAvg = *pAvg / (pTool->nPos);
    *pStdDev = *pStdDev / (pTool->nPos);
}

