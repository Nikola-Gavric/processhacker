/*
 * Process Hacker .NET Tools -
 *   .NET Assemblies property page
 *
 * Copyright (C) 2011-2015 wj32
 * Copyright (C) 2016-2019 dmex
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

#include "dn.h"
#include "clretw.h"
#include <evntcons.h>

#define DNATNC_STRUCTURE 0
#define DNATNC_ID 1
#define DNATNC_FLAGS 2
#define DNATNC_PATH 3
#define DNATNC_NATIVEPATH 4
#define DNATNC_MAXIMUM 5

#define DNA_TYPE_CLR 1
#define DNA_TYPE_APPDOMAIN 2
#define DNA_TYPE_ASSEMBLY 3

#define DN_ASM_UPDATE_MSG (WM_APP + 1)

typedef struct _DNA_NODE
{
    PH_TREENEW_NODE Node;

    struct _DNA_NODE *Parent;
    PPH_LIST Children;

    PH_STRINGREF TextCache[DNATNC_MAXIMUM];

    ULONG Type;
    BOOLEAN IsFakeClr;

    union
    {
        struct
        {
            USHORT ClrInstanceID;
            ULONG StartupFlags;
            PPH_STRING DisplayName;
        } Clr;
        struct
        {
            ULONG64 AppDomainID;
            ULONG AppDomainFlags;
            PPH_STRING DisplayName;
        } AppDomain;
        struct
        {
            ULONG64 AssemblyID;
            ULONG AssemblyFlags;
            PPH_STRING FullyQualifiedAssemblyName;
        } Assembly;
    } u;

    PH_STRINGREF StructureText;
    PPH_STRING IdText;
    PPH_STRING FlagsText;
    PPH_STRING PathText;
    PPH_STRING NativePathText;
} DNA_NODE, *PDNA_NODE;

typedef struct _ASMPAGE_CONTEXT
{
    HWND WindowHandle;
    HWND SearchBoxHandle;
    HWND TreeNewHandle;

    PPH_STRING SearchBoxText;
    PPH_STRING TreeErrorMessage;
    
    PPH_PROCESS_ITEM ProcessItem; 
    PDNA_NODE ClrV2Node;

    union
    {
        ULONG Flags;
        struct
        {
            ULONG EnableStateHighlighting : 1;
            ULONG HideDynamicModules : 1;
            ULONG HighlightDynamicModules : 1;
            ULONG HideNativeModules : 1;
            ULONG HighlightNativeModules : 1;
            ULONG Spare : 27;
        };
    };

    PPH_LIST NodeList;
    PPH_LIST NodeRootList;
    PPH_TN_FILTER_ENTRY TreeFilterEntry;
    PH_TN_FILTER_SUPPORT TreeFilterSupport;
} ASMPAGE_CONTEXT, *PASMPAGE_CONTEXT;

typedef struct _ASMPAGE_QUERY_CONTEXT
{
    HANDLE WindowHandle;

    HANDLE ProcessId;
    ULONG ClrVersions;
    PDNA_NODE ClrV2Node;

    BOOLEAN TraceClrV2;
    ULONG TraceResult;
    LONG TraceHandleActive;
    TRACEHANDLE TraceHandle;

    PPH_LIST NodeList;
    PPH_LIST NodeRootList;
} ASMPAGE_QUERY_CONTEXT, *PASMPAGE_QUERY_CONTEXT;

typedef struct _FLAG_DEFINITION
{
    PWSTR Name;
    ULONG Flag;
} FLAG_DEFINITION, *PFLAG_DEFINITION;

VOID DestroyDotNetTraceQuery(
    _In_ PASMPAGE_QUERY_CONTEXT Context
    );

INT_PTR CALLBACK DotNetAsmPageDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

static UNICODE_STRING DotNetLoggerName = RTL_CONSTANT_STRING(L"PhDnLogger");
static GUID ClrRuntimeProviderGuid = { 0xe13c0d23, 0xccbc, 0x4e12, { 0x93, 0x1b, 0xd9, 0xcc, 0x2e, 0xee, 0x27, 0xe4 } };
static GUID ClrRundownProviderGuid = { 0xa669021c, 0xc450, 0x4609, { 0xa0, 0x35, 0x5a, 0xf5, 0x9a, 0xf4, 0xdf, 0x18 } };

static FLAG_DEFINITION AppDomainFlagsMap[] =
{
    { L"Default", 0x1 },
    { L"Executable", 0x2 },
    { L"Shared", 0x4 }
};

static FLAG_DEFINITION AssemblyFlagsMap[] =
{
    { L"DomainNeutral", 0x1 },
    { L"Dynamic", 0x2 },
    { L"Native", 0x4 },
    { L"Collectible", 0x8 }
};

static FLAG_DEFINITION ModuleFlagsMap[] =
{
    { L"DomainNeutral", 0x1 },
    { L"Native", 0x2 },
    { L"Dynamic", 0x4 },
    { L"Manifest", 0x8 }
};

static FLAG_DEFINITION StartupModeMap[] =
{
    { L"ManagedExe", 0x1 },
    { L"HostedCLR", 0x2 },
    { L"IjwDll", 0x4 },
    { L"ComActivated", 0x8 },
    { L"Other", 0x10 }
};

static FLAG_DEFINITION StartupFlagsMap[] =
{
    { L"CONCURRENT_GC", 0x1 },
    { L"LOADER_OPTIMIZATION_SINGLE_DOMAIN", 0x2 },
    { L"LOADER_OPTIMIZATION_MULTI_DOMAIN", 0x4 },
    { L"LOADER_SAFEMODE", 0x10 },
    { L"LOADER_SETPREFERENCE", 0x100 },
    { L"SERVER_GC", 0x1000 },
    { L"HOARD_GC_VM", 0x2000 },
    { L"SINGLE_VERSION_HOSTING_INTERFACE", 0x4000 },
    { L"LEGACY_IMPERSONATION", 0x10000 },
    { L"DISABLE_COMMITTHREADSTACK", 0x20000 },
    { L"ALWAYSFLOW_IMPERSONATION", 0x40000 },
    { L"TRIM_GC_COMMIT", 0x80000 },
    { L"ETW", 0x100000 },
    { L"SERVER_BUILD", 0x200000 },
    { L"ARM", 0x400000 }
};

VOID AddAsmPageToPropContext(
    _In_ PPH_PLUGIN_PROCESS_PROPCONTEXT PropContext
    )
{
    PhAddProcessPropPage(
        PropContext->PropContext,
        PhCreateProcessPropPageContextEx(PluginInstance->DllBase, MAKEINTRESOURCE(IDD_PROCDOTNETASM), DotNetAsmPageDlgProc, NULL)
        );
}

PPH_STRING FlagsToString(
    _In_ ULONG Flags,
    _In_ PFLAG_DEFINITION Map,
    _In_ ULONG SizeOfMap
    )
{
    PH_STRING_BUILDER sb;
    ULONG i;

    PhInitializeStringBuilder(&sb, 100);

    for (i = 0; i < SizeOfMap / sizeof(FLAG_DEFINITION); i++)
    {
        if (Flags & Map[i].Flag)
        {
            PhAppendStringBuilder2(&sb, Map[i].Name);
            PhAppendStringBuilder2(&sb, L", ");
        }
    }

    if (sb.String->Length != 0)
        PhRemoveEndStringBuilder(&sb, 2);

    return PhFinalStringBuilderString(&sb);
}

PDNA_NODE AddNode(
    _Inout_ PASMPAGE_QUERY_CONTEXT Context
    )
{
    PDNA_NODE node;

    node = PhAllocateZero(sizeof(DNA_NODE));
    PhInitializeTreeNewNode(&node->Node);

    memset(node->TextCache, 0, sizeof(PH_STRINGREF) * DNATNC_MAXIMUM);
    node->Node.TextCache = node->TextCache;
    node->Node.TextCacheSize = DNATNC_MAXIMUM;

    node->Children = PhCreateList(1);

    PhAddItemList(Context->NodeList, node);

    return node;
}

VOID DotNetAsmDestroyNode(
    _In_ PDNA_NODE Node
    )
{
    if (Node->Children) PhDereferenceObject(Node->Children);

    if (Node->Type == DNA_TYPE_CLR)
    {
        if (Node->u.Clr.DisplayName) PhDereferenceObject(Node->u.Clr.DisplayName);
    }
    else if (Node->Type == DNA_TYPE_APPDOMAIN)
    {
        if (Node->u.AppDomain.DisplayName) PhDereferenceObject(Node->u.AppDomain.DisplayName);
    }
    else if (Node->Type == DNA_TYPE_ASSEMBLY)
    {
        if (Node->u.Assembly.FullyQualifiedAssemblyName) PhDereferenceObject(Node->u.Assembly.FullyQualifiedAssemblyName);
    }

    if (Node->IdText) PhDereferenceObject(Node->IdText);
    if (Node->FlagsText) PhDereferenceObject(Node->FlagsText);
    if (Node->PathText) PhDereferenceObject(Node->PathText);
    if (Node->NativePathText) PhDereferenceObject(Node->NativePathText);

    PhFree(Node);
}

PDNA_NODE AddFakeClrNode(
    _In_ PASMPAGE_QUERY_CONTEXT Context,
    _In_ PWSTR DisplayName
    )
{
    PDNA_NODE node;

    node = AddNode(Context);
    node->Type = DNA_TYPE_CLR;
    node->IsFakeClr = TRUE;
    node->u.Clr.ClrInstanceID = 0;
    node->u.Clr.DisplayName = NULL;
    PhInitializeStringRef(&node->StructureText, DisplayName);

    PhAddItemList(Context->NodeRootList, node);

    return node;
}

PDNA_NODE FindClrNode(
    _In_ PASMPAGE_QUERY_CONTEXT Context,
    _In_ USHORT ClrInstanceID
    )
{
    for (ULONG i = 0; i < Context->NodeRootList->Count; i++)
    {
        PDNA_NODE node = Context->NodeRootList->Items[i];

        if (!node->IsFakeClr && node->u.Clr.ClrInstanceID == ClrInstanceID)
            return node;
    }

    return NULL;
}

PDNA_NODE FindAppDomainNode(
    _In_ PDNA_NODE ClrNode,
    _In_ ULONG64 AppDomainID
    )
{
    for (ULONG i = 0; i < ClrNode->Children->Count; i++)
    {
        PDNA_NODE node = ClrNode->Children->Items[i];

        if (node->u.AppDomain.AppDomainID == AppDomainID)
            return node;
    }

    return NULL;
}

PDNA_NODE FindAssemblyNode(
    _In_ PDNA_NODE AppDomainNode,
    _In_ ULONG64 AssemblyID
    )
{
    for (ULONG i = 0; i < AppDomainNode->Children->Count; i++)
    {
        PDNA_NODE node = AppDomainNode->Children->Items[i];

        if (node->u.Assembly.AssemblyID == AssemblyID)
            return node;
    }

    return NULL;
}

PDNA_NODE FindAssemblyNode2(
    _In_ PDNA_NODE ClrNode,
    _In_ ULONG64 AssemblyID
    )
{
    for (ULONG i = 0; i < ClrNode->Children->Count; i++)
    {
        PDNA_NODE appDomainNode = ClrNode->Children->Items[i];

        for (ULONG j = 0; j < appDomainNode->Children->Count; j++)
        {
            PDNA_NODE assemblyNode = appDomainNode->Children->Items[j];

            if (assemblyNode->u.Assembly.AssemblyID == AssemblyID)
                return assemblyNode;
        }
    }

    return NULL;
}

static int __cdecl AssemblyNodeNameCompareFunction(
    _In_ const void *elem1,
    _In_ const void *elem2
    )
{
    PDNA_NODE node1 = *(PDNA_NODE *)elem1;
    PDNA_NODE node2 = *(PDNA_NODE *)elem2;

    return PhCompareStringRef(&node1->StructureText, &node2->StructureText, TRUE);
}
VOID DotNetAsmExpandAllTreeNodes(
    _In_ PASMPAGE_CONTEXT Context,
    _In_ BOOLEAN Expand
    )
{
    ULONG i;
    BOOLEAN needsRestructure = FALSE;

    for (i = 0; i < Context->NodeList->Count; i++)
    {
        PPH_MODULE_NODE node = Context->NodeList->Items[i];

        if (node->Node.Expanded != Expand)
        {
            node->Node.Expanded = Expand;
            needsRestructure = TRUE;
        }
    }

    if (needsRestructure)
        TreeNew_NodesStructured(Context->TreeNewHandle);
}

VOID DotNetAsmDestroyTreeNodes(
    _In_ PASMPAGE_CONTEXT Context
    )
{
    if (Context->NodeList)
    {
        for (ULONG i = 0; i < Context->NodeList->Count; i++)
            DotNetAsmDestroyNode(Context->NodeList->Items[i]);

        PhDereferenceObject(Context->NodeList);
    }

    if (Context->NodeRootList)
        PhDereferenceObject(Context->NodeRootList);
}

VOID DotNetAsmClearTreeNodes(
    _In_ PASMPAGE_CONTEXT Context
    )
{
    if (Context->NodeList)
    {
        for (ULONG i = 0; i < Context->NodeList->Count; i++)
            DotNetAsmDestroyNode(Context->NodeList->Items[i]);

        PhClearList(Context->NodeList);
    }

    if (Context->NodeRootList)
    {
        PhClearList(Context->NodeRootList);
    }

    TreeNew_NodesStructured(Context->TreeNewHandle);
}

PDNA_NODE DotNetAsmGetSelectedTreeNode(
    _In_ PASMPAGE_CONTEXT Context
    )
{
    if (Context->NodeList)
    {
        for (ULONG i = 0; i < Context->NodeList->Count; i++)
        {
            PDNA_NODE node = Context->NodeList->Items[i];

            if (node->Node.Selected)
            {
                return node;
            }
        }
    }

    return NULL;
}

VOID DotNetAsmShowContextMenu(
    _In_ PASMPAGE_CONTEXT Context,
    _In_ PPH_TREENEW_CONTEXT_MENU ContextMenuEvent
    )
{
    PDNA_NODE node;
    PPH_EMENU menu;
    PPH_EMENU_ITEM selectedItem;

    if (!(node = DotNetAsmGetSelectedTreeNode(Context)))
        return;

    menu = PhCreateEMenu();
    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, ID_CLR_INSPECT, L"&Inspect", NULL, NULL), ULONG_MAX);
    PhInsertEMenuItem(menu, PhCreateEMenuSeparator(), ULONG_MAX);
    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, ID_CLR_OPENFILELOCATION, L"Open &file location", NULL, NULL), ULONG_MAX);
    PhInsertEMenuItem(menu, PhCreateEMenuSeparator(), ULONG_MAX);
    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, ID_CLR_COPY, L"&Copy", NULL, NULL), ULONG_MAX);
    PhInsertCopyCellEMenuItem(menu, ID_CLR_COPY, Context->TreeNewHandle, ContextMenuEvent->Column);
    PhSetFlagsEMenuItem(menu, ID_CLR_INSPECT, PH_EMENU_DEFAULT, PH_EMENU_DEFAULT);

    if (PhIsNullOrEmptyString(node->PathText) || !PhDoesFileExistsWin32(PhGetString(node->PathText)))
    {
        PhSetFlagsEMenuItem(menu, ID_CLR_INSPECT, PH_EMENU_DISABLED, PH_EMENU_DISABLED);
        PhSetFlagsEMenuItem(menu, ID_CLR_OPENFILELOCATION, PH_EMENU_DISABLED, PH_EMENU_DISABLED);
    }

    selectedItem = PhShowEMenu(
        menu,
        Context->WindowHandle,
        PH_EMENU_SHOW_LEFTRIGHT,
        PH_ALIGN_LEFT | PH_ALIGN_TOP,
        ContextMenuEvent->Location.x,
        ContextMenuEvent->Location.y
        );

    if (selectedItem && selectedItem->Id != ULONG_MAX)
    {
        BOOLEAN handled = FALSE;

        handled = PhHandleCopyCellEMenuItem(selectedItem);

        if (!handled)
        {
            switch (selectedItem->Id)
            {
            case ID_CLR_INSPECT:
                {
                    if (!PhIsNullOrEmptyString(node->PathText) && PhDoesFileExistsWin32(PhGetString(node->PathText)))
                    {
                        PhShellExecuteUserString(
                            Context->WindowHandle,
                            L"ProgramInspectExecutables",
                            node->PathText->Buffer,
                            FALSE,
                            L"Make sure the PE Viewer executable file is present."
                            );
                    }
                }
                break;
            case ID_CLR_OPENFILELOCATION:
                {
                    if (!PhIsNullOrEmptyString(node->PathText) && PhDoesFileExistsWin32(PhGetString(node->PathText)))
                    {
                        PhShellExploreFile(Context->WindowHandle, node->PathText->Buffer);
                    }
                }
                break;
            case ID_CLR_COPY:
                {
                    PPH_STRING text;

                    text = PhGetTreeNewText(Context->TreeNewHandle, 0);
                    PhSetClipboardString(Context->TreeNewHandle, &text->sr);
                    PhDereferenceObject(text);
                }
                break;
            }
        }
    }

    PhDestroyEMenu(menu);
}

BOOLEAN NTAPI DotNetAsmTreeNewCallback(
    _In_ HWND hwnd,
    _In_ PH_TREENEW_MESSAGE Message,
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2,
    _In_opt_ PVOID Context
    )
{
    PASMPAGE_CONTEXT context = Context;

    if (!context)
        return FALSE;

    switch (Message)
    {
    case TreeNewGetChildren:
        {
            PPH_TREENEW_GET_CHILDREN getChildren = Parameter1;
            PDNA_NODE node;

            if (!getChildren)
                break;

            node = (PDNA_NODE)getChildren->Node;

            if (!node)
            {
                getChildren->Children = (PPH_TREENEW_NODE *)context->NodeRootList->Items;
                getChildren->NumberOfChildren = context->NodeRootList->Count;
            }
            else
            {
                if (node->Type == DNA_TYPE_APPDOMAIN || node == context->ClrV2Node)
                {
                    // Sort the assemblies.
                    qsort(node->Children->Items, node->Children->Count, sizeof(PVOID), AssemblyNodeNameCompareFunction);
                }

                getChildren->Children = (PPH_TREENEW_NODE *)node->Children->Items;
                getChildren->NumberOfChildren = node->Children->Count;
            }
        }
        return TRUE;
    case TreeNewIsLeaf:
        {
            PPH_TREENEW_IS_LEAF isLeaf = Parameter1;
            PDNA_NODE node;

            if (!isLeaf)
                break;

            node = (PDNA_NODE)isLeaf->Node;

            isLeaf->IsLeaf = node->Children->Count == 0;
        }
        return TRUE;
    case TreeNewGetCellText:
        {
            PPH_TREENEW_GET_CELL_TEXT getCellText = Parameter1;
            PDNA_NODE node;

            if (!getCellText)
                break;

            node = (PDNA_NODE)getCellText->Node;

            switch (getCellText->Id)
            {
            case DNATNC_STRUCTURE:
                getCellText->Text = node->StructureText;
                break;
            case DNATNC_ID:
                getCellText->Text = PhGetStringRef(node->IdText);
                break;
            case DNATNC_FLAGS:
                getCellText->Text = PhGetStringRef(node->FlagsText);
                break;
            case DNATNC_PATH:
                getCellText->Text = PhGetStringRef(node->PathText);
                break;
            case DNATNC_NATIVEPATH:
                getCellText->Text = PhGetStringRef(node->NativePathText);
                break;
            default:
                return FALSE;
            }

            getCellText->Flags = TN_CACHE;
        }
        return TRUE;
    case TreeNewGetNodeColor:
        {
            PPH_TREENEW_GET_NODE_COLOR getNodeColor = Parameter1;
            PDNA_NODE node;

            if (!getNodeColor)
                break;

            node = (PDNA_NODE)getNodeColor->Node;

            switch (node->Type)
            {
            case DNA_TYPE_CLR:
            case DNA_TYPE_APPDOMAIN:
                //getNodeColor->BackColor = PhGetIntegerSetting(L"ColorDotNet");
                break;
            case DNA_TYPE_ASSEMBLY:
                {
                    if (context->HighlightDynamicModules && (node->u.Assembly.AssemblyFlags & 0x2) == 0x2)
                    {
                        getNodeColor->BackColor = PhGetIntegerSetting(L"ColorPacked");
                    }
                    else if (context->HighlightNativeModules && (node->u.Assembly.AssemblyFlags & 0x4) == 0x4)
                    {
                        getNodeColor->BackColor = PhGetIntegerSetting(L"ColorSystemProcesses");
                    }
                    else
                    {
                        //getNodeColor->BackColor = PhGetIntegerSetting(L"ColorDotNet");
                    }
                }
                break;
            }

            getNodeColor->Flags = TN_AUTO_FORECOLOR;
        }
        return TRUE;
    case TreeNewGetCellTooltip:
        {
            PPH_TREENEW_GET_CELL_TOOLTIP getCellTooltip = Parameter1;
            PDNA_NODE node;

            if (!getCellTooltip)
                break;

            node = (PDNA_NODE)getCellTooltip->Node;

            if (getCellTooltip->Column->Id != 0 || node->Type != DNA_TYPE_ASSEMBLY)
                return FALSE;

            if (!PhIsNullOrEmptyString(node->u.Assembly.FullyQualifiedAssemblyName))
            {
                getCellTooltip->Text = node->u.Assembly.FullyQualifiedAssemblyName->sr;
                getCellTooltip->Unfolding = FALSE;
            }
            else
            {
                return FALSE;
            }
        }
        return TRUE;
    case TreeNewKeyDown:
        {
            PPH_TREENEW_KEY_EVENT keyEvent = Parameter1;

            if (!keyEvent)
                break;

            switch (keyEvent->VirtualKey)
            {
            case 'C':
                if (GetKeyState(VK_CONTROL) < 0)
                    SendMessage(context->WindowHandle, WM_COMMAND, ID_COPY, 0);
                break;
            }
        }
        return TRUE;
    case TreeNewLeftDoubleClick:
        {
            PDNA_NODE node;

            if (!(node = DotNetAsmGetSelectedTreeNode(Context)))
                break;

            if (!PhIsNullOrEmptyString(node->u.Assembly.FullyQualifiedAssemblyName) && PhDoesFileExistsWin32(PhGetString(node->PathText)))
            {
                PhShellExecuteUserString(
                    context->WindowHandle,
                    L"ProgramInspectExecutables",
                    node->PathText->Buffer,
                    FALSE,
                    L"Make sure the PE Viewer executable file is present."
                    );
            }
        }
        return TRUE;
    case TreeNewHeaderRightClick:
        {
            PH_TN_COLUMN_MENU_DATA data;

            data.TreeNewHandle = hwnd;
            data.MouseEvent = Parameter1;
            data.DefaultSortColumn = 0;
            data.DefaultSortOrder = AscendingSortOrder;
            PhInitializeTreeNewColumnMenu(&data);

            data.Selection = PhShowEMenu(data.Menu, hwnd, PH_EMENU_SHOW_LEFTRIGHT,
                PH_ALIGN_LEFT | PH_ALIGN_TOP, data.MouseEvent->ScreenLocation.x, data.MouseEvent->ScreenLocation.y);
            PhHandleTreeNewColumnMenu(&data);
            PhDeleteTreeNewColumnMenu(&data);
        }
        return TRUE;
    case TreeNewContextMenu:
        {
            PPH_TREENEW_CONTEXT_MENU contextMenuEvent = Parameter1;

            if (!contextMenuEvent)
                break;

            DotNetAsmShowContextMenu(context, contextMenuEvent);
        }
        return TRUE;
    }

    return FALSE;
}

VOID DotNetAsmLoadSettingsTreeList(
    _Inout_ PASMPAGE_CONTEXT Context
    )
{
    ULONG flags;
    PPH_STRING settings;
    //PH_INTEGER_PAIR sortSettings;

    flags = PhGetIntegerSetting(SETTING_NAME_ASM_TREE_LIST_FLAGS);
    settings = PhGetStringSetting(SETTING_NAME_ASM_TREE_LIST_COLUMNS);
    //sortSettings = PhGetIntegerPairSetting(SETTING_NAME_ASM_TREE_LIST_SORT);

    Context->Flags = flags;
    PhCmLoadSettings(Context->TreeNewHandle, &settings->sr);
    //TreeNew_SetSort(Context->TreeNewHandle, (ULONG)sortSettings.X, (PH_SORT_ORDER)sortSettings.Y);

    PhDereferenceObject(settings);
}

VOID DotNetAsmSaveSettingsTreeList(
    _Inout_ PASMPAGE_CONTEXT Context
    )
{
    PPH_STRING settings;
    //PH_INTEGER_PAIR sortSettings;
    //ULONG sortColumn;
    //PH_SORT_ORDER sortOrder;

    settings = PhCmSaveSettings(Context->TreeNewHandle);
    //TreeNew_GetSort(Context->TreeNewHandle, &sortColumn, &sortOrder);
    //sortSettings.X = sortColumn;
    //sortSettings.Y = sortOrder;

    PhSetIntegerSetting(SETTING_NAME_ASM_TREE_LIST_FLAGS, Context->Flags);
    PhSetStringSetting2(SETTING_NAME_ASM_TREE_LIST_COLUMNS, &settings->sr);
    //PhSetIntegerPairSetting(SETTING_NAME_ASM_TREE_LIST_SORT, sortSettings);

    PhDereferenceObject(settings);
}

VOID DotNetAsmSetOptionsTreeList(
    _Inout_ PASMPAGE_CONTEXT Context,
    _In_ ULONG Options
    )
{
    switch (Options)
    {
    case DN_ASM_MENU_HIDE_DYNAMIC_OPTION:
        Context->HideDynamicModules = !Context->HideDynamicModules;
        break;
    case DN_ASM_MENU_HIGHLIGHT_DYNAMIC_OPTION:
        Context->HighlightDynamicModules = !Context->HighlightDynamicModules;
        break;
    case DN_ASM_MENU_HIDE_NATIVE_OPTION:
        Context->HideNativeModules = !Context->HideNativeModules;
        break;
    case DN_ASM_MENU_HIGHLIGHT_NATIVE_OPTION:
        Context->HighlightNativeModules = !Context->HighlightNativeModules;
        break;
    }
}

_Success_(return == ERROR_SUCCESS)
static ULONG StartDotNetTrace(
    _Out_ PTRACEHANDLE SessionHandle,
    _Out_ PEVENT_TRACE_PROPERTIES *Properties
    )
{
    ULONG result;
    ULONG bufferSize;
    PEVENT_TRACE_PROPERTIES properties;
    TRACEHANDLE sessionHandle;

    bufferSize = sizeof(EVENT_TRACE_PROPERTIES) + DotNetLoggerName.Length + sizeof(WCHAR);
    properties = PhAllocateZero(bufferSize);

    properties->Wnode.BufferSize = bufferSize;
    properties->Wnode.ClientContext = 2; // System time clock resolution
    properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    properties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_USE_PAGED_MEMORY;
    properties->LogFileNameOffset = 0;
    properties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    result = StartTrace(&sessionHandle, DotNetLoggerName.Buffer, properties);

    if (result == ERROR_SUCCESS)
    {
        *SessionHandle = sessionHandle;
        *Properties = properties;

        return ERROR_SUCCESS;
    }
    else if (result == ERROR_ALREADY_EXISTS)
    {
        // Session already exists, so use that. Get the existing session handle.

        result = ControlTrace(0, DotNetLoggerName.Buffer, properties, EVENT_TRACE_CONTROL_QUERY);

        if (result != ERROR_SUCCESS)
        {
            PhFree(properties);
            return result;
        }

        *SessionHandle = properties->Wnode.HistoricalContext;
        *Properties = properties;

        return ERROR_SUCCESS;
    }
    else
    {
        PhFree(properties);

        return result;
    }
}

static ULONG NTAPI DotNetBufferCallback(
    _In_ PEVENT_TRACE_LOGFILE Buffer
    )
{
    return TRUE;
}

static VOID NTAPI DotNetEventCallback(
    _In_ PEVENT_RECORD EventRecord
    )
{
    PASMPAGE_QUERY_CONTEXT context = EventRecord->UserContext;
    PEVENT_HEADER eventHeader = &EventRecord->EventHeader;
    PEVENT_DESCRIPTOR eventDescriptor = &eventHeader->EventDescriptor;

    if (UlongToHandle(eventHeader->ProcessId) == context->ProcessId)
    {
        // .NET 4.0+

        switch (eventDescriptor->Id)
        {
        case RuntimeInformationDCStart:
            {
                PRuntimeInformationRundown data = EventRecord->UserData;
                PDNA_NODE node;
                PPH_STRING startupFlagsString;
                PPH_STRING startupModeString;

                // Check for duplicates.
                if (FindClrNode(context, data->ClrInstanceID))
                    break;

                node = AddNode(context);
                node->Type = DNA_TYPE_CLR;
                node->u.Clr.ClrInstanceID = data->ClrInstanceID;
                node->u.Clr.StartupFlags = data->StartupFlags;
                node->u.Clr.DisplayName = PhFormatString(L"CLR v%hu.%hu.%hu.%hu", data->VMMajorVersion, data->VMMinorVersion, data->VMBuildNumber, data->VMQfeNumber);
                node->StructureText = node->u.Clr.DisplayName->sr;
                node->IdText = PhFormatUInt64(data->ClrInstanceID, FALSE);

                startupFlagsString = FlagsToString(data->StartupFlags, StartupFlagsMap, sizeof(StartupFlagsMap));
                startupModeString = FlagsToString(data->StartupMode, StartupModeMap, sizeof(StartupModeMap));

                if (startupFlagsString->Length != 0 && startupModeString->Length != 0)
                {
                    node->FlagsText = PhConcatStrings(3, startupFlagsString->Buffer, L", ", startupModeString->Buffer);
                    PhDereferenceObject(startupFlagsString);
                    PhDereferenceObject(startupModeString);
                }
                else if (startupFlagsString->Length != 0)
                {
                    node->FlagsText = startupFlagsString;
                    PhDereferenceObject(startupModeString);
                }
                else if (startupModeString->Length != 0)
                {
                    node->FlagsText = startupModeString;
                    PhDereferenceObject(startupFlagsString);
                }

                if (data->CommandLine[0])
                    node->PathText = PhCreateString(data->CommandLine);

                PhAddItemList(context->NodeRootList, node);
            }
            break;
        case AppDomainDCStart_V1:
            {
                PAppDomainLoadUnloadRundown_V1 data = EventRecord->UserData;
                SIZE_T appDomainNameLength;
                USHORT clrInstanceID;
                PDNA_NODE parentNode;
                PDNA_NODE node;

                appDomainNameLength = PhCountStringZ(data->AppDomainName) * sizeof(WCHAR);
                clrInstanceID = *(PUSHORT)PTR_ADD_OFFSET(data, FIELD_OFFSET(AppDomainLoadUnloadRundown_V1, AppDomainName) + appDomainNameLength + sizeof(WCHAR) + sizeof(ULONG));

                // Find the CLR node to add the AppDomain node to.
                parentNode = FindClrNode(context, clrInstanceID);

                if (parentNode)
                {
                    // Check for duplicates.
                    if (FindAppDomainNode(parentNode, data->AppDomainID))
                        break;

                    node = AddNode(context);
                    node->Type = DNA_TYPE_APPDOMAIN;
                    node->u.AppDomain.AppDomainID = data->AppDomainID;
                    node->u.AppDomain.AppDomainFlags = data->AppDomainFlags;
                    node->u.AppDomain.DisplayName = PhConcatStrings2(L"AppDomain: ", data->AppDomainName);
                    node->StructureText = node->u.AppDomain.DisplayName->sr;
                    node->IdText = PhFormatUInt64(data->AppDomainID, FALSE);
                    node->FlagsText = FlagsToString(data->AppDomainFlags, AppDomainFlagsMap, sizeof(AppDomainFlagsMap));

                    PhAddItemList(parentNode->Children, node);
                }
            }
            break;
        case AssemblyDCStart_V1:
            {
                PAssemblyLoadUnloadRundown_V1 data = EventRecord->UserData;
                SIZE_T fullyQualifiedAssemblyNameLength;
                USHORT clrInstanceID;
                PDNA_NODE parentNode;
                PDNA_NODE node;
                PH_STRINGREF remainingPart;

                fullyQualifiedAssemblyNameLength = PhCountStringZ(data->FullyQualifiedAssemblyName) * sizeof(WCHAR);
                clrInstanceID = *(PUSHORT)PTR_ADD_OFFSET(data, FIELD_OFFSET(AssemblyLoadUnloadRundown_V1, FullyQualifiedAssemblyName) + fullyQualifiedAssemblyNameLength + sizeof(WCHAR));

                // Find the AppDomain node to add the Assembly node to.

                parentNode = FindClrNode(context, clrInstanceID);

                if (parentNode)
                    parentNode = FindAppDomainNode(parentNode, data->AppDomainID);

                if (parentNode)
                {
                    // Check for duplicates.
                    if (FindAssemblyNode(parentNode, data->AssemblyID))
                        break;

                    node = AddNode(context);
                    node->Type = DNA_TYPE_ASSEMBLY;
                    node->u.Assembly.AssemblyID = data->AssemblyID;
                    node->u.Assembly.AssemblyFlags = data->AssemblyFlags;
                    node->u.Assembly.FullyQualifiedAssemblyName = PhCreateStringEx(data->FullyQualifiedAssemblyName, fullyQualifiedAssemblyNameLength);

                    // Display only the assembly name, not the whole fully qualified name.
                    if (!PhSplitStringRefAtChar(&node->u.Assembly.FullyQualifiedAssemblyName->sr, ',', &node->StructureText, &remainingPart))
                        node->StructureText = node->u.Assembly.FullyQualifiedAssemblyName->sr;

                    node->IdText = PhFormatUInt64(data->AssemblyID, FALSE);
                    node->FlagsText = FlagsToString(data->AssemblyFlags, AssemblyFlagsMap, sizeof(AssemblyFlagsMap));

                    PhAddItemList(parentNode->Children, node);
                }
            }
            break;
        case ModuleDCStart_V1:
            {
                PModuleLoadUnloadRundown_V1 data = EventRecord->UserData;
                PWSTR moduleILPath;
                SIZE_T moduleILPathLength;
                PWSTR moduleNativePath;
                SIZE_T moduleNativePathLength;
                USHORT clrInstanceID;
                PDNA_NODE node;

                moduleILPath = data->ModuleILPath;
                moduleILPathLength = PhCountStringZ(moduleILPath) * sizeof(WCHAR);
                moduleNativePath = (PWSTR)PTR_ADD_OFFSET(moduleILPath, moduleILPathLength + sizeof(WCHAR));
                moduleNativePathLength = PhCountStringZ(moduleNativePath) * sizeof(WCHAR);
                clrInstanceID = *(PUSHORT)PTR_ADD_OFFSET(moduleNativePath, moduleNativePathLength + sizeof(WCHAR));

                // Find the Assembly node to set the path on.

                node = FindClrNode(context, clrInstanceID);

                if (node)
                    node = FindAssemblyNode2(node, data->AssemblyID);

                if (node)
                {
                    PhMoveReference(&node->PathText, PhCreateStringEx(moduleILPath, moduleILPathLength));

                    if (moduleNativePathLength != 0)
                        PhMoveReference(&node->NativePathText, PhCreateStringEx(moduleNativePath, moduleNativePathLength));
                }
            }
            break;
        case DCStartComplete_V1:
            {
                if (_InterlockedExchange(&context->TraceHandleActive, 0) == 1)
                {
                    CloseTrace(context->TraceHandle);
                }
            }
            break;
        }

        // .NET 2.0

        if (eventDescriptor->Id == 0)
        {
            switch (eventDescriptor->Opcode)
            {
            case CLR_MODULEDCSTART_OPCODE:
                {
                    PModuleLoadUnloadRundown_V1 data = EventRecord->UserData;
                    PWSTR moduleILPath;
                    SIZE_T moduleILPathLength;
                    PWSTR moduleNativePath;
                    SIZE_T moduleNativePathLength;
                    PDNA_NODE node;
                    ULONG_PTR indexOfBackslash;
                    ULONG_PTR indexOfLastDot;

                    moduleILPath = data->ModuleILPath;
                    moduleILPathLength = PhCountStringZ(moduleILPath) * sizeof(WCHAR);
                    moduleNativePath = (PWSTR)PTR_ADD_OFFSET(moduleILPath, moduleILPathLength + sizeof(WCHAR));
                    moduleNativePathLength = PhCountStringZ(moduleNativePath) * sizeof(WCHAR);

                    if (context->ClrV2Node && (moduleILPathLength != 0 || moduleNativePathLength != 0))
                    {
                        node = AddNode(context);
                        node->Type = DNA_TYPE_ASSEMBLY;
                        node->FlagsText = FlagsToString(data->ModuleFlags, ModuleFlagsMap, sizeof(ModuleFlagsMap));
                        node->PathText = PhCreateStringEx(moduleILPath, moduleILPathLength);

                        if (moduleNativePathLength != 0)
                            node->NativePathText = PhCreateStringEx(moduleNativePath, moduleNativePathLength);

                        // Use the name between the last backslash and the last dot for the structure column text.
                        // (E.g. C:\...\AcmeSoft.BigLib.dll -> AcmeSoft.BigLib)

                        indexOfBackslash = PhFindLastCharInString(node->PathText, 0, OBJ_NAME_PATH_SEPARATOR);
                        indexOfLastDot = PhFindLastCharInString(node->PathText, 0, '.');

                        if (indexOfBackslash != -1)
                        {
                            node->StructureText.Buffer = node->PathText->Buffer + indexOfBackslash + 1;

                            if (indexOfLastDot != -1 && indexOfLastDot > indexOfBackslash)
                            {
                                node->StructureText.Length = (indexOfLastDot - indexOfBackslash - 1) * sizeof(WCHAR);
                            }
                            else
                            {
                                node->StructureText.Length = node->PathText->Length - indexOfBackslash * sizeof(WCHAR) - sizeof(WCHAR);
                            }
                        }
                        else
                        {
                            node->StructureText = node->PathText->sr;
                        }

                        PhAddItemList(context->ClrV2Node->Children, node);
                    }
                }
                break;
            case CLR_METHODDC_DCSTARTCOMPLETE_OPCODE:
                {
                    if (_InterlockedExchange(&context->TraceHandleActive, 0) == 1)
                    {
                        CloseTrace(context->TraceHandle);
                    }
                }
                break;
            }
        }
    }
}

static ULONG ProcessDotNetTrace(
    _In_ PASMPAGE_QUERY_CONTEXT Context
    )
{
    ULONG result;
    TRACEHANDLE traceHandle;
    EVENT_TRACE_LOGFILE logFile;

    memset(&logFile, 0, sizeof(EVENT_TRACE_LOGFILE));
    logFile.LoggerName = DotNetLoggerName.Buffer;
    logFile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logFile.BufferCallback = DotNetBufferCallback;
    logFile.EventRecordCallback = DotNetEventCallback;
    logFile.Context = Context;

    traceHandle = OpenTrace(&logFile);

    if (traceHandle == INVALID_PROCESSTRACE_HANDLE)
        return GetLastError();

    Context->TraceHandleActive = 1;
    Context->TraceHandle = traceHandle;
    result = ProcessTrace(&traceHandle, 1, NULL, NULL);

    if (_InterlockedExchange(&Context->TraceHandleActive, 0) == 1)
    {
        CloseTrace(traceHandle);
    }

    return result;
}

NTSTATUS UpdateDotNetTraceInfoThreadStart(
    _In_ PVOID Parameter
    )
{
    PASMPAGE_QUERY_CONTEXT context = Parameter;
    TRACEHANDLE sessionHandle;
    PEVENT_TRACE_PROPERTIES properties;
    PGUID guidToEnable;

    context->TraceResult = StartDotNetTrace(&sessionHandle, &properties);

    if (context->TraceResult != 0)
        return context->TraceResult;

    if (context->TraceClrV2)
        guidToEnable = &ClrRuntimeProviderGuid;
    else
        guidToEnable = &ClrRundownProviderGuid;

    EnableTraceEx(
        guidToEnable,
        NULL,
        sessionHandle,
        1,
        TRACE_LEVEL_INFORMATION,
        CLR_LOADER_KEYWORD | CLR_STARTENUMERATION_KEYWORD,
        0,
        0,
        NULL
        );

    context->TraceResult = ProcessDotNetTrace(context);

    ControlTrace(sessionHandle, NULL, properties, EVENT_TRACE_CONTROL_STOP);
    PhFree(properties);

    return context->TraceResult;
}

ULONG UpdateDotNetTraceInfoWithTimeout(
    _In_ PASMPAGE_QUERY_CONTEXT Context,
    _In_ BOOLEAN ClrV2,
    _In_opt_ PLARGE_INTEGER Timeout
    )
{
    HANDLE threadHandle;
    BOOLEAN timeout = FALSE;

    // ProcessDotNetTrace is not guaranteed to complete within any period of time, because
    // the target process might terminate before it writes the DCStartComplete_V1 event.
    // If the timeout is reached, the trace handle is closed, forcing ProcessTrace to stop
    // processing.

    Context->TraceClrV2 = ClrV2;
    Context->TraceResult = 0;
    Context->TraceHandleActive = 0;
    Context->TraceHandle = 0;

    if (!NT_SUCCESS(PhCreateThreadEx(&threadHandle, UpdateDotNetTraceInfoThreadStart, Context)))
        return ERROR_INVALID_HANDLE;

    if (NtWaitForSingleObject(threadHandle, FALSE, Timeout) != STATUS_WAIT_0)
    {
        // Timeout has expired. Stop the trace processing if it's still active.
        // BUG: This assumes that the thread is in ProcessTrace. It might still be
        // setting up though!
        if (_InterlockedExchange(&Context->TraceHandleActive, 0) == 1)
        {
            CloseTrace(Context->TraceHandle);
            timeout = TRUE;
        }

        NtWaitForSingleObject(threadHandle, FALSE, NULL);
    }

    NtClose(threadHandle);

    if (timeout)
        return ERROR_TIMEOUT;

    return Context->TraceResult;
}

NTSTATUS DotNetTraceQueryThreadStart(
    _In_ PVOID Parameter
    )
{
    LARGE_INTEGER timeout;
    PASMPAGE_QUERY_CONTEXT context = Parameter;
    BOOLEAN timeoutReached = FALSE;
    BOOLEAN nonClrNode = FALSE;
    ULONG i;
    ULONG result = 0;

    if (context->ClrVersions & PH_CLR_VERSION_1_0)
    {
        AddFakeClrNode(context, L"CLR v1.0.3705"); // what PE displays
    }

    if (context->ClrVersions & PH_CLR_VERSION_1_1)
    {
        AddFakeClrNode(context, L"CLR v1.1.4322");
    }

    timeout.QuadPart = -(LONGLONG)UInt32x32To64(10, PH_TIMEOUT_SEC);

    if (context->ClrVersions & PH_CLR_VERSION_2_0)
    {
        context->ClrV2Node = AddFakeClrNode(context, L"CLR v2.0.50727");
        result = UpdateDotNetTraceInfoWithTimeout(context, TRUE, &timeout);

        if (result == ERROR_TIMEOUT)
        {
            timeoutReached = TRUE;
            result = ERROR_SUCCESS;
        }
    }

    if ((context->ClrVersions & PH_CLR_VERSION_4_ABOVE) || (context->ClrVersions & PH_CLR_JIT_PRESENT)) // PH_CLR_JIT_PRESENT CoreCLR support. (dmex)
    {
        result = UpdateDotNetTraceInfoWithTimeout(context, FALSE, &timeout);

        if (result == ERROR_TIMEOUT)
        {
            timeoutReached = TRUE;
            result = ERROR_SUCCESS;
        }
    }

    // If we reached the timeout, check whether we got any data back.
    if (timeoutReached)
    {
        for (i = 0; i < context->NodeList->Count; i++)
        {
            PDNA_NODE node = context->NodeList->Items[i];

            if (node->Type != DNA_TYPE_CLR)
            {
                nonClrNode = TRUE;
                break;
            }
        }

        if (!nonClrNode)
            result = ERROR_TIMEOUT;
    }

    if (IsWindow(context->WindowHandle))
        PostMessage(context->WindowHandle, DN_ASM_UPDATE_MSG, result, (LPARAM)context);
    else
        DestroyDotNetTraceQuery(context);

    return STATUS_SUCCESS;
}

VOID CreateDotNetTraceQueryThread(
    _In_ PASMPAGE_CONTEXT Context,
    _In_ HANDLE ProcessId
    )
{
    PASMPAGE_QUERY_CONTEXT context;

    context = PhAllocateZero(sizeof(ASMPAGE_QUERY_CONTEXT));
    context->WindowHandle = Context->WindowHandle;
    context->ProcessId = ProcessId;
    context->NodeList = PhCreateList(64);
    context->NodeRootList = PhCreateList(2);

    PhGetProcessIsDotNetEx(
        ProcessId,
        NULL,
        0,
        NULL,
        &context->ClrVersions
        );

    PhQueueItemWorkQueue(PhGetGlobalWorkQueue(), DotNetTraceQueryThreadStart, context);
}

VOID DestroyDotNetTraceQuery(
    _In_ PASMPAGE_QUERY_CONTEXT Context
    )
{
    if (Context->NodeRootList)
    {
        PhClearList(Context->NodeRootList);
        PhDereferenceObject(Context->NodeRootList);
    }

    if (Context->NodeList)
    {
        PhClearList(Context->NodeList);
        PhDereferenceObject(Context->NodeList);
    }

    PhFree(Context);
}

BOOLEAN IsProcessSuspended(
    _In_ HANDLE ProcessId
    )
{
    PVOID processes;
    PSYSTEM_PROCESS_INFORMATION process;

    if (NT_SUCCESS(PhEnumProcesses(&processes)))
    {
        if (process = PhFindProcessInformation(processes, ProcessId))
            return PhGetProcessIsSuspended(process);

        PhFree(processes);
    }

    return FALSE;
}

VOID DotNetAsmRefreshTraceQuery(
    _In_ PASMPAGE_CONTEXT Context
    )
{
    PhMoveReference(&Context->TreeErrorMessage, PhCreateString(L"Loading .NET assemblies..."));
    TreeNew_SetEmptyText(Context->TreeNewHandle, &Context->TreeErrorMessage->sr, 0);
    TreeNew_NodesStructured(Context->TreeNewHandle);

    if (!IsProcessSuspended(Context->ProcessItem->ProcessId) || PhShowMessage(
        Context->WindowHandle,
        MB_ICONWARNING | MB_YESNO,
        L".NET assembly enumeration may not work properly because the process is currently suspended. Do you want to continue?"
        ) == IDYES)
    {
        CreateDotNetTraceQueryThread(Context, Context->ProcessItem->ProcessId);
    }
    else
    {
        PhMoveReference(
            &Context->TreeErrorMessage,
            PhCreateString(L"Unable to start the event tracing session because the process is suspended.")
            );
        TreeNew_SetEmptyText(Context->TreeNewHandle, &Context->TreeErrorMessage->sr, 0);
        TreeNew_NodesStructured(Context->TreeNewHandle);
    }
}

BOOLEAN WordMatchStringRef(
    _Inout_ PASMPAGE_CONTEXT Context,
    _In_ PPH_STRINGREF Text
    )
{
    PH_STRINGREF part;
    PH_STRINGREF remainingPart;

    remainingPart = PhGetStringRef(Context->SearchBoxText);

    while (remainingPart.Length != 0)
    {
        PhSplitStringRefAtChar(&remainingPart, '|', &part, &remainingPart);

        if (part.Length != 0)
        {
            if (PhFindStringInStringRef(Text, &part, TRUE) != -1)
                return TRUE;
        }
    }

    return FALSE;
}

BOOLEAN WordMatchStringZ(
    _Inout_ PASMPAGE_CONTEXT Context,
    _In_ PWSTR Text
    )
{
    PH_STRINGREF text;

    PhInitializeStringRef(&text, Text);

    return WordMatchStringRef(Context, &text);
}

BOOLEAN DotNetAsmTreeFilterCallback(
    _In_ PPH_TREENEW_NODE Node,
    _In_opt_ PVOID Context
    )
{
    PASMPAGE_CONTEXT context = Context;
    PDNA_NODE node = (PDNA_NODE)Node;

    if (!context)
        return FALSE;
    if (context->HideDynamicModules && node->Type == DNA_TYPE_ASSEMBLY && (node->u.Assembly.AssemblyFlags & 0x2) == 0x2)
        return FALSE;
    if (context->HideNativeModules && node->Type == DNA_TYPE_ASSEMBLY && (node->u.Assembly.AssemblyFlags & 0x4) == 0x4)
        return FALSE;

    if (PhIsNullOrEmptyString(context->SearchBoxText))
        return TRUE;

    if (!PhIsNullOrEmptyString(node->IdText))
    {
        if (WordMatchStringRef(context, &node->IdText->sr))
            return TRUE;
    }

    if (!PhIsNullOrEmptyString(node->FlagsText))
    {
        if (WordMatchStringRef(context, &node->FlagsText->sr))
            return TRUE;
    }

    if (!PhIsNullOrEmptyString(node->PathText))
    {
        if (WordMatchStringRef(context, &node->PathText->sr))
            return TRUE;
    }

    if (!PhIsNullOrEmptyString(node->NativePathText))
    {
        if (WordMatchStringRef(context, &node->NativePathText->sr))
            return TRUE;
    }

    return FALSE;
}

INT_PTR CALLBACK DotNetAsmPageDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;
    PPH_PROCESS_ITEM processItem;
    PASMPAGE_CONTEXT context;

    if (PhPropPageDlgProcHeader(hwndDlg, uMsg, lParam, &propSheetPage, &propPageContext, &processItem))
    {
        context = propPageContext->Context;
    }
    else
    {
        return FALSE;
    }

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            context = propPageContext->Context = PhAllocateZero(sizeof(ASMPAGE_CONTEXT));
            context->WindowHandle = hwndDlg;
            context->ProcessItem = processItem;
            context->SearchBoxHandle = GetDlgItem(hwndDlg, IDC_SEARCHEDIT);
            context->TreeNewHandle = GetDlgItem(hwndDlg, IDC_LIST);

            PhCreateSearchControl(hwndDlg, context->SearchBoxHandle, L"Search Assemblies (Ctrl+K)");

            context->NodeList = PhCreateList(64);
            context->NodeRootList = PhCreateList(2);
            TreeNew_SetCallback(context->TreeNewHandle, DotNetAsmTreeNewCallback, context);
            TreeNew_SetExtendedFlags(context->TreeNewHandle, TN_FLAG_ITEM_DRAG_SELECT, TN_FLAG_ITEM_DRAG_SELECT);
            PhSetControlTheme(context->TreeNewHandle, L"explorer");
            SendMessage(TreeNew_GetTooltips(context->TreeNewHandle), TTM_SETMAXTIPWIDTH, 0, MAXSHORT);
            PhAddTreeNewColumn(context->TreeNewHandle, DNATNC_STRUCTURE, TRUE, L"Structure", 240, PH_ALIGN_LEFT, -2, 0);
            PhAddTreeNewColumn(context->TreeNewHandle, DNATNC_ID, FALSE, L"ID", 50, PH_ALIGN_RIGHT, 1, DT_RIGHT);
            PhAddTreeNewColumn(context->TreeNewHandle, DNATNC_FLAGS, FALSE, L"Flags", 120, PH_ALIGN_LEFT, 2, 0);
            PhAddTreeNewColumn(context->TreeNewHandle, DNATNC_PATH, TRUE, L"File name", 600, PH_ALIGN_LEFT, 3, DT_PATH_ELLIPSIS);
            PhAddTreeNewColumn(context->TreeNewHandle, DNATNC_NATIVEPATH, FALSE, L"Native image path", 600, PH_ALIGN_LEFT, 4, DT_PATH_ELLIPSIS);
            DotNetAsmLoadSettingsTreeList(context);

            context->SearchBoxText = PhReferenceEmptyString();

            PhInitializeTreeNewFilterSupport(
                &context->TreeFilterSupport,
                context->TreeNewHandle,
                context->NodeList
                );

            context->TreeFilterEntry = PhAddTreeNewFilter(
                &context->TreeFilterSupport,
                DotNetAsmTreeFilterCallback,
                context
                );

            DotNetAsmRefreshTraceQuery(context);

            PhInitializeWindowTheme(hwndDlg, !!PhGetIntegerSetting(L"EnableThemeSupport"));
        }
        break;
    case WM_DESTROY:
        {
            PhRemoveTreeNewFilter(&context->TreeFilterSupport, context->TreeFilterEntry);
            PhDeleteTreeNewFilterSupport(&context->TreeFilterSupport);

            DotNetAsmSaveSettingsTreeList(context);
            DotNetAsmDestroyTreeNodes(context);

            if (context->SearchBoxText) PhDereferenceObject(context->SearchBoxText);
            if (context->TreeErrorMessage) PhDereferenceObject(context->TreeErrorMessage);
            PhFree(context);
        }
        break;
    case WM_SHOWWINDOW:
        {
            PPH_LAYOUT_ITEM dialogItem;

            if (dialogItem = PhBeginPropPageLayout(hwndDlg, propPageContext))
            {
                PhAddPropPageLayoutItem(hwndDlg, context->SearchBoxHandle, dialogItem, PH_ANCHOR_TOP | PH_ANCHOR_RIGHT);
                PhAddPropPageLayoutItem(hwndDlg, context->TreeNewHandle, dialogItem, PH_ANCHOR_ALL);
                PhEndPropPageLayout(hwndDlg, propPageContext);
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            switch (header->code)
            {
            case PSN_QUERYINITIALFOCUS:
                SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, (LPARAM)context->TreeNewHandle);
                return TRUE;
            }
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_CMD(wParam, lParam))
            {
            case EN_CHANGE:
                {
                    PPH_STRING newSearchboxText;

                    if (GET_WM_COMMAND_HWND(wParam, lParam) != context->SearchBoxHandle)
                        break;

                    newSearchboxText = PH_AUTO(PhGetWindowText(context->SearchBoxHandle));

                    if (!PhEqualString(context->SearchBoxText, newSearchboxText, FALSE))
                    {
                        DotNetAsmExpandAllTreeNodes(context, TRUE);

                        PhSwapReference(&context->SearchBoxText, newSearchboxText);
                        PhApplyTreeNewFilters(&context->TreeFilterSupport);
                        TreeNew_NodesStructured(context->TreeNewHandle);
                    }
                }
                break;
            }

            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case ID_COPY:
                {
                    PPH_STRING text;

                    text = PhGetTreeNewText(context->TreeNewHandle, 0);
                    PhSetClipboardString(context->TreeNewHandle, &text->sr);
                    PhDereferenceObject(text);
                }
                break;
            case IDC_REFRESH:
                {
                    DotNetAsmRefreshTraceQuery(context);
                }
                break;
            case IDC_OPTIONS:
                {
                    RECT rect;
                    PPH_EMENU menu;
                    PPH_EMENU_ITEM dynamicItem;
                    PPH_EMENU_ITEM nativeItem;
                    PPH_EMENU_ITEM highlightDynamicItem;
                    PPH_EMENU_ITEM highlightNativeItem;
                    PPH_EMENU_ITEM selectedItem;

                    GetWindowRect(GetDlgItem(hwndDlg, IDC_OPTIONS), &rect);

                    menu = PhCreateEMenu();
                    PhInsertEMenuItem(menu, dynamicItem = PhCreateEMenuItem(0, DN_ASM_MENU_HIDE_DYNAMIC_OPTION, L"Hide dynamic", NULL, NULL), ULONG_MAX);
                    PhInsertEMenuItem(menu, nativeItem = PhCreateEMenuItem(0, DN_ASM_MENU_HIDE_NATIVE_OPTION, L"Hide native", NULL, NULL), ULONG_MAX);
                    PhInsertEMenuItem(menu, PhCreateEMenuSeparator(), ULONG_MAX);
                    PhInsertEMenuItem(menu, highlightDynamicItem = PhCreateEMenuItem(0, DN_ASM_MENU_HIGHLIGHT_DYNAMIC_OPTION, L"Highlight dynamic", NULL, NULL), ULONG_MAX);
                    PhInsertEMenuItem(menu, highlightNativeItem = PhCreateEMenuItem(0, DN_ASM_MENU_HIGHLIGHT_NATIVE_OPTION, L"Highlight native", NULL, NULL), ULONG_MAX);

                    if (context->HideDynamicModules)
                        dynamicItem->Flags |= PH_EMENU_CHECKED;
                    if (context->HighlightDynamicModules)
                        highlightDynamicItem->Flags |= PH_EMENU_CHECKED;
                    if (context->HideNativeModules)
                        nativeItem->Flags |= PH_EMENU_CHECKED;
                    if (context->HighlightNativeModules)
                        highlightNativeItem->Flags |= PH_EMENU_CHECKED;

                    selectedItem = PhShowEMenu(
                        menu,
                        hwndDlg,
                        PH_EMENU_SHOW_LEFTRIGHT,
                        PH_ALIGN_LEFT | PH_ALIGN_TOP,
                        rect.left,
                        rect.bottom
                        );

                    if (selectedItem && selectedItem->Id)
                    {
                        DotNetAsmSetOptionsTreeList(context, selectedItem->Id);
                        PhApplyTreeNewFilters(&context->TreeFilterSupport);
                        TreeNew_NodesStructured(context->TreeNewHandle);
                    }

                    PhDestroyEMenu(menu);
                }
                break;
            }
        }
        break;
    case WM_KEYDOWN:
        {
            if (LOWORD(wParam) == 'K')
            {
                if (GetKeyState(VK_CONTROL) < 0)
                {
                    SetFocus(context->SearchBoxHandle);
                    return TRUE;
                }
            }
        }
        break;
    case DN_ASM_UPDATE_MSG:
        {
            ULONG result = (ULONG)wParam;
            PASMPAGE_QUERY_CONTEXT queryContext = (PASMPAGE_QUERY_CONTEXT)lParam;

            if (result == 0)
            {
                DotNetAsmClearTreeNodes(context);

                if (queryContext->NodeRootList)
                {
                    PhAddItemsList(
                        context->NodeRootList,
                        queryContext->NodeRootList->Items,
                        queryContext->NodeRootList->Count
                        );
                }

                if (queryContext->NodeList)
                {
                    PhAddItemsList(
                        context->NodeList,
                        queryContext->NodeList->Items,
                        queryContext->NodeList->Count
                        );
                }

                PhMoveReference(&context->TreeErrorMessage, PhCreateString(L"There are no assemblies to display."));
                TreeNew_SetEmptyText(context->TreeNewHandle, &context->TreeErrorMessage->sr, 0);

                PhApplyTreeNewFilters(&context->TreeFilterSupport);
                TreeNew_NodesStructured(context->TreeNewHandle);
            }
            else
            {
                PPH_STRING errorMessage = PhGetWin32Message(result);

                PhMoveReference(&context->TreeErrorMessage, PhConcatStrings2(
                    L"Unable to start the event tracing session: ",
                    PhGetStringOrDefault(errorMessage, L"Unknown error")
                    ));
                TreeNew_SetEmptyText(context->TreeNewHandle, &context->TreeErrorMessage->sr, 0);
                TreeNew_NodesStructured(context->TreeNewHandle);

                PhClearReference(&errorMessage);
            }

            DestroyDotNetTraceQuery(queryContext);
        }
        break;
    }

    return FALSE;
}

