/*
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
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "demos.h"
#include "quake_common.h"
#include "winqlx_common.h"

#ifdef _MSC_VER
#define pthread_sigmask(how, set, oset) (0)
#endif

extern serverStatic_t *svs; // defined in dllmain.c

#define SVC_EOF 8
#define MAX_DEMO_CLIENTS 64                // QL MAX_CLIENTS
#define DEMO_RING_SIZE (16u * 1024 * 1024) // power of two

typedef enum {
    DEMO_REC_OPEN = 1,  // payload: null-terminated file path
    DEMO_REC_BLOCK,     // payload: message bytes incl. Huffman svc_EOF
    DEMO_REC_CLOSE,     // no payload
    DEMO_REC_CLOSE_ALL, // no payload, slot ignored
    DEMO_REC_SHUTDOWN,  // no payload; writer finalises every open demo and exits
} demo_rec_type_t;

typedef struct {
    int32_t type; // demo_rec_type_t
    int32_t slot;
    int32_t seq;  // BLOCK only: netchan outgoingSequence
    uint32_t len; // payload bytes following this header
} demo_rec_hdr_t; // 16 bytes

typedef enum {
    DEMO_THREAD_STOPPED = 0,
    DEMO_THREAD_RUNNING,
    DEMO_THREAD_STOPPING, // writer still draining
} demo_thread_state_t;

static unsigned char demo_ring[DEMO_RING_SIZE];
static uint64_t demo_head; // advanced by the game thread.
static uint64_t demo_tail; // advanced by the writer thread.
CRITICAL_SECTION demo_lock;     // (win32)            
CONDITION_VARIABLE demo_cond;   // (win32)
static demo_thread_state_t demo_thread_state = DEMO_THREAD_STOPPED;

static demo_thread_state_t demo_state_cached = DEMO_THREAD_STOPPED;
static uint8_t demo_active[MAX_DEMO_CLIENTS]; // slot has an open segment.
static unsigned char demo_scratch[MAX_NETCHAN_MSGLEN + 64];

// Per-slot override of sv_demoRecord set from Python: 0 follows the cvar, 1 always
// records, -1 never does. Game thread only, same as demo_active.
static int8_t demo_request[MAX_DEMO_CLIENTS];
static int demo_forced_on; // number of slots currently set to 1.

static char demo_path[MAX_DEMO_CLIENTS][512];
static uint32_t demo_gen[MAX_DEMO_CLIENTS]; // bumped per OPEN, to match completions up.

typedef struct {
    FILE *fh;
    char path[512];
    long blocks;  // blocks written to this segment (gamestate counts as 1.)
    long bytes;   // bytes written to the file so far.
    uint32_t gen; // demo_gen[slot] of the OPEN this segment came from.
} demo_client_t;

static demo_client_t demos[MAX_DEMO_CLIENTS];
static unsigned char writer_scratch[MAX_NETCHAN_MSGLEN + 64];

// Finalised segments waiting to be reported to Python: writer pushes, game thread pops,
// both under demo_lock. Sized to absorb a whole-server finalise, 64 slots at once.
#define DEMO_DONE_MAX (MAX_DEMO_CLIENTS + 16)
static demo_finished_t demo_done[DEMO_DONE_MAX];
static unsigned demo_done_head, demo_done_tail;
static unsigned demo_done_dropped;
// Queued + dropped, so the frame hook can bail without touching demo_lock at all.
static volatile long demo_done_pending; // (win32)

// Records queued and records fully handled, so Demo_DrainFinalise can wait for one to have taken
// effect rather than merely been read out of the ring. demo_seq_put is written only by the game
// thread and demo_seq_done only by the writer, so a release store paired with the other side's
// acquire load is enough.
static volatile __int64 demo_seq_put;  // (win32)
static volatile __int64 demo_seq_done; // (win32)

// How long the shutdown finalise waits for the writer before giving up and leaving the
// segments as .part. Bounded so a wedged writer cannot hang the server's exit.
#define DEMO_DRAIN_TIMEOUT_MS 2000

static cvar_t *sv_demoRecord;       // 0 = off, 1 = record every connected client
static cvar_t *sv_demoDir;          // output subdirectory, under fs_homepath
static cvar_t *sv_demoNameFormat;   // filename template: %date %slot %name
static cvar_t *sv_demoCleanupParts; // remove leftover .part files at startup
static cvar_t *fs_homepath;

static const int32_t demo_eof[2] = {-1, -1};

static void demo_sanitise(char *dst, size_t n, const char *src) {
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 1 < n; i++) {
        char c = src[i];
        if (c == '^' && src[i + 1]) { // colour code: skip '^' and the following char.
            i++;
            continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
            dst[j++] = c;
        } else if (c == ' ') {
            dst[j++] = '_';
        }
    }
    if (j == 0) {
        dst[j++] = 'x';
    }
    dst[j] = '\0';
}

static void demo_mkdir_p(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            _mkdir(tmp);
            *p = '/';
        }
    }
    _mkdir(tmp);
}

static void demo_build_name(char *out, size_t n, int slot, client_t *client) {
    const char *subdir = (sv_demoDir && sv_demoDir->string[0]) ? sv_demoDir->string : "demos";

    time_t now = time(NULL);
    struct tm tmv;
    char date[32] = "00000000-000000";
    if (localtime_s(&tmv, &now) == 0) {
        strftime(date, sizeof(date), "%Y%m%d-%H%M%S", &tmv);
    }

    char name[64];
    demo_sanitise(name, sizeof(name), client->name);

    const char *fmt = (sv_demoNameFormat && sv_demoNameFormat->string[0]) ? sv_demoNameFormat->string
                                                                          : "%date_slot%slot_%name";
    char body[256];
    size_t o = 0;
    char num[16] = {0}; 

    for (size_t i = 0; fmt[i] && o + 1 < sizeof(body);) {
        if (fmt[i] == '%') {
            const char *rep = NULL;

            if (!strncmp(fmt + i + 1, "date", 4)) {
                rep = date;
                i += 5;
            } else if (!strncmp(fmt + i + 1, "name", 4)) {
                rep = name;
                i += 5;
            } else if (!strncmp(fmt + i + 1, "slot", 4)) {
                snprintf(num, sizeof(num), "%02d", slot);
                rep = num;
                i += 5;
            }
            if (rep) {
                for (const char *p = rep; *p && o + 1 < sizeof(body); p++) {
                    body[o++] = *p;
                }
                continue;
            }
        }
        body[o++] = fmt[i++];
    }
    body[o] = '\0';

    snprintf(out, n, "%s/%s/%s.dm_91", fs_homepath->string, subdir, body);
}

// Both ring copy helpers require demo_lock to be held and handle wraparound with a split copy.
static void ring_copy_in(uint64_t pos, const void *src, size_t n) {
    size_t off   = (size_t)(pos & (DEMO_RING_SIZE - 1));
    size_t first = DEMO_RING_SIZE - off;
    if (first > n) {
        first = n;
    }
    memcpy(demo_ring + off, src, first);
    memcpy(demo_ring, (const unsigned char *)src + first, n - first);
}

static void ring_copy_out(uint64_t pos, void *dst, size_t n) {
    size_t off   = (size_t)(pos & (DEMO_RING_SIZE - 1));
    size_t first = DEMO_RING_SIZE - off;
    if (first > n) {
        first = n;
    }
    memcpy(dst, demo_ring + off, first);
    memcpy((unsigned char *)dst + first, demo_ring, n - first);
}

// Caller holds demo_lock. Returns 0 on success, -1 if the record does not fit. The signal
// cannot wake the writer before the caller unlocks, so state may be published after it in
// the same critical section.
static int demo_ring_put_locked(const demo_rec_hdr_t *hdr, const void *payload) {
    size_t need = sizeof(*hdr) + hdr->len;

    if (DEMO_RING_SIZE - (demo_head - demo_tail) < need) {
        return -1;
    }
    ring_copy_in(demo_head, hdr, sizeof(*hdr));
    if (hdr->len) {
        ring_copy_in(demo_head + sizeof(*hdr), payload, hdr->len);
    }
    demo_head += need;
    // Under the lock and before the signal, so the writer can never bump demo_seq_done
    // past a put that has not been counted yet.
    InterlockedExchangeAdd64((volatile __int64*)&demo_seq_put, 1); // (win32)
    WakeConditionVariable(&demo_cond); // (win32)
    return 0;
}

// Game thread only. Returns 0 on success, -1 if the record does not fit.
static int demo_ring_put(const demo_rec_hdr_t *hdr, const void *payload) {
    EnterCriticalSection(&demo_lock); // (win32)
    int rc = demo_ring_put_locked(hdr, payload);
    LeaveCriticalSection(&demo_lock); // (win32)
    return rc;
}

// d->path holds the final name; the segment is recorded into "<name>.part" until finalised.
static void demo_part_name(char *out, size_t n, const char *path) {
    snprintf(out, n, "%s.part", path);
}

// Queues a finished segment for the game thread to report. Must not block: on overflow we
// count the loss instead of stalling the writer.
static void writer_publish_done(int slot, uint32_t gen, const char *path, long bytes, int discarded,
                                int failed) {
    EnterCriticalSection(&demo_lock); // (win32)
    if (demo_done_head - demo_done_tail >= DEMO_DONE_MAX) {
        demo_done_dropped++;
    } else {
        demo_finished_t *f = &demo_done[demo_done_head % DEMO_DONE_MAX];
        f->slot            = slot;
        f->gen             = gen;
        f->discarded       = discarded;
        f->failed          = failed;
        f->bytes           = bytes;
        snprintf(f->path, sizeof(f->path), "%s", path);
        demo_done_head++;
    }
    // Under the lock, so the count can never drift from the queue it describes.
    InterlockedIncrement((volatile long*)&demo_done_pending); // (win32)
    LeaveCriticalSection(&demo_lock); // (win32)
}

static void writer_finalise(demo_client_t *d) {
    if (!d->fh) {
        return;
    }
    // The stream is 64 KB fully buffered, so on a full disk the last blocks are still in
    // stdio and fclose's return is the only sign.
    int bad = (fwrite(demo_eof, sizeof(demo_eof), 1, d->fh) != 1) || ferror(d->fh);
    d->bytes += (long)sizeof(demo_eof);
    bad |= (fclose(d->fh) != 0);
    d->fh = NULL;

    int slot = (int)(d - demos);

    char part[sizeof(d->path) + 8];
    demo_part_name(part, sizeof(part), d->path);
    if (bad) {
        // Leave it as .part and report the failure, matching writer_handle_block. The byte
        // count is what we handed to stdio, so it overstates what reached the disk.
        DebugPrint("demo: write failed finalising %s\n", part);
        writer_publish_done(slot, d->gen, part, d->bytes, 0, 1);
        return;
    }
    if (d->blocks <= 1) { // only the gamestate.
        unlink(part);
        DebugPrint("demo: discarded empty segment %s\n", d->path);
        writer_publish_done(slot, d->gen, d->path, d->bytes, 1, 0);
        return;
    }
    // Published only once the file is in its final place, so a handler never sees a
    // half-written .part. A failed rename leaves it under the .part name, so report that.
    if (rename(part, d->path)) {
        DebugPrint("demo: could not rename %s into place\n", part);
        writer_publish_done(slot, d->gen, part, d->bytes, 0, 1);
        return;
    }
    writer_publish_done(slot, d->gen, d->path, d->bytes, 0, 0);
}

static void writer_handle_open(int slot, uint32_t gen, const char *path) {
    demo_client_t *d = &demos[slot];
    writer_finalise(d); // just in case this slot's CLOSE was dropped.

    d->gen = gen;

    size_t plen = strlen(path);
    if (plen >= sizeof(d->path)) {
        DebugPrint("demo: path too long, not recording slot %d\n", slot);
        writer_publish_done(slot, gen, path, 0, 0, 1);
        return;
    }
    memcpy(d->path, path, plen + 1);

    char dir[512];
    memcpy(dir, d->path, plen + 1);
    char *sep = strrchr(dir, '/');
    if (sep) {
        *sep = '\0';
        demo_mkdir_p(dir);
    }

    char part[sizeof(d->path) + 8];
    demo_part_name(part, sizeof(part), d->path);
    d->fh = fopen(part, "wb");
    if (!d->fh) {
        DebugPrint("demo: could not open %s\n", part);
        writer_publish_done(slot, gen, part, 0, 0, 1);
        return;
    }
    setvbuf(d->fh, NULL, _IOFBF, 64 * 1024);
    d->blocks = 0;
    d->bytes  = 0;
    DebugPrint("demo: recording slot %d -> %s\n", slot, d->path);
}

static void writer_handle_block(int slot, int32_t seq, const unsigned char *data, uint32_t len) {
    demo_client_t *d = &demos[slot];
    if (!d->fh) {
        return; // dropped OPEN or earlier write error.
    }
    int32_t hdr[2] = {seq, (int32_t)len};
    if (fwrite(hdr, sizeof(hdr), 1, d->fh) != 1 || fwrite(data, 1, len, d->fh) != len) {
        DebugPrint("demo: write error on slot %d, closing\n", slot);
        fclose(d->fh); // left behind as .part to mark it incomplete.
        d->fh = NULL;
        // Report it so the game thread stops capturing this segment; without that it
        // keeps queueing blocks the writer will drop until the next gamestate.
        char part[sizeof(d->path) + 8];
        demo_part_name(part, sizeof(part), d->path);
        writer_publish_done(slot, d->gen, part, d->bytes, 0, 1);
        return;
    }
    d->blocks++;
    d->bytes += (long)sizeof(hdr) + (long)len;
}

// Replacement POSIX Function model to return DWORD for CreateThread
static DWORD WINAPI demo_writer_main(LPVOID lpParam) { 
    (void)lpParam;

    for (;;) {
        demo_rec_hdr_t hdr;
        uint32_t len;

        EnterCriticalSection(&demo_lock); // (win32)
        while (demo_head == demo_tail) {
            SleepConditionVariableCS(&demo_cond, &demo_lock, INFINITE); // (win32)
        }
        ring_copy_out(demo_tail, &hdr, sizeof(hdr));
        len = hdr.len;
        if (len > sizeof(writer_scratch)) { // should be impossible.
            len = 0;
        }
        if (len) {
            ring_copy_out(demo_tail + sizeof(hdr), writer_scratch, len);
        }
        demo_tail += sizeof(hdr) + hdr.len;
        LeaveCriticalSection(&demo_lock); // (win32)

        if (hdr.type == DEMO_REC_SHUTDOWN) {
            for (int i = 0; i < MAX_DEMO_CLIENTS; i++) {
                writer_finalise(&demos[i]);
            }
            EnterCriticalSection(&demo_lock); // (win32)
            demo_thread_state = DEMO_THREAD_STOPPED;
            LeaveCriticalSection(&demo_lock); // (win32)
            DebugPrint("demo: writer thread stopped\n");
            return 0;
        }
        // A malformed record is still a record that has been consumed, so it has to be
        // counted like any other or Demo_DrainFinalise would wait for it forever.
        if (len == hdr.len && hdr.slot >= 0 && hdr.slot < MAX_DEMO_CLIENTS) {
            switch (hdr.type) {
            case DEMO_REC_OPEN:
                if (len > 0) {
                    writer_scratch[len - 1] = '\0';
                    writer_handle_open(hdr.slot, (uint32_t)hdr.seq, (const char *)writer_scratch);
                }
                break;
            case DEMO_REC_BLOCK:
                writer_handle_block(hdr.slot, hdr.seq, writer_scratch, len);
                break;
            case DEMO_REC_CLOSE:
                writer_finalise(&demos[hdr.slot]);
                break;
            case DEMO_REC_CLOSE_ALL:
                for (int i = 0; i < MAX_DEMO_CLIENTS; i++) {
                    writer_finalise(&demos[i]);
                }
                break;
            }
        }

        // Release, so a waiter that sees this count has also seen the finalise behind it.
        InterlockedExchangeAdd64((volatile __int64*)&demo_seq_done, 1); // (win32)
    }
}

// An explicit per-slot request wins over the global cvar.
static int demo_slot_wanted(int slot) {
    if (demo_request[slot] > 0) {
        return 1;
    }
    if (demo_request[slot] < 0) {
        return 0;
    }
    return sv_demoRecord->integer != 0;
}

static int demo_reconcile_thread(void) {
    int enabled = sv_demoRecord->integer != 0 || demo_forced_on > 0;

    if (enabled && demo_state_cached == DEMO_THREAD_RUNNING) {
        return 1;
    }
    if (!enabled && demo_state_cached == DEMO_THREAD_STOPPED) {
        return 0;
    }
    if (demo_state_cached == DEMO_THREAD_STOPPING) {
        EnterCriticalSection(&demo_lock); // (win32)
        demo_state_cached = demo_thread_state;
        LeaveCriticalSection(&demo_lock); // (win32)
        if (demo_state_cached != DEMO_THREAD_STOPPED) {
            return 0; 
        }
        if (!enabled) {
            return 0;
        }
    }

    if (enabled) {
        HANDLE th = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)demo_writer_main, NULL, 0, NULL); // (win32)
        if (th == NULL) {
            DebugPrint("demo: could not start writer thread; recording disabled\n");
            return 0;
        }
        CloseHandle(th); // (win32)
        EnterCriticalSection(&demo_lock); // (win32)
        demo_thread_state = DEMO_THREAD_RUNNING;
        LeaveCriticalSection(&demo_lock); // (win32)
        demo_state_cached = DEMO_THREAD_RUNNING;
        DebugPrint("demo: writer thread started\n");
        return 1;
    }

    demo_rec_hdr_t hdr = {DEMO_REC_SHUTDOWN, 0, 0, 0};
    EnterCriticalSection(&demo_lock); // (win32)
    int queued = demo_ring_put_locked(&hdr, NULL);
    if (queued == 0) {
        demo_thread_state = DEMO_THREAD_STOPPING;
    }
    LeaveCriticalSection(&demo_lock); // (win32)

    if (queued == 0) {
        demo_state_cached = DEMO_THREAD_STOPPING;
        memset(demo_active, 0, sizeof(demo_active));
    }
    return 0;
}

// sv_demoNameFormat can contain a '/', so segments are not necessarily all in one directory.
#define DEMO_SWEEP_DEPTH   8
#define DEMO_SWEEP_MIN_AGE 60 // seconds; see the note about other servers below.

// Complete replacement POSIX Routines to be compatible with Windows CRT (win32)
static void demo_sweep_dir(const char* dir, int depth, time_t cutoff, unsigned* removed, unsigned* kept) {
    // Native Windows Struct
    WIN32_FIND_DATAA find_data;
    char search_path[512];
    
    // Windows requires that the filter end in "/*" to index all content
    if ((size_t)sprintf_s(search_path, sizeof(search_path), "%s/*", dir) >= sizeof(search_path)) {
        return;
    }

    // Replacement of opendir
    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        return; // can't access
    }

    do {
        // Ignore virtual directories
        if (!strcmp(find_data.cFileName, ".") || !strcmp(find_data.cFileName, "..")) {
            continue;
        }

        char path[512];
        if ((size_t)sprintf_s(path, sizeof(path), "%s/%s", dir, find_data.cFileName) >= sizeof(path)) {
            continue;
        }

        // Eval if register is Directory or not
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth + 1 < DEMO_SWEEP_DEPTH) {
                // Recursión segura sobre subcarpetas
                demo_sweep_dir(path, depth + 1, cutoff, removed, kept);
            }
            continue;
        }

        // forget some type of files (hidden, system, other)
        if (find_data.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_SYSTEM)) {
            continue; 
        }

        // file .part control
        size_t len = strlen(find_data.cFileName);
        if (len <= 5 || strcmp(find_data.cFileName + len - 5, ".part")) {
            continue;
        }

        // Replaces st.st_mtime.
        // Windows stores the time in 64-bit structures of 100 nanoseconds (FILETIME).
        ULARGE_INTEGER ull;
        ull.LowPart = find_data.ftLastWriteTime.dwLowDateTime;
        ull.HighPart = find_data.ftLastWriteTime.dwHighDateTime;
        time_t file_mtime = (time_t)((ull.QuadPart / 10000000ULL) - 11644473600ULL);

        if (file_mtime > cutoff) {
            (*kept)++;
            continue;
        }

        if (_unlink(path)) {
            DebugPrint("demo: could not remove %s\n", path);
        } else {
            (*removed)++;
        }

    } while (FindNextFileA(hFind, &find_data));

    // Close handle to avoid leaks
    FindClose(hFind);
}

// The writer renames a segment into place once finalised, and every ordinary way out of the
// server finalises first, so a surviving .part belongs to a run that was killed outright or
// crashed with it open, and will never be completed.
static void demo_sweep_parts(void) {
    if (sv_demoCleanupParts && !sv_demoCleanupParts->integer) {
        return;
    }
    if (!fs_homepath || !fs_homepath->string[0]) {
        return;
    }

    const char* subdir = (sv_demoDir && sv_demoDir->string[0]) ? sv_demoDir->string : "demos";
    char dir[512];
    if ((size_t)snprintf(dir, sizeof(dir), "%s/%s", fs_homepath->string, subdir) >= sizeof(dir)) {
        return;
    }

    unsigned removed = 0, kept = 0;
    demo_sweep_dir(dir, 0, time(NULL) - DEMO_SWEEP_MIN_AGE, &removed, &kept);
    if (removed) {
        DebugPrint("demo: removed %u incomplete .part file(s) left by a previous run.\n", removed);
    }
    if (kept) {
        DebugPrint("demo: left %u .part file(s) written in the last %d seconds alone; another server may "
                   "still be recording them.\n",
                   kept, DEMO_SWEEP_MIN_AGE);
    }
}

void Demo_Init(void) {
    if (!Cvar_Get) {
        return;
    }
    sv_demoRecord       = Cvar_Get("sv_demoRecord", "0", CVAR_ARCHIVE);
    sv_demoDir          = Cvar_Get("sv_demoDir", "demos", CVAR_ARCHIVE);
    sv_demoNameFormat   = Cvar_Get("sv_demoNameFormat", "%date_slot%slot_%name", CVAR_ARCHIVE);
    sv_demoCleanupParts = Cvar_Get("sv_demoCleanupParts", "1", CVAR_ARCHIVE);
    fs_homepath         = Cvar_FindVar("fs_homepath");

     // Register demo_lock before init (Win32)
    InitializeCriticalSection(&demo_lock); 
    
     // Register demo_condition before init (Win32)
    InitializeConditionVariable(&demo_cond);

    DebugPrint("Demo Module Initialized (tjone270).\n");

    // Once per process: by the second G_InitGame the .part files on disk are our own. So
    // sv_demoCleanupParts only has a say at startup.
    static qboolean swept = qfalse;
    if (!swept) {
        swept = qtrue;
        demo_sweep_parts();
    }

}

// Split out of Demo_Capture so the profiler wrapper covers every early return.
static void Demo_CaptureBody(msg_t *msg, client_t *client) {
    if (!sv_demoRecord || !MSG_WriteBits || !svs || !svs->clients || !fs_homepath) {
        return;
    }
    if (!demo_reconcile_thread()) {
        return;
    }
    if (msg->cursize <= 0 || msg->cursize > MAX_NETCHAN_MSGLEN) {
        return;
    }

    long slot = client - svs->clients;
    if (slot < 0 || slot >= MAX_DEMO_CLIENTS) {
        return;
    }

    int seq          = client->netchan.outgoingSequence;
    int is_gamestate = (seq == client->gamestateMessageNum);

    demo_rec_hdr_t hdr = {0};
    hdr.slot           = (int32_t)slot;

    if (!demo_slot_wanted((int)slot)) {
        // Turned off mid-segment, so finalise now instead of waiting for a disconnect.
        if (demo_active[slot]) {
            hdr.type = DEMO_REC_CLOSE;
            demo_ring_put(&hdr, NULL);
            demo_active[slot] = 0;
        }
        return;
    }

    if (is_gamestate) {
        // A valid demo begins at a gamestate.
        if (demo_active[slot]) {
            hdr.type = DEMO_REC_CLOSE;
            demo_ring_put(&hdr, NULL); // OPEN should also finalise the writer-side.
            demo_active[slot] = 0;
        }
        char path[512];
        demo_build_name(path, sizeof(path), (int)slot, client);
        hdr.type = DEMO_REC_OPEN;
        hdr.len  = (uint32_t)strlen(path) + 1;
        hdr.seq  = (int32_t)(demo_gen[slot] + 1); // seq is unused for OPEN.
        if (demo_ring_put(&hdr, path) != 0) {
            return; // ring full; retry at this client's next gamestate.
        }

        demo_gen[slot]++;
        demo_active[slot] = 1;
        snprintf(demo_path[slot], sizeof(demo_path[slot]), "%s", path);
    } else if (!demo_active[slot]) {
        return; // we have not seen this slot's gamestate yet.
    }

    // Use a scratch buffer, never mutate the live outgoing message.
    msg_t tmp   = *msg;
    tmp.data    = demo_scratch;
    tmp.maxsize = (int)sizeof(demo_scratch);
    memcpy(demo_scratch, msg->data, (size_t)msg->cursize);
    MSG_WriteBits(&tmp, SVC_EOF, 8); // bit-accurate append.

    hdr.type = DEMO_REC_BLOCK;
    hdr.seq  = (int32_t)seq;
    hdr.len  = (uint32_t)tmp.cursize;
    if (demo_ring_put(&hdr, demo_scratch) != 0) {
        DebugPrint("demo: ring full, dropping slot %ld segment\n", slot);
        demo_active[slot] = 0;
        hdr.type          = DEMO_REC_CLOSE;
        hdr.len           = 0;
        demo_ring_put(&hdr, NULL);
    }
}

void Demo_Capture(msg_t *msg, client_t *client) {
    Demo_CaptureBody(msg, client);

}

void Demo_ClientDisconnect(int slot) {
    if (slot < 0 || slot >= MAX_DEMO_CLIENTS) {
        return;
    }
    demo_active[slot] = 0;
    // Drop the override too: the next player in this slot shouldn't inherit it.
    Demo_Request(slot, 0);
    if (demo_state_cached == DEMO_THREAD_RUNNING) {
        demo_rec_hdr_t hdr = {DEMO_REC_CLOSE, slot, 0, 0};
        demo_ring_put(&hdr, NULL);
    }
}

void Demo_CloseAll(void) {
    memset(demo_active, 0, sizeof(demo_active));
    if (demo_state_cached == DEMO_THREAD_RUNNING) {
        demo_rec_hdr_t hdr = {DEMO_REC_CLOSE_ALL, 0, 0, 0};
        demo_ring_put(&hdr, NULL);
    }
}

// Drops every per-slot override. Not part of Demo_CloseAll: a map change keeps the same clients
// in the same slots, so an override has to survive it. A shutdown does not. SV_Shutdown Z_Free's
// the client array outright (@qzeroded 0x43d880) and never calls SV_DropClient, so nothing
// reaches Demo_ClientDisconnect and the override would still stand for the next occupant.
void Demo_ClearRequests(void) {
    for (int slot = 0; slot < MAX_DEMO_CLIENTS; slot++) {
        Demo_Request(slot, 0); // keeps demo_forced_on in step
    }
}

// Converted to Win32 MSVC Routine
static void demo_deadline(ULONGLONG* deadline_ms, long timeout_ms) {
    if (deadline_ms == NULL) return;
    
    // Capturamos el milisegundo de hardware actual y le sumamos el tiempo límite
    *deadline_ms = GetTickCount64() + (ULONGLONG)timeout_ms;
}

// Converted to Win32 MSVC Routine
static int demo_past_deadline(const ULONGLONG* deadline_ms) {
    if (deadline_ms == NULL) return 1;
    
    return (GetTickCount64() >= *deadline_ms);
}

// Finalise every open segment and wait for the writer to have actually done it. Demo_CloseAll
// only posts to the ring, and the writer is detached, so at process exit that races and finalises
// nothing. The wait is bounded and polled. See demo_seq_done.
void Demo_DrainFinalise(void) {
    ULONGLONG deadline; // (win32)
    demo_deadline(&deadline, DEMO_DRAIN_TIMEOUT_MS);

    // Already stopping means a SHUTDOWN record is in flight and finalises everything on its way
    // out, so wait for the writer rather than posting a CLOSE_ALL behind it. Nothing else can
    // have queued that SHUTDOWN; demo_reconcile_thread posts it from this same thread.
    if (demo_state_cached == DEMO_THREAD_STOPPING) {
        for (;;) {
            EnterCriticalSection(&demo_lock); // (win32)
            demo_thread_state_t state = demo_thread_state;
            LeaveCriticalSection(&demo_lock); // (win32)
            if (state == DEMO_THREAD_STOPPED) {
                demo_state_cached = state;
                return;
            }
            if (demo_past_deadline(&deadline)) {
                DebugPrint("demo: writer still draining after %d ms; segment(s) left as .part\n",
                           DEMO_DRAIN_TIMEOUT_MS);
                return;
            }
            Sleep(1);
        }
    }

    if (demo_state_cached != DEMO_THREAD_RUNNING) {
        return; // nothing has been recorded, so there is nothing open to finalise.
    }

    memset(demo_active, 0, sizeof(demo_active));
    demo_rec_hdr_t hdr = {DEMO_REC_CLOSE_ALL, 0, 0, 0};
    if (demo_ring_put(&hdr, NULL) != 0) {
        DebugPrint("demo: ring full at shutdown; open segment(s) left as .part\n");
        return;
    }

    // No other thread puts, so the count now standing is the record we just queued.
    unsigned __int64 target = (unsigned __int64)InterlockedCompareExchange64((volatile __int64*)&demo_seq_put, 0, 0); // (win32)
    while ((unsigned __int64)InterlockedCompareExchange64((volatile __int64*)&demo_seq_done, 0, 0) < target) { // (win32)
        if (demo_past_deadline(&deadline)) {
            DebugPrint("demo: writer did not finalise within %d ms; segment(s) left as .part\n",
                       DEMO_DRAIN_TIMEOUT_MS);
            return;
        }
        Sleep(1);
    }
}

qboolean Demo_Request(int slot, int mode) {
    if (slot < 0 || slot >= MAX_DEMO_CLIENTS) {
        return qfalse;
    }

    int8_t want = (mode > 0) ? 1 : (mode < 0 ? -1 : 0);
    if (demo_request[slot] == want) {
        return qtrue;
    }

    // demo_forced_on only changes here, so it can't drift from the array.
    if (demo_request[slot] > 0) {
        demo_forced_on--;
    }
    if (want > 0) {
        demo_forced_on++;
    }
    demo_request[slot] = want;
    return qtrue;
}

int Demo_GetRequest(int slot) {
    if (slot < 0 || slot >= MAX_DEMO_CLIENTS) {
        return 0;
    }
    return demo_request[slot];
}

qboolean Demo_IsRecording(int slot) {
    if (slot < 0 || slot >= MAX_DEMO_CLIENTS) {
        return qfalse;
    }
    return demo_active[slot] ? qtrue : qfalse;
}

const char *Demo_GetPath(int slot) {
    if (slot < 0 || slot >= MAX_DEMO_CLIENTS || !demo_active[slot] || !demo_path[slot][0]) {
        return NULL;
    }
    return demo_path[slot];
}

// The writer lost the segment, so stop feeding it. Ignored unless the slot is still on
// the same segment, so a reopen in the meantime is never killed by a stale completion.
void Demo_AbandonSlot(int slot, uint32_t gen) {
    if (slot < 0 || slot >= MAX_DEMO_CLIENTS || demo_gen[slot] != gen) {
        return;
    }
    demo_active[slot] = 0;
}

unsigned Demo_PendingFinished(void) {
    return InterlockedCompareExchange((volatile long*)&demo_done_pending, 0, 0); // (win32)
}

qboolean Demo_PollFinished(demo_finished_t *out) {
    qboolean got = qfalse;
    EnterCriticalSection(&demo_lock); // (win32)
    if (demo_done_tail != demo_done_head) {
        *out = demo_done[demo_done_tail % DEMO_DONE_MAX];
        demo_done_tail++;
        InterlockedDecrement((volatile long*)&demo_done_pending);
        got = qtrue;
    }
    LeaveCriticalSection(&demo_lock); // (win32)
    return got;
}

unsigned Demo_TakeDroppedCount(void) {
    EnterCriticalSection(&demo_lock); // (win32)
    unsigned n        = demo_done_dropped;
    demo_done_dropped = 0;
    InterlockedExchangeAdd((volatile long*)&demo_done_pending, -n); // (win32)
    LeaveCriticalSection(&demo_lock); // (win32)
    return n;
}