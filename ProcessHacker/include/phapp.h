#ifndef PHAPP_H
#define PHAPP_H

#if !defined(_PHAPP_)
#define PHAPPAPI __declspec(dllimport)
#else
#define PHAPPAPI __declspec(dllexport)
#endif

#include <ph.h>
#include <guisup.h>
#include <provider.h>
#include <filestream.h>
#include <fastlock.h>
#include <treenew.h>
#include <graph.h>
#include <circbuf.h>
#include <dltmgr.h>
#include <phnet.h>

#include "../resource.h"
#include <phfwddef.h>
#include <appsup.h>

// main

typedef struct _PH_STARTUP_PARAMETERS
{
    union
    {
        struct
        {
            ULONG NoSettings : 1;
            ULONG ShowVisible : 1;
            ULONG ShowHidden : 1;
            ULONG CommandMode : 1;
            ULONG NoKph : 1;
            ULONG InstallKph : 1;
            ULONG UninstallKph : 1;
            ULONG Debug : 1;
            ULONG ShowOptions : 1;
            ULONG PhSvc : 1;
            ULONG NoPlugins : 1;
            ULONG NewInstance : 1;
            ULONG Elevate : 1;
            ULONG Silent : 1;
            ULONG Help : 1;
            ULONG Spare : 17;
        };
        ULONG Flags;
    };

    PPH_STRING SettingsFileName;

    HWND WindowHandle;
    POINT Point;

    PPH_STRING CommandType;
    PPH_STRING CommandObject;
    PPH_STRING CommandAction;
    PPH_STRING CommandValue;

    PPH_STRING RunAsServiceMode;

    ULONG SelectPid;
    ULONG PriorityClass;

    PPH_LIST PluginParameters;
    PPH_STRING SelectTab;
    PPH_STRING SysInfo;
} PH_STARTUP_PARAMETERS, *PPH_STARTUP_PARAMETERS;

extern BOOLEAN PhPluginsEnabled;
extern PPH_STRING PhSettingsFileName;
extern PH_STARTUP_PARAMETERS PhStartupParameters;

extern PH_PROVIDER_THREAD PhPrimaryProviderThread;
extern PH_PROVIDER_THREAD PhSecondaryProviderThread;

#define PH_SCALE_DPI(Value) PhMultiplyDivide(Value, PhGlobalDpi, 96) // phapppub

// begin_phapppub
PHAPPAPI
VOID
NTAPI
PhRegisterDialog(
    _In_ HWND DialogWindowHandle
    );

PHAPPAPI
VOID
NTAPI
PhUnregisterDialog(
    _In_ HWND DialogWindowHandle
    );

typedef BOOLEAN (NTAPI *PPH_MESSAGE_LOOP_FILTER)(
    _In_ PMSG Message,
    _In_ PVOID Context
    );

typedef struct _PH_MESSAGE_LOOP_FILTER_ENTRY
{
    PPH_MESSAGE_LOOP_FILTER Filter;
    PVOID Context;
} PH_MESSAGE_LOOP_FILTER_ENTRY, *PPH_MESSAGE_LOOP_FILTER_ENTRY;

PHAPPAPI
PPH_MESSAGE_LOOP_FILTER_ENTRY
NTAPI
PhRegisterMessageLoopFilter(
    _In_ PPH_MESSAGE_LOOP_FILTER Filter,
    _In_opt_ PVOID Context
    );

PHAPPAPI
VOID
NTAPI
PhUnregisterMessageLoopFilter(
    _In_ PPH_MESSAGE_LOOP_FILTER_ENTRY FilterEntry
    );
// end_phapppub

VOID PhInitializeFont(
    VOID
    );

// plugin

extern PH_AVL_TREE PhPluginsByName;

VOID PhInitializeCallbacks(
    VOID
    );

BOOLEAN PhIsPluginDisabled(
    _In_ PPH_STRINGREF BaseName
    );

VOID PhSetPluginDisabled(
    _In_ PPH_STRINGREF BaseName,
    _In_ BOOLEAN Disable
    );

VOID PhLoadPlugins(
    VOID
    );

VOID PhUnloadPlugins(
    VOID
    );

struct _PH_PLUGIN *PhFindPlugin2(
    _In_ PPH_STRINGREF Name
    );

// log

#define PH_LOG_ENTRY_PROCESS_FIRST 1
#define PH_LOG_ENTRY_PROCESS_CREATE 1
#define PH_LOG_ENTRY_PROCESS_DELETE 2
#define PH_LOG_ENTRY_PROCESS_LAST 2

