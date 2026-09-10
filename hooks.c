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
#include <stdarg.h>
#include <string.h>
#include <MinHook.h> 
#include "patterns.h"
#include "quake_common.h"
#include "winqlx_common.h" 

void* qagame_base = NULL;
void* qagame_entry = NULL;
uintptr_t hook_base;
qboolean skipFrameDispatcher;
uintptr_t* offset_rel; // for logging

void __cdecl My_Cmd_AddCommand(char* cmd, void* func) {
    if (!common_initialized) InitializeStatic();

    Cmd_AddCommand(cmd, func);
}

// REPLACEMENT FUNCTION FOR My_Sys_SetModuleOffset
void* __cdecl My_VM_Create(int name, unsigned int b, unsigned int c, int d) {

        void* result_vm_t = VM_Create(name, b, c, d);

        if (result_vm_t != NULL) {
            uintptr_t struct_address = (uintptr_t)result_vm_t;
            void* pp_struct = *(void**)(struct_address + 0x40);

            if (pp_struct != NULL && pp_struct != qagame_base) {
                qagame_base = pp_struct;
                DebugPrint("[VM] struct intercepted \t- Offset: %p\n", (void*)struct_address);
                DebugPrint("[VM] qagamex86.dll captured \t- Offset: %p\n", qagame_base);
                if (common_initialized) {
                    HookVm();
                    InitializeVm();
                    // patch pending, need more investigation
                    //patch_vm();
                }
            } else {
                return result_vm_t;
                //DebugPrint("[VM] > Nothing Intercepted <\n");
            }

        } else {
            DebugPrint("[VM] > Nothing Captured <\n");
        
        }
 
    // Return EAX pointer intact to the engine
    return result_vm_t; 
}

void __cdecl My_SV_SendServerCommand(client_t* cl, char* fmt, ...) {
    va_list argptr;
    char buffer[MAX_MSGLEN];

    va_start(argptr, fmt);
    vsnprintf((char *)buffer, sizeof(buffer), fmt, argptr);
    va_end(argptr);

    char* res = buffer;

    #ifndef NOPY
    if (cl && cl->gentity) {
        res = ServerCommandDispatcher(cl - svs->clients, buffer);
    } else if (cl == NULL) {
        res = ServerCommandDispatcher(-1, buffer);
    }

    if (!res) {
        return;
    }
    #endif
    SV_SendServerCommand(cl, "%s", res);
}

void __cdecl My_SV_ExecuteClientCommand(client_t* cl, char* s, qboolean clientOK) {
    
    char* res = s;

    #ifndef NOPY
    if (clientOK && cl->gentity) {
        res = ClientCommandDispatcher(cl - svs->clients, s);
        if (!res) {
            return;
        }
    } 
    #endif

    SV_ExecuteClientCommand(cl, s, clientOK);
}

void __cdecl My_SV_ClientEnterWorld(client_t* client, usercmd_t* cmd) {
    clientState_t state = client->state; // State before we call real one.
    SV_ClientEnterWorld(client, cmd);
    
    #ifndef NOPY
    // gentity is NULL if map changed.
    // state is CS_PRIMED only if it's the first time they connect to the server,
    // otherwise the dispatcher would also go off when a game starts and such.
    if (client->gentity != NULL && state == CS_PRIMED) {
        ClientLoadedDispatcher(client - svs->clients);
    }
    #endif

}

void __cdecl My_SV_SetConfigstring(int index, char* value) {
    // Indices 16 and 66X are spammed a ton every frame for some reason,
    // so we add some exceptions for those. I don't think we should have any
    // use for those particular ones anyway. If we don't do this, we get
    // like a 25% increase in CPU usage on an empty server.
    if (index == 16 || (index >= 662 && index < 670)) {
        SV_SetConfigstring(index, value);
        return;
    }

    if (!value) value = "";

    // WORKAROUND: Send Workshop config string to client, pending in server, WIP.
    //if (index == 715 && strlen(value) == 0)
    //    return;

    #ifndef NOPY // Required by CmakeFiles
    char* res = SetConfigstringDispatcher(index, value);

    // NULL means stop the event.
    if (res)
        SV_SetConfigstring(index, res);
    #else
    SV_SetConfigstring(index, value);
    #endif

}

