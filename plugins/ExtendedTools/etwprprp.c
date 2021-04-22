/*
 * Process Hacker Extended Tools -
 *   ETW process properties page
 *
 * Copyright (C) 2010-2011 wj32
 * Copyright (C) 2015 dmex
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

static RECT NormalGraphTextMargin = { 5, 5, 5, 5 };
static RECT NormalGraphTextPadding = { 3, 3, 3, 3 };

typedef struct _ET_DISKNET_CONTEXT
{
    HWND WindowHandle;
    PET_PROCESS_BLOCK Block;
    PH_CALLBACK_REGISTRATION ProcessesUpdatedRegistration;
    BOOLEAN Enabled;

    PH_LAYOUT_MANAGER LayoutManager;

    HWND DiskGroupBox;
    HWND NetworkGroupBox;

    HWND DiskGraphHandle;
    HWND NetworkGraphHandle;
    HWND PanelHandle;

    PH_GRAPH_STATE DiskGraphState;
    PH_GRAPH_STATE NetworkGraphState;
} ET_DISKNET_CONTEXT, *PET_DISKNET_CONTEXT;

INT_PTR CALLBACK EtwDiskNetworkPanelDialogProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    return FALSE;
}

VOID EtwDiskNetworkCreateGraphs(
    _In_ PET_DISKNET_CONTEXT Context
    )
{
    Context->DiskGraphHandle = CreateWindow(
        PH_GRAPH_CLASSNAME,
        NULL,
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS,
        0,
        0,
        3,
        3,
        Context->WindowHandle,
        NULL,
        NULL,
        NULL
        );
    Graph_SetTooltip(Context->DiskGraphHandle, TRUE);

    Context->NetworkGraphHandle = CreateWindow(
        PH_GRAPH_CLASSNAME,
        NULL,
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_CLIPSIBLINGS,
        0,
        0,
        3,
        3,
        Context->WindowHandle,
        NULL,
        NULL,
        NULL
        );
    Graph_SetTooltip(Context->NetworkGraphHandle, TRUE);
}

VOID EtwDiskNetworkCreatePanel(
    _In_ PET_DISKNET_CONTEXT Context
    )
{
    RECT margin;

    Context->PanelHandle = CreateDialogParam(
        PluginInstance->DllBase,
        MAKEINTRESOURCE(IDD_PROCDISKNET_PANEL),
        Context->WindowHandle,
        EtwDiskNetworkPanelDialogProc,
        (LPARAM)Context
        );

    SetWindowPos(
        Context->PanelHandle,
        NULL,
        10, 0, 0, 0,
        SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOSIZE | SWP_NOZORDER
        );

    ShowWindow(Context->PanelHandle, SW_SHOW);

    margin.left = 0;
    margin.top = 0;
    margin.right = 0;
    margin.bottom = 10;
    MapDialogRect(Context->WindowHandle, &margin);

    PhAddLayoutItemEx(
        &Context->LayoutManager,
        Context->PanelHandle,
        NULL,
        PH_ANCHOR_BOTTOM | PH_ANCHOR_LEFT,
        margin
        );

    SendMessage(Context->WindowHandle, WM_SIZE, 0, 0);
}

VOID EtwDiskNetworkLayoutGraphs(
    _In_ PET_DISKNET_CONTEXT Context
    )
{
    HDWP deferHandle;
    RECT clientRect;
    RECT panelRect;
    RECT margin = { PH_SCALE_DPI(13), PH_SCALE_DPI(13), PH_SCALE_DPI(13), PH_SCALE_DPI(13) };
    RECT innerMargin = { PH_SCALE_DPI(10), PH_SCALE_DPI(20), PH_SCALE_DPI(10), PH_SCALE_DPI(10) };
    LONG between = PH_SCALE_DPI(3);
    ULONG graphWidth;
    ULONG graphHeight;

    PhLayoutManagerLayout(&Context->LayoutManager);

    Context->DiskGraphState.Valid = FALSE;
    Context->NetworkGraphState.Valid = FALSE;

    GetClientRect(Context->WindowHandle, &clientRect);

    // Limit the rectangle bottom to the top of the panel.
    GetWindowRect(Context->PanelHandle, &panelRect);
    MapWindowPoints(NULL, Context->WindowHandle, (PPOINT)&panelRect, 2);
    clientRect.bottom = panelRect.top + 10; // +10 removing extra spacing

    graphWidth = clientRect.right - margin.left - margin.right;
    graphHeight = (clientRect.bottom - margin.top - margin.bottom - between * 2) / 2;

    deferHandle = BeginDeferWindowPos(4);

    deferHandle = DeferWindowPos(deferHandle, Context->DiskGroupBox, NULL, margin.left, margin.top, graphWidth, graphHeight, SWP_NOACTIVATE | SWP_NOZORDER);
    deferHandle = DeferWindowPos(
        deferHandle,
        Context->DiskGraphHandle,
        NULL,
        margin.left + innerMargin.left,
        margin.top + innerMargin.top,
        graphWidth - innerMargin.left - innerMargin.right,
        graphHeight - innerMargin.top - innerMargin.bottom,
        SWP_NOACTIVATE | SWP_NOZORDER
        );

    deferHandle = DeferWindowPos(deferHandle, Context->NetworkGroupBox, NULL, margin.left, margin.top + graphHeight + between, graphWidth, graphHeight, SWP_NOACTIVATE | SWP_NOZORDER);
    deferHandle = DeferWindowPos(
        deferHandle,
        Context->NetworkGraphHandle,
        NULL,
        margin.left + innerMargin.left,
        margin.top + graphHeight + between + innerMargin.top,
        graphWidth - innerMargin.left - innerMargin.right,
        graphHeight - innerMargin.top - innerMargin.bottom,
        SWP_NOACTIVATE | SWP_NOZORDER
        );

    EndDeferWindowPos(deferHandle);
}

VOID EtwDiskNetworkUpdateGraphs(
    _In_ PET_DISKNET_CONTEXT Context
    )
{
    Context->DiskGraphState.Valid = FALSE;
    Context->DiskGraphState.TooltipIndex = ULONG_MAX;
    Graph_MoveGrid(Context->DiskGraphHandle, 1);
    Graph_Draw(Context->DiskGraphHandle);
    Graph_UpdateTooltip(Context->DiskGraphHandle);
    InvalidateRect(Context->DiskGraphHandle, NULL, FALSE);

    Context->NetworkGraphState.Valid = FALSE;
    Context->NetworkGraphState.TooltipIndex = ULONG_MAX;
    Graph_MoveGrid(Context->NetworkGraphHandle, 1);
    Graph_Draw(Context->NetworkGraphHandle);
    Graph_UpdateTooltip(Context->NetworkGraphHandle);
    InvalidateRect(Context->NetworkGraphHandle, NULL, FALSE);
}

VOID EtwDiskNetworkUpdatePanel(
    _Inout_ PET_DISKNET_CONTEXT Context
    )
{
    PET_PROCESS_BLOCK block = Context->Block;

    PhSetDialogItemText(Context->PanelHandle, IDC_ZREADS_V, PhaFormatUInt64(block->DiskReadCount, TRUE)->Buffer);
    PhSetDialogItemText(Context->PanelHandle, IDC_ZREADBYTES_V, PhaFormatSize(block->DiskReadRawDelta.Value, ULONG_MAX)->Buffer);
    PhSetDialogItemText(Context->PanelHandle, IDC_ZREADBYTESDELTA_V, PhaFormatSize(block->DiskReadRawDelta.Delta, ULONG_MAX)->Buffer);
    PhSetDialogItemText(Context->PanelHandle, IDC_ZWRITES_V, PhaFormatUInt64(block->DiskWriteCount, TRUE)->Buffer);
    PhSetDialogItemText(Context->PanelHandle, IDC_ZWRITEBYTES_V, PhaFormatSize(block->DiskWriteRawDelta.Value, ULONG_MAX)->Buffer);
    PhSetDialogItemText(Context->PanelHandle, IDC_ZWRITEBYTESDELTA_V, PhaFormatSize(block->DiskWriteRawDelta.Delta, ULONG_MAX)->Buffer);

    PhSetDialogItemText(Context->PanelHandle, IDC_ZRECEIVES_V, PhaFormatUInt64(block->NetworkReceiveCount, TRUE)->Buffer);
    PhSetDialogItemText(Context->PanelHandle, IDC_ZRECEIVEBYTES_V, PhaFormatSize(block->NetworkReceiveRawDelta.Value, ULONG_MAX)->Buffer);
    PhSetDialogItemText(Context->PanelHandle, IDC_ZRECEIVEBYTESDELTA_V, PhaFormatSize(block->NetworkReceiveRawDelta.Delta, ULONG_MAX)->Buffer);
    PhSetDialogItemText(Context->PanelHandle, IDC_ZSENDS_V, PhaFormatUInt64(block->NetworkSendCount, TRUE)->Buffer);
    PhSetDialogItemText(Context->PanelHandle, IDC_ZSENDBYTES_V, PhaFormatSize(block->NetworkSendRawDelta.Value, ULONG_MAX)->Buffer);
    PhSetDialogItemText(Context->PanelHandle, IDC_ZSENDBYTESDELTA_V, PhaFormatSize(block->NetworkSendRawDelta.Delta, ULONG_MAX)->Buffer);
}

VOID NTAPI EtwDiskNetworkUpdateHandler(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PET_DISKNET_CONTEXT context = Context;

    if (context && context->WindowHandle && context->Enabled)
    {
        PostMessage(context->WindowHandle, ET_WM_UPDATE, 0, 0);
    }
}

INT_PTR CALLBACK EtwDiskNetworkPageDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;
    PPH_PROCESS_ITEM processItem;
    PET_DISKNET_CONTEXT context;

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
            // We have already set the group boxes to have WS_EX_TRANSPARENT to fix
            // the drawing issue that arises when using WS_CLIPCHILDREN. However
            // in removing the flicker from the graphs the group boxes will now flicker.
            // It's a good tradeoff since no one stares at the group boxes.
            PhSetWindowStyle(hwndDlg, WS_CLIPCHILDREN, WS_CLIPCHILDREN);

            context = PhAllocateZero(sizeof(ET_DISKNET_CONTEXT));
            context->WindowHandle = hwndDlg;
            context->Block = EtGetProcessBlock(processItem);
            context->Enabled = TRUE;
            context->DiskGroupBox = GetDlgItem(hwndDlg, IDC_GROUPDISK);
            context->NetworkGroupBox = GetDlgItem(hwndDlg, IDC_GROUPNETWORK);
            propPageContext->Context = context;

            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);

            PhInitializeGraphState(&context->DiskGraphState);
            PhInitializeGraphState(&context->NetworkGraphState);

            EtwDiskNetworkCreateGraphs(context);
            EtwDiskNetworkCreatePanel(context);
            EtwDiskNetworkUpdatePanel(context);

            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackProcessProviderUpdatedEvent),
                EtwDiskNetworkUpdateHandler,
                context,
                &context->ProcessesUpdatedRegistration
                );

            PhInitializeWindowTheme(hwndDlg, !!PhGetIntegerSetting(L"EnableThemeSupport"));
        }
        break;
    case WM_DESTROY:
        {
            PhDeleteLayoutManager(&context->LayoutManager);

            PhDeleteGraphState(&context->DiskGraphState);
            PhDeleteGraphState(&context->NetworkGraphState);

            if (context->DiskGraphHandle)
                DestroyWindow(context->DiskGraphHandle);
            if (context->NetworkGraphHandle)
                DestroyWindow(context->NetworkGraphHandle);
            if (context->PanelHandle)
                DestroyWindow(context->PanelHandle);

            PhUnregisterCallback(PhGetGeneralCallback(GeneralCallbackProcessProviderUpdatedEvent), &context->ProcessesUpdatedRegistration);
            PhFree(context);
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (PhBeginPropPageLayout(hwndDlg, propPageContext))
                PhEndPropPageLayout(hwndDlg, propPageContext);
        }
        break;
    case WM_NOTIFY:
        {
            LPNMHDR header = (LPNMHDR)lParam;

            switch (header->code)
            {
            case PSN_SETACTIVE:
                context->Enabled = TRUE;
                break;
            case PSN_KILLACTIVE:
                context->Enabled = FALSE;
                break;
            case GCN_GETDRAWINFO:
                {
                    PPH_GRAPH_GETDRAWINFO getDrawInfo = (PPH_GRAPH_GETDRAWINFO)header;
                    PPH_GRAPH_DRAW_INFO drawInfo = getDrawInfo->DrawInfo;

                    if (header->hwndFrom == context->DiskGraphHandle)
                    {
                        if (PhGetIntegerSetting(L"GraphShowText"))
                        {
                            HDC hdc;

                            PhMoveReference(&context->DiskGraphState.Text, PhFormatString(
                                L"R: %s, W: %s",
                                PhaFormatSize(context->Block->CurrentDiskRead, ULONG_MAX)->Buffer,
                                PhaFormatSize(context->Block->CurrentDiskWrite, ULONG_MAX)->Buffer
                                ));

                            hdc = Graph_GetBufferedContext(context->DiskGraphHandle);
                            SelectFont(hdc, PhApplicationFont);
                            PhSetGraphText(hdc, drawInfo, &context->DiskGraphState.Text->sr,
                                &NormalGraphTextMargin, &NormalGraphTextPadding, PH_ALIGN_TOP | PH_ALIGN_LEFT);
                        }
                        else
                        {
                            drawInfo->Text.Buffer = NULL;
                        }

                        drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y | PH_GRAPH_LABEL_MAX_Y | PH_GRAPH_USE_LINE_2;
                        PhSiSetColorsGraphDrawInfo(drawInfo, PhGetIntegerSetting(L"ColorIoReadOther"), PhGetIntegerSetting(L"ColorIoWrite"));
                        PhGraphStateGetDrawInfo(&context->DiskGraphState, getDrawInfo, context->Block->DiskReadHistory.Count);

                        if (!context->DiskGraphState.Valid)
                        {
                            FLOAT max = 0;

                            for (ULONG i = 0; i < drawInfo->LineDataCount; i++)
                            {
                                FLOAT data1;
                                FLOAT data2;

                                context->DiskGraphState.Data1[i] = data1 = (FLOAT)PhGetItemCircularBuffer_ULONG64(&context->Block->DiskReadHistory, i);
                                context->DiskGraphState.Data2[i] = data2 = (FLOAT)PhGetItemCircularBuffer_ULONG64(&context->Block->DiskWriteHistory, i);

                                if (max < data1 + data2)
                                    max = data1 + data2;
                            }

                            // Minimum scaling of 1 MB.
                            //if (max < 1024 * 1024)
                            //    max = 1024 * 1024;

                            if (max != 0)
                            {
                                // Scale the data.

                                PhDivideSinglesBySingle(
                                    context->DiskGraphState.Data1,
                                    max,
                                    drawInfo->LineDataCount
                                    );
                                PhDivideSinglesBySingle(
                                    context->DiskGraphState.Data2,
                                    max,
                                    drawInfo->LineDataCount
                                    );
                            }

                            drawInfo->LabelYFunction = PhSiSizeLabelYFunction;
                            drawInfo->LabelYFunctionParameter = max;

                            context->DiskGraphState.Valid = TRUE;
                        }
                    }
                    else if (header->hwndFrom == context->NetworkGraphHandle)
                    {
                        if (PhGetIntegerSetting(L"GraphShowText"))
                        {
                            HDC hdc;

                            PhMoveReference(&context->NetworkGraphState.Text, PhFormatString(
                                L"R: %s, S: %s",
                                PhaFormatSize(context->Block->CurrentNetworkReceive, ULONG_MAX)->Buffer,
                                PhaFormatSize(context->Block->CurrentNetworkSend, ULONG_MAX)->Buffer
                                ));

                            hdc = Graph_GetBufferedContext(context->NetworkGraphHandle);
                            SelectFont(hdc, PhApplicationFont);
                            PhSetGraphText(hdc, drawInfo, &context->NetworkGraphState.Text->sr,
                                &NormalGraphTextMargin, &NormalGraphTextPadding, PH_ALIGN_TOP | PH_ALIGN_LEFT);
                        }
                        else
                        {
                            drawInfo->Text.Buffer = NULL;
                        }

                        drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y | PH_GRAPH_LABEL_MAX_Y | PH_GRAPH_USE_LINE_2;
                        PhSiSetColorsGraphDrawInfo(drawInfo, PhGetIntegerSetting(L"ColorIoReadOther"), PhGetIntegerSetting(L"ColorIoWrite"));
                        PhGraphStateGetDrawInfo(&context->NetworkGraphState, getDrawInfo, context->Block->NetworkSendHistory.Count);

                        if (!context->NetworkGraphState.Valid)
                        {
                            FLOAT max = 0;

                            for (ULONG i = 0; i < drawInfo->LineDataCount; i++)
                            {
                                FLOAT data1;
                                FLOAT data2;

                                context->NetworkGraphState.Data1[i] = data1 = (FLOAT)PhGetItemCircularBuffer_ULONG64(&context->Block->NetworkReceiveHistory, i);
                                context->NetworkGraphState.Data2[i] = data2 = (FLOAT)PhGetItemCircularBuffer_ULONG64(&context->Block->NetworkSendHistory, i);

                                if (max < data1 + data2)
                                    max = data1 + data2;
                            }

                            // Minimum scaling of 1 MB.
                            //if (max < 1024 * 1024)
                            //    max = 1024 * 1024;

                            if (max != 0)
                            {
                                // Scale the data.

                                PhDivideSinglesBySingle(
                                    context->NetworkGraphState.Data1,
                                    max,
                                    drawInfo->LineDataCount
                                    );
                                PhDivideSinglesBySingle(
                                    context->NetworkGraphState.Data2,
                                    max,
                                    drawInfo->LineDataCount
                                    );
                            }

                            drawInfo->LabelYFunction = PhSiSizeLabelYFunction;
                            drawInfo->LabelYFunctionParameter = max;

                            context->NetworkGraphState.Valid = TRUE;
                        }
                    }
                }
                break;
            case GCN_GETTOOLTIPTEXT:
                {
                    PPH_GRAPH_GETTOOLTIPTEXT getTooltipText = (PPH_GRAPH_GETTOOLTIPTEXT)lParam;

                    if (getTooltipText->Index < getTooltipText->TotalCount)
                    {
                        if (header->hwndFrom == context->DiskGraphHandle)
                        {
                            if (context->DiskGraphState.TooltipIndex != getTooltipText->Index)
                            {
                                ULONG64 diskRead = PhGetItemCircularBuffer_ULONG64(
                                    &context->Block->DiskReadHistory,
                                    getTooltipText->Index
                                    );

                                ULONG64 diskWrite = PhGetItemCircularBuffer_ULONG64(
                                    &context->Block->DiskWriteHistory,
                                    getTooltipText->Index
                                    );

                                PhMoveReference(&context->DiskGraphState.TooltipText, PhFormatString(
                                    L"R: %s\nW: %s\n%s",
                                    PhaFormatSize(diskRead, ULONG_MAX)->Buffer,
                                    PhaFormatSize(diskWrite, ULONG_MAX)->Buffer,
                                    ((PPH_STRING)PH_AUTO(PhGetStatisticsTimeString(NULL, getTooltipText->Index)))->Buffer
                                    ));
                            }

                            getTooltipText->Text = PhGetStringRef(context->DiskGraphState.TooltipText);
                        }
                        else if (header->hwndFrom == context->NetworkGraphHandle)
                        {
                            if (context->NetworkGraphState.TooltipIndex != getTooltipText->Index)
                            {
                                ULONG64 networkSend = PhGetItemCircularBuffer_ULONG64(
                                    &context->Block->NetworkSendHistory,
                                    getTooltipText->Index
                                    );

                                ULONG64 networkReceive = PhGetItemCircularBuffer_ULONG64(
                                    &context->Block->NetworkReceiveHistory,
                                    getTooltipText->Index
                                    );

                                PhMoveReference(&context->NetworkGraphState.TooltipText, PhFormatString(
                                    L"S: %s\nR: %s\n%s",
                                    PhaFormatSize(networkSend, ULONG_MAX)->Buffer,
                                    PhaFormatSize(networkReceive, ULONG_MAX)->Buffer,
                                    ((PPH_STRING)PH_AUTO(PhGetStatisticsTimeString(NULL, getTooltipText->Index)))->Buffer
                                    ));
                            }

                            getTooltipText->Text = PhGetStringRef(context->NetworkGraphState.TooltipText);
                        }
                    }
                }
                break;
            }
        }
        break;
    case ET_WM_UPDATE:
        {
            if (context->Enabled)
            {
                EtwDiskNetworkUpdateGraphs(context);
                EtwDiskNetworkUpdatePanel(context);
            }
        }
        break;
    case WM_SIZE:
        {
            EtwDiskNetworkLayoutGraphs(context);
        }
        break;
    }

    return FALSE;
}

VOID EtProcessEtwPropertiesInitializing(
    _In_ PVOID Parameter
    )
{
    PPH_PLUGIN_PROCESS_PROPCONTEXT propContext = Parameter;

    PhAddProcessPropPage(
        propContext->PropContext,
        PhCreateProcessPropPageContextEx(PluginInstance->DllBase, MAKEINTRESOURCE(IDD_PROCDISKNET), EtwDiskNetworkPageDlgProc, NULL)
        );
}
