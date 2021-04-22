/*
 * Process Hacker -
 *   UI actions
 *
 * Copyright (C) 2010-2016 wj32
 * Copyright (C) 2017-2018 dmex
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

/*
 * These are a set of consistent functions which will perform actions on objects such as processes,
 * threads and services, while displaying any necessary prompts and error messages. Automatic
 * elevation can also easily be added if necessary.
 */

#include <phapp.h>
#include <actions.h>

#include <iphlpapi.h>
#include <winsta.h>

#include <apiimport.h>
#include <kphuser.h>
#include <svcsup.h>
#include <settings.h>

#include <hndlprv.h>
#include <memprv.h>
#include <modprv.h>
#include <netprv.h>
#include <phsvccl.h>
#include <procprv.h>
#include <phsettings.h>
#include <srvprv.h>
#include <thrdprv.h>

static PWSTR DangerousProcesses[] =
{
    L"csrss.exe", L"dwm.exe", L"logonui.exe", L"lsass.exe", L"lsm.exe",
    L"services.exe", L"smss.exe", L"wininit.exe", L"winlogon.exe"
};

static PPH_STRING DebuggerCommand = NULL;
static ULONG PhSvcReferenceCount = 0;
static PH_PHSVC_MODE PhSvcCurrentMode;
static PH_QUEUED_LOCK PhSvcStartLock = PH_QUEUED_LOCK_INIT;

HRESULT CALLBACK PhpElevateActionCallbackProc(
    _In_ HWND hwnd,
    _In_ UINT uNotification,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam,
    _In_ LONG_PTR dwRefData
    )
{
    switch (uNotification)
    {
    case TDN_CREATED:
        SendMessage(hwnd, TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, IDYES, TRUE);
        break;
    }

    return S_OK;
}

