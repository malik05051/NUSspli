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

#include "downloader.h"

#include "logger.h"
#include "version.h"

#include <curl/curl.h>

#include <cstdint>
#include <cstdio>
#include <sys/stat.h>

#define USERAGENT  "NUSspli-BackgroundDownloads/" PLUGIN_VERSION
#define IO_BUFSIZE (128 * 1024)

static bool downloaderInited = false;

typedef struct
{
    CURL *curl;
    FILE *fp;
    uint64_t have; // What was on the SD card when this transfer started
    uint64_t written; // What this transfer added to it
    const DownloadProgress *progress;
    bool resumed;
    bool checked;
    bool aborted;
    bool resumeIgnored;
} TransferContext;

// Tells the caller how far the file is and takes its answer: false means the
// download has to stop, which both curl callbacks turn into an error.
static bool reportProgress(TransferContext *ctx)
{
    if(ctx->progress == nullptr || ctx->progress->onProgress == nullptr)
        return true;

    if(ctx->progress->onProgress(ctx->progress->ctx, ctx->have + ctx->written))
        return true;

    ctx->aborted = true;
    return false;
}

static size_t writeCallback(char *buf, size_t size, size_t nmemb, void *userp)
{
    TransferContext *ctx = static_cast<TransferContext *>(userp);
    size_t bytes = size * nmemb;
    if(bytes == 0)
        return 0;

    if(!ctx->checked)
    {
        ctx->checked = true;
        long code = 0;
        if(ctx->resumed && curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &code) == CURLE_OK && code == 200)
        {
            // The server ignored the resume and answers with the whole file
            // instead of the rest of it, so appending would corrupt the file.
            // Stop here and let the caller download it from the start.
            debugPrintf("Resume was ignored, starting over");
            ctx->resumeIgnored = true;
            return 0;
        }
    }

    if(fwrite(buf, 1, bytes, ctx->fp) != bytes)
    {
        debugPrintf("Can't write to the SD card");
        return 0;
    }

    ctx->written += bytes;
    return reportProgress(ctx) ? bytes : 0;
}

static int progressCallback(void *userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;

    // Only there to notice a stop while no data arrives, the bytes are
    // counted in writeCallback().
    return reportProgress(static_cast<TransferContext *>(userp)) ? 0 : 1;
}

bool initDownloader()
{
    if(downloaderInited)
        return true;

    if(curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
    {
        debugPrintf("Can't init curl");
        return false;
    }

    downloaderInited = true;
    return true;
}

void deinitDownloader()
{
    if(!downloaderInited)
        return;

    curl_global_cleanup();
    downloaderInited = false;
}

/*
 * One transfer: appends to the file when have is not 0, writes it from the
 * start when it is 0. Sets resumeIgnored when the server does not support
 * continuing, in which case nothing was written and the caller retries from
 * the start.
 */
static DOWNLOAD_RESULT transferFile(const char *url, const char *path, uint64_t have, uint64_t expectedSize, const DownloadProgress *progress, bool *resumeIgnored)
{
    FILE *fp = fopen(path, have != 0 ? "ab" : "wb");
    if(fp == nullptr)
    {
        debugPrintf("Can't open \"%s\"", path);
        return DOWNLOAD_RESULT_ERROR;
    }

    setvbuf(fp, nullptr, _IOFBF, IO_BUFSIZE);

    CURL *curl = curl_easy_init();
    if(curl == nullptr)
    {
        debugPrintf("Can't create a curl handle");
        fclose(fp);
        return DOWNLOAD_RESULT_ERROR;
    }

    TransferContext ctx = {
        .curl = curl,
        .fp = fp,
        .have = have,
        .written = 0,
        .progress = progress,
        .resumed = have != 0,
        .checked = false,
        .aborted = false,
        .resumeIgnored = false,
    };

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USERAGENT);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, (long)IO_BUFSIZE);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    // The console sleeps, loses the access point or the game hogs the
    // network: give up on a stalled transfer instead of hanging on it, the
    // next run continues the file.
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    if(have != 0)
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)have);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    bool closed = fclose(fp) == 0;

    *resumeIgnored = ctx.resumeIgnored;
    if(ctx.resumeIgnored)
        return DOWNLOAD_RESULT_ERROR;

    if(ctx.aborted)
        return DOWNLOAD_RESULT_ABORTED;

    if(res != CURLE_OK)
    {
        debugPrintf("Download of \"%s\" failed: %s", url, curl_easy_strerror(res));
        return DOWNLOAD_RESULT_ERROR;
    }

    if(!closed)
    {
        debugPrintf("Can't close \"%s\"", path);
        return DOWNLOAD_RESULT_ERROR;
    }

    if(expectedSize != 0 && ctx.have + ctx.written != expectedSize)
    {
        debugPrintf("\"%s\" ended up with %llu of %llu bytes", path, (unsigned long long)(ctx.have + ctx.written), (unsigned long long)expectedSize);
        return DOWNLOAD_RESULT_ERROR;
    }

    return DOWNLOAD_RESULT_OK;
}

DOWNLOAD_RESULT downloadFile(const char *url, const char *path, uint64_t expectedSize, const DownloadProgress *progress)
{
    if(!downloaderInited)
        return DOWNLOAD_RESULT_ERROR;

    struct stat st;
    uint64_t have = stat(path, &st) == 0 && st.st_size > 0 ? (uint64_t)st.st_size : 0;
    if(expectedSize != 0 && have != 0)
    {
        if(have == expectedSize)
            return DOWNLOAD_RESULT_OK;

        if(have > expectedSize)
        {
            debugPrintf("\"%s\" is bigger than its %llu bytes, downloading it again", path, (unsigned long long)expectedSize);
            have = 0;
        }
    }

    bool resumeIgnored = false;
    DOWNLOAD_RESULT ret = transferFile(url, path, have, expectedSize, progress, &resumeIgnored);
    if(resumeIgnored)
        ret = transferFile(url, path, 0, expectedSize, progress, &resumeIgnored);

    return ret;
}
