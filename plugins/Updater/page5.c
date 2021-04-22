/*
 * Process Hacker Plugins -
 *   Update Checker Plugin
 *
 * Copyright (C) 2016-2019 dmex
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

#include "updater.h"
#include <shellapi.h>
#include <shlobj.h>

static TASKDIALOG_BUTTON TaskDialogButtonArray[] =
{
    { IDYES, L"Install" }
};

BOOLEAN UpdaterCheckKphInstallState(
    VOID
    )
{
    static PH_STRINGREF kph3ServiceKeyName = PH_STRINGREF_INIT(L"System\\CurrentControlSet\\Services\\KProcessHacker3");
    BOOLEAN kphInstallRequired = FALSE;
    HANDLE runKeyHandle;

    if (NT_SUCCESS(PhOpenKey(
        &runKeyHandle,
        KEY_READ,
        PH_KEY_LOCAL_MACHINE,
        &kph3ServiceKeyName,
        0
        )))
    {
        // Make sure we re-install the driver when KPH was installed as a service. 
        if (PhQueryRegistryUlong(runKeyHandle, L"Start") == SERVICE_SYSTEM_START)
        {
            kphInstallRequired = TRUE;
        }

        NtClose(runKeyHandle);
    }

    return kphInstallRequired;
}

BOOLEAN UpdaterCheckApplicationDirectory(
    VOID
    )
{
    HANDLE fileHandle;
    PPH_STRING directory;
    PPH_STRING file;

    if (UpdaterCheckKphInstallState())
        return FALSE;

    directory = PhGetApplicationDirectory();
    file = PhConcatStrings(2, PhGetStringOrEmpty(directory), L"\\processhacker.update");

    if (NT_SUCCESS(PhCreateFileWin32(
        &fileHandle,
        PhGetString(file),
        FILE_GENERIC_WRITE | DELETE,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        FILE_OPEN_IF,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_DELETE_ON_CLOSE
        )))
    {
        PhDereferenceObject(file);
        PhDereferenceObject(directory);

        NtClose(fileHandle);
        return TRUE;
    }

    PhDereferenceObject(file);
    PhDereferenceObject(directory);
    return FALSE;
}

HRESULT CALLBACK FinalTaskDialogCallbackProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam,
    _In_ LONG_PTR dwRefData
    )
{
    PPH_UPDATER_CONTEXT context = (PPH_UPDATER_CONTEXT)dwRefData;

    switch (uMsg)
    {
    case TDN_NAVIGATED:
        {
            if (!UpdaterCheckApplicationDirectory())
            {
                SendMessage(hwndDlg, TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, IDYES, TRUE);
            }
        }
        break;
    case TDN_BUTTON_CLICKED:
        {
            INT buttonId = (INT)wParam;

            if (buttonId == IDRETRY)
            {
                ShowCheckForUpdatesDialog(context);
                return S_FALSE;
            }
            else if (buttonId == IDYES)
            {
                SHELLEXECUTEINFO info = { sizeof(SHELLEXECUTEINFO) };
                PPH_STRING parameters;

                if (PhIsNullOrEmptyString(context->SetupFilePath))
                    break;

                parameters = PH_AUTO(PhGetApplicationDirectory());
                parameters = PH_AUTO(PhBufferToHexString((PUCHAR)parameters->Buffer, (ULONG)parameters->Length));
                parameters = PH_AUTO(PhConcatStrings(3, L"-update \"", PhGetStringOrEmpty(parameters), L"\""));

                info.lpFile = PhGetStringOrEmpty(context->SetupFilePath);
                info.lpParameters = PhGetString(parameters);
                info.lpVerb = UpdaterCheckApplicationDirectory() ? NULL : L"runas";
                info.nShow = SW_SHOW;
                info.hwnd = hwndDlg;
                info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOZONECHECKS;

                ProcessHacker_PrepareForEarlyShutdown(PhMainWndHandle);

                if (ShellExecuteEx(&info))
                {
                    ProcessHacker_Destroy(PhMainWndHandle);
                }
                else
                {
                    ULONG errorCode = GetLastError();

                    // Install failed, cancel the shutdown.
                    ProcessHacker_CancelEarlyShutdown(PhMainWndHandle);

                    // Show error dialog.
                    if (errorCode != ERROR_CANCELLED) // Ignore UAC decline.
                    {
                        PhShowStatus(hwndDlg, L"Unable to execute the setup.", 0, errorCode);

                        if (context->StartupCheck)
                            ShowAvailableDialog(context);
                        else
                            ShowCheckForUpdatesDialog(context);
                    }

                    return S_FALSE;
                }
            }
        }
        break;
    case TDN_HYPERLINK_CLICKED:
        {
            TaskDialogLinkClicked(context);
            return S_FALSE;
        }
        break;
    }

    return S_OK;
}

VOID ShowUpdateInstallDialog(
    _In_ PPH_UPDATER_CONTEXT Context
    )
{
    TASKDIALOGCONFIG config;

    memset(&config, 0, sizeof(TASKDIALOGCONFIG));
    config.cbSize = sizeof(TASKDIALOGCONFIG);
    config.dwFlags = TDF_USE_HICON_MAIN | TDF_ALLOW_DIALOG_CANCELLATION | TDF_CAN_BE_MINIMIZED;
    config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    config.hMainIcon = Context->IconLargeHandle;
    config.cxWidth = 200;
    config.pfCallback = FinalTaskDialogCallbackProc;
    config.lpCallbackData = (LONG_PTR)Context;
    config.pButtons = TaskDialogButtonArray;
    config.cButtons = RTL_NUMBER_OF(TaskDialogButtonArray);

    config.pszWindowTitle = L"Process Hacker - Updater";
    config.pszMainInstruction = L"Ready to install update?";
    config.pszContent = L"The update has been successfully downloaded and verified.\r\n\r\nClick Install to continue.";

    TaskDialogNavigatePage(Context->DialogHandle, &config);
}

VOID ShowLatestVersionDialog(
    _In_ PPH_UPDATER_CONTEXT Context
    )
{
    TASKDIALOGCONFIG config;
    LARGE_INTEGER time;
    SYSTEMTIME systemTime = { 0 };
    PIMAGE_DOS_HEADER imageDosHeader;
    PIMAGE_NT_HEADERS imageNtHeader;

    memset(&config, 0, sizeof(TASKDIALOGCONFIG));
    config.cbSize = sizeof(TASKDIALOGCONFIG);
    config.dwFlags = TDF_USE_HICON_MAIN | TDF_ALLOW_DIALOG_CANCELLATION | TDF_CAN_BE_MINIMIZED | TDF_ENABLE_HYPERLINKS;
    config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    config.hMainIcon = Context->IconLargeHandle;
    config.cxWidth = 200;
    config.pfCallback = FinalTaskDialogCallbackProc;
    config.lpCallbackData = (LONG_PTR)Context;
    
    // HACK
    imageDosHeader = (PIMAGE_DOS_HEADER)NtCurrentPeb()->ImageBaseAddress;
    imageNtHeader = (PIMAGE_NT_HEADERS)PTR_ADD_OFFSET(imageDosHeader, imageDosHeader->e_lfanew);
    RtlSecondsSince1970ToTime(imageNtHeader->FileHeader.TimeDateStamp, &time);
    PhLargeIntegerToLocalSystemTime(&systemTime, &time);

    config.pszWindowTitle = L"Process Hacker - Updater";
    config.pszMainInstruction = L"You're running the latest version.";
    config.pszContent = PhaFormatString(
        L"Version: v%s\r\nCompiled: %s\r\n\r\n<A HREF=\"changelog.txt\">View Changelog</A>",
        PhGetStringOrEmpty(Context->CurrentVersionString),
        PhaFormatDateTime(&systemTime)->Buffer
        )->Buffer;

    TaskDialogNavigatePage(Context->DialogHandle, &config);
}

VOID ShowNewerVersionDialog(
    _In_ PPH_UPDATER_CONTEXT Context
    )
{
    TASKDIALOGCONFIG config;

    memset(&config, 0, sizeof(TASKDIALOGCONFIG));
    config.cbSize = sizeof(TASKDIALOGCONFIG);
    config.dwFlags = TDF_USE_HICON_MAIN | TDF_ALLOW_DIALOG_CANCELLATION | TDF_CAN_BE_MINIMIZED;
    config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    config.hMainIcon = Context->IconLargeHandle;
    config.cxWidth = 200;
    config.pfCallback = FinalTaskDialogCallbackProc;
    config.lpCallbackData = (LONG_PTR)Context;

    config.pszWindowTitle = L"Process Hacker - Updater";
    config.pszMainInstruction = L"You're running a pre-release build.";
    config.pszContent = PhaFormatString(
        L"Pre-release build: v%s\r\n",
        PhGetStringOrEmpty(Context->CurrentVersionString)
        )->Buffer;

    TaskDialogNavigatePage(Context->DialogHandle, &config);
}

VOID ShowUpdateFailedDialog(
    _In_ PPH_UPDATER_CONTEXT Context,
    _In_ BOOLEAN HashFailed,
    _In_ BOOLEAN SignatureFailed
    )
{
    TASKDIALOGCONFIG config;

    memset(&config, 0, sizeof(TASKDIALOGCONFIG));
    config.cbSize = sizeof(TASKDIALOGCONFIG);
    //config.pszMainIcon = MAKEINTRESOURCE(65529);
    config.dwFlags = TDF_USE_HICON_MAIN | TDF_ALLOW_DIALOG_CANCELLATION | TDF_CAN_BE_MINIMIZED;
    config.dwCommonButtons = TDCBF_CLOSE_BUTTON | TDCBF_RETRY_BUTTON;
    config.hMainIcon = Context->IconLargeHandle;

    config.pszWindowTitle = L"Process Hacker - Updater";
    config.pszMainInstruction = L"Error downloading the update.";

    if (SignatureFailed)
    {
        config.pszContent = L"Signature check failed. Click Retry to download the update again.";
    }
    else if (HashFailed)
    {
        config.pszContent = L"Hash check failed. Click Retry to download the update again.";
    }
    else
    {
        if (Context->ErrorCode)
        {
            PPH_STRING errorMessage;
          
            if (errorMessage = PhHttpSocketGetErrorMessage(Context->ErrorCode))
            {
                config.pszContent = PhaFormatString(L"[%lu] %s", Context->ErrorCode, errorMessage->Buffer)->Buffer;
                PhDereferenceObject(errorMessage);
            }
            else
            {
                config.pszContent = L"Click Retry to download the update again.";
            }
        }
        else
        {
            config.pszContent = L"Click Retry to download the update again.";
        }
    }

    config.cxWidth = 200;
    config.pfCallback = FinalTaskDialogCallbackProc;
    config.lpCallbackData = (LONG_PTR)Context;

    TaskDialogNavigatePage(Context->DialogHandle, &config);
}
