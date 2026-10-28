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

#include "worker.h"

#include "config.h"
#include "downloader.h"
#include "logger.h"
#include "powersave.h"
#include "tmd.h"

#include <notifications/notifications.h>
#include <rpxloader/rpxloader.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>

using namespace std::literals;

/*
 * NUSspli saves a download as a folder below the install folder on the root
 * of the SD card - the one WUPInstaller reads - and writes title.tmd,
 * title.tik and title.cert into it before the contents follow. That is all
 * this plugin needs: the TMD names every content and its size, so an
 * unfinished download can be told apart from a finished one by looking at
 * the files, and the plugin finishes what is missing without NUSspli having
 * to hand anything over.
 *
 * "/vol/app_sd/install/" of NUSspli is this path seen from a plugin.
 */
#define INSTALL_DIR "fs:/vol/external01/install/"

// A TMD without contents is 0x0B04 bytes, num_contents is 16 bit.
#define TMD_MAX_SIZE         (sizeof(TMD_HEADER) + (0xFFFF * sizeof(TMD_CONTENT)))

#define NOTIFICATION_UPDATE  500ms
#define NOTIFICATION_SECONDS 15.0f

typedef struct
{
    std::string name; // File name inside of the folder
    std::string url; // Where it comes from
    uint64_t size; // The size it has when it is complete, 0 if unknown
    uint64_t have; // What is on the SD card right now
} ContentJob;

typedef struct
{
    std::string folder; // Folder name below INSTALL_DIR
    std::vector<ContentJob> contents;
    uint64_t missing; // Bytes left to download for this title
} TitleJob;

static std::optional<std::jthread> sWorker;
static std::mutex sStatusLock;
static BackgroundStatus sStatus = {
    .state = BACKGROUND_STATE_IDLE,
    .title = {},
    .titles = 0,
    .titlesDone = 0,
    .bytes = 0,
    .bytesDone = 0,
};

static NotificationModuleHandle sNotification = 0;
static bool sNotificationOpen = false;

BackgroundStatus getBackgroundStatus()
{
    std::lock_guard<std::mutex> lock(sStatusLock);
    return sStatus;
}

