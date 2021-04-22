/*
 * Process Hacker Extended Services -
 *   main program
 *
 * Copyright (C) 2010-2015 wj32
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

PPH_PLUGIN PluginInstance;
PH_CALLBACK_REGISTRATION PluginMenuItemCallbackRegistration;
PH_CALLBACK_REGISTRATION ProcessMenuInitializingCallbackRegistration;
PH_CALLBACK_REGISTRATION ServicePropertiesInitializingCallbackRegistration;
PH_CALLBACK_REGISTRATION ServiceMenuInitializingCallbackRegistration;

VOID NTAPI MenuItemCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_MENU_ITEM menuItem = Parameter;

    switch (menuItem->Id)
    {
    case ID_SERVICE_GOTOSERVICE:
        {
            ProcessHacker_SelectTabPage(PhMainWndHandle, 1);
            ProcessHacker_SelectServiceItem(PhMainWndHandle, (PPH_SERVICE_ITEM)menuItem->Context);
        }
        break;
    case ID_SERVICE_START:
        {
            PhUiStartService(PhMainWndHandle, (PPH_SERVICE_ITEM)menuItem->Context);
        }
        break;
    case ID_SERVICE_CONTINUE:
        {
            PhUiContinueService(PhMainWndHandle, (PPH_SERVICE_ITEM)menuItem->Context);
        }
        break;
    case ID_SERVICE_PAUSE:
        {
            PhUiPauseService(PhMainWndHandle, (PPH_SERVICE_ITEM)menuItem->Context);
        }
        break;
    case ID_SERVICE_STOP:
        {
            PhUiStopService(PhMainWndHandle, (PPH_SERVICE_ITEM)menuItem->Context);
        }
        break;
    case ID_SERVICE_RESTART:
        {
            PPH_SERVICE_ITEM serviceItem = menuItem->Context;
            SC_HANDLE serviceHandle;
            ULONG win32Result = ERROR_SUCCESS;

            if (serviceHandle = PhOpenService(serviceItem->Name->Buffer, SERVICE_QUERY_STATUS))
            {
                EsRestartServiceWithProgress(PhMainWndHandle, serviceItem, serviceHandle);
                CloseServiceHandle(serviceHandle);
            }
            else
            {
                win32Result = GetLastError();
            }

            if (win32Result != ERROR_SUCCESS)
            {
                PhShowStatus(
                    PhMainWndHandle,
                    PhaFormatString(L"Unable to restart %s", serviceItem->Name->Buffer)->Buffer,
                    0,
                    win32Result
                    );
            }
        }
        break;
    }
}

static int __cdecl ServiceForServicesMenuCompare(
    _In_ const void *elem1,
    _In_ const void *elem2
    )
{
    PPH_SERVICE_ITEM serviceItem1 = *(PPH_SERVICE_ITEM *)elem1;
    PPH_SERVICE_ITEM serviceItem2 = *(PPH_SERVICE_ITEM *)elem2;

    return PhCompareString(serviceItem1->Name, serviceItem2->Name, TRUE);
}

VOID NTAPI ProcessMenuInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_MENU_INFORMATION menuInfo = Parameter;

    if (
        PhGetIntegerSetting(SETTING_NAME_ENABLE_SERVICES_MENU) &&
        menuInfo->u.Process.NumberOfProcesses == 1 &&
        menuInfo->u.Process.Processes[0]->ServiceList &&
        menuInfo->u.Process.Processes[0]->ServiceList->Count != 0
        )
    {
        PPH_PROCESS_ITEM processItem;
        PPH_EMENU_ITEM servicesMenuItem = NULL;
        ULONG enumerationKey;
        PPH_SERVICE_ITEM serviceItem;
        PPH_LIST serviceList;
        ULONG i;
        PPH_EMENU_ITEM priorityMenuItem;
        ULONG insertIndex;

        processItem = menuInfo->u.Process.Processes[0];

        // Create a service list so we can sort it.

        serviceList = PH_AUTO(PhCreateList(processItem->ServiceList->Count));
        enumerationKey = 0;

        PhAcquireQueuedLockShared(&processItem->ServiceListLock);

        while (PhEnumPointerList(processItem->ServiceList, &enumerationKey, &serviceItem))
        {
            PhReferenceObject(serviceItem);
            // We need to use the service item when the user chooses a menu item.
            PH_AUTO(serviceItem);
            PhAddItemList(serviceList, serviceItem);
        }

        PhReleaseQueuedLockShared(&processItem->ServiceListLock);

        // Sort the service list.
        qsort(serviceList->Items, serviceList->Count, sizeof(PPH_SERVICE_ITEM), ServiceForServicesMenuCompare);

        // If there is only one service:
        // * We use the text "Service (Xxx)".
        // * There are no extra submenus.
        if (serviceList->Count != 1)
        {
            servicesMenuItem = PhPluginCreateEMenuItem(PluginInstance, 0, 0, L"Ser&vices", NULL);
        }

        // Create and add a menu item for each service.

        for (i = 0; i < serviceList->Count; i++)
        {
            PPH_STRING escapedName;
            PPH_EMENU_ITEM serviceMenuItem;
            PPH_EMENU_ITEM startMenuItem;
            PPH_EMENU_ITEM continueMenuItem;
            PPH_EMENU_ITEM pauseMenuItem;
            PPH_EMENU_ITEM stopMenuItem;

            serviceItem = serviceList->Items[i];
            escapedName = PH_AUTO(PhEscapeStringForMenuPrefix(&serviceItem->Name->sr));

            if (serviceList->Count == 1)
            {
                // "Service (Xxx)"
                escapedName = PhaFormatString(L"Ser&vice (%s)", escapedName->Buffer);
            }

            serviceMenuItem = PhPluginCreateEMenuItem(PluginInstance, 0, 0, escapedName->Buffer, NULL);

            if (serviceList->Count == 1)
            {
                // Make this the root submenu that we will insert.
                servicesMenuItem = serviceMenuItem;
            }

            PhInsertEMenuItem(serviceMenuItem, PhPluginCreateEMenuItem(PluginInstance, 0, ID_SERVICE_GOTOSERVICE, L"&Go to service", serviceItem), ULONG_MAX);
            PhInsertEMenuItem(serviceMenuItem, startMenuItem = PhPluginCreateEMenuItem(PluginInstance, 0, ID_SERVICE_START, L"Sta&rt", serviceItem), ULONG_MAX);
            PhInsertEMenuItem(serviceMenuItem, continueMenuItem = PhPluginCreateEMenuItem(PluginInstance, 0, ID_SERVICE_CONTINUE, L"&Continue", serviceItem), ULONG_MAX);
            PhInsertEMenuItem(serviceMenuItem, pauseMenuItem = PhPluginCreateEMenuItem(PluginInstance, 0, ID_SERVICE_PAUSE, L"&Pause", serviceItem), ULONG_MAX);
            PhInsertEMenuItem(serviceMenuItem, stopMenuItem = PhPluginCreateEMenuItem(PluginInstance, 0, ID_SERVICE_STOP, L"St&op", serviceItem), ULONG_MAX);

            // Massive copy and paste from mainwnd.c.
            // == START ==

#define SET_MENU_ITEM_ENABLED(MenuItem, Enabled) if (!(Enabled)) (MenuItem)->Flags |= PH_EMENU_DISABLED;

            switch (serviceItem->State)
            {
            case SERVICE_RUNNING:
                {
                    SET_MENU_ITEM_ENABLED(startMenuItem, FALSE);
                    SET_MENU_ITEM_ENABLED(continueMenuItem, FALSE);
                    SET_MENU_ITEM_ENABLED(pauseMenuItem,
                        serviceItem->ControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE);
                    SET_MENU_ITEM_ENABLED(stopMenuItem,
                        serviceItem->ControlsAccepted & SERVICE_ACCEPT_STOP);
                }
                break;
            case SERVICE_PAUSED:
                {
                    SET_MENU_ITEM_ENABLED(startMenuItem, FALSE);
                    SET_MENU_ITEM_ENABLED(continueMenuItem,
                        serviceItem->ControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE);
                    SET_MENU_ITEM_ENABLED(pauseMenuItem, FALSE);
                    SET_MENU_ITEM_ENABLED(stopMenuItem,
                        serviceItem->ControlsAccepted & SERVICE_ACCEPT_STOP);
                }
                break;
            case SERVICE_STOPPED:
                {
                    SET_MENU_ITEM_ENABLED(continueMenuItem, FALSE);
                    SET_MENU_ITEM_ENABLED(pauseMenuItem, FALSE);
                    SET_MENU_ITEM_ENABLED(stopMenuItem, FALSE);
                }
                break;
            case SERVICE_START_PENDING:
            case SERVICE_CONTINUE_PENDING:
            case SERVICE_PAUSE_PENDING:
            case SERVICE_STOP_PENDING:
                {
                    SET_MENU_ITEM_ENABLED(startMenuItem, FALSE);
                    SET_MENU_ITEM_ENABLED(continueMenuItem, FALSE);
                    SET_MENU_ITEM_ENABLED(pauseMenuItem, FALSE);
                    SET_MENU_ITEM_ENABLED(stopMenuItem, FALSE);
                }
                break;
            }

            if (!(serviceItem->ControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE))
            {
                PhDestroyEMenuItem(continueMenuItem);
                PhDestroyEMenuItem(pauseMenuItem);
            }

            // == END ==

            if (serviceList->Count != 1)
                PhInsertEMenuItem(servicesMenuItem, serviceMenuItem, ULONG_MAX);
        }

        // Insert our Services menu after the I/O Priority menu.

        priorityMenuItem = PhFindEMenuItem(menuInfo->Menu, 0, NULL, PHAPP_ID_PROCESS_IOPRIORITY);

        if (!priorityMenuItem)
            priorityMenuItem = PhFindEMenuItem(menuInfo->Menu, 0, NULL, PHAPP_ID_PROCESS_PRIORITY);

        if (priorityMenuItem)
            insertIndex = PhIndexOfEMenuItem(menuInfo->Menu, priorityMenuItem) + 1;
        else
            insertIndex = 0;

        PhInsertEMenuItem(menuInfo->Menu, servicesMenuItem, insertIndex);
    }
}

VOID NTAPI ServicePropertiesInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_OBJECT_PROPERTIES objectProperties = Parameter;
    PROPSHEETPAGE propSheetPage;
    PPH_SERVICE_ITEM serviceItem;

    if (!objectProperties)
        return;

    serviceItem = objectProperties->Parameter;

    // Recovery
    if (objectProperties->NumberOfPages < objectProperties->MaximumNumberOfPages)
    {
        memset(&propSheetPage, 0, sizeof(PROPSHEETPAGE));
        propSheetPage.dwSize = sizeof(PROPSHEETPAGE);
        propSheetPage.hInstance = PluginInstance->DllBase;
        propSheetPage.lParam = (LPARAM)serviceItem;

        if (!(serviceItem->Flags & SERVICE_RUNS_IN_SYSTEM_PROCESS))
        {
            propSheetPage.pszTemplate = MAKEINTRESOURCE(IDD_SRVRECOVERY);
            propSheetPage.pfnDlgProc = EspServiceRecoveryDlgProc;
        }
        else
        {
            // Services which run in system processes don't support failure actions.
            // Create a different page with a message saying this.
            propSheetPage.pszTemplate = MAKEINTRESOURCE(IDD_SRVRECOVERY2);
            propSheetPage.pfnDlgProc = EspServiceRecovery2DlgProc;
        }

        objectProperties->Pages[objectProperties->NumberOfPages++] = CreatePropertySheetPage(&propSheetPage);
    }

    // Dependencies
    if (objectProperties->NumberOfPages < objectProperties->MaximumNumberOfPages)
    {
        memset(&propSheetPage, 0, sizeof(PROPSHEETPAGE));
        propSheetPage.dwSize = sizeof(PROPSHEETPAGE);
        propSheetPage.dwFlags = PSP_USETITLE;
        propSheetPage.hInstance = PluginInstance->DllBase;
        propSheetPage.pszTemplate = MAKEINTRESOURCE(IDD_SRVLIST);
        propSheetPage.pszTitle = L"Dependencies";
        propSheetPage.pfnDlgProc = EspServiceDependenciesDlgProc;
        propSheetPage.lParam = (LPARAM)serviceItem;
        objectProperties->Pages[objectProperties->NumberOfPages++] = CreatePropertySheetPage(&propSheetPage);
    }

    // Dependents
    if (objectProperties->NumberOfPages < objectProperties->MaximumNumberOfPages)
    {
        memset(&propSheetPage, 0, sizeof(PROPSHEETPAGE));
        propSheetPage.dwSize = sizeof(PROPSHEETPAGE);
        propSheetPage.dwFlags = PSP_USETITLE;
        propSheetPage.hInstance = PluginInstance->DllBase;
        propSheetPage.pszTemplate = MAKEINTRESOURCE(IDD_SRVLIST);
        propSheetPage.pszTitle = L"Dependents";
        propSheetPage.pfnDlgProc = EspServiceDependentsDlgProc;
        propSheetPage.lParam = (LPARAM)serviceItem;
        objectProperties->Pages[objectProperties->NumberOfPages++] = CreatePropertySheetPage(&propSheetPage);
    }

    // Other
    if (objectProperties->NumberOfPages < objectProperties->MaximumNumberOfPages)
    {
        memset(&propSheetPage, 0, sizeof(PROPSHEETPAGE));
        propSheetPage.dwSize = sizeof(PROPSHEETPAGE);
        propSheetPage.dwFlags = PSP_USETITLE;
        propSheetPage.hInstance = PluginInstance->DllBase;
        propSheetPage.pszTemplate = MAKEINTRESOURCE(IDD_SRVTRIGGERS);
        propSheetPage.pszTitle = L"Triggers";
        propSheetPage.pfnDlgProc = EspServiceTriggersDlgProc;
        propSheetPage.lParam = (LPARAM)serviceItem;
        objectProperties->Pages[objectProperties->NumberOfPages++] = CreatePropertySheetPage(&propSheetPage);
    }

    // Other
    if (objectProperties->NumberOfPages < objectProperties->MaximumNumberOfPages)
    {
        memset(&propSheetPage, 0, sizeof(PROPSHEETPAGE));
        propSheetPage.dwSize = sizeof(PROPSHEETPAGE);
        propSheetPage.dwFlags = PSP_USETITLE;
        propSheetPage.hInstance = PluginInstance->DllBase;
        propSheetPage.pszTemplate = MAKEINTRESOURCE(IDD_SRVOTHER);
        propSheetPage.pszTitle = L"Other";
        propSheetPage.pfnDlgProc = EspServiceOtherDlgProc;
        propSheetPage.lParam = (LPARAM)serviceItem;
        objectProperties->Pages[objectProperties->NumberOfPages++] = CreatePropertySheetPage(&propSheetPage);
    }
}

VOID NTAPI ServiceMenuInitializingCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGIN_MENU_INFORMATION menuInfo = Parameter;
    PPH_EMENU_ITEM menuItem;
    ULONG indexOfMenuItem;

    if (!menuInfo)
        return;

    if (
        menuInfo->u.Service.NumberOfServices == 1 &&
        (menuInfo->u.Service.Services[0]->State == SERVICE_RUNNING || menuInfo->u.Service.Services[0]->State == SERVICE_PAUSED)
        )
    {
        // Insert our Restart menu item after the Stop menu item.

        menuItem = PhFindEMenuItem(menuInfo->Menu, 0, NULL, PHAPP_ID_SERVICE_STOP);

        if (menuItem)
            indexOfMenuItem = PhIndexOfEMenuItem(menuInfo->Menu, menuItem) + 1;
        else
            indexOfMenuItem = ULONG_MAX;

        PhInsertEMenuItem(
            menuInfo->Menu,
            PhPluginCreateEMenuItem(PluginInstance, 0, ID_SERVICE_RESTART, L"R&estart", menuInfo->u.Service.Services[0]),
            indexOfMenuItem
            );
    }
}

LOGICAL DllMain(
    _In_ HINSTANCE Instance,
    _In_ ULONG Reason,
    _Reserved_ PVOID Reserved
    )
{
    switch (Reason)
    {
    case DLL_PROCESS_ATTACH:
        {
            PPH_PLUGIN_INFORMATION info;
            PH_SETTING_CREATE settings[] =
            {
                { IntegerSettingType, SETTING_NAME_ENABLE_SERVICES_MENU, L"1" }
            };

            PluginInstance = PhRegisterPlugin(PLUGIN_NAME, Instance, &info);

            if (!PluginInstance)
                return FALSE;

            info->DisplayName = L"Extended Services";
            info->Author = L"wj32";
            info->Description = L"Extends service management capabilities.";
            info->Url = L"https://wj32.org/processhacker/forums/viewtopic.php?t=1113";

            PhRegisterCallback(
                PhGetPluginCallback(PluginInstance, PluginCallbackMenuItem),
                MenuItemCallback,
                NULL,
                &PluginMenuItemCallbackRegistration
                );

            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackProcessMenuInitializing),
                ProcessMenuInitializingCallback,
                NULL,
                &ProcessMenuInitializingCallbackRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackServicePropertiesInitializing),
                ServicePropertiesInitializingCallback,
                NULL,
                &ServicePropertiesInitializingCallbackRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackServiceMenuInitializing),
                ServiceMenuInitializingCallback,
                NULL,
                &ServiceMenuInitializingCallbackRegistration
                );

            PhAddSettings(settings, ARRAYSIZE(settings));
        }
        break;
    }

    return TRUE;
}
