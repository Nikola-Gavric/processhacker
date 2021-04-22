#ifndef PH_PROCTREE_H
#define PH_PROCTREE_H

#include <phuisup.h>

// Columns

// Default columns should go first
#define PHPRTLC_NAME 0
#define PHPRTLC_PID 1
#define PHPRTLC_CPU 2
#define PHPRTLC_IOTOTALRATE 3
#define PHPRTLC_PRIVATEBYTES 4
#define PHPRTLC_USERNAME 5
#define PHPRTLC_DESCRIPTION 6

#define PHPRTLC_COMPANYNAME 7
#define PHPRTLC_VERSION 8
#define PHPRTLC_FILENAME 9
#define PHPRTLC_COMMANDLINE 10
#define PHPRTLC_PEAKPRIVATEBYTES 11
#define PHPRTLC_WORKINGSET 12
#define PHPRTLC_PEAKWORKINGSET 13
#define PHPRTLC_PRIVATEWS 14
#define PHPRTLC_SHAREDWS 15
#define PHPRTLC_SHAREABLEWS 16
#define PHPRTLC_VIRTUALSIZE 17
#define PHPRTLC_PEAKVIRTUALSIZE 18
#define PHPRTLC_PAGEFAULTS 19
#define PHPRTLC_SESSIONID 20
#define PHPRTLC_PRIORITYCLASS 21
#define PHPRTLC_BASEPRIORITY 22

#define PHPRTLC_THREADS 23
#define PHPRTLC_HANDLES 24
#define PHPRTLC_GDIHANDLES 25
#define PHPRTLC_USERHANDLES 26
#define PHPRTLC_IORORATE 27
#define PHPRTLC_IOWRATE 28
#define PHPRTLC_INTEGRITY 29
#define PHPRTLC_IOPRIORITY 30
#define PHPRTLC_PAGEPRIORITY 31
#define PHPRTLC_STARTTIME 32
#define PHPRTLC_TOTALCPUTIME 33
#define PHPRTLC_KERNELCPUTIME 34
#define PHPRTLC_USERCPUTIME 35
#define PHPRTLC_VERIFICATIONSTATUS 36
#define PHPRTLC_VERIFIEDSIGNER 37
#define PHPRTLC_ASLR 38
#define PHPRTLC_RELATIVESTARTTIME 39
#define PHPRTLC_BITS 40
#define PHPRTLC_ELEVATION 41
#define PHPRTLC_WINDOWTITLE 42
#define PHPRTLC_WINDOWSTATUS 43
#define PHPRTLC_CYCLES 44
#define PHPRTLC_CYCLESDELTA 45
#define PHPRTLC_CPUHISTORY 46
#define PHPRTLC_PRIVATEBYTESHISTORY 47
#define PHPRTLC_IOHISTORY 48
#define PHPRTLC_DEP 49
#define PHPRTLC_VIRTUALIZED 50
#define PHPRTLC_CONTEXTSWITCHES 51
#define PHPRTLC_CONTEXTSWITCHESDELTA 52
#define PHPRTLC_PAGEFAULTSDELTA 53

#define PHPRTLC_IOREADS 54
#define PHPRTLC_IOWRITES 55
#define PHPRTLC_IOOTHER 56
#define PHPRTLC_IOREADBYTES 57
#define PHPRTLC_IOWRITEBYTES 58
#define PHPRTLC_IOOTHERBYTES 59
#define PHPRTLC_IOREADSDELTA 60
#define PHPRTLC_IOWRITESDELTA 61
#define PHPRTLC_IOOTHERDELTA 62

#define PHPRTLC_OSCONTEXT 63
#define PHPRTLC_PAGEDPOOL 64
#define PHPRTLC_PEAKPAGEDPOOL 65
#define PHPRTLC_NONPAGEDPOOL 66
#define PHPRTLC_PEAKNONPAGEDPOOL 67
#define PHPRTLC_MINIMUMWORKINGSET 68
#define PHPRTLC_MAXIMUMWORKINGSET 69
#define PHPRTLC_PRIVATEBYTESDELTA 70
#define PHPRTLC_SUBSYSTEM 71
#define PHPRTLC_PACKAGENAME 72
#define PHPRTLC_APPID 73
#define PHPRTLC_DPIAWARENESS 74
#define PHPRTLC_CFGUARD 75
#define PHPRTLC_TIMESTAMP 76
#define PHPRTLC_FILEMODIFIEDTIME 77
#define PHPRTLC_FILESIZE 78
#define PHPRTLC_SUBPROCESSCOUNT 79
#define PHPRTLC_JOBOBJECTID 80
#define PHPRTLC_PROTECTION 81
#define PHPRTLC_DESKTOP 82
#define PHPRTLC_CRITICAL 83
#define PHPRTLC_PIDHEX 84

