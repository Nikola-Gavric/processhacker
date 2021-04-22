/*
 * Process Hacker Online Checks -
 *   database functions
 *
 * Copyright (C) 2016 dmex
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

#include "onlnchk.h"

PPH_HASHTABLE ProcessObjectDb;
PH_QUEUED_LOCK ProcessObjectDbLock = PH_QUEUED_LOCK_INIT;
PH_STRINGREF ProcessObjectDbHash = PH_STRINGREF_INIT(L"386f3b6b3f6c35346c69346c6b343d69396b6b3468386b683d383d356b3e383e38356b343f69393b683d3b3a39386b3c6b3a3a3e696835696e686f6b38683e6e");
PH_STRINGREF ServiceObjectDbHash = PH_STRINGREF_INIT(L"39666e39663d6e66356e3935666a66626e6e627e6e35623d6a3d6a3d6e6e6e6a6a6a35396a6a3d6e7a357e7e7a35626a663d6e7a3d6a3d6e7a397e3d3d6e6e7e");
PH_STRINGREF NetworkObjectDbHash = PH_STRINGREF_INIT(L"6e61653c676065676b6b7a393d6a66357a396a6e6e66397a35");

BOOLEAN NTAPI ProcessObjectDbEqualFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    )
{
    PPROCESS_DB_OBJECT object1 = *(PPROCESS_DB_OBJECT *)Entry1;
    PPROCESS_DB_OBJECT object2 = *(PPROCESS_DB_OBJECT *)Entry2;

    return PhEqualStringRef(&object1->FileName, &object2->FileName, TRUE);
}

ULONG NTAPI ProcessObjectDbHashFunction(
    _In_ PVOID Entry
    )
{
    PPROCESS_DB_OBJECT object = *(PPROCESS_DB_OBJECT *)Entry;

    return PhHashStringRef(&object->FileName, TRUE);
}

VOID InitializeProcessDb(
    VOID
    )
{
    ProcessObjectDb = PhCreateHashtable(
        sizeof(PPROCESS_DB_OBJECT),
        ProcessObjectDbEqualFunction,
        ProcessObjectDbHashFunction,
        64
        );
}

VOID CleanupProcessDb(
    VOID
    )
{
    PhDereferenceObject(ProcessObjectDb);
}

ULONG GetNumberOfProcessDbObjects(
    VOID
    )
{
    return ProcessObjectDb->Count;
}

VOID LockProcessDb(
    VOID
    )
{
    PhAcquireQueuedLockExclusive(&ProcessObjectDbLock);
}

VOID UnlockProcessDb(
    VOID
    )
{
    PhReleaseQueuedLockExclusive(&ProcessObjectDbLock);
}

PPROCESS_DB_OBJECT FindProcessDbObject(
    _In_ PPH_STRINGREF FileName
    )
{
    PROCESS_DB_OBJECT lookupObject;
    PPROCESS_DB_OBJECT lookupObjectPtr;
    PPROCESS_DB_OBJECT *objectPtr;

    lookupObject.FileName = *FileName;
    lookupObjectPtr = &lookupObject;

    objectPtr = PhFindEntryHashtable(ProcessObjectDb, &lookupObjectPtr);

    if (objectPtr)
        return *objectPtr;
    else
        return NULL;
}

PPROCESS_DB_OBJECT CreateProcessDbObject(
    _In_ PPH_STRING FileName,
    _In_opt_ INT64 Positives,
    _In_opt_ INT64 Total,
    _In_opt_ PPH_STRING Hash
    )
{
    PPROCESS_DB_OBJECT object;
    BOOLEAN added;
    PPROCESS_DB_OBJECT *realObject;

    object = PhAllocate(sizeof(PROCESS_DB_OBJECT));
    memset(object, 0, sizeof(PROCESS_DB_OBJECT));

    PhInitializeStringRefLongHint(&object->FileName, FileName->Buffer);

    PhReferenceObject(FileName);
    object->Positives = Positives;
    object->Hash = FileName;
    object->Result = PhFormatString(L"%lu | %lu", (ULONG)Positives, (ULONG)Total);

    realObject = PhAddEntryHashtableEx(ProcessObjectDb, &object, &added);

    if (added)   
        return object;
    else
        return NULL;
}
