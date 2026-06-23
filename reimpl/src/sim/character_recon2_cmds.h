#pragma once
// character_recon2_cmds — faithful 1:1 reconstruction of the remaining
// VIBE_Character "record / control-flow" cluster of gilde.exe (32-bit x86,
// imagebase 0x400000).  This slice covers the two action-queue *builders* and
// the family of script-command (`Cmd*`) dispatchers that the registered-command
// table routes the take/drop/look/ani opcodes to.
//
// Translated functions (this TU):
//   0x43d260 VIBE_Character_CmdTakeObject
//   0x43d30c VIBE_Character_CmdTakeObjectLeft
//   0x43d3b8 VIBE_Character_CmdTakeObjectScript
//   0x43d548 VIBE_Character_CmdDropObject
//   0x43d600 VIBE_Character_CmdDropObjectLeft
//   0x43d0f8 VIBE_Character_CmdPlayAnimationScript
//   0x43d844 VIBE_Character_CmdSetCharacterCamera
//   0x43dbc8 VIBE_Character_CmdLookAtCharacter
//   0x43dcb0 VIBE_Character_CmdLookAtObject
//   0x43dd38 VIBE_Character_CmdSetCharacterToDummy
//   0x43debc VIBE_Character_CmdAttachObjectToBone
//   0x43df30 VIBE_Character_CmdPlayCharacterAni
//   0x43d9a4 VIBE_Character_PreloadAnimation
//
// ALREADY PRESENT (not re-defined here).  The two action-queue *builders* this
// cluster's Left dispatchers call — VIBE_Character_CreateTakeObjectActionAlt
// (0x405e68) and VIBE_Character_CreateDropObjectActionAlt (0x4060dc) — are
// already reconstructed in src/sim/character_render4 as the `hand==1` (alt)
// path of CreateTakeObjectAction / CreateDropObjectAction.  To avoid an ODR
// clash this TU routes the Left dispatchers through hooks (createTakeObjectAlt /
// createDropObjectAlt) instead of re-defining the builders.
//
// WHY THESE.  The wider VIBE_Character cluster (FadeOutSlots, AttachTransport,
// UpdateTransportAttach, ReleaseMorphAni, RefreshAllFlags, Ensure*Avatar,
// PreloadSceneAnimations, SyncTurnState, Spawn*Actor) is dominated by
// scene-graph / matrix-transform / mesh / global-avatar-table coupling; those
// are deferred (project rule 8 — never a cheap analogue).  The functions in this
// TU are genuine record / control-flow logic: validity branches, the YIELD/RESUME
// re-arm protocol, UTF-16 verbatim string copies, and byte-faithful field writes
// into an action-queue entry.  Their only out-of-TU dependencies (the action-queue
// allocator, the script-VM error reporter, the math/transform helpers, object
// lookup, etc.) are routed through an installable hook table with inert defaults.
//
// SHARED ENGINE STATE.  Each Cmd body reaches the same three process globals the
// established script-VM slice (src/sim/script_import2) uses:
//   dword_62E8A8  current script context base (currentCtx); +152 cursor, +2528
//                 command-blocked re-arm slot, +2564 statement-mode byte.
//   dword_62E8CC  currently-executing command record (execCmd); +44 is the
//                 command fn pointer used for the YIELD/RESUME identity check.
//   dword_62E8D4  owning scene/caller id (ownerId).
// To keep this TU self-contained (HARD CONSTRAINT: new files only, no edits to
// the existing script_import2 state) these live in a *local* state struct with
// unique symbol names.  Tests poke it directly.
#include "guild/common/types.h"
#include <cstdint>

