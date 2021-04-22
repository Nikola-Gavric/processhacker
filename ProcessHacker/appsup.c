/*
 * Process Hacker -
 *   application support functions
 *
 * Copyright (C) 2010-2016 wj32
 * Copyright (C) 2017-2019 dmex
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

#include <winsta.h>
#include <dbghelp.h>
#include <appmodel.h>
#include <shellapi.h>

#include <cpysave.h>
#include <emenu.h>
#include <svcsup.h>
#include <symprv.h>
#include <settings.h>

#include <actions.h>
#include <phappres.h>
#include <phsvccl.h>
#include <phsettings.h>

#include "pcre/pcre2.h"

GUID XP_CONTEXT_GUID = { 0xbeb1b341, 0x6837, 0x4c83, { 0x83, 0x66, 0x2b, 0x45, 0x1e, 0x7c, 0xe6, 0x9b } };
GUID VISTA_CONTEXT_GUID = { 0xe2011457, 0x1546, 0x43c5, { 0xa5, 0xfe, 0x00, 0x8d, 0xee, 0xe3, 0xd3, 0xf0 } };
GUID WIN7_CONTEXT_GUID = { 0x35138b9a, 0x5d96, 0x4fbd, { 0x8e, 0x2d, 0xa2, 0x44, 0x02, 0x25, 0xf9, 0x3a } };
GUID WIN8_CONTEXT_GUID = { 0x4a2f28e3, 0x53b9, 0x4441, { 0xba, 0x9c, 0xd6, 0x9d, 0x4a, 0x4a, 0x6e, 0x38 } };
GUID WINBLUE_CONTEXT_GUID = { 0x1f676c76, 0x80e1, 0x4239, { 0x95, 0xbb, 0x83, 0xd0, 0xf6, 0xd0, 0xda, 0x78 } };
GUID WIN10_CONTEXT_GUID = { 0x8e0f7a12, 0xbfb3, 0x4fe8, { 0xb9, 0xa5, 0x48, 0xfd, 0x50, 0xa1, 0x5a, 0x9a } };

/**
 * Determines whether a process is suspended.
 *
 * \param Process The SYSTEM_PROCESS_INFORMATION structure
 * of the process.
 */
BOOLEAN PhGetProcessIsSuspended(
    _In_ PSYSTEM_PROCESS_INFORMATION Process
    )
{
    ULONG i;

    for (i = 0; i < Process->NumberOfThreads; i++)
    {
        if (
            Process->Threads[i].ThreadState != Waiting ||
            Process->Threads[i].WaitReason != Suspended
            )
            return FALSE;
    }

    return Process->NumberOfThreads != 0;
}

BOOLEAN PhIsProcessSuspended(
    _In_ HANDLE ProcessId
    )
{
    BOOLEAN suspended = FALSE;
    PVOID processes;
    PSYSTEM_PROCESS_INFORMATION process;

    if (NT_SUCCESS(PhEnumProcesses(&processes)))
    {
        if (process = PhFindProcessInformation(processes, ProcessId))
        {
            suspended = PhGetProcessIsSuspended(process);
        }

        PhFree(processes);
    }

    return suspended;
}

/**
 * Determines the OS compatibility context of a process.
 *
 * \param ProcessHandle A handle to a process.
 * \param Guid A variable which receives a GUID identifying an
 * operating system version.
 */
NTSTATUS PhGetProcessSwitchContext(
    _In_ HANDLE ProcessHandle,
    _Out_ PGUID Guid
    )
{
    NTSTATUS status;
    PROCESS_BASIC_INFORMATION basicInfo;
#ifdef _WIN64
    PVOID peb32;
    ULONG data32;
#endif
    PVOID data;

    // Reverse-engineered from WdcGetProcessSwitchContext (wdc.dll).
    // On Windows 8, the function is now SdbGetAppCompatData (apphelp.dll).
    // On Windows 10, the function is again WdcGetProcessSwitchContext.

#ifdef _WIN64
    if (NT_SUCCESS(PhGetProcessPeb32(ProcessHandle, &peb32)) && peb32)
    {
        if (WindowsVersion >= WINDOWS_8)
        {
            if (!NT_SUCCESS(status = NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(peb32, FIELD_OFFSET(PEB32, pShimData)),
                &data32,
                sizeof(ULONG),
                NULL
                )))
                return status;
        }
        else
        {
            if (!NT_SUCCESS(status = NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(peb32, FIELD_OFFSET(PEB32, pContextData)),
                &data32,
                sizeof(ULONG),
                NULL
                )))
                return status;
        }

        data = UlongToPtr(data32);
    }
    else
    {
#endif
        if (!NT_SUCCESS(status = PhGetProcessBasicInformation(ProcessHandle, &basicInfo)))
            return status;

        if (WindowsVersion >= WINDOWS_8)
        {
            if (!NT_SUCCESS(status = NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(basicInfo.PebBaseAddress, FIELD_OFFSET(PEB, pShimData)),
                &data,
                sizeof(PVOID),
                NULL
                )))
                return status;
        }
        else
        {
            if (!NT_SUCCESS(status = NtReadVirtualMemory(
                ProcessHandle,
                PTR_ADD_OFFSET(basicInfo.PebBaseAddress, FIELD_OFFSET(PEB, pUnused)),
                &data,
                sizeof(PVOID),
                NULL
                )))
                return status;
        }
#ifdef _WIN64
    }
#endif

    if (!data)
        return STATUS_UNSUCCESSFUL; // no compatibility context data

    if (WindowsVersion >= WINDOWS_10_RS5)
    {
        if (!NT_SUCCESS(status = NtReadVirtualMemory(
            ProcessHandle,
            PTR_ADD_OFFSET(data, 2040 + 24), // Magic value from SbReadProcContextByHandle
            Guid,
            sizeof(GUID),
            NULL
            )))
            return status;
    }
    else if (WindowsVersion >= WINDOWS_10_RS2)
    {
        if (!NT_SUCCESS(status = NtReadVirtualMemory(
            ProcessHandle,
            PTR_ADD_OFFSET(data, 1544),
            Guid,
            sizeof(GUID),
            NULL
            )))
            return status;
    }
    else if (WindowsVersion >= WINDOWS_10)
    {
        if (!NT_SUCCESS(status = NtReadVirtualMemory(
            ProcessHandle,
            PTR_ADD_OFFSET(data, 2040 + 24), // Magic value from SbReadProcContextByHandle
            Guid,
            sizeof(GUID),
            NULL
            )))
            return status;
    }
    else if (WindowsVersion >= WINDOWS_8_1)
    {
        if (!NT_SUCCESS(status = NtReadVirtualMemory(
            ProcessHandle,
            PTR_ADD_OFFSET(data, 2040 + 16), // Magic value from SbReadProcContextByHandle
            Guid,
            sizeof(GUID),
            NULL
            )))
            return status;
    }
    else if (WindowsVersion >= WINDOWS_8)
    {
        if (!NT_SUCCESS(status = NtReadVirtualMemory(
            ProcessHandle,
            PTR_ADD_OFFSET(data, 2040), // Magic value from SbReadProcContextByHandle
            Guid,
            sizeof(GUID),
            NULL
            )))
            return status;
    }
    else
    {
        if (!NT_SUCCESS(status = NtReadVirtualMemory(
            ProcessHandle,
            PTR_ADD_OFFSET(data, 32), // Magic value from WdcGetProcessSwitchContext
            Guid,
            sizeof(GUID),
            NULL
            )))
            return status;
    }

    return STATUS_SUCCESS;
}

/**
 * Determines the type of a process based on its image file name.
 *
 * \param ProcessHandle A handle to a process.
 * \param KnownProcessType A variable which receives the process
 * type.
 */
NTSTATUS PhGetProcessKnownType(
    _In_ HANDLE ProcessHandle,
    _Out_ PH_KNOWN_PROCESS_TYPE *KnownProcessType
    )
{
    NTSTATUS status;
    PROCESS_BASIC_INFORMATION basicInfo;
    PPH_STRING fileName;
    PPH_STRING newFileName;

    if (!NT_SUCCESS(status = PhGetProcessBasicInformation(
        ProcessHandle,
        &basicInfo
        )))
        return status;

    if (basicInfo.UniqueProcessId == SYSTEM_PROCESS_ID)
    {
        *KnownProcessType = SystemProcessType;
        return STATUS_SUCCESS;
    }

    if (!NT_SUCCESS(status = PhGetProcessImageFileName(
        ProcessHandle,
        &fileName
        )))
    {
        return status;
    }

    newFileName = PhGetFileName(fileName);
    PhDereferenceObject(fileName);

    *KnownProcessType = PhGetProcessKnownTypeEx(
        basicInfo.UniqueProcessId, 
        newFileName
        );

    PhDereferenceObject(newFileName);

    return status;
}

