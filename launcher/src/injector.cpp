#include "injector.h"
#include <windows.h>
#include <iostream>
#include <string>

/**
 * @brief Injects a specified DLL into a target process using CreateRemoteThread and LoadLibraryA.
 * 
 * @param processId The PID of the target process.
 * @param dllPath The full path to the DLL to be injected.
 * @param pythonEnvPath path of python Dll embed environment.
 * @return true if injection was successful, false otherwise.
 */
bool Injector::InjectDLL(DWORD processId, const std::string& dllPath, const std::string& pythonEnvPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (hProcess == NULL) {
        return false;
    }

    // Obtain the physical addresses of the functions in Kernel32
    void* addr_SetDllDirectoryA = (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetDllDirectoryA");
    void* addr_LoadLibraryA     = (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");

    // ----------------------------------------------------------------
    // STEP A: FORCE SETDLLDIRECTORY IN THE REMOTE PROCESS (IAT Bypass)
    // ----------------------------------------------------------------
    void* remoteMemPython = VirtualAllocEx(hProcess, NULL, pythonEnvPath.length() + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remoteMemPython == NULL) {
        CloseHandle(hProcess);
        return false;
    }
    WriteProcessMemory(hProcess, remoteMemPython, pythonEnvPath.c_str(), pythonEnvPath.length() + 1, NULL);

    // create the intermediate remote thread to divert the Windows search radar
    HANDLE hThreadRuta = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)addr_SetDllDirectoryA, remoteMemPython, 0, NULL);
    if (hThreadRuta == NULL) {
        VirtualFreeEx(hProcess, remoteMemPython, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }
    WaitForSingleObject(hThreadRuta, INFINITE); // Secure block until radar is updated
    CloseHandle(hThreadRuta);
    VirtualFreeEx(hProcess, remoteMemPython, 0, MEM_RELEASE);

    // -------------------------------------------------------------------
    // STEP B: HOOKS.DLL INJECTION (WINDOWS ALREADY KNOWS WHERE PYTHON IS)
    // -------------------------------------------------------------------
    void* remoteMemDLL = VirtualAllocEx(hProcess, NULL, dllPath.length() + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remoteMemDLL == NULL) {
        CloseHandle(hProcess);
        return false;
    }
    WriteProcessMemory(hProcess, remoteMemDLL, dllPath.c_str(), dllPath.length() + 1, NULL);

    // Launch the loading thread for hybrid mod
    HANDLE hThreadDLL = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)addr_LoadLibraryA, remoteMemDLL, 0, NULL);
    DWORD exitCodeDLL = 0;

    if (hThreadDLL != NULL) {
        WaitForSingleObject(hThreadDLL, INFINITE);
        GetExitCodeThread(hThreadDLL, &exitCodeDLL); //  extract base address assigned by the RAM
        CloseHandle(hThreadDLL);
    }

    VirtualFreeEx(hProcess, remoteMemDLL, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    // If the CPU returns 0, LoadLibraryA bounced due to missing dependencies
    return (exitCodeDLL != 0); 
}
