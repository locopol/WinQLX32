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
#include "quake_common.h"
#include "winqlx_common.h"

// Emulated function G_AddEvent Bypass
// Replicated Function because doesn't exist in qagamex86.dll
// It works like original described in ioquakelive source code.
void __cdecl G_AddEvent(gentity_t* ent, int event, int eventParm) {
    int bits;

    // [QL] no zero-event guard: qagamex86.dll G_AddEvent (0x1006c650) has no
    // "if (!event)" check or "zero event added" G_Printf; it goes straight to
    // the client/entity branch.

    // clients need to add the event in playerState_t instead of entityState_t
    if (ent->client) {
        bits = ent->client->ps.externalEvent & EV_EVENT_BITS;
        bits = (bits + EV_EVENT_BIT1) & EV_EVENT_BITS;
        ent->client->ps.externalEvent = event | bits;
        ent->client->ps.externalEventParm = eventParm;
        // [QL] externalEventTime removed from playerState_t
    } else {
        bits = ent->s.event & EV_EVENT_BITS;
        bits = (bits + EV_EVENT_BIT1) & EV_EVENT_BITS;
        ent->s.event = event | bits;
        ent->s.eventParm = eventParm;
    }
    ent->eventTime = level->time;
}

void __cdecl SendServerCommand(void) {
    SV_SendServerCommand(NULL, "%s\n", Cmd_Args());
}

void __cdecl CenterPrint(void) {
    SV_SendServerCommand(NULL, "cp \"%s\"\n", Cmd_Args());
}

void __cdecl RegularPrint(void) {
    SV_SendServerCommand(NULL, "print \"%s\n\"\n", Cmd_Args());
}

void __cdecl Slap(void) {
    int dmg = 0;
    int argc = Cmd_Argc();
    if (argc < 2) {
        Com_Printf("Usage: %s <client_id> [damage]\n", Cmd_Argv(0));
        return;
    }
    int i = atoi(Cmd_Argv(1));
    if (i < 0 || i >= sv_maxclients->integer) {
        Com_Printf("client_id must be a number between 0 and %d\n.", sv_maxclients->integer - 1);
        return;
    }
    else if (argc > 2)
        dmg = atoi(Cmd_Argv(2));

    if (g_entities[i].inuse && g_entities[i].health > 0) {
        Com_Printf("Slapping...\n");
        if (dmg)
            SV_SendServerCommand(NULL, "print \"%s^7 was slapped for %d damage!\n\"\n", svs->clients[i].name, dmg);
        else
            SV_SendServerCommand(NULL, "print \"%s^7 was slapped!\n\"\n", svs->clients[i].name);
        // RandomFloatWithNegative() * 200.0f;
        g_entities[i].client->ps.velocity[0] += ((float)rand() / (float)(RAND_MAX / 2) - 1) * 200.0f;
        g_entities[i].client->ps.velocity[1] += ((float)rand() / (float)(RAND_MAX / 2) - 1) * 200.0f;
        g_entities[i].client->ps.velocity[2] += 300.0f;
        g_entities[i].health -= dmg; // Will be 0 if argument wasn't passed.

        if (g_entities[i].health > 0) {
            G_AddEvent(&g_entities[i], EV_PAIN, 99); // 99 health = pain100_1.wav
        } else { 
            G_AddEvent(&g_entities[i], EV_GIB_PLAYER, g_entities[i].s.number);
            g_entities[i].health = 1;
        }
    }
    else
        Com_Printf("The player is currently not active.\n");
}

void __cdecl Slay(void) {
    int argc = Cmd_Argc();
    if (argc < 2) {
        Com_Printf("Usage: %s <client_id>\n", Cmd_Argv(0));
        return;
    }
    int i = atoi(Cmd_Argv(1));

    if (i < 0 || i >= sv_maxclients->integer) {
        Com_Printf("client_id must be a number between 0 and %d\n.", sv_maxclients->integer - 1);
        return;
    } else if (g_entities[i].inuse && g_entities[i].health > 0) {
        Com_Printf("Slaying player...\n");
        SV_SendServerCommand(NULL, "print \"%s^7 was slain!\n\"\n", svs->clients[i].name);
        DebugPrint("Slaying '%s'!\n", svs->clients[i].name);
        g_entities[i].health = -40;
        G_AddEvent(&g_entities[i], EV_GIB_PLAYER, g_entities[i].s.number);
        g_entities[i].health = 1;
    } else {
        Com_Printf("The player is currently not active.\n");
    }
}

void __cdecl DownloadWorkshopItem(void) { // different to steam_downloadugc as we defer the FS_Restart.
    int argc = Cmd_Argc();
    if (argc < 2) {
        Com_Printf("Usage: %s <workshop_id>\n", Cmd_Argv(0));
        return;
    }
    idSteamServer_DownloadItem(atoi(Cmd_Argv(1)), 1); // determined by cutter call from 0x004bbb1f (push 0x01, push ecx, push edx)
}

void __cdecl StopFollowing(void) {
    int argc = Cmd_Argc();
    if (argc < 2) {
        Com_Printf("Usage: %s <client_id>\n", Cmd_Argv(0));
        return;
    }

    int i = atoi(Cmd_Argv(1));
    if (i < 0 || i >= sv_maxclients->integer) {
        Com_Printf("client_id must be a number between 0 and %d\n.", sv_maxclients->integer - 1);
        return;
    }

    if (!g_entities[i].inuse) {
        Com_Printf("That player isn't currently active.\n");
        return;
    }

    if (g_entities[i].client->sess.spectatorState != SPECTATOR_FOLLOW) {
        Com_Printf("That player is not following anyone, current spectatorState == %d\n", g_entities[i].client->sess.spectatorState);
        return;
    }

    Com_Printf("Stopping player %d following player %d... ", i, g_entities[i].client->sess.spectatorClient);
    g_entities[i].client->sess.spectatorState = SPECTATOR_FREE;
    g_entities[i].client->ps.pm_flags &= ~PMF_FOLLOW;
    g_entities[i].r.svFlags &= ~SVF_BOT;
    g_entities[i].client->ps.clientNum = i;
    Com_Printf("Done.\n");
}

#ifndef NOPY
// Execute a pyminqlx command as if it were the owner executing it.
// Output will appear in the console.
void __cdecl PyRcon(void) {
    RconDispatcher(Cmd_Args());
}

void __cdecl PyCommand(void) {
    if (!custom_command_handler) {
        return; // No registered handler.
    }
    PyGILState_STATE gstate = PyGILState_Ensure();

    PyObject* result = PyObject_CallFunction(custom_command_handler, "s", Cmd_Args());
    if (result == Py_False) {
        Com_Printf("The command failed to be executed. pyminqlx found no handler.\n");
    }

    Py_XDECREF(result);
    PyGILState_Release(gstate);
}

void __cdecl RestartPython(void) {
    Com_Printf("Restarting Python...\n");
    if (PyMinqlx_IsInitialized())
        PyMinqlx_Finalize();
    PyMinqlx_Initialize();
    // minqlx initializes after the first new game starts, but since the game already
    // start, we manually trigger the event to make it initialize properly.
    NewGameDispatcher(0);
}
#endif