PH_KNOWN_PROCESS_TYPE PhGetProcessKnownTypeEx(
    _In_ HANDLE ProcessId,
    _In_ PPH_STRING FileName
    )
{
    PH_KNOWN_PROCESS_TYPE knownProcessType;
    PH_STRINGREF systemRootPrefix;
    PPH_STRING fileName;
    PH_STRINGREF name;
#ifdef _WIN64
    BOOLEAN isWow64 = FALSE;
#endif

    if (ProcessId == SYSTEM_PROCESS_ID || ProcessId == SYSTEM_IDLE_PROCESS_ID)
        return SystemProcessType;

    if (PhIsNullOrEmptyString(FileName))
        return UnknownProcessType;

    PhGetSystemRoot(&systemRootPrefix);

    fileName = PhReferenceObject(FileName);
    name = fileName->sr;

    knownProcessType = UnknownProcessType;

    if (PhStartsWithStringRef(&name, &systemRootPrefix, TRUE))
    {
        // Skip the system root, and we now have three cases:
        // 1. \\xyz.exe - Windows executable.
        // 2. \\System32\\xyz.exe - system32 executable.
        // 3. \\SysWow64\\xyz.exe - system32 executable + WOW64.
        PhSkipStringRef(&name, systemRootPrefix.Length);

        if (PhEqualStringRef2(&name, L"\\explorer.exe", TRUE))
        {
            knownProcessType = ExplorerProcessType;
        }
        else if (
            PhStartsWithStringRef2(&name, L"\\System32", TRUE)
#ifdef _WIN64
            || (PhStartsWithStringRef2(&name, L"\\SysWow64", TRUE) && (isWow64 = TRUE, TRUE)) // ugly but necessary
#endif
            )
        {
            // SysTem32 and SysWow64 are both 8 characters long.
            PhSkipStringRef(&name, 9 * sizeof(WCHAR));

            if (FALSE)
                ; // Dummy
            else if (PhEqualStringRef2(&name, L"\\smss.exe", TRUE))
                knownProcessType = SessionManagerProcessType;
            else if (PhEqualStringRef2(&name, L"\\csrss.exe", TRUE))
                knownProcessType = WindowsSubsystemProcessType;
            else if (PhEqualStringRef2(&name, L"\\wininit.exe", TRUE))
                knownProcessType = WindowsStartupProcessType;
            else if (PhEqualStringRef2(&name, L"\\services.exe", TRUE))
                knownProcessType = ServiceControlManagerProcessType;
            else if (PhEqualStringRef2(&name, L"\\lsass.exe", TRUE))
                knownProcessType = LocalSecurityAuthorityProcessType;
            else if (PhEqualStringRef2(&name, L"\\lsm.exe", TRUE))
                knownProcessType = LocalSessionManagerProcessType;
            else if (PhEqualStringRef2(&name, L"\\winlogon.exe", TRUE))
                knownProcessType = WindowsLogonProcessType;
            else if (PhEqualStringRef2(&name, L"\\svchost.exe", TRUE))
                knownProcessType = ServiceHostProcessType;
            else if (PhEqualStringRef2(&name, L"\\rundll32.exe", TRUE))
                knownProcessType = RunDllAsAppProcessType;
            else if (PhEqualStringRef2(&name, L"\\dllhost.exe", TRUE))
                knownProcessType = ComSurrogateProcessType;
            else if (PhEqualStringRef2(&name, L"\\taskeng.exe", TRUE))
                knownProcessType = TaskHostProcessType;
            else if (PhEqualStringRef2(&name, L"\\taskhost.exe", TRUE))
                knownProcessType = TaskHostProcessType;
            else if (PhEqualStringRef2(&name, L"\\taskhostex.exe", TRUE))
                knownProcessType = TaskHostProcessType;
            else if (PhEqualStringRef2(&name, L"\\taskhostw.exe", TRUE))
                knownProcessType = TaskHostProcessType;
            else if (PhEqualStringRef2(&name, L"\\wudfhost.exe", TRUE))
                knownProcessType = UmdfHostProcessType;
            else if (PhEqualStringRef2(&name, L"\\wbem\\WmiPrvSE.exe", TRUE))
                knownProcessType = WmiProviderHostType;
            else if (PhEqualStringRef2(&name, L"\\MicrosoftEdgeCP.exe", TRUE)) // RS5
                knownProcessType = EdgeProcessType;
            else if (PhEqualStringRef2(&name, L"\\MicrosoftEdgeSH.exe", TRUE)) // RS5
                knownProcessType = EdgeProcessType;
        }
        else
        {
            if (PhEndsWithStringRef2(&name, L"\\MicrosoftEdgeCP.exe", TRUE)) // RS4
                knownProcessType = EdgeProcessType;
            else if (PhEndsWithStringRef2(&name, L"\\MicrosoftEdge.exe", TRUE))
                knownProcessType = EdgeProcessType;
            else if (PhEndsWithStringRef2(&name, L"\\ServiceWorkerHost.exe", TRUE))
                knownProcessType = EdgeProcessType;
            else if (PhEndsWithStringRef2(&name, L"\\Windows.WARP.JITService.exe", TRUE))
                knownProcessType = EdgeProcessType;
        }
    }

    PhDereferenceObject(fileName);

#ifdef _WIN64
    if (isWow64)
        knownProcessType |= KnownProcessWow64;
#endif

    return knownProcessType;
}

static BOOLEAN NTAPI PhpSvchostCommandLineCallback(
    _In_opt_ PPH_COMMAND_LINE_OPTION Option,
    _In_opt_ PPH_STRING Value,
    _In_opt_ PVOID Context
    )
{
    PPH_KNOWN_PROCESS_COMMAND_LINE knownCommandLine = Context;

    if (knownCommandLine && Option && Option->Id == 1)
    {
        PhSwapReference(&knownCommandLine->ServiceHost.GroupName, Value);
    }

    return TRUE;
}

_Success_(return)
BOOLEAN PhaGetProcessKnownCommandLine(
    _In_ PPH_STRING CommandLine,
    _In_ PH_KNOWN_PROCESS_TYPE KnownProcessType,
    _Out_ PPH_KNOWN_PROCESS_COMMAND_LINE KnownCommandLine
    )
{
    switch (KnownProcessType & KnownProcessTypeMask)
    {
    case ServiceHostProcessType:
        {
            // svchost.exe -k <GroupName>

            static PH_COMMAND_LINE_OPTION options[] =
            {
                { 1, L"k", MandatoryArgumentType }
            };

            KnownCommandLine->ServiceHost.GroupName = NULL;

            PhParseCommandLine(
                &CommandLine->sr,
                options,
                sizeof(options) / sizeof(PH_COMMAND_LINE_OPTION),
                PH_COMMAND_LINE_IGNORE_UNKNOWN_OPTIONS,
                PhpSvchostCommandLineCallback,
                KnownCommandLine
                );

            if (KnownCommandLine->ServiceHost.GroupName)
            {
                PH_AUTO(KnownCommandLine->ServiceHost.GroupName);
                return TRUE;
            }
            else
            {
                return FALSE;
            }
        }
        break;
    case RunDllAsAppProcessType:
        {
            // rundll32.exe <DllName>,<ProcedureName> ...

            SIZE_T i;
            PH_STRINGREF dllNamePart;
            PH_STRINGREF procedureNamePart;
            PPH_STRING dllName;
            PPH_STRING procedureName;

            i = 0;

            // Get the rundll32.exe part.

            dllName = PhParseCommandLinePart(&CommandLine->sr, &i);

            if (!dllName)
                return FALSE;

            PhDereferenceObject(dllName);

            // Get the DLL name part.

            while (i < CommandLine->Length / sizeof(WCHAR) && CommandLine->Buffer[i] == ' ')
                i++;

            dllName = PhParseCommandLinePart(&CommandLine->sr, &i);

            if (!dllName)
                return FALSE;

            PH_AUTO(dllName);

            // The procedure name begins after the last comma.

            if (!PhSplitStringRefAtLastChar(&dllName->sr, ',', &dllNamePart, &procedureNamePart))
                return FALSE;

            dllName = PH_AUTO(PhCreateString2(&dllNamePart));
            procedureName = PH_AUTO(PhCreateString2(&procedureNamePart));

            // If the DLL name isn't an absolute path, assume it's in system32.
            // TODO: Use a proper search function.

            if (RtlDetermineDosPathNameType_U(dllName->Buffer) == RtlPathTypeRelative)
            {
                dllName = PhaConcatStrings(
                    3,
                    PH_AUTO_T(PH_STRING, PhGetSystemDirectory())->Buffer,
                    L"\\",
                    dllName->Buffer
                    );
            }

            KnownCommandLine->RunDllAsApp.FileName = dllName;
            KnownCommandLine->RunDllAsApp.ProcedureName = procedureName;
        }
        break;
    case ComSurrogateProcessType:
        {
            // dllhost.exe /processid:<Guid>

            static PH_STRINGREF inprocServer32Name = PH_STRINGREF_INIT(L"InprocServer32");

            SIZE_T i;
            ULONG_PTR indexOfProcessId;
            PPH_STRING argPart;
            PPH_STRING guidString;
            UNICODE_STRING guidStringUs;
            GUID guid;
            HANDLE rootKeyHandle;
            HANDLE inprocServer32KeyHandle;
            PPH_STRING fileName;

            i = 0;

            // Get the dllhost.exe part.

            argPart = PhParseCommandLinePart(&CommandLine->sr, &i);

            if (!argPart)
                return FALSE;

            PhDereferenceObject(argPart);

            // Get the argument part.

            while (i < (ULONG)CommandLine->Length / sizeof(WCHAR) && CommandLine->Buffer[i] == ' ')
                i++;

            argPart = PhParseCommandLinePart(&CommandLine->sr, &i);

            if (!argPart)
                return FALSE;

            PH_AUTO(argPart);

            // Find "/processid:"; the GUID is just after that.

            PhUpperString(argPart);
            indexOfProcessId = PhFindStringInString(argPart, 0, L"/PROCESSID:");

            if (indexOfProcessId == -1)
                return FALSE;

            guidString = PhaSubstring(
                argPart,
                indexOfProcessId + 11,
                (ULONG)argPart->Length / sizeof(WCHAR) - indexOfProcessId - 11
                );
            PhStringRefToUnicodeString(&guidString->sr, &guidStringUs);

            if (!NT_SUCCESS(RtlGUIDFromString(
                &guidStringUs,
                &guid
                )))
                return FALSE;

            KnownCommandLine->ComSurrogate.Guid = guid;
            KnownCommandLine->ComSurrogate.Name = NULL;
            KnownCommandLine->ComSurrogate.FileName = NULL;

            // Lookup the GUID in the registry to determine the name and file name.

            if (NT_SUCCESS(PhOpenKey(
                &rootKeyHandle,
                KEY_READ,
                PH_KEY_CLASSES_ROOT,
                &PhaConcatStrings2(L"CLSID\\", guidString->Buffer)->sr,
                0
                )))
            {
                KnownCommandLine->ComSurrogate.Name =
                    PH_AUTO(PhQueryRegistryString(rootKeyHandle, NULL));

                if (NT_SUCCESS(PhOpenKey(
                    &inprocServer32KeyHandle,
                    KEY_READ,
                    rootKeyHandle,
                    &inprocServer32Name,
                    0
                    )))
                {
                    KnownCommandLine->ComSurrogate.FileName =
                        PH_AUTO(PhQueryRegistryString(inprocServer32KeyHandle, NULL));

                    if (fileName = PH_AUTO(PhExpandEnvironmentStrings(
                        &KnownCommandLine->ComSurrogate.FileName->sr
                        )))
                    {
                        KnownCommandLine->ComSurrogate.FileName = fileName;
                    }

                    NtClose(inprocServer32KeyHandle);
                }

                NtClose(rootKeyHandle);
            }
            else if (NT_SUCCESS(PhOpenKey(
                &rootKeyHandle,
                KEY_READ,
                PH_KEY_CLASSES_ROOT,
                &PhaConcatStrings2(L"AppID\\", guidString->Buffer)->sr,
                0
                )))
            {
                KnownCommandLine->ComSurrogate.Name =
                    PH_AUTO(PhQueryRegistryString(rootKeyHandle, NULL));
                NtClose(rootKeyHandle);
            }
        }
        break;
    default:
        return FALSE;
    }

    return TRUE;
}

