// Script-VM long-tail, slice 3: command-table registration + the last Cmd body.
// Faithful 1:1 from gilde.exe pseudocode. See script_import3.h for the map and
// the shared-table / hooks design notes.
#include "sim/script_import3.h"
#include "sim/script_import.h"   // FindCommandByName (reused, 0x445b70)
#include <cstdlib>
#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Library-owned table storage (one definition each).
//   Commands()    -> dword_62E8AC base   (52*256 bytes)
//   EventTokens() -> dword_767944/8/C    (growable 48-byte records)
// ---------------------------------------------------------------------------
CommandTable& Commands() { static CommandTable t; return t; }
ScriptCmdFn CommandFn(const u8* record) {
    const int slot = static_cast<int>((record - Commands().bytes) / kCommandStride);
    return (slot >= 0 && slot < kCommandCapacity) ? Commands().fnSlots[slot] : nullptr;
}
void ResetCommands() {
    auto& t = Commands();
    std::memset(t.bytes, 0, sizeof(t.bytes));
    for (auto& f : t.fnSlots) f = nullptr;
}

EventTokenTable& EventTokens() { static EventTokenTable t; return t; }

// ---------------------------------------------------------------------------
// Cross-module leaf hooks (inert defaults in this library .cpp; tests install).
// ---------------------------------------------------------------------------
namespace {
const ScriptImport3Hooks* g_hooks = nullptr;
ScriptImport3Hooks        g_inert{};   // see accessors below for inert behaviour
} // namespace
void SetScriptImport3Hooks(const ScriptImport3Hooks* hooks) { g_hooks = hooks; }
const ScriptImport3Hooks& GetScriptImport3Hooks() { return g_hooks ? *g_hooks : g_inert; }

namespace {
// Inert-default-safe wrappers. A no-op host: transforms write zeros, the model
// loader fails (returns 0), the allocator falls back to malloc/free so the
// event-token table can still grow deterministically without a host.
void HostPointThroughBoneChain(const float* a, const float* b, float* out) {
    const auto& h = GetScriptImport3Hooks();
    if (h.pointThroughBoneChain) h.pointThroughBoneChain(a, b, out);
    else { out[0] = out[1] = out[2] = 0.0f; }
}
ScriptHandle HostCreateFromModel(const char* model, const char** outSlot) {
    const auto& h = GetScriptImport3Hooks();
    return h.createFromModel ? h.createFromModel(model, outSlot) : 0;
}
void HostRotateByHierarchy(ScriptHandle obj, const float* axis, float* out) {
    const auto& h = GetScriptImport3Hooks();
    if (h.rotateByHierarchy) h.rotateByHierarchy(obj, axis, out);
    else { out[0] = out[1] = out[2] = 0.0f; }
}
float HostAngleBetween(const float* a, const float* b) {
    const auto& h = GetScriptImport3Hooks();
    return h.angleBetween ? h.angleBetween(a, b) : 0.0f;
}
void HostSetWorldTranslation(ScriptHandle subObj, const float* vec) {
    const auto& h = GetScriptImport3Hooks();
    if (h.setWorldTranslation) h.setWorldTranslation(subObj, vec);
}
void HostSetPosition(ScriptHandle subObj, const float* vec) {
    const auto& h = GetScriptImport3Hooks();
    if (h.setPosition) h.setPosition(subObj, vec);
}
void* HostAllocDebug(std::size_t bytes, const char* tag) {
    const auto& h = GetScriptImport3Hooks();
    return h.allocDebug ? h.allocDebug(bytes, tag) : std::malloc(bytes);
}
void HostFreeDebug(void* p) {
    const auto& h = GetScriptImport3Hooks();
    if (h.freeDebug) h.freeDebug(p);
    else std::free(p);
}

// Reuse the slice-2 ReportError leaf (VIBE_Script_ReportError @0x440f94) exactly
// as CmdCreateCharacterAtDummy's siblings do, via the shared ScriptCmdHooks.
void HostReportError(u8* ctx, u32 cursor, const char* msg) {
    const auto& h = GetScriptCmdHooks();
    if (h.reportError) h.reportError(ctx, cursor, msg);
}

// gilde.exe context-cursor field (+152) used as the ReportError arg, matching
// the slice-2 bodies. dword_62E8A8 == ScriptEngine().currentCtx.
constexpr int kScCursorField = 152;
inline u32 CtxCursor(u8* ctx) {
    return ctx ? *reinterpret_cast<u32*>(ctx + kScCursorField) : 0;
}

// The name-copy loop used by ImportCommand, AddEventToken and
// CmdCreateCharacterAtDummy (identical inlined loop in each, and in slice 2):
// a 2-bytes-per-iteration UNROLLED strcpy.  Source/dest both advance 2 bytes
// per pair, so for a contiguous NUL-terminated byte string the result is the
// same contiguous byte string (NOT a wide/UTF-16 expansion).
void CopyNameZ(char* dst, const char* src) {
    for (;;) {
        char lo = src[0];
        dst[0] = lo;
        if (!lo) break;
        char hi = src[1];
        dst[1] = hi;
        dst += 2; src += 2;
        if (!hi) break;
    }
}

// ---------------------------------------------------------------------------
// Leaf command-function identity tokens.  The Register* tables only STORE these
// pointers (at command-record +44) and never call through them here, so — as in
// script_import2's kFnCmd* tokens — each gets a unique stable sentinel address.
// One byte per token; the address is the identity, the value is unused.
// ---------------------------------------------------------------------------
char g_fnTokens[256];
inline ScriptCmdFn Tok(int i) { return &g_fnTokens[i]; }
} // namespace