#define PH_LOG_ENTRY_SERVICE_FIRST 3
#define PH_LOG_ENTRY_SERVICE_CREATE 3
#define PH_LOG_ENTRY_SERVICE_DELETE 4
#define PH_LOG_ENTRY_SERVICE_START 5
#define PH_LOG_ENTRY_SERVICE_STOP 6
#define PH_LOG_ENTRY_SERVICE_CONTINUE 7
#define PH_LOG_ENTRY_SERVICE_PAUSE 8
#define PH_LOG_ENTRY_SERVICE_LAST 8

#define PH_LOG_ENTRY_MESSAGE 9 // phapppub

typedef struct _PH_LOG_ENTRY *PPH_LOG_ENTRY; // phapppub

typedef struct _PH_LOG_ENTRY
{
    UCHAR Type;
    UCHAR Reserved1;
    USHORT Flags;
    LARGE_INTEGER Time;
    union
    {
        struct
        {
            HANDLE ProcessId;
            PPH_STRING Name;
            HANDLE ParentProcessId;
            PPH_STRING ParentName;
            NTSTATUS ExitStatus;
        } Process;
        struct
        {
            PPH_STRING Name;
            PPH_STRING DisplayName;
        } Service;
        PPH_STRING Message;
    };
    UCHAR Buffer[1];
} PH_LOG_ENTRY, *PPH_LOG_ENTRY;

extern PH_CIRCULAR_BUFFER_PVOID PhLogBuffer;

VOID PhLogInitialization(
    VOID
    );

VOID PhClearLogEntries(
    VOID
    );

VOID PhLogProcessEntry(
    _In_ UCHAR Type,
    _In_ HANDLE ProcessId,
    _In_opt_ HANDLE QueryHandle,
    _In_ PPH_STRING Name,
    _In_opt_ HANDLE ParentProcessId,
    _In_opt_ PPH_STRING ParentName
    );

VOID PhLogServiceEntry(
    _In_ UCHAR Type,
    _In_ PPH_STRING Name,
    _In_ PPH_STRING DisplayName
    );

// begin_phapppub
PHAPPAPI
VOID
NTAPI
PhLogMessageEntry(
    _In_ UCHAR Type,
    _In_ PPH_STRING Message
    );

PHAPPAPI
PPH_STRING
NTAPI
PhFormatLogEntry(
    _In_ PPH_LOG_ENTRY Entry
    );
// end_phapppub

// dbgcon

VOID PhShowDebugConsole(
    VOID
    );

// itemtips

PPH_STRING PhGetProcessTooltipText(
    _In_ PPH_PROCESS_ITEM Process,
    _Out_opt_ PULONG64 ValidToTickCount
    );

PPH_STRING PhGetServiceTooltipText(
    _In_ PPH_SERVICE_ITEM Service
    );

// cmdmode

NTSTATUS PhCommandModeStart(
    VOID
    );

// anawait

VOID PhUiAnalyzeWaitThread(
    _In_ HWND hWnd,
    _In_ HANDLE ProcessId,
    _In_ HANDLE ThreadId,
    _In_ PPH_SYMBOL_PROVIDER SymbolProvider
    );

// mdump

BOOLEAN PhUiCreateDumpFileProcess(
    _In_ HWND hWnd,
    _In_ PPH_PROCESS_ITEM Process
    );

// about

VOID PhShowAboutDialog(
    VOID
    );

PPH_STRING PhGetDiagnosticsString(
    VOID
    );

// affinity

VOID PhShowProcessAffinityDialog(
    _In_ HWND ParentWindowHandle,
    _In_opt_ PPH_PROCESS_ITEM ProcessItem,
    _In_opt_ PPH_THREAD_ITEM ThreadItem
    );

// begin_phapppub
PHAPPAPI
BOOLEAN
NTAPI
PhShowProcessAffinityDialog2(
    _In_ HWND ParentWindowHandle,
    _In_ ULONG_PTR AffinityMask,
    _Out_ PULONG_PTR NewAffinityMask
    );
// end_phapppub

// chcol

#define PH_CONTROL_TYPE_TREE_NEW 1

VOID PhShowChooseColumnsDialog(
    _In_ HWND ParentWindowHandle,
    _In_ HWND ControlHandle,
    _In_ ULONG Type
    );

// chdlg

