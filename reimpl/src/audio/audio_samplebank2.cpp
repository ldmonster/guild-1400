// guild::audio::sb2 — see audio_samplebank2.h. 1:1 translations of the second
// sample-bank / audio-slot / 3d-sound leaf slice of gilde.exe.
#include "audio/audio_samplebank2.h"

#include <cstring>
#include <new>

namespace guild::audio::sb2 {

// ---- module globals (single definition) -------------------------------------
Record*     g_activeBank  = nullptr; // dword_62E8E8
int         g_dirtyCount  = 0;       // dword_62E8EC
FileHooks   g_fileHooks{};
Sound3dHooks g_sound3dHooks{};

// ---- local helpers faithful to the original CRT/VIBE_Util callees -----------

// VIBE_Util_StrChr @0x5d3ef0 — returns pointer to the LAST occurrence of `c`
// (strrchr semantics), or null. (The original scans through the terminator.)
static const char* StrChrLast(const char* s, char c) {
    const char* found = nullptr;
    do {
        if (*s == c) found = s;
    } while (*s++);
    return found;
}

// VIBE_Util_StrNCopyPad @0x5d9360 — copy up to `n` bytes from src (stop at NUL),
// then zero-pad the remainder of `n`. Returns dst. Does NOT NUL-terminate if src
// fills the whole field (matches the binary).
static char* StrNCopyPad(char* dst, const char* src, int n) {
    char* out = dst;
    while (n) {
        if (!*src) break;
        *out++ = *src++;
        --n;
    }
    while (n) {
        *out++ = 0;
        --n;
    }
    return dst;
}

// loc_5CB930 — case-sensitive substring search used by ClassifyAudioFormat:
// loc_5CB930(haystack, needle) is truthy when `needle` occurs in `haystack`.
static bool ContainsSubstr(const char* hay, const char* needle) {
    return std::strstr(hay, needle) != nullptr;
}

// ---- SampleBank leaves -------------------------------------------------------

int ExtractFileExtension(const char* path, char* out, int cap) {
    // Despite the recovered name, the binary extracts the BASE FILENAME — the
    // bytes BETWEEN the last '\\' and the trailing '.' — not the extension.
    //   v7  = StrChr(path,'\\')   ; last backslash               (0x447793)
    //   v8  = StrChr(v7,'.')      ; last dot at/after backslash   (0x44779f)
    //   if (!v8) return -1                                        (0x4477a6)
    //   if (!v7) return -1        ; ecx==v7 guard                 (0x4477aa)
    //   v10 = v8 - v7 - 1         ; chars between '\\' and '.'    (0x4477b1)
    //   if (v10 >= cap) return -1 ; signed jge                    (0x4477b5)
    //   StrNCopyPad(out, v7+1, v10)                               (0x4477b9)
    if (!path) return -1;            // 0x44777c
    if (!out)  return -1;            // 0x447780
    const char* bs = StrChrLast(path, '\\'); // 0x447793  v7
    if (!bs) {
        // The binary would dereference null in StrChr(0,'.') here; in practice
        // every sample path carries a separator.  Treat a missing backslash as
        // scanning from the start of the path so well-formed inputs are exact.
        bs = path;
        // NB: when there is no backslash the "ecx==v7" guard at 0x4477aa would
        // also fire (v7==0 -> return -1) in the binary, but that path cannot be
        // reached without first crashing on StrChr(0,'.').
    }
    const char* dot = StrChrLast(bs, '.');   // 0x44779f  v8
    if (!dot) return -1;             // 0x4477a6
    int needed = static_cast<int>(dot - bs - 1); // 0x4477b1  v10 = v8 - v7 - 1
    if (needed >= cap) return -1;    // 0x4477b5 (signed jge >= a3)
    StrNCopyPad(out, bs + 1, needed); // 0x4477b9  copy from v7+1 (after backslash)
    return 0;                        // 0x447789
}

int ClassifyAudioFormat(const char* ext) {
    if (!ext) return 0;                                  // 0x4477d1
    if (ContainsSubstr(ext, ".wav") || ContainsSubstr(ext, ".WAV"))
        return 1;                                        // 0x4477e9
    if (ContainsSubstr(ext, ".mp3") || ContainsSubstr(ext, ".MP3"))
        return 2;                                        // 0x447811
    return 0;                                            // 0x4477d7
}

int ValidateSampleFile(const char* path) {
    if (!g_fileHooks.open) return -1; // inert default: no file system available
    unsigned size = 0;
    void* h = g_fileHooks.open(path, "rb", &size); // VIBE_File_OpenStream
    int result = 0;                                // v5 = 0
    if (!size || !h) return -1;                    // 0x44784c
    // The original reads `size` bytes once into the file's own buffer; we model
    // the single-read contract (read() must return 1).
    char tmp[1];
    if (g_fileHooks.read(h, tmp, size, 1) != 1)    // 0x44786f
        result = -1;                               // 0x447871
    if (g_fileHooks.close) g_fileHooks.close(h);   // 0x447878
    return result;                                 // 0x447855
}

int GetDirtyCount() {
    return g_dirtyCount; // 0x4493fd
}

// FindVariationByName @0x44773c — first variation (bank+60 chain) with matching
// name. Used by CountSamples / VariationHasSamples / AddVariation.
static Record* FindVariationByName(const char* name) {
    if (!g_activeBank) return nullptr;
    for (Record* v = g_activeBank->next; v; v = v->next) {
        if (std::strcmp(name, v->name) == 0) return v;
    }
    return nullptr;
}

int CountSamples(const char* name) {
    int count = 0;
    if (!name) {                                   // 0x4481f2 null -> sum all
        if (!g_activeBank) return 0;
        for (Record* v = g_activeBank->next; v; v = v->next)
            count += CountSamples(v->name);        // recurse (0x448203)
        return count;
    }
    Record* var = FindVariationByName(name);       // 0x448219
    if (!var) return count;                        // 0x448220
    Record* s = var->sampleHead;                   // +56
    if (!s) return count;                          // 0x448227
    do {
        s = s->smpNext;                            // +260 (0x448229)
        ++count;                                   // 0x44822f
    } while (s);                                   // 0x448232
    return count;
}

int HasSamples(char* out, int cap) {
    if (!g_activeBank) return 0;                   // 0x448249
    Record* head = g_activeBank->sampleHead;       // +56
    if (!head) return 0;                           // 0x448250
    StrNCopyPad(out, head->name, cap);             // 0x44825e
    return 1;                                       // dst is nonzero
}

int VariationHasSamples(const char* name, char* out, int cap) {
    if (!g_activeBank) return 0;                   // 0x44829f
    Record* var = FindVariationByName(name);       // 0x4482a7
    if (!var || !var->sampleHead) return 0;        // 0x4482b0
    StrNCopyPad(out, var->sampleHead->name, cap);  // 0x4482bd
    return 1;
}

int HasVariations(char* out, int cap) {
    if (!g_activeBank) return 0;                   // 0x4482d5
    Record* head = g_activeBank->next;             // +60
    if (!head) return 0;                           // 0x4482dc
    StrNCopyPad(out, head->name, cap);             // 0x4482ea
    return 1;
}

int IsValid(char* out, int cap) {
    if (!g_activeBank) return -1;                  // 0x448324
    StrNCopyPad(out, g_activeBank->name, cap);     // 0x448337 (bank name at +0)
    return 0;
}

int AddVariation(const char* name) {
    if (!g_activeBank || FindVariationByName(name)) // 0x447a0a
        return -1;                                  // 0x447a11
    // Walk to the tail of the variation chain (bank+60 then node+60).
    Record* tail = g_activeBank->next;             // 0x447a1f
    if (tail) {
        while (tail->next) tail = tail->next;      // 0x447a2b
    }
    Record* node = new (std::nothrow) Record();    // VIBE_Memory_AllocDebug 0x40
    if (!node) return -1;                          // 0x447a49 / 0x447aa3
    // The original memsets 0x40 bytes to zero then StrNCopyPad(name, 50). Our
    // Record() value-initializes; copy the name (cap 50) into the node.
    StrNCopyPad(node->name, name, 50);             // 0x447a78 / 0x447ad5
    if (tail) {
        tail->next = node;                         // 0x447a44
    } else {
        g_activeBank->next = node;                 // 0x447ae6
    }
    ++g_dirtyCount;                                // 0x447a7d / 0x447ae9
    return 0;
}

int SetVariationName(const char* varKey, const char* oldName, const char* newName) {
    char buf[256];
    // VariationHasSamples loads the first sample's name into buf and returns the
    // sample-head pointer truthiness. The original then walks the sample chain
    // (StrCmp against oldName) to find the node, rewriting its name in place.
    Record* var = FindVariationByName(varKey);
    if (!var || !var->sampleHead) return -1;       // 0x447b84 (no samples)
    StrNCopyPad(buf, var->sampleHead->name, 256);
    Record* cur = var->sampleHead;                 // HasSamples result == head
    while (std::strcmp(oldName, cur->name) != 0) {  // 0x447b91 StrCmp
        Record* nextNode = cur->smpNext;           // +260 (0x447b99)
        if (!nextNode) return -1;                  // 0x447bb5
        StrNCopyPad(buf, nextNode->name, 256);     // 0x447bac
        cur = nextNode;                            // 0x447bb1
    }
    // Byte-copy newName (incl. terminator) over the matched node's name.
    const char* src = newName;                     // 0x447bc4
    char* dst = cur->name;
    for (;;) {
        char c0 = *src;
        *dst = c0;                                 // 0x447bce
        if (!c0) break;                            // 0x447bd2
        char c1 = src[1];
        src += 2;                                  // 0x447bd7
        dst[1] = c1;                               // 0x447bda
        dst += 2;                                  // 0x447bdd
        if (!c1) break;                            // 0x447be2
    }
    return 0;                                       // 0x447bb9
}

int ClearVariationSamples(const char* name, const char* sampleName) {
    Record* var = FindVariationByName(name);       // 0x447cb7
    if (!var) return -1;                           // 0x447cc4
    if (sampleName) {                              // 0x447ccc (v4 != 0)
        Record* cur = var->sampleHead;             // +56 (0x447cd2)
        if (cur) {
            Record* prev = nullptr;                // v6
            while (std::strcmp(sampleName, cur->name) != 0) { // 0x447ce4
                prev = cur;                        // 0x447d2d
                cur = cur->smpNext;                // +260 (0x447d2f)
                if (!cur) return 0;                // 0x447d40 (not found)
            }
            if (prev)
                prev->smpNext = cur->smpNext;      // 0x447cf0
            else
                var->sampleHead = cur->smpNext;    // 0x447d28
            var->size -= cur->smpSize;             // +52 -= +256 (0x447cfc)
            if (g_activeBank)
                g_activeBank->size -= cur->smpSize; // 0x447d0a
            delete cur;                            // VIBE_Memory_FreeDebug 0x447d0f
            ++g_dirtyCount;                        // 0x447d14
        }
        return 0;                                  // 0x447d1a
    }
    // sampleName == null: free every sample in the variation.
    Record* s = var->sampleHead;                   // 0x447d41
    while (s) {
        var->size -= s->smpSize;                   // 0x447d59
        if (g_activeBank)
            g_activeBank->size -= s->smpSize;      // 0x447d68
        Record* nextNode = s->smpNext;             // v13
        delete s;                                  // 0x447d6b
        ++g_dirtyCount;                            // 0x447d79
        s = nextNode;                              // 0x447d77
    }
    var->sampleHead = nullptr;                      // 0x447d84
    return 0;                                       // 0x447d8b
}

// ---- Audio slot scans --------------------------------------------------------

int FindFreeAmbientSlot(const uint8_t* flagA, const uint8_t* flagB) {
    // while ( flagB[v0] || !flagA[v0] ) v0 += 296; if (v0 >= 2960) return 0
    // 2960 / 296 == 10 slots. The byte arrays are indexed at i*296.
    for (int i = 0; i < 10; ++i) {
        int off = i * 296;
        if (!flagB[off] && flagA[off])             // free & active (0x43a8aa)
            return i;
    }
    return -1;                                      // 0x43a8ba
}

int FindFreeVoiceSlot(const uint8_t* flagByte, const int32_t* word0, const int32_t* word1) {
    // Pass 1: i in [0,740) step 74 (10 slots): flagByte[(i)*4]==0 && word0[i]==0
    //         && word1[i]==0.
    for (int i = 0; i < 740; i += 74) {            // 0x43a8c4
        if (!flagByte[i * 4] && !word0[i] && !word1[i]) // 0x43a8df
            return i / 74;
    }
    // Pass 2 (fallback): drop the word1 condition.
    for (int j = 0; j < 740; j += 74) {            // 0x43a8ed
        if (!flagByte[j * 4] && !word0[j])         // 0x43a8ff
            return j / 74;
    }
    return -1;                                      // 0x43a90f
}

// ---- Sound3d trivial leaves --------------------------------------------------

Sound3dRangeEntry* SetRange(Sound3dRangeEntry* entry, int range) {
    entry->range = range;  // *(int*)(entry+64) = range  (0x424cb0)
    return entry;          // return entry (0x424cb3)
}

int DetachIfValid(int handle) {
    if (handle) {                                  // 0x4248ca
        if (g_sound3dHooks.detachEntry)
            return g_sound3dHooks.detachEntry(handle); // 0x4248cd
        return handle; // inert default: no pool wired, leave handle unchanged
    }
    return handle;                                 // 0x4248cc
}

} // namespace guild::audio::sb2
