/*
 * Process Hacker Toolchain -
 *   project setup
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

#ifndef _SETUP_H
#define _SETUP_H

#include <ph.h>
#include <guisup.h>
#include <prsht.h>
#include <workqueue.h>
#include <svcsup.h>
#include <json.h>

#include <aclapi.h>
#include <io.h>
#include <netlistmgr.h>
#include <propvarutil.h>
#include <propkey.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <sddl.h>
#include <wincodec.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <uxtheme.h>

#include "resource.h"
#include "..\..\ProcessHacker\include\phappres.h"

#define SETUP_SHOWDIALOG (WM_APP + 1)
#define SETUP_SHOWINSTALL (WM_APP + 2)
#define SETUP_SHOWFINAL (WM_APP + 3)
#define SETUP_SHOWERROR (WM_APP + 4)
#define SETUP_SHOWUNINSTALL (WM_APP + 5)
#define SETUP_SHOWUNINSTALLFINAL (WM_APP + 6)
#define SETUP_SHOWUNINSTALLERROR (WM_APP + 7)
#define SETUP_SHOWUPDATE (WM_APP + 8)
#define SETUP_SHOWUPDATEFINAL (WM_APP + 9)
#define SETUP_SHOWUPDATEERROR (WM_APP + 10)

#define TaskDialogNavigatePage(WindowHandle, Config) \
    assert(HandleToUlong(NtCurrentThreadId()) == GetWindowThreadProcessId(WindowHandle, NULL)); \
    SendMessage(WindowHandle, TDM_NAVIGATE_PAGE, 0, (LPARAM)Config);

typedef enum _SETUP_COMMAND_TYPE
{
    SETUP_COMMAND_INSTALL,
    SETUP_COMMAND_UNINSTALL,
    SETUP_COMMAND_UPDATE,
} SETUP_COMMAND_TYPE;

typedef struct _PH_SETUP_CONTEXT
{
    HWND DialogHandle;
    HICON IconSmallHandle;
    HICON IconLargeHandle;
    WNDPROC TaskDialogWndProc;

    union
    {
        ULONG Flags;
        struct
        {
            ULONG SetupRemoveAppData: 1;
            ULONG SetupKphInstallRequired : 1;
            ULONG Spare : 30;
        };
    };

    SETUP_COMMAND_TYPE SetupMode;
    PPH_STRING SetupInstallPath;

    ULONG ErrorCode;
    PPH_STRING FilePath;

    PPH_STRING RelDate;
    PPH_STRING RelVersion;

    PPH_STRING BinFileDownloadUrl;
    PPH_STRING BinFileLength;
    PPH_STRING BinFileHash;
    PPH_STRING BinFileSignature;
    PPH_STRING SetupFileDownloadUrl;
    PPH_STRING SetupFileLength;
    PPH_STRING SetupFileHash;
    PPH_STRING SetupFileSignature;

    PPH_STRING WebSetupFileDownloadUrl;
    PPH_STRING WebSetupFileVersion;
    PPH_STRING WebSetupFileLength;
    PPH_STRING WebSetupFileHash;
    PPH_STRING WebSetupFileSignature;

    ULONG CurrentMajorVersion;
    ULONG CurrentMinorVersion;
    ULONG CurrentRevisionVersion;
} PH_SETUP_CONTEXT, *PPH_SETUP_CONTEXT;

VOID SetupParseCommandLine(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID ShowWelcomePageDialog(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID ShowConfigPageDialog(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID ShowDownloadPageDialog(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID ShowCompletedPageDialog(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID ShowErrorPageDialog(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID ShowInstallPageDialog(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID ShowUninstallPageDialog(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID ShowUninstallingPageDialog(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID ShowUninstallCompletedPageDialog(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID ShowUninstallErrorPageDialog(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID ShowUpdatePageDialog(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID ShowUpdateCompletedPageDialog(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID ShowUpdateErrorPageDialog(
    _In_ PPH_SETUP_CONTEXT Context
    );

// util.c

PPH_STRING SetupFindInstallDirectory(
    VOID
    );

PPH_STRING SetupFindAppdataDirectory(
    VOID
    );

VOID SetupDeleteAppdataDirectory(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID SetupStartKph(
    _In_ PPH_SETUP_CONTEXT Context,
    _In_ BOOLEAN ForceInstall
    );

BOOLEAN SetupUninstallKph(
    _In_ PPH_SETUP_CONTEXT Context
    );

NTSTATUS SetupCreateUninstallKey(
    _In_ PPH_SETUP_CONTEXT Context
    );

NTSTATUS SetupDeleteUninstallKey(
    VOID
    );

VOID SetupSetWindowsOptions(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID SetupDeleteWindowsOptions(
    _In_ PPH_SETUP_CONTEXT Context
    );

BOOLEAN SetupCreateUninstallFile(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID SetupDeleteUninstallFile(
    _In_ PPH_SETUP_CONTEXT Context
    );

BOOLEAN SetupExecuteProcessHacker(
    _In_ PPH_SETUP_CONTEXT Context
    );

VOID SetupUpgradeSettingsFile(
    VOID
    );

VOID SetupCreateImageFileExecutionOptions(
    VOID
    );

extern PH_STRINGREF UninstallKeyName;

typedef struct _SETUP_EXTRACT_FILE
{
    PSTR FileName;
    PWSTR ExtractFileName;
} SETUP_EXTRACT_FILE, *PSETUP_EXTRACT_FILE;

typedef struct _SETUP_REMOVE_FILE
{
    PWSTR FileName;
} SETUP_REMOVE_FILE, *PSETUP_REMOVE_FILE;

VOID SetupCreateLink(
    _In_ PWSTR LinkFilePath,
    _In_ PWSTR FilePath,
    _In_ PWSTR FileParentDir
    );

BOOLEAN CheckProcessHackerInstalled(
    VOID
    );

PPH_STRING GetProcessHackerInstallPath(
    VOID
    );

BOOLEAN ShutdownProcessHacker(
    VOID
    );

NTSTATUS QueryProcessesUsingVolumeOrFile(
    _In_ HANDLE VolumeOrFileHandle,
    _Out_ PFILE_PROCESS_IDS_USING_FILE_INFORMATION *Information
    );

PPH_STRING SetupCreateFullPath(
    _In_ PPH_STRING Path,
    _In_ PWSTR FileName
    );

BOOLEAN SetupBase64StringToBufferEx(
    _In_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ PVOID* OutputBuffer,
    _Out_opt_ ULONG* OutputBufferLength
    );

// download.c

#define MAKE_VERSION_ULONGLONG(major, minor, build, revision) \
    (((ULONGLONG)(major) << 48) | \
    ((ULONGLONG)(minor) << 32) | \
    ((ULONGLONG)(build) << 16) | \
    ((ULONGLONG)(revision) <<  0))

ULONG64 ParseVersionString(
    _Inout_ PPH_STRING VersionString
    );

BOOLEAN SetupQueryUpdateData(
    _Inout_ PPH_SETUP_CONTEXT Context
    );

BOOLEAN UpdateDownloadUpdateData(
    _In_ PPH_SETUP_CONTEXT Context
    );

// extract.c

BOOLEAN SetupExtractBuild(
    _In_ PPH_SETUP_CONTEXT Context
    );

#endif
