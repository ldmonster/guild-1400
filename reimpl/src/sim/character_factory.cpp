// character_factory — see character_factory.h. Faithful 1:1 ports of the
// character-from-model factory trio:
//   0x402254 VIBE_Character_AllocSlot       (REUSED from character_path.cpp)
//   0x4029c4 VIBE_Character_CreateMesh
//   0x402d10 VIBE_Character_CreateFromModel
//
// AllocSlot is not redefined here — CreateFromModel calls the existing
// guild::sim::AllocSlot() (character_path.cpp), which allocates the real
// 516-byte record into the g_live[512] table and writes the slot index to
// record[0]. CreateMesh then writes the remaining fields by raw byte offset, the
// same way the binary indexes *(rec+N) and *(node+N).
#include "sim/character_factory.h"

#include "sim/character_path.h"     // guild::sim::AllocSlot (0x402254), CharacterPathHooks
#include "sim/character_query.h"    // LiveActor, g_live, kLiveCapacity

#include <cstring>   // std::strncpy / std::memcpy / std::strstr (the inert leaves)
#include <cstdio>    // std::snprintf (the model-load failure message)

namespace guild::sim {

namespace {

// dword_401010[0..3] — the 4-dword transform seed (all zero) handed to
// Object_AttachToUniverseNode. Modeled for parity (the leaf is a hook).
const int kAttachSeed[4] = {0, 0, 0, 0};

// loc_5CB930 creature needles (aRatte/aHund/aKatze/aPferd @0x610138..).
const char kNeedleRatte[] = "RATTE";
const char kNeedleHund[]  = "HUND";
const char kNeedleKatze[] = "KATZE";
const char kNeedlePferd[] = "PFERD";

// byte_610134 — the no-'_' default base name: an empty string.
const char kDefaultBase[] = "";

// PreloadAniSet(rec, 2, "bewegung/gehen", "stehen/stehen_newnoise").
const char kGehen[]            = "bewegung/gehen";          // aBewegungGehen
const char kStehenNewnoise[]   = "stehen/stehen_newnoise";  // aStehenStehenNe
const char* const kGaitIdle[2] = {kGehen, kStehenNewnoise};
// PreloadLowPolyAniSet(rec, 1, "gehen").
const char kLowGehen[]         = "gehen";                   // aGehen
const char* const kLowSet[1]   = {kLowGehen};

// -------------------------------------------------------------------------
// Inert default hooks (defined in THIS library .cpp so any src/ reference to a
// callee resolves in the unified build). Tests install their own.
// -------------------------------------------------------------------------
void*    DefAttach(void*, const char*) { return nullptr; }
unsigned DefIndex(void*) { return 0; }
void     DefQueryTerrain(LiveActor*) {}
void     DefBuildCache(void*) {}
void     DefPropagate(void*) {}
int      DefFindSubstring(const char* h, const char* n) {
    if (!h || !n) return 0;
    return std::strstr(h, n) != nullptr;
}
void     DefPreloadAni(LiveActor*, const char* const*, int) {}
void     DefPreloadLow(LiveActor*, const char* const*, int) {}
void     DefUpdateLow(LiveActor*) {}
void     DefReport(const char*) {}
void     DefDestroy(LiveActor* rec) {
    // Inert teardown standing in for VIBE_Character_Destroy @0x402120: drop the
    // record from g_live and free it. The full Destroy (action-queue clear, mesh
    // / object-node release, morph teardown, universe-slot switch) is a separate
    // reconstruction target (see report).
    if (!rec) return;
    for (int i = 0; i < kLiveCapacity; ++i) {
        if (g_live[i] == rec) { g_live[i] = nullptr; break; }
    }
    // The record was allocated through CharacterPathHooks.allocDebug (default
    // malloc); release it through the matching freeDebug so the heap stays paired.
    CharacterPathGetHooks().freeDebug(rec);
}

CharacterFactoryHooks MakeDefaults() {
    CharacterFactoryHooks h{};
    h.attachToUniverseNode = DefAttach;
    h.indexFromPointer = DefIndex;
    h.queryTerrainType = DefQueryTerrain;
    h.buildObjectCache = DefBuildCache;
    h.propagateDirtyFlag = DefPropagate;
    h.findSubstring = DefFindSubstring;
    h.preloadAniSet = DefPreloadAni;
    h.preloadLowPolyAniSet = DefPreloadLow;
    h.updateLowPolyMesh = DefUpdateLow;
    h.lowPolyEnabled = 0;
    h.networkBit = 0;
    h.reportError = DefReport;
    h.destroy = DefDestroy;
    return h;
}

CharacterFactoryHooks g_hooks = MakeDefaults();

// Raw byte accessors mirroring the binary's *(rec+N) / *(node+N) addressing.
inline unsigned char* B(void* p) { return static_cast<unsigned char*>(p); }
inline void WriteI32(void* base, int off, int v) {
    std::memcpy(B(base) + off, &v, sizeof(int));
}
inline void WriteF32(void* base, int off, float v) {
    std::memcpy(B(base) + off, &v, sizeof(float));
}
inline void WritePtr(void* base, int off, void* v) {
    std::memcpy(B(base) + off, &v, sizeof(void*));
}
inline void* ReadPtr(void* base, int off) {
    void* v = nullptr;
    std::memcpy(&v, B(base) + off, sizeof(void*));
    return v;
}

// The character record is a 516-byte (0x204) heap block (AllocSlot /
// kCharRecordSize). The original copies model-name substrings into it byte-by-
// byte up to the NUL; model names are short paths so the copies stay inside the
// block. HARDENING (wave-11): a malformed/overlong/NUL-less model name would run
// the strcpy past the field span and overflow the 516-byte block. Copy into the
// field's own span, always NUL-terminated, never past the record. For valid
// (short) names this writes exactly the same bytes as the original.
constexpr int kCharRecordBytes = 0x204;   // == kCharRecordSize (character_path.h)
inline void WriteStrField(void* base, int off, int spanEnd, const char* s) {
    if (off < 0 || off >= kCharRecordBytes) return;
    int limit = spanEnd < kCharRecordBytes ? spanEnd : kCharRecordBytes; // exclusive
    int maxLen = limit - off - 1;          // reserve 1 byte for the NUL
    if (maxLen < 0) maxLen = 0;
    char* dst = reinterpret_cast<char*>(B(base) + off);
    if (!s) { dst[0] = '\0'; return; }
    int i = 0;
    for (; i < maxLen && s[i]; ++i)
        dst[i] = s[i];
    dst[i] = '\0';
}

} // namespace

CharacterFactoryHooks SetCharacterFactoryHooks(const CharacterFactoryHooks* hooks) {
    CharacterFactoryHooks prev = g_hooks;
    g_hooks = hooks ? *hooks : MakeDefaults();
    return prev;
}
const CharacterFactoryHooks& GetCharacterFactoryHooks() { return g_hooks; }

// ===========================================================================
// Name decomposition (disasm @0x402a4c). See the header for the exact rule.
// ===========================================================================
void DecomposeModelName(const char* model, char outBase[256], char outPrefix[256],
                        bool* wrotePrefix) {
    // The original copies the model name into a 256-byte scratch buffer with a
    // word-strided byte copy that terminates on the NUL; reproduce as a bounded
    // strcpy (model names are short paths well under 256).
    char buf[256];
    std::strncpy(buf, model ? model : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* p = std::strchr(buf, '_');            // VIBE_Util_StrChr(buf, '_')
    if (p) {
        char* q = std::strchr(p + 1, '_');      // strchr(p+1, '_')
        if (q) *q = '\0';                        // cut at the second '_'
        // base = [after first '_' .. (truncated at second '_')]  -> rec+304
        std::strncpy(outBase, p + 1, 255);
        outBase[255] = '\0';
        // zero the first '_' (p[-1] in the original; p points AT the '_' here, the
        // original's `ecx` is p+1 so `ecx-1` is the '_'); buf is now the prefix.
        *p = '\0';
        std::strncpy(outPrefix, buf, 255);       // prefix = [.. first '_')  -> rec+368
        outPrefix[255] = '\0';
        if (wrotePrefix) *wrotePrefix = true;
    } else {
        // No '_': base = byte_610134 (""); prefix field left untouched.
        std::strncpy(outBase, kDefaultBase, 255);
        outBase[255] = '\0';
        if (wrotePrefix) *wrotePrefix = false;
    }
}

// ===========================================================================
// gilde.exe 0x4029c4 — VIBE_Character_CreateMesh.
// ===========================================================================
int CreateMesh(LiveActor* rec, const char* model, void* parentMat) {
    const CharacterFactoryHooks& h = g_hooks;
    void* recv = rec;
    (void)kAttachSeed;   // dword_401010 seed; consumed by the (hooked) attach leaf.

    if (!rec)                                           // 0x4029ec: if (!result) return result
        return 0;

    // *(rec+52) = Object_AttachToUniverseNode(0, parentMat, model, seed, &nameOut)
    void* node = h.attachToUniverseNode(parentMat, model);  // 0x402a0d
    WritePtr(recv, kRecMesh, node);
    WriteI32(recv, kRecScriptH, -1);                    // 0x402a13: *(rec+40) = -1

    if (!node) {                                        // 0x402a16
        // Sprintf("ch_CreateMesh(): Could not load 3D-Character: %s", model); Report.
        char msg[512];
        std::snprintf(msg, sizeof(msg),
                      "ch_CreateMesh(): Could not load 3D-Character: %s",
                      model ? model : "");
        h.reportError(msg);                             // 0x402c9b
        return 0;                                       // 0x402ca0
    }

    // --- name decomposition -> rec+304 (base) / rec+368 (prefix) ---
    // The original copies each substring byte-by-byte up to (and including) the
    // NUL — NOT a fixed-width block. Model names are short paths; we copy just the
    // string + terminator so we stay inside the 516-byte record.
    char base[256], prefix[256];
    bool wrotePrefix = false;
    DecomposeModelName(model, base, prefix, &wrotePrefix);
    // base field [+304,+368), prefix field [+368,+416) — bounded to the record.
    WriteStrField(recv, kRecBaseName, kRecPrefix, base);   // *(rec+304)
    if (wrotePrefix)
        WriteStrField(recv, kRecPrefix, kRecScale, prefix); // *(rec+368)

    // *(rec+136) = off_649D64 (the active/owning universe).
    WritePtr(recv, kRecUniverse, g_activeUniverse);

    // --- object-node flag init ---
    *(B(node) + kNodeFlag530) |= 0x0Cu;                 // 0x402ac5
    *(B(node) + kNodeFlag529) &= ~0x02u;                // 0x402acf
    *(B(node) + kNodeByte535) = 2;                      // 0x402ad9
    WriteI32(node, kNodeDw536, 1);                      // 0x402ae3
    *(B(node) + kNodeFlag531) &= ~0x04u;                // 0x402af0
    // *(*(node+492)+2296) = 1.8f anim rate.
    void* sub = ReadPtr(node, kNodeSubObj);             // 0x402af6
    if (sub) WriteF32(sub, 2296, kNodeAnimRate);        // 0x402b00

    WriteF32(recv, kRecScale, kInitialScale);           // 0x402b0a: *(rec+416) = 1.0f
    WriteI32(recv, kRecUniverseId, -1);                 // 0x402b1b: *(rec+44) = -1
    WriteI32(recv, kRecGroupId, -1);                    // 0x402b25: *(rec+48) = -1

    // copy full model name -> rec+5 (byte-copy up to the NUL, as the original).
    // model-name field [+5,+40) — bounded to the record (kRecScriptH == 40).
    WriteStrField(recv, kRecModelName, kRecScriptH, model ? model : "");

    *(B(node) + kNodeFlag529) |= 0x04u;                 // 0x402b49

    // local-universe vs network-universe node bit (+529 & 0x08).
    if (h.indexFromPointer(ReadPtr(recv, kRecUniverse))) {  // 0x402b56
        *(B(node) + kNodeFlag529) &= ~0x08u;            // 0x402cc0
    } else {
        unsigned char v = *(B(node) + kNodeFlag529) & 0xF7u; // 0x402b73
        *(B(node) + kNodeFlag529) = v;                  // 0x402b76
        *(B(node) + kNodeFlag529) =
            static_cast<unsigned char>((8 * (h.networkBit & 1)) | v); // 0x402b83
    }

    WriteI32(node, kNodeWord72Off, kNodeWord72);        // 0x402b8e: *(node+72) = 0x3000000

    h.queryTerrainType(rec);                            // 0x402b97
    h.buildObjectCache(node);                           // 0x402ba4
    h.propagateDirtyFlag(node);                         // 0x402baf

    // --- creature-type probe (loc_5CB930 == strstr on rec+5) ---
    const char* name = reinterpret_cast<const char*>(B(recv) + kRecModelName);
    *(B(recv) + kRecTypeByte) = 1;                      // 0x402bbb: *(rec+4) = 1 (human)
    bool isAnimal = h.findSubstring(name, kNeedleRatte) // 0x402bbf
                 || h.findSubstring(name, kNeedleHund)  // 0x402cd3
                 || h.findSubstring(name, kNeedleKatze) // 0x402ce7
                 || h.findSubstring(name, kNeedlePferd);// 0x402cfb
    if (isAnimal) {                                     // loc_402BCC
        *(B(recv) + kRecTypeByte) = 2;                  // 0x402bd4: *(rec+4) = 2 (animal)
        if (h.findSubstring(name, kNeedleRatte)) {      // 0x402bd8
            *(B(node) + kNodeFlag529) &= ~0x08u;        // 0x402be4
            *(B(node) + kNodeFlag529) &= ~0x04u;        // 0x402bee
        }
        if (h.findSubstring(name, kNeedlePferd)) {      // 0x402bfd
            *(B(recv) + kRecTypeByte) |= 0x08u;         // 0x402c0f: *(rec+4) |= 8
            *(B(node) + kNodeFlag529) &= ~0x08u;        // 0x402c12
        }
    }

    // gait + idle preload (REUSES the PreloadAniSet reconstruction via hook).
    h.preloadAniSet(rec, kGaitIdle, 2);                 // 0x402c26

    if (h.lowPolyEnabled) {                             // 0x402c36 (dword_62D088)
        if (ReadPtr(recv, kRecLowPoly)) {               // 0x402c38: *(rec+492)
            h.preloadLowPolyAniSet(rec, kLowSet, 1);     // 0x402c49
            h.updateLowPolyMesh(rec);                    // 0x402c53
        }
    }

    *(B(recv) + kRecFlagsB) |= 0x10u;                   // 0x402c66: *(rec+141) |= 0x10
    return 1;                                            // 0x402c61
}

// ===========================================================================
// gilde.exe 0x402d10 — VIBE_Character_CreateFromModel.
// ===========================================================================
LiveActor* CreateFromModel(const char* model, void* parentMat) {
    LiveActor* rec = AllocSlot();                       // 0x402d15 (REUSED)
    if (CreateMesh(rec, model, parentMat))              // 0x402d20
        return rec;                                     // 0x402d29
    g_hooks.destroy(rec);                               // 0x402d31
    return nullptr;                                     // 0x402d2b
}

} // namespace guild::sim