namespace guild::sim::character_recon2 {

// Operand values the original image treats as record POINTERS (the character /
// dummy / object the body dereferences).  In the 32-bit image these are dword
// pointers; modeled at pointer width here so field accesses stay portable.
using Handle = std::intptr_t;

// Action-queue entry record (gilde.exe — the base returned by the allocator).
// Only the fields the builders touch are modeled.  Stride/contents otherwise
// opaque.  Field offsets are the literal byte offsets from the decompile:
//   +0x00  update fn / vtable slot (identity token)
//   +0x09  type byte (0x31 take, 0x32 drop)
//   +0x0C  cleared dword
//   +0x10  cleared byte
//   +0x14  character pointer
//   +0x28  cleared dword
//   +0x30  ecx operand (take: caller ecx; drop: a2)
//   +0x34  cleared dword
//   +0x38  set to 1
//   +0x90  (144)  script extra name (CmdPlayAnimationScript / TakeObjectScript)
//   +0xF0  (240)  primary name (dummy / animation script)
//   +0x130 (304)  object dummy name (from obj +0x1EC -> +0x104)
//   +0x17C (380)  ownerId (dword_62E8D4)
//   +0x184 (384)  extra dword operand
struct ActionEntry;  // opaque; field writes go through helpers in the .cpp

// ---------------------------------------------------------------------------
// Local script-engine state (one definition in character_recon2_cmds.cpp).
// Mirrors src/sim/script_import2 ScriptEngineState but under a unique name so
// this TU links without touching the existing definition.
// ---------------------------------------------------------------------------
struct Recon2EngineState {
    u8*   currentCtx = nullptr;  // dword_62E8A8
    void* execCmd    = nullptr;  // dword_62E8CC (command record base)
    i32   ownerId    = 0;        // dword_62E8D4
};
Recon2EngineState& Recon2Engine();

// Command-record +44 fn pointer reader (execCmd->fn).  Set/Get so tests can
// simulate the resume check without a real command-record layout.
void* Recon2ExecCmdFn();
void  SetRecon2ExecCmdFn(void* fn);

// Identity tokens for the command bodies (the +44 fn-ptr values the resume
// check compares against; the original compares the literal function address).
extern void* const kFnCmdTakeObject;
extern void* const kFnCmdTakeObjectLeft;
extern void* const kFnCmdTakeObjectScript;
extern void* const kFnCmdDropObject;
extern void* const kFnCmdDropObjectLeft;
extern void* const kFnCmdPlayAnimationScript;

// Opaque action-update callback identity tokens (stored at entry+0; never
// called through in this TU — they are pointer values the engine dispatches).
extern void* const kCbTakeObjectActionUpdate;   // 0x405c88
extern void* const kCbDropObjectActionUpdate;   // 0x405f28
extern void* const kCbPlayAnimationScript;       // 0x43cf84 (insertAction callback)
extern void* const kCbLoadRunAndStoreResult;     // 0x43d0bc (insertAction callback)

// ---------------------------------------------------------------------------
// Cross-module leaves (inert defaults in the .cpp; tests / callers install).
// ---------------------------------------------------------------------------
struct Recon2Hooks {
    // VIBE_Script_ReportError @0x440f94 (ctx, cursor, msg).
    void (*reportError)(u8* ctx, u32 cursor, const char* msg) = nullptr;

    // VIBE_CharAction_QueueInsertEntry @0x40c15c -> entry base (null = fail).
    ActionEntry* (*queueInsertEntry)(Handle character) = nullptr;
    // VIBE_ActionQueue_UnlinkEntry @0x404370 (entry) — drop a half-built entry.
    void (*unlinkEntry)(ActionEntry* entry) = nullptr;

    // VIBE_CharAction_InsertActionArgs @0x40c2f0 -> entry base (null = fail).
    // arg packs (flagByte | (opcode<<32)); extra is the ebx operand.
    ActionEntry* (*insertActionArgs)(Handle character, void* callback,
                                     i64 arg, i32 extra) = nullptr;
    // VIBE_CharAction_InsertActionVararg @0x40c1e4 (packed64, angleArg, 0).
    void (*insertActionVararg)(i64 packed, i32 angleArg, i32 zero) = nullptr;

    // VIBE_Character_CreateTakeObjectAction @0x405da8 (char, name, obj) — normal.
    void (*createTakeObjectAction)(Handle character, const char* name, Handle obj) = nullptr;
    // VIBE_Character_CreateDropObjectAction @0x40602c (char, ecxArg, name) — normal.
    void (*createDropObjectAction)(Handle character, Handle ecxArg, const char* name) = nullptr;
    // VIBE_Character_CreateTakeObjectActionAlt @0x405e68 — the hand==1 (alt) take
    // builder (already reconstructed in character_render4 CreateTakeObjectAction).
    // Dispatcher forwards (char, name, obj); the builder resolves obj's model name.
    void (*createTakeObjectAlt)(Handle character, const char* name, Handle obj) = nullptr;
    // VIBE_Character_CreateDropObjectActionAlt @0x4060dc — the hand==1 (alt) drop
    // builder (already reconstructed in character_render4 CreateDropObjectAction).
    // Dispatcher forwards (char, param, name) exactly as CmdDropObjectLeft does:
    // CreateDropObjectActionAlt(*a1, *a3, *a2)  ->  param = *objPtr, name = *namePtr.
    void (*createDropObjectAlt)(Handle character, Handle param, const char* name) = nullptr;

