/*
Copyright (C) 2026 Paul Asalgado <locopol@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "quake_common.h"
#include "winqlx_common.h" 

static char path_wks[MAX_PATH] = {0};
static char path_acf[MAX_PATH] = {0};
static char path_content[MAX_PATH] = {0};
cvar_t* cv_homepath; 
cvar_t* cv_basepath;

uint64_t workshop_path_check(const char* curr_path) {
    char pattr_mask[MAX_PATH];
    uint64_t t_bytes = 0;
    // Set pattern mask required by windows
    _snprintf(pattr_mask, sizeof(pattr_mask), "%s\\*", curr_path);

    WIN32_FIND_DATAA filedata;
    HANDLE hFind = FindFirstFileA(pattr_mask, &filedata);

    // if invalid, exit
    if (hFind == INVALID_HANDLE_VALUE) {
        return 0;
    }

    do {
        // discard relative points of directory paths
        if (strcmp(filedata.cFileName, ".") == 0 || strcmp(filedata.cFileName, "..") == 0)
            continue;

        // build path element
        char curr_elem[MAX_PATH];
        _snprintf(curr_elem, sizeof(curr_elem), "%s\\%s", curr_path, filedata.cFileName);

        // recursive loop finder
        if (filedata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            t_bytes += workshop_path_check(curr_elem);
        } 
        else {
            // Sum 64-bits integer (high|low)
            t_bytes += ((uint64_t)filedata.nFileSizeHigh << 32) | filedata.nFileSizeLow;
        }

    } while (FindNextFileA(hFind, &filedata));

    FindClose(hFind);
    return t_bytes;
}

DWORD WINAPI workshop_dir_polling(LPVOID lpParam) {
    qboolean success = qfalse;
    int iterloop = 0;
    const int max_ret = 60; 
    uint64_t oldsize = 0;
    int nogrow_loop = 0;

    while (!success && iterloop < max_ret) {
        iterloop++;
        // check directory size recursively
        uint64_t cursize = workshop_path_check(path_content);

        FILE* acf_file = fopen(path_acf, "r");
        uint64_t bytes_acf = 0;
        
        if (acf_file != NULL) {
            char fline[512];
            char buffer_size[64];
            memset(buffer_size, 0, sizeof(buffer_size));

            // Get global sizevalue of steamapps dir
            while (fgets(fline, sizeof(fline), acf_file)) {
                if (strstr(fline, "\"SizeOnDisk\"")) {
                    if (sscanf(fline, " \"SizeOnDisk\" \"%63[^\"]\"", buffer_size) == 1) {
                        bytes_acf = _strtoui64(buffer_size, NULL, 10);
                        break;
                    }
                }
            }
            fclose(acf_file);
        }

        // eval growing size of dir
        if (cursize > 0 && cursize == oldsize) {
            nogrow_loop++;
        } else {
            nogrow_loop = 0;
            DebugPrint("[DLL] Workshop dir: %llu Kb...\n", (cursize/1024));
        }

        if (nogrow_loop >= 3) { 
            if (cursize == bytes_acf) {
                success = qtrue;
                DebugPrint("[DLL] Reload Workshop Items...\n");
        
            } else {
                DebugPrint("Waiting...\n");
            }
        }

        oldsize = cursize;
        Sleep(500); 
    }

    // if finish, reload items, but the server require a full restart to operate correctly
    if (success)
        workshop_process();
    else
        DebugPrint("[DLL] Workshop failed, skipping...\n");

    return 0;
}

void workshop_dir_polling_thread(void) {
    cv_basepath = Cvar_Get("fs_basepath", "", 0);
    _snprintf(path_acf, sizeof(path_acf), "%s\\steamapps\\workshop\\appworkshop_282440.acf", cv_basepath->string);
    _snprintf(path_content, sizeof(path_content), "%s\\steamapps\\workshop\\content\\282440", cv_basepath->string);

    // Run dir polling to check steamapps content in parallel
    HANDLE hThread = CreateThread(NULL, 0, workshop_dir_polling, NULL, 0, NULL);

    if (hThread != NULL)
        CloseHandle(hThread);

}

qboolean workshop_check_content(void) {
    int filecount = 0;
    cv_basepath = Cvar_Get("fs_basepath", "", 0);
    _snprintf(path_content, sizeof(path_content), "%s\\steamapps\\workshop\\content\\282440", cv_basepath->string);
    DWORD f_attrib = GetFileAttributesA(path_content);

    // exit if directory not exist
    if (f_attrib == INVALID_FILE_ATTRIBUTES || !(f_attrib & FILE_ATTRIBUTE_DIRECTORY))
        return qfalse;

    // Check steamapps directory content
    char pattr_mask[MAX_PATH];
    _snprintf(pattr_mask, sizeof(pattr_mask), "%s\\*", path_content);
    
    WIN32_FIND_DATAA filedata;
    HANDLE hFind = FindFirstFileA(pattr_mask, &filedata);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            // discard relative points of directory paths
            if (strcmp(filedata.cFileName, ".") != 0 && strcmp(filedata.cFileName, "..") != 0) {
                filecount++;
            }

        } while (FindNextFileA(hFind, &filedata));
        
        FindClose(hFind);
    }

    if (filecount == 0)
        return qfalse;
    else
        return qtrue;

}

void workshop_process(void) {
    uint64_t workshop_id = 0;
    int fline;
    cv_homepath = Cvar_Get("fs_homepath", "", 0);
    _snprintf(path_wks, sizeof(path_wks), "%s\\baseq3\\workshop.txt", cv_homepath->string);
    FILE *fp = fopen(path_wks, "r");

    if (fp != NULL && sizeof(fp) > 0) {
        char frow[256];
        int rowcount = 0;
        int queued = 0;
        int checked = 0;

        Com_Printf("[WinQLX32] Loading workshop.txt...\n");

        while (fgets(frow, sizeof(frow), fp)) {
            rowcount++;
            if (frow[0] == '#' || frow[0] == '\n' || frow[0] == '\r' || frow[0] == '\0') {
                continue;
            }

            fline = sscanf(frow, "%llu", &workshop_id);
            if (fline == 1 && workshop_id > 0) {
                Com_Printf("[WinQLX32] ");
                int qres = idSteamServer_DownloadItem(workshop_id, qfalse);
                
                if (qres) {
                    DebugPrint("[DLL] Workshop item %llu requested.\n", workshop_id);
                    queued++;
                } else {
                    checked++;
                }
            } 
            else {
                strtok(frow, "\r\n");
                DebugPrint("[DLL] Failed line % ('%s'), discarted.\n", rowcount, frow);
            }
        }

        fclose(fp);
        if (queued > 0) {
            Com_Printf("[WinQLX32] Require restart after download, pending items: %d\n", queued);

        } else if (checked > 0) {
            DebugPrint("[DLL] Workshop items loaded: %d.\n", checked);
        }
    
    } // if WORKSHOP.TXT it doesn't exist, simple ignored silently

}