void __cdecl My_SV_DropClient(client_t* drop, const char* reason) {
    int slot = (int)(drop - svs->clients);
    #ifndef NOPY
    ClientDisconnectDispatcher(drop - svs->clients, reason);
    #endif

    Demo_ClientDisconnect(slot); // finalize this client's demo, if any

    SV_DropClient(drop, reason);
}

void __cdecl My_Com_Printf(char* fmt, ...) {
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    #ifndef NOPY
    char* res = ConsolePrintDispatcher(buf);
    // NULL means stop the event.
    if (res)
        Com_Printf("%s", res);
    #else
    Com_Printf("%s",buf);
    #endif

}

void __cdecl My_SV_SpawnServer(char* server, qboolean killBots) {
    Demo_CloseAll();
    skipFrameDispatcher = qtrue;
    SV_SpawnServer(server, killBots);
    skipFrameDispatcher = qfalse;

    #ifndef NOPY
    // We call NewGameDispatcher here instead of G_InitGame when it's not just a map_restart,
    // otherwise configstring 0 and such won't be initialized and we can't instantiate minqlx.Game.
    NewGameDispatcher(qfalse);
    #endif
}

void __cdecl My_SV_SendMessageToClient(msg_t* msg, client_t* client) {
    Demo_Capture(msg, client);
    SV_SendMessageToClient(msg, client);
}

void __cdecl My_G_InitGame(int levelTime, int randomSeed, int restart) {
    Com_Printf("[minqlx] Started in G_InitGame\n");
    G_InitGame(levelTime, randomSeed, restart);

    if (!cvars_initialized) { // Only called once.
        SetTag();
    }
    InitializeCvars();

#ifndef NOPY
    if (restart) {
        NewGameDispatcher(restart);
    }
#endif
}

void __cdecl My_G_RunFrame(int time) {
    // Dropping frames is probably not a good idea, so we don't allow cancelling.
    #ifndef NOPY
    if (!skipFrameDispatcher) {
        // Skip running frame hooks while game is not initialized
        FrameDispatcher();
    }
    #endif

    G_RunFrame(time);

}

char* __cdecl My_ClientConnect(int clientNum, qboolean firstTime, qboolean isBot) {
    #ifndef NOPY
    if (firstTime) {
        char* res = ClientConnectDispatcher(clientNum, isBot);
        if (res && !isBot) {
            return res;
        }
    }
    #endif
    
    return ClientConnect(clientNum, firstTime, isBot);
}

void __cdecl My_ClientSpawn(gentity_t* ent) {
    ClientSpawn(ent);
    
    #ifndef NOPY
    // Since we won't ever stop the real function from being called,
    // we trigger the event after calling the real one. This will allow
    // us to set weapons and such without it getting overriden later.
    ClientSpawnDispatcher(ent - g_entities);
    #endif

}

void __cdecl My_G_StartKamikaze(gentity_t* ent) {
    int client_id, is_used_on_demand;

    if (ent->client) {
        // player activated kamikaze item
        ent->client->ps.eFlags &= ~EF_KAMIKAZE;
        client_id = ent->client->ps.clientNum;
        is_used_on_demand = 1;
    } else if (ent->activator) {
        // dead player's body blast
        client_id = ent->activator->r.ownerNum;
        is_used_on_demand = 0;
    } else {
        // I don't know
        client_id = -1;
        is_used_on_demand = 0;
    }

   #ifndef NOPY
    if (is_used_on_demand)
       KamikazeUseDispatcher(client_id);
    #endif

    G_StartKamikaze(ent);

    #ifndef NOPY
    if (client_id != -1)
        KamikazeExplodeDispatcher(client_id, is_used_on_demand);
    #endif
}