    // VIBE_Object_FindByHandle @0x5b7be4 (a,b,c, handle, ctxOut) -> nonzero ok.
    int (*objectFindByHandle)(i32 a, i32 b, i32 c, Handle handle, Handle ctxOut) = nullptr;

    // VIBE_Character_ApplyAttachOffset @0x404964 (char, mode, charAgain).
    void (*applyAttachOffset)(Handle character, i32 mode, Handle charAgain) = nullptr;
    // VIBE_Character_AttachItemToBone @0x4068c0 (char, bone, item).
    void (*attachItemToBone)(Handle character, i32 bone, Handle item) = nullptr;
    // VIBE_Character_PreloadAniSet @0x403c34 (char, kind=4, a,b,c,d).
    void (*preloadAniSet)(Handle character, i32 kind, i32 a, i32 b, i32 c, i32 d) = nullptr;

    // VIBE_Character_ApplyVisibilityState @0x4019cc (char, 1, vec3).
    void (*applyVisibilityState)(Handle character, i32 flag, const float* vec3) = nullptr;
    // VIBE_Object_SetWorldTranslation @0x5af50c (objRecord, dataPtr).
    void (*objectSetWorldTranslation)(Handle objRecord, const void* dataPtr) = nullptr;
    // VIBE_Object_ChangeTransparency @0x5b2710 (a, b, packed, ctx).
    void (*objectChangeTransparency)(Handle a, Handle b, i32 packed, Handle ctx) = nullptr;

    // Math / transform helpers (the body's pure-math leaves):
    // VIBE_Math_AngleToTargetSigned @0x5b6d1c (fromRecord, targetVec3) -> angle.
    float (*angleToTargetSigned)(Handle fromRecord, const float* targetVec3) = nullptr;
    // VIBE_Math_SnapVectorToAxis @0x5ca940 (vecInOut, refVec3).
    void (*snapVectorToAxis)(float* vecInOut, const void* refVec3) = nullptr;
    // VIBE_Math_VectorAngleBetween @0x5ca334 (a, b) -> angle.
    float (*vectorAngleBetween)(const float* a, const float* b) = nullptr;
    // VIBE_Transform_PointThroughBoneChain @0x5c8b38 (objRecord, vec3, out3).
    void (*pointThroughBoneChain)(const void* objRecord, const float* vec3, float* out3) = nullptr;
    // VIBE_Transform_RotateVectorByHierarchy @0x5c8990 (obj, in3, out3).
    void (*rotateVectorByHierarchy)(Handle obj, const float* in3, float* out3) = nullptr;
    // VIBE_Coord_ConvertX @0x5c6b08 () — fixup hook (no observable args here).
    void (*coordConvertX)() = nullptr;
    // VIBE_Light_SetGrayColorThunk @0x5c6af0 (a, b, outPacked).
    void (*lightSetGrayColor)(i32 a, i32 b, i32* outPacked) = nullptr;

