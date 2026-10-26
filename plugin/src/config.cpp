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
#include "worker.h"

#include <wups/config/WUPSConfigCategory.h>
#include <wups/config/WUPSConfigItemBoolean.h>
#include <wups/config/WUPSConfigItemStub.h>
#include <wups/config_api.h>
#include <wups/storage.h>

#include <exception>
#include <string>
#include <string_view>
#include <utility>

bool gEnabled = ENABLED_DEFAULT;
bool gNotify = NOTIFY_DEFAULT;

bool initConfig()
{
    WUPSStorageError err;
    auto sub = WUPSStorageAPI::GetOrCreateSubItem(CONFIG_CATEGORY, err);
    if(!sub)
    {
        debugPrintf("Can't open the \"%s\" storage: %s", CONFIG_CATEGORY, WUPSStorageAPI::GetStatusStr(err).data());
        return false;
    }

    bool ret = true;
    if(sub->GetOrStoreDefault(ENABLED_CONFIG_ID, gEnabled, ENABLED_DEFAULT) != WUPS_STORAGE_ERROR_SUCCESS)
    {
        debugPrintf("Can't read \"%s\"", ENABLED_CONFIG_ID);
        ret = false;
    }

    if(sub->GetOrStoreDefault(NOTIFY_CONFIG_ID, gNotify, NOTIFY_DEFAULT) != WUPS_STORAGE_ERROR_SUCCESS)
    {
        debugPrintf("Can't read \"%s\"", NOTIFY_CONFIG_ID);
        ret = false;
    }

    if(WUPSStorageAPI::SaveStorage() != WUPS_STORAGE_ERROR_SUCCESS)
    {
        debugPrintf("Can't save the storage");
        ret = false;
    }

    return ret;
}

static void boolItemChanged(ConfigItemBoolean *item, bool newValue)
{
    WUPSStorageError err;
    auto sub = WUPSStorageAPI::GetSubItem(CONFIG_CATEGORY, err);
    if(!sub)
    {
        debugPrintf("Can't open the \"%s\" storage: %s", CONFIG_CATEGORY, WUPSStorageAPI::GetStatusStr(err).data());
        return;
    }

    if(std::string_view(ENABLED_CONFIG_ID) == item->identifier)
    {
        gEnabled = newValue;
        err = sub->Store(ENABLED_CONFIG_ID, newValue);
    }
    else if(std::string_view(NOTIFY_CONFIG_ID) == item->identifier)
    {
        gNotify = newValue;
        err = sub->Store(NOTIFY_CONFIG_ID, newValue);
    }
    else
        return;

    if(err != WUPS_STORAGE_ERROR_SUCCESS)
        debugPrintf("Can't store \"%s\" as %d", item->identifier, (int)newValue);
}

static WUPSConfigAPICallbackStatus configMenuOpened(WUPSConfigCategoryHandle rootHandle)
{
    try
    {
        WUPSConfigCategory root = WUPSConfigCategory(rootHandle);

        root.add(WUPSConfigItemBoolean::Create(ENABLED_CONFIG_ID,
            "Finish NUSspli downloads in the background", ENABLED_DEFAULT, gEnabled, &boolItemChanged));

        root.add(WUPSConfigItemBoolean::Create(NOTIFY_CONFIG_ID,
            "Show notifications", NOTIFY_DEFAULT, gNotify, &boolItemChanged));

        // The menu is built every time it opens, so a stub item is enough to
        // show where the download stands: whoever opens the menu to look gets
        // the state of this moment.
        auto status = WUPSConfigCategory::Create("Status");
        status.add(WUPSConfigItemStub::Create(getBackgroundStatusLine()));
        root.add(std::move(status));
    }
    catch(std::exception &e)
    {
        debugPrintf("Can't build the config menu: %s", e.what());
        return WUPSCONFIG_API_CALLBACK_RESULT_ERROR;
    }

    return WUPSCONFIG_API_CALLBACK_RESULT_SUCCESS;
}

static void configMenuClosed()
{
    if(WUPSStorageAPI::SaveStorage() != WUPS_STORAGE_ERROR_SUCCESS)
        debugPrintf("Can't save the storage");
}

void initConfigMenu()
{
    WUPSConfigAPIOptionsV1 options = { .name = "NUSspli Background Downloads" };
    if(WUPSConfigAPI_Init(options, configMenuOpened, configMenuClosed) != WUPSCONFIG_API_RESULT_SUCCESS)
        debugPrintf("Can't init the config API");
}
