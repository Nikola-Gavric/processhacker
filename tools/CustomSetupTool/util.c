/*
 * Process Hacker Toolchain -
 *   project setup
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

#include <setup.h>

PH_STRINGREF UninstallKeyName = PH_STRINGREF_INIT(L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ProcessHacker");
PH_STRINGREF PhImageOptionsKeyName = PH_STRINGREF_INIT(L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\ProcessHacker.exe");
PH_STRINGREF PhImageOptionsWow64KeyName = PH_STRINGREF_INIT(L"Software\\WOW6432Node\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\ProcessHacker.exe");
PH_STRINGREF TmImageOptionsKeyName = PH_STRINGREF_INIT(L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\taskmgr.exe");

NTSTATUS SetupCreateUninstallKey(
    _In_ PPH_SETUP_CONTEXT Context
    )
{
    NTSTATUS status;
    HANDLE keyHandle;
    UNICODE_STRING name;
    UNICODE_STRING value;
    ULONG ulong = 1;

    status = PhCreateKey(
        &keyHandle,
        KEY_ALL_ACCESS | KEY_WOW64_64KEY,
        PH_KEY_LOCAL_MACHINE,
        &UninstallKeyName,
        0,
        0,
        NULL
        );

    if (NT_SUCCESS(status))
    {
        PPH_STRING tempString;
        
        tempString = SetupCreateFullPath(Context->SetupInstallPath, L"\\processhacker.exe,0");
        PhStringRefToUnicodeString(&tempString->sr, &value);
        RtlInitUnicodeString(&name, L"DisplayIcon");
        PhStringRefToUnicodeString(&tempString->sr, &value);
        NtSetValueKey(keyHandle, &name, 0, REG_SZ, value.Buffer, (ULONG)value.MaximumLength);
        PhDereferenceObject(tempString);

        RtlInitUnicodeString(&name, L"DisplayName");
        RtlInitUnicodeString(&value, L"Process Hacker");
        NtSetValueKey(keyHandle, &name, 0, REG_SZ, value.Buffer, (ULONG)value.MaximumLength);

        RtlInitUnicodeString(&name, L"DisplayVersion");
        RtlInitUnicodeString(&value, L"3.x");
        NtSetValueKey(keyHandle, &name, 0, REG_SZ, value.Buffer, (ULONG)value.MaximumLength);

        RtlInitUnicodeString(&name, L"HelpLink");
        RtlInitUnicodeString(&value, L"http://wj32.org/processhacker/forums/");
        NtSetValueKey(keyHandle, &name, 0, REG_SZ, value.Buffer, (ULONG)value.MaximumLength);

        RtlInitUnicodeString(&name, L"InstallLocation");
        RtlInitUnicodeString(&value, PhGetString(Context->SetupInstallPath));
        NtSetValueKey(keyHandle, &name, 0, REG_SZ, value.Buffer, (ULONG)value.MaximumLength);

        RtlInitUnicodeString(&name, L"Publisher");
        RtlInitUnicodeString(&value, L"Process Hacker");
        NtSetValueKey(keyHandle, &name, 0, REG_SZ, value.Buffer, (ULONG)value.MaximumLength);

        RtlInitUnicodeString(&name, L"UninstallString");
        tempString = PhFormatString(L"\"%s\\processhacker-setup.exe\" -uninstall", PhGetString(Context->SetupInstallPath));
        PhStringRefToUnicodeString(&tempString->sr, &value);
        NtSetValueKey(keyHandle, &name, 0, REG_SZ, value.Buffer, (ULONG)value.MaximumLength);
        PhDereferenceObject(tempString);

        RtlInitUnicodeString(&value, L"NoModify");
        ulong = TRUE;
        NtSetValueKey(keyHandle, &value, 0, REG_DWORD, &ulong, sizeof(ULONG));

        RtlInitUnicodeString(&value, L"NoRepair");
        ulong = TRUE;
        NtSetValueKey(keyHandle, &value, 0, REG_DWORD, &ulong, sizeof(ULONG));

        NtClose(keyHandle);
    }

    return status;
}

NTSTATUS SetupDeleteUninstallKey(
    VOID
    )
{
    NTSTATUS status;
    HANDLE keyHandle;

    status = PhOpenKey(
        &keyHandle,
        DELETE | KEY_WOW64_64KEY,
        PH_KEY_LOCAL_MACHINE,
        &UninstallKeyName,
        0
        );

    if (NT_SUCCESS(status))
    {
        status = NtDeleteKey(keyHandle);
        NtClose(keyHandle);
    }

    return status;
}

PPH_STRING SetupFindInstallDirectory(
    VOID
    )
{
    // Find the current installation path.
    PPH_STRING setupInstallPath = GetProcessHackerInstallPath();

    // Check if the string is valid.
    if (PhIsNullOrEmptyString(setupInstallPath))
    {
        static PH_STRINGREF programW6432 = PH_STRINGREF_INIT(L"%ProgramW6432%\\Process Hacker\\");
        static PH_STRINGREF programFiles = PH_STRINGREF_INIT(L"%ProgramFiles%\\Process Hacker\\");
        SYSTEM_INFO info;

        GetNativeSystemInfo(&info);

        if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
            setupInstallPath = PhExpandEnvironmentStrings(&programW6432);
        else
            setupInstallPath = PhExpandEnvironmentStrings(&programFiles);
    }

    if (PhIsNullOrEmptyString(setupInstallPath))
    {
        setupInstallPath = PhCreateString(L"C:\\Program Files\\Process Hacker\\");
    }

    if (!PhIsNullOrEmptyString(setupInstallPath))
    {
        if (!PhEndsWithString2(setupInstallPath, L"\\", TRUE))
        {
            PhSwapReference(&setupInstallPath, PhConcatStringRefZ(&setupInstallPath->sr, L"\\"));
        }
    }

    return setupInstallPath;
}

PPH_STRING SetupFindAppdataDirectory(
    VOID
    )
{
    static PH_STRINGREF appdataPath = PH_STRINGREF_INIT(L"%APPDATA%\\Process Hacker\\");
    PPH_STRING appdataFilePath;

    if (appdataFilePath = PhExpandEnvironmentStrings(&appdataPath))
    {
        PhMoveReference(&appdataFilePath, PhGetFullPath(appdataFilePath->Buffer, NULL));
        return appdataFilePath;
    }

    return NULL;
}

VOID SetupDeleteAppdataDirectory(
    _In_ PPH_SETUP_CONTEXT Context
    )
{
    PPH_STRING uninstallFilePath;

    if (uninstallFilePath = SetupFindAppdataDirectory())
    {
        PhDeleteDirectory(uninstallFilePath);

        PhDereferenceObject(uninstallFilePath);
    }
}

BOOLEAN SetupCreateUninstallFile(
    _In_ PPH_SETUP_CONTEXT Context
    )
{
    NTSTATUS status;
    PPH_STRING currentFilePath;
    PPH_STRING backupFilePath;
    PPH_STRING uninstallFilePath;

    if (!NT_SUCCESS(status = PhGetProcessImageFileNameWin32(NtCurrentProcess(), &currentFilePath)))
    {
        Context->ErrorCode = WIN32_FROM_NTSTATUS(status);
        return FALSE;
    }

    // Check if the user has started the setup from the installation folder.
    if (PhStartsWithStringRef2(&currentFilePath->sr, PhGetString(Context->SetupInstallPath), TRUE))
    {
        PhDereferenceObject(currentFilePath);
        return TRUE;
    }

    backupFilePath = PhConcatStrings2(PhGetString(Context->SetupInstallPath), L"\\processhacker-setup.bak");
    uninstallFilePath = PhConcatStrings2(PhGetString(Context->SetupInstallPath), L"\\processhacker-setup.exe");

    if (PhDoesFileExistsWin32(backupFilePath->Buffer))
    {    
        PPH_STRING tempFileName;
        PPH_STRING tempFilePath;

        tempFileName = PhCreateString(L"processhacker-setup.bak");
        tempFilePath = PhCreateCacheFile(tempFileName);

        //if (!NT_SUCCESS(PhDeleteFileWin32(backupFilePath->Buffer)))
        if (!MoveFile(backupFilePath->Buffer, tempFilePath->Buffer))
        {
            Context->ErrorCode = GetLastError();
            return FALSE;
        }

        PhDereferenceObject(tempFilePath);
        PhDereferenceObject(tempFileName);
    }

    if (PhDoesFileExistsWin32(uninstallFilePath->Buffer))
    {
        PPH_STRING tempFileName;
        PPH_STRING tempFilePath;

        //if (!NT_SUCCESS(PhDeleteFileWin32(uninstallFilePath->Buffer)))
        tempFileName = PhCreateString(L"processhacker-setup.exe");
        tempFilePath = PhCreateCacheFile(tempFileName);

        if (!MoveFile(uninstallFilePath->Buffer, tempFilePath->Buffer))
        {
            Context->ErrorCode = GetLastError();
            return FALSE;
        }

        PhDereferenceObject(tempFilePath);
        PhDereferenceObject(tempFileName);
    }

    if (!CopyFile(currentFilePath->Buffer, uninstallFilePath->Buffer, TRUE))
    {
        Context->ErrorCode = GetLastError();
        return FALSE;
    }

    PhDereferenceObject(uninstallFilePath);
    PhDereferenceObject(currentFilePath);

    return TRUE;
}

VOID SetupDeleteUninstallFile(
    _In_ PPH_SETUP_CONTEXT Context
    )
{
    PPH_STRING uninstallFilePath;

    uninstallFilePath = PhConcatStrings2(PhGetString(Context->SetupInstallPath), L"\\processhacker-setup.exe");

    if (PhDoesFileExistsWin32(uninstallFilePath->Buffer))
    {
        PPH_STRING tempFileName;
        PPH_STRING tempFilePath;

        tempFileName = PhCreateString(L"processhacker-setup.exe");
        tempFilePath = PhCreateCacheFile(tempFileName);

        if (PhIsNullOrEmptyString(tempFilePath))
        {
            PhDereferenceObject(tempFileName);
            goto CleanupExit;
        }
    
        MoveFile(uninstallFilePath->Buffer, tempFilePath->Buffer);

        PhDereferenceObject(tempFilePath);
        PhDereferenceObject(tempFileName);
    }

CleanupExit:

    PhDereferenceObject(uninstallFilePath);
}

VOID SetupStartService(
    _In_ PWSTR ServiceName
    )
{
    SC_HANDLE serviceHandle;

    serviceHandle = PhOpenService(
        ServiceName, 
        SERVICE_QUERY_STATUS | SERVICE_START
        );

    if (serviceHandle)
    {
        ULONG statusLength = 0;
        SERVICE_STATUS_PROCESS status;

        memset(&status, 0, sizeof(SERVICE_STATUS_PROCESS));

        if (QueryServiceStatusEx(
            serviceHandle,
            SC_STATUS_PROCESS_INFO,
            (PBYTE)&status,
            sizeof(SERVICE_STATUS_PROCESS),
            &statusLength
            ))
        {
            if (status.dwCurrentState != SERVICE_RUNNING)
            {
                ULONG attempts = 10;

                do
                {
                    StartService(serviceHandle, 0, NULL);

                    if (QueryServiceStatusEx(
                        serviceHandle,
                        SC_STATUS_PROCESS_INFO,
                        (PBYTE)&status,
                        sizeof(SERVICE_STATUS_PROCESS),
                        &statusLength
                        ))
                    {
                        if (status.dwCurrentState == SERVICE_RUNNING)
                        {
                            break;
                        }
                    }

                    PhDelayExecution(1000);

                } while (--attempts != 0);
            }
        }

        CloseServiceHandle(serviceHandle);
    }
}

VOID SetupStopService(
    _In_ PWSTR ServiceName,
    _In_ BOOLEAN RemoveService
    )
{
    SC_HANDLE serviceHandle;

    if (serviceHandle = PhOpenService(
        ServiceName,
        SERVICE_QUERY_STATUS | SERVICE_STOP
        ))
    {
        ULONG statusLength = 0;
        SERVICE_STATUS_PROCESS status;

        memset(&status, 0, sizeof(SERVICE_STATUS_PROCESS));

        if (QueryServiceStatusEx(
            serviceHandle,
            SC_STATUS_PROCESS_INFO,
            (PBYTE)&status,
            sizeof(SERVICE_STATUS_PROCESS),
            &statusLength
            ))
        {
            if (status.dwCurrentState != SERVICE_STOPPED)
            {
                ULONG attempts = 60;

                do
                {
                    SERVICE_STATUS serviceStatus;

                    memset(&serviceStatus, 0, sizeof(SERVICE_STATUS));

                    ControlService(serviceHandle, SERVICE_CONTROL_STOP, &serviceStatus);

                    if (QueryServiceStatusEx(
                        serviceHandle,
                        SC_STATUS_PROCESS_INFO,
                        (PBYTE)&status,
                        sizeof(SERVICE_STATUS_PROCESS),
                        &statusLength
                        ))
                    {
                        if (status.dwCurrentState == SERVICE_STOPPED)
                        {
                            break;
                        }
                    }

                    PhDelayExecution(1000);

                } while (--attempts != 0);
            }
        }

        CloseServiceHandle(serviceHandle);
    }

    if (RemoveService)
    {
        if (serviceHandle = PhOpenService(
            ServiceName,
            DELETE
            ))
        {
            DeleteService(serviceHandle);
            CloseServiceHandle(serviceHandle);
        }
    }
}

BOOLEAN SetupKphCheckInstallState(
    VOID
    )
{
    static PH_STRINGREF kph3ServiceKeyName = PH_STRINGREF_INIT(L"System\\CurrentControlSet\\Services\\KProcessHacker3");
    BOOLEAN kphInstallRequired = FALSE;
    HANDLE runKeyHandle;

    if (NT_SUCCESS(PhOpenKey(
        &runKeyHandle,
        KEY_READ,
        PH_KEY_LOCAL_MACHINE,
        &kph3ServiceKeyName,
        0
        )))
    {
        // Make sure we re-install the driver when KPH was installed as a service. 
        if (PhQueryRegistryUlong(runKeyHandle, L"Start") == SERVICE_SYSTEM_START)
        {
            kphInstallRequired = TRUE;
        }

        NtClose(runKeyHandle);
    }

    return kphInstallRequired;
}

VOID SetupStartKph(
    _In_ PPH_SETUP_CONTEXT Context,
    _In_ BOOLEAN ForceInstall
    )
{
    if (ForceInstall || Context->SetupKphInstallRequired)
    {
        PPH_STRING clientPath;

        clientPath = SetupCreateFullPath(Context->SetupInstallPath, L"\\ProcessHacker.exe");

        if (PhDoesFileExistsWin32(PhGetString(clientPath)))
        {
            HANDLE processHandle;

            if (PhShellExecuteEx(
                NULL,
                PhGetString(clientPath),
                L"-installkph -s",
                SW_NORMAL,
                PhGetOwnTokenAttributes().Elevated ? 0 : PH_SHELL_EXECUTE_ADMIN,
                0,
                &processHandle
                ))
            {
                NtWaitForSingleObject(processHandle, FALSE, NULL);
                NtClose(processHandle);
            }
        }

        PhDereferenceObject(clientPath);
    }

    SetupStartService(L"KProcessHacker3");
}

BOOLEAN SetupUninstallKph(
    _In_ PPH_SETUP_CONTEXT Context
    )
{
    PPH_STRING clientPath;

    // Query the current KPH installation state.
    Context->SetupKphInstallRequired = SetupKphCheckInstallState();

    // Stop and uninstall the current installation.
    clientPath = SetupCreateFullPath(Context->SetupInstallPath, L"\\ProcessHacker.exe");

    if (PhDoesFileExistsWin32(PhGetString(clientPath)))
    {
        HANDLE processHandle;

        if (PhShellExecuteEx(
            NULL,
            PhGetString(clientPath),
            L"-uninstallkph -s",
            SW_NORMAL,
            0,
            0,
            &processHandle
            ))
        {
            NtWaitForSingleObject(processHandle, FALSE, NULL);
            NtClose(processHandle);
        }
    }
    else
    {
        SetupStopService(L"KProcessHacker3", TRUE);
    }

    return TRUE;
}

VOID SetupSetWindowsOptions(
    _In_ PPH_SETUP_CONTEXT Context
    )
{
    static PH_STRINGREF desktopStartmenuPathSr = PH_STRINGREF_INIT(L"%ALLUSERSPROFILE%\\Microsoft\\Windows\\Start Menu\\Programs\\Process Hacker.lnk");
    static PH_STRINGREF peviewerShortcutPathSr = PH_STRINGREF_INIT(L"%ALLUSERSPROFILE%\\Microsoft\\Windows\\Start Menu\\Programs\\PE Viewer.lnk");
    static PH_STRINGREF desktopAllusersPathSr = PH_STRINGREF_INIT(L"%PUBLIC%\\Desktop\\Process Hacker.lnk");
    PPH_STRING clientPathString;
    PPH_STRING string;

    if (string = PhExpandEnvironmentStrings(&desktopStartmenuPathSr))
    {
        clientPathString = SetupCreateFullPath(Context->SetupInstallPath, L"\\ProcessHacker.exe");

        SetupCreateLink(
            PhGetString(string),
            PhGetString(clientPathString),
            PhGetString(Context->SetupInstallPath)
            );

        PhDereferenceObject(clientPathString);
        PhDereferenceObject(string);
    }

    if (string = PhExpandEnvironmentStrings(&desktopAllusersPathSr))
    {
        clientPathString = SetupCreateFullPath(Context->SetupInstallPath, L"\\ProcessHacker.exe");

        SetupCreateLink(
            PhGetString(string),
            PhGetString(clientPathString),
            PhGetString(Context->SetupInstallPath)
            );

        PhDereferenceObject(clientPathString);
        PhDereferenceObject(string);
    }

    if (string = PhExpandEnvironmentStrings(&peviewerShortcutPathSr))
    {
        clientPathString = SetupCreateFullPath(Context->SetupInstallPath, L"\\peview.exe");

        SetupCreateLink(
            PhGetString(string),
            PhGetString(clientPathString),
            PhGetString(Context->SetupInstallPath)
            );

        PhDereferenceObject(clientPathString);
        PhDereferenceObject(string);
    }

    // PhGetKnownLocation(CSIDL_COMMON_PROGRAMS, L"\\Process Hacker.lnk"))
    // PhGetKnownLocation(CSIDL_COMMON_DESKTOPDIRECTORY, L"\\Process Hacker.lnk")
    // PhGetKnownLocation(CSIDL_COMMON_PROGRAMS, L"\\PE Viewer.lnk")

    // Reset the settings file.
    //if (Context->SetupResetSettings)
    //{
    //    static PH_STRINGREF settingsPath = PH_STRINGREF_INIT(L"%APPDATA%\\Process Hacker\\settings.xml");
    //    PPH_STRING settingsFilePath;
    //
    //    if (settingsFilePath = PhExpandEnvironmentStrings(&settingsPath))
    //    {
    //        if (PhDoesFileExistsWin32(settingsFilePath->Buffer))
    //        {
    //            PhDeleteFileWin32(settingsFilePath->Buffer);
    //        }
    //
    //        PhDereferenceObject(settingsFilePath);
    //    }
    //}

    // Set the Windows default Task Manager.
    //if (Context->SetupCreateDefaultTaskManager)
    //{
    //    NTSTATUS status;
    //    HANDLE taskmgrKeyHandle;
    //
    //    status = PhCreateKey(
    //        &taskmgrKeyHandle,
    //        KEY_READ | KEY_WRITE,
    //        PH_KEY_LOCAL_MACHINE,
    //        &TmImageOptionsKeyName,
    //        OBJ_OPENIF,
    //        0,
    //        NULL
    //        );
    //
    //    if (NT_SUCCESS(status))
    //    {
    //        PPH_STRING value;
    //        UNICODE_STRING valueName;
    //
    //        RtlInitUnicodeString(&valueName, L"Debugger");
    //
    //        value = PhConcatStrings(3, L"\"", PhGetString(clientPathString), L"\"");
    //        status = NtSetValueKey(
    //            taskmgrKeyHandle, 
    //            &valueName,
    //            0,
    //            REG_SZ,
    //            value->Buffer, 
    //            (ULONG)value->Length + sizeof(UNICODE_NULL)
    //            );
    //
    //        PhDereferenceObject(value);
    //        NtClose(taskmgrKeyHandle);
    //    }
    //
    //    if (!NT_SUCCESS(status))
    //    {
    //        PhShowStatus(NULL, L"Unable to set the Windows default Task Manager.", status, 0);
    //    }
    //}

    // Create the run startup key.
    //if (Context->SetupCreateSystemStartup)
    //{
    //    NTSTATUS status;
    //    HANDLE runKeyHandle;
    //
    //    if (NT_SUCCESS(status = PhOpenKey(
    //        &runKeyHandle,
    //        KEY_WRITE,
    //        PH_KEY_CURRENT_USER,
    //        &CurrentUserRunKeyName,
    //        0
    //        )))
    //    {
    //        PPH_STRING value;
    //        UNICODE_STRING valueName;
    //
    //        RtlInitUnicodeString(&valueName, L"Process Hacker");
    //
    //        if (Context->SetupCreateMinimizedSystemStartup)
    //            value = PhConcatStrings(3, L"\"", PhGetString(clientPathString), L"\" -hide");
    //        else
    //            value = PhConcatStrings(3, L"\"", PhGetString(clientPathString), L"\"");
    //
    //        NtSetValueKey(runKeyHandle, &valueName, 0, REG_SZ, value->Buffer, (ULONG)value->Length + 2);
    //        NtClose(runKeyHandle);
    //    }
    //    else
    //    {
    //        PhShowStatus(NULL, L"Unable to create the startup entry.", status, 0);
    //    }
    //}
}

VOID SetupDeleteWindowsOptions(
    _In_ PPH_SETUP_CONTEXT Context
    )
{
    static PH_STRINGREF desktopShortcutPathSr = PH_STRINGREF_INIT(L"%ALLUSERSPROFILE%\\Microsoft\\Windows\\Start Menu\\Programs\\Process Hacker.lnk");
    static PH_STRINGREF peviewerShortcutPathSr = PH_STRINGREF_INIT(L"%ALLUSERSPROFILE%\\Microsoft\\Windows\\Start Menu\\Programs\\PE Viewer.lnk");
    static PH_STRINGREF desktopAllusersPathSr = PH_STRINGREF_INIT(L"%PUBLIC%\\Desktop\\Process Hacker.lnk");
    PPH_STRING string;
    HANDLE keyHandle;

    if (string = PhExpandEnvironmentStrings(&desktopShortcutPathSr))
    {
        PhDeleteFileWin32(string->Buffer);
        PhDereferenceObject(string);
    }
 
    if (string = PhExpandEnvironmentStrings(&peviewerShortcutPathSr))
    {
        PhDeleteFileWin32(string->Buffer);
        PhDereferenceObject(string);
    }

    if (string = PhExpandEnvironmentStrings(&desktopAllusersPathSr))
    {
        PhDeleteFileWin32(string->Buffer);
        PhDereferenceObject(string);
    }

    if (NT_SUCCESS(PhOpenKey(
        &keyHandle,
        DELETE,
        PH_KEY_LOCAL_MACHINE,
        &PhImageOptionsKeyName,
        0
        )))
    {
        NtDeleteKey(keyHandle);
        NtClose(keyHandle);
    }

    if (NT_SUCCESS(PhOpenKey(
        &keyHandle,
        DELETE,
        PH_KEY_LOCAL_MACHINE,
        &PhImageOptionsWow64KeyName,
        0
        )))
    {
        NtDeleteKey(keyHandle);
        NtClose(keyHandle);
    }

    if (NT_SUCCESS(PhOpenKey(
        &keyHandle,
        DELETE,
        PH_KEY_LOCAL_MACHINE,
        &TmImageOptionsKeyName,
        0
        )))
    {
        NtDeleteKey(keyHandle);
        NtClose(keyHandle);
    }
}

BOOLEAN SetupExecuteProcessHacker(
    _In_ PPH_SETUP_CONTEXT Context
    )
{
    BOOLEAN success = FALSE;
    PPH_STRING clientPath;

    if (PhIsNullOrEmptyString(Context->SetupInstallPath))
        return FALSE;

    clientPath = SetupCreateFullPath(Context->SetupInstallPath, L"\\ProcessHacker.exe");

    if (PhDoesFileExistsWin32(clientPath->Buffer))
    {
        success = PhShellExecuteEx(
            Context->DialogHandle,
            clientPath->Buffer,
            NULL,
            SW_SHOWNORMAL,
            0,
            0,
            NULL
            );
    }

    PhDereferenceObject(clientPath);
    return success;
}

VOID SetupUpgradeSettingsFile(
    VOID
    )
{
    static PH_STRINGREF settingsPath = PH_STRINGREF_INIT(L"%APPDATA%\\Process Hacker\\settings.xml");
    static PH_STRINGREF settingsLegacyPath = PH_STRINGREF_INIT(L"%APPDATA%\\Process Hacker 2\\settings.xml");
    PPH_STRING settingsFilePath;
    PPH_STRING oldSettingsFileName;

    settingsFilePath = PhExpandEnvironmentStrings(&settingsPath);
    oldSettingsFileName = PhExpandEnvironmentStrings(&settingsLegacyPath);

    if (settingsFilePath && oldSettingsFileName)
    {
        if (!PhDoesFileExistsWin32(settingsFilePath->Buffer))
        {
            CopyFile(oldSettingsFileName->Buffer, settingsFilePath->Buffer, FALSE);
        }
    }

    if (oldSettingsFileName) PhDereferenceObject(oldSettingsFileName);
    if (settingsFilePath) PhDereferenceObject(settingsFilePath);
}

VOID SetupCreateImageFileExecutionOptions(
    VOID
    )
{
    HANDLE keyHandle;
    SYSTEM_INFO info;

    if (WindowsVersion < WINDOWS_10)
        return;

    GetNativeSystemInfo(&info);

    if (NT_SUCCESS(PhCreateKey(
        &keyHandle,
        KEY_WRITE,
        PH_KEY_LOCAL_MACHINE,
        &PhImageOptionsKeyName,
        OBJ_OPENIF,
        0,
        NULL
        )))
    {
        static UNICODE_STRING valueName = RTL_CONSTANT_STRING(L"MitigationOptions");

        NtSetValueKey(keyHandle, &valueName, 0, REG_QWORD, &(ULONG64)
        {
            PROCESS_CREATION_MITIGATION_POLICY_HEAP_TERMINATE_ALWAYS_ON |
            PROCESS_CREATION_MITIGATION_POLICY_BOTTOM_UP_ASLR_ALWAYS_ON |
            PROCESS_CREATION_MITIGATION_POLICY_HIGH_ENTROPY_ASLR_ALWAYS_ON |
            PROCESS_CREATION_MITIGATION_POLICY_EXTENSION_POINT_DISABLE_ALWAYS_ON |
            PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON |
            PROCESS_CREATION_MITIGATION_POLICY_CONTROL_FLOW_GUARD_ALWAYS_ON |
            PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_REMOTE_ALWAYS_ON |
            PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_LOW_LABEL_ALWAYS_ON
        }, sizeof(ULONG64));

        NtClose(keyHandle);
    }

    if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
    {
        if (NT_SUCCESS(PhCreateKey(
            &keyHandle,
            KEY_WRITE,
            PH_KEY_LOCAL_MACHINE,
            &PhImageOptionsWow64KeyName,
            OBJ_OPENIF,
            0,
            NULL
            )))
        {
            static UNICODE_STRING valueName = RTL_CONSTANT_STRING(L"MitigationOptions");

            NtSetValueKey(keyHandle, &valueName, 0, REG_QWORD, &(ULONG64)
            {
                PROCESS_CREATION_MITIGATION_POLICY_HEAP_TERMINATE_ALWAYS_ON |
                PROCESS_CREATION_MITIGATION_POLICY_BOTTOM_UP_ASLR_ALWAYS_ON |
                PROCESS_CREATION_MITIGATION_POLICY_HIGH_ENTROPY_ASLR_ALWAYS_ON |
                PROCESS_CREATION_MITIGATION_POLICY_EXTENSION_POINT_DISABLE_ALWAYS_ON |
                PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON |
                PROCESS_CREATION_MITIGATION_POLICY_CONTROL_FLOW_GUARD_ALWAYS_ON |
                PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_REMOTE_ALWAYS_ON |
                PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_LOW_LABEL_ALWAYS_ON
            }, sizeof(ULONG64));

            NtClose(keyHandle);
        }
    }
}


VOID ExtractResourceToFile(
    _In_ PWSTR Resource, 
    _In_ PWSTR FileName
    )
{
    HANDLE fileHandle = NULL;
    PVOID resourceBuffer;
    ULONG resourceLength;
    IO_STATUS_BLOCK isb;

    if (!PhLoadResource(
        PhInstanceHandle, 
        Resource,
        RT_RCDATA, 
        &resourceLength, 
        &resourceBuffer
        ))
    {
        goto CleanupExit;
    }

    if (!NT_SUCCESS(PhCreateFileWin32(
        &fileHandle,
        FileName,
        FILE_GENERIC_READ | FILE_GENERIC_WRITE,
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        FILE_OVERWRITE_IF,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
        )))
    {
        goto CleanupExit;
    }

    if (!NT_SUCCESS(NtWriteFile(
        fileHandle,
        NULL,
        NULL,
        NULL,
        &isb,
        resourceBuffer,
        resourceLength,
        NULL,
        NULL
        )))
    {
        goto CleanupExit;
    }

    if (isb.Information != resourceLength)
        goto CleanupExit;

CleanupExit:

    if (fileHandle)
        NtClose(fileHandle);
}

BOOLEAN ConnectionAvailable(VOID)
{
    INetworkListManager* networkListManager = NULL;

    if (SUCCEEDED(CoCreateInstance(
        &CLSID_NetworkListManager, 
        NULL, 
        CLSCTX_ALL, 
        &IID_INetworkListManager, 
        &networkListManager
        )))
    {
        VARIANT_BOOL isConnected = VARIANT_FALSE;
        VARIANT_BOOL isConnectedInternet = VARIANT_FALSE;

        // Query the relevant properties.
        INetworkListManager_get_IsConnected(networkListManager, &isConnected);
        INetworkListManager_get_IsConnectedToInternet(networkListManager, &isConnectedInternet);

        // Cleanup the INetworkListManger COM objects.
        INetworkListManager_Release(networkListManager);

        // Check if Windows is connected to a network and it's connected to the internet.
        if (isConnected == VARIANT_TRUE && isConnectedInternet == VARIANT_TRUE)
            return TRUE;

        // We're not connected to anything
    }

    return FALSE;
}

VOID SetupCreateLink(
    _In_ PWSTR LinkFilePath,
    _In_ PWSTR FilePath,
    _In_ PWSTR FileParentDir
    )
{
    IShellLink* shellLinkPtr = NULL;
    IPersistFile* persistFilePtr = NULL;
    //IPropertyStore* propertyStorePtr;

    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, &shellLinkPtr)))
        goto CleanupExit;

    if (FAILED(IShellLinkW_QueryInterface(shellLinkPtr, &IID_IPersistFile, &persistFilePtr)))
        goto CleanupExit;

    //if (SUCCEEDED(IShellLinkW_QueryInterface(shellLinkPtr, &IID_IPropertyStore, &propertyStorePtr)))
    //{
    //    PROPVARIANT appIdPropVar;
    //
    //    PropVariantInit(&appIdPropVar);
    //
    //    appIdPropVar.vt = VT_BSTR;
    //    appIdPropVar.bstrVal = SysAllocString(AppUserModelId);
    //
    //    if (SUCCEEDED(IPropertyStore_SetValue(propertyStorePtr, &PKEY_AppUserModel_ID, &appIdPropVar)))
    //    {
    //        IPropertyStore_Commit(propertyStorePtr);
    //    }
    //
    //    PropVariantClear(&appIdPropVar);
    //    IPropertyStore_Release(propertyStorePtr);
    //}

    // Load existing shell item if it exists...
    //IPersistFile_Load(persistFilePtr, LinkFilePath, STGM_READWRITE);

    //IShellLinkW_SetDescription(shellLinkPtr, FileComment);
    //IShellLinkW_SetHotkey(shellLinkPtr, MAKEWORD(VK_END, HOTKEYF_CONTROL | HOTKEYF_ALT));
    IShellLinkW_SetWorkingDirectory(shellLinkPtr, FileParentDir);
    IShellLinkW_SetIconLocation(shellLinkPtr, FilePath, 0);

    // Set the shortcut target path...
    if (FAILED(IShellLinkW_SetPath(shellLinkPtr, FilePath)))
        goto CleanupExit;

    if (PhDoesFileExistsWin32(LinkFilePath))
        PhDeleteFileWin32(LinkFilePath);

    // Save the shortcut to the file system...
    IPersistFile_Save(persistFilePtr, LinkFilePath, TRUE);

    //SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATH | SHCNF_FLUSHNOWAIT, LinkFilePath, NULL);

CleanupExit:
    if (persistFilePtr)
        IPersistFile_Release(persistFilePtr);
    if (shellLinkPtr)
        IShellLinkW_Release(shellLinkPtr);
}

BOOLEAN CheckProcessHackerInstalled(
    VOID
    )
{
    BOOLEAN installed = FALSE;
    PPH_STRING installPath;
    PPH_STRING exePath;

    installPath = GetProcessHackerInstallPath();

    if (!PhIsNullOrEmptyString(installPath))
    {
        exePath = SetupCreateFullPath(installPath, L"\\ProcessHacker.exe");

        // Check if the value has a valid file path.
        installed = GetFileAttributes(PhGetString(exePath)) != INVALID_FILE_ATTRIBUTES;

        PhDereferenceObject(exePath);
    }

    if (installPath)
        PhDereferenceObject(installPath);

    return installed;
}

PPH_STRING GetProcessHackerInstallPath(
    VOID
    )
{
    HANDLE keyHandle;
    PPH_STRING installPath = NULL;

    if (NT_SUCCESS(PhOpenKey(
        &keyHandle,
        KEY_READ | KEY_WOW64_64KEY,
        PH_KEY_LOCAL_MACHINE,
        &UninstallKeyName,
        0
        )))
    {
        installPath = PhQueryRegistryString(keyHandle, L"InstallLocation");
        NtClose(keyHandle);
    }

    return installPath;
}

static BOOLEAN NTAPI PhpPreviousInstancesCallback(
    _In_ PPH_STRINGREF Name,
    _In_ PPH_STRINGREF TypeName,
    _In_opt_ PVOID Context
    )
{
    HANDLE objectHandle;
    UNICODE_STRING objectNameUs;
    OBJECT_ATTRIBUTES objectAttributes;
    MUTANT_OWNER_INFORMATION objectInfo;

    if (!PhStartsWithStringRef2(Name, L"PhMutant_", TRUE) &&
        !PhStartsWithStringRef2(Name, L"PhSetupMutant_", TRUE) &&
        !PhStartsWithStringRef2(Name, L"PeViewerMutant_", TRUE))
    {
        return TRUE;
    }

    if (!PhStringRefToUnicodeString(Name, &objectNameUs))
        return TRUE;

    InitializeObjectAttributes(
        &objectAttributes,
        &objectNameUs,
        OBJ_CASE_INSENSITIVE,
        PhGetNamespaceHandle(),
        NULL
        );

    if (!NT_SUCCESS(NtOpenMutant(
        &objectHandle,
        MUTANT_QUERY_STATE,
        &objectAttributes
        )))
    {
        return TRUE;
    }

    if (NT_SUCCESS(PhGetMutantOwnerInformation(
        objectHandle,
        &objectInfo
        )))
    {
        HWND hwnd;
        HANDLE processHandle = NULL;

        if (objectInfo.ClientId.UniqueProcess == NtCurrentProcessId())
            goto CleanupExit;

        PhOpenProcess(
            &processHandle, 
            ProcessQueryAccess | PROCESS_TERMINATE,
            objectInfo.ClientId.UniqueProcess
            );
        
        hwnd = PhGetProcessMainWindowEx(
            objectInfo.ClientId.UniqueProcess,
            processHandle,
            FALSE
            );

        if (hwnd)
        {
            SendMessageTimeout(hwnd, WM_QUIT, 0, 0, SMTO_BLOCK, 5000, NULL);
        }

        if (processHandle)
        {
            NtTerminateProcess(processHandle, 1);
        }

    CleanupExit:
        if (processHandle) NtClose(processHandle);
    }

    NtClose(objectHandle);

    return TRUE;
}

BOOLEAN ShutdownProcessHacker(
    VOID
    )
{
    PhEnumDirectoryObjects(PhGetNamespaceHandle(), PhpPreviousInstancesCallback, NULL);
    return TRUE;
}

NTSTATUS QueryProcessesUsingVolumeOrFile(
    _In_ HANDLE VolumeOrFileHandle,
    _Out_ PFILE_PROCESS_IDS_USING_FILE_INFORMATION *Information
    )
{
    static ULONG initialBufferSize = 0x4000;
    NTSTATUS status;
    PVOID buffer;
    ULONG bufferSize;
    IO_STATUS_BLOCK isb;

    bufferSize = initialBufferSize;
    buffer = malloc(bufferSize);

    while ((status = NtQueryInformationFile(
        VolumeOrFileHandle,
        &isb,
        buffer,
        bufferSize,
        FileProcessIdsUsingFileInformation
        )) == STATUS_INFO_LENGTH_MISMATCH)
    {
        free(buffer);
        bufferSize *= 2;

        // Fail if we're resizing the buffer to something very large.
        if (bufferSize > SIZE_MAX)
            return STATUS_INSUFFICIENT_RESOURCES;

        buffer = malloc(bufferSize);
    }

    if (!NT_SUCCESS(status))
    {
        free(buffer);
        return status;
    }

    if (bufferSize <= 0x100000) initialBufferSize = bufferSize;
    *Information = (PFILE_PROCESS_IDS_USING_FILE_INFORMATION)buffer;

    return status;
}

PPH_STRING SetupCreateFullPath(
    _In_ PPH_STRING Path,
    _In_ PWSTR FileName
    )
{
    PPH_STRING pathString;
    PPH_STRING tempString;

    pathString = PhConcatStrings2(PhGetString(Path), FileName);

    if (NT_SUCCESS(PhGetFullPathEx(pathString->Buffer, NULL, &tempString)))
    {
        PhMoveReference(&pathString, tempString);
        return pathString;
    }

    return pathString;
}

BOOLEAN SetupBase64StringToBufferEx(
    _In_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ PVOID* OutputBuffer,
    _Out_opt_ ULONG* OutputBufferLength
    )
{
    PVOID buffer = NULL;
    ULONG bufferLength = 0;
    ULONG bufferSkip = 0;
    ULONG bufferFlags = 0;

    if (CryptStringToBinary(InputBuffer, InputBufferLength, CRYPT_STRING_BASE64, NULL, &bufferLength, &bufferSkip, &bufferFlags))
    {
        if (buffer = PhAllocateSafe(bufferLength))
        {
            if (!CryptStringToBinary(InputBuffer, InputBufferLength, CRYPT_STRING_BASE64, buffer, &bufferLength, &bufferSkip, &bufferFlags))
            {
                PhFree(buffer);
                buffer = NULL;
            }
        }
    }

    if (buffer)
    {
        if (OutputBuffer)
            *OutputBuffer = buffer;
        else
            PhFree(buffer);

        if (OutputBufferLength)
            *OutputBufferLength = bufferLength;

        return TRUE;
    }

    return FALSE;
}
