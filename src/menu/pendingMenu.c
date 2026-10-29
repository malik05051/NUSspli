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

#include <wut-fixups.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <file.h>
#include <filesystem.h>
#include <input.h>
#include <localisation.h>
#include <menu/installer.h>
#include <menu/pending.h>
#include <menu/utils.h>
#include <titles.h>
#include <tmd.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#pragma GCC diagnostic pop

// How many entries of the install folder the look costs at most: the TMD of
// a folder is read to see whether it is finished, so a card with hundreds of
// them must not hold up the boot.
#define PENDING_SCAN_MAX 64

/*
 * True when every content the TMD names lies in the folder at its full
 * size. path holds the folder with its trailing slash and has room for a
 * content name behind len, which is where the names are written.
 */
static bool downloadComplete(char *path, size_t len, const TMD *tmd)
{
    for(uint16_t i = 0; i < tmd->num_contents; ++i)
    {
        hex(tmd->contents[i].cid, 8, path + len);
        strcpy(path + len + 8, ".app");
        if((uint64_t)getFilesize(path) != tmd->contents[i].size)
            return false;

        if((tmd->contents[i].type & TMD_CONTENT_TYPE_HASHED) == 0)
            continue;

        strcpy(path + len + 8, ".h3");
        if((uint64_t)getFilesize(path) != (uint64_t)getH3size(tmd->contents[i].size))
            return false;
    }

    return true;
}

// The folder name of the first finished download in the install folder of
// the SD card, written into path, or false when there is none.
static bool findPendingInstall(char *path, size_t size)
{
    strcpy(path, INSTALL_DIR_SD);
    const size_t dirLen = strlen(path);

    FSADirectoryHandle dir;
    if(FSAOpenDir(getFSAClient(), path, &dir) != FS_ERROR_OK)
        return false;

    bool found = false;
    FSADirectoryEntry entry;
    for(int i = 0; !found && i < PENDING_SCAN_MAX && FSAReadDir(getFSAClient(), dir, &entry) == FS_ERROR_OK; ++i)
    {
        if(!(entry.info.flags & FS_STAT_DIRECTORY) || entry.name[0] == '.')
            continue;

        // The folder, its slash and the longest name written behind it.
        const size_t len = dirLen + strlen(entry.name) + 1;
        if(len + sizeof("00000000.app") > size)
        {
            debugPrintf("Path too long: %s", entry.name);
            continue;
        }

        strcpy(path + dirLen, entry.name);
        path[len - 1] = '/';
        path[len] = '\0';

        // MCP installs a title with both of these, so a folder without them
        // is no finished download whatever else it holds.
        strcpy(path + len, "title.tik");
        bool ticket = fileExists(path);
        strcpy(path + len, "title.cert");
        if(!ticket || !fileExists(path))
        {
            path[len] = '\0';
            continue;
        }

        // getTmd() appends "/title.tmd" of its own, so it wants the folder
        // without the trailing slash this one carries.
        path[len - 1] = '\0';
        TMD *tmd = getTmd(path, false);
        path[len - 1] = '/';
        path[len] = '\0';
        if(tmd == NULL)
            continue;

        found = tmd->num_contents != 0 && downloadComplete(path, len, tmd);
        MEMFreeToDefaultHeap(tmd);
        // downloadComplete() wrote content names behind len: give the
        // caller the folder back, not the last file that was looked at.
        path[len] = '\0';
    }

    FSACloseDir(getFSAClient(), dir);
    return found;
}

void offerPendingInstalls()
{
    char path[FS_MAX_PATH];
    if(!findPendingInstall(path, sizeof(path)))
        return;

    debugPrintf("Finished download found: %s", path);

    char toFrame[512];
    snprintf(toFrame, sizeof(toFrame), "%s\n\n" BUTTON_A " %s || " BUTTON_B " %s",
        localise("A download in the install folder of the SD card is finished but not\ninstalled: the background plugin downloads, the install stays with\nNUSspli.\n\nOpen the installer?"),
        localise("Yes"),
        localise("No"));

    if(askYesNo(toFrame))
        installerMenu();
}
