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
#include <string>

// What the worker thread is doing right now. The config menu shows it as
// text, the notification shows it as a line on screen, and both only ever
// read a copy taken under the lock of the worker.
typedef enum
{
    BACKGROUND_STATE_IDLE, // Nothing to do, no interrupted download found
    BACKGROUND_STATE_RUNNING, // Downloading
    BACKGROUND_STATE_FINISHED, // Everything that was found is downloaded
    BACKGROUND_STATE_FAILED, // Gave up, NUSspli can resume the rest
    BACKGROUND_STATE_STOPPED, // The title closed while we were downloading
    BACKGROUND_STATE_DISABLED, // Turned off in the config menu of the plugin
} BACKGROUND_STATE;

typedef struct
{
    BACKGROUND_STATE state;
    std::string title; // The download the worker is on, empty when idle
    uint32_t titles; // How many interrupted downloads were found
    uint32_t titlesDone; // How many of them are done
    uint64_t bytes; // Bytes to download in this run
    uint64_t bytesDone; // Bytes of them that are in
} BackgroundStatus;

// A snapshot, safe to read from any thread.
BackgroundStatus getBackgroundStatus();
// The snapshot as the one line that the notification and the config menu show.
std::string getBackgroundStatusLine();

// Looks for interrupted NUSspli downloads on the SD card and finishes them on
// a thread of its own. Returns at once, does nothing when the plugin is
// turned off or when the running title is NUSspli itself.
void startBackgroundDownloads();
// Asks the worker to stop and waits for it. The downloaded bytes stay on the
// SD card, so the next start - or NUSspli - resumes where this run stopped.
void stopBackgroundDownloads();
