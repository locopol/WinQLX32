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
#include <MinHook.h> 

#include "patterns.h"
#include "quake_common.h"
#include "winqlx_common.h" 

/* THIS FILE ONLY INCLUDES AUDIT ROUTINES */
/* In Spanish, Sorry :D */
static void lanaddress_audit(uint32_t* ptr, uint32_t* array) {
    DebugPrint("==================================================\n");
    DebugPrint("[NETWORK_AUDIT] >>> LAN INTERFACES TEST <<<\n");
    DebugPrint("==================================================\n");
    DebugPrint("RAM Address Pointer:\t%p\n", (void*)ptr);
    DebugPrint("IPs Array RAM Address:\t%p\n", (void*)array);

    // MEDIDA DE SEGURIDAD ABSOLUTA: Validamos que Windows permita leer esa zona de datos
    if (ptr != NULL && !IsBadReadPtr(ptr, 4)) {
        
        // Extraemos la cantidad de tarjetas de red que detectó el juego
        uint32_t total_found = *ptr;
        DebugPrint("Local network interfaces detected by Engine: %u\n", total_found);

        // Si el motor guardó interfaces, volcamos sus IPs decodificándolas byte por byte
        if (total_found > 0 && total_found < 16) { // Filtro de cordura para no leer basura
            DebugPrint("[LIST OF IPs DECLARED AS NATIVE LANs]:\n");
            
            for (uint32_t i = 0; i < total_found; i++) {
                uint32_t raw_ip = array[i]; // Lee los 4 bytes de la IP de 32 bits
                
                // Decodificamos el formato binario (Little Endian) a los 4 octetos estándar
                unsigned char b1 = (raw_ip & 0xFF);
                unsigned char b2 = ((raw_ip >> 8) & 0xFF);
                unsigned char b3 = ((raw_ip >> 16) & 0xFF);
                unsigned char b4 = ((raw_ip >> 24) & 0xFF);

                DebugPrint("\tInterface [%u] -> Raw IP Hex: 0x%08X -> Text IP: %u.%u.%u.%u\n", 
                       i, raw_ip, b1, b2, b3, b4);
            }
        } else if (total_found == 0) {
            DebugPrint("\tThe engine reports 0 interfaces. (Empty loop or initializing).\n");
        } else {
            DebugPrint("\tWARN: The counter returned an inconsistent number (%u). Possible misalignment.\n", total_found);
        }
    } else {
        DebugPrint("ERROR: Network cell %p is inaccessible in this thread.\n", (void*)ptr);
    }
    DebugPrint("==================================================\n"); 

}

static void vmt_audit(uintptr_t* vmt) {
    DebugPrint("==================================================\n");
    DebugPrint("[VMT_AUDIT] >>> EXAMINATING VM TABLE <<<\n");
    DebugPrint("==================================================\n");
    DebugPrint("qagame_base captured: %p\n", qagame_base);
    DebugPrint("[Testing RAM Address: %p\n", (void*)vmt);
    DebugPrint("\tIndex 0 (G_Shutdown): %p\n", (void*)vmt[0]);
    DebugPrint("\tIndex 1 (G_RunFrame): %p\n", (void*)vmt[1]);
    DebugPrint("\tIndex 2 (G_Cvars):    %p\n", (void*)vmt[2]);
    DebugPrint("\tIndex 3 (G_InitGame): %p\n", (void*)vmt[3]);
    DebugPrint("==================================================\n");

}

