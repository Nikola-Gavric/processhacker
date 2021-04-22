/*
 * Process Hacker -
 *   module provider
 *
 * Copyright (C) 2009-2016 wj32
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
#include <appresolver.h>
#include <modprv.h>

#include <mapimg.h>
#include <kphuser.h>
#include <verify.h>
#include <workqueue.h>

#include <extmgri.h>
#include <procprv.h>
#include <phsettings.h>

typedef struct _PH_MODULE_QUERY_DATA
{
    SLIST_ENTRY ListEntry;
    PPH_MODULE_PROVIDER ModuleProvider;
    PPH_MODULE_ITEM ModuleItem;

    VERIFY_RESULT VerifyResult;
    PPH_STRING VerifySignerName;
    ULONG ImageFlags;
} PH_MODULE_QUERY_DATA, *PPH_MODULE_QUERY_DATA;

VOID NTAPI PhpModuleProviderDeleteProcedure(
    _In_ PVOID Object,
    _In_ ULONG Flags
    );

VOID NTAPI PhpModuleItemDeleteProcedure(
    _In_ PVOID Object,
    _In_ ULONG Flags
    );

BOOLEAN NTAPI PhpModuleHashtableEqualFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    );

ULONG NTAPI PhpModuleHashtableHashFunction(
    _In_ PVOID Entry
    );

PPH_OBJECT_TYPE PhModuleProviderType = NULL;
PPH_OBJECT_TYPE PhModuleItemType = NULL;

PPH_MODULE_PROVIDER PhCreateModuleProvider(
    _In_ HANDLE ProcessId
    )
{
    static PH_INITONCE initOnce = PH_INITONCE_INIT;
    NTSTATUS status;
    PPH_MODULE_PROVIDER moduleProvider;
    PPH_STRING fileName;

    if (PhBeginInitOnce(&initOnce))
    {
        PhModuleProviderType = PhCreateObjectType(L"ModuleProvider", 0, PhpModuleProviderDeleteProcedure);
        PhModuleItemType = PhCreateObjectType(L"ModuleItem", 0, PhpModuleItemDeleteProcedure);
        PhEndInitOnce(&initOnce);
    }

    moduleProvider = PhCreateObject(
        PhEmGetObjectSize(EmModuleProviderType, sizeof(PH_MODULE_PROVIDER)),
        PhModuleProviderType
        );
    memset(moduleProvider, 0, sizeof(PH_MODULE_PROVIDER));

    moduleProvider->ModuleHashtable = PhCreateHashtable(
        sizeof(PPH_MODULE_ITEM),
        PhpModuleHashtableEqualFunction,
        PhpModuleHashtableHashFunction,
        20
        );
    PhInitializeFastLock(&moduleProvider->ModuleHashtableLock);

    PhInitializeCallback(&moduleProvider->ModuleAddedEvent);
    PhInitializeCallback(&moduleProvider->ModuleModifiedEvent);
    PhInitializeCallback(&moduleProvider->ModuleRemovedEvent);
    PhInitializeCallback(&moduleProvider->UpdatedEvent);

    moduleProvider->ProcessId = ProcessId;
    moduleProvider->ProcessHandle = NULL;
    moduleProvider->ProcessFileName = NULL;
    moduleProvider->PackageFullName = NULL;
    moduleProvider->RunStatus = STATUS_SUCCESS;

    // It doesn't matter if we can't get a process handle.

    // Try to get a handle with query information + vm read access.
    if (!NT_SUCCESS(status = PhOpenProcess(
        &moduleProvider->ProcessHandle,
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
        ProcessId
        )))
    {
        // Try to get a handle with query limited information + vm read access.
        status = PhOpenProcess(
            &moduleProvider->ProcessHandle,
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
            ProcessId
            );

        moduleProvider->RunStatus = status;
    }

    if (NT_SUCCESS(PhGetProcessImageFileNameByProcessId(ProcessId, &fileName)))
    {
        PhMoveReference(&moduleProvider->ProcessFileName, PhGetFileName(fileName));
    }

    if (WindowsVersion >= WINDOWS_8 && moduleProvider->ProcessHandle)
    {
        moduleProvider->PackageFullName = PhGetProcessPackageFullName(moduleProvider->ProcessHandle);

        if (WindowsVersion >= WINDOWS_8_1)
        {
            BOOLEAN cfguardEnabled;

            if (NT_SUCCESS(PhGetProcessIsCFGuardEnabled(moduleProvider->ProcessHandle, &cfguardEnabled)))
            {
                moduleProvider->ControlFlowGuardEnabled = cfguardEnabled;
            }
        }
    }

    if (WindowsVersion >= WINDOWS_10 && moduleProvider->ProcessHandle)
    {
        PROCESS_EXTENDED_BASIC_INFORMATION basicInfo;

        if (NT_SUCCESS(PhGetProcessExtendedBasicInformation(moduleProvider->ProcessHandle, &basicInfo)))
        {
            moduleProvider->IsSubsystemProcess = !!basicInfo.IsSubsystemProcess;
        }
    }

    RtlInitializeSListHead(&moduleProvider->QueryListHead);

    PhEmCallObjectOperation(EmModuleProviderType, moduleProvider, EmObjectCreate);

    return moduleProvider;
}

VOID PhpModuleProviderDeleteProcedure(
    _In_ PVOID Object,
    _In_ ULONG Flags
    )
{
    PPH_MODULE_PROVIDER moduleProvider = (PPH_MODULE_PROVIDER)Object;

    PhEmCallObjectOperation(EmModuleProviderType, moduleProvider, EmObjectDelete);

    // Dereference all module items (we referenced them
    // when we added them to the hashtable).
    PhDereferenceAllModuleItems(moduleProvider);

    PhDereferenceObject(moduleProvider->ModuleHashtable);
    PhDeleteFastLock(&moduleProvider->ModuleHashtableLock);
    PhDeleteCallback(&moduleProvider->ModuleAddedEvent);
    PhDeleteCallback(&moduleProvider->ModuleModifiedEvent);
    PhDeleteCallback(&moduleProvider->ModuleRemovedEvent);
    PhDeleteCallback(&moduleProvider->UpdatedEvent);

    // Destroy all queue items.
    {
        PSLIST_ENTRY entry;
        PPH_MODULE_QUERY_DATA data;

        entry = RtlInterlockedFlushSList(&moduleProvider->QueryListHead);

        while (entry)
        {
            data = CONTAINING_RECORD(entry, PH_MODULE_QUERY_DATA, ListEntry);
            entry = entry->Next;

            if (data->VerifySignerName) PhDereferenceObject(data->VerifySignerName);
            PhDereferenceObject(data->ModuleItem);
            PhFree(data);
        }
    }

    if (moduleProvider->ProcessFileName) PhDereferenceObject(moduleProvider->ProcessFileName);
    if (moduleProvider->PackageFullName) PhDereferenceObject(moduleProvider->PackageFullName);
    if (moduleProvider->ProcessHandle) NtClose(moduleProvider->ProcessHandle);
}

PPH_MODULE_ITEM PhCreateModuleItem(
    VOID
    )
{
    PPH_MODULE_ITEM moduleItem;

    moduleItem = PhCreateObject(
        PhEmGetObjectSize(EmModuleItemType, sizeof(PH_MODULE_ITEM)),
        PhModuleItemType
        );
    memset(moduleItem, 0, sizeof(PH_MODULE_ITEM));
    PhEmCallObjectOperation(EmModuleItemType, moduleItem, EmObjectCreate);

    return moduleItem;
}

VOID PhpModuleItemDeleteProcedure(
    _In_ PVOID Object,
    _In_ ULONG Flags
    )
{
    PPH_MODULE_ITEM moduleItem = (PPH_MODULE_ITEM)Object;

    PhEmCallObjectOperation(EmModuleItemType, moduleItem, EmObjectDelete);

    if (moduleItem->Name) PhDereferenceObject(moduleItem->Name);
    if (moduleItem->FileName) PhDereferenceObject(moduleItem->FileName);
    if (moduleItem->VerifySignerName) PhDereferenceObject(moduleItem->VerifySignerName);
    PhDeleteImageVersionInfo(&moduleItem->VersionInfo);
}

BOOLEAN NTAPI PhpModuleHashtableEqualFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    )
{
    return
        (*(PPH_MODULE_ITEM *)Entry1)->BaseAddress ==
        (*(PPH_MODULE_ITEM *)Entry2)->BaseAddress;
}

ULONG NTAPI PhpModuleHashtableHashFunction(
    _In_ PVOID Entry
    )
{
    PVOID baseAddress = (*(PPH_MODULE_ITEM *)Entry)->BaseAddress;

    return PhHashIntPtr((ULONG_PTR)baseAddress);
}

PPH_MODULE_ITEM PhReferenceModuleItem(
    _In_ PPH_MODULE_PROVIDER ModuleProvider,
    _In_ PVOID BaseAddress
    )
{
    PH_MODULE_ITEM lookupModuleItem;
    PPH_MODULE_ITEM lookupModuleItemPtr = &lookupModuleItem;
    PPH_MODULE_ITEM *moduleItemPtr;
    PPH_MODULE_ITEM moduleItem;

    lookupModuleItem.BaseAddress = BaseAddress;

    PhAcquireFastLockShared(&ModuleProvider->ModuleHashtableLock);

    moduleItemPtr = (PPH_MODULE_ITEM *)PhFindEntryHashtable(
        ModuleProvider->ModuleHashtable,
        &lookupModuleItemPtr
        );

    if (moduleItemPtr)
    {
        moduleItem = *moduleItemPtr;
        PhReferenceObject(moduleItem);
    }
    else
    {
        moduleItem = NULL;
    }

    PhReleaseFastLockShared(&ModuleProvider->ModuleHashtableLock);

    return moduleItem;
}

VOID PhDereferenceAllModuleItems(
    _In_ PPH_MODULE_PROVIDER ModuleProvider
    )
{
    ULONG enumerationKey = 0;
    PPH_MODULE_ITEM *moduleItem;

    PhAcquireFastLockExclusive(&ModuleProvider->ModuleHashtableLock);

    while (PhEnumHashtable(ModuleProvider->ModuleHashtable, (PVOID *)&moduleItem, &enumerationKey))
    {
        PhDereferenceObject(*moduleItem);
    }

    PhReleaseFastLockExclusive(&ModuleProvider->ModuleHashtableLock);
}

VOID PhpRemoveModuleItem(
    _In_ PPH_MODULE_PROVIDER ModuleProvider,
    _In_ PPH_MODULE_ITEM ModuleItem
    )
{
    PhRemoveEntryHashtable(ModuleProvider->ModuleHashtable, &ModuleItem);
    PhDereferenceObject(ModuleItem);
}

NTSTATUS PhpModuleQueryWorker(
    _In_ PVOID Parameter
    )
{
    PPH_MODULE_QUERY_DATA data = (PPH_MODULE_QUERY_DATA)Parameter;
    PH_MAPPED_IMAGE mappedImage = { 0 };

    if (PhEnableProcessQueryStage2) // HACK (dmex)
    {
        data->VerifyResult = PhVerifyFileCached(
            data->ModuleItem->FileName,
            data->ModuleProvider->PackageFullName,
            &data->VerifySignerName,
            FALSE
            );
    }

    {
        // HACK HACK HACK (dmex)
        // 3rd party CLR's don't set the LDRP_COR_IMAGE flag so we'll check binaries for a CLR section and set the flag ourselves.
        // This is needed to detect standard .NET images loaded by .NET core, Mono and other CLR runtimes.
        if (NT_SUCCESS(PhLoadMappedImageEx(PhGetString(data->ModuleItem->FileName), NULL, TRUE, &mappedImage)))
        {
            if (mappedImage.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            {
                PIMAGE_OPTIONAL_HEADER32 optionalHeader = (PIMAGE_OPTIONAL_HEADER32)&mappedImage.NtHeaders->OptionalHeader;

                if (optionalHeader->NumberOfRvaAndSizes >= IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR)
                {
                    if (optionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress)
                    {
                        data->ImageFlags |= LDRP_COR_IMAGE;
                    }
                }
            }
            else if (mappedImage.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            {
                PIMAGE_OPTIONAL_HEADER64 optionalHeader = (PIMAGE_OPTIONAL_HEADER64)&mappedImage.NtHeaders->OptionalHeader;

                if (optionalHeader->NumberOfRvaAndSizes >= IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR)
                {
                    if (optionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress)
                    {
                        data->ImageFlags |= LDRP_COR_IMAGE;
                    }
                }
            }

            PhUnloadMappedImage(&mappedImage);
        }
    }

    RtlInterlockedPushEntrySList(&data->ModuleProvider->QueryListHead, &data->ListEntry);

    PhDereferenceObject(data->ModuleProvider);

    return STATUS_SUCCESS;
}

VOID PhpQueueModuleQuery(
    _In_ PPH_MODULE_PROVIDER ModuleProvider,
    _In_ PPH_MODULE_ITEM ModuleItem
    )
{
    PPH_MODULE_QUERY_DATA data;
    PH_WORK_QUEUE_ENVIRONMENT environment;

    data = PhAllocate(sizeof(PH_MODULE_QUERY_DATA));
    memset(data, 0, sizeof(PH_MODULE_QUERY_DATA));
    data->ModuleProvider = ModuleProvider;
    data->ModuleItem = ModuleItem;

    PhReferenceObject(ModuleProvider);
    PhReferenceObject(ModuleItem);

    PhInitializeWorkQueueEnvironment(&environment);
    environment.BasePriority = THREAD_PRIORITY_BELOW_NORMAL;
    environment.IoPriority = IoPriorityLow;
    environment.PagePriority = MEMORY_PRIORITY_LOW;

    PhQueueItemWorkQueueEx(PhGetGlobalWorkQueue(), PhpModuleQueryWorker, data, NULL, &environment);
}

static BOOLEAN NTAPI EnumModulesCallback(
    _In_ PPH_MODULE_INFO Module,
    _In_opt_ PVOID Context
    )
{
    PPH_MODULE_INFO copy;

    copy = PhAllocateCopy(Module, sizeof(PH_MODULE_INFO));
    PhReferenceObject(copy->Name);
    PhReferenceObject(copy->FileName);

    PhAddItemList((PPH_LIST)Context, copy);

    return TRUE;
}

VOID PhModuleProviderUpdate(
    _In_ PVOID Object
    )
{
    PPH_MODULE_PROVIDER moduleProvider = (PPH_MODULE_PROVIDER)Object;
    PPH_LIST modules;
    ULONG i;

    // If we didn't get a handle when we created the provider,
    // abort (unless this is the System process - in that case
    // we don't need a handle).
    if (!moduleProvider->ProcessHandle && moduleProvider->ProcessId != SYSTEM_PROCESS_ID)
        goto UpdateExit;

    modules = PhCreateList(100);

    moduleProvider->RunStatus = PhEnumGenericModules(
        moduleProvider->ProcessId,
        moduleProvider->ProcessHandle,
        PH_ENUM_GENERIC_MAPPED_FILES | PH_ENUM_GENERIC_MAPPED_IMAGES,
        EnumModulesCallback,
        modules
        );

    // Look for removed modules.
    {
        PPH_LIST modulesToRemove = NULL;
        ULONG enumerationKey = 0;
        PPH_MODULE_ITEM *moduleItem;

        while (PhEnumHashtable(moduleProvider->ModuleHashtable, (PVOID *)&moduleItem, &enumerationKey))
        {
            BOOLEAN found = FALSE;

            // Check if the module still exists.
            for (i = 0; i < modules->Count; i++)
            {
                PPH_MODULE_INFO module = modules->Items[i];

                if ((*moduleItem)->BaseAddress == module->BaseAddress &&
                    PhEqualString((*moduleItem)->FileName, module->FileName, TRUE))
                {
                    found = TRUE;
                    break;
                }
            }

            if (!found)
            {
                // Raise the module removed event.
                PhInvokeCallback(&moduleProvider->ModuleRemovedEvent, *moduleItem);

                if (!modulesToRemove)
                    modulesToRemove = PhCreateList(2);

                PhAddItemList(modulesToRemove, *moduleItem);
            }
        }

        if (modulesToRemove)
        {
            PhAcquireFastLockExclusive(&moduleProvider->ModuleHashtableLock);

            for (i = 0; i < modulesToRemove->Count; i++)
            {
                PhpRemoveModuleItem(
                    moduleProvider,
                    (PPH_MODULE_ITEM)modulesToRemove->Items[i]
                    );
            }

            PhReleaseFastLockExclusive(&moduleProvider->ModuleHashtableLock);
            PhDereferenceObject(modulesToRemove);
        }
    }

    // Go through the queued thread query data.
    {
        PSLIST_ENTRY entry;
        PPH_MODULE_QUERY_DATA data;

        entry = RtlInterlockedFlushSList(&moduleProvider->QueryListHead);

        while (entry)
        {
            data = CONTAINING_RECORD(entry, PH_MODULE_QUERY_DATA, ListEntry);
            entry = entry->Next;

            data->ModuleItem->VerifyResult = data->VerifyResult;
            data->ModuleItem->VerifySignerName = data->VerifySignerName;
            data->ModuleItem->Flags |= data->ImageFlags;
            data->ModuleItem->JustProcessed = TRUE;

            PhDereferenceObject(data->ModuleItem);
            PhFree(data);
        }
    }

    // Look for new modules.
    for (i = 0; i < modules->Count; i++)
    {
        PPH_MODULE_INFO module = modules->Items[i];
        PPH_MODULE_ITEM moduleItem;

        moduleItem = PhReferenceModuleItem(moduleProvider, module->BaseAddress);

        if (!moduleItem)
        {
            FILE_NETWORK_OPEN_INFORMATION networkOpenInfo;

            PhReferenceObject(module->Name);
            PhReferenceObject(module->FileName);

            moduleItem = PhCreateModuleItem();
            moduleItem->BaseAddress = module->BaseAddress;
            moduleItem->EntryPoint = module->EntryPoint;
            moduleItem->Size = module->Size;
            moduleItem->Flags = module->Flags;
            moduleItem->Type = module->Type;
            moduleItem->LoadReason = module->LoadReason;
            moduleItem->LoadCount = module->LoadCount;
            moduleItem->LoadTime = module->LoadTime;
            moduleItem->Name = module->Name;
            moduleItem->FileName = module->FileName;
            moduleItem->ParentBaseAddress = module->ParentBaseAddress;

            PhInitializeImageVersionInfo(&moduleItem->VersionInfo, moduleItem->FileName->Buffer);

            if (moduleProvider->IsSubsystemProcess)
            {
                // HACK: Update the module type. (TODO: Move into PhEnumGenericModules) (dmex)
                moduleItem->Type = PH_MODULE_TYPE_ELF_MAPPED_IMAGE;
            }
            else
            {
                // Fix up the load count. If this is not an ordinary DLL or kernel module, set the load count to 0.
                if (moduleItem->Type != PH_MODULE_TYPE_MODULE &&
                    moduleItem->Type != PH_MODULE_TYPE_WOW64_MODULE &&
                    moduleItem->Type != PH_MODULE_TYPE_KERNEL_MODULE)
                {
                    moduleItem->LoadCount = 0;
                }
            }

            if (!moduleProvider->HaveFirst)
            {
                if (WindowsVersion < WINDOWS_10)
                {
                    moduleItem->IsFirst = i == 0;
                    moduleProvider->HaveFirst = TRUE;
                }
                else
                {
                    // Windows loads the PE image first and WSL loads the ELF image last (dmex)
                    if (moduleProvider->ProcessFileName && PhEqualString(moduleProvider->ProcessFileName, moduleItem->FileName, FALSE))
                    {
                        moduleItem->IsFirst = TRUE;
                        moduleProvider->HaveFirst = TRUE;
                    }
                }
            }

            if (moduleItem->Type == PH_MODULE_TYPE_MODULE ||
                moduleItem->Type == PH_MODULE_TYPE_WOW64_MODULE ||
                moduleItem->Type == PH_MODULE_TYPE_MAPPED_IMAGE ||
                moduleItem->Type == PH_MODULE_TYPE_KERNEL_MODULE)
            {
                PH_REMOTE_MAPPED_IMAGE remoteMappedImage;
                PPH_READ_VIRTUAL_MEMORY_CALLBACK readVirtualMemoryCallback;

                if (moduleItem->Type == PH_MODULE_TYPE_KERNEL_MODULE)
                    readVirtualMemoryCallback = KphReadVirtualMemoryUnsafe;
                else
                    readVirtualMemoryCallback = NtReadVirtualMemory;

                // Note:
                // On Windows 7 the LDRP_IMAGE_NOT_AT_BASE flag doesn't appear to be used
                // anymore. Instead we'll check ImageBase in the image headers. We read this in
                // from the process' memory because:
                //
                // 1. It (should be) faster than opening the file and mapping it in, and
                // 2. It contains the correct original image base relocated by ASLR, if present.

                moduleItem->Flags &= ~LDRP_IMAGE_NOT_AT_BASE;

                if (NT_SUCCESS(PhLoadRemoteMappedImageEx(moduleProvider->ProcessHandle, moduleItem->BaseAddress, readVirtualMemoryCallback, &remoteMappedImage)))
                {
                    ULONG_PTR imageBase = 0;
                    ULONG entryPoint = 0;

                    moduleItem->ImageTimeDateStamp = remoteMappedImage.NtHeaders->FileHeader.TimeDateStamp;
                    moduleItem->ImageCharacteristics = remoteMappedImage.NtHeaders->FileHeader.Characteristics;

                    if (remoteMappedImage.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
                    {
                        PIMAGE_OPTIONAL_HEADER32 optionalHeader = (PIMAGE_OPTIONAL_HEADER32)&remoteMappedImage.NtHeaders->OptionalHeader;

                        imageBase = (ULONG_PTR)optionalHeader->ImageBase;
                        entryPoint = optionalHeader->AddressOfEntryPoint;
                        moduleItem->ImageDllCharacteristics = optionalHeader->DllCharacteristics;
                    }
                    else if (remoteMappedImage.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
                    {
                        PIMAGE_OPTIONAL_HEADER64 optionalHeader = (PIMAGE_OPTIONAL_HEADER64)&remoteMappedImage.NtHeaders->OptionalHeader;

                        imageBase = (ULONG_PTR)optionalHeader->ImageBase;
                        entryPoint = optionalHeader->AddressOfEntryPoint;
                        moduleItem->ImageDllCharacteristics = optionalHeader->DllCharacteristics;
                    }

                    if (imageBase != (ULONG_PTR)moduleItem->BaseAddress)
                        moduleItem->Flags |= LDRP_IMAGE_NOT_AT_BASE;

                    if (entryPoint != 0)
                        moduleItem->EntryPoint = PTR_ADD_OFFSET(moduleItem->BaseAddress, entryPoint);

                    PhUnloadRemoteMappedImage(&remoteMappedImage);
                }
            }

            // remove CF Guard flag if CFG mitigation is not enabled for the process
            if (!moduleProvider->ControlFlowGuardEnabled)
                moduleItem->ImageDllCharacteristics &= ~IMAGE_DLLCHARACTERISTICS_GUARD_CF;

            if (NT_SUCCESS(PhQueryFullAttributesFileWin32(moduleItem->FileName->Buffer, &networkOpenInfo)))
            {
                moduleItem->FileLastWriteTime = networkOpenInfo.LastWriteTime;
                moduleItem->FileEndOfFile = networkOpenInfo.EndOfFile;
            }
            else
            {
                moduleItem->FileEndOfFile.QuadPart = -1;
            }

            if (moduleItem->Type != PH_MODULE_TYPE_ELF_MAPPED_IMAGE)
            {
                // See if the file has already been verified; if not, queue for verification.
                moduleItem->VerifyResult = PhVerifyFileCached(moduleItem->FileName, NULL, &moduleItem->VerifySignerName, TRUE);

                //if (moduleItem->VerifyResult == VrUnknown) // (dmex)
                PhpQueueModuleQuery(moduleProvider, moduleItem);
            }

            // Add the module item to the hashtable.
            PhAcquireFastLockExclusive(&moduleProvider->ModuleHashtableLock);
            PhAddEntryHashtable(moduleProvider->ModuleHashtable, &moduleItem);
            PhReleaseFastLockExclusive(&moduleProvider->ModuleHashtableLock);

            // Raise the module added event.
            PhInvokeCallback(&moduleProvider->ModuleAddedEvent, moduleItem);
        }
        else
        {
            BOOLEAN modified = FALSE;

            if (moduleItem->JustProcessed)
                modified = TRUE;

            moduleItem->JustProcessed = FALSE;

            if (moduleItem->LoadCount != module->LoadCount)
            {
                moduleItem->LoadCount = module->LoadCount;
                modified = TRUE;
            }

            if (modified)
                PhInvokeCallback(&moduleProvider->ModuleModifiedEvent, moduleItem);

            PhDereferenceObject(moduleItem);
        }
    }

    if (!moduleProvider->HaveFirst) // Some processes don't have a primary image (e.g. System/Registry/Secure System) (dmex)
        moduleProvider->HaveFirst = TRUE;

    // Free the modules list.

    for (i = 0; i < modules->Count; i++)
    {
        PPH_MODULE_INFO module = modules->Items[i];

        PhDereferenceObject(module->Name);
        PhDereferenceObject(module->FileName);
        PhFree(module);
    }

    PhDereferenceObject(modules);

UpdateExit:
    PhInvokeCallback(&moduleProvider->UpdatedEvent, NULL);
}
