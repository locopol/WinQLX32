#ifndef WATCHER_H
#define WATCHER_H

#include <string>

class ProcessWatcher {
public:
    /**
     * @brief Finds the process ID of a given process name.
     * 
     * @param processName The name of the executable (e.g., "explorer.exe").
     * @return The process ID, or 0 if not found.
     */
    static int GetProcessId(const std::string& processName);
};

#endif // WATCHER_H
