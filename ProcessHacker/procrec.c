/*
 * Process Hacker -
 *   process record properties
 *
 * Copyright (C) 2010 wj32
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

#include <mainwnd.h>
#include <procprv.h>

typedef struct _PROCESS_RECORD_CONTEXT
{
    PPH_PROCESS_RECORD Record;
    HICON FileIcon;
} PROCESS_RECORD_CONTEXT, *PPROCESS_RECORD_CONTEXT;

INT_PTR CALLBACK PhpProcessRecordDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

VOID PhShowProcessRecordDialog(
    _In_ HWND ParentWindowHandle,
    _In_ PPH_PROCESS_RECORD Record
    )
{
    PROCESS_RECORD_CONTEXT context;

    context.Record = Record;

    DialogBoxParam(
        PhInstanceHandle,
        MAKEINTRESOURCE(IDD_PROCRECORD),
        ParentWindowHandle,
        PhpProcessRecordDlgProc,
        (LPARAM)&context
        );
}

PPH_STRING PhpaGetRelativeTimeString(
    _In_ PLARGE_INTEGER Time
    )
{
    LARGE_INTEGER time;
    LARGE_INTEGER currentTime;
    SYSTEMTIME timeFields;
    PPH_STRING timeRelativeString;
    PPH_STRING timeString;

    time = *Time;
    PhQuerySystemTime(&currentTime);
    timeRelativeString = PH_AUTO(PhFormatTimeSpanRelative(currentTime.QuadPart - time.QuadPart));

    PhLargeIntegerToLocalSystemTime(&timeFields, &time);
    timeString = PhaFormatDateTime(&timeFields);

    return PhaFormatString(L"%s ago (%s)", timeRelativeString->Buffer, timeString->Buffer);
}

FORCEINLINE PWSTR PhpGetStringOrNa(
    _In_ _Maybenull_ PPH_STRING String
    )
{
    if (String)
        return String->Buffer;
    else
        return L"N/A";
}

INT_PTR CALLBACK PhpProcessRecordDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PPROCESS_RECORD_CONTEXT context = NULL;

    if (uMsg == WM_INITDIALOG)
    {
        context = (PPROCESS_RECORD_CONTEXT)lParam;
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
            PH_IMAGE_VERSION_INFO versionInfo;
            BOOLEAN versionInfoInitialized;
            PPH_STRING processNameString;
            PPH_PROCESS_ITEM processItem;

            if (!PH_IS_FAKE_PROCESS_ID(context->Record->ProcessId))
            {
                processNameString = PhaFormatString(L"%s (%u)",
                    context->Record->ProcessName->Buffer, HandleToUlong(context->Record->ProcessId));
            }
            else
            {
                processNameString = context->Record->ProcessName;
            }

            PhCenterWindow(hwndDlg, GetParent(hwndDlg));
            PhSetDialogFocus(hwndDlg, GetDlgItem(hwndDlg, IDOK));
            PhSetWindowText(hwndDlg, processNameString->Buffer);

            PhSetDialogItemText(hwndDlg, IDC_PROCESSNAME, processNameString->Buffer);

            if (processItem = PhReferenceProcessItemForRecord(context->Record))
            {
                PPH_PROCESS_ITEM parentProcess;

                if (parentProcess = PhReferenceProcessItemForParent(processItem))
                {
                    CLIENT_ID clientId;

                    clientId.UniqueProcess = parentProcess->ProcessId;
                    clientId.UniqueThread = NULL;

                    PhSetDialogItemText(hwndDlg, IDC_PARENT,
                        PH_AUTO_T(PH_STRING, PhGetClientIdNameEx(&clientId, parentProcess->ProcessName))->Buffer);

                    PhDereferenceObject(parentProcess);
                }
                else
                {
                    PhSetDialogItemText(hwndDlg, IDC_PARENT, PhaFormatString(L"Non-existent process (%u)",
                        HandleToUlong(context->Record->ParentProcessId))->Buffer);
                }

                PhDereferenceObject(processItem);
            }
            else
            {
                PhSetDialogItemText(hwndDlg, IDC_PARENT, PhaFormatString(L"Unknown process (%u)",
                    HandleToUlong(context->Record->ParentProcessId))->Buffer);

                EnableWindow(GetDlgItem(hwndDlg, IDC_PROPERTIES), FALSE);
            }

            memset(&versionInfo, 0, sizeof(PH_IMAGE_VERSION_INFO));
            versionInfoInitialized = FALSE;

            if (context->Record->FileName)
            {
                PhExtractIcon(
                    context->Record->FileName->Buffer,
                    &context->FileIcon,
                    NULL
                    );

                if (PhInitializeImageVersionInfo(&versionInfo, context->Record->FileName->Buffer))
                    versionInfoInitialized = TRUE;
            }

            if (context->FileIcon)
            {
                SendMessage(GetDlgItem(hwndDlg, IDC_FILEICON), STM_SETICON, (WPARAM)context->FileIcon, 0);
            }
            else
            {
                HICON largeIcon;

                PhGetStockApplicationIcon(NULL, &largeIcon);
                SendMessage(GetDlgItem(hwndDlg, IDC_FILEICON), STM_SETICON, (WPARAM)largeIcon, 0);
            }

            SendMessage(GetDlgItem(hwndDlg, IDC_OPENFILENAME), BM_SETIMAGE, IMAGE_ICON,
                (LPARAM)PH_LOAD_SHARED_ICON_SMALL(PhInstanceHandle, MAKEINTRESOURCE(IDI_FOLDER)));
  
            PhSetDialogItemText(hwndDlg, IDC_NAME, PhpGetStringOrNa(versionInfo.FileDescription));
            PhSetDialogItemText(hwndDlg, IDC_COMPANYNAME, PhpGetStringOrNa(versionInfo.CompanyName));
            PhSetDialogItemText(hwndDlg, IDC_VERSION, PhpGetStringOrNa(versionInfo.FileVersion));
            PhSetDialogItemText(hwndDlg, IDC_FILENAME, PhpGetStringOrNa(context->Record->FileName));

            if (versionInfoInitialized)
                PhDeleteImageVersionInfo(&versionInfo);

            if (!context->Record->FileName)
                EnableWindow(GetDlgItem(hwndDlg, IDC_OPENFILENAME), FALSE);

            PhSetDialogItemText(hwndDlg, IDC_CMDLINE, PhpGetStringOrNa(context->Record->CommandLine));

            if (context->Record->CreateTime.QuadPart != 0)
                PhSetDialogItemText(hwndDlg, IDC_STARTED, PhpaGetRelativeTimeString(&context->Record->CreateTime)->Buffer);
            else
                PhSetDialogItemText(hwndDlg, IDC_STARTED, L"N/A");

            if (context->Record->ExitTime.QuadPart != 0)
                PhSetDialogItemText(hwndDlg, IDC_TERMINATED, PhpaGetRelativeTimeString(&context->Record->ExitTime)->Buffer);
            else
                PhSetDialogItemText(hwndDlg, IDC_TERMINATED, L"N/A");

            PhSetDialogItemValue(hwndDlg, IDC_SESSIONID, context->Record->SessionId, FALSE);
        }
        break;
    case WM_DESTROY:
        {
            if (context->FileIcon)
                DestroyIcon(context->FileIcon);
        }
        break;
    case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
            case IDCANCEL:
            case IDOK:
                {
                    EndDialog(hwndDlg, IDOK);
                }
                break;
            case IDC_OPENFILENAME:
                {
                    if (context->Record->FileName)
                    {
                        PhShellExecuteUserString(
                            PhMainWndHandle,
                            L"FileBrowseExecutable",
                            context->Record->FileName->Buffer,
                            FALSE,
                            L"Make sure the Explorer executable file is present."
                            );
                    }
                }
                break;
            case IDC_PROPERTIES:
                {
                    PPH_PROCESS_ITEM processItem;

                    if (processItem = PhReferenceProcessItemForRecord(context->Record))
                    {
                        ProcessHacker_ShowProcessProperties(PhMainWndHandle, processItem);
                        PhDereferenceObject(processItem);
                    }
                    else
                    {
                        PhShowError(hwndDlg, L"The process has already terminated; only the process record is available.");
                    }
                }
                break;
            }
        }
        break;
    }

    return FALSE;
}
