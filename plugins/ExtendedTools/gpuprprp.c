/*
 * Process Hacker Extended Tools -
 *   GPU process properties page
 *
 * Copyright (C) 2011 wj32
 * Copyright (C) 2015-2018 dmex
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

typedef struct _ET_GPU_CONTEXT
{
    HWND WindowHandle;
    //HWND PanelHandle;
    //HWND DetailsHandle;

    PET_PROCESS_BLOCK Block;
    PH_CALLBACK_REGISTRATION ProcessesUpdatedRegistration;
    BOOLEAN Enabled;
    PH_LAYOUT_MANAGER LayoutManager;

    HWND GpuGroupBox;
    HWND MemGroupBox;
    HWND SharedGroupBox;
    HWND CommittedGroupBox;

    HWND GpuGraphHandle;
    HWND MemGraphHandle;
    HWND SharedGraphHandle;
    HWND CommittedGraphHandle;

    PH_GRAPH_STATE GpuGraphState;
    PH_GRAPH_STATE MemoryGraphState;
    PH_GRAPH_STATE MemorySharedGraphState;
    PH_GRAPH_STATE GpuCommittedGraphState;
} ET_GPU_CONTEXT, *PET_GPU_CONTEXT;

static RECT NormalGraphTextMargin = { 5, 5, 5, 5 };
static RECT NormalGraphTextPadding = { 3, 3, 3, 3 };

VOID GpuPropCreateGraphs(
    _In_ PET_GPU_CONTEXT Context
    )
{
    Context->GpuGraphHandle = CreateWindow(
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
    Graph_SetTooltip(Context->GpuGraphHandle, TRUE);

    Context->MemGraphHandle = CreateWindow(
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
    Graph_SetTooltip(Context->MemGraphHandle, TRUE);

    Context->SharedGraphHandle = CreateWindow(
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
    Graph_SetTooltip(Context->SharedGraphHandle, TRUE);

    Context->CommittedGraphHandle = CreateWindow(
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
    Graph_SetTooltip(Context->CommittedGraphHandle, TRUE);
}

VOID GpuPropCreatePanel(
    _In_ PET_GPU_CONTEXT Context
    )
{
    RECT margin;

    //Context->PanelHandle = CreateDialogParam(
    //    PluginInstance->DllBase,
    //    MAKEINTRESOURCE(IDD_PROCGPU_PANEL),
    //    Context->WindowHandle,
    //    GpuPanelDialogProc,
    //    (LPARAM)Context
    //    );
    //
    //SetWindowPos(
    //    Context->PanelHandle,
    //    NULL,
    //    10, 0, 0, 0,
    //    SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOSIZE | SWP_NOZORDER
    //    );
    //
    //ShowWindow(Context->PanelHandle, SW_SHOW);

    margin.left = 0;
    margin.top = 0;
    margin.right = 0;
    margin.bottom = 10;
    MapDialogRect(Context->WindowHandle, &margin);

    //PhAddLayoutItemEx(
    //    &Context->LayoutManager,
    //    Context->PanelHandle,
    //    NULL,
    //    PH_ANCHOR_BOTTOM | PH_ANCHOR_LEFT,
    //    margin
    //    );

    SendMessage(Context->WindowHandle, WM_SIZE, 0, 0);
}

VOID GpuPropLayoutGraphs(
    _In_ PET_GPU_CONTEXT Context
    )
{
    HDWP deferHandle;
    RECT clientRect;
    //RECT panelRect;
    RECT margin = { PH_SCALE_DPI(13), PH_SCALE_DPI(13), PH_SCALE_DPI(13), PH_SCALE_DPI(13) };
    RECT innerMargin = { PH_SCALE_DPI(10), PH_SCALE_DPI(20), PH_SCALE_DPI(10), PH_SCALE_DPI(10) };
    LONG between = PH_SCALE_DPI(3);
    ULONG graphWidth;
    ULONG graphHeight;

    PhLayoutManagerLayout(&Context->LayoutManager);

    Context->GpuGraphState.Valid = FALSE;
    Context->MemoryGraphState.Valid = FALSE;
    Context->MemorySharedGraphState.Valid = FALSE;

    GetClientRect(Context->WindowHandle, &clientRect);

    // Limit the rectangle bottom to the top of the panel.
    //GetWindowRect(Context->PanelHandle, &panelRect);
    //MapWindowPoints(NULL, Context->WindowHandle, (PPOINT)&panelRect, 2);
    //clientRect.bottom = panelRect.top + 10; // +10 removing extra spacing

    graphWidth = clientRect.right - margin.left - margin.right;
    graphHeight = (clientRect.bottom - margin.top - margin.bottom - between * 4) / 4;

    deferHandle = BeginDeferWindowPos(8);

    deferHandle = DeferWindowPos(deferHandle, Context->GpuGroupBox, NULL, margin.left, margin.top, graphWidth, graphHeight, SWP_NOACTIVATE | SWP_NOZORDER);
    deferHandle = DeferWindowPos(
        deferHandle,
        Context->GpuGraphHandle,
        NULL,
        margin.left + innerMargin.left,
        margin.top + innerMargin.top,
        graphWidth - innerMargin.left - innerMargin.right,
        graphHeight - innerMargin.top - innerMargin.bottom,
        SWP_NOACTIVATE | SWP_NOZORDER
        );

    deferHandle = DeferWindowPos(deferHandle, Context->MemGroupBox, NULL, margin.left, margin.top + graphHeight + between, graphWidth, graphHeight, SWP_NOACTIVATE | SWP_NOZORDER);
    deferHandle = DeferWindowPos(
        deferHandle,
        Context->MemGraphHandle,
        NULL,
        margin.left + innerMargin.left,
        margin.top + graphHeight + between + innerMargin.top,
        graphWidth - innerMargin.left - innerMargin.right,
        graphHeight - innerMargin.top - innerMargin.bottom,
        SWP_NOACTIVATE | SWP_NOZORDER
        );

    deferHandle = DeferWindowPos(deferHandle, Context->SharedGroupBox, NULL, margin.left, margin.top + (graphHeight + between) * 2, graphWidth, graphHeight, SWP_NOACTIVATE | SWP_NOZORDER);
    deferHandle = DeferWindowPos(
        deferHandle,
        Context->SharedGraphHandle,
        NULL,
        margin.left + innerMargin.left,
        margin.top + (graphHeight + between) * 2 + innerMargin.top,
        graphWidth - innerMargin.left - innerMargin.right,
        graphHeight - innerMargin.top - innerMargin.bottom,
        SWP_NOACTIVATE | SWP_NOZORDER
        );

    deferHandle = DeferWindowPos(deferHandle, Context->CommittedGroupBox, NULL, margin.left, margin.top + (graphHeight + between) * 3, graphWidth, graphHeight, SWP_NOACTIVATE | SWP_NOZORDER);
    deferHandle = DeferWindowPos(
        deferHandle,
        Context->CommittedGraphHandle,
        NULL,
        margin.left + innerMargin.left,
        margin.top + (graphHeight + between) * 3 + innerMargin.top,
        graphWidth - innerMargin.left - innerMargin.right,
        graphHeight - innerMargin.top - innerMargin.bottom,
        SWP_NOACTIVATE | SWP_NOZORDER
        );

    EndDeferWindowPos(deferHandle);
}

VOID GpuPropUpdateGraphs(
    _In_ PET_GPU_CONTEXT Context
    )
{
    Context->GpuGraphState.Valid = FALSE;
    Context->GpuGraphState.TooltipIndex = ULONG_MAX;
    Graph_MoveGrid(Context->GpuGraphHandle, 1);
    Graph_Draw(Context->GpuGraphHandle);
    Graph_UpdateTooltip(Context->GpuGraphHandle);
    InvalidateRect(Context->GpuGraphHandle, NULL, FALSE);

    Context->MemoryGraphState.Valid = FALSE;
    Context->MemoryGraphState.TooltipIndex = ULONG_MAX;
    Graph_MoveGrid(Context->MemGraphHandle, 1);
    Graph_Draw(Context->MemGraphHandle);
    Graph_UpdateTooltip(Context->MemGraphHandle);
    InvalidateRect(Context->MemGraphHandle, NULL, FALSE);

    Context->MemorySharedGraphState.Valid = FALSE;
    Context->MemorySharedGraphState.TooltipIndex = ULONG_MAX;
    Graph_MoveGrid(Context->SharedGraphHandle, 1);
    Graph_Draw(Context->SharedGraphHandle);
    Graph_UpdateTooltip(Context->SharedGraphHandle);
    InvalidateRect(Context->SharedGraphHandle, NULL, FALSE);

    Context->GpuCommittedGraphState.Valid = FALSE;
    Context->GpuCommittedGraphState.TooltipIndex = ULONG_MAX;
    Graph_MoveGrid(Context->CommittedGraphHandle, 1);
    Graph_Draw(Context->CommittedGraphHandle);
    Graph_UpdateTooltip(Context->CommittedGraphHandle);
    InvalidateRect(Context->CommittedGraphHandle, NULL, FALSE);
}

//VOID GpuPropUpdatePanel(
//    _Inout_ PET_GPU_CONTEXT Context
//    )
//{
//    PET_PROCESS_BLOCK block = Context->Block;
//    WCHAR runningTimeString[PH_TIMESPAN_STR_LEN_1] = L"N/A";
//
//    PhPrintTimeSpan(runningTimeString, block->GpuRunningTimeDelta.Value * 10, PH_TIMESPAN_HMSM);
//    PhSetDialogItemText(Context->PanelHandle, IDC_ZRUNNINGTIME_V, runningTimeString);
//    PhSetDialogItemText(Context->PanelHandle, IDC_ZCONTEXTSWITCHES_V, PhaFormatUInt64(block->GpuContextSwitches, TRUE)->Buffer);
//
//    if (Context->DetailsHandle)
//    {
//        ET_PROCESS_GPU_STATISTICS processGpuStatistics;
//    
//        if (Context->Block->ProcessItem->QueryHandle)
//            EtQueryProcessGpuStatistics(Context->Block->ProcessItem->QueryHandle, &processGpuStatistics);
//        else
//            memset(&processGpuStatistics, 0, sizeof(ET_PROCESS_GPU_STATISTICS));
//    
//        PhSetDialogItemText(Context->DetailsHandle, IDC_ZDEDICATEDCOMMITTED_V, PhaFormatSize(processGpuStatistics.DedicatedCommitted, ULONG_MAX)->Buffer);
//        PhSetDialogItemText(Context->DetailsHandle, IDC_ZSHAREDCOMMITTED_V, PhaFormatSize(processGpuStatistics.SharedCommitted, ULONG_MAX)->Buffer);
//        PhSetDialogItemText(Context->DetailsHandle, IDC_ZTOTALALLOCATED_V, PhaFormatSize(processGpuStatistics.BytesAllocated, ULONG_MAX)->Buffer);
//        PhSetDialogItemText(Context->DetailsHandle, IDC_ZTOTALRESERVED_V, PhaFormatSize(processGpuStatistics.BytesReserved, ULONG_MAX)->Buffer);
//        PhSetDialogItemText(Context->DetailsHandle, IDC_ZWRITECOMBINEDALLOCATED_V, PhaFormatSize(processGpuStatistics.WriteCombinedBytesAllocated, ULONG_MAX)->Buffer);
//        PhSetDialogItemText(Context->DetailsHandle, IDC_ZWRITECOMBINEDRESERVED_V, PhaFormatSize(processGpuStatistics.WriteCombinedBytesReserved, ULONG_MAX)->Buffer);
//        PhSetDialogItemText(Context->DetailsHandle, IDC_ZCACHEDALLOCATED_V, PhaFormatSize(processGpuStatistics.CachedBytesAllocated, ULONG_MAX)->Buffer);
//        PhSetDialogItemText(Context->DetailsHandle, IDC_ZCACHEDRESERVED_V, PhaFormatSize(processGpuStatistics.CachedBytesReserved, ULONG_MAX)->Buffer);
//        PhSetDialogItemText(Context->DetailsHandle, IDC_ZSECTIONALLOCATED_V, PhaFormatSize(processGpuStatistics.SectionBytesAllocated, ULONG_MAX)->Buffer);
//        PhSetDialogItemText(Context->DetailsHandle, IDC_ZSECTIONRESERVED_V, PhaFormatSize(processGpuStatistics.SectionBytesReserved, ULONG_MAX)->Buffer);
//    }
//}

VOID NTAPI ProcessesUpdatedHandler(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PET_GPU_CONTEXT context = Context;

    if (context && context->WindowHandle && context->Enabled)
    {
        PostMessage(context->WindowHandle, ET_WM_UPDATE, 0, 0);
    }
}

INT_PTR CALLBACK EtpGpuPageDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    LPPROPSHEETPAGE propSheetPage;
    PPH_PROCESS_PROPPAGECONTEXT propPageContext;
    PPH_PROCESS_ITEM processItem;
    PET_GPU_CONTEXT context;

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

            context = PhAllocate(sizeof(ET_GPU_CONTEXT));
            memset(context, 0, sizeof(ET_GPU_CONTEXT));

            context->WindowHandle = hwndDlg;
            context->Block = EtGetProcessBlock(processItem);
            context->Enabled = TRUE;
            context->GpuGroupBox = GetDlgItem(hwndDlg, IDC_GROUPGPU);
            context->MemGroupBox = GetDlgItem(hwndDlg, IDC_GROUPMEM);
            context->SharedGroupBox = GetDlgItem(hwndDlg, IDC_GROUPSHARED);
            context->CommittedGroupBox = GetDlgItem(hwndDlg, IDC_GROUPCOMMIT);
            propPageContext->Context = context;

            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);

            PhInitializeGraphState(&context->GpuGraphState);
            PhInitializeGraphState(&context->MemoryGraphState);
            PhInitializeGraphState(&context->MemorySharedGraphState);
            PhInitializeGraphState(&context->GpuCommittedGraphState);

            GpuPropCreateGraphs(context);
            GpuPropCreatePanel(context);

            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackProcessProviderUpdatedEvent),
                ProcessesUpdatedHandler,
                context,
                &context->ProcessesUpdatedRegistration
                );

            PhInitializeWindowTheme(hwndDlg, !!PhGetIntegerSetting(L"EnableThemeSupport"));
        }
        break;
    case WM_DESTROY:
        {
            PhDeleteLayoutManager(&context->LayoutManager);

            PhDeleteGraphState(&context->GpuGraphState);
            PhDeleteGraphState(&context->MemoryGraphState);
            PhDeleteGraphState(&context->MemorySharedGraphState);
            PhDeleteGraphState(&context->GpuCommittedGraphState);

            if (context->GpuGraphHandle)
                DestroyWindow(context->GpuGraphHandle);
            if (context->MemGraphHandle)
                DestroyWindow(context->MemGraphHandle);
            if (context->SharedGraphHandle)
                DestroyWindow(context->SharedGraphHandle);
            if (context->CommittedGraphHandle)
                DestroyWindow(context->CommittedGraphHandle);

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

                    if (header->hwndFrom == context->GpuGraphHandle)
                    {
                        if (PhGetIntegerSetting(L"GraphShowText"))
                        {
                            HDC hdc;

                            PhMoveReference(&context->GpuGraphState.Text, PhFormatString(
                                L"%.2f%%",
                                context->Block->CurrentGpuUsage * 100
                                ));

                            hdc = Graph_GetBufferedContext(context->GpuGraphHandle);
                            SelectFont(hdc, PhApplicationFont);
                            PhSetGraphText(hdc, drawInfo, &context->GpuGraphState.Text->sr,
                                &NormalGraphTextMargin, &NormalGraphTextPadding, PH_ALIGN_TOP | PH_ALIGN_LEFT);
                        }
                        else
                        {
                            drawInfo->Text.Buffer = NULL;
                        }

                        drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y;
                        PhSiSetColorsGraphDrawInfo(drawInfo, PhGetIntegerSetting(L"ColorCpuKernel"), 0);
                        PhGraphStateGetDrawInfo(&context->GpuGraphState, getDrawInfo, context->Block->GpuHistory.Count);

                        if (!context->GpuGraphState.Valid)
                        {
                            PhCopyCircularBuffer_FLOAT(&context->Block->GpuHistory, context->GpuGraphState.Data1, drawInfo->LineDataCount);
                            context->GpuGraphState.Valid = TRUE;
                        }
                    }
                    else if (header->hwndFrom == context->MemGraphHandle)
                    {
                        if (PhGetIntegerSetting(L"GraphShowText"))
                        {
                            HDC hdc;

                            PhMoveReference(&context->MemoryGraphState.Text, PhFormatString(
                                L"%s",
                                PhaFormatSize(UInt32x32To64(context->Block->CurrentMemUsage, PAGE_SIZE), ULONG_MAX)->Buffer
                                ));

                            hdc = Graph_GetBufferedContext(context->MemGraphHandle);
                            SelectFont(hdc, PhApplicationFont);
                            PhSetGraphText(
                                hdc,
                                drawInfo,
                                &context->MemoryGraphState.Text->sr,
                                &NormalGraphTextMargin,
                                &NormalGraphTextPadding,
                                PH_ALIGN_TOP | PH_ALIGN_LEFT
                                );
                        }
                        else
                        {
                            drawInfo->Text.Buffer = NULL;
                        }

                        drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y;
                        PhSiSetColorsGraphDrawInfo(drawInfo, PhGetIntegerSetting(L"ColorPhysical"), 0);
                        PhGraphStateGetDrawInfo(
                            &context->MemoryGraphState,
                            getDrawInfo,
                            context->Block->MemoryHistory.Count
                            );

                        if (!context->MemoryGraphState.Valid)
                        {
                            for (ULONG i = 0; i < drawInfo->LineDataCount; i++)
                            {
                                context->MemoryGraphState.Data1[i] = (FLOAT)PhGetItemCircularBuffer_ULONG(&context->Block->MemoryHistory, i);
                            }

                            if (EtGpuDedicatedLimit != 0)
                            {
                                PhDivideSinglesBySingle(
                                    context->MemoryGraphState.Data1,
                                    (FLOAT)EtGpuDedicatedLimit / PAGE_SIZE,
                                    drawInfo->LineDataCount
                                    );
                            }

                            context->MemoryGraphState.Valid = TRUE;
                        }
                    }
                    else if (header->hwndFrom == context->SharedGraphHandle)
                    {
                        if (PhGetIntegerSetting(L"GraphShowText"))
                        {
                            HDC hdc;

                            PhMoveReference(&context->MemorySharedGraphState.Text, PhFormatString(
                                L"%s",
                                PhaFormatSize(UInt32x32To64(context->Block->CurrentMemSharedUsage, PAGE_SIZE), ULONG_MAX)->Buffer
                                ));

                            hdc = Graph_GetBufferedContext(context->SharedGraphHandle);
                            SelectFont(hdc, PhApplicationFont);
                            PhSetGraphText(hdc, drawInfo, &context->MemorySharedGraphState.Text->sr,
                                &NormalGraphTextMargin, &NormalGraphTextPadding, PH_ALIGN_TOP | PH_ALIGN_LEFT);
                        }
                        else
                        {
                            drawInfo->Text.Buffer = NULL;
                        }

                        drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y;
                        PhSiSetColorsGraphDrawInfo(drawInfo, PhGetIntegerSetting(L"ColorPrivate"), 0);
                        PhGraphStateGetDrawInfo(
                            &context->MemorySharedGraphState,
                            getDrawInfo,
                            context->Block->MemorySharedHistory.Count
                            );

                        if (!context->MemorySharedGraphState.Valid)
                        {
                            for (ULONG i = 0; i < drawInfo->LineDataCount; i++)
                            {
                                context->MemorySharedGraphState.Data1[i] = (FLOAT)PhGetItemCircularBuffer_ULONG(&context->Block->MemorySharedHistory, i);
                            }

                            if (EtGpuSharedLimit != 0)
                            {
                                PhDivideSinglesBySingle(
                                    context->MemorySharedGraphState.Data1,
                                    (FLOAT)EtGpuSharedLimit / PAGE_SIZE,
                                    drawInfo->LineDataCount
                                    );
                            }

                            context->MemorySharedGraphState.Valid = TRUE;
                        }
                    }
                    else if (header->hwndFrom == context->CommittedGraphHandle)
                    {
                        if (PhGetIntegerSetting(L"GraphShowText"))
                        {
                            HDC hdc;

                            PhMoveReference(&context->GpuCommittedGraphState.Text, PhFormatString(
                                L"%s",
                                PhaFormatSize(UInt32x32To64(context->Block->CurrentCommitUsage, PAGE_SIZE), ULONG_MAX)->Buffer
                                ));

                            hdc = Graph_GetBufferedContext(context->CommittedGraphHandle);
                            SelectFont(hdc, PhApplicationFont);
                            PhSetGraphText(hdc, drawInfo, &context->GpuCommittedGraphState.Text->sr,
                                &NormalGraphTextMargin, &NormalGraphTextPadding, PH_ALIGN_TOP | PH_ALIGN_LEFT);
                        }
                        else
                        {
                            drawInfo->Text.Buffer = NULL;
                        }

                        drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y;
                        PhSiSetColorsGraphDrawInfo(drawInfo, PhGetIntegerSetting(L"ColorPrivate"), 0);
                        PhGraphStateGetDrawInfo(
                            &context->GpuCommittedGraphState,
                            getDrawInfo,
                            context->Block->GpuCommittedHistory.Count
                            );

                        if (!context->GpuCommittedGraphState.Valid)
                        {
                            ULONG i;
                            static FLOAT max = 1024 * 1024; // minimum scaling

                            for (i = 0; i < drawInfo->LineDataCount; i++)
                            {
                                FLOAT data1;

                                context->GpuCommittedGraphState.Data1[i] = data1 = (FLOAT)PhGetItemCircularBuffer_ULONG(&context->Block->GpuCommittedHistory, i);

                                if (max < data1)
                                    max = data1;
                            }

                            // Scale the data.
                            PhDivideSinglesBySingle(
                                context->GpuCommittedGraphState.Data1,
                                max,
                                drawInfo->LineDataCount
                                );

                            context->GpuCommittedGraphState.Valid = TRUE;
                        }
                    }
                }
                break;
            case GCN_GETTOOLTIPTEXT:
                {
                    PPH_GRAPH_GETTOOLTIPTEXT getTooltipText = (PPH_GRAPH_GETTOOLTIPTEXT)lParam;

                    if (getTooltipText->Index < getTooltipText->TotalCount)
                    {
                        if (header->hwndFrom == context->GpuGraphHandle)
                        {
                            if (context->GpuGraphState.TooltipIndex != getTooltipText->Index)
                            {
                                FLOAT gpuUsage = PhGetItemCircularBuffer_FLOAT(
                                    &context->Block->GpuHistory,
                                    getTooltipText->Index
                                    );

                                PhMoveReference(&context->GpuGraphState.TooltipText, PhFormatString(
                                    L"%.2f%%\n%s",
                                    gpuUsage * 100,
                                    PH_AUTO_T(PH_STRING, PhGetStatisticsTimeString(NULL, getTooltipText->Index))->Buffer)
                                    );
                            }

                            getTooltipText->Text = PhGetStringRef(context->GpuGraphState.TooltipText);
                        }
                        else if (header->hwndFrom == context->MemGraphHandle)
                        {
                            if (context->MemoryGraphState.TooltipIndex != getTooltipText->Index)
                            {
                                ULONG gpuMemory = PhGetItemCircularBuffer_ULONG(
                                    &context->Block->MemoryHistory,
                                    getTooltipText->Index
                                    );

                                PhMoveReference(&context->MemoryGraphState.TooltipText, PhFormatString(
                                    L"%s\n%s",
                                    PhFormatSize(UInt32x32To64(gpuMemory, PAGE_SIZE), ULONG_MAX)->Buffer,
                                    PH_AUTO_T(PH_STRING, PhGetStatisticsTimeString(NULL, getTooltipText->Index))->Buffer)
                                    );
                            }

                            getTooltipText->Text = PhGetStringRef(context->MemoryGraphState.TooltipText);
                        }
                        else if (header->hwndFrom == context->SharedGraphHandle)
                        {
                            if (context->MemorySharedGraphState.TooltipIndex != getTooltipText->Index)
                            {
                                ULONG gpuSharedMemory = PhGetItemCircularBuffer_ULONG(
                                    &context->Block->MemorySharedHistory,
                                    getTooltipText->Index
                                    );

                                PhMoveReference(&context->MemorySharedGraphState.TooltipText, PhFormatString(
                                    L"%s\n%s",
                                    PhFormatSize(UInt32x32To64(gpuSharedMemory, PAGE_SIZE), ULONG_MAX)->Buffer,
                                    PH_AUTO_T(PH_STRING, PhGetStatisticsTimeString(NULL, getTooltipText->Index))->Buffer)
                                    );
                            }

                            getTooltipText->Text = PhGetStringRef(context->MemorySharedGraphState.TooltipText);
                        }
                        else if (header->hwndFrom == context->CommittedGraphHandle)
                        {
                            if (context->GpuCommittedGraphState.TooltipIndex != getTooltipText->Index)
                            {
                                ULONG gpuCommitMemory = PhGetItemCircularBuffer_ULONG(
                                    &context->Block->GpuCommittedHistory,
                                    getTooltipText->Index
                                    );

                                PhMoveReference(&context->GpuCommittedGraphState.TooltipText, PhFormatString(
                                    L"%s\n%s",
                                    PhFormatSize(UInt32x32To64(gpuCommitMemory, PAGE_SIZE), ULONG_MAX)->Buffer,
                                    PH_AUTO_T(PH_STRING, PhGetStatisticsTimeString(NULL, getTooltipText->Index))->Buffer)
                                    );
                            }

                            getTooltipText->Text = PhGetStringRef(context->GpuCommittedGraphState.TooltipText);
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
                GpuPropUpdateGraphs(context);
                //GpuPropUpdatePanel(context);
            }
        }
        break;
    case WM_SIZE:
        {
            GpuPropLayoutGraphs(context);
        }
        break;
    }

    return FALSE;
}

VOID EtProcessGpuPropertiesInitializing(
    _In_ PVOID Parameter
    )
{
    PPH_PLUGIN_PROCESS_PROPCONTEXT propContext = Parameter;

    if (EtGpuEnabled)
    {
        PhAddProcessPropPage(
            propContext->PropContext,
            PhCreateProcessPropPageContextEx(PluginInstance->DllBase, MAKEINTRESOURCE(IDD_PROCGPU), EtpGpuPageDlgProc, NULL)
            );
    }
}