// Error/event strings recovered from the image (data refs).
static const char kErrCharAtDummyInvalid[] = "CreateCharacterAtDummy(): invalid dummy";
static const char kErrCharAtDummyLoad[]    = "CreateCharacterAtDummy(): Could not load character";

// flt_5CA2B0 — the reference axis vector passed to RotateVectorByHierarchy /
// VectorAngleBetween.  get_bytes @0x5ca2b0 = 00 00 00 00 | 00 00 00 00 | 00 00
// 80 3F  ==  {0.0f, 0.0f, 1.0f} (a unit +Z axis) — VERIFIED exact constant.
static const float kRefAxis[3] = {0.0f, 0.0f, 1.0f};

// ===========================================================================
// 0x445bc8 — VIBE_Script_ImportCommand
// ===========================================================================
i32 ImportCommand(const char* name, ScriptCmdFn fn, u8 kind, int argc,
                  const u8* argTypes) {
    // strlen(name) > 0x1F -> name too long.
    if (std::strlen(name) > 0x1F) {                          /*0x445be5*/
        // evt_ImportCommand: Commandnamelength is invalid... (logged, ignored)
        return 0;                                            /*0x445cc1*/
    }
    // Name already exists -> reject.
    if (FindCommandByName(Commands().bytes, name)) {         /*0x445bef*/
        // evt_ImportCommand: Commandname already exists... (logged, ignored)
        return 0;                                            /*0x445cd8*/
    }
    // Scan for the first free slot (record +44 fn-ptr == 0).
    u8* base = Commands().bytes;                             // v6 = dword_62E8AC
    int slot = 0;                                            // v5
    int off  = 0;                                            // CommandByName (byte offset)
    while (off < kCommandStride * kCommandCapacity) {        // < 13312 /*0x445c14*/
        if (!*reinterpret_cast<i32*>(base + off + 44))       // +44 fn/occupancy (4 bytes) /*0x445c04*/
            break;
        off += kCommandStride;                               /*0x445c0b*/
        ++slot;                                              /*0x445c0e*/
    }
    if (slot >= kCommandCapacity) {                          // >= 256 /*0x445c22*/
        // evt_ImportCommand: Too many commands! (logged, ignored)
        return 0;                                            /*0x445cef*/
    }
    const int rec = kCommandStride * slot;                   // v7 = 52*v5
    // Original writes the 4-byte fn ptr at +44; on 64-bit it lives in fnSlots,
    // and +44 gets a nonzero occupancy marker so the free-slot scan still works.
    Commands().fnSlots[slot] = fn;                           /*0x445c41*/
    *reinterpret_cast<i32*>(base + rec + 44) = fn ? 1 : 0;
    *reinterpret_cast<i32*>(base + rec + 32) = argc;          /*0x445c49*/
    base[rec + 48] = kind;                                    /*0x445c51*/
    for (int i = 0; i < argc; ++i)                            /*0x445c61*/
        base[rec + 36 + i] = argTypes ? argTypes[i] : 0;      /*0x445c68*/
    CopyNameZ(reinterpret_cast<char*>(base + rec), name);     /*0x445c86*/
    return 1;                                                 /*0x445c9f*/
}

