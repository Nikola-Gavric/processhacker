/*
 * Process Hacker Extended Tools -
 *   GPU details window
 *
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
#include "gpumon.h"

typedef enum _GPUADAPTER_DETAILS_INDEX
{
    GPUADAPTER_DETAILS_INDEX_PHYSICALLOCTION,
    GPUADAPTER_DETAILS_INDEX_DRIVERDATE,
    GPUADAPTER_DETAILS_INDEX_DRIVERVERSION,
    GPUADAPTER_DETAILS_INDEX_WDDMVERSION,
    GPUADAPTER_DETAILS_INDEX_VENDORID,
    GPUADAPTER_DETAILS_INDEX_DEVICEID,
    GPUADAPTER_DETAILS_INDEX_TOTALMEMORY,
    GPUADAPTER_DETAILS_INDEX_RESERVEDMEMORY,
    GPUADAPTER_DETAILS_INDEX_MEMORYFREQUENCY,
    GPUADAPTER_DETAILS_INDEX_MEMORYBANDWIDTH,
    GPUADAPTER_DETAILS_INDEX_PCIEBANDWIDTH,
    GPUADAPTER_DETAILS_INDEX_FANRPM,
    GPUADAPTER_DETAILS_INDEX_POWERUSAGE,
    GPUADAPTER_DETAILS_INDEX_TEMPERATURE,
} GPUADAPTER_DETAILS_INDEX;

VOID EtpGpuDetailsAddListViewItemGroups(
    _In_ HWND ListViewHandle,
    _In_ INT GpuGroupId)
{
    PhAddListViewGroupItem(ListViewHandle, GpuGroupId, GPUADAPTER_DETAILS_INDEX_PHYSICALLOCTION, L"Physical Location", NULL);
    PhAddListViewGroupItem(ListViewHandle, GpuGroupId, GPUADAPTER_DETAILS_INDEX_DRIVERDATE, L"Driver Date", NULL);
    PhAddListViewGroupItem(ListViewHandle, GpuGroupId, GPUADAPTER_DETAILS_INDEX_DRIVERVERSION, L"Driver Version", NULL);
    PhAddListViewGroupItem(ListViewHandle, GpuGroupId, GPUADAPTER_DETAILS_INDEX_WDDMVERSION, L"WDDM Version", NULL);
    PhAddListViewGroupItem(ListViewHandle, GpuGroupId, GPUADAPTER_DETAILS_INDEX_VENDORID, L"Vendor ID", NULL);
    PhAddListViewGroupItem(ListViewHandle, GpuGroupId, GPUADAPTER_DETAILS_INDEX_DEVICEID, L"Device ID", NULL);
    PhAddListViewGroupItem(ListViewHandle, GpuGroupId, GPUADAPTER_DETAILS_INDEX_TOTALMEMORY, L"Total Memory", NULL);
    PhAddListViewGroupItem(ListViewHandle, GpuGroupId, GPUADAPTER_DETAILS_INDEX_RESERVEDMEMORY, L"Reserved Memory", NULL);
    PhAddListViewGroupItem(ListViewHandle, GpuGroupId, GPUADAPTER_DETAILS_INDEX_MEMORYFREQUENCY, L"Memory Frequency", NULL);
    PhAddListViewGroupItem(ListViewHandle, GpuGroupId, GPUADAPTER_DETAILS_INDEX_MEMORYBANDWIDTH, L"Memory Bandwidth", NULL);
    PhAddListViewGroupItem(ListViewHandle, GpuGroupId, GPUADAPTER_DETAILS_INDEX_PCIEBANDWIDTH, L"PCIE Bandwidth", NULL);
    PhAddListViewGroupItem(ListViewHandle, GpuGroupId, GPUADAPTER_DETAILS_INDEX_FANRPM, L"Fan RPM", NULL);
    PhAddListViewGroupItem(ListViewHandle, GpuGroupId, GPUADAPTER_DETAILS_INDEX_POWERUSAGE, L"Power Usage", NULL);
    PhAddListViewGroupItem(ListViewHandle, GpuGroupId, GPUADAPTER_DETAILS_INDEX_TEMPERATURE, L"Temperature", NULL);
}

VOID EtpQueryAdapterDeviceProperties(
    _In_ PWSTR DeviceName,
    _In_ HWND ListViewHandle)
{
    PPH_STRING driverDate;
    PPH_STRING driverVersion;
    PPH_STRING locationInfo;
    ULONG64 installedMemory;

    if (EtQueryDeviceProperties(DeviceName, NULL, &driverDate, &driverVersion, &locationInfo, &installedMemory))
    {
        PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_DRIVERDATE, 1, PhGetStringOrEmpty(driverDate));
        PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_DRIVERVERSION, 1, PhGetStringOrEmpty(driverVersion));
        PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_PHYSICALLOCTION, 1, PhGetStringOrEmpty(locationInfo));
 
        if (installedMemory != ULLONG_MAX)
        {
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_TOTALMEMORY, 1, PhaFormatSize(installedMemory, ULONG_MAX)->Buffer);
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_RESERVEDMEMORY, 1, PhaFormatSize(installedMemory - EtGpuDedicatedLimit, ULONG_MAX)->Buffer);
        }

        PhClearReference(&locationInfo);
        PhClearReference(&driverVersion);
        PhClearReference(&driverDate);
    }
}

VOID EtpQueryAdapterRegistryInfo(
    _In_ D3DKMT_HANDLE AdapterHandle, 
    _In_ HWND ListViewHandle)
{
    D3DKMT_ADAPTERREGISTRYINFO adapterInfo;

    memset(&adapterInfo, 0, sizeof(D3DKMT_ADAPTERREGISTRYINFO));

    if (NT_SUCCESS(EtQueryAdapterInformation(
        AdapterHandle,
        KMTQAITYPE_ADAPTERREGISTRYINFO,
        &adapterInfo,
        sizeof(D3DKMT_ADAPTERREGISTRYINFO)
        )))
    {
        NOTHING;
    }
}

VOID EtpQueryAdapterDriverModel(
    _In_ D3DKMT_HANDLE AdapterHandle, 
    _In_ HWND ListViewHandle)
{
    D3DKMT_DRIVERVERSION wddmversion;

    memset(&wddmversion, 0, sizeof(D3DKMT_DRIVERVERSION));

    if (NT_SUCCESS(EtQueryAdapterInformation(
        AdapterHandle,
        KMTQAITYPE_DRIVERVERSION,
        &wddmversion,
        sizeof(D3DKMT_DRIVERVERSION)
        )))
    {
        switch (wddmversion)
        {
        case KMT_DRIVERVERSION_WDDM_1_0:
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_WDDMVERSION, 1, L"WDDM 1.0");
            break;
        case KMT_DRIVERVERSION_WDDM_1_1_PRERELEASE:
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_WDDMVERSION, 1, L"WDDM 1.1 (pre-release)");
            break;
        case KMT_DRIVERVERSION_WDDM_1_1:
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_WDDMVERSION, 1, L"WDDM 1.1");
            break;
        case KMT_DRIVERVERSION_WDDM_1_2:
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_WDDMVERSION, 1, L"WDDM 1.2");
            break;
        case KMT_DRIVERVERSION_WDDM_1_3:
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_WDDMVERSION, 1, L"WDDM 1.3");
            break;
        case KMT_DRIVERVERSION_WDDM_2_0:
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_WDDMVERSION, 1, L"WDDM 2.0");
            break;
        case KMT_DRIVERVERSION_WDDM_2_1:
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_WDDMVERSION, 1, L"WDDM 2.1");
            break;
        case KMT_DRIVERVERSION_WDDM_2_2:
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_WDDMVERSION, 1, L"WDDM 2.2");
            break;
        case KMT_DRIVERVERSION_WDDM_2_3:
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_WDDMVERSION, 1, L"WDDM 2.3");
            break;
        case KMT_DRIVERVERSION_WDDM_2_4:
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_WDDMVERSION, 1, L"WDDM 2.4");
            break;
        case KMT_DRIVERVERSION_WDDM_2_5:
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_WDDMVERSION, 1, L"WDDM 2.5");
            break;
        case KMT_DRIVERVERSION_WDDM_2_6:
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_WDDMVERSION, 1, L"WDDM 2.6");
            break;
        default:
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_WDDMVERSION, 1, L"ERROR");
            break;
        }
    }
}

VOID EtpQueryAdapterDriverVersion(
    _In_ D3DKMT_HANDLE AdapterHandle,
    _In_ HWND ListViewHandle)
{
    D3DKMT_UMD_DRIVER_VERSION driverUserVersion;
    D3DKMT_KMD_DRIVER_VERSION driverKernelVersion;

    memset(&driverUserVersion, 0, sizeof(D3DKMT_UMD_DRIVER_VERSION));
    memset(&driverKernelVersion, 0, sizeof(D3DKMT_KMD_DRIVER_VERSION));

    if (NT_SUCCESS(EtQueryAdapterInformation(
        AdapterHandle,
        KMTQAITYPE_UMD_DRIVER_VERSION,
        &driverUserVersion,
        sizeof(D3DKMT_UMD_DRIVER_VERSION)
        )))
    {
        PPH_STRING driverVersionString = PhFormatString(
            L"%hu.%hu.%hu.%hu",
            HIWORD(driverUserVersion.DriverVersion.HighPart),
            LOWORD(driverUserVersion.DriverVersion.HighPart),
            HIWORD(driverUserVersion.DriverVersion.LowPart),
            LOWORD(driverUserVersion.DriverVersion.LowPart)
            );
        PhDereferenceObject(driverVersionString);
    }

    if (NT_SUCCESS(EtQueryAdapterInformation(
        AdapterHandle,
        KMTQAITYPE_KMD_DRIVER_VERSION,
        &driverKernelVersion,
        sizeof(D3DKMT_KMD_DRIVER_VERSION)
        )))
    {
        PPH_STRING driverVersionString = PhFormatString(
            L"%hu.%hu.%hu.%hu",
            HIWORD(driverKernelVersion.DriverVersion.HighPart),
            LOWORD(driverKernelVersion.DriverVersion.HighPart),
            HIWORD(driverKernelVersion.DriverVersion.LowPart),
            LOWORD(driverKernelVersion.DriverVersion.LowPart)
            );
        PhDereferenceObject(driverVersionString);
    }
}

VOID EtpQueryAdapterDeviceIds(
    _In_ D3DKMT_HANDLE AdapterHandle,
    _In_ HWND ListViewHandle)
{
    D3DKMT_QUERY_DEVICE_IDS adapterDeviceId;

    memset(&adapterDeviceId, 0, sizeof(D3DKMT_QUERY_DEVICE_IDS));

    if (NT_SUCCESS(EtQueryAdapterInformation(
        AdapterHandle,
        KMTQAITYPE_PHYSICALADAPTERDEVICEIDS,
        &adapterDeviceId,
        sizeof(D3DKMT_QUERY_DEVICE_IDS)
        )))
    {
        WCHAR value[PH_PTR_STR_LEN_1];

        PhPrintPointer(value, UlongToPtr(adapterDeviceId.DeviceIds.VendorID));
        PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_VENDORID, 1, value);

        PhPrintPointer(value, UlongToPtr(adapterDeviceId.DeviceIds.DeviceID));
        PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_DEVICEID, 1, value);

        //PhPrintPointer(value, UlongToPtr(adapterDeviceId.DeviceIds.SubVendorID));
        //PhPrintPointer(value, UlongToPtr(adapterDeviceId.DeviceIds.SubSystemID));
        //PhPrintPointer(value, UlongToPtr(adapterDeviceId.DeviceIds.RevisionID));
        //PhPrintPointer(value, UlongToPtr(adapterDeviceId.DeviceIds.BusType));
    }
}

VOID EtpQueryAdapterPerfInfo(
    _In_ D3DKMT_HANDLE AdapterHandle,
    _In_ HWND ListViewHandle)
{
    D3DKMT_ADAPTER_PERFDATA adapterPerfData;

    memset(&adapterPerfData, 0, sizeof(D3DKMT_ADAPTER_PERFDATA));

    if (NT_SUCCESS(EtQueryAdapterInformation(
        AdapterHandle,
        KMTQAITYPE_ADAPTERPERFDATA,
        &adapterPerfData,
        sizeof(D3DKMT_ADAPTER_PERFDATA)
        )))
    {
        PH_FORMAT format[2];
        WCHAR formatBuffer[256];

        PhInitFormatI64U(&format[0], adapterPerfData.MemoryFrequency / 1000 / 1000);
        PhInitFormatS(&format[1], L" MHz");

        if (PhFormatToBuffer(format, 2, formatBuffer, sizeof(formatBuffer), NULL))
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_MEMORYFREQUENCY, 1, formatBuffer);
        else
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_MEMORYFREQUENCY, 1, PhaFormatString(L"%I64u MHz", adapterPerfData.MemoryFrequency / 1000 / 1000)->Buffer);

        PhInitFormatSize(&format[0], adapterPerfData.MemoryBandwidth);

        if (PhFormatToBuffer(format, 1, formatBuffer, sizeof(formatBuffer), NULL))
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_MEMORYBANDWIDTH, 1, formatBuffer);
        else
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_MEMORYBANDWIDTH, 1, PhaFormatSize(adapterPerfData.MemoryBandwidth, ULONG_MAX)->Buffer);

        PhInitFormatSize(&format[0], adapterPerfData.PCIEBandwidth);

        if (PhFormatToBuffer(format, 1, formatBuffer, sizeof(formatBuffer), NULL))
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_PCIEBANDWIDTH, 1, formatBuffer);
        else
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_PCIEBANDWIDTH, 1, PhaFormatSize(adapterPerfData.PCIEBandwidth, ULONG_MAX)->Buffer);

        PhInitFormatI64U(&format[0], adapterPerfData.FanRPM);

        if (PhFormatToBuffer(format, 1, formatBuffer, sizeof(formatBuffer), NULL))
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_FANRPM, 1, formatBuffer);
        else
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_FANRPM, 1, PhaFormatUInt64(adapterPerfData.FanRPM, FALSE)->Buffer);

        PhInitFormatI64U(&format[0], adapterPerfData.Power * 100 / 1000);
        PhInitFormatS(&format[1], L"%");

        if (PhFormatToBuffer(format, 2, formatBuffer, sizeof(formatBuffer), NULL))
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_POWERUSAGE, 1, formatBuffer);
        else
            PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_POWERUSAGE, 1, PhaFormatString(L"%lu%%", adapterPerfData.Power * 100 / 1000)->Buffer);

        if (PhGetIntegerSetting(SETTING_NAME_ENABLE_FAHRENHEIT))
        {
            ULONG gpuCurrentTemp = adapterPerfData.Temperature * 100 / 1000;
            FLOAT gpuFahrenheitTemp = (FLOAT)(gpuCurrentTemp * 1.8 + 32);

            PhInitFormatF(&format[0], gpuFahrenheitTemp, 1);
            PhInitFormatS(&format[1], L"\u00b0F");

            if (PhFormatToBuffer(format, RTL_NUMBER_OF(format), formatBuffer, sizeof(formatBuffer), NULL))
            {
                PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_TEMPERATURE, 1, formatBuffer);
            }
            else
            {
                PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_TEMPERATURE, 1, PhaFormatString(
                    L"%.1f\u00b0F (%lu\u00b0C)",
                    gpuFahrenheitTemp,
                    gpuCurrentTemp
                    )->Buffer);
            }
        }
        else
        {
            ULONG gpuCurrentTemp = adapterPerfData.Temperature * 100 / 1000;

            PhInitFormatI64U(&format[0], gpuCurrentTemp);
            PhInitFormatS(&format[1], L"\u00b0C");

            if (PhFormatToBuffer(format, RTL_NUMBER_OF(format), formatBuffer, sizeof(formatBuffer), NULL))
            {
                PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_TEMPERATURE, 1, formatBuffer);
            }
            else
            {
                PhSetListViewSubItem(ListViewHandle, GPUADAPTER_DETAILS_INDEX_TEMPERATURE, 1, PhaFormatString(
                    L"%lu\u00b0C",
                    gpuCurrentTemp
                    )->Buffer);
            }
        }
    }
}

VOID EtpGpuDetailsEnumAdapters(
    _In_ HWND ListViewHandle
    )
{
    PETP_GPU_ADAPTER gpuAdapter;
    D3DKMT_OPENADAPTERFROMDEVICENAME openAdapterFromDeviceName;

    for (ULONG i = 0; i < EtpGpuAdapterList->Count; i++)
    {
        gpuAdapter = EtpGpuAdapterList->Items[i];

        memset(&openAdapterFromDeviceName, 0, sizeof(D3DKMT_OPENADAPTERFROMDEVICENAME));
        openAdapterFromDeviceName.DeviceName = PhGetString(gpuAdapter->DeviceInterface);

        if (!NT_SUCCESS(D3DKMTOpenAdapterFromDeviceName(&openAdapterFromDeviceName)))
            continue;

        if (!ListView_HasGroup(ListViewHandle, i))
        {
            if (PhAddListViewGroup(ListViewHandle, i, PhGetString(gpuAdapter->Description)) == MAXINT)
            {
                EtCloseAdapterHandle(openAdapterFromDeviceName.AdapterHandle);
                continue;
            }

            EtpGpuDetailsAddListViewItemGroups(ListViewHandle, i);
        }

        EtpQueryAdapterDeviceProperties(openAdapterFromDeviceName.DeviceName, ListViewHandle);
        //EtpQueryAdapterRegistryInfo(openAdapterFromDeviceName.AdapterHandle, ListViewHandle);
        EtpQueryAdapterDriverModel(openAdapterFromDeviceName.AdapterHandle, ListViewHandle);
        //EtpQueryAdapterDriverVersion(openAdapterFromDeviceName.AdapterHandle, ListViewHandle);
        EtpQueryAdapterDeviceIds(openAdapterFromDeviceName.AdapterHandle, ListViewHandle);
        //EtQueryAdapterFeatureLevel(openAdapterFromDeviceName.AdapterLuid);
        EtpQueryAdapterPerfInfo(openAdapterFromDeviceName.AdapterHandle, ListViewHandle);

        EtCloseAdapterHandle(openAdapterFromDeviceName.AdapterHandle);
    }
}

static VOID ProcessesUpdatedCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PostMessage((HWND)Context, ET_WM_UPDATE, 0, 0);
}

typedef struct _ET_GPU_DETAILS_CONTEXT
{
    HWND DialogHandle;
    HWND ListViewHandle;
    PH_CALLBACK_REGISTRATION ProcessesUpdatedCallbackRegistration;
    PH_LAYOUT_MANAGER LayoutManager;
} ET_GPU_DETAILS_CONTEXT, *PET_GPU_DETAILS_CONTEXT;

INT_PTR CALLBACK EtpGpuDetailsDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PET_GPU_DETAILS_CONTEXT context = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        context = PhAllocateZero(sizeof(ET_GPU_DETAILS_CONTEXT));

        PhSetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT, context);
    }
    else
    {
        context = PhGetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);

        if (uMsg == WM_DESTROY)
        {
            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
        }
    }

    if (context == NULL)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            SendMessage(hwndDlg, WM_SETICON, ICON_SMALL, (LPARAM)PH_LOAD_SHARED_ICON_SMALL(PhInstanceHandle, MAKEINTRESOURCE(PHAPP_IDI_PROCESSHACKER)));
            SendMessage(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM)PH_LOAD_SHARED_ICON_LARGE(PhInstanceHandle, MAKEINTRESOURCE(PHAPP_IDI_PROCESSHACKER)));

            context->DialogHandle = hwndDlg;
            context->ListViewHandle = GetDlgItem(hwndDlg, IDC_GPULIST);

            PhSetListViewStyle(context->ListViewHandle, FALSE, TRUE);
            PhSetControlTheme(context->ListViewHandle, L"explorer");
            PhAddListViewColumn(context->ListViewHandle, 0, 0, 0, LVCFMT_LEFT, 230, L"Property");
            PhAddListViewColumn(context->ListViewHandle, 1, 1, 1, LVCFMT_LEFT, 200, L"Value");
            //PhSetExtendedListView(context->ListViewHandle);
            ListView_EnableGroupView(context->ListViewHandle, TRUE);

            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);
            PhAddLayoutItem(&context->LayoutManager, context->ListViewHandle, NULL, PH_ANCHOR_ALL);

            PhCenterWindow(hwndDlg, GetParent(hwndDlg));

            PhInitializeWindowTheme(hwndDlg, !!PhGetIntegerSetting(L"EnableThemeSupport"));

            EtpGpuDetailsEnumAdapters(context->ListViewHandle);

            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackProcessProviderUpdatedEvent),
                ProcessesUpdatedCallback,
                hwndDlg,
                &context->ProcessesUpdatedCallbackRegistration
                );
        }
        break;
    case WM_DESTROY:
        {
            PhUnregisterCallback(PhGetGeneralCallback(GeneralCallbackProcessProviderUpdatedEvent), &context->ProcessesUpdatedCallbackRegistration);

            PhDeleteLayoutManager(&context->LayoutManager);
            PhFree(context);

            PostQuitMessage(0);
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
            }
        }
        break;
    case WM_SIZE:
        {
            PhLayoutManagerLayout(&context->LayoutManager);
        }
        break;
    case ET_WM_UPDATE:
        {
            EtpGpuDetailsEnumAdapters(context->ListViewHandle);
        }
        break;
    case WM_CONTEXTMENU:
        {
            if ((HWND)wParam == context->ListViewHandle)
            {
                POINT point;
                PPH_EMENU menu;
                PPH_EMENU item;
                PVOID *listviewItems;
                ULONG numberOfItems;

                point.x = GET_X_LPARAM(lParam);
                point.y = GET_Y_LPARAM(lParam);

                if (point.x == -1 && point.y == -1)
                    PhGetListViewContextMenuPoint((HWND)wParam, &point);

                PhGetSelectedListViewItemParams(context->ListViewHandle, &listviewItems, &numberOfItems);

                if (numberOfItems != 0)
                {
                    menu = PhCreateEMenu();
                    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, 1, L"&Copy", NULL, NULL), ULONG_MAX);
                    PhInsertCopyListViewEMenuItem(menu, 1, context->ListViewHandle);

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

                        //if (!handled && PhPluginsEnabled)
                        //    handled = PhPluginTriggerEMenuItem(&menuInfo, item);

                        if (!handled)
                        {
                            switch (item->Id)
                            {
                            case 1:
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
    }

    return FALSE;
}

NTSTATUS EtGpuDetailsDialogThreadStart(
    _In_ PVOID Parameter
    )
{
    BOOL result;
    MSG message;
    HWND windowHandle;
    PH_AUTO_POOL autoPool;

    PhInitializeAutoPool(&autoPool);

    windowHandle = CreateDialog(
        PluginInstance->DllBase,
        MAKEINTRESOURCE(IDD_SYSINFO_GPUDETAILS),
        !!PhGetIntegerSetting(L"ForceNoParent") ? NULL : Parameter,
        EtpGpuDetailsDlgProc
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

VOID EtShowGpuDetailsDialog(
    _In_ HWND ParentWindowHandle
    )
{
    PhCreateThread2(EtGpuDetailsDialogThreadStart, ParentWindowHandle);
}
