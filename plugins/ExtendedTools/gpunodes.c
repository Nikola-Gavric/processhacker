/*
 * Process Hacker Extended Tools -
 *   GPU nodes window
 *
 * Copyright (C) 2011-2015 wj32
 * Copyright (C) 2018 dmex
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

#define GRAPH_PADDING 5
static RECT NormalGraphTextMargin = { 5, 5, 5, 5 };
static RECT NormalGraphTextPadding = { 3, 3, 3, 3 };

INT_PTR CALLBACK EtpGpuNodesDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    );

static RECT MinimumSize;
static PH_LAYOUT_MANAGER LayoutManager;
static RECT LayoutMargin;
static HWND *GraphHandle;
static PPH_GRAPH_STATE GraphState;
static PH_CALLBACK_REGISTRATION ProcessesUpdatedCallbackRegistration;

static HANDLE EtGpuNodesThreadHandle = NULL;
static HWND EtGpuNodesWindowHandle = NULL;
static PH_EVENT EtGpuNodesInitializedEvent = PH_EVENT_INIT;

NTSTATUS EtpGpuNodesDialogThreadStart(
    _In_ PVOID Parameter
    )
{
    BOOL result;
    MSG message;
    PH_AUTO_POOL autoPool;

    PhInitializeAutoPool(&autoPool);

    EtGpuNodesWindowHandle = CreateDialogParam(
        PluginInstance->DllBase,
        MAKEINTRESOURCE(IDD_GPUNODES),
        NULL,
        EtpGpuNodesDlgProc,
        (LPARAM)Parameter
        );

    PhSetEvent(&EtGpuNodesInitializedEvent);

    while (result = GetMessage(&message, NULL, 0, 0))
    {
        if (result == -1)
            break;

        if (!IsDialogMessage(EtGpuNodesWindowHandle, &message))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        PhDrainAutoPool(&autoPool);
    }

    PhDeleteAutoPool(&autoPool);
    PhResetEvent(&EtGpuNodesInitializedEvent);

    if (EtGpuNodesThreadHandle)
        NtClose(EtGpuNodesThreadHandle);

    EtGpuNodesThreadHandle = NULL;
    EtGpuNodesWindowHandle = NULL;

    return STATUS_SUCCESS;
}

VOID EtShowGpuNodesDialog(
    _In_ HWND ParentWindowHandle
    )
{
    if (!EtGpuNodesThreadHandle)
    {
        if (!NT_SUCCESS(PhCreateThreadEx(&EtGpuNodesThreadHandle, EtpGpuNodesDialogThreadStart, ParentWindowHandle)))
        {
            PhShowError(PhMainWndHandle, L"Unable to create the window.");
            return;
        }

        PhWaitForEvent(&EtGpuNodesInitializedEvent, NULL);
    }

    PostMessage(EtGpuNodesWindowHandle, ET_WM_SHOWDIALOG, 0, 0);
}

static VOID ProcessesUpdatedCallback(
    _In_opt_ PVOID Parameter,
    _In_opt_ PVOID Context
    )
{
    PostMessage((HWND)Context, ET_WM_UPDATE, 0, 0);
}

INT_PTR CALLBACK EtpGpuNodesDlgProc(
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
            ULONG i;
            ULONG numberOfRows;
            ULONG numberOfColumns;

            SendMessage(hwndDlg, WM_SETICON, ICON_SMALL, (LPARAM)PH_LOAD_SHARED_ICON_SMALL(PhInstanceHandle, MAKEINTRESOURCE(PHAPP_IDI_PROCESSHACKER)));
            SendMessage(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM)PH_LOAD_SHARED_ICON_LARGE(PhInstanceHandle, MAKEINTRESOURCE(PHAPP_IDI_PROCESSHACKER)));

            PhInitializeLayoutManager(&LayoutManager, hwndDlg);
            LayoutMargin = PhAddLayoutItem(&LayoutManager, GetDlgItem(hwndDlg, IDC_LAYOUT), NULL, PH_ANCHOR_ALL)->Margin;

            GraphHandle = PhAllocate(sizeof(HWND) * EtGpuTotalNodeCount);
            GraphState = PhAllocate(sizeof(PH_GRAPH_STATE) * EtGpuTotalNodeCount);

            for (i = 0; i < EtGpuTotalNodeCount; i++)
            {
                GraphHandle[i] = CreateWindow(
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
                Graph_SetTooltip(GraphHandle[i], TRUE);
                PhInitializeGraphState(&GraphState[i]);
            }

            // Calculate the minimum size.

            numberOfRows = (ULONG)sqrt(EtGpuTotalNodeCount);
            numberOfColumns = (EtGpuTotalNodeCount + numberOfRows - 1) / numberOfRows;

            MinimumSize.left = 0;
            MinimumSize.top = 0;
            MinimumSize.right = 55;
            MinimumSize.bottom = 60;
            MapDialogRect(hwndDlg, &MinimumSize);
            MinimumSize.right += (MinimumSize.right + GRAPH_PADDING) * numberOfColumns;
            MinimumSize.bottom += (MinimumSize.bottom + GRAPH_PADDING) * numberOfRows;

            SetWindowPos(hwndDlg, NULL, 0, 0, MinimumSize.right, MinimumSize.bottom, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER);

            // Note: This dialog must be centered after all other graphs and controls have been added.
            if (PhGetIntegerPairSetting(SETTING_NAME_GPU_NODES_WINDOW_POSITION).X != 0)
                PhLoadWindowPlacementFromSetting(SETTING_NAME_GPU_NODES_WINDOW_POSITION, SETTING_NAME_GPU_NODES_WINDOW_SIZE, hwndDlg);
            else
                PhCenterWindow(hwndDlg, (HWND)lParam);

            PhRegisterCallback(
                PhGetGeneralCallback(GeneralCallbackProcessProviderUpdatedEvent),
                ProcessesUpdatedCallback,
                hwndDlg,
                &ProcessesUpdatedCallbackRegistration
                );

            PhInitializeWindowTheme(hwndDlg, !!PhGetIntegerSetting(L"EnableThemeSupport"));
        }
        break;
    case WM_DESTROY:
        {
            PhSaveWindowPlacementToSetting(SETTING_NAME_GPU_NODES_WINDOW_POSITION, SETTING_NAME_GPU_NODES_WINDOW_SIZE, hwndDlg);

            PhUnregisterCallback(PhGetGeneralCallback(GeneralCallbackProcessProviderUpdatedEvent), &ProcessesUpdatedCallbackRegistration);

            for (ULONG i = 0; i < EtGpuTotalNodeCount; i++)
            {
                PhDeleteGraphState(&GraphState[i]);
            }

            PhFree(GraphHandle);
            PhFree(GraphState);

            PhDeleteLayoutManager(&LayoutManager);

            PostQuitMessage(0);
        }
        break;
    case WM_SIZE:
        {
            HDWP deferHandle;
            RECT clientRect;
            ULONG numberOfRows = (ULONG)sqrt(EtGpuTotalNodeCount);
            ULONG numberOfColumns = (EtGpuTotalNodeCount + numberOfRows - 1) / numberOfRows;
            ULONG numberOfYPaddings = numberOfRows - 1;
            ULONG numberOfXPaddings = numberOfColumns - 1;
            ULONG cellHeight;
            ULONG y;
            ULONG cellWidth;
            ULONG x;
            ULONG i;

            PhLayoutManagerLayout(&LayoutManager);

            deferHandle = BeginDeferWindowPos(EtGpuTotalNodeCount);

            GetClientRect(hwndDlg, &clientRect);
            cellHeight = (clientRect.bottom - LayoutMargin.top - LayoutMargin.bottom - GRAPH_PADDING * numberOfYPaddings) / numberOfRows;
            y = LayoutMargin.top;
            i = 0;

            for (ULONG row = 0; row < numberOfRows; ++row)
            {
                // Give the last row the remaining space; the height we calculated might be off by a few
                // pixels due to integer division.
                if (row == numberOfRows - 1)
                    cellHeight = clientRect.bottom - LayoutMargin.bottom - y;

                cellWidth = (clientRect.right - LayoutMargin.left - LayoutMargin.right - GRAPH_PADDING * numberOfXPaddings) / numberOfColumns;
                x = LayoutMargin.left;

                for (ULONG column = 0; column < numberOfColumns; column++)
                {
                    // Give the last cell the remaining space; the width we calculated might be off by a few
                    // pixels due to integer division.
                    if (column == numberOfColumns - 1)
                        cellWidth = clientRect.right - LayoutMargin.right - x;

                    if (i < EtGpuTotalNodeCount)
                    {
                        deferHandle = DeferWindowPos(
                            deferHandle,
                            GraphHandle[i],
                            NULL,
                            x,
                            y,
                            cellWidth,
                            cellHeight,
                            SWP_NOACTIVATE | SWP_NOZORDER
                            );
                        i++;
                    }
                    x += cellWidth + GRAPH_PADDING;
                }

                y += cellHeight + GRAPH_PADDING;
            }

            EndDeferWindowPos(deferHandle);
        }
        break;
    case WM_SIZING:
        {
            PhResizingMinimumSize((PRECT)lParam, wParam, MinimumSize.right, MinimumSize.bottom);
        }
        break;
    case WM_COMMAND:
        {
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
            case IDCANCEL:
                DestroyWindow(hwndDlg);
                break;
            }
        }
        break;
    case WM_NOTIFY:
        {
            NMHDR *header = (NMHDR *)lParam;
            ULONG i;

            switch (header->code)
            {
            case GCN_GETDRAWINFO:
                {
                    PPH_GRAPH_GETDRAWINFO getDrawInfo = (PPH_GRAPH_GETDRAWINFO)header;
                    PPH_GRAPH_DRAW_INFO drawInfo = getDrawInfo->DrawInfo;

                    drawInfo->Flags = PH_GRAPH_USE_GRID_X | PH_GRAPH_USE_GRID_Y;
                    PhSiSetColorsGraphDrawInfo(drawInfo, PhGetIntegerSetting(L"ColorCpuKernel"), 0);

                    for (i = 0; i < EtGpuTotalNodeCount; i++)
                    {
                        if (header->hwndFrom == GraphHandle[i])
                        {
                            if (PhGetIntegerSetting(L"GraphShowText"))
                            {
                                HDC hdc;
                                FLOAT gpu;
                                ULONG adapterIndex;
                                PPH_STRING engineName = NULL;

                                gpu = PhGetItemCircularBuffer_FLOAT(&EtGpuNodesHistory[i], 0);

                                if ((adapterIndex = EtGetGpuAdapterIndexFromNodeIndex(i)) != ULONG_MAX)
                                    engineName = EtGetGpuAdapterNodeDescription(adapterIndex, i);

                                if (!PhIsNullOrEmptyString(engineName))
                                {
                                    PhMoveReference(&GraphState[i].Text, PhFormatString(
                                        L"%.2f%% (%s)",
                                        gpu * 100,
                                        engineName->Buffer
                                        ));
                                }
                                else
                                {
                                    PhMoveReference(&GraphState[i].Text, PhFormatString(
                                        L"%.2f%% (Node %lu)",
                                        gpu * 100,
                                        i
                                        ));
                                }

                                hdc = Graph_GetBufferedContext(GraphHandle[i]);
                                SelectFont(hdc, PhApplicationFont);
                                PhSetGraphText(hdc, drawInfo, &GraphState[i].Text->sr,
                                    &NormalGraphTextMargin, &NormalGraphTextPadding, PH_ALIGN_TOP | PH_ALIGN_LEFT);
                            }
                            else
                            {
                                drawInfo->Text.Buffer = NULL;
                            }

                            PhGraphStateGetDrawInfo(
                                &GraphState[i],
                                getDrawInfo,
                                EtGpuNodesHistory[i].Count
                                );

                            if (!GraphState[i].Valid)
                            {
                                PhCopyCircularBuffer_FLOAT(&EtGpuNodesHistory[i], GraphState[i].Data1, drawInfo->LineDataCount);
                                GraphState[i].Valid = TRUE;
                            }

                            break;
                        }
                    }
                }
                break;
            case GCN_GETTOOLTIPTEXT:
                {
                    PPH_GRAPH_GETTOOLTIPTEXT getTooltipText = (PPH_GRAPH_GETTOOLTIPTEXT)header;

                    if (getTooltipText->Index < getTooltipText->TotalCount)
                    {
                        for (i = 0; i < EtGpuTotalNodeCount; i++)
                        {
                            if (header->hwndFrom == GraphHandle[i])
                            {
                                if (GraphState[i].TooltipIndex != getTooltipText->Index)
                                {
                                    FLOAT gpu;
                                    ULONG adapterIndex;
                                    PPH_STRING adapterEngineName = NULL;
                                    PPH_STRING adapterDescription;

                                    gpu = PhGetItemCircularBuffer_FLOAT(&EtGpuNodesHistory[i], getTooltipText->Index);
                                    adapterIndex = EtGetGpuAdapterIndexFromNodeIndex(i);

                                    if (adapterIndex != ULONG_MAX)
                                    {
                                        adapterEngineName = EtGetGpuAdapterNodeDescription(adapterIndex, i);
                                        adapterDescription = EtGetGpuAdapterDescription(adapterIndex);

                                        if (adapterDescription && adapterDescription->Length == 0)
                                            PhClearReference(&adapterDescription);

                                        if (!adapterDescription)
                                            adapterDescription = PhFormatString(L"Adapter %lu", adapterIndex);
                                    }
                                    else
                                    {
                                        adapterDescription = PhCreateString(L"Unknown Adapter");
                                    }

                                    if (!PhIsNullOrEmptyString(adapterEngineName))
                                    {
                                        PhMoveReference(&GraphState[i].TooltipText, PhFormatString(
                                            L"Node %lu (%s) on %s\n%.2f%%\n%s",
                                            i,
                                            adapterEngineName->Buffer,
                                            adapterDescription->Buffer,
                                            gpu * 100,
                                            ((PPH_STRING)PH_AUTO(PhGetStatisticsTimeString(NULL, getTooltipText->Index)))->Buffer
                                            ));
                                    }
                                    else
                                    {
                                        PhMoveReference(&GraphState[i].TooltipText, PhFormatString(
                                            L"Node %lu on %s\n%.2f%%\n%s",
                                            i,
                                            adapterDescription->Buffer,
                                            gpu * 100,
                                            ((PPH_STRING)PH_AUTO(PhGetStatisticsTimeString(NULL, getTooltipText->Index)))->Buffer
                                            ));
                                    }

               
                                    PhDereferenceObject(adapterDescription);
                                }

                                getTooltipText->Text = GraphState[i].TooltipText->sr;

                                break;
                            }
                        }
                    }
                }
                break;
            }
        }
        break;
    case ET_WM_SHOWDIALOG:    
        {
            if (IsMinimized(hwndDlg))
                ShowWindow(hwndDlg, SW_RESTORE);
            else
                ShowWindow(hwndDlg, SW_SHOW);

            SetForegroundWindow(hwndDlg);
        }
        break;
    case ET_WM_UPDATE:
        {
            for (ULONG i = 0; i < EtGpuTotalNodeCount; i++)
            {
                GraphState[i].Valid = FALSE;
                GraphState[i].TooltipIndex = ULONG_MAX;
                Graph_MoveGrid(GraphHandle[i], 1);
                Graph_Draw(GraphHandle[i]);
                Graph_UpdateTooltip(GraphHandle[i]);
                InvalidateRect(GraphHandle[i], NULL, FALSE);
            }
        }
        break;
    }

    return FALSE;
}
