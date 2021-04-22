/*
 * Process Hacker -
 *   tree new column set manager
 *
 * Copyright (C) 2017 dmex
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
#include <colsetmgr.h>
#include <settings.h>
#include <extmgri.h>

PPH_LIST PhInitializeColumnSetList(
    _In_ PWSTR SettingName
    )
{
    PPH_LIST columnSetList;
    PPH_STRING settingsString;
    ULONG64 count;
    ULONG64 index;
    PH_STRINGREF remaining;
    PH_STRINGREF part;

    columnSetList = PhCreateList(10);
    settingsString = PhaGetStringSetting(SettingName);
    remaining = settingsString->sr;

    if (remaining.Length == 0)
        goto CleanupExit;
    if (!PhSplitStringRefAtChar(&remaining, '-', &part, &remaining))
        goto CleanupExit;
    if (!PhStringToInteger64(&part, 10, &count))
        goto CleanupExit;

    for (index = 0; index < count; index++)
    {
        PH_STRINGREF columnSetNamePart;
        PH_STRINGREF columnSetSettingPart;
        PH_STRINGREF columnSetSortPart;

        if (remaining.Length == 0)
            break;

        PhSplitStringRefAtChar(&remaining, '-', &columnSetNamePart, &remaining);
        PhSplitStringRefAtChar(&remaining, '-', &columnSetSettingPart, &remaining);
        PhSplitStringRefAtChar(&remaining, '-', &columnSetSortPart, &remaining);

        {
            PPH_COLUMN_SET_ENTRY entry;

            entry = PhAllocate(sizeof(PH_COLUMN_SET_ENTRY));
            entry->Name = PhCreateString2(&columnSetNamePart);
            entry->Setting = PhCreateString2(&columnSetSettingPart);
            entry->Sorting = PhCreateString2(&columnSetSortPart);

            PhAddItemList(columnSetList, entry);
        }
    }

CleanupExit:
    return columnSetList;
}

VOID PhSaveSettingsColumnList(
    _In_ PWSTR SettingName,
    _In_ PPH_LIST ColumnSetList
    )
{
    ULONG index;
    PPH_STRING settingsString;
    PH_STRING_BUILDER stringBuilder;

    PhInitializeStringBuilder(&stringBuilder, 100);
    PhAppendFormatStringBuilder(
        &stringBuilder,
        L"%lu-",
        ColumnSetList->Count
        );

    for (index = 0; index < ColumnSetList->Count; index++)
    {
        PPH_COLUMN_SET_ENTRY entry = ColumnSetList->Items[index];

        if (PhIsNullOrEmptyString(entry->Name))
            continue;

        PhAppendFormatStringBuilder(
            &stringBuilder,
            L"%s-%s-%s-",
            entry->Name->Buffer,
            PhGetStringOrEmpty(entry->Setting),
            PhGetStringOrEmpty(entry->Sorting)
            );
    }

    if (stringBuilder.String->Length != 0)
        PhRemoveEndStringBuilder(&stringBuilder, 1);

    settingsString = PH_AUTO(PhFinalStringBuilderString(&stringBuilder));
    PhSetStringSetting2(SettingName, &settingsString->sr);
}

VOID PhDeleteColumnSetList(
    _In_ PPH_LIST ColumnSetList
    )
{
    for (ULONG i = 0; i < ColumnSetList->Count; i++)
    {
        PPH_COLUMN_SET_ENTRY entry = ColumnSetList->Items[i];

        //PhRemoveItemList(ColumnSetList, PhFindItemList(ColumnSetList, entry));

        PhClearReference(&entry->Name);
        PhClearReference(&entry->Setting);
        PhClearReference(&entry->Sorting);

        PhFree(entry);
    }

    PhDereferenceObject(ColumnSetList);
}

_Success_(return)
BOOLEAN PhLoadSettingsColumnSet(
    _In_ PWSTR SettingName,
    _In_ PPH_STRING ColumnSetName,
    _Out_ PPH_STRING *TreeListSettings,
    _Out_ PPH_STRING *TreeSortSettings
    )
{
    PPH_STRING treeSettings = NULL;
    PPH_STRING sortSettings = NULL;
    PPH_STRING settingsString;
    ULONG64 count;
    ULONG64 index;
    PH_STRINGREF remaining;
    PH_STRINGREF part;

    settingsString = PhaGetStringSetting(SettingName);
    remaining = settingsString->sr;

    if (remaining.Length == 0)
        return FALSE;

    if (!PhSplitStringRefAtChar(&remaining, '-', &part, &remaining))
        return FALSE;
    if (!PhStringToInteger64(&part, 10, &count))
        return FALSE;

    for (index = 0; index < count; index++)
    {
        PH_STRINGREF columnSetNamePart;
        PH_STRINGREF columnSetSettingPart;
        PH_STRINGREF columnSetSortPart;

        if (remaining.Length == 0)
            break;

        PhSplitStringRefAtChar(&remaining, '-', &columnSetNamePart, &remaining);
        PhSplitStringRefAtChar(&remaining, '-', &columnSetSettingPart, &remaining);
        PhSplitStringRefAtChar(&remaining, '-', &columnSetSortPart, &remaining);

        if (PhEqualStringRef(&columnSetNamePart, &ColumnSetName->sr, FALSE))
        {
            treeSettings = PhCreateString2(&columnSetSettingPart);
            sortSettings = PhCreateString2(&columnSetSortPart);
            break;
        }
    }

    if (!PhIsNullOrEmptyString(treeSettings) && !PhIsNullOrEmptyString(sortSettings))
    {
        *TreeListSettings = treeSettings;
        *TreeSortSettings = sortSettings;
        return TRUE;
    }
    else
    {
        if (treeSettings)
            PhDereferenceObject(treeSettings);
        if (sortSettings)
            PhDereferenceObject(sortSettings);
        return FALSE;
    }
}

VOID PhSaveSettingsColumnSet(
    _In_ PWSTR SettingName,
    _In_ PPH_STRING ColumnSetName,
    _In_ PPH_STRING TreeListSettings,
    _In_ PPH_STRING TreeSortSettings
    )
{
    ULONG index;
    BOOLEAN found = FALSE;
    PPH_LIST columnSetList;

    columnSetList = PhInitializeColumnSetList(SettingName);

    for (index = 0; index < columnSetList->Count; index++)
    {
        PPH_COLUMN_SET_ENTRY entry = columnSetList->Items[index];

        if (PhEqualString(entry->Name, ColumnSetName, FALSE))
        {
            PhReferenceObject(TreeListSettings);
            PhReferenceObject(TreeSortSettings);

            PhMoveReference(&entry->Setting, TreeListSettings);
            PhMoveReference(&entry->Sorting, TreeSortSettings);

            found = TRUE;
            break;
        }
    }

    if (!found)
    {
        PPH_COLUMN_SET_ENTRY entry;

        PhReferenceObject(ColumnSetName);
        PhReferenceObject(TreeListSettings);
        PhReferenceObject(TreeSortSettings);

        entry = PhAllocate(sizeof(PH_COLUMN_SET_ENTRY));
        entry->Name = ColumnSetName;
        entry->Setting = TreeListSettings;
        entry->Sorting = TreeSortSettings;

        PhAddItemList(columnSetList, entry);
    }

    PhSaveSettingsColumnList(SettingName, columnSetList);

    PhDeleteColumnSetList(columnSetList);
}

// Column Set Editor Dialog

typedef struct _COLUMNSET_DIALOG_CONTEXT
{
    HWND DialogHandle;
    HWND ListViewHandle;
    HWND RenameButtonHandle;
    HWND MoveUpButtonHandle;
    HWND MoveDownButtonHandle;
    HWND RemoveButtonHandle;
    PPH_STRING SettingName;
    PPH_LIST ColumnSetList;
    BOOLEAN LabelEditActive;
} COLUMNSET_DIALOG_CONTEXT, *PCOLUMNSET_DIALOG_CONTEXT;

INT_PTR CALLBACK PhpColumnSetEditorDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

VOID PhShowColumnSetEditorDialog(
    _In_ HWND ParentWindowHandle,
    _In_ PWSTR SettingName
    )
{
    DialogBoxParam(
        PhInstanceHandle,
        MAKEINTRESOURCE(IDD_COLUMNSETS),
        ParentWindowHandle,
        PhpColumnSetEditorDlgProc,
        (LPARAM)SettingName
        );
}

VOID PhpMoveListViewItem(
    _In_ HWND ListViewHandle,
    _In_ INT ItemIndex1,
    _In_ INT ItemIndex2
    )
{
    LVITEM item1;
    LVITEM item2;
    WCHAR buffer1[MAX_PATH];
    WCHAR buffer2[MAX_PATH];

    item1.mask = LVIF_TEXT | LVIF_PARAM | LVIF_STATE;
    item1.stateMask = (UINT)-1;
    item1.iItem = ItemIndex1;
    item1.iSubItem = 0;
    item1.cchTextMax = sizeof(buffer1);
    item1.pszText = buffer1;

    item2.mask = LVIF_TEXT | LVIF_PARAM | LVIF_STATE;
    item2.stateMask = (UINT)-1;
    item2.iItem = ItemIndex2;
    item2.iSubItem = 0;
    item2.cchTextMax = sizeof(buffer2);
    item2.pszText = buffer2;

    if (
        ListView_GetItem(ListViewHandle, &item1) &&
        ListView_GetItem(ListViewHandle, &item2)
        )
    {
        item1.iItem = ItemIndex2;
        item2.iItem = ItemIndex1;

        ListView_SetItem(ListViewHandle, &item1);
        ListView_SetItem(ListViewHandle, &item2);
    }
}

VOID PhpMoveSelectedListViewItemUp(
    _In_ HWND ListViewHandle
    )
{
    INT lvItemIndex;

    lvItemIndex = ListView_GetNextItem(ListViewHandle, -1, LVNI_SELECTED);

    if (lvItemIndex != -1)
    {
        PhpMoveListViewItem(ListViewHandle, lvItemIndex, lvItemIndex - 1);
    }

    SetFocus(ListViewHandle);
    ListView_SetItemState(ListViewHandle, lvItemIndex - 1, LVNI_SELECTED, LVNI_SELECTED);
    //ListView_EnsureVisible(ListViewHandle, lvItemIndex - 1, FALSE);
}

VOID PhpMoveSelectedListViewItemDown(
    _In_ HWND ListViewHandle
    )
{
    INT lvItemIndex;

    lvItemIndex = ListView_GetNextItem(ListViewHandle, -1, LVNI_SELECTED);

    if (lvItemIndex != -1)
    {
        PhpMoveListViewItem(ListViewHandle, lvItemIndex, lvItemIndex + 1);
    }

    SetFocus(ListViewHandle);
    ListView_SetItemState(ListViewHandle, lvItemIndex + 1, LVNI_SELECTED, LVNI_SELECTED);
    //ListView_EnsureVisible(ListViewHandle, lvItemIndex + 1, FALSE);
}

INT_PTR CALLBACK PhpColumnSetEditorDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PCOLUMNSET_DIALOG_CONTEXT context = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        context = PhAllocate(sizeof(COLUMNSET_DIALOG_CONTEXT));
        memset(context, 0, sizeof(COLUMNSET_DIALOG_CONTEXT));

        context->SettingName = PhCreateString((PWSTR)lParam);

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
            context->DialogHandle = hwndDlg;
            context->ListViewHandle = GetDlgItem(hwndDlg, IDC_COLUMNSETLIST);
            context->RenameButtonHandle = GetDlgItem(hwndDlg, IDC_RENAME);
            context->MoveUpButtonHandle = GetDlgItem(hwndDlg, IDC_MOVEUP);
            context->MoveDownButtonHandle = GetDlgItem(hwndDlg, IDC_MOVEDOWN);
            context->RemoveButtonHandle = GetDlgItem(hwndDlg, IDC_REMOVE);

            PhCenterWindow(hwndDlg, GetParent(hwndDlg));

            PhSetListViewStyle(context->ListViewHandle, FALSE, TRUE);
            PhSetControlTheme(context->ListViewHandle, L"explorer");
            PhAddListViewColumn(context->ListViewHandle, 0, 0, 0, LVCFMT_LEFT, 250, L"Name");
            PhSetExtendedListView(context->ListViewHandle);

            context->ColumnSetList = PhInitializeColumnSetList(PhGetString(context->SettingName));

            for (ULONG i = 0; i <  context->ColumnSetList->Count; i++)
            {
                PPH_COLUMN_SET_ENTRY entry = context->ColumnSetList->Items[i];

                PhAddListViewItem(context->ListViewHandle, MAXINT, entry->Name->Buffer, entry);
            }

            Button_Enable(context->RenameButtonHandle, FALSE);
            Button_Enable(context->MoveUpButtonHandle, FALSE);
            Button_Enable(context->MoveDownButtonHandle, FALSE);
            Button_Enable(context->RemoveButtonHandle, FALSE);
        }
        break;
    case WM_DESTROY:
        {
            PhDeleteColumnSetList(context->ColumnSetList);

            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
            PhFree(context);
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
                    if (context->LabelEditActive)
                        break;

                    PhSaveSettingsColumnList(PhGetString(context->SettingName), context->ColumnSetList);

                    EndDialog(hwndDlg, IDOK);
                }
                break;
            case IDC_RENAME:
                {
                    INT lvItemIndex;

                    lvItemIndex = ListView_GetNextItem(context->ListViewHandle, -1, LVNI_SELECTED);

                    if (lvItemIndex != -1)
                    {
                        SetFocus(context->ListViewHandle);
                        ListView_EditLabel(context->ListViewHandle, lvItemIndex);
                    }
                }
                break;
            case IDC_MOVEUP:
                {
                    INT lvItemIndex;
                    PPH_COLUMN_SET_ENTRY entry;
                    ULONG index;

                    PhpMoveSelectedListViewItemUp(context->ListViewHandle);

                    lvItemIndex = ListView_GetNextItem(context->ListViewHandle, -1, LVNI_SELECTED);

                    if (lvItemIndex != -1 && PhGetListViewItemParam(context->ListViewHandle, lvItemIndex, (PVOID *)&entry))
                    {
                        index = PhFindItemList(context->ColumnSetList, entry);

                        if (index != ULONG_MAX)
                        {
                            PhRemoveItemList(context->ColumnSetList, index);
                            PhInsertItemList(context->ColumnSetList, lvItemIndex, entry);   
                        }
                    }
                }
                break;
            case IDC_MOVEDOWN:
                {
                    INT lvItemIndex;
                    PPH_COLUMN_SET_ENTRY entry;
                    ULONG index;

                    PhpMoveSelectedListViewItemDown(context->ListViewHandle);

                    lvItemIndex = ListView_GetNextItem(context->ListViewHandle, -1, LVNI_SELECTED);

                    if (lvItemIndex != -1 && PhGetListViewItemParam(context->ListViewHandle, lvItemIndex, (PVOID *)&entry))
                    {
                        index = PhFindItemList(context->ColumnSetList, entry);

                        if (index != ULONG_MAX)
                        {
                            PhRemoveItemList(context->ColumnSetList, index);
                            PhInsertItemList(context->ColumnSetList, lvItemIndex, entry);
                        }
                    }
                }
                break;
            case IDC_REMOVE:
                {
                    INT lvItemIndex;
                    PPH_COLUMN_SET_ENTRY entry;
                    ULONG index;

                    lvItemIndex = ListView_GetNextItem(context->ListViewHandle, -1, LVNI_SELECTED);

                    if (lvItemIndex != -1 && PhGetListViewItemParam(context->ListViewHandle, lvItemIndex, (PVOID *)&entry))
                    {
                        index = PhFindItemList(context->ColumnSetList, entry);

                        if (index != ULONG_MAX)
                        {
                            PhRemoveItemList(context->ColumnSetList, index);
                            PhRemoveListViewItem(context->ListViewHandle, lvItemIndex);

                            PhClearReference(&entry->Name);
                            PhClearReference(&entry->Setting);
                            PhClearReference(&entry->Sorting);
                            PhFree(entry);
                        }

                        SetFocus(context->ListViewHandle);
                        ListView_SetItemState(context->ListViewHandle, 0, LVNI_SELECTED, LVNI_SELECTED);
                        //ListView_EnsureVisible(context->ListViewHandle, 0, FALSE);
                    }
                }
                break;
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            switch (header->code)
            {
            case NM_DBLCLK:
                {
                    INT lvItemIndex;

                    lvItemIndex = ListView_GetNextItem(context->ListViewHandle, -1, LVNI_SELECTED);

                    if (lvItemIndex != -1)
                    {
                        SetFocus(context->ListViewHandle);
                        ListView_EditLabel(context->ListViewHandle, lvItemIndex);
                    }
                }
                break;
            case LVN_ITEMCHANGED:
                {
                    LPNMLISTVIEW listview = (LPNMLISTVIEW)lParam;
                    INT index;
                    INT lvItemIndex;
                    INT count;

                    index = listview->iItem;
                    lvItemIndex = ListView_GetNextItem(context->ListViewHandle, -1, LVNI_SELECTED);
                    count = ListView_GetItemCount(context->ListViewHandle);

                    if (count == 0 || index == -1 || lvItemIndex == -1)
                    {
                        Button_Enable(context->RenameButtonHandle, FALSE);
                        Button_Enable(context->MoveUpButtonHandle, FALSE);
                        Button_Enable(context->MoveDownButtonHandle, FALSE);
                        Button_Enable(context->RemoveButtonHandle, FALSE);
                        break;
                    }

                    if (index != lvItemIndex)
                        break;

                    if (index == 0 && count == 1)
                    {
                        // First and last item
                        Button_Enable(context->MoveUpButtonHandle, FALSE);
                        Button_Enable(context->MoveDownButtonHandle, FALSE);
                    }
                    else if (index == (count - 1))
                    {
                        // Last item
                        Button_Enable(context->MoveUpButtonHandle, TRUE);
                        Button_Enable(context->MoveDownButtonHandle, FALSE);
                    }
                    else if (index == 0)
                    {
                        // First item
                        Button_Enable(context->MoveUpButtonHandle, FALSE);
                        Button_Enable(context->MoveDownButtonHandle, TRUE);
                    }
                    else
                    {
                        Button_Enable(context->MoveUpButtonHandle, TRUE);
                        Button_Enable(context->MoveDownButtonHandle, TRUE);
                    }

                    Button_Enable(context->RenameButtonHandle, TRUE);
                    Button_Enable(context->RemoveButtonHandle, TRUE);
                }
                break;
            case LVN_BEGINLABELEDIT:
                context->LabelEditActive = TRUE;
                break;
            case LVN_ENDLABELEDIT:
                {
                    LV_DISPINFO* lvinfo = (LV_DISPINFO*)lParam;

                    if (lvinfo->item.iItem != -1 && lvinfo->item.pszText)
                    {
                        BOOLEAN found = FALSE;
                        PPH_COLUMN_SET_ENTRY entry;
                        ULONG index;

                        for (ULONG i = 0; i < context->ColumnSetList->Count; i++)
                        {
                            entry = context->ColumnSetList->Items[i];

                            if (PhEqualStringRef2(&entry->Name->sr, lvinfo->item.pszText, FALSE))
                            {
                                found = TRUE;
                                break;
                            }
                        }

                        if (!found && PhGetListViewItemParam(context->ListViewHandle, lvinfo->item.iItem, (PVOID *)&entry))
                        {
                            index = PhFindItemList(context->ColumnSetList, entry);

                            if (index != -1)
                            {
                                PhMoveReference(&entry->Name, PhCreateString(lvinfo->item.pszText));
                                ListView_SetItemText(context->ListViewHandle, lvinfo->item.iItem, 0, lvinfo->item.pszText);
                            }
                        }
                    }

                    context->LabelEditActive = FALSE;
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}
