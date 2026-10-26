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

#include "config.h"
#include "logger.h"
#include "version.h"
#include "worker.h"

#include <notifications/notifications.h>
#include <rpxloader/rpxloader.h>

#include <wups.h>

WUPS_PLUGIN_NAME("NUSspli Background Downloads");
WUPS_PLUGIN_DESCRIPTION("Finishes unfinished NUSspli downloads while a game or the Wii U Menu is running and shows how far they are.");
WUPS_PLUGIN_VERSION(PLUGIN_VERSION);
WUPS_PLUGIN_AUTHOR("NUSspli contributors");
WUPS_PLUGIN_LICENSE("GPLv3");

WUPS_USE_WUT_DEVOPTAB();
WUPS_USE_STORAGE("nusspli_background_downloads");

INITIALIZE_PLUGIN()
{
    if(NotificationModule_InitLibrary() != NOTIFICATION_MODULE_RESULT_SUCCESS)
        debugPrintf("Can't reach the notification module, running without notifications");

    if(RPXLoader_InitLibrary() != RPX_LOADER_RESULT_SUCCESS)
        debugPrintf("Can't reach the RPX loader");

    initConfig();
    initConfigMenu();
}

ON_APPLICATION_START()
{
    startBackgroundDownloads();
}

ON_APPLICATION_ENDS()
{
    stopBackgroundDownloads();
}

DEINITIALIZE_PLUGIN()
{
    NotificationModule_DeInitLibrary();
    RPXLoader_DeInitLibrary();
}
