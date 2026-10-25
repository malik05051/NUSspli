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

#include <wut-fixups.h>

#include <stdint.h>

#include <nim.h>
#include <tmd.h>
#include <utils.h>

#pragma GCC diagnostic ignored "-Wundef"
#include <coreinit/memdefaultheap.h>
#include <nn/result.h>
#pragma GCC diagnostic pop

// nn::nim::TitlePackageTaskConfig, as Cemu fills it for the eShop.
typedef struct WUT_PACKED
{
    uint32_t titleIdHigh;
    uint32_t titleIdLow;
    uint32_t regionOrLanguage;
    uint8_t titleType;
    uint8_t applicationBoxDevice1;
    WUT_UNKNOWN_BYTES(1);
    uint8_t applicationBoxDevice2;
    WUT_UNKNOWN_BYTES(6);
    uint8_t postDownloadAction;
    WUT_UNKNOWN_BYTES(1);
} NimTitlePackageTaskConfig;
WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x00, titleIdHigh);
WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x08, regionOrLanguage);
WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x0C, titleType);
WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x0F, applicationBoxDevice2);
WUT_CHECK_OFFSET(NimTitlePackageTaskConfig, 0x16, postDownloadAction);
WUT_CHECK_SIZE(NimTitlePackageTaskConfig, 0x18);

// The C++ exports of nn_nim.rpl. WUT links them, but ships no header: the
// names carry the mangling, and a config returned by value travels through
// a hidden pointer in r3, which is what a struct return does in C as well.
extern NNResult NIMInitialize(void) __asm__("Initialize__Q2_2nn3nimFv");
extern NNResult NIMFinalize(void) __asm__("Finalize__Q2_2nn3nimFv");
extern NimTitlePackageTaskConfig NIMMakeTitlePackageTaskConfig(uint64_t titleId, int32_t regionOrLanguage, uint32_t titleType) __asm__("MakeTitlePackageTaskConfigAutoUsingBgInstallPolicy__Q3_2nn3nim4utilFULiQ3_2nn4Cafe9TitleType");
extern NNResult NIMRegisterTitlePackageTask(const NimTitlePackageTaskConfig *config, const uint16_t *contents, uint32_t numContents) __asm__("RegisterTitlePackageTask__Q2_2nn3nimFRCQ3_2nn3nim22TitlePackageTaskConfigPCUsUi");

// The values of nn::Cafe::TitleType are not documented, 0 is the guess for
// an ordinary title. The region is ignored by Cemu and left at 0 as well.
#define NIM_TITLE_TYPE 0

int32_t registerSystemDownload(const TMD *tmd)
{
    // One line in front of every call: the first test crashed the console,
    // and the last line that reaches the UDP log names the call that did it.
    debugPrintf("NIM: Initialize()");
    NNResult res = NIMInitialize();
    debugPrintf("NIM: Initialize() returned %#010x", res.value);
    if(NNResult_IsFailure(res))
    {
        debugPrintf("nn::nim::Initialize() failed: %#010x", res.value);
        return res.value;
    }

    // The task downloads exactly the contents it is handed, so it gets all
    // of them, the way a fresh install needs them.
    uint16_t *contents = MEMAllocFromDefaultHeap(sizeof(uint16_t) * (tmd->num_contents == 0 ? 1 : tmd->num_contents));
    if(contents == NULL)
    {
        NIMFinalize();
        return -1;
    }

    for(uint16_t i = 0; i < tmd->num_contents; ++i)
        contents[i] = tmd->contents[i].index;

    debugPrintf("NIM: MakeTitlePackageTaskConfigAutoUsingBgInstallPolicy(%016llX)", tmd->tid);
    NimTitlePackageTaskConfig config = NIMMakeTitlePackageTaskConfig(tmd->tid, 0, NIM_TITLE_TYPE);
    debugPrintf("NIM config for %016llX: type %u, devices %u/%u, action %u", tmd->tid, config.titleType, config.applicationBoxDevice1, config.applicationBoxDevice2, config.postDownloadAction);

    debugPrintf("NIM: RegisterTitlePackageTask()");
    res = NIMRegisterTitlePackageTask(&config, contents, tmd->num_contents);
    debugPrintf("nn::nim::RegisterTitlePackageTask(%016llX, %u contents): %#010x", tmd->tid, tmd->num_contents, res.value);

    MEMFreeToDefaultHeap(contents);
    debugPrintf("NIM: Finalize()");
    NIMFinalize();
    return NNResult_IsFailure(res) ? res.value : 0;
}
