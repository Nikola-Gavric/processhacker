/*
 * Process Hacker Network Tools -
 *   GeoIP dialogs
 *
 * Copyright (C) 2016 dmex
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

#include "nettools.h"
#include <shellapi.h>

TASKDIALOG_BUTTON RestartButtonArray[] =
{
    { IDYES, L"Restart" }
};

TASKDIALOG_BUTTON DownloadButtonArray[] =
{
    { IDOK, L"Download" }
};

HRESULT CALLBACK CheckForUpdatesDbCallbackProc(
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
        PhSetEvent(&InitializedEvent);
        break;
    case TDN_BUTTON_CLICKED:
        {
            if ((INT)wParam == IDOK)
            {
                ShowDbCheckingForUpdatesDialog(context);
                return S_FALSE;
            }
        }
        break;
    case TDN_HYPERLINK_CLICKED:
        {
            TaskDialogLinkClicked(context);
        }
        break;
    }

    return S_OK;
}

HRESULT CALLBACK CheckingForUpdatesDbCallbackProc(
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
            SendMessage(hwndDlg, TDM_SET_MARQUEE_PROGRESS_BAR, TRUE, 0);
            SendMessage(hwndDlg, TDM_SET_PROGRESS_BAR_MARQUEE, TRUE, 1);

            PhReferenceObject(context);
            PhQueueItemWorkQueue(PhGetGlobalWorkQueue(), GeoIPUpdateThread, context);
        }
        break;
    }

    return S_OK;
}

HRESULT CALLBACK RestartDbTaskDialogCallbackProc(
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
    case TDN_BUTTON_CLICKED:
        {
            if ((INT)wParam == IDYES)
            {
                ProcessHacker_PrepareForEarlyShutdown(PhMainWndHandle);
                PhShellProcessHacker(
                    PhMainWndHandle,
                    NULL,
                    SW_SHOW,
                    0,
                    PH_SHELL_APP_PROPAGATE_PARAMETERS | PH_SHELL_APP_PROPAGATE_PARAMETERS_IGNORE_VISIBILITY,
                    0,
                    NULL
                    );
                ProcessHacker_Destroy(PhMainWndHandle);
            }
        }
        break;
    }

    return S_OK;
}

HRESULT CALLBACK FinalDbTaskDialogCallbackProc(
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
            if (!PhGetOwnTokenAttributes().Elevated)
            {
                SendMessage(hwndDlg, TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, IDYES, TRUE);
            }
        }
        break;
    case TDN_BUTTON_CLICKED:
        {
            if ((INT)wParam == IDRETRY)
            {
                ShowDbCheckForUpdatesDialog(context);
                return S_FALSE;
            }
        }
        break;
    }

    return S_OK;
}

VOID ShowDbCheckForUpdatesDialog(
    _In_ PPH_UPDATER_CONTEXT Context
    )
{
    TASKDIALOGCONFIG config;

    memset(&config, 0, sizeof(TASKDIALOGCONFIG));
    config.cbSize = sizeof(TASKDIALOGCONFIG);
    config.dwFlags = TDF_USE_HICON_MAIN | TDF_ALLOW_DIALOG_CANCELLATION | TDF_CAN_BE_MINIMIZED | TDF_ENABLE_HYPERLINKS;
    config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    config.hMainIcon = Context->IconLargeHandle;
    config.cxWidth = 200;
    config.pButtons = DownloadButtonArray;
    config.cButtons = ARRAYSIZE(DownloadButtonArray);
    config.pfCallback = CheckForUpdatesDbCallbackProc;
    config.lpCallbackData = (LONG_PTR)Context;

    config.pszWindowTitle = L"Network Tools - GeoIP Updater";
    config.pszMainInstruction = L"Download the latest GeoIP database?";
    config.pszContent = L"This product includes GeoLite2 data created by MaxMind, available from <a href=\"http://www.maxmind.com\">http://www.maxmind.com</a>\r\n\r\nSelect download to continue.";

    TaskDialogNavigatePage(Context->DialogHandle, &config);
}

VOID ShowDbCheckingForUpdatesDialog(
    _In_ PPH_UPDATER_CONTEXT Context
    )
{
    TASKDIALOGCONFIG config;

    memset(&config, 0, sizeof(TASKDIALOGCONFIG));
    config.cbSize = sizeof(TASKDIALOGCONFIG);
    config.dwFlags = TDF_USE_HICON_MAIN | TDF_ALLOW_DIALOG_CANCELLATION | TDF_SHOW_MARQUEE_PROGRESS_BAR;
    config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
    config.hMainIcon = Context->IconLargeHandle;
    config.cxWidth = 200;
    config.pfCallback = CheckingForUpdatesDbCallbackProc;
    config.lpCallbackData = (LONG_PTR)Context;

    config.pszWindowTitle = L"Network Tools - GeoIP Updater";
    config.pszMainInstruction = L"Downloading";
    config.pszContent = L"Downloaded: ~ of ~ (~%%)\r\nSpeed: ~/s";

    TaskDialogNavigatePage(Context->DialogHandle, &config);
}

VOID ShowDbInstallRestartDialog(
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
    config.pfCallback = RestartDbTaskDialogCallbackProc;
    config.lpCallbackData = (LONG_PTR)Context;
    config.pButtons = RestartButtonArray;
    config.cButtons = ARRAYSIZE(RestartButtonArray);

    config.pszWindowTitle = L"Network Tools - GeoIP Updater";
    config.pszMainInstruction = L"The GeoIP database has been installed";
    config.pszContent = L"You need to restart Process Hacker for the changes to take effect...";

    TaskDialogNavigatePage(Context->DialogHandle, &config);
}

VOID ShowDbUpdateFailedDialog(
    _In_ PPH_UPDATER_CONTEXT Context
    )
{
    TASKDIALOGCONFIG config;

    memset(&config, 0, sizeof(TASKDIALOGCONFIG));
    config.cbSize = sizeof(TASKDIALOGCONFIG);
    //config.pszMainIcon = MAKEINTRESOURCE(65529);
    config.dwFlags = TDF_USE_HICON_MAIN | TDF_ALLOW_DIALOG_CANCELLATION | TDF_CAN_BE_MINIMIZED;
    config.dwCommonButtons = TDCBF_CLOSE_BUTTON | TDCBF_RETRY_BUTTON;
    config.hMainIcon = Context->IconLargeHandle;
    config.cxWidth = 200;
    config.pfCallback = FinalDbTaskDialogCallbackProc;
    config.lpCallbackData = (LONG_PTR)Context;

    config.pszWindowTitle = L"Network Tools - GeoIP Updater";
    config.pszMainInstruction = L"Error downloading GeoIP database.";
    config.pszContent = L"Click Retry to download the database again.";

    TaskDialogNavigatePage(Context->DialogHandle, &config);
}
