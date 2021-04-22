/*
 * Process Hacker -
 *   LXSS support helpers
 *
 * Copyright (C) 2019 dmex
 *
 * This file is part of Process Hacker.
 *
 * Process Hacker is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Process Hacker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _PH_WSLSUP_H
#define _PH_WSLSUP_H

#ifdef __cplusplus
extern "C" {
#endif

BOOLEAN PhInitializeLxssImageVersionInfo(
    _Inout_ PPH_IMAGE_VERSION_INFO ImageVersionInfo,
    _In_ PPH_STRING FileName
    );

ULONG PhCreateProcessLxss(
    _In_ PWSTR LxssDistribution,
    _In_ PWSTR LxssCommandLine,
    _In_opt_ PWSTR LxssCurrentDirectory,
    _Out_ PPH_STRING *Result
    );

#ifdef __cplusplus
}
#endif

#endif
