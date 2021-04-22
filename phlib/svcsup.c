/*
 * Process Hacker -
 *   service support functions
 *
 * Copyright (C) 2010-2012 wj32
 * Copyright (C) 2019 dmex
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

#include <ph.h>
#include <subprocesstag.h>
#include <svcsup.h>

#define SIP(String, Integer) \
    { (String), (PVOID)(Integer) }

static PH_KEY_VALUE_PAIR PhpServiceStatePairs[] =
{
    SIP(L"Stopped", SERVICE_STOPPED),
    SIP(L"Start pending", SERVICE_START_PENDING),
    SIP(L"Stop pending", SERVICE_STOP_PENDING),
    SIP(L"Running", SERVICE_RUNNING),
    SIP(L"Continue pending", SERVICE_CONTINUE_PENDING),
    SIP(L"Pause pending", SERVICE_PAUSE_PENDING),
    SIP(L"Paused", SERVICE_PAUSED)
};

static PH_KEY_VALUE_PAIR PhpServiceTypePairs[] =
{
    SIP(L"Driver", SERVICE_KERNEL_DRIVER),
    SIP(L"FS driver", SERVICE_FILE_SYSTEM_DRIVER),
    SIP(L"Own process", SERVICE_WIN32_OWN_PROCESS),
    SIP(L"Share process", SERVICE_WIN32_SHARE_PROCESS),
    SIP(L"Own interactive process", SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS),
    SIP(L"Share interactive process", SERVICE_WIN32_SHARE_PROCESS | SERVICE_INTERACTIVE_PROCESS),
    SIP(L"User own process", SERVICE_USER_OWN_PROCESS),
    SIP(L"User own process (instance)", SERVICE_USER_OWN_PROCESS | SERVICE_USERSERVICE_INSTANCE),
    SIP(L"User share process", SERVICE_USER_SHARE_PROCESS),
    SIP(L"User share process (instance)", SERVICE_USER_SHARE_PROCESS | SERVICE_USERSERVICE_INSTANCE)
};

static PH_KEY_VALUE_PAIR PhpServiceStartTypePairs[] =
{
    SIP(L"Disabled", SERVICE_DISABLED),
    SIP(L"Boot start", SERVICE_BOOT_START),
    SIP(L"System start", SERVICE_SYSTEM_START),
    SIP(L"Auto start", SERVICE_AUTO_START),
    SIP(L"Demand start", SERVICE_DEMAND_START)
};

static PH_KEY_VALUE_PAIR PhpServiceErrorControlPairs[] =
{
    SIP(L"Ignore", SERVICE_ERROR_IGNORE),
    SIP(L"Normal", SERVICE_ERROR_NORMAL),
    SIP(L"Severe", SERVICE_ERROR_SEVERE),
    SIP(L"Critical", SERVICE_ERROR_CRITICAL)
};

PWSTR PhServiceTypeStrings[10] =
{
    L"Driver",
    L"FS driver",
    L"Own process",
    L"Share process",
    L"Own interactive process",
    L"Share interactive process",
    L"User own process",
    L"User own process (instance)",
    L"User share process",
    L"User share process (instance)"
};

PWSTR PhServiceStartTypeStrings[5] =
{
    L"Disabled",
    L"Boot start",
    L"System start",
    L"Auto start",
    L"Demand start"
};

PWSTR PhServiceErrorControlStrings[4] =
{
    L"Ignore",
    L"Normal",
    L"Severe",
    L"Critical"
};

PVOID PhEnumServices(
    _In_ SC_HANDLE ScManagerHandle,
    _In_opt_ ULONG Type,
    _In_opt_ ULONG State,
    _Out_ PULONG Count
    )
{
    static ULONG initialBufferSize = 0x8000;
    LOGICAL result;
    PVOID buffer;
    ULONG bufferSize;
    ULONG returnLength;
    ULONG servicesReturned;

    if (!Type)
    {
        if (WindowsVersion >= WINDOWS_10_RS1)
        {
            Type = SERVICE_TYPE_ALL;
        }
        else if (WindowsVersion >= WINDOWS_10)
        {
            Type = SERVICE_WIN32 |
                SERVICE_ADAPTER |
                SERVICE_DRIVER |
                SERVICE_INTERACTIVE_PROCESS |
                SERVICE_USER_SERVICE |
                SERVICE_USERSERVICE_INSTANCE;
        }
        else
        {
            Type = SERVICE_DRIVER | SERVICE_WIN32;
        }
    }

    if (!State)
        State = SERVICE_STATE_ALL;

    bufferSize = initialBufferSize;
    buffer = PhAllocate(bufferSize);

    if (!(result = EnumServicesStatusEx(
        ScManagerHandle,
        SC_ENUM_PROCESS_INFO,
        Type,
        State,
        (PBYTE)buffer,
        bufferSize,
        &returnLength,
        &servicesReturned,
        NULL,
        NULL
        )))
    {
        if (GetLastError() == ERROR_MORE_DATA)
        {
            PhFree(buffer);
            bufferSize += returnLength;
            buffer = PhAllocate(bufferSize);

            result = EnumServicesStatusEx(
                ScManagerHandle,
                SC_ENUM_PROCESS_INFO,
                Type,
                State,
                (PBYTE)buffer,
                bufferSize,
                &returnLength,
                &servicesReturned,
                NULL,
                NULL
                );
        }

        if (!result)
        {
            PhFree(buffer);
            return NULL;
        }
    }

    if (bufferSize <= 0x20000) initialBufferSize = bufferSize;
    *Count = servicesReturned;

    return buffer;
}

SC_HANDLE PhOpenService(
    _In_ PWSTR ServiceName,
    _In_ ACCESS_MASK DesiredAccess
    )
{
    SC_HANDLE scManagerHandle;
    SC_HANDLE serviceHandle;

    scManagerHandle = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);

    if (!scManagerHandle)
        return NULL;

    serviceHandle = OpenService(scManagerHandle, ServiceName, DesiredAccess);
    CloseServiceHandle(scManagerHandle);

    return serviceHandle;
}

NTSTATUS PhOpenServiceEx(
    _In_ PWSTR ServiceName,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ SC_HANDLE ScManagerHandle,
    _Out_ SC_HANDLE* ServiceHandle
    )
{
    SC_HANDLE serviceHandle;

    if (ScManagerHandle)
    {
        if (serviceHandle = OpenService(ScManagerHandle, ServiceName, DesiredAccess))
        {
            *ServiceHandle = serviceHandle;
            return STATUS_SUCCESS;
        }

        return PhGetLastWin32ErrorAsNtStatus();
    }
    else
    {
        NTSTATUS status;
        SC_HANDLE scManagerHandle;

        if (!(scManagerHandle = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT)))
        {
            return PhGetLastWin32ErrorAsNtStatus();
        }

        if (serviceHandle = OpenService(ScManagerHandle, ServiceName, DesiredAccess))
        {
            *ServiceHandle = serviceHandle;
            status = STATUS_SUCCESS;
        }
        else
        {
            status = PhGetLastWin32ErrorAsNtStatus();
        }
        
        CloseServiceHandle(scManagerHandle);

        return status;
    }
}

PVOID PhGetServiceConfig(
    _In_ SC_HANDLE ServiceHandle
    )
{
    PVOID buffer;
    ULONG bufferSize = 0x200;

    buffer = PhAllocate(bufferSize);

    if (!QueryServiceConfig(ServiceHandle, buffer, bufferSize, &bufferSize))
    {
        PhFree(buffer);
        buffer = PhAllocate(bufferSize);

        if (!QueryServiceConfig(ServiceHandle, buffer, bufferSize, &bufferSize))
        {
            PhFree(buffer);
            return NULL;
        }
    }

    return buffer;
}

PVOID PhQueryServiceVariableSize(
    _In_ SC_HANDLE ServiceHandle,
    _In_ ULONG InfoLevel
    )
{
    PVOID buffer;
    ULONG bufferSize = 0x100;

    buffer = PhAllocate(bufferSize);

    if (!QueryServiceConfig2(
        ServiceHandle,
        InfoLevel,
        (BYTE *)buffer,
        bufferSize,
        &bufferSize
        ))
    {
        PhFree(buffer);
        buffer = PhAllocate(bufferSize);

        if (!QueryServiceConfig2(
            ServiceHandle,
            InfoLevel,
            (BYTE *)buffer,
            bufferSize,
            &bufferSize
            ))
        {
            PhFree(buffer);
            return NULL;
        }
    }

    return buffer;
}

PPH_STRING PhGetServiceDescription(
    _In_ SC_HANDLE ServiceHandle
    )
{
    PPH_STRING description = NULL;
    LPSERVICE_DESCRIPTION serviceDescription;

    serviceDescription = PhQueryServiceVariableSize(ServiceHandle, SERVICE_CONFIG_DESCRIPTION);

    if (serviceDescription)
    {
        if (serviceDescription->lpDescription)
            description = PhCreateString(serviceDescription->lpDescription);

        PhFree(serviceDescription);

        return description;
    }
    else
    {
        return NULL;
    }
}

BOOLEAN PhGetServiceDelayedAutoStart(
    _In_ SC_HANDLE ServiceHandle,
    _Out_ PBOOLEAN DelayedAutoStart
    )
{
    SERVICE_DELAYED_AUTO_START_INFO delayedAutoStartInfo;
    ULONG returnLength;

    if (QueryServiceConfig2(
        ServiceHandle,
        SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
        (BYTE *)&delayedAutoStartInfo,
        sizeof(SERVICE_DELAYED_AUTO_START_INFO),
        &returnLength
        ))
    {
        *DelayedAutoStart = !!delayedAutoStartInfo.fDelayedAutostart;
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

BOOLEAN PhSetServiceDelayedAutoStart(
    _In_ SC_HANDLE ServiceHandle,
    _In_ BOOLEAN DelayedAutoStart
    )
{
    SERVICE_DELAYED_AUTO_START_INFO delayedAutoStartInfo;

    delayedAutoStartInfo.fDelayedAutostart = DelayedAutoStart;

    return !!ChangeServiceConfig2(
        ServiceHandle,
        SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
        &delayedAutoStartInfo
        );
}

PWSTR PhGetServiceStateString(
    _In_ ULONG ServiceState
    )
{
    PWSTR string;

    if (PhFindStringSiKeyValuePairs(
        PhpServiceStatePairs,
        sizeof(PhpServiceStatePairs),
        ServiceState,
        &string
        ))
        return string;
    else
        return L"Unknown";
}

PWSTR PhGetServiceTypeString(
    _In_ ULONG ServiceType
    )
{
    PWSTR string;

    if (PhFindStringSiKeyValuePairs(
        PhpServiceTypePairs,
        sizeof(PhpServiceTypePairs),
        ServiceType,
        &string
        ))
        return string;
    else
        return L"Unknown";
}

ULONG PhGetServiceTypeInteger(
    _In_ PWSTR ServiceType
    )
{
    ULONG integer;

    if (PhFindIntegerSiKeyValuePairs(
        PhpServiceTypePairs,
        sizeof(PhpServiceTypePairs),
        ServiceType,
        &integer
        ))
        return integer;
    else
        return ULONG_MAX;
}

PWSTR PhGetServiceStartTypeString(
    _In_ ULONG ServiceStartType
    )
{
    PWSTR string;

    if (PhFindStringSiKeyValuePairs(
        PhpServiceStartTypePairs,
        sizeof(PhpServiceStartTypePairs),
        ServiceStartType,
        &string
        ))
        return string;
    else
        return L"Unknown";
}

ULONG PhGetServiceStartTypeInteger(
    _In_ PWSTR ServiceStartType
    )
{
    ULONG integer;

    if (PhFindIntegerSiKeyValuePairs(
        PhpServiceStartTypePairs,
        sizeof(PhpServiceStartTypePairs),
        ServiceStartType,
        &integer
        ))
        return integer;
    else
        return ULONG_MAX;
}

PWSTR PhGetServiceErrorControlString(
    _In_ ULONG ServiceErrorControl
    )
{
    PWSTR string;

    if (PhFindStringSiKeyValuePairs(
        PhpServiceErrorControlPairs,
        sizeof(PhpServiceErrorControlPairs),
        ServiceErrorControl,
        &string
        ))
        return string;
    else
        return L"Unknown";
}

ULONG PhGetServiceErrorControlInteger(
    _In_ PWSTR ServiceErrorControl
    )
{
    ULONG integer;

    if (PhFindIntegerSiKeyValuePairs(
        PhpServiceErrorControlPairs,
        sizeof(PhpServiceErrorControlPairs),
        ServiceErrorControl,
        &integer
        ))
        return integer;
    else
        return ULONG_MAX;
}

PPH_STRING PhGetServiceNameFromTag(
    _In_ HANDLE ProcessId,
    _In_ PVOID ServiceTag
    )
{
    static PQUERY_TAG_INFORMATION I_QueryTagInformation = NULL;
    PPH_STRING serviceName = NULL;
    TAG_INFO_NAME_FROM_TAG nameFromTag;

    if (!I_QueryTagInformation)
    {
        I_QueryTagInformation = PhGetDllProcedureAddress(L"advapi32.dll", "I_QueryTagInformation", 0);

        if (!I_QueryTagInformation)
            return NULL;
    }

    memset(&nameFromTag, 0, sizeof(TAG_INFO_NAME_FROM_TAG));
    nameFromTag.InParams.dwPid = HandleToUlong(ProcessId);
    nameFromTag.InParams.dwTag = PtrToUlong(ServiceTag);

    I_QueryTagInformation(NULL, eTagInfoLevelNameFromTag, &nameFromTag);

    if (nameFromTag.OutParams.pszName)
    {
        serviceName = PhCreateString(nameFromTag.OutParams.pszName);
        LocalFree(nameFromTag.OutParams.pszName);
    }

    return serviceName;
}

PPH_STRING PhGetServiceNameForModuleReference(
    _In_ HANDLE ProcessId,
    _In_ PWSTR ModuleName
    )
{
    static PQUERY_TAG_INFORMATION I_QueryTagInformation = NULL;
    PPH_STRING serviceNames = NULL;
    TAG_INFO_NAMES_REFERENCING_MODULE moduleNameRef;

    if (!I_QueryTagInformation)
    {
        I_QueryTagInformation = PhGetDllProcedureAddress(L"advapi32.dll", "I_QueryTagInformation", 0);

        if (!I_QueryTagInformation)
            return NULL;
    }

    memset(&moduleNameRef, 0, sizeof(TAG_INFO_NAMES_REFERENCING_MODULE));
    moduleNameRef.InParams.dwPid = HandleToUlong(ProcessId);
    moduleNameRef.InParams.pszModule = ModuleName;

    I_QueryTagInformation(NULL, eTagInfoLevelNamesReferencingModule, &moduleNameRef);

    if (moduleNameRef.OutParams.pmszNames)
    {
        PH_STRING_BUILDER sb;
        PWSTR serviceName;

        PhInitializeStringBuilder(&sb, 0x40);

        for (serviceName = moduleNameRef.OutParams.pmszNames; *serviceName; serviceName += PhCountStringZ(serviceName) + 1)
            PhAppendFormatStringBuilder(&sb, L"%s, ", serviceName);

        if (sb.String->Length != 0)
            PhRemoveEndStringBuilder(&sb, 2);

        serviceNames = PhFinalStringBuilderString(&sb);
        LocalFree(moduleNameRef.OutParams.pmszNames);
    }

    return serviceNames;
}

NTSTATUS PhGetThreadServiceTag(
    _In_ HANDLE ThreadHandle,
    _In_opt_ HANDLE ProcessHandle,
    _Out_ PVOID *ServiceTag
    )
{
    NTSTATUS status;
    THREAD_BASIC_INFORMATION basicInfo;
    BOOLEAN openedProcessHandle = FALSE;

    if (!NT_SUCCESS(status = PhGetThreadBasicInformation(ThreadHandle, &basicInfo)))
        return status;

    if (!ProcessHandle)
    {
        if (!NT_SUCCESS(status = PhOpenThreadProcess(
            ThreadHandle,
            PROCESS_VM_READ,
            &ProcessHandle
            )))
            return status;

        openedProcessHandle = TRUE;
    }

    status = NtReadVirtualMemory(
        ProcessHandle,
        PTR_ADD_OFFSET(basicInfo.TebBaseAddress, FIELD_OFFSET(TEB, SubProcessTag)),
        ServiceTag,
        sizeof(PVOID),
        NULL
        );

    if (openedProcessHandle)
        NtClose(ProcessHandle);

    return status;
}

NTSTATUS PhGetServiceDllParameter(
    _In_ ULONG ServiceType,
    _In_ PPH_STRINGREF ServiceName,
    _Out_ PPH_STRING *ServiceDll
    )
{
    static PH_STRINGREF servicesKeyName = PH_STRINGREF_INIT(L"System\\CurrentControlSet\\Services\\");
    static PH_STRINGREF parameters = PH_STRINGREF_INIT(L"\\Parameters");
    NTSTATUS status;
    HANDLE keyHandle;
    PPH_STRING keyName;

    if (ServiceType & SERVICE_USERSERVICE_INSTANCE)
    {
        PH_STRINGREF hostServiceName;
        PH_STRINGREF userSessionLuid;

        // The SCM creates multiple "user service instance" processes for each user session with the following template:
        // [Host Service Instance Name]_[LUID for Session]
        // The SCM internally uses the ServiceDll of the "host service instance" for all "user service instance" processes/services
        // and we need to parse the user service template and query the "host service instance" configuration. (hsebs)

        if (PhSplitStringRefAtLastChar(ServiceName, L'_', &hostServiceName, &userSessionLuid))
            keyName = PhConcatStringRef3(&servicesKeyName, &hostServiceName, &parameters);
        else
            keyName = PhConcatStringRef3(&servicesKeyName, ServiceName, &parameters);
    }
    else
    {
        keyName = PhConcatStringRef3(&servicesKeyName, ServiceName, &parameters);
    }

    if (NT_SUCCESS(status = PhOpenKey(
        &keyHandle,
        KEY_READ,
        PH_KEY_LOCAL_MACHINE,
        &keyName->sr,
        0
        )))
    {
        PPH_STRING serviceDllString;

        if (serviceDllString = PhQueryRegistryString(keyHandle, L"ServiceDll"))
        {
            PPH_STRING expandedString;

            if (expandedString = PhExpandEnvironmentStrings(&serviceDllString->sr))
            {
                *ServiceDll = expandedString;
                PhDereferenceObject(serviceDllString);
            }
            else
            {
                *ServiceDll = serviceDllString;
            }
        }
        else
        {
            status = STATUS_NOT_FOUND;
        }

        NtClose(keyHandle);
    }

    PhDereferenceObject(keyName);

    return status;
}
