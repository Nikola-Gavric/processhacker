/*
 * Process Hacker ToolStatus -
 *   main toolbar
 *
 * Copyright (C) 2011-2019 dmex
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

#include "toolstatus.h"
#include "commonutil.h"

SIZE ToolBarImageSize = { 16, 16 };
HIMAGELIST ToolBarImageList = NULL;
HFONT ToolStatusWindowFont = NULL;
TBBUTTON ToolbarButtons[MAX_TOOLBAR_ITEMS] =
{
    // Default toolbar buttons (displayed)
    { I_IMAGECALLBACK, PHAPP_ID_VIEW_REFRESH, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, { 0 }, 0, 0 },
    { I_IMAGECALLBACK, PHAPP_ID_HACKER_OPTIONS, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, { 0 }, 0, 0 },
    { 0, 0, 0, BTNS_SEP, { 0 }, 0, 0 },
    { I_IMAGECALLBACK, PHAPP_ID_HACKER_FINDHANDLESORDLLS, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, { 0 }, 0, 0 },
    { I_IMAGECALLBACK, PHAPP_ID_VIEW_SYSTEMINFORMATION, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, { 0 }, 0, 0 },
    { 0, 0, 0, BTNS_SEP, { 0 }, 0, 0 },
    { I_IMAGECALLBACK, TIDC_FINDWINDOW, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, { 0 }, 0, 0 },
    { I_IMAGECALLBACK, TIDC_FINDWINDOWTHREAD, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, { 0 }, 0, 0 },
    { I_IMAGECALLBACK, TIDC_FINDWINDOWKILL, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, { 0 }, 0, 0 },
    // Available toolbar buttons (hidden)
    { I_IMAGECALLBACK, PHAPP_ID_VIEW_ALWAYSONTOP, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT, { 0 }, 0, 0 },
    { I_IMAGECALLBACK, TIDC_POWERMENUDROPDOWN, TBSTATE_ENABLED, BTNS_WHOLEDROPDOWN | BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT,{ 0 }, 0, 0 },
    { I_IMAGECALLBACK, PHAPP_ID_HACKER_SHOWDETAILSFORALLPROCESSES, TBSTATE_ENABLED, BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT,{ 0 }, 0, 0 },
};

VOID RebarBandInsert(
    _In_ UINT BandID,
    _In_ HWND HwndChild,
    _In_ UINT cxMinChild,
    _In_ UINT cyMinChild
    )
{
    UINT index;
    REBARBANDINFO rebarBandInfo =
    {
        sizeof(REBARBANDINFO),
        RBBIM_STYLE | RBBIM_ID | RBBIM_CHILD | RBBIM_CHILDSIZE,
        RBBS_USECHEVRON | RBBS_VARIABLEHEIGHT // RBBS_NOGRIPPER | RBBS_HIDETITLE | RBBS_TOPALIGN
    };

    rebarBandInfo.wID = BandID;
    rebarBandInfo.hwndChild = HwndChild;
    rebarBandInfo.cxMinChild = cxMinChild;
    rebarBandInfo.cyMinChild = cyMinChild;

    if (ToolStatusConfig.ToolBarLocked)
    {
        rebarBandInfo.fStyle |= RBBS_NOGRIPPER;
    }

    if ((index = (UINT)SendMessage(RebarHandle, RB_IDTOINDEX, REBAR_BAND_ID_SEARCHBOX, 0)) != UINT_MAX)
    {
        SendMessage(RebarHandle, RB_INSERTBAND, (WPARAM)index, (LPARAM)&rebarBandInfo);
    }
    else
    {
        SendMessage(RebarHandle, RB_INSERTBAND, (WPARAM)-1, (LPARAM)&rebarBandInfo);
    }
}

VOID RebarBandRemove(
    _In_ UINT BandID
    )
{
    UINT index = (UINT)SendMessage(RebarHandle, RB_IDTOINDEX, (WPARAM)BandID, 0);

    if (index == UINT_MAX)
        return;

    SendMessage(RebarHandle, RB_DELETEBAND, (WPARAM)index, 0);
}

BOOLEAN RebarBandExists(
    _In_ UINT BandID
    )
{
    UINT index = (UINT)SendMessage(RebarHandle, RB_IDTOINDEX, (WPARAM)BandID, 0);

    if (index != UINT_MAX)
        return TRUE;

    return FALSE;
}

VOID RebarLoadSettings(
    VOID
    )
{
    if (ToolStatusConfig.ToolBarEnabled && !ToolBarImageList)
    {
        ToolBarImageSize.cx = GetSystemMetrics(SM_CXSMICON);
        ToolBarImageSize.cy = GetSystemMetrics(SM_CYSMICON);
        ToolBarImageList = ImageList_Create(ToolBarImageSize.cx, ToolBarImageSize.cy, ILC_COLOR32, 0, 0);

        HFONT newFont;

        if (newFont = (HFONT)SendMessage(PhMainWndHandle, WM_PH_GET_FONT, 0, 0))
        {
            if (ToolStatusWindowFont) DeleteFont(ToolStatusWindowFont);
            ToolStatusWindowFont = newFont;
        }
    }

    if (ToolStatusConfig.ToolBarEnabled && !RebarHandle)
    {
        RebarHandle = CreateWindowEx(
            WS_EX_TOOLWINDOW,
            REBARCLASSNAME,
            NULL,
            WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CCS_NODIVIDER | CCS_TOP | RBS_VARHEIGHT | RBS_AUTOSIZE, // CCS_NOPARENTALIGN
            0, 0, 0, 0,
            PhMainWndHandle,
            NULL,
            NULL,
            NULL
            );

        ToolBarHandle = CreateWindowEx(
            0,
            TOOLBARCLASSNAME,
            NULL,
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CCS_NOPARENTALIGN | CCS_NODIVIDER | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TRANSPARENT | TBSTYLE_TOOLTIPS | TBSTYLE_AUTOSIZE,
            0, 0, 0, 0,
            RebarHandle,
            NULL,
            NULL,
            NULL
            );

        // Set the rebar info with no imagelist.
        SendMessage(RebarHandle, RB_SETBARINFO, 0, (LPARAM)&(REBARINFO){ sizeof(REBARINFO) });
        // Set the toolbar struct size.
        SendMessage(ToolBarHandle, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
        // Set the toolbar extended toolbar styles.
        SendMessage(ToolBarHandle, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DOUBLEBUFFER | TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_HIDECLIPPEDBUTTONS);

        // Add the buttons to the toolbar.
        ToolbarLoadButtonSettings();
        // Configure the toolbar imagelist.
        SendMessage(ToolBarHandle, TB_SETIMAGELIST, 0, (LPARAM)ToolBarImageList);
        // Configure the toolbar font.
        SetWindowFont(ToolBarHandle, ToolStatusWindowFont, FALSE);
        // Resize the toolbar.
        SendMessage(ToolBarHandle, TB_AUTOSIZE, 0, 0);

        // Inset the toolbar into the rebar control.
        ULONG toolbarButtonSize = (ULONG)SendMessage(ToolBarHandle, TB_GETBUTTONSIZE, 0, 0);
        RebarBandInsert(REBAR_BAND_ID_TOOLBAR, ToolBarHandle, LOWORD(toolbarButtonSize), HIWORD(toolbarButtonSize));
    }

    if (ToolStatusConfig.SearchBoxEnabled && !SearchboxHandle)
    {
        SearchboxText = PhReferenceEmptyString();
        ProcessTreeFilterEntry = PhAddTreeNewFilter(PhGetFilterSupportProcessTreeList(), ProcessTreeFilterCallback, NULL);
        ServiceTreeFilterEntry = PhAddTreeNewFilter(PhGetFilterSupportServiceTreeList(), ServiceTreeFilterCallback, NULL);
        NetworkTreeFilterEntry = PhAddTreeNewFilter(PhGetFilterSupportNetworkTreeList(), NetworkTreeFilterCallback, NULL);

        if (SearchboxHandle = CreateWindowEx(
            WS_EX_CLIENTEDGE,
            WC_EDIT,
            NULL,
            WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | ES_LEFT | ES_AUTOHSCROLL | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            RebarHandle,
            NULL,
            NULL,
            NULL
            ))
        {
            PhCreateSearchControl(RebarHandle, SearchboxHandle, L"Search Processes (Ctrl+K)");
        }
    }

    if (ToolStatusConfig.StatusBarEnabled && !StatusBarHandle)
    {
        StatusBarHandle = CreateWindowEx(
            0,
            STATUSCLASSNAME,
            NULL,
            WS_CHILD | WS_VISIBLE | CCS_BOTTOM | SBARS_SIZEGRIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            PhMainWndHandle,
            NULL,
            NULL,
            NULL
            );

        if (StatusBarHandle && PhGetIntegerSetting(L"EnableThemeSupport"))
        {
            PhInitializeWindowThemeStatusBar(StatusBarHandle);
        }
    }

    // Hide or show controls (Note: don't unload or remove at runtime).
    if (ToolStatusConfig.ToolBarEnabled)
    {
        if (RebarHandle && !IsWindowVisible(RebarHandle))
            ShowWindow(RebarHandle, SW_SHOW);
    }
    else
    {
        if (RebarHandle && IsWindowVisible(RebarHandle))
            ShowWindow(RebarHandle, SW_HIDE);
    }

    if (ToolStatusConfig.SearchBoxEnabled && RebarHandle && SearchboxHandle)
    {
        UINT height = (UINT)SendMessage(RebarHandle, RB_GETROWHEIGHT, 0, 0);

        // Add the Searchbox band into the rebar control.
        if (!RebarBandExists(REBAR_BAND_ID_SEARCHBOX))
            RebarBandInsert(REBAR_BAND_ID_SEARCHBOX, SearchboxHandle, PH_SCALE_DPI(180), height);

        if (!IsWindowVisible(SearchboxHandle))
            ShowWindow(SearchboxHandle, SW_SHOW);

        if (SearchBoxDisplayMode == SEARCHBOX_DISPLAY_MODE_HIDEINACTIVE)
        {
            if (RebarBandExists(REBAR_BAND_ID_SEARCHBOX))
                RebarBandRemove(REBAR_BAND_ID_SEARCHBOX);
        }
    }
    else
    {
        // Remove the Searchbox band from the rebar control.
        if (RebarBandExists(REBAR_BAND_ID_SEARCHBOX))
            RebarBandRemove(REBAR_BAND_ID_SEARCHBOX);

        if (SearchboxHandle)
        {
            // Clear search text and reset search filters.
            SetFocus(SearchboxHandle);
            Static_SetText(SearchboxHandle, L"");

            if (IsWindowVisible(SearchboxHandle))
                ShowWindow(SearchboxHandle, SW_HIDE);
        }
    }

    if (ToolStatusConfig.StatusBarEnabled)
    {
        if (StatusBarHandle && !IsWindowVisible(StatusBarHandle))
            ShowWindow(StatusBarHandle, SW_SHOW);
    }
    else
    {
        if (StatusBarHandle && IsWindowVisible(StatusBarHandle))
            ShowWindow(StatusBarHandle, SW_HIDE);
    }
}

VOID ToolbarLoadSettings(
    VOID
    )
{
    RebarLoadSettings();

    if (ToolStatusConfig.ToolBarEnabled && ToolBarHandle)
    {
        INT index = 0;
        INT buttonCount = 0;

        buttonCount = (INT)SendMessage(ToolBarHandle, TB_BUTTONCOUNT, 0, 0);

        for (index = 0; index < buttonCount; index++)
        {
            TBBUTTONINFO buttonInfo =
            {
                sizeof(TBBUTTONINFO),
                TBIF_BYINDEX | TBIF_STYLE | TBIF_COMMAND | TBIF_STATE
            };

            // Get settings for first button
            if (SendMessage(ToolBarHandle, TB_GETBUTTONINFO, index, (LPARAM)&buttonInfo) == -1)
                break;

            // Skip separator buttons
            if (buttonInfo.fsStyle == BTNS_SEP)
                continue;

            // Add the button text
            buttonInfo.dwMask |= TBIF_TEXT;
            buttonInfo.pszText = ToolbarGetText(buttonInfo.idCommand);

            switch (DisplayStyle)
            {
            case TOOLBAR_DISPLAY_STYLE_IMAGEONLY:
                buttonInfo.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
                break;
            case TOOLBAR_DISPLAY_STYLE_SELECTIVETEXT:
                {
                    switch (buttonInfo.idCommand)
                    {
                    case PHAPP_ID_VIEW_REFRESH:
                    case PHAPP_ID_HACKER_OPTIONS:
                    case PHAPP_ID_HACKER_FINDHANDLESORDLLS:
                    case PHAPP_ID_VIEW_SYSTEMINFORMATION:
                        buttonInfo.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
                        break;
                    default:
                        buttonInfo.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
                        break;
                    }
                }
                break;
            case TOOLBAR_DISPLAY_STYLE_ALLTEXT:
                buttonInfo.fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
                break;
            }

            switch (buttonInfo.idCommand)
            {
            case PHAPP_ID_HACKER_SHOWDETAILSFORALLPROCESSES:
                {
                    if (PhGetOwnTokenAttributes().Elevated)
                    {
                        buttonInfo.fsState &= ~TBSTATE_ENABLED;
                    }
                }
                break;
            case PHAPP_ID_VIEW_ALWAYSONTOP:
                {
                    // Set the pressed state
                    if (PhGetIntegerSetting(L"MainWindowAlwaysOnTop"))
                    {
                        buttonInfo.fsState |= TBSTATE_PRESSED;
                    }
                }
                break;
            case TIDC_POWERMENUDROPDOWN:
                {
                    buttonInfo.fsStyle |= BTNS_WHOLEDROPDOWN;
                }
                break;
            }

            // Set updated button info
            SendMessage(ToolBarHandle, TB_SETBUTTONINFO, index, (LPARAM)&buttonInfo);
        }

        // Resize the toolbar
        SendMessage(ToolBarHandle, TB_AUTOSIZE, 0, 0);
    }

    if (ToolStatusConfig.ToolBarEnabled && RebarHandle && ToolBarHandle)
    {
        UINT index;
        REBARBANDINFO rebarBandInfo =
        {
            sizeof(REBARBANDINFO),
            RBBIM_IDEALSIZE
        };

        if ((index = (UINT)SendMessage(RebarHandle, RB_IDTOINDEX, REBAR_BAND_ID_TOOLBAR, 0)) != UINT_MAX)
        {
            // Get settings for Rebar band.
            if (SendMessage(RebarHandle, RB_GETBANDINFO, index, (LPARAM)&rebarBandInfo))
            {
                SIZE idealWidth = { 0, 0 };

                // Reset the cxIdeal for the Chevron
                if (SendMessage(ToolBarHandle, TB_GETIDEALSIZE, FALSE, (LPARAM)&idealWidth))
                {
                    rebarBandInfo.cxIdeal = (UINT)idealWidth.cx;

                    SendMessage(RebarHandle, RB_SETBANDINFO, index, (LPARAM)&rebarBandInfo);
                }
            }
        }
    }

    // Invoke the LayoutPaddingCallback.
    SendMessage(PhMainWndHandle, WM_SIZE, 0, 0);
}

VOID ToolbarResetSettings(
    VOID
    )
{
    // Remove all buttons.
    INT buttonCount = (INT)SendMessage(ToolBarHandle, TB_BUTTONCOUNT, 0, 0);

    while (buttonCount--)
        SendMessage(ToolBarHandle, TB_DELETEBUTTON, (WPARAM)buttonCount, 0);

    // Add the default buttons.
    SendMessage(ToolBarHandle, TB_ADDBUTTONS, MAX_DEFAULT_TOOLBAR_ITEMS, (LPARAM)ToolbarButtons);
}

PWSTR ToolbarGetText(
    _In_ INT CommandID
    )
{
    switch (CommandID)
    {
    case PHAPP_ID_VIEW_REFRESH:
        return L"Refresh";
    case PHAPP_ID_HACKER_OPTIONS:
        return L"Options";
    case PHAPP_ID_HACKER_FINDHANDLESORDLLS:
        return L"Find handles or DLLs";
    case PHAPP_ID_VIEW_SYSTEMINFORMATION:
        return L"System information";
    case TIDC_FINDWINDOW:
        return L"Find window";
    case TIDC_FINDWINDOWTHREAD:
        return L"Find window and thread";
    case TIDC_FINDWINDOWKILL:
        return L"Find window and kill";
    case PHAPP_ID_VIEW_ALWAYSONTOP:
        return L"Always on top";
    case TIDC_POWERMENUDROPDOWN:
        return L"Computer";
    case PHAPP_ID_HACKER_SHOWDETAILSFORALLPROCESSES:
        return L"Show details for all processes";
    }

    return L"ERROR";
}

HBITMAP ToolbarLoadImageFromIcon(
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ PWSTR Name
    )
{
    HICON icon = PhLoadIcon(PluginInstance->DllBase, Name, 0, Width, Height);
    HBITMAP bitmap = PhIconToBitmap(icon, Width, Height);
    DestroyIcon(icon);
    return bitmap;
}

HBITMAP ToolbarGetImage(
    _In_ INT CommandID
    )
{
    switch (CommandID)
    {
    case PHAPP_ID_VIEW_REFRESH:
        {
            HBITMAP toolbarBitmap = NULL;

            if (ToolStatusConfig.ModernIcons)
            {
                toolbarBitmap = PhLoadPngImageFromResource(PluginInstance->DllBase, ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDB_ARROW_REFRESH_MODERN), FALSE);
            }
            else
            {
                toolbarBitmap = ToolbarLoadImageFromIcon(ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDI_ARROW_REFRESH));
            }

            return toolbarBitmap;
        }
        break;
    case PHAPP_ID_HACKER_OPTIONS:
        {
            HBITMAP toolbarBitmap = NULL;

            if (ToolStatusConfig.ModernIcons)
            {
                toolbarBitmap = PhLoadPngImageFromResource(PluginInstance->DllBase, ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDB_COG_EDIT_MODERN), FALSE);
            }
            else
            {
                toolbarBitmap = ToolbarLoadImageFromIcon(ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDI_COG_EDIT));
            }

            return toolbarBitmap;
        }
        break;
    case PHAPP_ID_HACKER_FINDHANDLESORDLLS:
        {
            HBITMAP toolbarBitmap = NULL;

            if (ToolStatusConfig.ModernIcons)
            {
                toolbarBitmap = PhLoadPngImageFromResource(PluginInstance->DllBase, ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDB_FIND_MODERN), FALSE);
            }
            else
            {
                toolbarBitmap = ToolbarLoadImageFromIcon(ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDI_FIND));
            }

            return toolbarBitmap;
        }
        break;
    case PHAPP_ID_VIEW_SYSTEMINFORMATION:
        {
            HBITMAP toolbarBitmap = NULL;

            if (ToolStatusConfig.ModernIcons)
            {
                toolbarBitmap = PhLoadPngImageFromResource(PluginInstance->DllBase, ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDB_CHART_LINE_MODERN), FALSE);
            }
            else
            {
                toolbarBitmap = ToolbarLoadImageFromIcon(ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDI_CHART_LINE));
            }

            return toolbarBitmap;
        }
        break;
    case TIDC_FINDWINDOW:
        {
            HBITMAP toolbarBitmap = NULL;

            if (ToolStatusConfig.ModernIcons)
            {
                toolbarBitmap = PhLoadPngImageFromResource(PluginInstance->DllBase, ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDB_APPLICATION_MODERN), FALSE);
            }
            else
            {
                toolbarBitmap = ToolbarLoadImageFromIcon(ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDI_TBAPPLICATION));
            }

            return toolbarBitmap;
        }
        break;
    case TIDC_FINDWINDOWTHREAD:
        {
            HBITMAP toolbarBitmap = NULL;

            if (ToolStatusConfig.ModernIcons)
            {
                toolbarBitmap = PhLoadPngImageFromResource(PluginInstance->DllBase, ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDB_APPLICATION_GO_MODERN), FALSE);
            }
            else
            {
                toolbarBitmap = ToolbarLoadImageFromIcon(ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDI_APPLICATION_GO));
            }

            return toolbarBitmap;
        }
        break;
    case TIDC_FINDWINDOWKILL:
        {
            HBITMAP toolbarBitmap = NULL;

            if (ToolStatusConfig.ModernIcons)
            {
                toolbarBitmap = PhLoadPngImageFromResource(PluginInstance->DllBase, ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDB_CROSS_MODERN), FALSE);
            }
            else
            {
                toolbarBitmap = ToolbarLoadImageFromIcon(ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDI_CROSS));
            }

            return toolbarBitmap;
        }
        break;
    case PHAPP_ID_VIEW_ALWAYSONTOP:
        {
            HBITMAP toolbarBitmap = NULL;

            if (ToolStatusConfig.ModernIcons)
            {
                toolbarBitmap = PhLoadPngImageFromResource(PluginInstance->DllBase, ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDB_APPLICATION_GET_MODERN), FALSE);
            }
            else
            {
                toolbarBitmap = ToolbarLoadImageFromIcon(ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDI_APPLICATION_GET));
            }

            return toolbarBitmap;
        }
        break;
    case TIDC_POWERMENUDROPDOWN:
        {
            HBITMAP toolbarBitmap = NULL;

            if (ToolStatusConfig.ModernIcons)
            {
                toolbarBitmap = PhLoadPngImageFromResource(PluginInstance->DllBase, ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDB_POWER_MODERN), FALSE);
            }
            else
            {
                toolbarBitmap = ToolbarLoadImageFromIcon(ToolBarImageSize.cx, ToolBarImageSize.cy, MAKEINTRESOURCE(IDI_LIGHTBULB_OFF));
            }

            return toolbarBitmap;
        }
        break;
    case PHAPP_ID_HACKER_SHOWDETAILSFORALLPROCESSES:
        {
            HICON shieldIcon;
            HBITMAP toolbarBitmap = NULL;

            if (shieldIcon = PhLoadIcon(NULL, IDI_SHIELD, PH_LOAD_ICON_SIZE_SMALL, 0, 0))
            {
                toolbarBitmap = PhIconToBitmap(shieldIcon, ToolBarImageSize.cx, ToolBarImageSize.cy);
                DestroyIcon(shieldIcon);
            }

            return toolbarBitmap;
        }
        break;
    }

    return NULL;
}

VOID ToolbarLoadButtonSettings(
    VOID
    )
{
    INT count;
    ULONG64 countInteger;
    PPH_STRING settingsString;
    PTBBUTTON buttonArray;
    PH_STRINGREF remaining;
    PH_STRINGREF part;

    settingsString = PhaGetStringSetting(SETTING_NAME_TOOLBAR_CONFIG);
    remaining = settingsString->sr;

    if (remaining.Length == 0)
    {
        // Load default settings
        SendMessage(ToolBarHandle, TB_ADDBUTTONS, MAX_DEFAULT_TOOLBAR_ITEMS, (LPARAM)ToolbarButtons);
        return;
    }

    // Query the number of buttons to insert
    if (!PhSplitStringRefAtChar(&remaining, '|', &part, &remaining))
    {
        // Load default settings
        SendMessage(ToolBarHandle, TB_ADDBUTTONS, MAX_DEFAULT_TOOLBAR_ITEMS, (LPARAM)ToolbarButtons);
        return;
    }

    if (!PhStringToInteger64(&part, 10, &countInteger))
    {
        // Load default settings
        SendMessage(ToolBarHandle, TB_ADDBUTTONS, MAX_DEFAULT_TOOLBAR_ITEMS, (LPARAM)ToolbarButtons);
        return;
    }

    count = (INT)countInteger;

    // Allocate the button array
    buttonArray = PhAllocate(count * sizeof(TBBUTTON));
    memset(buttonArray, 0, count * sizeof(TBBUTTON));

    for (INT index = 0; index < count; index++)
    {
        ULONG64 commandInteger;
        PH_STRINGREF commandIdPart;

        if (remaining.Length == 0)
            break;

        PhSplitStringRefAtChar(&remaining, '|', &commandIdPart, &remaining);
        PhStringToInteger64(&commandIdPart, 10, &commandInteger);

        buttonArray[index].idCommand = (INT)commandInteger;
        buttonArray[index].iBitmap = I_IMAGECALLBACK;
        buttonArray[index].fsState = TBSTATE_ENABLED;

        if (commandInteger)
        {
            buttonArray[index].fsStyle = BTNS_BUTTON | BTNS_AUTOSIZE;
        }
        else
        {
            buttonArray[index].fsStyle = BTNS_SEP;
        }

        // Pre-cache the image in the Toolbar array on startup.
        for (INT i = 0; i < ARRAYSIZE(ToolbarButtons); i++)
        {
            if (ToolbarButtons[i].idCommand == buttonArray[index].idCommand)
            {
                HBITMAP bitmap;

                if (buttonArray[index].fsStyle & BTNS_SEP)
                    continue;

                if (bitmap = ToolbarGetImage(ToolbarButtons[i].idCommand))
                {
                    // Add the image, cache the value in the ToolbarButtons array, set the bitmap index.
                    buttonArray[index].iBitmap = ToolbarButtons[i].iBitmap = ImageList_Add(
                        ToolBarImageList,
                        bitmap,
                        NULL
                        );

                    DeleteBitmap(bitmap);
                }
                break;
            }
        }
    }

    SendMessage(ToolBarHandle, TB_ADDBUTTONS, count, (LPARAM)buttonArray);

    PhFree(buttonArray);
}

VOID ToolbarSaveButtonSettings(
    VOID
    )
{
    INT index = 0;
    INT count = 0;
    PPH_STRING settingsString;
    PH_STRING_BUILDER stringBuilder;

    PhInitializeStringBuilder(&stringBuilder, 100);

    count = (INT)SendMessage(ToolBarHandle, TB_BUTTONCOUNT, 0, 0);

    PhAppendFormatStringBuilder(
        &stringBuilder,
        L"%d|",
        count
        );

    for (index = 0; index < count; index++)
    {
        TBBUTTONINFO buttonInfo =
        {
            sizeof(TBBUTTONINFO),
            TBIF_BYINDEX | TBIF_IMAGE | TBIF_STYLE | TBIF_COMMAND
        };

        if (SendMessage(ToolBarHandle, TB_GETBUTTONINFO, index, (LPARAM)&buttonInfo) == -1)
            break;

        PhAppendFormatStringBuilder(
            &stringBuilder,
            L"%d|",
            buttonInfo.idCommand
            );
    }

    if (stringBuilder.String->Length != 0)
        PhRemoveEndStringBuilder(&stringBuilder, 1);

    settingsString = PH_AUTO(PhFinalStringBuilderString(&stringBuilder));
    PhSetStringSetting2(SETTING_NAME_TOOLBAR_CONFIG, &settingsString->sr);
}

VOID ReBarLoadLayoutSettings(
    VOID
    )
{
    UINT index = 0;
    UINT count = 0;
    PPH_STRING settingsString;
    PH_STRINGREF remaining;

    settingsString = PhGetStringSetting(SETTING_NAME_REBAR_CONFIG);
    remaining = settingsString->sr;

    if (remaining.Length == 0)
        return;

    count = (UINT)SendMessage(RebarHandle, RB_GETBANDCOUNT, 0, 0);

    for (index = 0; index < count; index++)
    {
        PH_STRINGREF idPart;
        PH_STRINGREF cxPart;
        PH_STRINGREF stylePart;
        ULONG64 idInteger;
        ULONG64 cxInteger;
        ULONG64 styleInteger;
        UINT oldBandIndex;
        REBARBANDINFO rebarBandInfo =
        {
            sizeof(REBARBANDINFO),
            RBBIM_STYLE | RBBIM_SIZE
        };

        if (remaining.Length == 0)
            break;

        PhSplitStringRefAtChar(&remaining, '|', &idPart, &remaining);
        PhSplitStringRefAtChar(&remaining, '|', &cxPart, &remaining);
        PhSplitStringRefAtChar(&remaining, '|', &stylePart, &remaining);

        PhStringToInteger64(&idPart, 10, &idInteger);
        PhStringToInteger64(&cxPart, 10, &cxInteger);
        PhStringToInteger64(&stylePart, 10, &styleInteger);

        if ((oldBandIndex = (UINT)SendMessage(RebarHandle, RB_IDTOINDEX, (UINT)idInteger, 0)) == UINT_MAX)
            continue;

        if (oldBandIndex != index)
        {
            SendMessage(RebarHandle, RB_MOVEBAND, oldBandIndex, index);
        }

        if (SendMessage(RebarHandle, RB_GETBANDINFO, index, (LPARAM)&rebarBandInfo))
        {
            rebarBandInfo.cx = (UINT)cxInteger;
            rebarBandInfo.fStyle |= (UINT)styleInteger;

            SendMessage(RebarHandle, RB_SETBANDINFO, index, (LPARAM)&rebarBandInfo);
        }
    }
}

VOID ReBarSaveLayoutSettings(
    VOID
    )
{
    UINT index = 0;
    UINT count = 0;
    PPH_STRING settingsString;
    PH_STRING_BUILDER stringBuilder;

    PhInitializeStringBuilder(&stringBuilder, 100);

    count = (UINT)SendMessage(RebarHandle, RB_GETBANDCOUNT, 0, 0);

    for (index = 0; index < count; index++)
    {
        REBARBANDINFO rebarBandInfo =
        {
            sizeof(REBARBANDINFO),
            RBBIM_STYLE | RBBIM_SIZE | RBBIM_ID
        };

        SendMessage(RebarHandle, RB_GETBANDINFO, index, (LPARAM)&rebarBandInfo);

        if (rebarBandInfo.fStyle & RBBS_GRIPPERALWAYS)
        {
            rebarBandInfo.fStyle &= ~RBBS_GRIPPERALWAYS;
        }

        if (rebarBandInfo.fStyle & RBBS_NOGRIPPER)
        {
            rebarBandInfo.fStyle &= ~RBBS_NOGRIPPER;
        }

        if (rebarBandInfo.fStyle & RBBS_FIXEDSIZE)
        {
            rebarBandInfo.fStyle &= ~RBBS_FIXEDSIZE;
        }

        PhAppendFormatStringBuilder(
            &stringBuilder,
            L"%u|%u|%u|",
            rebarBandInfo.wID,
            rebarBandInfo.cx,
            rebarBandInfo.fStyle
            );
    }

    if (stringBuilder.String->Length != 0)
        PhRemoveEndStringBuilder(&stringBuilder, 1);

    settingsString = PH_AUTO(PhFinalStringBuilderString(&stringBuilder));
    PhSetStringSetting2(SETTING_NAME_REBAR_CONFIG, &settingsString->sr);
}
