/*
 * Process Hacker -
 *   run as dialog
 *
 * Copyright (C) 2010-2013 wj32
 * Copyright (C) 2018 dmex
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
 * The run-as mechanism has three stages:
 * 1. The user enters the information into the dialog box. Here it is decided whether the run-as
 *    service is needed. If it is not, PhCreateProcessAsUser is called directly. Otherwise,
 *    PhExecuteRunAsCommand2 is called for stage 2.
 * 2. PhExecuteRunAsCommand2 creates a random service name and tries to create the service and
 *    execute it (using PhExecuteRunAsCommand). If the process has insufficient permissions, an
 *    elevated instance of phsvc is started and PhSvcCallExecuteRunAsCommand is called.
 * 3. The service is started, and sets up an instance of phsvc with the same random service name as
 *    its port name. Either the original or elevated Process Hacker instance then calls
 *    PhSvcCallInvokeRunAsService to complete the operation.
 */

/*
 *
 * ProcessHacker.exe (user, limited privileges)
 *   *                       | ^
 *   |                       | | phsvc API (LPC)
 *   |                       | |
 *   |                       v |
 *   ProcessHacker.exe (user, full privileges)
 *         | ^                    | ^
 *         | | SCM API (RPC)      | |
 *         | |                    | |
 *         v |                    | | phsvc API (LPC)
 * services.exe                   | |
 *   *                            | |
 *   |                            | |
 *   |                            | |
 *   |                            v |
 *   ProcessHacker.exe (NT AUTHORITY\SYSTEM)
 *     *
 *     |
 *     |
 *     |
 *     program.exe
 */

#include <phapp.h>

#include <shellapi.h>
#include <shlwapi.h>
#include <userenv.h>
#include <winsta.h>
#include <lm.h>

#include <emenu.h>
#include <lsasup.h>

#include <apiimport.h>
#include <actions.h>
#include <phsvc.h>
#include <phsvccl.h>
#include <phsettings.h>
#include <settings.h>
#include <svcsup.h>
#include <mainwnd.h>

typedef struct _RUNAS_DIALOG_CONTEXT
{
    HWND ProgramComboBoxWindowHandle;
    HWND UserComboBoxWindowHandle;
    HWND TypeComboBoxWindowHandle;
    HWND PasswordEditWindowHandle;
    HWND SessionEditWindowHandle;
    HWND DesktopEditWindowHandle;
    HANDLE ProcessId;
    PPH_STRING CurrentWinStaName;
} RUNAS_DIALOG_CONTEXT, *PRUNAS_DIALOG_CONTEXT;

typedef struct _PH_RUNAS_SESSION_ITEM
{
    ULONG SessionId;
    PPH_STRING SessionName;
} PH_RUNAS_SESSION_ITEM, *PPH_RUNAS_SESSION_ITEM;

typedef struct _PH_RUNAS_DESKTOP_ITEM
{
    PPH_STRING DesktopName;
} PH_RUNAS_DESKTOP_ITEM, *PPH_RUNAS_DESKTOP_ITEM;

typedef INT (CALLBACK *MRUSTRINGCMPPROC)(PCWSTR pString1, PCWSTR pString2);
typedef INT (CALLBACK *MRUINARYCMPPROC)(LPCVOID pString1, LPCVOID pString2, ULONG length);

#define MRU_STRING 0x0000 // list will contain strings.
#define MRU_BINARY 0x0001 // list will contain binary data.
#define MRU_CACHEWRITE 0x0002 // only save list order to reg. is FreeMRUList.

typedef struct _MRUINFO
{
    ULONG cbSize;
    UINT uMaxItems;
    UINT uFlags;
    HKEY hKey;
    LPCTSTR lpszSubKey;
    MRUSTRINGCMPPROC lpfnCompare;
} MRUINFO, *PMRUINFO;

static ULONG (WINAPI *NetUserEnum_I)(
    _In_ PCWSTR servername,
    _In_ ULONG level,
    _In_ ULONG filter,
    _Out_ PVOID *bufptr,
    _In_ ULONG prefmaxlen,
    _Out_ PULONG entriesread,
    _Out_ PULONG totalentries,
    _Inout_ PULONG resume_handle
    );

static ULONG (WINAPI *NetApiBufferFree_I)(
    _Frees_ptr_opt_ PVOID Buffer
    );

static HANDLE (WINAPI *CreateMRUList_I)(
    _In_ PMRUINFO lpmi
    );
static INT (WINAPI *AddMRUString_I)(
    _In_ HANDLE hMRU,
    _In_ PWSTR szString
    );
static INT (WINAPI *EnumMRUList_I)(
    _In_ HANDLE hMRU,
    _In_ INT nItem,
    _Out_ PVOID lpData,
    _In_ UINT uLen
    );
static INT (WINAPI *FreeMRUList_I)(
    _In_ HANDLE hMRU
    );

INT_PTR CALLBACK PhpRunAsDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

INT_PTR CALLBACK PhpRunFileWndProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

VOID PhSetDesktopWinStaAccess(
    VOID
    );

VOID PhpSplitUserName(
    _In_ PWSTR UserName,
    _Out_opt_ PPH_STRING* DomainPart,
    _Out_opt_ PPH_STRING* UserPart
    );

#define SIP(String, Integer) { (String), (PVOID)(Integer) }

static PH_KEY_VALUE_PAIR PhpLogonTypePairs[] =
{
    SIP(L"Batch", LOGON32_LOGON_BATCH),
    SIP(L"Interactive", LOGON32_LOGON_INTERACTIVE),
    SIP(L"Network", LOGON32_LOGON_NETWORK),
    SIP(L"New credentials", LOGON32_LOGON_NEW_CREDENTIALS),
    SIP(L"Service", LOGON32_LOGON_SERVICE)
};

static WCHAR RunAsOldServiceName[32] = L"";
static PH_QUEUED_LOCK RunAsOldServiceLock = PH_QUEUED_LOCK_INIT;

static PPH_STRING RunAsServiceName;
static SERVICE_STATUS_HANDLE RunAsServiceStatusHandle;
static PHSVC_STOP RunAsServiceStop;

VOID PhShowRunAsDialog(
    _In_ HWND ParentWindowHandle,
    _In_opt_ HANDLE ProcessId
    )
{
    DialogBoxParam(
        PhInstanceHandle,
        MAKEINTRESOURCE(IDD_RUNAS),
        PhCsForceNoParent ? NULL : ParentWindowHandle,
        PhpRunAsDlgProc,
        (LPARAM)ProcessId
        );
}

BOOLEAN PhShowRunFileDialog(
    _In_ HWND ParentWindowHandle
    )
{
    if (DialogBox(
        PhInstanceHandle,
        MAKEINTRESOURCE(IDD_RUNFILEDLG),
        ParentWindowHandle,
        PhpRunFileWndProc
        ) == IDOK)
    {
        return TRUE;
    }

    return FALSE;

    // Removed from guisup.c (dmex)
    //BOOL (WINAPI *RunFileDlg_I)(
    //    _In_ HWND hwndOwner,
    //    _In_opt_ HICON hIcon,
    //    _In_opt_ LPCWSTR lpszDirectory,
    //    _In_opt_ LPCWSTR lpszTitle,
    //    _In_opt_ LPCWSTR lpszDescription,
    //    _In_ ULONG uFlags
    //    );
    //PVOID shell32Handle;
    //
    //if (shell32Handle = LoadLibrary(L"shell32.dll"))
    //{
    //    if (RunFileDlg_I = PhGetDllBaseProcedureAddress(shell32Handle, NULL, 61))
    //    {
    //        result = !!RunFileDlg_I(
    //            WindowHandle,
    //            WindowIcon,
    //            WorkingDirectory,
    //            WindowTitle,
    //            WindowDescription,
    //            Flags
    //            );
    //    }
    //
    //    FreeLibrary(shell32Handle);
    //}
}

BOOLEAN IsServiceAccount(
    _In_ PPH_STRING UserName
    )
{
    BOOLEAN serviceAccount = FALSE;
    PPH_STRING localSystemSidName;
    PPH_STRING localServiceSidName;
    PPH_STRING localNetworkSidName;

    localSystemSidName = PhGetSidFullName(&PhSeLocalSystemSid, TRUE, NULL);
    localServiceSidName = PhGetSidFullName(&PhSeLocalServiceSid, TRUE, NULL);
    localNetworkSidName = PhGetSidFullName(&PhSeNetworkServiceSid, TRUE, NULL);

    if (
        PhEqualString(localSystemSidName, UserName, TRUE) ||
        PhEqualString(localServiceSidName, UserName, TRUE) ||
        PhEqualString(localNetworkSidName, UserName, TRUE)
        )
    {
        serviceAccount = TRUE;
    }

    PhDereferenceObject(localNetworkSidName);
    PhDereferenceObject(localServiceSidName);
    PhDereferenceObject(localSystemSidName);

    return serviceAccount;
}

BOOLEAN IsCurrentUserAccount(
    _In_ PPH_STRING UserName
    )
{
    PPH_STRING userName;

    if (userName = PhGetTokenUserString(PhGetOwnTokenAttributes().TokenHandle, TRUE))
    {
        if (PhEndsWithString(userName, UserName, TRUE))
        {
            PhDereferenceObject(userName);
            return TRUE;
        }

        PhDereferenceObject(userName);
    }

    return FALSE;
}

PPH_STRING GetCurrentWinStaName(
    VOID
    )
{
    PPH_STRING string;

    string = PhCreateStringEx(NULL, 0x200);

    if (GetUserObjectInformation(
        GetProcessWindowStation(),
        UOI_NAME,
        string->Buffer,
        (ULONG)string->Length + sizeof(UNICODE_NULL),
        NULL
        ))
    {
        PhTrimToNullTerminatorString(string);
        return string;
    }
    else
    {
        PhDereferenceObject(string);
        return PhCreateString(L"WinSta0"); // assume the current window station is WinSta0
    }
}

PPH_STRING GetCurrentDesktopName(
    VOID
    )
{
    PPH_STRING string;

    string = PhCreateStringEx(NULL, 0x200);

    if (GetUserObjectInformation(
        GetThreadDesktop(HandleToUlong(NtCurrentThreadId())),
        UOI_NAME,
        string->Buffer,
        (ULONG)string->Length + sizeof(UNICODE_NULL),
        NULL
        ))
    {
        PhTrimToNullTerminatorString(string);
        return string;
    }
    else
    {
        PhDereferenceObject(string);
        return PhCreateString(L"Default");
    }
}

PPH_STRING PhpGetCurrentDesktopInfo(
    VOID
    )
{
    static PH_STRINGREF seperator = PH_STRINGREF_INIT(L"\\"); // OBJ_NAME_PATH_SEPARATOR
    PPH_STRING desktopInfo = NULL;
    PPH_STRING winstationName = NULL;
    PPH_STRING desktopName = NULL;

    winstationName = GetCurrentWinStaName();
    desktopName = GetCurrentDesktopName();

    if (winstationName && desktopName)
    {
        desktopInfo = PhConcatStringRef3(&winstationName->sr, &seperator, &desktopName->sr);
    }

    if (PhIsNullOrEmptyString(desktopInfo))
    {
        PhMoveReference(&desktopInfo, PhCreateStringFromUnicodeString(&NtCurrentPeb()->ProcessParameters->DesktopInfo));
    }

    if (winstationName)
        PhDereferenceObject(winstationName);
    if (desktopName)
        PhDereferenceObject(desktopName);

    return desktopInfo;
}

