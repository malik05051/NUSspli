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

#include <cstdint>

// The same server NUSspli downloads from, see include/downloader.h there.
#define DOWNLOAD_URL "http://ccs.cdn.wup.shop.nintendo.net/ccs/download/"

typedef struct
{
    void *ctx;
    /*
     * Called while the file comes in, with the number of bytes that are on
     * the SD card for this file at that moment - not with the difference, so
     * that a server which answers a resume request with the whole file
     * instead of the rest of it does not push the number past the end.
     *
     * It is also called without new bytes every now and then, so that a
     * stopped download does not have to wait for the next block, and
     * returning false stops the download there.
     */
    bool (*onProgress)(void *ctx, uint64_t fileBytes);
} DownloadProgress;

typedef enum
{
    DOWNLOAD_RESULT_OK = 0, // The file is complete on the SD card
    DOWNLOAD_RESULT_ABORTED, // onProgress asked us to stop
    DOWNLOAD_RESULT_ERROR, // Network or filesystem error
} DOWNLOAD_RESULT;

bool initDownloader();
void deinitDownloader();

/*
 * Downloads url to path, continuing an unfinished file instead of starting
 * over. expectedSize is the size the finished file has, 0 when it is unknown:
 * with a size the file is left alone when it is already that big, and a file
 * that grew past it is downloaded again from the start.
 */
DOWNLOAD_RESULT downloadFile(const char *url, const char *path, uint64_t expectedSize, const DownloadProgress *progress);
