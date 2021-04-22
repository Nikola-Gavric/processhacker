/*
 * Process Hacker Extended Tools -
 *   ETW system information section
 *
 * Copyright (C) 2010-2011 wj32
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

#include "exttools.h"
#include "etwsys.h"

static PPH_SYSINFO_SECTION DiskSection;
static HWND DiskDialog;
static PH_LAYOUT_MANAGER DiskLayoutManager;
static HWND DiskGraphHandle;
static PH_GRAPH_STATE DiskGraphState;
static HWND DiskPanel;

static PPH_SYSINFO_SECTION NetworkSection;
static HWND NetworkDialog;
static PH_LAYOUT_MANAGER NetworkLayoutManager;
static HWND NetworkGraphHandle;
static PH_GRAPH_STATE NetworkGraphState;
static HWND NetworkPanel;

VOID EtEtwSystemInformationInitializing(
    _In_ PPH_PLUGIN_SYSINFO_POINTERS Pointers
    )
{
    PH_SYSINFO_SECTION section;

    memset(&section, 0, sizeof(PH_SYSINFO_SECTION));
    PhInitializeStringRef(&section.Name, L"Disk");
    section.Flags = 0;
    section.Callback = EtpDiskSysInfoSectionCallback;

    DiskSection = Pointers->CreateSection(&section);

    memset(&section, 0, sizeof(PH_SYSINFO_SECTION));
    PhInitializeStringRef(&section.Name, L"Network");
    section.Flags = 0;
    section.Callback = EtpNetworkSysInfoSectionCallback;

    NetworkSection = Pointers->CreateSection(&section);
}

BOOLEAN EtpDiskSysInfoSectionCallback(
    _In_ PPH_SYSINFO_SECTION Section,
    _In_ PH_SYSINFO_SECTION_MESSAGE Message,
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2
    )
{
    switch (Message)
    {
    case SysInfoDestroy:
        {
            if (DiskDialog)
            {
                PhDeleteGraphState(&DiskGraphState);
                DiskDialog = NULL;
            }
        }
        return TRUE;
    case SysInfoTick:
        {
            if (DiskDialog)
            {
                EtpUpdateDiskGraph();
                EtpUpdateDiskPanel();
            }
        }
        return TRUE;
    case SysInfoCreateDialog:
        {
            PPH_SYSINFO_CREATE_DIALOG createDialog = Parameter1;

            createDialog->Instance = PluginInstance->DllBase;
            createDialog->Template = MAKEINTRESOURCE(IDD_SYSINFO_DISK);
            createDialog->DialogProc = EtpDiskDialogProc;
        }
        return TRUE;
    case SysInfoGraphGetDrawInfo:
        {
            PPH_GRAPH_DRAW_INFO drawInfo = Parameter1;

            drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y | PH_GRAPH_LABEL_MAX_Y | PH_GRAPH_USE_LINE_2;
            Section->Parameters->ColorSetupFunction(drawInfo, PhGetIntegerSetting(L"ColorIoReadOther"), PhGetIntegerSetting(L"ColorIoWrite"));
            PhGetDrawInfoGraphBuffers(&Section->GraphState.Buffers, drawInfo, EtDiskReadHistory.Count);

            if (!Section->GraphState.Valid)
            {
                ULONG i;
                FLOAT max = 1024 * 1024; // Minimum scaling of 1 MB

                for (i = 0; i < drawInfo->LineDataCount; i++)
                {
                    FLOAT data1;
                    FLOAT data2;

                    Section->GraphState.Data1[i] = data1 =
                        (FLOAT)PhGetItemCircularBuffer_ULONG(&EtDiskReadHistory, i);
                    Section->GraphState.Data2[i] = data2 =
                        (FLOAT)PhGetItemCircularBuffer_ULONG(&EtDiskWriteHistory, i);

                    if (max < data1 + data2)
                        max = data1 + data2;
                }

                if (max != 0)
                {
                    // Scale the data.

                    PhDivideSinglesBySingle(
                        Section->GraphState.Data1,
                        max,
                        drawInfo->LineDataCount
                        );
                    PhDivideSinglesBySingle(
                        Section->GraphState.Data2,
                        max,
                        drawInfo->LineDataCount
                        );
                }

                drawInfo->LabelYFunction = PhSiSizeLabelYFunction;
                drawInfo->LabelYFunctionParameter = max;

                Section->GraphState.Valid = TRUE;
            }
        }
        return TRUE;
    case SysInfoGraphGetTooltipText:
        {
            PPH_SYSINFO_GRAPH_GET_TOOLTIP_TEXT getTooltipText = Parameter1;
            ULONG64 diskRead;
            ULONG64 diskWrite;

            diskRead = PhGetItemCircularBuffer_ULONG(&EtDiskReadHistory, getTooltipText->Index);
            diskWrite = PhGetItemCircularBuffer_ULONG(&EtDiskWriteHistory, getTooltipText->Index);

            PhMoveReference(&Section->GraphState.TooltipText, PhFormatString(
                L"R: %s\nW: %s%s\n%s",
                PhaFormatSize(diskRead, ULONG_MAX)->Buffer,
                PhaFormatSize(diskWrite, ULONG_MAX)->Buffer,
                PhGetStringOrEmpty(EtpGetMaxDiskString(getTooltipText->Index)),
                ((PPH_STRING)PH_AUTO(PhGetStatisticsTimeString(NULL, getTooltipText->Index)))->Buffer
                ));
            getTooltipText->Text = Section->GraphState.TooltipText->sr;
        }
        return TRUE;
    case SysInfoGraphDrawPanel:
        {
            PPH_SYSINFO_DRAW_PANEL drawPanel = Parameter1;

            drawPanel->Title = PhCreateString(L"Disk");
            drawPanel->SubTitle = PhFormatString(
                L"R: %s\nW: %s",
                PhaFormatSize(EtDiskReadDelta.Delta, ULONG_MAX)->Buffer,
                PhaFormatSize(EtDiskWriteDelta.Delta, ULONG_MAX)->Buffer
                );
        }
        return TRUE;
    }

    return FALSE;
}

INT_PTR CALLBACK EtpDiskDialogProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            PPH_LAYOUT_ITEM graphItem;
            PPH_LAYOUT_ITEM panelItem;

            PhInitializeGraphState(&DiskGraphState);

            DiskDialog = hwndDlg;
            PhInitializeLayoutManager(&DiskLayoutManager, hwndDlg);
            graphItem = PhAddLayoutItem(&DiskLayoutManager, GetDlgItem(hwndDlg, IDC_GRAPH_LAYOUT), NULL, PH_ANCHOR_ALL);
            panelItem = PhAddLayoutItem(&DiskLayoutManager, GetDlgItem(hwndDlg, IDC_PANEL_LAYOUT), NULL, PH_ANCHOR_LEFT | PH_ANCHOR_RIGHT | PH_ANCHOR_BOTTOM);

            SetWindowFont(GetDlgItem(hwndDlg, IDC_TITLE), DiskSection->Parameters->LargeFont, FALSE);

            DiskPanel = CreateDialog(PluginInstance->DllBase, MAKEINTRESOURCE(IDD_SYSINFO_DISKPANEL), hwndDlg, EtpDiskPanelDialogProc);
            ShowWindow(DiskPanel, SW_SHOW);
            PhAddLayoutItemEx(&DiskLayoutManager, DiskPanel, NULL, PH_ANCHOR_LEFT | PH_ANCHOR_RIGHT | PH_ANCHOR_BOTTOM, panelItem->Margin);

            DiskGraphHandle = CreateWindow(
                PH_GRAPH_CLASSNAME,
                NULL,
                WS_VISIBLE | WS_CHILD | WS_BORDER,
                0,
                0,
                3,
                3,
                hwndDlg,
                NULL,
                NULL,
                NULL
                );
            Graph_SetTooltip(DiskGraphHandle, TRUE);

            PhAddLayoutItemEx(&DiskLayoutManager, DiskGraphHandle, NULL, PH_ANCHOR_ALL, graphItem->Margin);

            EtpUpdateDiskGraph();
            EtpUpdateDiskPanel();
        }
        break;
    case WM_DESTROY:
        {
            PhDeleteLayoutManager(&DiskLayoutManager);
        }
        break;
    case WM_SIZE:
        {
            PhLayoutManagerLayout(&DiskLayoutManager);
        }
        break;
    case WM_NOTIFY:
        {
            NMHDR *header = (NMHDR *)lParam;

            if (header->hwndFrom == DiskGraphHandle)
            {
                EtpNotifyDiskGraph(header);
            }
        }
        break;
    }

    return FALSE;
}

INT_PTR CALLBACK EtpDiskPanelDialogProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    return FALSE;
}

VOID EtpNotifyDiskGraph(
    _In_ NMHDR *Header
    )
{
    switch (Header->code)
    {
    case GCN_GETDRAWINFO:
        {
            PPH_GRAPH_GETDRAWINFO getDrawInfo = (PPH_GRAPH_GETDRAWINFO)Header;
            PPH_GRAPH_DRAW_INFO drawInfo = getDrawInfo->DrawInfo;

            drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y | PH_GRAPH_LABEL_MAX_Y | PH_GRAPH_USE_LINE_2;
            DiskSection->Parameters->ColorSetupFunction(drawInfo, PhGetIntegerSetting(L"ColorIoReadOther"), PhGetIntegerSetting(L"ColorIoWrite"));

            PhGraphStateGetDrawInfo(
                &DiskGraphState,
                getDrawInfo,
                EtDiskReadHistory.Count
                );

            if (!DiskGraphState.Valid)
            {
                ULONG i;
                FLOAT max = 1024 * 1024; // Minimum scaling of 1 MB

                for (i = 0; i < drawInfo->LineDataCount; i++)
                {
                    FLOAT data1;
                    FLOAT data2;

                    DiskGraphState.Data1[i] = data1 =
                        (FLOAT)PhGetItemCircularBuffer_ULONG(&EtDiskReadHistory, i);
                    DiskGraphState.Data2[i] = data2 =
                        (FLOAT)PhGetItemCircularBuffer_ULONG(&EtDiskWriteHistory, i);

                    if (max < data1 + data2)
                        max = data1 + data2;
                }

                if (max != 0)
                {
                    // Scale the data.

                    PhDivideSinglesBySingle(
                        DiskGraphState.Data1,
                        max,
                        drawInfo->LineDataCount
                        );
                    PhDivideSinglesBySingle(
                        DiskGraphState.Data2,
                        max,
                        drawInfo->LineDataCount
                        );
                }

                drawInfo->LabelYFunction = PhSiSizeLabelYFunction;
                drawInfo->LabelYFunctionParameter = max;

                DiskGraphState.Valid = TRUE;
            }
        }
        break;
    case GCN_GETTOOLTIPTEXT:
        {
            PPH_GRAPH_GETTOOLTIPTEXT getTooltipText = (PPH_GRAPH_GETTOOLTIPTEXT)Header;

            if (getTooltipText->Index < getTooltipText->TotalCount)
            {
                if (DiskGraphState.TooltipIndex != getTooltipText->Index)
                {
                    ULONG64 diskRead;
                    ULONG64 diskWrite;

                    diskRead = PhGetItemCircularBuffer_ULONG(&EtDiskReadHistory, getTooltipText->Index);
                    diskWrite = PhGetItemCircularBuffer_ULONG(&EtDiskWriteHistory, getTooltipText->Index);

                    PhMoveReference(&DiskGraphState.TooltipText, PhFormatString(
                        L"R: %s\nW: %s%s\n%s",
                        PhaFormatSize(diskRead, ULONG_MAX)->Buffer,
                        PhaFormatSize(diskWrite, ULONG_MAX)->Buffer,
                        PhGetStringOrEmpty(EtpGetMaxDiskString(getTooltipText->Index)),
                        ((PPH_STRING)PH_AUTO(PhGetStatisticsTimeString(NULL, getTooltipText->Index)))->Buffer
                        ));
                }

                getTooltipText->Text = DiskGraphState.TooltipText->sr;
            }
        }
        break;
    case GCN_MOUSEEVENT:
        {
            PPH_GRAPH_MOUSEEVENT mouseEvent = (PPH_GRAPH_MOUSEEVENT)Header;
            PPH_PROCESS_RECORD record;

            record = NULL;

            if (mouseEvent->Message == WM_LBUTTONDBLCLK && mouseEvent->Index < mouseEvent->TotalCount)
            {
                record = EtpReferenceMaxDiskRecord(mouseEvent->Index);
            }

            if (record)
            {
                PhShowProcessRecordDialog(DiskDialog, record);
                PhDereferenceProcessRecord(record);
            }
        }
        break;
    }
}

VOID EtpUpdateDiskGraph(
    VOID
    )
{
    DiskGraphState.Valid = FALSE;
    DiskGraphState.TooltipIndex = ULONG_MAX;
    Graph_MoveGrid(DiskGraphHandle, 1);
    Graph_Draw(DiskGraphHandle);
    Graph_UpdateTooltip(DiskGraphHandle);
    InvalidateRect(DiskGraphHandle, NULL, FALSE);
}

VOID EtpUpdateDiskPanel(
    VOID
    )
{
    PhSetDialogItemText(DiskPanel, IDC_ZREADSDELTA_V, PhaFormatUInt64(EtDiskReadCountDelta.Delta, TRUE)->Buffer);
    PhSetDialogItemText(DiskPanel, IDC_ZREADBYTESDELTA_V, PhaFormatSize(EtDiskReadDelta.Delta, ULONG_MAX)->Buffer);
    PhSetDialogItemText(DiskPanel, IDC_ZWRITESDELTA_V, PhaFormatUInt64(EtDiskWriteCountDelta.Delta, TRUE)->Buffer);
    PhSetDialogItemText(DiskPanel, IDC_ZWRITEBYTESDELTA_V, PhaFormatSize(EtDiskWriteDelta.Delta, ULONG_MAX)->Buffer);
}

PPH_PROCESS_RECORD EtpReferenceMaxDiskRecord(
    _In_ LONG Index
    )
{
    LARGE_INTEGER time;
    ULONG maxProcessId;

    maxProcessId = PhGetItemCircularBuffer_ULONG(&EtMaxDiskHistory, Index);

    if (!maxProcessId)
        return NULL;

    PhGetStatisticsTime(NULL, Index, &time);
    time.QuadPart += PH_TICKS_PER_SEC - 1;

    return PhFindProcessRecord(UlongToHandle(maxProcessId), &time);
}

PPH_STRING EtpGetMaxDiskString(
    _In_ LONG Index
    )
{
    PPH_PROCESS_RECORD maxProcessRecord;
    PPH_STRING maxUsageString = NULL;

    if (maxProcessRecord = EtpReferenceMaxDiskRecord(Index))
    {
        maxUsageString = PhaFormatString(
            L"\n%s (%lu)",
            maxProcessRecord->ProcessName->Buffer,
            HandleToUlong(maxProcessRecord->ProcessId)
            );
        PhDereferenceProcessRecord(maxProcessRecord);
    }

    return maxUsageString;
}

BOOLEAN EtpNetworkSysInfoSectionCallback(
    _In_ PPH_SYSINFO_SECTION Section,
    _In_ PH_SYSINFO_SECTION_MESSAGE Message,
    _In_opt_ PVOID Parameter1,
    _In_opt_ PVOID Parameter2
    )
{
    switch (Message)
    {
    case SysInfoDestroy:
        {
            if (NetworkDialog)
            {
                PhDeleteGraphState(&NetworkGraphState);
                NetworkDialog = NULL;
            }
        }
        return TRUE;
    case SysInfoTick:
        {
            if (NetworkDialog)
            {
                EtpUpdateNetworkGraph();
                EtpUpdateNetworkPanel();
            }
        }
        return TRUE;
    case SysInfoCreateDialog:
        {
            PPH_SYSINFO_CREATE_DIALOG createDialog = Parameter1;

            createDialog->Instance = PluginInstance->DllBase;
            createDialog->Template = MAKEINTRESOURCE(IDD_SYSINFO_NET);
            createDialog->DialogProc = EtpNetworkDialogProc;
        }
        return TRUE;
    case SysInfoGraphGetDrawInfo:
        {
            PPH_GRAPH_DRAW_INFO drawInfo = Parameter1;

            drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y | PH_GRAPH_LABEL_MAX_Y | PH_GRAPH_USE_LINE_2;
            Section->Parameters->ColorSetupFunction(drawInfo, PhGetIntegerSetting(L"ColorIoReadOther"), PhGetIntegerSetting(L"ColorIoWrite"));
            PhGetDrawInfoGraphBuffers(&Section->GraphState.Buffers, drawInfo, EtNetworkReceiveHistory.Count);

            if (!Section->GraphState.Valid)
            {
                ULONG i;
                FLOAT max = 1024 * 1024; // Minimum scaling of 1 MB

                for (i = 0; i < drawInfo->LineDataCount; i++)
                {
                    FLOAT data1;
                    FLOAT data2;

                    Section->GraphState.Data1[i] = data1 =
                        (FLOAT)PhGetItemCircularBuffer_ULONG(&EtNetworkReceiveHistory, i);
                    Section->GraphState.Data2[i] = data2 =
                        (FLOAT)PhGetItemCircularBuffer_ULONG(&EtNetworkSendHistory, i);

                    if (max < data1 + data2)
                        max = data1 + data2;
                }

                if (max != 0)
                {
                    // Scale the data.

                    PhDivideSinglesBySingle(
                        Section->GraphState.Data1,
                        max,
                        drawInfo->LineDataCount
                        );
                    PhDivideSinglesBySingle(
                        Section->GraphState.Data2,
                        max,
                        drawInfo->LineDataCount
                        );
                }

                drawInfo->LabelYFunction = PhSiSizeLabelYFunction;
                drawInfo->LabelYFunctionParameter = max;

                Section->GraphState.Valid = TRUE;
            }
        }
        return TRUE;
    case SysInfoGraphGetTooltipText:
        {
            PPH_SYSINFO_GRAPH_GET_TOOLTIP_TEXT getTooltipText = Parameter1;
            ULONG64 networkReceive;
            ULONG64 networkSend;

            networkReceive = PhGetItemCircularBuffer_ULONG(&EtNetworkReceiveHistory, getTooltipText->Index);
            networkSend = PhGetItemCircularBuffer_ULONG(&EtNetworkSendHistory, getTooltipText->Index);

            PhMoveReference(&Section->GraphState.TooltipText, PhFormatString(
                L"R: %s\nS: %s%s\n%s",
                PhaFormatSize(networkReceive, ULONG_MAX)->Buffer,
                PhaFormatSize(networkSend, ULONG_MAX)->Buffer,
                PhGetStringOrEmpty(EtpGetMaxNetworkString(getTooltipText->Index)),
                ((PPH_STRING)PH_AUTO(PhGetStatisticsTimeString(NULL, getTooltipText->Index)))->Buffer
                ));
            getTooltipText->Text = Section->GraphState.TooltipText->sr;
        }
        return TRUE;
    case SysInfoGraphDrawPanel:
        {
            PPH_SYSINFO_DRAW_PANEL drawPanel = Parameter1;

            drawPanel->Title = PhCreateString(L"Network");
            drawPanel->SubTitle = PhFormatString(
                L"R: %s\nS: %s",
                PhaFormatSize(EtNetworkReceiveDelta.Delta, ULONG_MAX)->Buffer,
                PhaFormatSize(EtNetworkSendDelta.Delta, ULONG_MAX)->Buffer
                );
        }
        return TRUE;
    }

    return FALSE;
}

INT_PTR CALLBACK EtpNetworkDialogProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            PPH_LAYOUT_ITEM graphItem;
            PPH_LAYOUT_ITEM panelItem;

            PhInitializeGraphState(&NetworkGraphState);

            NetworkDialog = hwndDlg;
            PhInitializeLayoutManager(&NetworkLayoutManager, hwndDlg);
            graphItem = PhAddLayoutItem(&NetworkLayoutManager, GetDlgItem(hwndDlg, IDC_GRAPH_LAYOUT), NULL, PH_ANCHOR_ALL);
            panelItem = PhAddLayoutItem(&NetworkLayoutManager, GetDlgItem(hwndDlg, IDC_PANEL_LAYOUT), NULL, PH_ANCHOR_LEFT | PH_ANCHOR_RIGHT | PH_ANCHOR_BOTTOM);

            SetWindowFont(GetDlgItem(hwndDlg, IDC_TITLE), NetworkSection->Parameters->LargeFont, FALSE);

            NetworkPanel = CreateDialog(PluginInstance->DllBase, MAKEINTRESOURCE(IDD_SYSINFO_NETPANEL), hwndDlg, EtpNetworkPanelDialogProc);
            ShowWindow(NetworkPanel, SW_SHOW);
            PhAddLayoutItemEx(&NetworkLayoutManager, NetworkPanel, NULL, PH_ANCHOR_LEFT | PH_ANCHOR_RIGHT | PH_ANCHOR_BOTTOM, panelItem->Margin);

            NetworkGraphHandle = CreateWindow(
                PH_GRAPH_CLASSNAME,
                NULL,
                WS_VISIBLE | WS_CHILD | WS_BORDER,
                0,
                0,
                3,
                3,
                hwndDlg,
                NULL,
                NULL,
                NULL
                );
            Graph_SetTooltip(NetworkGraphHandle, TRUE);

            PhAddLayoutItemEx(&NetworkLayoutManager, NetworkGraphHandle, NULL, PH_ANCHOR_ALL, graphItem->Margin);

            EtpUpdateNetworkGraph();
            EtpUpdateNetworkPanel();
        }
        break;
    case WM_DESTROY:
        {
            PhDeleteLayoutManager(&NetworkLayoutManager);
        }
        break;
    case WM_SIZE:
        {
            PhLayoutManagerLayout(&NetworkLayoutManager);
        }
        break;
    case WM_NOTIFY:
        {
            NMHDR *header = (NMHDR *)lParam;

            if (header->hwndFrom == NetworkGraphHandle)
            {
                EtpNotifyNetworkGraph(header);
            }
        }
        break;
    }

    return FALSE;
}

INT_PTR CALLBACK EtpNetworkPanelDialogProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    return FALSE;
}

VOID EtpNotifyNetworkGraph(
    _In_ NMHDR *Header
    )
{
    switch (Header->code)
    {
    case GCN_GETDRAWINFO:
        {
            PPH_GRAPH_GETDRAWINFO getDrawInfo = (PPH_GRAPH_GETDRAWINFO)Header;
            PPH_GRAPH_DRAW_INFO drawInfo = getDrawInfo->DrawInfo;

            drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y | PH_GRAPH_LABEL_MAX_Y | PH_GRAPH_USE_LINE_2;
            NetworkSection->Parameters->ColorSetupFunction(drawInfo, PhGetIntegerSetting(L"ColorIoReadOther"), PhGetIntegerSetting(L"ColorIoWrite"));

            PhGraphStateGetDrawInfo(
                &NetworkGraphState,
                getDrawInfo,
                EtNetworkReceiveHistory.Count
                );

            if (!NetworkGraphState.Valid)
            {
                ULONG i;
                FLOAT max = 1024 * 1024; // Minimum scaling of 1 MB

                for (i = 0; i < drawInfo->LineDataCount; i++)
                {
                    FLOAT data1;
                    FLOAT data2;

                    NetworkGraphState.Data1[i] = data1 =
                        (FLOAT)PhGetItemCircularBuffer_ULONG(&EtNetworkReceiveHistory, i);
                    NetworkGraphState.Data2[i] = data2 =
                        (FLOAT)PhGetItemCircularBuffer_ULONG(&EtNetworkSendHistory, i);

                    if (max < data1 + data2)
                        max = data1 + data2;
                }

                if (max != 0)
                {
                    // Scale the data.

                    PhDivideSinglesBySingle(
                        NetworkGraphState.Data1,
                        max,
                        drawInfo->LineDataCount
                        );
                    PhDivideSinglesBySingle(
                        NetworkGraphState.Data2,
                        max,
                        drawInfo->LineDataCount
                        );
                }

                drawInfo->LabelYFunction = PhSiSizeLabelYFunction;
                drawInfo->LabelYFunctionParameter = max;

                NetworkGraphState.Valid = TRUE;
            }
        }
        break;
    case GCN_GETTOOLTIPTEXT:
        {
            PPH_GRAPH_GETTOOLTIPTEXT getTooltipText = (PPH_GRAPH_GETTOOLTIPTEXT)Header;

            if (getTooltipText->Index < getTooltipText->TotalCount)
            {
                if (NetworkGraphState.TooltipIndex != getTooltipText->Index)
                {
                    ULONG64 networkReceive;
                    ULONG64 networkSend;

                    networkReceive = PhGetItemCircularBuffer_ULONG(&EtNetworkReceiveHistory, getTooltipText->Index);
                    networkSend = PhGetItemCircularBuffer_ULONG(&EtNetworkSendHistory, getTooltipText->Index);

                    PhMoveReference(&NetworkGraphState.TooltipText, PhFormatString(
                        L"R: %s\nS: %s%s\n%s",
                        PhaFormatSize(networkReceive, ULONG_MAX)->Buffer,
                        PhaFormatSize(networkSend, ULONG_MAX)->Buffer,
                        PhGetStringOrEmpty(EtpGetMaxNetworkString(getTooltipText->Index)),
                        ((PPH_STRING)PH_AUTO(PhGetStatisticsTimeString(NULL, getTooltipText->Index)))->Buffer
                        ));
                }

                getTooltipText->Text = NetworkGraphState.TooltipText->sr;
            }
        }
        break;
    case GCN_MOUSEEVENT:
        {
            PPH_GRAPH_MOUSEEVENT mouseEvent = (PPH_GRAPH_MOUSEEVENT)Header;
            PPH_PROCESS_RECORD record;

            record = NULL;

            if (mouseEvent->Message == WM_LBUTTONDBLCLK && mouseEvent->Index < mouseEvent->TotalCount)
            {
                record = EtpReferenceMaxNetworkRecord(mouseEvent->Index);
            }

            if (record)
            {
                PhShowProcessRecordDialog(NetworkDialog, record);
                PhDereferenceProcessRecord(record);
            }
        }
        break;
    }
}

VOID EtpUpdateNetworkGraph(
    VOID
    )
{
    NetworkGraphState.Valid = FALSE;
    NetworkGraphState.TooltipIndex = ULONG_MAX;
    Graph_MoveGrid(NetworkGraphHandle, 1);
    Graph_Draw(NetworkGraphHandle);
    Graph_UpdateTooltip(NetworkGraphHandle);
    InvalidateRect(NetworkGraphHandle, NULL, FALSE);
}

VOID EtpUpdateNetworkPanel(
    VOID
    )
{
    PhSetDialogItemText(NetworkPanel, IDC_ZRECEIVESDELTA_V, PhaFormatUInt64(EtNetworkReceiveCountDelta.Delta, TRUE)->Buffer);
    PhSetDialogItemText(NetworkPanel, IDC_ZRECEIVEBYTESDELTA_V, PhaFormatSize(EtNetworkReceiveDelta.Delta, ULONG_MAX)->Buffer);
    PhSetDialogItemText(NetworkPanel, IDC_ZSENDSDELTA_V, PhaFormatUInt64(EtNetworkSendCountDelta.Delta, TRUE)->Buffer);
    PhSetDialogItemText(NetworkPanel, IDC_ZSENDBYTESDELTA_V, PhaFormatSize(EtNetworkSendDelta.Delta, ULONG_MAX)->Buffer);
}

PPH_PROCESS_RECORD EtpReferenceMaxNetworkRecord(
    _In_ LONG Index
    )
{
    LARGE_INTEGER time;
    ULONG maxProcessId;

    maxProcessId = PhGetItemCircularBuffer_ULONG(&EtMaxNetworkHistory, Index);

    if (!maxProcessId)
        return NULL;

    PhGetStatisticsTime(NULL, Index, &time);
    time.QuadPart += PH_TICKS_PER_SEC - 1;

    return PhFindProcessRecord(UlongToHandle(maxProcessId), &time);
}

PPH_STRING EtpGetMaxNetworkString(
    _In_ LONG Index
    )
{
    PPH_PROCESS_RECORD maxProcessRecord;
    PPH_STRING maxUsageString = NULL;

    if (maxProcessRecord = EtpReferenceMaxNetworkRecord(Index))
    {
        maxUsageString = PhaFormatString(
            L"\n%s (%lu)",
            maxProcessRecord->ProcessName->Buffer,
            HandleToUlong(maxProcessRecord->ProcessId)
            );
        PhDereferenceProcessRecord(maxProcessRecord);
    }

    return maxUsageString;
}