BOOLEAN PhpInitializeNetApi(VOID)
{
    static PH_INITONCE initOnce = PH_INITONCE_INIT;
    static PVOID netapiModuleHandle = NULL;

    if (PhBeginInitOnce(&initOnce))
    {
        if (netapiModuleHandle = LoadLibrary(L"netapi32.dll"))
        {
            NetUserEnum_I = PhGetDllBaseProcedureAddress(netapiModuleHandle, "NetUserEnum", 0);
            NetApiBufferFree_I = PhGetDllBaseProcedureAddress(netapiModuleHandle, "NetApiBufferFree", 0);
        }

        if (netapiModuleHandle && !NetUserEnum_I && !NetApiBufferFree_I)
        {
            FreeLibrary(netapiModuleHandle);
            netapiModuleHandle = NULL;
        }

        PhEndInitOnce(&initOnce);
    }

    if (netapiModuleHandle)
        return TRUE;

    return FALSE;
}

BOOLEAN PhpInitializeMRUList(VOID)
{
    static PH_INITONCE initOnce = PH_INITONCE_INIT;
    static PVOID comctl32ModuleHandle = NULL;

    if (PhBeginInitOnce(&initOnce))
    {
        if (comctl32ModuleHandle = LoadLibrary(L"comctl32.dll"))
        {
            CreateMRUList_I = PhGetDllBaseProcedureAddress(comctl32ModuleHandle, "CreateMRUListW", 0);
            AddMRUString_I = PhGetDllBaseProcedureAddress(comctl32ModuleHandle, "AddMRUStringW", 0);
            EnumMRUList_I = PhGetDllBaseProcedureAddress(comctl32ModuleHandle, "EnumMRUListW", 0);
            FreeMRUList_I = PhGetDllBaseProcedureAddress(comctl32ModuleHandle, "FreeMRUList", 0);
        }

        if (!CreateMRUList_I && !AddMRUString_I && !EnumMRUList_I && !FreeMRUList_I && comctl32ModuleHandle)
        {
            FreeLibrary(comctl32ModuleHandle);
            comctl32ModuleHandle = NULL;
        }

        PhEndInitOnce(&initOnce);
    }

    if (comctl32ModuleHandle)
        return TRUE;

    return FALSE;
}

static HANDLE PhpCreateRunMRUList(
    VOID
    )
{
    MRUINFO info;

    if (!CreateMRUList_I)
        return NULL;

    memset(&info, 0, sizeof(MRUINFO));
    info.cbSize = sizeof(MRUINFO);
    info.uMaxItems = UINT_MAX;
    info.hKey = HKEY_CURRENT_USER;
    info.lpszSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RunMRU";

    return CreateMRUList_I(&info);
}

static VOID PhpAddRunMRUListEntry(
    _In_ PPH_STRING CommandLine
    )
{
    static PH_STRINGREF prefixSr = PH_STRINGREF_INIT(L"\\1");
    HANDLE listHandle;
    PPH_STRING commandString;

    if (!(listHandle = PhpCreateRunMRUList()))
        return;

    commandString = PhConcatStringRef2(&CommandLine->sr, &prefixSr);
    AddMRUString_I(listHandle, commandString->Buffer);
    PhDereferenceObject(commandString);

    FreeMRUList_I(listHandle);
}

static VOID PhpAddProgramsToComboBox(
    _In_ HWND ComboBoxHandle
    )
{
    static PH_STRINGREF prefixSr = PH_STRINGREF_INIT(L"\\1");
    HANDLE listHandle;
    INT listCount;

    if (!PhpInitializeMRUList())
        return;
    if (!(listHandle = PhpCreateRunMRUList()))
        return;

    listCount = EnumMRUList_I(
        listHandle,
        MAXINT,
        NULL,
        0
        );

    for (INT i = 0; i < listCount; i++)
    {
        PPH_STRING programName;
        PH_STRINGREF nameSr;
        PH_STRINGREF firstPart;
        PH_STRINGREF remainingPart;
        WCHAR entry[MAX_PATH] = L"";

        if (!EnumMRUList_I(
            listHandle,
            i,
            entry,
            RTL_NUMBER_OF(entry)
            ))
        {
            break;
        }

        PhInitializeStringRefLongHint(&nameSr, entry);

        if (!PhSplitStringRefAtString(&nameSr, &prefixSr, TRUE, &firstPart, &remainingPart))
        {
            ComboBox_AddString(ComboBoxHandle, entry);
            continue;
        }

        programName = PhCreateString2(&firstPart);
        ComboBox_AddString(ComboBoxHandle, PhGetString(programName));
        PhDereferenceObject(programName);
    }

    FreeMRUList_I(listHandle);
}

VOID PhpFreeProgramsComboBox(
    _In_ HWND ComboBoxHandle
    )
{
    ULONG total;

    if ((total = ComboBox_GetCount(ComboBoxHandle)) == CB_ERR)
        return;

    for (ULONG i = 0; i < total; i++)
    {
        ComboBox_DeleteString(ComboBoxHandle, i);
    }
}

static VOID PhpFreeAccountsComboBox(
    _In_ HWND ComboBoxHandle
    )
{
    ULONG total;

    if ((total = ComboBox_GetCount(ComboBoxHandle)) == CB_ERR)
        return;

    for (ULONG i = 0; i < total; i++)
    {
        ComboBox_DeleteString(ComboBoxHandle, i);
    }

    ComboBox_ResetContent(ComboBoxHandle);
}

static VOID PhpAddAccountsToComboBox(
    _In_ HWND ComboBoxHandle
    )
{
    NET_API_STATUS status;
    LPUSER_INFO_0 userinfoArray = NULL;
    ULONG userinfoEntriesRead = 0;
    ULONG userinfoTotalEntries = 0;

    PhpFreeAccountsComboBox(ComboBoxHandle);

    ComboBox_AddString(ComboBoxHandle, PH_AUTO_T(PH_STRING, PhGetSidFullName(&PhSeLocalSystemSid, TRUE, NULL))->Buffer);
    ComboBox_AddString(ComboBoxHandle, PH_AUTO_T(PH_STRING, PhGetSidFullName(&PhSeLocalServiceSid, TRUE, NULL))->Buffer);
    ComboBox_AddString(ComboBoxHandle, PH_AUTO_T(PH_STRING, PhGetSidFullName(&PhSeNetworkServiceSid, TRUE, NULL))->Buffer);

    if (!PhpInitializeNetApi())
        return;

    NetUserEnum_I(
        NULL,
        0,
        FILTER_NORMAL_ACCOUNT,
        &userinfoArray,
        MAX_PREFERRED_LENGTH,
        &userinfoEntriesRead,
        &userinfoTotalEntries,
        NULL
        );

    if (userinfoArray)
    {
        NetApiBufferFree_I(userinfoArray);
        userinfoArray = NULL;
    }

    status = NetUserEnum_I(
        NULL,
        0,
        FILTER_NORMAL_ACCOUNT,
        &userinfoArray,
        MAX_PREFERRED_LENGTH,
        &userinfoEntriesRead,
        &userinfoTotalEntries,
        NULL
        );

    if (status == NERR_Success)
    {
        PPH_STRING username;
        PPH_STRING userDomainName = NULL;

        if (username = PhGetSidFullName(PhGetOwnTokenAttributes().TokenSid, TRUE, NULL))
        {
            PhpSplitUserName(username->Buffer, &userDomainName, NULL);
            PhDereferenceObject(username);
        }

        for (ULONG i = 0; i < userinfoEntriesRead; i++)
        {
            LPUSER_INFO_0 entry = PTR_ADD_OFFSET(userinfoArray, sizeof(USER_INFO_0) * i);

            if (entry->usri0_name)
            {
                if (userDomainName)
                {
                    PPH_STRING usernameString;

                    usernameString = PhConcatStrings(
                        3, 
                        userDomainName->Buffer, 
                        L"\\", 
                        entry->usri0_name
                        );

                    ComboBox_AddString(ComboBoxHandle, usernameString->Buffer);
                    PhDereferenceObject(usernameString);
                }
                else
                {
                    ComboBox_AddString(ComboBoxHandle, entry->usri0_name);
                }
            }
        }

        if (userDomainName)
            PhDereferenceObject(userDomainName);
    }

    if (userinfoArray)
        NetApiBufferFree_I(userinfoArray);

    //LSA_HANDLE policyHandle;
    //LSA_ENUMERATION_HANDLE enumerationContext = 0;
    //PLSA_ENUMERATION_INFORMATION buffer;
    //ULONG count;
    //PPH_STRING name;
    //SID_NAME_USE nameUse;
    //
    //if (NT_SUCCESS(PhOpenLsaPolicy(&policyHandle, POLICY_VIEW_LOCAL_INFORMATION, NULL)))
    //{
    //    while (NT_SUCCESS(LsaEnumerateAccounts(
    //        policyHandle,
    //        &enumerationContext,
    //        &buffer,
    //        0x100,
    //        &count
    //        )))
    //    {
    //        for (i = 0; i < count; i++)
    //        {
    //            name = PhGetSidFullName(buffer[i].Sid, TRUE, &nameUse);
    //            if (name)
    //            {
    //                if (nameUse == SidTypeUser)
    //                    ComboBox_AddString(ComboBoxHandle, name->Buffer);
    //                PhDereferenceObject(name);
    //            }
    //        }
    //        LsaFreeMemory(buffer);
    //    }
    //
    //    LsaClose(policyHandle);
    //}
}

static VOID PhpFreeSessionsComboBox(
    _In_ HWND ComboBoxHandle
    )
{
    PPH_RUNAS_SESSION_ITEM entry;
    ULONG total;
    ULONG i;

    if ((total = ComboBox_GetCount(ComboBoxHandle)) == CB_ERR)
        return;

    for (i = 0; i < total; i++)
    {
        entry = (PPH_RUNAS_SESSION_ITEM)ComboBox_GetItemData(ComboBoxHandle, i);

        if (entry->SessionName)
            PhDereferenceObject(entry->SessionName);

        PhFree(entry);
    }

    ComboBox_ResetContent(ComboBoxHandle);
}

static VOID PhpAddSessionsToComboBox(
    _In_ HWND ComboBoxHandle
    )
{
    PSESSIONIDW sessions;
    ULONG numberOfSessions;
    ULONG i;

    PhpFreeSessionsComboBox(ComboBoxHandle);

    if (WinStationEnumerateW(NULL, &sessions, &numberOfSessions))
    {
        for (i = 0; i < numberOfSessions; i++)
        {
            PPH_STRING menuString;
            WINSTATIONINFORMATION winStationInfo;
            ULONG returnLength;

            if (!WinStationQueryInformationW(
                NULL,
                sessions[i].SessionId,
                WinStationInformation,
                &winStationInfo,
                sizeof(WINSTATIONINFORMATION),
                &returnLength
                ))
            {
                winStationInfo.Domain[0] = UNICODE_NULL;
                winStationInfo.UserName[0] = UNICODE_NULL;
            }

            if (
                winStationInfo.UserName[0] != UNICODE_NULL &&
                sessions[i].WinStationName[0] != UNICODE_NULL
                )
            {
                menuString = PhFormatString(L"%lu: %s (%s\\%s)",
                    sessions[i].SessionId,
                    sessions[i].WinStationName,
                    winStationInfo.Domain,
                    winStationInfo.UserName
                    );
            }
            else if (winStationInfo.UserName[0] != UNICODE_NULL)
            {
                menuString = PhFormatString(L"%lu: %s\\%s",
                    sessions[i].SessionId,
                    winStationInfo.Domain,
                    winStationInfo.UserName
                    );
            }
            else if (sessions[i].WinStationName[0] != UNICODE_NULL)
            {
                menuString = PhFormatString(L"%lu: %s",
                    sessions[i].SessionId,
                    sessions[i].WinStationName
                    );
            }
            else
            {
                menuString = PhFormatString(L"%lu", sessions[i].SessionId);
            }

            {
                PPH_RUNAS_SESSION_ITEM entry;
                INT itemIndex;

                entry = PhAllocate(sizeof(PH_RUNAS_SESSION_ITEM));
                entry->SessionId = sessions[i].SessionId;
                entry->SessionName = menuString;

                if ((itemIndex = ComboBox_AddString(ComboBoxHandle, menuString->Buffer)) != CB_ERR)
                {
                    ComboBox_SetItemData(ComboBoxHandle, itemIndex, entry);
                }
            }
        }

        WinStationFreeMemory(sessions);
    }
}

