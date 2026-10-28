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

#include <stdio.h>
#include <string.h>

#include <input.h>
#include <list.h>
#include <localisation.h>
#include <menu/engineFrames.h>
#include <menu/main.h>
#include <menu/queue.h>
#include <menu/utils.h>
#include <nim.h>
#include <queue.h>
#include <renderer.h>
#include <state.h>
#include <ticket.h>
#include <ui.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

#define MAX_ENTRIES (MAX_LINES - 4)
#define SPACER      7
#define SPACER_END  14

static LIST *titleQueue;
static uint32_t oldHold;
static size_t frameCount;
static size_t cursor;
static size_t pos;
static bool mov;

static void enterQueueMenu(void *param)
{
    (void)param;
    oldHold = 0;
    frameCount = 0;
    cursor = 0;
    pos = 0;
    titleQueue = getTitleQueue();
    mov = getListSize(titleQueue) >= MAX_ENTRIES;
}

// The list shrinks on its own while the download manager runs: keep the
// view inside of it.
static void clampQueueView()
{
    size_t size = getListSize(titleQueue);
    mov = size >= MAX_ENTRIES;
    if(size == 0)
    {
        cursor = pos = 0;
        return;
    }

    if(pos >= size)
        pos = 0;
    if(cursor + pos >= size)
        cursor = size - pos - 1;
}

static void renderQueueMenu()
{
    startNewFrame();
    boxToFrame(0, MAX_LINES - 3);

    char toScreen[FS_MAX_PATH + 256];
    size_t i = 0;
    size_t drawPos = pos; // local copy: the paging state must survive rendering
    int p;
    TitleData *data;
    MCPRegion region;

    lockQueue();
    const TitleData *active = getActiveQueueEntry();
    forEachListEntry(titleQueue, data)
    {
        if(drawPos)
        {
            --drawPos;
            continue;
        }

        if(cursor == i++)
            arrowToFrame(i, 1);

        if(data->operation & OPERATION_DOWNLOAD)
        {
            switch(data->dlDev)
            {
                case NUSDEV_SD:
                    deviceToFrame(i, 4, DEVICE_TYPE_SD);
                    break;
                case NUSDEV_MLC:
                    deviceToFrame(i, 4, DEVICE_TYPE_NAND);
                    break;
                default:
                    deviceToFrame(i, 4, DEVICE_TYPE_USB);
                    break;
            }
        }

        if(data->operation & OPERATION_INSTALL)
            deviceToFrame(i, SPACER, data->toUSB ? DEVICE_TYPE_USB : DEVICE_TYPE_NAND);

        if(isDLC(data->tmd->tid))
        {
            p = sizeof("[DLC] ") - 1;
            OSBlockMove(toScreen, "[DLC] ", p, false);
        }
        else if(isUpdate(data->tmd->tid))
        {
            p = sizeof("[UPD] ") - 1;
            OSBlockMove(toScreen, "[UPD] ", p, false);
        }
        else
            p = 0;

        if(data->entry == NULL)
        {
            region = MCP_REGION_UNKNOWN;
            strcpy(toScreen + p, prettyDir(data->folderName));
        }
        else
        {
            region = data->entry->region;
            strcpy(toScreen + p, data->entry->name);
        }

        flagToFrame(i, SPACER + 3, region);
        // The entry the download manager works on right now.
        if(data == active)
            textToFrameColoredCut(i, SPACER + 6, toScreen, SCREEN_COLOR_GREEN, (SCREEN_WIDTH - (FONT_SIZE << 1)) - (getSpaceWidth() * SPACER_END));
        else
            textToFrameCut(i, SPACER + 6, toScreen, (SCREEN_WIDTH - (FONT_SIZE << 1)) - (getSpaceWidth() * SPACER_END));

        if(i == MAX_ENTRIES)
            break;
    }
    unlockQueue();

    if(queueRunning())
    {
        strcpy(toScreen, localise("Press " BUTTON_B " to return"));
        strcat(toScreen, " || ");
        strcat(toScreen, localise(BUTTON_PLUS " to show the progress"));
        textToFrame(MAX_LINES - 2, ALIGNED_CENTER, toScreen);
        textToFrame(MAX_LINES - 1, ALIGNED_CENTER, localise(BUTTON_MINUS " to delete an item"));
        return;
    }

    strcpy(toScreen, localise("Press " BUTTON_B " to return"));
    strcat(toScreen, " || ");
    strcat(toScreen, localise(BUTTON_X " to download in the background"));
    textToFrame(MAX_LINES - 2, ALIGNED_CENTER, toScreen);

    strcpy(toScreen, localise(BUTTON_PLUS " to start the queue"));
    strcat(toScreen, " || ");
    strcat(toScreen, localise(BUTTON_MINUS " to delete an item"));
    strcat(toScreen, " || ");
    strcat(toScreen, localise(BUTTON_Y " Download Management"));
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, toScreen);
}

