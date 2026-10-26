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

#ifdef DEBUG

#include <coreinit/debug.h>

// A plugin has no screen of its own, so the debug build writes to the system
// log, which the syslog of Mocha or a coreinit log dump shows.
#define debugPrintf(fmt, ...) OSReport("[NUSspli-BGDL] " fmt "\n", ##__VA_ARGS__)

#else

// Still a function so that the arguments count as used in a release build
// and the compiler keeps checking them against the format string.
__attribute__((__format__(__printf__, 1, 2))) static inline void debugPrintf(const char *fmt, ...)
{
    (void)fmt;
}

#endif
