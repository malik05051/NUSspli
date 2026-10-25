/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2022 Xpl0itU <DaThinkingChair@protonmail.com>             *
 * Copyright (c) 2022-2024 V10lator <v10lator@myway.de>                    *
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

#include <string.h>

#include <downloader.h>
#include <filesystem.h>
#include <installer.h>
#include <list.h>
#include <menu/engineFrames.h>
#include <menu/utils.h>
#include <queue.h>
#include <state.h>
#include <thread.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#pragma GCC diagnostic pop

static LIST *titleQueue;

// The download manager: the queue can run on a thread of its own while the
// menus stay usable, and they keep adding to it and removing from it in the
// meantime. Every access to the list goes through this lock.
static spinlock queueLock = SPINLOCK_FREE;
// The entry the queue works on right now. It stays in the list so the menus
// can show it, but they may not remove it.
static TitleData *volatile activeTitle = NULL;
static OSThread *queueThread = NULL;
static volatile bool queueThreadDone = false;
static QUEUE_DATA backgroundData;

static void stopBackgroundQueue();

bool initQueue()
{
    titleQueue = createList();
    return titleQueue != NULL;
}

void shutdownQueue()
{
    // The steps torn down after this one are what the queue thread uses.
    stopBackgroundQueue();
    clearQueue();
    destroyList(titleQueue, false);
}

void lockQueue()
{
    spinLockAsMutex(queueLock);
}

void unlockQueue()
{
    spinReleaseLock(queueLock);
}

const TitleData *getActiveQueueEntry()
{
    return activeTitle;
}

int addToQueue(TitleData *data)
{
    TitleData *title;
    int ret = 0;
    lockQueue();
    forEachListEntry(titleQueue, title)
    {
        if(data->operation & OPERATION_INSTALL && title->operation & OPERATION_INSTALL)
        {
            if(data->toUSB == title->toUSB && data->tmd->tid == title->tmd->tid)
            {
                ret = 2;
                goto unlock;
            }
        }
        if(data->operation & OPERATION_DOWNLOAD && title->operation & OPERATION_DOWNLOAD)
        {
            if(data->dlDev == title->dlDev && data->tmd->tid == title->tmd->tid)
            {
                ret = 3;
                goto unlock;
            }
        }
    }

    ret = addToListEnd(titleQueue, data) ? 1 : 0;
unlock:
    unlockQueue();
    return ret;
}

static inline void freeTitle(TitleData *title)
{
    if(title->rambuf != NULL)
        freeRamBuf(title->rambuf);
    else
        MEMFreeToDefaultHeap(title->tmd);

    MEMFreeToDefaultHeap(title);
}

static inline void removeFQ(TitleData *title)
{
    if(title != NULL)
    {
        lockQueue();
        removeFromList(titleQueue, title);
        unlockQueue();
        freeTitle(title);
    }
}

// What an entry needs: sizes[0] on USB, sizes[1] on the SD card and
// sizes[2] on NAND, and the bytes to download into queueData (may be NULL).
static void addTitleSizes(const TitleData *title, uint64_t sizes[3], QUEUE_DATA *queueData)
{
    for(uint16_t i = 0; i < title->tmd->num_contents; ++i)
    {
        if(title->operation & OPERATION_INSTALL)
            sizes[title->toUSB ? 0 : 2] += title->tmd->contents[i].size;

        if(title->operation & OPERATION_DOWNLOAD)
        {
            if(queueData != NULL)
            {
                queueData->dlSize += title->tmd->contents[i].size;
                if(title->tmd->contents[i].type & TMD_CONTENT_TYPE_HASHED)
                    queueData->dlSize += getH3size(title->tmd->contents[i].size);
            }

            if(title->keepFiles)
            {
                int j = title->dlDev & NUSDEV_USB ? 0 : (title->dlDev & NUSDEV_SD ? 1 : 2);
                if(title->tmd->contents[i].type & TMD_CONTENT_TYPE_HASHED)
                    sizes[j] += getH3size(title->tmd->contents[i].size);

                sizes[j] += title->tmd->contents[i].size;
            }
        }
    }
}

static bool checkSizes(const uint64_t sizes[3])
{
    for(int i = 0; i < 3; ++i)
    {
        if(sizes[i] != 0)
        {
            NUSDEV toCheck;
            switch(i)
            {
                case 0:
                    toCheck = getUSB();
                    break;
                case 1:
                    toCheck = NUSDEV_SD;
                    break;
                default:
                    toCheck = NUSDEV_MLC;
            }

            if(!checkFreeSpace(toCheck, sizes[i]))
                return false;
        }
    }

    return true;
}

// Main thread, before a run: is there room for the whole queue?
static bool checkQueueSpace()
{
    TitleData *title;
    uint64_t sizes[3] = { 0, 0, 0 };

    lockQueue();
    forEachListEntry(titleQueue, title)
        addTitleSizes(title, sizes, NULL);
    unlockQueue();

    return checkSizes(sizes);
}