// The A/B question of the handoff below stays an overlay on the frame of
// the queue, the way the pre-download screen asks its questions.
static void *qmOverlay;

static void updateQMAsk()
{
    if(vpad.trigger & VPAD_BUTTON_B)
        uiPop();
    else if(vpad.trigger & VPAD_BUTTON_A)
    {
        uiSetResult(1);
        uiPop();
    }
}

static void leaveQMAsk()
{
    uiHideOverlay(qmOverlay);
    qmOverlay = NULL;
}

static const UIScreen qmAskScreen = {
    .name = "queue question",
    .buttons = VPAD_BUTTON_A | VPAD_BUTTON_B,
    .update = updateQMAsk,
    .leave = leaveQMAsk,
};

// Returns 1 on A, 0 on B and -1 when there is no overlay or the app
// stopped. The overlay is gone when this returns.
static int qmAskAnswer(void *ovl)
{
    if(ovl == NULL)
        return -1;

    qmOverlay = ovl;
    int ret = uiModal(&qmAskScreen, NULL);
    if(!AppRunning(true))
        return -1;

    return ret;
}

/*
 * Hands the entries the system can take to the Wii U Download Management,
 * which downloads and installs them itself - in its own background and,
 * with the standby functions of the console turned on, while the console
 * is switched off, which NUSspli itself cannot do. What it took is out of
 * the queue, the rest stays in it for NUSspli.
 *
 * Returns true when the screen below is gone with the last entry.
 */
static bool qmSystemDownload()
{
    char txt[1024];
    snprintf(txt, sizeof(txt), "%s\n\n" BUTTON_A " %s || " BUTTON_B " %s",
        localise("Experimental: the Wii U Download Management downloads and installs\nthe titles itself, also while the console is switched off when its\nstandby functions are on. It installs to the internal memory, it needs\nthe ticket of a title on the console, and it keeps the rest here."),
        localise("Yes"), localise("No"));

    if(qmAskAnswer(uiShowOverlay(txt)) != 1)
        return false;

    // Both halves of the walk below take their time - hasTicket() reads the
    // ticket bucket of the console and the registration took seconds in the
    // tests - so a frame says so while they run.
    showStatusFrame(localise("Please wait..."), NULL, false, -1);

    NNResult res = nimInitialize();
    if(NNResult_IsFailure(res))
    {
        readInput();
        snprintf(txt, sizeof(txt), "%s\n\n%#010x", localise("The Wii U Download Management refused the title."), (unsigned int)res.value);
        showErrorFrame(txt);
        return false;
    }

    NimTitlePackageTaskConfig config __attribute__((__aligned__(0x40)));
    size_t total = getListSize(titleQueue);
    size_t handed = 0;
    size_t kept = 0;
    size_t refused = 0;
    int32_t lastCode = 0;

    // From the back of the queue: an entry that leaves it keeps the indices
    // below it where they are.
    for(size_t i = total; i > 0; --i)
    {
        uint32_t idx = (uint32_t)(i - 1);

        lockQueue();
        const TitleData *data = getContent(titleQueue, idx);
        // The Download Management always installs instead of leaving the
        // files of a title on a device, so an entry that only downloads is
        // none of its business.
        bool installs = data != NULL && (data->operation & OPERATION_INSTALL);
        uint64_t tid = data == NULL ? 0 : data->tmd->tid;
        uint16_t tver = data == NULL ? 0 : data->tmd->title_version;
        unlockQueue();

        // It installs with the ticket the console has, so without one it
        // downloads the title but fails to install it (105-3683, see
        // nim.h). The check reads files, so it runs without the lock.
        if(!installs || !hasTicket(tid))
        {
            ++kept;
            continue;
        }

        nimMakeConfig(&config, tid, tver);
        res = nimRegister(&config, tid);
        if(NNResult_IsFailure(res))
        {
            lastCode = nimErrorCode(res);
            ++refused;
            continue;
        }

        // The entry the download manager works on is the only one that
        // cannot be removed, and the handoff is not offered while it runs.
        removeFromQueue(idx);
        ++handed;
    }

    nimFinalize();
    // A button pressed during the wait must not answer the dialogs below.
    readInput();

    addToScreenLog("%u of %u queue entries handed to the Wii U Download Management", (unsigned int)handed, (unsigned int)total);
    if(kept != 0)
        addToScreenLog("%u stayed in the queue for NUSspli itself", (unsigned int)kept);

    if(refused != 0)
    {
        snprintf(txt, sizeof(txt), "%s\n\n%03d-%04d", localise("The Wii U Download Management refused the title."), (int)(lastCode / 10000), (int)(lastCode % 10000));
        showErrorFrame(txt);
    }

    if(handed == 0)
    {
        if(refused == 0)
            showErrorFrame(localise("The Wii U Download Management can take none of these entries: it\nalways installs to the internal memory and needs the ticket of a\ntitle on the console, which a title NUSspli installed once has.\nNUSspli downloads the rest itself."));

        return false;
    }

    showFinishedScreen(NULL, FINISHING_OPERATION_SYSTEM_DOWNLOAD_QUEUE);
    if(getListSize(titleQueue) == 0)
    {
        uiSetResult(0);
        uiPop();
        return true;
    }

    clampQueueView();
    return false;
}

