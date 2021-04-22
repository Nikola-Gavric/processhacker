/*
 * Process Hacker Extended Services -
 *   triggers page
 *
 * Copyright (C) 2015 wj32
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

#include "extsrv.h"

typedef struct _SERVICE_TRIGGERS_CONTEXT
{
    PPH_SERVICE_ITEM ServiceItem;
    HWND TriggersLv;
    PH_LAYOUT_MANAGER LayoutManager;
    struct _ES_TRIGGER_CONTEXT *TriggerContext;
} SERVICE_TRIGGERS_CONTEXT, *PSERVICE_TRIGGERS_CONTEXT;

NTSTATUS EspLoadTriggerInfo(
    _In_ HWND hwndDlg,
    _In_ PSERVICE_TRIGGERS_CONTEXT Context
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    SC_HANDLE serviceHandle;

    if (!(serviceHandle = PhOpenService(Context->ServiceItem->Name->Buffer, SERVICE_QUERY_CONFIG)))
        return NTSTATUS_FROM_WIN32(GetLastError());

    EsLoadServiceTriggerInfo(Context->TriggerContext, serviceHandle);
    CloseServiceHandle(serviceHandle);

    return status;
}

INT_PTR CALLBACK EspServiceTriggersDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PSERVICE_TRIGGERS_CONTEXT context;

    if (uMsg == WM_INITDIALOG)
    {
        context = PhAllocate(sizeof(SERVICE_TRIGGERS_CONTEXT));
        memset(context, 0, sizeof(SERVICE_TRIGGERS_CONTEXT));

        PhSetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT, context);
    }
    else
    {
        context = PhGetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);

        if (uMsg == WM_DESTROY)
            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
    }

    if (!context)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            NTSTATUS status;
            LPPROPSHEETPAGE propSheetPage = (LPPROPSHEETPAGE)lParam;
            PPH_SERVICE_ITEM serviceItem = (PPH_SERVICE_ITEM)propSheetPage->lParam;
            HWND triggersLv;

            context->ServiceItem = serviceItem;
            context->TriggersLv = triggersLv = GetDlgItem(hwndDlg, IDC_TRIGGERS);
            context->TriggerContext = EsCreateServiceTriggerContext(
                context->ServiceItem,
                hwndDlg,
                triggersLv
                );

            status = EspLoadTriggerInfo(hwndDlg, context);

            if (!NT_SUCCESS(status))
            {
                PPH_STRING errorMessage = PhGetNtMessage(status);

                PhShowWarning(
                    hwndDlg,
                    L"Unable to query service trigger information: %s",
                    PhGetStringOrDefault(errorMessage, L"Unknown error.")
                    );

                PhClearReference(&errorMessage);
            }

            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_TRIGGERS), NULL, PH_ANCHOR_ALL);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_NEW), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_RIGHT);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_EDIT), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_RIGHT);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_DELETE), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_RIGHT);

            PhInitializeWindowTheme(hwndDlg, !!PhGetIntegerSetting(L"EnableThemeSupport"));
        }
        break;
    case WM_DESTROY:
        {
            PhDeleteLayoutManager(&context->LayoutManager);

            EsDestroyServiceTriggerContext(context->TriggerContext);
            PhFree(context);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDC_NEW:
                if (context->TriggerContext)
                    EsHandleEventServiceTrigger(context->TriggerContext, ES_TRIGGER_EVENT_NEW);
                break;
            case IDC_EDIT:
                if (context->TriggerContext)
                    EsHandleEventServiceTrigger(context->TriggerContext, ES_TRIGGER_EVENT_EDIT);
                break;
            case IDC_DELETE:
                if (context->TriggerContext)
                    EsHandleEventServiceTrigger(context->TriggerContext, ES_TRIGGER_EVENT_DELETE);
                break;
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            switch (header->code)
            {
            case PSN_KILLACTIVE:
                {
                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, FALSE);
                }
                return TRUE;
            case PSN_APPLY:
                {
                    ULONG win32Result = ERROR_SUCCESS;

                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, PSNRET_NOERROR);

                    if (!EsSaveServiceTriggerInfo(context->TriggerContext, &win32Result))
                    {
                        if (win32Result == ERROR_CANCELLED)
                        {
                            SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, PSNRET_INVALID);
                        }
                        else
                        {
                            PPH_STRING errorMessage = PhGetWin32Message(win32Result);

                            if (PhShowMessage(
                                hwndDlg,
                                MB_ICONERROR | MB_RETRYCANCEL,
                                L"Unable to change service trigger information: %s",
                                PhGetStringOrDefault(errorMessage, L"Unknown error.")
                                ) == IDRETRY)
                            {
                                SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, PSNRET_INVALID);
                            }

                            PhClearReference(&errorMessage);
                        }
                    }

                    return TRUE;
                }
                break;
            case LVN_ITEMCHANGED:
                {
                    if (header->hwndFrom == context->TriggersLv && context->TriggerContext)
                    {
                        EsHandleEventServiceTrigger(context->TriggerContext, ES_TRIGGER_EVENT_SELECTIONCHANGED);
                    }
                }
                break;
            case NM_DBLCLK:
                {
                    if (header->hwndFrom == context->TriggersLv && context->TriggerContext)
                    {
                        EsHandleEventServiceTrigger(context->TriggerContext, ES_TRIGGER_EVENT_EDIT);
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
