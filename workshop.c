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

// Declaramos las rutas globales para que el hilo secundario tenga acceso libre
static char g_path_manifiesto_acf[MAX_PATH] = {0};
static char g_path_content[MAX_PATH] = {0};
extern g_workshop_necesita_descarga;

/**
 * Sonda de Almacenamiento (Se ejecuta en paralelo)
 */
uint64_t WinQLX32_Calcular_Peso_Fisico_Directorio(const char* path_raiz) {
    char mascara_busqueda[MAX_PATH];
    // Aseguramos el formateo de hardware con el comodín de Windows
    _snprintf(mascara_busqueda, sizeof(mascara_busqueda), "%s\\*", path_raiz);
    mascara_busqueda[sizeof(mascara_busqueda) - 1] = '\0';

    WIN32_FIND_DATAA datos_archivo;
    HANDLE hBusqueda = FindFirstFileA(mascara_busqueda, &datos_archivo);
    uint64_t acumulado_bytes = 0;

    if (hBusqueda == INVALID_HANDLE_VALUE) {
        // Si la folder 282440 aún no existe en el disco duro, retornamos 0 de forma segura
        return 0;
    }

    do {
        // Descartamos estrictamente los punteros de navegación del sistema operativo ("." y "..")
        if (strcmp(datos_archivo.cFileName, ".") == 0 || strcmp(datos_archivo.cFileName, "..") == 0) {
            continue;
        }

        // Construimos la ruta absoluta del elemento actual mapeado en la RAM
        char path_elemento_actual[MAX_PATH];
        _snprintf(path_elemento_actual, sizeof(path_elemento_actual), "%s\\%s", path_raiz, datos_archivo.cFileName);
        path_elemento_actual[sizeof(path_elemento_actual) - 1] = '\0';

        // COMPUERTA DE ENTRADA RECURSIVA: Si el elemento es una subcarpeta (ID de un mapa)...
        if (datos_archivo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Saltamos de forma atómica hacia adentro del sub-directorio para sumar sus archivos
            acumulado_bytes += WinQLX32_Calcular_Peso_Fisico_Directorio(path_elemento_actual);
        } 
        else {
            // Amalgamamos los bloques de 32-bits (High/Low) para reconstruir el entero de 64-bits
            uint64_t tamano_individual = ((uint64_t)datos_archivo.nFileSizeHigh << 32) | datos_archivo.nFileSizeLow;
            acumulado_bytes += tamano_individual;
        }

    } while (FindNextFileA(hBusqueda, &datos_archivo));

    FindClose(hBusqueda);
    return acumulado_bytes;
}

/**
 * TRABAJADOR ASÍNCRONO (Worker Thread): 
 * Executa el bucle de polling en paralelo sin congelar el hilo principal del juego.
 */
DWORD WINAPI WinQLX32_Hilo_Polling_Workshop(LPVOID lpParam) {
    uintptr_t engine_base = (uintptr_t)GetModuleHandleA(NULL);

    BOOL verificacion_exitosa = FALSE;
    int iteraciones_bucle = 0;
    const int max_intentos = 60; 

    // Variables de persistencia para medir el estancamiento del disco
    uint64_t peso_disco_anterior = 0;
    int ciclos_sin_crecimiento = 0;

    while (!verificacion_exitosa && iteraciones_bucle < max_intentos) {
        iteraciones_bucle++;
        uint64_t peso_disco_actual = WinQLX32_Calcular_Peso_Fisico_Directorio(g_path_content);

        FILE* archivo_acf = fopen(g_path_manifiesto_acf, "r");
        uint64_t bytes_esperados_acf = 0;
        
        if (archivo_acf != NULL) {
            char cadena_linea[512];
            char buffer_size[64];
            memset(buffer_size, 0, sizeof(buffer_size));

            while (fgets(cadena_linea, sizeof(cadena_linea), archivo_acf)) {
                if (strstr(cadena_linea, "\"SizeOnDisk\"")) {
                    if (sscanf(cadena_linea, " \"SizeOnDisk\" \"%63[^\"]\"", buffer_size) == 1) {
                        bytes_esperados_acf = _strtoui64(buffer_size, NULL, 10);
                        break;
                    }
                }
            }
            fclose(archivo_acf);
        }

        if (peso_disco_actual > 0 && peso_disco_actual == peso_disco_anterior) {
            ciclos_sin_crecimiento++;
        } else {
            ciclos_sin_crecimiento = 0;
            DebugPrint("[DLL] Workshop dir: %llu Kb...\n", (peso_disco_actual/1024));
        }

        if (ciclos_sin_crecimiento >= 3) { 
            if (peso_disco_actual == bytes_esperados_acf) {
                verificacion_exitosa = TRUE;
                DebugPrint("[DLL] Reload Workshop Items...\n");
        
            } else {
                DebugPrint("Waiting...\n");
            }
        }

        peso_disco_anterior = peso_disco_actual;

        Sleep(500); 
    }

    if (verificacion_exitosa) 
        WinQLX32_Ejecutar_Lectura_Y_Presentacion_Workshop();

    return 0;
}

