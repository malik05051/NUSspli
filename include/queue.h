/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2022 Xpl0itU <DaThinkingChair@protonmail.com>             *
 * Copyright (c) 2022 V10lator <v10lator@myway.de>                         *
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

#include <wut-fixups.h>

#include <stdbool.h>
#include <stdint.h>

#include <file.h>
#include <filesystem.h>
#include <list.h>
#include <titles.h>
#include <tmd.h>

#include <curl/curl.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        OPERATION_DOWNLOAD = 0x01,
        OPERATION_INSTALL = 0x02,
        OPERATION_DOWNLOAD_INSTALL = OPERATION_DOWNLOAD | OPERATION_INSTALL,
    } OPERATION;

    typedef struct
    {
        char *buf;
        size_t size;
    } RAMBUF;

    typedef struct
    {
        TMD *tmd;
        size_t tmdSize;
        RAMBUF *rambuf;
        OPERATION operation;
        const TitleEntry *entry;
        char titleVer[33];
        char folderName[FS_MAX_PATH - 11] __attribute__((__aligned__(0x40)));
        NUSDEV dlDev;
        bool toUSB;
        bool keepFiles;
    } TitleData;

    typedef struct
    {
        curl_off_t downloaded;
        curl_off_t dlSize;
        uint32_t packages;
        uint32_t current;
        uint32_t eta;
    } QUEUE_DATA;

    bool initQueue();
    void shutdownQueue();
    int addToQueue(TitleData *data);
    bool removeFromQueue(uint32_t index);
    void clearQueue();
    bool proccessQueue();
    LIST *getTitleQueue();
    // Walks the queue and answers whether an entry with this name is in it.
    bool isQueued(const char *name);
    // The same for an entry of the title database.
    bool isEntryQueued(const TitleEntry *entry);

    /*
     * The download manager: startQueueInBackground() runs the queue on a
     * thread of its own and returns at once, so the menus stay usable
     * while it downloads and installs. Entries added in the meantime are
     * worked through as well. The engine code is the same as for
     * proccessQueue(): it reaches the UI through ui.h only, which hands
     * its dialogs to the main thread and its frames to the progress screen.
     *
     * The main thread is the only caller of everything below.
     */
    bool startQueueInBackground();
    // True until the run ended (finished, failed or cancelled).
    bool queueRunning();
    // Asks the run to stop. The entry it works on stays in the queue, a
    // download resumes from where it stopped the next time.
    void cancelQueue();
    // The list may change under the menus while the queue runs: they hold
    // this lock while they walk it.
    void lockQueue();
    void unlockQueue();
    // The entry the run works on right now (it cannot be removed), or NULL.
    const TitleData *getActiveQueueEntry();

#ifdef __cplusplus
}
#endif
