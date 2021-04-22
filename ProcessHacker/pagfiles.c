/*
 * Process Hacker -
 *   pagefiles viewer
 *
 * Copyright (C) 2010 wj32
 * Copyright (C) 2020 dmex
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

#include <phapp.h>
#include <phsettings.h>
#include <settings.h>

HWND PhPageFileWindowHandle = NULL;

typedef struct _PHP_PAGEFILE_PROPERTIES_CONTEXT
{
    HWND WindowHandle;
    HWND ListViewHandle;
    PH_LAYOUT_MANAGER LayoutManager;
} PHP_PAGEFILE_PROPERTIES_CONTEXT, *PPHP_PAGEFILE_PROPERTIES_CONTEXT;

INT_PTR CALLBACK PhpPagefilesDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

VOID PhShowPagefilesDialog(
    _In_ HWND ParentWindowHandle
    )
{
    if (!PhPageFileWindowHandle)
    {
        PhPageFileWindowHandle = CreateDialog(
            PhInstanceHandle,
            MAKEINTRESOURCE(IDD_PAGEFILES),
            PhCsForceNoParent ? NULL : ParentWindowHandle,
            PhpPagefilesDlgProc
            );
        PhRegisterDialog(PhPageFileWindowHandle);
        ShowWindow(PhPageFileWindowHandle, SW_SHOW);
    }

    if (IsMinimized(PhPageFileWindowHandle))
        ShowWindow(PhPageFileWindowHandle, SW_RESTORE);
    else
        SetForegroundWindow(PhPageFileWindowHandle);
}

VOID PhpAddPagefileItems(
    _In_ HWND ListViewHandle,
    _In_ PVOID Pagefiles
    )
{
    PSYSTEM_PAGEFILE_INFORMATION pagefile;

    pagefile = PH_FIRST_PAGEFILE(Pagefiles);

    while (pagefile)
    {
        INT lvItemIndex;
        PPH_STRING fileName;
        PPH_STRING newFileName;
        PPH_STRING usage;

        fileName = PhCreateStringFromUnicodeString(&pagefile->PageFileName);
        newFileName = PhGetFileName(fileName);
        PhDereferenceObject(fileName);

        lvItemIndex = PhAddListViewItem(ListViewHandle, MAXINT, newFileName->Buffer, NULL);
        PhDereferenceObject(newFileName);

        // Usage
        usage = PhFormatSize(UInt32x32To64(pagefile->TotalInUse, PAGE_SIZE), ULONG_MAX);
        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 1, usage->Buffer);
        PhDereferenceObject(usage);

        // Peak usage
        usage = PhFormatSize(UInt32x32To64(pagefile->PeakUsage, PAGE_SIZE), ULONG_MAX);
        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 2, usage->Buffer);
        PhDereferenceObject(usage);

        // Total
        usage = PhFormatSize(UInt32x32To64(pagefile->TotalSize, PAGE_SIZE), ULONG_MAX);
        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 3, usage->Buffer);
        PhDereferenceObject(usage);

        pagefile = PH_NEXT_PAGEFILE(pagefile);
    }
}

VOID PhpAddPagefileItemsEx(
    _In_ HWND ListViewHandle,
    _In_ PVOID Pagefiles
    )
{
    PSYSTEM_PAGEFILE_INFORMATION_EX pagefile;

    pagefile = PH_FIRST_PAGEFILE_EX(Pagefiles);

    while (pagefile)
    {
        INT lvItemIndex;
        PPH_STRING fileName;
        PPH_STRING newFileName;
        PPH_STRING usage;

        fileName = PhCreateStringFromUnicodeString(&pagefile->Info.PageFileName);
        newFileName = PhGetFileName(fileName);
        PhDereferenceObject(fileName);

        lvItemIndex = PhAddListViewItem(ListViewHandle, MAXINT, newFileName->Buffer, NULL);
        PhDereferenceObject(newFileName);

        // Usage
        usage = PhFormatSize(UInt32x32To64(pagefile->Info.TotalInUse, PAGE_SIZE), ULONG_MAX);
        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 1, usage->Buffer);
        PhDereferenceObject(usage);

        // Peak usage
        usage = PhFormatSize(UInt32x32To64(pagefile->Info.PeakUsage, PAGE_SIZE), ULONG_MAX);
        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 2, usage->Buffer);
        PhDereferenceObject(usage);

        // Total
        usage = PhFormatSize(UInt32x32To64(pagefile->Info.TotalSize, PAGE_SIZE), ULONG_MAX);
        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 3, usage->Buffer);
        PhDereferenceObject(usage);

        // Minimum
        usage = PhFormatSize(UInt32x32To64(pagefile->MinimumSize, PAGE_SIZE), ULONG_MAX);
        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 4, usage->Buffer);
        PhDereferenceObject(usage);

        // Maximum
        usage = PhFormatSize(UInt32x32To64(pagefile->MaximumSize, PAGE_SIZE), ULONG_MAX);
        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 5, usage->Buffer);
        PhDereferenceObject(usage);

        pagefile = PH_NEXT_PAGEFILE_EX(pagefile);
    }
}

INT_PTR CALLBACK PhpPagefilesDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PPHP_PAGEFILE_PROPERTIES_CONTEXT context;

    if (uMsg == WM_INITDIALOG)
    {
        context = PhAllocateZero(sizeof(PHP_PAGEFILE_PROPERTIES_CONTEXT));
        context->WindowHandle = hwndDlg;

        PhSetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT, context);
    }
    else
    {
        context = PhGetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
    }

    if (!context)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            NTSTATUS status;
            PVOID pagefiles;

            SendMessage(hwndDlg, WM_SETICON, ICON_SMALL, (LPARAM)PH_LOAD_SHARED_ICON_SMALL(PhInstanceHandle, MAKEINTRESOURCE(IDI_PROCESSHACKER)));
            SendMessage(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM)PH_LOAD_SHARED_ICON_LARGE(PhInstanceHandle, MAKEINTRESOURCE(IDI_PROCESSHACKER)));

            context->WindowHandle = hwndDlg;
            context->ListViewHandle = GetDlgItem(hwndDlg, IDC_LIST);

            PhAddListViewColumn(context->ListViewHandle, 0, 0, 0, LVCFMT_LEFT, 120, L"File name");
            PhAddListViewColumn(context->ListViewHandle, 1, 1, 1, LVCFMT_LEFT, 100, L"Usage");
            PhAddListViewColumn(context->ListViewHandle, 2, 2, 2, LVCFMT_LEFT, 100, L"Peak usage");
            PhAddListViewColumn(context->ListViewHandle, 3, 3, 3, LVCFMT_LEFT, 100, L"Total");

            if (WindowsVersion > WINDOWS_8)
            {
                PhAddListViewColumn(context->ListViewHandle, 4, 4, 4, LVCFMT_LEFT, 100, L"Minimum");
                PhAddListViewColumn(context->ListViewHandle, 5, 5, 5, LVCFMT_LEFT, 100, L"Maximum");
            }

            PhSetListViewStyle(context->ListViewHandle, FALSE, TRUE);
            PhSetControlTheme(context->ListViewHandle, L"explorer");
            PhSetExtendedListView(context->ListViewHandle);

            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);
            PhAddLayoutItem(&context->LayoutManager, context->ListViewHandle, NULL, PH_ANCHOR_ALL);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_REFRESH), NULL, PH_ANCHOR_RIGHT | PH_ANCHOR_BOTTOM);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDOK), NULL, PH_ANCHOR_RIGHT | PH_ANCHOR_BOTTOM);
            PhLoadListViewColumnsFromSetting(L"PageFileListViewColumns", context->ListViewHandle);

            ExtendedListView_SetRedraw(context->ListViewHandle, FALSE);
            ListView_DeleteAllItems(context->ListViewHandle);

            if (WindowsVersion > WINDOWS_8)
            {
                if (NT_SUCCESS(status = PhEnumPagefilesEx(&pagefiles)))
                {
                    PhpAddPagefileItemsEx(context->ListViewHandle, pagefiles);
                    PhFree(pagefiles);
                }
            }
            else
            {
                if (NT_SUCCESS(status = PhEnumPagefiles(&pagefiles)))
                {
                    PhpAddPagefileItems(context->ListViewHandle, pagefiles);
                    PhFree(pagefiles);
                }
            }

            ExtendedListView_SetRedraw(context->ListViewHandle, TRUE);

            if (!NT_SUCCESS(status))
            {
                PhShowStatus(hwndDlg, L"Unable to query pagefile information.", status, 0);
                DestroyWindow(hwndDlg);
            }

            if (PhGetIntegerPairSetting(L"PageFileWindowPosition").X)
                PhLoadWindowPlacementFromSetting(L"PageFileWindowPosition", L"PageFileWindowSize", hwndDlg);
            else
                PhCenterWindow(hwndDlg, GetParent(hwndDlg));

            PhSetDialogFocus(hwndDlg, GetDlgItem(hwndDlg, IDOK));

            PhInitializeWindowTheme(hwndDlg, PhEnableThemeSupport);
        }
        break;
    case WM_DESTROY:
        {
            PhSaveListViewColumnsToSetting(L"PageFileListViewColumns", context->ListViewHandle);
            PhSaveWindowPlacementToSetting(L"PageFileWindowPosition", L"PageFileWindowSize", hwndDlg);

            PhUnregisterDialog(PhPageFileWindowHandle);
            PhPageFileWindowHandle = NULL;

            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);

            PhDeleteLayoutManager(&context->LayoutManager);
            PhFree(context);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDCANCEL:
            case IDOK:
                DestroyWindow(hwndDlg);
                break;
            case IDC_REFRESH:
                {
                    NTSTATUS status;
                    PVOID pagefiles;

                    ExtendedListView_SetRedraw(context->ListViewHandle, FALSE);
                    ListView_DeleteAllItems(context->ListViewHandle);

                    if (WindowsVersion > WINDOWS_8)
                    {
                        if (NT_SUCCESS(status = PhEnumPagefilesEx(&pagefiles)))
                        {
                            PhpAddPagefileItemsEx(context->ListViewHandle, pagefiles);
                            PhFree(pagefiles);
                        }
                    }
                    else
                    {
                        if (NT_SUCCESS(status = PhEnumPagefiles(&pagefiles)))
                        {
                            PhpAddPagefileItems(context->ListViewHandle, pagefiles);
                            PhFree(pagefiles);
                        }
                    }

                    ExtendedListView_SetRedraw(context->ListViewHandle, TRUE);

                    if (!NT_SUCCESS(status))
                    {
                        PhShowStatus(hwndDlg, L"Unable to query pagefile information.", status, 0);
                    }
                }
                break;
            }
        }
        break;
    case WM_SIZE:
        {
            PhLayoutManagerLayout(&context->LayoutManager);
        }
        break;
    }

    return FALSE;
}