_Success_(return)
BOOLEAN PhpShowElevatePrompt(
    _In_ HWND hWnd,
    _In_ PWSTR Message,
    _Out_ PINT Button
    )
{
    TASKDIALOGCONFIG config = { sizeof(config) };
    TASKDIALOG_BUTTON buttons[1];
    INT button;

    // Currently the error dialog box is similar to the one displayed
    // when you try to label a drive in Windows Explorer. It's much better
    // than the clunky dialog in PH 1.x.

    config.hwndParent = hWnd;
    config.hInstance = PhInstanceHandle;
    config.dwFlags = IsWindowVisible(hWnd) ? TDF_POSITION_RELATIVE_TO_WINDOW : 0;
    config.pszWindowTitle = PhApplicationName;
    config.pszMainIcon = TD_ERROR_ICON;
    config.pszMainInstruction = PhaConcatStrings2(Message, L".")->Buffer;
    config.pszContent = L"You will need to provide administrator permission. "
        L"Click Continue to complete this operation.";
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;

    buttons[0].nButtonID = IDYES;
    buttons[0].pszButtonText = L"Continue";

    config.cButtons = 1;
    config.pButtons = buttons;
    config.nDefaultButton = IDYES;

    config.pfCallback = PhpElevateActionCallbackProc;

    if (SUCCEEDED(TaskDialogIndirect(
        &config,
        &button,
        NULL,
        NULL
        )))
    {
        *Button = button;
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/**
 * Shows an error, prompts for elevation, and executes a command.
 *
 * \param hWnd The window to display user interface components on.
 * \param Message A message describing the operation that failed.
 * \param Status A NTSTATUS value.
 * \param Command The arguments to pass to the new instance of
 * the application, if required.
 * \param Success A variable which receives TRUE if the elevated
 * action succeeded or FALSE if the action failed.
 *
 * \return TRUE if the user was prompted for elevation, otherwise
 * FALSE, in which case you need to show your own error message.
 */
_Success_(return)
BOOLEAN PhpShowErrorAndElevateAction(
    _In_ HWND hWnd,
    _In_ PWSTR Message,
    _In_ NTSTATUS Status,
    _In_ PWSTR Command,
    _Out_opt_ PBOOLEAN Success
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    PH_ACTION_ELEVATION_LEVEL elevationLevel;
    INT button = IDNO;

    if (!(
        Status == STATUS_ACCESS_DENIED ||
        Status == STATUS_PRIVILEGE_NOT_HELD ||
        (NT_NTWIN32(Status) && WIN32_FROM_NTSTATUS(Status) == ERROR_ACCESS_DENIED)
        ))
        return FALSE;

    if (PhGetOwnTokenAttributes().Elevated)
        return FALSE;

    elevationLevel = PhGetIntegerSetting(L"ElevationLevel");

    if (elevationLevel == NeverElevateAction)
        return FALSE;

    if (elevationLevel == PromptElevateAction)
    {
        if (!PhpShowElevatePrompt(hWnd, Message, &button))
            return FALSE;
    }

    if (elevationLevel == AlwaysElevateAction || button == IDYES)
    {
        HANDLE processHandle;
        LARGE_INTEGER timeout;
        PROCESS_BASIC_INFORMATION basicInfo;

        if (PhShellProcessHacker(
            hWnd,
            Command,
            SW_SHOW,
            PH_SHELL_EXECUTE_ADMIN,
            PH_SHELL_APP_PROPAGATE_PARAMETERS,
            0,
            &processHandle
            ))
        {
            timeout.QuadPart = -(LONGLONG)UInt32x32To64(10, PH_TIMEOUT_SEC);
            status = NtWaitForSingleObject(processHandle, FALSE, &timeout);

            if (
                status == STATUS_WAIT_0 &&
                NT_SUCCESS(status = PhGetProcessBasicInformation(processHandle, &basicInfo))
                )
            {
                status = basicInfo.ExitStatus;
            }

            NtClose(processHandle);
        }
    }

    if (Success)
        *Success = NT_SUCCESS(status);
    if (!NT_SUCCESS(status))
        PhShowStatus(hWnd, Message, status, 0);

    return TRUE;
}

/**
 * Shows an error, prompts for elevation, and connects to phsvc.
 *
 * \param hWnd The window to display user interface components on.
 * \param Message A message describing the operation that failed.
 * \param Status A NTSTATUS value.
 * \param Connected A variable which receives TRUE if the user
 * elevated the action and phsvc was started, or FALSE if the user
 * cancelled elevation. If the value is TRUE, you need to
 * perform any necessary phsvc calls and use PhUiDisconnectFromPhSvc()
 * to disconnect from phsvc.
 *
 * \return TRUE if the user was prompted for elevation, otherwise
 * FALSE, in which case you need to show your own error message.
 */
BOOLEAN PhpShowErrorAndConnectToPhSvc(
    _In_ HWND hWnd,
    _In_ PWSTR Message,
    _In_ NTSTATUS Status,
    _Out_ PBOOLEAN Connected
    )
{
    PH_ACTION_ELEVATION_LEVEL elevationLevel;
    INT button = IDNO;

    *Connected = FALSE;

    if (!(
        Status == STATUS_ACCESS_DENIED ||
        Status == STATUS_PRIVILEGE_NOT_HELD ||
        (NT_NTWIN32(Status) && WIN32_FROM_NTSTATUS(Status) == ERROR_ACCESS_DENIED)
        ))
        return FALSE;

    if (PhGetOwnTokenAttributes().Elevated)
        return FALSE;

    elevationLevel = PhGetIntegerSetting(L"ElevationLevel");

    if (elevationLevel == NeverElevateAction)
        return FALSE;

    // Try to connect now so we can avoid prompting the user.
    if (PhUiConnectToPhSvc(hWnd, TRUE))
    {
        *Connected = TRUE;
        return TRUE;
    }

    if (elevationLevel == PromptElevateAction)
    {
        if (!PhpShowElevatePrompt(hWnd, Message, &button))
            return FALSE;
    }

    if (elevationLevel == AlwaysElevateAction || button == IDYES)
    {
        *Connected = PhUiConnectToPhSvc(hWnd, FALSE);
    }

    return TRUE;
}

/**
 * Connects to phsvc.
 *
 * \param hWnd The window to display user interface components on.
 * \param ConnectOnly TRUE to only try to connect to phsvc, otherwise
 * FALSE to try to elevate and start phsvc if the initial connection
 * attempt failed.
 */
BOOLEAN PhUiConnectToPhSvc(
    _In_opt_ HWND hWnd,
    _In_ BOOLEAN ConnectOnly
    )
{
    return PhUiConnectToPhSvcEx(hWnd, ElevatedPhSvcMode, ConnectOnly);
}

VOID PhpGetPhSvcPortName(
    _In_ PH_PHSVC_MODE Mode,
    _Out_ PUNICODE_STRING PortName
    )
{
    switch (Mode)
    {
    case ElevatedPhSvcMode:
        if (!PhIsExecutingInWow64())
            RtlInitUnicodeString(PortName, PHSVC_PORT_NAME);
        else
            RtlInitUnicodeString(PortName, PHSVC_WOW64_PORT_NAME);
        break;
    case Wow64PhSvcMode:
        RtlInitUnicodeString(PortName, PHSVC_WOW64_PORT_NAME);
        break;
    default:
        PhRaiseStatus(STATUS_INVALID_PARAMETER);
        break;
    }
}

BOOLEAN PhpStartPhSvcProcess(
    _In_opt_ HWND hWnd,
    _In_ PH_PHSVC_MODE Mode
    )
{
    switch (Mode)
    {
    case ElevatedPhSvcMode:
        if (PhShellProcessHacker(
            hWnd,
            L"-phsvc",
            SW_HIDE,
            PH_SHELL_EXECUTE_ADMIN | PH_SHELL_EXECUTE_NOZONECHECKS,
            PH_SHELL_APP_PROPAGATE_PARAMETERS,
            0,
            NULL
            ))
        {
            return TRUE;
        }

        break;
    case Wow64PhSvcMode:
        {
            static PWSTR relativeFileNames[] =
            {
                L"\\x86\\ProcessHacker.exe",
                L"\\..\\x86\\ProcessHacker.exe",
#ifdef DEBUG
                L"\\..\\Debug32\\ProcessHacker.exe",
#endif
                L"\\..\\Release32\\ProcessHacker.exe"
            };

            ULONG i;
            PPH_STRING applicationDirectory;

            if (!(applicationDirectory = PhGetApplicationDirectory()))
                return FALSE;

            for (i = 0; i < RTL_NUMBER_OF(relativeFileNames); i++)
            {
                PPH_STRING fileName;
                PPH_STRING fileFullPath;

                fileName = PhConcatStringRefZ(&applicationDirectory->sr, relativeFileNames[i]);

                if (fileFullPath = PhGetFullPath(fileName->Buffer, NULL))
                    PhMoveReference(&fileName, fileFullPath);

                if (PhDoesFileExistsWin32(fileName->Buffer))
                {
                    if (PhShellProcessHackerEx(
                        hWnd,
                        fileName->Buffer,
                        L"-phsvc",
                        SW_HIDE,
                        PH_SHELL_EXECUTE_NOZONECHECKS,
                        PH_SHELL_APP_PROPAGATE_PARAMETERS,
                        0,
                        NULL
                        ))
                    {
                        PhDereferenceObject(fileName);
                        PhDereferenceObject(applicationDirectory);
                        return TRUE;
                    }
                }

                PhDereferenceObject(fileName);
            }

            PhDereferenceObject(applicationDirectory);
        }
        break;
    }

    return FALSE;
}

/**
 * Connects to phsvc.
 *
 * \param hWnd The window to display user interface components on.
 * \param Mode The type of phsvc instance to connect to.
 * \param ConnectOnly TRUE to only try to connect to phsvc, otherwise
 * FALSE to try to elevate and start phsvc if the initial connection
 * attempt failed.
 */
BOOLEAN PhUiConnectToPhSvcEx(
    _In_opt_ HWND hWnd,
    _In_ PH_PHSVC_MODE Mode,
    _In_ BOOLEAN ConnectOnly
    )
{
    NTSTATUS status;
    BOOLEAN started;
    UNICODE_STRING portName;

    if (_InterlockedIncrementNoZero(&PhSvcReferenceCount))
    {
        if (PhSvcCurrentMode == Mode)
        {
            started = TRUE;
        }
        else
        {
            _InterlockedDecrement(&PhSvcReferenceCount);
            started = FALSE;
        }
    }
    else
    {
        PhAcquireQueuedLockExclusive(&PhSvcStartLock);

        if (_InterlockedExchange(&PhSvcReferenceCount, 0) == 0)
        {
            started = FALSE;
            PhpGetPhSvcPortName(Mode, &portName);

            // Try to connect first, then start the server if we failed.
            status = PhSvcConnectToServer(&portName, 0);

            if (NT_SUCCESS(status))
            {
                started = TRUE;
                PhSvcCurrentMode = Mode;
                _InterlockedIncrement(&PhSvcReferenceCount);
            }
            else if (!ConnectOnly)
            {
                // Prompt for elevation, and then try to connect to the server.

                if (PhpStartPhSvcProcess(hWnd, Mode))
                    started = TRUE;

                if (started)
                {
                    ULONG attempts = 50;

                    // Try to connect several times because the server may take
                    // a while to initialize.
                    do
                    {
                        status = PhSvcConnectToServer(&portName, 0);

                        if (NT_SUCCESS(status))
                            break;

                        PhDelayExecution(100);

                    } while (--attempts != 0);

                    // Increment the reference count even if we failed.
                    // We don't want to prompt the user again.

                    PhSvcCurrentMode = Mode;
                    _InterlockedIncrement(&PhSvcReferenceCount);
                }
            }
        }
        else
        {
            if (PhSvcCurrentMode == Mode)
            {
                started = TRUE;
                _InterlockedIncrement(&PhSvcReferenceCount);
            }
            else
            {
                started = FALSE;
            }
        }

        PhReleaseQueuedLockExclusive(&PhSvcStartLock);
    }

    return started;
}

/**
 * Disconnects from phsvc.
 */
VOID PhUiDisconnectFromPhSvc(
    VOID
    )
{
    PhAcquireQueuedLockExclusive(&PhSvcStartLock);

    if (_InterlockedDecrement(&PhSvcReferenceCount) == 0)
    {
        PhSvcDisconnectFromServer();
    }

    PhReleaseQueuedLockExclusive(&PhSvcStartLock);
}

BOOLEAN PhUiLockComputer(
    _In_ HWND hWnd
    )
{
    if (LockWorkStation())
        return TRUE;
    else
        PhShowStatus(hWnd, L"Unable to lock the computer.", 0, GetLastError());

    return FALSE;
}

BOOLEAN PhUiLogoffComputer(
    _In_ HWND hWnd
    )
{
    if (ExitWindowsEx(EWX_LOGOFF, 0))
        return TRUE;
    else
        PhShowStatus(hWnd, L"Unable to log off the computer.", 0, GetLastError());

    return FALSE;
}

BOOLEAN PhUiSleepComputer(
    _In_ HWND hWnd
    )
{
    NTSTATUS status;

    if (NT_SUCCESS(status = NtInitiatePowerAction(
        PowerActionSleep,
        PowerSystemSleeping1,
        0,
        FALSE
        )))
        return TRUE;
    else
        PhShowStatus(hWnd, L"Unable to sleep the computer.", status, 0);

    return FALSE;
}

BOOLEAN PhUiHibernateComputer(
    _In_ HWND hWnd
    )
{
    NTSTATUS status;

    if (NT_SUCCESS(status = NtInitiatePowerAction(
        PowerActionHibernate,
        PowerSystemSleeping1,
        0,
        FALSE
        )))
        return TRUE;
    else
        PhShowStatus(hWnd, L"Unable to hibernate the computer.", status, 0);

    return FALSE;
}

BOOLEAN PhUiRestartComputer(
    _In_ HWND hWnd,
    _In_ ULONG Flags
    )
{
    ULONG status;
    BOOLEAN forceShutdown;

    // Force shutdown when holding the control key. (dmex)
    forceShutdown = !!(GetKeyState(VK_CONTROL) < 0);

    if (!PhGetIntegerSetting(L"EnableWarnings") || PhShowConfirmMessage(
        hWnd,
        L"restart",
        L"the computer",
        NULL,
        FALSE
        ))
    {
        if (forceShutdown)
        {
            status = NtShutdownSystem(ShutdownReboot);

            if (NT_SUCCESS(status))
                return TRUE;

            PhShowStatus(hWnd, L"Unable to restart the computer.", status, 0);
        }
        else
        {
            status = InitiateShutdown(
                NULL,
                NULL,
                0,
                SHUTDOWN_RESTART | Flags,
                SHTDN_REASON_FLAG_PLANNED
                );

            if (status == ERROR_SUCCESS)
                return TRUE;

            PhShowStatus(hWnd, L"Unable to restart the computer.", 0, status);

            //if (ExitWindowsEx(EWX_REBOOT | EWX_BOOTOPTIONS, 0))
            //    return TRUE;
            //else
            //    PhShowStatus(hWnd, L"Unable to restart the computer.", 0, GetLastError());
        }
    }

    return FALSE;
}

BOOLEAN PhUiShutdownComputer(
    _In_ HWND hWnd,
    _In_ ULONG Flags
    )
{
    ULONG status;
    BOOLEAN forceShutdown;

    // Force shutdown when holding the control key. (dmex)
    forceShutdown = !!(GetKeyState(VK_CONTROL) < 0);

    if (!PhGetIntegerSetting(L"EnableWarnings") || PhShowConfirmMessage(
        hWnd,
        L"shut down",
        L"the computer",
        NULL,
        FALSE
        ))
    {
        if (forceShutdown)
        {
            status = NtShutdownSystem(ShutdownPowerOff);

            if (!NT_SUCCESS(status))
            {
                PhShowStatus(hWnd, L"Unable to shut down the computer.", status, 0);
            }
        }
        else
        {
            status = InitiateShutdown(
                NULL,
                NULL,
                0,
                SHUTDOWN_POWEROFF | Flags,
                SHTDN_REASON_FLAG_PLANNED
                );

            if (status == ERROR_SUCCESS)
                return TRUE;

            PhShowStatus(hWnd, L"Unable to shut down the computer.", 0, status);

            //if (ExitWindowsEx(EWX_POWEROFF | EWX_HYBRID_SHUTDOWN, 0))
            //    return TRUE;
            //else if (ExitWindowsEx(EWX_SHUTDOWN | EWX_HYBRID_SHUTDOWN, 0))
            //    return TRUE;
            //else
            //    PhShowStatus(hWnd, L"Unable to shut down the computer.", 0, GetLastError());
        }
    }

    return FALSE;
}

BOOLEAN PhUiConnectSession(
    _In_ HWND hWnd,
    _In_ ULONG SessionId
    )
{
    BOOLEAN success = FALSE;
    PPH_STRING selectedChoice = NULL;
    PPH_STRING oldSelectedChoice = NULL;

    // Try once with no password.
    if (WinStationConnectW(NULL, SessionId, LOGONID_CURRENT, L"", TRUE))
        return TRUE;

    while (PhaChoiceDialog(
        hWnd,
        L"Connect to session",
        L"Password:",
        NULL,
        0,
        NULL,
        PH_CHOICE_DIALOG_PASSWORD,
        &selectedChoice,
        NULL,
        NULL
        ))
    {
        if (oldSelectedChoice)
        {
            RtlSecureZeroMemory(oldSelectedChoice->Buffer, oldSelectedChoice->Length);
            PhDereferenceObject(oldSelectedChoice);
        }

        oldSelectedChoice = selectedChoice;

        if (WinStationConnectW(NULL, SessionId, LOGONID_CURRENT, selectedChoice->Buffer, TRUE))
        {
            success = TRUE;
            break;
        }
        else
        {
            if (!PhShowContinueStatus(hWnd, L"Unable to connect to the session", 0, GetLastError()))
                break;
        }
    }

    if (oldSelectedChoice)
    {
        RtlSecureZeroMemory(oldSelectedChoice->Buffer, oldSelectedChoice->Length);
        PhDereferenceObject(oldSelectedChoice);
    }

    return success;
}

BOOLEAN PhUiDisconnectSession(
    _In_ HWND hWnd,
    _In_ ULONG SessionId
    )
{
    if (WinStationDisconnect(NULL, SessionId, FALSE))
        return TRUE;
    else
        PhShowStatus(hWnd, L"Unable to disconnect the session", 0, GetLastError());

    return FALSE;
}

BOOLEAN PhUiLogoffSession(
    _In_ HWND hWnd,
    _In_ ULONG SessionId
    )
{
    if (!PhGetIntegerSetting(L"EnableWarnings") || PhShowConfirmMessage(
        hWnd,
        L"logoff",
        L"the user",
        NULL,
        FALSE
        ))
    {
        if (WinStationReset(NULL, SessionId, FALSE))
            return TRUE;
        else
            PhShowStatus(hWnd, L"Unable to logoff the session", 0, GetLastError());
    }

    return FALSE;
}

/**
 * Determines if a process is a system process.
 *
 * \param ProcessId The PID of the process to check.
 */
static BOOLEAN PhpIsDangerousProcess(
    _In_ HANDLE ProcessId
    )
{
    NTSTATUS status;
    PPH_STRING fileName;
    PPH_STRING systemDirectory;
    ULONG i;

    if (ProcessId == SYSTEM_PROCESS_ID)
        return TRUE;

    if (!NT_SUCCESS(status = PhGetProcessImageFileNameByProcessId(ProcessId, &fileName)))
        return FALSE;

    PhMoveReference(&fileName, PhGetFileName(fileName));
    PH_AUTO(fileName);

    systemDirectory = PH_AUTO(PhGetSystemDirectory());

    for (i = 0; i < sizeof(DangerousProcesses) / sizeof(PWSTR); i++)
    {
        PPH_STRING fullName;

        fullName = PhaConcatStrings(3, systemDirectory->Buffer, L"\\", DangerousProcesses[i]);

        if (PhEqualString(fileName, fullName, TRUE))
            return TRUE;
    }

    return FALSE;
}

/**
 * Checks if the user wants to proceed with an operation.
 *
 * \param hWnd A handle to the parent window.
 * \param Verb A verb describing the action.
 * \param Message A message containing additional information
 * about the action.
 * \param WarnOnlyIfDangerous TRUE to skip the confirmation
 * dialog if none of the processes are system processes,
 * FALSE to always show the confirmation dialog.
 * \param Processes An array of pointers to process items.
 * \param NumberOfProcesses The number of process items.
 *
 * \return TRUE if the user wants to proceed with the operation,
 * otherwise FALSE.
 */
static BOOLEAN PhpShowContinueMessageProcesses(
    _In_ HWND hWnd,
    _In_ PWSTR Verb,
    _In_opt_ PWSTR Message,
    _In_ BOOLEAN WarnOnlyIfDangerous,
    _In_ PPH_PROCESS_ITEM *Processes,
    _In_ ULONG NumberOfProcesses
    )
{
    PWSTR object;
    ULONG i;
    BOOLEAN critical = FALSE;
    BOOLEAN dangerous = FALSE;
    BOOLEAN cont = FALSE;

    if (NumberOfProcesses == 0)
        return FALSE;

    for (i = 0; i < NumberOfProcesses; i++)
    {
        HANDLE processHandle;
        BOOLEAN breakOnTermination = FALSE;

        if (PhpIsDangerousProcess(Processes[i]->ProcessId))
        {
            critical = TRUE;
            dangerous = TRUE;
            break;
        }

        if (NT_SUCCESS(PhOpenProcess(&processHandle, PROCESS_QUERY_INFORMATION, Processes[i]->ProcessId)))
        {
            PhGetProcessBreakOnTermination(processHandle, &breakOnTermination);
            NtClose(processHandle);
        }

        if (breakOnTermination)
        {
            critical = TRUE;
            dangerous = TRUE;
            break;
        }
    }

    if (WarnOnlyIfDangerous && !dangerous)
        return TRUE;

    if (PhGetIntegerSetting(L"EnableWarnings"))
    {
        if (NumberOfProcesses == 1)
        {
            object = Processes[0]->ProcessName->Buffer;
        }
        else if (NumberOfProcesses == 2)
        {
            object = PhaConcatStrings(
                3,
                Processes[0]->ProcessName->Buffer,
                L" and ",
                Processes[1]->ProcessName->Buffer
                )->Buffer;
        }
        else
        {
            object = L"the selected processes";
        }

        if (!dangerous)
        {
            cont = PhShowConfirmMessage(
                hWnd,
                Verb,
                object,
                Message,
                FALSE
                );
        }
        else if (!critical)
        {
            cont = PhShowConfirmMessage(
                hWnd,
                Verb,
                object,
                PhaConcatStrings(
                3,
                L"You are about to ",
                Verb,
                L" one or more system processes."
                )->Buffer,
                TRUE
                );
        }
        else
        {
            PPH_STRING message;

            if (PhEqualStringZ(Verb, L"terminate", FALSE))
            {
                message = PhaConcatStrings(
                    3,
                    L"You are about to ",
                    Verb,
                    L" one or more critical processes. This will shut down the operating system immediately."
                    );
            }
            else
            {
                message = PhaConcatStrings(
                    3,
                    L"You are about to ",
                    Verb,
                    L" one or more critical processes."
                    );
            }

            cont = PhShowConfirmMessage(
                hWnd,
                Verb,
                object,
                message->Buffer,
                TRUE
                );
        }
    }
    else
    {
        cont = TRUE;
    }

    return cont;
}

/**
 * Shows an error message to the user and checks
 * if the user wants to continue.
 *
 * \param hWnd A handle to the parent window.
 * \param Verb A verb describing the action which
 * resulted in an error.
 * \param Process The process item which the action
 * was performed on.
 * \param Status A NT status value representing the
 * error.
 * \param Win32Result A Win32 error code representing
 * the error.
 *
 * \return TRUE if the user wants to continue, otherwise
 * FALSE. The result is typically only useful when
 * executing an action on multiple processes.
 */
static BOOLEAN PhpShowErrorProcess(
    _In_ HWND hWnd,
    _In_ PWSTR Verb,
    _In_ PPH_PROCESS_ITEM Process,
    _In_ NTSTATUS Status,
    _In_opt_ ULONG Win32Result
    )
{
    if (!PH_IS_FAKE_PROCESS_ID(Process->ProcessId))
    {
        return PhShowContinueStatus(
            hWnd,
            PhaFormatString(
            L"Unable to %s %s (PID %lu)",
            Verb,
            Process->ProcessName->Buffer,
            HandleToUlong(Process->ProcessId)
            )->Buffer,
            Status,
            Win32Result
            );
    }
    else
    {
        return PhShowContinueStatus(
            hWnd,
            PhaFormatString(
            L"Unable to %s %s",
            Verb,
            Process->ProcessName->Buffer
            )->Buffer,
            Status,
            Win32Result
            );
    }
}

BOOLEAN PhUiTerminateProcesses(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM *Processes,
    _In_ ULONG NumberOfProcesses
    )
{
    BOOLEAN success = TRUE;
    BOOLEAN cancelled = FALSE;
    ULONG i;

    if (!PhpShowContinueMessageProcesses(
        hWnd,
        L"terminate",
        L"Terminating a process will cause unsaved data to be lost.",
        FALSE,
        Processes,
        NumberOfProcesses
        ))
        return FALSE;

    for (i = 0; i < NumberOfProcesses; i++)
    {
        NTSTATUS status;
        HANDLE processHandle;

        if (NT_SUCCESS(status = PhOpenProcess(
            &processHandle,
            PROCESS_TERMINATE,
            Processes[i]->ProcessId
            )))
        {
            // An exit status of 1 is used here for compatibility reasons:
            // 1. Both Task Manager and Process Explorer use 1.
            // 2. winlogon tries to restart explorer.exe if the exit status is not 1.

            status = PhTerminateProcess(processHandle, 1);
            NtClose(processHandle);
        }

        if (!NT_SUCCESS(status))
        {
            BOOLEAN connected;

            success = FALSE;

            if (!cancelled && PhpShowErrorAndConnectToPhSvc(
                hWnd,
                PhaConcatStrings2(L"Unable to terminate ", Processes[i]->ProcessName->Buffer)->Buffer,
                status,
                &connected
                ))
            {
                if (connected)
                {
                    if (NT_SUCCESS(status = PhSvcCallControlProcess(Processes[i]->ProcessId, PhSvcControlProcessTerminate, 0)))
                        success = TRUE;
                    else
                        PhpShowErrorProcess(hWnd, L"terminate", Processes[i], status, 0);

                    PhUiDisconnectFromPhSvc();
                }
                else
                {
                    cancelled = TRUE;
                }
            }
            else
            {
                if (!PhpShowErrorProcess(hWnd, L"terminate", Processes[i], status, 0))
                    break;
            }
        }
    }

    return success;
}

BOOLEAN PhpUiTerminateTreeProcess(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM Process,
    _In_ PVOID Processes,
    _Inout_ PBOOLEAN Success
    )
{
    NTSTATUS status;
    PSYSTEM_PROCESS_INFORMATION process;
    HANDLE processHandle;
    PPH_PROCESS_ITEM processItem;

    // Note:
    // FALSE should be written to Success if any part of the operation failed.
    // The return value of this function indicates whether to continue with
    // the operation (FALSE if user cancelled).

    // Terminate the process.

    if (NT_SUCCESS(status = PhOpenProcess(
        &processHandle,
        PROCESS_TERMINATE,
        Process->ProcessId
        )))
    {
        status = PhTerminateProcess(processHandle, 1);
        NtClose(processHandle);
    }

    if (!NT_SUCCESS(status))
    {
        *Success = FALSE;

        if (!PhpShowErrorProcess(hWnd, L"terminate", Process, status, 0))
            return FALSE;
    }

    // Terminate the process' children.

    process = PH_FIRST_PROCESS(Processes);

    do
    {
        if (process->UniqueProcessId != Process->ProcessId &&
            process->InheritedFromUniqueProcessId == Process->ProcessId)
        {
            if (processItem = PhReferenceProcessItem(process->UniqueProcessId))
            {
                if (WindowsVersion >= WINDOWS_10_RS3)
                {
                    // Check the sequence number to make sure it is a descendant.
                    if (processItem->ProcessSequenceNumber >= Process->ProcessSequenceNumber)
                    {
                        if (!PhpUiTerminateTreeProcess(hWnd, processItem, Processes, Success))
                        {
                            PhDereferenceObject(processItem);
                            return FALSE;
                        }
                    }
                }
                else
                {
                    // Check the creation time to make sure it is a descendant.
                    if (processItem->CreateTime.QuadPart >= Process->CreateTime.QuadPart)
                    {
                        if (!PhpUiTerminateTreeProcess(hWnd, processItem, Processes, Success))
                        {
                            PhDereferenceObject(processItem);
                            return FALSE;
                        }
                    }
                }

                PhDereferenceObject(processItem);
            }
        }
    } while (process = PH_NEXT_PROCESS(process));

    return TRUE;
}

BOOLEAN PhUiTerminateTreeProcess(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM Process
    )
{
    NTSTATUS status;
    BOOLEAN success = TRUE;
    BOOLEAN cont = FALSE;
    PVOID processes;

    if (PhGetIntegerSetting(L"EnableWarnings"))
    {
        cont = PhShowConfirmMessage(
            hWnd,
            L"terminate",
            PhaConcatStrings2(Process->ProcessName->Buffer, L" and its descendants")->Buffer,
            L"Terminating a process tree will cause the process and its descendants to be terminated.",
            FALSE
            );
    }
    else
    {
        cont = TRUE;
    }

    if (!cont)
        return FALSE;

    if (!NT_SUCCESS(status = PhEnumProcesses(&processes)))
    {
        PhShowStatus(hWnd, L"Unable to enumerate processes", status, 0);
        return FALSE;
    }

    PhpUiTerminateTreeProcess(hWnd, Process, processes, &success);
    PhFree(processes);

    return success;
}

BOOLEAN PhUiSuspendProcesses(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM *Processes,
    _In_ ULONG NumberOfProcesses
    )
{
    BOOLEAN success = TRUE;
    BOOLEAN cancelled = FALSE;
    ULONG i;

    if (!PhpShowContinueMessageProcesses(
        hWnd,
        L"suspend",
        NULL,
        TRUE,
        Processes,
        NumberOfProcesses
        ))
        return FALSE;

    for (i = 0; i < NumberOfProcesses; i++)
    {
        NTSTATUS status;
        HANDLE processHandle;

        if (NT_SUCCESS(status = PhOpenProcess(
            &processHandle,
            PROCESS_SUSPEND_RESUME,
            Processes[i]->ProcessId
            )))
        {
            status = NtSuspendProcess(processHandle);
            NtClose(processHandle);
        }

        if (!NT_SUCCESS(status))
        {
            BOOLEAN connected;

            success = FALSE;

            if (!cancelled && PhpShowErrorAndConnectToPhSvc(
                hWnd,
                PhaConcatStrings2(L"Unable to suspend ", Processes[i]->ProcessName->Buffer)->Buffer,
                status,
                &connected
                ))
            {
                if (connected)
                {
                    if (NT_SUCCESS(status = PhSvcCallControlProcess(Processes[i]->ProcessId, PhSvcControlProcessSuspend, 0)))
                        success = TRUE;
                    else
                        PhpShowErrorProcess(hWnd, L"suspend", Processes[i], status, 0);

                    PhUiDisconnectFromPhSvc();
                }
                else
                {
                    cancelled = TRUE;
                }
            }
            else
            {
                if (!PhpShowErrorProcess(hWnd, L"suspend", Processes[i], status, 0))
                    break;
            }
        }
    }

    return success;
}

BOOLEAN PhUiResumeProcesses(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM *Processes,
    _In_ ULONG NumberOfProcesses
    )
{
    BOOLEAN success = TRUE;
    BOOLEAN cancelled = FALSE;
    ULONG i;

    if (!PhpShowContinueMessageProcesses(
        hWnd,
        L"resume",
        NULL,
        TRUE,
        Processes,
        NumberOfProcesses
        ))
        return FALSE;

    for (i = 0; i < NumberOfProcesses; i++)
    {
        NTSTATUS status;
        HANDLE processHandle;

        if (NT_SUCCESS(status = PhOpenProcess(
            &processHandle,
            PROCESS_SUSPEND_RESUME,
            Processes[i]->ProcessId
            )))
        {
            status = NtResumeProcess(processHandle);
            NtClose(processHandle);
        }

        if (!NT_SUCCESS(status))
        {
            BOOLEAN connected;

            success = FALSE;

            if (!cancelled && PhpShowErrorAndConnectToPhSvc(
                hWnd,
                PhaConcatStrings2(L"Unable to resume ", Processes[i]->ProcessName->Buffer)->Buffer,
                status,
                &connected
                ))
            {
                if (connected)
                {
                    if (NT_SUCCESS(status = PhSvcCallControlProcess(Processes[i]->ProcessId, PhSvcControlProcessResume, 0)))
                        success = TRUE;
                    else
                        PhpShowErrorProcess(hWnd, L"resume", Processes[i], status, 0);

                    PhUiDisconnectFromPhSvc();
                }
                else
                {
                    cancelled = TRUE;
                }
            }
            else
            {
                if (!PhpShowErrorProcess(hWnd, L"resume", Processes[i], status, 0))
                    break;
            }
        }
    }

    return success;
}

BOOLEAN PhUiRestartProcess(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM Process
    )
{
    NTSTATUS status;
    BOOLEAN cont = FALSE;
    HANDLE processHandle = NULL;
    PPH_STRING commandLine;
    PPH_STRING currentDirectory;

    if (PhGetIntegerSetting(L"EnableWarnings"))
    {
        cont = PhShowConfirmMessage(
            hWnd,
            L"restart",
            Process->ProcessName->Buffer,
            L"The process will be restarted with the same command line and "
            L"working directory, but if it is running under a different user it "
            L"will be restarted under the current user.",
            FALSE
            );
    }
    else
    {
        cont = TRUE;
    }

    if (!cont)
        return FALSE;

    // Open the process and get the command line and current directory.

    if (!NT_SUCCESS(status = PhOpenProcess(
        &processHandle,
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
        Process->ProcessId
        )))
        goto ErrorExit;

    if (!NT_SUCCESS(status = PhGetProcessCommandLine(
        processHandle,
        &commandLine
        )))
        goto ErrorExit;

    PH_AUTO(commandLine);

    if (!NT_SUCCESS(status = PhGetProcessPebString(
        processHandle,
        PhpoCurrentDirectory,
        &currentDirectory
        )))
        goto ErrorExit;

    PH_AUTO(currentDirectory);

    NtClose(processHandle);
    processHandle = NULL;

    // Open the process and terminate it.

    if (!NT_SUCCESS(status = PhOpenProcess(
        &processHandle,
        PROCESS_TERMINATE,
        Process->ProcessId
        )))
        goto ErrorExit;

    if (!NT_SUCCESS(status = PhTerminateProcess(
        processHandle,
        1
        )))
        goto ErrorExit;

    NtClose(processHandle);
    processHandle = NULL;

    // Start the process.

    status = PhCreateProcessWin32(
        PhGetString(Process->FileNameWin32), // we didn't wait for S1 processing
        commandLine->Buffer,
        NULL,
        currentDirectory->Buffer,
        0,
        NULL,
        NULL,
        NULL
        );

ErrorExit:
    if (processHandle)
        NtClose(processHandle);

    if (!NT_SUCCESS(status))
    {
        PhpShowErrorProcess(hWnd, L"restart", Process, status, 0);
        return FALSE;
    }

    return TRUE;
}

// Contributed by evilpie (#2981421)
BOOLEAN PhUiDebugProcess(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM Process
    )
{
    static PH_STRINGREF aeDebugKeyName = PH_STRINGREF_INIT(L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AeDebug");
#ifdef _WIN64
    static PH_STRINGREF aeDebugWow64KeyName = PH_STRINGREF_INIT(L"Software\\Wow6432Node\\Microsoft\\Windows NT\\CurrentVersion\\AeDebug");
#endif
    NTSTATUS status;
    BOOLEAN cont = FALSE;
    PH_STRING_BUILDER commandLineBuilder;
    HANDLE keyHandle;
    PPH_STRING debugger;
    PH_STRINGREF commandPart;
    PH_STRINGREF dummy;

    if (PhGetIntegerSetting(L"EnableWarnings"))
    {
        cont = PhShowConfirmMessage(
            hWnd,
            L"debug",
            Process->ProcessName->Buffer,
            L"Debugging a process may result in loss of data.",
            FALSE
            );
    }
    else
    {
        cont = TRUE;
    }

    if (!cont)
        return FALSE;

    status = PhOpenKey(
        &keyHandle,
        KEY_READ,
        PH_KEY_LOCAL_MACHINE,
#ifdef _WIN64
        Process->IsWow64 ? &aeDebugWow64KeyName : &aeDebugKeyName,
#else
        &aeDebugKeyName,
#endif
        0
        );

    if (NT_SUCCESS(status))
    {
        if (debugger = PH_AUTO(PhQueryRegistryString(keyHandle, L"Debugger")))
        {
            if (PhSplitStringRefAtChar(&debugger->sr, '"', &dummy, &commandPart) &&
                PhSplitStringRefAtChar(&commandPart, '"', &commandPart, &dummy))
            {
                DebuggerCommand = PhCreateString2(&commandPart);
            }
        }

        NtClose(keyHandle);
    }

    if (PhIsNullOrEmptyString(DebuggerCommand))
    {
        PhShowError(hWnd, L"Unable to locate the debugger.");
        return FALSE;
    }

    PhInitializeStringBuilder(&commandLineBuilder, DebuggerCommand->Length + 30);

    PhAppendCharStringBuilder(&commandLineBuilder, '"');
    PhAppendStringBuilder(&commandLineBuilder, &DebuggerCommand->sr);
    PhAppendCharStringBuilder(&commandLineBuilder, '"');
    PhAppendFormatStringBuilder(&commandLineBuilder, L" -p %lu", HandleToUlong(Process->ProcessId));

    status = PhCreateProcessWin32(
        NULL,
        commandLineBuilder.String->Buffer,
        NULL,
        NULL,
        0,
        NULL,
        NULL,
        NULL
        );

    PhDeleteStringBuilder(&commandLineBuilder);

    if (!NT_SUCCESS(status))
    {
        PhpShowErrorProcess(hWnd, L"debug", Process, status, 0);
        return FALSE;
    }

    return TRUE;
}

BOOLEAN PhUiReduceWorkingSetProcesses(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM *Processes,
    _In_ ULONG NumberOfProcesses
    )
{
    BOOLEAN success = TRUE;
    ULONG i;

    for (i = 0; i < NumberOfProcesses; i++)
    {
        NTSTATUS status;
        HANDLE processHandle;

        if (NT_SUCCESS(status = PhOpenProcess(
            &processHandle,
            PROCESS_SET_QUOTA,
            Processes[i]->ProcessId
            )))
        {
            QUOTA_LIMITS quotaLimits;

            memset(&quotaLimits, 0, sizeof(QUOTA_LIMITS));
            quotaLimits.MinimumWorkingSetSize = -1;
            quotaLimits.MaximumWorkingSetSize = -1;

            status = PhSetProcessQuotaLimits(processHandle, quotaLimits);

            NtClose(processHandle);
        }

        if (!NT_SUCCESS(status))
        {
            success = FALSE;

            if (!PhpShowErrorProcess(hWnd, L"reduce the working set of", Processes[i], status, 0))
                break;
        }
    }

    return success;
}

BOOLEAN PhUiSetVirtualizationProcess(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM Process,
    _In_ BOOLEAN Enable
    )
{
    NTSTATUS status;
    BOOLEAN cont = FALSE;
    HANDLE processHandle;
    HANDLE tokenHandle;

    if (PhGetIntegerSetting(L"EnableWarnings"))
    {
        cont = PhShowConfirmMessage(
            hWnd,
            L"set",
            L"virtualization for the process",
            L"Enabling or disabling virtualization for a process may "
            L"alter its functionality and produce undesirable effects.",
            FALSE
            );
    }
    else
    {
        cont = TRUE;
    }

    if (!cont)
        return FALSE;

    if (NT_SUCCESS(status = PhOpenProcess(
        &processHandle,
        PROCESS_QUERY_LIMITED_INFORMATION,
        Process->ProcessId
        )))
    {
        if (NT_SUCCESS(status = PhOpenProcessToken(
            processHandle,
            TOKEN_WRITE,
            &tokenHandle
            )))
        {
            status = PhSetTokenIsVirtualizationEnabled(tokenHandle, Enable);
            NtClose(tokenHandle);
        }

        NtClose(processHandle);
    }

    if (!NT_SUCCESS(status))
    {
        PhpShowErrorProcess(hWnd, L"set virtualization for", Process, status, 0);
        return FALSE;
    }

    return TRUE;
}

BOOLEAN PhUiSetCriticalProcess(
    _In_ HWND WindowHandle,
    _In_ PPH_PROCESS_ITEM Process
    )
{
    NTSTATUS status;
    HANDLE processHandle;
    BOOLEAN breakOnTermination;

    status = PhOpenProcess(
        &processHandle,
        PROCESS_QUERY_INFORMATION | PROCESS_SET_INFORMATION,
        Process->ProcessId
        );

    if (NT_SUCCESS(status))
    {
        status = PhGetProcessBreakOnTermination(
            processHandle,
            &breakOnTermination
            );

        if (NT_SUCCESS(status))
        {
            if (!breakOnTermination && (!PhGetIntegerSetting(L"EnableWarnings") || PhShowConfirmMessage(
                WindowHandle,
                L"enable",
                L"critical status on the process",
                L"If the process ends, the operating system will shut down immediately.",
                TRUE
                )))
            {
                status = PhSetProcessBreakOnTermination(processHandle, TRUE);
            }
            else if (breakOnTermination && (!PhGetIntegerSetting(L"EnableWarnings") || PhShowConfirmMessage(
                WindowHandle,
                L"disable",
                L"critical status on the process",
                NULL,
                FALSE
                )))
            {
                status = PhSetProcessBreakOnTermination(processHandle, FALSE);
            }
        }

        NtClose(processHandle);
    }

    if (!NT_SUCCESS(status))
    {
        PhpShowErrorProcess(WindowHandle, L"set critical status", Process, status, 0);
        return FALSE;
    }

    return TRUE;
}

BOOLEAN PhUiDetachFromDebuggerProcess(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM Process
    )
{
    NTSTATUS status;
    HANDLE processHandle;
    HANDLE debugObjectHandle;

    if (NT_SUCCESS(status = PhOpenProcess(
        &processHandle,
        PROCESS_QUERY_INFORMATION | PROCESS_SUSPEND_RESUME,
        Process->ProcessId
        )))
    {
        if (NT_SUCCESS(status = PhGetProcessDebugObject(
            processHandle,
            &debugObjectHandle
            )))
        {
            // Disable kill-on-close.
            if (NT_SUCCESS(status = PhSetDebugKillProcessOnExit(
                debugObjectHandle,
                FALSE
                )))
            {
                status = NtRemoveProcessDebug(processHandle, debugObjectHandle);
            }

            NtClose(debugObjectHandle);
        }

        NtClose(processHandle);
    }

    if (status == STATUS_PORT_NOT_SET)
    {
        PhShowInformation2(hWnd, L"The process is not being debugged.", L"");
        return FALSE;
    }

    if (!NT_SUCCESS(status))
    {
        PhpShowErrorProcess(hWnd, L"detach debugger from", Process, status, 0);
        return FALSE;
    }

    return TRUE;
}

BOOLEAN PhUiLoadDllProcess(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM Process
    )
{
    static PH_FILETYPE_FILTER filters[] =
    {
        { L"DLL files (*.dll)", L"*.dll" },
        { L"All files (*.*)", L"*.*" }
    };

    NTSTATUS status;
    PVOID fileDialog;
    PPH_STRING fileName;
    HANDLE processHandle;

    fileDialog = PhCreateOpenFileDialog();
    PhSetFileDialogFilter(fileDialog, filters, RTL_NUMBER_OF(filters));

    if (!PhShowFileDialog(hWnd, fileDialog))
    {
        PhFreeFileDialog(fileDialog);
        return FALSE;
    }

    fileName = PH_AUTO(PhGetFileDialogFileName(fileDialog));
    PhFreeFileDialog(fileDialog);

    if (NT_SUCCESS(status = PhOpenProcess(
        &processHandle,
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
        PROCESS_VM_READ | PROCESS_VM_WRITE,
        Process->ProcessId
        )))
    {
        LARGE_INTEGER timeout;

        timeout.QuadPart = -(LONGLONG)UInt32x32To64(5, PH_TIMEOUT_SEC);
        status = PhLoadDllProcess(
            processHandle,
            fileName->Buffer,
            &timeout
            );

        NtClose(processHandle);
    }

    if (!NT_SUCCESS(status))
    {
        PhpShowErrorProcess(hWnd, L"load the DLL into", Process, status, 0);
        return FALSE;
    }

    return TRUE;
}

BOOLEAN PhUiSetIoPriorityProcesses(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM *Processes,
    _In_ ULONG NumberOfProcesses,
    _In_ IO_PRIORITY_HINT IoPriority
    )
{
    BOOLEAN success = TRUE;
    BOOLEAN cancelled = FALSE;
    ULONG i;

    for (i = 0; i < NumberOfProcesses; i++)
    {
        NTSTATUS status;
        HANDLE processHandle;

        if (NT_SUCCESS(status = PhOpenProcess(
            &processHandle,
            PROCESS_SET_INFORMATION,
            Processes[i]->ProcessId
            )))
        {
            if (Processes[i]->ProcessId != SYSTEM_PROCESS_ID)
            {
                status = PhSetProcessIoPriority(processHandle, IoPriority);
            }
            else
            {
                // See comment in PhUiSetPriorityProcesses.
                status = STATUS_UNSUCCESSFUL;
            }

            NtClose(processHandle);
        }

        if (!NT_SUCCESS(status))
        {
            BOOLEAN connected;

            success = FALSE;

            // The operation may have failed due to the lack of SeIncreaseBasePriorityPrivilege.
            if (!cancelled && PhpShowErrorAndConnectToPhSvc(
                hWnd,
                PhaConcatStrings2(L"Unable to set the I/O priority of ", Processes[i]->ProcessName->Buffer)->Buffer,
                status,
                &connected
                ))
            {
                if (connected)
                {
                    if (NT_SUCCESS(status = PhSvcCallControlProcess(Processes[i]->ProcessId, PhSvcControlProcessIoPriority, IoPriority)))
                        success = TRUE;
                    else
                        PhpShowErrorProcess(hWnd, L"set the I/O priority of", Processes[i], status, 0);

                    PhUiDisconnectFromPhSvc();
                }
                else
                {
                    cancelled = TRUE;
                }
            }
            else
            {
                if (!PhpShowErrorProcess(hWnd, L"set the I/O priority of", Processes[i], status, 0))
                    break;
            }
        }
    }

    return success;
}

BOOLEAN PhUiSetPagePriorityProcess(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM Process,
    _In_ ULONG PagePriority
    )
{
    NTSTATUS status;
    HANDLE processHandle;

    if (NT_SUCCESS(status = PhOpenProcess(
        &processHandle,
        PROCESS_SET_INFORMATION,
        Process->ProcessId
        )))
    {
        if (Process->ProcessId != SYSTEM_PROCESS_ID)
        {
            status = PhSetProcessPagePriority(processHandle, PagePriority);
        }
        else
        {
            // See comment in PhUiSetPriorityProcesses.
            status = STATUS_UNSUCCESSFUL;
        }

        NtClose(processHandle);
    }

    if (!NT_SUCCESS(status))
    {
        PhpShowErrorProcess(hWnd, L"set the page priority of", Process, status, 0);
        return FALSE;
    }

    return TRUE;
}

BOOLEAN PhUiSetPriorityProcesses(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM *Processes,
    _In_ ULONG NumberOfProcesses,
    _In_ ULONG PriorityClass
    )
{
    BOOLEAN success = TRUE;
    BOOLEAN cancelled = FALSE;
    ULONG i;

    for (i = 0; i < NumberOfProcesses; i++)
    {
        NTSTATUS status;
        HANDLE processHandle;

        if (NT_SUCCESS(status = PhOpenProcess(
            &processHandle,
            PROCESS_SET_INFORMATION,
            Processes[i]->ProcessId
            )))
        {
            if (Processes[i]->ProcessId != SYSTEM_PROCESS_ID)
            {
                PROCESS_PRIORITY_CLASS priorityClass;

                priorityClass.Foreground = FALSE;
                priorityClass.PriorityClass = (UCHAR)PriorityClass;

                status = PhSetProcessPriority(processHandle, priorityClass);
            }
            else
            {
                // Changing the priority of System can lead to a BSOD on some versions of Windows,
                // so disallow this.
                status = STATUS_UNSUCCESSFUL;
            }

            NtClose(processHandle);
        }

        if (!NT_SUCCESS(status))
        {
            BOOLEAN connected;

            success = FALSE;

            // The operation may have failed due to the lack of SeIncreaseBasePriorityPrivilege.
            if (!cancelled && PhpShowErrorAndConnectToPhSvc(
                hWnd,
                PhaConcatStrings2(L"Unable to set the priority of ", Processes[i]->ProcessName->Buffer)->Buffer,
                status,
                &connected
                ))
            {
                if (connected)
                {
                    if (NT_SUCCESS(status = PhSvcCallControlProcess(Processes[i]->ProcessId, PhSvcControlProcessPriority, PriorityClass)))
                        success = TRUE;
                    else
                        PhpShowErrorProcess(hWnd, L"set the priority of", Processes[i], status, 0);

                    PhUiDisconnectFromPhSvc();
                }
                else
                {
                    cancelled = TRUE;
                }
            }
            else
            {
                if (!PhpShowErrorProcess(hWnd, L"set the priority of", Processes[i], status, 0))
                    break;
            }
        }
    }

    return success;
}

static VOID PhpShowErrorService(
    _In_ HWND hWnd,
    _In_ PWSTR Verb,
    _In_ PPH_SERVICE_ITEM Service,
    _In_ NTSTATUS Status,
    _In_opt_ ULONG Win32Result
    )
{
    PhShowStatus(
        hWnd,
        PhaFormatString(
        L"Unable to %s %s.",
        Verb,
        Service->Name->Buffer
        )->Buffer,
        Status,
        Win32Result
        );
}

BOOLEAN PhUiStartService(
    _In_ HWND hWnd,
    _In_ PPH_SERVICE_ITEM Service
    )
{
    SC_HANDLE serviceHandle;
    BOOLEAN success = FALSE;

    serviceHandle = PhOpenService(Service->Name->Buffer, SERVICE_START);

    if (serviceHandle)
    {
        if (StartService(serviceHandle, 0, NULL))
            success = TRUE;

        CloseServiceHandle(serviceHandle);
    }

    if (!success)
    {
        NTSTATUS status;
        BOOLEAN connected;

        status = PhGetLastWin32ErrorAsNtStatus();

        if (PhpShowErrorAndConnectToPhSvc(
            hWnd,
            PhaConcatStrings2(L"Unable to start ", Service->Name->Buffer)->Buffer,
            status,
            &connected
            ))
        {
            if (connected)
            {
                if (NT_SUCCESS(status = PhSvcCallControlService(Service->Name->Buffer, PhSvcControlServiceStart)))
                    success = TRUE;
                else
                    PhpShowErrorService(hWnd, L"start", Service, status, 0);

                PhUiDisconnectFromPhSvc();
            }
        }
        else
        {
            PhpShowErrorService(hWnd, L"start", Service, status, 0);
        }
    }

    return success;
}

BOOLEAN PhUiContinueService(
    _In_ HWND hWnd,
    _In_ PPH_SERVICE_ITEM Service
    )
{
    SC_HANDLE serviceHandle;
    BOOLEAN success = FALSE;

    serviceHandle = PhOpenService(Service->Name->Buffer, SERVICE_PAUSE_CONTINUE);

    if (serviceHandle)
    {
        SERVICE_STATUS serviceStatus;

        if (ControlService(serviceHandle, SERVICE_CONTROL_CONTINUE, &serviceStatus))
            success = TRUE;

        CloseServiceHandle(serviceHandle);
    }

    if (!success)
    {
        NTSTATUS status;
        BOOLEAN connected;

        status = PhGetLastWin32ErrorAsNtStatus();

        if (PhpShowErrorAndConnectToPhSvc(
            hWnd,
            PhaConcatStrings2(L"Unable to continue ", Service->Name->Buffer)->Buffer,
            status,
            &connected
            ))
        {
            if (connected)
            {
                if (NT_SUCCESS(status = PhSvcCallControlService(Service->Name->Buffer, PhSvcControlServiceContinue)))
                    success = TRUE;
                else
                    PhpShowErrorService(hWnd, L"continue", Service, status, 0);

                PhUiDisconnectFromPhSvc();
            }
        }
        else
        {
            PhpShowErrorService(hWnd, L"continue", Service, status, 0);
        }
    }

    return success;
}

BOOLEAN PhUiPauseService(
    _In_ HWND hWnd,
    _In_ PPH_SERVICE_ITEM Service
    )
{
    SC_HANDLE serviceHandle;
    BOOLEAN success = FALSE;

    serviceHandle = PhOpenService(Service->Name->Buffer, SERVICE_PAUSE_CONTINUE);

    if (serviceHandle)
    {
        SERVICE_STATUS serviceStatus;

        if (ControlService(serviceHandle, SERVICE_CONTROL_PAUSE, &serviceStatus))
            success = TRUE;

        CloseServiceHandle(serviceHandle);
    }

    if (!success)
    {
        NTSTATUS status;
        BOOLEAN connected;

        status = PhGetLastWin32ErrorAsNtStatus();

        if (PhpShowErrorAndConnectToPhSvc(
            hWnd,
            PhaConcatStrings2(L"Unable to pause ", Service->Name->Buffer)->Buffer,
            status,
            &connected
            ))
        {
            if (connected)
            {
                if (NT_SUCCESS(status = PhSvcCallControlService(Service->Name->Buffer, PhSvcControlServicePause)))
                    success = TRUE;
                else
                    PhpShowErrorService(hWnd, L"pause", Service, status, 0);

                PhUiDisconnectFromPhSvc();
            }
        }
        else
        {
            PhpShowErrorService(hWnd, L"pause", Service, status, 0);
        }
    }

    return success;
}

BOOLEAN PhUiStopService(
    _In_ HWND hWnd,
    _In_ PPH_SERVICE_ITEM Service
    )
{
    SC_HANDLE serviceHandle;
    BOOLEAN success = FALSE;

    serviceHandle = PhOpenService(Service->Name->Buffer, SERVICE_STOP);

    if (serviceHandle)
    {
        SERVICE_STATUS serviceStatus;

        if (ControlService(serviceHandle, SERVICE_CONTROL_STOP, &serviceStatus))
            success = TRUE;

        CloseServiceHandle(serviceHandle);
    }

    if (!success)
    {
        NTSTATUS status;
        BOOLEAN connected;

        status = PhGetLastWin32ErrorAsNtStatus();

        if (PhpShowErrorAndConnectToPhSvc(
            hWnd,
            PhaConcatStrings2(L"Unable to stop ", Service->Name->Buffer)->Buffer,
            status,
            &connected
            ))
        {
            if (connected)
            {
                if (NT_SUCCESS(status = PhSvcCallControlService(Service->Name->Buffer, PhSvcControlServiceStop)))
                    success = TRUE;
                else
                    PhpShowErrorService(hWnd, L"stop", Service, status, 0);

                PhUiDisconnectFromPhSvc();
            }
        }
        else
        {
            PhpShowErrorService(hWnd, L"stop", Service, status, 0);
        }
    }

    return success;
}

BOOLEAN PhUiDeleteService(
    _In_ HWND hWnd,
    _In_ PPH_SERVICE_ITEM Service
    )
{
    SC_HANDLE serviceHandle;
    BOOLEAN success = FALSE;

    // Warnings cannot be disabled for service deletion.
    if (!PhShowConfirmMessage(
        hWnd,
        L"delete",
        Service->Name->Buffer,
        L"Deleting a service can prevent the system from starting "
        L"or functioning properly.",
        TRUE
        ))
        return FALSE;

    serviceHandle = PhOpenService(Service->Name->Buffer, DELETE);

    if (serviceHandle)
    {
        if (DeleteService(serviceHandle))
            success = TRUE;

        CloseServiceHandle(serviceHandle);
    }

    if (!success)
    {
        NTSTATUS status;
        BOOLEAN connected;

        status = PhGetLastWin32ErrorAsNtStatus();

        if (PhpShowErrorAndConnectToPhSvc(
            hWnd,
            PhaConcatStrings2(L"Unable to delete ", Service->Name->Buffer)->Buffer,
            status,
            &connected
            ))
        {
            if (connected)
            {
                if (NT_SUCCESS(status = PhSvcCallControlService(Service->Name->Buffer, PhSvcControlServiceDelete)))
                    success = TRUE;
                else
                    PhpShowErrorService(hWnd, L"delete", Service, status, 0);

                PhUiDisconnectFromPhSvc();
            }
        }
        else
        {
            PhpShowErrorService(hWnd, L"delete", Service, status, 0);
        }
    }

    return success;
}

BOOLEAN PhUiCloseConnections(
    _In_ HWND hWnd,
    _In_ PPH_NETWORK_ITEM *Connections,
    _In_ ULONG NumberOfConnections
    )
{

    BOOLEAN success = TRUE;
    BOOLEAN cancelled = FALSE;
    ULONG result;
    ULONG i;
    MIB_TCPROW tcpRow;

    for (i = 0; i < NumberOfConnections; i++)
    {
        if (
            Connections[i]->ProtocolType != PH_TCP4_NETWORK_PROTOCOL ||
            Connections[i]->State != MIB_TCP_STATE_ESTAB
            )
            continue;

        tcpRow.dwState = MIB_TCP_STATE_DELETE_TCB;
        tcpRow.dwLocalAddr = Connections[i]->LocalEndpoint.Address.Ipv4;
        tcpRow.dwLocalPort = _byteswap_ushort((USHORT)Connections[i]->LocalEndpoint.Port);
        tcpRow.dwRemoteAddr = Connections[i]->RemoteEndpoint.Address.Ipv4;
        tcpRow.dwRemotePort = _byteswap_ushort((USHORT)Connections[i]->RemoteEndpoint.Port);

        if ((result = SetTcpEntry(&tcpRow)) != NO_ERROR)
        {
            NTSTATUS status;
            BOOLEAN connected;

            success = FALSE;

            // SetTcpEntry returns ERROR_MR_MID_NOT_FOUND for access denied errors for some reason.
            if (result == ERROR_MR_MID_NOT_FOUND)
                result = ERROR_ACCESS_DENIED;

            if (!cancelled && PhpShowErrorAndConnectToPhSvc(
                hWnd,
                L"Unable to close the TCP connection",
                NTSTATUS_FROM_WIN32(result),
                &connected
                ))
            {
                if (connected)
                {
                    if (NT_SUCCESS(status = PhSvcCallSetTcpEntry(&tcpRow)))
                        success = TRUE;
                    else
                        PhShowStatus(hWnd, L"Unable to close the TCP connection", status, 0);

                    PhUiDisconnectFromPhSvc();
                }
                else
                {
                    cancelled = TRUE;
                }
            }
            else
            {
                if (PhShowMessage2(
                    hWnd,
                    TDCBF_OK_BUTTON,
                    TD_ERROR_ICON,
                    L"Unable to close the TCP connection.",
                    L"Make sure Process Hacker is running with administrative privileges."
                    ) != IDOK)
                    break;
            }
        }
    }

    return success;
}

static BOOLEAN PhpShowContinueMessageThreads(
    _In_ HWND hWnd,
    _In_ PWSTR Verb,
    _In_ PWSTR Message,
    _In_ BOOLEAN Warning,
    _In_ PPH_THREAD_ITEM *Threads,
    _In_ ULONG NumberOfThreads
    )
{
    PWSTR object;
    BOOLEAN cont = FALSE;

    if (NumberOfThreads == 0)
        return FALSE;

    if (PhGetIntegerSetting(L"EnableWarnings"))
    {
        if (NumberOfThreads == 1)
        {
            object = L"the selected thread";
        }
        else
        {
            object = L"the selected threads";
        }

        cont = PhShowConfirmMessage(
            hWnd,
            Verb,
            object,
            Message,
            Warning
            );
    }
    else
    {
        cont = TRUE;
    }

    return cont;
}

static BOOLEAN PhpShowErrorThread(
    _In_ HWND hWnd,
    _In_ PWSTR Verb,
    _In_ PPH_THREAD_ITEM Thread,
    _In_ NTSTATUS Status,
    _In_opt_ ULONG Win32Result
    )
{
    return PhShowContinueStatus(
        hWnd,
        PhaFormatString(
        L"Unable to %s thread %lu",
        Verb,
        HandleToUlong(Thread->ThreadId)
        )->Buffer,
        Status,
        Win32Result
        );
}

BOOLEAN PhUiTerminateThreads(
    _In_ HWND hWnd,
    _In_ PPH_THREAD_ITEM *Threads,
    _In_ ULONG NumberOfThreads
    )
{
    BOOLEAN success = TRUE;
    BOOLEAN cancelled = FALSE;
    ULONG i;

    if (!PhpShowContinueMessageThreads(
        hWnd,
        L"terminate",
        L"Terminating a thread may cause the process to stop working.",
        FALSE,
        Threads,
        NumberOfThreads
        ))
        return FALSE;

    for (i = 0; i < NumberOfThreads; i++)
    {
        NTSTATUS status;
        HANDLE threadHandle;

        if (NT_SUCCESS(status = PhOpenThread(
            &threadHandle,
            THREAD_TERMINATE,
            Threads[i]->ThreadId
            )))
        {
            status = NtTerminateThread(threadHandle, STATUS_SUCCESS);
            NtClose(threadHandle);
        }

        if (!NT_SUCCESS(status))
        {
            BOOLEAN connected;

            success = FALSE;

            if (!cancelled && PhpShowErrorAndConnectToPhSvc(
                hWnd,
                PhaFormatString(L"Unable to terminate thread %lu", HandleToUlong(Threads[i]->ThreadId))->Buffer,
                status,
                &connected
                ))
            {
                if (connected)
                {
                    if (NT_SUCCESS(status = PhSvcCallControlThread(Threads[i]->ThreadId, PhSvcControlThreadTerminate, 0)))
                        success = TRUE;
                    else
                        PhpShowErrorThread(hWnd, L"terminate", Threads[i], status, 0);

                    PhUiDisconnectFromPhSvc();
                }
                else
                {
                    cancelled = TRUE;
                }
            }
            else
            {
                if (!PhpShowErrorThread(hWnd, L"terminate", Threads[i], status, 0))
                    break;
            }
        }
    }

    return success;
}

BOOLEAN PhUiSuspendThreads(
    _In_ HWND hWnd,
    _In_ PPH_THREAD_ITEM *Threads,
    _In_ ULONG NumberOfThreads
    )
{
    BOOLEAN success = TRUE;
    BOOLEAN cancelled = FALSE;
    ULONG i;

    for (i = 0; i < NumberOfThreads; i++)
    {
        NTSTATUS status;
        HANDLE threadHandle;

        if (NT_SUCCESS(status = PhOpenThread(
            &threadHandle,
            THREAD_SUSPEND_RESUME,
            Threads[i]->ThreadId
            )))
        {
            status = NtSuspendThread(threadHandle, NULL);
            NtClose(threadHandle);
        }

        if (!NT_SUCCESS(status))
        {
            BOOLEAN connected;

            success = FALSE;

            if (!cancelled && PhpShowErrorAndConnectToPhSvc(
                hWnd,
                PhaFormatString(L"Unable to suspend thread %lu", HandleToUlong(Threads[i]->ThreadId))->Buffer,
                status,
                &connected
                ))
            {
                if (connected)
                {
                    if (NT_SUCCESS(status = PhSvcCallControlThread(Threads[i]->ThreadId, PhSvcControlThreadSuspend, 0)))
                        success = TRUE;
                    else
                        PhpShowErrorThread(hWnd, L"suspend", Threads[i], status, 0);

                    PhUiDisconnectFromPhSvc();
                }
                else
                {
                    cancelled = TRUE;
                }
            }
            else
            {
                if (!PhpShowErrorThread(hWnd, L"suspend", Threads[i], status, 0))
                    break;
            }
        }
    }

    return success;
}

BOOLEAN PhUiResumeThreads(
    _In_ HWND hWnd,
    _In_ PPH_THREAD_ITEM *Threads,
    _In_ ULONG NumberOfThreads
    )
{
    BOOLEAN success = TRUE;
    BOOLEAN cancelled = FALSE;
    ULONG i;

    for (i = 0; i < NumberOfThreads; i++)
    {
        NTSTATUS status;
        HANDLE threadHandle;

        if (NT_SUCCESS(status = PhOpenThread(
            &threadHandle,
            THREAD_SUSPEND_RESUME,
            Threads[i]->ThreadId
            )))
        {
            status = NtResumeThread(threadHandle, NULL);
            NtClose(threadHandle);
        }

        if (!NT_SUCCESS(status))
        {
            BOOLEAN connected;

            success = FALSE;

            if (!cancelled && PhpShowErrorAndConnectToPhSvc(
                hWnd,
                PhaFormatString(L"Unable to resume thread %lu", HandleToUlong(Threads[i]->ThreadId))->Buffer,
                status,
                &connected
                ))
            {
                if (connected)
                {
                    if (NT_SUCCESS(status = PhSvcCallControlThread(Threads[i]->ThreadId, PhSvcControlThreadResume, 0)))
                        success = TRUE;
                    else
                        PhpShowErrorThread(hWnd, L"resume", Threads[i], status, 0);

                    PhUiDisconnectFromPhSvc();
                }
                else
                {
                    cancelled = TRUE;
                }
            }
            else
            {
                if (!PhpShowErrorThread(hWnd, L"resume", Threads[i], status, 0))
                    break;
            }
        }
    }

    return success;
}

BOOLEAN PhUiSetPriorityThread(
    _In_ HWND hWnd,
    _In_ PPH_THREAD_ITEM Thread,
    _In_ LONG Increment
    )
{
    NTSTATUS status;
    HANDLE threadHandle;

    // Special saturation values
    if (Increment == THREAD_PRIORITY_TIME_CRITICAL)
        Increment = THREAD_BASE_PRIORITY_LOWRT + 1;
    else if (Increment == THREAD_PRIORITY_IDLE)
        Increment = THREAD_BASE_PRIORITY_IDLE - 1;

    if (NT_SUCCESS(status = PhOpenThread(
        &threadHandle,
        THREAD_SET_LIMITED_INFORMATION,
        Thread->ThreadId
        )))
    {
        status = PhSetThreadBasePriority(threadHandle, Increment);
        NtClose(threadHandle);
    }

    if (!NT_SUCCESS(status))
    {
        PhpShowErrorThread(hWnd, L"set the priority of", Thread, status, 0);
        return FALSE;
    }

    return TRUE;
}

BOOLEAN PhUiSetIoPriorityThread(
    _In_ HWND hWnd,
    _In_ PPH_THREAD_ITEM Thread,
    _In_ IO_PRIORITY_HINT IoPriority
    )
{
    NTSTATUS status;
    BOOLEAN success = TRUE;
    HANDLE threadHandle;

    if (NT_SUCCESS(status = PhOpenThread(
        &threadHandle,
        THREAD_SET_INFORMATION,
        Thread->ThreadId
        )))
    {
        status = PhSetThreadIoPriority(threadHandle, IoPriority);
        NtClose(threadHandle);
    }

    if (!NT_SUCCESS(status))
    {
        BOOLEAN connected;

        success = FALSE;

        // The operation may have failed due to the lack of SeIncreaseBasePriorityPrivilege.
        if (PhpShowErrorAndConnectToPhSvc(
            hWnd,
            PhaFormatString(L"Unable to set the I/O priority of thread %lu", HandleToUlong(Thread->ThreadId))->Buffer,
            status,
            &connected
            ))
        {
            if (connected)
            {
                if (NT_SUCCESS(status = PhSvcCallControlThread(Thread->ThreadId, PhSvcControlThreadIoPriority, IoPriority)))
                    success = TRUE;
                else
                    PhpShowErrorThread(hWnd, L"set the I/O priority of", Thread, status, 0);

                PhUiDisconnectFromPhSvc();
            }
        }
        else
        {
            PhpShowErrorThread(hWnd, L"set the I/O priority of", Thread, status, 0);
        }
    }

    return success;
}

BOOLEAN PhUiSetPagePriorityThread(
    _In_ HWND hWnd,
    _In_ PPH_THREAD_ITEM Thread,
    _In_ ULONG PagePriority
    )
{
    NTSTATUS status;
    HANDLE threadHandle;

    if (NT_SUCCESS(status = PhOpenThread(
        &threadHandle,
        THREAD_SET_INFORMATION,
        Thread->ThreadId
        )))
    {
        status = PhSetThreadPagePriority(threadHandle, PagePriority);

        NtClose(threadHandle);
    }

    if (!NT_SUCCESS(status))
    {
        PhpShowErrorThread(hWnd, L"set the page priority of", Thread, status, 0);
        return FALSE;
    }

    return TRUE;
}

BOOLEAN PhUiUnloadModule(
    _In_ HWND hWnd,
    _In_ HANDLE ProcessId,
    _In_ PPH_MODULE_ITEM Module
    )
{
    NTSTATUS status;
    BOOLEAN cont = FALSE;
    HANDLE processHandle;

    if (PhGetIntegerSetting(L"EnableWarnings"))
    {
        PWSTR verb;
        PWSTR message;

        switch (Module->Type)
        {
        case PH_MODULE_TYPE_MODULE:
        case PH_MODULE_TYPE_WOW64_MODULE:
            verb = L"unload";
            message = L"Unloading a module may cause the process to crash.";

            if (WindowsVersion >= WINDOWS_8)
                message = L"Unloading a module may cause the process to crash. NOTE: This feature may not work correctly on your version of Windows and some programs may restrict access or ban your account.";

            break;
        case PH_MODULE_TYPE_KERNEL_MODULE:
            verb = L"unload";
            message = L"Unloading a driver may cause system instability.";
            break;
        case PH_MODULE_TYPE_MAPPED_FILE:
        case PH_MODULE_TYPE_MAPPED_IMAGE:
            verb = L"unmap";
            message = L"Unmapping a section view may cause the process to crash.";
            break;
        default:
            return FALSE;
        }

        cont = PhShowConfirmMessage(
            hWnd,
            verb,
            Module->Name->Buffer,
            message,
            TRUE
            );
    }
    else
    {
        cont = TRUE;
    }

    if (!cont)
        return FALSE;

    switch (Module->Type)
    {
    case PH_MODULE_TYPE_MODULE:
    case PH_MODULE_TYPE_WOW64_MODULE:
        if (NT_SUCCESS(status = PhOpenProcess(
            &processHandle,
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
            PROCESS_VM_READ | PROCESS_VM_WRITE,
            ProcessId
            )))
        {
            LARGE_INTEGER timeout;

            timeout.QuadPart = -(LONGLONG)UInt32x32To64(5, PH_TIMEOUT_SEC);
            status = PhUnloadDllProcess(
                processHandle,
                Module->BaseAddress,
                &timeout
                );

            NtClose(processHandle);
        }

        if (status == STATUS_DLL_NOT_FOUND)
        {
            PhShowError(hWnd, L"Unable to find the module to unload.");
            return FALSE;
        }

        if (!NT_SUCCESS(status))
        {
            PhShowStatus(
                hWnd,
                PhaConcatStrings2(L"Unable to unload ", Module->Name->Buffer)->Buffer,
                status,
                0
                );
            return FALSE;
        }

        break;

    case PH_MODULE_TYPE_KERNEL_MODULE:
        status = PhUnloadDriver(Module->BaseAddress, Module->Name->Buffer);

        if (!NT_SUCCESS(status))
        {
            BOOLEAN success = FALSE;
            BOOLEAN connected;

            if (PhpShowErrorAndConnectToPhSvc(
                hWnd,
                PhaConcatStrings2(L"Unable to unload ", Module->Name->Buffer)->Buffer,
                status,
                &connected
                ))
            {
                if (connected)
                {
                    if (NT_SUCCESS(status = PhSvcCallUnloadDriver(Module->BaseAddress, Module->Name->Buffer)))
                        success = TRUE;
                    else
                        PhShowStatus(hWnd, PhaConcatStrings2(L"Unable to unload ", Module->Name->Buffer)->Buffer, status, 0);

                    PhUiDisconnectFromPhSvc();
                }
            }
            else
            {
                PhShowStatus(
                    hWnd,
                    PhaConcatStrings(
                    3,
                    L"Unable to unload ",
                    Module->Name->Buffer,
                    L". Make sure Process Hacker is running with "
                    L"administrative privileges."
                    )->Buffer,
                    status,
                    0
                    );
                return FALSE;
            }

            return success;
        }

        break;

    case PH_MODULE_TYPE_MAPPED_FILE:
    case PH_MODULE_TYPE_MAPPED_IMAGE:
        if (NT_SUCCESS(status = PhOpenProcess(
            &processHandle,
            PROCESS_VM_OPERATION,
            ProcessId
            )))
        {
            status = NtUnmapViewOfSection(processHandle, Module->BaseAddress);
            NtClose(processHandle);
        }

        if (!NT_SUCCESS(status))
        {
            PhShowStatus(
                hWnd,
                PhaFormatString(L"Unable to unmap the section view at 0x%p", Module->BaseAddress)->Buffer,
                status,
                0
                );
            return FALSE;
        }

        break;

    default:
        return FALSE;
    }

    return TRUE;
}

BOOLEAN PhUiFreeMemory(
    _In_ HWND hWnd,
    _In_ HANDLE ProcessId,
    _In_ PPH_MEMORY_ITEM MemoryItem,
    _In_ BOOLEAN Free
    )
{
    NTSTATUS status;
    BOOLEAN cont = FALSE;
    HANDLE processHandle;

    if (PhGetIntegerSetting(L"EnableWarnings"))
    {
        PWSTR verb;
        PWSTR message;

        if (!(MemoryItem->Type & (MEM_MAPPED | MEM_IMAGE)))
        {
            if (Free)
            {
                verb = L"free";
                message = L"Freeing memory regions may cause the process to crash.\r\n\r\nSome programs may also restrict access or ban your account when freeing the memory of the process.";
            }
            else
            {
                verb = L"decommit";
                message = L"Decommitting memory regions may cause the process to crash.\r\n\r\nSome programs may also restrict access or ban your account when decommitting the memory of the process.";
            }
        }
        else
        {
            verb = L"unmap";
            message = L"Unmapping a section view may cause the process to crash.\r\n\r\nSome programs may also restrict access or ban your account when unmapping the memory of the process.";
        }

        cont = PhShowConfirmMessage(
            hWnd,
            verb,
            L"the memory region",
            message,
            TRUE
            );
    }
    else
    {
        cont = TRUE;
    }

    if (!cont)
        return FALSE;

    if (NT_SUCCESS(status = PhOpenProcess(
        &processHandle,
        PROCESS_VM_OPERATION,
        ProcessId
        )))
    {
        PVOID baseAddress;
        SIZE_T regionSize;

        baseAddress = MemoryItem->BaseAddress;

        if (!(MemoryItem->Type & (MEM_MAPPED | MEM_IMAGE)))
        {
            // The size needs to be 0 if we're freeing.
            if (Free)
                regionSize = 0;
            else
                regionSize = MemoryItem->RegionSize;

            status = NtFreeVirtualMemory(
                processHandle,
                &baseAddress,
                &regionSize,
                Free ? MEM_RELEASE : MEM_DECOMMIT
                );
        }
        else
        {
            status = NtUnmapViewOfSection(processHandle, baseAddress);
        }

        NtClose(processHandle);
    }

    if (!NT_SUCCESS(status))
    {
        PWSTR message;

        if (!(MemoryItem->Type & (MEM_MAPPED | MEM_IMAGE)))
        {
            if (Free)
                message = L"Unable to free the memory region";
            else
                message = L"Unable to decommit the memory region";
        }
        else
        {
            message = L"Unable to unmap the section view";
        }

        PhShowStatus(
            hWnd,
            message,
            status,
            0
            );
        return FALSE;
    }

    return TRUE;
}

static BOOLEAN PhpShowErrorHandle(
    _In_ HWND hWnd,
    _In_ PWSTR Verb,
    _In_ PPH_HANDLE_ITEM Handle,
    _In_ NTSTATUS Status,
    _In_opt_ ULONG Win32Result
    )
{
    WCHAR value[PH_PTR_STR_LEN_1];

    PhPrintPointer(value, (PVOID)Handle->Handle);

    if (!PhIsNullOrEmptyString(Handle->BestObjectName))
    {
        return PhShowContinueStatus(
            hWnd,
            PhaFormatString(
            L"Unable to %s handle \"%s\" (%s)",
            Verb,
            Handle->BestObjectName->Buffer,
            value
            )->Buffer,
            Status,
            Win32Result
            );
    }
    else
    {
        return PhShowContinueStatus(
            hWnd,
            PhaFormatString(
            L"Unable to %s handle %s",
            Verb,
            value
            )->Buffer,
            Status,
            Win32Result
            );
    }
}

BOOLEAN PhUiCloseHandles(
    _In_ HWND hWnd,
    _In_ HANDLE ProcessId,
    _In_ PPH_HANDLE_ITEM *Handles,
    _In_ ULONG NumberOfHandles,
    _In_ BOOLEAN Warn
    )
{
    NTSTATUS status;
    BOOLEAN cont = FALSE;
    BOOLEAN success = TRUE;
    HANDLE processHandle;

    if (NumberOfHandles == 0)
        return FALSE;

    if (Warn && PhGetIntegerSetting(L"EnableWarnings"))
    {
        cont = PhShowConfirmMessage(
            hWnd,
            L"close",
            NumberOfHandles == 1 ? L"the selected handle" : L"the selected handles",
            L"Closing handles may cause system instability and data corruption.",
            FALSE
            );
    }
    else
    {
        cont = TRUE;
    }

    if (!cont)
        return FALSE;

    if (NT_SUCCESS(status = PhOpenProcess(
        &processHandle,
        PROCESS_QUERY_INFORMATION | PROCESS_DUP_HANDLE,
        ProcessId
        )))
    {
        BOOLEAN critical = FALSE;
        BOOLEAN strict = FALSE;

        if (WindowsVersion >= WINDOWS_10)
        {
            BOOLEAN breakOnTermination;
            PROCESS_MITIGATION_POLICY_INFORMATION policyInfo;

            if (NT_SUCCESS(PhGetProcessBreakOnTermination(
                processHandle,
                &breakOnTermination
                )))
            {
                if (breakOnTermination)
                {
                    critical = TRUE;
                }
            }

            policyInfo.Policy = ProcessStrictHandleCheckPolicy;
            policyInfo.StrictHandleCheckPolicy.Flags = 0;

            if (NT_SUCCESS(NtQueryInformationProcess(
                processHandle,
                ProcessMitigationPolicy,
                &policyInfo,
                sizeof(PROCESS_MITIGATION_POLICY_INFORMATION),
                NULL
                )))
            {
                if (policyInfo.StrictHandleCheckPolicy.Flags != 0)
                {
                    strict = TRUE;
                }
            }
        }

        if (critical && strict)
        {
            cont = PhShowConfirmMessage(
                hWnd,
                L"close",
                L"critical process handle(s)",
                L"You are about to close one or more handles for a critical process with strict handle checks enabled. This will shut down the operating system immediately.\r\n\r\n",
                TRUE
                );
        }

        if (!cont)
            return FALSE;

        for (ULONG i = 0; i < NumberOfHandles; i++)
        {
            status = NtDuplicateObject(
                processHandle,
                Handles[i]->Handle,
                NULL,
                NULL,
                0,
                0,
                DUPLICATE_CLOSE_SOURCE
                );

            if (!NT_SUCCESS(status))
            {
                success = FALSE;

                if (!PhpShowErrorHandle(
                    hWnd,
                    L"close",
                    Handles[i],
                    status,
                    0
                    ))
                    break;
            }
        }

        NtClose(processHandle);
    }
    else
    {
        PhShowStatus(hWnd, L"Unable to open the process", status, 0);
        return FALSE;
    }

    return success;
}

BOOLEAN PhUiSetAttributesHandle(
    _In_ HWND hWnd,
    _In_ HANDLE ProcessId,
    _In_ PPH_HANDLE_ITEM Handle,
    _In_ ULONG Attributes
    )
{
    NTSTATUS status;
    HANDLE processHandle;

    if (!KphIsConnected())
    {
        PhShowError2(hWnd, PH_KPH_ERROR_TITLE, PH_KPH_ERROR_MESSAGE);
        return FALSE;
    }

    if (NT_SUCCESS(status = PhOpenProcess(
        &processHandle,
        PROCESS_QUERY_LIMITED_INFORMATION,
        ProcessId
        )))
    {
        OBJECT_HANDLE_FLAG_INFORMATION handleFlagInfo;

        handleFlagInfo.Inherit = !!(Attributes & OBJ_INHERIT);
        handleFlagInfo.ProtectFromClose = !!(Attributes & OBJ_PROTECT_CLOSE);

        status = KphSetInformationObject(
            processHandle,
            Handle->Handle,
            KphObjectHandleFlagInformation,
            &handleFlagInfo,
            sizeof(OBJECT_HANDLE_FLAG_INFORMATION)
            );

        NtClose(processHandle);
    }

    if (!NT_SUCCESS(status))
    {
        PhpShowErrorHandle(hWnd, L"set attributes of", Handle, status, 0);
        return FALSE;
    }

    return TRUE;
}