// begin_phapppub
#define PH_CHOICE_DIALOG_SAVED_CHOICES 10

#define PH_CHOICE_DIALOG_CHOICE 0x0
#define PH_CHOICE_DIALOG_USER_CHOICE 0x1
#define PH_CHOICE_DIALOG_PASSWORD 0x2
#define PH_CHOICE_DIALOG_TYPE_MASK 0x3

PHAPPAPI
BOOLEAN
NTAPI
PhaChoiceDialog(
    _In_ HWND ParentWindowHandle,
    _In_ PWSTR Title,
    _In_ PWSTR Message,
    _In_opt_ PWSTR *Choices,
    _In_opt_ ULONG NumberOfChoices,
    _In_opt_ PWSTR Option,
    _In_ ULONG Flags,
    _Inout_ PPH_STRING *SelectedChoice,
    _Inout_opt_ PBOOLEAN SelectedOption,
    _In_opt_ PWSTR SavedChoicesSettingName
    );
// end_phapppub

// chproc

// begin_phapppub
PHAPPAPI
BOOLEAN
NTAPI
PhShowChooseProcessDialog(
    _In_ HWND ParentWindowHandle,
    _In_ PWSTR Message,
    _Out_ PHANDLE ProcessId
    );
// end_phapppub

// findobj

VOID PhShowFindObjectsDialog(
    VOID
    );

// gdihndl

VOID PhShowGdiHandlesDialog(
    _In_ HWND ParentWindowHandle,
    _In_ PPH_PROCESS_ITEM ProcessItem
    );

// hidnproc

VOID PhShowHiddenProcessesDialog(
    VOID
    );

// hndlprp

VOID PhShowHandleProperties(
    _In_ HWND ParentWindowHandle,
    _In_ HANDLE ProcessId,
    _In_ PPH_HANDLE_ITEM HandleItem
    );

// hndlstat

VOID PhShowHandleStatisticsDialog(
    _In_ HWND ParentWindowHandle,
    _In_ HANDLE ProcessId
    );

// infodlg

VOID PhShowInformationDialog(
    _In_ HWND ParentWindowHandle,
    _In_ PWSTR String,
    _Reserved_ ULONG Flags
    );

// jobprp

VOID PhShowJobProperties(
    _In_ HWND ParentWindowHandle,
    _In_ PPH_OPEN_OBJECT OpenObject,
    _In_opt_ PVOID Context,
    _In_opt_ PWSTR Title
    );

HPROPSHEETPAGE PhCreateJobPage(
    _In_ PPH_OPEN_OBJECT OpenObject,
    _In_opt_ PVOID Context,
    _In_opt_ DLGPROC HookProc
    );

// kdump

VOID PhShowLiveDumpDialog(
    _In_ HWND ParentWindowHandle
    );

// logwnd

VOID PhShowLogDialog(
    VOID
    );

// memedit

#define PH_MEMORY_EDITOR_UNMAP_VIEW_OF_SECTION 0x1

VOID PhShowMemoryEditorDialog(
    _In_ HWND OwnerWindow,
    _In_ HANDLE ProcessId,
    _In_ PVOID BaseAddress,
    _In_ SIZE_T RegionSize,
    _In_ ULONG SelectOffset,
    _In_ ULONG SelectLength,
    _In_opt_ PPH_STRING Title,
    _In_ ULONG Flags
    );

// memlists

VOID PhShowMemoryListsDialog(
    _In_ HWND ParentWindowHandle,
    _In_opt_ VOID (NTAPI *RegisterDialog)(HWND),
    _In_opt_ VOID (NTAPI *UnregisterDialog)(HWND)
    );

// memprot

VOID PhShowMemoryProtectDialog(
    _In_ HWND ParentWindowHandle,
    _In_ PPH_PROCESS_ITEM ProcessItem,
    _In_ PPH_MEMORY_ITEM MemoryItem
    );

// memrslt

VOID PhShowMemoryResultsDialog(
    _In_ HANDLE ProcessId,
    _In_ PPH_LIST Results
    );

// memsrch

VOID PhShowMemoryStringDialog(
    _In_ HWND ParentWindowHandle,
    _In_ PPH_PROCESS_ITEM ProcessItem
    );

// mtgndlg

VOID PhShowProcessMitigationPolicyDialog(
    _In_ HWND ParentWindowHandle,
    _In_ HANDLE ProcessId
    );

// netstk

