// guild::audio::sb3 — see audio_samplebank3.h.  1:1 translations of the .txt
// sample-bank parser/serializer + tree mutators + player slice of gilde.exe.
#include "audio/audio_samplebank3.h"

#include <cstdio>
#include <cstring>
#include <new>

namespace guild::audio::sb3 {

// ---- module globals (single definition) -------------------------------------
SbRecord* g_activeBank   = nullptr; // dword_62E8E8
int       g_dirtyCount   = 0;       // dword_62E8EC
int       g_compiledSize  = 0;      // dword_62E8F4
void*     g_compiledBlob  = nullptr; // dword_62E8F8

// ---- local helpers faithful to the original CRT / VIBE_Util callees ----------

// VIBE_Util_StrNCopyPad @0x5d9360 — copy up to `n` bytes from src (stop at NUL),
// then zero-pad the remainder of `n`.  Does NOT NUL-terminate when src fills the
// whole field (matches the binary).
static void StrNCopyPad(char* dst, const char* src, int n) {
    while (n) {
        if (!*src) break;
        *dst++ = *src++;
        --n;
    }
    while (n) {
        *dst++ = 0;
        --n;
    }
}

// loc_5CB930 — VIBE_Util_StrStr(eax=hay, edx=needle): pointer to first occurrence
// of `needle` in `hay`, or null.  Faithful clone for the inert default hook.
static char* DefaultStrStr(char* hay, const char* needle) {
    if (!needle || !needle[0]) return hay; // empty needle path (edx[0]==0)
    if (!needle[1]) {                      // single-char fast path (edx[1]==0)
        for (char* p = hay; *p; ++p)
            if (*p == needle[0]) return p;
        return nullptr;
    }
    std::size_t nlen = std::strlen(needle);
    for (char* p = hay; *p; ++p) {
        if (std::strncmp(p, needle, nlen) == 0) return p;
    }
    return nullptr;
}

// ---- inert default hooks -----------------------------------------------------
static char* HookStrStr(char* hay, const char* needle) { return DefaultStrStr(hay, needle); }
static int   HookSampleFileSize(const char*) { return 0; }
static int   HookReadToken(void*, char*, int) { return -1; }
static void  HookWriteText(void*, const char*) {}
static void* HookOpenStream(const char*, const char*) { return nullptr; }
static void  HookCloseStream(void*) {}

Sb3Hooks g_hooks{
    &HookStrStr, &HookSampleFileSize, &HookReadToken,
    &HookWriteText, &HookOpenStream, &HookCloseStream
};

// Resolve a hook member to its default if a test cleared it to nullptr (matches
// the "inert default" pattern even after Sb3Hooks{} reset).
static char* StrStrH(char* hay, const char* needle) {
    return g_hooks.strStr ? g_hooks.strStr(hay, needle) : DefaultStrStr(hay, needle);
}
static int SampleFileSizeH(const char* p) {
    return g_hooks.sampleFileSize ? g_hooks.sampleFileSize(p) : 0;
}
static int ReadTokenH(void* s, char* out, int cap) {
    return g_hooks.readToken ? g_hooks.readToken(s, out, cap) : -1;
}
static void WriteTextH(void* s, const char* t) {
    if (g_hooks.writeText) g_hooks.writeText(s, t);
}
static void* OpenStreamH(const char* p, const char* m) {
    return g_hooks.openStream ? g_hooks.openStream(p, m) : nullptr;
}
static void CloseStreamH(void* s) {
    if (g_hooks.closeStream) g_hooks.closeStream(s);
}

// gilde.exe 0x4475d4 — VIBE_Audio_GetSampleFileSize.  Open `path` "rb", seek to
// end, the readable length is the result; 0 when the file is absent.
int GetSampleFileSize(const char* path) {
    // The original opens the stream and measures it; our hook returns the length
    // (or 0).  Encapsulated so callers share the exact same probe.
    return SampleFileSizeH(path);
}

// The original Compile/LoadFromText apply a path rewrite: scan the token for a
// substring (the `needle`) and, when found, splice the runtime `root` in its
// place, then resume at needle+5 (all the originals use 5-char tokens: "%lang").
// `dst` receives the rewritten path; `name` is the stored token; `root` the
// substitution.  When no needle hits, the path is just `name` (no-op).
static void RewritePath(char* dst, const char* name, const char* root, const char* needle) {
    // Copy `name` into a scratch buffer so we can mutate (the original works on a
    // 256-byte stack copy filled by StrNCopyPad).
    char scratch[256];
    StrNCopyPad(scratch, name, 256);
    char* hit = StrStrH(scratch, needle);
    if (!hit) {
        // No token: dst = scratch (already the stored name).  /*0x4495f0 false*/
        std::memcpy(dst, scratch, 256);
        return;
    }
    *hit = 0;                       // terminate before the token /*0x449600*/
    // dst = [scratch prefix][root][scratch after token].
    std::size_t plen = std::strlen(scratch);
    std::memcpy(dst, scratch, plen);
    char* w = dst + plen;
    for (const char* r = root; *r; ++r) *w++ = *r; // append root /*0x44960c..*/
    const char* tail = hit + 5;     // skip the 5-char token /*needle+5*/
    while (*tail) *w++ = *tail++;    // append remainder      /*0x449644..*/
    *w = 0;
}

// ---- tree allocation helpers (VIBE_Memory_AllocDebug + zero-fill) ------------
static SbRecord* NewRecord() { return new (std::nothrow) SbRecord(); }
static void FreeRecord(SbRecord* r) { delete r; }

// gilde.exe 0x4493b4 — VIBE_SampleBank_Destroy.
int Destroy() {
    if (!g_activeBank) return -1;        // 0x4493c0
    RemoveVariation(nullptr);            // 0x4493ce  (frees all variations)
    RemoveSample(nullptr);               // 0x4493d5  (frees all top-level)
    FreeRecord(g_activeBank);            // 0x4493df  VIBE_Memory_FreeDebug
    g_activeBank = nullptr;              // 0x4493e4
    g_dirtyCount = 0;                    // 0x4493ea
    return 0;
}

// gilde.exe 0x447bf4 — VIBE_SampleBank_RemoveVariation.  name==null -> clear all.
int RemoveVariation(const char* name) {
    if (!name) {                                 // 0x447c01
        // Loop:  RemoveVariation(head) until the list is empty.  Each recursive
        // call clears that variation's samples then unlinks+frees it (head case),
        // bumping g_dirtyCount.  /*0x447c08..0x447c33*/
        while (g_activeBank->varNext) {
            SbRecord* head = g_activeBank->varNext;
            RemoveVariation(head->name);          // clears samples + unlinks head
            ++g_dirtyCount;                       // 0x447c2b
        }
        return 0;                                 // 0x447c3c
    }
    // Targeted removal of one named variation.
    // First clear its samples (ClearVariationSamples 0x447c44).
    SbRecord* victim = nullptr;
    SbRecord* prev = nullptr;
    for (SbRecord* v = g_activeBank->varNext; v; prev = v, v = v->varNext) {
        if (std::strcmp(name, v->name) == 0) { victim = v; break; } // 0x447c62
    }
    if (!victim) return 0;                         // 0x447ca0 (not found)
    for (SbRecord* s = victim->sampleHead; s; ) {  // free its samples
        SbRecord* next = s->smpNext;
        FreeRecord(s);
        ++g_dirtyCount;
        s = next;
    }
    victim->sampleHead = nullptr;
    if (prev) prev->varNext = victim->varNext;     // 0x447c6b
    else g_activeBank->varNext = victim->varNext;  // 0x447c8b
    FreeRecord(victim);                            // 0x447c70
    ++g_dirtyCount;                                // 0x447c75
    return 0;
}

// gilde.exe 0x4480cc — VIBE_SampleBank_RemoveSample.  name==null -> clear all.
int RemoveSample(const char* name) {
    if (!g_activeBank) return -1;                  // 0x4480e0
    if (name) {                                    // 0x4480e8
        SbRecord* prev = nullptr;
        for (SbRecord* s = g_activeBank->sampleHead; s; prev = s, s = s->smpNext) {
            if (std::strcmp(name, s->name) == 0) { // 0x448100
                if (prev) prev->smpNext = s->smpNext;          // 0x44810c
                else g_activeBank->sampleHead = s->smpNext;     // 0x448145
                g_activeBank->size -= s->smpSize;               // 0x44811d
                FreeRecord(s);                                  // 0x448122
                ++g_dirtyCount;                                 // 0x448127
                return 0;
            }
        }
        return 0;                                  // 0x44815e (not found)
    }
    // Clear every top-level sample. /*0x44815f*/
    for (SbRecord* s = g_activeBank->sampleHead; s; ) {
        SbRecord* next = s->smpNext;               // v9
        g_activeBank->size -= s->smpSize;          // 0x448175
        FreeRecord(s);                             // 0x448180
        ++g_dirtyCount;                            // 0x44818e
        s = next;
    }
    g_activeBank->sampleHead = nullptr;
    return 0;
}

// gilde.exe 0x447f44 — VIBE_SampleBank_AddSample (top-level).
int AddSample(const char* path) {
    if (!g_activeBank) return -1;                   // 0x447f72
    // FindSampleByName: reject duplicates.
    for (SbRecord* s = g_activeBank->sampleHead; s; s = s->smpNext)
        if (std::strcmp(path, s->name) == 0) return -1;
    // Walk to the tail of the top-level list. /*0x447f87..0x447f9e*/
    SbRecord* tail = g_activeBank->sampleHead;
    if (tail) while (tail->smpNext) tail = tail->smpNext;
    SbRecord* node = NewRecord();                   // 0x447fae / 0x448042
    if (!node) return -1;
    StrNCopyPad(node->name, path, 256);             // 0x447fed / 0x44808a
    node->smpSize = GetSampleFileSize(path);        // 0x447fff / 0x44809f
    if (tail) tail->smpNext = node;                 // 0x447fb3
    else g_activeBank->sampleHead = node;           // 0x44804d
    g_activeBank->size += node->smpSize;            // 0x448028 / 0x4480b3
    ++g_dirtyCount;                                 // 0x448022 / 0x4480b6
    return 0;
}

// gilde.exe 0x447da0 — VIBE_SampleBank_AddSampleToVariation.
int AddSampleToVariation(const char* varName, const char* path) {
    // FindVariationByName.
    SbRecord* var = nullptr;
    for (SbRecord* v = g_activeBank ? g_activeBank->varNext : nullptr; v; v = v->varNext)
        if (std::strcmp(varName, v->name) == 0) { var = v; break; }
    if (!var) return -1;                            // 0x447dd3
    // FindSampleInVariation: reject duplicates.
    for (SbRecord* s = var->sampleHead; s; s = s->smpNext)
        if (std::strcmp(path, s->name) == 0) return -1;
    // GetLastSample: tail of the variation's sample list.
    SbRecord* tail = var->sampleHead;
    if (tail) while (tail->smpNext) tail = tail->smpNext;
    SbRecord* node = NewRecord();                   // 0x447dfd / 0x447eb2
    if (!node) return -1;
    StrNCopyPad(node->name, path, 256);             // 0x447e40 / 0x447eef
    node->smpSize = GetSampleFileSize(path);        // 0x447e56 / 0x447efe
    if (tail) tail->smpNext = node;                 // 0x447e02
    else var->sampleHead = node;                    // 0x447eb7
    var->size += node->smpSize;                     // 0x447e73 / 0x447f12 (var+52)
    g_activeBank->size += node->smpSize;            // 0x447e99 / 0x447f35
    ++g_dirtyCount;                                 // 0x447e93 / 0x447f2f
    return 0;
}

// gilde.exe 0x448958 — VIBE_SampleBank_LoadFromText.
int LoadFromText(const char* root, const char* path) {
    if (g_activeBank) return -1;                    // 0x4489ae (already loaded)
    void* stream = OpenStreamH(path, "rt");         // VIBE_File_OpenStream
    if (!stream) return -1;
    int status = 0;
    char tok[256];

    g_activeBank = NewRecord();                     // 0x4489bc (alloc 0x40)
    if (!g_activeBank) { CloseStreamH(stream); return -1; }

    // Bank name.
    if (ReadTokenH(stream, tok, 256) == -1) {       // 0x448a08
        status = -1;
    } else {
        StrNCopyPad(g_activeBank->name, tok, 256);
    }

    // Top-level samples until "{EndOfSamples}".
    SbRecord* lastSample = nullptr;
    while (!status) {                               // 0x448a20
        if (ReadTokenH(stream, tok, 256) == -1) { status = -1; break; } // 0x448a43
        if (std::strcmp("{EndOfSamples}", tok) == 0) break;             // 0x448a59
        SbRecord* node = NewRecord();               // alloc 0x108
        if (!node) { status = -1; break; }          // 0x448bcc
        if (lastSample) lastSample->smpNext = node; // 0x448a76
        else g_activeBank->sampleHead = node;       // 0x448b4e
        lastSample = node;
        char resolved[256];
        RewritePath(resolved, tok, root, "%lang");  // path rewrite (no-op typ.)
        StrNCopyPad(node->name, tok, 256);          // 0x448ada (stores the token)
        node->smpSize = GetSampleFileSize(resolved); // 0x448afc
    }

    // Variations until "{EndOfSampleBank}".
    if (!status) {
        SbRecord* lastVar = nullptr;
        while (1) {                                 // 0x448c01
            if (ReadTokenH(stream, tok, 256) == -1) { status = -1; break; } // 0x448c0e
            if (std::strcmp("{EndOfSampleBank}", tok) == 0) break;          // 0x448c24
            SbRecord* var = NewRecord();            // alloc 0x40
            if (!var) { status = -1; break; }       // 0x448c5b
            if (lastVar) lastVar->varNext = var;    // 0x448c4f
            else g_activeBank->varNext = var;       // 0x448e8f
            lastVar = var;
            StrNCopyPad(var->name, tok, 50);        // 0x448cb8

            // Samples of this variation until "{EndOfVariation}".
            SbRecord* lastVarSample = nullptr;
            while (!status) {                       // 0x448cc5
                if (ReadTokenH(stream, tok, 256) == -1) { status = -1; break; } // 0x448ce8
                if (std::strcmp("{EndOfVariation}", tok) == 0) break;           // 0x448cfe
                SbRecord* node = NewRecord();       // alloc 0x108
                if (!node) { status = -1; break; }  // 0x448d25
                if (lastVarSample) lastVarSample->smpNext = node; // 0x448d1b
                else var->sampleHead = node;        // 0x448eaf (var first sample)
                lastVarSample = node;
                char resolved[256];
                RewritePath(resolved, tok, root, "%lang");
                StrNCopyPad(node->name, tok, 256);  // 0x448d7f
                node->smpSize = GetSampleFileSize(resolved); // 0x448dff
                if (node->smpSize) {                // 0x448e0c
                    g_activeBank->size += node->smpSize; // 0x448e19
                    var->size += node->smpSize;          // 0x448e29
                }
            }
            if (status) break;                      // 0x448e42
        }
    }

    CloseStreamH(stream);                           // 0x448e48 VIBE_Vfs_CloseAndFreeEntry
    return status;
}

// gilde.exe 0x448ed0 — VIBE_SampleBank_SaveToText.
int SaveToText(const char* path) {
    if (!g_activeBank) return -1;                   // 0x448ede
    void* stream = OpenStreamH(path, "wt");         // 0x448ef6
    if (!stream) return -1;                         // 0x448efa
    char line[512];

    // "%s\n" of the bank name. /*0x448f35*/
    std::snprintf(line, sizeof(line), "%s\n", g_activeBank->name);
    WriteTextH(stream, line);

    for (SbRecord* s = g_activeBank->sampleHead; s; s = s->smpNext) { // 0x448f40
        std::snprintf(line, sizeof(line), "%s\n", s->name);          // 0x448f76
        WriteTextH(stream, line);
    }
    WriteTextH(stream, "{EndOfSamples}\n");         // 0x448f8e

    for (SbRecord* v = g_activeBank->varNext; v; v = v->varNext) {   // 0x448f99
        std::snprintf(line, sizeof(line), "%s\n", v->name);          // 0x448fd7
        WriteTextH(stream, line);
        for (SbRecord* s = v->sampleHead; s; s = s->smpNext) {       // 0x448fe4
            std::snprintf(line, sizeof(line), "%s\n", s->name);      // 0x449009
            WriteTextH(stream, line);
        }
        WriteTextH(stream, "{EndOfVariation}\n");   // 0x449021
    }
    WriteTextH(stream, "{EndOfSampleBank}\n");      // 0x449036
    CloseStreamH(stream);                           // 0x449040
    g_dirtyCount = 0;                               // 0x449047 (dword_62E8EC)
    return 0;
}

// gilde.exe 0x449058 — VIBE_SampleBank_SaveBinary.
int SaveBinary(const char* path) {
    if (!g_compiledSize) return 0;                  // 0x449066
    void* stream = OpenStreamH(path, "wb");         // 0x44907b
    if (!stream) return 0;                          // 0x44907f
    int result = 0;
    // ComputeTotalSize() then WriteBuffered(blob, size, 1) == 1 expected.
    // We surface the blob via writeText (treated as an opaque payload write);
    // a successful write is assumed when a blob is present.
    if (g_compiledBlob) {
        WriteTextH(stream, static_cast<const char*>(g_compiledBlob));
    } else {
        result = -1;                                // 0x4490a1 (short write)
    }
    CloseStreamH(stream);                           // 0x4490a8
    return result;
}

// gilde.exe 0x449580 — VIBE_SampleBank_ResolveSamplePaths.
int ResolveSamplePaths(const char* root) {
    if (!g_activeBank) return -1;                   // 0x44959b false -> 0x449816
    g_activeBank->size = 0;                         // 0x4495cc

    // Top-level samples. /*0x4495a7*/
    for (SbRecord* s = g_activeBank->sampleHead; s; s = s->smpNext) {
        char resolved[256];
        RewritePath(resolved, s->name, root, "%lang"); // 0x4495e7
        s->smpSize = GetSampleFileSize(resolved);       // 0x449664
        if (!s->smpSize) return -1;                      // 0x449671 break -> -1
        g_activeBank->size += s->smpSize;                // 0x449685
    }

    // Variations. /*0x4496c3*/
    for (SbRecord* v = g_activeBank->varNext; v; v = v->varNext) {
        v->size = 0;                                // 0x4496fb (var+13/+52)
        for (SbRecord* s = v->sampleHead; s; s = s->smpNext) {
            char resolved[256];
            RewritePath(resolved, s->name, root, "%lang"); // 0x449716
            s->smpSize = GetSampleFileSize(resolved);       // 0x449793
            if (!s->smpSize) return -1;                      // 0x4497a0 break -> -1
            g_activeBank->size += s->smpSize;                // 0x4497b1
            v->size += s->smpSize;                           // 0x4497c1
        }
    }
    return 0;                                       // 0x449822
}

// gilde.exe 0x4490e4 — VIBE_SampleBank_PlaySample.
// `name` with no '.' -> a variation name: choose sample (index % count), recurse.
// A dotted name -> a concrete sample: probe its size and "play" it.  The Miles
// device interaction is routed through the hooks (no-op by default).
int PlaySample(const char* name, int index) {
    // loc_5CB930(name, ".") != 0  <=>  name contains a '.'.  /*0x449108*/
    char scan[256];
    StrNCopyPad(scan, name, 256);
    bool hasDot = StrStrH(scan, ".") != nullptr;
    if (!hasDot) {
        // Variation branch.  CountSamples then walk to (index % count).
        SbRecord* var = nullptr;
        for (SbRecord* v = g_activeBank ? g_activeBank->varNext : nullptr; v; v = v->varNext)
            if (std::strcmp(name, v->name) == 0) { var = v; break; } // FindVariationByName
        if (!var) return -1;                        // 0x449122
        // Walk: i starts at first sample, decrement index until -1.  /*0x44935e*/
        SbRecord* s = var->sampleHead;
        int n = index;
        while (s) {
            if (--n == -1) break;
            s = s->smpNext;
        }
        if (s) return PlaySample(s->name, /*index unused on recurse*/ 0); // 0x44937a
        return 0;                                   // 0x44913f
    }

    // Concrete sample branch:  resolve path, probe size, "load + play".
    char resolved[256];
    RewritePath(resolved, name, "", "%lang");       // path rewrite (no-op typ.)
    int size = GetSampleFileSize(resolved);         // 0x449201
    if (!size) return -1;                           // 0x449211
    void* stream = OpenStreamH(resolved, "rb");     // 0x449235
    if (!stream) return -1;                         // 0x44930e branch
    // (The original reads `size` bytes and pushes them to the Miles sample API;
    // here a present, openable file with non-zero size is a successful play.)
    CloseStreamH(stream);                           // 0x4492a4
    return 0;
}

} // namespace guild::audio::sb3
