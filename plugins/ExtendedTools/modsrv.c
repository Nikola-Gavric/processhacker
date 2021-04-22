/*
 * Process Hacker Extended Tools -
 *   services referencing module
 *
 * Copyright (C) 2010-2011 wj32
 * Copyright (C) 2018-2019 dmex
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

#include "exttools.h"
#include <subprocesstag.h>

typedef struct _MODULE_SERVICES_CONTEXT
{
    HWND ParentWindowHandle;
    HWND ServiceListHandle;
    HANDLE ProcessId;
    PPH_STRING ModuleName;
    PH_LAYOUT_MANAGER LayoutManager;
} MODULE_SERVICES_CONTEXT, *PMODULE_SERVICES_CONTEXT;

INT_PTR CALLBACK EtpModuleServicesDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

NTSTATUS EtpModuleServicesDialogThreadStart(
    _In_ PVOID Parameter
    )
{
    BOOL result;
    MSG message;
    HWND windowHandle;
    PH_AUTO_POOL autoPool;

    PhInitializeAutoPool(&autoPool);

    windowHandle = CreateDialogParam(
        PluginInstance->DllBase,
        MAKEINTRESOURCE(IDD_MODSERVICES),
        NULL,
        EtpModuleServicesDlgProc,
        (LPARAM)Parameter
        );

    ShowWindow(windowHandle, SW_SHOW);
    SetForegroundWindow(windowHandle);

    while (result = GetMessage(&message, NULL, 0, 0))
    {
        if (result == -1)
            break;

        if (!IsDialogMessage(windowHandle, &message))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        PhDrainAutoPool(&autoPool);
    }

    PhDeleteAutoPool(&autoPool);

    return STATUS_SUCCESS;
}

VOID EtShowModuleServicesDialog(
    _In_ HWND ParentWindowHandle,
    _In_ HANDLE ProcessId,
    _In_ PPH_STRING ModuleName
    )
{
    PMODULE_SERVICES_CONTEXT context;

    context = PhAllocateZero(sizeof(MODULE_SERVICES_CONTEXT));
    context->ParentWindowHandle = ParentWindowHandle;
    context->ProcessId = ProcessId;
    context->ModuleName = PhReferenceObject(ModuleName);

    PhCreateThread2(EtpModuleServicesDialogThreadStart, context);
}

ULONG PhpQueryModuleServiceReferences(
    _In_ HWND WindowHandle,
    _In_ PMODULE_SERVICES_CONTEXT Context,
    _Out_ PPH_LIST *ServiceList
    )
{
    ULONG win32Result;
    PQUERY_TAG_INFORMATION I_QueryTagInformation;
    TAG_INFO_NAMES_REFERENCING_MODULE namesReferencingModule;
    PPH_LIST serviceList;

    if (!(I_QueryTagInformation = PhGetModuleProcAddress(L"advapi32.dll", "I_QueryTagInformation")))
        return ERROR_SUCCESS;

    memset(&namesReferencingModule, 0, sizeof(TAG_INFO_NAMES_REFERENCING_MODULE));
    namesReferencingModule.InParams.dwPid = HandleToUlong(Context->ProcessId);
    namesReferencingModule.InParams.pszModule = PhGetString(Context->ModuleName);

    win32Result = I_QueryTagInformation(NULL, eTagInfoLevelNamesReferencingModule, &namesReferencingModule);

    if (win32Result == ERROR_NO_MORE_ITEMS)
        win32Result = ERROR_SUCCESS;

    if (win32Result != ERROR_SUCCESS)
        return win32Result;

    serviceList = PhCreateList(16);

    if (namesReferencingModule.OutParams.pmszNames)
    {
        PPH_SERVICE_ITEM serviceItem;
        PWSTR serviceName;
        ULONG nameLength;

        serviceName = namesReferencingModule.OutParams.pmszNames;

        while (TRUE)
        {
            nameLength = (ULONG)PhCountStringZ(serviceName);

            if (nameLength == 0)
                break;

            if (serviceItem = PhReferenceServiceItem(serviceName))
                PhAddItemList(serviceList, serviceItem);

            serviceName += nameLength + 1;
        }

        LocalFree(namesReferencingModule.OutParams.pmszNames);
    }

    *ServiceList = serviceList;

    //if (serviceList->Count == 0)
    //{
    //    PhShowInformation2(GetParent(WindowHandle), L"", L"This module was not referenced by a service.");
    //    EndDialog(WindowHandle, IDCANCEL);
    //    return NULL;
    //}

    return win32Result;
}

INT_PTR CALLBACK EtpModuleServicesDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PMODULE_SERVICES_CONTEXT context = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        context = (PMODULE_SERVICES_CONTEXT)lParam;
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
            ULONG win32Result;
            PPH_LIST serviceList;
            PPH_SERVICE_ITEM *serviceItems;
            RECT rect;

            if ((win32Result = PhpQueryModuleServiceReferences(hwndDlg, context, &serviceList)) != STATUS_SUCCESS)
            {
                PhShowStatus(
                    context->ParentWindowHandle,
                    L"Unable to query module references.", 0, win32Result
                    );
                DestroyWindow(hwndDlg);
                return FALSE;
            }

            SendMessage(hwndDlg, WM_SETICON, ICON_SMALL, (LPARAM)PH_LOAD_SHARED_ICON_SMALL(PhInstanceHandle, MAKEINTRESOURCE(PHAPP_IDI_PROCESSHACKER)));
            SendMessage(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM)PH_LOAD_SHARED_ICON_LARGE(PhInstanceHandle, MAKEINTRESOURCE(PHAPP_IDI_PROCESSHACKER)));

            serviceItems = PhAllocateCopy(serviceList->Items, serviceList->Count * sizeof(PPH_SERVICE_ITEM));
            context->ServiceListHandle = PhCreateServiceListControl(hwndDlg, serviceItems, serviceList->Count);
            SendMessage(context->ServiceListHandle, WM_PH_SET_LIST_VIEW_SETTINGS, 0, (LPARAM)SETTING_NAME_MODULE_SERVICES_COLUMNS);
            PhDereferenceObject(serviceList);

            {
                PPH_PROCESS_ITEM processItem;
                PPH_STRING message;

                if (processItem = PhReferenceProcessItem(context->ProcessId))
                {
                    message = PhFormatString(
                        L"Services referencing %s in %s (%lu):",
                        PhGetString(context->ModuleName),
                        PhGetStringOrEmpty(processItem->ProcessName),
                        HandleToUlong(processItem->ProcessId)
                        );
                    PhDereferenceObject(processItem);
                }
                else
                {
                    message = PhFormatString(L"Services referencing %s:", PhGetString(context->ModuleName));
                }

                PhSetDialogItemText(hwndDlg, IDC_MESSAGE, message->Buffer);
                PhDereferenceObject(message);
            }

            // Position the control.
            GetWindowRect(GetDlgItem(hwndDlg, IDC_SERVICES_LAYOUT), &rect);
            MapWindowPoints(NULL, hwndDlg, (POINT *)&rect, 2);
            MoveWindow(context->ServiceListHandle, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, FALSE);
            ShowWindow(context->ServiceListHandle, SW_SHOW);

            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_SERVICES_LAYOUT), NULL, PH_ANCHOR_ALL);
            PhAddLayoutItem(&context->LayoutManager, context->ServiceListHandle, NULL, PH_ANCHOR_ALL);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDOK), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_RIGHT);

            if (PhGetIntegerPairSetting(SETTING_NAME_MODULE_SERVICES_WINDOW_POSITION).X != 0)
                PhLoadWindowPlacementFromSetting(SETTING_NAME_MODULE_SERVICES_WINDOW_POSITION, SETTING_NAME_MODULE_SERVICES_WINDOW_SIZE, hwndDlg);
            else
                PhCenterWindow(hwndDlg, GetParent(hwndDlg));

            PhInitializeWindowTheme(hwndDlg, !!PhGetIntegerSetting(L"EnableThemeSupport"));
        }
        break;
    case WM_DESTROY:
        {
            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);

            if (context->LayoutManager.List) // HACK (dmex)
                PhDeleteLayoutManager(&context->LayoutManager);

            PhDereferenceObject(context->ModuleName);
            PhFree(context);

            PostQuitMessage(0);
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
            case IDOK:
                {
                    // NOTE: Don't save placement during WM_DESTROY since the dialog won't be created after an error querying service references. (dmex)
                    PhSaveWindowPlacementToSetting(SETTING_NAME_MODULE_SERVICES_WINDOW_POSITION, SETTING_NAME_MODULE_SERVICES_WINDOW_SIZE, hwndDlg);

                    DestroyWindow(hwndDlg);
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}
