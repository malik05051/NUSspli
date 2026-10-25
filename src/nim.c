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
#include <coreinit/memory.h>
#pragma GCC diagnostic pop

// The C++ exports of nn_nim.rpl. WUT links them, but ships no header: the
// names carry the mangling, and a config returned by value travels through
// a hidden pointer in r3, which is what a struct return does in C as well.
extern NNResult NIMInitialize(void) __asm__("Initialize__Q2_2nn3nimFv");
extern NNResult NIMFinalize(void) __asm__("Finalize__Q2_2nn3nimFv");
extern NNResult NIMRegisterTitlePackageTask(const NimTitlePackageTaskConfig *config, const uint16_t *contents, uint32_t numContents) __asm__("RegisterTitlePackageTask__Q2_2nn3nimFRCQ3_2nn3nim22TitlePackageTaskConfigPCUsUi");

// Every line goes out in front of its call as well: the last one that
// reaches the UDP log names the call that crashed.
NNResult nimInitialize()
{
    debugPrintf("NIM: Initialize()");
    NNResult res = NIMInitialize();
    debugPrintf("NIM: Initialize() returned %#010x", res.value);
    return res;
}

// The eShop fills the config with
// nn::nim::util::MakeTitlePackageTaskConfigAutoUsingBgInstallPolicy(), but
// that call took a 5.5.5E console down in the step by step test, with a
// title type of 0 and a region of 0. Cemu only stubs it, so whatever it
// checks or reads on a console is unknown: the config is filled here the
// way Cemu fills it instead, which is what the eShop runs with in Cemu.
void nimMakeConfig(NimTitlePackageTaskConfig *config, uint64_t tid)
{
    OSBlockSet(config, 0, sizeof(NimTitlePackageTaskConfig));
    config->titleIdHigh = (uint32_t)(tid >> 32);
    config->titleIdLow = (uint32_t)tid;
    config->regionOrLanguage = 0;
    // The values of nn::Cafe::TitleType are not documented. A type of 0 for
    // an update was refused as a usage error (0xC0512E80), so the type is
    // taken from the title ID now: its category, 0x00 for a game, 0x02 for a
    // demo, 0x0C for DLC and 0x0E for an update.
    config->titleType = (uint8_t)(tid >> 32);
    // 1 is the internal memory in Cemu, the eShop reads these two to find
    // the device of the title.
    config->applicationBoxDevice1 = 1;
    config->applicationBoxDevice2 = 1;
    // Byte 0x15 is set to 1 by Cemu, its meaning is unknown.
    ((uint8_t *)config)[0x15] = 1;
    // Cemu notes 2 as "use the background install policy", which is what
    // the call of the eShop is named after (its stub leaves 0).
    config->postDownloadAction = 2;

    debugPrintf("NIM config for %016llX: type %u, devices %u/%u, action %u", tid, config->titleType, config->applicationBoxDevice1, config->applicationBoxDevice2, config->postDownloadAction);
}

NNResult nimRegister(const NimTitlePackageTaskConfig *config, const TMD *tmd, bool withContents)
{
    if(!withContents)
    {
        debugPrintf("NIM: RegisterTitlePackageTask(%016llX, no contents)", tmd->tid);
        NNResult res = NIMRegisterTitlePackageTask(config, NULL, 0);
        debugPrintf("NIM: RegisterTitlePackageTask() returned %#010x", res.value);
        return res;
    }

    // The task downloads exactly the contents it is handed, so it gets all
    // of them, the way a fresh install needs them. Aligned like every buffer
    // that may travel to the IOSU, in case the library hands it on as it is.
    uint16_t *contents = MEMAllocFromDefaultHeapEx(sizeof(uint16_t) * (tmd->num_contents == 0 ? 1 : tmd->num_contents), 0x40);
    if(contents == NULL)
        return (NNResult) { .value = -1 };

    for(uint16_t i = 0; i < tmd->num_contents; ++i)
        contents[i] = tmd->contents[i].index;

    debugPrintf("NIM: RegisterTitlePackageTask(%016llX, %u contents)", tmd->tid, tmd->num_contents);
    NNResult res = NIMRegisterTitlePackageTask(config, contents, tmd->num_contents);
    debugPrintf("NIM: RegisterTitlePackageTask() returned %#010x", res.value);

    MEMFreeToDefaultHeap(contents);
    return res;
}

void nimFinalize()
{
    debugPrintf("NIM: Finalize()");
    NIMFinalize();
}
