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

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <target_process_name> <dll_path>" << std::endl;
        std::cerr << "Example: " << argv[0] << " quakelive_steam.exe winqlx.dll" << std::endl;
        return 1;
    }

    std::string targetProcess = argv[1];
    std::string dllPath = argv[2];
    AppState currentState = AppState::IDLE;
    DWORD targetPid = 0;
    HANDLE hProcess = NULL;

  char fullPathBuffer[MAX_PATH] = {0};
    
   // We convert any relative path entered (e.g., "bin\hooks.dll") to an actual absolute disk path
    if (GetFullPathNameA(dllPath.c_str(), MAX_PATH, fullPathBuffer, NULL) == 0) {
        std::cerr << "[Launcher] ERROR:Can resolve absolute path of DLL. Error: " << GetLastError() << std::endl;
        return 1;
    }

    std::string absoluteDllPath(fullPathBuffer);
    std::string pythonEnvPath = "";

    //We extract the base directory where the DLL resides by shortening the filename
    size_t lastSlash = absoluteDllPath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        std::string dllDirectory = absoluteDllPath.substr(0, lastSlash);
        // We anchored the portable subfolder in the same physical neighborhood as the DLL
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

    while (true) {
        switch (currentState) {
            case AppState::IDLE: {
                targetPid = ProcessWatcher::GetProcessId(targetProcess);
                if (targetPid > 0) {
                    std::cout << "[Launcher] Target process found! PID: " << targetPid << std::endl;
                    currentState = AppState::INJECTING;
                } else {
                    // Small sleep to prevent high CPU usage while polling
                    Sleep(1000);
                }
                break;
            }

            case AppState::INJECTING: {
                std::cout << "[Launcher] Attempting to inject DLL into PID: " << targetPid << "..." << std::endl;
                
                if (Injector::InjectDLL(targetPid, dllPath, pythonEnvPath)) {
                    std::cout << "[Launcher] Success!" << std::endl;
                    
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
