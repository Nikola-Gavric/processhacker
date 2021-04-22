/*
 * Process Hacker -
 *   handle properties
 *
 * Copyright (C) 2010-2013 wj32
 * Copyright (C) 2018-2020 dmex
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
#include <hndlprv.h>
#include <phplug.h>
#include <phsettings.h>

#include <emenu.h>
#include <settings.h>
#include <kphuser.h>
#include <hndlinfo.h>
#include <secedit.h>

typedef enum _PHP_HANDLE_GENERAL_CATEGORY
{
    // common
    PH_HANDLE_GENERAL_CATEGORY_BASICINFO,
    PH_HANDLE_GENERAL_CATEGORY_REFERENCES,
    PH_HANDLE_GENERAL_CATEGORY_QUOTA,
    // extra
    PH_HANDLE_GENERAL_CATEGORY_ALPC,
    PH_HANDLE_GENERAL_CATEGORY_FILE,
    PH_HANDLE_GENERAL_CATEGORY_SECTION,
    PH_HANDLE_GENERAL_CATEGORY_MUTANT,
    PH_HANDLE_GENERAL_CATEGORY_PROCESSTHREAD,
    PH_HANDLE_GENERAL_CATEGORY_ETW,

    PH_HANDLE_GENERAL_CATEGORY_MAXIMUM
} PHP_HANDLE_GENERAL_CATEGORY;

typedef enum _PHP_HANDLE_GENERAL_INDEX
{
    PH_HANDLE_GENERAL_INDEX_NAME,
    PH_HANDLE_GENERAL_INDEX_TYPE,
    PH_HANDLE_GENERAL_INDEX_OBJECT,
    PH_HANDLE_GENERAL_INDEX_ACCESSMASK,

    PH_HANDLE_GENERAL_INDEX_REFERENCES,
    PH_HANDLE_GENERAL_INDEX_HANDLES,

    PH_HANDLE_GENERAL_INDEX_PAGED,
    PH_HANDLE_GENERAL_INDEX_NONPAGED,

    PH_HANDLE_GENERAL_INDEX_SEQUENCENUMBER,
    PH_HANDLE_GENERAL_INDEX_PORTCONTEXT,

    PH_HANDLE_GENERAL_INDEX_FILETYPE,
    PH_HANDLE_GENERAL_INDEX_FILEMODE,
    PH_HANDLE_GENERAL_INDEX_FILEPOSITION,
    PH_HANDLE_GENERAL_INDEX_FILESIZE,

    PH_HANDLE_GENERAL_INDEX_SECTIONTYPE,
    PH_HANDLE_GENERAL_INDEX_SECTIONFILE,
    PH_HANDLE_GENERAL_INDEX_SECTIONSIZE,

    PH_HANDLE_GENERAL_INDEX_MUTANTCOUNT,
    PH_HANDLE_GENERAL_INDEX_MUTANTABANDONED,
    PH_HANDLE_GENERAL_INDEX_MUTANTOWNER,

    PH_HANDLE_GENERAL_INDEX_PROCESSTHREADNAME,
    PH_HANDLE_GENERAL_INDEX_PROCESSTHREADCREATETIME,
    PH_HANDLE_GENERAL_INDEX_PROCESSTHREADEXITTIME,
    PH_HANDLE_GENERAL_INDEX_PROCESSTHREADEXITCODE,

    PH_HANDLE_GENERAL_INDEX_ETWORIGINALNAME,
    PH_HANDLE_GENERAL_INDEX_ETWGROUPNAME,

    PH_HANDLE_GENERAL_INDEX_MAXIMUM
} PHP_PROCESS_STATISTICS_INDEX;

typedef struct _HANDLE_PROPERTIES_CONTEXT
{
    HWND ListViewHandle;
    HANDLE ProcessId;
    PPH_HANDLE_ITEM HandleItem;
    PH_LAYOUT_MANAGER LayoutManager;
    ULONG ListViewRowCache[PH_HANDLE_GENERAL_INDEX_MAXIMUM];
} HANDLE_PROPERTIES_CONTEXT, *PHANDLE_PROPERTIES_CONTEXT;

#define PH_FILEMODE_ASYNC 0x01000000
#define PhFileModeUpdAsyncFlag(mode) \
    (mode & (FILE_SYNCHRONOUS_IO_ALERT | FILE_SYNCHRONOUS_IO_NONALERT) ? mode &~ PH_FILEMODE_ASYNC: mode | PH_FILEMODE_ASYNC)

PH_ACCESS_ENTRY FileModeAccessEntries[6] = 
{
    { L"FILE_FLAG_OVERLAPPED", PH_FILEMODE_ASYNC, FALSE, FALSE, L"Asynchronous" },
    { L"FILE_FLAG_WRITE_THROUGH", FILE_WRITE_THROUGH, FALSE, FALSE, L"Write through" },
    { L"FILE_FLAG_SEQUENTIAL_SCAN", FILE_SEQUENTIAL_ONLY, FALSE, FALSE, L"Sequental" },
    { L"FILE_FLAG_NO_BUFFERING", FILE_NO_INTERMEDIATE_BUFFERING, FALSE, FALSE, L"No buffering" },
    { L"FILE_SYNCHRONOUS_IO_ALERT", FILE_SYNCHRONOUS_IO_ALERT, FALSE, FALSE, L"Synchronous alert" },
    { L"FILE_SYNCHRONOUS_IO_NONALERT", FILE_SYNCHRONOUS_IO_NONALERT, FALSE, FALSE, L"Synchronous non-alert" },
};

INT_PTR CALLBACK PhpHandleGeneralDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

static NTSTATUS PhpDuplicateHandleFromProcess(
    _Out_ PHANDLE Handle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ PVOID Context
    )
{
    PHANDLE_PROPERTIES_CONTEXT context = Context;
    NTSTATUS status;
    HANDLE processHandle;

    if (!context)
        return STATUS_UNSUCCESSFUL;

    if (!NT_SUCCESS(status = PhOpenProcess(
        &processHandle,
        PROCESS_DUP_HANDLE,
        context->ProcessId
        )))
        return status;

    status = NtDuplicateObject(
        processHandle,
        context->HandleItem->Handle,
        NtCurrentProcess(),
        Handle,
        DesiredAccess,
        0,
        0
        );
    NtClose(processHandle);

    return status;
}

typedef struct _HANDLE_PROPERTIES_THREAD_CONTEXT
{
    HWND ParentWindowHandle;
    HANDLE ProcessId;
    PPH_HANDLE_ITEM HandleItem;
} HANDLE_PROPERTIES_THREAD_CONTEXT, *PHANDLE_PROPERTIES_THREAD_CONTEXT;

NTSTATUS PhpShowHandlePropertiesThread(
    _In_ PVOID Parameter
    )
{
    PHANDLE_PROPERTIES_THREAD_CONTEXT handleContext = Parameter;
    PROPSHEETHEADER propSheetHeader = { sizeof(PROPSHEETHEADER) };
    PROPSHEETPAGE propSheetPage;
    HPROPSHEETPAGE pages[16];
    HANDLE_PROPERTIES_CONTEXT context;
    PH_AUTO_POOL autoPool;

    context.ProcessId = handleContext->ProcessId;
    context.HandleItem = handleContext->HandleItem;

    PhInitializeAutoPool(&autoPool);

    propSheetHeader.dwFlags =
        PSH_MODELESS |
        PSH_NOAPPLYNOW |
        PSH_NOCONTEXTHELP |
        PSH_PROPTITLE;
    propSheetHeader.hInstance = PhInstanceHandle;
    propSheetHeader.hwndParent = handleContext->ParentWindowHandle;
    propSheetHeader.pszCaption = L"Handle";
    propSheetHeader.nPages = 0;
    propSheetHeader.nStartPage = 0;
    propSheetHeader.phpage = pages;

    // General page
    memset(&propSheetPage, 0, sizeof(PROPSHEETPAGE));
    propSheetPage.dwSize = sizeof(PROPSHEETPAGE);
    propSheetPage.pszTemplate = MAKEINTRESOURCE(IDD_HNDLGENERAL);
    propSheetPage.hInstance = PhInstanceHandle;
    propSheetPage.pfnDlgProc = PhpHandleGeneralDlgProc;
    propSheetPage.lParam = (LPARAM)&context;
    pages[propSheetHeader.nPages++] = CreatePropertySheetPage(&propSheetPage);

    // Object-specific page
    if (PhIsNullOrEmptyString(handleContext->HandleItem->TypeName))
    {
        NOTHING;
    }
    else if (PhEqualString2(handleContext->HandleItem->TypeName, L"Event", TRUE))
    {
        pages[propSheetHeader.nPages++] = PhCreateEventPage(
            PhpDuplicateHandleFromProcess,
            &context
            );
    }
    else if (PhEqualString2(handleContext->HandleItem->TypeName, L"EventPair", TRUE))
    {
        pages[propSheetHeader.nPages++] = PhCreateEventPairPage(
            PhpDuplicateHandleFromProcess,
            &context
            );
    }
    else if (PhEqualString2(handleContext->HandleItem->TypeName, L"Job", TRUE))
    {
        pages[propSheetHeader.nPages++] = PhCreateJobPage(
            PhpDuplicateHandleFromProcess,
            &context,
            NULL
            );
    }
    else if (PhEqualString2(handleContext->HandleItem->TypeName, L"Semaphore", TRUE))
    {
        pages[propSheetHeader.nPages++] = PhCreateSemaphorePage(
            PhpDuplicateHandleFromProcess,
            &context
            );
    }
    else if (PhEqualString2(handleContext->HandleItem->TypeName, L"Timer", TRUE))
    {
        pages[propSheetHeader.nPages++] = PhCreateTimerPage(
            PhpDuplicateHandleFromProcess,
            &context
            );
    }
    else if (PhEqualString2(handleContext->HandleItem->TypeName, L"Token", TRUE))
    {
        pages[propSheetHeader.nPages++] = PhCreateTokenPage(
            PhpDuplicateHandleFromProcess,
            context.ProcessId,
            &context,
            NULL
            );
    }

    // Security page
    pages[propSheetHeader.nPages++] = PhCreateSecurityPage(
        PhGetStringOrEmpty(handleContext->HandleItem->BestObjectName),
        PhGetStringOrEmpty(handleContext->HandleItem->TypeName),
        PhpDuplicateHandleFromProcess,
        NULL,
        &context
        );

    if (PhPluginsEnabled)
    {
        PH_PLUGIN_OBJECT_PROPERTIES objectProperties;
        PH_PLUGIN_HANDLE_PROPERTIES_CONTEXT propertiesContext;

        propertiesContext.ProcessId = handleContext->ProcessId;
        propertiesContext.HandleItem = handleContext->HandleItem;

        objectProperties.Parameter = &propertiesContext;
        objectProperties.NumberOfPages = propSheetHeader.nPages;
        objectProperties.MaximumNumberOfPages = RTL_NUMBER_OF(pages);
        objectProperties.Pages = pages;

        PhInvokeCallback(PhGetGeneralCallback(GeneralCallbackHandlePropertiesInitializing), &objectProperties);

        propSheetHeader.nPages = objectProperties.NumberOfPages;
    }

    PhModalPropertySheet(&propSheetHeader);

    PhDeleteAutoPool(&autoPool);

    PhDereferenceObject(handleContext->HandleItem);
    PhFree(handleContext);

    return STATUS_SUCCESS;
}

VOID PhShowHandleProperties(
    _In_ HWND ParentWindowHandle,
    _In_ HANDLE ProcessId,
    _In_ PPH_HANDLE_ITEM HandleItem
    )
{
    PHANDLE_PROPERTIES_THREAD_CONTEXT context;

    context = PhAllocate(sizeof(HANDLE_PROPERTIES_THREAD_CONTEXT));
    context->ParentWindowHandle = PhCsForceNoParent ? NULL : ParentWindowHandle;
    context->ProcessId = ProcessId;
    context->HandleItem = HandleItem;
    PhReferenceObject(HandleItem);

    PhCreateThread2(PhpShowHandlePropertiesThread, context);
}

VOID PhpUpdateHandleGeneralListViewGroups(
    _In_ PHANDLE_PROPERTIES_CONTEXT Context
    )
{
    ListView_EnableGroupView(Context->ListViewHandle, TRUE);
    PhAddListViewGroup(Context->ListViewHandle, PH_HANDLE_GENERAL_CATEGORY_BASICINFO, L"Basic information");
    PhAddListViewGroup(Context->ListViewHandle, PH_HANDLE_GENERAL_CATEGORY_REFERENCES, L"References");
    PhAddListViewGroup(Context->ListViewHandle, PH_HANDLE_GENERAL_CATEGORY_QUOTA, L"Quota charges");

    Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_NAME] = PhAddListViewGroupItem(
        Context->ListViewHandle,
        PH_HANDLE_GENERAL_CATEGORY_BASICINFO,
        PH_HANDLE_GENERAL_INDEX_NAME,
        L"Name",
        NULL
        );
    Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_TYPE] = PhAddListViewGroupItem(
        Context->ListViewHandle,
        PH_HANDLE_GENERAL_CATEGORY_BASICINFO,
        PH_HANDLE_GENERAL_INDEX_TYPE,
        L"Type",
        NULL
        );
    Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_OBJECT] = PhAddListViewGroupItem(
        Context->ListViewHandle,
        PH_HANDLE_GENERAL_CATEGORY_BASICINFO,
        PH_HANDLE_GENERAL_INDEX_OBJECT,
        L"Object address",
        NULL
        );
    Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_ACCESSMASK] = PhAddListViewGroupItem(
        Context->ListViewHandle,
        PH_HANDLE_GENERAL_CATEGORY_BASICINFO,
        PH_HANDLE_GENERAL_INDEX_ACCESSMASK,
        L"Granted access",
        NULL
        );
    Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_REFERENCES] = PhAddListViewGroupItem(
        Context->ListViewHandle,
        PH_HANDLE_GENERAL_CATEGORY_REFERENCES,
        PH_HANDLE_GENERAL_INDEX_REFERENCES,
        L"References",
        NULL
        );
    Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_HANDLES] = PhAddListViewGroupItem(
        Context->ListViewHandle,
        PH_HANDLE_GENERAL_CATEGORY_REFERENCES,
        PH_HANDLE_GENERAL_INDEX_HANDLES,
        L"Handles",
        NULL
        );
    Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PAGED] = PhAddListViewGroupItem(
        Context->ListViewHandle,
        PH_HANDLE_GENERAL_CATEGORY_QUOTA,
        PH_HANDLE_GENERAL_INDEX_PAGED,
        L"Paged",
        NULL
        );
    Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_NONPAGED] = PhAddListViewGroupItem(
        Context->ListViewHandle,
        PH_HANDLE_GENERAL_CATEGORY_QUOTA,
        PH_HANDLE_GENERAL_INDEX_NONPAGED,
        L"Virtual size",
        NULL
        );

    if (PhIsNullOrEmptyString(Context->HandleItem->TypeName))
    {
        NOTHING;
    }
    else if (PhEqualString2(Context->HandleItem->TypeName, L"ALPC Port", TRUE))
    {
        PhAddListViewGroup(Context->ListViewHandle, PH_HANDLE_GENERAL_CATEGORY_ALPC, L"ALPC Port");
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_SEQUENCENUMBER] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_ALPC,
            PH_HANDLE_GENERAL_INDEX_SEQUENCENUMBER,
            L"Sequence Number",
            NULL
            );
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PORTCONTEXT] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_ALPC,
            PH_HANDLE_GENERAL_INDEX_PORTCONTEXT,
            L"Port Context",
            NULL
            );
    }
    else if (PhEqualString2(Context->HandleItem->TypeName, L"EtwRegistration", TRUE))
    {
        PhAddListViewGroup(Context->ListViewHandle, PH_HANDLE_GENERAL_CATEGORY_ETW, L"Event trace information");
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_ETWORIGINALNAME] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_ETW,
            PH_HANDLE_GENERAL_INDEX_ETWORIGINALNAME,
            L"GUID",
            NULL
            );
        //Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_ETWGROUPNAME] = PhAddListViewGroupItem(
        //    Context->ListViewHandle,
        //    PH_HANDLE_GENERAL_CATEGORY_ETW,
        //    PH_HANDLE_GENERAL_INDEX_ETWGROUPNAME,
        //    L"Group GUID",
        //    NULL
        //    );
    }
    else if (PhEqualStringRef2(&Context->HandleItem->TypeName->sr, L"File", TRUE))
    {
        PhAddListViewGroup(Context->ListViewHandle, PH_HANDLE_GENERAL_CATEGORY_FILE, L"File information");

        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_FILETYPE] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_FILE,
            PH_HANDLE_GENERAL_INDEX_FILETYPE,
            L"Type",
            NULL
            );
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_FILEMODE] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_FILE,
            PH_HANDLE_GENERAL_INDEX_FILEMODE,
            L"Mode",
            NULL
            );
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_FILEPOSITION] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_FILE,
            PH_HANDLE_GENERAL_INDEX_FILEPOSITION,
            L"Position",
            NULL
            );
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_FILESIZE] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_FILE,
            PH_HANDLE_GENERAL_INDEX_FILESIZE,
            L"Size",
            NULL
            );
    }
    else if (PhEqualStringRef2(&Context->HandleItem->TypeName->sr, L"Section", TRUE))
    {
        PhAddListViewGroup(Context->ListViewHandle, PH_HANDLE_GENERAL_CATEGORY_SECTION, L"Section information");

        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_SECTIONTYPE] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_SECTION,
            PH_HANDLE_GENERAL_INDEX_SECTIONTYPE,
            L"Type",
            NULL
            );
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_SECTIONFILE] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_SECTION,
            PH_HANDLE_GENERAL_INDEX_SECTIONFILE,
            L"File",
            NULL
            );
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_SECTIONSIZE] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_SECTION,
            PH_HANDLE_GENERAL_INDEX_SECTIONSIZE,
            L"Size",
            NULL
            );
    }
    else if (PhEqualStringRef2(&Context->HandleItem->TypeName->sr, L"Mutant", TRUE))
    {
        PhAddListViewGroup(Context->ListViewHandle, PH_HANDLE_GENERAL_CATEGORY_MUTANT, L"Mutant information");

        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_MUTANTCOUNT] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_MUTANT,
            PH_HANDLE_GENERAL_INDEX_MUTANTCOUNT,
            L"Count",
            NULL
            );
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_MUTANTABANDONED] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_MUTANT,
            PH_HANDLE_GENERAL_INDEX_MUTANTABANDONED,
            L"Abandoned",
            NULL
            );
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_MUTANTOWNER] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_MUTANT,
            PH_HANDLE_GENERAL_INDEX_MUTANTOWNER,
            L"Owner",
            NULL
            );
    }
    else if (PhEqualStringRef2(&Context->HandleItem->TypeName->sr, L"Process", TRUE))
    {
        PhAddListViewGroup(Context->ListViewHandle, PH_HANDLE_GENERAL_CATEGORY_PROCESSTHREAD, L"Process information");

        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADNAME] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_PROCESSTHREAD,
            PH_HANDLE_GENERAL_INDEX_PROCESSTHREADNAME,
            L"Name",
            NULL
            );
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADCREATETIME] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_PROCESSTHREAD,
            PH_HANDLE_GENERAL_INDEX_PROCESSTHREADCREATETIME,
            L"Created",
            NULL
            );
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADEXITTIME] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_PROCESSTHREAD,
            PH_HANDLE_GENERAL_INDEX_PROCESSTHREADEXITTIME,
            L"Exited",
            NULL
            );
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADEXITCODE] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_PROCESSTHREAD,
            PH_HANDLE_GENERAL_INDEX_PROCESSTHREADEXITCODE,
            L"Exit status",
            NULL
            );
    }
    else if (PhEqualStringRef2(&Context->HandleItem->TypeName->sr, L"Thread", TRUE))
    {
        PhAddListViewGroup(Context->ListViewHandle, PH_HANDLE_GENERAL_CATEGORY_PROCESSTHREAD, L"Thread information");

        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADNAME] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_PROCESSTHREAD,
            PH_HANDLE_GENERAL_INDEX_PROCESSTHREADNAME,
            L"Name",
            NULL
            );
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADCREATETIME] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_PROCESSTHREAD,
            PH_HANDLE_GENERAL_INDEX_PROCESSTHREADCREATETIME,
            L"Created",
            NULL
            );
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADEXITTIME] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_PROCESSTHREAD,
            PH_HANDLE_GENERAL_INDEX_PROCESSTHREADEXITTIME,
            L"Exited",
            NULL
            );
        Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADEXITCODE] = PhAddListViewGroupItem(
            Context->ListViewHandle,
            PH_HANDLE_GENERAL_CATEGORY_PROCESSTHREAD,
            PH_HANDLE_GENERAL_INDEX_PROCESSTHREADEXITCODE,
            L"Exit status",
            NULL
            );
    }
}

VOID PhpUpdateHandleGeneral(
    _In_ PHANDLE_PROPERTIES_CONTEXT Context
    )
{
    HANDLE processHandle;
    PPH_ACCESS_ENTRY accessEntries;
    ULONG numberOfAccessEntries;
    OBJECT_BASIC_INFORMATION basicInfo;
    WCHAR string[PH_INT64_STR_LEN_1];

    PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_NAME], 1, PhGetStringOrEmpty(Context->HandleItem->BestObjectName));
    PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_TYPE], 1, PhGetStringOrEmpty(Context->HandleItem->TypeName));

    if (Context->HandleItem->Object)
    {
        PhPrintPointer(string, Context->HandleItem->Object);
        PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_OBJECT], 1, string);
    }

    if (PhGetAccessEntries(
        PhGetStringOrEmpty(Context->HandleItem->TypeName),
        &accessEntries,
        &numberOfAccessEntries
        ))
    {
        PPH_STRING accessString;
        PPH_STRING grantedAccessString;

        accessString = PH_AUTO(PhGetAccessString(
            Context->HandleItem->GrantedAccess,
            accessEntries,
            numberOfAccessEntries
            ));

        if (accessString->Length != 0)
        {
            grantedAccessString = PH_AUTO(PhFormatString(
                L"0x%x (%s)",
                Context->HandleItem->GrantedAccess,
                accessString->Buffer
                ));

            PhSetListViewSubItem(Context->ListViewHandle, PH_HANDLE_GENERAL_INDEX_ACCESSMASK, 1, grantedAccessString->Buffer);
        }
        else
        {
            PhPrintPointer(string, UlongToPtr(Context->HandleItem->GrantedAccess));
            PhSetListViewSubItem(Context->ListViewHandle, PH_HANDLE_GENERAL_INDEX_ACCESSMASK, 1, string);
        }

        PhFree(accessEntries);
    }
    else
    {
        PhPrintPointer(string, UlongToPtr(Context->HandleItem->GrantedAccess));
        PhSetListViewSubItem(Context->ListViewHandle, PH_HANDLE_GENERAL_INDEX_ACCESSMASK, 1, string);
    }

    if (NT_SUCCESS(PhOpenProcess(
        &processHandle,
        PROCESS_DUP_HANDLE,
        Context->ProcessId
        )))
    {
        if (NT_SUCCESS(PhGetHandleInformation(
            processHandle,
            Context->HandleItem->Handle,
            ULONG_MAX,
            &basicInfo,
            NULL,
            NULL,
            NULL
            )))
        {
            PhPrintUInt32(string, basicInfo.PointerCount);
            PhSetListViewSubItem(Context->ListViewHandle, PH_HANDLE_GENERAL_INDEX_REFERENCES, 1, string);

            PhPrintUInt32(string, basicInfo.HandleCount);
            PhSetListViewSubItem(Context->ListViewHandle, PH_HANDLE_GENERAL_INDEX_HANDLES, 1, string);

            PhPrintUInt32(string, basicInfo.PagedPoolCharge);
            PhSetListViewSubItem(Context->ListViewHandle, PH_HANDLE_GENERAL_INDEX_PAGED, 1, string);

            PhPrintUInt32(string, basicInfo.NonPagedPoolCharge);
            PhSetListViewSubItem(Context->ListViewHandle, PH_HANDLE_GENERAL_INDEX_NONPAGED, 1, string);
        }

        NtClose(processHandle);
    }

    if (PhIsNullOrEmptyString(Context->HandleItem->TypeName))
    {
        NOTHING;
    }
    else if (PhEqualString2(Context->HandleItem->TypeName, L"ALPC Port", TRUE))
    {
        NTSTATUS status;
        HANDLE processHandle;
        HANDLE alpcPortHandle;

        if (NT_SUCCESS(status = PhOpenProcess(
            &processHandle,
            PROCESS_DUP_HANDLE,
            Context->ProcessId
            )))
        {
            status = NtDuplicateObject(
                processHandle,
                Context->HandleItem->Handle,
                NtCurrentProcess(),
                &alpcPortHandle,
                READ_CONTROL,
                0,
                0
                );
            NtClose(processHandle);
        }

        if (NT_SUCCESS(status))
        {
            ALPC_BASIC_INFORMATION basicInfo;

            if (NT_SUCCESS(NtAlpcQueryInformation(
                alpcPortHandle,
                AlpcBasicInformation,
                &basicInfo,
                sizeof(ALPC_BASIC_INFORMATION),
                NULL
                )))
            {
                PhPrintUInt32(string, basicInfo.SequenceNo);
                PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_SEQUENCENUMBER], 1, string);

                PhPrintPointer(string, basicInfo.PortContext);
                PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PORTCONTEXT], 1, string);
            }

            NtClose(alpcPortHandle);
        }
    }
    else if (PhEqualString2(Context->HandleItem->TypeName, L"EtwRegistration", TRUE))
    {
        PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_ETWORIGINALNAME], 1, PhGetString(Context->HandleItem->ObjectName));
    }
    else if (PhEqualString2(Context->HandleItem->TypeName, L"File", TRUE))
    {
        NTSTATUS status;
        HANDLE processHandle;
        HANDLE fileHandle;

        if (NT_SUCCESS(status = PhOpenProcess(
            &processHandle,
            PROCESS_DUP_HANDLE,
            Context->ProcessId
            )))
        {
            status = NtDuplicateObject(
                processHandle,
                Context->HandleItem->Handle,
                NtCurrentProcess(),
                &fileHandle,
                MAXIMUM_ALLOWED,
                0,
                0
                );
            NtClose(processHandle);
        }

        if (NT_SUCCESS(status))
        {
            BOOLEAN disableFlushButton = FALSE;
            BOOLEAN isFileOrDirectory = FALSE;
            BOOLEAN isConsoleHandle = FALSE;
            BOOLEAN isPipeHandle = FALSE;
            BOOLEAN isNetworkHandle = FALSE;
            FILE_FS_DEVICE_INFORMATION fileDeviceInfo;
            FILE_MODE_INFORMATION fileModeInfo;
            FILE_STANDARD_INFORMATION fileStandardInfo;
            FILE_POSITION_INFORMATION filePositionInfo;
            IO_STATUS_BLOCK isb;

            if (NT_SUCCESS(NtQueryVolumeInformationFile(
                fileHandle,
                &isb,
                &fileDeviceInfo,
                sizeof(FILE_FS_DEVICE_INFORMATION),
                FileFsDeviceInformation
                )))
            {
                switch (fileDeviceInfo.DeviceType)
                {
                case FILE_DEVICE_NAMED_PIPE:
                    isPipeHandle = TRUE;
                    PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_FILETYPE], 1, L"Pipe");
                    break;
                case FILE_DEVICE_NETWORK:
                    isNetworkHandle = TRUE;
                    PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_FILETYPE], 1, L"Network");
                    break;
                case FILE_DEVICE_CD_ROM:
                case FILE_DEVICE_CD_ROM_FILE_SYSTEM:
                case FILE_DEVICE_CONTROLLER:
                case FILE_DEVICE_DATALINK:
                case FILE_DEVICE_DFS:
                case FILE_DEVICE_DISK:
                case FILE_DEVICE_DISK_FILE_SYSTEM:
                case FILE_DEVICE_VIRTUAL_DISK:
                    isFileOrDirectory = TRUE;
                    PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_FILETYPE], 1, L"File or directory");
                    break;
                case FILE_DEVICE_CONSOLE:
                    isConsoleHandle = TRUE;
                    PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_FILETYPE], 1, L"Console");
                    break;
                default:
                    PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_FILETYPE], 1, L"Other");
                    break;
                }
            }

            // Note: These devices deadlock without a timeout (dmex)
            // 1) Named pipes
            // 2) \Device\ConDrv\CurrentIn
            // 3) \Device\VolMgrControl

            if (NT_SUCCESS(status = PhCallNtQueryFileInformationWithTimeout(
                fileHandle,
                FileModeInformation,
                &fileModeInfo,
                sizeof(FILE_MODE_INFORMATION)
                )))
            {
                PH_FORMAT format[5];
                PPH_STRING fileModeAccessStr;
                WCHAR fileModeString[MAX_PATH];

                // Since FILE_MODE_INFORMATION has no flag for asynchronous I/O we should use our own flag and set
                // it only if none of synchronous flags are present. That's why we need PhFileModeUpdAsyncFlag.
                fileModeAccessStr = PhGetAccessString(
                    PhFileModeUpdAsyncFlag(fileModeInfo.Mode),
                    FileModeAccessEntries,
                    RTL_NUMBER_OF(FileModeAccessEntries)
                    );

                PhInitFormatS(&format[0], L"0x");
                PhInitFormatX(&format[1], fileModeInfo.Mode);
                PhInitFormatS(&format[2], L" (");
                PhInitFormatSR(&format[3], fileModeAccessStr->sr);
                PhInitFormatS(&format[4], L")");

                if (PhFormatToBuffer(format, RTL_NUMBER_OF(format), fileModeString, sizeof(fileModeString), NULL))
                {
                    PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_FILEMODE], 1, fileModeString);
                }

                PhDereferenceObject(fileModeAccessStr);
            }

            if (!isConsoleHandle)
            {
                if (NT_SUCCESS(status = PhCallNtQueryFileInformationWithTimeout(
                    fileHandle,
                    FileStandardInformation,
                    &fileStandardInfo,
                    sizeof(FILE_STANDARD_INFORMATION)
                    )))
                {
                    PH_FORMAT format[1];
                    WCHAR fileSizeString[PH_INT64_STR_LEN];

                    PhInitFormatSize(&format[0], fileStandardInfo.EndOfFile.QuadPart);

                    if (PhFormatToBuffer(format, RTL_NUMBER_OF(format), fileSizeString, sizeof(fileSizeString), NULL))
                    {
                        PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_FILESIZE], 1, fileSizeString);
                    }

                    if (isFileOrDirectory)
                    {
                        PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_FILETYPE], 1, fileStandardInfo.Directory ? L"Directory" : L"File");
                    }

                    disableFlushButton |= fileStandardInfo.Directory;
                }

                if (NT_SUCCESS(status = PhCallNtQueryFileInformationWithTimeout(
                    fileHandle,
                    FilePositionInformation,
                    &filePositionInfo,
                    sizeof(FILE_POSITION_INFORMATION)
                    )))
                {
                    if (filePositionInfo.CurrentByteOffset.QuadPart != 0 && fileStandardInfo.EndOfFile.QuadPart != 0)
                    {
                        PH_FORMAT format[4];
                        WCHAR filePositionString[PH_INT64_STR_LEN];

                        PhInitFormatI64UGroupDigits(&format[0], filePositionInfo.CurrentByteOffset.QuadPart);
                        PhInitFormatS(&format[1], L" (");
                        PhInitFormatF(&format[2], (DOUBLE)filePositionInfo.CurrentByteOffset.QuadPart / (DOUBLE)fileStandardInfo.EndOfFile.QuadPart * 100.0, 1);
                        PhInitFormatS(&format[3], L"%)");

                        if (PhFormatToBuffer(format, RTL_NUMBER_OF(format), filePositionString, sizeof(filePositionString), NULL))
                        {
                            PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_FILEPOSITION], 1, filePositionString);
                        }
                    }
                    else if (filePositionInfo.CurrentByteOffset.QuadPart != 0)
                    {
                        PH_FORMAT format[1];
                        WCHAR filePositionString[PH_INT64_STR_LEN];

                        PhInitFormatI64UGroupDigits(&format[0], filePositionInfo.CurrentByteOffset.QuadPart);

                        if (PhFormatToBuffer(format, RTL_NUMBER_OF(format), filePositionString, sizeof(filePositionString), NULL))
                        {
                            PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_FILEPOSITION], 1, filePositionString);
                        }
                    }
                }
            }

            NtClose(fileHandle);
        }
    }
    else if (PhEqualString2(Context->HandleItem->TypeName, L"Section", TRUE))
    {
        NTSTATUS status;
        HANDLE processHandle;
        HANDLE sectionHandle;

        if (NT_SUCCESS(status = PhOpenProcess(
            &processHandle,
            PROCESS_DUP_HANDLE,
            Context->ProcessId
            )))
        {
            status = NtDuplicateObject(
                processHandle,
                Context->HandleItem->Handle,
                NtCurrentProcess(),
                &sectionHandle,
                SECTION_QUERY | SECTION_MAP_READ,
                0,
                0
                );
            
            if (!NT_SUCCESS(status))
            {
                status = NtDuplicateObject(
                    processHandle,
                    Context->HandleItem->Handle,
                    NtCurrentProcess(),
                    &sectionHandle,
                    SECTION_QUERY,
                    0,
                    0
                    );
            }

            NtClose(processHandle);
        }

        if (NT_SUCCESS(status))
        {
            SECTION_BASIC_INFORMATION basicInfo;
            PWSTR sectionType = L"Unknown";
            PPH_STRING sectionSize = NULL;
            PPH_STRING fileName = NULL;

            if (NT_SUCCESS(PhGetSectionBasicInformation(sectionHandle, &basicInfo)))
            {
                if (basicInfo.AllocationAttributes & SEC_COMMIT)
                    sectionType = L"Commit";
                else if (basicInfo.AllocationAttributes & SEC_FILE)
                    sectionType = L"File";
                else if (basicInfo.AllocationAttributes & SEC_IMAGE)
                    sectionType = L"Image";
                else if (basicInfo.AllocationAttributes & SEC_RESERVE)
                    sectionType = L"Reserve";

                sectionSize = PhaFormatSize(basicInfo.MaximumSize.QuadPart, -1);
            }

            if (NT_SUCCESS(PhGetSectionFileName(sectionHandle, &fileName)))
            {
                PPH_STRING newFileName;

                PH_AUTO(fileName);

                if (newFileName = PhResolveDevicePrefix(fileName))
                    fileName = PH_AUTO(newFileName);
            }

            PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_SECTIONTYPE], 1, sectionType);
            PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_SECTIONFILE], 1, PhGetStringOrDefault(fileName, L"N/A"));
            PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_SECTIONSIZE], 1, PhGetStringOrDefault(sectionSize, L"Unknown"));

            NtClose(sectionHandle);
        }
    }
    else if (PhEqualString2(Context->HandleItem->TypeName, L"Mutant", TRUE))
    {
        NTSTATUS status;
        HANDLE processHandle;
        HANDLE mutantHandle;

        if (NT_SUCCESS(status = PhOpenProcess(
            &processHandle,
            PROCESS_DUP_HANDLE,
            Context->ProcessId
            )))
        {
            status = NtDuplicateObject(
                processHandle,
                Context->HandleItem->Handle,
                NtCurrentProcess(),
                &mutantHandle,
                SEMAPHORE_QUERY_STATE,
                0,
                0
                );
            NtClose(processHandle);
        }

        if (NT_SUCCESS(status))
        {
            MUTANT_BASIC_INFORMATION basicInfo;
            MUTANT_OWNER_INFORMATION ownerInfo;

            if (NT_SUCCESS(PhGetMutantBasicInformation(mutantHandle, &basicInfo)))
            {
                PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_MUTANTCOUNT], 1, PhaFormatUInt64(basicInfo.CurrentCount, TRUE)->Buffer);
                PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_MUTANTABANDONED], 1, basicInfo.AbandonedState ? L"True" : L"False");
            }

            if (NT_SUCCESS(PhGetMutantOwnerInformation(mutantHandle, &ownerInfo)))
            {
                PPH_STRING name;

                if (ownerInfo.ClientId.UniqueProcess)
                {
                    name = PhStdGetClientIdName(&ownerInfo.ClientId);
                    PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_MUTANTOWNER], 1, name->Buffer);
                    PhDereferenceObject(name);
                }
            }

            NtClose(mutantHandle);
        }
    }
    else if (PhEqualString2(Context->HandleItem->TypeName, L"Process", TRUE))
    {
        NTSTATUS status;
        HANDLE processHandle;
        HANDLE dupHandle;

        if (NT_SUCCESS(status = PhOpenProcess(
            &processHandle,
            PROCESS_DUP_HANDLE,
            Context->ProcessId
            )))
        {
            status = NtDuplicateObject(
                processHandle,
                Context->HandleItem->Handle,
                NtCurrentProcess(),
                &dupHandle,
                PROCESS_QUERY_LIMITED_INFORMATION,
                0,
                0
                );

            NtClose(processHandle);
        }

        if (NT_SUCCESS(status))
        {
            NTSTATUS exitStatus = STATUS_PENDING;
            PPH_STRING fileName;
            PROCESS_BASIC_INFORMATION basicInfo;
            KERNEL_USER_TIMES times;

            if (NT_SUCCESS(PhGetProcessImageFileName(dupHandle, &fileName)))
            {
                PhMoveReference(&fileName, PhGetFileName(fileName));
                PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADNAME], 1, PhGetStringOrEmpty(fileName));
                PhDereferenceObject(fileName);
            }

            if (NT_SUCCESS(PhGetProcessBasicInformation(dupHandle, &basicInfo)))
            {
                exitStatus = basicInfo.ExitStatus;
            }

            if (NT_SUCCESS(PhGetProcessTimes(dupHandle, &times)))
            {
                SYSTEMTIME time;

                PhLargeIntegerToLocalSystemTime(&time, &times.CreateTime);
                PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADCREATETIME], 1, PhaFormatDateTime(&time)->Buffer);

                if (exitStatus != STATUS_PENDING)
                {
                    PhLargeIntegerToLocalSystemTime(&time, &times.ExitTime);
                    PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADEXITTIME], 1, PhaFormatDateTime(&time)->Buffer);
                }
            }

            if (exitStatus != STATUS_PENDING)
            {
                PPH_STRING status;
                PPH_STRING exitcode;

                status = PhGetStatusMessage(exitStatus, 0);
                exitcode = PhFormatString(
                    L"0x%x (%s)",
                    exitStatus,
                    PhGetStringOrDefault(status, L"Unknown")
                    );

                PhSetListViewSubItem(
                    Context->ListViewHandle,
                    Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADEXITCODE],
                    1,
                    PhGetStringOrEmpty(exitcode)
                    );

                PhDereferenceObject(exitcode);
                PhClearReference(&status);
            }

            NtClose(dupHandle);
        }
    }
    else if (PhEqualString2(Context->HandleItem->TypeName, L"Thread", TRUE))
    {
        NTSTATUS status;
        HANDLE processHandle;
        HANDLE dupHandle;

        if (NT_SUCCESS(status = PhOpenProcess(
            &processHandle,
            PROCESS_DUP_HANDLE,
            Context->ProcessId
            )))
        {
            status = NtDuplicateObject(
                processHandle,
                Context->HandleItem->Handle,
                NtCurrentProcess(),
                &dupHandle,
                THREAD_QUERY_LIMITED_INFORMATION,
                0,
                0
                );

            NtClose(processHandle);
        }

        if (NT_SUCCESS(status))
        {
            BOOLEAN isTerminated = FALSE;
            THREAD_BASIC_INFORMATION basicInfo;
            KERNEL_USER_TIMES times;
            PPH_STRING name;

            if (NT_SUCCESS(PhGetThreadName(dupHandle, &name)))
            {
                PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADNAME], 1, PhGetStringOrEmpty(name));
                PhDereferenceObject(name);
            }

            PhGetThreadIsTerminated(dupHandle, &isTerminated);

            if (isTerminated && NT_SUCCESS(PhGetThreadBasicInformation(dupHandle, &basicInfo)))
            {
                PPH_STRING status;
                PPH_STRING exitcode;

                status = PhGetStatusMessage(basicInfo.ExitStatus, 0);
                exitcode = PhFormatString(
                    L"0x%x (%s)",
                    basicInfo.ExitStatus,
                    PhGetStringOrDefault(status, L"Unknown")
                    );

                PhSetListViewSubItem(
                    Context->ListViewHandle,
                    Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADEXITCODE],
                    1,
                    PhGetStringOrEmpty(exitcode)
                    );

                PhDereferenceObject(exitcode);
                PhClearReference(&status);

                //if (NT_SUCCESS(PhOpenProcess(
                //    &processHandle,
                //    PROCESS_QUERY_LIMITED_INFORMATION,
                //    basicInfo.ClientId.UniqueProcess
                //    )))
                //{
                //    if (NT_SUCCESS(PhGetProcessImageFileName(processHandle, &fileName)))
                //    {
                //        PhMoveReference(&fileName, PhGetFileName(fileName));
                //        PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADNAME], 1, PhGetStringOrEmpty(fileName));
                //        PhDereferenceObject(fileName);
                //    }
                //
                //    NtClose(processHandle);
                //}
            }

            if (NT_SUCCESS(PhGetThreadTimes(dupHandle, &times)))
            {
                SYSTEMTIME time;

                PhLargeIntegerToLocalSystemTime(&time, &times.CreateTime);
                PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADCREATETIME], 1, PhaFormatDateTime(&time)->Buffer);

                if (isTerminated)
                {
                    PhLargeIntegerToLocalSystemTime(&time, &times.ExitTime);
                    PhSetListViewSubItem(Context->ListViewHandle, Context->ListViewRowCache[PH_HANDLE_GENERAL_INDEX_PROCESSTHREADEXITTIME], 1, PhaFormatDateTime(&time)->Buffer);
                }
            }

            NtClose(dupHandle);
        }
    }
}

INT_PTR CALLBACK PhpHandleGeneralDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PHANDLE_PROPERTIES_CONTEXT context;

    if (uMsg == WM_INITDIALOG)
    {
        LPPROPSHEETPAGE propSheetPage = (LPPROPSHEETPAGE)lParam;
        context = (PHANDLE_PROPERTIES_CONTEXT)propSheetPage->lParam;

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
            // HACK
            SendMessage(GetParent(hwndDlg), WM_SETICON, ICON_SMALL, (LPARAM)PH_LOAD_SHARED_ICON_SMALL(PhInstanceHandle, MAKEINTRESOURCE(IDI_PROCESSHACKER)));
            SendMessage(GetParent(hwndDlg), WM_SETICON, ICON_BIG, (LPARAM)PH_LOAD_SHARED_ICON_LARGE(PhInstanceHandle, MAKEINTRESOURCE(IDI_PROCESSHACKER)));

            context->ListViewHandle = GetDlgItem(hwndDlg, IDC_LIST);
            PhSetListViewStyle(context->ListViewHandle, FALSE, TRUE);
            PhSetControlTheme(context->ListViewHandle, L"explorer");
            PhAddListViewColumn(context->ListViewHandle, 0, 0, 0, LVCFMT_LEFT, 120, L"Name");
            PhAddListViewColumn(context->ListViewHandle, 1, 1, 1, LVCFMT_LEFT, 250, L"Value");
            PhSetExtendedListView(context->ListViewHandle);

            // HACK
            if (PhGetIntegerPairSetting(L"HandlePropertiesWindowPosition").X != 0)
                PhLoadWindowPlacementFromSetting(L"HandlePropertiesWindowPosition", NULL, GetParent(hwndDlg));
            else
                PhCenterWindow(GetParent(hwndDlg), GetParent(GetParent(hwndDlg))); // HACK

            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);
            PhAddLayoutItem(&context->LayoutManager, context->ListViewHandle, NULL, PH_ANCHOR_ALL);

            PhpUpdateHandleGeneralListViewGroups(context);
            PhpUpdateHandleGeneral(context);

            PhRegisterWindowCallback(GetParent(hwndDlg), PH_PLUGIN_WINDOW_EVENT_TYPE_TOPMOST, NULL);

            if (PhEnableThemeSupport) // TODO: Required for compat (dmex)
                PhInitializeWindowTheme(GetParent(hwndDlg), PhEnableThemeSupport);
            else
                PhInitializeWindowTheme(hwndDlg, FALSE);
        }
        break;
    case WM_DESTROY:
        {
            PhUnregisterWindowCallback(GetParent(hwndDlg));

            PhSaveWindowPlacementToSetting(L"HandlePropertiesWindowPosition", NULL, GetParent(hwndDlg)); // HACK

            PhDeleteLayoutManager(&context->LayoutManager);

            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
        }
        break;
    case WM_SIZE:
        {
            PhLayoutManagerLayout(&context->LayoutManager);
            ExtendedListView_SetColumnWidth(context->ListViewHandle, 0, ELVSCW_AUTOSIZE_REMAININGSPACE);
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            PhHandleListViewNotifyBehaviors(lParam, context->ListViewHandle, PH_LIST_VIEW_DEFAULT_1_BEHAVIORS);

            switch (header->code)
            {
            case PSN_QUERYINITIALFOCUS:
                {
                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, (LONG_PTR)GetDlgItem(hwndDlg, IDC_BASICINFORMATION));
                }
                return TRUE;
            }
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

                    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, IDC_COPY, L"&Copy", NULL, NULL), ULONG_MAX);
                    PhInsertCopyListViewEMenuItem(menu, IDC_COPY, context->ListViewHandle);

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
                            case IDC_COPY:
                                {
                                    PhCopyListView(context->ListViewHandle);
                                }
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

    REFLECT_MESSAGE_DLG(hwndDlg, context->ListViewHandle, uMsg, wParam, lParam);

    return FALSE;
}
