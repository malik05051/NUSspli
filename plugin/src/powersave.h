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

/*
 * The console powers itself down after a while without input, and a
 * download that runs while nobody plays is exactly the case it was made
 * for: a queue left to finish on the HOME Menu would be cut short after
 * the power down period of the console.
 *
 * So the automatic power down is held off while a download runs and put
 * back the way the user has it afterwards. Both calls can be made more
 * than once, only the first hold and the first release do anything.
 */
bool holdAutoPowerDown();
void releaseAutoPowerDown();
