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

#ifndef WINQLX_COMMON_H
#define WINQLX_COMMON_H

#ifndef MINQLX_VERSION
#define MINQLX_VERSION "NOT_SET"
#endif

#define DEBUG_PRINT_PREFIX  "[WinQLX32] "
#define DEBUG_ERROR_FORMAT  "[WinQLX32] ERROR @ %s:%d in %s:\n" DEBUG_PRINT_PREFIX
#define WINQLX32_DLL_MODULE "winqlx.dll"

#ifndef NOPY
#define CORE_MODULE "minqlx.zip"
#define SV_TAGS_PREFIX "WinQLX32"
#else
#define SV_TAGS_PREFIX "WinQLX32-NOPY"
#endif

// TODO: Add minqlx version to serverinfo.

#include <stdint.h>

// We need an unsigned integer that's guaranteed to be the size of a pointer.
// "unsigned int" should do it, but we might want to port this to Windows for
// listen servers, where ints are 32 even on 64-bit Windows, so we're explicit.
#if defined(__i386) || defined(_M_IX86)
typedef uint32_t pint;
typedef int32_t sint;
#endif

#define _USE_MATH_DEFINES // Must come first!
#include <math.h>

extern const char* qagame;
extern void* qagame_base;
extern void* qagame_entry;
extern void* qlds_base;
extern void* qlds_entry;

extern int common_initialized;
extern int cvars_initialized;

void InitializeStatic(void);
void InitializeVm(void);
void InitializeCvars(void);
void SearchVmFunctions(void); // Needs to be called every time the VM is loaded.
void HookStatic(void);
void HookVm(void);
void DebugPrint(const char* fmt, ...);
void DebugError(const char* fmt, const char* file, int line, const char* func, ...);
static void SetTag(void); 

// New
int MH_Hook(uintptr_t, void*, LPVOID);
static int Sys_IsLANAddress(void);
static void vmt_audit(uintptr_t* vmt);
static void lanaddress_audit(uint32_t* ptr, uint32_t* array);

// Misc.
int GetPendingPlayer(uint64_t* players);
void SetPendingPlayer(uint64_t* players, int client_id);
float RandomFloat(void);
float RandomFloatWithNegative(void);
void* PatternSearch(void* address, size_t length, const char* pattern, const char* mask);

// Internal QL function pointer types.
typedef void(__cdecl *Com_Printf_ptr)(char *fmt, ...);
typedef void(__cdecl *Cmd_AddCommand_ptr)(char *cmd, void *func);
typedef char *(__cdecl *Cmd_Args_ptr)(void);
typedef char *(__cdecl *Cmd_Argv_ptr)(int arg);
typedef int(__cdecl *Cmd_Argc_ptr)(void);
typedef void(__cdecl *Cmd_TokenizeString_ptr)(const char *text_in);
typedef void(__cdecl *Cbuf_ExecuteText_ptr)(int exec_when, const char *text);
typedef cvar_t* (__cdecl* Cvar_FindVar_ptr)(const char *var_name);
typedef cvar_t *(__cdecl *Cvar_Get_ptr)(const char *var_name, const char *var_value, int flags);
typedef cvar_t *(__cdecl *Cvar_GetLimit_ptr)(const char *var_name, const char *var_value, const char *min, const char *max, int flag);
typedef cvar_t *(__cdecl *Cvar_Set2_ptr)(const char *var_name, const char *value, qboolean force);
typedef void(__cdecl *SV_SendServerCommand_ptr)(client_t *cl, const char *fmt, ...);
typedef void(__cdecl *SV_ExecuteClientCommand_ptr)(client_t *cl, const char *s, qboolean clientOK);
typedef void(__cdecl *SV_ClientEnterWorld_ptr)(client_t *client, usercmd_t *cmd);
typedef void(__cdecl *SV_Shutdown_ptr)(char *finalmsg);
typedef void(__cdecl *SV_Map_f_ptr)(void);
typedef void(__cdecl *SV_ClientThink_ptr)(client_t *cl, usercmd_t *cmd);
typedef void(__cdecl *SV_SetConfigstring_ptr)(int index, const char *value);
typedef void(__cdecl *SV_GetConfigstring_ptr)(int index, char *buffer, int bufferSize);
typedef void(__cdecl *SV_DropClient_ptr)(client_t *drop, const char *reason);
typedef void(__cdecl *FS_Startup_ptr)(const char *gameName);
typedef void(__cdecl *SV_LinkEntity_ptr)(sharedEntity_t *gEnt);
typedef void(__cdecl *SV_SpawnServer_ptr)(char *server, qboolean killBots);
typedef void(__cdecl *Cmd_ExecuteString_ptr)(const char *text);

