/*
 * Process Hacker Window Explorer -
 *   window properties
 *
 * Copyright (C) 2011 wj32
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

#include "wndexp.h"
#include "resource.h"
#include <appresolver.h>
#include <workqueue.h>
#include <symprv.h>

#include <shellapi.h>
#include <propsys.h>
#include <propvarutil.h>

#define WEM_RESOLVE_DONE (WM_APP + 1234)

typedef struct _WINDOW_PROPERTIES_CONTEXT
{
    HWND WindowHandle;
    HWND ParentWindowHandle;
    HWND ListViewHandle;
    CLIENT_ID ClientId;
    PH_INITONCE SymbolProviderInitOnce;
    PPH_SYMBOL_PROVIDER SymbolProvider;
    LIST_ENTRY ResolveListHead;
    PH_QUEUED_LOCK ResolveListLock;

    PPH_STRING WndProcSymbol;
    ULONG WndProcResolving;
    PPH_STRING DlgProcSymbol;
    ULONG DlgProcResolving;
    PPH_STRING ClassWndProcSymbol;
    ULONG ClassWndProcResolving;

    ULONG_PTR WndProc;
    ULONG_PTR DlgProc;
    WNDCLASSEX ClassInfo;
} WINDOW_PROPERTIES_CONTEXT, *PWINDOW_PROPERTIES_CONTEXT;

typedef struct _SYMBOL_RESOLVE_CONTEXT
{
    LIST_ENTRY ListEntry;
    ULONG64 Address;
    PPH_STRING Symbol;
    PH_SYMBOL_RESOLVE_LEVEL ResolveLevel;
    HWND NotifyWindow;
    PWINDOW_PROPERTIES_CONTEXT Context;
    ULONG Id;
} SYMBOL_RESOLVE_CONTEXT, *PSYMBOL_RESOLVE_CONTEXT;

typedef struct _STRING_INTEGER_PAIR
{
    PWSTR String;
    ULONG Integer;
} STRING_INTEGER_PAIR, *PSTRING_INTEGER_PAIR;

typedef enum _WINDOW_PROPERTIES_CATEGORY
{
    WINDOW_PROPERTIES_CATEGORY_GENERAL,
    WINDOW_PROPERTIES_CATEGORY_CLASS
} WINDOW_PROPERTIES_CATEGORY;

typedef enum _NETADAPTER_DETAILS_INDEX
{
    WINDOW_PROPERTIES_INDEX_APPID,
    WINDOW_PROPERTIES_INDEX_TEXT,
    WINDOW_PROPERTIES_INDEX_THREAD,
    WINDOW_PROPERTIES_INDEX_RECT,
    WINDOW_PROPERTIES_INDEX_NORMALRECT,
    WINDOW_PROPERTIES_INDEX_CLIENTRECT,
    WINDOW_PROPERTIES_INDEX_INSTANCE,
    WINDOW_PROPERTIES_INDEX_MENUHANDLE,
    WINDOW_PROPERTIES_INDEX_USERDATA,
    WINDOW_PROPERTIES_INDEX_UNICODE,
    WINDOW_PROPERTIES_INDEX_WNDEXTRA,
    WINDOW_PROPERTIES_INDEX_WNDPROC,
    WINDOW_PROPERTIES_INDEX_DLGPROC,
    WINDOW_PROPERTIES_INDEX_DLGCTLID,
    WINDOW_PROPERTIES_INDEX_FONTNAME,
    WINDOW_PROPERTIES_INDEX_STYLES,
    WINDOW_PROPERTIES_INDEX_EXSTYLES,
    WINDOW_PROPERTIES_INDEX_AUTOMATION,

    WINDOW_PROPERTIES_INDEX_CLASS_NAME,
    WINDOW_PROPERTIES_INDEX_CLASS_ATOM,
    WINDOW_PROPERTIES_INDEX_CLASS_STYLES,
    WINDOW_PROPERTIES_INDEX_CLASS_INSTANCE,
    WINDOW_PROPERTIES_INDEX_CLASS_LARGEICON,
    WINDOW_PROPERTIES_INDEX_CLASS_SMALLICON,
    WINDOW_PROPERTIES_INDEX_CLASS_CURSOR,
    WINDOW_PROPERTIES_INDEX_CLASS_BACKBRUSH,
    WINDOW_PROPERTIES_INDEX_CLASS_MENUNAME,
    WINDOW_PROPERTIES_INDEX_CLASS_WNDPROC
} NETADAPTER_DETAILS_INDEX;

NTSTATUS WepPropertiesThreadStart(
    _In_ PVOID Parameter
    );

INT_PTR CALLBACK WepWindowGeneralDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

INT_PTR CALLBACK WepWindowPropertiesDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

INT_PTR CALLBACK WepWindowPropStoreDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

#define DEFINE_PAIR(Symbol) { L#Symbol, Symbol }

static STRING_INTEGER_PAIR WepStylePairs[] =
{
    DEFINE_PAIR(WS_POPUP),
    DEFINE_PAIR(WS_CHILD),
    DEFINE_PAIR(WS_MINIMIZE),
    DEFINE_PAIR(WS_VISIBLE),
    DEFINE_PAIR(WS_DISABLED),
    DEFINE_PAIR(WS_CLIPSIBLINGS),
    DEFINE_PAIR(WS_CLIPCHILDREN),
    DEFINE_PAIR(WS_MAXIMIZE),
    DEFINE_PAIR(WS_BORDER),
    DEFINE_PAIR(WS_DLGFRAME),
    DEFINE_PAIR(WS_VSCROLL),
    DEFINE_PAIR(WS_HSCROLL),
    DEFINE_PAIR(WS_SYSMENU),
    DEFINE_PAIR(WS_THICKFRAME),
    DEFINE_PAIR(WS_GROUP),
    DEFINE_PAIR(WS_TABSTOP),
    DEFINE_PAIR(WS_MINIMIZEBOX),
    DEFINE_PAIR(WS_MAXIMIZEBOX)
};

static STRING_INTEGER_PAIR WepExtendedStylePairs[] =
{
    DEFINE_PAIR(WS_EX_DLGMODALFRAME),
    DEFINE_PAIR(WS_EX_NOPARENTNOTIFY),
    DEFINE_PAIR(WS_EX_TOPMOST),
    DEFINE_PAIR(WS_EX_ACCEPTFILES),
    DEFINE_PAIR(WS_EX_TRANSPARENT),
    DEFINE_PAIR(WS_EX_MDICHILD),
    DEFINE_PAIR(WS_EX_TOOLWINDOW),
    DEFINE_PAIR(WS_EX_WINDOWEDGE),
    DEFINE_PAIR(WS_EX_CLIENTEDGE),
    DEFINE_PAIR(WS_EX_CONTEXTHELP),
    DEFINE_PAIR(WS_EX_RIGHT),
    DEFINE_PAIR(WS_EX_RTLREADING),
    DEFINE_PAIR(WS_EX_LEFTSCROLLBAR),
    DEFINE_PAIR(WS_EX_CONTROLPARENT),
    DEFINE_PAIR(WS_EX_STATICEDGE),
    DEFINE_PAIR(WS_EX_APPWINDOW),
    DEFINE_PAIR(WS_EX_LAYERED),
    DEFINE_PAIR(WS_EX_NOINHERITLAYOUT),
    DEFINE_PAIR(WS_EX_LAYOUTRTL),
    DEFINE_PAIR(WS_EX_COMPOSITED),
    DEFINE_PAIR(WS_EX_NOACTIVATE)
};

static STRING_INTEGER_PAIR WepClassStylePairs[] =
{
    DEFINE_PAIR(CS_VREDRAW),
    DEFINE_PAIR(CS_HREDRAW),
    DEFINE_PAIR(CS_DBLCLKS),
    DEFINE_PAIR(CS_OWNDC),
    DEFINE_PAIR(CS_CLASSDC),
    DEFINE_PAIR(CS_PARENTDC),
    DEFINE_PAIR(CS_NOCLOSE),
    DEFINE_PAIR(CS_SAVEBITS),
    DEFINE_PAIR(CS_BYTEALIGNCLIENT),
    DEFINE_PAIR(CS_BYTEALIGNWINDOW),
    DEFINE_PAIR(CS_GLOBALCLASS),
    DEFINE_PAIR(CS_IME),
    DEFINE_PAIR(CS_DROPSHADOW)
};

PPH_OBJECT_TYPE WeWindowItemType = NULL;

VOID NTAPI WeWindowItemDeleteProcedure(
    _In_ PVOID Object,
    _In_ ULONG Flags
    )
{
    PWINDOW_PROPERTIES_CONTEXT context = Object;

    PLIST_ENTRY listEntry;

    PhClearReference(&context->SymbolProvider);

    // Destroy results that have not been processed by any property pages.

    listEntry = context->ResolveListHead.Flink;

    while (listEntry != &context->ResolveListHead)
    {
        PSYMBOL_RESOLVE_CONTEXT resolveContext;

        resolveContext = CONTAINING_RECORD(listEntry, SYMBOL_RESOLVE_CONTEXT, ListEntry);
        listEntry = listEntry->Flink;

        PhClearReference(&resolveContext->Symbol);
        PhFree(resolveContext);
    }

    PhClearReference(&context->WndProcSymbol);
    PhClearReference(&context->DlgProcSymbol);
    PhClearReference(&context->ClassWndProcSymbol);
}

VOID WeShowWindowProperties(
    _In_ HWND ParentWindowHandle,
    _In_ HWND WindowHandle
    )
{
    PWINDOW_PROPERTIES_CONTEXT context;
    ULONG threadId;
    ULONG processId;

    if (!WeWindowItemType)
        WeWindowItemType = PhCreateObjectType(L"WindowItemType", 0, WeWindowItemDeleteProcedure);

    context = PhCreateObjectZero(sizeof(WINDOW_PROPERTIES_CONTEXT), WeWindowItemType);
    context->WindowHandle = WindowHandle;
    context->ParentWindowHandle = ParentWindowHandle;

    PhInitializeInitOnce(&context->SymbolProviderInitOnce);
    InitializeListHead(&context->ResolveListHead);
    PhInitializeQueuedLock(&context->ResolveListLock);

    processId = 0;
    threadId = GetWindowThreadProcessId(WindowHandle, &processId);
    context->ClientId.UniqueProcess = UlongToHandle(processId);
    context->ClientId.UniqueThread = UlongToHandle(threadId);

    PhCreateThread2(WepPropertiesThreadStart, context);
}

NTSTATUS WepPropertiesThreadStart(
    _In_ PVOID Parameter
    )
{
    PWINDOW_PROPERTIES_CONTEXT context = Parameter;
    PPV_PROPCONTEXT propContext;
    PH_AUTO_POOL autoPool;

    PhInitializeAutoPool(&autoPool);

    if (propContext = HdCreatePropContext(PhaFormatString(L"Window %Ix", (ULONG_PTR)context->WindowHandle)->Buffer))
    {
        PPV_PROPPAGECONTEXT newPage;

        // General
        newPage = PvCreatePropPageContext(
            MAKEINTRESOURCE(IDD_WNDGENERAL),
            WepWindowGeneralDlgProc,
            context);
        PvAddPropPage(propContext, newPage);

        // Properties
        newPage = PvCreatePropPageContext(
            MAKEINTRESOURCE(IDD_WNDPROPLIST),
            WepWindowPropertiesDlgProc,
            context);
        PvAddPropPage(propContext, newPage);

        // Property store
        newPage = PvCreatePropPageContext(
            MAKEINTRESOURCE(IDD_WNDPROPSTORAGE),
            WepWindowPropStoreDlgProc,
            context);
        PvAddPropPage(propContext, newPage);

        PhModalPropertySheet(&propContext->PropSheetHeader);
        PhDereferenceObject(propContext);
    }

    PhDeleteAutoPool(&autoPool);

    PhDereferenceObject(context);

    return STATUS_SUCCESS;
}

BOOLEAN NTAPI EnumGenericModulesCallback(
    _In_ PPH_MODULE_INFO Module,
    _In_opt_ PVOID Context
    )
{
    PWINDOW_PROPERTIES_CONTEXT context = Context;

    if (!context)
        return FALSE;

    PhLoadModuleSymbolProvider(context->SymbolProvider, Module->FileName->Buffer,
        (ULONG64)Module->BaseAddress, Module->Size);

    return TRUE;
}

NTSTATUS WepResolveSymbolFunction(
    _In_ PVOID Parameter
    )
{
    PSYMBOL_RESOLVE_CONTEXT context = Parameter;

    if (PhBeginInitOnce(&context->Context->SymbolProviderInitOnce))
    {
        PhEnumGenericModules(context->Context->ClientId.UniqueProcess, NULL, 0, EnumGenericModulesCallback, context->Context);
        PhEndInitOnce(&context->Context->SymbolProviderInitOnce);
    }

    context->Symbol = PhGetSymbolFromAddress(
        context->Context->SymbolProvider,
        (ULONG64)context->Address,
        &context->ResolveLevel,
        NULL,
        NULL,
        NULL
        );

    // Fail if we don't have a symbol.
    if (!context->Symbol)
    {
        PhDereferenceObject(context->Context);
        PhFree(context);
        return STATUS_SUCCESS;
    }

    PhAcquireQueuedLockExclusive(&context->Context->ResolveListLock);
    InsertHeadList(&context->Context->ResolveListHead, &context->ListEntry);
    PhReleaseQueuedLockExclusive(&context->Context->ResolveListLock);

    PostMessage(context->NotifyWindow, WEM_RESOLVE_DONE, 0, (LPARAM)context);

    PhDereferenceObject(context->Context);

    return STATUS_SUCCESS;
}

VOID WepQueueResolveSymbol(
    _In_ PWINDOW_PROPERTIES_CONTEXT Context,
    _In_ HWND NotifyWindow,
    _In_ ULONG64 Address,
    _In_ ULONG Id
    )
{
    PSYMBOL_RESOLVE_CONTEXT resolveContext;

    if (!Context->SymbolProvider)
    {
        Context->SymbolProvider = PhCreateSymbolProvider(Context->ClientId.UniqueProcess);
        PhLoadSymbolProviderOptions(Context->SymbolProvider);
    }

    PhReferenceObject(Context);

    resolveContext = PhAllocateZero(sizeof(SYMBOL_RESOLVE_CONTEXT));
    resolveContext->Address = Address;
    resolveContext->Symbol = NULL;
    resolveContext->ResolveLevel = PhsrlInvalid;
    resolveContext->NotifyWindow = NotifyWindow;
    resolveContext->Context = Context;
    resolveContext->Id = Id;

    PhQueueItemWorkQueue(PhGetGlobalWorkQueue(), WepResolveSymbolFunction, resolveContext);
}

HICON WepGetWindowIcon(
    _In_ HWND WindowHandle
    )
{
    static HICON (WINAPI *InternalGetWindowIcon_I)(
        _In_ HWND hwnd,
        _In_ UINT iconType
        ) = NULL;

    if (!InternalGetWindowIcon_I)
        InternalGetWindowIcon_I = PhGetModuleProcAddress(L"user32.dll", "InternalGetWindowIcon");

    if (!InternalGetWindowIcon_I)
        return NULL;

    return InternalGetWindowIcon_I(WindowHandle, ICON_BIG);
}

PPH_STRING WepFormatRect(
    _In_ PRECT Rect
    )
{
    return PhaFormatString(L"(%ld, %ld) - (%ld, %ld) [%ldx%ld]",
        Rect->left, Rect->top, Rect->right, Rect->bottom,
        Rect->right - Rect->left, Rect->bottom - Rect->top);
}

VOID WepRefreshWindowGeneralInfoSymbols(
    _In_ HWND ListViewHandle,
    _In_ PWINDOW_PROPERTIES_CONTEXT Context
    )
{
    if (Context->WndProcResolving != 0)
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_WNDPROC, 1, PhaFormatString(L"0x%Ix (resolving...)", Context->WndProc)->Buffer);
    else if (Context->WndProcSymbol)
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_WNDPROC, 1, PhaFormatString(L"0x%Ix (%s)", Context->WndProc, Context->WndProcSymbol->Buffer)->Buffer);
    else if (Context->WndProc != 0)
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_WNDPROC, 1, PhaFormatString(L"0x%Ix", Context->WndProc)->Buffer);
    else
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_WNDPROC, 1, L"Unknown");

    if (Context->DlgProcResolving != 0)
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_DLGPROC, 1, PhaFormatString(L"0x%Ix (resolving...)", Context->DlgProc)->Buffer);
    else if (Context->DlgProcSymbol)
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_DLGPROC, 1, PhaFormatString(L"0x%Ix (%s)", Context->DlgProc, Context->DlgProcSymbol->Buffer)->Buffer);
    else if (Context->DlgProc != 0)
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_DLGPROC, 1, PhaFormatString(L"0x%Ix", Context->DlgProc)->Buffer);
    else if (Context->WndProc != 0)
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_DLGPROC, 1, L"N/A");
    else
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_DLGPROC, 1, L"Unknown");
}

VOID WepRefreshWindowGeneralInfo(
    _In_ HWND hwndDlg,
    _In_ HWND ListViewHandle,
    _In_ PWINDOW_PROPERTIES_CONTEXT Context
    )
{
    WINDOWINFO windowInfo = { sizeof(WINDOWINFO) };
    WINDOWPLACEMENT windowPlacement = { sizeof(WINDOWPLACEMENT) };
    MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
    HANDLE processHandle;
    PPH_STRING fileName = NULL;
    PPH_STRING appIdText;
    HMENU menuHandle;
    PVOID instanceHandle;
    PVOID userdataHandle;
    ULONG windowId;
    ULONG windowExtra;
    HFONT fontHandle;

    menuHandle = GetMenu(Context->WindowHandle);
    instanceHandle = (PVOID)GetWindowLongPtr(Context->WindowHandle, GWLP_HINSTANCE);
    userdataHandle = (PVOID)GetWindowLongPtr(Context->WindowHandle, GWLP_USERDATA);
    windowId = (ULONG)GetWindowLongPtr(Context->WindowHandle, GWLP_ID);
    windowExtra = (ULONG)GetClassLongPtr(Context->WindowHandle, GCL_CBWNDEXTRA);
    // TODO: GetWindowLongPtr(Context->WindowHandle, GCLP_WNDPROC);

    PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_TEXT, 1, PhGetStringOrEmpty(PH_AUTO(PhGetWindowText(Context->WindowHandle))));
    PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_THREAD, 1, PH_AUTO_T(PH_STRING, PhGetClientIdName(&Context->ClientId))->Buffer);

    if (GetWindowInfo(Context->WindowHandle, &windowInfo))
    {
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_RECT, 1, WepFormatRect(&windowInfo.rcWindow)->Buffer);
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLIENTRECT, 1, WepFormatRect(&windowInfo.rcClient)->Buffer);
    }
    else
    {
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_RECT, 1, L"N/A");
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLIENTRECT, 1, L"N/A");
    }

    if (GetWindowPlacement(Context->WindowHandle, &windowPlacement))
    {
        // The rectangle is in workspace coordinates. Convert the values back to screen coordinates.
        if (GetMonitorInfo(MonitorFromRect(&windowPlacement.rcNormalPosition, MONITOR_DEFAULTTOPRIMARY), &monitorInfo))
        {
            windowPlacement.rcNormalPosition.left += monitorInfo.rcWork.left;
            windowPlacement.rcNormalPosition.top += monitorInfo.rcWork.top;
            windowPlacement.rcNormalPosition.right += monitorInfo.rcWork.left;
            windowPlacement.rcNormalPosition.bottom += monitorInfo.rcWork.top;
        }

        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_NORMALRECT, 1, WepFormatRect(&windowPlacement.rcNormalPosition)->Buffer);
    }
    else
    {
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_NORMALRECT, 1, L"N/A");
    }

    if (NT_SUCCESS(PhOpenProcess(&processHandle, PROCESS_QUERY_LIMITED_INFORMATION, Context->ClientId.UniqueProcess)))
    {
        if (NT_SUCCESS(PhGetProcessMappedFileName(processHandle, instanceHandle, &fileName)))
        {
            PhMoveReference(&fileName, PhGetFileName(fileName));
            PhMoveReference(&fileName, PhGetBaseName(fileName));
        }

        NtClose(processHandle);
    }

    if (fileName)
    {
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_INSTANCE, 1, PhaFormatString(
            L"0x%Ix (%s)", 
            (ULONG_PTR)instanceHandle,
            PhGetStringOrEmpty(fileName)
            )->Buffer);
        PhDereferenceObject(fileName);
    }
    else
    {
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_INSTANCE, 1, PhaFormatString(
            L"0x%Ix", 
            (ULONG_PTR)instanceHandle
            )->Buffer);
    }

    PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_MENUHANDLE, 1, PhaFormatString(L"0x%Ix", (ULONG_PTR)menuHandle)->Buffer);
    PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_USERDATA, 1, PhaFormatString(L"0x%Ix", (ULONG_PTR)userdataHandle)->Buffer);
    PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_UNICODE, 1, IsWindowUnicode(Context->WindowHandle) ? L"Yes" : L"No");
    PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_WNDEXTRA, 1, PhaFormatSize(windowExtra, ULONG_MAX)->Buffer);
    PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_DLGCTLID, 1, PhaFormatString(L"%lu", windowId)->Buffer);

    if (fontHandle = (HFONT)SendMessage(Context->WindowHandle, WM_GETFONT, 0, 0))
    {
        LOGFONT logFont;

        if (GetObject(fontHandle, sizeof(LOGFONT), &logFont))
            PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_FONTNAME, 1, logFont.lfFaceName);
        else
            PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_FONTNAME, 1, L"N/A");
    }
    else
    {
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_FONTNAME, 1, L"N/A");
    }

    //ULONG version;
    //if (SendMessageTimeout(Context->WindowHandle, CCM_GETVERSION, 0, 0, SMTO_ABORTIFHUNG, 5000, &version))
    //WepQueryProcessWndProc(Context);

    if (Context->WndProc != 0)
    {
        Context->WndProcResolving++;
        WepQueueResolveSymbol(Context, hwndDlg, Context->WndProc, 1);
    }

    if (Context->DlgProc != 0)
    {
        Context->DlgProcResolving++;
        WepQueueResolveSymbol(Context, hwndDlg, Context->DlgProc, 2);
    }

    WepRefreshWindowGeneralInfoSymbols(ListViewHandle, Context);

    if (PhAppResolverGetAppIdForWindow(Context->WindowHandle, &appIdText))
    {
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_APPID, 1, appIdText->Buffer);
        PhDereferenceObject(appIdText);
    }
}

VOID WepRefreshWindowStyles(
    _In_ HWND ListViewHandle,
    _In_ PWINDOW_PROPERTIES_CONTEXT Context
    )
{
    WINDOWINFO windowInfo = { sizeof(WINDOWINFO) };
    PH_STRING_BUILDER styleStringBuilder;
    PH_STRING_BUILDER styleExStringBuilder;
    ULONG i;

    if (GetWindowInfo(Context->WindowHandle, &windowInfo))
    {
        PhInitializeStringBuilder(&styleStringBuilder, 100);
        PhInitializeStringBuilder(&styleExStringBuilder, 100);

        PhAppendFormatStringBuilder(&styleStringBuilder, L"0x%x (", windowInfo.dwStyle);
        PhAppendFormatStringBuilder(&styleExStringBuilder, L"0x%x (", windowInfo.dwExStyle);

        for (i = 0; i < RTL_NUMBER_OF(WepStylePairs); i++)
        {
            if (windowInfo.dwStyle & WepStylePairs[i].Integer)
            {
                // Skip irrelevant styles.
                if (WepStylePairs[i].Integer == WS_MAXIMIZEBOX ||
                    WepStylePairs[i].Integer == WS_MINIMIZEBOX)
                {
                    if (windowInfo.dwStyle & WS_CHILD)
                        continue;
                }

                if (WepStylePairs[i].Integer == WS_TABSTOP ||
                    WepStylePairs[i].Integer == WS_GROUP)
                {
                    if (!(windowInfo.dwStyle & WS_CHILD))
                        continue;
                }

                PhAppendStringBuilder2(&styleStringBuilder, WepStylePairs[i].String);
                PhAppendStringBuilder2(&styleStringBuilder, L", ");
            }
        }

        if (PhEndsWithString2(styleStringBuilder.String, L", ", FALSE))
        {
            PhRemoveEndStringBuilder(&styleStringBuilder, 2);
            PhAppendCharStringBuilder(&styleStringBuilder, ')');
        }
        else
        {
            PhRemoveEndStringBuilder(&styleStringBuilder, 1);
        }

        for (i = 0; i < RTL_NUMBER_OF(WepExtendedStylePairs); i++)
        {
            if (windowInfo.dwExStyle & WepExtendedStylePairs[i].Integer)
            {
                PhAppendStringBuilder2(&styleExStringBuilder, WepExtendedStylePairs[i].String);
                PhAppendStringBuilder2(&styleExStringBuilder, L", ");
            }
        }

        if (PhEndsWithString2(styleExStringBuilder.String, L", ", FALSE))
        {
            PhRemoveEndStringBuilder(&styleExStringBuilder, 2);
            PhAppendCharStringBuilder(&styleExStringBuilder, ')');
        }
        else
        {
            PhRemoveEndStringBuilder(&styleExStringBuilder, 1);
        }

        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_STYLES, 1, PhFinalStringBuilderString(&styleStringBuilder)->Buffer);
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_EXSTYLES, 1, PhFinalStringBuilderString(&styleExStringBuilder)->Buffer);

        PhDeleteStringBuilder(&styleStringBuilder);
        PhDeleteStringBuilder(&styleExStringBuilder);
    }
    else
    {
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_STYLES, 1, L"N/A");
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_EXSTYLES, 1, L"N/A");
    }
}

VOID WepRefreshClassStyles(
    _In_ HWND ListViewHandle,
    _In_ PWINDOW_PROPERTIES_CONTEXT Context
    )
{
    PH_STRING_BUILDER stringBuilder;
    ULONG i;

    PhInitializeStringBuilder(&stringBuilder, 100);
    PhAppendFormatStringBuilder(&stringBuilder, L"0x%x (", Context->ClassInfo.style);

    for (i = 0; i < RTL_NUMBER_OF(WepClassStylePairs); i++)
    {
        if (Context->ClassInfo.style & WepClassStylePairs[i].Integer)
        {
            PhAppendStringBuilder2(&stringBuilder, WepClassStylePairs[i].String);
            PhAppendStringBuilder2(&stringBuilder, L", ");
        }
    }

    if (PhEndsWithString2(stringBuilder.String, L", ", FALSE))
    {
        PhRemoveEndStringBuilder(&stringBuilder, 2);
        PhAppendCharStringBuilder(&stringBuilder, ')');
    }
    else
    {
        // No styles. Remove the brackets.
        PhRemoveEndStringBuilder(&stringBuilder, 1);
    }

    PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLASS_STYLES, 1, PhFinalStringBuilderString(&stringBuilder)->Buffer);
    PhDeleteStringBuilder(&stringBuilder);
}

VOID WepRefreshClassModule(
    _In_ HWND ListViewHandle,
    _In_ PWINDOW_PROPERTIES_CONTEXT Context
    )
{
    HANDLE processHandle;
    PPH_STRING fileName = NULL;
    PVOID instanceHandle = (PVOID)GetClassLongPtr(Context->WindowHandle, GCLP_HMODULE);

    if (NT_SUCCESS(PhOpenProcess(&processHandle, PROCESS_QUERY_LIMITED_INFORMATION, Context->ClientId.UniqueProcess)))
    {
        if (NT_SUCCESS(PhGetProcessMappedFileName(processHandle, instanceHandle, &fileName)))
        {
            PhMoveReference(&fileName, PhGetFileName(fileName));
            PhMoveReference(&fileName, PhGetBaseName(fileName));
        }

        NtClose(processHandle);
    }

    if (fileName)
    {
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLASS_INSTANCE, 1, PhaFormatString(
            L"0x%Ix (%s)",
            (ULONG_PTR)instanceHandle,
            PhGetStringOrEmpty(fileName)
            )->Buffer);
        PhDereferenceObject(fileName);
    }
    else
    {
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLASS_INSTANCE, 1, PhaFormatString(
            L"0x%Ix",
            (ULONG_PTR)instanceHandle
            )->Buffer);
    }
}

VOID WepRefreshWindowClassInfoSymbols(
    _In_ HWND ListViewHandle,
    _In_ PWINDOW_PROPERTIES_CONTEXT Context
    )
{
    if (Context->ClassWndProcResolving != 0)
    {
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLASS_WNDPROC, 1, PhaFormatString(
            L"0x%Ix (resolving...)",
            (ULONG_PTR)Context->ClassInfo.lpfnWndProc
            )->Buffer);
    }
    else if (Context->ClassWndProcSymbol)
    {
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLASS_WNDPROC, 1, PhaFormatString(
            L"0x%Ix (%s)",
            (ULONG_PTR)Context->ClassInfo.lpfnWndProc,
            Context->ClassWndProcSymbol->Buffer
            )->Buffer);
    }
    else if (Context->ClassInfo.lpfnWndProc)
    {
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLASS_WNDPROC, 1, PhaFormatString(
            L"0x%Ix",
            (ULONG_PTR)Context->ClassInfo.lpfnWndProc
            )->Buffer);
    }
    else
    {
        PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLASS_WNDPROC, 1, L"Unknown");
    }
}

VOID WepRefreshWindowClassInfo(
    _In_ HWND hwndDlg,
    _In_ HWND ListViewHandle,
    _In_ PWINDOW_PROPERTIES_CONTEXT Context
    )
{
    WCHAR className[256];

    if (!GetClassName(Context->WindowHandle, className, RTL_NUMBER_OF(className)))
        className[0] = UNICODE_NULL;

    Context->ClassInfo.cbSize = sizeof(WNDCLASSEX);
    GetClassInfoEx(NULL, className, &Context->ClassInfo);

    PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLASS_NAME, 1, className);
    PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLASS_ATOM, 1, PhaFormatString(L"0x%Ix", GetClassLongPtr(Context->WindowHandle, GCW_ATOM))->Buffer);
    PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLASS_LARGEICON, 1, PhaFormatString(L"0x%Ix", (ULONG_PTR)Context->ClassInfo.hIcon)->Buffer);
    PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLASS_SMALLICON, 1, PhaFormatString(L"0x%Ix", (ULONG_PTR)Context->ClassInfo.hIconSm)->Buffer);
    PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLASS_MENUNAME, 1, PhaFormatString(L"0x%Ix", (ULONG_PTR)Context->ClassInfo.lpszMenuName)->Buffer);
    PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLASS_CURSOR, 1, PhaFormatString(L"0x%Ix", (ULONG_PTR)Context->ClassInfo.hCursor)->Buffer);
    PhSetListViewSubItem(ListViewHandle, WINDOW_PROPERTIES_INDEX_CLASS_BACKBRUSH, 1, PhaFormatString(L"0x%Ix", (ULONG_PTR)Context->ClassInfo.hbrBackground)->Buffer);

    WepRefreshClassStyles(ListViewHandle, Context);
    WepRefreshClassModule(ListViewHandle, Context);

    if (Context->ClassInfo.lpfnWndProc)
    {
        Context->ClassWndProcResolving++;
        WepQueueResolveSymbol(Context, hwndDlg, (ULONG_PTR)Context->ClassInfo.lpfnWndProc, 3);
    }

    WepRefreshWindowClassInfoSymbols(ListViewHandle, Context);
}

static BOOLEAN WepWindowHasAutomationProvider(
    _In_ HWND WindowHandle
    )
{
    static PH_INITONCE initOnce = PH_INITONCE_INIT;
    static BOOL (WINAPI *UiaHasServerSideProvider_I)(
        _In_ HWND WindowHandle
        );

    if (PhBeginInitOnce(&initOnce))
    {
        HANDLE moduleHandle;

        if (moduleHandle = LoadLibrary(L"uiautomationcore.dll"))
        {
            UiaHasServerSideProvider_I = PhGetProcedureAddress(moduleHandle, "UiaHasServerSideProvider", 0);
        }

        PhEndInitOnce(&initOnce);
    }

    if (!UiaHasServerSideProvider_I)
        return FALSE;

    return !!UiaHasServerSideProvider_I(WindowHandle);
}

VOID WepRefreshAutomationProvider(
    _In_ PWINDOW_PROPERTIES_CONTEXT Context
    )
{
    if (WepWindowHasAutomationProvider(Context->WindowHandle))
        PhSetListViewSubItem(Context->ListViewHandle, WINDOW_PROPERTIES_INDEX_AUTOMATION, 1, L"Yes");
    else
        PhSetListViewSubItem(Context->ListViewHandle, WINDOW_PROPERTIES_INDEX_AUTOMATION, 1, L"No");
}

VOID WepGeneralAddListViewItemGroups(
    _In_ HWND ListViewHandle
    )
{
    ListView_EnableGroupView(ListViewHandle, TRUE);
    PhAddListViewGroup(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, L"General");
    PhAddListViewGroup(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_CLASS, L"Class");

    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_APPID, L"AppId", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_TEXT, L"Text", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_THREAD, L"Thread", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_RECT, L"Rectangle", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_NORMALRECT, L"Normal rectangle", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_CLIENTRECT, L"Client rectangle", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_INSTANCE, L"Instance handle", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_MENUHANDLE, L"Menu handle", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_USERDATA, L"User data", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_UNICODE, L"Unicode", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_WNDEXTRA, L"Window extra bytes", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_WNDPROC, L"Window procedure", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_DLGPROC, L"Dialog procedure", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_DLGCTLID, L"Dialog control ID", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_FONTNAME, L"Font", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_STYLES, L"Styles", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_EXSTYLES, L"Extended styles", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_GENERAL, WINDOW_PROPERTIES_INDEX_AUTOMATION, L"Automation server", NULL);

    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_CLASS, WINDOW_PROPERTIES_INDEX_CLASS_NAME, L"Name", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_CLASS, WINDOW_PROPERTIES_INDEX_CLASS_ATOM, L"Atom", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_CLASS, WINDOW_PROPERTIES_INDEX_CLASS_STYLES, L"Styles", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_CLASS, WINDOW_PROPERTIES_INDEX_CLASS_INSTANCE, L"Instance handle", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_CLASS, WINDOW_PROPERTIES_INDEX_CLASS_LARGEICON, L"Large icon handle", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_CLASS, WINDOW_PROPERTIES_INDEX_CLASS_SMALLICON, L"Small icon handle", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_CLASS, WINDOW_PROPERTIES_INDEX_CLASS_CURSOR, L"Cursor handle", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_CLASS, WINDOW_PROPERTIES_INDEX_CLASS_BACKBRUSH, L"Background brush", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_CLASS, WINDOW_PROPERTIES_INDEX_CLASS_MENUNAME, L"Menu name", NULL);
    PhAddListViewGroupItem(ListViewHandle, WINDOW_PROPERTIES_CATEGORY_CLASS, WINDOW_PROPERTIES_INDEX_CLASS_WNDPROC, L"Window procedure", NULL);
}

INT_PTR CALLBACK WepWindowGeneralDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PWINDOW_PROPERTIES_CONTEXT context;
    LPPROPSHEETPAGE propSheetPage;
    PPV_PROPPAGECONTEXT propPageContext;

    if (!PvPropPageDlgProcHeader(hwndDlg, uMsg, lParam, &propSheetPage, &propPageContext))
        return FALSE;

    context = (PWINDOW_PROPERTIES_CONTEXT)propPageContext->Context;

    if (!context)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            context->ListViewHandle = GetDlgItem(hwndDlg, IDC_WINDOWINFO);

            // HACK
            SendMessage(GetParent(hwndDlg), WM_SETICON, ICON_SMALL, (LPARAM)PH_LOAD_SHARED_ICON_SMALL(WE_PhInstanceHandle, MAKEINTRESOURCE(PHAPP_IDI_PROCESSHACKER)));
            SendMessage(GetParent(hwndDlg), WM_SETICON, ICON_BIG, (LPARAM)PH_LOAD_SHARED_ICON_LARGE(WE_PhInstanceHandle, MAKEINTRESOURCE(PHAPP_IDI_PROCESSHACKER)));

            if (PhGetIntegerPairSetting(SETTING_NAME_WINDOWS_PROPERTY_POSITION).X == 0) // HACK
                PhCenterWindow(GetParent(hwndDlg), context->ParentWindowHandle);

            PhSetListViewStyle(context->ListViewHandle, FALSE, TRUE);
            PhSetControlTheme(context->ListViewHandle, L"explorer");
            PhAddListViewColumn(context->ListViewHandle, 0, 0, 0, LVCFMT_LEFT, 180, L"Name");
            PhAddListViewColumn(context->ListViewHandle, 1, 1, 1, LVCFMT_LEFT, 200, L"Value");
            PhSetExtendedListView(context->ListViewHandle);
            PhLoadListViewColumnsFromSetting(SETTING_NAME_WINDOWS_PROPERTY_COLUMNS, context->ListViewHandle);

            WepGeneralAddListViewItemGroups(context->ListViewHandle);
            WepRefreshWindowGeneralInfo(hwndDlg, context->ListViewHandle, context);
            WepRefreshWindowStyles(context->ListViewHandle, context);
            WepRefreshWindowClassInfo(hwndDlg, context->ListViewHandle, context);
            WepRefreshAutomationProvider(context);

            if (!!PhGetIntegerSetting(L"EnableThemeSupport")) // TODO: Required for compat (dmex)
                PhInitializeWindowTheme(GetParent(hwndDlg), !!PhGetIntegerSetting(L"EnableThemeSupport"));
            else
                PhInitializeWindowTheme(hwndDlg, FALSE);
        }
        break;
    case WM_DESTROY:
        {
            PhSaveListViewColumnsToSetting(SETTING_NAME_WINDOWS_PROPERTY_COLUMNS, context->ListViewHandle);
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!propPageContext->LayoutInitialized)
            {
                PPH_LAYOUT_ITEM dialogItem;

                dialogItem = PvAddPropPageLayoutItem(hwndDlg, hwndDlg, PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);
                PvAddPropPageLayoutItem(hwndDlg, context->ListViewHandle, dialogItem, PH_ANCHOR_ALL);
                PvDoPropPageLayout(hwndDlg);

                propPageContext->LayoutInitialized = TRUE;
            }
        }
        break;
    case WM_NOTIFY:
        {
            PhHandleListViewNotifyBehaviors(lParam, context->ListViewHandle, PH_LIST_VIEW_DEFAULT_1_BEHAVIORS);
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

                    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, PHAPP_IDC_COPY, L"&Copy", NULL, NULL), ULONG_MAX);
                    PhInsertCopyListViewEMenuItem(menu, PHAPP_IDC_COPY, context->ListViewHandle);

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
                        if (!PhHandleCopyListViewEMenuItem(item))
                        {
                            switch (item->Id)
                            {
                            case PHAPP_IDC_COPY:
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
    case WEM_RESOLVE_DONE:
        {
            PSYMBOL_RESOLVE_CONTEXT resolveContext = (PSYMBOL_RESOLVE_CONTEXT)lParam;

            if (resolveContext->Id == 1)
            {
                PhAcquireQueuedLockExclusive(&context->ResolveListLock);
                RemoveEntryList(&resolveContext->ListEntry);
                PhReleaseQueuedLockExclusive(&context->ResolveListLock);

                if (resolveContext->ResolveLevel != PhsrlModule && resolveContext->ResolveLevel != PhsrlFunction)
                    PhClearReference(&resolveContext->Symbol);

                PhMoveReference(&context->WndProcSymbol, resolveContext->Symbol);
                PhFree(resolveContext);

                context->WndProcResolving--;
            }
            else if (resolveContext->Id == 2)
            {
                PhAcquireQueuedLockExclusive(&context->ResolveListLock);
                RemoveEntryList(&resolveContext->ListEntry);
                PhReleaseQueuedLockExclusive(&context->ResolveListLock);

                if (resolveContext->ResolveLevel != PhsrlModule && resolveContext->ResolveLevel != PhsrlFunction)
                    PhClearReference(&resolveContext->Symbol);

                PhMoveReference(&context->DlgProcSymbol, resolveContext->Symbol);
                PhFree(resolveContext);

                context->DlgProcResolving--;
            }
            else if (resolveContext->Id == 3)
            {
                PhAcquireQueuedLockExclusive(&context->ResolveListLock);
                RemoveEntryList(&resolveContext->ListEntry);
                PhReleaseQueuedLockExclusive(&context->ResolveListLock);

                if (resolveContext->ResolveLevel != PhsrlModule && resolveContext->ResolveLevel != PhsrlFunction)
                    PhClearReference(&resolveContext->Symbol);

                PhMoveReference(&context->ClassWndProcSymbol, resolveContext->Symbol);
                PhFree(resolveContext);

                context->ClassWndProcResolving--;
            }

            WepRefreshWindowGeneralInfoSymbols(context->ListViewHandle, context);
            WepRefreshWindowClassInfoSymbols(context->ListViewHandle, context);
        }
        break;
    }

    return FALSE;
}

BOOL CALLBACK EnumPropsExCallback(
    _In_ HWND hwnd,
    _In_ PWSTR lpszString,
    _In_ HANDLE hData,
    _In_ ULONG_PTR dwData
    )
{
    INT lvItemIndex;
    WCHAR value[PH_PTR_STR_LEN_1];

    if ((ULONG_PTR)lpszString < USHRT_MAX) // This is an integer atom.
    {
        PPH_STRING propName;

        propName = PhFormatString(L"#%hu", (USHORT)lpszString);
        lvItemIndex = PhAddListViewItem((HWND)dwData, MAXINT, propName->Buffer, NULL);
        PhDereferenceObject(propName);
    }
    else
    {
        lvItemIndex = PhAddListViewItem((HWND)dwData, MAXINT, lpszString, NULL);
    }

    PhPrintPointer(value, (PVOID)hData);
    PhSetListViewSubItem((HWND)dwData, lvItemIndex, 1, value);

    return TRUE;
}

VOID WepRefreshWindowProps(
    _In_ HWND hwndDlg,
    _In_ HWND ListViewHandle,
    _In_ PWINDOW_PROPERTIES_CONTEXT Context
    )
{
    ExtendedListView_SetRedraw(ListViewHandle, FALSE);
    ListView_DeleteAllItems(ListViewHandle);

    EnumPropsEx(Context->WindowHandle, EnumPropsExCallback, (LPARAM)ListViewHandle);

    ExtendedListView_SortItems(ListViewHandle);
    ExtendedListView_SetRedraw(ListViewHandle, TRUE);
}

INT_PTR CALLBACK WepWindowPropertiesDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PWINDOW_PROPERTIES_CONTEXT context;
    LPPROPSHEETPAGE propSheetPage;
    PPV_PROPPAGECONTEXT propPageContext;

    if (!PvPropPageDlgProcHeader(hwndDlg, uMsg, lParam, &propSheetPage, &propPageContext))
        return FALSE;

    context = (PWINDOW_PROPERTIES_CONTEXT)propPageContext->Context;

    if (!context)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            HWND lvHandle;

            lvHandle = GetDlgItem(hwndDlg, IDC_LIST);
            PhSetListViewStyle(lvHandle, FALSE, TRUE);
            PhSetControlTheme(lvHandle, L"explorer");

            PhAddListViewColumn(lvHandle, 0, 0, 0, LVCFMT_LEFT, 160, L"Name");
            PhAddListViewColumn(lvHandle, 1, 1, 1, LVCFMT_LEFT, 100, L"Value");
            PhSetExtendedListView(lvHandle);
            PhLoadListViewColumnsFromSetting(SETTING_NAME_WINDOWS_PROPLIST_COLUMNS, lvHandle);

            WepRefreshWindowProps(hwndDlg, lvHandle, context);

            PhInitializeWindowTheme(hwndDlg, !!PhGetIntegerSetting(L"EnableThemeSupport"));
        }
        break;
    case WM_DESTROY:
        {
            PhSaveListViewColumnsToSetting(SETTING_NAME_WINDOWS_PROPLIST_COLUMNS, GetDlgItem(hwndDlg, IDC_LIST));
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDC_REFRESH:
                WepRefreshWindowProps(hwndDlg, GetDlgItem(hwndDlg, IDC_LIST), context);
                break;
            }
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!propPageContext->LayoutInitialized)
            {
                PPH_LAYOUT_ITEM dialogItem;

                dialogItem = PvAddPropPageLayoutItem(hwndDlg, hwndDlg, PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);
                PvAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_LIST), dialogItem, PH_ANCHOR_ALL);
                PvDoPropPageLayout(hwndDlg);

                propPageContext->LayoutInitialized = TRUE;
            }
        }
        break;
    case WM_NOTIFY:
        {
            PhHandleListViewNotifyBehaviors(lParam, GetDlgItem(hwndDlg, IDC_LIST), PH_LIST_VIEW_DEFAULT_1_BEHAVIORS);
        }
        break;
    case WM_CONTEXTMENU:
        {
            HWND listViewHandle = GetDlgItem(hwndDlg, IDC_LIST);

            if ((HWND)wParam == listViewHandle)
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

                PhGetSelectedListViewItemParams(listViewHandle, &listviewItems, &numberOfItems);

                if (numberOfItems != 0)
                {
                    menu = PhCreateEMenu();

                    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, PHAPP_IDC_COPY, L"&Copy", NULL, NULL), ULONG_MAX);
                    PhInsertCopyListViewEMenuItem(menu, PHAPP_IDC_COPY, listViewHandle);

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
                        if (!PhHandleCopyListViewEMenuItem(item))
                        {
                            switch (item->Id)
                            {
                            case PHAPP_IDC_COPY:
                                {
                                    PhCopyListView(listViewHandle);
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

    return FALSE;
}

VOID WepRefreshWindowPropertyStorage(
    _In_ HWND hwndDlg,
    _In_ HWND ListViewHandle,
    _In_ PWINDOW_PROPERTIES_CONTEXT Context
    )
{
    IPropertyStore *propstore;
    ULONG count;
    ULONG i;

    ExtendedListView_SetRedraw(ListViewHandle, FALSE);
    ListView_DeleteAllItems(ListViewHandle);

    if (SUCCEEDED(SHGetPropertyStoreForWindow(Context->WindowHandle, &IID_IPropertyStore, &propstore)))
    {
        if (SUCCEEDED(IPropertyStore_GetCount(propstore, &count)))
        {
            for (i = 0; i < count; i++)
            {
                PROPERTYKEY propkey;

                if (SUCCEEDED(IPropertyStore_GetAt(propstore, i, &propkey)))
                {
                    INT lvItemIndex;
                    PROPVARIANT propKeyVariant = { 0 };
                    PWSTR propKeyName;

                    if (SUCCEEDED(PSGetNameFromPropertyKey(&propkey, &propKeyName)))
                    {
                        lvItemIndex = PhAddListViewItem(ListViewHandle, MAXINT, propKeyName, NULL);
                        CoTaskMemFree(propKeyName);
                    }
                    else
                    {
                        WCHAR propKeyString[PKEYSTR_MAX];

                        if (SUCCEEDED(PSStringFromPropertyKey(&propkey, propKeyString, RTL_NUMBER_OF(propKeyString))))
                            lvItemIndex = PhAddListViewItem(ListViewHandle, MAXINT, propKeyString, NULL);
                        else
                            lvItemIndex = PhAddListViewItem(ListViewHandle, MAXINT, L"Unknown", NULL);
                    }

                    if (SUCCEEDED(IPropertyStore_GetValue(propstore, &propkey, &propKeyVariant)))
                    {
                        if (SUCCEEDED(PSFormatForDisplayAlloc(&propkey, &propKeyVariant, PDFF_DEFAULT, &propKeyName)))
                        {
                            PhSetListViewSubItem(ListViewHandle, lvItemIndex, 1, propKeyName);
                            CoTaskMemFree(propKeyName);
                        }

                        //if (SUCCEEDED(PropVariantToStringAlloc(&propKeyVariant, &propKeyName)))
                        //{
                        //    PhSetListViewSubItem(ListViewHandle, lvItemIndex, 1, propKeyName);
                        //    CoTaskMemFree(propKeyName);
                        //}

                        PropVariantClear(&propKeyVariant);
                    }
                }
            }
        }

        IPropertyStore_Release(propstore);
    }

    ExtendedListView_SortItems(ListViewHandle);
    ExtendedListView_SetRedraw(ListViewHandle, TRUE);
}

INT_PTR CALLBACK WepWindowPropStoreDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PWINDOW_PROPERTIES_CONTEXT context;
    LPPROPSHEETPAGE propSheetPage;
    PPV_PROPPAGECONTEXT propPageContext;

    if (!PvPropPageDlgProcHeader(hwndDlg, uMsg, lParam, &propSheetPage, &propPageContext))
        return FALSE;

    context = (PWINDOW_PROPERTIES_CONTEXT)propPageContext->Context;

    if (!context)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            HWND lvHandle;

            lvHandle = GetDlgItem(hwndDlg, IDC_LIST);
            PhSetListViewStyle(lvHandle, FALSE, TRUE);
            PhSetControlTheme(lvHandle, L"explorer");

            PhAddListViewColumn(lvHandle, 0, 0, 0, LVCFMT_LEFT, 160, L"Name");
            PhAddListViewColumn(lvHandle, 1, 1, 1, LVCFMT_LEFT, 100, L"Value");
            PhSetExtendedListView(lvHandle);
            PhLoadListViewColumnsFromSetting(SETTING_NAME_WINDOWS_PROPSTORAGE_COLUMNS, lvHandle);

            WepRefreshWindowPropertyStorage(hwndDlg, lvHandle, context);

            PhInitializeWindowTheme(hwndDlg, !!PhGetIntegerSetting(L"EnableThemeSupport"));
        }
        break;
    case WM_DESTROY:
        {
            PhSaveListViewColumnsToSetting(SETTING_NAME_WINDOWS_PROPSTORAGE_COLUMNS, GetDlgItem(hwndDlg, IDC_LIST));
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (!propPageContext->LayoutInitialized)
            {
                PPH_LAYOUT_ITEM dialogItem;

                dialogItem = PvAddPropPageLayoutItem(hwndDlg, hwndDlg, PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);
                PvAddPropPageLayoutItem(hwndDlg, GetDlgItem(hwndDlg, IDC_LIST), dialogItem, PH_ANCHOR_ALL);
                PvDoPropPageLayout(hwndDlg);

                propPageContext->LayoutInitialized = TRUE;
            }
        }
        break;
    case WM_NOTIFY:
        {
            PhHandleListViewNotifyBehaviors(lParam, GetDlgItem(hwndDlg, IDC_LIST), PH_LIST_VIEW_DEFAULT_1_BEHAVIORS);
        }
        break;
    case WM_CONTEXTMENU:
        {
            HWND listViewHandle = GetDlgItem(hwndDlg, IDC_LIST);

            if ((HWND)wParam == listViewHandle)
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

                PhGetSelectedListViewItemParams(listViewHandle, &listviewItems, &numberOfItems);

                if (numberOfItems != 0)
                {
                    menu = PhCreateEMenu();

                    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, PHAPP_IDC_COPY, L"&Copy", NULL, NULL), ULONG_MAX);
                    PhInsertCopyListViewEMenuItem(menu, PHAPP_IDC_COPY, listViewHandle);

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
                        if (!PhHandleCopyListViewEMenuItem(item))
                        {
                            switch (item->Id)
                            {
                            case PHAPP_IDC_COPY:
                                {
                                    PhCopyListView(listViewHandle);
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

    return FALSE;
}