typedef struct _RUNAS_DIALOG_DESKTOP_CALLBACK
{
    PPH_LIST DesktopList;
    PPH_STRING WinStaName;
} RUNAS_DIALOG_DESKTOP_CALLBACK, *PRUNAS_DIALOG_DESKTOP_CALLBACK;

static BOOL CALLBACK EnumDesktopsCallback(
    _In_ PWSTR DesktopName,
    _In_ LPARAM Context
    )
{
    PRUNAS_DIALOG_DESKTOP_CALLBACK context = (PRUNAS_DIALOG_DESKTOP_CALLBACK)Context;

    PhAddItemList(context->DesktopList, PhConcatStrings(
        3,
        PhGetString(context->WinStaName),
        L"\\",
        DesktopName
        ));

    return TRUE;
}

static VOID PhpFreeDesktopsComboBox(
    _In_ HWND ComboBoxHandle
    )
{
    PPH_RUNAS_DESKTOP_ITEM entry;
    ULONG total;
    ULONG i;

    if ((total = ComboBox_GetCount(ComboBoxHandle)) == CB_ERR)
        return;

    for (i = 0; i < total; i++)
    {
        entry = (PPH_RUNAS_DESKTOP_ITEM)ComboBox_GetItemData(ComboBoxHandle, i);

        if (entry->DesktopName)
            PhDereferenceObject(entry->DesktopName);

        PhFree(entry);
    }

    ComboBox_ResetContent(ComboBoxHandle);
}

static VOID PhpAddDesktopsToComboBox(
    _In_ HWND ComboBoxHandle
    )
{
    ULONG i;
    RUNAS_DIALOG_DESKTOP_CALLBACK callback;

    PhpFreeDesktopsComboBox(ComboBoxHandle);

    callback.DesktopList = PhCreateList(10);
    callback.WinStaName = GetCurrentWinStaName();

    EnumDesktops(GetProcessWindowStation(), EnumDesktopsCallback, (LPARAM)&callback);

    for (i = 0; i < callback.DesktopList->Count; i++)
    {
        INT itemIndex = ComboBox_AddString(
            ComboBoxHandle, 
            PhGetString(callback.DesktopList->Items[i])
            );

        if (itemIndex != CB_ERR)
        {
            PPH_RUNAS_DESKTOP_ITEM entry;

            entry = PhAllocate(sizeof(PH_RUNAS_DESKTOP_ITEM));
            entry->DesktopName = callback.DesktopList->Items[i];

            ComboBox_SetItemData(ComboBoxHandle, itemIndex, entry);
        }
    }

    PhDereferenceObject(callback.DesktopList);
    PhDereferenceObject(callback.WinStaName);
}

VOID SetDefaultProgramEntry(
    _In_ HWND ComboBoxHandle
    )
{
    //Edit_SetText(ComboBoxHandle, PhaGetStringSetting(L"RunAsProgram")->Buffer);
    ComboBox_SetCurSel(ComboBoxHandle, 0);
}

VOID SetDefaultSessionEntry(
    _In_ HWND ComboBoxHandle
    )
{
    INT sessionCount;
    ULONG currentSessionId = 0;

    if (!NT_SUCCESS(PhGetProcessSessionId(NtCurrentProcess(), &currentSessionId)))
        return;

    if ((sessionCount = ComboBox_GetCount(ComboBoxHandle)) == CB_ERR)
        return;

    for (INT i = 0; i < sessionCount; i++)
    {
        PPH_RUNAS_SESSION_ITEM entry = (PPH_RUNAS_SESSION_ITEM)ComboBox_GetItemData(ComboBoxHandle, i);

        if (entry && entry->SessionId == currentSessionId)
        {
            ComboBox_SetCurSel(ComboBoxHandle, i);
            break;
        }
    }
}

VOID SetDefaultDesktopEntry(
    _In_ PRUNAS_DIALOG_CONTEXT Context,
    _In_ HWND ComboBoxHandle
    )
{
    INT sessionCount;
    PPH_STRING desktopName;

    if (!(desktopName = PhpGetCurrentDesktopInfo()))
        return;

    if ((sessionCount = ComboBox_GetCount(ComboBoxHandle)) == CB_ERR)
    {
        PhClearReference(&desktopName);
        return;
    }

    for (INT i = 0; i < sessionCount; i++)
    {
        PPH_RUNAS_DESKTOP_ITEM entry = (PPH_RUNAS_DESKTOP_ITEM)ComboBox_GetItemData(ComboBoxHandle, i);

        if (PhEqualStringRef(&entry->DesktopName->sr, &desktopName->sr, TRUE))
        {
            ComboBox_SetCurSel(ComboBoxHandle, i);
            break;
        }
    }

    PhClearReference(&desktopName);
}

