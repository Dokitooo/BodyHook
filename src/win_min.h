/*
 * win_min.h - the handful of Win32 declarations BodyHook needs.
 *
 * Used only when building WITHOUT the Windows SDK (e.g. clang-cl + lld-link
 * on Linux, which is how the prebuilt binaries were produced). When you build
 * with Visual Studio, <windows.h> is used instead and this file is ignored.
 */
#pragma once

typedef unsigned char       BYTE;
typedef unsigned short      WORD;
typedef unsigned long       DWORD;
typedef int                 BOOL;
typedef long                LONG;
typedef unsigned int        UINT;
typedef void               *LPVOID, *HANDLE, *HMODULE, *HINSTANCE;
typedef const void         *LPCVOID;
typedef char               *LPSTR;
typedef const char         *LPCSTR;
typedef unsigned long       SIZE_T, ULONG_PTR, DWORD_PTR;
typedef DWORD              *LPDWORD;
#if defined(__clang__)
typedef __builtin_va_list    va_list;
#else
typedef char               *va_list;
#endif
typedef int (__stdcall *FARPROC)(void);
typedef DWORD (__stdcall *LPTHREAD_START_ROUTINE)(LPVOID);

#define WINAPI __stdcall
#define TRUE  1
#define FALSE 0
#define NULL  ((void*)0)
#define INVALID_HANDLE_VALUE ((HANDLE)(long)-1)
#define MAX_PATH 260

#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0

#define GENERIC_WRITE          0x40000000
#define GENERIC_READ           0x80000000
#define OPEN_EXISTING          3
#define FILE_SHARE_READ        0x00000001
#define FILE_SHARE_WRITE       0x00000002
#define CREATE_ALWAYS          2
#define OPEN_ALWAYS            4
#define FILE_ATTRIBUTE_NORMAL  0x80
#define FILE_END               2
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)

#define PAGE_NOACCESS          0x01
#define PAGE_READONLY          0x02
#define PAGE_READWRITE         0x04
#define PAGE_WRITECOPY         0x08
#define PAGE_EXECUTE           0x10
#define PAGE_EXECUTE_READ      0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD             0x100
#define MEM_COMMIT             0x1000
#define MEM_RESERVE            0x2000
#define MEM_RELEASE            0x8000

typedef struct _MEMORY_BASIC_INFORMATION {
    LPVOID BaseAddress;
    LPVOID AllocationBase;
    DWORD  AllocationProtect;
    SIZE_T RegionSize;
    DWORD  State;
    DWORD  Protect;
    DWORD  Type;
} MEMORY_BASIC_INFORMATION;