VOID PhShowNetworkStackDialog(
    _In_ HWND ParentWindowHandle,
    _In_ PPH_NETWORK_ITEM NetworkItem
    );

// ntobjprp

HPROPSHEETPAGE PhCreateEventPage(
    _In_ PPH_OPEN_OBJECT OpenObject,
    _In_opt_ PVOID Context
    );

HPROPSHEETPAGE PhCreateEventPairPage(
    _In_ PPH_OPEN_OBJECT OpenObject,
    _In_opt_ PVOID Context
    );

HPROPSHEETPAGE PhCreateSemaphorePage(
    _In_ PPH_OPEN_OBJECT OpenObject,
    _In_opt_ PVOID Context
    );

HPROPSHEETPAGE PhCreateTimerPage(
    _In_ PPH_OPEN_OBJECT OpenObject,
    _In_opt_ PVOID Context
    );

// options

VOID PhShowOptionsDialog(
    _In_ HWND ParentWindowHandle
    );

// pagfiles

VOID PhShowPagefilesDialog(
    _In_ HWND ParentWindowHandle
    );

// plugman

VOID PhShowPluginsDialog(
    _In_ HWND ParentWindowHandle
    );

// procrec

// begin_phapppub
PHAPPAPI
VOID
NTAPI
PhShowProcessRecordDialog(
    _In_ HWND ParentWindowHandle,
    _In_ PPH_PROCESS_RECORD Record
    );
// end_phapppub

// runas

typedef struct _PH_RUNAS_SERVICE_PARAMETERS
{
    ULONG ProcessId;
    PWSTR UserName;
    PWSTR Password;
    ULONG LogonType;
    ULONG SessionId;
    PWSTR CurrentDirectory;
    PWSTR CommandLine;
    PWSTR FileName;
    PWSTR DesktopName;
    BOOLEAN UseLinkedToken;
    PWSTR ServiceName;
    BOOLEAN CreateSuspendedProcess;
} PH_RUNAS_SERVICE_PARAMETERS, *PPH_RUNAS_SERVICE_PARAMETERS;

VOID PhShowRunAsDialog(
    _In_ HWND ParentWindowHandle,
    _In_opt_ HANDLE ProcessId
    );

// begin_phapppub
PHLIBAPI
BOOLEAN
NTAPI
PhShowRunFileDialog(
    _In_ HWND ParentWindowHandle
    );
// end_phapppub

NTSTATUS PhExecuteRunAsCommand(
    _In_ PPH_RUNAS_SERVICE_PARAMETERS Parameters
    );

// begin_phapppub
PHAPPAPI
NTSTATUS
NTAPI
PhExecuteRunAsCommand2(
    _In_ HWND hWnd,
    _In_ PWSTR Program,
    _In_opt_ PWSTR UserName,
    _In_opt_ PWSTR Password,
    _In_opt_ ULONG LogonType,
    _In_opt_ HANDLE ProcessIdWithToken,
    _In_ ULONG SessionId,
    _In_ PWSTR DesktopName,
    _In_ BOOLEAN UseLinkedToken
    );
// end_phapppub

PHAPPAPI
NTSTATUS
NTAPI
PhExecuteRunAsCommand3(
    _In_ HWND hWnd,
    _In_ PWSTR Program,
    _In_opt_ PWSTR UserName,
    _In_opt_ PWSTR Password,
    _In_opt_ ULONG LogonType,
    _In_opt_ HANDLE ProcessIdWithToken,
    _In_ ULONG SessionId,
    _In_ PWSTR DesktopName,
    _In_ BOOLEAN UseLinkedToken,
    _In_ BOOLEAN CreateSuspendedProcess
    );

NTSTATUS PhRunAsServiceStart(
    _In_ PPH_STRING ServiceName
    );

NTSTATUS PhInvokeRunAsService(
    _In_ PPH_RUNAS_SERVICE_PARAMETERS Parameters
    );

// searchbox

// begin_phapppub
PHAPPAPI
VOID
NTAPI
PhCreateSearchControl(
    _In_ HWND Parent,
    _In_ HWND WindowHandle,
    _In_opt_ PWSTR BannerText
    );

PHAPPAPI
HBITMAP 
NTAPI
PhLoadPngImageFromResource(
    _In_ PVOID DllBase,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ PCWSTR Name,
    _In_ BOOLEAN RGBAImage
    );

PHAPPAPI
HBITMAP
NTAPI
PhLoadPngImageFromFile(
    _In_ PWSTR FileName,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ BOOLEAN RGBAImage
    );