static void vars_audit(msg_t *msg, client_t *client) {
      FILE* log_file = fopen("winqlx_msg_convention.log", "a");
      uintptr_t base = (uintptr_t)msg;

        // El tamaño de tu compilador convertido a la cantidad de celdas de 4 bytes
        // 158280 bytes / 4 = 39570 iteraciones
        unsigned int total_celdas = 8;

        fprintf(log_file, "==================================================\n");
        fprintf(log_file, "[WinQLX32] MAPEO CRUDO SECUENCIAL DE MSG_T\n");
        fprintf(log_file, "==================================================\n");
        fprintf(log_file, "Dirección Base del Cliente en RAM:   %p\n", (void*)base);
        fprintf(log_file, "Total celdas de 8 bytes a escanear:  %u\n", total_celdas);
        fprintf(log_file, "--------------------------------------------------\n");

   // BUCLE AUTOMATIZADO EN SALTOS DE 4 BYTES
    for (unsigned int i = 0; i < total_celdas; i++) {
        unsigned int offset_actual = i * 4;
        
        if (IsBadReadPtr((void*)(base + offset_actual), 4)) {
            fprintf(log_file, "  Offset +0x%02X | [MEMORIA_INACCESIBLE]\n", offset_actual);
            break;
        }

        unsigned int valor_crudo = *(unsigned int*)(base + offset_actual);

        fprintf(log_file, "  Offset +0x%02X (Decimal: %2u) | Valor Crudo: 0x%08X", offset_actual, offset_actual, valor_crudo);

        // MARCADORES VISUALES DE HARDWARE:
        if (offset_actual == 0x1C) {
            fprintf(log_file, " <-- Frontera Teórica Celda 8 (¿Padding o Registro?)");
        }
        
        // Si el valor crudo coincide con el rango de direcciones de tu ejecutable principal (qlds base)
        // o con el rango de los 2 MB de la pila (ej. 0x02XXXXXX), es un puntero de retorno de la CPU.
        if (valor_crudo >= 0x00400000 && valor_crudo <= 0x00A00000) {
            fprintf(log_file, " <-- [POSIBLE DIRECCIÓN DE RETORNO EIP NATIVA]");
        }


        fprintf(log_file, "\n");
    }

        fprintf(log_file, "==================================================\n");
        fprintf(log_file, "==================================================\n");
        fprintf(log_file, "[WinQLX32] EXAMEN DE CONVENCIÓN DE LLAMADAS\n");
        fprintf(log_file, "==================================================\n");
        fprintf(log_file, "qlds: %p\n",(void*)qlds_base);
        fprintf(log_file, "Valor leído de la Pila2 (msg_pasado_pila): %p\n", (void*)msg);
        fprintf(log_file, "-> overflowed: %d\n", msg->overflowed);
        fprintf(log_file, "-> oob:%d\n", msg->oob); 
        fprintf(log_file, "-> data:%p\n", (void*)msg->data);
        fprintf(log_file, "-> maxsize:%d\n", msg->maxsize);
        fprintf(log_file, "-> cursize:%d\n", msg->cursize);
        fprintf(log_file, "-> readcount:%d\n", msg->readcount);
        fprintf(log_file, "-> bit:%d\n", msg->bit);

    
      uintptr_t base_ram = (uintptr_t)client;

        // El tamaño de tu compilador convertido a la cantidad de celdas de 4 bytes
        // 158280 bytes / 4 = 39570 iteraciones
        total_celdas = sizeof(client_t) / 4;

        fprintf(log_file, "==================================================\n");
        fprintf(log_file, "[WinQLX32] MAPEO CRUDO SECUENCIAL DE CLIENT_T\n");
        fprintf(log_file, "==================================================\n");
        fprintf(log_file, "Dirección Base del Cliente en RAM:   %p\n", (void*)base_ram);
        fprintf(log_file, "Total celdas de 4 bytes a escanear:  %u\n", total_celdas);
        fprintf(log_file, "--------------------------------------------------\n");

        unsigned int* vector_datos = (unsigned int*)base_ram;

        // BUCLE AUTOMATIZADO CON INCREMENTOS DE 4 BYTES (INDEXADO POR i)
        for (unsigned int i = 0; i < total_celdas; i++) {
            // Cálculo del desplazamiento hexadecimal real desde el inicio del objeto
            unsigned int offset_actual = i * 4;

            // Lectura defensiva de la celda de memoria antes de imprimir
            if (IsBadReadPtr((void*)(base_ram + offset_actual), 4)) {
                fprintf(log_file, "  Offset +0x%05X (Decimal: %6u) | [MEMORIA_INACCESIBLE]\n", offset_actual, offset_actual);
                break; // Detener el volcado si topamos con un límite duro de página
            }

            unsigned int valor_crudo = vector_datos[i];

            // Formato estricto: Offset hexadecimal, Offset decimal y Valor en la RAM
            fprintf(log_file, "  Offset +0x%05X (Decimal: %6u) | Valor: 0x%08X", offset_actual, offset_actual, valor_crudo);

            // EVALUACIÓN FORENSE ASOCIADA: Si la celda contiene una string ASCII imprimible corta (como las de red o chat)
            // mostramos una previsualización de texto al final de la línea para facilitar tu inspección visual.
            unsigned char* as_bytes = (unsigned char*)&valor_crudo;
            if (as_bytes[0] >= 32 && as_bytes[0] <= 126 &&
                as_bytes[1] >= 32 && as_bytes[1] <= 126 &&
                as_bytes[2] >= 32 && as_bytes[2] <= 126 &&
                as_bytes[3] >= 32 && as_bytes[3] <= 126) {
                fprintf(log_file, " -> Text: '%c%c%c%c'", as_bytes[0], as_bytes[1], as_bytes[2], as_bytes[3]);
            }

            fprintf(log_file, "\n");
        }

    base = (uintptr_t)client;

    fprintf(log_file, "==================================================\n");
    fprintf(log_file, "[WinQLX32] EXAMEN DE CONVENCIÓN DE LLAMADAS\n");
    fprintf(log_file, "==================================================\n");
    fprintf(log_file, "qlds: %p\n",(void*)qlds_base);
    fprintf(log_file, "Valor leído de la Pila2 (client_pasado_pila): %p\n", (void*)client);
    
    // 2. AUDITORÍA DE SEGURIDAD EN CALIENTE
    fprintf(log_file, "-> Estado del jugador: %d\n", client->state);
    fprintf(log_file, "  [userinfo]:  %.16s \n", client->userinfo);
    fprintf(log_file, "  [reliableSequence]: %d \n", client->reliableSequence);    
    fprintf(log_file, " [reliableAcknowledge]:  %d\n", client->reliableAcknowledge);
    fprintf(log_file, " [lastUsercmd.serverTime]:%d \n", client->lastUsercmd.serverTime);
    fprintf(log_file, " [downloadBlockSize]:%d \n",  (unsigned int)client->downloadBlockSize);
    fprintf(log_file, " [gentity*]:%p \n", (void*)client->gentity);
    fprintf(log_file, " [name]:%s \n", client->name);
    fprintf(log_file, " [downloadSendTime]:%d \n", client->downloadSendTime);
    fprintf(log_file, " [deltaMessage]:%d \n", client->deltaMessage);
    fprintf(log_file, " [lastPacketTime]:%d \n", client->lastPacketTime);
    fprintf(log_file, " [lastConnectTime]:%d \n", client->lastConnectTime);
    fprintf(log_file, " [nextSnapshotTime]:%d \n", client->nextSnapshotTime);
    fprintf(log_file, " [frames->areabytes]:%d \n", client->frames->areabytes);
    fprintf(log_file, " [frames->num_entities]:%d \n", client->frames->num_entities);
    fprintf(log_file, " [frames->first_entity]:%d \n", client->frames->first_entity);
    fprintf(log_file, " [frames->messageSent]:%d \n", client->frames->messageSent);
    fprintf(log_file, " [frames->messageAcked]:%d \n", client->frames->messageAcked);
    fprintf(log_file, " [frames->ps->clientNum]:%d \n", client->frames->ps.clientNum);
    fprintf(log_file, " [frames->ps->pm_type]:%d \n", client->frames->ps.pm_type);
    fprintf(log_file, " [frames->ps->weaponPrimary]:%d \n", client->frames->ps.weaponPrimary);                
    fprintf(log_file, " [usercmd_t->serverTime]:%d \n", client->lastUsercmd.serverTime);
    fprintf(log_file, "  +0x0004 [angles[0] - Pitch]:     %d (raw: 0x%08X)\n", client->lastUsercmd.angles[0], *(unsigned int*)(base + 0x04));
    fprintf(log_file, "  +0x0008 [angles[1] - Yaw]:       %d (raw: 0x%08X)\n", client->lastUsercmd.angles[1], *(unsigned int*)(base + 0x08));
    fprintf(log_file, "  +0x000C [angles[2] - Roll]:      %d (raw: 0x%08X)\n", client->lastUsercmd.angles[2], *(unsigned int*)(base + 0x0C));
    fprintf(log_file, " [usercmd_t->buttons]:%d \n", client->lastUsercmd.buttons);
    fprintf(log_file, " [usercmd_t->weapon]:%d \n", client->lastUsercmd.weapon);
    fprintf(log_file, " [usercmd_t->weaponPrimary]:%d \n", client->lastUsercmd.weaponPrimary);
    fprintf(log_file, " [usercmd_t->fov]:%d \n", client->lastUsercmd.fov);
    fprintf(log_file, " [usercmd_t->forwardmove]:%d \n", client->lastUsercmd.forwardmove);
    fprintf(log_file, " [usercmd_t->rightmove]:%d \n", client->lastUsercmd.rightmove);
    fprintf(log_file, " [usercmd_t->upmove]:%d \n", client->lastUsercmd.upmove);
    fprintf(log_file, " [usercmd_t->doubleTap]:%d \n", client->lastUsercmd.doubleTap);
    fprintf(log_file, " [usercmd_t->_pad]:%d \n", client->lastUsercmd._pad);
    fprintf(log_file, " STEAMID: %lld\n", client->steam_id);
    fprintf(log_file, "==================================================\n\n");
            
    netchan_t* ptr_netchan = &client->netchan;
    uintptr_t dir_base_client = (uintptr_t)client;
    uintptr_t dir_calculada_netchan = (uintptr_t)ptr_netchan;
    unsigned int base2 = (unsigned int)(dir_calculada_netchan - dir_base_client);

        fprintf(log_file, "==================================================\n");
        fprintf(log_file, "[WinQLX32] DISECCIÓN DE CAMPOS EN NETCHAN_T\n");
        fprintf(log_file, "==================================================\n");
        fprintf(log_file, "Dirección Base de Netchan en RAM: %p\n", (void*)base);
        fprintf(log_file, "--------------------------------------------------\n");

        // --- BLOQUE INICIAL DE CONTROL DE CONEXIÓN ---
        fprintf(log_file, "  +0x0000 [sock]:                 %d (raw: 0x%08X)\n", 
                client->netchan.sock, (base2 + 0x00));
                
        fprintf(log_file, "  +0x0004 [dropped]:              %d (raw: 0x%08X)\n", 
                client->netchan.dropped, (base2 + 0x04));

        // --- SUBESTRUCTURA DE DIRECCIÓN IP (netadr_t) ---
        // Auditamos los primeros 3 casilleros de 4 bytes de remoteAddress
        fprintf(log_file, "  +0x0008 [remoteAddress (Cell 0)]: 0x%08X\n", (base2 + 0x08));
        fprintf(log_file, "  +0x000C [remoteAddress (Cell 1)]: 0x%08X\n", (base2 + 0x0C));
        fprintf(log_file, "  +0x0010 [remoteAddress (Cell 2)]: 0x%08X\n", (base2 + 0x10));
        fprintf(log_file, "  +0x001C [qport]:                %d (raw: 0x%08X)\n", client->netchan.qport, (base2 + 0x1C));

        // --- VARIABLES DE SECUENCIADOR ---
        fprintf(log_file, "  +0x0020 [incomingSequence]:     %d (raw: 0x%08X)\n", client->netchan.incomingSequence, (base2 + 0x20));      
        fprintf(log_file, "  +0x0024 [outgoingSequence]:     %d (raw: 0x%08X)\n", 
                client->netchan.outgoingSequence, (base2 + 0x24));

        // --- BUFFER DE FRAGMENTOS ENTRANTE ---
        fprintf(log_file, "  +0x0028 [fragmentSequence]:     %d (raw: 0x%08X)\n",  client->netchan.fragmentSequence, (base2 + 0x28));     
        fprintf(log_file, "  +0x002C [fragmentLength]:       %d (raw: 0x%08X)\n", client->netchan.fragmentLength, (base2 + 0x2C));      
        fprintf(log_file, "  +0x0030 [fragmentBuffer[0-3]]:   (raw: 0x%08X)\n", (base2 + 0x30));

        // --- CONTROL DE FRAGMENTOS SALIENTE (Fronteras Altas) ---
        fprintf(log_file, "  +0x8030 [unsentFragments]:      %d (raw: 0x%08X)\n", client->netchan.unsentFragments,(base2 + 0x8030));        
        fprintf(log_file, "  +0x8034 [unsentFragmentStart]:  %d (raw: 0x%08X)\n", client->netchan.unsentFragmentStart, (base2 + 0x8034));
        fprintf(log_file, "  +0x8038 [unsentLength]:         %d (raw: 0x%08X)\n", client->netchan.unsentLength, (base2 + 0x8038));          
        fprintf(log_file, "  +0x803C [unsentBuffer[0-3]]:     (raw: 0x%08X)\n", (base2 + 0x803C));
        fprintf(log_file, "==================================================\n\n");
        fclose(log_file);

}