void HookStatic(void) {
    MH_STATUS res = 0;
    int failed = 0;
    hook_base = (uintptr_t)qlds_base;
 
    if (MH_Initialize() != MH_OK) {
        DebugPrint("[DLL] ERROR: MinHook initialization failed.\n");
        exit(1);

    } else {
  
        res = MH_Hook(addr_Cmd_AddCommand,&My_Cmd_AddCommand,(LPVOID*)&Cmd_AddCommand);
        if (res) {
            DebugPrint("ERROR: Failed to hook Cmd_AddCommand: %d\n", res);
            failed = 1;
        } else { DebugPrint("[DLL] Cmd_AddCommand \t- Offset: %p\n", (void*)offset_rel); }

        res = MH_Hook(addr_VM_Create,&My_VM_Create,(LPVOID*)&VM_Create);
        if (res) {
            DebugPrint("ERROR: Failed to hook VM_Create: %d\n", res);
            failed = 1;
        } else { DebugPrint("[DLL] VM_Create \t\t- Offset: %p\n", (void*)offset_rel); }

        // Moved here because is required to base tests
        res = MH_Hook(addr_Com_Printf,&My_Com_Printf,(LPVOID*)&Com_Printf);
        if (res) {
            DebugPrint("ERROR: Failed to hook Com_Printf: %d\n", res);
            failed = 1;
        } else { DebugPrint("[DLL] Com_Printf \t\t- Offset: %p\n", (void*)offset_rel); }

        #ifndef NOPY

        res = MH_Hook(addr_SV_SendMessageToClient,&My_SV_SendMessageToClient,(LPVOID*)&SV_SendMessageToClient);
        if (res) {
            DebugPrint("ERROR: Failed to hook SV_SendMessageToClient: %d\n", res);
            failed = 1;
        } else { DebugPrint("[DLL] SV_SendMessageToClient - Offset: %p\n", (void*)offset_rel); }

        res = MH_Hook(addr_SV_DropClient,&My_SV_DropClient,(LPVOID*)&SV_DropClient);
        if (res) {
            DebugPrint("ERROR: Failed to hook SV_DropClient: %d\n", res);
            failed = 1;
        } else { DebugPrint("[DLL] SV_DropClient \t\t- Offset: %p\n", (void*)offset_rel); }

        res = MH_Hook(addr_SV_SendServerCommand,&My_SV_SendServerCommand,(LPVOID*)&SV_SendServerCommand);
        if (res) {
            DebugPrint("ERROR: Failed to hook SV_SendServerCommand: %d\n", res);
            failed = 1;
        } else { DebugPrint("[DLL] SV_SendServerCommand \t- Offset: %p\n", (void*)offset_rel); }                

        res = MH_Hook(addr_SV_SpawnServer,&My_SV_SpawnServer,(LPVOID*)&SV_SpawnServer);
        if (res) {
            DebugPrint("ERROR: Failed to hook SV_SpawnServer: %d\n", res);
            failed = 1;
        } else { DebugPrint("[DLL] SV_SpawnServer \t- Offset: %p\n", (void*)offset_rel); }

        res = MH_Hook(addr_SV_ExecuteClientCommand,&My_SV_ExecuteClientCommand,(LPVOID*)&SV_ExecuteClientCommand);
        if (res) {
            DebugPrint("ERROR: Failed to hook SV_ExecuteClientCmd: %d\n", res);
            failed = 1;
        } else { DebugPrint("[DLL] SV_ExecuteClientCmd \t- Offset: %p\n", (void*)offset_rel); }       
 
        res = MH_Hook(addr_SV_SetConfigstring,&My_SV_SetConfigstring,(LPVOID*)&SV_SetConfigstring);
        if (res) {
            DebugPrint("ERROR: Failed to hook SV_SetConfigstring: %d\n", res);
            failed = 1;
        } else { DebugPrint("[DLL] SV_SetConfigstring \t- Offset: %p\n", (void*)offset_rel); }

        res = MH_Hook(addr_SV_ClientEnterWorld,&My_SV_ClientEnterWorld,(LPVOID*)&SV_ClientEnterWorld);
        if (res) {
            DebugPrint("ERROR: Failed to hook SV_ClientEnterWorld: %d\n", res);
            failed = 1;
        } else { DebugPrint("[DLL] SV_ClientEnterWorld \t- Offset: %p\n", (void*)offset_rel); }
   
        #endif

        if (failed) { DebugPrint("[DLL] Hooking proccess failed, exiting. \n"); exit(1); }

    }
}