PPH_STRING PhGetServiceRelevantFileName(
    _In_ PPH_STRINGREF ServiceName,
    _In_ SC_HANDLE ServiceHandle
    )
{
    PPH_STRING fileName = NULL;
    LPQUERY_SERVICE_CONFIG config;

    if (config = PhGetServiceConfig(ServiceHandle))
    {
        PhGetServiceDllParameter(config->dwServiceType, ServiceName, &fileName);

        if (!fileName)
        {
            PPH_STRING commandLine;

            if (config->lpBinaryPathName[0])
            {
                commandLine = PhCreateString(config->lpBinaryPathName);

                if (config->dwServiceType & SERVICE_WIN32)
                {
                    PH_STRINGREF dummyFileName;
                    PH_STRINGREF dummyArguments;

                    PhParseCommandLineFuzzy(&commandLine->sr, &dummyFileName, &dummyArguments, &fileName);

                    if (!fileName)
                        PhSwapReference(&fileName, commandLine);
                }
                else
                {
                    fileName = PhGetFileName(commandLine);
                }

                PhDereferenceObject(commandLine);
            }
        }

        PhFree(config);
    }

    return fileName;
}

PPH_STRING PhEscapeStringForDelimiter(
    _In_ PPH_STRING String,
    _In_ WCHAR Delimiter
    )
{
    PH_STRING_BUILDER stringBuilder;
    SIZE_T length;
    SIZE_T i;
    WCHAR temp[2];

    length = String->Length / sizeof(WCHAR);
    PhInitializeStringBuilder(&stringBuilder, String->Length / sizeof(WCHAR) * 3);

    temp[0] = '\\';

    for (i = 0; i < length; i++)
    {
        if (String->Buffer[i] == '\\' || String->Buffer[i] == Delimiter)
        {
            temp[1] = String->Buffer[i];
            PhAppendStringBuilderEx(&stringBuilder, temp, 4);
        }
        else
        {
            PhAppendCharStringBuilder(&stringBuilder, String->Buffer[i]);
        }
    }

    return PhFinalStringBuilderString(&stringBuilder);
}

PPH_STRING PhUnescapeStringForDelimiter(
    _In_ PPH_STRING String,
    _In_ WCHAR Delimiter
    )
{
    PH_STRING_BUILDER stringBuilder;
    SIZE_T length;
    SIZE_T i;

    length = String->Length / sizeof(WCHAR);
    PhInitializeStringBuilder(&stringBuilder, String->Length / sizeof(WCHAR) * 3);

    for (i = 0; i < length; i++)
    {
        if (String->Buffer[i] == '\\')
        {
            if (i != length - 1)
            {
                PhAppendCharStringBuilder(&stringBuilder, String->Buffer[i + 1]);
                i++;
            }
            else
            {
                // Trailing backslash. Just ignore it.
                break;
            }
        }
        else
        {
            PhAppendCharStringBuilder(&stringBuilder, String->Buffer[i]);
        }
    }

    return PhFinalStringBuilderString(&stringBuilder);
}

VOID PhSearchOnlineString(
    _In_ HWND hWnd,
    _In_ PWSTR String
    )
{
    PhShellExecuteUserString(hWnd, L"SearchEngine", String, TRUE, NULL);
}

VOID PhShellExecuteUserString(
    _In_ HWND hWnd,
    _In_ PWSTR Setting,
    _In_ PWSTR String,
    _In_ BOOLEAN UseShellExecute,
    _In_opt_ PWSTR ErrorMessage
    )
{
    static PH_STRINGREF replacementToken = PH_STRINGREF_INIT(L"%s");
    PPH_STRING applicationDirectory;
    PPH_STRING executeString;
    PH_STRINGREF stringBefore;
    PH_STRINGREF stringAfter;
    PPH_STRING ntMessage;

    if (!(applicationDirectory = PhGetApplicationDirectory()))
    {
        PhShowStatus(hWnd, L"Unable to locate the application directory.", STATUS_NOT_FOUND, 0);
        return;
    }

    // Get the execute command.
    executeString = PhGetStringSetting(Setting);

    // Expand environment strings.
    PhMoveReference(&executeString, PhExpandEnvironmentStrings(&executeString->sr));

    // Make sure the user executable string is absolute. We can't use RtlDetermineDosPathNameType_U
    // here because the string may be a URL.
    if (PhFindCharInString(executeString, 0, ':') == -1)
    {
        INT stringArgCount;
        PWSTR* stringArgList;

        // (dmex) HACK: Escape the individual executeString components.
        if ((stringArgList = CommandLineToArgvW(executeString->Buffer, &stringArgCount)) && stringArgCount == 2)
        {
            PPH_STRING fileName = PhCreateString(stringArgList[0]);
            PPH_STRING fileArgs = PhCreateString(stringArgList[1]);

            // Make sure the string is absolute and escape the filename.
            if (RtlDetermineDosPathNameType_U(fileName->Buffer) == RtlPathTypeRelative)
                PhMoveReference(&fileName, PhConcatStrings(4, L"\"", applicationDirectory->Buffer, fileName->Buffer, L"\""));
            else
                PhMoveReference(&fileName, PhConcatStrings(3, L"\"", fileName->Buffer, L"\""));

            // Escape the parameters.
            PhMoveReference(&fileArgs, PhConcatStrings(3, L"\"", fileArgs->Buffer, L"\""));

            // Create the escaped execute string.
            PhMoveReference(&executeString, PhConcatStrings(3, fileName->Buffer, L" ", fileArgs->Buffer));

            PhDereferenceObject(fileArgs);
            PhDereferenceObject(fileName);
            LocalFree(stringArgList);
        }
        else
        {
            if (RtlDetermineDosPathNameType_U(executeString->Buffer) == RtlPathTypeRelative)
                PhMoveReference(&executeString, PhConcatStrings(4, L"\"", applicationDirectory->Buffer, executeString->Buffer, L"\""));
            else
                PhMoveReference(&executeString, PhConcatStrings(3, L"\"", executeString->Buffer, L"\""));
        }
    }

    // Replace the token with the string, or use the original string if the token is not present.
    if (PhSplitStringRefAtString(&executeString->sr, &replacementToken, FALSE, &stringBefore, &stringAfter))
    {
        PPH_STRING stringTemp;
        PPH_STRING stringMiddle;

        // Note: This code is needed to solve issues with faulty RamDisk software that doesn't use the Mount Manager API
        // and instead returns \device\ FileName strings. We also can't change the way the process provider stores 
        // the FileName string since it'll break various features and use-cases required by developers 
        // who need the raw untranslated FileName string.
        stringTemp = PhCreateString(String);
        stringMiddle = PhGetFileName(stringTemp);

        PhMoveReference(&executeString, PhConcatStringRef3(&stringBefore, &stringMiddle->sr, &stringAfter));

        PhDereferenceObject(stringMiddle);
        PhDereferenceObject(stringTemp);
    }

    if (UseShellExecute)
    {
        PhShellExecute(hWnd, executeString->Buffer, NULL);
    }
    else
    {
        NTSTATUS status;

        status = PhCreateProcessWin32(NULL, executeString->Buffer, NULL, NULL, 0, NULL, NULL, NULL);

        if (!NT_SUCCESS(status))
        {
            if (ErrorMessage)
            {
                ntMessage = PhGetNtMessage(status);
                PhShowError2(
                    hWnd,
                    L"Unable to execute the command.",
                    L"%s\n%s",
                    PhGetStringOrDefault(ntMessage, L"An unknown error occurred."),
                    ErrorMessage
                    );
                PhDereferenceObject(ntMessage);
            }
            else
            {
                PhShowStatus(hWnd, L"Unable to execute the command.", status, 0);
            }
        }
    }

    PhDereferenceObject(executeString);
    PhDereferenceObject(applicationDirectory);
}

VOID PhLoadSymbolProviderOptions(
    _Inout_ PPH_SYMBOL_PROVIDER SymbolProvider
    )
{
    PPH_STRING searchPath;

    PhSetOptionsSymbolProvider(
        SYMOPT_UNDNAME,
        PhGetIntegerSetting(L"DbgHelpUndecorate") ? SYMOPT_UNDNAME : 0
        );

    searchPath = PhGetStringSetting(L"DbgHelpSearchPath");

    if (searchPath->Length != 0)
        PhSetSearchPathSymbolProvider(SymbolProvider, searchPath->Buffer);

    PhDereferenceObject(searchPath);
}

/**
 * Copies a string into a NMLVGETINFOTIP structure.
 *
 * \param GetInfoTip The NMLVGETINFOTIP structure.
 * \param Tip The string to copy.
 *
 * \remarks The text is truncated if it is too long.
 */
VOID PhCopyListViewInfoTip(
    _Inout_ LPNMLVGETINFOTIP GetInfoTip,
    _In_ PPH_STRINGREF Tip
    )
{
    ULONG copyIndex;
    ULONG bufferRemaining;
    ULONG copyLength;

    if (GetInfoTip->dwFlags == 0)
    {
        copyIndex = (ULONG)PhCountStringZ(GetInfoTip->pszText) + 1; // plus one for newline

        if (GetInfoTip->cchTextMax - copyIndex < 2) // need at least two bytes
            return;

        bufferRemaining = GetInfoTip->cchTextMax - copyIndex - 1;
        GetInfoTip->pszText[copyIndex - 1] = '\n';
    }
    else
    {
        copyIndex = 0;
        bufferRemaining = GetInfoTip->cchTextMax;
    }

    copyLength = min((ULONG)Tip->Length / sizeof(WCHAR), bufferRemaining - 1);
    memcpy(
        &GetInfoTip->pszText[copyIndex],
        Tip->Buffer,
        copyLength * 2
        );
    GetInfoTip->pszText[copyIndex + copyLength] = UNICODE_NULL;
}

