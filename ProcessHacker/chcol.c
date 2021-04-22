/*
 * Process Hacker -
 *   column chooser
 *
 * Copyright (C) 2010 wj32
 * Copyright (C) 2017-2018 dmex
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
#include <settings.h>

#include <phsettings.h>

typedef struct _COLUMNS_DIALOG_CONTEXT
{
    HWND ControlHandle;
    ULONG Type;
    PPH_LIST Columns;

    HWND InactiveList;
    HWND ActiveList;
} COLUMNS_DIALOG_CONTEXT, *PCOLUMNS_DIALOG_CONTEXT;

INT_PTR CALLBACK PhpColumnsDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

VOID PhShowChooseColumnsDialog(
    _In_ HWND ParentWindowHandle,
    _In_ HWND ControlHandle,
    _In_ ULONG Type
    )
{
    COLUMNS_DIALOG_CONTEXT context;

    context.ControlHandle = ControlHandle;
    context.Type = Type;

    if (Type == PH_CONTROL_TYPE_TREE_NEW)
        context.Columns = PhCreateList(TreeNew_GetColumnCount(ControlHandle));
    else
        return;

    DialogBoxParam(
        PhInstanceHandle,
        MAKEINTRESOURCE(IDD_CHOOSECOLUMNS),
        ParentWindowHandle,
        PhpColumnsDlgProc,
        (LPARAM)&context
        );

    PhDereferenceObject(context.Columns);
}

static int __cdecl PhpColumnsCompareDisplayIndexTn(
    _In_ const void *elem1,
    _In_ const void *elem2
    )
{
    PPH_TREENEW_COLUMN column1 = *(PPH_TREENEW_COLUMN *)elem1;
    PPH_TREENEW_COLUMN column2 = *(PPH_TREENEW_COLUMN *)elem2;

    return uintcmp(column1->DisplayIndex, column2->DisplayIndex);
}

_Success_(return != -1)
static ULONG IndexOfStringInList(
    _In_ PPH_LIST List,
    _In_ PWSTR String
    )
{
    ULONG i;

    for (i = 0; i < List->Count; i++)
    {
        if (PhEqualString2(List->Items[i], String, FALSE))
            return i;
    }

    return -1;
}

INT_PTR CALLBACK PhpColumnsDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PCOLUMNS_DIALOG_CONTEXT context = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        context = (PCOLUMNS_DIALOG_CONTEXT)lParam;
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
            ULONG count;
            ULONG total;
            ULONG i;
            PPH_LIST displayOrderList = NULL;

            context->InactiveList = GetDlgItem(hwndDlg, IDC_INACTIVE);
            context->ActiveList = GetDlgItem(hwndDlg, IDC_ACTIVE);
            PhCenterWindow(hwndDlg, GetParent(hwndDlg));

            if (context->Type == PH_CONTROL_TYPE_TREE_NEW)
            {
                PH_TREENEW_COLUMN column;

                count = 0;
                total = TreeNew_GetColumnCount(context->ControlHandle);
                i = 0;

                displayOrderList = PhCreateList(total);

                while (count < total)
                {
                    if (TreeNew_GetColumn(context->ControlHandle, i, &column))
                    {
                        PPH_TREENEW_COLUMN copy;

                        if (column.Fixed)
                        {
                            i++;
                            total--;
                            continue;
                        }

                        copy = PhAllocateCopy(&column, sizeof(PH_TREENEW_COLUMN));
                        PhAddItemList(context->Columns, copy);
                        count++;

                        if (column.Visible)
                        {
                            PhAddItemList(displayOrderList, copy);
                        }
                        else
                        {
                            ListBox_AddString(context->InactiveList, column.Text);
                        }
                    }

                    i++;
                }

                qsort(displayOrderList->Items, displayOrderList->Count, sizeof(PVOID), PhpColumnsCompareDisplayIndexTn);
            }

            if (displayOrderList)
            {
                for (i = 0; i < displayOrderList->Count; i++)
                {
                    if (context->Type == PH_CONTROL_TYPE_TREE_NEW)
                    {
                        PPH_TREENEW_COLUMN copy = displayOrderList->Items[i];

                        ListBox_AddString(context->ActiveList, copy->Text);
                    }
                }

                PhDereferenceObject(displayOrderList);
            }

            SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_INACTIVE, LBN_SELCHANGE), (LPARAM)context->InactiveList);
            SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_ACTIVE, LBN_SELCHANGE), (LPARAM)context->ActiveList);

            PhInitializeWindowTheme(hwndDlg, PhEnableThemeSupport);
        }
        break;
    case WM_DESTROY:
        {
            ULONG i;

            for (i = 0; i < context->Columns->Count; i++)
                PhFree(context->Columns->Items[i]);

            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDCANCEL:
                EndDialog(hwndDlg, IDCANCEL);
                break;
            case IDOK:
                {
#define ORDER_LIMIT 200
                    PPH_LIST activeList;
                    ULONG activeCount;
                    ULONG i;
                    INT orderArray[ORDER_LIMIT];
                    INT maxOrder;

                    memset(orderArray, 0, sizeof(orderArray));
                    maxOrder = 0;

                    activeCount = ListBox_GetCount(context->ActiveList);
                    activeList = PhCreateList(activeCount);

                    for (i = 0; i < activeCount; i++)
                        PhAddItemList(activeList, PhGetListBoxString(context->ActiveList, i));

                    if (context->Type == PH_CONTROL_TYPE_TREE_NEW)
                    {
                        // Apply visiblity settings and build the order array.

                        TreeNew_SetRedraw(context->ControlHandle, FALSE);

                        for (i = 0; i < context->Columns->Count; i++)
                        {
                            PPH_TREENEW_COLUMN column = context->Columns->Items[i];
                            ULONG index;

                            index = IndexOfStringInList(activeList, column->Text);
                            column->Visible = index != -1;

                            TreeNew_SetColumn(context->ControlHandle, TN_COLUMN_FLAG_VISIBLE, column);

                            if (column->Visible && index < ORDER_LIMIT)
                            {
                                orderArray[index] = column->Id;

                                if ((ULONG)maxOrder < index + 1)
                                    maxOrder = index + 1;
                            }
                        }

                        // Apply display order.
                        TreeNew_SetColumnOrderArray(context->ControlHandle, maxOrder, orderArray);

                        TreeNew_SetRedraw(context->ControlHandle, TRUE);

                        PhDereferenceObject(activeList);

                        InvalidateRect(context->ControlHandle, NULL, FALSE);
                    }

                    EndDialog(hwndDlg, IDOK);
                }
                break;
            case IDC_INACTIVE:
                {
                    switch (HIWORD(wParam))
                    {
                    case LBN_DBLCLK:
                        {
                            SendMessage(hwndDlg, WM_COMMAND, IDC_SHOW, 0);
                        }
                        break;
                    case LBN_SELCHANGE:
                        {
                            INT sel = ListBox_GetCurSel(context->InactiveList);

                            EnableWindow(GetDlgItem(hwndDlg, IDC_SHOW), sel != -1);
                        }
                        break;
                    }
                }
                break;
            case IDC_ACTIVE:
                {
                    switch (HIWORD(wParam))
                    {
                    case LBN_DBLCLK:
                        {
                            SendMessage(hwndDlg, WM_COMMAND, IDC_HIDE, 0);
                        }
                        break;
                    case LBN_SELCHANGE:
                        {
                            INT sel = ListBox_GetCurSel(context->ActiveList);
                            INT count = ListBox_GetCount(context->ActiveList);

                            EnableWindow(GetDlgItem(hwndDlg, IDC_HIDE), sel != -1 && count != 1);
                            EnableWindow(GetDlgItem(hwndDlg, IDC_MOVEUP), sel != 0 && sel != -1);
                            EnableWindow(GetDlgItem(hwndDlg, IDC_MOVEDOWN), sel != count - 1 && sel != -1);
                        }
                        break;
                    }
                }
                break;
            case IDC_SHOW:
                {
                    INT sel;
                    INT count;
                    PPH_STRING string;

                    sel = ListBox_GetCurSel(context->InactiveList);
                    count = ListBox_GetCount(context->InactiveList);

                    if (string = PhGetListBoxString(context->InactiveList, sel))
                    {
                        ListBox_DeleteString(context->InactiveList, sel);
                        ListBox_AddString(context->ActiveList, string->Buffer);
                        PhDereferenceObject(string);

                        count--;

                        if (sel >= count - 1)
                            sel = count - 1;

                        ListBox_SetCurSel(context->InactiveList, sel);

                        SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_INACTIVE, LBN_SELCHANGE), (LPARAM)context->InactiveList);
                        SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_ACTIVE, LBN_SELCHANGE), (LPARAM)context->ActiveList);
                    }
                }
                break;
            case IDC_HIDE:
                {
                    INT sel;
                    INT count;
                    PPH_STRING string;

                    sel = ListBox_GetCurSel(context->ActiveList);
                    count = ListBox_GetCount(context->ActiveList);

                    if (count != 1)
                    {
                        if (string = PhGetListBoxString(context->ActiveList, sel))
                        {
                            ListBox_DeleteString(context->ActiveList, sel);
                            ListBox_AddString(context->InactiveList, string->Buffer);
                            PhDereferenceObject(string);

                            count--;

                            if (sel >= count - 1)
                                sel = count - 1;

                            ListBox_SetCurSel(context->ActiveList, sel);

                            SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_INACTIVE, LBN_SELCHANGE), (LPARAM)context->InactiveList);
                            SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_ACTIVE, LBN_SELCHANGE), (LPARAM)context->ActiveList);
                        }
                    }
                }
                break;
            case IDC_MOVEUP:
                {
                    INT sel;
                    INT count;
                    PPH_STRING string;

                    sel = ListBox_GetCurSel(context->ActiveList);
                    count = ListBox_GetCount(context->ActiveList);

                    if (sel != 0)
                    {
                        if (string = PhGetListBoxString(context->ActiveList, sel))
                        {
                            ListBox_DeleteString(context->ActiveList, sel);
                            ListBox_InsertString(context->ActiveList, sel - 1, string->Buffer);
                            PhDereferenceObject(string);

                            sel -= 1;
                            ListBox_SetCurSel(context->ActiveList, sel);
                            EnableWindow(GetDlgItem(hwndDlg, IDC_MOVEUP), sel != 0);
                            EnableWindow(GetDlgItem(hwndDlg, IDC_MOVEDOWN), sel != count - 1);
                        }
                    }
                }
                break;
            case IDC_MOVEDOWN:
                {
                    INT sel;
                    INT count;
                    PPH_STRING string;

                    sel = ListBox_GetCurSel(context->ActiveList);
                    count = ListBox_GetCount(context->ActiveList);

                    if (sel != count - 1)
                    {
                        if (string = PhGetListBoxString(context->ActiveList, sel))
                        {
                            ListBox_DeleteString(context->ActiveList, sel);
                            ListBox_InsertString(context->ActiveList, sel + 1, string->Buffer);
                            PhDereferenceObject(string);

                            sel += 1;
                            ListBox_SetCurSel(context->ActiveList, sel);
                            EnableWindow(GetDlgItem(hwndDlg, IDC_MOVEUP), sel != 0);
                            EnableWindow(GetDlgItem(hwndDlg, IDC_MOVEDOWN), sel != count - 1);
                        }
                    }
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}
