/*
 * Process Hacker Plugins -
 *   Update Checker Plugin
 *
 * Copyright (C) 2011-2020 dmex
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

INT_PTR CALLBACK OptionsDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            if (PhGetIntegerSetting(SETTING_NAME_AUTO_CHECK))
                Button_SetCheck(GetDlgItem(hwndDlg, IDC_AUTOCHECKBOX), BST_CHECKED);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDC_AUTOCHECKBOX:
                {
                    PhSetIntegerSetting(SETTING_NAME_AUTO_CHECK,
                        Button_GetCheck(GetDlgItem(hwndDlg, IDC_AUTOCHECKBOX)) == BST_CHECKED);
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}

typedef struct _PH_UPDATER_COMMIT_ENTRY
{
    PPH_STRING CommitHashString;
    PPH_STRING CommitUrlString;
    PPH_STRING CommitMessageString;
    PPH_STRING CommitAuthorString;
    PPH_STRING CommitDateString;
    UINT64 CommitMessageCommentCount;
} PH_UPDATER_COMMIT_ENTRY, *PPH_UPDATER_COMMIT_ENTRY;

PPH_STRING TrimString(
    _In_ PPH_STRING String
    )
{
    static PH_STRINGREF whitespace = PH_STRINGREF_INIT(L"  ");
    PH_STRINGREF sr = String->sr;
    PhTrimStringRef(&sr, &whitespace, 0);
    return PhCreateString2(&sr);
}

_Success_(return)
BOOLEAN PhpUpdaterExtractCoAuthorName(
    _In_ PPH_STRING CommitMessage,
    _Out_opt_ PPH_STRING* CommitCoAuthorName
    )
{
    ULONG_PTR authoredByNameIndex;
    ULONG_PTR authoredByNameLength;
    PPH_STRING authoredByName;

    // Co-Authored-By: NAME <EMAIL>

    if ((authoredByNameIndex = PhFindStringInString(CommitMessage, 0, L"Co-Authored-By:")) == -1)
        return FALSE;
    if ((authoredByNameLength = PhFindStringInString(CommitMessage, authoredByNameIndex, L" <")) == -1)
        return FALSE;
    if ((authoredByNameLength = authoredByNameLength - authoredByNameIndex) == 0)
        return FALSE;

    authoredByName = PhSubstring(
        CommitMessage,
        authoredByNameIndex + wcslen(L"Co-Authored-By:"),
        (ULONG)authoredByNameLength - wcslen(L"Co-Authored-By:")
        );

    if (CommitCoAuthorName)
        *CommitCoAuthorName = TrimString(authoredByName);

    PhDereferenceObject(authoredByName);
    return TRUE;
}

PPH_LIST PhpUpdaterQueryCommitHistory(
    VOID
    )
{
    PPH_LIST results = NULL;
    PPH_BYTES jsonString = NULL;
    PPH_STRING versionString = NULL;
    PPH_STRING userAgentString = NULL;
    PPH_HTTP_CONTEXT httpContext = NULL;
    PVOID jsonRootObject = NULL;
    ULONG i;
    ULONG arrayLength;

    versionString = PhGetPhVersion();
    userAgentString = PhConcatStrings2(L"ProcessHacker_", versionString->Buffer);

    if (!PhHttpSocketCreate(&httpContext, PhGetString(userAgentString)))
        goto CleanupExit;

    if (!PhHttpSocketConnect(httpContext, L"api.github.com", PH_HTTP_DEFAULT_HTTPS_PORT))
        goto CleanupExit;

    if (!PhHttpSocketBeginRequest(
        httpContext,
        NULL,
        L"/repos/processhacker/processhacker/commits",
        PH_HTTP_FLAG_REFRESH | PH_HTTP_FLAG_SECURE
        ))
    {
        goto CleanupExit;
    }

    if (!PhHttpSocketSendRequest(httpContext, NULL, 0))
        goto CleanupExit;

    if (!PhHttpSocketEndRequest(httpContext))
        goto CleanupExit;

    if (!(jsonString = PhHttpSocketDownloadString(httpContext, FALSE)))
        goto CleanupExit;

    if (!(jsonRootObject = PhCreateJsonParser(jsonString->Buffer)))
        goto CleanupExit;

    if (!(arrayLength = PhGetJsonArrayLength(jsonRootObject)))
        goto CleanupExit;

    if (arrayLength > 0)
    {
        results = PhCreateList(arrayLength);

        for (i = 0; i < arrayLength; i++)
        {
            PPH_UPDATER_COMMIT_ENTRY entry;
            PVOID jsonArrayObject;
            PVOID jsonCommitObject;
            PVOID jsonAuthorObject;

            if (!(jsonArrayObject = PhGetJsonArrayIndexObject(jsonRootObject, i)))
                continue;

            entry = PhAllocateZero(sizeof(PH_UPDATER_COMMIT_ENTRY));
            entry->CommitHashString = PhGetJsonValueAsString(jsonArrayObject, "sha");
            entry->CommitUrlString = PhGetJsonValueAsString(jsonArrayObject, "html_url");

            if (jsonCommitObject = PhGetJsonObject(jsonArrayObject, "commit"))
            {
                entry->CommitMessageString = PhGetJsonValueAsString(jsonCommitObject, "message");
                entry->CommitMessageCommentCount = PhGetJsonValueAsLong64(jsonCommitObject, "comment_count");

                if (jsonAuthorObject = PhGetJsonObject(jsonCommitObject, "author"))
                {
                    entry->CommitAuthorString = PhGetJsonValueAsString(jsonAuthorObject, "name");
                    entry->CommitDateString = PhGetJsonValueAsString(jsonAuthorObject, "date");
                }
            }

            //if (jsonAuthorObject = PhGetJsonObject(jsonArrayObject, "author"))
            //{
            //    entry->CommitAuthorString = PhGetJsonValueAsString(jsonAuthorObject, "login");
            //}

            if (!(
                PhIsNullOrEmptyString(entry->CommitHashString) &&
                PhIsNullOrEmptyString(entry->CommitUrlString) &&
                PhIsNullOrEmptyString(entry->CommitMessageString) &&
                PhIsNullOrEmptyString(entry->CommitAuthorString) &&
                PhIsNullOrEmptyString(entry->CommitDateString)
                ))
            {

            }

            if (!PhIsNullOrEmptyString(entry->CommitMessageString))
            {
                PH_STRING_BUILDER sb;
                PPH_STRING authorNameString = NULL;
                ULONG_PTR commitMessageAuthorIndex;

                PhInitializeStringBuilder(&sb, 0x100);

                for (SIZE_T i = 0; i < entry->CommitMessageString->Length / sizeof(WCHAR); i++)
                {
                    if (entry->CommitMessageString->Data[i] == L'\n')
                        PhAppendStringBuilder2(&sb, L" ");
                    else
                        PhAppendCharStringBuilder(&sb, entry->CommitMessageString->Data[i]);
                }

                PhMoveReference(&entry->CommitMessageString, PhFinalStringBuilderString(&sb));

                if (PhpUpdaterExtractCoAuthorName(
                    entry->CommitMessageString,
                    &authorNameString
                    ))
                {
                    PhMoveReference(&entry->CommitAuthorString, authorNameString);
                }

                if ((commitMessageAuthorIndex = PhFindStringInString(entry->CommitMessageString, 0, L"Co-Authored-By:")) != -1)
                {
                    PhMoveReference(
                        &entry->CommitMessageString,
                        PhSubstring(entry->CommitMessageString, 0, (ULONG)commitMessageAuthorIndex - 2) // minus space
                        );
                }
            }

            PhAddItemList(results, entry);
        }
    }

CleanupExit:

    if (httpContext)
        PhHttpSocketDestroy(httpContext);

    if (jsonRootObject)
        PhFreeJsonParser(jsonRootObject);

    PhClearReference(&jsonString);
    PhClearReference(&versionString);
    PhClearReference(&userAgentString);

    return results;
}

#define WM_UPDATER_COMMITS (WM_APP + 1)

typedef struct _PH_UPDATER_COMMIT_CONTEXT
{
    HWND WindowHandle;
    HWND ListViewHandle;
    HFONT ListViewBoldFont;
    PPH_STRING BuildMessage;
    PPH_STRING CommitHash;
    PH_LAYOUT_MANAGER LayoutManager;
} PH_UPDATER_COMMIT_CONTEXT, *PPH_UPDATER_COMMIT_CONTEXT;

PPH_STRING PhpUpdaterCommitStringToTime(
    _In_ PPH_STRING Time
    )
{
    PPH_STRING result = NULL;
    SYSTEMTIME time = { 0 };
    SYSTEMTIME localTime = { 0 };
    INT count;

    count = swscanf(
        PhGetString(Time),
        L"%hu-%hu-%huT%hu:%hu:%huZ",
        &time.wYear,
        &time.wMonth,
        &time.wDay,
        &time.wHour,
        &time.wMinute,
        &time.wSecond
        );

    if (count == 6)
    {
        if (SystemTimeToTzSpecificLocalTime(NULL, &time, &localTime))
        {
            //result = PhFormatDateTime(&localTime);
            result = PhFormatDate(&localTime, NULL);
        }
    }

    return result;
}

NTSTATUS NTAPI PhpUpdaterQueryCommitHistoryThread(
    _In_ PVOID ThreadParameter
    )
{
    HWND windowHandle = (HWND)ThreadParameter;
    PPH_LIST commentHistoryList;

    if (commentHistoryList = PhpUpdaterQueryCommitHistory())
    {
        SendMessage(windowHandle, WM_UPDATER_COMMITS, 0, (LPARAM)commentHistoryList);
    }

    return STATUS_SUCCESS;
}

VOID PhpUpdaterFreeListViewEntries(
    _In_ PPH_UPDATER_COMMIT_CONTEXT Context
    )
{
    ULONG index = ULONG_MAX;

    while ((index = PhFindListViewItemByFlags(
        Context->ListViewHandle,
        index,
        LVNI_ALL
        )) != ULONG_MAX)
    {
        PPH_UPDATER_COMMIT_ENTRY entry;

        if (PhGetListViewItemParam(Context->ListViewHandle, index, &entry))
        {
            if (entry->CommitHashString)
                PhDereferenceObject(entry->CommitHashString);
            if (entry->CommitUrlString)
                PhDereferenceObject(entry->CommitUrlString);
            if (entry->CommitMessageString)
                PhDereferenceObject(entry->CommitMessageString);
            if (entry->CommitAuthorString)
                PhDereferenceObject(entry->CommitAuthorString);
            if (entry->CommitDateString)
                PhDereferenceObject(entry->CommitDateString);

            PhFree(entry);
        }
    }
}

INT_PTR CALLBACK TextDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PPH_UPDATER_COMMIT_CONTEXT context = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        context = PhAllocateZero(sizeof(PH_UPDATER_COMMIT_CONTEXT));

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
            context->WindowHandle = hwndDlg;
            context->ListViewHandle = GetDlgItem(hwndDlg, IDC_COMMITS);
            context->ListViewBoldFont = PhDuplicateFontWithNewWeight(GetWindowFont(context->ListViewHandle), FW_BOLD);

            SendMessage(hwndDlg, WM_SETICON, ICON_SMALL, (LPARAM)PH_LOAD_SHARED_ICON_SMALL(PhInstanceHandle, MAKEINTRESOURCE(PHAPP_IDI_PROCESSHACKER)));
            SendMessage(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM)PH_LOAD_SHARED_ICON_LARGE(PhInstanceHandle, MAKEINTRESOURCE(PHAPP_IDI_PROCESSHACKER)));

            PhSetListViewStyle(context->ListViewHandle, FALSE, FALSE); // TRUE, TRUE (tooltips)
            PhSetControlTheme(context->ListViewHandle, L"explorer");
            PhAddListViewColumn(context->ListViewHandle, 0, 0, 0, LVCFMT_LEFT, 120, L"Date");
            PhAddListViewColumn(context->ListViewHandle, 1, 1, 1, LVCFMT_LEFT, 100, L"Author");
            PhAddListViewColumn(context->ListViewHandle, 2, 2, 2, LVCFMT_LEFT, 250, L"Comments");
            PhAddListViewColumn(context->ListViewHandle, 3, 3, 3, LVCFMT_LEFT, 100, L"Commit");
            PhSetExtendedListView(context->ListViewHandle);

            PhLoadListViewColumnsFromSetting(SETTING_NAME_CHANGELOG_COLUMNS, context->ListViewHandle);
            PhLoadListViewSortColumnsFromSetting(SETTING_NAME_CHANGELOG_SORTCOLUMN, context->ListViewHandle);

            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);
            PhAddLayoutItem(&context->LayoutManager, context->ListViewHandle, NULL, PH_ANCHOR_ALL);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDCANCEL), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_RIGHT);

            if (PhGetIntegerPairSetting(SETTING_NAME_CHANGELOG_WINDOW_POSITION).X != 0)
                PhLoadWindowPlacementFromSetting(SETTING_NAME_CHANGELOG_WINDOW_POSITION, SETTING_NAME_CHANGELOG_WINDOW_SIZE, hwndDlg);
            else
                PhCenterWindow(hwndDlg, GetParent(hwndDlg));

            PhSetDialogFocus(hwndDlg, GetDlgItem(hwndDlg, IDCANCEL));

            {
                context->CommitHash = PhGetPhVersionHash();

                if (PhIsNullOrEmptyString(context->CommitHash) || PhEqualString2(context->CommitHash, L"\"\"", TRUE))
                    PhMoveReference(&context->CommitHash, PhReferenceObject(((PPH_UPDATER_CONTEXT)lParam)->CommitHash)); // HACK
            }

            //PhSetWindowText(GetDlgItem(hwndDlg, IDC_TEXT), PhGetString(context->BuildMessage));
            PhCreateThread2(PhpUpdaterQueryCommitHistoryThread, hwndDlg);
        }
        break;
    case WM_DESTROY:
        {
            PhSaveListViewSortColumnsToSetting(SETTING_NAME_CHANGELOG_SORTCOLUMN, context->ListViewHandle);
            PhSaveListViewColumnsToSetting(SETTING_NAME_CHANGELOG_COLUMNS, context->ListViewHandle);
            PhSaveWindowPlacementToSetting(SETTING_NAME_CHANGELOG_WINDOW_POSITION, SETTING_NAME_CHANGELOG_WINDOW_SIZE, hwndDlg);

            PhDeleteLayoutManager(&context->LayoutManager);

            if (context->CommitHash)
                PhDereferenceObject(context->CommitHash);
            if (context->ListViewBoldFont)
                DeleteFont(context->ListViewBoldFont);

            PhpUpdaterFreeListViewEntries(context);

            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
            PhFree(context);
        }
        break;
    case WM_SIZE:
        {
            PhLayoutManagerLayout(&context->LayoutManager);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDCANCEL:
                EndDialog(hwndDlg, IDCANCEL);
                break;
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            switch (header->code)
            {
            //case LVN_GETINFOTIP:
            //    {
            //        NMLVGETINFOTIP* getInfoTip = (NMLVGETINFOTIP*)lParam;
            //        PH_STRINGREF tip;
            //        PhInitializeStringRefLongHint(&tip, L"Commit: Author: Author Date: Committer: Commit Date: Commit Message:");
            //        PhCopyListViewInfoTip(getInfoTip, &tip);
            //    }
            //    break;
            case NM_CUSTOMDRAW:
                {
                    if (header->hwndFrom == context->ListViewHandle)
                    {
                        LPNMLVCUSTOMDRAW customDraw = (LPNMLVCUSTOMDRAW)header;

                        switch (customDraw->nmcd.dwDrawStage)
                        {
                        case CDDS_PREPAINT:
                            {
                                SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
                                return CDRF_NOTIFYITEMDRAW;
                            }
                            break;
                        case CDDS_ITEMPREPAINT:
                            {
                                HFONT newFont = NULL;
                                PPH_STRING commitHash;
                                PPH_STRING shortCommitHash;

                                if (commitHash = PhGetListViewItemText(context->ListViewHandle, (INT)customDraw->nmcd.dwItemSpec, 3))
                                {
                                    if (shortCommitHash = PhSubstring(context->CommitHash, 0, 7))
                                    {
                                        if (PhEqualString(commitHash, shortCommitHash, TRUE))
                                        {
                                            newFont = context->ListViewBoldFont;
                                        }

                                        PhDereferenceObject(shortCommitHash);
                                    }

                                    PhDereferenceObject(commitHash);
                                }

                                if (newFont)
                                    SelectFont(customDraw->nmcd.hdc, newFont);

                                if (newFont)
                                {
                                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, CDRF_NEWFONT);
                                    return CDRF_NEWFONT;
                                }
                                else
                                {
                                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, CDRF_DODEFAULT);
                                    return CDRF_DODEFAULT;
                                }

                                //SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, CDRF_NOTIFYSUBITEMDRAW);
                                //return CDRF_NOTIFYSUBITEMDRAW;
                            }
                            break;
                        //case CDDS_SUBITEM | CDDS_ITEMPREPAINT:
                        //    {
                        //        if (customDraw->iSubItem == 1)
                        //        {
                        //            PPH_STRING commitAuthor;
                        //
                        //            if (commitAuthor = PhGetListViewItemText(context->ListViewHandle, (INT)customDraw->nmcd.dwItemSpec, 1))
                        //            {
                        //                if (PhEqualString2(commitAuthor, L"dmex", TRUE))
                        //                {
                        //                    customDraw->clrText = RGB(0xff, 0x0, 0x0);
                        //                }
                        //                else
                        //                {
                        //                    customDraw->clrText = RGB(0x0, 0x0, 0xff);
                        //                }
                        //
                        //                PhDereferenceObject(commitAuthor);
                        //            }
                        //        }
                        //        else
                        //        {
                        //            customDraw->clrText = RGB(0x0, 0x0, 0x0);
                        //        }
                        //
                        //        SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, CDRF_NEWFONT);
                        //        return CDRF_NEWFONT;
                        //    }
                        //    break;
                        }
                    }
                }
                break;
            }

            PhHandleListViewNotifyForCopy(lParam, context->ListViewHandle);
        }
        break;
    case WM_CONTEXTMENU:
        {
            if ((HWND)wParam == context->ListViewHandle)
            {
                POINT point;
                PPH_EMENU menu;
                PPH_EMENU item;
                PVOID* listviewItems;
                ULONG numberOfItems;

                point.x = GET_X_LPARAM(lParam);
                point.y = GET_Y_LPARAM(lParam);

                if (point.x == -1 && point.y == -1)
                    PhGetListViewContextMenuPoint((HWND)wParam, &point);

                PhGetSelectedListViewItemParams(context->ListViewHandle, &listviewItems, &numberOfItems);

                if (numberOfItems != 0)
                {
                    menu = PhCreateEMenu();
                    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, 1, L"View on Github", NULL, NULL), ULONG_MAX);
                    PhInsertEMenuItem(menu, PhCreateEMenuSeparator(), ULONG_MAX);
                    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, 2, L"&Copy", NULL, NULL), ULONG_MAX);
                    PhInsertCopyListViewEMenuItem(menu, 2, context->ListViewHandle);

                    item = PhShowEMenu(
                        menu,
                        hwndDlg,
                        PH_EMENU_SHOW_SEND_COMMAND | PH_EMENU_SHOW_LEFTRIGHT,
                        PH_ALIGN_LEFT | PH_ALIGN_TOP,
                        point.x,
                        point.y
                        );

                    if (item)
                    {
                        BOOLEAN handled = FALSE;

                        handled = PhHandleCopyListViewEMenuItem(item);

                        if (!handled)
                        {
                            switch (item->Id)
                            {
                            case 1:
                                {
                                    PPH_UPDATER_COMMIT_ENTRY entry;
                                    PPH_STRING commitHashUrl;
                                    INT lvItemIndex;

                                    lvItemIndex = ListView_GetNextItem(context->ListViewHandle, -1, LVNI_SELECTED);

                                    if (lvItemIndex != -1)
                                    {
                                        if (PhGetListViewItemParam(context->ListViewHandle, lvItemIndex, &entry))
                                        {
                                            if (commitHashUrl = PhConcatStrings2(
                                                L"https://github.com/processhacker/processhacker/commit/",
                                                PhGetString(entry->CommitHashString)
                                                ))
                                            {
                                                PhShellExecute(hwndDlg, PhGetString(commitHashUrl), NULL);
                                                PhDereferenceObject(commitHashUrl);
                                            }
                                        }
                                    }
                                }
                                break;
                            case 2:
                                PhCopyListView(context->ListViewHandle);
                                break;
                            }
                        }
                    }

                    PhDestroyEMenu(menu);
                }

                PhFree(listviewItems);
            }
        }
        break;
    case WM_UPDATER_COMMITS:
        {
            PPH_LIST commitList = (PPH_LIST)lParam;

            if (!commitList)
                break;

            ExtendedListView_SetRedraw(context->ListViewHandle, FALSE);

            for (ULONG i = 0; i < commitList->Count; i++)
            {
                PPH_UPDATER_COMMIT_ENTRY entry = commitList->Items[i];
                INT lvItemIndex;
                PPH_STRING shortCommitHash;
                PPH_STRING commitTimeString;

                commitTimeString = PhpUpdaterCommitStringToTime(entry->CommitDateString);
                lvItemIndex = PhAddListViewItem(context->ListViewHandle, MAXINT, PhGetStringOrEmpty(commitTimeString), entry);
                if (commitTimeString) PhDereferenceObject(commitTimeString);

                PhSetListViewSubItem(context->ListViewHandle, lvItemIndex, 1, PhGetStringOrEmpty(entry->CommitAuthorString));
                PhSetListViewSubItem(context->ListViewHandle, lvItemIndex, 2, PhGetStringOrEmpty(entry->CommitMessageString));

                if (shortCommitHash = PhSubstring(entry->CommitHashString, 0, 7))
                {
                    PhSetListViewSubItem(context->ListViewHandle, lvItemIndex, 3, PhGetStringOrEmpty(shortCommitHash));
                    PhDereferenceObject(shortCommitHash);
                }
            }

            ExtendedListView_SetRedraw(context->ListViewHandle, TRUE);

            PhDereferenceObject(commitList);
        }
        break;
    }

    REFLECT_MESSAGE_DLG(hwndDlg, context->ListViewHandle, uMsg, wParam, lParam);

    return FALSE;
}