INT_PTR CALLBACK PhpRunAsDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PRUNAS_DIALOG_CONTEXT context;

    if (uMsg == WM_INITDIALOG)
    {
        context = PhAllocateZero(sizeof(RUNAS_DIALOG_CONTEXT));

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
            SendMessage(hwndDlg, WM_SETICON, ICON_SMALL, (LPARAM)PH_LOAD_SHARED_ICON_SMALL(PhInstanceHandle, MAKEINTRESOURCE(IDI_PROCESSHACKER)));
            SendMessage(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM)PH_LOAD_SHARED_ICON_LARGE(PhInstanceHandle, MAKEINTRESOURCE(IDI_PROCESSHACKER)));

            context->ProgramComboBoxWindowHandle = GetDlgItem(hwndDlg, IDC_PROGRAMCOMBO);
            context->SessionEditWindowHandle = GetDlgItem(hwndDlg, IDC_SESSIONCOMBO);
            context->DesktopEditWindowHandle = GetDlgItem(hwndDlg, IDC_DESKTOPCOMBO);
            context->TypeComboBoxWindowHandle = GetDlgItem(hwndDlg, IDC_TYPE);
            context->UserComboBoxWindowHandle = GetDlgItem(hwndDlg, IDC_USERNAME);
            context->PasswordEditWindowHandle = GetDlgItem(hwndDlg, IDC_PASSWORD);
            context->ProcessId = (HANDLE)lParam;

            PhCenterWindow(hwndDlg, PhMainWndHandle);

            {
                COMBOBOXINFO info = { sizeof(COMBOBOXINFO) };

                if (SendMessage(context->ProgramComboBoxWindowHandle, CB_GETCOMBOBOXINFO, 0, (LPARAM)&info))
                {
                    if (SHAutoComplete)
                        SHAutoComplete(info.hwndItem, SHACF_DEFAULT);
                }
            }

            ComboBox_AddString(context->TypeComboBoxWindowHandle, L"Batch");
            ComboBox_AddString(context->TypeComboBoxWindowHandle, L"Interactive");
            ComboBox_AddString(context->TypeComboBoxWindowHandle, L"Network");
            ComboBox_AddString(context->TypeComboBoxWindowHandle, L"New credentials");
            ComboBox_AddString(context->TypeComboBoxWindowHandle, L"Service");
            PhSelectComboBoxString(context->TypeComboBoxWindowHandle, L"Interactive", FALSE);

            PhpAddProgramsToComboBox(context->ProgramComboBoxWindowHandle);
            PhpAddAccountsToComboBox(context->UserComboBoxWindowHandle);
            PhpAddSessionsToComboBox(context->SessionEditWindowHandle);
            PhpAddDesktopsToComboBox(context->DesktopEditWindowHandle);

            SetDefaultProgramEntry(context->ProgramComboBoxWindowHandle);
            SetDefaultSessionEntry(context->SessionEditWindowHandle);
            SetDefaultDesktopEntry(context, context->DesktopEditWindowHandle);

            if (!context->ProcessId)
            {
                PPH_STRING runAsUserName = PhaGetStringSetting(L"RunAsUserName");
                INT runAsUserNameIndex = CB_ERR;

                // Fire the user name changed event so we can fix the logon type.
                if (!PhIsNullOrEmptyString(runAsUserName))
                {
                    runAsUserNameIndex = ComboBox_FindString(
                        context->UserComboBoxWindowHandle, 
                        0, 
                        PhGetString(runAsUserName)
                        );
                }

                if (runAsUserNameIndex != CB_ERR)
                    ComboBox_SetCurSel(context->UserComboBoxWindowHandle, runAsUserNameIndex);
                else
                    ComboBox_SetCurSel(context->UserComboBoxWindowHandle, 0);

                SendMessage(hwndDlg, WM_COMMAND, MAKEWPARAM(IDC_USERNAME, CBN_EDITCHANGE), 0);
            }
            else
            {
                HANDLE processHandle;
                HANDLE tokenHandle;
                PPH_STRING userName;

                if (NT_SUCCESS(PhOpenProcess(
                    &processHandle,
                    PROCESS_QUERY_LIMITED_INFORMATION,
                    context->ProcessId
                    )))
                {
                    if (NT_SUCCESS(PhOpenProcessToken(
                        processHandle,
                        TOKEN_QUERY,
                        &tokenHandle
                        )))
                    {
                        if (userName = PhGetTokenUserString(tokenHandle, TRUE))
                        {
                            PhSetWindowText(context->UserComboBoxWindowHandle, userName->Buffer);
                            PhDereferenceObject(userName);
                        }

                        NtClose(tokenHandle);
                    }

                    NtClose(processHandle);
                }

                EnableWindow(context->UserComboBoxWindowHandle, FALSE);
                EnableWindow(context->PasswordEditWindowHandle, FALSE);
                EnableWindow(context->TypeComboBoxWindowHandle, FALSE);
            }

            PhSetDialogFocus(hwndDlg, context->ProgramComboBoxWindowHandle);
            Edit_SetSel(context->ProgramComboBoxWindowHandle, -1, -1);

            //if (!PhGetOwnTokenAttributes().Elevated)
            //    Button_SetElevationRequiredState(GetDlgItem(hwndDlg, IDOK), TRUE);

            PhInitializeWindowTheme(hwndDlg, PhEnableThemeSupport);
        }
        break;
    case WM_DESTROY:
        {
            PhpFreeDesktopsComboBox(context->DesktopEditWindowHandle);
            PhpFreeSessionsComboBox(context->SessionEditWindowHandle);
            PhpFreeAccountsComboBox(context->UserComboBoxWindowHandle);
            PhpFreeProgramsComboBox(context->ProgramComboBoxWindowHandle);

            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
            PhFree(context);
        }
        break;
    case WM_COMMAND:
        {    
            switch (GET_WM_COMMAND_CMD(wParam, lParam))
            {
            case CBN_DROPDOWN:
                {
                    if (GET_WM_COMMAND_HWND(wParam, lParam) == context->UserComboBoxWindowHandle)
                    {
                        //PhpAddAccountsToComboBox(context->UserComboBoxWindowHandle);
                    }

                    if (GET_WM_COMMAND_HWND(wParam, lParam) == context->SessionEditWindowHandle)
                    {
                        PhpAddSessionsToComboBox(context->SessionEditWindowHandle);
                        SetDefaultSessionEntry(context->SessionEditWindowHandle);
                    }

                    if (GET_WM_COMMAND_HWND(wParam, lParam) == context->DesktopEditWindowHandle)
                    {
                        PhpAddDesktopsToComboBox(context->DesktopEditWindowHandle);
                        SetDefaultDesktopEntry(context, context->DesktopEditWindowHandle);
                    }
                }
                break;
            }

            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDCANCEL:
                EndDialog(hwndDlg, IDCANCEL);
                break;
            case IDOK:
                {
                    NTSTATUS status;
                    BOOLEAN useLinkedToken = FALSE;
                    BOOLEAN createSuspended = FALSE;
                    ULONG logonType = ULONG_MAX;
                    ULONG sessionId = ULONG_MAX;
                    PPH_STRING program = NULL;
                    PPH_STRING username = NULL;
                    PPH_STRING password = NULL;
                    PPH_STRING logonTypeString;
                    PPH_STRING desktopName = NULL;
                    INT selectionIndex = CB_ERR;

                    program = PH_AUTO(PhGetWindowText(context->ProgramComboBoxWindowHandle));
                    username = PH_AUTO(PhGetWindowText(context->UserComboBoxWindowHandle));
                    logonTypeString = PH_AUTO(PhGetWindowText(context->TypeComboBoxWindowHandle));
                    useLinkedToken = Button_GetCheck(GetDlgItem(hwndDlg, IDC_TOGGLEELEVATION)) == BST_CHECKED;
                    createSuspended = Button_GetCheck(GetDlgItem(hwndDlg, IDC_TOGGLESUSPENDED)) == BST_CHECKED;

                    if (PhIsNullOrEmptyString(program))
                        break;

                    if ((selectionIndex = ComboBox_GetCurSel(context->SessionEditWindowHandle)) != CB_ERR)
                    {
                        PPH_RUNAS_SESSION_ITEM sessionEntry;

                        if (sessionEntry = (PPH_RUNAS_SESSION_ITEM)ComboBox_GetItemData(context->SessionEditWindowHandle, selectionIndex))
                        {
                            sessionId = sessionEntry->SessionId;
                        }
                    }

                    if ((selectionIndex = ComboBox_GetCurSel(context->DesktopEditWindowHandle)) != CB_ERR)
                    {
                        PPH_RUNAS_DESKTOP_ITEM desktopEntry;

                        if (desktopEntry = (PPH_RUNAS_DESKTOP_ITEM)ComboBox_GetItemData(context->DesktopEditWindowHandle, selectionIndex))
                        {
                            desktopName = desktopEntry->DesktopName;
                        }
                    }

                    if (selectionIndex == CB_ERR)
                        break;
                    if (sessionId == ULONG_MAX)
                        break;

                    // Fix up the user name if it doesn't have a domain.
                    if (PhFindCharInString(username, 0, '\\') == -1)
                    {
                        PSID sid;
                        PPH_STRING newUserName;

                        if (NT_SUCCESS(PhLookupName(&username->sr, &sid, NULL, NULL)))
                        {
                            if (newUserName = PH_AUTO(PhGetSidFullName(sid, TRUE, NULL)))
                                PhSwapReference(&username, newUserName);

                            PhFree(sid);
                        }
                    }

                    if (!IsServiceAccount(username))
                    {
                        password = PhGetWindowText(context->PasswordEditWindowHandle);
                        PhSetWindowText(context->PasswordEditWindowHandle, L"");
                    }

                    //if (IsCurrentUserAccount(username))
                    //{
                    //    status = PhCreateProcessWin32(
                    //        NULL,
                    //        program->Buffer,
                    //        NULL,
                    //        NULL,
                    //        0,
                    //        NULL,
                    //        NULL,
                    //        NULL
                    //        );
                    //}

                    if (PhFindIntegerSiKeyValuePairs(
                        PhpLogonTypePairs,
                        sizeof(PhpLogonTypePairs),
                        logonTypeString->Buffer,
                        &logonType
                        ))
                    {
                        ULONG currentSessionId = ULONG_MAX;

                        PhGetProcessSessionId(NtCurrentProcess(), &currentSessionId);

                        if (
                            logonType == LOGON32_LOGON_INTERACTIVE &&
                            !context->ProcessId &&
                            sessionId == currentSessionId &&
                            !useLinkedToken
                            )
                        {
                            // We are eligible to load the user profile.
                            // This must be done here, not in the service, because
                            // we need to be in the target session.

                            PH_CREATE_PROCESS_AS_USER_INFO createInfo;
                            PPH_STRING domainPart = NULL;
                            PPH_STRING userPart = NULL;

                            PhpSplitUserName(username->Buffer, &domainPart, &userPart);

                            memset(&createInfo, 0, sizeof(PH_CREATE_PROCESS_AS_USER_INFO));
                            createInfo.CommandLine = PhGetString(program);
                            createInfo.UserName = PhGetString(userPart);
                            createInfo.DomainName = PhGetString(domainPart);
                            createInfo.Password = PhGetStringOrEmpty(password);

                            // Whenever we can, try not to set the desktop name; it breaks a lot of things.
                            if (!PhIsNullOrEmptyString(desktopName) && !PhEqualString2(desktopName, L"WinSta0\\Default", TRUE))
                                createInfo.DesktopName = PhGetString(desktopName);

                            PhSetDesktopWinStaAccess();

                            status = PhCreateProcessAsUser(
                                &createInfo,
                                PH_CREATE_PROCESS_WITH_PROFILE | (createSuspended ? PH_CREATE_PROCESS_SUSPENDED : 0),
                                NULL,
                                NULL,
                                NULL
                                );

                            if (domainPart) PhDereferenceObject(domainPart);
                            if (userPart) PhDereferenceObject(userPart);
                        }
                        else
                        {
                            if (context->ProcessId)
                            {
                                HANDLE processHandle = NULL;
                                HANDLE newProcessHandle;
                                STARTUPINFOEX startupInfo;
                                SIZE_T attributeListLength = 0;
                                PSECURITY_DESCRIPTOR processSecurityDescriptor = NULL;
                                PSECURITY_DESCRIPTOR tokenSecurityDescriptor = NULL;
                                PVOID environment = NULL;
                                HANDLE tokenHandle;
                                ULONG flags = 0;

                                memset(&startupInfo, 0, sizeof(STARTUPINFOEX));
                                startupInfo.StartupInfo.cb = sizeof(STARTUPINFOEX);
                                startupInfo.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
                                startupInfo.StartupInfo.wShowWindow = SW_SHOWNORMAL;

                                status = PhOpenProcess(
                                    &processHandle,
                                    PROCESS_CREATE_PROCESS | (PhGetOwnTokenAttributes().Elevated ? PROCESS_QUERY_LIMITED_INFORMATION | READ_CONTROL : 0),
                                    context->ProcessId
                                    );

                                if (!NT_SUCCESS(status))
                                    goto CleanupExit;

                                if (!InitializeProcThreadAttributeList(NULL, 1, 0, &attributeListLength) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                                {
                                    status = PhGetLastWin32ErrorAsNtStatus();
                                    goto CleanupExit;
                                }

                                startupInfo.lpAttributeList = PhAllocate(attributeListLength);

                                if (!InitializeProcThreadAttributeList(startupInfo.lpAttributeList, 1, 0, &attributeListLength))
                                {
                                    status = PhGetLastWin32ErrorAsNtStatus();
                                    goto CleanupExit;
                                }

                                if (!UpdateProcThreadAttribute(startupInfo.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &processHandle, sizeof(HANDLE), NULL, NULL))
                                {
                                    status = PhGetLastWin32ErrorAsNtStatus();
                                    goto CleanupExit;
                                }

                                if (PhGetOwnTokenAttributes().Elevated)
                                {
                                    PhGetObjectSecurity(
                                        processHandle,
                                        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
                                        &processSecurityDescriptor
                                        );
                                }

                                if (NT_SUCCESS(PhOpenProcessToken(
                                    processHandle,
                                    TOKEN_QUERY | (PhGetOwnTokenAttributes().Elevated ? READ_CONTROL : 0),
                                    &tokenHandle
                                    )))
                                {
                                    if (PhGetOwnTokenAttributes().Elevated)
                                    {
                                        PhGetObjectSecurity(
                                            tokenHandle,
                                            OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
                                            &tokenSecurityDescriptor
                                            );
                                    }

                                    if (CreateEnvironmentBlock_Import() && CreateEnvironmentBlock_Import()(&environment, tokenHandle, FALSE))
                                    {
                                        flags |= PH_CREATE_PROCESS_UNICODE_ENVIRONMENT;
                                    }

                                    NtClose(tokenHandle);
                                }
                
                                status = PhCreateProcessWin32Ex(
                                    NULL,
                                    PhGetString(program),
                                    environment,
                                    NULL,
                                    &startupInfo.StartupInfo,
                                    PH_CREATE_PROCESS_SUSPENDED | PH_CREATE_PROCESS_NEW_CONSOLE | PH_CREATE_PROCESS_EXTENDED_STARTUPINFO | flags,
                                    NULL,
                                    NULL,
                                    &newProcessHandle,
                                    NULL
                                    );

                                if (NT_SUCCESS(status))
                                {
                                    PROCESS_BASIC_INFORMATION basicInfo;

                                    if (PhGetOwnTokenAttributes().Elevated)
                                    {
                                        // Note: This is needed to workaround a severe bug with PROC_THREAD_ATTRIBUTE_PARENT_PROCESS 
                                        // where the process and token security descriptors are created without an ACE for the current user, 
                                        // owned by the wrong user and with a High-IL when the process token is Medium-IL 
                                        // preventing the new process from accessing user/system resources above Low-IL. (dmex)

                                        if (processSecurityDescriptor)
                                        {
                                            PhSetObjectSecurity(
                                                newProcessHandle,
                                                OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
                                                processSecurityDescriptor
                                                );
                                        }

                                        if (tokenSecurityDescriptor && NT_SUCCESS(PhOpenProcessToken(
                                            newProcessHandle,
                                            WRITE_DAC | WRITE_OWNER,
                                            &tokenHandle
                                            )))
                                        {
                                            PhSetObjectSecurity(
                                                tokenHandle,
                                                OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
                                                tokenSecurityDescriptor
                                                );
                                            NtClose(tokenHandle);
                                        }
                                    }

                                    if (NT_SUCCESS(PhGetProcessBasicInformation(newProcessHandle, &basicInfo)))
                                    {
                                        AllowSetForegroundWindow(ASFW_ANY); // HandleToUlong(basicInfo.UniqueProcessId));
                                    }

                                    NtResumeProcess(newProcessHandle);
                                    NtClose(newProcessHandle);
                                }

                            CleanupExit:

                                if (environment && DestroyEnvironmentBlock_Import())
                                {
                                    DestroyEnvironmentBlock_Import()(environment);
                                }

                                if (tokenSecurityDescriptor)
                                {
                                    PhFree(tokenSecurityDescriptor);
                                }

                                if (processSecurityDescriptor)
                                {
                                    PhFree(processSecurityDescriptor);
                                }

                                if (startupInfo.lpAttributeList)
                                {
                                    DeleteProcThreadAttributeList(startupInfo.lpAttributeList);
                                    PhFree(startupInfo.lpAttributeList);
                                }

                                if (processHandle)
                                {
                                    NtClose(processHandle);
                                }
                            }
                            else
                            {
                                status = PhExecuteRunAsCommand3(
                                    hwndDlg,
                                    PhGetString(program),
                                    PhGetString(username),
                                    PhGetStringOrEmpty(password),
                                    logonType,
                                    context->ProcessId,
                                    sessionId,
                                    PhGetString(desktopName),
                                    useLinkedToken,
                                    createSuspended
                                    );
                            }
                        }
                    }
                    else
                    {
                        status = STATUS_INVALID_PARAMETER;
                    }

                    if (password)
                    {
                        RtlSecureZeroMemory(password->Buffer, password->Length);
                        PhDereferenceObject(password);
                    }

                    if (!NT_SUCCESS(status))
                    {
                        if (status != STATUS_CANCELLED)
                            PhShowStatus(hwndDlg, L"Unable to start the program.", status, 0);
                    }
                    else if (status != STATUS_TIMEOUT)
                    {
                        PhpAddRunMRUListEntry(program);

                        //PhSetStringSetting2(L"RunAsProgram", &program->sr);
                        PhSetStringSetting2(L"RunAsUserName", &username->sr);
                        EndDialog(hwndDlg, IDOK);
                    }
                }
                break;
            case IDC_BROWSE:
                {
                    static PH_FILETYPE_FILTER filters[] =
                    {
                        { L"Programs (*.exe;*.pif;*.com;*.bat)", L"*.exe;*.pif;*.com;*.bat" },
                        { L"All files (*.*)", L"*.*" }
                    };
                    PVOID fileDialog;

                    fileDialog = PhCreateOpenFileDialog();
                    PhSetFileDialogFilter(fileDialog, filters, sizeof(filters) / sizeof(PH_FILETYPE_FILTER));
                    PhSetFileDialogFileName(fileDialog, PH_AUTO_T(PH_STRING, PhGetWindowText(context->ProgramComboBoxWindowHandle))->Buffer);

                    if (PhShowFileDialog(hwndDlg, fileDialog))
                    {
                        PPH_STRING fileName;

                        fileName = PhGetFileDialogFileName(fileDialog);
                        PhSetWindowText(context->ProgramComboBoxWindowHandle, fileName->Buffer);
                        PhDereferenceObject(fileName);
                    }

                    PhFreeFileDialog(fileDialog);
                }
                break;
            case IDC_USERNAME:
                {
                    PPH_STRING username = NULL;

                    if (!context->ProcessId && GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
                    {
                        username = PH_AUTO(PhGetComboBoxString(context->UserComboBoxWindowHandle, -1));
                    }
                    else if (!context->ProcessId && (
                        GET_WM_COMMAND_CMD(wParam, lParam) == CBN_EDITCHANGE ||
                        GET_WM_COMMAND_CMD(wParam, lParam) == CBN_CLOSEUP
                        ))
                    {
                        username = PH_AUTO(PhGetWindowText(context->UserComboBoxWindowHandle));
                    }

                    if (username)
                    {
                        if (IsServiceAccount(username))
                        {
                            EnableWindow(context->PasswordEditWindowHandle, FALSE);
                            PhSelectComboBoxString(context->TypeComboBoxWindowHandle, L"Service", FALSE);
                        }
                        else
                        {
                            EnableWindow(context->PasswordEditWindowHandle, TRUE);
                            PhSelectComboBoxString(context->TypeComboBoxWindowHandle, L"Interactive", FALSE);
                        }
                    }
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}

/**
 * Sets the access control lists of the current window station
 * and desktop to allow all access.
 */
VOID PhSetDesktopWinStaAccess(
    VOID
    )
{
    static SID_IDENTIFIER_AUTHORITY appPackageAuthority = SECURITY_APP_PACKAGE_AUTHORITY;

    HWINSTA wsHandle;
    HDESK desktopHandle;
    ULONG allocationLength;
    PSECURITY_DESCRIPTOR securityDescriptor;
    PACL dacl;
    CHAR allAppPackagesSidBuffer[FIELD_OFFSET(SID, SubAuthority) + sizeof(ULONG) * 2];
    PSID allAppPackagesSid;

    // TODO: Set security on the correct window station and desktop.

    allAppPackagesSid = (PISID)allAppPackagesSidBuffer;
    RtlInitializeSid(allAppPackagesSid, &appPackageAuthority, SECURITY_BUILTIN_APP_PACKAGE_RID_COUNT);
    *RtlSubAuthoritySid(allAppPackagesSid, 0) = SECURITY_APP_PACKAGE_BASE_RID;
    *RtlSubAuthoritySid(allAppPackagesSid, 1) = SECURITY_BUILTIN_PACKAGE_ANY_PACKAGE;

    // We create a DACL that allows everyone to access everything.

    allocationLength = SECURITY_DESCRIPTOR_MIN_LENGTH +
        (ULONG)sizeof(ACL) +
        (ULONG)sizeof(ACCESS_ALLOWED_ACE) +
        RtlLengthSid(&PhSeEveryoneSid) +
        (ULONG)sizeof(ACCESS_ALLOWED_ACE) +
        RtlLengthSid(allAppPackagesSid);
    securityDescriptor = PhAllocate(allocationLength);
    dacl = PTR_ADD_OFFSET(securityDescriptor, SECURITY_DESCRIPTOR_MIN_LENGTH);

    RtlCreateSecurityDescriptor(securityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    RtlCreateAcl(dacl, allocationLength - SECURITY_DESCRIPTOR_MIN_LENGTH, ACL_REVISION);
    RtlAddAccessAllowedAce(dacl, ACL_REVISION, GENERIC_ALL, &PhSeEveryoneSid);

    if (WindowsVersion >= WINDOWS_8)
    {
        RtlAddAccessAllowedAce(dacl, ACL_REVISION, GENERIC_ALL, allAppPackagesSid);
    }

    RtlSetDaclSecurityDescriptor(securityDescriptor, TRUE, dacl, FALSE);

    if (wsHandle = OpenWindowStation(
        L"WinSta0",
        FALSE,
        WRITE_DAC
        ))
    {
        PhSetObjectSecurity(wsHandle, DACL_SECURITY_INFORMATION, securityDescriptor);
        CloseWindowStation(wsHandle);
    }

    if (desktopHandle = OpenDesktop(
        L"Default",
        0,
        FALSE,
        WRITE_DAC | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS
        ))
    {
        PhSetObjectSecurity(desktopHandle, DACL_SECURITY_INFORMATION, securityDescriptor);
        CloseDesktop(desktopHandle);
    }

    PhFree(securityDescriptor);
}

/**
 * Executes the run-as service.
 *
 * \param Parameters The run-as parameters.
 *
 * \remarks This function requires administrator-level access.
 */
NTSTATUS PhExecuteRunAsCommand(
    _In_ PPH_RUNAS_SERVICE_PARAMETERS Parameters
    )
{
    NTSTATUS status;
    ULONG win32Result;
    PPH_STRING applicationFileName;
    PPH_STRING commandLine;
    SC_HANDLE scManagerHandle;
    SC_HANDLE serviceHandle;
    PPH_STRING portName;
    UNICODE_STRING portNameUs;
    ULONG attempts;

    if (!(scManagerHandle = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE)))
        return PhGetLastWin32ErrorAsNtStatus();

    if (!(applicationFileName = PhGetApplicationFileName()))
        return STATUS_FAIL_CHECK;

    commandLine = PhFormatString(L"\"%s\" -ras \"%s\"", applicationFileName->Buffer, Parameters->ServiceName);

    serviceHandle = CreateService(
        scManagerHandle,
        Parameters->ServiceName,
        Parameters->ServiceName,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_IGNORE,
        commandLine->Buffer,
        NULL,
        NULL,
        NULL,
        L"LocalSystem",
        L""
        );
    win32Result = GetLastError();

    PhDereferenceObject(commandLine);
    PhDereferenceObject(applicationFileName);

    CloseServiceHandle(scManagerHandle);

    if (!serviceHandle)
    {
        return NTSTATUS_FROM_WIN32(win32Result);
    }

    PhSetDesktopWinStaAccess();

    StartService(serviceHandle, 0, NULL);
    DeleteService(serviceHandle);

    portName = PhConcatStrings2(L"\\BaseNamedObjects\\", Parameters->ServiceName);
    PhStringRefToUnicodeString(&portName->sr, &portNameUs);
    attempts = 50;

    // Try to connect several times because the server may take
    // a while to initialize.
    do
    {
        status = PhSvcConnectToServer(&portNameUs, 0);

        if (NT_SUCCESS(status))
            break;

        PhDelayExecution(100);

    } while (--attempts != 0);

    PhDereferenceObject(portName);

    if (NT_SUCCESS(status))
    {
        status = PhSvcCallInvokeRunAsService(Parameters);
        PhSvcDisconnectFromServer();
    }

    if (serviceHandle)
        CloseServiceHandle(serviceHandle);

    return status;
}

/**
 * Starts a program as another user.
 *
 * \param hWnd A handle to the parent window.
 * \param Program The command line of the program to start.
 * \param UserName The user to start the program as. The user
 * name should be specified as: domain\\name. This parameter
 * can be NULL if \a ProcessIdWithToken is specified.
 * \param Password The password for the specified user. If there
 * is no password, specify an empty string. This parameter
 * can be NULL if \a ProcessIdWithToken is specified.
 * \param LogonType The logon type for the specified user. This
 * parameter can be 0 if \a ProcessIdWithToken is specified.
 * \param ProcessIdWithToken The ID of a process from which
 * to duplicate the token.
 * \param SessionId The ID of the session to run the program
 * under.
 * \param DesktopName The window station and desktop to run the
 * program under.
 * \param UseLinkedToken Uses the linked token if possible.
 *
 * \retval STATUS_CANCELLED The user cancelled the operation.
 *
 * \remarks This function will cause another instance of
 * Process Hacker to be executed if the current security context
 * does not have sufficient system access. This is done
 * through a UAC elevation prompt.
 */
NTSTATUS PhExecuteRunAsCommand2(
    _In_ HWND hWnd,
    _In_ PWSTR Program,
    _In_opt_ PWSTR UserName,
    _In_opt_ PWSTR Password,
    _In_opt_ ULONG LogonType,
    _In_opt_ HANDLE ProcessIdWithToken,
    _In_ ULONG SessionId,
    _In_ PWSTR DesktopName,
    _In_ BOOLEAN UseLinkedToken
    )
{
    return PhExecuteRunAsCommand3(hWnd, Program, UserName, Password, LogonType, ProcessIdWithToken, SessionId, DesktopName, UseLinkedToken, FALSE);
}

NTSTATUS PhExecuteRunAsCommand3(
    _In_ HWND hWnd,
    _In_ PWSTR Program,
    _In_opt_ PWSTR UserName,
    _In_opt_ PWSTR Password,
    _In_opt_ ULONG LogonType,
    _In_opt_ HANDLE ProcessIdWithToken,
    _In_ ULONG SessionId,
    _In_ PWSTR DesktopName,
    _In_ BOOLEAN UseLinkedToken,
    _In_ BOOLEAN CreateSuspendedProcess
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    PH_RUNAS_SERVICE_PARAMETERS parameters;
    WCHAR serviceName[32];
    PPH_STRING portName;
    UNICODE_STRING portNameUs;

    memset(&parameters, 0, sizeof(PH_RUNAS_SERVICE_PARAMETERS));
    parameters.ProcessId = HandleToUlong(ProcessIdWithToken);
    parameters.UserName = UserName;
    parameters.Password = Password;
    parameters.LogonType = LogonType;
    parameters.SessionId = SessionId;
    parameters.CommandLine = Program;
    parameters.DesktopName = DesktopName;
    parameters.UseLinkedToken = UseLinkedToken;
    parameters.CreateSuspendedProcess = CreateSuspendedProcess;

    // Try to use an existing instance of the service if possible.
    if (RunAsOldServiceName[0] != UNICODE_NULL)
    {
        PhAcquireQueuedLockExclusive(&RunAsOldServiceLock);

        portName = PhConcatStrings2(L"\\BaseNamedObjects\\", RunAsOldServiceName);
        PhStringRefToUnicodeString(&portName->sr, &portNameUs);

        if (NT_SUCCESS(PhSvcConnectToServer(&portNameUs, 0)))
        {
            parameters.ServiceName = RunAsOldServiceName;
            status = PhSvcCallInvokeRunAsService(&parameters);
            PhSvcDisconnectFromServer();

            PhDereferenceObject(portName);
            PhReleaseQueuedLockExclusive(&RunAsOldServiceLock);

            return status;
        }

        PhDereferenceObject(portName);
        PhReleaseQueuedLockExclusive(&RunAsOldServiceLock);
    }

    // An existing instance was not available. Proceed normally.

    memcpy(serviceName, L"ProcessHacker", 13 * sizeof(WCHAR));
    PhGenerateRandomAlphaString(&serviceName[13], 16);
    PhAcquireQueuedLockExclusive(&RunAsOldServiceLock);
    memcpy(RunAsOldServiceName, serviceName, sizeof(serviceName));
    PhReleaseQueuedLockExclusive(&RunAsOldServiceLock);

    parameters.ServiceName = serviceName;

    if (PhGetOwnTokenAttributes().Elevated)
    {
        status = PhExecuteRunAsCommand(&parameters);
    }
    else
    {
        if (PhUiConnectToPhSvc(hWnd, FALSE))
        {
            status = PhSvcCallExecuteRunAsCommand(&parameters);
            PhUiDisconnectFromPhSvc();
        }
        else
        {
            status = STATUS_CANCELLED;
        }
    }

    return status;
}

static VOID PhpSplitUserName(
    _In_ PWSTR UserName,
    _Out_opt_ PPH_STRING *DomainPart,
    _Out_opt_ PPH_STRING *UserPart
    )
{
    PH_STRINGREF userName;
    PH_STRINGREF domainPart;
    PH_STRINGREF userPart;

    PhInitializeStringRefLongHint(&userName, UserName);

    if (PhSplitStringRefAtChar(&userName, OBJ_NAME_PATH_SEPARATOR, &domainPart, &userPart))
    {
        if (DomainPart)
            *DomainPart = PhCreateString2(&domainPart);
        if (UserPart)
            *UserPart = PhCreateString2(&userPart);
    }
    else
    {
        if (DomainPart)
            *DomainPart = NULL;
        if (UserPart)
            *UserPart = PhCreateString2(&userName);
    }
}

static VOID SetRunAsServiceStatus(
    _In_ ULONG State
    )
{
    SERVICE_STATUS status;

    memset(&status, 0, sizeof(SERVICE_STATUS));
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = State;
    status.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    SetServiceStatus(RunAsServiceStatusHandle, &status);
}

static ULONG WINAPI RunAsServiceHandlerEx(
    _In_ ULONG dwControl,
    _In_ ULONG dwEventType,
    _In_ PVOID lpEventData,
    _In_ PVOID lpContext
    )
{
    switch (dwControl)
    {
    case SERVICE_CONTROL_STOP:
        PhSvcStop(&RunAsServiceStop);
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    }

    return ERROR_CALL_NOT_IMPLEMENTED;
}

static VOID WINAPI RunAsServiceMain(
    _In_ ULONG dwArgc,
    _In_ PWSTR *lpszArgv
    )
{
    PPH_STRING portName;

    memset(&RunAsServiceStop, 0, sizeof(PHSVC_STOP));

    RunAsServiceStatusHandle = RegisterServiceCtrlHandlerEx(RunAsServiceName->Buffer, RunAsServiceHandlerEx, NULL);
    SetRunAsServiceStatus(SERVICE_RUNNING);

    portName = PhConcatStrings2(
        L"\\BaseNamedObjects\\", 
        RunAsServiceName->Buffer
        );

    PhSvcMain(portName, &RunAsServiceStop);

    SetRunAsServiceStatus(SERVICE_STOPPED);
}

NTSTATUS PhRunAsServiceStart(
    _In_ PPH_STRING ServiceName
    )
{
    HANDLE tokenHandle;
    SERVICE_TABLE_ENTRY entry;

    // Enable some required privileges.

    if (NT_SUCCESS(PhOpenProcessToken(
        NtCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES,
        &tokenHandle
        )))
    {
        PhSetTokenPrivilege2(tokenHandle, SE_ASSIGNPRIMARYTOKEN_PRIVILEGE, SE_PRIVILEGE_ENABLED);
        PhSetTokenPrivilege2(tokenHandle, SE_INCREASE_QUOTA_PRIVILEGE, SE_PRIVILEGE_ENABLED);
        PhSetTokenPrivilege2(tokenHandle, SE_BACKUP_PRIVILEGE, SE_PRIVILEGE_ENABLED);
        PhSetTokenPrivilege2(tokenHandle, SE_RESTORE_PRIVILEGE, SE_PRIVILEGE_ENABLED);
        PhSetTokenPrivilege2(tokenHandle, SE_IMPERSONATE_PRIVILEGE, SE_PRIVILEGE_ENABLED);
        NtClose(tokenHandle);
    }

    RunAsServiceName = ServiceName;

    entry.lpServiceName = ServiceName->Buffer;
    entry.lpServiceProc = RunAsServiceMain;

    StartServiceCtrlDispatcher(&entry);

    return STATUS_SUCCESS;
}

NTSTATUS PhInvokeRunAsService(
    _In_ PPH_RUNAS_SERVICE_PARAMETERS Parameters
    )
{
    NTSTATUS status;
    PPH_STRING domainName;
    PPH_STRING userName;
    PH_CREATE_PROCESS_AS_USER_INFO createInfo;
    ULONG flags;

    if (Parameters->UserName)
    {
        PhpSplitUserName(Parameters->UserName, &domainName, &userName);
    }
    else
    {
        domainName = NULL;
        userName = NULL;
    }

    memset(&createInfo, 0, sizeof(PH_CREATE_PROCESS_AS_USER_INFO));
    createInfo.ApplicationName = Parameters->FileName;
    createInfo.CommandLine = Parameters->CommandLine;
    createInfo.CurrentDirectory = Parameters->CurrentDirectory;
    createInfo.DomainName = PhGetString(domainName);
    createInfo.UserName = PhGetString(userName);
    createInfo.Password = Parameters->Password;
    createInfo.LogonType = Parameters->LogonType;
    createInfo.SessionId = Parameters->SessionId;
    createInfo.DesktopName = Parameters->DesktopName;

    flags = PH_CREATE_PROCESS_SET_SESSION_ID;

    if (Parameters->ProcessId)
    {
        createInfo.ProcessIdWithToken = UlongToHandle(Parameters->ProcessId);
        flags |= PH_CREATE_PROCESS_USE_PROCESS_TOKEN;
    }

    if (Parameters->UseLinkedToken)
        flags |= PH_CREATE_PROCESS_USE_LINKED_TOKEN;
    if (Parameters->CreateSuspendedProcess)
        flags |= PH_CREATE_PROCESS_SUSPENDED;

    status = PhCreateProcessAsUser(
        &createInfo,
        flags,
        NULL,
        NULL,
        NULL
        );

    if (domainName) PhDereferenceObject(domainName);
    if (userName) PhDereferenceObject(userName);

    return status;
}

typedef struct _PHP_RUNFILEDLG
{
    HWND WindowHandle;
    HWND ComboBoxHandle;
    HWND RunAsCheckboxHandle;
    HWND RunAsInstallerCheckboxHandle;
    HIMAGELIST ImageListHandle;
    BOOLEAN RunAsInstallerCheckboxDisabled;
} PHP_RUNFILEDLG, *PPHP_RUNFILEDLG;

PPH_STRING PhpQueryRunFileParentDirectory(
    _In_ BOOLEAN Elevated
    )
{
    // Note: Explorer creates new processes with the parent directory as SystemRoot when elevated or
    // the below environment variables when not elevated. (dmex)
    if (!Elevated)
    {
        static PH_STRINGREF homeDriveNameSr = PH_STRINGREF_INIT(L"HOMEDRIVE");
        static PH_STRINGREF homePathNameSr = PH_STRINGREF_INIT(L"HOMEPATH");
        PPH_STRING parentDirectoryString = NULL;
        PPH_STRING homeDriveNameString = NULL;
        PPH_STRING homePathNameString = NULL;

        PhQueryEnvironmentVariable(NULL, &homeDriveNameSr, &homeDriveNameString);
        PhQueryEnvironmentVariable(NULL, &homePathNameSr, &homePathNameString);

        if (homeDriveNameString && homePathNameString)
        {
            parentDirectoryString = PhConcatStringRef2(
                &homeDriveNameString->sr,
                &homePathNameString->sr
                );
        }

        if (homeDriveNameString)
            PhDereferenceObject(homeDriveNameString);
        if (homePathNameString)
            PhDereferenceObject(homePathNameString);

        return parentDirectoryString;
    }
    else
    {
        return PhGetSystemDirectory();
    }
}

NTSTATUS PhpCustomShellExecute(
    _In_ HWND hWnd,
    _In_ PWSTR FileName,
    _In_opt_ PWSTR Parameters,
    _In_ BOOLEAN Elevated
    )
{
    NTSTATUS status;
    PPH_STRING parentDirectory = NULL;
    SHELLEXECUTEINFO info;

    parentDirectory = PhpQueryRunFileParentDirectory(Elevated);

    memset(&info, 0, sizeof(SHELLEXECUTEINFO));
    info.cbSize = sizeof(SHELLEXECUTEINFO);
    info.lpFile = FileName;
    info.lpParameters = Parameters;
    info.lpDirectory = PhGetString(parentDirectory);
    info.fMask = SEE_MASK_FLAG_NO_UI;
    info.nShow = SW_SHOWNORMAL;
    info.hwnd = hWnd;

    if (Elevated)
        info.lpVerb = L"runas";

    if (ShellExecuteEx(&info))
    {
        if (info.hProcess)
            NtClose(info.hProcess);

        status = STATUS_SUCCESS;
    }
    else
    {
        status = PhGetLastWin32ErrorAsNtStatus();
    }

    if (parentDirectory)
        PhDereferenceObject(parentDirectory);

    return status;
}

BOOLEAN PhpRunFileAsInteractiveUser(
    _In_ PPHP_RUNFILEDLG Context,
    _In_ PPH_STRING Command
    )
{
    ULONG (WINAPI *WdcRunTaskAsInteractiveUser_I)(
        _In_ PWSTR CommandLine,
        _In_ PWSTR CurrentDirectory,
        _In_ ULONG Reserved
        ) = NULL;
    BOOLEAN success = FALSE;
    PVOID wdcLibraryHandle;
    PPH_STRING executeString = NULL;
    INT cmdlineArgCount;
    PWSTR* cmdlineArgList;

    if (!(wdcLibraryHandle = LoadLibrary(L"wdc.dll")))
        return FALSE;

    if (!(WdcRunTaskAsInteractiveUser_I = PhGetDllBaseProcedureAddress(wdcLibraryHandle, "WdcRunTaskAsInteractiveUser", 0)))
    {
        FreeLibrary(wdcLibraryHandle);
        return FALSE;
    }

    // Extract the filename.
    if (cmdlineArgList = CommandLineToArgvW(Command->Buffer, &cmdlineArgCount))
    {
        PPH_STRING fileName = PhCreateString(cmdlineArgList[0]);

        if (fileName && !PhDoesFileExistsWin32(fileName->Buffer))
        {
            PPH_STRING filePathString;

            // The user typed a name without a path so attempt to locate the executable.
            if (filePathString = PhSearchFilePath(fileName->Buffer, L".exe"))
                PhMoveReference(&fileName, filePathString);
            else
                PhClearReference(&fileName);
        }

        if (fileName)
        {
            // Escape the filename.
            PhMoveReference(&fileName, PhConcatStrings(3, L"\"", fileName->Buffer, L"\""));

            if (cmdlineArgCount == 2)
            {
                PPH_STRING fileArgs = PhCreateString(cmdlineArgList[1]);

                // Escape the parameters.
                PhMoveReference(&fileArgs, PhConcatStrings(3, L"\"", fileArgs->Buffer, L"\""));

                // Create the escaped execute string.
                PhMoveReference(&executeString, PhConcatStrings(3, fileName->Buffer, L" ", fileArgs->Buffer));

                // Cleanup.
                PhDereferenceObject(fileArgs);
            }
            else
            {
                // Create the escaped execute string.
                executeString = PhReferenceObject(fileName);
            }

            PhDereferenceObject(fileName);
        }

        LocalFree(cmdlineArgList);
    }

    if (!PhIsNullOrEmptyString(executeString))
    {
        PPH_STRING parentDirectory = PhpQueryRunFileParentDirectory(FALSE);

        if (WdcRunTaskAsInteractiveUser_I(PhGetString(executeString), PhGetString(parentDirectory), 0) == 0)
        {
            success = TRUE;
        }

        if (parentDirectory)
        {
            PhDereferenceObject(parentDirectory);
        }
    }

    if (executeString) PhDereferenceObject(executeString);
    FreeLibrary(wdcLibraryHandle);

    return success;
}

NTSTATUS PhpRunFileProgram(
    _In_ PPHP_RUNFILEDLG Context,
    _In_ PPH_STRING Command
    )
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    PPH_STRING commandString = NULL;
    PPH_STRING fullFileName = NULL;
    PPH_STRING argumentsString = NULL;
    PH_STRINGREF fileName;
    PH_STRINGREF arguments;
    FILE_BASIC_INFORMATION basicInfo;
    BOOLEAN isDirectory = FALSE;

    if (PhIsNullOrEmptyString(Command))
        return STATUS_UNSUCCESSFUL;

    if (!(commandString = PhExpandEnvironmentStrings(&Command->sr)))
        commandString = PhCreateString2(&Command->sr);

    PhParseCommandLineFuzzy(&commandString->sr, &fileName, &arguments, &fullFileName);

    if (PhIsNullOrEmptyString(fullFileName))
        PhMoveReference(&fullFileName, PhCreateString2(&fileName));

    if (PhIsNullOrEmptyString(fullFileName))
    {
        if (fullFileName)
            PhDereferenceObject(fullFileName);

        return STATUS_UNSUCCESSFUL;
    }

    if (arguments.Length)
    {
        argumentsString = PhCreateString2(&arguments);
    }

    if (NT_SUCCESS(PhQueryAttributesFileWin32(fullFileName->Buffer, &basicInfo)))
    {
        isDirectory = !!(basicInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    // If the file doesn't exist its probably a URL with http, https, www (dmex)
    if (isDirectory || !PhDoesFileExistsWin32(fullFileName->Buffer))
    {
        status = PhpCustomShellExecute(
            Context->WindowHandle,
            commandString->Buffer,
            NULL,
            FALSE
            );
    }
    else if (Button_GetCheck(Context->RunAsCheckboxHandle) == BST_CHECKED ||
        // The explorer runas dialog executes programs as administrator when holding ctrl/shift keys 
        // and clicking the OK button, so we'll implement the same functionality. (dmex)
        (!!(GetKeyState(VK_CONTROL) < 0 && !!(GetKeyState(VK_SHIFT) < 0))))
    {
        status = PhpCustomShellExecute(
            Context->WindowHandle,
            commandString->Buffer,
            NULL,
            TRUE
            );
    }
    else
    {
        ULONG processId = ULONG_MAX;
        PPH_STRING parentDirectory = NULL;
        HANDLE processHandle = NULL;
        HANDLE newProcessHandle;
        HANDLE tokenHandle;
        HWND shellWindow;
        STARTUPINFOEX startupInfo;
        SIZE_T attributeListLength = 0;
        PSECURITY_DESCRIPTOR processSecurityDescriptor = NULL;
        PSECURITY_DESCRIPTOR tokenSecurityDescriptor = NULL;
        PVOID environment = NULL;
        ULONG flags = 0;

        memset(&startupInfo, 0, sizeof(STARTUPINFOEX));
        startupInfo.StartupInfo.cb = sizeof(STARTUPINFOEX);
        startupInfo.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
        startupInfo.StartupInfo.wShowWindow = SW_SHOWNORMAL;
        parentDirectory = PhpQueryRunFileParentDirectory(FALSE);

        if (!(shellWindow = GetShellWindow()))
        {
            if (PhpRunFileAsInteractiveUser(Context, commandString))
                status = STATUS_SUCCESS;

            goto CleanupExit;
        }

        GetWindowThreadProcessId(shellWindow, &processId);

        if (processId == ULONG_MAX)
        {
            status = STATUS_UNSUCCESSFUL;
            goto CleanupExit;
        }

        status = PhOpenProcess(
            &processHandle,
            PROCESS_CREATE_PROCESS | (PhGetOwnTokenAttributes().Elevated ? PROCESS_QUERY_LIMITED_INFORMATION | READ_CONTROL : 0),
            UlongToHandle(processId)
            );

        if (!NT_SUCCESS(status))
            goto CleanupExit;

        if (!InitializeProcThreadAttributeList(NULL, 1, 0, &attributeListLength) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            status = PhGetLastWin32ErrorAsNtStatus();
            goto CleanupExit;
        }

        startupInfo.lpAttributeList = PhAllocate(attributeListLength);

        if (!InitializeProcThreadAttributeList(startupInfo.lpAttributeList, 1, 0, &attributeListLength))
        {
            status = PhGetLastWin32ErrorAsNtStatus();
            goto CleanupExit;
        }

        if (!UpdateProcThreadAttribute(startupInfo.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &processHandle, sizeof(HANDLE), NULL, NULL))
        {
            status = PhGetLastWin32ErrorAsNtStatus();
            goto CleanupExit;
        }

        if (PhGetOwnTokenAttributes().Elevated)
        {
            PhGetObjectSecurity(
                processHandle,
                OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
                &processSecurityDescriptor
                );
        }

        if (NT_SUCCESS(PhOpenProcessToken(
            processHandle,
            TOKEN_QUERY | (PhGetOwnTokenAttributes().Elevated ? READ_CONTROL : 0),
            &tokenHandle
            )))
        {
            if (PhGetOwnTokenAttributes().Elevated)
            {
                PhGetObjectSecurity(
                    tokenHandle,
                    OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
                    &tokenSecurityDescriptor
                    );
            }

            if (CreateEnvironmentBlock_Import() && CreateEnvironmentBlock_Import()(&environment, tokenHandle, FALSE))
            {
                flags |= PH_CREATE_PROCESS_UNICODE_ENVIRONMENT;
            }

            NtClose(tokenHandle);
        }

        status = PhCreateProcessWin32Ex(
            fullFileName->Buffer,
            PhGetString(argumentsString),
            environment,
            PhGetString(parentDirectory),
            &startupInfo.StartupInfo,
            PH_CREATE_PROCESS_SUSPENDED | PH_CREATE_PROCESS_NEW_CONSOLE | PH_CREATE_PROCESS_EXTENDED_STARTUPINFO | flags,
            NULL,
            NULL,
            &newProcessHandle,
            NULL
            );

        if (NT_SUCCESS(status))
        {
            PROCESS_BASIC_INFORMATION basicInfo;

            if (PhGetOwnTokenAttributes().Elevated)
            {
                // Note: This is needed to workaround a severe bug with PROC_THREAD_ATTRIBUTE_PARENT_PROCESS 
                // where the process and token security descriptors are created without an ACE for the current user, 
                // owned by the wrong user and with a High-IL when the process token is Medium-IL 
                // preventing the new process from accessing user/system resources above Low-IL. (dmex)

                if (processSecurityDescriptor)
                {
                    PhSetObjectSecurity(
                        newProcessHandle,
                        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
                        processSecurityDescriptor
                        );
                }

                if (tokenSecurityDescriptor && NT_SUCCESS(PhOpenProcessToken(
                    newProcessHandle,
                    WRITE_DAC | WRITE_OWNER,
                    &tokenHandle
                    )))
                {
                    PhSetObjectSecurity(
                        tokenHandle,
                        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
                        tokenSecurityDescriptor
                        );
                    NtClose(tokenHandle);
                }
            }

            if (NT_SUCCESS(PhGetProcessBasicInformation(newProcessHandle, &basicInfo)))
            {
                AllowSetForegroundWindow(ASFW_ANY);// HandleToUlong(basicInfo.UniqueProcessId));
            }

            NtResumeProcess(newProcessHandle);

            NtClose(newProcessHandle);
        }
        else if (WIN32_FROM_NTSTATUS(status) == ERROR_ELEVATION_REQUIRED)
        {
            status = PhpCustomShellExecute(
                Context->WindowHandle,
                commandString->Buffer,
                NULL,
                TRUE
                );
        }

    CleanupExit:

        if (environment && DestroyEnvironmentBlock_Import())
        {
            DestroyEnvironmentBlock_Import()(environment);
        }

        if (tokenSecurityDescriptor)
        {
            PhFree(tokenSecurityDescriptor);
        }

        if (processSecurityDescriptor)
        {
            PhFree(processSecurityDescriptor);
        }

        if (startupInfo.lpAttributeList)
        {
            DeleteProcThreadAttributeList(startupInfo.lpAttributeList);
            PhFree(startupInfo.lpAttributeList);
        }

        if (processHandle)
        {
            NtClose(processHandle);
        }

        if (parentDirectory)
        {
            PhDereferenceObject(parentDirectory);
        }
    }

    if (fullFileName) PhDereferenceObject(fullFileName);
    if (argumentsString) PhDereferenceObject(argumentsString);
    if (commandString) PhDereferenceObject(commandString);

    return status;
}

NTSTATUS RunAsCreateProcessThread(
    _In_ PVOID Parameter
    )
{
    PPH_STRING command = Parameter;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    SERVICE_STATUS_PROCESS serviceStatus = { 0 };
    SC_HANDLE serviceHandle = NULL;
    HANDLE processHandle = NULL;
    STARTUPINFOEX startupInfo;
    SIZE_T attributeListLength = 0;
    PPH_STRING commandLine = NULL;
    ULONG bytesNeeded = 0;
    PPH_STRING filePathString;

    if (filePathString = PhSearchFilePath(command->Buffer, L".exe"))
        PhMoveReference(&commandLine, filePathString);
    else
        commandLine = command; // HACK (dmex)

    memset(&startupInfo, 0, sizeof(STARTUPINFOEX));
    startupInfo.StartupInfo.cb = sizeof(STARTUPINFOEX);
    startupInfo.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.StartupInfo.wShowWindow = SW_SHOWNORMAL;

    if (!(serviceHandle = PhOpenService(L"TrustedInstaller", SERVICE_QUERY_STATUS | SERVICE_START)))
    {
        status = PhGetLastWin32ErrorAsNtStatus();
        goto CleanupExit;
    }

    if (!QueryServiceStatusEx(
        serviceHandle,
        SC_STATUS_PROCESS_INFO,
        (PBYTE)&serviceStatus,
        sizeof(SERVICE_STATUS_PROCESS),
        &bytesNeeded
        ))
    {
        status = PhGetLastWin32ErrorAsNtStatus();
        goto CleanupExit;
    }

    if (serviceStatus.dwCurrentState == SERVICE_RUNNING)
    {
        status = STATUS_SUCCESS;
    }
    else
    {
        ULONG attempts = 10;

        StartService(serviceHandle, 0, NULL);

        do
        {
            if (QueryServiceStatusEx(
                serviceHandle,
                SC_STATUS_PROCESS_INFO,
                (PBYTE)&serviceStatus,
                sizeof(SERVICE_STATUS_PROCESS),
                &bytesNeeded
                ))
            {
                if (serviceStatus.dwCurrentState == SERVICE_RUNNING)
                {
                    status = STATUS_SUCCESS;
                    break;
                }
            }

            PhDelayExecution(1000);

        } while (--attempts != 0);
    }

    if (!NT_SUCCESS(status))
    {
        status = STATUS_SERVICES_FAILED_AUTOSTART;
        goto CleanupExit;
    }

    if (!NT_SUCCESS(status = PhOpenProcess(&processHandle, PROCESS_CREATE_PROCESS, UlongToHandle(serviceStatus.dwProcessId))))
        goto CleanupExit;


    if (!InitializeProcThreadAttributeList(NULL, 1, 0, &attributeListLength) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        status = PhGetLastWin32ErrorAsNtStatus();
        goto CleanupExit;
    }

    startupInfo.lpAttributeList = PhAllocate(attributeListLength);

    if (!InitializeProcThreadAttributeList(startupInfo.lpAttributeList, 1, 0, &attributeListLength))
    {
        status = PhGetLastWin32ErrorAsNtStatus();
        goto CleanupExit;
    }

    if (!UpdateProcThreadAttribute(startupInfo.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &processHandle, sizeof(HANDLE), NULL, NULL))
    {
        status = PhGetLastWin32ErrorAsNtStatus();
        goto CleanupExit;
    }

    AllowSetForegroundWindow(ASFW_ANY);

    status = PhCreateProcessWin32Ex(
        NULL,
        PhGetString(commandLine),
        NULL,
        NULL,
        &startupInfo.StartupInfo,
        PH_CREATE_PROCESS_NEW_CONSOLE | PH_CREATE_PROCESS_EXTENDED_STARTUPINFO,
        NULL,
        NULL,
        NULL,
        NULL
        );

CleanupExit:

    if (processHandle)
        NtClose(processHandle);

    if (serviceHandle)
        CloseServiceHandle(serviceHandle);

    if (startupInfo.lpAttributeList)
    {
        DeleteProcThreadAttributeList(startupInfo.lpAttributeList);
        PhFree(startupInfo.lpAttributeList);
    }

    if (commandLine)
    {
        PhDereferenceObject(commandLine);
    }

    return status;
}

INT_PTR CALLBACK PhpRunFileWndProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PPHP_RUNFILEDLG context = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        context = PhAllocateZero(sizeof(PHP_RUNFILEDLG));

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

    if (!context)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            SendMessage(hwndDlg, WM_SETICON, ICON_SMALL, (LPARAM)PH_LOAD_SHARED_ICON_SMALL(PhInstanceHandle, MAKEINTRESOURCE(IDI_PROCESSHACKER)));
            SendMessage(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM)PH_LOAD_SHARED_ICON_LARGE(PhInstanceHandle, MAKEINTRESOURCE(IDI_PROCESSHACKER)));

            context->WindowHandle = hwndDlg;
            context->ComboBoxHandle = GetDlgItem(hwndDlg, IDC_PROGRAMCOMBO);
            context->RunAsCheckboxHandle = GetDlgItem(hwndDlg, IDC_TOGGLEELEVATION);
            context->RunAsInstallerCheckboxHandle = GetDlgItem(hwndDlg, IDC_TRUSTEDINSTALLER);

            PhpAddProgramsToComboBox(context->ComboBoxHandle);
            ComboBox_SetCurSel(context->ComboBoxHandle, 0);

            {
                COMBOBOXINFO info = { sizeof(COMBOBOXINFO) };

                if (SendMessage(context->ComboBoxHandle, CB_GETCOMBOBOXINFO, 0, (LPARAM)& info))
                {
                    if (SHAutoComplete && info.hwndItem)
                        SHAutoComplete(info.hwndItem, SHACF_DEFAULT);
                }
            }

            Button_SetCheck(context->RunAsCheckboxHandle, PhGetIntegerSetting(L"RunFileDlgState") ? TRUE : FALSE);

            if (!PhGetOwnTokenAttributes().Elevated)
            {
                HICON shieldIcon;

                Button_Enable(context->RunAsInstallerCheckboxHandle, FALSE);
                context->RunAsInstallerCheckboxDisabled = TRUE;

                if (shieldIcon = PhLoadIcon(
                    NULL,
                    IDI_SHIELD,
                    PH_LOAD_ICON_SIZE_SMALL,
                    PhSmallIconSize.X,
                    PhSmallIconSize.Y
                    ))
                {
                    context->ImageListHandle = ImageList_Create(
                        PhSmallIconSize.X,
                        PhSmallIconSize.Y,
                        ILC_COLOR32,
                        1,
                        1
                        );

                    ImageList_AddIcon(context->ImageListHandle, shieldIcon);
                    DestroyIcon(shieldIcon);
                }
            }
        }
        break;
    case WM_DESTROY:
        {
            PhSetIntegerSetting(L"RunFileDlgState", Button_GetCheck(context->RunAsCheckboxHandle) == BST_CHECKED);

            if (context->ImageListHandle)
                ImageList_Destroy(context->ImageListHandle);

            PhFree(context);
        }
        break;
    case WM_CTLCOLORSTATIC:
        {
            HDC hdc = (HDC)wParam;

            SetBkMode(hdc, TRANSPARENT);

            return (INT_PTR)GetStockBrush(WHITE_BRUSH);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDCANCEL:
                EndDialog(hwndDlg, IDCANCEL);
                break;
            case IDOK:
                {
                    NTSTATUS status;
                    PPH_STRING commandString;

                    if (commandString = PhGetWindowText(context->ComboBoxHandle))
                    {
                        if (Button_GetCheck(context->RunAsInstallerCheckboxHandle) == BST_CHECKED)
                        {
                            status = PhCreateThread2(RunAsCreateProcessThread, commandString);
                        }
                        else
                        {
                            status = PhpRunFileProgram(context, commandString);
                        }

                        if (NT_SUCCESS(status))
                        {
                            PhpAddRunMRUListEntry(commandString);

                            EndDialog(hwndDlg, IDOK);
                        }
                        else
                        {
                            if (!(NT_NTWIN32(status) && WIN32_FROM_NTSTATUS(status) == ERROR_CANCELLED))
                            {
                                PhShowStatus(hwndDlg, L"Unable to execute the command.", status, 0);
                            }
                        }

                        PhDereferenceObject(commandString);
                    }
                }
                break;
            case IDC_BROWSE:
                {
                    PH_FILETYPE_FILTER filters[] =
                    {
                        { L"Executable files (*.exe;*.pif;*.com;*.bat;*.cmd)", L"*.exe;*.pif;*.com;*.bat;*.cmd" },
                        { L"All files (*.*)", L"*.*" }
                    };
                    PVOID fileDialog = PhCreateOpenFileDialog();

                    PhSetFileDialogFilter(fileDialog, filters, RTL_NUMBER_OF(filters));

                    if (PhShowFileDialog(hwndDlg, fileDialog))
                    {
                        PPH_STRING fileName;

                        if (fileName = PhGetFileDialogFileName(fileDialog))
                        {
                            ComboBox_SetText(context->ComboBoxHandle, PhGetString(fileName));
                            PhDereferenceObject(fileName);
                        }
                    }

                    PhFreeFileDialog(fileDialog);
                }
                break;
            }
        }
        break;
    case WM_ERASEBKGND:
        {
            HDC hdc = (HDC)wParam;
            RECT clientRect;

            if (!GetClientRect(hwndDlg, &clientRect))
                break;

            SetBkMode(hdc, TRANSPARENT);

            clientRect.bottom -= PH_SCALE_DPI(60);
            FillRect(hdc, &clientRect, GetSysColorBrush(COLOR_WINDOW));

            clientRect.top = clientRect.bottom;
            clientRect.bottom = clientRect.top + PH_SCALE_DPI(60);
            FillRect(hdc, &clientRect, GetSysColorBrush(COLOR_3DFACE));

            SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, TRUE);
        }
        return TRUE;
    case WM_NOTIFY:
        {
            LPNMHDR data = (LPNMHDR)lParam;

            if (data->hwndFrom != context->RunAsInstallerCheckboxHandle || !context->RunAsInstallerCheckboxDisabled)
                break;

            switch (data->code)
            {
            case NM_CUSTOMDRAW:
                {
                    LPNMCUSTOMDRAW customDraw = (LPNMCUSTOMDRAW)lParam;
                    WCHAR className[MAX_PATH];

                    if (!GetClassName(customDraw->hdr.hwndFrom, className, RTL_NUMBER_OF(className)))
                        className[0] = UNICODE_NULL;

                    if (PhEqualStringZ(className, L"Button", FALSE))
                    {
                        ULONG_PTR buttonStyle = PhGetWindowStyle(customDraw->hdr.hwndFrom);

                        if ((buttonStyle & BS_CHECKBOX) == BS_CHECKBOX)
                        {
                            switch (customDraw->dwDrawStage)
                            {
                            case CDDS_PREPAINT:
                                {
                                    PPH_STRING buttonText;

                                    SetTextColor(customDraw->hdc, RGB(0, 0, 0));
                                    SetDCBrushColor(customDraw->hdc, RGB(0xff, 0xff, 0xff));
                                    FillRect(customDraw->hdc, &customDraw->rc, GetStockBrush(DC_BRUSH));

                                    if (buttonText = PhGetWindowText(customDraw->hdr.hwndFrom))
                                    {
                                        customDraw->rc.left += PhSmallIconSize.X;
                                        DrawText(
                                            customDraw->hdc,
                                            buttonText->Buffer,
                                            (UINT)buttonText->Length / sizeof(WCHAR),
                                            &customDraw->rc,
                                            DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_HIDEPREFIX
                                            );
                                        customDraw->rc.left -= PhSmallIconSize.X;

                                        PhDereferenceObject(buttonText);
                                    }

                                    ImageList_Draw(
                                        context->ImageListHandle,
                                        0,
                                        customDraw->hdc,
                                        customDraw->rc.left,
                                        customDraw->rc.top + 1, // offset
                                        ILD_TRANSPARENT
                                        );

                                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, CDRF_SKIPDEFAULT);
                                    return TRUE;
                                }
                                break;
                            }

                            SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, CDRF_DODEFAULT);
                            return TRUE;
                        }
                    }
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}