typedef void* (__cdecl* VM_Create_ptr)(int name, unsigned int b, unsigned int c, int d); // Replacement of Sys_SetModuleOffset
typedef int(__cdecl *idSteamServer_DownloadItem_ptr)(uint64_t workshopId, qboolean defer);
typedef void(__cdecl *SV_SendMessageToClient_ptr)(msg_t *msg, client_t *client);
typedef void(__cdecl *SV_Netchan_Transmit_ptr)(client_t* client, msg_t* msg);
typedef void(__cdecl *MSG_WriteBits_ptr)(msg_t *msg, int value, int bits);

// Some of them are initialized by Initialize(), but not all of them necessarily.
extern Com_Printf_ptr Com_Printf;
extern Cmd_AddCommand_ptr Cmd_AddCommand;
extern Cmd_Args_ptr Cmd_Args;
extern Cmd_Argv_ptr Cmd_Argv;
extern Cmd_Argc_ptr Cmd_Argc;
extern Cmd_TokenizeString_ptr Cmd_TokenizeString;
extern Cbuf_ExecuteText_ptr Cbuf_ExecuteText;
extern Cvar_FindVar_ptr Cvar_FindVar;
extern Cvar_Get_ptr Cvar_Get;
extern Cvar_GetLimit_ptr Cvar_GetLimit;
extern Cvar_Set2_ptr Cvar_Set2;
extern SV_SendServerCommand_ptr SV_SendServerCommand;
extern SV_ExecuteClientCommand_ptr SV_ExecuteClientCommand;
extern SV_ClientEnterWorld_ptr SV_ClientEnterWorld;
extern SV_Shutdown_ptr SV_Shutdown;                         // Used to get svs pointer.
extern SV_Map_f_ptr SV_Map_f;                               // Used to get Cmd_Argc
extern SV_SetConfigstring_ptr SV_SetConfigstring;
extern SV_GetConfigstring_ptr SV_GetConfigstring;
extern SV_DropClient_ptr SV_DropClient;
extern SV_SpawnServer_ptr SV_SpawnServer;
extern Cmd_ExecuteString_ptr Cmd_ExecuteString;
extern MSG_WriteBits_ptr MSG_WriteBits;                     // used to append the Huffman svc_EOF
extern VM_Create_ptr VM_Create;                             // Replacement of Sys_SetModuleOffset

extern idSteamServer_DownloadItem_ptr idSteamServer_DownloadItem;
extern SV_Netchan_Transmit_ptr SV_Netchan_Transmit;
extern SV_SendMessageToClient_ptr SV_SendMessageToClient;

// VM functions.
typedef void(__cdecl *G_RunFrame_ptr)(int time);
typedef void(__cdecl *G_InitGame_ptr)(int levelTime, int randomSeed, int restart);
typedef int(__cdecl *CheckPrivileges_ptr)(gentity_t *ent, char *cmd);
typedef char *(__cdecl *ClientConnect_ptr)(int clientNum, qboolean firstTime, qboolean isBot);
typedef void(__cdecl *ClientSpawn_ptr)(gentity_t *ent);
typedef void(__cdecl *Cmd_CallVote_f_ptr)(gentity_t *ent);
typedef void(__cdecl *G_Damage_ptr)(gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, vec3_t dir, vec3_t point, int damage, int dflags, int mod);
typedef void(__cdecl *Touch_Item_ptr)(gentity_t *ent, gentity_t *other, trace_t *trace);
typedef gentity_t *(__cdecl *LaunchItem_ptr)(gitem_t *item, vec3_t origin, vec3_t velocity);
typedef gentity_t *(__cdecl *Drop_Item_ptr)(gentity_t *ent, gitem_t *item, float angle);
typedef void(__cdecl *G_StartKamikaze_ptr)(gentity_t *ent);
typedef void(__cdecl *G_FreeEntity_ptr)(gentity_t *ed);

// VM functions.
extern G_RunFrame_ptr G_RunFrame;
extern G_InitGame_ptr G_InitGame;
extern CheckPrivileges_ptr CheckPrivileges;
extern ClientConnect_ptr ClientConnect;
extern ClientSpawn_ptr ClientSpawn;
extern Cmd_CallVote_f_ptr Cmd_CallVote_f;
extern G_Damage_ptr G_Damage;
extern Touch_Item_ptr Touch_Item;
extern LaunchItem_ptr LaunchItem;
extern Drop_Item_ptr Drop_Item;
extern G_StartKamikaze_ptr G_StartKamikaze;
extern G_FreeEntity_ptr G_FreeEntity;

