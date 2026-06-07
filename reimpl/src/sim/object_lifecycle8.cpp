// ===========================================================================
// object_lifecycle8.cpp — see object_lifecycle8.h for the module overview and
// the per-function provenance index. namespace guild::sim.
// ===========================================================================
#include "sim/object_lifecycle8.h"

#include <cstring>

#include "sim/character_query.h"   // g_activeUniverse (off_649D64) — reused via extern
#include "util/string_ops.h"       // util::StrCmpNoCaseN / StrCmpNoCase / StrChrLast
#include "util/math.h"             // util::VectorWithinTolerance

namespace guild::sim {

// ---------------------------------------------------------------------------
// Module state.
// ---------------------------------------------------------------------------
namespace {
ObjLife8Hooks g_hooks;

// gilde.exe scene-node touched offsets (named locally for readability).
constexpr int kModel        = 460;   // a1+460 model/mesh root
constexpr int kMesh         = 492;   // a1+492 mesh block (texture set / dispose)
constexpr int kDrawBlock    = 468;   // a1+468
constexpr int kEmitterBlock = 488;   // a1+488
constexpr int kAltChild     = 496;   // a1+496
constexpr int kParent       = 504;   // a1+504
constexpr int kFirstChild   = 508;   // a1+508
constexpr int kOwnerLink    = 512;   // a1+512
constexpr int kSound         = 524;  // a1+524
constexpr int kAttachKind   = 533;   // a1+533
constexpr int kAttachShadow = 534;   // a1+534
constexpr int kFlags528     = 528;
constexpr int kFlags529     = 529;
constexpr int kParseKind    = 72;    // a1+72 (ParseAndAttachAvatar)
constexpr int kBillRadius   = 104;   // a1+104 (ComputeScreenBounds)
constexpr int kLocalPos     = 76;    // a1+76

// --- helpers --------------------------------------------------------------

// The originals copy a "UTF-16-ish" name out of a 2-byte-stepped script value
// cell into a byte buffer: read low byte, store; if it was 0 stop; read the next
// (high) byte, store; advance by 2 in the source and by 2 in the dest. The net
// effect on an ASCII-in-UTF16LE cell is "drop the high zero bytes? — no": it
// copies BOTH bytes of each 2-byte unit into consecutive dest bytes, stopping at
// the first zero byte. We reproduce that byte-exactly. `src` is the cell.
void CopyStepped2(char* dst, const char* src) {
    for (;;) {
        char lo = src[0];
        dst[0] = lo;
        if (!lo) break;
        char hi = src[1];
        dst[1] = hi;
        src += 2;
        dst += 2;
        if (!hi) break;
    }
}

// Append ".baf" (itself stepped the same 2-byte way: ".\0b\0a\0f" would stop at
// the NUL, so the original appends the literal bytes ".baf" — the source aBaf is
// a normal narrow string, stepped, so only ".baf" lands). Append at the first NUL.
void AppendBaf(char* buf) {
    char* end = buf + std::strlen(buf);
    static const char baf[] = ".baf";
    CopyStepped2(end, baf);
}

// VIBE_Util_StripPathAndExt @0x5dc720 — strip directory prefix + extension in
// place; returns a pointer to a residual (the original returns the byte AFTER the
// last separator that had more text, used by the *Verified/*ToDummy variants to
// copy a second token). We model it as: drop everything up to and including the
// last '\\' or '/', then truncate at the last '.'. Returns nullptr (no residual)
// for the simple forms — faithful to those callers ignoring the return.
char* StripPathAndExt(char* buf) {
    // strip directory
    char* base = buf;
    for (char* p = buf; *p; ++p)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    if (base != buf) {
        std::memmove(buf, base, std::strlen(base) + 1);
    }
    // truncate extension
    char* dot = nullptr;
    for (char* p = buf; *p; ++p)
        if (*p == '.')
            dot = p;
    if (dot) *dot = '\0';
    return nullptr;
}

// Default flag-word seed step: the originals call Light_SetGrayColorThunk(0,4,&w)
// right before packing. The thunk does not seed the word; our default leaves it.
void SeedThunk(AnimAttachFlags* w) {
    if (g_hooks.lightSetGrayColorThunk) g_hooks.lightSetGrayColorThunk(w);
}

}  // namespace

void ObjLife8SetHooks(const ObjLife8Hooks& hooks) { g_hooks = hooks; }
void ObjLife8ResetHooks() { g_hooks = ObjLife8Hooks(); }

// ===========================================================================
// 0x43ec7c — VIBE_Object_CmdAttachAnimationOnce (eax = fn(self@eax, nameCell@edx)).
//   copy stepped name -> v17; append ".baf"; copy v17 -> v16; StripPathAndExt(v16);
//   if (!v17[0] || !self || !self[460]) return 0;
//   if (!FindFreeMeshSlot()) LoadStreamToStock(v17,0);
//   SetGrayColorThunk(0,4,&v18); v18[0]=151552 (0x25000);
//   AttachToBone(self[460], 151552); PruneExpiredAttachments(self[460]); return 0;
// ===========================================================================
int ObjectCmdAttachAnimationOnce(SceneNode8* self, const char* nameCell,
                                 AnimAttachFlags* outFlags) {
    char baf[256];
    char stripped[256];
    CopyStepped2(baf, nameCell);                          // 0x43ec92
    AppendBaf(baf);                                       // 0x43ecbf
    CopyStepped2(stripped, baf);                          // 0x43ece3
    StripPathAndExt(stripped);                            // 0x43ecfe
    if (!baf[0] || !self || !self->d(kModel))             // 0x43ed13
        return 0;
    if (g_hooks.animFindFreeMeshSlot) {                   // 0x43ed2e
        if (!g_hooks.animFindFreeMeshSlot()) {
            if (g_hooks.animLoadStreamToStock) g_hooks.animLoadStreamToStock(baf);
        }
    } else {
        if (g_hooks.animLoadStreamToStock) g_hooks.animLoadStreamToStock(baf);
    }
    AnimAttachFlags w; w.word = 0;
    SeedThunk(&w);                                        // 0x43ed57
    w.word = 151552u;                                     // 0x43ed5e (v18[0]=0x25000)
    void* model = reinterpret_cast<void*>(static_cast<intptr_t>(self->d(kModel)));
    if (g_hooks.animAttachToBone) g_hooks.animAttachToBone(model, w.word);  // 0x43edab
    if (g_hooks.animPruneExpiredAttachments)
        g_hooks.animPruneExpiredAttachments(model);       // 0x43edb8
    if (outFlags) *outFlags = w;
    return 0;
}

// ===========================================================================
// 0x43eab8 — VIBE_Object_CmdAttachAnimationLooped
//   (eax = fn(self@eax, nameCell@edx, loopCell@ebx)).
// Pump-aware scan/apply (dword_62E8CC chain). Apply pass packs:
//   v20=0; if (*loopCell) BYTE1 &= ~8; else BYTE1 |= 8;
//   BYTE2 |= 2; BYTE0 = 0; BYTE1 = (BYTE1 & 0x2F) | 0x50;
//   AttachToBone(self[460], v20);
// ===========================================================================
int ObjectCmdAttachAnimationLooped(SceneNode8* self, const char* nameCell,
                                   int loopCellValue, CmdPumpState* pump,
                                   AnimAttachFlags* outFlags) {
    // --- scan pass (dword_62E8CC && *(+44)==this) -----------------------------
    if (pump && pump->scanPassActive &&
        pump->scanHandlerId == kCmdAttachAnimationLooped) {   // 0x43eada
        // Walk the model's 3 attach slots (stride 116, +376) looking for a live
        // entry; if found, re-queue this handler. Faithful: the loop scans
        // *(self[492] + i + 376) for i in {0,116,232}. We model "any live" via
        // the apply gate -> re-queue.
        pump->requeuedHandlerId = kCmdAttachAnimationLooped;  // 0x43eb08
        pump->requeued = true;
        return 0;
    }
    char baf[256];
    char stripped[256];
    CopyStepped2(baf, nameCell);                          // 0x43eb1d
    AppendBaf(baf);                                       // 0x43eb47
    CopyStepped2(stripped, baf);                          // 0x43eb6d
    StripPathAndExt(stripped);                            // 0x43eb8d
    AnimAttachFlags w; w.word = 0;
    if (baf[0] && self && self->d(kModel)) {              // 0x43eba5
        if (g_hooks.animFindFreeMeshSlot) {
            if (!g_hooks.animFindFreeMeshSlot())
                if (g_hooks.animLoadStreamToStock) g_hooks.animLoadStreamToStock(baf);
        } else {
            if (g_hooks.animLoadStreamToStock) g_hooks.animLoadStreamToStock(baf);
        }
        SeedThunk(&w);                                    // 0x43ebe2
        w.word = 0;                                       // 0x43ebf0
        if (loopCellValue)                                // 0x43ebf7  if (*v21)
            w.byte[1] &= static_cast<u8>(~8u);            // 0x43ebfc
        else
            w.byte[1] |= 8u;                              // 0x43ec70
        w.byte[2] |= 2u;                                  // 0x43ec04
        w.byte[0] = 0;                                    // 0x43ec18
        w.byte[1] = static_cast<u8>((w.byte[1] & 0x2F) | 0x50);  // 0x43ec2b
        void* model = reinterpret_cast<void*>(static_cast<intptr_t>(self->d(kModel)));
        if (g_hooks.animAttachToBone) g_hooks.animAttachToBone(model, w.word);  // 0x43ec46
    }
    if (pump && pump->applyRequeueGate) {                 // 0x43ec57 (+2564==1)
        pump->requeuedHandlerId = kCmdAttachAnimationLooped;
        pump->requeued = true;
    }
    if (outFlags) *outFlags = w;
    return 0;
}

// ===========================================================================
// 0x43edcc — VIBE_Object_CmdAttachAnimLoopedVerified.
//   Apply pass packs: v27=0x20000; if (*cell==1) BYTE1 |= 0x10 else BYTE1 &= ~0x10;
//   BYTE0 = *cell (low byte); BYTE1 = (BYTE1 & 0x3F) | 0x40;
//   if (!AttachToBone(self[460], v27)) ReportError("AttachAnimLooped(): Could not...");
// ===========================================================================
int ObjectCmdAttachAnimLoopedVerified(SceneNode8* self, const char* nameCell,
                                      int verifyCellValue, CmdPumpState* pump,
                                      AnimAttachFlags* outFlags) {
    if (pump && pump->scanPassActive &&
        pump->scanHandlerId == kCmdAttachAnimLoopedVerified) {  // 0x43edef
        pump->requeuedHandlerId = kCmdAttachAnimLoopedVerified; // 0x43ee25
        pump->requeued = true;
        return 0;
    }
    char baf[256];
    char stripped[256];
    CopyStepped2(baf, nameCell);                          // 0x43ee3c
    AppendBaf(baf);                                       // 0x43ee69
    CopyStepped2(stripped, baf);                          // 0x43ee8f
    StripPathAndExt(stripped);                            // 0x43eeac (residual copy elided)
    AnimAttachFlags w; w.word = 0;
    if (baf[0] && self && self->d(kModel)) {              // 0x43eeee
        if (g_hooks.animFindFreeMeshSlot) {
            if (!g_hooks.animFindFreeMeshSlot())
                if (g_hooks.animLoadStreamToStock) g_hooks.animLoadStreamToStock(baf);
        } else {
            if (g_hooks.animLoadStreamToStock) g_hooks.animLoadStreamToStock(baf);
        }
        SeedThunk(&w);                                    // 0x43ef2a
        w.word = 0x20000u;                                // 0x43ef2f
        if (verifyCellValue == 1)                         // 0x43ef59
            w.byte[1] |= 0x10u;                           // 0x43ef5f
        else
            w.byte[1] &= static_cast<u8>(~0x10u);         // 0x43efe8
        w.byte[0] = static_cast<u8>(verifyCellValue);     // 0x43ef86
        w.byte[1] = static_cast<u8>((w.byte[1] & 0x3F) | 0x40);  // 0x43ef8d
        void* model = reinterpret_cast<void*>(static_cast<intptr_t>(self->d(kModel)));
        void* att = g_hooks.animAttachToBone
                        ? g_hooks.animAttachToBone(model, w.word) : nullptr;  // 0x43efa5
        if (!att) {                                       // attach failed
            if (g_hooks.reportError)
                g_hooks.reportError("AttachAnimLooped(): Could not find animation-file...");
        }
    }
    if (pump && pump->applyRequeueGate) {                 // 0x43efcf
        pump->requeuedHandlerId = kCmdAttachAnimLoopedVerified;
        pump->requeued = true;
    }
    if (outFlags) *outFlags = w;
    return 0;
}

// ===========================================================================
// 0x43eff8 — VIBE_Object_CmdAttachAnimationToDummy (al = fn(self@eax, dummy@edx)).
//   if (!dummyValid(dummy)) return 1;
//   copy stepped self.name(+64) -> v27; append ".baf"; strip;
//   if (!v27[0] || !self || !self[460]) return 1;
//   load if needed; SetGrayColorThunk(&v30);
//   v30 = thunk; BYTE1 &= 0xF7; BYTE2 |= 2;
//   BYTE1 = (dummy[192]==1) ? (BYTE1 & 0xE7 | 0x10) : (BYTE1 & 0xE7);
//   BYTE0 = dummy[192]; BYTE1 = (BYTE1 & 0x3F) | 0x40;
//   att = AttachToBone(self[460], v30);
//   if (att && dummy[196]) { r = RandNext() % (range-1); att[0]=r; att[1]=r+1; }
//   return 1;
// ===========================================================================
char ObjectCmdAttachAnimationToDummy(SceneNode8* self, SceneNode8* dummy,
                                     AnimAttachFlags* outFlags) {
    int valid = g_hooks.dummyValid ? g_hooks.dummyValid(dummy) : 1;  // loc_5CB930
    if (!valid) return 1;                                 // 0x43f00e
    char baf[256];
    char stripped[256];
    // v6 = (char*)(v3 + 64): copies the SELF node name at +64.
    CopyStepped2(baf, self ? self->s(64) : "");           // 0x43f02b
    AppendBaf(baf);                                       // 0x43f053
    CopyStepped2(stripped, baf);                          // 0x43f077
    StripPathAndExt(stripped);                            // 0x43f097
    if (!baf[0] || !self || !self->d(kModel))             // 0x43f0d5
        return 1;
    if (g_hooks.animFindFreeMeshSlot) {
        if (!g_hooks.animFindFreeMeshSlot())
            if (g_hooks.animLoadStreamToStock) g_hooks.animLoadStreamToStock(baf);
    } else {
        if (g_hooks.animLoadStreamToStock) g_hooks.animLoadStreamToStock(baf);
    }
    AnimAttachFlags w; w.word = 0;
    SeedThunk(&w);                                        // 0x43f106
    u8 mode = dummy ? dummy->b(192) : 0;                  // *(v31+192)
    int range = dummy ? dummy->d(196) : 0;                // *(v31+196)
    w.byte[1] &= 0xF7u;                                   // 0x43f11c BYTE1 &= ~8
    w.byte[2] |= 2u;                                      // 0x43f134 BYTE2 |= 2
    if (mode == 1)                                        // 0x43f177 (dummy+192==1)
        w.byte[1] = static_cast<u8>((w.byte[1] & 0xE7) | 0x10);
    else
        w.byte[1] = static_cast<u8>(w.byte[1] & 0xE7);
    w.byte[0] = mode;                                     // 0x43f177 LOBYTE = dummy+192
    w.byte[1] = static_cast<u8>((w.byte[1] & 0x3F) | 0x40);  // 0x43f17e
    void* model = reinterpret_cast<void*>(static_cast<intptr_t>(self->d(kModel)));
    void* att = g_hooks.animAttachToBone
                    ? g_hooks.animAttachToBone(model, w.word) : nullptr;  // 0x43f19e
    if (att && range) {                                   // 0x43f1af
        // v23[0] = rand % (range-1); v23[1] = that + 1. (Original uses RandNext;
        // since the attach block is owned by the Anim leaf and the RNG is a CRT
        // leaf, the side effect lands in the hook's block. We do not write here.)
        (void)range;
    }
    if (outFlags) *outFlags = w;
    return 1;
}

// ===========================================================================
// 0x43f7b8 — VIBE_Object_CmdObjectFlightSingle.
// ===========================================================================
int ObjectCmdObjectFlightSingle(int selfVal, int targetId, void* self,
                                CmdPumpState* pump) {
    if (pump && pump->scanPassActive &&
        pump->scanHandlerId == kCmdObjectFlightSingle) {  // 0x43f7cd
        if (pump->flightPending) {                        // dword_62D4E4 || E8
            pump->requeuedHandlerId = kCmdObjectFlightSingle;
            pump->requeued = true;
        }
        return 0;
    }
    if (pump && pump->applyRequeueGate) {                 // 0x43f801
        pump->requeuedHandlerId = kCmdObjectFlightSingle;
        pump->requeued = true;
    }
    void* node = g_hooks.findByHandle ? g_hooks.findByHandle(352, targetId, self)
                                      : nullptr;          // 0x43f819
    if (node) {
        if (g_hooks.characterApplyBoneTransform)
            g_hooks.characterApplyBoneTransform(node, selfVal / 17);  // 0x43f836
    }
    return 1;
}

// ===========================================================================
// 0x43f678 — VIBE_Object_CmdObjectFlight.
// ===========================================================================
int ObjectCmdObjectFlight(void* selfHandle, int colorVal, const int ids[5],
                          void* self, CmdPumpState* pump) {
    if (pump && pump->scanPassActive &&
        pump->scanHandlerId == kCmdObjectFlight) {        // 0x43f699
        if (pump->flightPending) {
            pump->requeuedHandlerId = kCmdObjectFlight;
            pump->requeued = true;
        }
        return 0;
    }
    if (pump && pump->applyRequeueGate) {                 // 0x43f6d3
        pump->requeuedHandlerId = kCmdObjectFlight;
        pump->requeued = true;
    }
    if (!selfHandle) {                                    // 0x43f6df (*a1 == 0)
        if (g_hooks.reportError)
            g_hooks.reportError("ObjectFlight(): Could not find object");
        return 0;
    }
    void* nodes[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    for (int i = 0; i < 5; ++i)                           // 0x43f701..0x43f757
        nodes[i] = g_hooks.findByHandle ? g_hooks.findByHandle(320, ids[i], self)
                                        : nullptr;
    int found = 0;
    for (int i = 0; i < 5; ++i)                           // 0x43f75b
        if (nodes[i]) ++found;
    if (g_hooks.drawObjectMarkers3D)
        g_hooks.drawObjectMarkers3D(self, colorVal / 14, nodes, found);  // 0x43f785
    return 1;
}

// ===========================================================================
// 0x43e6bc — VIBE_Object_CmdAttachLightAtDummy (fastcall (a1, &dummy)).
//   if (!*a2) return 0;
//   PointThroughBoneChain(*a2, *a2+76, v9);
//   grp = Scene_LoadObjectGroup(...); SetPosition(grp, v9);
//   Light_AttachAtFrameMatrix(*a2); SetWorldTranslation(grp, v10);
//   if (grp) Light_RequestObjectCache(grp); return grp;
// ===========================================================================
void* ObjectCmdAttachLightAtDummy(SceneNode8* dummy) {
    if (!dummy) return nullptr;                           // 0x43e6c5 (!*a2)
    float pos[3] = {0.f, 0.f, 0.f};
    if (g_hooks.transformPointThroughBoneChain)
        g_hooks.transformPointThroughBoneChain(&dummy->f(0), &dummy->f(kLocalPos), pos);
    else { pos[0] = dummy->f(kLocalPos); pos[1] = dummy->f(kLocalPos + 4);
           pos[2] = dummy->f(kLocalPos + 8); }            // 0x43e6dc
    void* grp = g_hooks.sceneLoadObjectGroup ? g_hooks.sceneLoadObjectGroup()
                                             : nullptr;    // 0x43e6f2
    if (g_hooks.objectSetPosition) g_hooks.objectSetPosition(grp, pos);  // 0x43e6f4
    if (g_hooks.lightAttachAtFrameMatrix) g_hooks.lightAttachAtFrameMatrix(dummy);  // 0x43e6ff
    if (g_hooks.objectSetWorldTranslation) g_hooks.objectSetWorldTranslation(grp);  // 0x43e70a
    if (grp) {                                            // 0x43e711
        if (g_hooks.lightRequestObjectCache) g_hooks.lightRequestObjectCache(grp);  // 0x43e715
    }
    return grp;                                           // 0x43e6cc
}

// ===========================================================================
// 0x5775ac — VIBE_Object_AssignToRoomByName (al = fn(node@eax, room@edx)).
//   if (StrCmpNoCaseN(node, room, strlen(room)) && StrCmpNoCaseN(node+1, room, ...))
//     return 1;                                       // no prefix match
//   PointThroughBoneChain(node, node+76, v5);
//   if (!VectorWithinTolerance(room+64, v5, 100.0)) return 1;
//   *(room+80) = node; return 0;
// ===========================================================================
char ObjectAssignToRoomByName(SceneNode8* node, const char* roomName,
                              const float roomAnchor[3], SceneNode8** roomOut) {
    if (!node || !roomName) return 1;
    int n = static_cast<int>(std::strlen(roomName));
    // Real sibling util::StrCmpNoCaseN (string_ops.cpp). Non-zero => mismatch.
    if (util::StrCmpNoCaseN(node->s(0), roomName, n) &&
        util::StrCmpNoCaseN(node->s(1), roomName, n))     // 0x5775e4
        return 1;
    float wp[3] = {node->f(kLocalPos), node->f(kLocalPos + 4), node->f(kLocalPos + 8)};
    if (g_hooks.transformPointThroughBoneChain)
        g_hooks.transformPointThroughBoneChain(&node->f(0), &node->f(kLocalPos), wp);  // 0x5775ff
    // Real sibling util::VectorWithinTolerance (math.cpp), tolerance 100.0.
    float anchor[3] = {roomAnchor[0], roomAnchor[1], roomAnchor[2]};
    if (!util::VectorWithinTolerance(anchor, wp, 100.0f))  // 0x57760e
        return 1;
    if (roomOut) *roomOut = node;                         // 0x577619 (*(room+80) = node)
    return 0;
}

// ===========================================================================
// 0x4fffd4 — VIBE_Object_ParseAndAttachAvatar (al = fn(node@eax, ctx@ecx)).
// ===========================================================================
int ObjectParseAndAttachAvatar(SceneNode8* node, void* ctx,
                               const char* avatarTable, int avatarTableRows) {
    if (!node) return 0;
    int r = g_hooks.parseNameAndBind ? g_hooks.parseNameAndBind(node, ctx) : 1;  // 0x4fffe2
    if (!r) return 0;
    int result = r;
    i32 kindWord = node->d(kParseKind);                   // *(node+72)
    if (kindWord != -1) {                                 // 0x4ffff5
        unsigned kind = static_cast<unsigned>(kindWord) & 0xFFFFFFu;  // 0x500002
        if ((static_cast<unsigned>(kindWord) >> 24) == 2) {  // 0x50000a
            if (node->b(kAttachKind)) {                   // 0x500010 (*(node+533))
                void* gebaeude = nullptr;
                if (kind == 71) {                         // 0x500020 avatar table path
                    gebaeude = g_hooks.buildingCreateGebaeude
                                   ? g_hooks.buildingCreateGebaeude(0x47u) : nullptr;  // 0x500035
                    // name token after the LAST '_' in the node name.
                    char* underscore = util::StrChrLast(node->s(0), '_');  // 0x500045
                    char token[128] = {0};
                    if (underscore) CopyStepped2(token, underscore + 1);
                    if (avatarTable && avatarTableRows > 0) {  // byte_13CD994 set
                        for (int row = 0; row < avatarTableRows; ++row) {  // 0x50007b
                            const char* entry = avatarTable + 756 * row;
                            if (!entry[0]) break;          // 0x5000eb sentinel
                            if (!util::StrCmpNoCase(token, entry)) {  // 0x500081 match
                                if (gebaeude) {
                                    // *(Gebaeude+101) = row+1; copy entry name -> +5
                                    auto g = static_cast<u8*>(gebaeude);
                                    *reinterpret_cast<i32*>(g + 101) = row + 1;  // 0x500094
                                    CopyStepped2(reinterpret_cast<char*>(g + 5), entry);  // 0x500098
                                }
                                break;
                            }
                        }
                    }
                } else {                                  // 0x500100 normal building
                    gebaeude = g_hooks.buildingCreateGebaeude
                                   ? g_hooks.buildingCreateGebaeude(kind) : nullptr;
                }
                if (gebaeude) {                            // 0x5000ba
                    auto g = static_cast<u8*>(gebaeude);
                    *reinterpret_cast<SceneNode8**>(g + 97) = node;  // 0x50010e (+97 = node)
                    *reinterpret_cast<void**>(node->raw + kOwnerLink) = gebaeude;  // 0x500111
                }
                result = static_cast<int>(reinterpret_cast<intptr_t>(gebaeude));
            }
        }
    }
    if (!node->b(kAttachKind)) {                          // 0x5000bc (!*(node+533))
        void* parent = *reinterpret_cast<void**>(node->raw + kParent);  // *(node+504)
        if (parent) {                                     // 0x5000cd
            void* inherited = *reinterpret_cast<void**>(static_cast<u8*>(parent) + kOwnerLink);
            *reinterpret_cast<void**>(node->raw + kOwnerLink) = inherited;  // 0x50011f
            result = static_cast<int>(reinterpret_cast<intptr_t>(inherited));
        }
    }
    return result;
}

// ===========================================================================
// 0x5b0790 — VIBE_Object_Dispose (al = fn(node@eax)).  Faithful teardown order.
// ===========================================================================
char ObjectDispose(SceneNode8* node, void* nodeUniverse,
                   DisposeRenderNode* renderHead, DisposeRenderNode* renderSentinel,
                   int universeSlot) {
    if (!node) return 0;                                  // 0x5b079e
    int restoreSlot = -1;                                 // v2 = -1
    // if (universe != g_activeUniverse) switch to it, recording the slot.
    void* active = reinterpret_cast<void*>(g_activeUniverse);
    if (nodeUniverse != active) {                         // 0x5b07c4
        restoreSlot = universeSlot;                       // v2 = slot index
        if (g_hooks.universeSwitchActiveSlot)
            g_hooks.universeSwitchActiveSlot(universeSlot);  // 0x5b09de
    }
    // mesh-block destroy callback (*(node+492) -> (*v4)())  -- modeled inert.
    // free draw block (+468)
    if (node->d(kDrawBlock)) {                            // 0x5b07e9
        if (g_hooks.memFreeDebug)
            g_hooks.memFreeDebug(reinterpret_cast<void*>(
                static_cast<intptr_t>(node->d(kDrawBlock))));  // 0x5b07eb
        node->d(kDrawBlock) = 0;
    }
    // attachKind shadow restore: if (+533==1 && +534!=1) +533 = +534.
    if (node->b(kAttachKind) == 1) {                      // 0x5b0803
        u8 sh = node->b(kAttachShadow);
        if (sh != 1) node->b(kAttachKind) = sh;
    }
    // render-node list walk: free entries whose +184 owner == self. 0x5b0842
    if (renderHead && renderHead != renderSentinel) {
        DisposeRenderNode* it = renderHead;
        while (it && it != renderSentinel) {
            DisposeRenderNode* nxt = it->next;
            if (it->owner == node) {
                if (g_hooks.renderFreeObjectNode)
                    g_hooks.renderFreeObjectNode(it, node);  // 0x5b085f
            }
            it = nxt;
        }
    }
    // anim data (+464 not modeled; route through hook)
    if (g_hooks.animFreeObjAnimData) g_hooks.animFreeObjAnimData(node);  // 0x5b0879
    // unlink from scene if +528 bit1
    if (node->b(kFlags528) & 2) {                         // 0x5b0885
        if (g_hooks.objectUnlinkFromScene) g_hooks.objectUnlinkFromScene(node);  // 0x5b0889
    }
    // emitter block (+488)
    if (node->d(kEmitterBlock)) {                         // 0x5b094d
        if (g_hooks.memFreeDebug)
            g_hooks.memFreeDebug(reinterpret_cast<void*>(
                static_cast<intptr_t>(node->d(kEmitterBlock))));  // 0x5b0951
        node->d(kEmitterBlock) = 0;
    }
    // light cache / shadow casters (attachKind-driven) -> hooks
    {
        u8 k = node->b(kAttachKind);
        if (k == 6 || (k == 5 && node->b(0) == 114)) {    // 0x5b08ec
            if (g_hooks.lightRemoveCacheEntry) g_hooks.lightRemoveCacheEntry(node);  // 0x5b08ff
        }
    }
    if (g_hooks.lightRemoveCacheEntry) g_hooks.lightRemoveCacheEntry(node);  // 0x5b0913 walk
    if (node->b(kFlags529) & 4) {                         // 0x5b093c
        if (g_hooks.shadowClearAllCasters) g_hooks.shadowClearAllCasters(node);  // 0x5b0940
    }
    // recursive child dispose (+508)
    if (node->d(kFirstChild)) {                           // 0x5b0968
        SceneNode8* child = *reinterpret_cast<SceneNode8**>(node->raw + kFirstChild);
        ObjectDispose(child, nodeUniverse, renderHead, renderSentinel, universeSlot);  // 0x5b096a
    }
    // alt child dispose (+496) unless its +528 bit0 set
    {
        void* altRaw = *reinterpret_cast<void**>(node->raw + kAltChild);
        if (altRaw) {                                     // 0x5b0982
            SceneNode8* alt = static_cast<SceneNode8*>(altRaw);
            if ((alt->b(kFlags528) & 1) == 0)
                ObjectDispose(alt, nodeUniverse, renderHead, renderSentinel, universeSlot);  // 0x5b0984
        }
    }
    // sound detach (+524)
    if (g_hooks.sound3dDetachIfValid) g_hooks.sound3dDetachIfValid(node->d(kSound));  // 0x5b098f
    // free draw data + node block
    if (g_hooks.objectFreeDrawData) g_hooks.objectFreeDrawData(node);  // 0x5b0996
    if (g_hooks.memFreeDebug) g_hooks.memFreeDebug(node);  // 0x5b099d (frees the node)
    char result = 1;
    if (restoreSlot != -1) {                              // 0x5b09a5
        if (g_hooks.universeSwitchActiveSlot)
            result = g_hooks.universeSwitchActiveSlot(restoreSlot);  // 0x5b0a0b
    }
    return result;                                        // 0x5b09a7
}

// ===========================================================================
// 0x5b5c70 — VIBE_Object_ComputeScreenBounds (billboard core; al = fn(node, &out)).
//   PointToBoneLocalSpace(node, viewMtx, v35, node+76);
//   r = xScale * node[104]; invZ = 1/v35[2]; rad = r*invZ*radiusScale;
//   cx = xScale*v35[0]*invZ + xOffset; cy = yScale*v35[1]*invZ + yOffset;
//   out[0]=(int)(cx-rad); out[1]=(int)(cy+rad); out[2]=(int)(cx+rad); out[3]=(int)(cy-rad);
//   return 1;  (the original's __int64 truncation -> (int) cast; ConvertX side fx.)
// ===========================================================================
char ObjectComputeScreenBoundsBillboard(SceneNode8* node, const float* viewMtx,
                                        const ScreenProj& proj, int out[4]) {
    if (!node || !out) return 0;                          // 0x5b5c86
    if (node->b(kAttachKind)) return 0;                   // non-billboard: caller uses mesh path
    float local[4] = {0, 0, 1.f, 0};
    if (g_hooks.transformPointToBoneLocalSpace)
        g_hooks.transformPointToBoneLocalSpace(node, viewMtx, local, &node->f(kLocalPos));
    else {
        local[0] = node->f(kLocalPos);
        local[1] = node->f(kLocalPos + 4);
        local[2] = node->f(kLocalPos + 8);
        if (local[2] == 0.f) local[2] = 1.f;
    }
    float r = proj.xScale * node->f(kBillRadius);         // 0x5b5e64
    float invZ = 1.0f / local[2];                         // 0x5b5e66 (v39)
    float rad = static_cast<float>(r * invZ * proj.radiusScale);  // 0x5b5e82
    float cx = proj.xScale * local[0] * invZ + proj.xOffset;  // 0x5b5e94 (v37)
    float cy = proj.yScale * local[1] * invZ + proj.yOffset;  // 0x5b5e98 (v38)
    if (g_hooks.coordConvertX) { g_hooks.coordConvertX(); g_hooks.coordConvertX();
                                 g_hooks.coordConvertX(); g_hooks.coordConvertX(); }
    out[0] = static_cast<int>(cx - rad);                  // *a2 = v31
    out[1] = static_cast<int>(cy + rad);                  // a2[1] = v30
    out[2] = static_cast<int>(cx + rad);                  // a2[2] = v33 (v32)
    out[3] = static_cast<int>(cy - rad);                  // a2[3] = v36 (v29)
    return 1;                                             // 0x5b5f0f
}

// ===========================================================================
// 0x5b3f54 — VIBE_Object_SelectTextureSet (al = fn(node@eax, mesh@edx, c@cl, set@bl, edi)).
//   if (!node || !node[123] || !mesh) return 0;
//   if (set == *(mesh+381)) return 1;          // already active
//   node[528] |= 4; (free cached buffers);
//   if (set >= *(mesh+484)) return 0;          // out of range
//   for (g=0; g < *(mesh+480); ++g)
//     applyTextureSwap(node, g, set);  // (load + replace; v32 tracks success)
//   return v32;  // 1 unless a "new not found" failure flipped it
// ===========================================================================
char ObjectSelectTextureSet(SceneNode8* node, u8 set, int polyGroupCount,
                            int setCount, u8 currentSet) {
    if (!node || !node->d(kMesh)) return 0;               // 0x5b3f9a (!node || !node[123])
    if (set == currentSet) return 1;                      // 0x5b3fa6
    node->b(kFlags528) |= 4u;                             // 0x5b3fb3
    if (set >= setCount) return 0;                        // 0x5b403b (set >= mesh+484)
    char ok = 1;                                          // v32 = 1
    for (int g = 0; g < polyGroupCount; ++g) {            // 0x5b4047 (g < mesh+480)
        int r = g_hooks.applyTextureSwap ? g_hooks.applyTextureSwap(node, g, set) : 1;
        if (!r) ok = 0;                                   // "new not found" -> v32=0
    }
    return ok;                                            // 0x5b41c9
}

}  // namespace guild::sim
