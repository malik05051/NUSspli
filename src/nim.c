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
// nn::nim::ResultError turns a result into the error code the system shows
// its users. The compiler of the system passes the nn::Result by pointer,
// which is how Cemu receives it, and the object holds two words there.
extern void NIMResultErrorConstruct(void *resultError, const NNResult *result, int32_t unknown) __asm__("Construct__Q3_2nn3nim11ResultErrorFQ2_2nn6Resulti");
extern int32_t NIMResultErrorGetErrorCode(const void *resultError) __asm__("GetErrorCode__Q3_2nn3nim11ResultErrorCFv");
extern NNResult NIMIsECommerceInfrastructureStandbyMode(bool *standby) __asm__("IsECommerceInfrastructureStandbyMode__Q2_2nn3nimFPb");
extern NNResult NIMGetECommerceInfrastructureCountry(void *country) __asm__("GetECommerceInfrastructureCountry__Q2_2nn3nimFPQ3_2nn3nim7Country");

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

// The contents are left to the system: the content list of the TMD was
// refused as a usage error (0xC0512E80) on a 5.5.5E console, with and
// without the right title type, while no list got past that check.
NNResult nimRegister(const NimTitlePackageTaskConfig *config, uint64_t tid)
{
    (void)tid; // Only read by the debug log
    debugPrintf("NIM: RegisterTitlePackageTask(%016llX, no contents)", tid);
    NNResult res = NIMRegisterTitlePackageTask(config, NULL, 0);
    debugPrintf("NIM: RegisterTitlePackageTask() returned %#010x", res.value);
    return res;
}

int32_t nimErrorCode(NNResult res)
{
    // Room for more than the two words Cemu knows of, in case the object
    // of a console is larger.
    uint32_t resultError[16] __attribute__((__aligned__(0x40)));
    OSBlockSet(resultError, 0, sizeof(resultError));
    debugPrintf("NIM: ResultError(%#010x)", res.value);
    NIMResultErrorConstruct(resultError, &res, 0);
    int32_t code = NIMResultErrorGetErrorCode(resultError);
    debugPrintf("NIM: error code %d", code);
    return code;
}

NNResult nimStandbyMode(bool *standby)
{
    // Room and alignment for a bool the system may write wider.
    uint8_t buf[0x40] __attribute__((__aligned__(0x40)));
    OSBlockSet(buf, 0, sizeof(buf));
    debugPrintf("NIM: IsECommerceInfrastructureStandbyMode()");
    NNResult res = NIMIsECommerceInfrastructureStandbyMode((bool *)buf);
    *standby = buf[0] != 0;
    return res;
}

NNResult nimCountry(uint8_t country[8])
{
    // The size of nn::nim::Country is unknown, Cemu writes one word.
    uint8_t buf[0x40] __attribute__((__aligned__(0x40)));
    OSBlockSet(buf, 0, sizeof(buf));
    debugPrintf("NIM: GetECommerceInfrastructureCountry()");
    NNResult res = NIMGetECommerceInfrastructureCountry(buf);
    OSBlockMove(country, buf, 8, false);
    return res;
}

void nimFinalize()
{
    debugPrintf("NIM: Finalize()");
    NIMFinalize();
}
