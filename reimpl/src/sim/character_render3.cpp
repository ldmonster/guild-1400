// character_render3 — third cluster of Character render/anim-coupled leaves (gilde.exe).
// Faithful 1:1 ports of the animation-attach / preload family, the footstep / noise
// audio leaves, and the camera ray-projection helpers. Every animation / path /
// renderer / sound / object call is routed through CharRender3Hooks; the default hook
// table is inert so the path formatting, table arithmetic, and ray math are golden-
// testable in isolation. Pure leaves (StrCmpNoCase, VectorNormalize,
// VectorWithinTolerance) delegate to their reconstructed homes (guild::util) — not
// redefined here.
#include "sim/character_render3.h"

#include <cstdio>    // std::snprintf (reproduces VIBE_Crt_Sprintf_0 formatting)

#include "util/string_ops.h"   // VIBE_Util_StrCmpNoCase   @0x5cb8f0
#include "util/math.h"         // VIBE_Math_VectorNormalize @0x5cb148,
                               // VIBE_Math_VectorWithinTolerance @0x5caa4c

namespace guild::sim {

// ===========================================================================
// Recovered string constants.
// ===========================================================================
namespace {
const char kFmtAniPath[]     = "character/%s/%s_%s.baf";
const char kFmtLowPolyPath[] = "lowpolycharacter/%s/%s_%s_LOW.baf";
const char kBewegungGehen[]  = "bewegung/gehen";          // aBewegungGehen
const char kBewegungKarren[] = "bewegung/karren_ziehen";  // aBewegungKarren
const char kD3LeftHand[]     = "d3_LeftHand";
const char kD3RightHand[]    = "d3_RightHand";
const char kD3Head[]         = "d3_Head";
const char kSndNormal[]      = "Normal_s";
const char kSndErde[]        = "Erde_s";
const char kSndWiese[]       = "Wiese_s";
const char kSndStein[]       = "Stein_s";
const char kSndPfuetze[]     = "Pfuetze_s";

// AttachAni mask: 0x20000 | (((8*(seq&1)) | 0xD0) << 8).
constexpr int kAttachAniBase = 0x20000;
// AttachMotion mask == 153600 == 0x25800.
constexpr int kAttachMotionMask = 153600;
// AttachMorph mask == 0x20000 | (24 << 8) == 0x21800.
constexpr int kAttachMorphMask = 0x20000 | (24 << 8);
// PreloadLowPoly mask == 133120 == 0x20800.
constexpr int kLowPolyMask = 133120;
} // namespace

// ---------------------------------------------------------------------------
// Hook table (inert defaults).
// ---------------------------------------------------------------------------
namespace {
const CharRender3Hooks* g_hooks = nullptr;

void  DefConvert(char*) {}
void* DefFindFree() { return nullptr; }
void  DefGrayColor(int, int) {}
void* DefLoadStream(const char*, int) { return nullptr; }
void* DefAttachBone(void*, int) { return nullptr; }
void  DefPrune(void*) {}
void* DefCreateMorph(void*, const char*) { return nullptr; }
void  DefBoneDelta(void*, void*) {}
void* DefAttachUniverse(void*, const char*) { return nullptr; }
void  DefDetach(void*) {}
void  DefObjAttachBone(void*, const char*) {}
void  DefBuildLight(void*) {}
int   DefSwitchUniverse(int) { return 0; }
void  DefSetPivot(void*, const float[3]) {}
void  DefSetPos(void*, const float[3]) {}
float DefPlaySample(void*, const char*) { return 0.0f; }
void  DefPlayOneShot(float, void*, int, float) {}
void  DefReportError(const char*) {}

const CharRender3Hooks g_default = {
    DefConvert, DefFindFree, DefGrayColor, DefLoadStream, DefAttachBone, DefPrune,
    DefCreateMorph, DefBoneDelta, DefAttachUniverse, DefDetach, DefObjAttachBone,
    DefBuildLight, DefSwitchUniverse, DefSetPivot, DefSetPos, DefPlaySample,
    DefPlayOneShot, DefReportError,
};
} // namespace

void SetCharRender3Hooks(const CharRender3Hooks* h) { g_hooks = h; }
const CharRender3Hooks& GetCharRender3Hooks() { return g_hooks ? *g_hooks : g_default; }

// ===========================================================================
// Path / name formatting.
// ===========================================================================
// The originals call VIBE_Crt_Sprintf_0(buf, "character/%s/%s_%s.baf", base, name, base)
// then build "%s_%s" and run VIBE_Path_ConvertBackslashToSlash on the second buffer (the
// dialog name). We reproduce the primary path string (the one fed to LoadStreamToStock).
void BuildAniPath(char out[256], const char* base, const char* name) {
    std::snprintf(out, 256, kFmtAniPath, base, name, base);
}
void BuildLowPolyAniPath(char out[256], const char* base, const char* name) {
    std::snprintf(out, 256, kFmtLowPolyPath, base, name, base);
}
bool IsLoopingGait(const char* name) {
    // !StrCmp(name, "bewegung/gehen") || !StrCmp(name, "bewegung/karren_ziehen").
    // The originals use StrCmp here (AttachAni) and StrCmpNoCase elsewhere; both reduce
    // to "is this one of the two gait clips". We fold to the case-insensitive compare so
    // the predicate is robust (the clip names are fixed lowercase literals).
    return guild::util::StrCmpNoCase(name, kBewegungGehen) == 0 ||
           guild::util::StrCmpNoCase(name, kBewegungKarren) == 0;
}

const char* FootstepSampleForTerrain(int terrain) {
    // v5 dispatch in PlayFootstepSound: 6/11 -> Stein, 3 -> Erde, 4 -> Wiese,
    // 8 -> Stein, 10 -> Pfuetze, anything else -> the appended "Normal_s" buffer.
    switch (terrain) {
        case 3:  return kSndErde;
        case 4:  return kSndWiese;
        case 6:  return kSndStein;
        case 8:  return kSndStein;
        case 10: return kSndPfuetze;
        case 11: return kSndStein;
        default: return kSndNormal;
    }
}

// ===========================================================================
// Animation-attach family.
// ===========================================================================

// gilde.exe 0x404038 — VIBE_Character_AttachAni.
//   ConvertBackslashToSlash(name);
//   Sprintf(path, "character/%s/%s_%s.baf", base, name, base);
//   Sprintf(dlg, "%s_%s", name, base); ConvertBackslashToSlash(dlg);
//   if (!path[0]) goto store;                                   // empty name
//   slot = FindFreeMeshSlot(); SetGrayColorThunk(0,4);
//   mask = 0x20000; BYTE1(mask) = (8*(seq&1)) | 0xD0;
//   if (!slot) { loop = !StrCmp(name,gehen)||!StrCmp(name,karren);
//                LoadStreamToStock(path, loop); }
//   ch = AttachToBone(bodyMesh+492+244, mask);
//   if (ch) { ch+60 = local ? dword_62D090 : -1; }
//   else { Sprintf(err, "ch_AttachAni()...", path); return 0; }
// store: a1+133 = -1; a1+112 = ch; return ch;
void* AttachAni(CharActor3* a, const char* name, int seq) {
    const CharRender3Hooks& h = GetCharRender3Hooks();
    char dlg[512];
    std::snprintf(dlg, sizeof(dlg), "%s_%s", name, a->baseName);
    h.convertBackslashToSlash(dlg);

    char path[256];
    BuildAniPath(path, a->baseName, name);

    void* ch = nullptr;
    if (path[0] != '\0') {
        void* slot = h.findFreeMeshSlot();
        h.setGrayColorThunk(0, 4);
        int mask = kAttachAniBase | (((8 * (seq & 1)) | 0xD0) << 8);
        if (slot == nullptr) {
            int loop = IsLoopingGait(name) ? 1 : 0;
            h.loadStreamToStock(path, loop);
        }
        // bodyMesh+492 (anim object) + 244 (anim attach base).
        ch = h.attachToBone(a->bodyMesh, mask);
        if (ch == nullptr) {
            h.reportError(path);
            return nullptr;
        }
        // ch+60 owner: dword_62D090 when local, else -1 (modeled by isLocalUniverse).
    }
    a->attachFlags = 0xFF;   // a1+133 = -1
    a->attachAnim  = ch;     // a1+112 = ch
    return ch;
}

// gilde.exe 0x4032f8 — VIBE_Character_AttachMotion.  (motion clip name resolved by caller)
//   Sprintf(path, "character/%s/%s_%s.baf", base, motionName, base);
//   Sprintf(dlg, "%s_%s", motionName, base); ConvertBackslashToSlash(dlg);
//   if (!path[0]) goto owner;
//   slot = FindFreeMeshSlot(); SetGrayColorThunk(0,4); mask = 153600;
//   if (!slot) LoadStreamToStock(path, 1);
//   ch = AttachToBone(bodyMesh+492+244, mask);
//   if (!ch) { Sprintf(err,...); return 0; }
//   owner: ch+60 = local ? dword_62D090 : -1; return ch;
void* AttachMotion(CharActor3* a, const char* motionName) {
    const CharRender3Hooks& h = GetCharRender3Hooks();
    char dlg[512];
    std::snprintf(dlg, sizeof(dlg), "%s_%s", motionName, a->baseName);
    h.convertBackslashToSlash(dlg);

    char path[256];
    BuildAniPath(path, a->baseName, motionName);

    void* ch = nullptr;
    if (path[0] != '\0') {
        void* slot = h.findFreeMeshSlot();
        h.setGrayColorThunk(0, 4);
        if (slot == nullptr)
            h.loadStreamToStock(path, 1);
        ch = h.attachToBone(a->bodyMesh, kAttachMotionMask);
        if (ch == nullptr) {
            h.reportError(path);
            return nullptr;
        }
    }
    // ch+60 owner: local ? dword_62D090 : -1.
    return ch;
}

// gilde.exe 0x4034c4 — VIBE_Character_AttachMovementAni.
//   if (a1+128) return 0;                                       // already bound
//   ConvertBackslashToSlash(name);
//   Sprintf(path, "character/%s/%s_%s.baf", base, name, base);
//   Sprintf(dlg, "%s_%s", name, base); ConvertBackslashToSlash(dlg);
//   slot = FindFreeMeshSlot();
//   loop = !StrCmpNoCase(name,gehen) || !StrCmpNoCase(name,karren);
//   a1+128 = slot ? slot : LoadStreamToStock(path, loop);
//   if (!a1+128) return 0;
//   a1+132 = dir; ++*(a1+128+332); return a1+128;
void* AttachMovementAni(CharActor3* a, const char* name, u8 dir) {
    if (a->moveAnim != nullptr)
        return nullptr;                       // a1+128 already set
    const CharRender3Hooks& h = GetCharRender3Hooks();

    h.convertBackslashToSlash(const_cast<char*>(name));
    char path[256];
    BuildAniPath(path, a->baseName, name);
    char dlg[512];
    std::snprintf(dlg, sizeof(dlg), "%s_%s", name, a->baseName);
    h.convertBackslashToSlash(dlg);

    void* slot = h.findFreeMeshSlot();
    int loop = IsLoopingGait(name) ? 1 : 0;
    void* handle = slot != nullptr ? slot : h.loadStreamToStock(path, loop);
    a->moveAnim = handle;
    if (handle == nullptr)
        return nullptr;
    a->attachFlags = dir;                     // a1+132 = dir
    // ++*(handle + 332): bump the clip's attach refcount (renderer-side, observed via mock).
    return handle;
}

// gilde.exe 0x4035d0 — VIBE_Character_DetachMorphAni.
//   v2 = result[13];                                            // body mesh
//   if (result[28]) {                                           // morph source bound
//     Sprintf(name, "morph_%i", result);
//     if (!FindFreeMeshSlot()) {
//       v4 = *(v2+460);
//       if (v4) morph = CreateMorphAnim(*(v4+16), ..., name, 4);
//       if (morph) { ch = AttachToBone(*(v2+492)+244, 0x20000|(24<<8));
//                    result[31] = ch;
//                    if (ch) ch+60 = local ? dword_62D08C : -1; }
//       v7 = result[28];
//       if (!*(*(v7+104)+361) && (*(v7+109)>>6)) ComputeBoneDelta(v2, v7, ...);
//       PruneExpiredAttachments(*(v2+492)+244);
//       result[28] = 0;
//     }
//   }
void DetachMorphAni(const MorphCtx& c) {
    if (!c.hasMorphSource)
        return;
    const CharRender3Hooks& h = GetCharRender3Hooks();
    if (h.findFreeMeshSlot() != nullptr)
        return;                                // a cached free slot blocks the teardown

    char name[96];
    std::snprintf(name, sizeof(name), "morph_%i", 0);   // "morph_%i" (record id, runtime)

    if (c.meshGeom != nullptr) {
        void* morph = h.createMorphAnim(c.meshGeom, name);
        if (morph != nullptr) {
            void* ch = h.attachToBone(c.bodyMesh, kAttachMorphMask);
            // result[31] = ch; ch+60 = local ? dword_62D08C : -1.
            (void)ch;
        }
    }
    if (c.computeDelta)
        h.computeBoneDelta(c.bodyMesh, nullptr);
    h.pruneExpiredAttachments(c.bodyMesh);     // *(v2+492)+244
    // result[28] = 0 (clears the morph source; caller-side state).
}

// ===========================================================================
// Preload family.
// ===========================================================================

// gilde.exe 0x403c34 — VIBE_Character_PreloadAniSet.
//   for (each name in [0,count)) {
//     if (!name[0]) continue;
//     loop = !StrCmpNoCase(name,gehen) || !StrCmpNoCase(name,karren);
//     Sprintf(path, "character/%s/%s_%s.baf", base, name, base);
//     Sprintf(dlg, "%s_%s", name, base); ConvertBackslashToSlash(dlg);
//     if (!FindFreeMeshSlot()) {
//       LoadStreamToStock(path, loop); SetGrayColorThunk(0,4);
//       AttachToBone(*(a1+52)+492+244, 153600);
//       PruneExpiredAttachments(*(a1+52)+492+244);
//     }
//   }
void PreloadAniSet(CharActor3* a, const char* const* names, int count) {
    const CharRender3Hooks& h = GetCharRender3Hooks();
    for (int i = 0; i < count; ++i) {
        const char* name = names[i];
        if (name == nullptr || name[0] == '\0')
            continue;
        int loop = IsLoopingGait(name) ? 1 : 0;
        char path[256];
        BuildAniPath(path, a->baseName, name);
        char dlg[512];
        std::snprintf(dlg, sizeof(dlg), "%s_%s", name, a->baseName);
        h.convertBackslashToSlash(dlg);
        if (h.findFreeMeshSlot() == nullptr) {
            h.loadStreamToStock(path, loop);
            h.setGrayColorThunk(0, 4);
            h.attachToBone(a->bodyMesh, kAttachMotionMask);          // mask 153600
            h.pruneExpiredAttachments(a->bodyMesh);
        }
    }
}

// gilde.exe 0x403f14 — VIBE_Character_PreloadAniSetByName. As PreloadAniSet but flag 0
// (never loop) and the global anim base (MEMORY[0x34]). We use the supplied base name
// and route the attach through the same anim-base hook (the mock supplies the target).
void PreloadAniSetByName(const char* base, const char* const* names, int count) {
    const CharRender3Hooks& h = GetCharRender3Hooks();
    for (int i = 0; i < count; ++i) {
        const char* name = names[i];
        if (name == nullptr || name[0] == '\0')
            continue;
        char path[256];
        BuildAniPath(path, base, name);
        char dlg[512];
        std::snprintf(dlg, sizeof(dlg), "%s_%s", name, base);
        h.convertBackslashToSlash(dlg);
        if (h.findFreeMeshSlot() == nullptr) {
            h.loadStreamToStock(path, 0);          // always flag 0
            h.setGrayColorThunk(0, 4);
            h.attachToBone(nullptr, kAttachMotionMask);   // MEMORY[0x34] global base
            h.pruneExpiredAttachments(nullptr);
        }
    }
}

// gilde.exe 0x403da0 — VIBE_Character_PreloadLowPolyAniSet.
//   for (each name) {
//     if (!name[0]) continue;
//     Sprintf(path, "lowpolycharacter/%s/%s_%s_LOW.baf", base, name, base);
//     Sprintf(dlg, "%s_%s_LOW.baf", name, base); ConvertBackslashToSlash(dlg);
//     slot = FindFreeMeshSlot(); SetGrayColorThunk(0,4); v14 = 133120;
//     if (!slot) slot = LoadStreamToStock(path, 1);
//     if (slot) { AttachToBone(*(a1+492)+492+244, 133120);
//                 PruneExpiredAttachments(*(a1+492)+492+244); }
//   }
void PreloadLowPolyAniSet(CharActor3* a, const char* const* names, int count) {
    const CharRender3Hooks& h = GetCharRender3Hooks();
    for (int i = 0; i < count; ++i) {
        const char* name = names[i];
        if (name == nullptr || name[0] == '\0')
            continue;
        char path[256];
        BuildLowPolyAniPath(path, a->baseName, name);
        char dlg[512];
        std::snprintf(dlg, sizeof(dlg), "%s_%s_LOW.baf", name, a->baseName);
        h.convertBackslashToSlash(dlg);
        void* slot = h.findFreeMeshSlot();
        h.setGrayColorThunk(0, 4);
        if (slot == nullptr)
            slot = h.loadStreamToStock(path, 1);
        if (slot != nullptr) {
            h.attachToBone(a->lowPolyObj, kLowPolyMask);             // mask 133120
            h.pruneExpiredAttachments(a->lowPolyObj);
        }
    }
}

// ===========================================================================
// AttachItemToBone.
// ===========================================================================

// gilde.exe 0x4068c0 — VIBE_Character_AttachItemToBone.
//   switch (slot) { 1: node=&a1[26]; 2: node=&a1[25]; 3: node=&a1[27]; }
//   if (name) {
//     prev = SwitchActiveSlot(IndexFromPointer(a1[34]));
//     if (*node) { DetachAndRelease(*node); *node = 0; }
//     n = AttachToUniverseNode(*(a1+52), mat, name, off); *node = n;
//     if (!n) { SwitchActiveSlot(prev); return; }
//     *(n+530) |= 0xC; *(n+529) &= ~2; *(n+531) &= ~4; *(n+535) = 5;
//     BuildObjectCache(*node);
//     switch (slot) { 1: bone="d3_LeftHand"; 2: bone="d3_RightHand"; 3: bone="d3_Head";
//                     default: SwitchActiveSlot(prev); return; }
//     AttachToBone(*node, bone);
//     SwitchActiveSlot(prev);
//   } else {
//     prev = SwitchActiveSlot(IndexFromPointer(a1[34]));
//     if (*node) { DetachAndRelease(*node); *node = 0; }
//     SwitchActiveSlot(prev);
//   }
void AttachItemToBone(CharActor3* a, ItemBone slot, const char* name) {
    void** node = nullptr;
    const char* boneName = nullptr;
    switch (slot) {
        case ItemBone::kLeftHand:  node = &a->itemLeft;     boneName = kD3LeftHand;  break;
        case ItemBone::kRightHand: node = &a->itemRight;    boneName = kD3RightHand; break;
        case ItemBone::kHead:      node = &a->itemHeadNode; boneName = kD3Head;      break;
        default:                   node = nullptr;          boneName = nullptr;      break;
    }
    const CharRender3Hooks& h = GetCharRender3Hooks();
    int prev = h.switchUniverse(0);            // SwitchActiveSlot(IndexFromPointer(a1[34]))

    if (name != nullptr) {
        if (node != nullptr && *node != nullptr) {
            h.detachAndRelease(*node);
            *node = nullptr;
        }
        void* n = h.attachToUniverseNode(a->bodyMesh, name);
        if (node != nullptr)
            *node = n;
        if (n == nullptr) {
            h.switchUniverse(prev);
            return;
        }
        // Renderer node flag fixups (+530 |= 0xC; +529 &= ~2; +531 &= ~4; +535 = 5).
        h.buildLightCache(n);
        if (boneName != nullptr)               // default slot: no bone -> early return
            h.objectAttachToBone(n, boneName);
        h.switchUniverse(prev);
    } else {
        if (node != nullptr && *node != nullptr) {
            h.detachAndRelease(*node);
            *node = nullptr;
        }
        h.switchUniverse(prev);
    }
}

// ===========================================================================
// ResolveHeadBone.
// ===========================================================================

// gilde.exe 0x57c5d4 — VIBE_Character_ResolveHeadBone. Resolves the head texture-bone
// index for the actor's attached head model. Two paths:
//   * attached (a1+388) is a real scene actor whose universe != the empty sentinel:
//     scan its head mesh's bone table (v3+480 bones, stride 64) for the first present
//     bone (+0 != 0) whose name matches "kopf"/"abt_kopf"; on a match write
//     a1[99] = matchIndex + 1468 and stop.
//   * attached is null: the actor is an office-staff dummy — resolve the staff model,
//     read 4 bone indices (stride 1 from +36, terminator -1), pick index
//     model[36 + id % count], and write a1[99] = that + 1468.
// The bone-table walk + name matching are renderer/data-table side; we model the
// resolved candidate set + the chosen index through the caller-supplied context so the
// 1468 bias, the modulo selection, and the match-stops-the-scan behaviour are faithful.
//
// Context: the caller resolves the bone-name list (already filtered to the present
// bones) and, for the office-staff path, the staff bone table + the record id. We expose
// ResolveHeadBone via two explicit helpers matching the two branches.
int ResolveHeadBoneFromScan(const char* const* boneNames, int boneCount) {
    // First present bone whose name is "kopf" / "abt_kopf" (case-insensitive). The
    // original counts mismatches against the abt_kopf table and, on the first match,
    // returns matchIndex + 1468 written to a1[99].
    for (int i = 0; i < boneCount; ++i) {
        const char* n = boneNames[i];
        if (n == nullptr || n[0] == '\0')
            continue;
        if (guild::util::StrCmpNoCase(n, "kopf") == 0 ||
            guild::util::StrCmpNoCase(n, "abt_kopf") == 0) {
            return i + 1468;
        }
    }
    return 0;
}
int ResolveHeadBoneFromStaff(const unsigned char staffBones[4], int recordId) {
    // model[36 + id % count] + 1468, where count is the number of valid (!= 0xFF) bone
    // entries (1..4). Mirrors: do { ++p; ++n; } while (n<4 && p[36]!=-1).
    int count = 0;
    while (count < 4 && staffBones[count] != 0xFF)
        ++count;
    if (count == 0)
        count = 1;                              // the loop runs at least once (model[36])
    int idx = recordId % count;
    return static_cast<int>(staffBones[idx]) + 1468;
}

// ===========================================================================
// Audio leaves.
// ===========================================================================

// gilde.exe 0x40905c — VIBE_Character_PlayFootstepSound.  (see header for the gating)
u8 PlayFootstepSound(const FootstepCtx& c, bool* played) {
    if (played)
        *played = false;
    // Outer gates: frame event present, sound enabled, actor in the active universe.
    if (!c.hasFrameEvent || !c.soundEnabled || !c.inActiveUniverse)
        return c.lastEvent;
    // Event must be a footstep code and differ from the last played one.
    const int e = c.eventCode;
    const bool isStepCode = (e == 4 || e == 12 || e == 18 || e == 24);
    if (static_cast<u8>(e) == c.lastEvent || !isStepCode)
        return c.lastEvent;

    // To actually emit a sample: terrain noise on, < 3 active sounds, actor local.
    if (!c.terrainNoiseOn || c.activeSoundCount >= 3 || !c.isLocalUniverse)
        return static_cast<u8>(e);             // LABEL_22: record event, no sound

    const CharRender3Hooks& h = GetCharRender3Hooks();
    // Sample base: when the actor's universe carries a terrain kind, use the suffixed
    // sample; otherwise plain "Normal_s".
    const char* sample = c.hasTerrainKind ? FootstepSampleForTerrain(c.terrainKind)
                                          : kSndNormal;
    float handle = h.soundPlaySample(c.actorMesh, sample);
    if (handle != 0.0f) {
        h.sound3dPlayOneShot(handle, c.soundObj, 40, 500.0f);
        if (played)
            *played = true;                    // caller bumps dword_62D078
    }
    return static_cast<u8>(e);                  // a1[256] = new event
}

// gilde.exe 0x4049ec — VIBE_Character_NoiseTimerUpdate.
NoiseTimerResult NoiseTimerUpdate(const NoiseTimerCtx& c) {
    NoiseTimerResult r{};
    r.stepped = false;
    r.reset   = false;
    r.newTimer = c.timer;
    for (int i = 0; i < 3; ++i)
        r.newPos[i] = c.noisePos[i];
    if (!c.active)
        return r;
    r.stepped = true;

    // dir = normalize(noisePos - boneWorld) * 0.4
    float dir[3] = {
        c.noisePos[0] - c.boneWorld[0],
        c.noisePos[1] - c.boneWorld[1],
        c.noisePos[2] - c.boneWorld[2],
    };
    guild::util::VectorNormalize(dir);
    dir[0] *= kNoiseStep;
    dir[1] *= kNoiseStep;
    dir[2] *= kNoiseStep;

    // pivotPos = dir + meshPivot; SetPivotVector(obj, pivotPos)
    float pivotPos[3] = {
        dir[0] + c.meshPivot[0],
        dir[1] + c.meshPivot[1],
        dir[2] + c.meshPivot[2],
    };
    const CharRender3Hooks& h = GetCharRender3Hooks();
    h.setPivotVector(c.obj, pivotPos);

    // worldPos = pivotPos + meshOrigin
    float worldPos[3] = {
        pivotPos[0] + c.meshOrigin[0],
        pivotPos[1] + c.meshOrigin[1],
        pivotPos[2] + c.meshOrigin[2],
    };

    // Decrement the timer (saturating at 0).
    if (c.timer != 0)
        r.newTimer = static_cast<u8>(c.timer - 1);

    // Reached the target?  (within 0.4 of a1+276 per component)
    float wp[3] = {worldPos[0], worldPos[1], worldPos[2]};
    float tgt[3] = {c.target[0], c.target[1], c.target[2]};
    if (guild::util::VectorWithinTolerance(wp, tgt, 0.40000001f)) {
        r.reset = true;
        r.newTimer = 0;                         // *(a1+272) = 0
        float zero[3] = {0.0f, 0.0f, 0.0f};     // flt_5CA2E0 == {0,0,0}
        h.setPivotVector(c.obj, zero);
        h.setObjectPosition(c.obj, worldPos);
    }
    for (int i = 0; i < 3; ++i)
        r.newPos[i] = worldPos[i];
    return r;
}

// ===========================================================================
// Camera ray helpers.
// ===========================================================================

// gilde.exe 0x426764 — VIBE_Character_ProjectRayDirection.
//   d = -dist;  base = {0,0,1} (dword_425DB0/B8);
//   local = base * d;  rotate local by camMat 3x3 (rows 0/4/8, 1/5/9, 2/6/10);
//   out = eye + rotated.
void ProjectRayDirection(const float cameraMat[16], const float eye[3], float dist,
                         float outOrigin[3]) {
    const float d = -dist;
    // base forward axis == {0, 0, 1} (dword_425DB0 -> 0,0 ; dword_425DB8 -> 1,...).
    const float lx = 0.0f * d;
    const float ly = 0.0f * d;
    const float lz = 1.0f * d;
    const float rx = lx * cameraMat[0] + ly * cameraMat[4] + lz * cameraMat[8];
    const float ry = lx * cameraMat[1] + ly * cameraMat[5] + lz * cameraMat[9];
    const float rz = lx * cameraMat[2] + ly * cameraMat[6] + lz * cameraMat[10];
    outOrigin[0] = eye[0] + rx;
    outOrigin[1] = eye[1] + ry;
    outOrigin[2] = eye[2] + rz;
}

// gilde.exe 0x426850 — VIBE_Character_ScreenToWorldRay.
//   v = { sx - cx, -(sy - cy), focal };  normalize(v);
//   dir = rotate v by camMat 3x3;
//   scaled = dir * (zFar - zNear) / focal.
void ScreenToWorldRay(const float cameraMat[16], float sx, float sy, float cx, float cy,
                      float focal, float zNear, float zFar,
                      float outDir[3], float outScaled[3]) {
    float v[3] = { sx - cx, -(sy - cy), focal };
    guild::util::VectorNormalize(v);
    const float dx = v[0] * cameraMat[0] + v[1] * cameraMat[4] + v[2] * cameraMat[8];
    const float dy = v[0] * cameraMat[1] + v[1] * cameraMat[5] + v[2] * cameraMat[9];
    const float dz = v[0] * cameraMat[2] + v[1] * cameraMat[6] + v[2] * cameraMat[10];
    outDir[0] = dx;
    outDir[1] = dy;
    outDir[2] = dz;
    const float k = (zFar - zNear) / focal;
    outScaled[0] = dx * k;
    outScaled[1] = dy * k;
    outScaled[2] = dz * k;
}

} // namespace guild::sim