static void updateQueueMenu()
{
    bool dpadAction;

    if(vpad.trigger & VPAD_BUTTON_B)
    {
        uiSetResult(0);
        uiPop();
        return;
    }

    // The download manager takes finished entries out of the list: show
    // that once a second.
    if(queueRunning() && uiFrameCount() % FRAMERATE == 0)
    {
        clampQueueView();
        uiInvalidate();
    }

    // Nothing left to move over (the run in the background may still be
    // finishing its last entry).
    if(getListSize(titleQueue) == 0)
    {
        if(!queueRunning())
        {
            uiSetResult(0);
            uiPop();
        }

        return;
    }

    if(vpad.hold & VPAD_BUTTON_UP)
    {
        if(oldHold != VPAD_BUTTON_UP)
        {
            oldHold = VPAD_BUTTON_UP;
            frameCount = DPAD_COOLDOWN_FRAMES;
            dpadAction = true;
        }
        else if(frameCount == 0)
            dpadAction = true;
        else
        {
            --frameCount;
            dpadAction = false;
        }

        if(dpadAction)
        {
            uiInvalidate();
            if(cursor)
                --cursor;
            else if(pos)
                --pos;
        }
    }
    else if(vpad.hold & VPAD_BUTTON_DOWN)
    {
        if(oldHold != VPAD_BUTTON_DOWN)
        {
            oldHold = VPAD_BUTTON_DOWN;
            frameCount = DPAD_COOLDOWN_FRAMES;
            dpadAction = true;
        }
        else if(frameCount == 0)
            dpadAction = true;
        else
        {
            --frameCount;
            dpadAction = false;
        }

        if(dpadAction)
        {
            uiInvalidate();
            if(cursor < getListSize(titleQueue) - pos - 1 && cursor < MAX_ENTRIES - 1)
                ++cursor;
            else if(mov && cursor + ++pos == getListSize(titleQueue))
                --pos;
        }
    }
    else if(mov)
    {
        if(vpad.hold & VPAD_BUTTON_RIGHT)
        {
            if(oldHold != VPAD_BUTTON_RIGHT)
            {
                oldHold = VPAD_BUTTON_RIGHT;
                frameCount = DPAD_COOLDOWN_FRAMES;
                dpadAction = true;
            }
            else if(frameCount == 0)
                dpadAction = true;
            else
            {
                --frameCount;
                dpadAction = false;
            }

            if(dpadAction)
            {
                uiInvalidate();
                pos += MAX_ENTRIES;
                if(pos >= getListSize(titleQueue))
                    pos = 0;

                cursor = 0;
            }
        }
        else if(vpad.hold & VPAD_BUTTON_LEFT)
        {
            if(oldHold != VPAD_BUTTON_LEFT)
            {
                oldHold = VPAD_BUTTON_LEFT;
                frameCount = DPAD_COOLDOWN_FRAMES;
                dpadAction = true;
            }
            else if(frameCount == 0)
                dpadAction = true;
            else
            {
                --frameCount;
                dpadAction = false;
            }

            if(dpadAction)
            {
                uiInvalidate();
                if(pos >= MAX_ENTRIES)
                    pos -= MAX_ENTRIES;
                else // Wrap to the last page RIGHT paging can reach, so paging can't oscillate
                    pos = ((getListSize(titleQueue) - 1) / MAX_ENTRIES) * MAX_ENTRIES;
                cursor = 0;
            }
        }
    }

    if(vpad.trigger & VPAD_BUTTON_X && !queueRunning())
    {
        // Back to the menus: the queue keeps running behind them.
        if(startQueueInBackground())
        {
            uiSetResult(0);
            uiPop();
            return;
        }
    }

    if(vpad.trigger & VPAD_BUTTON_Y && !queueRunning())
    {
        if(qmSystemDownload())
            return;

        if(!AppRunning(true))
            return;
    }

    if(vpad.trigger & VPAD_BUTTON_PLUS && queueRunning())
    {
        queueProgressMenu();
        clampQueueView();
    }
    else if(vpad.trigger & VPAD_BUTTON_PLUS)
    {
        if(proccessQueue())
        {
            showFinishedScreen(NULL, FINISHING_OPERATION_QUEUE);
            uiSetResult(1);
            uiPop();
            return;
        }

        // The queue may have shrunk on a mid-queue failure: re-clamp the view
        if(getListSize(titleQueue) == 0)
        {
            uiSetResult(0);
            uiPop();
            return;
        }

        clampQueueView();
    }

    // The entry the download manager works on cannot be removed.
    if(vpad.trigger & VPAD_BUTTON_MINUS && removeFromQueue(cursor + pos))
    {
        if(getListSize(titleQueue) == 0)
        {
            uiSetResult(0);
            uiPop();
            return;
        }

        if(cursor + pos == getListSize(titleQueue))
        {
            if(cursor)
                --cursor;
            else if(pos)
                --pos;
        }

        mov = getListSize(titleQueue) >= MAX_ENTRIES;
    }

    if(oldHold && !(vpad.hold & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT)))
        oldHold = 0;
}

