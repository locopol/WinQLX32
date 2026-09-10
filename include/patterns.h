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

#ifndef PATTERNS_H
#define PATTERNS_H

// Direct Calls! (GetModuleHandleA(NULL) + offset))
#define addr_SV_Map_f 0xDDCE0
#define addr_SV_DropClient 0xDF660
#define addr_SV_SpawnServer 0xE3510
#define addr_SV_SetConfigstring 0xE2CC0
#define addr_SV_GetConfigstring 0xE2EC0
#define addr_SV_ClientEnterWorld 0xDFA20
#define addr_SV_SendServerCommand 0xE4170 
#define addr_SV_SendMessageToClient 0xe5900 
#define addr_SV_ExecuteClientCommand 0xE0090
#define addr_Com_Printf 0xC9860

#define addr_Cmd_Argc 0xc7ED0
#define addr_Cmd_Argv 0xc7EE0
#define addr_Cmd_Args 0xc7F40
#define addr_Cmd_AddCommand 0xc81D0

#define addr_CM_EntityString 0xC0250

#define addr_VM_Create 0xe9FF0 // THE KEY

// Direct Calls! (GetModuleHandleA(qagamex86) + offset))
#define rel_VmCall_table 0x8FD08
#define rel_DataLanAddress 0xED00B4

#define rel_pp_svs 0xF337A0
#define rel_pp_level 0x5DCE40
#define addr_g_entity 0x4B3FA0

#define addr_G_ClientConnect 0x3AC10
#define addr_G_StartKamikaze 0x6FF20
#define addr_ClientSpawn 0x3BC30

#define addr_Cvar_FindVar 0xCCD10
#define addr_Cbuf_ExecuteText 0xC8900
#define addr_idSteamServer_DownloadItem 0x699C0

#define addr_SV_Netchan_Transmit 0xe4ee0
#define addr_MSG_WriteBits 0xd4af0
#define addr_Cvar_Get 0xce0d0
#define addr_Cvar_Set2 0xcce90
#define addr_Cvar_GetLimit 0xCDD30 // hard to find, tjone270 ioquakelive doesn't have declared the function
#define addr_Cmd_ExecuteString 0xC8320

#define addr_G_FreeEntity 0x5a660 // ??
#define rel_pp_bg_itemlist 0x39730

/* PENDING!!
#define addr_G_AddEvent 0x6c660 // ??


¿Required?
#define LaunchItem ? 
#define G_Damage?

*/

#define prueba 0x66fbb
#endif /* PATTERNS_H */