VOID PhCopyListView(
    _In_ HWND ListViewHandle
    )
{
    PPH_STRING text;

    text = PhGetListViewText(ListViewHandle);
    PhSetClipboardString(ListViewHandle, &text->sr);
    PhDereferenceObject(text);
}

VOID PhHandleListViewNotifyForCopy(
    _In_ LPARAM lParam,
    _In_ HWND ListViewHandle
    )
{
    PhHandleListViewNotifyBehaviors(lParam, ListViewHandle, PH_LIST_VIEW_CTRL_C_BEHAVIOR);
}

VOID PhHandleListViewNotifyBehaviors(
    _In_ LPARAM lParam,
    _In_ HWND ListViewHandle,
    _In_ ULONG Behaviors
    )
{
    if (((LPNMHDR)lParam)->hwndFrom == ListViewHandle && ((LPNMHDR)lParam)->code == LVN_KEYDOWN)
    {
        LPNMLVKEYDOWN keyDown = (LPNMLVKEYDOWN)lParam;

        switch (keyDown->wVKey)
        {
        case 'C':
            if (Behaviors & PH_LIST_VIEW_CTRL_C_BEHAVIOR)
            {
                if (GetKeyState(VK_CONTROL) < 0)
                    PhCopyListView(ListViewHandle);
            }
            break;
        case 'A':
            if (Behaviors & PH_LIST_VIEW_CTRL_A_BEHAVIOR)
            {
                if (GetKeyState(VK_CONTROL) < 0)
                    PhSetStateAllListViewItems(ListViewHandle, LVIS_SELECTED, LVIS_SELECTED);
            }
            break;
        }
    }
}

BOOLEAN PhGetListViewContextMenuPoint(
    _In_ HWND ListViewHandle,
    _Out_ PPOINT Point
    )
{
    INT selectedIndex;
    RECT bounds;
    RECT clientRect;

    // The user pressed a key to display the context menu.
    // Suggest where the context menu should display.

    if ((selectedIndex = ListView_GetNextItem(ListViewHandle, -1, LVNI_SELECTED)) != -1)
    {
        if (ListView_GetItemRect(ListViewHandle, selectedIndex, &bounds, LVIR_BOUNDS))
        {
            Point->x = bounds.left + PhSmallIconSize.X / 2;
            Point->y = bounds.top + PhSmallIconSize.Y / 2;

            GetClientRect(ListViewHandle, &clientRect);

            if (Point->x < 0 || Point->y < 0 || Point->x >= clientRect.right || Point->y >= clientRect.bottom)
            {
                // The menu is going to be outside of the control. Just put it at the top-left.
                Point->x = 0;
                Point->y = 0;
            }

            ClientToScreen(ListViewHandle, Point);

            return TRUE;
        }
    }

    Point->x = 0;
    Point->y = 0;
    ClientToScreen(ListViewHandle, Point);

    return FALSE;
}