// Works through the queue from its head until it is empty, on whichever
// thread calls it. Finished entries leave the list, a failed one stays.
static bool runQueue(QUEUE_DATA *queueData)
{
    TitleData *title;
    TitleData *entry;
    const bool background = !isMainThread();
    bool ret = false;

    disableApd();
    while(AppRunning(true))
    {
        // Entries may come in while the queue runs, so the totals are
        // counted anew for every title: the bytes of the titles before
        // are in downloaded already.
        lockQueue();
        title = getContent(titleQueue, 0);
        activeTitle = title;
        if(title != NULL)
        {
            queueData->packages = queueData->current;
            queueData->dlSize = queueData->downloaded;
            forEachListEntry(titleQueue, entry)
            {
                if(entry->operation & OPERATION_DOWNLOAD)
                    queueData->packages++;

                uint64_t unused[3] = { 0, 0, 0 };
                addTitleSizes(entry, unused, queueData);
            }
        }
        unlockQueue();

        if(title == NULL)
        {
            ret = true;
            break;
        }

        // The whole queue was checked before the run, but not what came
        // in while it ran.
        if(background)
        {
            uint64_t sizes[3] = { 0, 0, 0 };
            addTitleSizes(title, sizes, NULL);
            if(!checkSizes(sizes))
                break;
        }

        if(title->operation & OPERATION_DOWNLOAD)
        {
            queueData->current++;

            if(!downloadTitle(title->tmd, title->tmdSize, title->entry, title->titleVer, title->folderName, title->operation & OPERATION_INSTALL, title->dlDev, title->toUSB, title->keepFiles, queueData))
                break;
        }
        else if(title->operation & OPERATION_INSTALL)
        {
            bool hasDeps = title->tmd != NULL && (isDLC(title->tmd->tid) || isUpdate(title->tmd->tid));
            if(!install(title->entry == NULL ? prettyDir(title->folderName) : title->entry->name, hasDeps, title->dlDev, title->folderName, title->toUSB, title->keepFiles, title->tmd))
                break;
        }

        activeTitle = NULL;
        removeFQ(title);
    }

    activeTitle = NULL;
    enableApd();
    return ret;
}

bool proccessQueue()
{
    if(queueRunning())
        return false;

    QUEUE_DATA queueData = { .downloaded = 0, .dlSize = 0, .packages = 0, .current = 0, .eta = -1 };
    if(!checkQueueSpace())
        return false;

    return runQueue(&queueData);
}

static int queueThreadMain(int argc, const char **argv)
{
    (void)argc;
    (void)argv;

    // A cancelled run ends without the dialog: uiCallOnMain() withdraws it.
    if(runQueue(&backgroundData))
        showFinishedScreen(NULL, FINISHING_OPERATION_QUEUE);

    queueThreadDone = true;
    return 0;
}

bool startQueueInBackground()
{
    if(queueRunning() || getListSize(titleQueue) == 0)
        return false;

    if(!checkQueueSpace())
        return false;

    // The first query of the SD card joins the thread that measures it,
    // and two threads must not join one: the queue thread asks as well.
    checkSpaceThread();

    backgroundData.downloaded = 0;
    backgroundData.dlSize = 0;
    backgroundData.packages = 0;
    backgroundData.current = 0;
    backgroundData.eta = -1;
    resetEngineFrames();
    queueThreadDone = false;

    // Prepared and resumed in two steps: the thread has to be known as the
    // background thread (see AppRunning) before its first line runs.
    queueThread = prepareThread("NUSspli queue", THREAD_PRIORITY_MEDIUM, STACKSIZE_BIG, queueThreadMain, 0, NULL, OS_THREAD_ATTRIB_AFFINITY_CPU0);
    if(queueThread == NULL)
        return false;

    setBackgroundThread(queueThread);
    OSResumeThread(queueThread);
    return true;
}

bool queueRunning()
{
    // The thread cannot free itself, so the main thread collects it the
    // first time it asks after the run ended.
    if(queueThread != NULL && queueThreadDone && isMainThread())
    {
        stopThread(queueThread, NULL);
        queueThread = NULL;
        setBackgroundThread(NULL);
    }

    return queueThread != NULL;
}

void cancelQueue()
{
    if(queueRunning())
        cancelBackground();
}

static void stopBackgroundQueue()
{
    if(queueThread == NULL)
        return;

    cancelBackground();
    stopThread(queueThread, NULL);
    queueThread = NULL;
    setBackgroundThread(NULL);
}

bool removeFromQueue(uint32_t index)
{
    lockQueue();
    TitleData *title = getContent(titleQueue, index);
    if(title == NULL || title == activeTitle)
    {
        unlockQueue();
        return false;
    }

    removeFromList(titleQueue, title);
    unlockQueue();

    freeTitle(title);
    return true;
}

void clearQueue()
{
    TitleData *title;
    TitleData *last = NULL;
    forEachListEntry(titleQueue, title)
    {
        removeFQ(last);
        last = title;
    }

    removeFQ(last);
}

LIST *getTitleQueue()
{
    return titleQueue;
}

// The argument is called name on purpose: it is the string every queued
// entry is looked up by, the file browser passes a path today and a
// caller from the title side can pass a name of the database.
bool isQueued(const char *name)
{
    TitleData *title;
    bool ret = false;
    lockQueue();
    forEachListEntry(titleQueue, title)
    {
        if(strcmp(name, title->folderName) == 0)
        {
            ret = true;
            break;
        }
    }
    unlockQueue();

    return ret;
}

bool isEntryQueued(const TitleEntry *entry)
{
    TitleData *title;
    bool ret = false;
    lockQueue();
    forEachListEntry(titleQueue, title)
    {
        if(title->entry == entry)
        {
            ret = true;
            break;
        }
    }
    unlockQueue();

    return ret;
}
