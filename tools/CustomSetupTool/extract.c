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
#include "miniz\miniz.h"

BOOLEAN SetupExtractBuild(
    _In_ PPH_SETUP_CONTEXT Context
    )
{
    mz_bool status = MZ_FALSE;
    ULONG64 totalLength = 0;
    ULONG64 currentLength = 0;
    mz_zip_archive zip_archive = { 0 };
    PPH_STRING extractPath = NULL;
    SYSTEM_INFO info;

    GetNativeSystemInfo(&info);

#ifdef PH_BUILD_API
    ULONG resourceLength;
    PVOID resourceBuffer = NULL;
    PVOID zipBuffer = NULL;
    ULONG zipBufferLength = 0;
    PPH_STRING bufferString;

    if (!PhLoadResource(PhInstanceHandle, MAKEINTRESOURCE(IDR_BIN_DATA), RT_RCDATA, &resourceLength, &resourceBuffer))
        return FALSE;

    if (!(bufferString = PhZeroExtendToUtf16Ex(resourceBuffer, resourceLength)))
    {
        Context->ErrorCode = ERROR_PATH_NOT_FOUND;
        goto CleanupExit;
    }

    if (!SetupBase64StringToBufferEx(
        bufferString->Buffer,
        bufferString->Length / sizeof(WCHAR),
        &zipBuffer,
        &zipBufferLength
        ))
    {
        Context->ErrorCode = ERROR_PATH_NOT_FOUND;
        goto CleanupExit;
    }

    if (!(status = mz_zip_reader_init_mem(&zip_archive, zipBuffer, zipBufferLength, 0)))
    {
        Context->ErrorCode = ERROR_PATH_NOT_FOUND;
        goto CleanupExit;
    }

#else
    PPH_BYTES zipPathUtf8;

    if (PhIsNullOrEmptyString(Context->FilePath))
    {
        Context->ErrorCode = ERROR_PATH_NOT_FOUND;
        goto CleanupExit;
    }

    zipPathUtf8 = PhConvertUtf16ToUtf8(PhGetString(Context->FilePath));

    if (!(status = mz_zip_reader_init_file(&zip_archive, zipPathUtf8->Buffer, 0)))
    {
        Context->ErrorCode = ERROR_FILE_CORRUPT;
        goto CleanupExit;
    }

    PhDereferenceObject(zipPathUtf8);
#endif

    // Remove outdated files
    //for (ULONG i = 0; i < ARRAYSIZE(SetupRemoveFiles); i++)
    //    SetupDeleteDirectoryFile(SetupRemoveFiles[i].FileName);

    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip_archive); i++)
    {
        mz_zip_archive_file_stat zipFileStat;
        PPH_STRING fileName;

        if (!mz_zip_reader_file_stat(&zip_archive, i, &zipFileStat))
            continue;

        fileName = PhConvertUtf8ToUtf16(zipFileStat.m_filename);

        if (PhFindStringInString(fileName, 0, L"ProcessHacker.exe.settings.xml") != -1)
            continue;
        if (PhFindStringInString(fileName, 0, L"usernotesdb.xml") != -1)
            continue;

        if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
        {
            if (PhStartsWithString2(fileName, L"32bit\\", TRUE) ||
                PhStartsWithString2(fileName, L"x32\\", TRUE))
                continue;
        }
        else
        {
            if (PhStartsWithString2(fileName, L"64bit\\", TRUE) ||
                PhStartsWithString2(fileName, L"x64\\", TRUE))
                continue;
        }

        totalLength += zipFileStat.m_uncomp_size;
    }

    //SendMessage(Context->ExtractPageHandle, WM_START_SETUP, 0, 0);
    //SendMessage(context->ProgressHandle, PBM_SETRANGE32, 0, (LPARAM)ExtractTotalLength);
    SendMessage(Context->DialogHandle, TDM_SET_MARQUEE_PROGRESS_BAR, FALSE, 0);

    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip_archive); i++)
    {
        IO_STATUS_BLOCK isb;
        HANDLE fileHandle = NULL;
        PVOID buffer = NULL;
        ULONG bufferLength = 0;
        PPH_STRING fileName = NULL;
        PPH_STRING fullSetupPath = NULL;
        mz_ulong zipFileCrc32 = 0;
        ULONG indexOfFileName = ULONG_MAX;
        mz_zip_archive_file_stat zipFileStat;

        if (!mz_zip_reader_file_stat(&zip_archive, i, &zipFileStat))
            continue;

        fileName = PhConvertUtf8ToUtf16(zipFileStat.m_filename);

        if (PhFindStringInString(fileName, 0, L"ProcessHacker.exe.settings.xml") != -1)
            continue;
        if (PhFindStringInString(fileName, 0, L"usernotesdb.xml") != -1)
            continue;

        if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
        {
            if (PhStartsWithString2(fileName, L"32bit\\", TRUE) ||
                PhStartsWithString2(fileName, L"x32\\", TRUE))
                continue;

            if (PhStartsWithString2(fileName, L"64bit\\", TRUE))
                PhMoveReference(&fileName, PhSubstring(fileName, 6, (fileName->Length / sizeof(WCHAR)) - 6));
            if (PhStartsWithString2(fileName, L"x64\\", TRUE))
                PhMoveReference(&fileName, PhSubstring(fileName, 4, (fileName->Length / sizeof(WCHAR)) - 4));
        }
        else
        {
            if (PhStartsWithString2(fileName, L"64bit\\", TRUE) ||
                PhStartsWithString2(fileName, L"x64\\", TRUE))
                continue;

            if (PhStartsWithString2(fileName, L"32bit\\", TRUE))
                PhMoveReference(&fileName, PhSubstring(fileName, 6, (fileName->Length / sizeof(WCHAR)) - 6));
            if (PhStartsWithString2(fileName, L"x32\\", TRUE))
                PhMoveReference(&fileName, PhSubstring(fileName, 4, (fileName->Length / sizeof(WCHAR)) - 4));
        }

        if (!(buffer = mz_zip_reader_extract_to_heap(
            &zip_archive,
            zipFileStat.m_file_index,
            &bufferLength,
            0
            )))
        {
            Context->ErrorCode = ERROR_FILE_INVALID;
            goto CleanupExit;
        }

        if ((zipFileCrc32 = mz_crc32(zipFileCrc32, buffer, bufferLength)) != zipFileStat.m_crc32)
        {
            Context->ErrorCode = ERROR_CRC;
            goto CleanupExit;
        }

        extractPath = PhConcatStrings(
            3, 
            PhGetString(Context->SetupInstallPath),
            L"\\", 
            PhGetString(fileName)
            );

        if (fullSetupPath = PhGetFullPath(extractPath->Buffer, &indexOfFileName))
        {
            PPH_STRING directoryPath;

            if (indexOfFileName == ULONG_MAX)
            {
                Context->ErrorCode = ERROR_FILE_CORRUPT;
                goto CleanupExit;
            }

            if (directoryPath = PhSubstring(fullSetupPath, 0, indexOfFileName))
            {
                if (!NT_SUCCESS(PhCreateDirectory(directoryPath)))
                {
                    Context->ErrorCode = ERROR_DIRECTORY_NOT_SUPPORTED;
                    PhDereferenceObject(directoryPath);
                    PhDereferenceObject(fullSetupPath);
                    goto CleanupExit;
                }

                PhDereferenceObject(directoryPath);
            }

            PhDereferenceObject(fullSetupPath);
        }

        // TODO: Backup file and restore if the setup fails.
        //{
        //    PPH_STRING backupFilePath;
        //
        //    backupFilePath = PhConcatStrings(
        //        4, 
        //        PhGetString(Context->SetupInstallPath), 
        //        L"\\", 
        //        PhGetString(fileName),
        //        L".bak"
        //        );
        //
        //    MoveFile(extractPath->Buffer, backupFilePath->Buffer);
        //
        //    PhDereferenceObject(backupFilePath);
        //}

        if (!NT_SUCCESS(PhCreateFileWin32(
            &fileHandle,
            PhGetString(extractPath),
            FILE_GENERIC_READ | FILE_GENERIC_WRITE,
            FILE_ATTRIBUTE_NORMAL,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            FILE_OVERWRITE_IF,
            FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT
            )))
        {
            Context->ErrorCode = ERROR_FILE_CORRUPT;
            goto CleanupExit;
        }

        if (!NT_SUCCESS(NtWriteFile(
            fileHandle,
            NULL,
            NULL,
            NULL,
            &isb,
            buffer,
            bufferLength,
            NULL,
            NULL
            )))
        {
            Context->ErrorCode = ERROR_FILE_CORRUPT;
            goto CleanupExit;
        }

        if (isb.Information != bufferLength)
        {
            Context->ErrorCode = ERROR_FILE_CORRUPT;
            goto CleanupExit;
        }

        currentLength += bufferLength;

        {
            FLOAT percent = ((FLOAT)((double)currentLength / (double)totalLength) * 100);
            PH_FORMAT format[7];
            WCHAR string[MAX_PATH];
            PPH_STRING baseName = PhGetBaseName(extractPath);

            PhInitFormatS(&format[0], L"Extracting: ");
            PhInitFormatS(&format[1], PhGetStringOrEmpty(baseName));

            if (PhFormatToBuffer(format, 2, string, sizeof(string), NULL))
            {
                SendMessage(Context->DialogHandle, TDM_UPDATE_ELEMENT_TEXT, TDE_MAIN_INSTRUCTION, (LPARAM)string);
            }

            PhInitFormatS(&format[0], L"Progress: ");
            PhInitFormatSize(&format[1], currentLength);
            PhInitFormatS(&format[2], L" of ");
            PhInitFormatSize(&format[3], totalLength);
            PhInitFormatS(&format[4], L" (");
            PhInitFormatF(&format[5], percent, 1);
            PhInitFormatS(&format[6], L"%)");

            if (PhFormatToBuffer(format, RTL_NUMBER_OF(format), string, sizeof(string), NULL))
            {
                SendMessage(Context->DialogHandle, TDM_UPDATE_ELEMENT_TEXT, TDE_CONTENT, (LPARAM)string);
            }

            SendMessage(Context->DialogHandle, TDM_SET_PROGRESS_BAR_POS, (WPARAM)percent, 0);

            if (baseName)
                PhDereferenceObject(baseName);
        }

        if (fileHandle)
            NtClose(fileHandle);

        mz_free(buffer);
    }

    mz_zip_reader_end(&zip_archive);

#ifdef PH_BUILD_API
    if (zipBuffer)
        PhFree(zipBuffer);
    if (resourceBuffer)
        PhFree(resourceBuffer);
    if (bufferString)
        PhDereferenceObject(bufferString);
#endif
    if (extractPath)
        PhDereferenceObject(extractPath);

    return TRUE;

CleanupExit:

    mz_zip_reader_end(&zip_archive);

#ifdef PH_BUILD_API
    if (zipBuffer)
        PhFree(zipBuffer);
    if (resourceBuffer)
        PhFree(resourceBuffer);
    if (bufferString)
        PhDereferenceObject(bufferString);
#endif
    if (extractPath)
        PhDereferenceObject(extractPath);

    return FALSE;
}