static std::string formatSize(uint64_t bytes)
{
    char buf[32];
    if(bytes >= 1024ULL * 1024ULL * 1024ULL)
        snprintf(buf, sizeof(buf), "%.2f GiB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    else
        snprintf(buf, sizeof(buf), "%.1f MiB", (double)bytes / (1024.0 * 1024.0));

    return buf;
}

static uint32_t percent(uint64_t done, uint64_t total)
{
    if(total == 0)
        return 100;

    return (uint32_t)std::min<uint64_t>(100, (done * 100) / total);
}

std::string getBackgroundStatusLine()
{
    const BackgroundStatus status = getBackgroundStatus();
    char buf[256];

    switch(status.state)
    {
        case BACKGROUND_STATE_RUNNING:
            snprintf(buf, sizeof(buf), "%s: %u%% (title %u of %u, %s of %s)", status.title.c_str(),
                percent(status.bytesDone, status.bytes), std::min(status.titlesDone + 1, status.titles), status.titles,
                formatSize(status.bytesDone).c_str(), formatSize(status.bytes).c_str());
            break;
        case BACKGROUND_STATE_FINISHED:
            snprintf(buf, sizeof(buf), "Finished: %u download(s) are ready to be installed with NUSspli", status.titlesDone);
            break;
        case BACKGROUND_STATE_FAILED:
            snprintf(buf, sizeof(buf), "Failed after %u of %u download(s), NUSspli can continue the rest", status.titlesDone, status.titles);
            break;
        case BACKGROUND_STATE_STOPPED:
            snprintf(buf, sizeof(buf), "Stopped, the rest follows the next time a title starts");
            break;
        case BACKGROUND_STATE_DISABLED:
            snprintf(buf, sizeof(buf), "Turned off");
            break;
        case BACKGROUND_STATE_IDLE:
        default:
            snprintf(buf, sizeof(buf), "Nothing to do, no unfinished NUSspli download found");
            break;
    }

    return buf;
}

static void setState(BACKGROUND_STATE state)
{
    std::lock_guard<std::mutex> lock(sStatusLock);
    sStatus.state = state;
}

// Notifications are a comfort function, so a module that is not there or an
// overlay that never gets ready never stops a download.
static bool notificationsUsable()
{
    bool ready = false;
    return gNotify && NotificationModule_IsOverlayReady(&ready) == NOTIFICATION_MODULE_RESULT_SUCCESS && ready;
}

static void showInfo(const char *text)
{
    if(!gNotify)
        return;

    // Kept until it can be shown: the download often ends while the console
    // switches titles, and the user should still get to read that it is done.
    NotificationModule_AddInfoNotificationEx(text, NOTIFICATION_SECONDS, { 255, 255, 255, 255 }, { 100, 100, 100, 255 }, nullptr, nullptr, true);
}

static void showError(const char *text)
{
    if(!gNotify)
        return;

    NotificationModule_AddErrorNotificationEx(text, NOTIFICATION_SECONDS, 0.5f, { 255, 255, 255, 255 }, { 237, 28, 36, 255 }, nullptr, nullptr, true);
}

// The line that stays on screen while the download runs: this is the progress
// the notification at the start of the run points the user at.
static void updateProgressNotification()
{
    if(!gNotify)
        return;

    const std::string line = "NUSspli: " + getBackgroundStatusLine();
    if(!sNotificationOpen)
    {
        if(NotificationModule_AddDynamicNotificationEx(line.c_str(), &sNotification, { 255, 255, 255, 255 }, { 100, 100, 100, 255 }, nullptr, nullptr, false) != NOTIFICATION_MODULE_RESULT_SUCCESS)
            return;

        sNotificationOpen = true;
        return;
    }

    NotificationModule_UpdateDynamicNotificationText(sNotification, line.c_str());
}

static void closeProgressNotification()
{
    if(!sNotificationOpen)
        return;

    NotificationModule_FinishDynamicNotification(sNotification, 0.0f);
    sNotificationOpen = false;
}

static uint64_t fileSize(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && st.st_size > 0 ? (uint64_t)st.st_size : 0;
}

static bool fileExists(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static bool dirExists(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::vector<uint8_t> readTmd(const std::string &path)
{
    std::vector<uint8_t> tmd;
    const uint64_t size = fileSize(path);
    if(size < sizeof(TMD_HEADER) || size > TMD_MAX_SIZE)
        return tmd;

    FILE *fp = fopen(path.c_str(), "rb");
    if(fp == nullptr)
        return tmd;

    tmd.resize(size);
    if(fread(tmd.data(), 1, size, fp) != size)
        tmd.clear();

    fclose(fp);
    return tmd;
}

/*
 * Turns one download folder into the list of files that are still missing.
 * Returns nothing when the folder is no unfinished NUSspli download: when it
 * holds no readable TMD, when NUSspli has not written the ticket yet, or when
 * every content is already there.
 */
static std::optional<TitleJob> scanFolder(const std::string &folder)
{
    const std::string dir = INSTALL_DIR + folder + "/";
    if(!fileExists(dir + "title.tik") || !fileExists(dir + "title.cert"))
    {
        debugPrintf("\"%s\" has no ticket, skipping it", folder.c_str());
        return {};
    }

    const std::vector<uint8_t> tmd = readTmd(dir + "title.tmd");
    if(tmd.empty())
    {
        debugPrintf("\"%s\" has no readable title.tmd, skipping it", folder.c_str());
        return {};
    }

    const TMD_HEADER *header = reinterpret_cast<const TMD_HEADER *>(tmd.data());
    const uint16_t count = header->num_contents;
    if(count == 0 || sizeof(TMD_HEADER) + ((size_t)count * sizeof(TMD_CONTENT)) > tmd.size())
    {
        debugPrintf("\"%s\" has a title.tmd that is too short for its %u contents", folder.c_str(), count);
        return {};
    }

    const TMD_CONTENT *contents = reinterpret_cast<const TMD_CONTENT *>(tmd.data() + sizeof(TMD_HEADER));

    char tid[17];
    snprintf(tid, sizeof(tid), "%016llx", (unsigned long long)header->tid);
    const std::string url = DOWNLOAD_URL + std::string(tid) + "/";

    TitleJob job = { .folder = folder, .contents = {}, .missing = 0 };
    for(uint16_t i = 0; i < count; ++i)
    {
        char cid[9];
        snprintf(cid, sizeof(cid), "%08lx", (unsigned long)contents[i].cid);

        // NUSspli writes the contents under the same lowercase hex names the
        // server uses, so the file name is the rest of the URL.
        ContentJob app = {
            .name = std::string(cid) + ".app",
            .url = url + cid,
            .size = contents[i].size,
            .have = 0,
        };

        app.have = fileSize(dir + app.name);
        if(app.have < app.size)
        {
            job.missing += app.size - app.have;
            job.contents.push_back(app);
        }

        if((contents[i].type & TMD_CONTENT_TYPE_HASHED) == 0)
            continue;

        ContentJob h3 = {
            .name = std::string(cid) + ".h3",
            .url = url + cid + ".h3",
            .size = getH3size(contents[i].size),
            .have = 0,
        };

        h3.have = fileSize(dir + h3.name);
        if(h3.have < h3.size)
        {
            job.missing += h3.size - h3.have;
            job.contents.push_back(h3);
        }
    }

    if(job.contents.empty())
        return {};

    return job;
}

static std::vector<TitleJob> scanInstallDir()
{
    std::vector<TitleJob> jobs;

    DIR *dir = opendir(INSTALL_DIR);
    if(dir == nullptr)
    {
        debugPrintf("Can't open \"%s\"", INSTALL_DIR);
        return jobs;
    }

    for(const struct dirent *entry = readdir(dir); entry != nullptr; entry = readdir(dir))
    {
        // d_type is not filled in by every filesystem, so the folders are
        // told apart from the files by asking for the entry itself.
        if(entry->d_name[0] == '.' || !dirExists(INSTALL_DIR + std::string(entry->d_name)))
            continue;

        std::optional<TitleJob> job = scanFolder(entry->d_name);
        if(job)
            jobs.push_back(std::move(*job));
    }

    closedir(dir);
    return jobs;
}

typedef struct
{
    std::stop_token token;
    uint64_t base; // Bytes that were done before the running file
    std::chrono::steady_clock::time_point nextUpdate;
} ProgressContext;

static bool onProgress(void *ptr, uint64_t fileBytes)
{
    ProgressContext *ctx = static_cast<ProgressContext *>(ptr);

    {
        std::lock_guard<std::mutex> lock(sStatusLock);
        sStatus.bytesDone = ctx->base + fileBytes;
    }

    // The notification is drawn by the overlay, so it is updated a few times
    // a second instead of on every block that comes in.
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if(now >= ctx->nextUpdate)
    {
        ctx->nextUpdate = now + NOTIFICATION_UPDATE;
        updateProgressNotification();
    }

    return !ctx->token.stop_requested();
}

// True when the title that is running is NUSspli itself: it downloads on its
// own then, and two downloaders in the same folder would fight over the file.
static bool runningNUSspli()
{
    char path[256];
    if(RPXLoader_GetPathOfRunningExecutable(path, sizeof(path)) != RPX_LOADER_RESULT_SUCCESS)
        return false; // Not loaded from the SD card, so not NUSspli

    std::string lower(path);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c)
        { return std::tolower(c); });
    return lower.find("nusspli") != std::string::npos;
}

static void workerMain(std::stop_token token)
{
    // Without the overlay there is nowhere to show the progress, but the
    // download itself does not depend on it.
    for(int i = 0; i < 300 && gNotify && !token.stop_requested() && !notificationsUsable(); ++i)
        std::this_thread::sleep_for(16ms);

    if(token.stop_requested())
        return;

    std::vector<TitleJob> jobs = scanInstallDir();
    if(jobs.empty())
    {
        debugPrintf("Nothing to download");
        setState(BACKGROUND_STATE_IDLE);
        return;
    }

    uint64_t missing = 0;
    for(const TitleJob &job : jobs)
        missing += job.missing;

    {
        std::lock_guard<std::mutex> lock(sStatusLock);
        sStatus.state = BACKGROUND_STATE_RUNNING;
        sStatus.title = jobs[0].folder;
        sStatus.titles = (uint32_t)jobs.size();
        sStatus.titlesDone = 0;
        sStatus.bytes = missing;
        sStatus.bytesDone = 0;
    }

    // Nobody is playing while this runs, which is just when the console
    // would power itself down and cut the download short.
    holdAutoPowerDown();

    char toScreen[256];
    snprintf(toScreen, sizeof(toScreen), "NUSspli: finishing %u unfinished download(s), %s to go. The progress can be seen here and in the config menu of the plugin.",
        (uint32_t)jobs.size(), formatSize(missing).c_str());
    showInfo(toScreen);
    updateProgressNotification();

    ProgressContext progressCtx = {
        .token = token,
        .base = 0,
        .nextUpdate = std::chrono::steady_clock::now(),
    };
    const DownloadProgress progress = { .ctx = &progressCtx, .onProgress = onProgress };

    uint32_t done = 0;
    bool stopped = false;
    bool failed = false;
    for(const TitleJob &job : jobs)
    {
        {
            std::lock_guard<std::mutex> lock(sStatusLock);
            sStatus.title = job.folder;
        }

        const std::string dir = INSTALL_DIR + job.folder + "/";
        bool complete = true;
        for(const ContentJob &content : job.contents)
        {
            const DOWNLOAD_RESULT res = downloadFile(content.url.c_str(), (dir + content.name).c_str(), content.size, &progress);

            // What came in counts even when the file is not finished, so that
            // the next file starts from the right number.
            const uint64_t now = fileSize(dir + content.name);
            if(now > content.have)
                progressCtx.base += now - content.have;

            {
                std::lock_guard<std::mutex> lock(sStatusLock);
                sStatus.bytesDone = progressCtx.base;
            }

            if(res == DOWNLOAD_RESULT_OK)
                continue;

            complete = false;
            if(res == DOWNLOAD_RESULT_ABORTED)
                stopped = true;
            else
                failed = true;

            break;
        }

        if(stopped)
            break;

        if(complete)
        {
            ++done;
            std::lock_guard<std::mutex> lock(sStatusLock);
            sStatus.titlesDone = done;
        }
    }

    closeProgressNotification();
    releaseAutoPowerDown();

    if(stopped)
    {
        debugPrintf("Stopped after %u of %u downloads", done, (uint32_t)jobs.size());
        setState(BACKGROUND_STATE_STOPPED);
        return;
    }

    if(failed)
    {
        setState(BACKGROUND_STATE_FAILED);
        snprintf(toScreen, sizeof(toScreen), "NUSspli: a background download failed after %u of %u title(s). NUSspli continues it where it stopped.",
            done, (uint32_t)jobs.size());
        showError(toScreen);
        return;
    }

    setState(BACKGROUND_STATE_FINISHED);
    snprintf(toScreen, sizeof(toScreen), "NUSspli: the background download of %u title(s) is finished. Install them with NUSspli.", done);
    showInfo(toScreen);
    debugPrintf("Finished %u downloads", done);
}

void startBackgroundDownloads()
{
    stopBackgroundDownloads();

    if(!gEnabled)
    {
        setState(BACKGROUND_STATE_DISABLED);
        return;
    }

    if(runningNUSspli())
    {
        debugPrintf("NUSspli is running, leaving the downloads to it");
        setState(BACKGROUND_STATE_IDLE);
        return;
    }

    if(!initDownloader())
        return;

    sWorker.emplace(workerMain);
}

void stopBackgroundDownloads()
{
    // The destructor of the jthread asks the thread to stop and waits for it,
    // the download it was on continues from its bytes the next time.
    sWorker.reset();
    closeProgressNotification();
    releaseAutoPowerDown();
    deinitDownloader();
}
