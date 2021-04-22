#ifndef _PH_PHCONFIG_H
#define _PH_PHCONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#define _User_set_

PHLIBAPI extern _User_set_ PVOID PhInstanceHandle;
PHLIBAPI extern _User_set_ PWSTR PhApplicationName;
PHLIBAPI extern _User_set_ ULONG PhGlobalDpi;
extern PVOID PhHeapHandle;
PHLIBAPI extern RTL_OSVERSIONINFOEXW PhOsVersion;
PHLIBAPI extern SYSTEM_BASIC_INFORMATION PhSystemBasicInformation;
PHLIBAPI extern ULONG WindowsVersion;

PHLIBAPI extern ACCESS_MASK ProcessQueryAccess;
PHLIBAPI extern ACCESS_MASK ProcessAllAccess;
PHLIBAPI extern ACCESS_MASK ThreadQueryAccess;
PHLIBAPI extern ACCESS_MASK ThreadSetAccess;
PHLIBAPI extern ACCESS_MASK ThreadAllAccess;

#define WINDOWS_ANCIENT 0
#define WINDOWS_XP 51
#define WINDOWS_VISTA 60
#define WINDOWS_7 61
#define WINDOWS_8 62
#define WINDOWS_8_1 63
#define WINDOWS_10 100 // TH1
#define WINDOWS_10_TH2 101
#define WINDOWS_10_RS1 102
#define WINDOWS_10_RS2 103
#define WINDOWS_10_RS3 104
#define WINDOWS_10_RS4 105
#define WINDOWS_10_RS5 106
#define WINDOWS_10_19H1 107
#define WINDOWS_10_19H2 108
#define WINDOWS_NEW ULONG_MAX

// Debugging

#ifdef DEBUG
#define dprintf(format, ...) DbgPrint(format, __VA_ARGS__)
#else
#define dprintf(format, ...)
#endif

// global

// Initialization flags

// Features

// Imports

#define PHLIB_INIT_MODULE_RESERVED1 0x1
#define PHLIB_INIT_MODULE_RESERVED2 0x2
#define PHLIB_INIT_MODULE_RESERVED3 0x4
#define PHLIB_INIT_MODULE_RESERVED4 0x8
#define PHLIB_INIT_MODULE_RESERVED5 0x10
#define PHLIB_INIT_MODULE_RESERVED6 0x20
#define PHLIB_INIT_MODULE_RESERVED7 0x40

PHLIBAPI
NTSTATUS
NTAPI
PhInitializePhLib(
    VOID
    );

PHLIBAPI
NTSTATUS
NTAPI
PhInitializePhLibEx(
    _In_ PWSTR Name,
    _In_ ULONG Flags,
    _In_ PVOID ImageBaseAddress,
    _In_opt_ SIZE_T HeapReserveSize,
    _In_opt_ SIZE_T HeapCommitSize
    );

PHLIBAPI
BOOLEAN
NTAPI
PhIsExecutingInWow64(
    VOID
    );

#ifdef __cplusplus
}
#endif

#endif
