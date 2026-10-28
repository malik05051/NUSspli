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

#include "powersave.h"

#include "logger.h"

#include <cstdint>

#include <coreinit/im.h>

// What the console has in its settings, to be put back after the download.
static uint32_t sSetting = 0;
static bool sHeld = false;

bool holdAutoPowerDown()
{
    if(sHeld)
        return true;

    // The setting of the user, not the value in force: that is the one to
    // put back, and a console whose automatic power down is turned off
    // anyway has nothing that needs holding.
    if(IM_GetNvParameterWithoutHandleAndItb(IM_PARAMETER_APD_ENABLED, &sSetting) != 0)
    {
        debugPrintf("Can't read the automatic power down setting");
        return false;
    }

    if(sSetting == 0)
    {
        debugPrintf("The automatic power down is turned off anyway");
        return true;
    }

    /*
     * The runtime parameter, not the one in the NV storage of the console:
     * it is the value in force until the console reboots and it leaves the
     * setting of the user untouched. So the worst a plugin that dies
     * without putting it back can do is let this one boot of the console
     * run without an automatic power down, instead of turning the setting
     * off for good behind the back of the user.
     */
    if(IM_SetRuntimeParameter(IM_PARAMETER_APD_ENABLED, 0) != 0)
    {
        debugPrintf("Can't hold the automatic power down off");
        return false;
    }

    debugPrintf("The automatic power down is held off");
    sHeld = true;
    return true;
}

void releaseAutoPowerDown()
{
    if(!sHeld)
        return;

    // Held means the setting said it is on, so this puts that back.
    if(IM_SetRuntimeParameter(IM_PARAMETER_APD_ENABLED, sSetting) != 0)
        debugPrintf("Can't put the automatic power down back");
    else
        debugPrintf("The automatic power down is back");

    sHeld = false;
}
