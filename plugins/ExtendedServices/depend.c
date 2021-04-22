/*
 * Process Hacker Extended Services -
 *   dependencies and dependents
 *
 * Copyright (C) 2010 wj32
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

typedef struct _SERVICE_LIST_CONTEXT
{
    HWND ServiceListHandle;
    PH_LAYOUT_MANAGER LayoutManager;
} SERVICE_LIST_CONTEXT, *PSERVICE_LIST_CONTEXT;

LPENUM_SERVICE_STATUS EsEnumDependentServices(
    _In_ SC_HANDLE ServiceHandle,
    _In_opt_ ULONG State,
    _Out_ PULONG Count
    )
{
    LOGICAL result;
    PVOID buffer;
    ULONG bufferSize;
    ULONG returnLength;
    ULONG servicesReturned;

    if (!State)
        State = SERVICE_STATE_ALL;

    bufferSize = 0x800;
    buffer = PhAllocate(bufferSize);

    if (!(result = EnumDependentServices(
        ServiceHandle,
        State,
        buffer,
        bufferSize,
        &returnLength,
        &servicesReturned
        )))
    {
        if (GetLastError() == ERROR_MORE_DATA)
        {
            PhFree(buffer);
            bufferSize = returnLength;
            buffer = PhAllocate(bufferSize);

            result = EnumDependentServices(
                ServiceHandle,
                State,
                buffer,
                bufferSize,
                &returnLength,
                &servicesReturned
                );
        }

        if (!result)
        {
            PhFree(buffer);
            return NULL;
        }
    }

    *Count = servicesReturned;

    return buffer;
}

VOID EspLayoutServiceListControl(
    _In_ HWND hwndDlg,
    _In_ HWND ServiceListHandle
    )
{
    RECT rect;

    GetWindowRect(GetDlgItem(hwndDlg, IDC_SERVICES_LAYOUT), &rect);
    MapWindowPoints(NULL, hwndDlg, (POINT *)&rect, 2);

    MoveWindow(
        ServiceListHandle,
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
        TRUE
        );
}

INT_PTR CALLBACK EspServiceDependenciesDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PSERVICE_LIST_CONTEXT context;

    if (uMsg == WM_INITDIALOG)
    {
        context = PhAllocate(sizeof(SERVICE_LIST_CONTEXT));
        memset(context, 0, sizeof(SERVICE_LIST_CONTEXT));

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
            LPPROPSHEETPAGE propSheetPage = (LPPROPSHEETPAGE)lParam;
            PPH_SERVICE_ITEM serviceItem = (PPH_SERVICE_ITEM)propSheetPage->lParam;
            HWND serviceListHandle;
            PPH_LIST serviceList;
            SC_HANDLE serviceHandle;
            ULONG win32Result = ERROR_SUCCESS;
            BOOLEAN success = FALSE;
            PPH_SERVICE_ITEM *services;

            PhSetDialogItemText(hwndDlg, IDC_MESSAGE, L"This service depends on the following services:");

            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_SERVICES_LAYOUT), NULL, PH_ANCHOR_ALL);

            if (serviceHandle = PhOpenService(serviceItem->Name->Buffer, SERVICE_QUERY_CONFIG))
            {
                LPQUERY_SERVICE_CONFIG serviceConfig;

                if (serviceConfig = PhGetServiceConfig(serviceHandle))
                {
                    PWSTR dependency;
                    PPH_SERVICE_ITEM dependencyService;

                    dependency = serviceConfig->lpDependencies;
                    serviceList = PH_AUTO(PhCreateList(8));
                    success = TRUE;

                    if (dependency)
                    {
                        ULONG dependencyLength;

                        while (TRUE)
                        {
                            dependencyLength = (ULONG)PhCountStringZ(dependency);

                            if (dependencyLength == 0)
                                break;

                            if (dependency[0] == SC_GROUP_IDENTIFIER)
                                goto ContinueLoop;

                            if (dependencyService = PhReferenceServiceItem(dependency))
                                PhAddItemList(serviceList, dependencyService);

ContinueLoop:
                            dependency += dependencyLength + 1;
                        }
                    }

                    services = PhAllocateCopy(serviceList->Items, sizeof(PPH_SERVICE_ITEM) * serviceList->Count);

                    serviceListHandle = PhCreateServiceListControl(hwndDlg, services, serviceList->Count);
                    context->ServiceListHandle = serviceListHandle;
                    EspLayoutServiceListControl(hwndDlg, serviceListHandle);
                    ShowWindow(serviceListHandle, SW_SHOW);

                    PhFree(serviceConfig);
                }
                else
                {
                    win32Result = GetLastError();
                }

                CloseServiceHandle(serviceHandle);
            }
            else
            {
                win32Result = GetLastError();
            }

            if (!success)
            {
                PPH_STRING errorMessage = PhGetWin32Message(win32Result);

                PhSetDialogItemText(hwndDlg, IDC_SERVICES_LAYOUT, PhaConcatStrings2(
                    L"Unable to enumerate dependencies: ",
                    PhGetStringOrDefault(errorMessage, L"Unknown error.")
                    )->Buffer);

                ShowWindow(GetDlgItem(hwndDlg, IDC_SERVICES_LAYOUT), SW_SHOW);
                PhClearReference(&errorMessage);
            }

            PhInitializeWindowTheme(hwndDlg, !!PhGetIntegerSetting(L"EnableThemeSupport"));
        }
        break;
    case WM_DESTROY:
        {
            PhDeleteLayoutManager(&context->LayoutManager);
            PhFree(context);
        }
        break;
    case WM_SIZE:
        {
            PhLayoutManagerLayout(&context->LayoutManager);

            if (context->ServiceListHandle)
                EspLayoutServiceListControl(hwndDlg, context->ServiceListHandle);
        }
        break;
    }

    return FALSE;
}

INT_PTR CALLBACK EspServiceDependentsDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PSERVICE_LIST_CONTEXT context;

    if (uMsg == WM_INITDIALOG)
    {
        context = PhAllocate(sizeof(SERVICE_LIST_CONTEXT));
        memset(context, 0, sizeof(SERVICE_LIST_CONTEXT));

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
            LPPROPSHEETPAGE propSheetPage = (LPPROPSHEETPAGE)lParam;
            PPH_SERVICE_ITEM serviceItem = (PPH_SERVICE_ITEM)propSheetPage->lParam;
            HWND serviceListHandle;
            PPH_LIST serviceList;
            SC_HANDLE serviceHandle;
            ULONG win32Result = ERROR_SUCCESS;
            BOOLEAN success = FALSE;
            PPH_SERVICE_ITEM *services;

            PhSetDialogItemText(hwndDlg, IDC_MESSAGE, L"The following services depend on this service:");

            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_SERVICES_LAYOUT), NULL, PH_ANCHOR_ALL);

            if (serviceHandle = PhOpenService(serviceItem->Name->Buffer, SERVICE_ENUMERATE_DEPENDENTS))
            {
                LPENUM_SERVICE_STATUS dependentServices;
                ULONG numberOfDependentServices;

                if (dependentServices = EsEnumDependentServices(serviceHandle, 0, &numberOfDependentServices))
                {
                    ULONG i;
                    PPH_SERVICE_ITEM dependentService;

                    serviceList = PH_AUTO(PhCreateList(8));
                    success = TRUE;

                    for (i = 0; i < numberOfDependentServices; i++)
                    {
                        if (dependentService = PhReferenceServiceItem(dependentServices[i].lpServiceName))
                            PhAddItemList(serviceList, dependentService);
                    }

                    services = PhAllocateCopy(serviceList->Items, sizeof(PPH_SERVICE_ITEM) * serviceList->Count);

                    serviceListHandle = PhCreateServiceListControl(hwndDlg, services, serviceList->Count);
                    context->ServiceListHandle = serviceListHandle;
                    EspLayoutServiceListControl(hwndDlg, serviceListHandle);
                    ShowWindow(serviceListHandle, SW_SHOW);

                    PhFree(dependentServices);
                }
                else
                {
                    win32Result = GetLastError();
                }

                CloseServiceHandle(serviceHandle);
            }
            else
            {
                win32Result = GetLastError();
            }

            if (!success)
            {
                PPH_STRING errorMessage = PhGetWin32Message(win32Result);

                PhSetDialogItemText(hwndDlg, IDC_SERVICES_LAYOUT, PhaConcatStrings2(
                    L"Unable to enumerate dependents: ",
                    PhGetStringOrDefault(errorMessage, L"Unknown error.")
                    )->Buffer);

                ShowWindow(GetDlgItem(hwndDlg, IDC_SERVICES_LAYOUT), SW_SHOW);
                PhClearReference(&errorMessage);
            }

            PhInitializeWindowTheme(hwndDlg, !!PhGetIntegerSetting(L"EnableThemeSupport"));
        }
        break;
    case WM_DESTROY:
        {
            PhDeleteLayoutManager(&context->LayoutManager);
            PhFree(context);
        }
        break;
    case WM_SIZE:
        {
            PhLayoutManagerLayout(&context->LayoutManager);

            if (context->ServiceListHandle)
                EspLayoutServiceListControl(hwndDlg, context->ServiceListHandle);
        }
        break;
    }

    return FALSE;
}
