/*
 * Copyright (C) 2014 Collabora Ltd.
 *     Author: Nicolas Dufresne <nicolas.dufresne@collabora.co.uk>
 * Copyright (c) 2018-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>

#define UUID_SIZE 16
#define USER_DATA_UNREGISTERED_TYPE 5

gboolean check_uuid(uint8_t *stream, char *sei_uuid_string);
uint8_t* parse_sei_unit(uint8_t * bs_ptr, guint *size, char *sei_uuid_string);
uint8_t *parse_sei_data (uint8_t *bs, uint32_t size, uint32_t *payload_size, char *sei_uuid_string);

gboolean check_uuid(uint8_t *stream, char *sei_uuid_string)
{
    char uuid_string[UUID_SIZE] = {0};
    uint32_t size = snprintf (uuid_string, UUID_SIZE, "%s", stream);
    if (size == (UUID_SIZE-1))
    {
        if (!strncmp (uuid_string, sei_uuid_string, (UUID_SIZE-1)))
            return TRUE;
        else
            return FALSE;
    }
    else
        return FALSE;
}

uint8_t* parse_sei_unit(uint8_t * bs_ptr, guint *size, char *sei_uuid_string)
{
    int payload_type = 0;
    int payload_size = 0;
    uint8_t* payload = NULL;
    int i;

    /* printf("found a SEI NAL unit!\n"); */

    payload_type = *bs_ptr++;

    while (payload_size % 0xFF == 0)
    {
        payload_size += *bs_ptr++;
    }
    //printf("payload_type = %i payload_size = %i\n", payload_type, payload_size); 

    if (!check_uuid (bs_ptr, sei_uuid_string))
    {
        //printf ("Expected UUID not found\n");
        bs_ptr += (payload_size - UUID_SIZE);
        return NULL;
    }
    else
    {
        bs_ptr += UUID_SIZE;
    }

    *size = payload_size;

    if (payload_type == USER_DATA_UNREGISTERED_TYPE)
    {
        payload = (uint8_t*)malloc((payload_size - UUID_SIZE)*sizeof(uint8_t));

        for (i = 0; i < (payload_size - UUID_SIZE); i++)
        {
            payload[i] = *bs_ptr;
            // drop emulation prevention bytes
            if ((*(bs_ptr) == 0x03)
                  && (*(bs_ptr - 1) == 0x00)
                  && (*(bs_ptr - 2) == 0x00))
            {
                i--;
            }
            bs_ptr++;
        }
        return payload;
    }
    else
    {
        return NULL;
    }
}

uint8_t *parse_sei_data (uint8_t *bs, uint32_t size, uint32_t *payload_size, char *sei_uuid_string)
{
    if (sei_uuid_string == NULL)
        return NULL;
    int checklen = 0;
    unsigned int sei_payload_size = 0;
    uint8_t *bs_ptr = bs;
    uint8_t *bs_ptr_end = bs + size;
    uint8_t *payload = NULL;
    while (bs_ptr_end > bs_ptr)
    {
        if (checklen < 2 && *bs_ptr++ == 0x00)
            checklen++;
        else if (checklen == 2 && *bs_ptr++ == 0x00)
            checklen++;
        else if (checklen == 3 && *bs_ptr++ == 0x01)
            checklen++;
        else if (checklen == 4 && *bs_ptr++ == 0x06)
        {
            payload = parse_sei_unit(bs_ptr, &sei_payload_size, sei_uuid_string);
            checklen = 0;
            if (payload != NULL)
            {
                *payload_size = (sei_payload_size - 16);
                return payload;
            }
            else
            {
                continue;
            }
        }
        else
            checklen = 0;
    }
    return NULL;
}