    // Field readers the bodies need on opaque records (kept as hooks so this TU
    // is not coupled to the scene/object record abstraction):
    //   recordField(rec, off): read dword at rec+off (object/char inner fields).
    intptr_t (*recordField)(Handle rec, i32 off) = nullptr;
    //   sceneRecordOf(char): *(char + 52) — the inner scene record.
    Handle (*sceneRecordOf)(Handle character) = nullptr;
    //   transportOf(char): *(char + 292) — attached transport (may be 0).
    Handle (*transportOf)(Handle character) = nullptr;
    //   readByte(p): *(u8*)p (for the *a2 ani flag byte).
    u8 (*readByte)(Handle p) = nullptr;
    //   strCmp(a,b): VIBE_Util_StrCmp @0x5d3f10 (0 == equal).
    int (*strCmp)(const char* a, const char* b) = nullptr;
    //   strLen(s): VIBE_Crt strlen (used by CmdPlayAnimationScript guard).
    std::size_t (*strLen)(const char* s) = nullptr;
    //   strNCopyPad(dst, src, max): VIBE_Util_StrNCopyPad @0x5d9360.
    void (*strNCopyPad)(char* dst, const char* src, std::size_t max) = nullptr;
};
void SetRecon2Hooks(const Recon2Hooks* hooks);
const Recon2Hooks& GetRecon2Hooks();

// Float constants pulled from the image (data globals) so the angle scaling is
// byte-faithful.  Tests set these to golden values; defaults are the recovered
// image values (read with get_bytes below).
struct Recon2Floats {
    float lookAtCharScaleA = 0.0f;  // flt_616EA4
    float lookAtCharScaleB = 0.0f;  // flt_616EA8
    float lookAtObjScaleA  = 0.0f;  // flt_616EDC
    float lookAtObjScaleB  = 0.0f;  // flt_616EE0
};
Recon2Floats& Recon2FloatTable();

// ===========================================================================
// Script-command dispatchers.  Each takes its operand pointers as the original
// __usercall register prototype did; the implicit "current context" is
// Recon2Engine().  Return 0 on success / yield, 1 on a reported error.
// ===========================================================================

// 0x43d260 — VIBE_Character_CmdTakeObject(char@<eax>, name@<edx>, obj@<ecx>).
i32 CmdTakeObject(Handle* charPtr, char** namePtr, Handle* objPtr);
// 0x43d30c — VIBE_Character_CmdTakeObjectLeft(char@<eax>, name@<edx>, obj@<ecx>).
i32 CmdTakeObjectLeft(Handle* charPtr, char** namePtr, Handle* objPtr);
// 0x43d3b8 — VIBE_Character_CmdTakeObjectScript(char,name,obj,extra,flags,scriptName).
i32 CmdTakeObjectScript(Handle* charPtr, char** namePtr, Handle* objPtr,
                        i32* extraPtr, u8* flagsPtr, char** scriptNamePtr);
// 0x43d548 — VIBE_Character_CmdDropObject(char@<eax>, name@<ecx>, obj@<ebx>).
i32 CmdDropObject(Handle* charPtr, char** namePtr, Handle* objPtr);
// 0x43d600 — VIBE_Character_CmdDropObjectLeft(char@<eax>, name@<ecx>, obj@<ebx>).
i32 CmdDropObjectLeft(Handle* charPtr, char** namePtr, Handle* objPtr);

// 0x43d0f8 — VIBE_Character_CmdPlayAnimationScript.
//   Registers: char@<eax>, flagsPtr@<ecx>, objPtr@<ebx> (extra), scriptNamePtr@<edx>,
//   extraNamePtr (a4, stack), finalExtraPtr (a5, stack).
//   InsertActionArgs(char, LoadRunAndStoreResult, flags|0x2E00000000, *objPtr).
//   *scriptNamePtr (strlen-guarded < 0x5F) -> rec+240 (pad 63);
//   *extraNamePtr -> rec+144 (pad 95); ownerId -> rec+380; *finalExtraPtr -> rec+384.
i32 CmdPlayAnimationScript(Handle* charPtr, u8* flagsPtr, i32* objPtr,
                           char** scriptNamePtr, char** extraNamePtr,
                           i32* finalExtraPtr);

// 0x43d844 — VIBE_Character_CmdSetCharacterCamera(char@<eax>, name@<edx>).
//   name@<edx> is char** (the engine operand slot); *namePtr is the mode string.
i32 CmdSetCharacterCamera(Handle* charPtr, char** namePtr);

// 0x43dbc8 — VIBE_Character_CmdLookAtCharacter(char@<eax>, target@<edx>).
i32 CmdLookAtCharacter(Handle* charPtr, Handle* targetPtr);
// 0x43dcb0 — VIBE_Character_CmdLookAtObject(char@<eax>, object@<edx>).
i32 CmdLookAtObject(Handle* charPtr, Handle* objectPtr);

// 0x43dd38 — VIBE_Character_CmdSetCharacterToDummy(char@<eax>, dummy@<edx>).
i32 CmdSetCharacterToDummy(Handle* charPtr, Handle* dummyPtr);

// 0x43debc — VIBE_Character_CmdAttachObjectToBone(char@<eax>, boneName@<edx>, item@<ebx>).
i32 CmdAttachObjectToBone(Handle* charPtr, char** boneNamePtr, Handle* itemPtr);

// 0x43df30 — VIBE_Character_CmdPlayCharacterAni(char@<eax>, aniFlagPtr@<edx>).
i32 CmdPlayCharacterAni(Handle* charPtr, u8* aniFlagPtr);

// 0x43d9a4 — VIBE_Character_PreloadAnimation(char@<eax>, a@<edx>, c@<ecx>, b@<ebx>, d).
i32 PreloadAnimation(Handle* charPtr, i32* a, i32* c, i32* b, i32* d);

} // namespace guild::sim::character_recon2