FORCEINLINE
HFONT 
PhCreateFont(
    _In_ PWSTR Name,
    _In_ ULONG Size,
    _In_ ULONG Weight
    )
{
    return CreateFont(
        -(LONG)PhMultiplyDivide(Size, PhGlobalDpi, 72),
        0,
        0,
        0,
        Weight,
        FALSE,
        FALSE,
        FALSE,
        ANSI_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY,
        DEFAULT_PITCH,
        Name
        );
}

FORCEINLINE
HFONT
PhCreateCommonFont(
    _In_ LONG Size,
    _In_ INT Weight,
    _In_opt_ HWND hwnd
    )
{
    HFONT fontHandle;
    LOGFONT logFont;

    if (!SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof(LOGFONT), &logFont, 0))
        return NULL;

    fontHandle = CreateFont(
        -PhMultiplyDivideSigned(Size, PhGlobalDpi, 72),
        0,
        0,
        0,
        Weight,
        FALSE,
        FALSE,
        FALSE,
        ANSI_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY | ANTIALIASED_QUALITY,
        DEFAULT_PITCH,
        logFont.lfFaceName
        );

    if (!fontHandle)
        return NULL;

    if (hwnd)
        SetWindowFont(hwnd, fontHandle, TRUE);

    return fontHandle;
}

FORCEINLINE
HFONT
PhDuplicateFont(
    _In_ HFONT Font
    )
{
    LOGFONT logFont;

    if (GetObject(Font, sizeof(LOGFONT), &logFont))
        return CreateFontIndirect(&logFont);

    return NULL;
}

FORCEINLINE
HFONT
PhDuplicateFontWithNewWeight(
    _In_ HFONT Font,
    _In_ LONG NewWeight
    )
{
    LOGFONT logFont;

    if (GetObject(Font, sizeof(LOGFONT), &logFont))
    {
        logFont.lfWeight = NewWeight;
        return CreateFontIndirect(&logFont);
    }

    return NULL;
}

// end_phapppub

// sessmsg

VOID PhShowSessionSendMessageDialog(
    _In_ HWND ParentWindowHandle,
    _In_ ULONG SessionId
    );

// sessprp

VOID PhShowSessionProperties(
    _In_ HWND ParentWindowHandle,
    _In_ ULONG SessionId
    );

// sessshad

VOID PhShowSessionShadowDialog(
    _In_ HWND ParentWindowHandle,
    _In_ ULONG SessionId
    );

// srvcr

VOID PhShowCreateServiceDialog(
    _In_ HWND ParentWindowHandle
    );

// srvctl

// begin_phapppub
#define WM_PH_SET_LIST_VIEW_SETTINGS (WM_APP + 701)

PHAPPAPI
HWND
NTAPI
PhCreateServiceListControl(
    _In_ HWND ParentWindowHandle,
    _In_ PPH_SERVICE_ITEM *Services,
    _In_ ULONG NumberOfServices
    );
// end_phapppub

// srvprp

VOID PhShowServiceProperties(
    _In_ HWND ParentWindowHandle,
    _In_ PPH_SERVICE_ITEM ServiceItem
    );

// thrdstk

VOID PhShowThreadStackDialog(
    _In_ HWND ParentWindowHandle,
    _In_ HANDLE ProcessId,
    _In_ HANDLE ThreadId,
    _In_ PPH_THREAD_PROVIDER ThreadProvider
    );

// tokprp

PPH_STRING PhGetGroupAttributesString(
    _In_ ULONG Attributes,
    _In_ BOOLEAN Restricted
    );

PWSTR PhGetPrivilegeAttributesString(
    _In_ ULONG Attributes
    );

VOID PhShowTokenProperties(
    _In_ HWND ParentWindowHandle,
    _In_ PPH_OPEN_OBJECT OpenObject,
    _In_ HANDLE ProcessId,
    _In_opt_ PVOID Context,
    _In_opt_ PWSTR Title
    );

HPROPSHEETPAGE PhCreateTokenPage(
    _In_ PPH_OPEN_OBJECT OpenObject,
    _In_ HANDLE ProcessId,
    _In_opt_ PVOID Context,
    _In_opt_ DLGPROC HookProc
    );

// prpggen

PPH_STRING PhGetProcessItemProtectionText(
    _In_ PPH_PROCESS_ITEM ProcessItem
    );

#endif
