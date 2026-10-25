/***************************************************************************
 * This file is part of NUSspli.                                           *
 * Copyright (c) 2026 V10lator <v10lator@myway.de>                         *
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

#include <stdint.h>

#include <tmd.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * The Wii U Download Management: the system lists and runs the download
     * tasks of nn_nim, the title installer the eShop hands its downloads to.
     * registerSystemDownload() registers such a task for a title, so the
     * system downloads and installs it in the background and shows it in
     * its Download Management, NUSspli does not download anything itself.
     *
     * Experimental: nn_nim is not documented. The exports come from WUT and
     * the layout of the task config from Cemu, which emulates the call the
     * eShop makes. Which title type the eShop passes is not known, so a
     * refusal comes back as the result of the system and is shown as such.
     *
     * The system installs with the ticket it has: an update brings its own,
     * everything else needs one on the console already (bought, or left
     * behind by an earlier install).
     */

    // 0 when the task was registered, the nn::Result of the system else.
    int32_t registerSystemDownload(const TMD *tmd);

#ifdef __cplusplus
}
#endif