i32 ImportCommand(const char* name, ScriptCmdFn fn, u8 kind, int argc) {
    return ImportCommand(name, fn, kind, argc, nullptr);
}

// ===========================================================================
// 0x441140 — VIBE_Script_AddEventToken
// ===========================================================================
ScriptHandle AddEventToken(const char* name, i32 handler, u8 typeNibble) {
    auto& T = EventTokens();
    // Grow when the next record would exceed capacity.  gilde.exe 0x441148-0x44115b
    // is literally `count + 48 > capBytes` (disasm: `mov eax,count; add eax,30h;
    // cmp eax,capBytes; ja grow`) — the test uses the raw RECORD COUNT plus the
    // 48-byte stride, NOT count*48.  Reallocate to (count+16)*48 bytes.
    if (T.count + kEventTokenStride > T.capBytes) {                       /*0x44115b*/
        const std::size_t newBytes =
            static_cast<std::size_t>(kEventTokenStride) * (T.count + kEventTokenGrowRecords);
        u8* nb = static_cast<u8*>(HostAllocDebug(newBytes, "evt:t"));     /*0x441213*/
        if (T.base) {                                                     /*0x441224*/
            std::memcpy(nb, T.base, static_cast<std::size_t>(kEventTokenStride) * T.count); /*0x441244*/
            HostFreeDebug(T.base);                                        /*0x441255*/
        }
        T.base = nb;                                                      /*0x44126c*/
        T.capBytes += kEventTokenGrowBytes;                               /*0x441272*/
    }
    // Write the name as UTF-16 at record+1.
    u8* rec = T.base + kEventTokenStride * T.count;                       /*0x44117d*/
    CopyNameZ(reinterpret_cast<char*>(rec + 1), name);                    /*0x44117f*/
    // Type nibble: preserve the high nibble already present, set the low nibble.
    const u8 hi = rec[0] & 0xF0;                                          /*0x4411b3*/
    rec[0] = hi;                                                          /*0x4411b9*/
    *reinterpret_cast<i32*>(rec + 40) = 0;                                /*0x4411be*/
    rec[0] = static_cast<u8>((typeNibble & 0x0F) | hi);                   /*0x4411c8*/
    *reinterpret_cast<i32*>(rec + 44) = handler;                          /*0x4411ce*/
    *reinterpret_cast<i32*>(rec + 36) = 1;                                /*0x4411d5*/
    const u32 idx = T.count;
    T.count = idx + 1;                                                    /*0x4411e9*/
    return reinterpret_cast<ScriptHandle>(T.base + kEventTokenStride * idx); /*0x4411f1*/
}

void ResetEventTokens() {
    auto& T = EventTokens();
    if (T.base) HostFreeDebug(T.base);
    T.base = nullptr;
    T.count = 0;
    T.capBytes = 0;
}

