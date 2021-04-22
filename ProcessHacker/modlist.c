/*
 * Process Hacker -
 *   module list
 *
 * Copyright (C) 2010-2016 wj32
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
#include <modlist.h>

#include <emenu.h>
#include <verify.h>
#include <settings.h>

#include <extmgri.h>
#include <modprv.h>
#include <phplug.h>
#include <phsettings.h>

BOOLEAN PhpModuleNodeHashtableEqualFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    );

ULONG PhpModuleNodeHashtableHashFunction(
    _In_ PVOID Entry
    );

VOID PhpDestroyModuleNode(
    _In_ PPH_MODULE_LIST_CONTEXT Context,
    _In_ PPH_MODULE_NODE ModuleNode
    );

VOID PhpRemoveModuleNode(
    _In_ PPH_MODULE_NODE ModuleNode,
    _In_ PPH_MODULE_LIST_CONTEXT Context
    );

LONG PhpModuleTreeNewPostSortFunction(
    _In_ LONG Result,
    _In_ PVOID Node1,
    _In_ PVOID Node2,
    _In_ PH_SORT_ORDER SortOrder
    );

BOOLEAN NTAPI PhpModuleTreeNewCallback(
    _In_ HWND hwnd,
    _In_ PH_TREENEW_MESSAGE Message,
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2,
    _In_opt_ PVOID Context
    );

VOID PhInitializeModuleList(
    _In_ HWND ParentWindowHandle,
    _In_ HWND TreeNewHandle,
    _Out_ PPH_MODULE_LIST_CONTEXT Context
    )
{
    memset(Context, 0, sizeof(PH_MODULE_LIST_CONTEXT));
    Context->EnableStateHighlighting = TRUE;

    Context->NodeHashtable = PhCreateHashtable(
        sizeof(PPH_MODULE_NODE),
        PhpModuleNodeHashtableEqualFunction,
        PhpModuleNodeHashtableHashFunction,
        100
        );
    Context->NodeList = PhCreateList(100);
    Context->NodeRootList = PhCreateList(2);

    Context->ParentWindowHandle = ParentWindowHandle;
    Context->TreeNewHandle = TreeNewHandle;

    PhSetControlTheme(Context->TreeNewHandle, L"explorer");

    TreeNew_SetCallback(Context->TreeNewHandle, PhpModuleTreeNewCallback, Context);

    TreeNew_SetRedraw(Context->TreeNewHandle, FALSE);

    // Default columns
    PhAddTreeNewColumn(Context->TreeNewHandle, PHMOTLC_NAME, TRUE, L"Name", 100, PH_ALIGN_LEFT, -2, 0);
    PhAddTreeNewColumn(Context->TreeNewHandle, PHMOTLC_BASEADDRESS, TRUE, L"Base address", 80, PH_ALIGN_RIGHT, 0, DT_RIGHT);
    PhAddTreeNewColumnEx(Context->TreeNewHandle, PHMOTLC_SIZE, TRUE, L"Size", 60, PH_ALIGN_RIGHT, 1, DT_RIGHT, TRUE);
    PhAddTreeNewColumn(Context->TreeNewHandle, PHMOTLC_DESCRIPTION, TRUE, L"Description", 160, PH_ALIGN_LEFT, 2, 0);

    PhAddTreeNewColumn(Context->TreeNewHandle, PHMOTLC_COMPANYNAME, FALSE, L"Company name", 180, PH_ALIGN_LEFT, ULONG_MAX, 0);
    PhAddTreeNewColumn(Context->TreeNewHandle, PHMOTLC_VERSION, FALSE, L"Version", 100, PH_ALIGN_LEFT, ULONG_MAX, 0);
    PhAddTreeNewColumn(Context->TreeNewHandle, PHMOTLC_FILENAME, FALSE, L"File name", 180, PH_ALIGN_LEFT, ULONG_MAX, DT_PATH_ELLIPSIS);

    PhAddTreeNewColumn(Context->TreeNewHandle, PHMOTLC_TYPE, FALSE, L"Type", 80, PH_ALIGN_LEFT, ULONG_MAX, 0);
    PhAddTreeNewColumnEx(Context->TreeNewHandle, PHMOTLC_LOADCOUNT, FALSE, L"Load count", 40, PH_ALIGN_RIGHT, ULONG_MAX, DT_RIGHT, TRUE);
    PhAddTreeNewColumn(Context->TreeNewHandle, PHMOTLC_VERIFICATIONSTATUS, FALSE, L"Verification status", 70, PH_ALIGN_LEFT, ULONG_MAX, 0);
    PhAddTreeNewColumn(Context->TreeNewHandle, PHMOTLC_VERIFIEDSIGNER, FALSE, L"Verified signer", 100, PH_ALIGN_LEFT, ULONG_MAX, 0);
    PhAddTreeNewColumnEx(Context->TreeNewHandle, PHMOTLC_ASLR, FALSE, L"ASLR", 50, PH_ALIGN_LEFT, ULONG_MAX, 0, TRUE);
    PhAddTreeNewColumnEx(Context->TreeNewHandle, PHMOTLC_TIMESTAMP, FALSE, L"Time stamp", 100, PH_ALIGN_LEFT, ULONG_MAX, 0, TRUE);
    PhAddTreeNewColumnEx(Context->TreeNewHandle, PHMOTLC_CFGUARD, FALSE, L"CF Guard", 70, PH_ALIGN_LEFT, ULONG_MAX, 0, TRUE);
    PhAddTreeNewColumnEx(Context->TreeNewHandle, PHMOTLC_LOADTIME, FALSE, L"Load time", 100, PH_ALIGN_LEFT, ULONG_MAX, 0, TRUE);
    PhAddTreeNewColumn(Context->TreeNewHandle, PHMOTLC_LOADREASON, FALSE, L"Load reason", 80, PH_ALIGN_LEFT, ULONG_MAX, 0);
    PhAddTreeNewColumnEx(Context->TreeNewHandle, PHMOTLC_FILEMODIFIEDTIME, FALSE, L"File modified time", 140, PH_ALIGN_LEFT, ULONG_MAX, 0, TRUE);
    PhAddTreeNewColumnEx(Context->TreeNewHandle, PHMOTLC_FILESIZE, FALSE, L"File size", 70, PH_ALIGN_RIGHT, ULONG_MAX, DT_RIGHT, TRUE);
    PhAddTreeNewColumnEx(Context->TreeNewHandle, PHMOTLC_ENTRYPOINT, FALSE, L"Entry point", 70, PH_ALIGN_LEFT, ULONG_MAX, 0, TRUE);
    PhAddTreeNewColumnEx(Context->TreeNewHandle, PHMOTLC_PARENTBASEADDRESS, FALSE, L"Parent base address", 70, PH_ALIGN_RIGHT, ULONG_MAX, DT_RIGHT, TRUE);

    TreeNew_SetRedraw(Context->TreeNewHandle, TRUE);

    TreeNew_SetTriState(Context->TreeNewHandle, TRUE);
    TreeNew_SetSort(Context->TreeNewHandle, 0, NoSortOrder);

    PhCmInitializeManager(&Context->Cm, Context->TreeNewHandle, PHMOTLC_MAXIMUM, PhpModuleTreeNewPostSortFunction);

    PhInitializeTreeNewFilterSupport(&Context->TreeFilterSupport, Context->TreeNewHandle, Context->NodeList);
}

VOID PhDeleteModuleList(
    _In_ PPH_MODULE_LIST_CONTEXT Context
    )
{
    ULONG i;

    PhDeleteTreeNewFilterSupport(&Context->TreeFilterSupport);

    if (Context->BoldFont)
        DeleteFont(Context->BoldFont);

    PhCmDeleteManager(&Context->Cm);

    for (i = 0; i < Context->NodeList->Count; i++)
        PhpDestroyModuleNode(Context, Context->NodeList->Items[i]);

    PhDereferenceObject(Context->NodeHashtable);
    PhDereferenceObject(Context->NodeList);
    PhDereferenceObject(Context->NodeRootList);
}

BOOLEAN PhpModuleNodeHashtableEqualFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    )
{
    PPH_MODULE_NODE moduleNode1 = *(PPH_MODULE_NODE *)Entry1;
    PPH_MODULE_NODE moduleNode2 = *(PPH_MODULE_NODE *)Entry2;

    return moduleNode1->ModuleItem == moduleNode2->ModuleItem;
}

ULONG PhpModuleNodeHashtableHashFunction(
    _In_ PVOID Entry
    )
{
    return PhHashIntPtr((ULONG_PTR)(*(PPH_MODULE_NODE *)Entry)->ModuleItem);
}

VOID PhLoadSettingsModuleList(
    _Inout_ PPH_MODULE_LIST_CONTEXT Context
    )
{
    ULONG flags;
    PPH_STRING settings;
    PPH_STRING sortSettings;

    flags = PhGetIntegerSetting(L"ModuleTreeListFlags");
    settings = PhGetStringSetting(L"ModuleTreeListColumns");
    sortSettings = PhGetStringSetting(L"ModuleTreeListSort");

    Context->Flags = flags;
    PhCmLoadSettingsEx(Context->TreeNewHandle, &Context->Cm, 0, &settings->sr, &sortSettings->sr);

    PhDereferenceObject(settings);
    PhDereferenceObject(sortSettings);
}

VOID PhSaveSettingsModuleList(
    _Inout_ PPH_MODULE_LIST_CONTEXT Context
    )
{
    PPH_STRING settings;
    PPH_STRING sortSettings;

    settings = PhCmSaveSettingsEx(Context->TreeNewHandle, &Context->Cm, 0, &sortSettings);

    PhSetIntegerSetting(L"ModuleTreeListFlags", Context->Flags);
    PhSetStringSetting2(L"ModuleTreeListColumns", &settings->sr);
    PhSetStringSetting2(L"ModuleTreeListSort", &sortSettings->sr);

    PhDereferenceObject(settings);
    PhDereferenceObject(sortSettings);
}

VOID PhSetOptionsModuleList(
    _Inout_ PPH_MODULE_LIST_CONTEXT Context,
    _In_ ULONG Options
    )
{
    switch (Options)
    {
    case PH_MODULE_FLAGS_DYNAMIC_OPTION:
        Context->HideDynamicModules = !Context->HideDynamicModules;
        break;
    case PH_MODULE_FLAGS_MAPPED_OPTION:
        Context->HideMappedModules = !Context->HideMappedModules;
        break;
    case PH_MODULE_FLAGS_STATIC_OPTION:
        Context->HideStaticModules = !Context->HideStaticModules;
        break;
    case PH_MODULE_FLAGS_SIGNED_OPTION:
        Context->HideSignedModules = !Context->HideSignedModules;
        break;
    case PH_MODULE_FLAGS_HIGHLIGHT_UNSIGNED_OPTION:
        Context->HighlightUntrustedModules = !Context->HighlightUntrustedModules;
        break;
    case PH_MODULE_FLAGS_HIGHLIGHT_DOTNET_OPTION:
        Context->HighlightDotNetModules = !Context->HighlightDotNetModules;
        break;
    case PH_MODULE_FLAGS_HIGHLIGHT_IMMERSIVE_OPTION:
        Context->HighlightImmersiveModules = !Context->HighlightImmersiveModules;
        break;
    case PH_MODULE_FLAGS_HIGHLIGHT_RELOCATED_OPTION:
        Context->HighlightRelocatedModules = !Context->HighlightRelocatedModules;
        break;
    case PH_MODULE_FLAGS_SYSTEM_OPTION:
        Context->HideSystemModules = !Context->HideSystemModules;
        break;
    case PH_MODULE_FLAGS_HIGHLIGHT_SYSTEM_OPTION:
        Context->HighlightSystemModules = !Context->HighlightSystemModules;
        break;
    }
}

PPH_MODULE_NODE PhCreateModuleNode(
    _Inout_ PPH_MODULE_LIST_CONTEXT Context,
    _In_ PPH_MODULE_ITEM ModuleItem,
    _In_ ULONG RunId
    )
{
    PPH_MODULE_NODE moduleNode;

    moduleNode = PhAllocate(PhEmGetObjectSize(EmModuleNodeType, sizeof(PH_MODULE_NODE)));
    memset(moduleNode, 0, sizeof(PH_MODULE_NODE));
    PhInitializeTreeNewNode(&moduleNode->Node);

    moduleNode->Children = PhCreateList(1);

    if (Context->EnableStateHighlighting && RunId != 1)
    {
        PhChangeShStateTn(
            &moduleNode->Node,
            &moduleNode->ShState,
            &Context->NodeStateList,
            NewItemState,
            PhCsColorNew,
            NULL
            );
    }

    moduleNode->ModuleItem = PhReferenceObject(ModuleItem);

    memset(moduleNode->TextCache, 0, sizeof(PH_STRINGREF) * PHMOTLC_MAXIMUM);
    moduleNode->Node.TextCache = moduleNode->TextCache;
    moduleNode->Node.TextCacheSize = PHMOTLC_MAXIMUM;

    PhAddEntryHashtable(Context->NodeHashtable, &moduleNode);
    PhAddItemList(Context->NodeList, moduleNode);

    if (Context->TreeFilterSupport.FilterList)
        moduleNode->Node.Visible = PhApplyTreeNewFiltersToNode(&Context->TreeFilterSupport, &moduleNode->Node);

    PhEmCallObjectOperation(EmModuleNodeType, moduleNode, EmObjectCreate);

    TreeNew_NodesStructured(Context->TreeNewHandle);

    return moduleNode;
}

PPH_MODULE_NODE PhAddModuleNode(
    _Inout_ PPH_MODULE_LIST_CONTEXT Context,
    _In_ PPH_MODULE_ITEM ModuleItem,
    _In_ ULONG RunId
    )
{
    PPH_MODULE_NODE moduleNode;
    PPH_MODULE_NODE parentNode;
    ULONG i;

    moduleNode = PhCreateModuleNode(Context, ModuleItem, RunId);

    for (i = 0; i < Context->NodeList->Count; i++)
    {
        parentNode = Context->NodeList->Items[i];
    
        if (parentNode != moduleNode && parentNode->ModuleItem->BaseAddress == ModuleItem->ParentBaseAddress)
        {
            moduleNode->Parent = parentNode;
            PhAddItemList(parentNode->Children, moduleNode);
            break;
        }
    }

    if (!moduleNode->Parent)
    {
        moduleNode->Node.Expanded = TRUE;
        PhAddItemList(Context->NodeRootList, moduleNode);
    }

    return moduleNode;
}

PPH_MODULE_NODE PhFindModuleNode(
    _In_ PPH_MODULE_LIST_CONTEXT Context,
    _In_ PPH_MODULE_ITEM ModuleItem
    )
{
    PH_MODULE_NODE lookupModuleNode;
    PPH_MODULE_NODE lookupModuleNodePtr = &lookupModuleNode;
    PPH_MODULE_NODE *moduleNode;

    lookupModuleNode.ModuleItem = ModuleItem;

    moduleNode = (PPH_MODULE_NODE *)PhFindEntryHashtable(
        Context->NodeHashtable,
        &lookupModuleNodePtr
        );

    if (moduleNode)
        return *moduleNode;
    else
        return NULL;
}

VOID PhRemoveModuleNode(
    _In_ PPH_MODULE_LIST_CONTEXT Context,
    _In_ PPH_MODULE_NODE ModuleNode
    )
{
    // Remove from the hashtable here to avoid problems in case the key is re-used.
    PhRemoveEntryHashtable(Context->NodeHashtable, &ModuleNode);

    if (Context->EnableStateHighlighting)
    {
        PhChangeShStateTn(
            &ModuleNode->Node,
            &ModuleNode->ShState,
            &Context->NodeStateList,
            RemovingItemState,
            PhCsColorRemoved,
            Context->TreeNewHandle
            );
    }
    else
    {
        PhpRemoveModuleNode(ModuleNode, Context);
    }
}

VOID PhpDestroyModuleNode(
    _In_ PPH_MODULE_LIST_CONTEXT Context,
    _In_ PPH_MODULE_NODE ModuleNode
    )
{
    ULONG index;

    PhEmCallObjectOperation(EmModuleNodeType, ModuleNode, EmObjectDelete);

    if (ModuleNode->Parent)
    {
        // Remove the node from its parent.

        if ((index = PhFindItemList(ModuleNode->Parent->Children, ModuleNode)) != ULONG_MAX)
            PhRemoveItemList(ModuleNode->Parent->Children, index);
    }
    else
    {
        // Remove the node from the root list.

        if ((index = PhFindItemList(Context->NodeRootList, ModuleNode)) != ULONG_MAX)
            PhRemoveItemList(Context->NodeRootList, index);
    }

    // Move the node's children to the root list.
    for (index = 0; index < ModuleNode->Children->Count; index++)
    {
        PPH_MODULE_NODE node = ModuleNode->Children->Items[index];

        node->Parent = NULL;
        PhAddItemList(Context->NodeRootList, node);
    }

    PhClearReference(&ModuleNode->TooltipText);

    PhClearReference(&ModuleNode->SizeText);
    PhClearReference(&ModuleNode->TimeStampText);
    PhClearReference(&ModuleNode->LoadTimeText);
    PhClearReference(&ModuleNode->FileModifiedTimeText);
    PhClearReference(&ModuleNode->FileSizeText);

    PhDereferenceObject(ModuleNode->ModuleItem);

    PhFree(ModuleNode);
}

VOID PhpRemoveModuleNode(
    _In_ PPH_MODULE_NODE ModuleNode,
    _In_ PPH_MODULE_LIST_CONTEXT Context // PH_TICK_SH_STATE requires this parameter to be after ModuleNode
    )
{
    ULONG index;

    // Remove from list and cleanup.

    if ((index = PhFindItemList(Context->NodeList, ModuleNode)) != ULONG_MAX)
        PhRemoveItemList(Context->NodeList, index);

    PhpDestroyModuleNode(Context, ModuleNode);

    TreeNew_NodesStructured(Context->TreeNewHandle);
}

VOID PhUpdateModuleNode(
    _In_ PPH_MODULE_LIST_CONTEXT Context,
    _In_ PPH_MODULE_NODE ModuleNode
    )
{
    memset(ModuleNode->TextCache, 0, sizeof(PH_STRINGREF) * PHMOTLC_MAXIMUM);
    PhClearReference(&ModuleNode->TooltipText);

    ModuleNode->ValidMask = 0;
    PhInvalidateTreeNewNode(&ModuleNode->Node, TN_CACHE_COLOR);
    TreeNew_NodesStructured(Context->TreeNewHandle);
}

VOID PhExpandAllModuleNodes(
    _In_ PPH_MODULE_LIST_CONTEXT Context,
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

VOID PhTickModuleNodes(
    _In_ PPH_MODULE_LIST_CONTEXT Context
    )
{
    PH_TICK_SH_STATE_TN(PH_MODULE_NODE, ShState, Context->NodeStateList, PhpRemoveModuleNode, PhCsHighlightingDuration, Context->TreeNewHandle, TRUE, NULL, Context);
}

#define SORT_FUNCTION(Column) PhpModuleTreeNewCompare##Column

#define BEGIN_SORT_FUNCTION(Column) static int __cdecl PhpModuleTreeNewCompare##Column( \
    _In_ void *_context, \
    _In_ const void *_elem1, \
    _In_ const void *_elem2 \
    ) \
{ \
    PPH_MODULE_NODE node1 = *(PPH_MODULE_NODE *)_elem1; \
    PPH_MODULE_NODE node2 = *(PPH_MODULE_NODE *)_elem2; \
    PPH_MODULE_ITEM moduleItem1 = node1->ModuleItem; \
    PPH_MODULE_ITEM moduleItem2 = node2->ModuleItem; \
    int sortResult = 0;

#define END_SORT_FUNCTION \
    if (sortResult == 0) \
        sortResult = uintptrcmp((ULONG_PTR)moduleItem1->BaseAddress, (ULONG_PTR)moduleItem2->BaseAddress); \
    \
    return PhModifySort(sortResult, ((PPH_MODULE_LIST_CONTEXT)_context)->TreeNewSortOrder); \
}

LONG PhpModuleTreeNewPostSortFunction(
    _In_ LONG Result,
    _In_ PVOID Node1,
    _In_ PVOID Node2,
    _In_ PH_SORT_ORDER SortOrder
    )
{
    if (Result == 0)
        Result = uintptrcmp((ULONG_PTR)((PPH_MODULE_NODE)Node1)->ModuleItem->BaseAddress, (ULONG_PTR)((PPH_MODULE_NODE)Node2)->ModuleItem->BaseAddress);

    return PhModifySort(Result, SortOrder);
}

BEGIN_SORT_FUNCTION(TriState)
{
    if (moduleItem1->IsFirst)
    {
        sortResult = -1;
    }
    else if (moduleItem2->IsFirst)
    {
        sortResult = 1;
    }
    else
    {
        sortResult = PhCompareString(moduleItem1->Name, moduleItem2->Name, TRUE); // fall back to sorting by name
    }
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(Name)
{
    sortResult = PhCompareString(moduleItem1->Name, moduleItem2->Name, TRUE);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(BaseAddress)
{
    sortResult = uintptrcmp((ULONG_PTR)moduleItem1->BaseAddress, (ULONG_PTR)moduleItem2->BaseAddress);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(Size)
{
    sortResult = uintcmp(moduleItem1->Size, moduleItem2->Size);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(Description)
{
    sortResult = PhCompareStringWithNull(moduleItem1->VersionInfo.FileDescription, moduleItem2->VersionInfo.FileDescription, TRUE);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(CompanyName)
{
    sortResult = PhCompareStringWithNull(moduleItem1->VersionInfo.CompanyName, moduleItem2->VersionInfo.CompanyName, TRUE);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(Version)
{
    sortResult = PhCompareStringWithNull(moduleItem1->VersionInfo.FileVersion, moduleItem2->VersionInfo.FileVersion, TRUE);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(FileName)
{
    sortResult = PhCompareStringWithNull(moduleItem1->FileName, moduleItem2->FileName, TRUE);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(Type)
{
    sortResult = uintcmp(moduleItem1->Type, moduleItem2->Type);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(LoadCount)
{
    sortResult = uintcmp(moduleItem1->LoadCount, moduleItem2->LoadCount);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(VerificationStatus)
{
    sortResult = intcmp(moduleItem1->VerifyResult, moduleItem2->VerifyResult);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(VerifiedSigner)
{
    sortResult = PhCompareStringWithNull(
        moduleItem1->VerifySignerName,
        moduleItem2->VerifySignerName,
        TRUE
        );
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(Aslr)
{
    sortResult = intcmp(
        moduleItem1->ImageDllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE,
        moduleItem2->ImageDllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE
        );
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(TimeStamp)
{
    sortResult = uintcmp(moduleItem1->ImageTimeDateStamp, moduleItem2->ImageTimeDateStamp);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(CfGuard)
{
    sortResult = intcmp(
        moduleItem1->ImageDllCharacteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF,
        moduleItem2->ImageDllCharacteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF
        );
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(LoadTime)
{
    sortResult = uint64cmp(moduleItem1->LoadTime.QuadPart, moduleItem2->LoadTime.QuadPart);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(LoadReason)
{
    sortResult = uintcmp(moduleItem1->LoadReason, moduleItem2->LoadReason);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(FileModifiedTime)
{
    sortResult = int64cmp(moduleItem1->FileLastWriteTime.QuadPart, moduleItem2->FileLastWriteTime.QuadPart);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(FileSize)
{
    sortResult = int64cmp(moduleItem1->FileEndOfFile.QuadPart, moduleItem2->FileEndOfFile.QuadPart);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(EntryPoint)
{
    sortResult = uintptrcmp((ULONG_PTR)moduleItem1->EntryPoint, (ULONG_PTR)moduleItem2->EntryPoint);
}
END_SORT_FUNCTION

BEGIN_SORT_FUNCTION(ParentBaseAddress)
{
    sortResult = uintptrcmp((ULONG_PTR)moduleItem1->ParentBaseAddress, (ULONG_PTR)moduleItem2->ParentBaseAddress);
}
END_SORT_FUNCTION

BOOLEAN NTAPI PhpModuleTreeNewCallback(
    _In_ HWND hwnd,
    _In_ PH_TREENEW_MESSAGE Message,
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2,
    _In_opt_ PVOID Context
    )
{
    PPH_MODULE_LIST_CONTEXT context;
    PPH_MODULE_NODE node;

    context = Context;

    if (PhCmForwardMessage(hwnd, Message, Parameter1, Parameter2, &context->Cm))
        return TRUE;

    switch (Message)
    {
    case TreeNewGetChildren:
        {
            PPH_TREENEW_GET_CHILDREN getChildren = Parameter1;
            node = (PPH_MODULE_NODE)getChildren->Node;

            if (context->TreeNewSortOrder == NoSortOrder)
            {
                if (!node)
                {
                    getChildren->Children = (PPH_TREENEW_NODE *)context->NodeRootList->Items;
                    getChildren->NumberOfChildren = context->NodeRootList->Count;
                }
                else
                {
                    getChildren->Children = (PPH_TREENEW_NODE *)node->Children->Items;
                    getChildren->NumberOfChildren = node->Children->Count;
                }

                qsort_s(getChildren->Children, getChildren->NumberOfChildren, sizeof(PVOID), SORT_FUNCTION(TriState), context);
            }
            else
            {
                static PVOID sortFunctions[] =
                {
                    SORT_FUNCTION(Name),
                    SORT_FUNCTION(BaseAddress),
                    SORT_FUNCTION(Size),
                    SORT_FUNCTION(Description),
                    SORT_FUNCTION(CompanyName),
                    SORT_FUNCTION(Version),
                    SORT_FUNCTION(FileName),
                    SORT_FUNCTION(Type),
                    SORT_FUNCTION(LoadCount),
                    SORT_FUNCTION(VerificationStatus),
                    SORT_FUNCTION(VerifiedSigner),
                    SORT_FUNCTION(Aslr),
                    SORT_FUNCTION(TimeStamp),
                    SORT_FUNCTION(CfGuard),
                    SORT_FUNCTION(LoadTime),
                    SORT_FUNCTION(LoadReason),
                    SORT_FUNCTION(FileModifiedTime),
                    SORT_FUNCTION(FileSize),
                    SORT_FUNCTION(EntryPoint),
                    SORT_FUNCTION(ParentBaseAddress)
                };
                int (__cdecl *sortFunction)(void *, const void *, const void *);

                if (context->TreeNewSortOrder == NoSortOrder)
                {
                    sortFunction = SORT_FUNCTION(TriState);
                }
                else
                {
                    if (!PhCmForwardSort(
                        (PPH_TREENEW_NODE *)context->NodeList->Items,
                        context->NodeList->Count,
                        context->TreeNewSortColumn,
                        context->TreeNewSortOrder,
                        &context->Cm
                        ))
                    {
                        if (context->TreeNewSortColumn < PHMOTLC_MAXIMUM)
                            sortFunction = sortFunctions[context->TreeNewSortColumn];
                        else
                            sortFunction = NULL;
                    }
                    else
                    {
                        sortFunction = NULL;
                    }
                }

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
            PPH_TREENEW_IS_LEAF isLeaf = Parameter1;
            node = (PPH_MODULE_NODE)isLeaf->Node;

            if (context->TreeNewSortOrder == NoSortOrder)
                isLeaf->IsLeaf = node->Children->Count == 0;
            else
                isLeaf->IsLeaf = TRUE;
        }
        return TRUE;
    case TreeNewGetCellText:
        {
            PPH_TREENEW_GET_CELL_TEXT getCellText = Parameter1;
            PPH_MODULE_ITEM moduleItem;

            node = (PPH_MODULE_NODE)getCellText->Node;
            moduleItem = node->ModuleItem;

            switch (getCellText->Id)
            {
            case PHMOTLC_NAME:
                getCellText->Text = PhGetStringRef(moduleItem->Name);
                break;
            case PHMOTLC_BASEADDRESS:
                PhPrintPointer(moduleItem->BaseAddressString, moduleItem->BaseAddress);
                PhInitializeStringRefLongHint(&getCellText->Text, moduleItem->BaseAddressString);
                break;
            case PHMOTLC_SIZE:
                if (!node->SizeText)
                    node->SizeText = PhFormatSize(moduleItem->Size, ULONG_MAX);
                getCellText->Text = PhGetStringRef(node->SizeText);
                break;
            case PHMOTLC_DESCRIPTION:
                getCellText->Text = PhGetStringRef(moduleItem->VersionInfo.FileDescription);
                break;
            case PHMOTLC_COMPANYNAME:
                getCellText->Text = PhGetStringRef(moduleItem->VersionInfo.CompanyName);
                break;
            case PHMOTLC_VERSION:
                getCellText->Text = PhGetStringRef(moduleItem->VersionInfo.FileVersion);
                break;
            case PHMOTLC_FILENAME:
                getCellText->Text = PhGetStringRef(moduleItem->FileName);
                break;
            case PHMOTLC_TYPE:
                {
                    PWSTR typeString;

                    switch (moduleItem->Type)
                    {
                    case PH_MODULE_TYPE_MODULE:
                        typeString = L"DLL";
                        break;
                    case PH_MODULE_TYPE_MAPPED_FILE:
                        typeString = L"Mapped file";
                        break;
                    case PH_MODULE_TYPE_MAPPED_IMAGE:
                    case PH_MODULE_TYPE_ELF_MAPPED_IMAGE:
                        typeString = L"Mapped image";
                        break;
                    case PH_MODULE_TYPE_WOW64_MODULE:
                        typeString = L"WOW64 DLL";
                        break;
                    case PH_MODULE_TYPE_KERNEL_MODULE:
                        typeString = L"Kernel module";
                        break;
                    default:
                        typeString = L"Unknown";
                        break;
                    }

                    PhInitializeStringRefLongHint(&getCellText->Text, typeString);
                }
                break;
            case PHMOTLC_LOADCOUNT:
                if (moduleItem->Type == PH_MODULE_TYPE_MODULE || moduleItem->Type == PH_MODULE_TYPE_KERNEL_MODULE ||
                    moduleItem->Type == PH_MODULE_TYPE_WOW64_MODULE)
                {
                    if (moduleItem->LoadCount != USHRT_MAX)
                    {
                        PhPrintInt32(node->LoadCountText, moduleItem->LoadCount);
                        PhInitializeStringRefLongHint(&getCellText->Text, node->LoadCountText);
                    }
                    else
                    {
                        PhInitializeStringRef(&getCellText->Text, L"Static");
                    }
                }
                else
                {
                    PhInitializeEmptyStringRef(&getCellText->Text);
                }
                break;
            case PHMOTLC_VERIFICATIONSTATUS:
                {
                    if (moduleItem->Type != PH_MODULE_TYPE_ELF_MAPPED_IMAGE)
                    {
                        PhInitializeStringRef(&getCellText->Text, 
                            moduleItem->VerifyResult == VrTrusted ? L"Trusted" : L"Not trusted");
                    }
                    else
                    {
                        PhInitializeEmptyStringRef(&getCellText->Text);
                    }
                }
                break;
            case PHMOTLC_VERIFIEDSIGNER:
                getCellText->Text = PhGetStringRef(moduleItem->VerifySignerName);
                break;
            case PHMOTLC_ASLR:
                if (moduleItem->ImageDllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE)
                    PhInitializeStringRef(&getCellText->Text, L"ASLR");
                break;
            case PHMOTLC_TIMESTAMP:
                {
                    LARGE_INTEGER time;
                    SYSTEMTIME systemTime;

                    if (moduleItem->ImageTimeDateStamp != 0)
                    {
                        RtlSecondsSince1970ToTime(moduleItem->ImageTimeDateStamp, &time);
                        PhLargeIntegerToLocalSystemTime(&systemTime, &time);
                        PhMoveReference(&node->TimeStampText, PhFormatDateTime(&systemTime));
                        getCellText->Text = node->TimeStampText->sr;
                    }
                    else
                    {
                        PhInitializeEmptyStringRef(&getCellText->Text);
                    }
                }
                break;
            case PHMOTLC_CFGUARD:
                if (moduleItem->ImageDllCharacteristics & IMAGE_DLLCHARACTERISTICS_GUARD_CF)
                    PhInitializeStringRef(&getCellText->Text, L"CF Guard");
                break;
            case PHMOTLC_LOADTIME:
                {
                    SYSTEMTIME systemTime;

                    if (moduleItem->LoadTime.QuadPart != 0)
                    {
                        PhLargeIntegerToLocalSystemTime(&systemTime, &moduleItem->LoadTime);
                        PhMoveReference(&node->LoadTimeText, PhFormatDateTime(&systemTime));
                        getCellText->Text = node->LoadTimeText->sr;
                    }
                    else
                    {
                        PhInitializeEmptyStringRef(&getCellText->Text);
                    }
                }
                break;
            case PHMOTLC_LOADREASON:
                {
                    PWSTR string = L"";

                    if (moduleItem->Type == PH_MODULE_TYPE_KERNEL_MODULE)
                    {
                        string = L"Dynamic";
                    }
                    else if (moduleItem->Type == PH_MODULE_TYPE_MODULE || moduleItem->Type == PH_MODULE_TYPE_WOW64_MODULE)
                    {
                        switch (moduleItem->LoadReason)
                        {
                        case LoadReasonStaticDependency:
                            string = L"Static dependency";
                            break;
                        case LoadReasonStaticForwarderDependency:
                            string = L"Static forwarder dependency";
                            break;
                        case LoadReasonDynamicForwarderDependency:
                            string = L"Dynamic forwarder dependency";
                            break;
                        case LoadReasonDelayloadDependency:
                            string = L"Delay load dependency";
                            break;
                        case LoadReasonDynamicLoad:
                            string = L"Dynamic";
                            break;
                        case LoadReasonAsImageLoad:
                            string = L"As image";
                            break;
                        case LoadReasonAsDataLoad:
                            string = L"As data";
                            break;
                        case LoadReasonEnclavePrimary:
                            string = L"Enclave";
                            break;
                        case LoadReasonEnclaveDependency:
                            string = L"Enclave dependency";
                            break;
                        default:
                            if (WindowsVersion >= WINDOWS_8)
                                string = L"Unknown";
                            else
                                string = L"N/A";
                            break;
                        }
                    }

                    PhInitializeStringRefLongHint(&getCellText->Text, string);
                }
                break;
            case PHMOTLC_FILEMODIFIEDTIME:
                if (moduleItem->FileLastWriteTime.QuadPart != 0)
                {
                    SYSTEMTIME systemTime;

                    PhLargeIntegerToLocalSystemTime(&systemTime, &moduleItem->FileLastWriteTime);
                    PhMoveReference(&node->FileModifiedTimeText, PhFormatDateTime(&systemTime));
                    getCellText->Text = node->FileModifiedTimeText->sr;
                }
                break;
            case PHMOTLC_FILESIZE:
                if (moduleItem->FileEndOfFile.QuadPart != -1)
                {
                    PhMoveReference(&node->FileSizeText, PhFormatSize(moduleItem->FileEndOfFile.QuadPart, ULONG_MAX));
                    getCellText->Text = node->FileSizeText->sr;
                }
                break;
            case PHMOTLC_ENTRYPOINT:
                if (moduleItem->EntryPoint != 0)
                {
                    PhPrintPointer(moduleItem->EntryPointAddressString, moduleItem->EntryPoint);
                    PhInitializeStringRef(&getCellText->Text, moduleItem->EntryPointAddressString);
                }
                break;
            case PHMOTLC_PARENTBASEADDRESS:
                if (moduleItem->ParentBaseAddress != 0)
                {
                    PhPrintPointer(moduleItem->ParentBaseAddressString, moduleItem->ParentBaseAddress);
                    PhInitializeStringRefLongHint(&getCellText->Text, moduleItem->ParentBaseAddressString);
                }
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
            PPH_MODULE_ITEM moduleItem;

            node = (PPH_MODULE_NODE)getNodeColor->Node;
            moduleItem = node->ModuleItem;

            if (!moduleItem)
                ; // Dummy
            else if (PhEnableProcessQueryStage2 &&
                context->HighlightUntrustedModules &&
                moduleItem->VerifyResult != VrTrusted &&
                moduleItem->Type != PH_MODULE_TYPE_ELF_MAPPED_IMAGE
                )
            {
                getNodeColor->BackColor = PhCsColorUnknown;
            }
            else if (context->HighlightDotNetModules && (moduleItem->Flags & LDRP_COR_IMAGE))
                getNodeColor->BackColor = PhCsColorDotNet;
            else if (context->HighlightImmersiveModules && (moduleItem->ImageDllCharacteristics & IMAGE_DLLCHARACTERISTICS_APPCONTAINER))
                getNodeColor->BackColor = PhCsColorImmersiveProcesses;
            else if (context->HighlightRelocatedModules && (moduleItem->Flags & LDRP_IMAGE_NOT_AT_BASE))
                getNodeColor->BackColor = PhCsColorRelocatedModules;
            else if (PhEnableProcessQueryStage2 &&
                context->HighlightSystemModules &&
                moduleItem->VerifyResult == VrTrusted &&
                PhEqualStringRef2(&moduleItem->VerifySignerName->sr, L"Microsoft Windows", TRUE)
                )
            {
                getNodeColor->BackColor = PhCsColorSystemProcesses;
            }

            getNodeColor->Flags = TN_AUTO_FORECOLOR;
        }
        return TRUE;
    case TreeNewGetNodeFont:
        {
            PPH_TREENEW_GET_NODE_FONT getNodeFont = Parameter1;

            node = (PPH_MODULE_NODE)getNodeFont->Node;

            // Make the executable file module item bold.
            if (node->ModuleItem->IsFirst)
            {
                if (!context->BoldFont)
                    context->BoldFont = PhDuplicateFontWithNewWeight(GetWindowFont(hwnd), FW_BOLD);

                getNodeFont->Font = context->BoldFont ? context->BoldFont : NULL;
                getNodeFont->Flags = TN_CACHE;
                return TRUE;
            }
        }
        break;
    case TreeNewGetCellTooltip:
        {
            PPH_TREENEW_GET_CELL_TOOLTIP getCellTooltip = Parameter1;

            node = (PPH_MODULE_NODE)getCellTooltip->Node;

            if (getCellTooltip->Column->Id != 0)
                return FALSE;

            if (!node->TooltipText)
            {
                node->TooltipText = PhFormatImageVersionInfo(
                    node->ModuleItem->FileName,
                    &node->ModuleItem->VersionInfo,
                    NULL,
                    0
                    );

                // Make sure we don't try to create the tooltip text again.
                if (!node->TooltipText)
                    node->TooltipText = PhReferenceEmptyString();
            }

            if (!PhIsNullOrEmptyString(node->TooltipText))
            {
                getCellTooltip->Text = node->TooltipText->sr;
                getCellTooltip->Unfolding = FALSE;
                getCellTooltip->Font = NULL; // use default font
                getCellTooltip->MaximumWidth = 550;
            }
            else
            {
                return FALSE;
            }
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
                    SendMessage(context->ParentWindowHandle, WM_COMMAND, ID_MODULE_COPY, 0);
                break;
            case 'A':
                if (GetKeyState(VK_CONTROL) < 0)
                    TreeNew_SelectRange(context->TreeNewHandle, 0, -1);
                break;
            case VK_DELETE:
                SendMessage(context->ParentWindowHandle, WM_COMMAND, ID_MODULE_UNLOAD, 0);
                break;
            case VK_RETURN:
                if (GetKeyState(VK_CONTROL) >= 0)
                    SendMessage(context->ParentWindowHandle, WM_COMMAND, ID_MODULE_PROPERTIES, 0);
                else
                    SendMessage(context->ParentWindowHandle, WM_COMMAND, ID_MODULE_OPENFILELOCATION, 0);
                break;
            }
        }
        return TRUE;
    case TreeNewHeaderRightClick:
        {
            PH_TN_COLUMN_MENU_DATA data;

            data.TreeNewHandle = hwnd;
            data.MouseEvent = Parameter1;
            data.DefaultSortColumn = 0;
            data.DefaultSortOrder = NoSortOrder;
            PhInitializeTreeNewColumnMenuEx(&data, PH_TN_COLUMN_MENU_SHOW_RESET_SORT);

            data.Selection = PhShowEMenu(data.Menu, hwnd, PH_EMENU_SHOW_LEFTRIGHT,
                PH_ALIGN_LEFT | PH_ALIGN_TOP, data.MouseEvent->ScreenLocation.x, data.MouseEvent->ScreenLocation.y);
            PhHandleTreeNewColumnMenu(&data);
            PhDeleteTreeNewColumnMenu(&data);
        }
        return TRUE;
    case TreeNewLeftDoubleClick:
        {
            SendMessage(context->ParentWindowHandle, WM_COMMAND, ID_MODULE_PROPERTIES, 0);
        }
        return TRUE;
    case TreeNewContextMenu:
        {
            PPH_TREENEW_CONTEXT_MENU contextMenu = Parameter1;

            SendMessage(context->ParentWindowHandle, WM_COMMAND, ID_SHOWCONTEXTMENU, (LPARAM)contextMenu);
        }
        return TRUE;
    case TreeNewGetDialogCode:
        {
            PULONG code = Parameter2;

            if (PtrToUlong(Parameter1) == VK_RETURN)
            {
                *code = DLGC_WANTMESSAGE;
                return TRUE;
            }
        }
        return FALSE;
    }

    return FALSE;
}

PPH_MODULE_ITEM PhGetSelectedModuleItem(
    _In_ PPH_MODULE_LIST_CONTEXT Context
    )
{
    PPH_MODULE_ITEM moduleItem = NULL;
    ULONG i;

    for (i = 0; i < Context->NodeList->Count; i++)
    {
        PPH_MODULE_NODE node = Context->NodeList->Items[i];

        if (node->Node.Selected)
        {
            moduleItem = node->ModuleItem;
            break;
        }
    }

    return moduleItem;
}

VOID PhGetSelectedModuleItems(
    _In_ PPH_MODULE_LIST_CONTEXT Context,
    _Out_ PPH_MODULE_ITEM **Modules,
    _Out_ PULONG NumberOfModules
    )
{
    PH_ARRAY array;
    ULONG i;

    PhInitializeArray(&array, sizeof(PVOID), 2);

    for (i = 0; i < Context->NodeList->Count; i++)
    {
        PPH_MODULE_NODE node = Context->NodeList->Items[i];

        if (node->Node.Selected)
            PhAddItemArray(&array, &node->ModuleItem);
    }

    *NumberOfModules = (ULONG)array.Count;
    *Modules = PhFinalArrayItems(&array);
}

VOID PhDeselectAllModuleNodes(
    _In_ PPH_MODULE_LIST_CONTEXT Context
    )
{
    TreeNew_DeselectRange(Context->TreeNewHandle, 0, -1);
}
