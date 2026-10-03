#include <iostream>
#include <string>
#include <windows.h>
#include "watcher.h"
#include "injector.h"

enum class AppState {
    IDLE,
    INJECTING,
    ACTIVE,
    CLEANUP
};

std::string GetCommandLineArgs(std::string cmdLine) {
    LPWSTR lpCmdLine = GetCommandLineW();
    int numArgs = 0;
    LPWSTR* argList = CommandLineToArgvW(lpCmdLine, &numArgs);
    
    bool dedicated = false;

    if (argList != NULL) {
        // Omit the first argument (which is the path of launcher).
        for (int i = 1; i < numArgs; i++) {
            int len = WideCharToMultiByte(CP_UTF8, 0, argList[i], -1, NULL, 0, NULL, NULL);
            if (len > 0) {
                std::string arg(len, '\0');
                WideCharToMultiByte(CP_UTF8, 0, argList[i], -1, &arg[0], len, NULL, NULL);
                
                // Remove the trailing null character
                if (!arg.empty() && arg.back() == '\0') {
                    arg.pop_back();
                }

                // check and force param
                if (arg == "dedicated") {
                    dedicated = true;
                    cmdLine += " " + arg;
                    
                    // if dedicated param exists with "0", force to "1"
                    if (i + 1 < numArgs) {
                        int next_len = WideCharToMultiByte(CP_UTF8, 0, argList[i + 1], -1, NULL, 0, NULL, NULL);
                        if (next_len > 0) {
                            std::string next_arg(next_len, '\0');
                            WideCharToMultiByte(CP_UTF8, 0, argList[i + 1], -1, &next_arg[0], next_len, NULL, NULL);
                            if (!next_arg.empty() && next_arg.back() == '\0') {
                                next_arg.pop_back();
                            }

                             if (next_arg == "0")
                                cmdLine += " 1"; 
                             else
                                cmdLine += " " + next_arg;

                            i++; // Continue with rest params
                        }
                    }
                    continue;
                }

                cmdLine += " " + arg;
            }
        }
        LocalFree(argList);
    }

    if (!dedicated)
        cmdLine += " +set dedicated 1";

    return cmdLine;
}

int main() {
    std::string targetProcess = "quakelive_steam.exe";
    std::string dllPath = "winqlx.dll";
    DWORD targetPid = 0;
    HANDLE hProcess = NULL;
    AppState currentState = AppState::IDLE;

  char fullPathBuffer[MAX_PATH] = {0};
    
   // Convert any relative path entered to an actual absolute disk path
    if (GetFullPathNameA(dllPath.c_str(), MAX_PATH, fullPathBuffer, NULL) == 0) {
        std::cerr << "[Launcher] ERROR:Can resolve absolute path of DLL. Error: " << GetLastError() << std::endl;
        return 1;
    }

    std::string absoluteDllPath(fullPathBuffer);
    std::string pythonEnvPath = "";

    //extract the base directory of DLL
    size_t lastSlash = absoluteDllPath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        std::string dllDirectory = absoluteDllPath.substr(0, lastSlash);
        // WORKAROUND: fixed path of python embed relative to dll
        pythonEnvPath = dllDirectory + "\\deps\\python_embed";
    } else {
        std::cerr << "[Launcher] ERROR: Invalid path format of DLL." << std::endl;
        return 1;
    }

    std::cout << "---------------------------------------------------" << std::endl;
    std::cout << "[Launcher] Target Process: " << targetProcess << std::endl;
    std::cout << "[Launcher] Target DLL:     " << dllPath << std::endl;
    std::cout << "[Launcher] Python Env:     " << pythonEnvPath << std::endl; // Force embed Python env
    std::cout << "---------------------------------------------------" << std::endl;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    std::string commandLine = GetCommandLineArgs(targetProcess);

    // Check dll in relative path of launcher
    if(GetFileAttributesA(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::cerr << "Unable to find dll. exit code: " << GetLastError() << std::endl;
        return 1;
    }

    // CreateProcessA invokes binary in relative path of launcher.
    if (!CreateProcessA(
            targetProcess.c_str(),       // target process
            &commandLine[0],             // command and args
            NULL,                        
            NULL,                        
            FALSE,                       
            0,                           
            NULL,                        
            NULL,                        
            &si,                         // STARTUPINFO pointer struct
            &pi                          // PROCESS_INFORMATION poiner struct
        )) 
    {
        std::cerr << "Unable to run. exit code: " << GetLastError() << std::endl;
        return 1;
    }

    while (true) {
        switch (currentState) {
            case AppState::IDLE: {
                targetPid = pi.dwProcessId;
                if (targetPid > 0)
                    currentState = AppState::INJECTING;
                else
                    Sleep(1000); // Small sleep to prevent high CPU usage while polling
                
                break;
            }

            case AppState::INJECTING: {
                std::cout << "[Launcher] Attempting to inject DLL into PID: " << targetPid << "..." << std::endl;
                
                if (Injector::InjectDLL(targetPid, dllPath, pythonEnvPath)) {
                    std::cout << "[Launcher] Success!" << std::endl;
                    std::cout << "---------------------------------------------------" << std::endl;
                    
                    // Transition to ACTIVE: Open process to monitor it
                    hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, targetPid);
                    if (hProcess != NULL) {
                        currentState = AppState::ACTIVE;
                    } else {
                        std::cerr << "[Launcher] Error: Failed to open process for monitoring. Error: " << GetLastError() << std::endl;
                        currentState = AppState::CLEANUP;
                    }
                } else {
                    std::cerr << "[Launcher] Injection failed. Please check DLL path or run with sufficient permissions." << std::endl;
                    currentState = AppState::CLEANUP;
                }
                break;
            }

            case AppState::ACTIVE: {
                DWORD exitCode = 0;
                if (GetExitCodeProcess(hProcess, &exitCode)) {
                    if (exitCode != STILL_ACTIVE) {
                        std::cout << std::endl << "[Launcher] Process terminated, exit code: " << exitCode << std::endl;
                        currentState = AppState::CLEANUP;
                    }
                } else {
                    std::cerr << std::endl << "[Launcher] Error: Failed to check process exit code. Error: " << GetLastError() << std::endl;
                    currentState = AppState::CLEANUP;
                }
                
                if (currentState != AppState::CLEANUP) {
                    Sleep(1000); // Poll exit code every second
                }
                break;
            }

            case AppState::CLEANUP: {
                if (hProcess != NULL) {
                    CloseHandle(hProcess);
                    hProcess = NULL;
                }
                std::cout << "---------------------------------------------------" << std::endl;
                std::cout << "[Launcher] Exiting." << std::endl;
                return 0;
            }
        }
    }

    return 0;
}