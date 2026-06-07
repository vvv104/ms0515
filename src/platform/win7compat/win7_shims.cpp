// Win7 SP1 compatibility shims for the static MSVC runtime.
//
// The Universal C Runtime that ships statically with VS 2022 calls a small
// number of Win8+ kernel32 functions internally — long-path-aware file
// opens go through CreateFile2 (Win8+).  That leaves a CreateFile2 import
// in our exes which fails to resolve on Win7 with
//
//     "The procedure entry point CreateFile2 could not be located in
//      the dynamic link library KERNEL32.dll"
//
// We can't change the CRT, but we CAN steal its IAT slot.  Define
// __imp_CreateFile2 as a regular data symbol that points at our own
// implementation.  The linker resolves the CRT's reference to our symbol
// before it ever consults kernel32.lib, so CreateFile2 drops out of the
// PE import table entirely and our thunk gets the call instead.
//
// Our implementation just unpacks the CREATEFILE2_EXTENDED_PARAMETERS
// struct back into the flat parameter list expected by CreateFileW
// (Windows 2000+), which on Win7 produces the same observable effect.
//
// Project-wide _WIN32_WINNT=0x0601 hides the Win8 struct we have to know
// about here, so override the floor for just this translation unit before
// including <windows.h>.

#undef _WIN32_WINNT
#undef WINVER
#undef NTDDI_VERSION
#define _WIN32_WINNT  0x0A00      // Win10
#define WINVER        0x0A00
#define NTDDI_VERSION 0x0A000000
#include <windows.h>

extern "C" HANDLE WINAPI Win7CreateFile2_thunk(
    LPCWSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    DWORD dwCreationDisposition,
    LPCREATEFILE2_EXTENDED_PARAMETERS pCreateExParams)
{
    DWORD flagsAndAttributes = 0;
    LPSECURITY_ATTRIBUTES sa = nullptr;
    HANDLE hTemplate = nullptr;
    if (pCreateExParams) {
        flagsAndAttributes = pCreateExParams->dwFileAttributes
                           | pCreateExParams->dwFileFlags
                           | pCreateExParams->dwSecurityQosFlags;
        sa = pCreateExParams->lpSecurityAttributes;
        hTemplate = pCreateExParams->hTemplateFile;
    }
    return CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, sa,
                       dwCreationDisposition, flagsAndAttributes, hTemplate);
}

extern "C" void* __imp_CreateFile2 =
    reinterpret_cast<void*>(&Win7CreateFile2_thunk);