#define PHPRTLC_MAXIMUM 85
#define PHPRTLC_IOGROUP_COUNT 9

#define PHPN_WSCOUNTERS 0x1
#define PHPN_GDIUSERHANDLES 0x2
#define PHPN_IOPAGEPRIORITY 0x4
#define PHPN_WINDOW 0x8
#define PHPN_DEPSTATUS 0x10
#define PHPN_TOKEN 0x20
#define PHPN_OSCONTEXT 0x40
#define PHPN_QUOTALIMITS 0x80
#define PHPN_IMAGE 0x100
#define PHPN_APPID 0x200
#define PHPN_DPIAWARENESS 0x400
#define PHPN_FILEATTRIBUTES 0x800
#define PHPN_DESKTOPINFO 0x1000
#define PHPN_USERNAME 0x2000
#define PHPN_CRITICAL 0x4000

// begin_phapppub
typedef struct _PH_PROCESS_NODE
{
    PH_TREENEW_NODE Node;

    PH_HASH_ENTRY HashEntry;

    PH_SH_STATE ShState;

    HANDLE ProcessId;
    PPH_PROCESS_ITEM ProcessItem;

    struct _PH_PROCESS_NODE *Parent;
    PPH_LIST Children;
// end_phapppub

    PH_STRINGREF TextCache[PHPRTLC_MAXIMUM];

    PH_STRINGREF DescriptionText;

    // If the user has selected certain columns we need extra information that isn't retrieved by
    // the process provider.
    ULONG ValidMask;

    // WS counters
    PH_PROCESS_WS_COUNTERS WsCounters;
    // GDI, USER handles
    ULONG GdiHandles;
    ULONG UserHandles;
    // I/O, page priority
    IO_PRIORITY_HINT IoPriority;
    ULONG PagePriority;
    // Window
    HWND WindowHandle;
    PPH_STRING WindowText;
    BOOLEAN WindowHung;
    // DEP status
    ULONG DepStatus;
    // Token
    BOOLEAN VirtualizationAllowed;
    BOOLEAN VirtualizationEnabled;
    // OS Context
    GUID OsContextGuid;
    ULONG OsContextVersion;
    // Quota limits
    SIZE_T MinimumWorkingSetSize;
    SIZE_T MaximumWorkingSetSize;
    // Image
    ULONG ImageTimeDateStamp;
    USHORT ImageCharacteristics;
    USHORT ImageReserved;
    USHORT ImageSubsystem;
    USHORT ImageDllCharacteristics;
    // App ID
    PPH_STRING AppIdText;
    // DPI awareness
    ULONG DpiAwareness;
    // File attributes
    LARGE_INTEGER FileLastWriteTime;
    LARGE_INTEGER FileEndOfFile;
    // Critical
    BOOLEAN BreakOnTerminationEnabled;

    PPH_STRING TooltipText;
    ULONG64 TooltipTextValidToTickCount;

    // Text buffers
    WCHAR CpuUsageText[PH_INT32_STR_LEN_1 + 3];
    WCHAR IoTotalRateText[PH_INT32_STR_LEN_1 + 3];
    WCHAR PrivateBytesText[PH_INT32_STR_LEN_1];
    PPH_STRING PeakPrivateBytesText;
    PPH_STRING WorkingSetText;
    PPH_STRING PeakWorkingSetText;
    WCHAR PrivateWsText[PH_INT32_STR_LEN_1];
    PPH_STRING SharedWsText;
    PPH_STRING ShareableWsText;
    PPH_STRING VirtualSizeText;
    PPH_STRING PeakVirtualSizeText;
    PPH_STRING PageFaultsText;
    WCHAR BasePriorityText[PH_INT32_STR_LEN_1];
    WCHAR ThreadsText[PH_INT32_STR_LEN_1 + 3];
    WCHAR HandlesText[PH_INT32_STR_LEN_1 + 3];
    WCHAR GdiHandlesText[PH_INT32_STR_LEN_1 + 3];
    WCHAR UserHandlesText[PH_INT32_STR_LEN_1 + 3];
    PPH_STRING IoRoRateText;
    PPH_STRING IoWRateText;
    WCHAR PagePriorityText[PH_INT32_STR_LEN_1];
    PPH_STRING StartTimeText;
    PPH_STRING TotalCpuTimeText;
    PPH_STRING KernelCpuTimeText;
    PPH_STRING UserCpuTimeText;
    PPH_STRING RelativeStartTimeText;
    PPH_STRING WindowTitleText;
    PPH_STRING CyclesText;
    PPH_STRING CyclesDeltaText;
    PPH_STRING ContextSwitchesText;
    PPH_STRING ContextSwitchesDeltaText;
    PPH_STRING PageFaultsDeltaText;
    PPH_STRING IoGroupText[PHPRTLC_IOGROUP_COUNT];
    PPH_STRING PagedPoolText;
    PPH_STRING PeakPagedPoolText;
    PPH_STRING NonPagedPoolText;
    PPH_STRING PeakNonPagedPoolText;
    PPH_STRING MinimumWorkingSetText;
    PPH_STRING MaximumWorkingSetText;
    PPH_STRING PrivateBytesDeltaText;
    PPH_STRING TimeStampText;
    PPH_STRING FileModifiedTimeText;
    PPH_STRING FileSizeText;
    PPH_STRING SubprocessCountText;
    WCHAR JobObjectIdText[PH_INT32_STR_LEN_1];
    PPH_STRING ProtectionText;
    PPH_STRING DesktopInfoText;
    WCHAR PidHexText[PH_PTR_STR_LEN_1];

    // Graph buffers
    PH_GRAPH_BUFFERS CpuGraphBuffers;
    PH_GRAPH_BUFFERS PrivateGraphBuffers;
    PH_GRAPH_BUFFERS IoGraphBuffers;
// begin_phapppub
} PH_PROCESS_NODE, *PPH_PROCESS_NODE;
// end_phapppub