// ===========================================================================
// 0x43c850 — VIBE_Script_RegisterCommands
// ===========================================================================
// kind codes: 5 == "statement" (no return), 1 == "function" (returns a value).
// arg-type codes: 1 == int, 6 == string, 7 == float (per script_vm.h).
i32 RegisterCommands() {
    ImportCommand("ecmd_Dummy",             Tok(0),  5, 0);                              /*0x43c85e CmdReturnTrue*/
    { const u8 t[] = {6};       ImportCommand("Print",      Tok(1), 5, 1, t); }          /*0x43c876 CmdReturnFalse*/
    { const u8 t[] = {1};       ImportCommand("PrintInt",   Tok(1), 5, 1, t); }          /*0x43c88e*/
    { const u8 t[] = {7};       ImportCommand("PrintFloat", Tok(1), 5, 1, t); }          /*0x43c8a6*/
    { const u8 t[] = {1};       ImportCommand("rnd",        Tok(2), 1, 1, t); }          /*0x43c8be off_61688C=&"rnd" (value 0x646e72), RandModulo*/
    ImportCommand("GetTime",                Tok(3),  1, 0);                              /*0x43c8d4 GetScaledDelay*/
    { const u8 t[] = {6};       ImportCommand("RunScript",       Tok(4), 1, 1, t); }     /*0x43c8ec LoadAndRunMain*/
    { const u8 t[] = {6,1};     ImportCommand("RunScriptInt",    Tok(5), 1, 2, t); }     /*0x43c906 LoadAndRunWithArg*/
    { const u8 t[] = {6,6};     ImportCommand("RunScriptString", Tok(6), 1, 2, t); }     /*0x43c920 LoadAndRunWithArgAlt*/
    { const u8 t[] = {1};       ImportCommand("StopScript",      Tok(7), 5, 1, t); }     /*0x43c938 FinishThunk*/
    { const u8 t[] = {1};       ImportCommand("Sleep",           Tok(8), 5, 1, t); }     /*0x43c950 CmdSleep*/
    { const u8 t[] = {6};       ImportCommand("FindScript",      Tok(9), 1, 1, t); }     /*0x43c968 FindByNameThunk*/
    ImportCommand("KillLocalScripts",       Tok(10), 1, 0);                              /*0x43c97e CmdKillLocalScripts*/
    { const u8 t[] = {1,1};     ImportCommand("CallUserFunction",         Tok(11), 1, 2, t); } /*0x43c998*/
    const u8 te[] = {1,1,1};
    return ImportCommand("CallUserFunctionExtended", Tok(12), 1, 3, te);                 /*0x43c9bc*/
}

// ===========================================================================
// 0x440df0 — VIBE_Script_RegisterSoundCommands
// ===========================================================================
i32 RegisterSoundCommands() {
    { const u8 t[] = {6};         ImportCommand("LoadSampleBank",   Tok(20), 1, 1, t); } /*0x440e02*/
    { const u8 t[] = {1};         ImportCommand("KillSampleBank",   Tok(21), 1, 1, t); } /*0x440e1a*/
    { const u8 t[] = {1,6};       ImportCommand("PlaySampleHandle", Tok(22), 1, 2, t); } /*0x440e34*/
    { const u8 t[] = {6,1};       ImportCommand("PlaySample",       Tok(23), 1, 2, t); } /*0x440e4e*/
    { const u8 t[] = {1,1,6,1};   ImportCommand("PlaySample3D",     Tok(24), 1, 4, t); } /*0x440e6c*/
    { const u8 t[] = {1};         ImportCommand("StopSample",       Tok(25), 1, 1, t); } /*0x440e84*/
    { const u8 t[] = {6};         ImportCommand("GetSampleBankHandle", Tok(26), 1, 1, t); } /*0x440e9c*/
    { const u8 t[] = {1,6};       ImportCommand("GetSampleHandle",  Tok(27), 1, 2, t); } /*0x440eb6*/
    { const u8 t[] = {1,1,6,1,1}; ImportCommand("SpeechQueued",     Tok(28), 1, 5, t); } /*0x440edb*/
    AddEventToken("SND_W2",   0x62E784, 1);                                              /*0x440eed*/
    AddEventToken("SND_W1",   0x62E788, 1);                                              /*0x440f01*/
    AddEventToken("SND_M1",   0x62E794, 1);                                              /*0x440f15*/
    AddEventToken("SND_M2",   0x62E790, 1);                                              /*0x440f29*/
    AddEventToken("SND_M3",   0x62E78C, 1);                                              /*0x440f3d*/
    AddEventToken("SND_HS",   0x62E798, 1);                                              /*0x440f51*/
    AddEventToken("SND_NONE", 0x62E79C, 1);                                              /*0x440f65*/
    return static_cast<i32>(AddEventToken("SND_VAR", 0x62E7A0, 1) != 0);                 /*0x440f7f*/
}

