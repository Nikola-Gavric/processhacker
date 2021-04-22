/*
 * Process Hacker Plugins -
 *   Hardware Devices Plugin
 *
 * Copyright (C) 2016 wj32
 * Copyright (C) 2015-2018 dmex
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

#include "devices.h"
#include <ndisguid.h>
#include <devguid.h>

typedef struct _NET_ENUM_ENTRY
{
    BOOLEAN DevicePresent;
    IF_LUID DeviceLuid;
    PPH_STRING DeviceGuid;
    PPH_STRING DeviceName;
    PPH_STRING DeviceInterface;
} NET_ENUM_ENTRY, *PNET_ENUM_ENTRY;

static int __cdecl AdapterEntryCompareFunction(
    _In_ const void *elem1,
    _In_ const void *elem2
    )
{
    PNET_ENUM_ENTRY entry1 = *(PNET_ENUM_ENTRY *)elem1;
    PNET_ENUM_ENTRY entry2 = *(PNET_ENUM_ENTRY *)elem2;

    return uint64cmp(entry1->DeviceLuid.Value, entry2->DeviceLuid.Value);
}

VOID NetAdaptersLoadList(
    VOID
    )
{
    PPH_STRING settingsString;
    PH_STRINGREF remaining;

    settingsString = PhaGetStringSetting(SETTING_NAME_INTERFACE_LIST);
    remaining = settingsString->sr;

    while (remaining.Length != 0)
    {
        ULONG64 ifindex;
        ULONG64 luid64;
        PH_STRINGREF part1;
        PH_STRINGREF part2;
        PH_STRINGREF part3;
        IF_LUID ifLuid;
        DV_NETADAPTER_ID id;
        PDV_NETADAPTER_ENTRY entry;

        if (remaining.Length == 0)
            break;

        PhSplitStringRefAtChar(&remaining, ',', &part1, &remaining);
        PhSplitStringRefAtChar(&remaining, ',', &part2, &remaining);
        PhSplitStringRefAtChar(&remaining, ',', &part3, &remaining);

        PhStringToInteger64(&part1, 10, &ifindex);
        PhStringToInteger64(&part2, 10, &luid64);

        ifLuid.Value = luid64;
        InitializeNetAdapterId(&id, (IF_INDEX)ifindex, ifLuid, PhCreateString2(&part3));
        entry = CreateNetAdapterEntry(&id);
        DeleteNetAdapterId(&id);

        entry->UserReference = TRUE;
    }
}

VOID NetAdaptersSaveList(
    VOID
    )
{
    PH_STRING_BUILDER stringBuilder;
    PPH_STRING settingsString;

    PhInitializeStringBuilder(&stringBuilder, 260);

    PhAcquireQueuedLockShared(&NetworkAdaptersListLock);

    for (ULONG i = 0; i < NetworkAdaptersList->Count; i++)
    {
        PDV_NETADAPTER_ENTRY entry = PhReferenceObjectSafe(NetworkAdaptersList->Items[i]);

        if (!entry)
            continue;

        if (entry->UserReference)
        {
            PhAppendFormatStringBuilder(
                &stringBuilder,
                L"%lu,%I64u,%s,",
                entry->AdapterId.InterfaceIndex, // This value is UNSAFE and will change after reboot.
                entry->AdapterId.InterfaceLuid.Value, // This value is SAFE and does not change (Vista+).
                entry->AdapterId.InterfaceGuid->Buffer
                );
        }

        PhDereferenceObjectDeferDelete(entry);
    }

    PhReleaseQueuedLockShared(&NetworkAdaptersListLock);

    if (stringBuilder.String->Length != 0)
        PhRemoveEndStringBuilder(&stringBuilder, 1);

    settingsString = PH_AUTO(PhFinalStringBuilderString(&stringBuilder));
    PhSetStringSetting2(SETTING_NAME_INTERFACE_LIST, &settingsString->sr);
}

BOOLEAN FindAdapterEntry(
    _In_ PDV_NETADAPTER_ID Id,
    _In_ BOOLEAN RemoveUserReference
    )
{
    BOOLEAN found = FALSE;

    PhAcquireQueuedLockShared(&NetworkAdaptersListLock);

    for (ULONG i = 0; i < NetworkAdaptersList->Count; i++)
    {
        PDV_NETADAPTER_ENTRY currentEntry = PhReferenceObjectSafe(NetworkAdaptersList->Items[i]);

        if (!currentEntry)
            continue;

        found = EquivalentNetAdapterId(&currentEntry->AdapterId, Id);

        if (found)
        {
            if (RemoveUserReference)
            {
                if (currentEntry->UserReference)
                {
                    PhDereferenceObjectDeferDelete(currentEntry);
                    currentEntry->UserReference = FALSE;
                }
            }

            PhDereferenceObjectDeferDelete(currentEntry);

            break;
        }
        else
        {
            PhDereferenceObjectDeferDelete(currentEntry);
        }
    }

    PhReleaseQueuedLockShared(&NetworkAdaptersListLock);

    return found;
}

VOID AddNetworkAdapterToListView(
    _In_ PDV_NETADAPTER_CONTEXT Context,
    _In_ BOOLEAN AdapterPresent,
    _In_ IF_INDEX IfIndex,
    _In_ IF_LUID Luid,
    _In_ PPH_STRING Guid,
    _In_ PPH_STRING Description
    )
{
    DV_NETADAPTER_ID adapterId;
    INT lvItemIndex;
    BOOLEAN found = FALSE;
    PDV_NETADAPTER_ID newId = NULL;

    InitializeNetAdapterId(&adapterId, IfIndex, Luid, Guid);

    for (ULONG i = 0; i < NetworkAdaptersList->Count; i++)
    {
        PDV_NETADAPTER_ENTRY entry = PhReferenceObjectSafe(NetworkAdaptersList->Items[i]);

        if (!entry)
            continue;

        if (EquivalentNetAdapterId(&entry->AdapterId, &adapterId))
        {
            newId = PhAllocate(sizeof(DV_NETADAPTER_ID));
            CopyNetAdapterId(newId, &entry->AdapterId);

            if (entry->UserReference)
                found = TRUE;
        }

        PhDereferenceObjectDeferDelete(entry);

        if (newId)
            break;
    }

    if (!newId)
    {
        newId = PhAllocate(sizeof(DV_NETADAPTER_ID));
        CopyNetAdapterId(newId, &adapterId);
        PhMoveReference(&newId->InterfaceGuid, Guid);
    }

    lvItemIndex = PhAddListViewGroupItem(
        Context->ListViewHandle,
        AdapterPresent ? 0 : 1,
        MAXINT,
        Description->Buffer,
        newId
        );

    if (found)
        ListView_SetCheckState(Context->ListViewHandle, lvItemIndex, TRUE);

    DeleteNetAdapterId(&adapterId);
}

VOID FreeListViewAdapterEntries(
    _In_ PDV_NETADAPTER_CONTEXT Context
    )
{
    ULONG index = -1;

    while ((index = PhFindListViewItemByFlags(
        Context->ListViewHandle,
        index,
        LVNI_ALL
        )) != -1)
    {
        PDV_NETADAPTER_ID param;

        if (PhGetListViewItemParam(Context->ListViewHandle, index, &param))
        {
            DeleteNetAdapterId(param);
            PhFree(param);
        }
    }
}

_Success_(return)
BOOLEAN QueryNetworkDeviceInterfaceDescription(
    _In_ PWSTR DeviceInterface,
    _Out_ DEVINST *DeviceInstanceHandle,
    _Out_ PPH_STRING *DeviceDescription
    )
{
    CONFIGRET result;
    ULONG bufferSize;
    PPH_STRING deviceDescription;
    DEVPROPTYPE devicePropertyType;
    DEVINST deviceInstanceHandle;
    ULONG deviceInstanceIdLength = MAX_DEVICE_ID_LEN;
    WCHAR deviceInstanceId[MAX_DEVICE_ID_LEN + 1] = L"";

    if (CM_Get_Device_Interface_Property(
        DeviceInterface,
        &DEVPKEY_Device_InstanceId,
        &devicePropertyType,
        (PBYTE)deviceInstanceId,
        &deviceInstanceIdLength,
        0
        ) != CR_SUCCESS)
    {
        return FALSE;
    }

    if (CM_Locate_DevNode(
        &deviceInstanceHandle,
        deviceInstanceId,
        CM_LOCATE_DEVNODE_PHANTOM
        ) != CR_SUCCESS)
    {
        return FALSE;
    }

    bufferSize = 0x40;
    deviceDescription = PhCreateStringEx(NULL, bufferSize);

    // DEVPKEY_Device_DeviceDesc doesn't give us the full adapter name.
    // DEVPKEY_Device_FriendlyName does give us the full adapter name but is only
    //  supported on Windows 8 and above.

    // We use our NetworkAdapterQueryName function to query the full adapter name
    // from the NDIS driver directly, if that fails then we use one of the above properties.

    if ((result = CM_Get_DevNode_Property(
        deviceInstanceHandle,
        WindowsVersion >= WINDOWS_8 ? &DEVPKEY_Device_FriendlyName : &DEVPKEY_Device_DeviceDesc,
        &devicePropertyType,
        (PBYTE)deviceDescription->Buffer,
        &bufferSize,
        0
        )) != CR_SUCCESS)
    {
        PhDereferenceObject(deviceDescription);
        deviceDescription = PhCreateStringEx(NULL, bufferSize);

        result = CM_Get_DevNode_Property(
            deviceInstanceHandle,
            WindowsVersion >= WINDOWS_8 ? &DEVPKEY_Device_FriendlyName : &DEVPKEY_Device_DeviceDesc,
            &devicePropertyType,
            (PBYTE)deviceDescription->Buffer,
            &bufferSize,
            0
            );
    }

    if (result != CR_SUCCESS)
    {
        PhDereferenceObject(deviceDescription);
        return FALSE;
    }

    PhTrimToNullTerminatorString(deviceDescription);

    *DeviceInstanceHandle = deviceInstanceHandle;
    *DeviceDescription = deviceDescription;

    return TRUE;
}

VOID FindNetworkAdapters(
    _In_ PDV_NETADAPTER_CONTEXT Context
    )
{
    if (Context->UseAlternateMethod)
    {
        ULONG flags = GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_ALL_INTERFACES;
        ULONG bufferLength = 0;
        PVOID buffer;

        if (GetAdaptersAddresses(AF_UNSPEC, flags, NULL, NULL, &bufferLength) != ERROR_BUFFER_OVERFLOW)
            return;

        buffer = PhAllocate(bufferLength);
        memset(buffer, 0, bufferLength);

        if (GetAdaptersAddresses(AF_UNSPEC, flags, NULL, buffer, &bufferLength) == ERROR_SUCCESS)
        {
            PhAcquireQueuedLockShared(&NetworkAdaptersListLock);

            for (PIP_ADAPTER_ADDRESSES i = buffer; i; i = i->Next)
            {
                PPH_STRING description;

                if (description = PhCreateString(i->Description))
                {
                    AddNetworkAdapterToListView(
                        Context,
                        TRUE,
                        i->IfIndex,
                        i->Luid,
                        PhConvertMultiByteToUtf16(i->AdapterName),
                        description
                        );

                    PhDereferenceObject(description);
                }
            }

            PhReleaseQueuedLockShared(&NetworkAdaptersListLock);
        }

        PhFree(buffer);
    }
    else
    {
        static PH_STRINGREF devicePathSr = PH_STRINGREF_INIT(L"\\\\.\\");
        PPH_LIST deviceList;
        PWSTR deviceInterfaceList;
        ULONG deviceInterfaceListLength = 0;
        PWSTR deviceInterface;

        if (CM_Get_Device_Interface_List_Size(
            &deviceInterfaceListLength,
            (PGUID)&GUID_DEVINTERFACE_NET,
            NULL,
            CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES
            ) != CR_SUCCESS)
        {
            return;
        }

        deviceInterfaceList = PhAllocate(deviceInterfaceListLength * sizeof(WCHAR));
        memset(deviceInterfaceList, 0, deviceInterfaceListLength * sizeof(WCHAR));

        if (CM_Get_Device_Interface_List(
            (PGUID)&GUID_DEVINTERFACE_NET,
            NULL,
            deviceInterfaceList,
            deviceInterfaceListLength,
            CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES
            ) != CR_SUCCESS)
        {
            PhFree(deviceInterfaceList);
            return;
        }

        deviceList = PH_AUTO(PhCreateList(1));

        for (deviceInterface = deviceInterfaceList; *deviceInterface; deviceInterface += PhCountStringZ(deviceInterface) + 1)
        {
            HKEY keyHandle;
            DEVINST deviceInstanceHandle;
            PPH_STRING deviceDescription = NULL;

            if (!QueryNetworkDeviceInterfaceDescription(deviceInterface, &deviceInstanceHandle, &deviceDescription))
                continue;

            if (CM_Open_DevInst_Key(
                deviceInstanceHandle,
                KEY_QUERY_VALUE,
                0,
                RegDisposition_OpenExisting,
                &keyHandle,
                CM_REGISTRY_SOFTWARE
                ) == CR_SUCCESS)
            {
                PNET_ENUM_ENTRY adapterEntry;
                HANDLE deviceHandle;

                adapterEntry = PhAllocate(sizeof(NET_ENUM_ENTRY));
                memset(adapterEntry, 0, sizeof(NET_ENUM_ENTRY));

                adapterEntry->DeviceGuid = PhQueryRegistryString(keyHandle, L"NetCfgInstanceId");
                adapterEntry->DeviceInterface = PhConcatStringRef2(&devicePathSr, &adapterEntry->DeviceGuid->sr);
                adapterEntry->DeviceLuid.Info.IfType = PhQueryRegistryUlong64(keyHandle, L"*IfType");
                adapterEntry->DeviceLuid.Info.NetLuidIndex = PhQueryRegistryUlong64(keyHandle, L"NetLuidIndex");

                if (NT_SUCCESS(PhCreateFileWin32(
                    &deviceHandle,
                    PhGetString(adapterEntry->DeviceInterface),
                    FILE_GENERIC_READ,
                    FILE_ATTRIBUTE_NORMAL,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    FILE_OPEN,
                    FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
                    )))
                {
                    PPH_STRING adapterName;

                    // Try query the full adapter name
                    adapterName = NetworkAdapterQueryName(deviceHandle, adapterEntry->DeviceGuid);

                    if (adapterName)
                        adapterEntry->DeviceName = adapterName;

                    adapterEntry->DevicePresent = TRUE;

                    NtClose(deviceHandle);
                }

                if (!adapterEntry->DeviceName)
                    adapterEntry->DeviceName = PhCreateString2(&deviceDescription->sr);

                PhAddItemList(deviceList, adapterEntry);

                NtClose(keyHandle);
            }

            PhClearReference(&deviceDescription);
        }

        // Cleanup.
        PhFree(deviceInterfaceList);

        // Sort the entries
        qsort(deviceList->Items, deviceList->Count, sizeof(PVOID), AdapterEntryCompareFunction);

        PhAcquireQueuedLockShared(&NetworkAdaptersListLock);

        for (ULONG i = 0; i < deviceList->Count; i++)
        {
            PNET_ENUM_ENTRY entry = deviceList->Items[i];

            AddNetworkAdapterToListView(
                Context,
                entry->DevicePresent,
                0,
                entry->DeviceLuid,
                entry->DeviceGuid,
                entry->DeviceName
                );

            if (entry->DeviceName)
                PhDereferenceObject(entry->DeviceName);
            if (entry->DeviceInterface)
                PhDereferenceObject(entry->DeviceInterface);
            // Note: DeviceGuid is disposed by WM_DESTROY.

            PhFree(entry);
        }

        PhReleaseQueuedLockShared(&NetworkAdaptersListLock);
    }

    // HACK: Show all unknown devices.
    PhAcquireQueuedLockShared(&NetworkAdaptersListLock);

    for (ULONG i = 0; i < NetworkAdaptersList->Count; i++)
    {
        ULONG index = ULONG_MAX;
        BOOLEAN found = FALSE;
        PDV_NETADAPTER_ENTRY entry = PhReferenceObjectSafe(NetworkAdaptersList->Items[i]);

        if (!entry)
            continue;

        while ((index = PhFindListViewItemByFlags(
            Context->ListViewHandle,
            index,
            LVNI_ALL
            )) != ULONG_MAX)
        {
            PDV_NETADAPTER_ID param;

            if (PhGetListViewItemParam(Context->ListViewHandle, index, &param))
            {
                if (EquivalentNetAdapterId(param, &entry->AdapterId))
                {
                    found = TRUE;
                }
            }
        }

        if (!found)
        {
            PPH_STRING description;
            MIB_IF_ROW2 interfaceRow;

            memset(&interfaceRow, 0, sizeof(MIB_IF_ROW2));
            interfaceRow.InterfaceLuid = entry->AdapterId.InterfaceLuid;
            interfaceRow.InterfaceIndex = entry->AdapterId.InterfaceIndex;

            // HACK: Try query the description from the interface entry (if it exists).
            if (GetIfEntry2(&interfaceRow) == NO_ERROR)
                description = PhCreateString(interfaceRow.Description);
            else
                description = PhCreateString(L"Unknown network adapter");

            if (description)
            {
                AddNetworkAdapterToListView(
                    Context,
                    FALSE,
                    entry->AdapterId.InterfaceIndex,
                    entry->AdapterId.InterfaceLuid,
                    entry->AdapterId.InterfaceGuid,
                    description
                    );

                PhDereferenceObject(description);
            }
        }

        PhDereferenceObjectDeferDelete(entry);
    }

    PhReleaseQueuedLockShared(&NetworkAdaptersListLock);
}

PPH_STRING FindNetworkDeviceInstance(
    _In_ PPH_STRING DevicePath
    )
{
    PPH_STRING deviceInstanceString = NULL;
    PWSTR deviceInterfaceList;
    ULONG deviceInterfaceListLength = 0;
    PWSTR deviceInterface;

    if (CM_Get_Device_Interface_List_Size(
        &deviceInterfaceListLength,
        (PGUID)&GUID_DEVINTERFACE_NET,
        NULL,
        CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES
        ) != CR_SUCCESS)
    {
        return NULL;
    }

    deviceInterfaceList = PhAllocate(deviceInterfaceListLength * sizeof(WCHAR));
    memset(deviceInterfaceList, 0, deviceInterfaceListLength * sizeof(WCHAR));

    if (CM_Get_Device_Interface_List(
        (PGUID)&GUID_DEVINTERFACE_NET,
        NULL,
        deviceInterfaceList,
        deviceInterfaceListLength,
        CM_GET_DEVICE_INTERFACE_LIST_ALL_DEVICES
        ) != CR_SUCCESS)
    {
        PhFree(deviceInterfaceList);
        return NULL;
    }

    for (deviceInterface = deviceInterfaceList; *deviceInterface; deviceInterface += PhCountStringZ(deviceInterface) + 1)
    {
        HKEY keyHandle;
        DEVPROPTYPE devicePropertyType;
        DEVINST deviceInstanceHandle;
        ULONG deviceInstanceIdLength = MAX_DEVICE_ID_LEN;
        WCHAR deviceInstanceId[MAX_DEVICE_ID_LEN + 1] = L"";

        if (CM_Get_Device_Interface_Property(
            deviceInterface,
            &DEVPKEY_Device_InstanceId,
            &devicePropertyType,
            (PBYTE)deviceInstanceId,
            &deviceInstanceIdLength,
            0
            ) != CR_SUCCESS)
        {
            continue;
        }

        if (CM_Locate_DevNode(
            &deviceInstanceHandle,
            deviceInstanceId,
            CM_LOCATE_DEVNODE_PHANTOM
            ) != CR_SUCCESS)
        {
            continue;
        }

        if (CM_Open_DevInst_Key(
            deviceInstanceHandle,
            KEY_QUERY_VALUE,
            0,
            RegDisposition_OpenExisting,
            &keyHandle,
            CM_REGISTRY_SOFTWARE
            ) == CR_SUCCESS)
        {
            PPH_STRING deviceGuid;

            if (deviceGuid = PhQueryRegistryString(keyHandle, L"NetCfgInstanceId"))
            {
                if (PhEqualString(deviceGuid, DevicePath, TRUE))
                {
                    deviceInstanceString = PhCreateString(deviceInstanceId);

                    PhDereferenceObject(deviceGuid);
                    NtClose(keyHandle);
                    break;
                }

                PhDereferenceObject(deviceGuid);
            }

            NtClose(keyHandle);
        }
    }

    PhFree(deviceInterfaceList);

    return deviceInstanceString;
}

VOID LoadNetworkAdapterImages(
    _In_ PDV_NETADAPTER_CONTEXT Context
    )
{
    HICON smallIcon;
    CONFIGRET result;
    ULONG deviceIconPathLength;
    DEVPROPTYPE deviceIconPathPropertyType;
    PPH_STRING deviceIconPath;

    deviceIconPathLength = 0x40;
    deviceIconPath = PhCreateStringEx(NULL, deviceIconPathLength);

    if ((result = CM_Get_Class_Property(
        &GUID_DEVCLASS_NET,
        &DEVPKEY_DeviceClass_IconPath,
        &deviceIconPathPropertyType,
        (PBYTE)deviceIconPath->Buffer,
        &deviceIconPathLength,
        0
        )) != CR_SUCCESS)
    {
        PhDereferenceObject(deviceIconPath);
        deviceIconPath = PhCreateStringEx(NULL, deviceIconPathLength);

        result = CM_Get_Class_Property(
            &GUID_DEVCLASS_NET,
            &DEVPKEY_DeviceClass_IconPath,
            &deviceIconPathPropertyType,
            (PBYTE)deviceIconPath->Buffer,
            &deviceIconPathLength,
            0
            );
    }

    if (result != CR_SUCCESS)
    {
        PhDereferenceObject(deviceIconPath);
        return;
    }

    PhTrimToNullTerminatorString(deviceIconPath);

    {
        PPH_STRING dllIconPath;
        PH_STRINGREF dllPartSr;
        PH_STRINGREF indexPartSr;
        ULONG64 index = 0;

        if (
            PhSplitStringRefAtChar(&deviceIconPath->sr, ',', &dllPartSr, &indexPartSr) && 
            PhStringToInteger64(&indexPartSr, 10, &index)
            )
        {
            if (dllIconPath = PhExpandEnvironmentStrings(&dllPartSr))
            {
                if (PhExtractIconEx(dllIconPath->Buffer, (INT)index, &smallIcon, NULL))
                {
                    Context->ImageList = ImageList_Create(
                        GetSystemMetrics(SM_CXICON),
                        GetSystemMetrics(SM_CYICON),
                        ILC_COLOR32,
                        1,
                        1
                        );

                    ImageList_AddIcon(Context->ImageList, smallIcon);                 
                    ListView_SetImageList(Context->ListViewHandle, Context->ImageList, LVSIL_SMALL);
                    DestroyIcon(smallIcon);
                }

                PhDereferenceObject(dllIconPath);
            }
        }
    }

    PhDereferenceObject(deviceIconPath);
}

INT_PTR CALLBACK NetworkAdapterOptionsDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PDV_NETADAPTER_CONTEXT context = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        context = PhAllocateZero(sizeof(DV_NETADAPTER_CONTEXT));

        PhSetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT, context);
    }
    else
    {
        context = PhGetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
    }

    if (context == NULL)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            context->ListViewHandle = GetDlgItem(hwndDlg, IDC_NETADAPTERS_LISTVIEW);

            PhSetListViewStyle(context->ListViewHandle, FALSE, TRUE);
            ListView_SetExtendedListViewStyleEx(context->ListViewHandle, LVS_EX_CHECKBOXES, LVS_EX_CHECKBOXES);
            PhSetControlTheme(context->ListViewHandle, L"explorer");
            PhAddListViewColumn(context->ListViewHandle, 0, 0, 0, LVCFMT_LEFT, 350, L"Network Adapters");
            PhSetExtendedListView(context->ListViewHandle);
            LoadNetworkAdapterImages(context);

            ListView_EnableGroupView(context->ListViewHandle, TRUE);
            PhAddListViewGroup(context->ListViewHandle, 0, L"Connected");
            PhAddListViewGroup(context->ListViewHandle, 1, L"Disconnected");

            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);
            PhAddLayoutItem(&context->LayoutManager, context->ListViewHandle, NULL, PH_ANCHOR_ALL);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_SHOW_HIDDEN_ADAPTERS), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_LEFT);

            FindNetworkAdapters(context);

            context->OptionsChanged = FALSE;
        }
        break;
    case WM_DESTROY:
        {
            PhDeleteLayoutManager(&context->LayoutManager);

            if (context->OptionsChanged)
                NetAdaptersSaveList();

            FreeListViewAdapterEntries(context);

            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
            PhFree(context);
        }
        break;
    case WM_SIZE:
        {
            PhLayoutManagerLayout(&context->LayoutManager);

            ExtendedListView_SetColumnWidth(context->ListViewHandle, 0, ELVSCW_AUTOSIZE_REMAININGSPACE);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDC_SHOW_HIDDEN_ADAPTERS:
                {
                    context->UseAlternateMethod = !context->UseAlternateMethod;

                    if (context->UseAlternateMethod)
                    {
                        ListView_EnableGroupView(context->ListViewHandle, FALSE);
                    }
                    else
                    {
                        ListView_EnableGroupView(context->ListViewHandle, TRUE);
                    }

                    FreeListViewAdapterEntries(context);
                    ListView_DeleteAllItems(context->ListViewHandle);

                    FindNetworkAdapters(context);

                    ExtendedListView_SetColumnWidth(context->ListViewHandle, 0, ELVSCW_AUTOSIZE_REMAININGSPACE);
                }
                break;
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            if (header->code == LVN_ITEMCHANGED)
            {
                LPNM_LISTVIEW listView = (LPNM_LISTVIEW)lParam;

                if (!PhTryAcquireReleaseQueuedLockExclusive(&NetworkAdaptersListLock))
                    break;

                if (listView->uChanged & LVIF_STATE)
                {
                    switch (listView->uNewState & LVIS_STATEIMAGEMASK)
                    {
                    case INDEXTOSTATEIMAGEMASK(2): // checked
                        {
                            PDV_NETADAPTER_ID param = (PDV_NETADAPTER_ID)listView->lParam;

                            if (!FindAdapterEntry(param, FALSE))
                            {
                                PDV_NETADAPTER_ENTRY entry;

                                entry = CreateNetAdapterEntry(param);
                                entry->UserReference = TRUE;
                            }

                            context->OptionsChanged = TRUE;
                        }
                        break;
                    case INDEXTOSTATEIMAGEMASK(1): // unchecked
                        {
                            PDV_NETADAPTER_ID param = (PDV_NETADAPTER_ID)listView->lParam;

                            FindAdapterEntry(param, TRUE);

                            context->OptionsChanged = TRUE;
                        }
                        break;
                    }
                }
            }
            else if (header->code == NM_RCLICK)
            {
                PDV_NETADAPTER_ENTRY param;
                PPH_STRING deviceInstance;

                if (param = PhGetSelectedListViewItemParam(context->ListViewHandle))
                {
                    if (deviceInstance = FindNetworkDeviceInstance(param->AdapterId.InterfaceGuid))
                    {
                        ShowDeviceMenu(hwndDlg, deviceInstance);
                        PhDereferenceObject(deviceInstance);
                    }
                }
            }
        }
        break;
    }

    return FALSE;
}