VOID PhSetWindowOpacity(
    _In_ HWND WindowHandle,
    _In_ ULONG OpacityPercent
    )
{
    if (OpacityPercent == 0)
    {
        // Make things a bit faster by removing the WS_EX_LAYERED bit.
        PhSetWindowExStyle(WindowHandle, WS_EX_LAYERED, 0);
        RedrawWindow(WindowHandle, NULL, NULL, RDW_ERASE | RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
        return;
    }

    PhSetWindowExStyle(WindowHandle, WS_EX_LAYERED, WS_EX_LAYERED);

    // Disallow opacity values of less than 10%.
    OpacityPercent = min(OpacityPercent, 90);

    // The opacity value is backwards - 0 means opaque, 100 means transparent.
    SetLayeredWindowAttributes(
        WindowHandle,
        0,
        (BYTE)(255 * (100 - OpacityPercent) / 100),
        LWA_ALPHA
        );
}

PPH_STRING PhGetPhVersion(
    VOID
    )
{
    PH_FORMAT format[5];

    PhInitFormatU(&format[0], PHAPP_VERSION_MAJOR);
    PhInitFormatC(&format[1], '.');
    PhInitFormatU(&format[2], PHAPP_VERSION_MINOR);
    PhInitFormatC(&format[3], '.');
    PhInitFormatU(&format[4], PHAPP_VERSION_REVISION);

    return PhFormat(format, 5, 16);
}

VOID PhGetPhVersionNumbers(
    _Out_opt_ PULONG MajorVersion,
    _Out_opt_ PULONG MinorVersion,
    _Out_opt_ PULONG BuildNumber,
    _Out_opt_ PULONG RevisionNumber
    )
{
    if (MajorVersion)
        *MajorVersion = PHAPP_VERSION_MAJOR;
    if (MinorVersion)
        *MinorVersion = PHAPP_VERSION_MINOR;
    if (BuildNumber)
        *BuildNumber = PHAPP_VERSION_BUILD;
    if (RevisionNumber)
        *RevisionNumber = PHAPP_VERSION_REVISION;
}

PPH_STRING PhGetPhVersionHash(
    VOID
    )
{
    return PhConvertUtf8ToUtf16(PHAPP_VERSION_COMMIT);
}

VOID PhWritePhTextHeader(
    _Inout_ PPH_FILE_STREAM FileStream
    )
{
    PPH_STRING version;
    LARGE_INTEGER time;
    SYSTEMTIME systemTime;
    PPH_STRING timeString;

    PhWriteStringAsUtf8FileStream2(FileStream, L"Process Hacker ");

    if (version = PhGetPhVersion())
    {
        PhWriteStringAsUtf8FileStream(FileStream, &version->sr);
        PhDereferenceObject(version);
    }

    PhWriteStringFormatAsUtf8FileStream(FileStream, L"\r\nWindows NT %lu.%lu", PhOsVersion.dwMajorVersion, PhOsVersion.dwMinorVersion);

    if (PhOsVersion.szCSDVersion[0] != UNICODE_NULL)
        PhWriteStringFormatAsUtf8FileStream(FileStream, L" %s", PhOsVersion.szCSDVersion);

#ifdef _WIN64
    PhWriteStringAsUtf8FileStream2(FileStream, L" (64-bit)");
#else
    PhWriteStringAsUtf8FileStream2(FileStream, L" (32-bit)");
#endif

    PhQuerySystemTime(&time);
    PhLargeIntegerToLocalSystemTime(&systemTime, &time);

    timeString = PhFormatDateTime(&systemTime);
    PhWriteStringFormatAsUtf8FileStream(FileStream, L"\r\n%s\r\n\r\n", timeString->Buffer);
    PhDereferenceObject(timeString);
}

BOOLEAN PhShellProcessHacker(
    _In_opt_ HWND hWnd,
    _In_opt_ PWSTR Parameters,
    _In_ ULONG ShowWindowType,
    _In_ ULONG Flags,
    _In_ ULONG AppFlags,
    _In_opt_ ULONG Timeout,
    _Out_opt_ PHANDLE ProcessHandle
    )
{
    return PhShellProcessHackerEx(
        hWnd,
        NULL,
        Parameters,
        ShowWindowType,
        Flags,
        AppFlags,
        Timeout,
        ProcessHandle
        );
}

VOID PhpAppendCommandLineArgument(
    _Inout_ PPH_STRING_BUILDER StringBuilder,
    _In_ PWSTR Name,
    _In_ PPH_STRINGREF Value
    )
{
    PPH_STRING temp;

    PhAppendStringBuilder2(StringBuilder, L" -");
    PhAppendStringBuilder2(StringBuilder, Name);
    PhAppendStringBuilder2(StringBuilder, L" \"");
    temp = PhEscapeCommandLinePart(Value);
    PhAppendStringBuilder(StringBuilder, &temp->sr);
    PhDereferenceObject(temp);
    PhAppendCharStringBuilder(StringBuilder, '\"');
}

BOOLEAN PhShellProcessHackerEx(
    _In_opt_ HWND hWnd,
    _In_opt_ PWSTR FileName,
    _In_opt_ PWSTR Parameters,
    _In_ ULONG ShowWindowType,
    _In_ ULONG Flags,
    _In_ ULONG AppFlags,
    _In_opt_ ULONG Timeout,
    _Out_opt_ PHANDLE ProcessHandle
    )
{
    BOOLEAN result;
    PPH_STRING applicationFileName;
    PH_STRING_BUILDER sb;
    PWSTR parameters;

    if (!(applicationFileName = PhGetApplicationFileName()))
        return FALSE;

    if (AppFlags & PH_SHELL_APP_PROPAGATE_PARAMETERS)
    {
        PhInitializeStringBuilder(&sb, 128);

        // Propagate parameters.

        if (PhStartupParameters.NoSettings)
        {
            PhAppendStringBuilder2(&sb, L" -nosettings");
        }
        else if (PhStartupParameters.SettingsFileName && (PhSettingsFileName || (AppFlags & PH_SHELL_APP_PROPAGATE_PARAMETERS_FORCE_SETTINGS)))
        {
            PPH_STRINGREF fileName;

            if (PhSettingsFileName)
                fileName = &PhSettingsFileName->sr;
            else
                fileName = &PhStartupParameters.SettingsFileName->sr;

            PhpAppendCommandLineArgument(&sb, L"settings", fileName);
        }

        if (PhStartupParameters.NoKph)
            PhAppendStringBuilder2(&sb, L" -nokph");
        if (PhStartupParameters.InstallKph)
            PhAppendStringBuilder2(&sb, L" -installkph");
        if (PhStartupParameters.UninstallKph)
            PhAppendStringBuilder2(&sb, L" -uninstallkph");
        if (PhStartupParameters.Debug)
            PhAppendStringBuilder2(&sb, L" -debug");
        if (PhStartupParameters.NoPlugins)
            PhAppendStringBuilder2(&sb, L" -noplugins");
        if (PhStartupParameters.NewInstance)
            PhAppendStringBuilder2(&sb, L" -newinstance");

        if (PhStartupParameters.SelectPid != 0)
            PhAppendFormatStringBuilder(&sb, L" -selectpid %lu", PhStartupParameters.SelectPid);

        if (PhStartupParameters.PriorityClass != 0)
        {
            CHAR value = 0;

            switch (PhStartupParameters.PriorityClass)
            {
            case PROCESS_PRIORITY_CLASS_REALTIME:
                value = L'r';
                break;
            case PROCESS_PRIORITY_CLASS_HIGH:
                value = L'h';
                break;
            case PROCESS_PRIORITY_CLASS_NORMAL:
                value = L'n';
                break;
            case PROCESS_PRIORITY_CLASS_IDLE:
                value = L'l';
                break;
            }

            if (value != 0)
            {
                PhAppendStringBuilder2(&sb, L" -priority ");
                PhAppendCharStringBuilder(&sb, value);
            }
        }

        if (PhStartupParameters.PluginParameters)
        {
            ULONG i;

            for (i = 0; i < PhStartupParameters.PluginParameters->Count; i++)
            {
                PPH_STRING value = PhStartupParameters.PluginParameters->Items[i];
                PhpAppendCommandLineArgument(&sb, L"plugin", &value->sr);
            }
        }

        if (PhStartupParameters.SelectTab)
            PhpAppendCommandLineArgument(&sb, L"selecttab", &PhStartupParameters.SelectTab->sr);

        if (!(AppFlags & PH_SHELL_APP_PROPAGATE_PARAMETERS_IGNORE_VISIBILITY))
        {
            if (PhStartupParameters.ShowVisible)
                PhAppendStringBuilder2(&sb, L" -v");
            if (PhStartupParameters.ShowHidden)
                PhAppendStringBuilder2(&sb, L" -hide");
        }

        // Add user-specified parameters last so they can override the propagated parameters.
        if (Parameters)
        {
            PhAppendCharStringBuilder(&sb, ' ');
            PhAppendStringBuilder2(&sb, Parameters);
        }

        if (sb.String->Length != 0 && sb.String->Buffer[0] == ' ')
            parameters = sb.String->Buffer + 1;
        else
            parameters = sb.String->Buffer;
    }
    else
    {
        parameters = Parameters;
    }

    result = PhShellExecuteEx(
        hWnd,
        FileName ? FileName : PhGetString(applicationFileName),
        parameters,
        ShowWindowType,
        Flags,
        Timeout,
        ProcessHandle
        );

    if (AppFlags & PH_SHELL_APP_PROPAGATE_PARAMETERS)
        PhDeleteStringBuilder(&sb);

    PhDereferenceObject(applicationFileName);

    return result;
}

BOOLEAN PhCreateProcessIgnoreIfeoDebugger(
    _In_ PWSTR FileName
    )
{
    BOOLEAN result;
    BOOLEAN originalValue;
    HANDLE processHandle;

    result = FALSE;

    RtlEnterCriticalSection(NtCurrentPeb()->FastPebLock);
    originalValue = NtCurrentPeb()->ReadImageFileExecOptions;
    NtCurrentPeb()->ReadImageFileExecOptions = FALSE;
    RtlLeaveCriticalSection(NtCurrentPeb()->FastPebLock);

    // The combination of ReadImageFileExecOptions = FALSE and the DEBUG_PROCESS flag
    // allows us to skip the Debugger IFEO value. (wj32)

    if (NT_SUCCESS(PhCreateProcessWin32(
        FileName,
        NULL,
        NULL,
        NULL,
        PH_CREATE_PROCESS_DEBUG | PH_CREATE_PROCESS_DEBUG_ONLY_THIS_PROCESS,
        NULL,
        &processHandle,
        NULL
        )))
    {
        HANDLE debugObjectHandle;

        if (NT_SUCCESS(PhGetProcessDebugObject(
            processHandle,
            &debugObjectHandle
            )))
        {
            // Disable kill-on-close.
            if (NT_SUCCESS(PhSetDebugKillProcessOnExit(
                debugObjectHandle,
                FALSE
                )))
            {
                // Stop debugging the process now.
                NtRemoveProcessDebug(processHandle, debugObjectHandle);
            }

            NtClose(debugObjectHandle);
        }

        result = TRUE;

        NtClose(processHandle);
    }

    if (originalValue)
    {
        RtlEnterCriticalSection(NtCurrentPeb()->FastPebLock);
        NtCurrentPeb()->ReadImageFileExecOptions = originalValue;
        RtlLeaveCriticalSection(NtCurrentPeb()->FastPebLock);
    }    

    return result;
}

VOID PhInitializeTreeNewColumnMenu(
    _Inout_ PPH_TN_COLUMN_MENU_DATA Data
    )
{
    PhInitializeTreeNewColumnMenuEx(Data, 0);
}

VOID PhInitializeTreeNewColumnMenuEx(
    _Inout_ PPH_TN_COLUMN_MENU_DATA Data,
    _In_ ULONG Flags
    )
{
    PPH_EMENU_ITEM resetSortMenuItem = NULL;
    PPH_EMENU_ITEM sizeColumnToFitMenuItem;
    PPH_EMENU_ITEM sizeAllColumnsToFitMenuItem;
    PPH_EMENU_ITEM hideColumnMenuItem;
    PPH_EMENU_ITEM chooseColumnsMenuItem;
    ULONG minimumNumberOfColumns;

    Data->Menu = PhCreateEMenu();
    Data->Selection = NULL;
    Data->ProcessedId = 0;

    sizeColumnToFitMenuItem = PhCreateEMenuItem(0, PH_TN_COLUMN_MENU_SIZE_COLUMN_TO_FIT_ID, L"Size column to fit", NULL, NULL);
    sizeAllColumnsToFitMenuItem = PhCreateEMenuItem(0, PH_TN_COLUMN_MENU_SIZE_ALL_COLUMNS_TO_FIT_ID, L"Size all columns to fit", NULL, NULL);

    if (!(Flags & PH_TN_COLUMN_MENU_NO_VISIBILITY))
    {
        hideColumnMenuItem = PhCreateEMenuItem(0, PH_TN_COLUMN_MENU_HIDE_COLUMN_ID, L"Hide column", NULL, NULL);
        chooseColumnsMenuItem = PhCreateEMenuItem(0, PH_TN_COLUMN_MENU_CHOOSE_COLUMNS_ID, L"Choose columns...", NULL, NULL);
    }

    if (Flags & PH_TN_COLUMN_MENU_SHOW_RESET_SORT)
    {
        ULONG sortColumn;
        PH_SORT_ORDER sortOrder;

        TreeNew_GetSort(Data->TreeNewHandle, &sortColumn, &sortOrder);

        if (sortOrder != Data->DefaultSortOrder || (Data->DefaultSortOrder != NoSortOrder && sortColumn != Data->DefaultSortColumn))
            resetSortMenuItem = PhCreateEMenuItem(0, PH_TN_COLUMN_MENU_RESET_SORT_ID, L"Reset sort", NULL, NULL);
    }

    PhInsertEMenuItem(Data->Menu, sizeColumnToFitMenuItem, ULONG_MAX);
    PhInsertEMenuItem(Data->Menu, sizeAllColumnsToFitMenuItem, ULONG_MAX);

    if (!(Flags & PH_TN_COLUMN_MENU_NO_VISIBILITY))
    {
        PhInsertEMenuItem(Data->Menu, hideColumnMenuItem, ULONG_MAX);

        if (resetSortMenuItem)
            PhInsertEMenuItem(Data->Menu, resetSortMenuItem, ULONG_MAX);

        PhInsertEMenuItem(Data->Menu, PhCreateEMenuSeparator(), ULONG_MAX);
        PhInsertEMenuItem(Data->Menu, chooseColumnsMenuItem, ULONG_MAX);

        if (TreeNew_GetFixedColumn(Data->TreeNewHandle))
            minimumNumberOfColumns = 2; // don't allow user to remove all normal columns (the fixed column can never be removed)
        else
            minimumNumberOfColumns = 1;

        if (!Data->MouseEvent || !Data->MouseEvent->Column ||
            Data->MouseEvent->Column->Fixed || // don't allow the fixed column to be hidden
            TreeNew_GetVisibleColumnCount(Data->TreeNewHandle) < minimumNumberOfColumns + 1
            )
        {
            hideColumnMenuItem->Flags |= PH_EMENU_DISABLED;
        }
    }
    else
    {
        if (resetSortMenuItem)
            PhInsertEMenuItem(Data->Menu, resetSortMenuItem, ULONG_MAX);
    }

    if (!Data->MouseEvent || !Data->MouseEvent->Column)
    {
        sizeColumnToFitMenuItem->Flags |= PH_EMENU_DISABLED;
    }
}

VOID PhpEnsureValidSortColumnTreeNew(
    _Inout_ HWND TreeNewHandle,
    _In_ ULONG DefaultSortColumn,
    _In_ PH_SORT_ORDER DefaultSortOrder
    )
{
    ULONG sortColumn;
    PH_SORT_ORDER sortOrder;

    // Make sure the column we're sorting by is actually visible, and if not, don't sort anymore.

    TreeNew_GetSort(TreeNewHandle, &sortColumn, &sortOrder);

    if (sortOrder != NoSortOrder)
    {
        PH_TREENEW_COLUMN column;

        TreeNew_GetColumn(TreeNewHandle, sortColumn, &column);

        if (!column.Visible)
        {
            if (DefaultSortOrder != NoSortOrder)
            {
                // Make sure the default sort column is visible.
                TreeNew_GetColumn(TreeNewHandle, DefaultSortColumn, &column);

                if (!column.Visible)
                {
                    ULONG maxId;
                    ULONG id;
                    BOOLEAN found;

                    // Use the first visible column.
                    maxId = TreeNew_GetMaxId(TreeNewHandle);
                    id = 0;
                    found = FALSE;

                    while (id <= maxId)
                    {
                        if (TreeNew_GetColumn(TreeNewHandle, id, &column))
                        {
                            if (column.Visible)
                            {
                                DefaultSortColumn = id;
                                found = TRUE;
                                break;
                            }
                        }

                        id++;
                    }

                    if (!found)
                    {
                        DefaultSortColumn = 0;
                        DefaultSortOrder = NoSortOrder;
                    }
                }
            }

            TreeNew_SetSort(TreeNewHandle, DefaultSortColumn, DefaultSortOrder);
        }
    }
}

BOOLEAN PhHandleTreeNewColumnMenu(
    _Inout_ PPH_TN_COLUMN_MENU_DATA Data
    )
{
    if (!Data->Selection)
        return FALSE;

    switch (Data->Selection->Id)
    {
    case PH_TN_COLUMN_MENU_RESET_SORT_ID:
        {
            TreeNew_SetSort(Data->TreeNewHandle, Data->DefaultSortColumn, Data->DefaultSortOrder);
        }
        break;
    case PH_TN_COLUMN_MENU_SIZE_COLUMN_TO_FIT_ID:
        {
            if (Data->MouseEvent && Data->MouseEvent->Column)
            {
                TreeNew_AutoSizeColumn(Data->TreeNewHandle, Data->MouseEvent->Column->Id, 0);
            }
        }
        break;
    case PH_TN_COLUMN_MENU_SIZE_ALL_COLUMNS_TO_FIT_ID:
        {
            ULONG maxId;
            ULONG id;

            maxId = TreeNew_GetMaxId(Data->TreeNewHandle);
            id = 0;

            while (id <= maxId)
            {
                TreeNew_AutoSizeColumn(Data->TreeNewHandle, id, 0);
                id++;
            }
        }
        break;
    case PH_TN_COLUMN_MENU_HIDE_COLUMN_ID:
        {
            PH_TREENEW_COLUMN column;

            if (Data->MouseEvent && Data->MouseEvent->Column && !Data->MouseEvent->Column->Fixed)
            {
                column.Id = Data->MouseEvent->Column->Id;
                column.Visible = FALSE;
                TreeNew_SetColumn(Data->TreeNewHandle, TN_COLUMN_FLAG_VISIBLE, &column);
                PhpEnsureValidSortColumnTreeNew(Data->TreeNewHandle, Data->DefaultSortColumn, Data->DefaultSortOrder);
                InvalidateRect(Data->TreeNewHandle, NULL, FALSE);
            }
        }
        break;
    case PH_TN_COLUMN_MENU_CHOOSE_COLUMNS_ID:
        {
            PhShowChooseColumnsDialog(Data->TreeNewHandle, Data->TreeNewHandle, PH_CONTROL_TYPE_TREE_NEW);
            PhpEnsureValidSortColumnTreeNew(Data->TreeNewHandle, Data->DefaultSortColumn, Data->DefaultSortOrder);
        }
        break;
    default:
        return FALSE;
    }

    Data->ProcessedId = Data->Selection->Id;

    return TRUE;
}

VOID PhDeleteTreeNewColumnMenu(
    _In_ PPH_TN_COLUMN_MENU_DATA Data
    )
{
    if (Data->Menu)
    {
        PhDestroyEMenu(Data->Menu);
        Data->Menu = NULL;
    }
}

VOID PhInitializeTreeNewFilterSupport(
    _Out_ PPH_TN_FILTER_SUPPORT Support,
    _In_ HWND TreeNewHandle,
    _In_ PPH_LIST NodeList
    )
{
    Support->FilterList = NULL;
    Support->TreeNewHandle = TreeNewHandle;
    Support->NodeList = NodeList;
}

VOID PhDeleteTreeNewFilterSupport(
    _In_ PPH_TN_FILTER_SUPPORT Support
    )
{
    PhDereferenceObject(Support->FilterList);
}

PPH_TN_FILTER_ENTRY PhAddTreeNewFilter(
    _In_ PPH_TN_FILTER_SUPPORT Support,
    _In_ PPH_TN_FILTER_FUNCTION Filter,
    _In_opt_ PVOID Context
    )
{
    PPH_TN_FILTER_ENTRY entry;

    entry = PhAllocate(sizeof(PH_TN_FILTER_ENTRY));
    entry->Filter = Filter;
    entry->Context = Context;

    if (!Support->FilterList)
        Support->FilterList = PhCreateList(2);

    PhAddItemList(Support->FilterList, entry);

    return entry;
}

VOID PhRemoveTreeNewFilter(
    _In_ PPH_TN_FILTER_SUPPORT Support,
    _In_ PPH_TN_FILTER_ENTRY Entry
    )
{
    ULONG index;

    if (!Support->FilterList)
        return;

    index = PhFindItemList(Support->FilterList, Entry);

    if (index != ULONG_MAX)
    {
        PhRemoveItemList(Support->FilterList, index);
        PhFree(Entry);
    }
}

BOOLEAN PhApplyTreeNewFiltersToNode(
    _In_ PPH_TN_FILTER_SUPPORT Support,
    _In_ PPH_TREENEW_NODE Node
    )
{
    BOOLEAN show;
    ULONG i;

    show = TRUE;

    if (Support->FilterList)
    {
        for (i = 0; i < Support->FilterList->Count; i++)
        {
            PPH_TN_FILTER_ENTRY entry;

            entry = Support->FilterList->Items[i];

            if (!entry->Filter(Node, entry->Context))
            {
                show = FALSE;
                break;
            }
        }
    }

    return show;
}

VOID PhApplyTreeNewFilters(
    _In_ PPH_TN_FILTER_SUPPORT Support
    )
{
    ULONG i;

    for (i = 0; i < Support->NodeList->Count; i++)
    {
        PPH_TREENEW_NODE node;

        node = Support->NodeList->Items[i];
        node->Visible = PhApplyTreeNewFiltersToNode(Support, node);

        if (!node->Visible && node->Selected)
        {
            node->Selected = FALSE;
        }
    }

    TreeNew_NodesStructured(Support->TreeNewHandle);
}

VOID NTAPI PhpCopyCellEMenuItemDeleteFunction(
    _In_ struct _PH_EMENU_ITEM *Item
    )
{
    PPH_COPY_CELL_CONTEXT context;

    context = Item->Context;
    PhDereferenceObject(context->MenuItemText);
    PhFree(context);
}

BOOLEAN PhInsertCopyCellEMenuItem(
    _In_ struct _PH_EMENU_ITEM *Menu,
    _In_ ULONG InsertAfterId,
    _In_ HWND TreeNewHandle,
    _In_ PPH_TREENEW_COLUMN Column
    )
{
    PPH_EMENU_ITEM parentItem;
    ULONG indexInParent;
    PPH_COPY_CELL_CONTEXT context;
    PH_STRINGREF columnText;
    PPH_STRING escapedText;
    PPH_STRING menuItemText;
    PPH_EMENU_ITEM copyCellItem;

    if (!Column)
        return FALSE;

    if (!PhFindEMenuItemEx(Menu, 0, NULL, InsertAfterId, &parentItem, &indexInParent))
        return FALSE;

    indexInParent++;

    context = PhAllocate(sizeof(PH_COPY_CELL_CONTEXT));
    context->TreeNewHandle = TreeNewHandle;
    context->Id = Column->Id;

    PhInitializeStringRef(&columnText, Column->Text);
    escapedText = PhEscapeStringForMenuPrefix(&columnText);
    menuItemText = PhFormatString(L"Copy \"%s\"", escapedText->Buffer);
    PhDereferenceObject(escapedText);
    copyCellItem = PhCreateEMenuItem(0, ID_COPY_CELL, menuItemText->Buffer, NULL, context);
    copyCellItem->DeleteFunction = PhpCopyCellEMenuItemDeleteFunction;
    context->MenuItemText = menuItemText;

    if (Column->CustomDraw)
        copyCellItem->Flags |= PH_EMENU_DISABLED;

    PhInsertEMenuItem(parentItem, copyCellItem, indexInParent);

    return TRUE;
}

BOOLEAN PhHandleCopyCellEMenuItem(
    _In_ struct _PH_EMENU_ITEM *SelectedItem
    )
{
    PPH_COPY_CELL_CONTEXT context;
    PH_STRING_BUILDER stringBuilder;
    ULONG count;
    ULONG selectedCount;
    ULONG i;
    PPH_TREENEW_NODE node;
    PH_TREENEW_GET_CELL_TEXT getCellText;

    if (!SelectedItem)
        return FALSE;
    if (SelectedItem->Id != ID_COPY_CELL)
        return FALSE;

    context = SelectedItem->Context;

    PhInitializeStringBuilder(&stringBuilder, 0x100);
    count = TreeNew_GetFlatNodeCount(context->TreeNewHandle);
    selectedCount = 0;

    for (i = 0; i < count; i++)
    {
        node = TreeNew_GetFlatNode(context->TreeNewHandle, i);

        if (node && node->Selected)
        {
            selectedCount++;

            getCellText.Flags = 0;
            getCellText.Node = node;
            getCellText.Id = context->Id;
            PhInitializeEmptyStringRef(&getCellText.Text);
            TreeNew_GetCellText(context->TreeNewHandle, &getCellText);

            PhAppendStringBuilder(&stringBuilder, &getCellText.Text);
            PhAppendStringBuilder2(&stringBuilder, L"\r\n");
        }
    }

    if (stringBuilder.String->Length != 0 && selectedCount == 1)
        PhRemoveEndStringBuilder(&stringBuilder, 2);

    PhSetClipboardString(context->TreeNewHandle, &stringBuilder.String->sr);
    PhDeleteStringBuilder(&stringBuilder);

    return TRUE;
}

VOID NTAPI PhpCopyListViewEMenuItemDeleteFunction(
    _In_ struct _PH_EMENU_ITEM *Item
    )
{
    PPH_COPY_ITEM_CONTEXT context;

    context = Item->Context;
    PhDereferenceObject(context->MenuItemText);
    PhFree(context);
}

BOOLEAN PhInsertCopyListViewEMenuItem(
    _In_ struct _PH_EMENU_ITEM *Menu,
    _In_ ULONG InsertAfterId,
    _In_ HWND ListViewHandle
    )
{
    PPH_EMENU_ITEM parentItem;
    ULONG indexInParent;
    PPH_COPY_ITEM_CONTEXT context;
    PPH_STRING columnText = NULL;
    PPH_STRING escapedText;
    PPH_STRING menuItemText;
    PPH_EMENU_ITEM copyMenuItem;
    POINT location;
    LVHITTESTINFO lvHitInfo;
    HDITEM headerItem;
    WCHAR headerText[MAX_PATH] = L"";

    if (!GetCursorPos(&location))
        return FALSE;
    if (!ScreenToClient(ListViewHandle, &location))
        return FALSE;

    memset(&lvHitInfo, 0, sizeof(LVHITTESTINFO));
    lvHitInfo.pt = location;

    if (ListView_SubItemHitTest(ListViewHandle, &lvHitInfo) == -1)
        return FALSE;

    memset(headerText, 0, sizeof(headerText));
    memset(&headerItem, 0, sizeof(HDITEM));
    headerItem.mask = HDI_TEXT;
    headerItem.cchTextMax = RTL_NUMBER_OF(headerText);
    headerItem.pszText = headerText;

    if (!Header_GetItem(ListView_GetHeader(ListViewHandle), lvHitInfo.iSubItem, &headerItem))
        return FALSE;

    columnText = PhaCreateString(headerText);

    if (PhIsNullOrEmptyString(columnText))
        return FALSE;

    if (!PhFindEMenuItemEx(Menu, 0, NULL, InsertAfterId, &parentItem, &indexInParent))
        return FALSE;

    indexInParent++;

    context = PhAllocate(sizeof(PH_COPY_ITEM_CONTEXT));
    context->ListViewHandle = ListViewHandle;
    context->Id = lvHitInfo.iItem;
    context->SubId = lvHitInfo.iSubItem;

    escapedText = PhEscapeStringForMenuPrefix(&columnText->sr);
    menuItemText = PhFormatString(L"Copy \"%s\"", escapedText->Buffer);
    PhDereferenceObject(escapedText);

    copyMenuItem = PhCreateEMenuItem(0, ID_COPY_CELL, menuItemText->Buffer, NULL, context);
    copyMenuItem->DeleteFunction = PhpCopyListViewEMenuItemDeleteFunction;
    context->MenuItemText = menuItemText;

    PhInsertEMenuItem(parentItem, copyMenuItem, indexInParent);

    return TRUE;
}

BOOLEAN PhHandleCopyListViewEMenuItem(
    _In_ struct _PH_EMENU_ITEM *SelectedItem
    )
{
    PPH_COPY_ITEM_CONTEXT context;
    PH_STRING_BUILDER stringBuilder;
    ULONG count;
    ULONG selectedCount;
    ULONG i;
    PPH_STRING getItemText;

    if (!SelectedItem)
        return FALSE;
    if (SelectedItem->Id != ID_COPY_CELL)
        return FALSE;

    context = SelectedItem->Context;

    PhInitializeStringBuilder(&stringBuilder, 0x100);
    count = ListView_GetItemCount(context->ListViewHandle);
    selectedCount = 0;

    for (i = 0; i < count; i++)
    {
        if (!(ListView_GetItemState(context->ListViewHandle, i, LVIS_SELECTED) & LVIS_SELECTED))
            continue;

        getItemText = PhaGetListViewItemText(context->ListViewHandle, i, context->SubId);

        PhAppendStringBuilder(&stringBuilder, &getItemText->sr);
        PhAppendStringBuilder2(&stringBuilder, L"\r\n");

        selectedCount++;
    }

    if (stringBuilder.String->Length != 0 && selectedCount == 1)
        PhRemoveEndStringBuilder(&stringBuilder, 2);

    PhSetClipboardString(context->ListViewHandle, &stringBuilder.String->sr);
    PhDeleteStringBuilder(&stringBuilder);

    return TRUE;
}

BOOLEAN PhpSelectFavoriteInRegedit(
    _In_ HWND RegeditWindow,
    _In_ PPH_STRINGREF FavoriteName,
    _In_ BOOLEAN UsePhSvc
    )
{
    HMENU menu;
    HMENU favoritesMenu;
    ULONG count;
    ULONG i;
    ULONG id = ULONG_MAX;

    if (!(menu = GetMenu(RegeditWindow)))
        return FALSE;

    // Cause the Registry Editor to refresh the Favorites menu.
    if (UsePhSvc)
        PhSvcCallSendMessage(RegeditWindow, WM_MENUSELECT, MAKEWPARAM(3, MF_POPUP), (LPARAM)menu);
    else
        SendMessage(RegeditWindow, WM_MENUSELECT, MAKEWPARAM(3, MF_POPUP), (LPARAM)menu);

    if (!(favoritesMenu = GetSubMenu(menu, 3)))
        return FALSE;

    // Find our entry.

    count = GetMenuItemCount(favoritesMenu);

    if (count == -1)
        return FALSE;
    if (count > 1000)
        count = 1000;

    for (i = 3; i < count; i++)
    {
        MENUITEMINFO info = { sizeof(MENUITEMINFO) };
        WCHAR buffer[MAX_PATH];

        info.fMask = MIIM_ID | MIIM_STRING;
        info.dwTypeData = buffer;
        info.cch = RTL_NUMBER_OF(buffer);
        GetMenuItemInfo(favoritesMenu, i, TRUE, &info);

        if (info.cch == FavoriteName->Length / sizeof(WCHAR))
        {
            PH_STRINGREF text;

            text.Buffer = buffer;
            text.Length = info.cch * sizeof(WCHAR);

            if (PhEqualStringRef(&text, FavoriteName, TRUE))
            {
                id = info.wID;
                break;
            }
        }
    }

    if (id == ULONG_MAX)
        return FALSE;

    // Activate our entry.
    if (UsePhSvc)
        PhSvcCallSendMessage(RegeditWindow, WM_COMMAND, MAKEWPARAM(id, 0), 0);
    else
        SendMessage(RegeditWindow, WM_COMMAND, MAKEWPARAM(id, 0), 0);

    // "Close" the Favorites menu and restore normal status bar text.
    if (UsePhSvc)
        PhSvcCallPostMessage(RegeditWindow, WM_MENUSELECT, MAKEWPARAM(0, 0xffff), 0);
    else
        PostMessage(RegeditWindow, WM_MENUSELECT, MAKEWPARAM(0, 0xffff), 0);

    // Bring regedit to the top.
    if (IsMinimized(RegeditWindow))
    {
        ShowWindow(RegeditWindow, SW_RESTORE);
        SetForegroundWindow(RegeditWindow);
    }
    else
    {
        SetForegroundWindow(RegeditWindow);
    }

    return TRUE;
}

/**
 * Opens a key in the Registry Editor. If the Registry Editor is already open,
 * the specified key is selected in the Registry Editor.
 *
 * \param hWnd A handle to the parent window.
 * \param KeyName The key name to open.
 */
BOOLEAN PhShellOpenKey2(
    _In_ HWND hWnd,
    _In_ PPH_STRING KeyName
    )
{
    static PH_STRINGREF favoritesKeyName = PH_STRINGREF_INIT(L"Software\\Microsoft\\Windows\\CurrentVersion\\Applets\\Regedit\\Favorites");

    BOOLEAN result = FALSE;
    HWND regeditWindow;
    HANDLE favoritesKeyHandle;
    WCHAR favoriteName[32];
    UNICODE_STRING valueName;
    PH_STRINGREF valueNameSr;
    PPH_STRING expandedKeyName;

    regeditWindow = FindWindow(L"RegEdit_RegEdit", NULL);

    if (!regeditWindow)
    {
        PhShellOpenKey(hWnd, KeyName);
        return TRUE;
    }

    if (!PhGetOwnTokenAttributes().Elevated)
    {
        if (!PhUiConnectToPhSvc(hWnd, FALSE))
            return FALSE;
    }

    // Create our entry in Favorites.

    if (!NT_SUCCESS(PhCreateKey(
        &favoritesKeyHandle,
        KEY_WRITE,
        PH_KEY_CURRENT_USER,
        &favoritesKeyName,
        0,
        0,
        NULL
        )))
        goto CleanupExit;

    memcpy(favoriteName, L"A_ProcessHacker", 15 * sizeof(WCHAR));
    PhGenerateRandomAlphaString(&favoriteName[15], 16);
    RtlInitUnicodeString(&valueName, favoriteName);
    PhUnicodeStringToStringRef(&valueName, &valueNameSr);

    expandedKeyName = PhExpandKeyName(KeyName, FALSE);
    NtSetValueKey(favoritesKeyHandle, &valueName, 0, REG_SZ, expandedKeyName->Buffer, (ULONG)expandedKeyName->Length + sizeof(UNICODE_NULL));
    PhDereferenceObject(expandedKeyName);

    // Select our entry in regedit.
    result = PhpSelectFavoriteInRegedit(regeditWindow, &valueNameSr, !PhGetOwnTokenAttributes().Elevated);

    NtDeleteValueKey(favoritesKeyHandle, &valueName);
    NtClose(favoritesKeyHandle);

CleanupExit:
    if (!PhGetOwnTokenAttributes().Elevated)
        PhUiDisconnectFromPhSvc();

    return result;
}

PPH_STRING PhPcre2GetErrorMessage(
    _In_ INT ErrorCode
    )
{
    PPH_STRING buffer;
    SIZE_T bufferLength;
    INT_PTR returnLength;

    bufferLength = 128 * sizeof(WCHAR);
    buffer = PhCreateStringEx(NULL, bufferLength);

    while (TRUE)
    {
        if ((returnLength = pcre2_get_error_message(ErrorCode, buffer->Buffer, bufferLength / sizeof(WCHAR) + 1)) >= 0)
            break;

        PhDereferenceObject(buffer);
        bufferLength *= 2;

        if (bufferLength > 0x1000 * sizeof(WCHAR))
            break;

        buffer = PhCreateStringEx(NULL, bufferLength);
    }

    if (returnLength < 0)
        return NULL;

    buffer->Length = returnLength * sizeof(WCHAR);
    return buffer;
}

HBITMAP PhGetShieldBitmap(
    VOID
    )
{
    static HBITMAP shieldBitmap = NULL;

    if (!shieldBitmap)
    {
        HICON shieldIcon;

        if (shieldIcon = PhLoadIcon(NULL, IDI_SHIELD, PH_LOAD_ICON_SHARED | PH_LOAD_ICON_SIZE_SMALL | PH_LOAD_ICON_STRICT, 0, 0))
        {
            shieldBitmap = PhIconToBitmap(shieldIcon, PhSmallIconSize.X, PhSmallIconSize.Y);
            DestroyIcon(shieldIcon);
        }
    }

    return shieldBitmap;
}

// HACK - nightly build feature testing (dmex)
// TODO - move definitions to proper locations.
#include <taskschd.h>
DEFINE_GUID(CLSID_TaskScheduler, 0x0f87369f, 0xa4e5, 0x4cfc, 0xbd, 0x3e, 0x73, 0xe6, 0x15, 0x45, 0x72, 0xdd);
DEFINE_GUID(IID_ITaskService, 0x2FABA4C7, 0x4DA9, 0x4013, 0x96, 0x97, 0x20, 0xCC, 0x3F, 0xD4, 0x0F, 0x85);
DEFINE_GUID(IID_ITaskSettings2, 0x2C05C3F0, 0x6EED, 0x4C05, 0xA1, 0x5F, 0xED, 0x7D, 0x7A, 0x98, 0xA3, 0x69);
DEFINE_GUID(IID_ILogonTrigger, 0x72DADE38, 0xFAE4, 0x4b3e, 0xBA, 0xF4, 0x5D, 0x00, 0x9A, 0xF0, 0x2B, 0x1C);
DEFINE_GUID(IID_IExecAction, 0x4C3D624D, 0xfd6b, 0x49A3, 0xB9, 0xB7, 0x09, 0xCB, 0x3C, 0xD3, 0xF0, 0x47);

HRESULT PhRunAsAdminTask(
    _In_ PWSTR TaskName
    )
{
    HRESULT status = S_FALSE;
    VARIANT empty = { VT_EMPTY };
    ITaskService* taskService = NULL;
    ITaskFolder* taskFolder = NULL;
    IRegisteredTask* taskRegisteredTask = NULL;
    IRunningTask* taskRunningTask = NULL;

    status = CoCreateInstance(
        &CLSID_TaskScheduler,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_ITaskService,
        &taskService
        );

    if (FAILED(status))
        goto CleanupExit;

    status = ITaskService_Connect(
        taskService,
        empty,
        empty,
        empty,
        empty
        );

    if (FAILED(status))
        goto CleanupExit;

    status = ITaskService_GetFolder(
        taskService,
        L"\\",
        &taskFolder
        );

    if (FAILED(status))
        goto CleanupExit;

    status = ITaskFolder_GetTask(
        taskFolder,
        TaskName,
        &taskRegisteredTask
        );

    if (FAILED(status))
        goto CleanupExit;

    status = IRegisteredTask_RunEx(
        taskRegisteredTask,
        empty,
        TASK_RUN_AS_SELF,
        0,
        NULL,
        &taskRunningTask
        );

CleanupExit:

    if (taskRunningTask)
        IRunningTask_Release(taskRunningTask);
    if (taskRegisteredTask)
        IRegisteredTask_Release(taskRegisteredTask);
    if (taskFolder)
        ITaskFolder_Release(taskFolder);
    if (taskService)
        ITaskService_Release(taskService);

    return status;
}

HRESULT PhDeleteAdminTask(
    _In_ PWSTR TaskName
    )
{
    HRESULT status = S_FALSE;
    VARIANT empty = { VT_EMPTY };
    ITaskService* taskService = NULL;
    ITaskFolder* taskFolder = NULL;

    status = CoCreateInstance(
        &CLSID_TaskScheduler,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_ITaskService,
        &taskService
        );

    if (FAILED(status))
        goto CleanupExit;

    status = ITaskService_Connect(
        taskService,
        empty,
        empty,
        empty,
        empty
        );

    if (FAILED(status))
        goto CleanupExit;

    status = ITaskService_GetFolder(
        taskService,
        L"\\",
        &taskFolder
        );

    if (FAILED(status))
        goto CleanupExit;

    status = ITaskFolder_DeleteTask(
        taskFolder,
        TaskName,
        0
        );

CleanupExit:

    if (taskFolder)
        ITaskFolder_Release(taskFolder);
    if (taskService)
        ITaskService_Release(taskService);

    return status;
}

HRESULT PhCreateAdminTask(
    _In_ PWSTR TaskName,
    _In_ PWSTR FileName
    )
{
    HRESULT status = S_FALSE;
    VARIANT empty = { VT_EMPTY };
    ITaskService* taskService = NULL;
    ITaskFolder* taskFolder = NULL;
    ITaskDefinition* taskDefinition = NULL;
    ITaskSettings* taskSettings = NULL;
    ITaskSettings2* taskSettings2 = NULL;
    ITriggerCollection* taskTriggerCollection = NULL;
    ITrigger* taskTrigger = NULL;
    ILogonTrigger* taskLogonTrigger = NULL;
    IRegisteredTask* taskRegisteredTask = NULL;
    IPrincipal* taskPrincipal = NULL;
    IActionCollection* taskActionCollection = NULL;
    IAction* taskAction = NULL;
    IExecAction* taskExecAction = NULL;

    status = CoCreateInstance(
        &CLSID_TaskScheduler,
        NULL,
        CLSCTX_INPROC_SERVER,
        &IID_ITaskService,
        &taskService
        );

    if (FAILED(status))
        goto CleanupExit;

    status = ITaskService_Connect(
        taskService,
        empty,
        empty,
        empty,
        empty
        );

    if (FAILED(status))
        goto CleanupExit;

    status = ITaskService_GetFolder(
        taskService,
        L"\\",
        &taskFolder
        );

    if (FAILED(status))
        goto CleanupExit;

    status = ITaskService_NewTask(
        taskService,
        0,
        &taskDefinition
        );

    if (FAILED(status))
        goto CleanupExit;

    status = ITaskDefinition_get_Settings(
        taskDefinition,
        &taskSettings
        );

    if (FAILED(status))
        goto CleanupExit;

    ITaskSettings_put_Compatibility(taskSettings, TASK_COMPATIBILITY_V2_1);
    ITaskSettings_put_StartWhenAvailable(taskSettings, VARIANT_TRUE);
    ITaskSettings_put_DisallowStartIfOnBatteries(taskSettings, VARIANT_FALSE);
    ITaskSettings_put_StopIfGoingOnBatteries(taskSettings, VARIANT_FALSE);
    ITaskSettings_put_ExecutionTimeLimit(taskSettings, L"PT0S");
    //ITaskSettings_put_Priority(taskSettings, 1);

    if (SUCCEEDED(ITaskSettings_QueryInterface(
        taskSettings,
        &IID_ITaskSettings2,
        &taskSettings2
        )))
    {
        ITaskSettings2_put_UseUnifiedSchedulingEngine(taskSettings2, VARIANT_TRUE);
        ITaskSettings2_put_DisallowStartOnRemoteAppSession(taskSettings2, VARIANT_TRUE);
        ITaskSettings2_Release(taskSettings2);
    }

    //status = ITaskDefinition_get_Triggers(
    //    taskDefinition,
    //    &taskTriggerCollection
    //    );
    //
    //if (FAILED(status))
    //    goto CleanupExit;
    //
    //status = ITriggerCollection_Create(
    //    taskTriggerCollection,
    //    TASK_TRIGGER_LOGON,
    //    &taskTrigger
    //    );
    //
    //if (FAILED(status))
    //    goto CleanupExit;
    //
    //status = ITrigger_QueryInterface(
    //    taskTrigger,
    //    &IID_ILogonTrigger,
    //    &taskLogonTrigger
    //    );
    //
    //if (FAILED(status))
    //    goto CleanupExit;
    //
    //ILogonTrigger_put_Id(taskLogonTrigger, L"LogonTriggerId");
    //ILogonTrigger_put_UserId(taskLogonTrigger, PhGetString(PhGetTokenUserString(PhGetOwnTokenAttributes().TokenHandle, TRUE)));

    status = ITaskDefinition_get_Principal(
        taskDefinition,
        &taskPrincipal
        );

    if (FAILED(status))
        goto CleanupExit;

    IPrincipal_put_RunLevel(taskPrincipal, TASK_RUNLEVEL_HIGHEST);
    IPrincipal_put_LogonType(taskPrincipal, TASK_LOGON_INTERACTIVE_TOKEN);
        
    status = ITaskDefinition_get_Actions(
        taskDefinition,
        &taskActionCollection
        );

    if (FAILED(status))
        goto CleanupExit;

    status = IActionCollection_Create(
        taskActionCollection,
        TASK_ACTION_EXEC,
        &taskAction
        );

    if (FAILED(status))
        goto CleanupExit;

    status = IAction_QueryInterface(
        taskAction,
        &IID_IExecAction,
        &taskExecAction
        );

    if (FAILED(status))
        goto CleanupExit;

    status = IExecAction_put_Path(
        taskExecAction,
        FileName
        );

    if (FAILED(status))
        goto CleanupExit;

    ITaskFolder_DeleteTask(
        taskFolder,
        TaskName,
        0
        );

    status = ITaskFolder_RegisterTaskDefinition(
        taskFolder,
        TaskName,
        taskDefinition,
        TASK_CREATE_OR_UPDATE,
        empty,
        empty,
        TASK_LOGON_INTERACTIVE_TOKEN,
        empty,
        &taskRegisteredTask
        );

CleanupExit:

    if (taskRegisteredTask)
        IRegisteredTask_Release(taskRegisteredTask);
    if (taskActionCollection)
        IActionCollection_Release(taskActionCollection);
    if (taskPrincipal)
        IPrincipal_Release(taskPrincipal);
    if (taskLogonTrigger)
        ILogonTrigger_Release(taskLogonTrigger);
    if (taskTrigger)
        ITrigger_Release(taskTrigger);
    if (taskTriggerCollection)
        ITriggerCollection_Release(taskTriggerCollection);
    if (taskSettings)
        ITaskSettings_Release(taskSettings);
    if (taskDefinition)
        ITaskDefinition_Release(taskDefinition);
    if (taskFolder)
        ITaskFolder_Release(taskFolder);
    if (taskService)
        ITaskService_Release(taskService);

    return status;
}