// ===========================================================================
// 0x440618 — VIBE_Script_RegisterObjectCommands
// ===========================================================================
i32 RegisterObjectCommands() {
    { const u8 t[]={7,7,7,6};       ImportCommand("CreateObject",            Tok(40), 1, 4, t); } /*0x44062e*/
    { const u8 t[]={6,1};           ImportCommand("CreateObjectAtDummy",     Tok(41), 1, 2, t); } /*0x440648*/
    { const u8 t[]={6,1};           ImportCommand("CreateObjectGroupAtDummy",Tok(42), 1, 2, t); } /*0x440662*/
    { const u8 t[]={1};             ImportCommand("KillObject",              Tok(43), 5, 1, t); } /*0x44067a*/
    { const u8 t[]={1,7,7,7};       ImportCommand("SetPos",                  Tok(44), 5, 4, t); } /*0x440698*/
    { const u8 t[]={1,7,7,7};       ImportCommand("SetAngle",                Tok(45), 5, 4, t); } /*0x4406b6*/
    { const u8 t[]={1,1,1,1,1};     ImportCommand("MoveObject",              Tok(46), 5, 5, t); } /*0x4406d6*/
    { const u8 t[]={1,1,1,1,1};     ImportCommand("RotateObject",            Tok(47), 5, 5, t); } /*0x4406f6*/
    { const u8 t[]={1,1,1,1,1};     ImportCommand("MoveObjectRelative",      Tok(48), 5, 5, t); } /*0x440716*/
    { const u8 t[]={1,1,1,1,1};     ImportCommand("RotateObjectRelative",    Tok(49), 5, 5, t); } /*0x440736*/
    { const u8 t[]={1,1,1};         ImportCommand("SetAmbiente",             Tok(50), 5, 3, t); } /*0x440752*/
    { const u8 t[]={1};             ImportCommand("LightCalc",               Tok(51), 5, 1, t); } /*0x44076a*/
    { const u8 t[]={1,6};           ImportCommand("ReplaceObject",           Tok(52), 1, 2, t); } /*0x440784*/
    { const u8 t[]={6};             ImportCommand("LoadScene",               Tok(53), 5, 1, t); } /*0x44079c*/
    { const u8 t[]={1,6,1};         ImportCommand("AttachAnim",              Tok(54), 1, 3, t); } /*0x4407b8*/
    { const u8 t[]={1,6};           ImportCommand("PreloadAnim",             Tok(55), 1, 2, t); } /*0x4407d2*/
    { const u8 t[]={1,6,1};         ImportCommand("AttachAnimLooped",       Tok(56), 1, 3, t); } /*0x4407ee*/
    { const u8 t[]={6,6,1,1};       ImportCommand("AttachAnimLoopedToAll",  Tok(57), 1, 4, t); } /*0x44080c PlaySampleAt*/
    { const u8 t[]={6};             ImportCommand("DetachAnimFromAll",       Tok(58), 5, 1, t); } /*0x440824 PlaySampleLooped*/
    { const u8 t[]={6};             ImportCommand("GetObjectHandle",         Tok(59), 1, 1, t); } /*0x44083c*/
    { const u8 t[]={6};             ImportCommand("GetRndObjectHandle",      Tok(60), 1, 1, t); } /*0x440854*/
    { const u8 t[]={1,6};           ImportCommand("GetRndSubObjectHandle",   Tok(61), 1, 2, t); } /*0x44086e*/
    { const u8 t[]={1,6};           ImportCommand("GetSubObjectHandle",      Tok(62), 1, 2, t); } /*0x440888*/
    { const u8 t[]={1};             ImportCommand("SetNoTextures",           Tok(63), 5, 1, t); } /*0x4408a0*/
    { const u8 t[]={1};             ImportCommand("SetLightSet",             Tok(64), 5, 1, t); } /*0x4408b8*/
    { const u8 t[]={1,1,1};         ImportCommand("SetFogSet",               Tok(65), 5, 3, t); } /*0x4408d4*/
    { const u8 t[]={1,6,6};         ImportCommand("CameraFlight",            Tok(66), 5, 3, t); } /*0x4408f0*/
    { const u8 t[]={1,6};           ImportCommand("ZoomOnObject",            Tok(67), 5, 2, t); } /*0x44090a*/
    { const u8 t[]={1,6,6,6,6,6,6}; ImportCommand("CameraFlightEnhanced",    Tok(68), 5, 7, t); } /*0x44092e*/
    { const u8 t[]={1,6,6,6,6,6,6}; ImportCommand("NewCameraFlight",         Tok(69), 5, 7, t); } /*0x440952*/
    { const u8 t[]={1,1,6,6,6,6,6}; ImportCommand("ObjectFlight",            Tok(70), 5, 7, t); } /*0x440976*/
    { const u8 t[]={1};             ImportCommand("CreateRain",              Tok(71), 1, 1, t); } /*0x44098e*/
    { const u8 t[]={1};             ImportCommand("DeleteRain",              Tok(72), 1, 1, t); } /*0x4409a6*/
    { const u8 t[]={1};             ImportCommand("CreateParticle",          Tok(73), 5, 1, t); } /*0x4409be*/
    { const u8 t[]={1};             ImportCommand("KillParticle",            Tok(74), 5, 1, t); } /*0x4409d6*/
    { const u8 t[]={6};             ImportCommand("SetCameraToDummy",        Tok(75), 5, 1, t); } /*0x4409ee*/
    ImportCommand("StopCameraFlight",                                        Tok(76), 5, 0);     /*0x440a04*/
    { const u8 t[]={1,6};           ImportCommand("RenameObject",            Tok(77), 1, 2, t); } /*0x440a1e*/
    { const u8 t[]={1,1};           ImportCommand("SetBlocked",              Tok(78), 1, 2, t); } /*0x440a38*/
    { const u8 t[]={1,1};           ImportCommand("SetTransient",            Tok(79), 1, 2, t); } /*0x440a52*/
    { const u8 t[]={1,1,1,1,6,1,1}; ImportCommand("CreateEmitter",           Tok(80), 1, 7, t); } /*0x440a76*/
    { const u8 t[]={1};             ImportCommand("KillEmitter",             Tok(81), 1, 1, t); } /*0x440a8e*/
    { const u8 t[]={1,1,1,1,1,1};   ImportCommand("SetEmitterAmplitude",     Tok(82), 1, 6, t); } /*0x440ab0*/
    { const u8 t[]={1,1,1,1,1,1};   ImportCommand("SetEmitterPhasespeed",    Tok(83), 1, 6, t); } /*0x440ad2*/
    { const u8 t[]={1,1,1,1};       ImportCommand("SetEmitterDirection",     Tok(84), 1, 4, t); } /*0x440af0*/
    { const u8 t[]={1,1,1,1};       ImportCommand("SetEmitterSize",          Tok(85), 1, 4, t); } /*0x440b0e*/
    { const u8 t[]={1,1,1,1};       ImportCommand("SetEmitterVelocity",      Tok(86), 1, 4, t); } /*0x440b2c*/
    { const u8 t[]={1,1,1,1};       ImportCommand("SetEmitterAcceleration",  Tok(87), 1, 4, t); } /*0x440b4a*/
    { const u8 t[]={1,1,1,1};       ImportCommand("SetEmitterRndVelocity",   Tok(88), 1, 4, t); } /*0x440b68*/
    { const u8 t[]={1,1,1,1,1};     ImportCommand("SetEmitterPlane",         Tok(89), 1, 5, t); } /*0x440b88*/
    { const u8 t[]={1,1,1,1,1};     ImportCommand("SetEmitterTimeAndAlpha",  Tok(90), 1, 5, t); } /*0x440ba8*/
    { const u8 t[]={1,1,1,1,1};     ImportCommand("SetEmitterColor",         Tok(91), 1, 5, t); } /*0x440bc8*/
    { const u8 t[]={1,1,1,1,1,1,1}; ImportCommand("SetEmitterFlags",         Tok(92), 1, 7, t); } /*0x440bec argc 7, 7 type bytes*/
    { const u8 t[]={1,1};           ImportCommand("SetEmitterInitFill",      Tok(93), 1, 2, t); } /*0x440c06*/
    { const u8 t[]={1,1};           ImportCommand("SetEmitterRebirthFill",   Tok(94), 1, 2, t); } /*0x440c20*/
    { const u8 t[]={1,1};           ImportCommand("SetEmitterTextureMode",   Tok(95), 1, 2, t); } /*0x440c3a*/
    { const u8 t[]={1,1};           ImportCommand("SetEmitterIsTrigger",     Tok(96), 1, 2, t); } /*0x440c54*/
    { const u8 t[]={1,1};           ImportCommand("SetEmitterTriggerOnce",   Tok(97), 1, 2, t); } /*0x440c6e*/
    { const u8 t[]={1,1};           ImportCommand("SetEmitterMaxTrigger",    Tok(98), 1, 2, t); } /*0x440c88*/
    { const u8 t[]={1};             ImportCommand("TriggerEmitter",          Tok(99), 1, 1, t); } /*0x440ca0*/
    { const u8 t[]={1,1,1,1};       ImportCommand("SetParticlePos",          Tok(100),1, 4, t); } /*0x440cbe*/
    const u8 tlast[]={6,1};
    return ImportCommand("SelectAllTextureSets", Tok(101), 1, 2, tlast);                         /*0x440ce0*/
}