typedef struct _FILETIME { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct _WIN32_FIND_DATAA {
    DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
    char cFileName[MAX_PATH]; char cAlternateFileName[14];
} WIN32_FIND_DATAA;

typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD nFileSizeHigh, nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA;
typedef struct _LARGE_INTEGER { DWORD LowPart; LONG HighPart; } LARGE_INTEGER;

typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME;

typedef struct _CRITICAL_SECTION {
    LPVOID DebugInfo;
    LONG   LockCount;
    LONG   RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    ULONG_PTR SpinCount;
} CRITICAL_SECTION;

typedef struct _IMAGE_DOS_HEADER { WORD e_magic; BYTE pad[58]; LONG e_lfanew; } IMAGE_DOS_HEADER;
typedef struct _IMAGE_FILE_HEADER {
    WORD Machine; WORD NumberOfSections; DWORD TimeDateStamp; DWORD PointerToSymbolTable;
    DWORD NumberOfSymbols; WORD SizeOfOptionalHeader; WORD Characteristics;
} IMAGE_FILE_HEADER;
typedef struct _IMAGE_SECTION_HEADER {
    BYTE Name[8]; union { DWORD PhysicalAddress; DWORD VirtualSize; } Misc; DWORD VirtualAddress; DWORD SizeOfRawData;
    DWORD PointerToRawData; DWORD PointerToRelocations; DWORD PointerToLinenumbers;
    WORD NumberOfRelocations; WORD NumberOfLinenumbers; DWORD Characteristics;
} IMAGE_SECTION_HEADER;

__declspec(dllimport) HANDLE  WINAPI CreateThread(LPVOID, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
__declspec(dllimport) void    WINAPI Sleep(DWORD);
__declspec(dllimport) BOOL    WINAPI VirtualProtect(LPVOID, SIZE_T, DWORD, LPDWORD);
__declspec(dllimport) LPVOID  WINAPI VirtualAlloc(LPVOID, SIZE_T, DWORD, DWORD);
__declspec(dllimport) BOOL    WINAPI VirtualFree(LPVOID, SIZE_T, DWORD);
__declspec(dllimport) SIZE_T  WINAPI VirtualQuery(LPCVOID, MEMORY_BASIC_INFORMATION*, SIZE_T);
__declspec(dllimport) HMODULE WINAPI GetModuleHandleA(LPCSTR);
__declspec(dllimport) DWORD   WINAPI GetModuleFileNameA(HMODULE, LPSTR, DWORD);
__declspec(dllimport) HMODULE WINAPI LoadLibraryA(LPCSTR);
__declspec(dllimport) HMODULE WINAPI LoadLibraryExA(LPCSTR, HANDLE, DWORD);
__declspec(dllimport) FARPROC WINAPI GetProcAddress(HMODULE, LPCSTR);
__declspec(dllimport) HANDLE  WINAPI CreateFileA(LPCSTR, DWORD, DWORD, LPVOID, DWORD, DWORD, HANDLE);
__declspec(dllimport) BOOL    WINAPI WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, LPVOID);
__declspec(dllimport) BOOL    WINAPI CloseHandle(HANDLE);
__declspec(dllimport) BOOL    WINAPI FlushFileBuffers(HANDLE);
__declspec(dllimport) DWORD   WINAPI SetFilePointer(HANDLE, LONG, LONG*, DWORD);
__declspec(dllimport) UINT    WINAPI GetSystemDirectoryA(LPSTR, UINT);
__declspec(dllimport) UINT    WINAPI GetPrivateProfileIntA(LPCSTR, LPCSTR, int, LPCSTR);
__declspec(dllimport) DWORD   WINAPI GetPrivateProfileStringA(LPCSTR, LPCSTR, LPCSTR, LPSTR, DWORD, LPCSTR);
__declspec(dllimport) DWORD   WINAPI GetPrivateProfileSectionA(LPCSTR, LPSTR, DWORD, LPCSTR);
__declspec(dllimport) BOOL    WINAPI WritePrivateProfileStringA(LPCSTR, LPCSTR, LPCSTR, LPCSTR);
__declspec(dllimport) void    WINAPI GetLocalTime(SYSTEMTIME*);
__declspec(dllimport) void    WINAPI InitializeCriticalSection(CRITICAL_SECTION*);
__declspec(dllimport) void    WINAPI EnterCriticalSection(CRITICAL_SECTION*);
__declspec(dllimport) void    WINAPI LeaveCriticalSection(CRITICAL_SECTION*);
__declspec(dllimport) BOOL    WINAPI FlushInstructionCache(HANDLE, LPCVOID, SIZE_T);
__declspec(dllimport) HANDLE  WINAPI GetCurrentProcess(void);
__declspec(dllimport) DWORD   WINAPI GetCurrentThreadId(void);
__declspec(dllimport) DWORD   WINAPI GetTickCount(void);
__declspec(dllimport) HANDLE  WINAPI CreateMutexA(LPVOID, BOOL, LPCSTR);
__declspec(dllimport) DWORD   WINAPI GetLastError(void);
__declspec(dllimport) DWORD   WINAPI GetCurrentProcessId(void);
__declspec(dllimport) BOOL    WINAPI DisableThreadLibraryCalls(HMODULE);
__declspec(dllimport) DWORD   WINAPI GetFileAttributesA(LPCSTR);
__declspec(dllimport) HANDLE  WINAPI FindFirstFileA(LPCSTR, WIN32_FIND_DATAA*);
__declspec(dllimport) BOOL    WINAPI FindNextFileA(HANDLE, WIN32_FIND_DATAA*);
__declspec(dllimport) BOOL    WINAPI FindClose(HANDLE);
__declspec(dllimport) BOOL    WINAPI ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPVOID);
__declspec(dllimport) BOOL    WINAPI MoveFileExA(LPCSTR, LPCSTR, DWORD);
__declspec(dllimport) BOOL    WINAPI DeleteFileA(LPCSTR);
__declspec(dllimport) BOOL    WINAPI CreateDirectoryA(LPCSTR, LPVOID);
__declspec(dllimport) BOOL    WINAPI CopyFileA(LPCSTR, LPCSTR, BOOL);
#define MOVEFILE_REPLACE_EXISTING 1
__declspec(dllimport) BOOL    WINAPI GetFileAttributesExA(LPCSTR, int, LPVOID);
__declspec(dllimport) BOOL    WINAPI QueryPerformanceCounter(LARGE_INTEGER*);
__declspec(dllimport) BOOL    WINAPI QueryPerformanceFrequency(LARGE_INTEGER*);
__declspec(dllimport) int     WINAPI wvsprintfA(LPSTR, LPCSTR, va_list);

#if defined(__clang__)
#  define va_start(ap, last) __builtin_va_start(ap, last)
#  define va_end(ap)         __builtin_va_end(ap)
#else
#  define va_start(ap, last) ((ap) = (va_list)&(last) + ((sizeof(last) + 3) & ~3))
#  define va_end(ap)         ((ap) = (va_list)0)
#endif
