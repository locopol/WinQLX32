#ifndef INJECTOR_H
#define INJECTOR_H

#include <windows.h>
#include <string>

class Injector {
public:
    /**
     * @brief Injects a specified DLL into a target process using CreateRemoteThread and LoadLibraryA.
     * 
     * @param processId The PID of the target process.
     * @param dllPath The full path to the DLL to be injected.
     * @return true if injection was successful, false otherwise.
     */
    static bool InjectDLL(DWORD processId, const std::string& dllPath, const std::string& pythonEnvPath);
};

#endif // INJECTOR_H
