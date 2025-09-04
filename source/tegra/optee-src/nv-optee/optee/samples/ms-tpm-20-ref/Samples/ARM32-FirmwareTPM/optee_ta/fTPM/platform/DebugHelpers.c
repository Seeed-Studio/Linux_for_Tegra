/*
 * Copyright (c) 2023, NVIDIA Corporation & AFFILIATES. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdio.h>
#include "Platform.h"

#if CERTIFYX509_DEBUG

//*** DebugFileInit()
//  Return Type: int
//   0              success
//  != 0            error
int DebugFileInit(void)
{
    return 0;
}

//*** DebugDumpBuffer()
void DebugDumpBuffer(int size, unsigned char *buf, const char *identifier)
{
    int              i;

    if (identifier)
        printf("%s\n", identifier);

    if (buf) {
        for (i = 0; i < size; i++) {
            if (((i % 16) == 0) && (i))
                printf("\n");
            printf(" %02X", buf[i]);
        }

        if ((size % 16) != 0)
            printf("\n");
    }
}

#endif // CERTIFYX509_DEBUG
