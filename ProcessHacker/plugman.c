/*
 * Process Hacker -
 *   plugins
 *
 * Copyright (C) 2010-2011 wj32
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

#include <phapp.h>
#include <emenu.h>
#include <settings.h>
#include <mainwnd.h>
#include <colmgr.h>
#include <phplug.h>
#include <phsettings.h>

#define WM_PH_PLUGINS_SHOWDIALOG (WM_APP + 401)
#define WM_PH_PLUGINS_SHOWPROPERTIES (WM_APP + 402)

static HANDLE PhPluginsThreadHandle = NULL;
static HWND PhPluginsWindowHandle = NULL;
static PH_EVENT PhPluginsInitializedEvent = PH_EVENT_INIT;

typedef struct _PH_PLUGMAN_CONTEXT
{
    PH_LAYOUT_MANAGER LayoutManager;
    RECT MinimumSize;

    HFONT NormalFontHandle;
    HFONT TitleFontHandle;

    HWND WindowHandle;
    HWND TreeNewHandle;
    ULONG TreeNewSortColumn;
    PH_SORT_ORDER TreeNewSortOrder;
    PPH_HASHTABLE NodeHashtable;
    PPH_LIST NodeList;
} PH_PLUGMAN_CONTEXT, *PPH_PLUGMAN_CONTEXT;

typedef enum _PH_PLUGIN_TREE_ITEM_MENU
{
    PH_PLUGIN_TREE_ITEM_MENU_UNINSTALL,
    PH_PLUGIN_TREE_ITEM_MENU_DISABLE,
    PH_PLUGIN_TREE_ITEM_MENU_PROPERTIES
} PH_PLUGIN_TREE_ITEM_MENU;

typedef enum _PH_PLUGIN_TREE_COLUMN_ITEM
{
    PH_PLUGIN_TREE_COLUMN_ITEM_NAME,
    PH_PLUGIN_TREE_COLUMN_ITEM_AUTHOR,
    PH_PLUGIN_TREE_COLUMN_ITEM_VERSION,
    PH_PLUGIN_TREE_COLUMN_ITEM_MAXIMUM
} PH_PLUGIN_TREE_COLUMN_ITEM;

typedef struct _PH_PLUGIN_TREE_ROOT_NODE
{
    PH_TREENEW_NODE Node;

    BOOLEAN PluginOptions;
    PPH_PLUGIN PluginInstance;

    PPH_STRING InternalName;
    PPH_STRING Name;
    PPH_STRING Version;
    PPH_STRING Author;
    PPH_STRING Description;

    PH_STRINGREF TextCache[PH_PLUGIN_TREE_COLUMN_ITEM_MAXIMUM];
} PH_PLUGIN_TREE_ROOT_NODE, *PPH_PLUGIN_TREE_ROOT_NODE;

INT_PTR CALLBACK PhpPluginPropertiesDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

ULONG PhpDisabledPluginsCount(
    VOID
    );

INT_PTR CALLBACK PhpPluginsDisabledDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

#pragma region Plugin TreeList

#define SORT_FUNCTION(Column) PluginsTreeNewCompare##Column
#define BEGIN_SORT_FUNCTION(Column) static int __cdecl PluginsTreeNewCompare##Column( \
    _In_ void *_context, \
    _In_ const void *_elem1, \
    _In_ const void *_elem2 \
    ) \
{ \
    PPH_PLUGIN_TREE_ROOT_NODE node1 = *(PPH_PLUGIN_TREE_ROOT_NODE*)_elem1; \
    PPH_PLUGIN_TREE_ROOT_NODE node2 = *(PPH_PLUGIN_TREE_ROOT_NODE*)_elem2; \
    int sortResult = 0;

#define END_SORT_FUNCTION \
    if (sortResult == 0) \
        sortResult = uintptrcmp((ULONG_PTR)node1->Node.Index, (ULONG_PTR)node2->Node.Index); \
    \
    return PhModifySort(sortResult, ((PPH_PLUGMAN_CONTEXT)_context)->TreeNewSortOrder); \
}

BEGIN_SORT_FUNCTION(Name)
{
    sortResult = PhCompareString(node1->Name, node2->Name, TRUE);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(Author)
{
    sortResult = PhCompareString(node1->Author, node2->Author, TRUE);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(Version)
{
    sortResult = PhCompareString(node1->Version, node2->Version, TRUE);
}
END_SORT_FUNCTION

VOID PluginsLoadSettingsTreeList(
    _Inout_ PPH_PLUGMAN_CONTEXT Context
    )
{
    PPH_STRING settings;
    
    settings = PhGetStringSetting(L"PluginManagerTreeListColumns");
    PhCmLoadSettings(Context->TreeNewHandle, &settings->sr);
    PhDereferenceObject(settings);
}

VOID PluginsSaveSettingsTreeList(
    _Inout_ PPH_PLUGMAN_CONTEXT Context
    )
{
    PPH_STRING settings;

    settings = PhCmSaveSettings(Context->TreeNewHandle);
    PhSetStringSetting2(L"PluginManagerTreeListColumns", &settings->sr);
    PhDereferenceObject(settings);
}

BOOLEAN PluginsNodeHashtableEqualFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    )
{
    PPH_PLUGIN_TREE_ROOT_NODE node1 = *(PPH_PLUGIN_TREE_ROOT_NODE *)Entry1;
    PPH_PLUGIN_TREE_ROOT_NODE node2 = *(PPH_PLUGIN_TREE_ROOT_NODE *)Entry2;

    return PhEqualString(node1->InternalName, node2->InternalName, TRUE);
}

ULONG PluginsNodeHashtableHashFunction(
    _In_ PVOID Entry
    )
{
    return PhHashStringRef(&(*(PPH_PLUGIN_TREE_ROOT_NODE*)Entry)->InternalName->sr, TRUE);
}

VOID DestroyPluginsNode(
    _In_ PPH_PLUGIN_TREE_ROOT_NODE Node
    )
{
    PhClearReference(&Node->InternalName);
    PhClearReference(&Node->Name);
    PhClearReference(&Node->Version);
    PhClearReference(&Node->Author);
    PhClearReference(&Node->Description);

    PhFree(Node);
}

PPH_PLUGIN_TREE_ROOT_NODE AddPluginsNode(
    _Inout_ PPH_PLUGMAN_CONTEXT Context,
    _In_ PPH_PLUGIN Plugin
    )
{
    PPH_PLUGIN_TREE_ROOT_NODE pluginNode;
    PH_IMAGE_VERSION_INFO versionInfo;

    pluginNode = PhAllocate(sizeof(PH_PLUGIN_TREE_ROOT_NODE));
    memset(pluginNode, 0, sizeof(PH_PLUGIN_TREE_ROOT_NODE));

    PhInitializeTreeNewNode(&pluginNode->Node);

    memset(pluginNode->TextCache, 0, sizeof(PH_STRINGREF) * PH_PLUGIN_TREE_COLUMN_ITEM_MAXIMUM);
    pluginNode->Node.TextCache = pluginNode->TextCache;
    pluginNode->Node.TextCacheSize = PH_PLUGIN_TREE_COLUMN_ITEM_MAXIMUM;

    pluginNode->PluginInstance = Plugin;
    pluginNode->PluginOptions = Plugin->Information.HasOptions;
    pluginNode->InternalName = PhCreateString2(&Plugin->Name);
    pluginNode->Name = PhCreateString(Plugin->Information.DisplayName);
    pluginNode->Author = PhCreateString(Plugin->Information.Author);
    pluginNode->Description = PhCreateString(Plugin->Information.Description);

    if (PhInitializeImageVersionInfo(&versionInfo, Plugin->FileName->Buffer))
    {
        pluginNode->Version = PhReferenceObject(versionInfo.FileVersion);
        PhDeleteImageVersionInfo(&versionInfo);
    }

    PhAddEntryHashtable(Context->NodeHashtable, &pluginNode);
    PhAddItemList(Context->NodeList, pluginNode);

    TreeNew_NodesStructured(Context->TreeNewHandle);

    return pluginNode;
}

PPH_PLUGIN_TREE_ROOT_NODE FindPluginsNode(
    _In_ PPH_PLUGMAN_CONTEXT Context,
    _In_ PPH_STRING InternalName
    )
{
    PH_PLUGIN_TREE_ROOT_NODE lookupPluginsNode;
    PPH_PLUGIN_TREE_ROOT_NODE lookupPluginsNodePtr = &lookupPluginsNode;
    PPH_PLUGIN_TREE_ROOT_NODE *pluginsNode;

    lookupPluginsNode.InternalName = InternalName;

    pluginsNode = (PPH_PLUGIN_TREE_ROOT_NODE*)PhFindEntryHashtable(
        Context->NodeHashtable,
        &lookupPluginsNodePtr
        );

    if (pluginsNode)
        return *pluginsNode;
    else
        return NULL;
}

VOID RemovePluginsNode(
    _In_ PPH_PLUGMAN_CONTEXT Context,
    _In_ PPH_PLUGIN_TREE_ROOT_NODE Node
    )
{
    ULONG index = 0;

    PhRemoveEntryHashtable(Context->NodeHashtable, &Node);

    if ((index = PhFindItemList(Context->NodeList, Node)) != ULONG_MAX)
    {
        PhRemoveItemList(Context->NodeList, index);
    }

    DestroyPluginsNode(Node);
    TreeNew_NodesStructured(Context->TreeNewHandle);
}

VOID UpdatePluginsNode(
    _In_ PPH_PLUGMAN_CONTEXT Context,
    _In_ PPH_PLUGIN_TREE_ROOT_NODE Node
    )
{
    memset(Node->TextCache, 0, sizeof(PH_STRINGREF) * PH_PLUGIN_TREE_COLUMN_ITEM_MAXIMUM);

    PhInvalidateTreeNewNode(&Node->Node, TN_CACHE_COLOR);
    TreeNew_NodesStructured(Context->TreeNewHandle);
}

BOOLEAN NTAPI PluginsTreeNewCallback(
    _In_ HWND hwnd,
    _In_ PH_TREENEW_MESSAGE Message,
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2,
    _In_opt_ PVOID Context
    )
{
    PPH_PLUGMAN_CONTEXT context = Context;
    PPH_PLUGIN_TREE_ROOT_NODE node;

    switch (Message)
    {
    case TreeNewGetChildren:
        {
            PPH_TREENEW_GET_CHILDREN getChildren = Parameter1;
            node = (PPH_PLUGIN_TREE_ROOT_NODE)getChildren->Node;

            if (!getChildren->Node)
            {
                static PVOID sortFunctions[] =
                {
                    SORT_FUNCTION(Name),
                    SORT_FUNCTION(Author),
                    SORT_FUNCTION(Version)
                };
                int (__cdecl *sortFunction)(void *, const void *, const void *);

                if (context->TreeNewSortColumn < PH_PLUGIN_TREE_COLUMN_ITEM_MAXIMUM)
                    sortFunction = sortFunctions[context->TreeNewSortColumn];
                else
                    sortFunction = NULL;

                if (sortFunction)
                {
                    qsort_s(context->NodeList->Items, context->NodeList->Count, sizeof(PVOID), sortFunction, context);
                }

                getChildren->Children = (PPH_TREENEW_NODE *)context->NodeList->Items;
                getChildren->NumberOfChildren = context->NodeList->Count;
            }
        }
        return TRUE;
    case TreeNewIsLeaf:
        {
            PPH_TREENEW_IS_LEAF isLeaf = (PPH_TREENEW_IS_LEAF)Parameter1;
            node = (PPH_PLUGIN_TREE_ROOT_NODE)isLeaf->Node;

            isLeaf->IsLeaf = TRUE;
        }
        return TRUE;
    case TreeNewGetCellText:
        {
            PPH_TREENEW_GET_CELL_TEXT getCellText = (PPH_TREENEW_GET_CELL_TEXT)Parameter1;
            node = (PPH_PLUGIN_TREE_ROOT_NODE)getCellText->Node;

            switch (getCellText->Id)
            {
            case PH_PLUGIN_TREE_COLUMN_ITEM_NAME:
                getCellText->Text = PhGetStringRef(node->Name);
                break;
            case PH_PLUGIN_TREE_COLUMN_ITEM_AUTHOR:
                getCellText->Text = PhGetStringRef(node->Author);
                break;
            case PH_PLUGIN_TREE_COLUMN_ITEM_VERSION:
                getCellText->Text = PhGetStringRef(node->Version);
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
            node = (PPH_PLUGIN_TREE_ROOT_NODE)getNodeColor->Node;

            getNodeColor->Flags = TN_CACHE | TN_AUTO_FORECOLOR;
        }
        return TRUE;
    case TreeNewSortChanged:
        {
            TreeNew_GetSort(hwnd, &context->TreeNewSortColumn, &context->TreeNewSortOrder);
            // Force a rebuild to sort the items.
            TreeNew_NodesStructured(hwnd);
        }
        return TRUE;
    case TreeNewKeyDown:
        {
            PPH_TREENEW_KEY_EVENT keyEvent = Parameter1;

            switch (keyEvent->VirtualKey)
            {
            case 'C':
                if (GetKeyState(VK_CONTROL) < 0)
                    SendMessage(context->WindowHandle, WM_COMMAND, ID_OBJECT_COPY, 0);
                break;
            case 'A':
                if (GetKeyState(VK_CONTROL) < 0)
                    TreeNew_SelectRange(context->TreeNewHandle, 0, -1);
                break;
            case VK_DELETE:
                SendMessage(context->WindowHandle, WM_COMMAND, ID_OBJECT_CLOSE, 0);
                break;
            }
        }
        return TRUE;
    case TreeNewLeftDoubleClick:
        {
            PPH_TREENEW_MOUSE_EVENT mouseEvent = Parameter1;

            SendMessage(context->WindowHandle, WM_COMMAND, WM_PH_PLUGINS_SHOWPROPERTIES, (LPARAM)mouseEvent);
        }
        return TRUE;
    case TreeNewContextMenu:
        {
            PPH_TREENEW_CONTEXT_MENU contextMenuEvent = Parameter1;
            
            SendMessage(context->WindowHandle, WM_COMMAND, ID_SHOWCONTEXTMENU, (LPARAM)contextMenuEvent);
        }
        return TRUE;
    //case TreeNewHeaderRightClick:
    //    {
    //        PH_TN_COLUMN_MENU_DATA data;
    //
    //        data.TreeNewHandle = hwnd;
    //        data.MouseEvent = Parameter1;
    //        data.DefaultSortColumn = 0;
    //        data.DefaultSortOrder = AscendingSortOrder;
    //        PhInitializeTreeNewColumnMenu(&data);
    //
    //        data.Selection = PhShowEMenu(data.Menu, hwnd, PH_EMENU_SHOW_LEFTRIGHT,
    //            PH_ALIGN_LEFT | PH_ALIGN_TOP, data.MouseEvent->ScreenLocation.x, data.MouseEvent->ScreenLocation.y);
    //        PhHandleTreeNewColumnMenu(&data);
    //        PhDeleteTreeNewColumnMenu(&data);
    //    }
    //    return TRUE;
    case TreeNewCustomDraw:
        {
            PPH_TREENEW_CUSTOM_DRAW customDraw = Parameter1;
            RECT rect = customDraw->CellRect;
            node = (PPH_PLUGIN_TREE_ROOT_NODE)customDraw->Node;

            switch (customDraw->Column->Id)
            {
            case PH_PLUGIN_TREE_COLUMN_ITEM_NAME:
                {
                    PH_STRINGREF text;
                    SIZE nameSize;
                    SIZE textSize;
 
                    rect.left += PH_SCALE_DPI(15);
                    rect.top += PH_SCALE_DPI(5);
                    rect.right -= PH_SCALE_DPI(5);
                    rect.bottom -= PH_SCALE_DPI(8);

                    // top
                    if (PhEnableThemeSupport)
                        SetTextColor(customDraw->Dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
                    else
                        SetTextColor(customDraw->Dc, RGB(0x0, 0x0, 0x0));

                    SelectFont(customDraw->Dc, context->TitleFontHandle);
                    text = PhIsNullOrEmptyString(node->Name) ? PhGetStringRef(node->InternalName) : PhGetStringRef(node->Name);
                    GetTextExtentPoint32(customDraw->Dc, text.Buffer, (ULONG)text.Length / sizeof(WCHAR), &nameSize);
                    DrawText(customDraw->Dc, text.Buffer, (ULONG)text.Length / sizeof(WCHAR), &rect, DT_TOP | DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE);

                    // bottom
                    if (PhEnableThemeSupport)
                        SetTextColor(customDraw->Dc, RGB(0x90, 0x90, 0x90));
                    else
                        SetTextColor(customDraw->Dc, RGB(0x64, 0x64, 0x64));

                    SelectFont(customDraw->Dc, context->NormalFontHandle);
                    text = PhGetStringRef(node->Description);
                    GetTextExtentPoint32(customDraw->Dc, text.Buffer, (ULONG)text.Length / sizeof(WCHAR), &textSize);
                    DrawText(
                        customDraw->Dc,
                        text.Buffer,
                        (ULONG)text.Length / sizeof(WCHAR),
                        &rect,
                        DT_BOTTOM | DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE
                        );
                }
                break;
            }
        }
        return TRUE;
    }

    return FALSE;
}

VOID ClearPluginsTree(
    _In_ PPH_PLUGMAN_CONTEXT Context
    )
{
    for (ULONG i = 0; i < Context->NodeList->Count; i++)
        DestroyPluginsNode(Context->NodeList->Items[i]);

    PhClearHashtable(Context->NodeHashtable);
    PhClearList(Context->NodeList);

    TreeNew_NodesStructured(Context->TreeNewHandle);
}

PPH_PLUGIN_TREE_ROOT_NODE GetSelectedPluginsNode(
    _In_ PPH_PLUGMAN_CONTEXT Context
    )
{
    PPH_PLUGIN_TREE_ROOT_NODE windowNode = NULL;

    for (ULONG i = 0; i < Context->NodeList->Count; i++)
    {
        windowNode = Context->NodeList->Items[i];

        if (windowNode->Node.Selected)
            return windowNode;
    }

    return NULL;
}

VOID GetSelectedPluginsNodes(
    _In_ PPH_PLUGMAN_CONTEXT Context,
    _Out_ PPH_PLUGIN_TREE_ROOT_NODE **PluginsNodes,
    _Out_ PULONG NumberOfPluginsNodes
    )
{
    PPH_LIST list;

    list = PhCreateList(2);

    for (ULONG i = 0; i < Context->NodeList->Count; i++)
    {
        PPH_PLUGIN_TREE_ROOT_NODE node = (PPH_PLUGIN_TREE_ROOT_NODE)Context->NodeList->Items[i];

        if (node->Node.Selected)
        {
            PhAddItemList(list, node);
        }
    }

    *PluginsNodes = PhAllocateCopy(list->Items, sizeof(PVOID) * list->Count);
    *NumberOfPluginsNodes = list->Count;

    PhDereferenceObject(list);
}

VOID InitializePluginsTree(
    _Inout_ PPH_PLUGMAN_CONTEXT Context
    )
{
    Context->NodeList = PhCreateList(100);
    Context->NodeHashtable = PhCreateHashtable(
        sizeof(PPH_PLUGIN_TREE_ROOT_NODE),
        PluginsNodeHashtableEqualFunction,
        PluginsNodeHashtableHashFunction,
        100
        );

    Context->NormalFontHandle = PhCreateCommonFont(-10, FW_NORMAL, NULL);
    Context->TitleFontHandle = PhCreateCommonFont(-14, FW_BOLD, NULL);

    PhSetControlTheme(Context->TreeNewHandle, L"explorer");

    TreeNew_SetCallback(Context->TreeNewHandle, PluginsTreeNewCallback, Context);
    TreeNew_SetRowHeight(Context->TreeNewHandle, PH_SCALE_DPI(48));

    PhAddTreeNewColumnEx2(Context->TreeNewHandle, PH_PLUGIN_TREE_COLUMN_ITEM_NAME, TRUE, L"Plugin", 80, PH_ALIGN_LEFT, 0, 0, TN_COLUMN_FLAG_CUSTOMDRAW);
    PhAddTreeNewColumnEx2(Context->TreeNewHandle, PH_PLUGIN_TREE_COLUMN_ITEM_AUTHOR, TRUE, L"Author", 80, PH_ALIGN_LEFT, 1, 0, 0);
    //PhAddTreeNewColumnEx2(Context->TreeNewHandle, PH_PLUGIN_TREE_COLUMN_ITEM_VERSION, TRUE, L"Version", 80, PH_ALIGN_CENTER, 2, DT_CENTER, 0);

    TreeNew_SetTriState(Context->TreeNewHandle, TRUE);

    PluginsLoadSettingsTreeList(Context);
}

VOID DeletePluginsTree(
    _In_ PPH_PLUGMAN_CONTEXT Context
    )
{
    if (Context->TitleFontHandle)
        DeleteFont(Context->TitleFontHandle);
    if (Context->NormalFontHandle)
        DeleteFont(Context->NormalFontHandle);

    PluginsSaveSettingsTreeList(Context);

    for (ULONG i = 0; i < Context->NodeList->Count; i++)
        DestroyPluginsNode(Context->NodeList->Items[i]);

    PhDereferenceObject(Context->NodeHashtable);
    PhDereferenceObject(Context->NodeList);
}

#pragma endregion

PWSTR PhpGetPluginBaseName(
    _In_ PPH_PLUGIN Plugin
    )
{
    if (Plugin->FileName)
    {
        PH_STRINGREF pathNamePart;
        PH_STRINGREF baseNamePart;

        if (PhSplitStringRefAtLastChar(&Plugin->FileName->sr, OBJ_NAME_PATH_SEPARATOR, &pathNamePart, &baseNamePart))
            return baseNamePart.Buffer;
        else
            return Plugin->FileName->Buffer;
    }
    else
    {
        // Fake disabled plugin.
        return Plugin->Name.Buffer;
    }
}

VOID PhpEnumerateLoadedPlugins(
    _In_ PPH_PLUGMAN_CONTEXT Context
    )
{
    PPH_AVL_LINKS links;

    for (links = PhMinimumElementAvlTree(&PhPluginsByName); links; links = PhSuccessorElementAvlTree(links))
    {
        PPH_PLUGIN plugin = CONTAINING_RECORD(links, PH_PLUGIN, Links);
        PH_STRINGREF pluginBaseName;

        PhInitializeStringRefLongHint(&pluginBaseName, PhpGetPluginBaseName(plugin));

        if (PhIsPluginDisabled(&pluginBaseName))
            continue;

        AddPluginsNode(Context, plugin);
    }
}

INT_PTR CALLBACK PhpPluginsDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PPH_PLUGMAN_CONTEXT context;

    if (uMsg == WM_INITDIALOG)
    {
        context = PhAllocate(sizeof(PH_PLUGMAN_CONTEXT));
        memset(context, 0, sizeof(PH_PLUGMAN_CONTEXT));

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

            context->WindowHandle = hwndDlg;
            context->TreeNewHandle = GetDlgItem(hwndDlg, IDC_PLUGINTREE);

            InitializePluginsTree(context);

            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);
            PhAddLayoutItem(&context->LayoutManager, context->TreeNewHandle, NULL, PH_ANCHOR_ALL);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDC_DISABLED), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_LEFT);
            PhAddLayoutItem(&context->LayoutManager, GetDlgItem(hwndDlg, IDOK), NULL, PH_ANCHOR_BOTTOM | PH_ANCHOR_RIGHT);

            if (PhGetIntegerPairSetting(L"PluginManagerWindowPosition").X != 0)
                PhLoadWindowPlacementFromSetting(L"PluginManagerWindowPosition", L"PluginManagerWindowSize", hwndDlg);
            else
                PhCenterWindow(hwndDlg, PhMainWndHandle);

            context->MinimumSize.left = 0;
            context->MinimumSize.top = 0;
            context->MinimumSize.right = 300;
            context->MinimumSize.bottom = 100;
            MapDialogRect(hwndDlg, &context->MinimumSize);

            PhpEnumerateLoadedPlugins(context);
            TreeNew_AutoSizeColumn(context->TreeNewHandle, PH_PLUGIN_TREE_COLUMN_ITEM_NAME, TN_AUTOSIZE_REMAINING_SPACE);
            PhSetWindowText(GetDlgItem(hwndDlg, IDC_DISABLED), PhaFormatString(L"Disabled Plugins (%lu)", PhpDisabledPluginsCount())->Buffer);

            PhInitializeWindowTheme(hwndDlg, PhEnableThemeSupport);
        }
        break;
    case WM_DESTROY:
        {
            PhSaveWindowPlacementToSetting(L"PluginManagerWindowPosition", L"PluginManagerWindowSize", hwndDlg);

            PhDeleteLayoutManager(&context->LayoutManager);

            DeletePluginsTree(context);

            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);

            PhFree(context);

            PostQuitMessage(0);
        }
        break;
    case WM_PH_PLUGINS_SHOWDIALOG:
        {
            if (IsMinimized(hwndDlg))
                ShowWindow(hwndDlg, SW_RESTORE);
            else
                ShowWindow(hwndDlg, SW_SHOW);

            SetForegroundWindow(hwndDlg);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDOK:
            case IDCANCEL:
                {
                    DestroyWindow(hwndDlg);
                }
                break;
            case IDC_DISABLED:
                {
                    DialogBox(
                        PhInstanceHandle,
                        MAKEINTRESOURCE(IDD_PLUGINSDISABLED),
                        hwndDlg,
                        PhpPluginsDisabledDlgProc
                        );

                    ClearPluginsTree(context);
                    PhpEnumerateLoadedPlugins(context);
                    TreeNew_AutoSizeColumn(context->TreeNewHandle, PH_PLUGIN_TREE_COLUMN_ITEM_NAME, TN_AUTOSIZE_REMAINING_SPACE);
                    PhSetWindowText(GetDlgItem(hwndDlg, IDC_DISABLED), PhaFormatString(L"Disabled Plugins (%lu)", PhpDisabledPluginsCount())->Buffer);
                }
                break;
            case ID_SHOWCONTEXTMENU:
                {
                    PPH_EMENU menu;
                    PPH_EMENU_ITEM selectedItem;
                    //PPH_EMENU_ITEM uninstallItem;
                    PPH_TREENEW_CONTEXT_MENU contextMenuEvent = (PPH_TREENEW_CONTEXT_MENU)lParam;
                    PPH_PLUGIN_TREE_ROOT_NODE selectedNode = (PPH_PLUGIN_TREE_ROOT_NODE)contextMenuEvent->Node;

                    if (!selectedNode)
                        break;

                    menu = PhCreateEMenu();
                    //PhInsertEMenuItem(menu, uninstallItem = PhCreateEMenuItem(0, PH_PLUGIN_TREE_ITEM_MENU_UNINSTALL, L"Uninstall", NULL, NULL), ULONG_MAX);
                    //PhInsertEMenuItem(menu, PhCreateEMenuSeparator(), ULONG_MAX);
                    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, PH_PLUGIN_TREE_ITEM_MENU_DISABLE, L"Disable", NULL, NULL), ULONG_MAX);
                    PhInsertEMenuItem(menu, PhCreateEMenuSeparator(), ULONG_MAX);
                    PhInsertEMenuItem(menu, PhCreateEMenuItem(0, PH_PLUGIN_TREE_ITEM_MENU_PROPERTIES, L"Properties", NULL, NULL), ULONG_MAX);

                    //if (!PhGetOwnTokenAttributes().Elevated)
                    //{
                    //    HBITMAP shieldBitmap;
                    //
                    //    if (shieldBitmap = PhGetShieldBitmap())
                    //        uninstallItem->Bitmap = shieldBitmap;
                    //}

                    selectedItem = PhShowEMenu(
                        menu,
                        hwndDlg,
                        PH_EMENU_SHOW_LEFTRIGHT,
                        PH_ALIGN_LEFT | PH_ALIGN_TOP,
                        contextMenuEvent->Location.x,
                        contextMenuEvent->Location.y
                        );

                    if (selectedItem && selectedItem->Id != ULONG_MAX)
                    {
                        switch (selectedItem->Id)
                        {
                        case PH_PLUGIN_TREE_ITEM_MENU_UNINSTALL:
                            {
                                //if (PhShowConfirmMessage(
                                //    hwndDlg,
                                //    L"Uninstall",
                                //    PhGetString(selectedNode->Name),
                                //    L"Changes may require a restart to take effect...",
                                //    TRUE
                                //    ))
                                //{
                                //
                                //}
                            }
                            break;
                        case PH_PLUGIN_TREE_ITEM_MENU_DISABLE:
                            {
                                PWSTR baseName;
                                PH_STRINGREF baseNameRef;

                                baseName = PhpGetPluginBaseName(selectedNode->PluginInstance);
                                PhInitializeStringRef(&baseNameRef, baseName);

                                PhSetPluginDisabled(&baseNameRef, TRUE);

                                RemovePluginsNode(context, selectedNode);

                                PhSetWindowText(GetDlgItem(hwndDlg, IDC_DISABLED), PhaFormatString(L"Disabled Plugins (%lu)", PhpDisabledPluginsCount())->Buffer);
                            }
                            break;
                        case PH_PLUGIN_TREE_ITEM_MENU_PROPERTIES:
                            {
                                DialogBoxParam(
                                    PhInstanceHandle, 
                                    MAKEINTRESOURCE(IDD_PLUGINPROPERTIES), 
                                    hwndDlg, 
                                    PhpPluginPropertiesDlgProc,
                                    (LPARAM)selectedNode->PluginInstance
                                    );
                            }
                            break;
                        }
                    }
                }
                break;
            case WM_PH_PLUGINS_SHOWPROPERTIES:
                {
                    PPH_TREENEW_MOUSE_EVENT mouseEvent = (PPH_TREENEW_MOUSE_EVENT)lParam;
                    PPH_PLUGIN_TREE_ROOT_NODE selectedNode = (PPH_PLUGIN_TREE_ROOT_NODE)mouseEvent->Node;

                    if (!selectedNode)
                        break;

                    DialogBoxParam(
                        PhInstanceHandle,
                        MAKEINTRESOURCE(IDD_PLUGINPROPERTIES),
                        hwndDlg,
                        PhpPluginPropertiesDlgProc,
                        (LPARAM)selectedNode->PluginInstance
                        );
                }
                break;
            }
        }
        break;
    case WM_SIZE:
        {
            PhLayoutManagerLayout(&context->LayoutManager);
            TreeNew_AutoSizeColumn(context->TreeNewHandle, PH_PLUGIN_TREE_COLUMN_ITEM_NAME, TN_AUTOSIZE_REMAINING_SPACE);
        }
        break;
    case WM_SIZING:
        {
            PhResizingMinimumSize((PRECT)lParam, wParam, context->MinimumSize.right, context->MinimumSize.bottom);
        }
        break;
    }

    return FALSE;
}

NTSTATUS PhpPluginsDialogThreadStart(
    _In_ PVOID Parameter
    )
{
    BOOL result;
    MSG message;
    PH_AUTO_POOL autoPool;

    PhInitializeAutoPool(&autoPool);

    PhPluginsWindowHandle = CreateDialog(
        PhInstanceHandle,
        MAKEINTRESOURCE(IDD_PLUGINS),
        NULL,
        PhpPluginsDlgProc
        );

    PhSetEvent(&PhPluginsInitializedEvent);

    while (result = GetMessage(&message, NULL, 0, 0))
    {
        if (result == -1)
            break;

        if (!IsDialogMessage(PhPluginsWindowHandle, &message))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        PhDrainAutoPool(&autoPool);
    }

    PhDeleteAutoPool(&autoPool);
    PhResetEvent(&PhPluginsInitializedEvent);

    if (PhPluginsThreadHandle)
    {
        NtClose(PhPluginsThreadHandle);
        PhPluginsThreadHandle = NULL;
    }

    return STATUS_SUCCESS;
}

VOID PhShowPluginsDialog(
    _In_ HWND ParentWindowHandle
    )
{
    if (PhPluginsEnabled)
    {
        if (!PhPluginsThreadHandle)
        {
            if (!NT_SUCCESS(PhCreateThreadEx(&PhPluginsThreadHandle, PhpPluginsDialogThreadStart, NULL)))
            {
                PhShowError(PhMainWndHandle, L"Unable to create the window.");
                return;
            }

            PhWaitForEvent(&PhPluginsInitializedEvent, NULL);
        }

        PostMessage(PhPluginsWindowHandle, WM_PH_PLUGINS_SHOWDIALOG, 0, 0);
    }
    else
    {
        PhShowInformation2(
            ParentWindowHandle, 
            L"Plugins are not enabled.", 
            L"To use plugins enable them in Options and restart Process Hacker."
            );
    }
}

VOID PhpRefreshPluginDetails(
    _In_ HWND hwndDlg,
    _In_ PPH_PLUGIN SelectedPlugin
    )
{
    PPH_STRING fileName;
    PH_IMAGE_VERSION_INFO versionInfo;

    fileName = SelectedPlugin->FileName;

    PhSetDialogItemText(hwndDlg, IDC_NAME, SelectedPlugin->Information.DisplayName ? SelectedPlugin->Information.DisplayName : L"(unnamed)");
    PhSetDialogItemText(hwndDlg, IDC_INTERNALNAME, SelectedPlugin->Name.Buffer);
    PhSetDialogItemText(hwndDlg, IDC_AUTHOR, SelectedPlugin->Information.Author);
    PhSetDialogItemText(hwndDlg, IDC_FILENAME, PH_AUTO_T(PH_STRING, PhGetBaseName(fileName))->Buffer);
    PhSetDialogItemText(hwndDlg, IDC_DESCRIPTION, SelectedPlugin->Information.Description);
    PhSetDialogItemText(hwndDlg, IDC_URL, SelectedPlugin->Information.Url);

    if (PhInitializeImageVersionInfo(&versionInfo, fileName->Buffer))
    {
        PhSetDialogItemText(hwndDlg, IDC_VERSION, PhGetStringOrDefault(versionInfo.FileVersion, L"Unknown"));
        PhDeleteImageVersionInfo(&versionInfo);
    }
    else
    {
        PhSetDialogItemText(hwndDlg, IDC_VERSION, L"Unknown");
    }

    ShowWindow(GetDlgItem(hwndDlg, IDC_OPENURL), SelectedPlugin->Information.Url ? SW_SHOW : SW_HIDE);
    EnableWindow(GetDlgItem(hwndDlg, IDC_OPTIONS), SelectedPlugin->Information.HasOptions);
}

INT_PTR CALLBACK PhpPluginPropertiesDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PPH_PLUGIN selectedPlugin = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        selectedPlugin = (PPH_PLUGIN)lParam;
        PhSetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT, selectedPlugin);
    }
    else
    {
        selectedPlugin = PhGetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);

        if (uMsg == WM_DESTROY)
        {
            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
        }
    }

    if (selectedPlugin == NULL)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            PhCenterWindow(hwndDlg, GetParent(hwndDlg));

            PhpRefreshPluginDetails(hwndDlg, selectedPlugin);

            PhSetDialogFocus(hwndDlg, GetDlgItem(hwndDlg, IDOK));

            PhInitializeWindowTheme(hwndDlg, PhEnableThemeSupport);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDCANCEL:
            case IDOK:
                EndDialog(hwndDlg, IDOK);
                break;
            case IDC_OPTIONS:
                {
                    PhInvokeCallback(PhGetPluginCallback(selectedPlugin, PluginCallbackShowOptions), hwndDlg);
                }
                break;
            }
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            switch (header->code)
            {
            case NM_CLICK:
                {
                    if (header->hwndFrom == GetDlgItem(hwndDlg, IDC_OPENURL))
                    {
                        PhShellExecute(hwndDlg, selectedPlugin->Information.Url, NULL);
                    }
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}

typedef struct _PLUGIN_DISABLED_CONTEXT
{
    PH_QUEUED_LOCK ListLock;
    HWND DialogHandle;
    HWND ListViewHandle;
} PLUGIN_DISABLED_CONTEXT, *PPLUGIN_DISABLED_CONTEXT;

VOID PhpAddDisabledPlugins(
    _In_ PPLUGIN_DISABLED_CONTEXT Context
    )
{
    PPH_STRING disabled;
    PH_STRINGREF remainingPart;
    PH_STRINGREF part;
    PPH_STRING displayText;
    INT lvItemIndex;

    disabled = PhGetStringSetting(L"DisabledPlugins");
    remainingPart = disabled->sr;

    while (remainingPart.Length)
    {
        PhSplitStringRefAtChar(&remainingPart, '|', &part, &remainingPart);

        if (part.Length)
        {
            displayText = PhCreateString2(&part);

            PhAcquireQueuedLockExclusive(&Context->ListLock);
            lvItemIndex = PhAddListViewItem(Context->ListViewHandle, MAXINT, PhGetString(displayText), displayText);
            PhReleaseQueuedLockExclusive(&Context->ListLock);

            ListView_SetCheckState(Context->ListViewHandle, lvItemIndex, TRUE);
        }
    }

    PhDereferenceObject(disabled);
}

ULONG PhpDisabledPluginsCount(
    VOID
    )
{
    PPH_STRING disabled;
    PH_STRINGREF remainingPart;
    PH_STRINGREF part;
    ULONG count = 0;

    disabled = PhGetStringSetting(L"DisabledPlugins");
    remainingPart = disabled->sr;

    while (remainingPart.Length)
    {
        PhSplitStringRefAtChar(&remainingPart, '|', &part, &remainingPart);

        if (part.Length)
            count++;
    }

    PhDereferenceObject(disabled);

    return count;
}

INT_PTR CALLBACK PhpPluginsDisabledDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PPLUGIN_DISABLED_CONTEXT context = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        context = PhAllocate(sizeof(PLUGIN_DISABLED_CONTEXT));
        memset(context, 0, sizeof(PLUGIN_DISABLED_CONTEXT));

        PhSetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT, context);
    }
    else
    {
        context = PhGetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);

        if (uMsg == WM_DESTROY)
        {
            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
            PhFree(context);
            context = NULL;
        }
    }

    if (context == NULL)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            context->DialogHandle = hwndDlg;
            context->ListViewHandle = GetDlgItem(hwndDlg, IDC_LIST_DISABLED);

            PhCenterWindow(hwndDlg, GetParent(hwndDlg));

            PhSetListViewStyle(context->ListViewHandle, FALSE, TRUE);
            ListView_SetExtendedListViewStyleEx(context->ListViewHandle,
                LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER,
                LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            PhSetControlTheme(context->ListViewHandle, L"explorer");
            PhAddListViewColumn(context->ListViewHandle, 0, 0, 0, LVCFMT_LEFT, 400, L"Property");
            PhSetExtendedListView(context->ListViewHandle);

            PhpAddDisabledPlugins(context);
            ExtendedListView_SetColumnWidth(context->ListViewHandle, 0, ELVSCW_AUTOSIZE_REMAININGSPACE);

            PhSetDialogFocus(hwndDlg, GetDlgItem(hwndDlg, IDOK));

            PhInitializeWindowTheme(hwndDlg, PhEnableThemeSupport);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDCANCEL:
            case IDOK:
                EndDialog(hwndDlg, IDOK);
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

                if (!PhTryAcquireReleaseQueuedLockExclusive(&context->ListLock))
                    break;

                if (listView->uChanged & LVIF_STATE)
                {
                    switch (listView->uNewState & LVIS_STATEIMAGEMASK)
                    {
                    case INDEXTOSTATEIMAGEMASK(2): // checked
                        {
                            PPH_STRING param = (PPH_STRING)listView->lParam;

                            PhSetPluginDisabled(&param->sr, TRUE);
                        }
                        break;
                    case INDEXTOSTATEIMAGEMASK(1): // unchecked
                        {
                            PPH_STRING param = (PPH_STRING)listView->lParam;

                            PhSetPluginDisabled(&param->sr, FALSE);
                        }
                        break;
                    }
                }
            }
        }
        break;
    }

    return FALSE;
}