int Check_Gentity(gentity_t* ent) {
    if (ent == NULL) return 0;

    uintptr_t base_address = (uintptr_t)ent;
    size_t bytes_validos = 0;
    
    // Escaneamos en bloques de 4 bytes (DWORDs) hasta un límite seguro de 2048 bytes
    // para encontrar dónde termina físicamente la asignación del Hunk del motor.
    for (size_t offset = 0; offset < 512; offset += 4) {
        if (IsBadReadPtr((void*)(base_address + offset), 4)) {
            // Encontramos el límite duro de la página de memoria protegida
            break; 
        }
        bytes_validos = offset + 4;
    }

    DebugPrint("[WinQLX32] Frontera pasiva detectada de forma segura: %u bytes.\n", bytes_validos);

  // 1. Validamos la dirección de entrada de forma estrictamente pasiva
    if (ent == NULL || IsBadReadPtr(ent, 4)) {
        DebugPrint("[WinQLX32] [ERROR] Puntero ent es inválido en la entrada del gancho.\n");
        return 0;
    }

    // 2. Calculamos el tamaño exacto del Hunk sin invocar funciones del OS que causen crash
    size_t tamano_real = bytes_validos;
    
    if (tamano_real > 0) {
        unsigned int total_celdas = tamano_real / 4;
        unsigned int* vector_datos = (unsigned int*)ent;

        DebugPrint("==================================================\n");
        DebugPrint("[WinQLX32] MAPEO PASIVO DE GENTITY (HEAP COMPATIBLE)\n");
        DebugPrint("==================================================\n");

        // 3. TU BUCLE NATIVO DE CELDAS REESCRITO PARA EVITAR CORRUPCIONES
        for (unsigned int i = 0; i < total_celdas; i++) {
            unsigned int offset_actual = i * 4;
            unsigned int valor_crudo = vector_datos[i];

            DebugPrint("idx: %d -  Offset +0x%05X (Decimal: %6u) | Valor: 0x%08X", i,offset_actual, offset_actual, valor_crudo);

            // Previsualización de texto ASCII limpio
            unsigned char* as_bytes = (unsigned char*)&valor_crudo;
            if (as_bytes[0] >= 32 && as_bytes[0] <= 126 &&
                as_bytes[1] >= 32 && as_bytes[1] <= 126 &&
                as_bytes[2] >= 32 && as_bytes[2] <= 126 &&
                as_bytes[3] >= 32 && as_bytes[3] <= 126) {
                DebugPrint(" -> Text: '%c%c%c%c'", as_bytes[0], as_bytes[1], as_bytes[2], as_bytes[3]);
            }
            DebugPrint("\n");
        }
        DebugPrint("==================================================\n");
    }
    return 1;
}