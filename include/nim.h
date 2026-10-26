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

#pragma GCC diagnostic ignored "-Wundef"
#include <nn/result.h>
#pragma GCC diagnostic pop

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * The Wii U Download Management: the system lists and runs the download
     * tasks of nn_nim, the title installer the eShop hands its downloads to.
     * The calls below register such a task for a title, so the system
     * downloads and installs it in the background and shows it in its
     * Download Management, NUSspli does not download anything itself.
     *
     * nn_nim is not documented. The exports come from WUT, the layout of
     * the task config from Cemu, and what the system expects in it was
     * found out on a 5.5.5E console: see nimMakeConfig() in nim.c.
     *
     * The system installs with the ticket it has: an update gets its own
     * through nimDownloadPatchTicket(), everything else needs one on the
     * console already (bought, or left behind by an earlier install). It
     * also fetches data of its own from the servers of Nintendo (the icon
     * of the title, for example), so a title they no longer have all of
     * fails even with a ticket, while NUSspli itself can still install it.
     */

    // nn::nim::TitlePackageTaskConfig, as Cemu fills it for the eShop.
    typedef struct WUT_PACKED
    {
        uint32_t titleIdHigh;
        uint32_t titleIdLow;
        uint32_t titleVersion; // Cemu calls it region or language, see nimMakeConfig()
        uint8_t titleType;
        uint8_t applicationBoxDevice1;
        WUT_UNKNOWN_BYTES(1);
        uint8_t applicationBoxDevice2;
        WUT_UNKNOWN_BYTES(6);
        uint8_t postDownloadAction;
        WUT_UNKNOWN_BYTES(1);
    } NimTitlePackageTaskConfig;
    WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x00, titleIdHigh);
    WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x08, titleVersion);
    WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x0C, titleType);
    WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x0F, applicationBoxDevice2);
    WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x16, postDownloadAction);
    WUT_CHECK_SIZE(NimTitlePackageTaskConfig, 0x18);

    // The steps of the handoff, in this order. Every one that returns an
    // NNResult stops the handoff on a failure, and nimFinalize() closes
    // what nimInitialize() opened.
    NNResult nimInitialize();
    void nimMakeConfig(NimTitlePackageTaskConfig *config, uint64_t tid, uint16_t version);
    // Fetches the ticket of an update from the servers of Nintendo, which
    // the registration of an update without one needs.
    NNResult nimDownloadPatchTicket(uint64_t tid);
    // The system picks the contents of the title itself.
    NNResult nimRegister(const NimTitlePackageTaskConfig *config, uint64_t tid);

    // The error code the system shows its users for a result (105-0583 is
    // 1050583), between nimInitialize() and nimFinalize().
    int32_t nimErrorCode(NNResult res);
    void nimFinalize();

#ifdef __cplusplus
}
#endif