/* 
 * Hooks VM calls. Not all use Hook, since the VM calls are stored in a table of
 * pointers. We simply set our function pointer to the current pointer in the table and
 * then replace the it with our replacement function. Just like hooking a VMT.
 * 
 * This must be called AFTER VM_Create, since VM_Create is called between
 * the VM DLL has been loaded, meaning the pointer we use has been set.
 *
 * PROTIP FROM MinoMino: If you can, ALWAYS use VM_Call table hooks instead of using Hook().
*/
void HookVm(void) {
    DWORD oldPerms;
    MH_STATUS res = 0;
    int failed = 0;
    int vmt_i = 0; // vm table index

    hook_base = (uintptr_t)qagame_base;

    if (qagame_base == NULL) return;

    // Init RelPointer of VM
    uintptr_t* vmt_table = (uintptr_t*)((uintptr_t)qagame_base + rel_VmCall_table);
    DebugPrint("[VMT] vm_t struct \t\t- Offset: %p\n",(void*)vmt_table);
    //vmt_audit(vmt_table); (Optional audit)
    
    vmt_i = 3;
    G_InitGame = (G_InitGame_ptr)vmt_table[vmt_i]; 

    // Override Pointer index
    if (VirtualProtect(&vmt_table[vmt_i], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldPerms)) {   
        vmt_table[vmt_i] = (uintptr_t)&My_G_InitGame;
        offset_rel = (void*)vmt_table[vmt_i];
            
        // Restore protect
        VirtualProtect(&vmt_table[vmt_i], sizeof(void*), oldPerms, &oldPerms);
        DebugPrint("[VMT] G_InitGame \t\t- Offset: %p\n",offset_rel);

    } else {
        DebugPrint("[VMT] ERROR: Windows denied index [%d] access.\n", vmt_i);
    }

    //LANAddress fix - function is missing in Win32, in this point the ips array is loaded for modify

    res = Sys_IsLANAddress();
    if (res) {
        DebugPrint("ERROR: Failed to apply LANAddress FIX: %d\n", res);
        failed = 1;
    }

    vmt_i = 1;
    G_RunFrame = (G_RunFrame_ptr)vmt_table[vmt_i]; 

    #ifndef NOPY

    // Override Pointer index
    if (VirtualProtect(&vmt_table[vmt_i], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldPerms)) {       
        vmt_table[vmt_i] = (uintptr_t)&My_G_RunFrame;
        offset_rel = (void*)vmt_table[vmt_i];
        
        // Restore protect
        VirtualProtect(&vmt_table[vmt_i], sizeof(void*), oldPerms, &oldPerms);

        DebugPrint("[VMT] G_RunFrame \t\t- Offset: %p\n",offset_rel);

    } else {
        DebugPrint("[VMT] ERROR: Windows denied index [%d] access.\n", vmt_i);
    }

    vmt_i = 8;
    ClientConnect = (ClientConnect_ptr)vmt_table[vmt_i]; 

    // Override Pointer index
    if (VirtualProtect(&vmt_table[vmt_i], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldPerms)) {       
        vmt_table[vmt_i] = (uintptr_t)&My_ClientConnect;
        offset_rel = (void*)vmt_table[vmt_i];
        
        // Restore protect
        VirtualProtect(&vmt_table[vmt_i], sizeof(void*), oldPerms, &oldPerms);

        DebugPrint("[VMT] ClientConnect \t\t- Offset: %p\n",offset_rel);

    } else {
        DebugPrint("[VMT] ERROR: Windows denied index [%d] access.\n", vmt_i);
    }
   
    res = MH_Hook(addr_G_StartKamikaze,&My_G_StartKamikaze,(LPVOID*)&G_StartKamikaze);
    if (res) {
        DebugPrint("ERROR: Failed to hook G_StartKamikaze: %d\n", res);
       failed = 1;
    } else { DebugPrint("[VM] G_StartKamikaze \t- Offset: %p\n", (void*)offset_rel); }


    res = MH_Hook(addr_ClientSpawn,&My_ClientSpawn,(LPVOID*)&ClientSpawn);
    if (res) {
        DebugPrint("ERROR: Failed to hook ClientSpawn: %d\n", res);
        failed = 1;
    } else { DebugPrint("[VM] ClientSpawn \t\t- Offset: %p\n", (void*)offset_rel); }

    if (failed) { DebugPrint("[VM] Hooking process failed, exiting.\n"); exit(1); }

    #endif

}