// ===========================================================================
// 0x43ca0c — VIBE_Script_CmdCreateCharacterAtDummy
// ===========================================================================
ScriptHandle CmdCreateCharacterAtDummy(ScriptHandle* dummyPtr, char** namePtr) {
    auto& E = ScriptEngine();
    if (!*dummyPtr) {                                                     /*0x43ca19*/
        HostReportError(E.currentCtx, CtxCursor(E.currentCtx), kErrCharAtDummyInvalid); /*0x43caf3*/
        return 0;                                                        /*0x43caf8*/
    }
    const ScriptHandle dummy = *dummyPtr;
    // worldPos = PointThroughBoneChain(dummy, dummy+76).
    float worldPos[4] = {0,0,0,0};                                       // v19
    HostPointThroughBoneChain(reinterpret_cast<const float*>(dummy),
                              reinterpret_cast<const float*>(dummy + 76),
                              worldPos);                                  /*0x43ca2a*/
    // Create the character. The original reads the model name slot via ecx (v5);
    // we pass the dummy's name pointer slot as the model source.
    const char* modelSlot = *namePtr;
    ScriptHandle character = HostCreateFromModel(modelSlot, const_cast<const char**>(namePtr)); /*0x43ca33*/
    if (!character) {                                                    /*0x43ca40*/
        HostReportError(E.currentCtx, CtxCursor(E.currentCtx), kErrCharAtDummyLoad); /*0x43cb10*/
        return character;                                                /*0x43cb15*/
    }
    // Copy the name into the character (+5) and its sub-object (+52) as UTF-16.
    CopyNameZ(reinterpret_cast<char*>(character + 5), *namePtr);         /*0x43ca52*/
    ScriptHandle subObj = *reinterpret_cast<ScriptHandle*>(character + 52); /*0x43ca74*/
    CopyNameZ(reinterpret_cast<char*>(subObj), *namePtr);               /*0x43ca7c*/
    // Build the spawn orientation vec {0, yaw, 0} and place the sub-object.
    float orient[4] = {0,0,0,0};                                         // v21/v22/v23
    float rotated[3] = {0,0,0};                                          // v20
    HostRotateByHierarchy(dummy, kRefAxis, rotated);                     /*0x43caa8*/
    orient[1] = HostAngleBetween(kRefAxis, rotated);                     /*0x43cabb (v22)*/
    HostSetWorldTranslation(*reinterpret_cast<ScriptHandle*>(character + 52), orient); /*0x43cac6*/
    HostSetPosition(*reinterpret_cast<ScriptHandle*>(character + 52), worldPos);       /*0x43cad2*/
    return character;                                                    /*0x43cad9*/
}

} // namespace guild::sim