static const UIScreen queueScreen = {
    .name = "queue",
    .buttons = VPAD_BUTTON_B | VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT | VPAD_BUTTON_PLUS | VPAD_BUTTON_MINUS | VPAD_BUTTON_X | VPAD_BUTTON_Y,
    .enter = enterQueueMenu,
    .update = updateQueueMenu,
    .render = renderQueueMenu,
};

bool queueMenu()
{
    return uiModal(&queueScreen, NULL);
}

// The progress screen of the download manager: the frame the queue thread
// would have drawn (see engineFrames.h) with a footer of its own.
static void *cancelOverlay;
// The breadcrumb of the queue thread, shown as the overlay it would have
// been on the main thread.
static void *statusOverlay;
static char statusText[256];

static void hideStatusOverlay()
{
    if(statusOverlay != NULL)
    {
        uiHideOverlay(statusOverlay);
        statusOverlay = NULL;
    }
}

static void hideCancelOverlay()
{
    if(cancelOverlay != NULL)
    {
        uiHideOverlay(cancelOverlay);
        cancelOverlay = NULL;
    }
}

static void enterProgressMenu(void *param)
{
    (void)param;
    cancelOverlay = NULL;
    statusOverlay = NULL;
    statusText[0] = '\0';
    reserveScreenLogLines(2);
}

static void leaveProgressMenu()
{
    hideCancelOverlay();
    hideStatusOverlay();
    reserveScreenLogLines(0);
}

static void renderProgressMenu()
{
    drawEngineFrame();

    char toScreen[256];
    strcpy(toScreen, localise("Press " BUTTON_B " to return"));
    strcat(toScreen, " || ");
    strcat(toScreen, localise(BUTTON_X " to cancel the downloads"));
    lineToFrame(MAX_LINES - 2, SCREEN_COLOR_WHITE);
    textToFrame(MAX_LINES - 1, ALIGNED_CENTER, toScreen);
}

static void updateProgressMenu()
{
    // Finished, failed or cancelled: whatever it had to say is said.
    if(!queueRunning())
    {
        uiPop();
        return;
    }

    // The bars glide every frame, just like they do in the foreground.
    uiInvalidate();

    char text[sizeof(statusText)];
    if(getEngineStatus(text, sizeof(text)))
    {
        if(statusOverlay == NULL || strcmp(text, statusText) != 0)
        {
            hideStatusOverlay();
            strcpy(statusText, text);
            statusOverlay = uiShowOverlay(statusText);
        }
    }
    else
        hideStatusOverlay();

    if(cancelOverlay == NULL)
    {
        if(vpad.trigger & VPAD_BUTTON_B)
            uiPop();
        else if(vpad.trigger & VPAD_BUTTON_X)
        {
            char toScreen[256];
            strcpy(toScreen, localise("Do you really want to cancel?"));
            strcat(toScreen, "\n\n" BUTTON_A " ");
            strcat(toScreen, localise("Yes"));
            strcat(toScreen, " || " BUTTON_B " ");
            strcat(toScreen, localise("No"));
            cancelOverlay = uiShowOverlay(toScreen);
        }
    }
    else if(vpad.trigger & VPAD_BUTTON_A)
    {
        hideCancelOverlay();
        cancelQueue();
    }
    else if(vpad.trigger & VPAD_BUTTON_B)
        hideCancelOverlay();
}

static const UIScreen progressScreen = {
    .name = "download manager",
    .buttons = VPAD_BUTTON_A | VPAD_BUTTON_B | VPAD_BUTTON_X,
    .enter = enterProgressMenu,
    .update = updateProgressMenu,
    .render = renderProgressMenu,
    .leave = leaveProgressMenu,
};

void queueProgressMenu()
{
    if(queueRunning())
        uiModal(&progressScreen, NULL);
}

void downloadManagerMenu()
{
    if(queueRunning())
        queueProgressMenu();
    else if(getListSize(getTitleQueue()) != 0)
        queueMenu();
    else
        showErrorFrame(localise("The queue is empty"));
}
