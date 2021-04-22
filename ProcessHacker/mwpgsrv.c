/*
 * Process Hacker -
 *   Main window: Services tab
 *
 * Copyright (C) 2009-2016 wj32
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
#include <mainwnd.h>

#include <shellapi.h>

#include <cpysave.h>
#include <emenu.h>

#include <phplug.h>
#include <settings.h>
#include <srvlist.h>
#include <srvprv.h>

#include <mainwndp.h>

PPH_MAIN_TAB_PAGE PhMwpServicesPage;
HWND PhMwpServiceTreeNewHandle;
PH_PROVIDER_EVENT_QUEUE PhMwpServiceEventQueue;

static PH_CALLBACK_REGISTRATION ServiceAddedRegistration;
static PH_CALLBACK_REGISTRATION ServiceModifiedRegistration;
static PH_CALLBACK_REGISTRATION ServiceRemovedRegistration;
static PH_CALLBACK_REGISTRATION ServicesUpdatedRegistration;

static BOOLEAN ServiceTreeListLoaded = FALSE;
static PPH_TN_FILTER_ENTRY DriverFilterEntry = NULL;

BOOLEAN PhMwpServicesPageCallback(
    _In_ struct _PH_MAIN_TAB_PAGE *Page,
    _In_ PH_MAIN_TAB_PAGE_MESSAGE Message,
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2
    )
{
    switch (Message)
    {
    case MainTabPageCreate:
        {
            PhMwpServicesPage = Page;

            PhInitializeProviderEventQueue(&PhMwpServiceEventQueue, 100);

            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackServiceProviderAddedEvent),
                PhMwpServiceAddedHandler,
                NULL,
                &ServiceAddedRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackServiceProviderModifiedEvent),
                PhMwpServiceModifiedHandler,
                NULL,
                &ServiceModifiedRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackServiceProviderRemovedEvent),
                PhMwpServiceRemovedHandler,
                NULL,
                &ServiceRemovedRegistration
                );
            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackServiceProviderUpdatedEvent),
                PhMwpServicesUpdatedHandler,
                NULL,
                &ServicesUpdatedRegistration
                );
        }
        break;
    case MainTabPageCreateWindow:
        {
            if (Parameter1)
            {
                *(HWND*)Parameter1 = PhMwpServiceTreeNewHandle;
            }
        }
        return TRUE;
    case MainTabPageSelected:
        {
            BOOLEAN selected = (BOOLEAN)Parameter1;

            if (selected)
                PhMwpNeedServiceTreeList();
        }
        break;
    case MainTabPageInitializeSectionMenuItems:
        {
            PPH_MAIN_TAB_PAGE_MENU_INFORMATION menuInfo = Parameter1;
            PPH_EMENU menu;
            ULONG startIndex;
            PPH_EMENU_ITEM menuItem;

            if (!menuInfo)
                break;

            menu = menuInfo->Menu;
            startIndex = menuInfo->StartIndex;

            PhInsertEMenuItem(menu, PhCreateEMenuItem(0, ID_VIEW_HIDEDRIVERSERVICES, L"&Hide driver services", NULL, NULL), startIndex);

            if (DriverFilterEntry && (menuItem = PhFindEMenuItem(menu, 0, NULL, ID_VIEW_HIDEDRIVERSERVICES)))
                menuItem->Flags |= PH_EMENU_CHECKED;
        }
        return TRUE;
    case MainTabPageLoadSettings:
        {
            if (PhGetIntegerSetting(L"HideDriverServices"))
                DriverFilterEntry = PhAddTreeNewFilter(PhGetFilterSupportServiceTreeList(), PhMwpDriverServiceTreeFilter, NULL);
        }
        return TRUE;
    case MainTabPageSaveSettings:
        {
            if (ServiceTreeListLoaded)
                PhSaveSettingsServiceTreeList();
        }
        return TRUE;
    case MainTabPageExportContent:
        {
            PPH_MAIN_TAB_PAGE_EXPORT_CONTENT exportContent = Parameter1;

            if (exportContent)
                PhWriteServiceList(exportContent->FileStream, exportContent->Mode);
        }
        return TRUE;
    case MainTabPageFontChanged:
        {
            HFONT font = (HFONT)Parameter1;

            SetWindowFont(PhMwpServiceTreeNewHandle, font, TRUE);
        }
        break;
    case MainTabPageUpdateAutomaticallyChanged:
        {
            BOOLEAN updateAutomatically = (BOOLEAN)Parameter1;

            PhSetEnabledProvider(&PhMwpServiceProviderRegistration, updateAutomatically);
        }
        break;
    }

    return FALSE;
}

VOID PhMwpNeedServiceTreeList(
    VOID
    )
{
    if (!ServiceTreeListLoaded)
    {
        ServiceTreeListLoaded = TRUE;
        PhLoadSettingsServiceTreeList();
    }
}

VOID PhMwpToggleDriverServiceTreeFilter(
    VOID
    )
{
    if (!DriverFilterEntry)
    {
        DriverFilterEntry = PhAddTreeNewFilter(PhGetFilterSupportServiceTreeList(), PhMwpDriverServiceTreeFilter, NULL);
    }
    else
    {
        PhRemoveTreeNewFilter(PhGetFilterSupportServiceTreeList(), DriverFilterEntry);
        DriverFilterEntry = NULL;
    }

    PhApplyTreeNewFilters(PhGetFilterSupportServiceTreeList());

    PhSetIntegerSetting(L"HideDriverServices", !!DriverFilterEntry);
}

BOOLEAN PhMwpDriverServiceTreeFilter(
    _In_ PPH_TREENEW_NODE Node,
    _In_opt_ PVOID Context
    )
{
    PPH_SERVICE_NODE serviceNode = (PPH_SERVICE_NODE)Node;

    if (serviceNode->ServiceItem->Type & SERVICE_DRIVER)
        return FALSE;

    return TRUE;
}

VOID PhMwpInitializeServiceMenu(
    _In_ PPH_EMENU Menu,
    _In_ PPH_SERVICE_ITEM *Services,
    _In_ ULONG NumberOfServices
    )
{
    if (NumberOfServices == 0)
    {
        PhSetFlagsAllEMenuItems(Menu, PH_EMENU_DISABLED, PH_EMENU_DISABLED);
    }
    else if (NumberOfServices == 1)
    {
        if (!Services[0]->ProcessId)
            PhEnableEMenuItem(Menu, ID_SERVICE_GOTOPROCESS, FALSE);
    }
    else
    {
        PhSetFlagsAllEMenuItems(Menu, PH_EMENU_DISABLED, PH_EMENU_DISABLED);
        PhEnableEMenuItem(Menu, ID_SERVICE_COPY, TRUE);
    }

    if (NumberOfServices == 1)
    {
        switch (Services[0]->State)
        {
        case SERVICE_RUNNING:
            {
                PhEnableEMenuItem(Menu, ID_SERVICE_START, FALSE);
                PhEnableEMenuItem(Menu, ID_SERVICE_CONTINUE, FALSE);
                PhEnableEMenuItem(Menu, ID_SERVICE_PAUSE,
                    Services[0]->ControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE);
                PhEnableEMenuItem(Menu, ID_SERVICE_STOP,
                    Services[0]->ControlsAccepted & SERVICE_ACCEPT_STOP);
            }
            break;
        case SERVICE_PAUSED:
            {
                PhEnableEMenuItem(Menu, ID_SERVICE_START, FALSE);
                PhEnableEMenuItem(Menu, ID_SERVICE_CONTINUE,
                    Services[0]->ControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE);
                PhEnableEMenuItem(Menu, ID_SERVICE_PAUSE, FALSE);
                PhEnableEMenuItem(Menu, ID_SERVICE_STOP,
                    Services[0]->ControlsAccepted & SERVICE_ACCEPT_STOP);
            }
            break;
        case SERVICE_STOPPED:
            {
                PhEnableEMenuItem(Menu, ID_SERVICE_CONTINUE, FALSE);
                PhEnableEMenuItem(Menu, ID_SERVICE_PAUSE, FALSE);
                PhEnableEMenuItem(Menu, ID_SERVICE_STOP, FALSE);
            }
            break;
        case SERVICE_START_PENDING:
        case SERVICE_CONTINUE_PENDING:
        case SERVICE_PAUSE_PENDING:
        case SERVICE_STOP_PENDING:
            {
                PhEnableEMenuItem(Menu, ID_SERVICE_START, FALSE);
                PhEnableEMenuItem(Menu, ID_SERVICE_CONTINUE, FALSE);
                PhEnableEMenuItem(Menu, ID_SERVICE_PAUSE, FALSE);
                PhEnableEMenuItem(Menu, ID_SERVICE_STOP, FALSE);
            }
            break;
        }

        if (!(Services[0]->ControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE))
        {
            PPH_EMENU_ITEM item;

            if (item = PhFindEMenuItem(Menu, 0, NULL, ID_SERVICE_CONTINUE))
                PhDestroyEMenuItem(item);
            if (item = PhFindEMenuItem(Menu, 0, NULL, ID_SERVICE_PAUSE))
                PhDestroyEMenuItem(item);
        }
    }
}

VOID PhShowServiceContextMenu(
    _In_ PPH_TREENEW_CONTEXT_MENU ContextMenu
    )
{
    PH_PLUGIN_MENU_INFORMATION menuInfo;
    PPH_SERVICE_ITEM *services;
    ULONG numberOfServices;

    PhGetSelectedServiceItems(&services, &numberOfServices);

    if (numberOfServices != 0)
    {
        PPH_EMENU menu;
        PPH_EMENU_ITEM item;

        menu = PhCreateEMenu();
        PhInsertEMenuItem(menu, PhCreateEMenuItem(0, ID_SERVICE_START, L"&Start", NULL, NULL), ULONG_MAX);
        PhInsertEMenuItem(menu, PhCreateEMenuItem(0, ID_SERVICE_CONTINUE, L"C&ontinue", NULL, NULL), ULONG_MAX);
        PhInsertEMenuItem(menu, PhCreateEMenuItem(0, ID_SERVICE_PAUSE, L"&Pause", NULL, NULL), ULONG_MAX);
        PhInsertEMenuItem(menu, PhCreateEMenuItem(0, ID_SERVICE_STOP, L"S&top", NULL, NULL), ULONG_MAX);
        PhInsertEMenuItem(menu, PhCreateEMenuItem(0, ID_SERVICE_DELETE, L"&Delete\bDel", NULL, NULL), ULONG_MAX);
        PhInsertEMenuItem(menu, PhCreateEMenuSeparator(), ULONG_MAX);
        PhInsertEMenuItem(menu, PhCreateEMenuItem(0, ID_SERVICE_GOTOPROCESS, L"&Go to process", NULL, NULL), ULONG_MAX);
        PhInsertEMenuItem(menu, PhCreateEMenuSeparator(), ULONG_MAX);
        PhInsertEMenuItem(menu, PhCreateEMenuItem(0, ID_SERVICE_OPENKEY, L"Open &key", NULL, NULL), ULONG_MAX);
        PhInsertEMenuItem(menu, PhCreateEMenuItem(0, ID_SERVICE_OPENFILELOCATION, L"Open &file location\bCtrl+Enter", NULL, NULL), ULONG_MAX);
        PhInsertEMenuItem(menu, PhCreateEMenuItem(0, ID_SERVICE_PROPERTIES, L"P&roperties\bEnter", NULL, NULL), ULONG_MAX);
        PhInsertEMenuItem(menu, PhCreateEMenuSeparator(), ULONG_MAX);
        PhInsertEMenuItem(menu, PhCreateEMenuItem(0, ID_SERVICE_COPY, L"&Copy\bCtrl+C", NULL, NULL), ULONG_MAX);
        PhSetFlagsEMenuItem(menu, ID_SERVICE_PROPERTIES, PH_EMENU_DEFAULT, PH_EMENU_DEFAULT);
        PhMwpInitializeServiceMenu(menu, services, numberOfServices);
        PhInsertCopyCellEMenuItem(menu, ID_SERVICE_COPY, PhMwpServiceTreeNewHandle, ContextMenu->Column);

        if (PhPluginsEnabled)
        {
            PhPluginInitializeMenuInfo(&menuInfo, menu, PhMainWndHandle, 0);
            menuInfo.u.Service.Services = services;
            menuInfo.u.Service.NumberOfServices = numberOfServices;

            PhInvokeCallback(PhGetGeneralCallback(GeneralCallbackServiceMenuInitializing), &menuInfo);
        }

        item = PhShowEMenu(
            menu,
            PhMainWndHandle,
            PH_EMENU_SHOW_LEFTRIGHT,
            PH_ALIGN_LEFT | PH_ALIGN_TOP,
            ContextMenu->Location.x,
            ContextMenu->Location.y
            );

        if (item)
        {
            BOOLEAN handled = FALSE;

            handled = PhHandleCopyCellEMenuItem(item);

            if (!handled && PhPluginsEnabled)
                handled = PhPluginTriggerEMenuItem(&menuInfo, item);

            if (!handled)
                SendMessage(PhMainWndHandle, WM_COMMAND, item->Id, 0);
        }

        PhDestroyEMenu(menu);
    }

    PhFree(services);
}

VOID NTAPI PhMwpServiceAddedHandler(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_SERVICE_ITEM serviceItem = (PPH_SERVICE_ITEM)Parameter;

    if (!serviceItem)
        return;

    PhReferenceObject(serviceItem);
    PhPushProviderEventQueue(&PhMwpServiceEventQueue, ProviderAddedEvent, Parameter, PhGetRunIdProvider(&PhMwpServiceProviderRegistration));
}

VOID NTAPI PhMwpServiceModifiedHandler(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_SERVICE_MODIFIED_DATA serviceModifiedData = (PPH_SERVICE_MODIFIED_DATA)Parameter;
    PPH_SERVICE_MODIFIED_DATA copy;

    if (!serviceModifiedData)
        return;

    copy = PhAllocateCopy(serviceModifiedData, sizeof(PH_SERVICE_MODIFIED_DATA));

    PhPushProviderEventQueue(&PhMwpServiceEventQueue, ProviderModifiedEvent, copy, PhGetRunIdProvider(&PhMwpServiceProviderRegistration));
}

VOID NTAPI PhMwpServiceRemovedHandler(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PPH_SERVICE_ITEM serviceItem = (PPH_SERVICE_ITEM)Parameter;

    PhPushProviderEventQueue(&PhMwpServiceEventQueue, ProviderRemovedEvent, Parameter, PhGetRunIdProvider(&PhMwpServiceProviderRegistration));
}

VOID NTAPI PhMwpServicesUpdatedHandler(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    ProcessHacker_Invoke(PhMainWndHandle, PhMwpOnServicesUpdated, PhGetRunIdProvider(&PhMwpServiceProviderRegistration));
}

VOID PhMwpOnServiceAdded(
    _In_ _Assume_refs_(1) PPH_SERVICE_ITEM ServiceItem,
    _In_ ULONG RunId
    )
{
    PPH_SERVICE_NODE serviceNode;

    serviceNode = PhAddServiceNode(ServiceItem, RunId);
    // ServiceItem dereferenced below

    if (RunId != 1)
    {
        PhLogServiceEntry(PH_LOG_ENTRY_SERVICE_CREATE, ServiceItem->Name, ServiceItem->DisplayName);

        if (PhMwpNotifyIconNotifyMask & PH_NOTIFY_SERVICE_CREATE)
        {
            if (!PhPluginsEnabled || !PhMwpPluginNotifyEvent(PH_NOTIFY_SERVICE_CREATE, ServiceItem))
            {
                PhMwpClearLastNotificationDetails();
                PhMwpLastNotificationType = PH_NOTIFY_SERVICE_CREATE;
                PhSwapReference(&PhMwpLastNotificationDetails.ServiceName, ServiceItem->Name);

                PhShowIconNotification(L"Service Created", PhaFormatString(
                    L"The service %s (%s) has been created.",
                    ServiceItem->Name->Buffer,
                    ServiceItem->DisplayName->Buffer
                    )->Buffer, NIIF_INFO);
            }
        }
    }

    PhDereferenceObject(ServiceItem);
}

VOID PhMwpOnServiceModified(
    _In_ PPH_SERVICE_MODIFIED_DATA ServiceModifiedData
    )
{
    PH_SERVICE_CHANGE serviceChange;
    UCHAR logEntryType;

    PhUpdateServiceNode(PhFindServiceNode(ServiceModifiedData->Service));

    if (DriverFilterEntry)
        PhApplyTreeNewFilters(PhGetFilterSupportServiceTreeList());

    serviceChange = PhGetServiceChange(ServiceModifiedData);

    switch (serviceChange)
    {
    case ServiceStarted:
        logEntryType = PH_LOG_ENTRY_SERVICE_START;
        break;
    case ServiceStopped:
        logEntryType = PH_LOG_ENTRY_SERVICE_STOP;
        break;
    case ServiceContinued:
        logEntryType = PH_LOG_ENTRY_SERVICE_CONTINUE;
        break;
    case ServicePaused:
        logEntryType = PH_LOG_ENTRY_SERVICE_PAUSE;
        break;
    default:
        logEntryType = 0;
        break;
    }

    if (logEntryType != 0)
        PhLogServiceEntry(logEntryType, ServiceModifiedData->Service->Name, ServiceModifiedData->Service->DisplayName);

    if (PhMwpNotifyIconNotifyMask & (PH_NOTIFY_SERVICE_START | PH_NOTIFY_SERVICE_STOP))
    {
        PPH_SERVICE_ITEM serviceItem;

        serviceItem = ServiceModifiedData->Service;

        if (serviceChange == ServiceStarted && (PhMwpNotifyIconNotifyMask & PH_NOTIFY_SERVICE_START))
        {
            if (!PhPluginsEnabled || !PhMwpPluginNotifyEvent(PH_NOTIFY_SERVICE_START, serviceItem))
            {
                PhMwpClearLastNotificationDetails();
                PhMwpLastNotificationType = PH_NOTIFY_SERVICE_START;
                PhSwapReference(&PhMwpLastNotificationDetails.ServiceName, serviceItem->Name);

                PhShowIconNotification(L"Service Started", PhaFormatString(
                    L"The service %s (%s) has been started.",
                    serviceItem->Name->Buffer,
                    serviceItem->DisplayName->Buffer
                    )->Buffer, NIIF_INFO);
            }
        }
        else if (serviceChange == ServiceStopped && (PhMwpNotifyIconNotifyMask & PH_NOTIFY_SERVICE_STOP))
        {
            PhMwpClearLastNotificationDetails();
            PhMwpLastNotificationType = PH_NOTIFY_SERVICE_STOP;
            PhSwapReference(&PhMwpLastNotificationDetails.ServiceName, serviceItem->Name);

            if (!PhPluginsEnabled || !PhMwpPluginNotifyEvent(PH_NOTIFY_SERVICE_STOP, serviceItem))
            {
                PhShowIconNotification(L"Service Stopped", PhaFormatString(
                    L"The service %s (%s) has been stopped.",
                    serviceItem->Name->Buffer,
                    serviceItem->DisplayName->Buffer
                    )->Buffer, NIIF_INFO);
            }
        }
    }
}

VOID PhMwpOnServiceRemoved(
    _In_ PPH_SERVICE_ITEM ServiceItem
    )
{
    PhLogServiceEntry(PH_LOG_ENTRY_SERVICE_DELETE, ServiceItem->Name, ServiceItem->DisplayName);

    if (PhMwpNotifyIconNotifyMask & PH_NOTIFY_SERVICE_CREATE)
    {
        if (!PhPluginsEnabled || !PhMwpPluginNotifyEvent(PH_NOTIFY_SERVICE_DELETE, ServiceItem))
        {
            PhMwpClearLastNotificationDetails();
            PhMwpLastNotificationType = PH_NOTIFY_SERVICE_DELETE;
            PhSwapReference(&PhMwpLastNotificationDetails.ServiceName, ServiceItem->Name);

            PhShowIconNotification(L"Service Deleted", PhaFormatString(
                L"The service %s (%s) has been deleted.",
                ServiceItem->Name->Buffer,
                ServiceItem->DisplayName->Buffer
                )->Buffer, NIIF_INFO);
        }
    }

    PhRemoveServiceNode(PhFindServiceNode(ServiceItem));
}

VOID PhMwpOnServicesUpdated(
    _In_ ULONG RunId
    )
{
    PPH_PROVIDER_EVENT events;
    ULONG count;
    ULONG i;

    events = PhFlushProviderEventQueue(&PhMwpServiceEventQueue, RunId, &count);

    if (events)
    {
        TreeNew_SetRedraw(PhMwpServiceTreeNewHandle, FALSE);

        for (i = 0; i < count; i++)
        {
            PH_PROVIDER_EVENT_TYPE type = PH_PROVIDER_EVENT_TYPE(events[i]);
            PPH_SERVICE_ITEM serviceItem = PH_PROVIDER_EVENT_OBJECT(events[i]);

            switch (type)
            {
            case ProviderAddedEvent:
                PhMwpOnServiceAdded(serviceItem, events[i].RunId);
                break;
            case ProviderModifiedEvent:
                PhMwpOnServiceModified((PPH_SERVICE_MODIFIED_DATA)serviceItem);
                break;
            case ProviderRemovedEvent:
                PhMwpOnServiceRemoved(serviceItem);
                break;
            }
        }

        PhFree(events);
    }

    PhTickServiceNodes();

    if (count != 0)
        TreeNew_SetRedraw(PhMwpServiceTreeNewHandle, TRUE);
}