VOID PhProcessTreeListInitialization(
    VOID
    );

VOID PhInitializeProcessTreeList(
    _In_ HWND hwnd
    );

VOID PhLoadSettingsProcessTreeList(
    VOID
    );

VOID PhSaveSettingsProcessTreeList(
    VOID
    );

VOID PhLoadSettingsProcessTreeListEx(
    _In_ PPH_STRING TreeListSettings,
    _In_ PPH_STRING TreeSortSettings
    );

VOID PhSaveSettingsProcessTreeListEx(
    _Out_ PPH_STRING *TreeListSettings,
    _Out_ PPH_STRING *TreeSortSettings
    );

VOID PhReloadSettingsProcessTreeList(
    VOID
    );

// begin_phapppub
PHAPPAPI
struct _PH_TN_FILTER_SUPPORT *
NTAPI
PhGetFilterSupportProcessTreeList(
    VOID
    );
// end_phapppub

PPH_PROCESS_NODE PhAddProcessNode(
    _In_ PPH_PROCESS_ITEM ProcessItem,
    _In_ ULONG RunId
    );

// begin_phapppub
PHAPPAPI
PPH_PROCESS_NODE
NTAPI
PhFindProcessNode(
    _In_ HANDLE ProcessId
    );
// end_phapppub

VOID PhRemoveProcessNode(
    _In_ PPH_PROCESS_NODE ProcessNode
    );

// begin_phapppub
PHAPPAPI
VOID
NTAPI
PhUpdateProcessNode(
    _In_ PPH_PROCESS_NODE ProcessNode
    );
// end_phapppub

VOID PhTickProcessNodes(
    VOID
    );

// begin_phapppub
PHAPPAPI
PPH_PROCESS_ITEM
NTAPI
PhGetSelectedProcessItem(
    VOID
    );

PHAPPAPI
VOID
NTAPI
PhGetSelectedProcessItems(
    _Out_ PPH_PROCESS_ITEM **Processes,
    _Out_ PULONG NumberOfProcesses
    );

PHAPPAPI
VOID
NTAPI
PhDeselectAllProcessNodes(
    VOID
    );

PHAPPAPI
VOID
NTAPI
PhExpandAllProcessNodes(
    _In_ BOOLEAN Expand
    );

PHAPPAPI
VOID
NTAPI
PhInvalidateAllProcessNodes(
    VOID
    );

PHAPPAPI
VOID
NTAPI
PhSelectAndEnsureVisibleProcessNode(
    _In_ PPH_PROCESS_NODE ProcessNode
    );
// end_phapppub

VOID PhSelectAndEnsureVisibleProcessNodes(
    _In_ PPH_PROCESS_NODE *ProcessNodes,
    _In_ ULONG NumberOfProcessNodes
    );

PPH_LIST PhGetProcessTreeListLines(
    _In_ HWND TreeListHandle,
    _In_ ULONG NumberOfNodes,
    _In_ PPH_LIST RootNodes,
    _In_ ULONG Mode
    );

VOID PhCopyProcessTree(
    VOID
    );

VOID PhWriteProcessTree(
    _Inout_ PPH_FILE_STREAM FileStream,
    _In_ ULONG Mode
    );

// begin_phapppub
PHAPPAPI
PPH_LIST
NTAPI
PhDuplicateProcessNodeList(
    VOID
    );
// end_phapppub

#endif
