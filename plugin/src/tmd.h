/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2026 NUSspli contributors                                 *
 *                                                                         *
 * This program is free software; you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation; either version 3 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License along *
 * with this program; if not, If not, see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

// The layout of include/tmd.h of NUSspli, written out without the wut helpers
// so that the plugin does not have to pull NUSspli headers in. The plugin only
// reads the title.tmd files NUSspli already wrote, and the Wii U is big endian
// just like the files are, so the fields are used as they come off the SD card.
//
// From: https://wiiubrew.org/wiki/Title_metadata

#define TMD_CONTENT_TYPE_HASHED 0x0002

typedef struct __attribute__((packed))
{
    uint32_t cid;
    uint16_t index;
    uint16_t type;
    uint64_t size;
    uint32_t hash[8];
} TMD_CONTENT;
static_assert(sizeof(TMD_CONTENT) == 0x30, "TMD_CONTENT has the wrong size");

typedef struct __attribute__((packed))
{
    uint16_t index;
    uint16_t count;
    uint32_t hash[8];
} TMD_CONTENT_INFO;
static_assert(sizeof(TMD_CONTENT_INFO) == 0x24, "TMD_CONTENT_INFO has the wrong size");

typedef struct __attribute__((packed))
{
    uint32_t sig_type;
    uint8_t sig[256];
    uint8_t reserved0[60];
    uint8_t issuer[64];
    uint8_t version;
    uint8_t ca_crl_version;
    uint8_t signer_crl_version;
    uint8_t reserved1[1];
    uint64_t sys_version;
    uint64_t tid;
    uint32_t type;
    uint16_t group;
    uint8_t reserved2[62];
    uint32_t access_rights;
    uint16_t title_version;
    uint16_t num_contents;
    uint16_t boot_index;
    uint8_t reserved3[2];
    uint32_t hash[8];
    TMD_CONTENT_INFO content_infos[64];
} TMD_HEADER;
static_assert(offsetof(TMD_HEADER, issuer) == 0x0140, "TMD_HEADER.issuer sits at the wrong offset");
static_assert(offsetof(TMD_HEADER, tid) == 0x018C, "TMD_HEADER.tid sits at the wrong offset");
static_assert(offsetof(TMD_HEADER, num_contents) == 0x01DE, "TMD_HEADER.num_contents sits at the wrong offset");
static_assert(offsetof(TMD_HEADER, content_infos) == 0x0204, "TMD_HEADER.content_infos sits at the wrong offset");
static_assert(sizeof(TMD_HEADER) == 0x0B04, "TMD_HEADER has the wrong size");

// The .h3 file of a content holds one 0x14 byte hash per 0x1000000D bytes of
// it, so its size follows from the content size. Same formula as getH3size()
// of NUSspli, with the division done in integers instead of doubles.
static inline uint64_t getH3size(uint64_t appSize)
{
    return ((appSize + 0x1000000C) / 0x1000000D) * 0x14;
}
