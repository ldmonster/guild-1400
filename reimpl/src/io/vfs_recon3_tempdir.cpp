// gilde.exe — guild::io  (VFS path slice: temp-directory resolution)
// Reconstruction of VIBE_Vfs_GetTempDir @0x5fc8d0. See vfs_recon3_tempdir.h.

#include "vfs_recon3_tempdir.h"

#include <cstring>

namespace guild::io {

// VIBE_Vfs_GetWorkingDir @0x5eeeb0 — already reconstructed in file_ops3.cpp.
// Declared here (not redefined) to wire the working-dir fallback (rule 13 / no
// ODR clash).
char* VfsGetWorkingDir(char* dst, guild::u32 size);
// Partner free for the working-dir buffer (file_ops3.cpp); see header.
void  VfsFreeMem(void* ptr);

namespace {

// off_64ABFC @0x64abfc — the NULL-terminated table of environment-variable
// names probed, in this exact order. Strings: TMP @0x62c554, TEMP @0x62c558,
// TMPDIR @0x62c560, TEMPDIR @0x62c568. The original iterates `for (i = table;
// **i; ++i)`, i.e. stops at the NULL slot.
const char* const kEnvNames[] = {"TMP", "TEMP", "TMPDIR", "TEMPDIR", nullptr};

VfsTempDirHooks g_hooks{};

// byte_64AC10 @0x64ac10 — the 0x104-byte process-lifetime cache. byte_64AC10[0]
// non-zero means "already computed"; the memoization gate is the first byte
// being non-NUL, exactly as the original.
//
// Boundary note: the original accepts a path of strlen 0x103, then appends a
// separator + NUL, writing offsets 0x103 and 0x104. In the binary, byte_64AC10
// is 0x104 wide and that trailing NUL lands in the adjacent global (engine
// adjacency). To reproduce the identical visible string (0x103 chars + sep +
// NUL == 0x105 bytes) without an OOB write, the backing buffer is sized 0x105.
// All observable output (string contents, strlen) is byte-identical.
char g_cache[0x105] = {0};

} // namespace

VfsTempDirHooks VfsSetTempDirHooks(const VfsTempDirHooks& h) {
    VfsTempDirHooks prev = g_hooks;
    g_hooks = h;
    return prev;
}

void VfsResetTempDirCache() {
    g_cache[0] = '\0';
}

// gilde.exe 0x5fc8d0 — VIBE_Vfs_GetTempDir  (no args; eax = &byte_64AC10)
char* VfsGetTempDir() {
    // if (!byte_64AC10[0]) { ...compute once... }   /*0x5fc8dc*/
    if (g_cache[0] == '\0') {
        // for (i = &off_64ABFC; **i; ++i)            /*0x5fc8e2*/
        for (const char* const* i = kEnvNames; *i; ++i) {
            const char* envVar =
                g_hooks.findEnvVar ? g_hooks.findEnvVar(*i) : nullptr; /*0x5fc8e9*/
            if (envVar) {                                              /*0x5fc8f2*/
                // v2 = strlen(envVar) + 1;                            /*0x5fc900*/
                // if (v2 - 1 <= 0x103)   i.e. strlen(envVar) <= 0x103 /*0x5fc90c*/
                std::size_t v2 = std::strlen(envVar) + 1;
                if (v2 - 1 <= 0x103) {
                    // VIBE_File_GetFullPath(byte_64AC10, envVar, 0x103, ...) /*0x5fc918*/
                    // The 4th arg in the original is an OS-side scratch end
                    // pointer (&envVar[v2]); it does not bound the canonical
                    // output, whose cap is 0x103. We route through the inert
                    // canonicalizer; on success the cache is filled.
                    if (g_hooks.getFullPath)
                        g_hooks.getFullPath(g_cache, envVar, 0x103u);
                    break;                                            /*0x5fc91d*/
                }
            }
        }

        // if (!byte_64AC10[0])  -> working-directory fallback           /*0x5fc930*/
        if (g_cache[0] == '\0') {
            // WorkingDir = VIBE_Vfs_GetWorkingDir(0, 0);                /*0x5fc940*/
            // then a byte-pair copy loop terminating at NUL: a verbatim
            // (NUL-inclusive) string copy of the working directory.       /*0x5fc943..0x5fc959*/
            char* wd = VfsGetWorkingDir(nullptr, 0);
            if (wd) {
                char* dst = g_cache;
                char* src = wd;
                while ((*dst = *src) != '\0') {
                    ++dst;
                    ++src;
                }
                // VfsGetWorkingDir(nullptr,...) returns an owned heap buffer; the
                // original frees it after copying. Release via the partner hook.
                VfsFreeMem(wd);
            }
        }

        // v7 = strlen(byte_64AC10) + 1;                                  /*0x5fc96b*/
        // v8 = byte_64AC10[v7 - 2];  (last char before NUL)              /*0x5fc979*/
        // if (v8 != '\\' && v8 != '/') { append '\\'; NUL-terminate }    /*0x5fc983*/
        std::size_t v7 = std::strlen(g_cache) + 1;
        // v7 - 2 underflows when the cache is empty; the original reads
        // byte_64AC10[-1] in that degenerate case. We mirror the observable
        // result: with a non-empty path, append a separator if missing.
        char last = (v7 >= 2) ? g_cache[v7 - 2] : '\0';
        if (last != '\\' && last != '/') {
            g_cache[v7 - 1] = '\\';                                    /*0x5fc986*/
            g_cache[v7] = '\0';                                        /*0x5fc98a*/
        }
    }
    return g_cache;                                                   /*0x5fc996*/
}

} // namespace guild::io
