// character_recon2_cmds — implementation.  Faithful 1:1 from gilde.exe pseudocode.
// See character_recon2_cmds.h for the provenance table and the rationale for the
// hook boundary.  Every branch, constant, field offset and string-copy loop here
// is a direct translation of the Hex-Rays decompile of the listed addresses.
#include "sim/character_recon2_cmds.h"

#include <cstddef>
#include <cstring>

namespace guild::sim::character_recon2 {

// ---------------------------------------------------------------------------
// Script-VM context offsets (mirror src/sim/script_vm.h, kept local).
// ---------------------------------------------------------------------------
namespace {
// Sentinel for the CmdAttachObjectToBone unknown-bone path, which in the
// original reads an uninitialized stack byte (see that function).
constexpr int kUninitBone = -1;

constexpr int kScCursor     = 152;   // ctx +152  (current cursor / line)
constexpr int kScCmdBlocked = 2528;  // ctx +2528 (resume re-arm slot)
constexpr int kScStmtMode   = 2564;  // ctx +2564 (statement-mode byte)

// Action-entry field offsets written by the InsertActionArgs-based dispatchers
// (CmdTakeObjectScript / CmdPlayAnimationScript).  (The builders that fill the
// full fixed-field block — +0/+9/+12/... — are reconstructed in
// character_render4; this TU only writes the name / owner / extra fields.)
constexpr int kAeName2    = 144;   // +0x90  (script extra name)
constexpr int kAeName0    = 240;   // +0xF0  (primary name)
constexpr int kAeName1    = 304;   // +0x130 (object dummy name)
constexpr int kAeOwnerId  = 380;   // +0x17C
constexpr int kAeExtra    = 384;   // +0x184

// --- ctx field helpers (byte-faithful pointer arithmetic) ----------------
inline u32  ctxCursor(u8* ctx)  { u32 v; std::memcpy(&v, ctx + kScCursor, 4); return v; }
inline u8   ctxStmtMode(u8* ctx){ return ctx[kScStmtMode]; }
inline void ctxSetBlocked(u8* ctx, void* fn) {
    std::memcpy(ctx + kScCmdBlocked, &fn, sizeof(void*));
}

// --- raw action-entry writers --------------------------------------------
inline void aeSetDword(ActionEntry* e, int off, i32 v) {
    std::memcpy(reinterpret_cast<u8*>(e) + off, &v, 4);
}
inline char* aeStr(ActionEntry* e, int off) {
    return reinterpret_cast<char*>(reinterpret_cast<u8*>(e) + off);
}

// Inline strcpy, UNROLLED two bytes per iteration — exactly the original loop:
//   a = src[0]; dst[0] = a; if (!a) break;
//   b = src[1]; src += 2; dst[1] = b; dst += 2; while (b);
// (i.e. `mov al,[esi]; mov [edi],al; cmp al,0; jz end; mov al,[esi+1];
//  add esi,2; mov [edi+1],al; add edi,2; cmp al,0; jnz`).  Copies a NARROW,
// NUL-terminated string verbatim (including the terminator).
void copyStr2(char* dst, const char* src) {
    for (;;) {
        char a = src[0];
        dst[0] = a;
        if (a == 0) break;
        char b = src[1];
        src += 2;
        dst[1] = b;
        dst += 2;
        if (b == 0) break;
    }
}

// ---------------------------------------------------------------------------
// Local engine state + hooks (single definitions).
// ---------------------------------------------------------------------------
Recon2EngineState g_engine;
void*             g_execCmdFn = nullptr;
Recon2Hooks       g_defaultHooks;       // all-null inert defaults
const Recon2Hooks* g_hooks = &g_defaultHooks;
Recon2Floats      g_floats{180.0f, 0.31830987f, 180.0f, 0.31830987f};

// Identity token storage (addresses don't matter; only their uniqueness).
char tokTakeObject, tokTakeObjectLeft, tokTakeObjectScript;
char tokDropObject, tokDropObjectLeft, tokPlayAniScript;
char tokTakeUpdate, tokDropUpdate, tokPlayAniScriptCb, tokLoadRunCb;
}  // namespace

Recon2EngineState& Recon2Engine() { return g_engine; }
void* Recon2ExecCmdFn() { return g_execCmdFn; }
void  SetRecon2ExecCmdFn(void* fn) { g_execCmdFn = fn; }

void SetRecon2Hooks(const Recon2Hooks* hooks) { g_hooks = hooks ? hooks : &g_defaultHooks; }
const Recon2Hooks& GetRecon2Hooks() { return *g_hooks; }
Recon2Floats& Recon2FloatTable() { return g_floats; }

void* const kFnCmdTakeObject          = &tokTakeObject;
void* const kFnCmdTakeObjectLeft      = &tokTakeObjectLeft;
void* const kFnCmdTakeObjectScript    = &tokTakeObjectScript;
void* const kFnCmdDropObject          = &tokDropObject;
void* const kFnCmdDropObjectLeft      = &tokDropObjectLeft;
void* const kFnCmdPlayAnimationScript = &tokPlayAniScript;
void* const kCbTakeObjectActionUpdate = &tokTakeUpdate;
void* const kCbDropObjectActionUpdate = &tokDropUpdate;
void* const kCbPlayAnimationScript    = &tokPlayAniScriptCb;
void* const kCbLoadRunAndStoreResult  = &tokLoadRunCb;

// NB: VIBE_Character_CreateTakeObjectActionAlt (0x405e68) and
// VIBE_Character_CreateDropObjectActionAlt (0x4060dc) are NOT defined here —
// they are already reconstructed in src/sim/character_render4 as the hand==1
// path of CreateTakeObjectAction / CreateDropObjectAction.  The Left dispatchers
// below reach them through the createTakeObjectAlt / createDropObjectAlt hooks.

// ===========================================================================
// Helpers shared by the dispatchers.
// ===========================================================================
namespace {
void reportError(const char* msg) {
    u8* ctx = g_engine.currentCtx;
    if (g_hooks->reportError)
        g_hooks->reportError(ctx, ctx ? ctxCursor(ctx) : 0, msg);
}

// Common tail of the take/drop/playscript dispatchers: if statement-mode at
// ctx+2564 is 1, re-arm ctx+2528 with the command's own fn token.  Returns 0.
i32 rearmIfStmtMode(void* selfFn) {
    u8* ctx = g_engine.currentCtx;
    if (ctx && ctxStmtMode(ctx) == 1)
        ctxSetBlocked(ctx, selfFn);
    return 0;
}

// The leading YIELD/RESUME guard shared by the take/drop/script bodies:
//   if execCmd && execCmd->fn == selfFn:
//       if *(char + 296): ctx+2528 = selfFn
//       return 0   (still blocked)  -> *handled = true*
// Returns true when the guard fired (caller must return 0).
bool resumeGuard(void* selfFn, Handle character) {
    if (g_engine.execCmd && g_execCmdFn == selfFn) {
        if (g_hooks->recordField && g_hooks->recordField(character, 296))
            ctxSetBlocked(g_engine.currentCtx, selfFn);
        return true;
    }
    return false;
}
}  // namespace

// ===========================================================================
// 0x43d260 — VIBE_Character_CmdTakeObject.
// ===========================================================================
i32 CmdTakeObject(Handle* charPtr, char** namePtr, Handle* objPtr) {
    const Recon2Hooks& h = *g_hooks;
    Handle character = *charPtr;
    if (resumeGuard(kFnCmdTakeObject, character))
        return 0;
    if (!character) {
        reportError("TakeObject(): Invalid character");
        return 1;
    }
    if (!*objPtr) {
        reportError("TakeObject(): Invalid dummy");
        return 1;
    }
    if (h.createTakeObjectAction)
        h.createTakeObjectAction(character, *namePtr, *objPtr);
    return rearmIfStmtMode(kFnCmdTakeObject);
}

// ===========================================================================
// 0x43d30c — VIBE_Character_CmdTakeObjectLeft.
// ===========================================================================
i32 CmdTakeObjectLeft(Handle* charPtr, char** namePtr, Handle* objPtr) {
    const Recon2Hooks& h = *g_hooks;
    Handle character = *charPtr;
    if (resumeGuard(kFnCmdTakeObjectLeft, character))
        return 0;
    if (!character) {
        reportError("TakeObjectLeft(): Invalid character");
        return 1;
    }
    if (!*objPtr) {
        reportError("TakeObjectLeft(): Invalid dummy");
        return 1;
    }
    // VIBE_Character_CreateTakeObjectActionAlt(char, name, obj) — the already-
    // reconstructed hand==1 take builder (character_render4), via hook.
    if (h.createTakeObjectAlt)
        h.createTakeObjectAlt(character, *namePtr, *objPtr);
    return rearmIfStmtMode(kFnCmdTakeObjectLeft);
}

// ===========================================================================
// 0x43d3b8 — VIBE_Character_CmdTakeObjectScript.
// ===========================================================================
i32 CmdTakeObjectScript(Handle* charPtr, char** namePtr, Handle* objPtr,
                        i32* extraPtr, u8* flagsPtr, char** scriptNamePtr) {
    const Recon2Hooks& h = *g_hooks;
    Handle character = *charPtr;
    if (resumeGuard(kFnCmdTakeObjectScript, character))
        return 0;
    if (!character) {
        reportError("TakeObjectScript(): Invalid character");
        return 1;
    }
    if (!*objPtr) {
        reportError("TakeObjectScript(): Invalid dummy");
        return 1;
    }
    // VIBE_CharAction_InsertActionArgs(char, PlayAnimationScriptCallback,
    //   flags | 0x3100000000, extra).
    i64 arg = static_cast<i64>(*flagsPtr) | 0x3100000000LL;
    ActionEntry* rec = h.insertActionArgs
        ? h.insertActionArgs(character, kCbPlayAnimationScript, arg, *extraPtr)
        : nullptr;
    if (rec) {
        copyStr2(aeStr(rec, kAeName0), *namePtr);          // +240 dummy name
        // if (*objPtr) { v17 = *(*objPtr+0x1EC); if (v17 && *(v17+0x104)) copy ->+304 }
        Handle obj = *objPtr;
        if (obj && h.recordField) {
            intptr_t inner = h.recordField(obj, 0x1EC);
            if (inner) {
                intptr_t str = h.recordField(static_cast<Handle>(inner), 0x104);
                if (str)
                    copyStr2(aeStr(rec, kAeName1),
                                     reinterpret_cast<const char*>(str));
            }
        }
        copyStr2(aeStr(rec, kAeName2), *scriptNamePtr);    // +144 script name
        aeSetDword(rec, kAeOwnerId, g_engine.ownerId);            // +380 = dword_62E8D4
    }
    return rearmIfStmtMode(kFnCmdTakeObjectScript);
}

// ===========================================================================
// 0x43d548 — VIBE_Character_CmdDropObject.
//   NOTE: validity + object-handle check come BEFORE the resume guard here.
// ===========================================================================
i32 CmdDropObject(Handle* charPtr, char** namePtr, Handle* objPtr) {
    const Recon2Hooks& h = *g_hooks;
    Handle character = *charPtr;
    if (!character) {
        reportError("DropObject(): Invalid character");
        return 1;
    }
    if (*namePtr &&
        !(h.objectFindByHandle &&
          h.objectFindByHandle(0, 64, 0,
                               reinterpret_cast<Handle>(*namePtr),
                               reinterpret_cast<Handle>(charPtr))))
        return 1;
    if (resumeGuard(kFnCmdDropObject, character))
        return 0;
    // VIBE_Character_CreateDropObjectAction(char, *objPtr, *namePtr).
    if (h.createDropObjectAction)
        h.createDropObjectAction(character, *objPtr,
                                 reinterpret_cast<const char*>(*namePtr));
    return rearmIfStmtMode(kFnCmdDropObject);
}

// ===========================================================================
// 0x43d600 — VIBE_Character_CmdDropObjectLeft.
// ===========================================================================
i32 CmdDropObjectLeft(Handle* charPtr, char** namePtr, Handle* objPtr) {
    const Recon2Hooks& h = *g_hooks;
    Handle character = *charPtr;
    if (!character) {
        reportError("DropObjectLeft(): Invalid character");
        return 1;
    }
    if (*namePtr &&
        !(h.objectFindByHandle &&
          h.objectFindByHandle(0, 64, 0,
                               reinterpret_cast<Handle>(*namePtr),
                               reinterpret_cast<Handle>(charPtr))))
        return 1;
    if (resumeGuard(kFnCmdDropObjectLeft, character))
        return 0;
    // VIBE_Character_CreateDropObjectActionAlt(*a1, *a3, *a2) — the already-
    // reconstructed hand==1 drop builder (character_render4), via hook.
    if (h.createDropObjectAlt)
        h.createDropObjectAlt(character, *objPtr,
                              reinterpret_cast<const char*>(*namePtr));
    return rearmIfStmtMode(kFnCmdDropObjectLeft);
}

// ===========================================================================
// 0x43d0f8 — VIBE_Character_CmdPlayAnimationScript.
// ===========================================================================
i32 CmdPlayAnimationScript(Handle* charPtr, u8* flagsPtr, i32* objPtr,
                           char** scriptNamePtr, char** extraNamePtr,
                           i32* finalExtraPtr) {
    const Recon2Hooks& h = *g_hooks;
    Handle character = *charPtr;
    if (resumeGuard(kFnCmdPlayAnimationScript, character))
        return 0;
    if (!character) {
        reportError("PlayCharacterAniScriptInt(): invalid character");
        return 1;
    }
    // VIBE_CharAction_InsertActionArgs(char, LoadRunAndStoreResult,
    //   flags | 0x2E00000000, *objPtr).
    i64 arg = static_cast<i64>(*flagsPtr) | 0x2E00000000LL;
    ActionEntry* rec = h.insertActionArgs
        ? h.insertActionArgs(character, kCbLoadRunAndStoreResult, arg, *objPtr)
        : nullptr;
    if (rec) {
        const char* script = *scriptNamePtr;     // *edx
        std::size_t len = h.strLen ? h.strLen(script)
                                   : (script ? std::strlen(script) : 0);
        if (len >= 0x5F) {
            // VIBE_ErrorLog_ReportMessage("PlayCharacterAniScript(): script name too long")
            reportError("PlayCharacterAniScript(): script name too long");
            return 1;
        }
        if (h.strNCopyPad) {
            h.strNCopyPad(aeStr(rec, kAeName0), script, 63);          // +240 = *edx
            h.strNCopyPad(aeStr(rec, kAeName2), *extraNamePtr, 95);   // +144 = *a4
        }
        aeSetDword(rec, kAeOwnerId, g_engine.ownerId);                // +380
        aeSetDword(rec, kAeExtra, *finalExtraPtr);                    // +384 = *a5
    }
    return rearmIfStmtMode(kFnCmdPlayAnimationScript);
}

// ===========================================================================
// 0x43d844 — VIBE_Character_CmdSetCharacterCamera.
// ===========================================================================
i32 CmdSetCharacterCamera(Handle* charPtr, char** namePtr) {
    const Recon2Hooks& h = *g_hooks;
    Handle character = *charPtr;
    if (!character) {
        reportError("SetCharacterCamera(): Invalid character");
        return 1;
    }
    const char* name = *namePtr;
    auto eq = [&](const char* s) { return h.strCmp && h.strCmp(name, s) == 0; };
    if (eq("CLOSEUP")) {
        if (h.applyAttachOffset) h.applyAttachOffset(character, 0, character);
        return 0;
    }
    if (eq("LEFT_SHOULDER")) {
        if (h.applyAttachOffset) h.applyAttachOffset(character, 1, character);
        return 0;
    }
    if (eq("RIGHT_SHOULDER")) {
        if (h.applyAttachOffset) h.applyAttachOffset(character, 2, character);
        return 0;
    }
    if (eq("EGO")) {
        if (h.applyAttachOffset) h.applyAttachOffset(character, 3, character);
        return 0;
    }
    return 0;
}

// ===========================================================================
// 0x43dbc8 — VIBE_Character_CmdLookAtCharacter.
// ===========================================================================
i32 CmdLookAtCharacter(Handle* charPtr, Handle* targetPtr) {
    const Recon2Hooks& h = *g_hooks;
    Handle character = *charPtr;
    Handle target = *targetPtr;
    if (!character || !target) {
        reportError("LookAtCharacter(): invalid dest- or targetcharacter");
        return 1;
    }
    Handle charScene   = h.sceneRecordOf ? h.sceneRecordOf(character) : 0;
    Handle targetScene = h.sceneRecordOf ? h.sceneRecordOf(target) : 0;
    // targetVec3 = targetScene + 76
    float targetVec[3] = {0, 0, 0};
    if (h.recordField) {
        for (int i = 0; i < 3; ++i) {
            i32 raw = static_cast<i32>(h.recordField(static_cast<Handle>(targetScene + 76 + 4 * i), 0));
            std::memcpy(&targetVec[i], &raw, 4);
        }
    }
    float angle = h.angleToTargetSigned ? h.angleToTargetSigned(charScene, targetVec) : 0.0f;
    float v[3] = {0.0f, angle, 0.0f};
    // VIBE_Math_SnapVectorToAxis(&v, charScene + 132)
    if (h.snapVectorToAxis)
        h.snapVectorToAxis(v, reinterpret_cast<const void*>(charScene + 132));
    // v += *(charScene + 132/136/140)
    if (h.recordField) {
        for (int i = 0; i < 3; ++i) {
            i32 raw = static_cast<i32>(h.recordField(static_cast<Handle>(charScene + 132 + 4 * i), 0));
            float f; std::memcpy(&f, &raw, 4);
            v[i] += f;
        }
    }
    // v5 = angle * 180.0 * (1/pi)
    float scaled = angle * g_floats.lookAtCharScaleA * g_floats.lookAtCharScaleB;
    if (h.coordConvertX) h.coordConvertX();
    // pack: HIDWORD = 7, LODWORD = *charPtr; insert.
    i64 packed = (static_cast<i64>(7) << 32) | (static_cast<u32>(character));
    if (h.insertActionVararg)
        h.insertActionVararg(packed, static_cast<i32>(scaled), 0);
    return 0;
}

// ===========================================================================
// 0x43dcb0 — VIBE_Character_CmdLookAtObject.
// ===========================================================================
i32 CmdLookAtObject(Handle* charPtr, Handle* objectPtr) {
    const Recon2Hooks& h = *g_hooks;
    Handle character = *charPtr;
    Handle object = *objectPtr;
    if (!character || !object) {
        reportError("LookAtObject(): invalid dest- or targetobject");
        return 1;
    }
    // VIBE_Transform_PointThroughBoneChain(object, object+76, &out)
    float objVec[3] = {0, 0, 0};
    float out[3] = {0, 0, 0};
    if (h.recordField) {
        for (int i = 0; i < 3; ++i) {
            i32 raw = static_cast<i32>(h.recordField(static_cast<Handle>(object + 76 + 4 * i), 0));
            std::memcpy(&objVec[i], &raw, 4);
        }
    }
    if (h.pointThroughBoneChain)
        h.pointThroughBoneChain(reinterpret_cast<const void*>(object), objVec, out);
    Handle charScene = h.sceneRecordOf ? h.sceneRecordOf(character) : 0;
    float angle = h.angleToTargetSigned ? h.angleToTargetSigned(charScene, out) : 0.0f;
    float scaled = angle * g_floats.lookAtObjScaleA * g_floats.lookAtObjScaleB;
    if (h.coordConvertX) h.coordConvertX();
    i64 packed = (static_cast<i64>(7) << 32) | (static_cast<u32>(character));
    if (h.insertActionVararg)
        h.insertActionVararg(packed, static_cast<i32>(scaled), 0);
    return 0;
}

// ===========================================================================
// 0x43dd38 — VIBE_Character_CmdSetCharacterToDummy.
// ===========================================================================
i32 CmdSetCharacterToDummy(Handle* charPtr, Handle* dummyPtr) {
    const Recon2Hooks& h = *g_hooks;
    Handle character = *charPtr;
    Handle dummy = *dummyPtr;
    if (!character || !dummy) {
        reportError("SetCharacterToDummy(): invalid character or dummy");
        return 1;
    }
    float pos[4] = {0, 0, 0, 0};
    float dummyVec[3] = {0, 0, 0};
    if (h.recordField) {
        for (int i = 0; i < 3; ++i) {
            i32 raw = static_cast<i32>(h.recordField(static_cast<Handle>(dummy + 76 + 4 * i), 0));
            std::memcpy(&dummyVec[i], &raw, 4);
        }
    }
    // VIBE_Transform_PointThroughBoneChain(dummy, dummy+76, pos)
    if (h.pointThroughBoneChain)
        h.pointThroughBoneChain(reinterpret_cast<const void*>(dummy), dummyVec, pos);
    // VIBE_Character_ApplyVisibilityState(char, 1, pos)
    if (h.applyVisibilityState)
        h.applyVisibilityState(character, 1, pos);
    // rot = {0, angle, 0}; angle = VectorAngleBetween(refAxis, out3)
    float rot[3] = {0, 0, 0};
    float out3[3] = {0, 0, 0};
    static const float kRefAxis[3] = {0.0f, 0.0f, 1.0f};  // &flt_5CA2B0 axis seed
    if (h.rotateVectorByHierarchy)
        h.rotateVectorByHierarchy(dummy, kRefAxis, out3);
    rot[1] = h.vectorAngleBetween ? h.vectorAngleBetween(kRefAxis, out3) : 0.0f;
    // VIBE_Object_SetWorldTranslation(*(char+52), &rot)
    Handle charScene = h.sceneRecordOf ? h.sceneRecordOf(character) : 0;
    if (h.objectSetWorldTranslation)
        h.objectSetWorldTranslation(charScene, rot);
    return 0;
}

// ===========================================================================
// 0x43debc — VIBE_Character_CmdAttachObjectToBone.
// ===========================================================================
i32 CmdAttachObjectToBone(Handle* charPtr, char** boneNamePtr, Handle* itemPtr) {
    const Recon2Hooks& h = *g_hooks;
    Handle character = *charPtr;
    if (!character)
        return 0;
    const char* bone = *boneNamePtr;
    Handle item = *itemPtr;
    auto eq = [&](const char* s) { return h.strCmp && h.strCmp(s, bone) == 0; };
    // Disasm (0x43debc): a single stack byte var_C is the bone id.
    //   "d3_LeftHand"  == bone -> var_C = 1
    //   "d3_RightHand" == bone -> var_C = 2
    //   "d3_Head"      == bone -> var_C = 3
    //   none           ->        var_C UNINITIALIZED (jnz to the shared tail);
    //                            the shared tail reads it via `[esp-3] sar 0x18`.
    // The shared tail then calls AttachItemToBone(*charPtr, var_C, *itemPtr).
    // The unknown-bone case genuinely reads an uninitialized stack slot in the
    // original (rule 1 edge case; not faked — modeled as the caller-visible
    // sentinel kUninitBone so behavior is reproducible/inspectable).
    int bone_id;
    bool defined = true;
    if (eq("d3_LeftHand")) {
        bone_id = 1;
    } else if (eq("d3_RightHand")) {
        bone_id = 2;
    } else if (eq("d3_Head")) {
        bone_id = 3;
    } else {
        bone_id = kUninitBone;  // original: uninitialized stack byte
        defined = false;
    }
    (void)defined;
    if (h.attachItemToBone) h.attachItemToBone(character, bone_id, item);
    return 1;
}

// ===========================================================================
// 0x43df30 — VIBE_Character_CmdPlayCharacterAni.
// ===========================================================================
i32 CmdPlayCharacterAni(Handle* charPtr, u8* aniFlagPtr) {
    const Recon2Hooks& h = *g_hooks;
    Handle character = *charPtr;
    if (!character) {
        reportError("PlayCharacterAni(): invalid character or dummy");
        return 1;
    }
    // VIBE_Light_SetGrayColorThunk(0, 4, &packed); BYTE2(packed) &= ~1; LOBYTE=flag
    i32 packed = 0;
    if (h.lightSetGrayColor) h.lightSetGrayColor(0, 4, &packed);
    packed &= ~0x00010000;                          // BYTE2 &= ~1
    packed = (packed & ~0xFF) | (*aniFlagPtr & 0xFF);  // LOBYTE = *a2
    Handle charScene = h.sceneRecordOf ? h.sceneRecordOf(character) : 0;
    Handle texSet = h.recordField ? h.recordField(charScene, 460) : 0;
    if (h.objectChangeTransparency)
        h.objectChangeTransparency(charScene, texSet, packed,
                                   reinterpret_cast<Handle>(aniFlagPtr));
    // transport = *(char+292); if (transport) ChangeTransparency(*transport, ...)
    Handle transport = h.transportOf ? h.transportOf(character) : 0;
    if (transport) {
        Handle inner = h.recordField ? h.recordField(transport, 0) : 0;
        Handle ts = h.recordField ? h.recordField(inner, 460) : 0;
        if (h.objectChangeTransparency)
            h.objectChangeTransparency(inner, ts, packed,
                                       reinterpret_cast<Handle>(aniFlagPtr));
    }
    return 0;
}

// ===========================================================================
// 0x43d9a4 — VIBE_Character_PreloadAnimation.
// ===========================================================================
i32 PreloadAnimation(Handle* charPtr, i32* a, i32* c, i32* b, i32* d) {
    const Recon2Hooks& h = *g_hooks;
    Handle character = *charPtr;
    if (!character) {
        reportError("PreloadAnimation(): Invalid character");
        return 1;
    }
    // VIBE_Character_PreloadAniSet(char, 4, *a, *b, *c, *d).
    if (h.preloadAniSet)
        h.preloadAniSet(character, 4, *a, *b, *c, *d);
    return 0;
}

} // namespace guild::sim::character_recon2
