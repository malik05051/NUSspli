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

#define CONFIG_CATEGORY   "config"

#define ENABLED_CONFIG_ID "enabled"
#define ENABLED_DEFAULT   true

#define NOTIFY_CONFIG_ID  "notify"
#define NOTIFY_DEFAULT    true

extern bool gEnabled;
extern bool gNotify;

// Reads the settings of the plugin out of the WUPS storage, filling in the
// defaults above when the plugin runs for the first time.
bool initConfig();
// Hands the config menu of the plugin to the Aroma plugin menu.
void initConfigMenu();