/**
 * RUTINA ORIGINAL REFORMULADA: 
 * Prepara las variables locales y delega el bucle síncrono al hilo de fondo de Windows.
 */
void WinQLX32_Probar_Polling_Manifiesto_ACF(void) {
    char ruta_base_servidor[MAX_PATH];
    GetModuleFileNameA(NULL, ruta_base_servidor, MAX_PATH);
    char* separador = strrchr(ruta_base_servidor, '\\');
    if (separador) *separador = '\0';

    // Poblamos las rutas globales legibles por el hilo worker
    _snprintf(g_path_manifiesto_acf, sizeof(g_path_manifiesto_acf), "%s\\steamapps\\workshop\\appworkshop_282440.acf", ruta_base_servidor);
    _snprintf(g_path_content, sizeof(g_path_content), "%s\\steamapps\\workshop\\content\\282440", ruta_base_servidor);

    //Check workshop dir
    HANDLE hThread = CreateThread(NULL, 0, WinQLX32_Hilo_Polling_Workshop, NULL, 0, NULL);

    if (hThread != NULL)
        CloseHandle(hThread);

}

/**
 * Rutina de Auditoría de Hardware: Valida la existencia física y densidad de la 
 * carpeta contigua steamapps. Activa la bandera si se requiere intervención de red.
 */


void WinQLX32_Validar_Circuito_Workshop_Local(void) {
    char ruta_steamapps[MAX_PATH];
    
    // 1. OBTENEMOS LA RUTA FÍSICA RELATIVA CONTIGUA AL SERVIDOR
    GetModuleFileNameA(NULL, ruta_steamapps, MAX_PATH);
    char* last_slash = strrchr(ruta_steamapps, '\\');
    if (last_slash) {
        *last_slash = '\0'; // Nos paramos en el directorio raíz del ejecutable
    }
    
    // Apuntamos al nodo raíz del Workshop en el disco duro de Windows
    strcat(ruta_steamapps, "\\steamapps\\workshop\\content\\282440");

    // 2. INTERROGACIÓN DIRECTA AL KERNEL DE WINDOWS (GetFileAttributesA)
    DWORD atributos = GetFileAttributesA(ruta_steamapps);

    // COMPUERTA 1: Si los atributos devuelven INVALID o no es un directorio legítimo...
    if (atributos == INVALID_FILE_ATTRIBUTES || !(atributos & FILE_ATTRIBUTE_DIRECTORY)) {
        g_workshop_necesita_descarga = 1;
        return;
    }

    // COMPUERTA 2: Si el directorio existe, auditamos su tamaño/densidad física en caliente
    // Buscamos si existen subcarpetas de IDs de mapas mapeadas dentro de la folder 282440
    char mascara_busqueda[MAX_PATH];
    _snprintf(mascara_busqueda, sizeof(mascara_busqueda), "%s\\*", ruta_steamapps);
    
    WIN32_FIND_DATAA datos_busqueda;
    HANDLE hFind = FindFirstFileA(mascara_busqueda, &datos_busqueda);
    
    int total_archivos_encontrados = 0;

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            // Descartamos los punteros de navegación relativos del sistema operativo ( "." y ".." )
            if (strcmp(datos_busqueda.cFileName, ".") != 0 && strcmp(datos_busqueda.cFileName, "..") != 0) {
                total_archivos_encontrados++;
            }
        } while (FindNextFileA(hFind, &datos_busqueda));
        
        FindClose(hFind);
    }

    if (total_archivos_encontrados == 0) {
        g_workshop_necesita_descarga = 1;
    } else {
        g_workshop_necesita_descarga = 0;
    }
}

void WinQLX32_Ejecutar_Lectura_Y_Presentacion_Workshop(void) {
    // 1. RESOLUCIÓN DINÁMICA DE LA RUTA DEL SISTEMA
    cvar_t* cv_basepath = Cvar_Get("fs_basepath", "", 0);
    uint64_t workshop_id = 0;
    int fline;
    char path_txt[MAX_PATH];
    _snprintf(path_txt, sizeof(path_txt), "%s\\baseq3\\workshop.txt", cv_basepath->string);
    path_txt[sizeof(path_txt) - 1] = '\0';
    FILE *fp = fopen(path_txt, "r");

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
            Com_Printf("[DLL] Worshop items require restart after download, pending items: %d\n", queued);

        } else if (checked > 0) {
            DebugPrint("[DLL] Workshop items loaded: %d.\n", checked);
        }
    } 

}


