/*
Copyright (C) 2015 Mino <mino@minomino.org>
Copyright (C) 2022-2026 Thomas Jones <me@thomasjones.id.au>
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
#include "patterns.h"
#include "quake_common.h"
#include "winqlx_common.h"

HINSTANCE  hModuleInstance;
void* qlds_base = NULL;
void* qlds_entry = NULL;

// Global variables.
int common_initialized = 0;
int cvars_initialized  = 0;
serverStatic_t* svs;

Com_Printf_ptr Com_Printf = NULL;
Cmd_AddCommand_ptr Cmd_AddCommand  = NULL;
Cmd_Args_ptr Cmd_Args  = NULL;
Cmd_Argv_ptr Cmd_Argv  = NULL;
Cmd_Argc_ptr Cmd_Argc  = NULL;
Cmd_TokenizeString_ptr Cmd_TokenizeString  = NULL;
Cbuf_ExecuteText_ptr Cbuf_ExecuteText  = NULL;
Cvar_FindVar_ptr Cvar_FindVar;
Cvar_Get_ptr Cvar_Get  = NULL;
Cvar_GetLimit_ptr Cvar_GetLimit  = NULL;
Cvar_Set2_ptr Cvar_Set2  = NULL;
SV_SendServerCommand_ptr SV_SendServerCommand  = NULL;
SV_ExecuteClientCommand_ptr SV_ExecuteClientCommand  = NULL;
SV_ClientEnterWorld_ptr SV_ClientEnterWorld  = NULL;
SV_Shutdown_ptr SV_Shutdown  = NULL;
SV_Map_f_ptr SV_Map_f  = NULL;
SV_SetConfigstring_ptr SV_SetConfigstring  = NULL;
SV_GetConfigstring_ptr SV_GetConfigstring  = NULL;
SV_DropClient_ptr SV_DropClient  = NULL;
SV_SendMessageToClient_ptr SV_SendMessageToClient = NULL;
MSG_WriteBits_ptr MSG_WriteBits;
SV_SpawnServer_ptr SV_SpawnServer  = NULL;
Cmd_ExecuteString_ptr Cmd_ExecuteString = NULL;
idSteamServer_DownloadItem_ptr idSteamServer_DownloadItem = NULL;
SV_Netchan_Transmit_ptr SV_Netchan_Transmit = NULL;

// VM functions
VM_Create_ptr VM_Create = NULL;
G_RunFrame_ptr G_RunFrame = NULL;
G_InitGame_ptr G_InitGame = NULL;
CheckPrivileges_ptr CheckPrivileges = NULL;
ClientConnect_ptr ClientConnect  = NULL;
ClientSpawn_ptr ClientSpawn = NULL;
G_Damage_ptr G_Damage = NULL;
Touch_Item_ptr Touch_Item = NULL;
LaunchItem_ptr LaunchItem = NULL;
Drop_Item_ptr Drop_Item = NULL;
G_StartKamikaze_ptr G_StartKamikaze = NULL;
G_FreeEntity_ptr G_FreeEntity = NULL;

// VM global variables.
gentity_t* g_entities;
level_locals_t* level;
gitem_t* bg_itemlist;
int bg_numItems;

// Cvars.
cvar_t* sv_maxclients;

// TODO: Make it output everything to a file too.
void DebugPrint(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf(DEBUG_PRINT_PREFIX);
    vprintf(fmt, args);
    va_end(args);
}

// TODO: Make it output everything to a file too.
void DebugError(const char* fmt, const char* file, int line, const char* func, ...) {
    va_list args;
    va_start(args, func);
    fprintf(stderr, DEBUG_ERROR_FORMAT, file, line, func);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

// Currently called by My_Cmd_AddCommand(), since it's called at a point where we
// can safely do whatever we do below. It'll segfault if we do it at the entry
// point, since functions like Cmd_AddCommand need initialization first.
void InitializeStatic(void) {

    //Initialize some key structure pointers before hooking
    //I put here the "svs" extraction to prevent Race Condition
    svs = (serverStatic_t*)(uintptr_t)((uintptr_t)qlds_base + rel_pp_svs);

    DebugPrint("[Main] serverStatic_t \t- Offset: %p\n", (void*)svs);
    DebugPrint("[Main] Init commands...\n");

    // Set the seed for our RNG.
    srand(time(NULL));

    Cmd_AddCommand("cmd", SendServerCommand);
    Cmd_AddCommand("cp", CenterPrint);
    Cmd_AddCommand("print", RegularPrint);
    Cmd_AddCommand("slap", Slap);
    Cmd_AddCommand("slay", Slay);
    Cmd_AddCommand("steam_downloadugcdefer", DownloadWorkshopItem);
    Cmd_AddCommand("stopfollowing", StopFollowing);
#ifndef NOPY
    Cmd_AddCommand("qlx", PyRcon);
    Cmd_AddCommand("pycmd", PyCommand);
    Cmd_AddCommand("pyrestart", RestartPython);
    // Initialize Python and run the main script.
    PyMinqlx_InitStatus_t res = PyMinqlx_Initialize();
    if (res != PYM_SUCCESS) {
        DebugPrint("Python initialization failed: %d\n", res);
        exit(1);
    }
#endif

    common_initialized = 1;
}

void InitializeVm(void) {

    DebugPrint("[Main] Init VM pointers...\n");
    if (qagame_base != NULL) {
        // Relative pointer assignment from qagame_base
        level = (level_locals_t*)((uintptr_t)qagame_base + rel_pp_level);
        g_entities = (gentity_t*)(uintptr_t)((uintptr_t)qagame_base + addr_g_entity);
        bg_itemlist = (gitem_t*)(uintptr_t)((uintptr_t)qagame_base + rel_pp_bg_itemlist + 0x21); // Can't be tested because not exist any dispatcher function to activate it.
    }
}

// Called after the game is initialized.
void InitializeCvars(void) {
    sv_maxclients = Cvar_FindVar("sv_maxclients");
    Demo_Init();
    cvars_initialized = 1;

}

// Secondary thread for safely initialization.
DWORD WINAPI MainThread(LPVOID lpParam) {

    if (!AttachConsole((DWORD)-1)) {
        return 0;
    } else {
 
    FILE* stream;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);

} 
    Sleep(300); // Sleep to avoid Race Condition
    DebugPrint("[Main] Loading\n");
    DebugPrint("[Main] qlds (exe) ...    Base Address: %p\n", (unsigned char*)qlds_base);
    
    /* STATIC INITIALIZATION OF RAW POINTERS (SEARCHMODULE IMPLEMENTATION PENDING)*/
    Com_Printf = (Com_Printf_ptr)((uintptr_t)qlds_entry + addr_Com_Printf);
    Cmd_AddCommand = (Cmd_AddCommand_ptr)((uintptr_t)qlds_entry + addr_Cmd_AddCommand);
    Cmd_Argc = (Cmd_Argc_ptr)((uintptr_t)qlds_entry + addr_Cmd_Argc);
    Cmd_Argv = (Cmd_Argv_ptr)((uintptr_t)qlds_entry + addr_Cmd_Argv);
    Cmd_Args = (Cmd_Args_ptr)((uintptr_t)qlds_entry + addr_Cmd_Args);
    Cbuf_ExecuteText = (Cbuf_ExecuteText_ptr)((uintptr_t)qlds_entry + addr_Cbuf_ExecuteText);
    Cvar_FindVar = (Cvar_FindVar_ptr)((uintptr_t)qlds_entry + addr_Cvar_FindVar);
    Cvar_Get = (Cvar_Get_ptr)((uintptr_t)qlds_entry + addr_Cvar_Get);
    Cvar_Set2 = (Cvar_Set2_ptr)((uintptr_t)qlds_entry + addr_Cvar_Set2);
    SV_GetConfigstring = (SV_GetConfigstring_ptr)((uintptr_t)qlds_entry + addr_SV_GetConfigstring);
    SV_SetConfigstring = (SV_SetConfigstring_ptr)((uintptr_t)qlds_entry + addr_SV_SetConfigstring); ///////
    idSteamServer_DownloadItem = (idSteamServer_DownloadItem_ptr)((uintptr_t)qlds_entry + addr_idSteamServer_DownloadItem);
    SV_SendMessageToClient = (SV_SendMessageToClient_ptr)((uintptr_t)qlds_entry + addr_SV_SendMessageToClient);
    MSG_WriteBits = (MSG_WriteBits_ptr)((uintptr_t)qlds_entry + addr_MSG_WriteBits);
    Cvar_GetLimit = (Cvar_GetLimit_ptr)((uintptr_t)qlds_entry + addr_Cvar_GetLimit);
    Cmd_ExecuteString = (Cmd_ExecuteString_ptr)((uintptr_t)qlds_entry + addr_Cmd_ExecuteString);
    G_FreeEntity = (G_FreeEntity_ptr)((uintptr_t)qlds_entry + addr_G_FreeEntity);
    //SV_Netchan_Transmit = (SV_Netchan_Transmit_ptr)((uintptr_t)qlds_entry + addr_SV_Netchan_Transmit);
    //G_AddEvent = MISSING!!!!! (Declared in commands.c directly to add events like original function)

    // Ready.
    HookStatic();
    return 0;
}   

// Official entry point for any dynamic library in Windows
BOOL WINAPI DllMain(HINSTANCE hModule, DWORD dwReason, LPVOID lpReserved) {
    switch (dwReason) {
        case DLL_PROCESS_ATTACH:

    HMODULE hQagame = GetModuleHandleA(NULL);

    if (hQagame != NULL && qlds_base == NULL) {
        qlds_base = (void*)hQagame;
        qlds_entry = (void*)hQagame; // only Win32, the base point to the beginning of PE header
    } 
            // Disable unnecessary thread calls to optimize performance
            DisableThreadLibraryCalls(hModule);
            
            // Make a native Windows thread to run WinQLX in background
            HANDLE hThread = CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
            if (hThread) {
                CloseHandle(hThread); // Close the handler, the thread is still alive
            }
            break;

        case DLL_PROCESS_DETACH:
            // If dll is unloaded, can clean hooks or anything else, nothing for now
            break;
    }
    return TRUE;
}
    