int MH_Hook(uintptr_t v_offset, void* v_my_fn, LPVOID v_target_ptr) {
    uintptr_t result = hook_base + v_offset;

    //WORKAROUND: force to remove hook to bypass race condition of regenerate hooks after VM_Create
    MH_RemoveHook((LPVOID)result); 

    MH_STATUS res = MH_CreateHook(
                    (LPVOID)result,     // Physical address function
                    (LPVOID)v_my_fn,    // Modified function
                    v_target_ptr        // Saved trampoline
        );

        if (res != MH_OK) {
            DebugPrint("[DLL] ERROR: can't make hook. code: %d\n", res);

        }

        // Enable the configured hook in the process
        res = MH_EnableHook((LPVOID)result);
        if (res == MH_OK) {
            offset_rel = (void*)result;
        } else {
            DebugPrint("[DLL] ERROR: can't enable memory hook.\n");
        }

    return res;

}

/////////////
// HELPERS //
/////////////

static void SetTag(void) {
    // Add minqlx tag.
    char tags[1024]; // Surely 1024 is enough?
    cvar_t* sv_tags = Cvar_FindVar("sv_tags");
    if (strlen(sv_tags->string) > 2) { // Does it already have tags?
        snprintf(tags, sizeof(tags), "sv_tags \"" SV_TAGS_PREFIX ",%s\"", sv_tags->string);
        Cbuf_ExecuteText(EXEC_INSERT, tags);
    }
    else {
        Cbuf_ExecuteText(EXEC_INSERT, "sv_tags \"" SV_TAGS_PREFIX "\"");
    }
}

static int Sys_IsLANAddress(void) {
    DWORD oldPerms;
    uint32_t* interfaces_ptr = (uint32_t*)((uintptr_t)qlds_entry + rel_DataLanAddress);
    uint32_t* ips_array = (uint32_t*)((uintptr_t)qlds_entry + rel_DataLanAddress + 0x04);
    //lanaddress_audit(interfaces_ptr,ips_array); // (Optional audit)
    if (VirtualProtect((LPVOID)interfaces_ptr, 16, PAGE_READWRITE, &oldPerms)) {    
        // ====================================================================
        // MINQLX EMULATED LAN BYPASS: 
        // To replace the isLANAddress function (not found in win32 binaries)
        // we place the universal address '0.0.0.0' (0x00000000) in slot 0
        // for the Quake engine, this wildcard interface will validate LAN
        // any burst of mod commands or quick network requests.
        // ====================================================================
        if (*interfaces_ptr > 0) {
            ips_array[0] = 0x00000000; // network wildcard mod
        } else {
            return 1;
        }

        // Restore protect
        VirtualProtect((LPVOID)interfaces_ptr, 16, oldPerms, &oldPerms);
        DebugPrint("[DLL] Sys_IsLANAddress FIX \t- Offset: %p\n", (void*)ips_array);
    } else {
        return 1;
    }
    //lanaddress_audit(interfaces_ptr,ips_array); // (Optional audit)
    return 0;
}
