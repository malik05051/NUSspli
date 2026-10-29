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

#include <wut-fixups.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * Looks through the install folder of the SD card for a download that is
     * complete but not installed and offers to open the installer.
     *
     * The Aroma plugin only downloads: a download it finished while a game
     * was running is whole on the SD card and nothing on the console says
     * so, because the queue of NUSspli lives in RAM and the install half of
     * an entry is gone once the app is. This is where the user is told.
     *
     * Runs once when the boot is through, before the main menu opens.
     */
    void offerPendingInstalls();

#ifdef __cplusplus
}
#endif