// Server replacement functions for hooks.
void __cdecl My_Cmd_AddCommand(char *cmd, void *func);
void* __cdecl My_VM_Create(int a, unsigned int b, unsigned int c, int d);
#ifndef NOPY
void __cdecl My_SV_ExecuteClientCommand(client_t *cl, char *s, qboolean clientOK);
void __cdecl My_SV_SendServerCommand(client_t *cl, char *fmt, ...);
void __cdecl My_SV_ClientEnterWorld(client_t *client, usercmd_t *cmd);
void __cdecl My_SV_SetConfigstring(int index, char *value);
void __cdecl My_SV_DropClient(client_t *drop, const char *reason);
void __cdecl My_Com_Printf(char *fmt, ...);
void __cdecl My_SV_SpawnServer(char *server, qboolean killBots);
// VM replacement functions for hooks.
void __cdecl My_G_RunFrame(int time);
void __cdecl My_G_InitGame(int levelTime, int randomSeed, int restart);
char *__cdecl My_ClientConnect(int clientNum, qboolean firstTime, qboolean isBot);
void __cdecl My_ClientSpawn(gentity_t *ent);
void __cdecl My_G_StartKamikaze(gentity_t *ent);
#endif

// Custom commands added using Cmd_AddCommand during initialization.
void __cdecl SendServerCommand(void);    // "cmd"
void __cdecl CenterPrint(void);          // "cp"
void __cdecl RegularPrint(void);         // "p"
void __cdecl Slap(void);                 // "slap"
void __cdecl Slay(void);                 // "slay"
void __cdecl DownloadWorkshopItem(void); // "steam_downloadugcdefer"
void __cdecl StopFollowing(void);        // "stopfollowing"

//Python stuff
// PyRcon gives the owner the ability to execute pyminqlx commands as if the
// owner executed them.
void __cdecl PyRcon(void);
// PyCommand is special. It'll serve as the handler for console commands added
// using Python. This means it can serve as the handler for a bunch of commands,
// and it'll take care of redirecting it to Python.
void __cdecl PyCommand(void);
void __cdecl RestartPython(void); // "pyrestart"

#ifndef NOPY

#include <Python.h>
#include <structmember.h>
#include <patchlevel.h>
#include <structseq.h>

typedef struct {
    char* name;
    PyObject** handler;
} handler_t;

/* Dispatchers. These are called by hooks or whatever and should dispatch events to Python handlers.
 * The return values will often determine what is passed on to the engine. You could for instance
 * implement a chat filter by returning 0 whenever bad words are said through the client_command event.
 * Hell, it could even be used to fix bugs in the server or client (e.g. a broken userinfo command or
 * broken UTF sequences that could crash clients). */
char* ClientCommandDispatcher(int client_id, char* cmd);
char* ServerCommandDispatcher(int client_id, char* cmd);
void FrameDispatcher(void);
char* ClientConnectDispatcher(int client_id, int is_bot);
int ClientLoadedDispatcher(int client_id);
void ClientDisconnectDispatcher(int client_id, const char* reason);
void NewGameDispatcher(int restart);
char* SetConfigstringDispatcher(int index, char* value);
void RconDispatcher(const char* cmd);
char* ConsolePrintDispatcher(char* cmd);
void ClientSpawnDispatcher(int client_id);

void KamikazeUseDispatcher(int client_id);
void KamikazeExplodeDispatcher(int client_id, int is_used_on_demand);

extern PyObject* client_command_handler;
extern PyObject* server_command_handler;
extern PyObject* client_connect_handler;
extern PyObject* client_loaded_handler;
extern PyObject* client_disconnect_handler;
extern PyObject* frame_handler;
extern PyObject* new_game_handler;
extern PyObject* set_configstring_handler;
extern PyObject* rcon_handler;
extern PyObject* console_print_handler;
extern PyObject* client_spawn_handler;

extern PyObject* kamikaze_use_handler;
extern PyObject* kamikaze_explode_handler;
extern PyObject* custom_command_handler;

extern int allow_free_client;

typedef enum {
    PYM_SUCCESS,
    PYM_PY_INIT_ERROR,
    PYM_MAIN_SCRIPT_ERROR,
    PYM_ALREADY_INITIALIZED,
    PYM_NOT_INITIALIZED_ERROR
} PyMinqlx_InitStatus_t;

// Used primarily in Python, but defined here and added using PyModule_AddIntMacro().
enum {
    RET_NONE,
    RET_STOP,       // Stop execution of event handlers within Python.
    RET_STOP_EVENT, // Only stop the event, but let other handlers process it.
    RET_STOP_ALL,   // Stop execution at an engine level. SCARY STUFF!
    RET_USAGE       // Used for commands. Replies to the channel with a command's usage.
};

enum {
    PRI_HIGHEST,
    PRI_HIGH,
    PRI_NORMAL,
    PRI_LOW,
    PRI_LOWEST
};

int PyMinqlx_IsInitialized(void);
PyMinqlx_InitStatus_t PyMinqlx_Initialize(void);
PyMinqlx_InitStatus_t PyMinqlx_Finalize(void);
#endif

#endif /* COMMON_H */
